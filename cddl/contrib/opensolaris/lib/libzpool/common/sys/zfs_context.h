/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */
/*
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 */

#ifndef _SYS_ZFS_CONTEXT_H
#define	_SYS_ZFS_CONTEXT_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	_SYS_MUTEX_H
#define	_SYS_RWLOCK_H
#define	_SYS_CONDVAR_H
#define	_SYS_SYSTM_H
#define	_SYS_T_LOCK_H
#define	_SYS_VNODE_H
#define	_SYS_VFS_H
#define	_SYS_SUNDDI_H
#define	_SYS_CALLB_H
#define	_SYS_SCHED_H_

#include <solaris.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <thread.h>
#include <assert.h>
#include <limits.h>
#include <dirent.h>
#include <time.h>
#include <math.h>
#include <umem.h>
#include <inttypes.h>
#include <fsshare.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <sys/debug.h>
#include <sys/note.h>
#include <sys/types.h>
#include <sys/cred.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/bitmap.h>
#include <sys/resource.h>
#include <sys/byteorder.h>
#include <sys/list.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/zfs_debug.h>
#include <sys/sdt.h>
#include <sys/kstat.h>
#include <sys/u8_textprep.h>
#include <sys/kernel.h>
#include <sys/disk.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dev.h>
#include <machine/atomic.h>
#include <sys/debug.h>
#ifdef illumos
#include "zfs.h"
#endif

#define	ZFS_EXPORTS_PATH	"/etc/zfs/exports"

/*
 * Debugging
 */

/*
 * Note that we are not using the debugging levels.
 */

#define	CE_CONT		0	/* continuation		*/
#define	CE_NOTE		1	/* notice		*/
#define	CE_WARN		2	/* warning		*/
#define	CE_PANIC	3	/* panic		*/
#define	CE_IGNORE	4	/* print nothing	*/

/*
 * ZFS debugging
 */

#define	ZFS_LOG(...)	do {  } while (0)

typedef u_longlong_t      rlim64_t;
#define	RLIM64_INFINITY	((rlim64_t)-3)

#ifdef ZFS_DEBUG
extern void dprintf_setup(int *argc, char **argv);
#endif /* ZFS_DEBUG */

extern void cmn_err(int, const char *, ...);
extern void vcmn_err(int, const char *, __va_list);
extern void panic(const char *, ...)  __NORETURN;
extern void vpanic(const char *, __va_list)  __NORETURN;

#define	fm_panic	panic

extern int aok;

/*
 * DTrace SDT probes have different signatures in userland than they do in
 * the kernel.  If they're being used in kernel code, re-define them out of
 * existence for their counterparts in libzpool.
 *
 * Here's an example of how to use the set-error probes in userland:
 * zfs$target:::set-error /arg0 == EBUSY/ {stack();}
 *
 * Here's an example of how to use DTRACE_PROBE probes in userland:
 * If there is a probe declared as follows:
 * DTRACE_PROBE2(zfs__probe_name, uint64_t, blkid, dnode_t *, dn);
 * Then you can use it as follows:
 * zfs$target:::probe2 /copyinstr(arg0) == "zfs__probe_name"/
 *     {printf("%u %p\n", arg1, arg2);}
 */

