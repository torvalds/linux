/*	$OpenBSD: ext2fs_df.c,v 1.16 2016/03/01 17:57:49 mmcc Exp $	*/

/*
 * This file is substantially derived from src/sys/ufs/ext2fs/ext2fs_vfsops.c:e2fs_statfs().
 * That file's copyright is applied here.
 */

/* Modified for EXT2FS on NetBSD by Manuel Bouyer, April 1997 */

/*
 * Copyright (c) 1989, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_vfsops.c	8.14 (Berkeley) 11/28/94
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <string.h>

int		e2fs_df(int, char *, struct statfs *);

extern int	bread(int, off_t, void *, int);
extern char	*getmntpt(char *);

static union {
	struct ext2fs ie_fs;
	char dummy[SBSIZE];
} sb;
#define sblock sb.ie_fs

int
e2fs_df(int rfd, char *file, struct statfs *sfsp)
{
	char *mntpt;
	u_int32_t overhead, overhead_per_group;
	int32_t	ncg, ngdb, ipb, itpg;

	if (bread(rfd, (off_t)SBOFF, &sblock, SBSIZE) == 0) {
		return (-1);
	}
	if ((sblock.e2fs_magic != E2FS_MAGIC) ||
	    (sblock.e2fs_rev != E2FS_REV0 && sblock.e2fs_rev != E2FS_REV1)) {
		return (-1);
	}
	sfsp->f_flags = 0;	/* The fs is not mapped, so no flags */
	sfsp->f_bsize = 1024 << sblock.e2fs_log_bsize;
	sfsp->f_iosize = 1024 << sblock.e2fs_log_bsize;

	if ((ipb = sfsp->f_bsize / sizeof(struct ext2fs_dinode)) == 0)
		return (-1);
	itpg = sblock.e2fs_ipg / ipb;

	ncg = howmany(sblock.e2fs_bcount - sblock.e2fs_first_dblock,
		sblock.e2fs_bpg);
	ngdb = howmany(ncg, sfsp->f_bsize / sizeof(struct ext2_gd));
	overhead_per_group = 1 /* super block */ +
					ngdb +
					1 /* block bitmap */ +
					1 /* inode bitmap */ +
					itpg;
	overhead = sblock.e2fs_first_dblock + ncg * overhead_per_group;

	sfsp->f_blocks = sblock.e2fs_bcount - overhead;
	sfsp->f_bfree = sblock.e2fs_fbcount;
	sfsp->f_bavail = sfsp->f_bfree - sblock.e2fs_rbcount;
	sfsp->f_files = sblock.e2fs_icount;
	sfsp->f_ffree = sblock.e2fs_ficount;
	sfsp->f_fsid.val[0] = 0;
	sfsp->f_fsid.val[1] = 0;
	if ((mntpt = getmntpt(file)) == 0)
		mntpt = "";
	strlcpy(sfsp->f_mntonname, mntpt, sizeof(sfsp->f_mntonname));
	strlcpy(sfsp->f_mntfromname, file, sizeof(sfsp->f_mntfromname));
	strlcpy(sfsp->f_fstypename, MOUNT_EXT2FS, sizeof(sfsp->f_fstypename));
	return (0);
}
