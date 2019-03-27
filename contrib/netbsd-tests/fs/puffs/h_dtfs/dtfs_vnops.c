/*	$NetBSD: dtfs_vnops.c,v 1.10 2013/10/19 17:45:00 christos Exp $	*/

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
#include <sys/poll.h>

#include <assert.h>
#include <errno.h>
#include <puffs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "dtfs.h"

int
dtfs_node_lookup(struct puffs_usermount *pu, void *opc,
	struct puffs_newinfo *pni, const struct puffs_cn *pcn)
{
	struct puffs_node *pn_dir = opc;
	struct dtfs_file *df = DTFS_CTOF(opc);
	struct dtfs_dirent *dfd;
	extern int straightflush;
	int rv;

	/* parent dir? */
	if (PCNISDOTDOT(pcn)) {
		if (df->df_dotdot == NULL)
			return ENOENT;

		assert(df->df_dotdot->pn_va.va_type == VDIR);
		puffs_newinfo_setcookie(pni, df->df_dotdot);
		puffs_newinfo_setvtype(pni, df->df_dotdot->pn_va.va_type);

		return 0;
	}

	dfd = dtfs_dirgetbyname(df, pcn->pcn_name, pcn->pcn_namelen);
	if (dfd) {
		if ((pcn->pcn_flags & NAMEI_ISLASTCN) &&
		    (pcn->pcn_nameiop == NAMEI_DELETE)) {
			rv = puffs_access(VDIR, pn_dir->pn_va.va_mode,
			    pn_dir->pn_va.va_uid, pn_dir->pn_va.va_gid,
			    PUFFS_VWRITE, pcn->pcn_cred);
			if (rv)
				return rv;
		}
		puffs_newinfo_setcookie(pni, dfd->dfd_node);
		puffs_newinfo_setvtype(pni, dfd->dfd_node->pn_va.va_type);
		puffs_newinfo_setsize(pni, dfd->dfd_node->pn_va.va_size);
		puffs_newinfo_setrdev(pni, dfd->dfd_node->pn_va.va_rdev);

		if (straightflush)
			puffs_flush_pagecache_node(pu, dfd->dfd_node);

		return 0;
	}

	if ((pcn->pcn_flags & NAMEI_ISLASTCN)
	    && (pcn->pcn_nameiop == NAMEI_CREATE ||
	        pcn->pcn_nameiop == NAMEI_RENAME)) {
		rv = puffs_access(VDIR, pn_dir->pn_va.va_mode,
		    pn_dir->pn_va.va_uid, pn_dir->pn_va.va_gid,
		    PUFFS_VWRITE, pcn->pcn_cred);
		if (rv)
			return rv;
	}

	return ENOENT;
}

int
dtfs_node_access(struct puffs_usermount *pu, void *opc, int acc_mode,
	const struct puffs_cred *pcr)
{
	struct puffs_node *pn = opc;

	return puffs_access(pn->pn_va.va_type, pn->pn_va.va_mode,
	    pn->pn_va.va_uid, pn->pn_va.va_gid, acc_mode, pcr);
}

int
dtfs_node_setattr(struct puffs_usermount *pu, void *opc,
	const struct vattr *va, const struct puffs_cred *pcr)
{
	struct puffs_node *pn = opc;
	int rv;

	/* check permissions */
	if (va->va_flags != PUFFS_VNOVAL)
		return EOPNOTSUPP;

	if (va->va_uid != PUFFS_VNOVAL || va->va_gid != PUFFS_VNOVAL) {
		rv = puffs_access_chown(pn->pn_va.va_uid, pn->pn_va.va_gid,
		    va->va_uid, va->va_gid, pcr);
		if (rv)
			return rv;
	}

	if (va->va_mode != PUFFS_VNOVAL) {
		rv = puffs_access_chmod(pn->pn_va.va_uid, pn->pn_va.va_gid,
		    pn->pn_va.va_type, va->va_mode, pcr);
		if (rv)
			return rv;
	}

	if ((va->va_atime.tv_sec != PUFFS_VNOVAL
	      && va->va_atime.tv_nsec != PUFFS_VNOVAL)
	    || (va->va_mtime.tv_sec != PUFFS_VNOVAL
	      && va->va_mtime.tv_nsec != PUFFS_VNOVAL)) {
		rv = puffs_access_times(pn->pn_va.va_uid, pn->pn_va.va_gid,
		    pn->pn_va.va_mode, va->va_vaflags & VA_UTIMES_NULL, pcr);
		if (rv)
			return rv;
	}

	if (va->va_size != PUFFS_VNOVAL) {
		switch (pn->pn_va.va_type) {
		case VREG:
			dtfs_setsize(pn, va->va_size);
			pn->pn_va.va_bytes = va->va_size;
			break;
		case VBLK:
		case VCHR:
		case VFIFO:
			break;
		case VDIR:
			return EISDIR;
		default:
			return EOPNOTSUPP;
		}
	}

	puffs_setvattr(&pn->pn_va, va);

	return 0;
}

