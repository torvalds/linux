/*	$NetBSD: fstest_nfs.c,v 1.9 2011/02/28 21:08:46 pooka Exp $	*/

/*
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
#include <libgen.h>
#include <pthread.h>
#include <puffs.h>
#include <puffsdump.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_fsmacros.h"
#include "mount_nfs.h"
#include "../../net/config/netconfig.c"

#define SERVERADDR "10.3.2.1"
#define SERVERROADDR "10.4.2.1"
#define CLIENTADDR "10.3.2.2"
#define CLIENTROADDR "10.4.2.2"
#define NETNETMASK "255.255.255.0"
#define EXPORTPATH "/myexport"

static void
childfail(int status)
{

	atf_tc_fail("child died");
}

/* fork rump nfsd, configure interface */
static int
donewfs(const atf_tc_t *tc, void **argp,
	const char *image, off_t size, void *fspriv)
{
	const char *srcdir;
	char *nfsdargv[16];
	char nfsdpath[MAXPATHLEN];
	char imagepath[MAXPATHLEN];
	char ethername[MAXPATHLEN], ethername_ro[MAXPATHLEN];
	char ifname[IFNAMSIZ], ifname_ro[IFNAMSIZ];
	char cwd[MAXPATHLEN];
	struct nfstestargs *args;
	pid_t childpid;
	int pipes[2];
	int devnull;

	/*
	 * First, we start the nfs service.
	 */
	srcdir = atf_tc_get_config_var(tc, "srcdir");
	sprintf(nfsdpath, "%s/../nfs/nfsservice/rumpnfsd", srcdir);
	sprintf(ethername, "/%s/%s.etherbus", getcwd(cwd, sizeof(cwd)), image);
	sprintf(ethername_ro, "%s_ro", ethername);
	sprintf(imagepath, "/%s/%s", cwd, image);

	nfsdargv[0] = nfsdpath;
	nfsdargv[1] = ethername;
	nfsdargv[2] = ethername_ro;
	nfsdargv[3] = __UNCONST(SERVERADDR);
	nfsdargv[4] = __UNCONST(SERVERROADDR);
	nfsdargv[5] = __UNCONST(NETNETMASK);
	nfsdargv[6] = __UNCONST(EXPORTPATH);
	nfsdargv[7] = imagepath;
	nfsdargv[8] = NULL;

	signal(SIGCHLD, childfail);
	if (pipe(pipes) == -1)
		return errno;

	switch ((childpid = fork())) {
	case 0:
		if (chdir(dirname(nfsdpath)) == -1)
			err(1, "chdir");
		close(pipes[0]);
		if (dup2(pipes[1], 3) == -1)
			err(1, "dup2");
		if (execvp(nfsdargv[0], nfsdargv) == -1)
			err(1, "execvp");
	case -1:
		return errno;
	default:
		close(pipes[1]);
		break;	
	}

	/*
	 * Ok, nfsd has been run.  The following sleep helps with the
	 * theoretical problem that nfsd can't start fast enough to
	 * process our mount request and we end up doing a timeout
	 * before the mount.  This would take several seconds.  So
	 * try to make sure nfsd is up&running already at this stage.
	 */
	if (read(pipes[0], &devnull, 4) == -1)
		return errno;

	/*
	 * Configure our networking interface.
	 */
	rump_init();
	netcfg_rump_makeshmif(ethername, ifname);
	netcfg_rump_if(ifname, CLIENTADDR, NETNETMASK);
	netcfg_rump_makeshmif(ethername_ro, ifname_ro);
	netcfg_rump_if(ifname_ro, CLIENTROADDR, NETNETMASK);

	/*
	 * That's it.  The rest is done in mount, since we don't have
	 * the mountpath available here.
	 */
	args = malloc(sizeof(*args));
	if (args == NULL)
		return errno;
	memset(args, 0, sizeof(*args));
	args->ta_childpid = childpid;
	strcpy(args->ta_ethername, ethername);

	*argp = args;

	return 0;
}

int
nfs_fstest_newfs(const atf_tc_t *tc, void **argp,
	const char *image, off_t size, void *fspriv)
{

	return donewfs(tc, argp, image, size, fspriv);
}

