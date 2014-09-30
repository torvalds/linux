/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/inode.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 2005-2006 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/parser.h>
#include <linux/bitops.h>
#include <linux/magic.h>
#include "autofs_i.h"
#include <linux/module.h>

struct autofs_info *autofs4_new_ino(struct autofs_sb_info *sbi)
{
	struct autofs_info *ino = kzalloc(sizeof(*ino), GFP_KERNEL);
	if (ino) {
		INIT_LIST_HEAD(&ino->active);
		INIT_LIST_HEAD(&ino->expiring);
		ino->last_used = jiffies;
		ino->sbi = sbi;
	}
	return ino;
}

void autofs4_clean_ino(struct autofs_info *ino)
{
	ino->uid = GLOBAL_ROOT_UID;
	ino->gid = GLOBAL_ROOT_GID;
	ino->last_used = jiffies;
}

void autofs4_free_ino(struct autofs_info *ino)
{
	kfree(ino);
}

void autofs4_kill_sb(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs4_sbi(sb);

	/*
	 * In the event of a failure in get_sb_nodev the superblock
	 * info is not present so nothing else has been setup, so
	 * just call kill_anon_super when we are called from
	 * deactivate_super.
	 */
	if (sbi) {
		/* Free wait queues, close pipe */
		autofs4_catatonic_mode(sbi);
		put_pid(sbi->oz_pgrp);
	}

	DPRINTK("shutting down");
	kill_litter_super(sb);
	if (sbi)
		kfree_rcu(sbi, rcu);
}

static int autofs4_show_options(struct seq_file *m, struct dentry *root)
{
	struct autofs_sb_info *sbi = autofs4_sbi(root->d_sb);
	struct inode *root_inode = root->d_sb->s_root->d_inode;

	if (!sbi)
		return 0;

	seq_printf(m, ",fd=%d", sbi->pipefd);
	if (!uid_eq(root_inode->i_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			from_kuid_munged(&init_user_ns, root_inode->i_uid));
	if (!gid_eq(root_inode->i_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			from_kgid_munged(&init_user_ns, root_inode->i_gid));
	seq_printf(m, ",pgrp=%d", pid_vnr(sbi->oz_pgrp));
	seq_printf(m, ",timeout=%lu", sbi->exp_timeout/HZ);
	seq_printf(m, ",minproto=%d", sbi->min_proto);
	seq_printf(m, ",maxproto=%d", sbi->max_proto);

	if (autofs_type_offset(sbi->type))
		seq_printf(m, ",offset");
	else if (autofs_type_direct(sbi->type))
		seq_printf(m, ",direct");
	else
		seq_printf(m, ",indirect");

	return 0;
}

static void autofs4_evict_inode(struct inode *inode)
{
	clear_inode(inode);
	kfree(inode->i_private);
}

static const struct super_operations autofs4_sops = {
	.statfs		= simple_statfs,
	.show_options	= autofs4_show_options,
	.evict_inode	= autofs4_evict_inode,
};

enum {Opt_err, Opt_fd, Opt_uid, Opt_gid, Opt_pgrp, Opt_minproto, Opt_maxproto,
	Opt_indirect, Opt_direct, Opt_offset};

static const match_table_t tokens = {
	{Opt_fd, "fd=%u"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_pgrp, "pgrp=%u"},
	{Opt_minproto, "minproto=%u"},
	{Opt_maxproto, "maxproto=%u"},
	{Opt_indirect, "indirect"},
	{Opt_direct, "direct"},
	{Opt_offset, "offset"},
	{Opt_err, NULL}
};

static int parse_options(char *options, int *pipefd, kuid_t *uid, kgid_t *gid,
			 int *pgrp, bool *pgrp_set, unsigned int *type,
			 int *minproto, int *maxproto)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;

	*uid = current_uid();
	*gid = current_gid();

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
			*uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(*uid))
				return 1;
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return 1;
			*gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(*gid))
				return 1;
			break;
		case Opt_pgrp:
			if (match_int(args, &option))
				return 1;
			*pgrp = option;
			*pgrp_set = true;
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
		case Opt_indirect:
			set_autofs_type_indirect(type);
			break;
		case Opt_direct:
			set_autofs_type_direct(type);
			break;
		case Opt_offset:
			set_autofs_type_offset(type);
			break;
		default:
			return 1;
		}
	}
	return (*pipefd < 0);
}

