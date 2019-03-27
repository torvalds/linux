/*
 * Copyright (c) 2000-2002, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: mount_smbfs.c,v 1.17 2002/04/10 04:17:51 bp Exp $
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/linker.h>
#include <sys/mount.h>

#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <err.h>
#include <sysexits.h>

#include <cflib.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_lib.h>

#include <fs/smbfs/smbfs.h>

#include "mntopts.h"

static char mount_point[MAXPATHLEN + 1];
static void usage(void);

static struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_END
};

static char smbfs_vfsname[] = "smbfs";

int
main(int argc, char *argv[])
{
	struct iovec *iov;
	unsigned int iovlen;
	struct smb_ctx sctx, *ctx = &sctx;
	struct stat st;
#ifdef APPLE
	extern void dropsuid();
	extern int loadsmbvfs();
#else
	struct xvfsconf vfc;
#endif
	char *next, *p, *val;
	int opt, error, mntflags, caseopt, fd;
	uid_t uid;
	gid_t gid;
	mode_t dir_mode, file_mode;
	char errmsg[255] = { 0 };

	iov = NULL;
	iovlen = 0;
	fd = 0;
	uid = (uid_t)-1;
	gid = (gid_t)-1;
	caseopt = 0;
	file_mode = 0;
	dir_mode = 0;

#ifdef APPLE
	dropsuid();
#endif
	if (argc == 2) {
		if (strcmp(argv[1], "-h") == 0) {
			usage();
		}
	}
	if (argc < 3)
		usage();

#ifdef APPLE
	error = loadsmbvfs();
#else
	error = getvfsbyname(smbfs_vfsname, &vfc);
	if (error) {
		if (kldload(smbfs_vfsname) < 0)
			err(EX_OSERR, "kldload(%s)", smbfs_vfsname);
		error = getvfsbyname(smbfs_vfsname, &vfc);
	}
#endif
	if (error)
		errx(EX_OSERR, "SMB filesystem is not available");

	if (smb_lib_init() != 0)
		exit(1);

	mntflags = error = 0;

	caseopt = SMB_CS_NONE;

	if (smb_ctx_init(ctx, argc, argv, SMBL_SHARE, SMBL_SHARE, SMB_ST_DISK) != 0)
		exit(1);
	if (smb_ctx_readrc(ctx) != 0)
		exit(1);
	if (smb_rc)
		rc_close(smb_rc);

	while ((opt = getopt(argc, argv, STDPARAM_OPT"c:d:f:g:l:n:o:u:w:")) != -1) {
		switch (opt) {
		    case STDPARAM_ARGS:
			error = smb_ctx_opt(ctx, opt, optarg);
			if (error)
				exit(1);
			break;
		    case 'u': {
			struct passwd *pwd;

			pwd = isdigit(optarg[0]) ?
			    getpwuid(atoi(optarg)) : getpwnam(optarg);
			if (pwd == NULL)
				errx(EX_NOUSER, "unknown user '%s'", optarg);
			uid = pwd->pw_uid;
			break;
		    }
		    case 'g': {
			struct group *grp;

			grp = isdigit(optarg[0]) ?
			    getgrgid(atoi(optarg)) : getgrnam(optarg);
			if (grp == NULL)
				errx(EX_NOUSER, "unknown group '%s'", optarg);
			gid = grp->gr_gid;
			break;
		    }
		    case 'd':
			errno = 0;
			dir_mode = strtol(optarg, &next, 8);
			if (errno || *next != 0)
				errx(EX_DATAERR, "invalid value for directory mode");
			break;
		    case 'f':
			errno = 0;
			file_mode = strtol(optarg, &next, 8);
			if (errno || *next != 0)
				errx(EX_DATAERR, "invalid value for file mode");
			break;
		    case '?':
			usage();
			/*NOTREACHED*/
		    case 'n': {
			char *inp, *nsp;

			nsp = inp = optarg;
			while ((nsp = strsep(&inp, ",;:")) != NULL) {
				if (strcasecmp(nsp, "LONG") == 0) {
					build_iovec(&iov, &iovlen,
					    "nolong", NULL, 0);
				} else {
					errx(EX_DATAERR,
					    "unknown suboption '%s'", nsp);
				}
			}
			break;
		    };
		    case 'o':
			getmntopts(optarg, mopts, &mntflags, 0);
			p = strchr(optarg, '=');
			val = NULL;
			if (p != NULL) {
				*p = '\0';
				val = p + 1;
			}
			build_iovec(&iov, &iovlen, optarg, val, (size_t)-1);
			break;
		    case 'c':
			switch (optarg[0]) {
			    case 'l':
				caseopt |= SMB_CS_LOWER;
				break;
			    case 'u':
				caseopt |= SMB_CS_UPPER;
				break;
			    default:
		    		errx(EX_DATAERR, "invalid suboption '%c' for -c",
				    optarg[0]);
			}
			break;
		    default:
			usage();
		}
	}

	if (optind == argc - 2)
		optind++;
	
	if (optind != argc - 1)
		usage();
	realpath(argv[optind], mount_point);

	if (stat(mount_point, &st) == -1)
		err(EX_OSERR, "could not find mount point %s", mount_point);
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		err(EX_OSERR, "can't mount on %s", mount_point);
	}
