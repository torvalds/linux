/* -*- linux-c -*- --------------------------------------------------------- *
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
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/parser.h>
#include <linux/bitops.h>
#include <linux/magic.h>
#include "autofs_i.h"
#include <linux/module.h>

void autofs_kill_sb(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs_sbi(sb);
	unsigned int n;

	/*
	 * In the event of a failure in get_sb_nodev the superblock
	 * info is not present so nothing else has been setup, so
	 * just call kill_anon_super when we are called from
	 * deactivate_super.
	 */
	if (!sbi)
		goto out_kill_sb;

	if (!sbi->catatonic)
		autofs_catatonic_mode(sbi); /* Free wait queues, close pipe */

	autofs_hash_nuke(sbi);
	for (n = 0; n < AUTOFS_MAX_SYMLINKS; n++) {
		if (test_bit(n, sbi->symlink_bitmap))
			kfree(sbi->symlink[n].data);
	}

	kfree(sb->s_fs_info);

out_kill_sb:
	DPRINTK(("autofs: shutting down\n"));
	kill_anon_super(sb);
}

static void autofs_read_inode(struct inode *inode);

static const struct super_operations autofs_sops = {
	.read_inode	= autofs_read_inode,
	.statfs		= simple_statfs,
};

enum {Opt_err, Opt_fd, Opt_uid, Opt_gid, Opt_pgrp, Opt_minproto, Opt_maxproto};

static match_table_t autofs_tokens = {
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

	*minproto = *maxproto = AUTOFS_PROTO_VERSION;

	*pipefd = -1;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, autofs_tokens, args);
		switch (token) {
		case Opt_fd:
			if (match_int(&args[0], &option))
				return 1;
			*pipefd = option;
			break;
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 1;
			*uid = option;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 1;
			*gid = option;
			break;
		case Opt_pgrp:
			if (match_int(&args[0], &option))
				return 1;
			*pgrp = option;
			break;
		case Opt_minproto:
			if (match_int(&args[0], &option))
				return 1;
			*minproto = option;
			break;
		case Opt_maxproto:
			if (match_int(&args[0], &option))
				return 1;
			*maxproto = option;
			break;
		default:
			return 1;
		}
	}
	return (*pipefd < 0);
}

int autofs_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct file * pipe;
	int pipefd;
	struct autofs_sb_info *sbi;
	int minproto, maxproto;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		goto fail_unlock;
	DPRINTK(("autofs: starting up, sbi = %p\n",sbi));

	s->s_fs_info = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->pipe = NULL;
	sbi->catatonic = 1;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = process_group(current);
	autofs_initialize_hash(&sbi->dirhash);
	sbi->queues = NULL;
	memset(sbi->symlink_bitmap, 0, sizeof(long)*AUTOFS_SYMLINK_BITMAP_LEN);
	sbi->next_dir_ino = AUTOFS_FIRST_DIR_INO;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs_sops;
	s->s_time_gran = 1;
	sbi->sb = s;

	root_inode = iget(s, AUTOFS_ROOT_INO);
	root = d_alloc_root(root_inode);
	pipe = NULL;

	if (!root)
		goto fail_iput;

	/* Can this call block?  - WTF cares? s is locked. */
	if (parse_options(data, &pipefd, &root_inode->i_uid,
				&root_inode->i_gid, &sbi->oz_pgrp, &minproto,
				&maxproto)) {
		printk("autofs: called with bogus options\n");
		goto fail_dput;
	}

	/* Couldn't this be tested earlier? */
	if (minproto > AUTOFS_PROTO_VERSION ||
	     maxproto < AUTOFS_PROTO_VERSION) {
		printk("autofs: kernel does not match daemon version\n");
		goto fail_dput;
	}

	DPRINTK(("autofs: pipe fd = %d, pgrp = %u\n", pipefd, sbi->oz_pgrp));
	pipe = fget(pipefd);
	
	if (!pipe) {
		printk("autofs: could not open pipe file descriptor\n");
		goto fail_dput;
	}
	if (!pipe->f_op || !pipe->f_op->write)
		goto fail_fput;
	sbi->pipe = pipe;
	sbi->catatonic = 0;

	/*
	 * Success! Install the root dentry now to indicate completion.
	 */
	s->s_root = root;
	return 0;

fail_fput:
	printk("autofs: pipe file descriptor does not contain proper ops\n");
	fput(pipe);
fail_dput:
	dput(root);
	goto fail_free;
fail_iput:
	printk("autofs: get root dentry failed\n");
	iput(root_inode);
fail_free:
	kfree(sbi);
	s->s_fs_info = NULL;
fail_unlock:
	return -EINVAL;
}

static void autofs_read_inode(struct inode *inode)
{
	ino_t ino = inode->i_ino;
	unsigned int n;
	struct autofs_sb_info *sbi = autofs_sbi(inode->i_sb);

	/* Initialize to the default case (stub directory) */

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
	inode->i_nlink = 2;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;

	if (ino == AUTOFS_ROOT_INO) {
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
		inode->i_op = &autofs_root_inode_operations;
		inode->i_fop = &autofs_root_operations;
		inode->i_uid = inode->i_gid = 0; /* Changed in read_super */
		return;
	} 
	
	inode->i_uid = inode->i_sb->s_root->d_inode->i_uid;
	inode->i_gid = inode->i_sb->s_root->d_inode->i_gid;
	
	if (ino >= AUTOFS_FIRST_SYMLINK && ino < AUTOFS_FIRST_DIR_INO) {
		/* Symlink inode - should be in symlink list */
		struct autofs_symlink *sl;

		n = ino - AUTOFS_FIRST_SYMLINK;
		if (n >= AUTOFS_MAX_SYMLINKS || !test_bit(n,sbi->symlink_bitmap)) {
			printk("autofs: Looking for bad symlink inode %u\n", (unsigned int) ino);
			return;
		}
		
		inode->i_op = &autofs_symlink_inode_operations;
		sl = &sbi->symlink[n];
		inode->i_private = sl;
		inode->i_mode = S_IFLNK | S_IRWXUGO;
		inode->i_mtime.tv_sec = inode->i_ctime.tv_sec = sl->mtime;
		inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;
		inode->i_size = sl->len;
		inode->i_nlink = 1;
	}
}
