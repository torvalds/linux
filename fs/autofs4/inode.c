/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/inode.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/parser.h>
#include <linux/bitops.h>
#include <linux/smp_lock.h>
#include "autofs_i.h"
#include <linux/module.h>

static void ino_lnkfree(struct autofs_info *ino)
{
	kfree(ino->u.symlink);
	ino->u.symlink = NULL;
}

struct autofs_info *autofs4_init_ino(struct autofs_info *ino,
				     struct autofs_sb_info *sbi, mode_t mode)
{
	int reinit = 1;

	if (ino == NULL) {
		reinit = 0;
		ino = kmalloc(sizeof(*ino), GFP_KERNEL);
	}

	if (ino == NULL)
		return NULL;

	ino->flags = 0;
	ino->mode = mode;
	ino->inode = NULL;
	ino->dentry = NULL;
	ino->size = 0;

	ino->last_used = jiffies;

	ino->sbi = sbi;

	if (reinit && ino->free)
		(ino->free)(ino);

	memset(&ino->u, 0, sizeof(ino->u));

	ino->free = NULL;

	if (S_ISLNK(mode))
		ino->free = ino_lnkfree;

	return ino;
}

void autofs4_free_ino(struct autofs_info *ino)
{
	if (ino->dentry) {
		ino->dentry->d_fsdata = NULL;
		if (ino->dentry->d_inode)
			dput(ino->dentry);
		ino->dentry = NULL;
	}
	if (ino->free)
		(ino->free)(ino);
	kfree(ino);
}

/*
 * Deal with the infamous "Busy inodes after umount ..." message.
 *
 * Clean up the dentry tree. This happens with autofs if the user
 * space program goes away due to a SIGKILL, SIGSEGV etc.
 */
static void autofs4_force_release(struct autofs_sb_info *sbi)
{
	struct dentry *this_parent = sbi->root;
	struct list_head *next;

	spin_lock(&dcache_lock);
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct dentry *dentry = list_entry(next, struct dentry, d_u.d_child);

		/* Negative dentry - don`t care */
		if (!simple_positive(dentry)) {
			next = next->next;
			continue;
		}

		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
			goto repeat;
		}

		next = next->next;
		spin_unlock(&dcache_lock);

		DPRINTK("dentry %p %.*s",
			dentry, (int)dentry->d_name.len, dentry->d_name.name);

		dput(dentry);
		spin_lock(&dcache_lock);
	}

	if (this_parent != sbi->root) {
		struct dentry *dentry = this_parent;

		next = this_parent->d_u.d_child.next;
		this_parent = this_parent->d_parent;
		spin_unlock(&dcache_lock);
		DPRINTK("parent dentry %p %.*s",
			dentry, (int)dentry->d_name.len, dentry->d_name.name);
		dput(dentry);
		spin_lock(&dcache_lock);
		goto resume;
	}
	spin_unlock(&dcache_lock);

	dput(sbi->root);
	sbi->root = NULL;
	shrink_dcache_sb(sbi->sb);

	return;
}

static void autofs4_put_super(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs4_sbi(sb);

	sb->s_fs_info = NULL;

	if ( !sbi->catatonic )
		autofs4_catatonic_mode(sbi); /* Free wait queues, close pipe */

	/* Clean up and release dangling references */
	if (sbi)
		autofs4_force_release(sbi);

	kfree(sbi);

	DPRINTK("shutting down");
}

static struct super_operations autofs4_sops = {
	.put_super	= autofs4_put_super,
	.statfs		= simple_statfs,
};

enum {Opt_err, Opt_fd, Opt_uid, Opt_gid, Opt_pgrp, Opt_minproto, Opt_maxproto};

static match_table_t tokens = {
	{Opt_fd, "fd=%u"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_pgrp, "pgrp=%u"},
	{Opt_minproto, "minproto=%u"},
	{Opt_maxproto, "maxproto=%u"},
	{Opt_err, NULL}
};

static int parse_options(char *options, int *pipefd, uid_t *uid, gid_t *gid,
			 pid_t *pgrp, int *minproto, int *maxproto)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;

	*uid = current->uid;
	*gid = current->gid;
	*pgrp = process_group(current);

	*minproto = AUTOFS_MIN_PROTO_VERSION;
	*maxproto = AUTOFS_MAX_PROTO_VERSION;

	*pipefd = -1;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_fd:
			if (match_int(args, pipefd))
				return 1;
			break;
		case Opt_uid:
			if (match_int(args, &option))
				return 1;
			*uid = option;
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return 1;
			*gid = option;
			break;
		case Opt_pgrp:
			if (match_int(args, &option))
				return 1;
			*pgrp = option;
			break;
		case Opt_minproto:
			if (match_int(args, &option))
				return 1;
			*minproto = option;
			break;
		case Opt_maxproto:
			if (match_int(args, &option))
				return 1;
			*maxproto = option;
			break;
		default:
			return 1;
		}
	}
	return (*pipefd < 0);
}