#ifdef DTRACE_PROBE
#undef	DTRACE_PROBE
#endif	/* DTRACE_PROBE */
#ifdef illumos
#define	DTRACE_PROBE(a) \
	ZFS_PROBE0(#a)
#endif

#ifdef DTRACE_PROBE1
#undef	DTRACE_PROBE1
#endif	/* DTRACE_PROBE1 */
#ifdef illumos
#define	DTRACE_PROBE1(a, b, c) \
	ZFS_PROBE1(#a, (unsigned long)c)
#endif

#ifdef DTRACE_PROBE2
#undef	DTRACE_PROBE2
#endif	/* DTRACE_PROBE2 */
#ifdef illumos
#define	DTRACE_PROBE2(a, b, c, d, e) \
	ZFS_PROBE2(#a, (unsigned long)c, (unsigned long)e)
#endif

#ifdef DTRACE_PROBE3
#undef	DTRACE_PROBE3
#endif	/* DTRACE_PROBE3 */
#ifdef illumos
#define	DTRACE_PROBE3(a, b, c, d, e, f, g) \
	ZFS_PROBE3(#a, (unsigned long)c, (unsigned long)e, (unsigned long)g)
#endif

#ifdef DTRACE_PROBE4
#undef	DTRACE_PROBE4
#endif	/* DTRACE_PROBE4 */
#ifdef illumos
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i) \
	ZFS_PROBE4(#a, (unsigned long)c, (unsigned long)e, (unsigned long)g, \
	(unsigned long)i)
#endif

#ifdef illumos
/*
 * We use the comma operator so that this macro can be used without much
 * additional code.  For example, "return (EINVAL);" becomes
 * "return (SET_ERROR(EINVAL));".  Note that the argument will be evaluated
 * twice, so it should not have side effects (e.g. something like:
 * "return (SET_ERROR(log_error(EINVAL, info)));" would log the error twice).
 */
#define	SET_ERROR(err)	(ZFS_SET_ERROR(err), err)
#else	/* !illumos */

#define	DTRACE_PROBE(a)	((void)0)
#define	DTRACE_PROBE1(a, b, c)	((void)0)
#define	DTRACE_PROBE2(a, b, c, d, e)	((void)0)
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)	((void)0)
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)	((void)0)

#define SET_ERROR(err) (err)
#endif	/* !illumos */

/*
 * Threads
 */
#define	curthread	((void *)(uintptr_t)thr_self())

#define	kpreempt(x)	sched_yield()

typedef struct kthread kthread_t;

#define	thread_create(stk, stksize, func, arg, len, pp, state, pri)	\
	zk_thread_create(func, arg)
#define	thread_exit() thr_exit(NULL)
#define	thread_join(t)	panic("libzpool cannot join threads")

#define	newproc(f, a, cid, pri, ctp, pid)	(ENOSYS)

/* in libzpool, p0 exists only to have its address taken */
struct proc {
	uintptr_t	this_is_never_used_dont_dereference_it;
};

extern struct proc p0;
#define	curproc		(&p0)

#define	PS_NONE		-1

extern kthread_t *zk_thread_create(void (*func)(void*), void *arg);

#define	issig(why)	(FALSE)
#define	ISSIG(thr, why)	(FALSE)

/*
 * Mutexes
 */
typedef struct kmutex {
	void		*m_owner;
	boolean_t	initialized;
	mutex_t		m_lock;
} kmutex_t;

#define	MUTEX_DEFAULT	USYNC_THREAD
#undef	MUTEX_HELD
#undef	MUTEX_NOT_HELD
#define	MUTEX_HELD(m)	((m)->m_owner == curthread)
#define	MUTEX_NOT_HELD(m) (!MUTEX_HELD(m))
#define	_mutex_held(m)	pthread_mutex_isowned_np(m)

/*
 * Argh -- we have to get cheesy here because the kernel and userland
 * have different signatures for the same routine.
 */
//extern int _mutex_init(mutex_t *mp, int type, void *arg);
//extern int _mutex_destroy(mutex_t *mp);
//extern int _mutex_owned(mutex_t *mp);

#define	mutex_init(mp, b, c, d)		zmutex_init((kmutex_t *)(mp))
#define	mutex_destroy(mp)		zmutex_destroy((kmutex_t *)(mp))
#define	mutex_owned(mp)			zmutex_owned((kmutex_t *)(mp))

extern void zmutex_init(kmutex_t *mp);
extern void zmutex_destroy(kmutex_t *mp);
extern int zmutex_owned(kmutex_t *mp);
extern void mutex_enter(kmutex_t *mp);
extern void mutex_exit(kmutex_t *mp);
extern int mutex_tryenter(kmutex_t *mp);
extern void *mutex_owner(kmutex_t *mp);

/*
 * RW locks
 */
typedef struct krwlock {
	int		rw_count;
	void		*rw_owner;
	boolean_t	initialized;
	rwlock_t	rw_lock;
} krwlock_t;

typedef int krw_t;

#define	RW_READER	0
#define	RW_WRITER	1
#define	RW_DEFAULT	USYNC_THREAD

#undef RW_READ_HELD
#define RW_READ_HELD(x)		((x)->rw_owner == NULL && (x)->rw_count > 0)

#undef RW_WRITE_HELD
#define	RW_WRITE_HELD(x)	((x)->rw_owner == curthread)
#define	RW_LOCK_HELD(x)		rw_lock_held(x)

#undef RW_LOCK_HELD
#define	RW_LOCK_HELD(x)		(RW_READ_HELD(x) || RW_WRITE_HELD(x))

extern void rw_init(krwlock_t *rwlp, char *name, int type, void *arg);
extern void rw_destroy(krwlock_t *rwlp);
extern void rw_enter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryenter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryupgrade(krwlock_t *rwlp);
extern void rw_exit(krwlock_t *rwlp);
extern int rw_lock_held(krwlock_t *rwlp);
#define	rw_downgrade(rwlp) do { } while (0)

extern uid_t crgetuid(cred_t *cr);
extern uid_t crgetruid(cred_t *cr);
extern gid_t crgetgid(cred_t *cr);
extern int crgetngroups(cred_t *cr);
extern gid_t *crgetgroups(cred_t *cr);

/*
 * Condition variables
 */
typedef cond_t kcondvar_t;

#define	CV_DEFAULT	USYNC_THREAD
#define	CALLOUT_FLAG_ABSOLUTE	0x2

extern void cv_init(kcondvar_t *cv, char *name, int type, void *arg);
extern void cv_destroy(kcondvar_t *cv);
extern void cv_wait(kcondvar_t *cv, kmutex_t *mp);
extern clock_t cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime);
extern clock_t cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim,
    hrtime_t res, int flag);
