/*	$NetBSD: fstest_puffs.c,v 1.11 2013/09/09 19:47:38 pooka Exp $	*/

/*
 * Copyright (c) 2010, 2011 The NetBSD Foundation, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/wait.h>

#include <assert.h>
#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <puffs.h>
#include <puffsdump.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_fsmacros.h"

#define BUFSIZE (128*1024)
#define DTFS_DUMP "-o","dump"

static bool mayquit = false;

static ssize_t
xread(int fd, void *vp, size_t n)
{
	size_t left;

	left = n;
	do {
		ssize_t ssz;

		ssz = read(fd, vp, left);
		if (ssz == -1) {
			return ssz;
		}
		left -= ssz;
		vp = (char *)vp + ssz;
	} while (left > 0);
	return n;
}

static ssize_t
xwrite(int fd, const void *vp, size_t n)
{
	size_t left;

	left = n;
	do {
		ssize_t ssz;

		ssz = write(fd, vp, left);
		if (ssz == -1) {
			return ssz;
		}
		left -= ssz;
		vp = (const char *)vp + ssz;
	} while (left > 0);
	return n;
}

/*
 * Threads which shovel data between comfd and /dev/puffs.
 * (cannot use polling since fd's are in different namespaces)
 */
static void *
readshovel(void *arg)
{
	struct putter_hdr *phdr;
	struct puffs_req *preq;
	struct puffstestargs *args = arg;
	char buf[BUFSIZE];
	ssize_t n;
	int comfd, puffsfd;

	comfd = args->pta_servfd;
	puffsfd = args->pta_rumpfd;

	phdr = (void *)buf;
	preq = (void *)buf;

	rump_pub_lwproc_newlwp(1);

	for (;;) {
		n = rump_sys_read(puffsfd, buf, sizeof(*phdr));
		if (n <= 0) {
			fprintf(stderr, "readshovel r1 %zd / %d\n", n, errno);
			break;
		}

		assert(phdr->pth_framelen < BUFSIZE);
		n = rump_sys_read(puffsfd, buf+sizeof(*phdr), 
		    phdr->pth_framelen - sizeof(*phdr));
		if (n <= 0) {
			fprintf(stderr, "readshovel r2 %zd / %d\n", n, errno);
			break;
		}

		/* Analyze request */
		if (PUFFSOP_OPCLASS(preq->preq_opclass) == PUFFSOP_VFS) {
			assert(preq->preq_optype < PUFFS_VFS_MAX);
			args->pta_vfs_toserv_ops[preq->preq_optype]++;
		} else if (PUFFSOP_OPCLASS(preq->preq_opclass) == PUFFSOP_VN) {
			assert(preq->preq_optype < PUFFS_VN_MAX);
			args->pta_vn_toserv_ops[preq->preq_optype]++;
		}

		n = phdr->pth_framelen;
		if (xwrite(comfd, buf, n) != n) {
			fprintf(stderr, "readshovel write %zd / %d\n", n, errno);
			break;
		}
	}

	if (n != 0 && mayquit == false)
		abort();
	return NULL;
}

static void *
writeshovel(void *arg)
{
	struct puffstestargs *args = arg;
	struct putter_hdr *phdr;
	struct puffs_req *preq;
	char buf[BUFSIZE];
	size_t toread;
	ssize_t n;
	int comfd, puffsfd;

	rump_pub_lwproc_newlwp(1);

	comfd = args->pta_servfd;
	puffsfd = args->pta_rumpfd;

	phdr = (struct putter_hdr *)buf;
	preq = (void *)buf;

	for (;;) {
		uint64_t off;

		/*
		 * Need to write everything to the "kernel" in one chunk,
		 * so make sure we have it here.
		 */
		off = 0;
		toread = sizeof(struct putter_hdr);
		assert(toread < BUFSIZE);
		do {
			n = xread(comfd, buf+off, toread);
			if (n <= 0) {
				fprintf(stderr, "writeshovel read %zd / %d\n",
				    n, errno);
				goto out;
			}
			off += n;
			if (off >= sizeof(struct putter_hdr))
				toread = phdr->pth_framelen - off;
			else
				toread = off - sizeof(struct putter_hdr);
		} while (toread);

		if (__predict_false(
		    PUFFSOP_OPCLASS(preq->preq_opclass) == PUFFSOP_VFS
		    && preq->preq_optype == PUFFS_VFS_UNMOUNT)) {
			if (preq->preq_rv == 0)
				mayquit = true;
		}

		n = rump_sys_write(puffsfd, buf, phdr->pth_framelen);
		if ((size_t)n != phdr->pth_framelen) {
			fprintf(stderr, "writeshovel wr %zd / %d\n", n, errno);
			break;
		}
	}

 out:
	if (n != 0)
		abort();
	return NULL;
}

