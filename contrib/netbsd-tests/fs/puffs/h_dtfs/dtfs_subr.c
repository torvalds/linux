/*	$NetBSD: dtfs_subr.c,v 1.4 2013/10/19 17:45:00 christos Exp $	*/

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
#include <sys/time.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <puffs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "dtfs.h"

void
dtfs_baseattrs(struct vattr *vap, enum vtype type, ino_t id)
{
	struct timeval tv;
	struct timespec ts;

	gettimeofday(&tv, NULL);
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	vap->va_type = type;
	if (type == VDIR) {
		vap->va_mode = 0777;
		vap->va_nlink = 1;	/* n + 1 after adding dent */
	} else {
		vap->va_mode = 0666;
		vap->va_nlink = 0;	/* n + 1 */
	}
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_fileid = id;
	vap->va_size = 0;
	vap->va_blocksize = getpagesize();
	vap->va_gen = random();
	vap->va_flags = 0;
	vap->va_rdev = PUFFS_VNOVAL;
	vap->va_bytes = 0;
	vap->va_filerev = 1;
	vap->va_vaflags = 0;

	vap->va_atime = vap->va_mtime = vap->va_ctime = vap->va_birthtime = ts;
}

/*
 * Well, as you can probably see, this interface has the slight problem
 * of assuming file creation will always be succesful, or at least not
 * giving a reason for the failure.  Be sure to do better when you
 * implement your own fs.
 */
struct puffs_node *
dtfs_genfile(struct puffs_node *dir, const struct puffs_cn *pcn,
	enum vtype type)
{
	struct dtfs_file *dff;
	struct dtfs_dirent *dfd;
	struct dtfs_mount *dtm;
	struct puffs_node *newpn;
	uid_t uid;
	int rv;

	assert(dir->pn_va.va_type == VDIR);
	assert(dir->pn_mnt != NULL);

	uid = 0;
	rv = puffs_cred_getuid(pcn->pcn_cred, &uid);
	assert(rv == 0);

	if (type == VDIR) {
		dff = dtfs_newdir();
		dff->df_dotdot = dir;
	} else
		dff = dtfs_newfile();

	dtm = puffs_pn_getmntspecific(dir);
	newpn = puffs_pn_new(dir->pn_mnt, dff);
	if (newpn == NULL)
		errx(1, "getnewpnode");
	dtfs_baseattrs(&newpn->pn_va, type, dtm->dtm_nextfileid++);

	dfd = emalloc(sizeof(struct dtfs_dirent));
	dfd->dfd_node = newpn;
	dfd->dfd_name = estrndup(pcn->pcn_name, pcn->pcn_namelen);
	dfd->dfd_namelen = strlen(dfd->dfd_name);
	dfd->dfd_parent = dir;
	dtfs_adddent(dir, dfd);

	newpn->pn_va.va_uid = uid;
	newpn->pn_va.va_gid = dir->pn_va.va_gid;

	return newpn;
}

struct dtfs_file *
dtfs_newdir()
{
	struct dtfs_file *dff;

	dff = emalloc(sizeof(struct dtfs_file));
	memset(dff, 0, sizeof(struct dtfs_file));
	LIST_INIT(&dff->df_dirents);

	return dff;
}

struct dtfs_file *
dtfs_newfile()
{
	struct dtfs_file *dff;

	dff = emalloc(sizeof(struct dtfs_file));
	memset(dff, 0, sizeof(struct dtfs_file));

	return dff;
}

struct dtfs_dirent *
dtfs_dirgetnth(struct dtfs_file *searchdir, int n)
{
	struct dtfs_dirent *dirent;
	int i;

	i = 0;
	LIST_FOREACH(dirent, &searchdir->df_dirents, dfd_entries) {
		if (i == n)
			return dirent;
		i++;
	}

	return NULL;
}

struct dtfs_dirent *
dtfs_dirgetbyname(struct dtfs_file *searchdir, const char *fname, size_t fnlen)
{
	struct dtfs_dirent *dirent;

	LIST_FOREACH(dirent, &searchdir->df_dirents, dfd_entries)
		if (dirent->dfd_namelen == fnlen
		    && strncmp(dirent->dfd_name, fname, fnlen) == 0)
			return dirent;

	return NULL;
}

/*
 * common nuke, kill dirent from parent node
 */
void
dtfs_nukenode(struct puffs_node *nukeme, struct puffs_node *pn_parent,
	const char *fname, size_t fnlen)
{
	struct dtfs_dirent *dfd;
	struct dtfs_mount *dtm;

	assert(pn_parent->pn_va.va_type == VDIR);

	dfd = dtfs_dirgetbyname(DTFS_PTOF(pn_parent), fname, fnlen);
	assert(dfd);

	dtm = puffs_pn_getmntspecific(nukeme);
	dtm->dtm_nfiles--;
	assert(dtm->dtm_nfiles >= 1);

	dtfs_removedent(pn_parent, dfd);
	free(dfd);
}