int
nfsro_fstest_newfs(const atf_tc_t *tc, void **argp,
	const char *image, off_t size, void *fspriv)
{

	return donewfs(tc, argp, image, size, fspriv);
}

/* mount the file system */
static int
domount(const atf_tc_t *tc, void *arg, const char *serverpath,
	const char *path, int flags)
{
	char canon_dev[MAXPATHLEN], canon_dir[MAXPATHLEN];
	const char *nfscliargs[] = {
		"nfsclient",
		serverpath,
		path,
		NULL,
	};
	struct nfs_args args;
	int mntflags;

	if (rump_sys_mkdir(path, 0777) == -1)
		return errno;

	/* XXX: atf does not reset values */
	optind = 1;
	opterr = 1;

	/*
	 * We use nfs parseargs here, since as a side effect it
	 * takes care of the RPC hulabaloo.
	 */
	mount_nfs_parseargs(__arraycount(nfscliargs)-1, __UNCONST(nfscliargs),
	    &args, &mntflags, canon_dev, canon_dir);

	if (rump_sys_mount(MOUNT_NFS, path, flags, &args, sizeof(args)) == -1) {
		return errno;
	}

	return 0;
}

int
nfs_fstest_mount(const atf_tc_t *tc, void *arg, const char *path, int flags)
{

	return domount(tc, arg, SERVERADDR ":" EXPORTPATH, path, flags);
}

/*
 * This is where the magic happens!
 *
 * If we are mounting r/w, do the normal thing.  However, if we are
 * doing a r/o mount, switch use the r/o server export address
 * and do a r/w mount.  This way we end up testing the r/o export policy
 * of the server! (yes, slightly questionable semantics, but at least
 * we notice very quickly if our assumption is broken in the future ;)
 */
int
nfsro_fstest_mount(const atf_tc_t *tc, void *arg, const char *path, int flags)
{

	if (flags & MNT_RDONLY) {
		flags &= ~MNT_RDONLY;
		return domount(tc, arg, SERVERROADDR":"EXPORTPATH, path, flags);
	} else {
		return domount(tc, arg, SERVERADDR":"EXPORTPATH, path, flags);
	}
}

static int
dodelfs(const atf_tc_t *tc, void *arg)
{

	/*
	 * XXX: no access to "args" since we're called from "cleanup".
	 * Trust atf to kill nfsd process and remove etherfile.
	 */
#if 0
	/*
	 * It's highly expected that the child will die next, so we
	 * don't need that information anymore thank you very many.
	 */
	signal(SIGCHLD, SIG_IGN);

	/*
	 * Just KILL it.  Sending it SIGTERM first causes it to try
	 * to send some unmount RPCs, leading to sticky situations.
	 */
	kill(args->ta_childpid, SIGKILL);
	wait(&status);

	/* remove ethernet bus */
	if (unlink(args->ta_ethername) == -1)
		atf_tc_fail_errno("unlink ethername");
#endif

	return 0;
}

int
nfs_fstest_delfs(const atf_tc_t *tc, void *arg)
{

	return dodelfs(tc, arg);
}

int
nfsro_fstest_delfs(const atf_tc_t *tc, void *arg)
{

	return dodelfs(tc, arg);
}

static int
dounmount(const atf_tc_t *tc, const char *path, int flags)
{
	int status, i, sverrno;

	/*
	 * NFS handles sillyrenames in an workqueue.  Some of them might
	 * be still in the queue even if all user activity has ceased.
	 * We try to unmount for 2 seconds to give them a chance
	 * to flush out.
	 *
	 * PR kern/43799
	 */
	for (i = 0; i < 20; i++) {
		if ((status = rump_sys_unmount(path, flags)) == 0)
			break;
		sverrno = errno;
		if (sverrno != EBUSY)
			break;
		usleep(100000);
	}
	if (status == -1)
		return sverrno;

	if (rump_sys_rmdir(path) == -1)
		return errno;

	return 0;
}

int
nfs_fstest_unmount(const atf_tc_t *tc, const char *path, int flags)
{

	return dounmount(tc, path, flags);
}

int
nfsro_fstest_unmount(const atf_tc_t *tc, const char *path, int flags)
{

	return dounmount(tc, path, flags);
}