static void
rumpshovels(struct puffstestargs *args)
{
	pthread_t pt;
	int rv;

	if ((rv = rump_init()) == -1)
		err(1, "rump_init");

	if (pthread_create(&pt, NULL, readshovel, args) == -1)
		err(1, "read shovel");
	pthread_detach(pt);

	if (pthread_create(&pt, NULL, writeshovel, args) == -1)
		err(1, "write shovel");
	pthread_detach(pt);
}

static void
childfail(int sign)
{

	atf_tc_fail("child died"); /* almost signal-safe */
}

struct puffstestargs *theargs; /* XXX */

/* XXX: we don't support size */
static int
donewfs(const atf_tc_t *tc, void **argp,
	const char *image, off_t size, void *fspriv, char **theargv)
{
	struct puffstestargs *args;
	pid_t childpid;
	int *pflags;
	char comfd[16];
	int sv[2];
	int mntflags;
	size_t len;
	ssize_t n;

	*argp = NULL;

	args = malloc(sizeof(*args));
	if (args == NULL)
		return errno;
	memset(args, 0, sizeof(*args));

	pflags = &args->pta_pflags;

	/* Create sucketpair for communication with the real file server */
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sv) == -1)
		return errno;

	signal(SIGCHLD, childfail);

	switch ((childpid = fork())) {
	case 0:
		close(sv[1]);
		snprintf(comfd, sizeof(sv[0]), "%d", sv[0]);
		if (setenv("PUFFS_COMFD", comfd, 1) == -1)
			return errno;

		if (execvp(theargv[0], theargv) == -1)
			return errno;
	case -1:
		return errno;
	default:
		close(sv[0]);
		break;
	}

	/* read args */
	if ((n = xread(sv[1], &len, sizeof(len))) != sizeof(len))
		err(1, "mp 1 %zd", n);
	if (len > MAXPATHLEN)
		err(1, "mntpath > MAXPATHLEN");
	if ((size_t)xread(sv[1], args->pta_dir, len) != len)
		err(1, "mp 2");
	if (xread(sv[1], &len, sizeof(len)) != sizeof(len))
		err(1, "fn 1");
	if (len > MAXPATHLEN)
		err(1, "devpath > MAXPATHLEN");
	if ((size_t)xread(sv[1], args->pta_dev, len) != len)
		err(1, "fn 2");
	if (xread(sv[1], &mntflags, sizeof(mntflags)) != sizeof(mntflags))
		err(1, "mntflags");
	if (xread(sv[1], &args->pta_pargslen, sizeof(args->pta_pargslen))
	    != sizeof(args->pta_pargslen))
		err(1, "puffstest_args len");
	args->pta_pargs = malloc(args->pta_pargslen);
	if (args->pta_pargs == NULL)
		err(1, "malloc");
	if (xread(sv[1], args->pta_pargs, args->pta_pargslen)
	    != (ssize_t)args->pta_pargslen)
		err(1, "puffstest_args");
	if (xread(sv[1], pflags, sizeof(*pflags)) != sizeof(*pflags))
		err(1, "pflags");

	args->pta_childpid = childpid;
	args->pta_servfd = sv[1];
	strlcpy(args->pta_dev, image, sizeof(args->pta_dev));

	*argp = theargs = args;

	return 0;
}

