/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_IPC_H_
#define _FUSE_IPC_H_

#include <mach/mach_types.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/kernel_types.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <kern/assert.h>
#include <libkern/libkern.h>
#include <libkern/OSMalloc.h>
#include <libkern/locks.h>

#include "fuse.h"

struct fuse_iov {
    void   *base;
    size_t  len;
    size_t  allocated_size;
    int     credit;
};

void fiov_init(struct fuse_iov *fiov, size_t size);
void fiov_teardown(struct fuse_iov *fiov);
void fiov_refresh(struct fuse_iov *fiov);
void fiov_adjust(struct fuse_iov *fiov, size_t size);

#define FUSE_DIMALLOC(fiov, spc1, spc2, amnt)          \
do {                                                   \
    fiov_adjust(fiov, (sizeof(*(spc1)) + (amnt)));     \
    (spc1) = (fiov)->base;                             \
    (spc2) = (char *)(fiov)->base + (sizeof(*(spc1))); \
} while (0)

#define FU_AT_LEAST(siz) max((siz), 128)

struct fuse_ticket;
struct fuse_data;

typedef int fuse_handler_t(struct fuse_ticket *tick, uio_t uio);

struct fuse_ticket {
    uint64_t                     tk_unique;
    struct fuse_data            *tk_data;
    int                          tk_flag;
    unsigned int                 tk_age;

    STAILQ_ENTRY(fuse_ticket)    tk_freetickets_link;
    TAILQ_ENTRY(fuse_ticket)     tk_alltickets_link;

    struct fuse_iov              tk_ms_fiov;
    void                        *tk_ms_bufdata;
    unsigned long                tk_ms_bufsize;
    enum { FT_M_FIOV, FT_M_BUF } tk_ms_type;
    STAILQ_ENTRY(fuse_ticket)    tk_ms_link;

    struct fuse_iov              tk_aw_fiov;
    void                        *tk_aw_bufdata;
    unsigned long                tk_aw_bufsize;
    enum { FT_A_FIOV, FT_A_BUF } tk_aw_type;

    struct fuse_out_header       tk_aw_ohead;
    int                          tk_aw_errno;
    lck_mtx_t                   *tk_aw_mtx;
    fuse_handler_t              *tk_aw_handler;
    TAILQ_ENTRY(fuse_ticket)     tk_aw_link;
};

#define FT_ANSW  0x01  // request of ticket has already been answered
#define FT_INVAL 0x02  // ticket is invalidated
#define FT_DIRTY 0x04  // ticket has been used

static __inline__ struct fuse_iov *
fticket_resp(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    return (&tick->tk_aw_fiov);
}

static __inline__ int
fticket_answered(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    return (tick->tk_flag & FT_ANSW);
}

static __inline__ void
fticket_set_answered(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    tick->tk_flag |= FT_ANSW;
}

static __inline__ enum fuse_opcode
fticket_opcode(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    return (((struct fuse_in_header *)(tick->tk_ms_fiov.base))->opcode);
}

static __inline__ void
fticket_invalidate(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    tick->tk_flag |= FT_INVAL;
}

int fticket_pull(struct fuse_ticket *tick, uio_t uio);

enum mountpri { FM_NOMOUNTED, FM_PRIMARY, FM_SECONDARY };

/*
 * The data representing a FUSE session.
 */
struct fuse_data {
    enum mountpri              mpri;
    int                        mntco;
    struct fuse_softc         *fdev;
    mount_t                    mp;
    kauth_cred_t               daemoncred;
    int                        dataflag;

    lck_mtx_t                 *ms_mtx;
    STAILQ_HEAD(, fuse_ticket) ms_head;

    lck_mtx_t                 *aw_mtx;
    TAILQ_HEAD(, fuse_ticket)  aw_head;

    lck_mtx_t                 *ticket_mtx;
    STAILQ_HEAD(, fuse_ticket) freetickets_head;
    TAILQ_HEAD(, fuse_ticket)  alltickets_head;
    unsigned                   freeticket_counter;
    uint64_t                   ticketer;

#ifdef FUSE_EXPLICIT_RENAME_LOCK
    lck_rw_t                  *rename_lock;
#endif

    uint32_t                   fuse_libabi_major;
    uint32_t                   fuse_libabi_minor;

    uint32_t                   max_write;
    uint32_t                   max_read;
    uint32_t                   subtype;
    char                       volname[MAXPATHLEN];
};

#define FSESS_KICK                0x0001 // session is to be closed
#define FSESS_OPENED              0x0002 // session device has been opened
#define FSESS_NOFSYNC             0x0004 // daemon doesn't implement fsync
#define FSESS_NOFSYNCDIR          0x0008 // daemon doesn't implement fsyncdir
#define FSESS_NOACCESS            0x0010 // daemon doesn't implement access
#define FSESS_NOCREATE            0x0020 // daemon doesn't implement create
#define FSESS_INITED              0x0040 // session has been inited
#define FSESS_DAEMON_CAN_SPY      0x0080 // let non-owners access this fs
                                         // (and being observed by the daemon)
#define FSESS_NEGLECT_SHARES      0x0100 // presence of secondary mount is not
                                         // considered as "fs is busy"