extern void cv_signal(kcondvar_t *cv);
extern void cv_broadcast(kcondvar_t *cv);

/*
 * Thread-specific data
 */
#define	tsd_get(k) pthread_getspecific(k)
#define	tsd_set(k, v) pthread_setspecific(k, v)
#define	tsd_create(kp, d) pthread_key_create(kp, d)
#define	tsd_destroy(kp) /* nothing */

/*
 * Kernel memory
 */
#define	KM_SLEEP		UMEM_NOFAIL
#define	KM_PUSHPAGE		KM_SLEEP
#define	KM_NOSLEEP		UMEM_DEFAULT
#define	KM_NORMALPRI		0	/* not needed with UMEM_DEFAULT */
#define	KMC_NODEBUG		UMC_NODEBUG
#define	KMC_NOTOUCH		0	/* not needed for userland caches */
#define	KM_NODEBUG		0
#define	kmem_alloc(_s, _f)	umem_alloc(_s, _f)
#define	kmem_zalloc(_s, _f)	umem_zalloc(_s, _f)
#define	kmem_free(_b, _s)	umem_free(_b, _s)
#define	kmem_size()		(physmem * PAGESIZE)
#define	kmem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i) \
	umem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i)
#define	kmem_cache_destroy(_c)	umem_cache_destroy(_c)
#define	kmem_cache_alloc(_c, _f) umem_cache_alloc(_c, _f)
#define	kmem_cache_free(_c, _b)	umem_cache_free(_c, _b)
#define	kmem_debugging()	0
#define	kmem_cache_reap_active()	(B_FALSE)
#define	kmem_cache_reap_soon(_c)	/* nothing */
#define	kmem_cache_set_move(_c, _cb)	/* nothing */
#define	POINTER_INVALIDATE(_pp)		/* nothing */
#define	POINTER_IS_VALID(_p)	0

typedef umem_cache_t kmem_cache_t;

