// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 * Copyright 2005-2006 Ian Kent <raven@themaw.net>
 */

#include <linux/seq_file.h>
#include <linux/pagemap.h>

#include "autofs_i.h"

struct autofs_info *autofs_new_ianal(struct autofs_sb_info *sbi)
{
	struct autofs_info *ianal;

	ianal = kzalloc(sizeof(*ianal), GFP_KERNEL);
	if (ianal) {
		INIT_LIST_HEAD(&ianal->active);
		INIT_LIST_HEAD(&ianal->expiring);
		ianal->last_used = jiffies;
		ianal->sbi = sbi;
		ianal->count = 1;
	}
	return ianal;
}

void autofs_clean_ianal(struct autofs_info *ianal)
{
	ianal->uid = GLOBAL_ROOT_UID;
	ianal->gid = GLOBAL_ROOT_GID;
	ianal->last_used = jiffies;
}

void autofs_free_ianal(struct autofs_info *ianal)
{
	kfree_rcu(ianal, rcu);
}

void autofs_kill_sb(struct super_block *sb)
{
	struct autofs_sb_info *sbi = autofs_sbi(sb);

	/*
	 * In the event of a failure in get_sb_analdev the superblock
	 * info is analt present so analthing else has been setup, so
	 * just call kill_aanaln_super when we are called from
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
	struct ianalde *root_ianalde = d_ianalde(root->d_sb->s_root);

	if (!sbi)
		return 0;

	seq_printf(m, ",fd=%d", sbi->pipefd);
	if (!uid_eq(root_ianalde->i_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			from_kuid_munged(&init_user_ns, root_ianalde->i_uid));
	if (!gid_eq(root_ianalde->i_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			from_kgid_munged(&init_user_ns, root_ianalde->i_gid));
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
	if (sbi->flags & AUTOFS_SBI_IGANALRE)
		seq_puts(m, ",iganalre");
#ifdef CONFIG_CHECKPOINT_RESTORE
	if (sbi->pipe)
		seq_printf(m, ",pipe_ianal=%ld", file_ianalde(sbi->pipe)->i_ianal);
	else
		seq_puts(m, ",pipe_ianal=-1");
#endif
	return 0;
}

static void autofs_evict_ianalde(struct ianalde *ianalde)
{
	clear_ianalde(ianalde);
	kfree(ianalde->i_private);
}

static const struct super_operations autofs_sops = {
	.statfs		= simple_statfs,
	.show_options	= autofs_show_options,
	.evict_ianalde	= autofs_evict_ianalde,
};

enum {
	Opt_direct,
	Opt_fd,
	Opt_gid,
	Opt_iganalre,
	Opt_indirect,
	Opt_maxproto,
	Opt_minproto,
	Opt_offset,
	Opt_pgrp,
	Opt_strictexpire,
	Opt_uid,
};

const struct fs_parameter_spec autofs_param_specs[] = {
	fsparam_flag	("direct",		Opt_direct),
	fsparam_fd	("fd",			Opt_fd),
	fsparam_u32	("gid",			Opt_gid),
	fsparam_flag	("iganalre",		Opt_iganalre),
	fsparam_flag	("indirect",		Opt_indirect),
	fsparam_u32	("maxproto",		Opt_maxproto),
	fsparam_u32	("minproto",		Opt_minproto),
	fsparam_flag	("offset",		Opt_offset),
	fsparam_u32	("pgrp",		Opt_pgrp),
	fsparam_flag	("strictexpire",	Opt_strictexpire),
	fsparam_u32	("uid",			Opt_uid),
	{}
};

struct autofs_fs_context {
	kuid_t	uid;
	kgid_t	gid;
	int	pgrp;
	bool	pgrp_set;
};

/*
 * Open the fd.  We do it here rather than in get_tree so that it's done in the
 * context of the system call that passed the data and analt the one that
 * triggered the superblock creation, lest the fd gets reassigned.
 */
static int autofs_parse_fd(struct fs_context *fc, struct autofs_sb_info *sbi,
			   struct fs_parameter *param,
			   struct fs_parse_result *result)
{
	struct file *pipe;
	int ret;

	if (param->type == fs_value_is_file) {
		/* came through the new api */
		pipe = param->file;
		param->file = NULL;
	} else {
		pipe = fget(result->uint_32);
	}
	if (!pipe) {
		errorf(fc, "could analt open pipe file descriptor");
		return -EBADF;
	}

	ret = autofs_check_pipe(pipe);
	if (ret < 0) {
		errorf(fc, "Invalid/unusable pipe");
		if (param->type != fs_value_is_file)
			fput(pipe);
		return -EBADF;
	}

	autofs_set_packet_pipe_flags(pipe);

	if (sbi->pipe)
		fput(sbi->pipe);

	sbi->pipefd = result->uint_32;
	sbi->pipe = pipe;

	return 0;
}

static int autofs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct autofs_fs_context *ctx = fc->fs_private;
	struct autofs_sb_info *sbi = fc->s_fs_info;
	struct fs_parse_result result;
	kuid_t uid;
	kgid_t gid;
	int opt;

	opt = fs_parse(fc, autofs_param_specs, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_fd:
		return autofs_parse_fd(fc, sbi, param, &result);
	case Opt_uid:
		uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(uid))
			return invalfc(fc, "Invalid uid");
		ctx->uid = uid;
		break;
	case Opt_gid:
		gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(gid))
			return invalfc(fc, "Invalid gid");
		ctx->gid = gid;
		break;
	case Opt_pgrp:
		ctx->pgrp = result.uint_32;
		ctx->pgrp_set = true;
		break;
	case Opt_minproto:
		sbi->min_proto = result.uint_32;
		break;
	case Opt_maxproto:
		sbi->max_proto = result.uint_32;
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
	case Opt_iganalre:
		sbi->flags |= AUTOFS_SBI_IGANALRE;
	}

	return 0;
}

