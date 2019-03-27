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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <libgen.h>
#include <sys/assfail.h>
#include <sys/spa.h>
#include <sys/stat.h>
#include <sys/processor.h>
#include <sys/zfs_context.h>
#include <sys/rrwlock.h>
#include <sys/zmod.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

/*
 * Emulation of kernel services in userland.
 */

#ifndef __FreeBSD__
int aok;
#endif
uint64_t physmem;
vnode_t *rootdir = (vnode_t *)0xabcd1234;
char hw_serial[HW_HOSTID_LEN];
#ifdef illumos
kmutex_t cpu_lock;
#endif

/* If set, all blocks read will be copied to the specified directory. */
char *vn_dumpdir = NULL;

struct utsname utsname = {
	"userland", "libzpool", "1", "1", "na"
};

/* this only exists to have its address taken */
struct proc p0;

/*
 * =========================================================================
 * threads
 * =========================================================================
 */
/*ARGSUSED*/
kthread_t *
zk_thread_create(void (*func)(), void *arg)
{
	thread_t tid;

	VERIFY(thr_create(0, 0, (void *(*)(void *))func, arg, THR_DETACHED,
	    &tid) == 0);

	return ((void *)(uintptr_t)tid);
}

/*
 * =========================================================================
 * kstats
 * =========================================================================
 */
/*ARGSUSED*/
kstat_t *
kstat_create(char *module, int instance, char *name, char *class,
    uchar_t type, ulong_t ndata, uchar_t ks_flag)
{
	return (NULL);
}

/*ARGSUSED*/
void
kstat_named_init(kstat_named_t *knp, const char *name, uchar_t type)
{}

/*ARGSUSED*/
void
kstat_install(kstat_t *ksp)
{}

/*ARGSUSED*/
void
kstat_delete(kstat_t *ksp)
{}

/*
 * =========================================================================
 * mutexes
 * =========================================================================
 */
void
zmutex_init(kmutex_t *mp)
{
	mp->m_owner = NULL;
	mp->initialized = B_TRUE;
	(void) _mutex_init(&mp->m_lock, USYNC_THREAD, NULL);
}

void
zmutex_destroy(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mp->m_owner == NULL);
	(void) _mutex_destroy(&(mp)->m_lock);
	mp->m_owner = (void *)-1UL;
	mp->initialized = B_FALSE;
}

int
zmutex_owned(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);

	return (mp->m_owner == curthread);
}

void
mutex_enter(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mp->m_owner != (void *)-1UL);
	ASSERT(mp->m_owner != curthread);
	VERIFY(mutex_lock(&mp->m_lock) == 0);
	ASSERT(mp->m_owner == NULL);
	mp->m_owner = curthread;
}

int
mutex_tryenter(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mp->m_owner != (void *)-1UL);
	if (0 == mutex_trylock(&mp->m_lock)) {
		ASSERT(mp->m_owner == NULL);
		mp->m_owner = curthread;
		return (1);
	} else {
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	VERIFY(mutex_unlock(&mp->m_lock) == 0);
}

void *
mutex_owner(kmutex_t *mp)
{
	ASSERT(mp->initialized == B_TRUE);
	return (mp->m_owner);
}

/*
 * =========================================================================
 * rwlocks
 * =========================================================================
 */
/*ARGSUSED*/
void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	rwlock_init(&rwlp->rw_lock, USYNC_THREAD, NULL);
	rwlp->rw_owner = NULL;
	rwlp->initialized = B_TRUE;
	rwlp->rw_count = 0;
}

void
rw_destroy(krwlock_t *rwlp)
{
	ASSERT(rwlp->rw_count == 0);
	rwlock_destroy(&rwlp->rw_lock);
	rwlp->rw_owner = (void *)-1UL;
	rwlp->initialized = B_FALSE;
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	//ASSERT(!RW_LOCK_HELD(rwlp));
	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);
	ASSERT(rwlp->rw_owner != curthread);

	if (rw == RW_READER) {
		VERIFY(rw_rdlock(&rwlp->rw_lock) == 0);
		ASSERT(rwlp->rw_count >= 0);
		atomic_add_int(&rwlp->rw_count, 1);
	} else {
		VERIFY(rw_wrlock(&rwlp->rw_lock) == 0);
		ASSERT(rwlp->rw_count == 0);
		rwlp->rw_count = -1;
		rwlp->rw_owner = curthread;
	}
}