typedef enum kmem_cbrc {
	KMEM_CBRC_YES,
	KMEM_CBRC_NO,
	KMEM_CBRC_LATER,
	KMEM_CBRC_DONT_NEED,
	KMEM_CBRC_DONT_KNOW
} kmem_cbrc_t;

/*
 * Task queues
 */
typedef struct taskq taskq_t;
typedef uintptr_t taskqid_t;
typedef void (task_func_t)(void *);

typedef struct taskq_ent {
	struct taskq_ent	*tqent_next;
	struct taskq_ent	*tqent_prev;
	task_func_t		*tqent_func;
	void			*tqent_arg;
	uintptr_t		tqent_flags;
} taskq_ent_t;

#define	TQENT_FLAG_PREALLOC	0x1	/* taskq_dispatch_ent used */

#define	TASKQ_PREPOPULATE	0x0001
#define	TASKQ_CPR_SAFE		0x0002	/* Use CPR safe protocol */
#define	TASKQ_DYNAMIC		0x0004	/* Use dynamic thread scheduling */
#define	TASKQ_THREADS_CPU_PCT	0x0008	/* Scale # threads by # cpus */
#define	TASKQ_DC_BATCH		0x0010	/* Mark threads as batch */

#define	TQ_SLEEP	KM_SLEEP	/* Can block for memory */
#define	TQ_NOSLEEP	KM_NOSLEEP	/* cannot block for memory; may fail */
#define	TQ_NOQUEUE	0x02		/* Do not enqueue if can't dispatch */
#define	TQ_FRONT	0x08		/* Queue in front */

#define TASKQID_INVALID         ((taskqid_t)0)

extern taskq_t *system_taskq;

extern taskq_t	*taskq_create(const char *, int, pri_t, int, int, uint_t);
#define	taskq_create_proc(a, b, c, d, e, p, f) \
	    (taskq_create(a, b, c, d, e, f))
#define	taskq_create_sysdc(a, b, d, e, p, dc, f) \
	    (taskq_create(a, b, maxclsyspri, d, e, f))
extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern void	taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t,
    taskq_ent_t *);
extern void	taskq_destroy(taskq_t *);
extern void	taskq_wait(taskq_t *);
extern void	taskq_wait_id(taskq_t *, taskqid_t);
extern int	taskq_member(taskq_t *, void *);
extern void	system_taskq_init(void);
extern void	system_taskq_fini(void);

#define	taskq_dispatch_safe(tq, func, arg, flags, task)			\
	taskq_dispatch((tq), (func), (arg), (flags))

#define	XVA_MAPSIZE	3
#define	XVA_MAGIC	0x78766174

/*
 * vnodes
 */
typedef struct vnode {
	uint64_t	v_size;
	int		v_fd;
	char		*v_path;
	int		v_dump_fd;
} vnode_t;

extern char *vn_dumpdir;
#define	AV_SCANSTAMP_SZ	32		/* length of anti-virus scanstamp */

typedef struct xoptattr {
	timestruc_t	xoa_createtime;	/* Create time of file */
	uint8_t		xoa_archive;
	uint8_t		xoa_system;
	uint8_t		xoa_readonly;
	uint8_t		xoa_hidden;
	uint8_t		xoa_nounlink;
	uint8_t		xoa_immutable;
	uint8_t		xoa_appendonly;
	uint8_t		xoa_nodump;
	uint8_t		xoa_settable;
	uint8_t		xoa_opaque;
	uint8_t		xoa_av_quarantined;
	uint8_t		xoa_av_modified;
	uint8_t		xoa_av_scanstamp[AV_SCANSTAMP_SZ];
	uint8_t		xoa_reparse;
	uint8_t		xoa_offline;
	uint8_t		xoa_sparse;
} xoptattr_t;

typedef struct vattr {
	uint_t		va_mask;	/* bit-mask of attributes */
	u_offset_t	va_size;	/* file size in bytes */
} vattr_t;


