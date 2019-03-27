/*	$NetBSD: dtfs_vfsops.c,v 1.3 2012/11/04 23:37:02 christos Exp $	*/

/*
 * Copyright (c) 2006  Antti Kantee.  All Rights Reserved.
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
#include <sys/resource.h>

#include <stdio.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <puffs.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <util.h>

#include "dtfs.h"

static int
rtstr(struct puffs_usermount *pu, const char *str, enum vtype vt)
{
	struct puffs_node *pn = puffs_getroot(pu);
	struct vattr *va = &pn->pn_va;
	struct dtfs_file *df = DTFS_PTOF(pn);
	char ltarg[256+1];

	if (sscanf(str, "%*s %256s", ltarg) != 1)
		return 1;

	dtfs_baseattrs(va, vt, 2);
	df->df_linktarget = estrdup(ltarg);

	va->va_nlink = 1;
	va->va_size = strlen(df->df_linktarget);

	puffs_setrootinfo(pu, vt, 0, 0);

	return 0;
}

static int
rtdev(struct puffs_usermount *pu, const char *str, enum vtype vt)
{
	struct puffs_node *pn = puffs_getroot(pu);
	struct vattr *va = &pn->pn_va;
	int major, minor;

	if (sscanf(str, "%*s %d %d", &major, &minor) != 2)
		return 1;

	dtfs_baseattrs(va, vt, 2);
	va->va_nlink = 1;
	va->va_rdev = makedev(major, minor);

	if (vt == VBLK)
		va->va_mode |= S_IFBLK;
	else
		va->va_mode |= S_IFCHR;

	puffs_setrootinfo(pu, vt, 0, va->va_rdev);

	return 0;
}

static int
rtnorm(struct puffs_usermount *pu, const char *str, enum vtype vt)
{
	struct puffs_node *pn = puffs_getroot(pu);
	struct vattr *va = &pn->pn_va;

	dtfs_baseattrs(va, vt, 2);
	if (vt == VDIR)
		va->va_nlink = 2;
	else
		va->va_nlink = 1;

	puffs_setrootinfo(pu, vt, 0, 0);

	return 0;
}

struct rtype {
	char *tstr;
	enum vtype vt;
	int (*pfunc)(struct puffs_usermount *, const char *, enum vtype);
} rtypes[] = {
	{ "reg", VREG, rtnorm },
	{ "dir", VDIR, rtnorm },
	{ "blk", VBLK, rtdev },
	{ "chr", VCHR, rtdev },
	{ "lnk", VLNK, rtstr },
	{ "sock", VSOCK, rtnorm },
	{ "fifo", VFIFO, rtnorm }
};
#define NTYPES (sizeof(rtypes) / sizeof(rtypes[0]))

int
dtfs_domount(struct puffs_usermount *pu, const char *typestr)
{
	struct dtfs_mount *dtm;
	struct dtfs_file *dff;
	struct puffs_node *pn;
	int i;

	/* create mount-local thingie */
	dtm = puffs_getspecific(pu);
	dtm->dtm_nextfileid = 3;
	dtm->dtm_nfiles = 1;
	dtm->dtm_fsizes = 0;
	LIST_INIT(&dtm->dtm_pollent);

	/*
	 * create root directory, do it "by hand" to avoid special-casing
	 * dtfs_genfile()
	 */
	dff = dtfs_newdir();
	dff->df_dotdot = NULL;
	pn = puffs_pn_new(pu, dff);
	if (!pn)
		errx(1, "puffs_newpnode");
	puffs_setroot(pu, pn);

	if (!typestr) {
		rtnorm(pu, NULL, VDIR);
	} else {
		for (i = 0; i < NTYPES; i++) {
			if (strncmp(rtypes[i].tstr, typestr,
			    strlen(rtypes[i].tstr)) == 0) {
				if (rtypes[i].pfunc(pu, typestr,
				    rtypes[i].vt) != 0) {
					fprintf(stderr, "failed to parse "
					    "\"%s\"\n", typestr);
					return 1;
				}
				break;
			}
		}
		if (i == NTYPES) {
			fprintf(stderr, "no maching type for %s\n", typestr);
			return 1;
		}
	}

	return 0;
}