void
rw_exit(krwlock_t *rwlp)
{
	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	if (rwlp->rw_owner == curthread) {
		/* Write locked. */
		ASSERT(rwlp->rw_count == -1);
		rwlp->rw_count = 0;
		rwlp->rw_owner = NULL;
	} else {
		/* Read locked. */
		ASSERT(rwlp->rw_count > 0);
		atomic_add_int(&rwlp->rw_count, -1);
	}
	VERIFY(rw_unlock(&rwlp->rw_lock) == 0);
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int rv;

	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);
	ASSERT(rwlp->rw_owner != curthread);

	if (rw == RW_READER)
		rv = rw_tryrdlock(&rwlp->rw_lock);
	else
		rv = rw_trywrlock(&rwlp->rw_lock);

	if (rv == 0) {
		ASSERT(rwlp->rw_owner == NULL);
		if (rw == RW_READER) {
			ASSERT(rwlp->rw_count >= 0);
			atomic_add_int(&rwlp->rw_count, 1);
		} else {
			ASSERT(rwlp->rw_count == 0);
			rwlp->rw_count = -1;
			rwlp->rw_owner = curthread;
		}
		return (1);
	}

	return (0);
}

/*ARGSUSED*/
int
rw_tryupgrade(krwlock_t *rwlp)
{
	ASSERT(rwlp->initialized == B_TRUE);
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	return (0);
}

int
rw_lock_held(krwlock_t *rwlp)
{

	return (rwlp->rw_count != 0);
}

/*
 * =========================================================================
 * condition variables
 * =========================================================================
 */
/*ARGSUSED*/
void
cv_init(kcondvar_t *cv, char *name, int type, void *arg)
{
	VERIFY(cond_init(cv, name, NULL) == 0);
}

void
cv_destroy(kcondvar_t *cv)
{
	VERIFY(cond_destroy(cv) == 0);
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	int ret = cond_wait(cv, &mp->m_lock);
	VERIFY(ret == 0 || ret == EINTR);
	mp->m_owner = curthread;
}

clock_t
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	struct timespec ts;
	struct timeval tv;
	clock_t delta;

	abstime += ddi_get_lbolt();
top:
	delta = abstime - ddi_get_lbolt();
	if (delta <= 0)
		return (-1);

	if (gettimeofday(&tv, NULL) != 0)
		assert(!"gettimeofday() failed");

	ts.tv_sec = tv.tv_sec + delta / hz;
	ts.tv_nsec = tv.tv_usec * 1000 + (delta % hz) * (NANOSEC / hz);
	ASSERT(ts.tv_nsec >= 0);

	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == EINTR)
		goto top;

	if (error == ETIMEDOUT)
		return (-1);

	ASSERT(error == 0);

	return (1);
}

