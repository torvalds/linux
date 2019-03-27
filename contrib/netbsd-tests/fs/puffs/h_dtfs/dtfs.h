/*	$NetBSD: dtfs.h,v 1.2 2010/07/14 13:09:52 pooka Exp $	*/

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

#ifndef DTFS_H_
#define DTFS_H_

#include <sys/types.h>

#include <puffs.h>

PUFFSOP_PROTOS(dtfs);
int	dtfs_domount(struct puffs_usermount *, const char *);

#define DTFS_BLOCKSHIFT	(12)
#define DTFS_BLOCKSIZE	(1<<DTFS_BLOCKSHIFT)

#define ROUNDUP(a,b) ((a) & ((b)-1))
#define BLOCKNUM(a,b) (((a) & ~((1<<(b))-1)) >> (b))

struct dtfs_fid;
struct dtfs_mount {
	ino_t		dtm_nextfileid;	/* running number for file id	*/

	size_t		dtm_fsizes;	/* sum of file sizes in bytes	*/
	fsfilcnt_t	dtm_nfiles;	/* number of files		*/

	LIST_HEAD(, dtfs_poll) dtm_pollent;
	int		dtm_needwakeup;
	vm_prot_t	dtm_allowprot;
};

struct dtfs_file {
	union {
		struct {
			uint8_t **blocks;
			size_t numblocks;
			size_t datalen;
		} reg;
		struct {
			struct puffs_node *dotdot;
			LIST_HEAD(, dtfs_dirent) dirents;
		} dir; 
		struct {
			char *target;
		} link;
	} u;
#define df_blocks u.reg.blocks
#define df_numblocks u.reg.numblocks
#define df_datalen u.reg.datalen
#define df_dotdot u.dir.dotdot
#define df_dirents u.dir.dirents
#define df_linktarget u.link.target
};

struct dtfs_dirent {
	struct puffs_node *dfd_node;
	struct puffs_node *dfd_parent;
	char *dfd_name;
	size_t dfd_namelen;

	LIST_ENTRY(dtfs_dirent) dfd_entries;
};

struct dtfs_fid {
	struct puffs_node	*dfid_addr;

	/* best^Wsome-effort extra sanity check */
	ino_t			dfid_fileid;
	u_long			dfid_gen;
};
#define DTFS_FIDSIZE (sizeof(struct dtfs_fid))

struct dtfs_poll {
	struct puffs_cc *dp_pcc;
	LIST_ENTRY(dtfs_poll) dp_entries;
};

struct puffs_node *	dtfs_genfile(struct puffs_node *,
				     const struct puffs_cn *, enum vtype);
struct dtfs_file *	dtfs_newdir(void);
struct dtfs_file *	dtfs_newfile(void);
struct dtfs_dirent *	dtfs_dirgetnth(struct dtfs_file *, int);
struct dtfs_dirent *	dtfs_dirgetbyname(struct dtfs_file *,
					  const char *, size_t);

void			dtfs_nukenode(struct puffs_node *, struct puffs_node *,
				      const char *, size_t);
void			dtfs_freenode(struct puffs_node *);
void			dtfs_setsize(struct puffs_node *, off_t);

void	dtfs_adddent(struct puffs_node *, struct dtfs_dirent *);
void	dtfs_removedent(struct puffs_node *, struct dtfs_dirent *);

void	dtfs_baseattrs(struct vattr *, enum vtype, ino_t);
void	dtfs_updatetimes(struct puffs_node *, int, int, int);

bool	dtfs_isunder(struct puffs_node *, struct puffs_node *);


#define DTFS_CTOF(a) ((struct dtfs_file *)(((struct puffs_node *)a)->pn_data))
#define DTFS_PTOF(a) ((struct dtfs_file *)(a->pn_data))

#endif /* DTFS_H_ */