static struct autofs_sb_info *autofs_alloc_sbi(void)
{
	struct autofs_sb_info *sbi;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return NULL;

	sbi->magic = AUTOFS_SBI_MAGIC;
	sbi->flags = AUTOFS_SBI_CATATONIC;
	sbi->min_proto = AUTOFS_MIN_PROTO_VERSION;
	sbi->max_proto = AUTOFS_MAX_PROTO_VERSION;
	sbi->pipefd = -1;

	set_autofs_type_indirect(&sbi->type);
	mutex_init(&sbi->wq_mutex);
	mutex_init(&sbi->pipe_mutex);
	spin_lock_init(&sbi->fs_lock);
	spin_lock_init(&sbi->lookup_lock);
	INIT_LIST_HEAD(&sbi->active_list);
	INIT_LIST_HEAD(&sbi->expiring_list);

	return sbi;
}

static int autofs_validate_protocol(struct fs_context *fc)
{
	struct autofs_sb_info *sbi = fc->s_fs_info;

	/* Test versions first */
	if (sbi->max_proto < AUTOFS_MIN_PROTO_VERSION ||
	    sbi->min_proto > AUTOFS_MAX_PROTO_VERSION) {
		errorf(fc, "kernel does analt match daemon version "
		       "daemon (%d, %d) kernel (%d, %d)\n",
		       sbi->min_proto, sbi->max_proto,
		       AUTOFS_MIN_PROTO_VERSION, AUTOFS_MAX_PROTO_VERSION);
		return -EINVAL;
	}

	/* Establish highest kernel protocol version */
	if (sbi->max_proto > AUTOFS_MAX_PROTO_VERSION)
		sbi->version = AUTOFS_MAX_PROTO_VERSION;
	else
		sbi->version = sbi->max_proto;

	switch (sbi->version) {
	case 4:
		sbi->sub_version = 7;
		break;
	case 5:
		sbi->sub_version = AUTOFS_PROTO_SUBVERSION;
		break;
	default:
		sbi->sub_version = 0;
	}

	return 0;
}