/*ARGSUSED*/
clock_t
cv_timedwait_hires(kcondvar_t *cv, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{
	int error;
	timespec_t ts;
	hrtime_t delta;

	ASSERT(flag == 0 || flag == CALLOUT_FLAG_ABSOLUTE);

top:
	delta = tim;
	if (flag & CALLOUT_FLAG_ABSOLUTE)
		delta -= gethrtime();

	if (delta <= 0)
		return (-1);

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += delta / NANOSEC;
	ts.tv_nsec += delta % NANOSEC;
	if (ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == ETIMEDOUT)
		return (-1);

	if (error == EINTR)
		goto top;

	ASSERT(error == 0);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	VERIFY(cond_signal(cv) == 0);
}

void
cv_broadcast(kcondvar_t *cv)
{
	VERIFY(cond_broadcast(cv) == 0);
}

/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */
/*
 * Note: for the xxxat() versions of these functions, we assume that the
 * starting vp is always rootdir (which is true for spa_directory.c, the only
 * ZFS consumer of these interfaces).  We assert this is true, and then emulate
 * them by adding '/' in front of the path.
 */

/*ARGSUSED*/
int
vn_open(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2, int x3)
{
	int fd;
	int dump_fd;
	vnode_t *vp;
	int old_umask;
	char realpath[MAXPATHLEN];
	struct stat64 st;

	/*
	 * If we're accessing a real disk from userland, we need to use
	 * the character interface to avoid caching.  This is particularly
	 * important if we're trying to look at a real in-kernel storage
	 * pool from userland, e.g. via zdb, because otherwise we won't
	 * see the changes occurring under the segmap cache.
	 * On the other hand, the stupid character device returns zero
	 * for its size.  So -- gag -- we open the block device to get
	 * its size, and remember it for subsequent VOP_GETATTR().
	 */
	if (strncmp(path, "/dev/", 5) == 0) {
		char *dsk;
		fd = open64(path, O_RDONLY);
		if (fd == -1)
			return (errno);
		if (fstat64(fd, &st) == -1) {
			close(fd);
			return (errno);
		}
		close(fd);
		(void) sprintf(realpath, "%s", path);
		dsk = strstr(path, "/dsk/");
		if (dsk != NULL)
			(void) sprintf(realpath + (dsk - path) + 1, "r%s",
			    dsk + 1);
	} else {
		(void) sprintf(realpath, "%s", path);
		if (!(flags & FCREAT) && stat64(realpath, &st) == -1)
			return (errno);
	}

	if (flags & FCREAT)
		old_umask = umask(0);

	/*
	 * The construct 'flags - FREAD' conveniently maps combinations of
	 * FREAD and FWRITE to the corresponding O_RDONLY, O_WRONLY, and O_RDWR.
	 */
	fd = open64(realpath, flags - FREAD, mode);

	if (flags & FCREAT)
		(void) umask(old_umask);

	if (vn_dumpdir != NULL) {
		char dumppath[MAXPATHLEN];
		(void) snprintf(dumppath, sizeof (dumppath),
		    "%s/%s", vn_dumpdir, basename(realpath));
		dump_fd = open64(dumppath, O_CREAT | O_WRONLY, 0666);
		if (dump_fd == -1)
			return (errno);
	} else {
		dump_fd = -1;
	}

	if (fd == -1)
		return (errno);

	if (fstat64(fd, &st) == -1) {
		close(fd);
		return (errno);
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	*vpp = vp = umem_zalloc(sizeof (vnode_t), UMEM_NOFAIL);

	vp->v_fd = fd;
	vp->v_size = st.st_size;
	vp->v_path = spa_strdup(path);
	vp->v_dump_fd = dump_fd;

	return (0);
}

/*ARGSUSED*/
int
vn_openat(char *path, int x1, int flags, int mode, vnode_t **vpp, int x2,
    int x3, vnode_t *startvp, int fd)
{
	char *realpath = umem_alloc(strlen(path) + 2, UMEM_NOFAIL);
	int ret;

	ASSERT(startvp == rootdir);
	(void) sprintf(realpath, "/%s", path);

	/* fd ignored for now, need if want to simulate nbmand support */
	ret = vn_open(realpath, x1, flags, mode, vpp, x2, x3);

	umem_free(realpath, strlen(path) + 2);

	return (ret);
}

/*ARGSUSED*/
int
vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len, offset_t offset,
    int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp)
{
	ssize_t iolen, split;

	if (uio == UIO_READ) {
		iolen = pread64(vp->v_fd, addr, len, offset);
		if (vp->v_dump_fd != -1) {
			int status =
			    pwrite64(vp->v_dump_fd, addr, iolen, offset);
			ASSERT(status != -1);
		}
	} else {
		/*
		 * To simulate partial disk writes, we split writes into two
		 * system calls so that the process can be killed in between.
		 */
		int sectors = len >> SPA_MINBLOCKSHIFT;
		split = (sectors > 0 ? rand() % sectors : 0) <<
		    SPA_MINBLOCKSHIFT;
		iolen = pwrite64(vp->v_fd, addr, split, offset);
		iolen += pwrite64(vp->v_fd, (char *)addr + split,
		    len - split, offset + split);
	}

	if (iolen == -1)
		return (errno);
	if (residp)
		*residp = len - iolen;
	else if (iolen != len)
		return (EIO);
	return (0);
}

void
vn_close(vnode_t *vp, int openflag, cred_t *cr, kthread_t *td)
{
	close(vp->v_fd);
	if (vp->v_dump_fd != -1)
		close(vp->v_dump_fd);
	spa_strfree(vp->v_path);
	umem_free(vp, sizeof (vnode_t));
}

/*
 * At a minimum we need to update the size since vdev_reopen()
 * will no longer call vn_openat().
 */
int
fop_getattr(vnode_t *vp, vattr_t *vap)
{
	struct stat64 st;

	if (fstat64(vp->v_fd, &st) == -1) {
		close(vp->v_fd);
		return (errno);
	}

	vap->va_size = st.st_size;
	return (0);
}

#ifdef ZFS_DEBUG

/*
 * =========================================================================
 * Figure out which debugging statements to print
 * =========================================================================
 */

static char *dprintf_string;
static int dprintf_print_all;

int
dprintf_find_string(const char *string)
{
	char *tmp_str = dprintf_string;
	int len = strlen(string);

	/*
	 * Find out if this is a string we want to print.
	 * String format: file1.c,function_name1,file2.c,file3.c
	 */

	while (tmp_str != NULL) {
		if (strncmp(tmp_str, string, len) == 0 &&
		    (tmp_str[len] == ',' || tmp_str[len] == '\0'))
			return (1);
		tmp_str = strchr(tmp_str, ',');
		if (tmp_str != NULL)
			tmp_str++; /* Get rid of , */
	}
	return (0);
}

void
dprintf_setup(int *argc, char **argv)
{
	int i, j;

	/*
	 * Debugging can be specified two ways: by setting the
	 * environment variable ZFS_DEBUG, or by including a
	 * "debug=..."  argument on the command line.  The command
	 * line setting overrides the environment variable.
	 */

	for (i = 1; i < *argc; i++) {
		int len = strlen("debug=");
		/* First look for a command line argument */
		if (strncmp("debug=", argv[i], len) == 0) {
			dprintf_string = argv[i] + len;
			/* Remove from args */
			for (j = i; j < *argc; j++)
				argv[j] = argv[j+1];
			argv[j] = NULL;
			(*argc)--;
		}
	}

	if (dprintf_string == NULL) {
		/* Look for ZFS_DEBUG environment variable */
		dprintf_string = getenv("ZFS_DEBUG");
	}

	/*
	 * Are we just turning on all debugging?
	 */
	if (dprintf_find_string("on"))
		dprintf_print_all = 1;

	if (dprintf_string != NULL)
		zfs_flags |= ZFS_DEBUG_DPRINTF;
}

int
sysctl_handle_64(SYSCTL_HANDLER_ARGS)
{
	return (0);
}

/*
 * =========================================================================
 * debug printfs
 * =========================================================================
 */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	if (dprintf_print_all ||
	    dprintf_find_string(newfile) ||
	    dprintf_find_string(func)) {
		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%d ", getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%lu ", thr_self());
#if 0
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
#endif
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("%s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	}
}

#endif /* ZFS_DEBUG */

/*
 * =========================================================================
 * cmn_err() and panic()
 * =========================================================================
 */
static char ce_prefix[CE_IGNORE][10] = { "", "NOTICE: ", "WARNING: ", "" };
static char ce_suffix[CE_IGNORE][2] = { "", "\n", "\n", "" };

void
vpanic(const char *fmt, va_list adx)
{
	char buf[512];
	(void) vsnprintf(buf, 512, fmt, adx);
	assfail(buf, NULL, 0);
	abort(); /* necessary to make vpanic meet noreturn requirements */
}

void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vpanic(fmt, adx);
	va_end(adx);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	if (ce == CE_PANIC)
		vpanic(fmt, adx);
	if (ce != CE_NOTE) {	/* suppress noise in userland stress testing */
		(void) fprintf(stderr, "%s", ce_prefix[ce]);
		(void) vfprintf(stderr, fmt, adx);
		(void) fprintf(stderr, "%s", ce_suffix[ce]);
	}
}