/*
	if (smb_getextattr(mount_point, &einfo) == 0)
		errx(EX_OSERR, "can't mount on %s twice", mount_point);
*/
	if (uid == (uid_t)-1)
		uid = st.st_uid;
	if (gid == (gid_t)-1)
		gid = st.st_gid;
	if (file_mode == 0 )
		file_mode = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
	if (dir_mode == 0) {
		dir_mode = file_mode;
		if (dir_mode & S_IRUSR)
			dir_mode |= S_IXUSR;
		if (dir_mode & S_IRGRP)
			dir_mode |= S_IXGRP;
		if (dir_mode & S_IROTH)
			dir_mode |= S_IXOTH;
	}
	/*
	 * For now, let connection be private for this mount
	 */
	ctx->ct_ssn.ioc_opt |= SMBVOPT_PRIVATE;
	ctx->ct_ssn.ioc_owner = ctx->ct_sh.ioc_owner = 0; /* root */
	ctx->ct_ssn.ioc_group = ctx->ct_sh.ioc_group = gid;
	opt = 0;
	if (dir_mode & S_IXGRP)
		opt |= SMBM_EXECGRP;
	if (dir_mode & S_IXOTH)
		opt |= SMBM_EXECOTH;
	ctx->ct_ssn.ioc_rights |= opt;
	ctx->ct_sh.ioc_rights |= opt;
	error = smb_ctx_resolve(ctx);
	if (error)
		exit(1);
	error = smb_ctx_lookup(ctx, SMBL_SHARE, SMBLK_CREATE);
	if (error) {
		exit(1);
	}

	fd = ctx->ct_fd;

	build_iovec(&iov, &iovlen, "fstype", strdup("smbfs"), -1);
	build_iovec(&iov, &iovlen, "fspath", mount_point, -1);
	build_iovec_argf(&iov, &iovlen, "fd", "%d", fd);
	build_iovec(&iov, &iovlen, "mountpoint", mount_point, -1);
	build_iovec_argf(&iov, &iovlen, "uid", "%d", uid);
	build_iovec_argf(&iov, &iovlen, "gid", "%d", gid);
	build_iovec_argf(&iov, &iovlen, "file_mode", "%d", file_mode);
	build_iovec_argf(&iov, &iovlen, "dir_mode", "%d", dir_mode);
	build_iovec_argf(&iov, &iovlen, "caseopt", "%d", caseopt);
	build_iovec(&iov, &iovlen, "errmsg", errmsg, sizeof errmsg); 

	error = nmount(iov, iovlen, mntflags);
	smb_ctx_done(ctx);
	if (error) {
		smb_error("mount error: %s %s", error, mount_point, errmsg);
		exit(1);
	}
	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n",
	"usage: mount_smbfs [-E cs1:cs2] [-I host] [-L locale] [-M crights:srights]",
	"                   [-N] [-O cowner:cgroup/sowner:sgroup] [-R retrycount]",
	"                   [-T timeout] [-W workgroup] [-c case] [-d mode] [-f mode]",
	"                   [-g gid] [-n opt] [-u uid] [-U username] //user@server/share node");

	exit (1);
}