static struct autofs_info *autofs4_mkroot(struct autofs_sb_info *sbi)
{
	struct autofs_info *ino;

	ino = autofs4_init_ino(NULL, sbi, S_IFDIR | 0755);
	if (!ino)
		return NULL;

	return ino;
}

int autofs4_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct file * pipe;
	int pipefd;
	struct autofs_sb_info *sbi;
	struct autofs_info *ino;
	int minproto, maxproto;

	sbi = (struct autofs_sb_info *) kmalloc(sizeof(*sbi), GFP_KERNEL);
	if ( !sbi )
		goto fail_unlock;
	DPRINTK("starting up, sbi = %p",sbi);

	memset(sbi, 0, sizeof(*sbi));

	s->s_fs_info = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->root = NULL;
	sbi->catatonic = 0;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = process_group(current);
	sbi->sb = s;
	sbi->version = 0;
	sbi->sub_version = 0;
	mutex_init(&sbi->wq_mutex);
	spin_lock_init(&sbi->fs_lock);
	sbi->queues = NULL;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs4_sops;
	s->s_time_gran = 1;

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	ino = autofs4_mkroot(sbi);
	if (!ino)
		goto fail_free;
	root_inode = autofs4_get_inode(s, ino);
	kfree(ino);
	if (!root_inode)
		goto fail_free;

	root_inode->i_op = &autofs4_root_inode_operations;
	root_inode->i_fop = &autofs4_root_operations;
	root = d_alloc_root(root_inode);
	pipe = NULL;

	if (!root)
		goto fail_iput;

	/* Can this call block? */
	if (parse_options(data, &pipefd,
			  &root_inode->i_uid, &root_inode->i_gid,
			  &sbi->oz_pgrp,
			  &minproto, &maxproto)) {
		printk("autofs: called with bogus options\n");
		goto fail_dput;
	}

	/* Couldn't this be tested earlier? */
	if (maxproto < AUTOFS_MIN_PROTO_VERSION ||
	    minproto > AUTOFS_MAX_PROTO_VERSION) {
		printk("autofs: kernel does not match daemon version "
		       "daemon (%d, %d) kernel (%d, %d)\n",
			minproto, maxproto,
			AUTOFS_MIN_PROTO_VERSION, AUTOFS_MAX_PROTO_VERSION);
		goto fail_dput;
	}

	sbi->version = maxproto > AUTOFS_MAX_PROTO_VERSION ? AUTOFS_MAX_PROTO_VERSION : maxproto;
	sbi->sub_version = AUTOFS_PROTO_SUBVERSION;

	DPRINTK("pipe fd = %d, pgrp = %u", pipefd, sbi->oz_pgrp);
	pipe = fget(pipefd);
	
	if ( !pipe ) {
		printk("autofs: could not open pipe file descriptor\n");
		goto fail_dput;
	}
	if ( !pipe->f_op || !pipe->f_op->write )
		goto fail_fput;
	sbi->pipe = pipe;

	/*
	 * Take a reference to the root dentry so we get a chance to
	 * clean up the dentry tree on umount.
	 * See autofs4_force_release.
	 */
	sbi->root = dget(root);

	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	s->s_root = root;
	return 0;
	
	/*
	 * Failure ... clean up.
	 */
fail_fput:
	printk("autofs: pipe file descriptor does not contain proper ops\n");
	fput(pipe);
	/* fall through */
fail_dput:
	dput(root);
	goto fail_free;
fail_iput:
	printk("autofs: get root dentry failed\n");
	iput(root_inode);
fail_free:
	kfree(sbi);
fail_unlock:
	return -EINVAL;
}

struct inode *autofs4_get_inode(struct super_block *sb,
				struct autofs_info *inf)
{
	struct inode *inode = new_inode(sb);

	if (inode == NULL)
		return NULL;

	inf->inode = inode;
	inode->i_mode = inf->mode;
	if (sb->s_root) {
		inode->i_uid = sb->s_root->d_inode->i_uid;
		inode->i_gid = sb->s_root->d_inode->i_gid;
	} else {
		inode->i_uid = 0;
		inode->i_gid = 0;
	}
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	if (S_ISDIR(inf->mode)) {
		inode->i_nlink = 2;
		inode->i_op = &autofs4_dir_inode_operations;
		inode->i_fop = &autofs4_dir_operations;
	} else if (S_ISLNK(inf->mode)) {
		inode->i_size = inf->size;
		inode->i_op = &autofs4_symlink_inode_operations;
	}

	return inode;
}