/*PRINTFLIKE2*/
void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}

/*
 * =========================================================================
 * kobj interfaces
 * =========================================================================
 */
struct _buf *
kobj_open_file(char *name)
{
	struct _buf *file;
	vnode_t *vp;

	/* set vp as the _fd field of the file */
	if (vn_openat(name, UIO_SYSSPACE, FREAD, 0, &vp, 0, 0, rootdir,
	    -1) != 0)
		return ((void *)-1UL);

	file = umem_zalloc(sizeof (struct _buf), UMEM_NOFAIL);
	file->_fd = (intptr_t)vp;
	return (file);
}

int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	ssize_t resid;

	vn_rdwr(UIO_READ, (vnode_t *)file->_fd, buf, size, (offset_t)off,
	    UIO_SYSSPACE, 0, 0, 0, &resid);

	return (size - resid);
}

void
kobj_close_file(struct _buf *file)
{
	vn_close((vnode_t *)file->_fd, 0, NULL, NULL);
	umem_free(file, sizeof (struct _buf));
}

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
	struct stat64 st;
	vnode_t *vp = (vnode_t *)file->_fd;

	if (fstat64(vp->v_fd, &st) == -1) {
		vn_close(vp, 0, NULL, NULL);
		return (errno);
	}
	*size = st.st_size;
	return (0);
}

