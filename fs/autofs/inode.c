// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 * Copyright 2005-2006 Ian Kent <raven@themaw.net>
 */

#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/parser.h>

#include "autofs_i.h"

struct autofs_info *autofs_new_iyes(struct autofs_sb_info *sbi)
{
	struct autofs_info *iyes;

	iyes = kzalloc(sizeof(*iyes), GFP_KERNEL);
	if (iyes) {
		INIT_LIST_HEAD(&iyes->active);
		INIT_LIST_HEAD(&iyes->expiring);
		iyes->last_used = jiffies;
		iyes->sbi = sbi;
	}
	return iyes;
}

void autofs_clean_iyes(struct autofs_info *iyes)
{
	iyes->uid = GLOBAL_ROOT_UID;
	iyes->gid = GLOBAL_ROOT_GID;
	iyes->last_used = jiffies;
}

void autofs_free_iyes(struct autofs_info *iyes)
{
	kfree_rcu(iyes, rcu);
}

void autofs_kill_sb(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs_sbi(sb);

	/*
	 * In the event of a failure in get_sb_yesdev the superblock
	 * info is yest present so yesthing else has been setup, so
	 * just call kill_ayesn_super when we are called from
	 * deactivate_super.
	 */
	if (sbi) {
		/* Free wait queues, close pipe */
		autofs_catatonic_mode(sbi);
		put_pid(sbi->oz_pgrp);
	}

	pr_debug("shutting down\n");
	kill_litter_super(sb);
	if (sbi)
		kfree_rcu(sbi, rcu);
}

static int autofs_show_options(struct seq_file *m, struct dentry *root)
{
	struct autofs_sb_info *sbi = autofs_sbi(root->d_sb);
	struct iyesde *root_iyesde = d_iyesde(root->d_sb->s_root);

	if (!sbi)
		return 0;

	seq_printf(m, ",fd=%d", sbi->pipefd);
	if (!uid_eq(root_iyesde->i_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			from_kuid_munged(&init_user_ns, root_iyesde->i_uid));
	if (!gid_eq(root_iyesde->i_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			from_kgid_munged(&init_user_ns, root_iyesde->i_gid));
	seq_printf(m, ",pgrp=%d", pid_vnr(sbi->oz_pgrp));
	seq_printf(m, ",timeout=%lu", sbi->exp_timeout/HZ);
	seq_printf(m, ",minproto=%d", sbi->min_proto);
	seq_printf(m, ",maxproto=%d", sbi->max_proto);

	if (autofs_type_offset(sbi->type))
		seq_puts(m, ",offset");
	else if (autofs_type_direct(sbi->type))
		seq_puts(m, ",direct");
	else
		seq_puts(m, ",indirect");
	if (sbi->flags & AUTOFS_SBI_STRICTEXPIRE)
		seq_puts(m, ",strictexpire");
	if (sbi->flags & AUTOFS_SBI_IGNORE)
		seq_puts(m, ",igyesre");
#ifdef CONFIG_CHECKPOINT_RESTORE
	if (sbi->pipe)
		seq_printf(m, ",pipe_iyes=%ld", file_iyesde(sbi->pipe)->i_iyes);
	else
		seq_puts(m, ",pipe_iyes=-1");
#endif
	return 0;
}

static void autofs_evict_iyesde(struct iyesde *iyesde)
{
	clear_iyesde(iyesde);
	kfree(iyesde->i_private);
}

static const struct super_operations autofs_sops = {
	.statfs		= simple_statfs,
	.show_options	= autofs_show_options,
	.evict_iyesde	= autofs_evict_iyesde,
};

enum {Opt_err, Opt_fd, Opt_uid, Opt_gid, Opt_pgrp, Opt_minproto, Opt_maxproto,
	Opt_indirect, Opt_direct, Opt_offset, Opt_strictexpire,
	Opt_igyesre};

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
	{Opt_strictexpire, "strictexpire"},
	{Opt_igyesre, "igyesre"},
	{Opt_err, NULL}
};

static int parse_options(char *options,
			 struct iyesde *root, int *pgrp, bool *pgrp_set,
			 struct autofs_sb_info *sbi)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	int pipefd = -1;
	kuid_t uid;
	kgid_t gid;

	root->i_uid = current_uid();
	root->i_gid = current_gid();

	sbi->min_proto = AUTOFS_MIN_PROTO_VERSION;
	sbi->max_proto = AUTOFS_MAX_PROTO_VERSION;

	sbi->pipefd = -1;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_fd:
			if (match_int(args, &pipefd))
				return 1;
			sbi->pipefd = pipefd;
			break;
		case Opt_uid:
			if (match_int(args, &option))
				return 1;
			uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid))
				return 1;
			root->i_uid = uid;
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return 1;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid))
				return 1;
			root->i_gid = gid;
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
			sbi->min_proto = option;
			break;
		case Opt_maxproto:
			if (match_int(args, &option))
				return 1;
			sbi->max_proto = option;
			break;
		case Opt_indirect:
			set_autofs_type_indirect(&sbi->type);
			break;
		case Opt_direct:
			set_autofs_type_direct(&sbi->type);
			break;
		case Opt_offset:
			set_autofs_type_offset(&sbi->type);
			break;
		case Opt_strictexpire:
			sbi->flags |= AUTOFS_SBI_STRICTEXPIRE;
			break;
		case Opt_igyesre:
			sbi->flags |= AUTOFS_SBI_IGNORE;
			break;
		default:
			return 1;
		}
	}
	return (sbi->pipefd < 0);
}