typedef struct xvattr {
	vattr_t		xva_vattr;	/* Embedded vattr structure */
	uint32_t	xva_magic;	/* Magic Number */
	uint32_t	xva_mapsize;	/* Size of attr bitmap (32-bit words) */
	uint32_t	*xva_rtnattrmapp;	/* Ptr to xva_rtnattrmap[] */
	uint32_t	xva_reqattrmap[XVA_MAPSIZE];	/* Requested attrs */
	uint32_t	xva_rtnattrmap[XVA_MAPSIZE];	/* Returned attrs */
	xoptattr_t	xva_xoptattrs;	/* Optional attributes */
} xvattr_t;

typedef struct vsecattr {
	uint_t		vsa_mask;	/* See below */
	int		vsa_aclcnt;	/* ACL entry count */
	void		*vsa_aclentp;	/* pointer to ACL entries */
	int		vsa_dfaclcnt;	/* default ACL entry count */
	void		*vsa_dfaclentp;	/* pointer to default ACL entries */
	size_t		vsa_aclentsz;	/* ACE size in bytes of vsa_aclentp */
} vsecattr_t;

#define	AT_TYPE		0x00001
#define	AT_MODE		0x00002
#define	AT_UID		0x00004
#define	AT_GID		0x00008
#define	AT_FSID		0x00010
#define	AT_NODEID	0x00020
#define	AT_NLINK	0x00040
#define	AT_SIZE		0x00080
#define	AT_ATIME	0x00100
#define	AT_MTIME	0x00200
#define	AT_CTIME	0x00400
#define	AT_RDEV		0x00800
#define	AT_BLKSIZE	0x01000
#define	AT_NBLOCKS	0x02000
#define	AT_SEQ		0x08000
#define	AT_XVATTR	0x10000

#define	CRCREAT		0

extern int fop_getattr(vnode_t *vp, vattr_t *vap);

#define	VOP_CLOSE(vp, f, c, o, cr, ct)	0
#define	VOP_PUTPAGE(vp, of, sz, fl, cr, ct)	0
#define	VOP_GETATTR(vp, vap, cr)  fop_getattr((vp), (vap));

#define	VOP_FSYNC(vp, f, cr, ct)	fsync((vp)->v_fd)

#define	VN_RELE(vp)			vn_close(vp, 0, NULL, NULL)
#define	VN_RELE_ASYNC(vp, taskq)	vn_close(vp, 0, NULL, NULL)

#define	vn_lock(vp, type)
#define	VOP_UNLOCK(vp, type)

extern int vn_open(char *path, int x1, int oflags, int mode, vnode_t **vpp,
    int x2, int x3);
extern int vn_openat(char *path, int x1, int oflags, int mode, vnode_t **vpp,
    int x2, int x3, vnode_t *vp, int fd);
extern int vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len,
    offset_t offset, int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp);
extern void vn_close(vnode_t *vp, int openflag, cred_t *cr, kthread_t *td);

#define	vn_remove(path, x1, x2)		remove(path)
#define	vn_rename(from, to, seg)	rename((from), (to))
#define	vn_is_readonly(vp)		B_FALSE

extern vnode_t *rootdir;

#include <sys/file.h>		/* for FREAD, FWRITE, etc */
#define	FTRUNC	O_TRUNC

/*
 * Random stuff
 */
#define	ddi_get_lbolt()		(gethrtime() >> 23)
#define	ddi_get_lbolt64()	(gethrtime() >> 23)
#define	hz	119	/* frequency when using gethrtime() >> 23 for lbolt */

extern void delay(clock_t ticks);

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	NSEC_TO_TICK(nsec)	((nsec) / (NANOSEC / hz))

#define	gethrestime_sec() time(NULL)
#define	gethrestime(t) \
	do {\
		(t)->tv_sec = gethrestime_sec();\
		(t)->tv_nsec = 0;\
	} while (0);

#define	max_ncpus	64
#define	boot_ncpus	(sysconf(_SC_NPROCESSORS_ONLN))

#define	minclsyspri	60
#define	maxclsyspri	99

#define	CPU_SEQID	(thr_self() & (max_ncpus - 1))