int autofs4_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * root_inode;
	struct dentry * root;
	struct file * pipe;
	int pipefd;
	struct autofs_sb_info *sbi;
	struct autofs_info *ino;
	int pgrp = 0;
	bool pgrp_set = false;
	int ret = -EINVAL;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	DPRINTK("starting up, sbi = %p",sbi);

	s->s_fs_info = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->pipefd = -1;
	sbi->pipe = NULL;
	sbi->catatonic = 1;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = NULL;
	sbi->sb = s;
	sbi->version = 0;
	sbi->sub_version = 0;
	set_autofs_type_indirect(&sbi->type);
	sbi->min_proto = 0;
	sbi->max_proto = 0;
	mutex_init(&sbi->wq_mutex);
	mutex_init(&sbi->pipe_mutex);
	spin_lock_init(&sbi->fs_lock);
	sbi->queues = NULL;
	spin_lock_init(&sbi->lookup_lock);
	INIT_LIST_HEAD(&sbi->active_list);
	INIT_LIST_HEAD(&sbi->expiring_list);
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs4_sops;
	s->s_d_op = &autofs4_dentry_operations;
	s->s_time_gran = 1;

	/*
	 * Get the root inode and dentry, but defer checking for errors.
	 */
	ino = autofs4_new_ino(sbi);
	if (!ino) {
		ret = -ENOMEM;
		goto fail_free;
	}
	root_inode = autofs4_get_inode(s, S_IFDIR | 0755);
	root = d_make_root(root_inode);
	if (!root)
		goto fail_ino;
	pipe = NULL;

	root->d_fsdata = ino;

	/* Can this call block? */
	if (parse_options(data, &pipefd, &root_inode->i_uid, &root_inode->i_gid,
			  &pgrp, &pgrp_set, &sbi->type, &sbi->min_proto,
			  &sbi->max_proto)) {
		printk("autofs: called with bogus options\n");
		goto fail_dput;
	}

	if (pgrp_set) {
		sbi->oz_pgrp = find_get_pid(pgrp);
		if (!sbi->oz_pgrp) {
			pr_warn("autofs: could not find process group %d\n",
				pgrp);
			goto fail_dput;
		}
	} else {
		sbi->oz_pgrp = get_task_pid(current, PIDTYPE_PGID);
	}

	if (autofs_type_trigger(sbi->type))
		__managed_dentry_set_managed(root);

	root_inode->i_fop = &autofs4_root_operations;
	root_inode->i_op = &autofs4_dir_inode_operations;

	/* Couldn't this be tested earlier? */
	if (sbi->max_proto < AUTOFS_MIN_PROTO_VERSION ||
	    sbi->min_proto > AUTOFS_MAX_PROTO_VERSION) {
		printk("autofs: kernel does not match daemon version "
		       "daemon (%d, %d) kernel (%d, %d)\n",
			sbi->min_proto, sbi->max_proto,
			AUTOFS_MIN_PROTO_VERSION, AUTOFS_MAX_PROTO_VERSION);
		goto fail_dput;
	}

	/* Establish highest kernel protocol version */
	if (sbi->max_proto > AUTOFS_MAX_PROTO_VERSION)
		sbi->version = AUTOFS_MAX_PROTO_VERSION;
	else
		sbi->version = sbi->max_proto;
	sbi->sub_version = AUTOFS_PROTO_SUBVERSION;

	DPRINTK("pipe fd = %d, pgrp = %u", pipefd, pid_nr(sbi->oz_pgrp));
	pipe = fget(pipefd);

	if (!pipe) {
		printk("autofs: could not open pipe file descriptor\n");
		goto fail_dput;
	}
	ret = autofs_prepare_pipe(pipe);
	if (ret < 0)
		goto fail_fput;
	sbi->pipe = pipe;
	sbi->pipefd = pipefd;
	sbi->catatonic = 0;

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
fail_ino:
	kfree(ino);
fail_free:
	put_pid(sbi->oz_pgrp);
	kfree(sbi);
	s->s_fs_info = NULL;
	return ret;
}

struct inode *autofs4_get_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode = new_inode(sb);

	if (inode == NULL)
		return NULL;

	inode->i_mode = mode;
	if (sb->s_root) {
		inode->i_uid = sb->s_root->d_inode->i_uid;
		inode->i_gid = sb->s_root->d_inode->i_gid;
	}
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_ino = get_next_ino();

	if (S_ISDIR(mode)) {
		set_nlink(inode, 2);
		inode->i_op = &autofs4_dir_inode_operations;
		inode->i_fop = &autofs4_dir_operations;
	} else if (S_ISLNK(mode)) {
		inode->i_op = &autofs4_symlink_inode_operations;
	}

	return inode;
}