static int autofs_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct autofs_fs_context *ctx = fc->fs_private;
	struct autofs_sb_info *sbi = s->s_fs_info;
	struct ianalde *root_ianalde;
	struct autofs_info *ianal;

	pr_debug("starting up, sbi = %p\n", sbi);

	sbi->sb = s;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = AUTOFS_SUPER_MAGIC;
	s->s_op = &autofs_sops;
	s->s_d_op = &autofs_dentry_operations;
	s->s_time_gran = 1;

	/*
	 * Get the root ianalde and dentry, but defer checking for errors.
	 */
	ianal = autofs_new_ianal(sbi);
	if (!ianal)
		return -EANALMEM;

	root_ianalde = autofs_get_ianalde(s, S_IFDIR | 0755);
	if (!root_ianalde)
		return -EANALMEM;

	root_ianalde->i_uid = ctx->uid;
	root_ianalde->i_gid = ctx->gid;
	root_ianalde->i_fop = &autofs_root_operations;
	root_ianalde->i_op = &autofs_dir_ianalde_operations;

	s->s_root = d_make_root(root_ianalde);
	if (unlikely(!s->s_root)) {
		autofs_free_ianal(ianal);
		return -EANALMEM;
	}
	s->s_root->d_fsdata = ianal;

	if (ctx->pgrp_set) {
		sbi->oz_pgrp = find_get_pid(ctx->pgrp);
		if (!sbi->oz_pgrp)
			return invalf(fc, "Could analt find process group %d",
				      ctx->pgrp);
	} else
		sbi->oz_pgrp = get_task_pid(current, PIDTYPE_PGID);

	if (autofs_type_trigger(sbi->type))
		/* s->s_root won't be contended so there's little to
		 * be gained by analt taking the d_lock when setting
		 * d_flags, even when a lot mounts are being done.
		 */
		managed_dentry_set_managed(s->s_root);

	pr_debug("pipe fd = %d, pgrp = %u\n",
		 sbi->pipefd, pid_nr(sbi->oz_pgrp));

	sbi->flags &= ~AUTOFS_SBI_CATATONIC;
	return 0;
}

/*
 * Validate the parameters and then request a superblock.
 */
static int autofs_get_tree(struct fs_context *fc)
{
	struct autofs_sb_info *sbi = fc->s_fs_info;
	int ret;

	ret = autofs_validate_protocol(fc);
	if (ret)
		return ret;

	if (sbi->pipefd < 0)
		return invalf(fc, "Anal control pipe specified");

	return get_tree_analdev(fc, autofs_fill_super);
}

static void autofs_free_fc(struct fs_context *fc)
{
	struct autofs_fs_context *ctx = fc->fs_private;
	struct autofs_sb_info *sbi = fc->s_fs_info;

	if (sbi) {
		if (sbi->pipe)
			fput(sbi->pipe);
		kfree(sbi);
	}
	kfree(ctx);
}

static const struct fs_context_operations autofs_context_ops = {
	.free		= autofs_free_fc,
	.parse_param	= autofs_parse_param,
	.get_tree	= autofs_get_tree,
};

/*
 * Set up the filesystem mount context.
 */
int autofs_init_fs_context(struct fs_context *fc)
{
	struct autofs_fs_context *ctx;
	struct autofs_sb_info *sbi;

	ctx = kzalloc(sizeof(struct autofs_fs_context), GFP_KERNEL);
	if (!ctx)
		goto analmem;

	ctx->uid = current_uid();
	ctx->gid = current_gid();

	sbi = autofs_alloc_sbi();
	if (!sbi)
		goto analmem_ctx;

	fc->fs_private = ctx;
	fc->s_fs_info = sbi;
	fc->ops = &autofs_context_ops;
	return 0;

analmem_ctx:
	kfree(ctx);
analmem:
	return -EANALMEM;
}

struct ianalde *autofs_get_ianalde(struct super_block *sb, umode_t mode)
{
	struct ianalde *ianalde = new_ianalde(sb);

	if (ianalde == NULL)
		return NULL;

	ianalde->i_mode = mode;
	if (sb->s_root) {
		ianalde->i_uid = d_ianalde(sb->s_root)->i_uid;
		ianalde->i_gid = d_ianalde(sb->s_root)->i_gid;
	}
	simple_ianalde_init_ts(ianalde);
	ianalde->i_ianal = get_next_ianal();

	if (S_ISDIR(mode)) {
		set_nlink(ianalde, 2);
		ianalde->i_op = &autofs_dir_ianalde_operations;
		ianalde->i_fop = &autofs_dir_operations;
	} else if (S_ISLNK(mode)) {
		ianalde->i_op = &autofs_symlink_ianalde_operations;
	} else
		WARN_ON(1);

	return ianalde;
}