#define	kcred		NULL
#define	CRED()		NULL

#ifndef ptob
#define	ptob(x)		((x) * PAGESIZE)
#endif

extern uint64_t physmem;

extern int highbit64(uint64_t i);
extern int random_get_bytes(uint8_t *ptr, size_t len);
extern int random_get_pseudo_bytes(uint8_t *ptr, size_t len);

extern void kernel_init(int);
extern void kernel_fini(void);

struct spa;
extern void nicenum(uint64_t num, char *buf, size_t);
extern void show_pool_stats(struct spa *);
extern int set_global_var(char *arg);

typedef struct callb_cpr {
	kmutex_t	*cc_lockp;
} callb_cpr_t;

#define	CALLB_CPR_INIT(cp, lockp, func, name)	{		\
	(cp)->cc_lockp = lockp;					\
}

#define	CALLB_CPR_SAFE_BEGIN(cp) {				\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
}

#define	CALLB_CPR_SAFE_END(cp, lockp) {				\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
}

#define	CALLB_CPR_EXIT(cp) {					\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
	mutex_exit((cp)->cc_lockp);				\
}

#define	zone_dataset_visible(x, y)	(1)
#define	INGLOBALZONE(z)			(1)
extern uint32_t zone_get_hostid(void *zonep);

extern char *kmem_asprintf(const char *fmt, ...);
#define	strfree(str) kmem_free((str), strlen(str) + 1)

/*
 * Hostname information
 */
extern struct utsname utsname;
extern char hw_serial[];	/* for userland-emulated hostid access */
extern int ddi_strtoul(const char *str, char **nptr, int base,
    unsigned long *result);

extern int ddi_strtoull(const char *str, char **nptr, int base,
    u_longlong_t *result);

/* ZFS Boot Related stuff. */

struct _buf {
	intptr_t	_fd;
};

struct bootstat {
	uint64_t st_size;
};

typedef struct ace_object {
	uid_t		a_who;
	uint32_t	a_access_mask;
	uint16_t	a_flags;
	uint16_t	a_type;
	uint8_t		a_obj_type[16];
	uint8_t		a_inherit_obj_type[16];
} ace_object_t;


#define	ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE	0x05
#define	ACE_ACCESS_DENIED_OBJECT_ACE_TYPE	0x06
#define	ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE	0x07
#define	ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE	0x08

extern struct _buf *kobj_open_file(char *name);
extern int kobj_read_file(struct _buf *file, char *buf, unsigned size,
    unsigned off);
extern void kobj_close_file(struct _buf *file);
extern int kobj_get_filesize(struct _buf *file, uint64_t *size);
extern int zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr);
extern int zfs_secpolicy_rename_perms(const char *from, const char *to,
    cred_t *cr);
extern int zfs_secpolicy_destroy_perms(const char *name, cred_t *cr);
extern zoneid_t getzoneid(void);
/* Random compatibility stuff. */
#define	pwrite64(d, p, n, o)	pwrite(d, p, n, o)
#define	readdir64(d)		readdir(d)
#define	SIGPENDING(td)		(0)
#define	root_mount_wait()	do { } while (0)
#define	root_mounted()		(1)

#define	noinline	__attribute__((noinline))
#define	likely(x)	__builtin_expect((x), 1)

struct file {
	void *dummy;
};

#define	FCREAT	O_CREAT
#define	FOFFMAX	0x0

/* SID stuff */
typedef struct ksiddomain {
	uint_t	kd_ref;
	uint_t	kd_len;
	char	*kd_name;
} ksiddomain_t;

ksiddomain_t *ksid_lookupdomain(const char *);
void ksiddomain_rele(ksiddomain_t *);

typedef	uint32_t	idmap_rid_t;

#define	DDI_SLEEP	KM_SLEEP
#define	ddi_log_sysevent(_a, _b, _c, _d, _e, _f, _g)	(0)

#define	SX_SYSINIT(name, lock, desc)