#define FSESS_PRIVATE             0x0200 // don't allow secondary mounts
#define FSESS_PUSH_SYMLINKS_IN    0x0400 // prefix absolute symlinks with mp
#define FSESS_DEFAULT_PERMISSIONS 0x0800 // kernel does permission checking

#define FSESS_NO_ATTRCACHE        0x1000 // no attribute caching
#define FSESS_NO_READAHEAD        0x2000 // no readaheads
#define FSESS_NO_UBC              0x4000 // no caching

static __inline__ struct fuse_data *
fusefs_get_data(mount_t mp)
{
    struct fuse_data *data = vfs_fsprivate(mp);
    kdebug_printf("-> mp=%p\n", mp);
    return (data->mpri == FM_PRIMARY ? data : NULL);
}

static __inline__ void
fuse_ms_push(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    STAILQ_INSERT_TAIL(&tick->tk_data->ms_head, tick, tk_ms_link);
}

static __inline__ struct fuse_ticket *
fuse_ms_pop(struct fuse_data *data)
{
    struct fuse_ticket *tick = NULL;

    kdebug_printf("-> data=%p\n", data);

    if ((tick = STAILQ_FIRST(&data->ms_head))) {
        STAILQ_REMOVE_HEAD(&data->ms_head, tk_ms_link);
    }

    return (tick);
}

static __inline__ void
fuse_aw_push(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    TAILQ_INSERT_TAIL(&tick->tk_data->aw_head, tick, tk_aw_link);
}

static __inline__ void
fuse_aw_remove(struct fuse_ticket *tick)
{
    kdebug_printf("-> tick=%p\n", tick);
    TAILQ_REMOVE(&tick->tk_data->aw_head, tick, tk_aw_link);
}

static __inline__ struct fuse_ticket *
fuse_aw_pop(struct fuse_data *data)
{
    struct fuse_ticket *tick = NULL;

    kdebug_printf("-> data=%p\n", data);

    if ((tick = TAILQ_FIRST(&data->aw_head))) {
        fuse_aw_remove(tick);
    }

    return (tick);
}

struct fuse_ticket *fuse_ticket_fetch(struct fuse_data *data);
void fuse_ticket_drop(struct fuse_ticket *tick);
void fuse_ticket_drop_invalid(struct fuse_ticket *tick);
void fuse_insert_callback(struct fuse_ticket *tick, fuse_handler_t *handler);
void fuse_insert_message(struct fuse_ticket *tick);

static __inline__ int
fuse_libabi_geq(struct fuse_data *data, uint32_t maj, uint32_t min)
{
    return (data->fuse_libabi_major > maj ||
            (data->fuse_libabi_major == maj && data->fuse_libabi_minor >= min));
}

struct fuse_secondary_data {
    enum mountpri     mpri;
    mount_t           mp;
    struct fuse_data *master;

    LIST_ENTRY(fuse_secondary_data) slaves_link;
};

static __inline__ struct fuse_secondary_data *
fusefs_get_secondarydata(mount_t mp)
{
    struct fuse_secondary_data *fsdat = vfs_fsprivate(mp);
    return (fsdat->mpri == FM_SECONDARY ? fsdat : NULL);
}

struct fuse_data *fdata_alloc(struct fuse_softc *fdev, struct proc *p);
void fdata_destroy(struct fuse_data *data);
int  fdata_kick_get(struct fuse_data *data);
void fdata_kick_set(struct fuse_data *data);

struct fuse_dispatcher {

    struct fuse_ticket    *tick;
    struct fuse_in_header *finh;

    void    *indata;
    size_t   iosize;
    uint64_t nodeid;
    int      answ_stat;
    void    *answ;
};

static __inline__ void
fdisp_init(struct fuse_dispatcher *fdisp, size_t iosize)
{
    kdebug_printf("-> fdisp=%p, iosize=%x\n", fdisp, iosize);
    fdisp->iosize = iosize;
    fdisp->tick = NULL;
}

void fdisp_make(struct fuse_dispatcher *fdip, enum fuse_opcode op,
                mount_t mp, uint64_t nid, vfs_context_t context);

void fdisp_make_vp(struct fuse_dispatcher *fdip, enum fuse_opcode op,
                   vnode_t vp, vfs_context_t context);

int  fdisp_wait_answ(struct fuse_dispatcher *fdip);

static __inline__ int
fdisp_simple_putget_vp(struct fuse_dispatcher *fdip, enum fuse_opcode op,
                       vnode_t vp, vfs_context_t context)
{
    kdebug_printf("-> fdip=%p, opcode=%d, vp=%p, context=%p\n", fdip, op, vp, context);
    fdisp_init(fdip, 0);
    fdisp_make_vp(fdip, op, vp, context);
    return (fdisp_wait_answ(fdip));
}

static __inline__ int
fdisp_simple_vfs_getattr(struct fuse_dispatcher *fdip,
                         mount_t                 mp,
                         vfs_context_t           context)
{
   fdisp_init(fdip, 0);
   fdisp_make(fdip, FUSE_STATFS, mp, FUSE_ROOT_ID, context);
   return (fdisp_wait_answ(fdip));
}

#endif /* _FUSE_IPC_H_ */