/* create a new node in the parent directory specified by opc */
int
dtfs_node_create(struct puffs_usermount *pu, void *opc,
	struct puffs_newinfo *pni, const struct puffs_cn *pcn,
	const struct vattr *va)
{
	struct puffs_node *pn_parent = opc;
	struct puffs_node *pn_new;

	if (!(va->va_type == VREG || va->va_type == VSOCK))
		return ENODEV;

	pn_new = dtfs_genfile(pn_parent, pcn, va->va_type);
	puffs_setvattr(&pn_new->pn_va, va);

	puffs_newinfo_setcookie(pni, pn_new);

	return 0;
}

int
dtfs_node_remove(struct puffs_usermount *pu, void *opc, void *targ,
	const struct puffs_cn *pcn)
{
	struct puffs_node *pn_parent = opc;
	struct puffs_node *pn = targ;

	if (pn->pn_va.va_type == VDIR)
		return EPERM;

	dtfs_nukenode(targ, pn_parent, pcn->pcn_name, pcn->pcn_namelen);

	if (pn->pn_va.va_nlink == 0)
		puffs_setback(puffs_cc_getcc(pu), PUFFS_SETBACK_NOREF_N2);

	return 0;
}

int
dtfs_node_mkdir(struct puffs_usermount *pu, void *opc,
	struct puffs_newinfo *pni, const struct puffs_cn *pcn,
	const struct vattr *va)
{
	struct puffs_node *pn_parent = opc;
	struct puffs_node *pn_new;

	pn_new = dtfs_genfile(pn_parent, pcn, VDIR);
	puffs_setvattr(&pn_new->pn_va, va);

	puffs_newinfo_setcookie(pni, pn_new);

	return 0;
}

int
dtfs_node_rmdir(struct puffs_usermount *pu, void *opc, void *targ,
	const struct puffs_cn *pcn)
{
	struct puffs_node *pn_parent = opc;
	struct dtfs_file *df = DTFS_CTOF(targ);

	if (!LIST_EMPTY(&df->df_dirents))
		return ENOTEMPTY;

	dtfs_nukenode(targ, pn_parent, pcn->pcn_name, pcn->pcn_namelen);
	puffs_setback(puffs_cc_getcc(pu), PUFFS_SETBACK_NOREF_N2);

	return 0;
}

int
dtfs_node_readdir(struct puffs_usermount *pu, void *opc,
	struct dirent *dent, off_t *readoff, size_t *reslen,
	const struct puffs_cred *pcr,
	int *eofflag, off_t *cookies, size_t *ncookies)
{
	struct puffs_node *pn = opc;
	struct puffs_node *pn_nth;
	struct dtfs_dirent *dfd_nth;

	if (pn->pn_va.va_type != VDIR)
		return ENOTDIR;
	
	dtfs_updatetimes(pn, 1, 0, 0);

	*ncookies = 0;
 again:
	if (*readoff == DENT_DOT || *readoff == DENT_DOTDOT) {
		puffs_gendotdent(&dent, pn->pn_va.va_fileid, *readoff, reslen);
		(*readoff)++;
		PUFFS_STORE_DCOOKIE(cookies, ncookies, *readoff);
		goto again;
	}

	for (;;) {
		dfd_nth = dtfs_dirgetnth(pn->pn_data, DENT_ADJ(*readoff));
		if (!dfd_nth) {
			*eofflag = 1;
			break;
		}
		pn_nth = dfd_nth->dfd_node;

		if (!puffs_nextdent(&dent, dfd_nth->dfd_name,
		    pn_nth->pn_va.va_fileid,
		    puffs_vtype2dt(pn_nth->pn_va.va_type),
		    reslen))
			break;

		(*readoff)++;
		PUFFS_STORE_DCOOKIE(cookies, ncookies, *readoff);
	}

	return 0;
}

int
dtfs_node_poll(struct puffs_usermount *pu, void *opc, int *events)
{
	struct dtfs_mount *dtm = puffs_getspecific(pu);
	struct dtfs_poll dp;
	struct itimerval it;

	memset(&it, 0, sizeof(struct itimerval));
	it.it_value.tv_sec = 4;
	if (setitimer(ITIMER_REAL, &it, NULL) == -1)
		return errno;

	dp.dp_pcc = puffs_cc_getcc(pu);
	LIST_INSERT_HEAD(&dtm->dtm_pollent, &dp, dp_entries);
	puffs_cc_yield(dp.dp_pcc);

	*events = *events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);
	return 0;
}