int
puffs_fstest_newfs(const atf_tc_t *tc, void **argp,
	const char *image, off_t size, void *fspriv)
{
	char dtfs_path[MAXPATHLEN];
	char *dtfsargv[6];
	char **theargv;

	/* build dtfs exec path from atf test dir */
	sprintf(dtfs_path, "%s/../puffs/h_dtfs/h_dtfs",
	    atf_tc_get_config_var(tc, "srcdir"));

	if (fspriv) {
		theargv = fspriv;
		theargv[0] = dtfs_path;
	} else {
		dtfsargv[0] = dtfs_path;
		dtfsargv[1] = __UNCONST("-i");
		dtfsargv[2] = __UNCONST("-s");
		dtfsargv[3] = __UNCONST("dtfs");
		dtfsargv[4] = __UNCONST("fictional");
		dtfsargv[5] = NULL;

		theargv = dtfsargv;
	}

	return donewfs(tc, argp, image, size, fspriv, theargv);
}

int
p2k_ffs_fstest_newfs(const atf_tc_t *tc, void **argp,
	const char *image, off_t size, void *fspriv)
{
	char *rumpffs_argv[5];
	int rv;

	rump_init();
	if ((rv = ffs_fstest_newfs(tc, argp, image, size, fspriv)) != 0)
		return rv;
	if (mkdir("p2kffsfake", 0777) == -1 && errno != EEXIST)
		return errno;

	setenv("P2K_NODETACH", "1", 1);
	rumpffs_argv[0] = __UNCONST("rump_ffs");
	rumpffs_argv[1] = __UNCONST(image);
	rumpffs_argv[2] = __UNCONST("p2kffsfake"); /* NOTUSED */
	rumpffs_argv[3] = NULL;

	if ((rv = donewfs(tc, argp, image, size, fspriv, rumpffs_argv)) != 0)
		ffs_fstest_delfs(tc, argp);
	return rv;
}

int
puffs_fstest_mount(const atf_tc_t *tc, void *arg, const char *path, int flags)
{
	struct puffstestargs *pargs = arg;
	int fd;

	rump_init();
	fd = rump_sys_open("/dev/puffs", O_RDWR);
	if (fd == -1)
		return fd;

	if (rump_sys_mkdir(path, 0777) == -1)
		return -1;

	if (rump_sys_mount(MOUNT_PUFFS, path, flags,
	    pargs->pta_pargs, pargs->pta_pargslen) == -1) {
		/* apply "to kill a child" to avoid atf hang (kludge) */
		kill(pargs->pta_childpid, SIGKILL);
		return -1;
	}

	pargs->pta_rumpfd = fd;
	rumpshovels(pargs);

	return 0;
}
__strong_alias(p2k_ffs_fstest_mount,puffs_fstest_mount);

int
puffs_fstest_delfs(const atf_tc_t *tc, void *arg)
{

	/* useless ... */
	return 0;
}

int
p2k_ffs_fstest_delfs(const atf_tc_t *tc, void *arg)
{

	return ffs_fstest_delfs(tc, arg);
}

int
puffs_fstest_unmount(const atf_tc_t *tc, const char *path, int flags)
{
	struct puffstestargs *pargs = theargs;
	int status;
	int rv;

	/* ok, child might exit here */
	signal(SIGCHLD, SIG_IGN);

	rv = rump_sys_unmount(path, flags);
	if (rv)	
		return rv;

	if ((rv = rump_sys_rmdir(path)) != 0)
		return rv;

	if (waitpid(pargs->pta_childpid, &status, WNOHANG) > 0)
		return 0;
	kill(pargs->pta_childpid, SIGTERM);
	usleep(10);
	if (waitpid(pargs->pta_childpid, &status, WNOHANG) > 0)
		return 0;
	kill(pargs->pta_childpid, SIGKILL);
	usleep(500);
	wait(&status);

	rmdir("p2kffsfake");

	return 0;
}
__strong_alias(p2k_ffs_fstest_unmount,puffs_fstest_unmount);