/*
 * =========================================================================
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
	poll(0, 0, ticks * (1000 / hz));
}

#if 0
/*
 * Find highest one bit set.
 *	Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 */
int
highbit64(uint64_t i)
{
	int h = 1;

	if (i == 0)
		return (0);
	if (i & 0xffffffff00000000ULL) {
		h += 32; i >>= 32;
	}
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}
#endif

static int random_fd = -1, urandom_fd = -1;

static int
random_get_bytes_common(uint8_t *ptr, size_t len, int fd)
{
	size_t resid = len;
	ssize_t bytes;

	ASSERT(fd != -1);

	while (resid != 0) {
		bytes = read(fd, ptr, resid);
		ASSERT3S(bytes, >=, 0);
		ptr += bytes;
		resid -= bytes;
	}

	return (0);
}

int
random_get_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, random_fd));
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, urandom_fd));
}

int
ddi_strtoul(const char *hw_serial, char **nptr, int base, unsigned long *result)
{
	char *end;

	*result = strtoul(hw_serial, &end, base);
	if (*result == 0)
		return (errno);
	return (0);
}

int
ddi_strtoull(const char *str, char **nptr, int base, u_longlong_t *result)
{
	char *end;

	*result = strtoull(str, &end, base);
	if (*result == 0)
		return (errno);
	return (0);
}

#ifdef illumos
/* ARGSUSED */
cyclic_id_t
cyclic_add(cyc_handler_t *hdlr, cyc_time_t *when)
{
	return (1);
}

/* ARGSUSED */
void
cyclic_remove(cyclic_id_t id)
{
}

/* ARGSUSED */
int
cyclic_reprogram(cyclic_id_t id, hrtime_t expiration)
{
	return (1);
}
#endif

/*
 * =========================================================================
 * kernel emulation setup & teardown
 * =========================================================================
 */
static int
umem_out_of_memory(void)
{
	char errmsg[] = "out of memory -- generating core dump\n";

	write(fileno(stderr), errmsg, sizeof (errmsg));
	abort();
	return (0);
}