int
dtfs_node_mmap(struct puffs_usermount *pu, void *opc, vm_prot_t prot,
	const struct puffs_cred *pcr)
{
	struct dtfs_mount *dtm = puffs_getspecific(pu);

	if ((dtm->dtm_allowprot & prot) != prot)
		return EACCES;

	return 0;
}

int
dtfs_node_rename(struct puffs_usermount *pu, void *opc, void *src,
	const struct puffs_cn *pcn_src, void *targ_dir, void *targ,
	const struct puffs_cn *pcn_targ)
{
	struct dtfs_dirent *dfd_src;
	struct dtfs_file *df_targ;
	struct puffs_node *pn_sdir = opc;
	struct puffs_node *pn_sfile = src;
	struct puffs_node *pn_tdir = targ_dir;
	struct puffs_node *pn_tfile = targ;

	/* check that we don't do the old amigados trick */
	if (pn_sfile->pn_va.va_type == VDIR) {
		if (dtfs_isunder(pn_tdir, pn_sfile))
			return EINVAL;

		if ((pcn_src->pcn_namelen == 1 && pcn_src->pcn_name[0]=='.') ||
		    opc == src ||
		    PCNISDOTDOT(pcn_src) ||
		    PCNISDOTDOT(pcn_targ)) {
			return EINVAL;
		}
	}

	dfd_src = dtfs_dirgetbyname(DTFS_PTOF(pn_sdir),
	    pcn_src->pcn_name, pcn_src->pcn_namelen);

	/* does it still exist, or did someone race us here? */
	if (dfd_src == NULL) {
		return ENOENT;
	}

	/* if there's a target file, nuke it for atomic replacement */
	if (pn_tfile) {
		if (pn_tfile->pn_va.va_type == VDIR) {
			df_targ = DTFS_CTOF(pn_tfile);
			if (!LIST_EMPTY(&df_targ->df_dirents))
				return ENOTEMPTY;
		}
		dtfs_nukenode(pn_tfile, pn_tdir,
		    pcn_targ->pcn_name, pcn_targ->pcn_namelen);
	}

	/* out with the old */
	dtfs_removedent(pn_sdir, dfd_src);
	/* and in with the new */
	dtfs_adddent(pn_tdir, dfd_src);

	/* update name */
	free(dfd_src->dfd_name);
	dfd_src->dfd_name = estrndup(pcn_targ->pcn_name,pcn_targ->pcn_namelen);
	dfd_src->dfd_namelen = strlen(dfd_src->dfd_name);

	dtfs_updatetimes(src, 0, 1, 0);

	return 0;
}

int
dtfs_node_link(struct puffs_usermount *pu, void *opc, void *targ,
	const struct puffs_cn *pcn)
{
	struct puffs_node *pn_dir = opc;
	struct dtfs_dirent *dfd;

	dfd = emalloc(sizeof(struct dtfs_dirent));
	dfd->dfd_node = targ;
	dfd->dfd_name = estrndup(pcn->pcn_name, pcn->pcn_namelen);
	dfd->dfd_namelen = strlen(dfd->dfd_name);
	dtfs_adddent(pn_dir, dfd);

	dtfs_updatetimes(targ, 0, 1, 0);

	return 0;
}

int
dtfs_node_symlink(struct puffs_usermount *pu, void *opc,
	struct puffs_newinfo *pni, const struct puffs_cn *pcn_src,
	const struct vattr *va, const char *link_target)
{
	struct puffs_node *pn_parent = opc;
	struct puffs_node *pn_new;
	struct dtfs_file *df_new;

	if (va->va_type != VLNK)
		return ENODEV;

	pn_new = dtfs_genfile(pn_parent, pcn_src, VLNK);
	puffs_setvattr(&pn_new->pn_va, va);
	df_new = DTFS_PTOF(pn_new);
	df_new->df_linktarget = estrdup(link_target);
	pn_new->pn_va.va_size = strlen(df_new->df_linktarget);

	puffs_newinfo_setcookie(pni, pn_new);

	return 0;
}

int
dtfs_node_readlink(struct puffs_usermount *pu, void *opc,
	const struct puffs_cred *cred, char *linkname, size_t *linklen)
{
	struct dtfs_file *df = DTFS_CTOF(opc);
	struct puffs_node *pn = opc;

	assert(pn->pn_va.va_type == VLNK);
	strlcpy(linkname, df->df_linktarget, *linklen);
	*linklen = strlen(linkname);

	return 0;
}