/* free lingering information */
void
dtfs_freenode(struct puffs_node *pn)
{
	struct dtfs_file *df = DTFS_PTOF(pn);
	struct dtfs_mount *dtm;
	int i;

	assert(pn->pn_va.va_nlink == 0);
	dtm = puffs_pn_getmntspecific(pn);

	switch (pn->pn_va.va_type) {
	case VREG:
		assert(dtm->dtm_fsizes >= pn->pn_va.va_size);
		dtm->dtm_fsizes -= pn->pn_va.va_size;
		for (i = 0; i < BLOCKNUM(df->df_datalen, DTFS_BLOCKSHIFT); i++)
			free(df->df_blocks[i]);
		if (df->df_datalen > i << DTFS_BLOCKSHIFT)
			free(df->df_blocks[i]);
		break;
	case VLNK:
		free(df->df_linktarget);
		break;
	case VCHR:
	case VBLK:
	case VDIR:
	case VSOCK:
	case VFIFO:
		break;
	default:
		assert(0);
		break;
	}

	free(df);
	puffs_pn_put(pn);
}

void
dtfs_setsize(struct puffs_node *pn, off_t newsize)
{
	struct dtfs_file *df = DTFS_PTOF(pn);
	struct dtfs_mount *dtm;
	size_t newblocks;
	int needalloc, shrinks;
	int i;

	needalloc = newsize > ROUNDUP(df->df_datalen, DTFS_BLOCKSIZE);
	shrinks = newsize < pn->pn_va.va_size;

	if (needalloc || shrinks) {
		newblocks = BLOCKNUM(newsize, DTFS_BLOCKSHIFT) + 1;

		if (shrinks)
			for (i = newblocks; i < df->df_numblocks; i++)
				free(df->df_blocks[i]);

		df->df_blocks = erealloc(df->df_blocks,
		    newblocks * sizeof(uint8_t *));
		/*
		 * if extended, set storage to zero
		 * to match correct behaviour
		 */ 
		if (!shrinks) {
			for (i = df->df_numblocks; i < newblocks; i++) {
				df->df_blocks[i] = emalloc(DTFS_BLOCKSIZE);
				memset(df->df_blocks[i], 0, DTFS_BLOCKSIZE);
			}
		}

		df->df_datalen = newsize;
		df->df_numblocks = newblocks;
	}

	dtm = puffs_pn_getmntspecific(pn);
	if (!shrinks) {
		dtm->dtm_fsizes += newsize - pn->pn_va.va_size;
	} else {
		dtm->dtm_fsizes -= pn->pn_va.va_size - newsize;
	}

	pn->pn_va.va_size = newsize;
	pn->pn_va.va_bytes = BLOCKNUM(newsize,DTFS_BLOCKSHIFT)>>DTFS_BLOCKSHIFT;
}

/* add & bump link count */
void
dtfs_adddent(struct puffs_node *pn_dir, struct dtfs_dirent *dent)
{
	struct dtfs_file *dir = DTFS_PTOF(pn_dir);
	struct puffs_node *pn_file = dent->dfd_node;
	struct dtfs_file *file = DTFS_PTOF(pn_file);
	struct dtfs_mount *dtm;

	assert(pn_dir->pn_va.va_type == VDIR);
	LIST_INSERT_HEAD(&dir->df_dirents, dent, dfd_entries);
	pn_file->pn_va.va_nlink++;

	dtm = puffs_pn_getmntspecific(pn_file);
	dtm->dtm_nfiles++;

	dent->dfd_parent = pn_dir;
	if (dent->dfd_node->pn_va.va_type == VDIR) {
		file->df_dotdot = pn_dir;
		pn_dir->pn_va.va_nlink++;
	}

	dtfs_updatetimes(pn_dir, 0, 1, 1);
}

/* remove & lower link count */
void
dtfs_removedent(struct puffs_node *pn_dir, struct dtfs_dirent *dent)
{
	struct puffs_node *pn_file = dent->dfd_node;

	assert(pn_dir->pn_va.va_type == VDIR);
	LIST_REMOVE(dent, dfd_entries);
	if (pn_file->pn_va.va_type == VDIR) {
		struct dtfs_file *df = DTFS_PTOF(pn_file);

		pn_dir->pn_va.va_nlink--;
		df->df_dotdot = NULL;
	}
	pn_file->pn_va.va_nlink--;
	assert(pn_dir->pn_va.va_nlink >= 2);

	dtfs_updatetimes(pn_dir, 0, 1, 1);
}

void
dtfs_updatetimes(struct puffs_node *pn, int doatime, int doctime, int domtime)
{
	struct timeval tv;
	struct timespec ts;

	gettimeofday(&tv, NULL);
	TIMEVAL_TO_TIMESPEC(&tv, &ts);

	if (doatime)
		pn->pn_va.va_atime = ts;
	if (doctime)
		pn->pn_va.va_ctime = ts;
	if (domtime)
		pn->pn_va.va_mtime = ts;
}

bool
dtfs_isunder(struct puffs_node *pn, struct puffs_node *pn_parent)
{
	struct dtfs_file *df;

	while (pn) {
		if (pn == pn_parent)
			return true;
		df = DTFS_CTOF(pn);
		pn = df->df_dotdot;
	}

	return false;
}