int autofs_fill_super(struct super_block *s, void *data, int silent)
{
	struct iyesde *root_iyesde;
	struct dentry *root;
	struct file *pipe;
	struct autofs_sb_info *sbi;
	struct autofs_info *iyes;
	int pgrp = 0;
	bool pgrp_set = false;
	int ret = -EINVAL;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	pr_debug("starting up, sbi = %p\n", sbi);

	s->s_fs_info = sbi;
	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->pipefd = -1;
	sbi->pipe = NULL;
	sbi->exp_timeout = 0;
	sbi->oz_pgrp = NULL;
	sbi->sb = s;
	sbi->version = 0;
	sbi->sub_version = 0;
	sbi->flags = AUTOFS_SBI_CATATONIC;
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
	s->s_op = &autofs_sops;
	s->s_d_op = &autofs_dentry_operations;
	s->s_time_gran = 1;

	/*
	 * Get the root iyesde and dentry, but defer checking for errors.
	 */
	iyes = autofs_new_iyes(sbi);
	if (!iyes) {
		ret = -ENOMEM;
		goto fail_free;
	}
	root_iyesde = autofs_get_iyesde(s, S_IFDIR | 0755);
	root = d_make_root(root_iyesde);
	if (!root) {
		ret = -ENOMEM;
		goto fail_iyes;
	}
	pipe = NULL;

	root->d_fsdata = iyes;

	/* Can this call block? */
	if (parse_options(data, root_iyesde, &pgrp, &pgrp_set, sbi)) {
		pr_err("called with bogus options\n");
		goto fail_dput;
	}

	/* Test versions first */
	if (sbi->max_proto < AUTOFS_MIN_PROTO_VERSION ||
	    sbi->min_proto > AUTOFS_MAX_PROTO_VERSION) {
		pr_err("kernel does yest match daemon version "
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

	if (pgrp_set) {
		sbi->oz_pgrp = find_get_pid(pgrp);
		if (!sbi->oz_pgrp) {
			pr_err("could yest find process group %d\n",
				pgrp);
			goto fail_dput;
		}
	} else {
		sbi->oz_pgrp = get_task_pid(current, PIDTYPE_PGID);
	}

	if (autofs_type_trigger(sbi->type))
		__managed_dentry_set_managed(root);

	root_iyesde->i_fop = &autofs_root_operations;
	root_iyesde->i_op = &autofs_dir_iyesde_operations;

	pr_debug("pipe fd = %d, pgrp = %u\n",
		 sbi->pipefd, pid_nr(sbi->oz_pgrp));
	pipe = fget(sbi->pipefd);

	if (!pipe) {
		pr_err("could yest open pipe file descriptor\n");
		goto fail_put_pid;
	}
	ret = autofs_prepare_pipe(pipe);
	if (ret < 0)
		goto fail_fput;
	sbi->pipe = pipe;
	sbi->flags &= ~AUTOFS_SBI_CATATONIC;

	/*
	 * Success! Install the root dentry yesw to indicate completion.
	 */
	s->s_root = root;
	return 0;

	/*
	 * Failure ... clean up.
	 */
fail_fput:
	pr_err("pipe file descriptor does yest contain proper ops\n");
	fput(pipe);
fail_put_pid:
	put_pid(sbi->oz_pgrp);
fail_dput:
	dput(root);
	goto fail_free;
fail_iyes:
	autofs_free_iyes(iyes);
fail_free:
	kfree(sbi);
	s->s_fs_info = NULL;
	return ret;
}

struct iyesde *autofs_get_iyesde(struct super_block *sb, umode_t mode)
{
	struct iyesde *iyesde = new_iyesde(sb);

	if (iyesde == NULL)
		return NULL;

	iyesde->i_mode = mode;
	if (sb->s_root) {
		iyesde->i_uid = d_iyesde(sb->s_root)->i_uid;
		iyesde->i_gid = d_iyesde(sb->s_root)->i_gid;
	}
	iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	iyesde->i_iyes = get_next_iyes();

	if (S_ISDIR(mode)) {
		set_nlink(iyesde, 2);
		iyesde->i_op = &autofs_dir_iyesde_operations;
		iyesde->i_fop = &autofs_dir_operations;
	} else if (S_ISLNK(mode)) {
		iyesde->i_op = &autofs_symlink_iyesde_operations;
	} else
		WARN_ON(1);

	return iyesde;
}