int
dtfs_node_mknod(struct puffs_usermount *pu, void *opc,
	struct puffs_newinfo *pni, const struct puffs_cn *pcn,
	const struct vattr *va)
{
	struct puffs_node *pn_parent = opc;
	struct puffs_node *pn_new;

	if (!(va->va_type == VBLK || va->va_type == VCHR
	    || va->va_type == VFIFO))
		return EINVAL;

	pn_new = dtfs_genfile(pn_parent, pcn, va->va_type);
	puffs_setvattr(&pn_new->pn_va, va);

	puffs_newinfo_setcookie(pni, pn_new);

	return 0;
}

#define BLOCKOFF(a,b) ((a) & ((b)-1))
#define BLOCKLEFT(a,b) ((b) - BLOCKOFF(a,b))

/*
 * Read operation, used both for VOP_READ and VOP_GETPAGES
 */
int
dtfs_node_read(struct puffs_usermount *pu, void *opc, uint8_t *buf,
	off_t offset, size_t *resid, const struct puffs_cred *pcr, int ioflag)
{
	struct puffs_node *pn = opc;
	struct dtfs_file *df = DTFS_CTOF(opc);
	quad_t xfer, origxfer;
	uint8_t *src, *dest;
	size_t copylen;

	if (pn->pn_va.va_type != VREG)
		return EISDIR;

	xfer = MIN(*resid, df->df_datalen - offset);
	if (xfer < 0)
		return EINVAL;

	dest = buf;
	origxfer = xfer;
	while (xfer > 0) {
		copylen = MIN(xfer, BLOCKLEFT(offset, DTFS_BLOCKSIZE));
		src = df->df_blocks[BLOCKNUM(offset, DTFS_BLOCKSHIFT)]
		    + BLOCKOFF(offset, DTFS_BLOCKSIZE);
		memcpy(dest, src, copylen);
		offset += copylen;
		dest += copylen;
		xfer -= copylen;
	}
	*resid -= origxfer;

	dtfs_updatetimes(pn, 1, 0, 0);

	return 0;
}

/*
 * write operation on the wing
 */
int
dtfs_node_write(struct puffs_usermount *pu, void *opc, uint8_t *buf,
	off_t offset, size_t *resid, const struct puffs_cred *pcr, int ioflag)
{
	struct puffs_node *pn = opc;
	struct dtfs_file *df = DTFS_CTOF(opc);
	uint8_t *src, *dest;
	size_t copylen;

	if (pn->pn_va.va_type != VREG)
		return EISDIR;

	if (ioflag & PUFFS_IO_APPEND)
		offset = pn->pn_va.va_size;

	if (*resid + offset > pn->pn_va.va_size)
		dtfs_setsize(pn, *resid + offset);

	src = buf;
	while (*resid > 0) {
		int i;
		copylen = MIN(*resid, BLOCKLEFT(offset, DTFS_BLOCKSIZE));
		i = BLOCKNUM(offset, DTFS_BLOCKSHIFT);
		dest = df->df_blocks[i]
		    + BLOCKOFF(offset, DTFS_BLOCKSIZE);
		memcpy(dest, src, copylen);
		offset += copylen;
		dest += copylen;
		*resid -= copylen;
	}

	dtfs_updatetimes(pn, 0, 1, 1);

	return 0;
}

int
dtfs_node_pathconf(struct puffs_usermount *pu, puffs_cookie_t opc,
	int name, register_t *retval)
{

	switch (name) {
	case _PC_LINK_MAX:
		*retval = LINK_MAX;
		return 0;
	case _PC_NAME_MAX:
		*retval = NAME_MAX;
		return 0;
	case _PC_PATH_MAX:
		*retval = PATH_MAX;
		return 0;
	case _PC_PIPE_BUF:
		*retval = PIPE_BUF;
		return 0;
	case _PC_CHOWN_RESTRICTED:
		*retval = 1;
		return 0;
	case _PC_NO_TRUNC:
		*retval = 1;
		return 0;
	case _PC_SYNC_IO:
		*retval = 1;
		return 0;
	case _PC_FILESIZEBITS:
		*retval = 43; /* this one goes to 11 */
		return 0;
	case _PC_SYMLINK_MAX:
		*retval = MAXPATHLEN;
		return 0;
	case _PC_2_SYMLINKS:
		*retval = 1;
		return 0;
	default:
		return EINVAL;
	}
}

int
dtfs_node_inactive(struct puffs_usermount *pu, puffs_cookie_t opc)
{
	struct puffs_node *pn = opc;

	if (pn->pn_va.va_nlink == 0)
		puffs_setback(puffs_cc_getcc(pu), PUFFS_SETBACK_NOREF_N1);
	return 0;
}

int
dtfs_node_reclaim(struct puffs_usermount *pu, void *opc)
{
	struct puffs_node *pn = opc;

	if (pn->pn_va.va_nlink == 0)
		dtfs_freenode(pn);

	return 0;
}