void
kernel_init(int mode)
{
	extern uint_t rrw_tsd_key;

	umem_nofail_callback(umem_out_of_memory);

	physmem = sysconf(_SC_PHYS_PAGES);

	dprintf("physmem = %llu pages (%.2f GB)\n", physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	(void) snprintf(hw_serial, sizeof (hw_serial), "%lu",
	    (mode & FWRITE) ? (unsigned long)gethostid() : 0);

	VERIFY((random_fd = open("/dev/random", O_RDONLY)) != -1);
	VERIFY((urandom_fd = open("/dev/urandom", O_RDONLY)) != -1);

	system_taskq_init();

#ifdef illumos
	mutex_init(&cpu_lock, NULL, MUTEX_DEFAULT, NULL);
#endif

	spa_init(mode);

	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
}

void
kernel_fini(void)
{
	spa_fini();

	system_taskq_fini();

	close(random_fd);
	close(urandom_fd);

	random_fd = -1;
	urandom_fd = -1;
}

/* ARGSUSED */
uint32_t
zone_get_hostid(void *zonep)
{
	/*
	 * We're emulating the system's hostid in userland.
	 */
	return (strtoul(hw_serial, NULL, 10));
}

int
z_uncompress(void *dst, size_t *dstlen, const void *src, size_t srclen)
{
	int ret;
	uLongf len = *dstlen;

	if ((ret = uncompress(dst, &len, src, srclen)) == Z_OK)
		*dstlen = (size_t)len;

	return (ret);
}

int
z_compress_level(void *dst, size_t *dstlen, const void *src, size_t srclen,
    int level)
{
	int ret;
	uLongf len = *dstlen;

	if ((ret = compress2(dst, &len, src, srclen, level)) == Z_OK)
		*dstlen = (size_t)len;

	return (ret);
}

uid_t
crgetuid(cred_t *cr)
{
	return (0);
}

uid_t
crgetruid(cred_t *cr)
{
	return (0);
}

gid_t
crgetgid(cred_t *cr)
{
	return (0);
}

int
crgetngroups(cred_t *cr)
{
	return (0);
}

gid_t *
crgetgroups(cred_t *cr)
{
	return (NULL);
}

int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	return (0);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	return (0);
}

ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
	ksiddomain_t *kd;

	kd = umem_zalloc(sizeof (ksiddomain_t), UMEM_NOFAIL);
	kd->kd_name = spa_strdup(dom);
	return (kd);
}

void
ksiddomain_rele(ksiddomain_t *ksid)
{
	spa_strfree(ksid->kd_name);
	umem_free(ksid, sizeof (ksiddomain_t));
}

/*
 * Do not change the length of the returned string; it must be freed
 * with strfree().
 */
char *
kmem_asprintf(const char *fmt, ...)
{
	int size;
	va_list adx;
	char *buf;

	va_start(adx, fmt);
	size = vsnprintf(NULL, 0, fmt, adx) + 1;
	va_end(adx);

	buf = kmem_alloc(size, KM_SLEEP);

	va_start(adx, fmt);
	size = vsnprintf(buf, size, fmt, adx);
	va_end(adx);

	return (buf);
}

/* ARGSUSED */
int
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	*minorp = 0;
	return (0);
}

/* ARGSUSED */
void
zfs_onexit_fd_rele(int fd)
{
}

/* ARGSUSED */
int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uint64_t *action_handle)
{
	return (0);
}

/* ARGSUSED */
int
zfs_onexit_del_cb(minor_t minor, uint64_t action_handle, boolean_t fire)
{
	return (0);
}

/* ARGSUSED */
int
zfs_onexit_cb_data(minor_t minor, uint64_t action_handle, void **data)
{
	return (0);
}

#ifdef __FreeBSD__
/* ARGSUSED */
int
zvol_create_minors(const char *name)
{
	return (0);
}
#endif

#ifdef illumos
void
bioinit(buf_t *bp)
{
	bzero(bp, sizeof (buf_t));
}

void
biodone(buf_t *bp)
{
	if (bp->b_iodone != NULL) {
		(*(bp->b_iodone))(bp);
		return;
	}
	ASSERT((bp->b_flags & B_DONE) == 0);
	bp->b_flags |= B_DONE;
}

void
bioerror(buf_t *bp, int error)
{
	ASSERT(bp != NULL);
	ASSERT(error >= 0);

	if (error != 0) {
		bp->b_flags |= B_ERROR;
	} else {
		bp->b_flags &= ~B_ERROR;
	}
	bp->b_error = error;
}


int
geterror(struct buf *bp)
{
	int error = 0;

	if (bp->b_flags & B_ERROR) {
		error = bp->b_error;
		if (!error)
			error = EIO;
	}
	return (error);
}
#endif