#define SYSCTL_HANDLER_ARGS struct sysctl_oid *oidp, void *arg1,	\
	intptr_t arg2, struct sysctl_req *req

/*
 * This describes the access space for a sysctl request.  This is needed
 * so that we can use the interface from the kernel or from user-space.
 */
struct sysctl_req {
	struct thread	*td;		/* used for access checking */
	int		lock;		/* wiring state */
	void		*oldptr;
	size_t		oldlen;
	size_t		oldidx;
	int		(*oldfunc)(struct sysctl_req *, const void *, size_t);
	void		*newptr;
	size_t		newlen;
	size_t		newidx;
	int		(*newfunc)(struct sysctl_req *, void *, size_t);
	size_t		validlen;
	int		flags;
};

SLIST_HEAD(sysctl_oid_list, sysctl_oid);

/*
 * This describes one "oid" in the MIB tree.  Potentially more nodes can
 * be hidden behind it, expanded by the handler.
 */
struct sysctl_oid {
	struct sysctl_oid_list *oid_parent;
	SLIST_ENTRY(sysctl_oid) oid_link;
	int		oid_number;
	u_int		oid_kind;
	void		*oid_arg1;
	intptr_t	oid_arg2;
	const char	*oid_name;
	int 		(*oid_handler)(SYSCTL_HANDLER_ARGS);
	const char	*oid_fmt;
	int		oid_refcnt;
	u_int		oid_running;
	const char	*oid_descr;
};

#define	SYSCTL_DECL(...)
#define	SYSCTL_NODE(...)
#define	SYSCTL_INT(...)
#define	SYSCTL_UINT(...)
#define	SYSCTL_ULONG(...)
#define	SYSCTL_PROC(...)
#define	SYSCTL_QUAD(...)
#define	SYSCTL_UQUAD(...)
#ifdef TUNABLE_INT
#undef TUNABLE_INT
#undef TUNABLE_ULONG
#undef TUNABLE_QUAD
#endif
#define	TUNABLE_INT(...)
#define	TUNABLE_ULONG(...)
#define	TUNABLE_QUAD(...)

int sysctl_handle_64(SYSCTL_HANDLER_ARGS);

/* Errors */

#ifndef	ERESTART
#define	ERESTART	(-1)
#endif

#ifdef illumos
/*
 * Cyclic information
 */
extern kmutex_t cpu_lock;

typedef uintptr_t cyclic_id_t;
typedef uint16_t cyc_level_t;
typedef void (*cyc_func_t)(void *);

#define	CY_LOW_LEVEL	0
#define	CY_INFINITY	INT64_MAX
#define	CYCLIC_NONE	((cyclic_id_t)0)

typedef struct cyc_time {
	hrtime_t cyt_when;
	hrtime_t cyt_interval;
} cyc_time_t;

typedef struct cyc_handler {
	cyc_func_t cyh_func;
	void *cyh_arg;
	cyc_level_t cyh_level;
} cyc_handler_t;

extern cyclic_id_t cyclic_add(cyc_handler_t *, cyc_time_t *);
extern void cyclic_remove(cyclic_id_t);
extern int cyclic_reprogram(cyclic_id_t, hrtime_t);
#endif	/* illumos */

#ifdef illumos
/*
 * Buf structure
 */
#define	B_BUSY		0x0001
#define	B_DONE		0x0002
#define	B_ERROR		0x0004
#define	B_READ		0x0040	/* read when I/O occurs */
#define	B_WRITE		0x0100	/* non-read pseudo-flag */

typedef struct buf {
	int	b_flags;
	size_t b_bcount;
	union {
		caddr_t b_addr;
	} b_un;

	lldaddr_t	_b_blkno;
#define	b_lblkno	_b_blkno._f
	size_t	b_resid;
	size_t	b_bufsize;
	int	(*b_iodone)(struct buf *);
	int	b_error;
	void	*b_private;
} buf_t;

extern void bioinit(buf_t *);
extern void biodone(buf_t *);
extern void bioerror(buf_t *, int);
extern int geterror(buf_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_CONTEXT_H */
