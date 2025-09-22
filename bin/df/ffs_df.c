/*	$OpenBSD: ffs_df.c,v 1.19 2016/03/01 17:57:49 mmcc Exp $	*/

/*
 * Copyright (c) 1980, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>

#include <string.h>

int		ffs_df(int, char *, struct statfs *);

extern int	bread(int, off_t, void *, int);
extern char	*getmntpt(char *);

static union {
	struct fs iu_fs;
	char dummy[SBSIZE];
} sb;
#define sblock sb.iu_fs

int
ffs_df(int rfd, char *file, struct statfs *sfsp)
{
	char *mntpt;

	if (!((bread(rfd, (off_t)SBLOCK_UFS1, &sblock, SBSIZE) == 1 &&
	    sblock.fs_magic == FS_UFS1_MAGIC) ||
	    (bread(rfd, (off_t)SBLOCK_UFS2, &sblock, SBSIZE) == 1 &&
	    sblock.fs_magic == FS_UFS2_MAGIC))) {
		return (-1);
	}

	sfsp->f_flags = 0;
	sfsp->f_bsize = sblock.fs_fsize;
	sfsp->f_iosize = sblock.fs_bsize;
	if (sblock.fs_magic == FS_UFS1_MAGIC) {
		sfsp->f_blocks = sblock.fs_ffs1_dsize;
		sfsp->f_bfree = sblock.fs_ffs1_cstotal.cs_nbfree *
		    sblock.fs_frag + sblock.fs_ffs1_cstotal.cs_nffree;
		sfsp->f_bavail = sfsp->f_bfree -
		    ((int64_t)sblock.fs_ffs1_dsize * sblock.fs_minfree / 100);
		sfsp->f_files = sblock.fs_ncg * sblock.fs_ipg - ROOTINO;
		sfsp->f_ffree = sblock.fs_ffs1_cstotal.cs_nifree;
	} else {
		sfsp->f_blocks = sblock.fs_dsize;
		sfsp->f_bfree = sblock.fs_cstotal.cs_nbfree *
		    sblock.fs_frag + sblock.fs_cstotal.cs_nffree;
		sfsp->f_bavail = sfsp->f_bfree -
		    ((int64_t)sblock.fs_dsize * sblock.fs_minfree / 100);
		sfsp->f_files = sblock.fs_ncg * sblock.fs_ipg - ROOTINO;
		sfsp->f_ffree = sblock.fs_cstotal.cs_nifree;
	}
	sfsp->f_fsid.val[0] = 0;
	sfsp->f_fsid.val[1] = 0;
	if ((mntpt = getmntpt(file)) == 0)
		mntpt = "";
	strlcpy(sfsp->f_mntonname, mntpt, sizeof(sfsp->f_mntonname));
	strlcpy(sfsp->f_mntfromname, file, sizeof(sfsp->f_mntfromname));
	strlcpy(sfsp->f_fstypename, MOUNT_EXT2FS, sizeof(sfsp->f_fstypename));
	return (0);
}