/*
 * statvfs() should fill in the following members of struct statvfs:
 * 
 * unsigned long   f_bsize;         file system block size
 * unsigned long   f_frsize;        fundamental file system block size
 * unsigned long   f_iosize;        optimal file system block size
 * fsblkcnt_t      f_blocks;        number of blocks in file system,
 *                                            (in units of f_frsize)
 *
 * fsblkcnt_t      f_bfree;         free blocks avail in file system
 * fsblkcnt_t      f_bavail;        free blocks avail to non-root
 * fsblkcnt_t      f_bresvd;        blocks reserved for root
 *
 * fsfilcnt_t      f_files;         total file nodes in file system
 * fsfilcnt_t      f_ffree;         free file nodes in file system
 * fsfilcnt_t      f_favail;        free file nodes avail to non-root
 * fsfilcnt_t      f_fresvd;        file nodes reserved for root
 *
 *
 * The rest are filled in by the kernel.
 */
#define ROUND(a,b) (((a) + ((b)-1)) & ~((b)-1))
#define NFILES 1024*1024
int
dtfs_fs_statvfs(struct puffs_usermount *pu, struct statvfs *sbp)
{
	struct rlimit rlim;
	struct dtfs_mount *dtm;
	off_t btot, bfree;
	int pgsize;

	dtm = puffs_getspecific(pu);
	pgsize = getpagesize();
	memset(sbp, 0, sizeof(struct statvfs));

	/*
	 * Use datasize rlimit as an _approximation_ for free size.
	 * This, of course, is not accurate due to overhead and not
	 * accounting for metadata.
	 */
	if (getrlimit(RLIMIT_DATA, &rlim) == 0)
		btot = rlim.rlim_cur;
	else
		btot = 16*1024*1024;
	bfree = btot - dtm->dtm_fsizes;

	sbp->f_blocks = ROUND(btot, pgsize) / pgsize;
	sbp->f_files = NFILES;

	sbp->f_bsize = sbp->f_frsize = sbp->f_iosize = pgsize;
	sbp->f_bfree = sbp->f_bavail = ROUND(bfree, pgsize) / pgsize;
	sbp->f_ffree = sbp->f_favail = NFILES - dtm->dtm_nfiles;

	sbp->f_bresvd = sbp->f_fresvd = 0;

	return 0;
}
#undef ROUND 

static void *
addrcmp(struct puffs_usermount *pu, struct puffs_node *pn, void *arg)
{

	if (pn == arg)
		return pn;

	return NULL;
}

int
dtfs_fs_fhtonode(struct puffs_usermount *pu, void *fid, size_t fidsize,
	struct puffs_newinfo *pni)
{
	struct dtfs_fid *dfid;
	struct puffs_node *pn;

	assert(fidsize == sizeof(struct dtfs_fid));
	dfid = fid;

	pn = puffs_pn_nodewalk(pu, addrcmp, dfid->dfid_addr);
	if (pn == NULL)
		return ESTALE;

	if (pn->pn_va.va_fileid != dfid->dfid_fileid
	    || pn->pn_va.va_gen != dfid->dfid_gen)
		return ESTALE;
	
	puffs_newinfo_setcookie(pni, pn);
	puffs_newinfo_setvtype(pni, pn->pn_va.va_type);
	puffs_newinfo_setsize(pni, pn->pn_va.va_size);
	puffs_newinfo_setrdev(pni, pn->pn_va.va_rdev);

	return 0;
}

int
dtfs_fs_nodetofh(struct puffs_usermount *pu, void *cookie,
	void *fid, size_t *fidsize)
{
	struct puffs_node *pn = cookie;
	struct dtfs_fid *dfid;
	extern int dynamicfh;

	if (dynamicfh == 0) {
		assert(*fidsize >= sizeof(struct dtfs_fid));
	} else {
		if (*fidsize < sizeof(struct dtfs_fid)) {
			*fidsize = sizeof(struct dtfs_fid);
			return E2BIG;
		}
		*fidsize = sizeof(struct dtfs_fid);
	}

	dfid = fid;

	dfid->dfid_addr = pn;
	dfid->dfid_fileid = pn->pn_va.va_fileid;
	dfid->dfid_gen = pn->pn_va.va_gen;

	return 0;
}

int
dtfs_fs_unmount(struct puffs_usermount *pu, int flags)
{

	return 0;
}
