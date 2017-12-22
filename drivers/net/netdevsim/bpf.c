/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree.
 *
 * THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS"
 * WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE
 * OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
 * THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.
 */

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/rtnetlink.h>
#include <net/pkt_cls.h>

#include "netdevsim.h"

struct nsim_bpf_bound_prog {
	struct netdevsim *ns;
	struct bpf_prog *prog;
	struct dentry *ddir;
	const char *state;
	bool is_loaded;
	struct list_head l;
};

static int nsim_debugfs_bpf_string_read(struct seq_file *file, void *data)
{
	const char **str = file->private;

	if (*str)
		seq_printf(file, "%s\n", *str);

	return 0;
}

static int nsim_debugfs_bpf_string_open(struct inode *inode, struct file *f)
{
	return single_open(f, nsim_debugfs_bpf_string_read, inode->i_private);
}

static const struct file_operations nsim_bpf_string_fops = {
	.owner = THIS_MODULE,
	.open = nsim_debugfs_bpf_string_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static int
nsim_bpf_verify_insn(struct bpf_verifier_env *env, int insn_idx, int prev_insn)
{
	struct nsim_bpf_bound_prog *state;

	state = env->prog->aux->offload->dev_priv;
	if (state->ns->bpf_bind_verifier_delay && !insn_idx)
		msleep(state->ns->bpf_bind_verifier_delay);

	return 0;
}

static const struct bpf_ext_analyzer_ops nsim_bpf_analyzer_ops = {
	.insn_hook = nsim_bpf_verify_insn,
};

static bool nsim_xdp_offload_active(struct netdevsim *ns)
{
	return ns->xdp_prog_mode == XDP_ATTACHED_HW;
}

static void nsim_prog_set_loaded(struct bpf_prog *prog, bool loaded)
{
	struct nsim_bpf_bound_prog *state;

	if (!prog || !prog->aux->offload)
		return;

	state = prog->aux->offload->dev_priv;
	state->is_loaded = loaded;
}

static int
nsim_bpf_offload(struct netdevsim *ns, struct bpf_prog *prog, bool oldprog)
{
	nsim_prog_set_loaded(ns->bpf_offloaded, false);

	WARN(!!ns->bpf_offloaded != oldprog,
	     "bad offload state, expected offload %sto be active",
	     oldprog ? "" : "not ");
	ns->bpf_offloaded = prog;
	ns->bpf_offloaded_id = prog ? prog->aux->id : 0;
	nsim_prog_set_loaded(prog, true);

	return 0;
}

int nsim_bpf_setup_tc_block_cb(enum tc_setup_type type,
			       void *type_data, void *cb_priv)
{
	struct tc_cls_bpf_offload *cls_bpf = type_data;
	struct bpf_prog *prog = cls_bpf->prog;
	struct netdevsim *ns = cb_priv;
	struct bpf_prog *oldprog;

	if (type != TC_SETUP_CLSBPF ||
	    !tc_can_offload(ns->netdev) ||
	    cls_bpf->common.protocol != htons(ETH_P_ALL) ||
	    cls_bpf->common.chain_index)
		return -EOPNOTSUPP;

	if (!ns->bpf_tc_accept)
		return -EOPNOTSUPP;
	/* Note: progs without skip_sw will probably not be dev bound */
	if (prog && !prog->aux->offload && !ns->bpf_tc_non_bound_accept)
		return -EOPNOTSUPP;

	if (cls_bpf->command != TC_CLSBPF_OFFLOAD)
		return -EOPNOTSUPP;

	oldprog = cls_bpf->oldprog;

	/* Don't remove if oldprog doesn't match driver's state */
	if (ns->bpf_offloaded != oldprog) {
		oldprog = NULL;
		if (!cls_bpf->prog)
			return 0;
		if (ns->bpf_offloaded)
			return -EBUSY;
	}

	return nsim_bpf_offload(ns, cls_bpf->prog, oldprog);
}

int nsim_bpf_disable_tc(struct netdevsim *ns)
{
	if (ns->bpf_offloaded && !nsim_xdp_offload_active(ns))
		return -EBUSY;
	return 0;
}

static int nsim_xdp_offload_prog(struct netdevsim *ns, struct netdev_bpf *bpf)
{
	if (!nsim_xdp_offload_active(ns) && !bpf->prog)
		return 0;
	if (!nsim_xdp_offload_active(ns) && bpf->prog && ns->bpf_offloaded) {
		NSIM_EA(bpf->extack, "TC program is already loaded");
		return -EBUSY;
	}

	return nsim_bpf_offload(ns, bpf->prog, nsim_xdp_offload_active(ns));
}

static int nsim_xdp_set_prog(struct netdevsim *ns, struct netdev_bpf *bpf)
{
	int err;

	if (ns->xdp_prog && (bpf->flags ^ ns->xdp_flags) & XDP_FLAGS_MODES) {
		NSIM_EA(bpf->extack, "program loaded with different flags");
		return -EBUSY;
	}

	if (bpf->command == XDP_SETUP_PROG && !ns->bpf_xdpdrv_accept) {
		NSIM_EA(bpf->extack, "driver XDP disabled in DebugFS");
		return -EOPNOTSUPP;
	}
	if (bpf->command == XDP_SETUP_PROG_HW && !ns->bpf_xdpoffload_accept) {
		NSIM_EA(bpf->extack, "XDP offload disabled in DebugFS");
		return -EOPNOTSUPP;
	}

	if (bpf->command == XDP_SETUP_PROG_HW) {
		err = nsim_xdp_offload_prog(ns, bpf);
		if (err)
			return err;
	}

	if (ns->xdp_prog)
		bpf_prog_put(ns->xdp_prog);

	ns->xdp_prog = bpf->prog;
	ns->xdp_flags = bpf->flags;

	if (!bpf->prog)
		ns->xdp_prog_mode = XDP_ATTACHED_NONE;
	else if (bpf->command == XDP_SETUP_PROG)
		ns->xdp_prog_mode = XDP_ATTACHED_DRV;
	else
		ns->xdp_prog_mode = XDP_ATTACHED_HW;

	return 0;
}

static int nsim_bpf_create_prog(struct netdevsim *ns, struct bpf_prog *prog)
{
	struct nsim_bpf_bound_prog *state;
	char name[16];

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->ns = ns;
	state->prog = prog;
	state->state = "verify";

	/* Program id is not populated yet when we create the state. */
	sprintf(name, "%u", ns->prog_id_gen++);
	state->ddir = debugfs_create_dir(name, ns->ddir_bpf_bound_progs);
	if (IS_ERR_OR_NULL(state->ddir)) {
		kfree(state);
		return -ENOMEM;
	}

	debugfs_create_u32("id", 0400, state->ddir, &prog->aux->id);
	debugfs_create_file("state", 0400, state->ddir,
			    &state->state, &nsim_bpf_string_fops);
	debugfs_create_bool("loaded", 0400, state->ddir, &state->is_loaded);

	list_add_tail(&state->l, &ns->bpf_bound_progs);

	prog->aux->offload->dev_priv = state;

	return 0;
}

static void nsim_bpf_destroy_prog(struct bpf_prog *prog)
{
	struct nsim_bpf_bound_prog *state;

	state = prog->aux->offload->dev_priv;
	WARN(state->is_loaded,
	     "offload state destroyed while program still bound");
	debugfs_remove_recursive(state->ddir);
	list_del(&state->l);
	kfree(state);
}

static int nsim_setup_prog_checks(struct netdevsim *ns, struct netdev_bpf *bpf)
{
	if (bpf->prog && bpf->prog->aux->offload) {
		NSIM_EA(bpf->extack, "attempt to load offloaded prog to drv");
		return -EINVAL;
	}
	if (ns->netdev->mtu > NSIM_XDP_MAX_MTU) {
		NSIM_EA(bpf->extack, "MTU too large w/ XDP enabled");
		return -EINVAL;
	}
	if (nsim_xdp_offload_active(ns)) {
		NSIM_EA(bpf->extack, "xdp offload active, can't load drv prog");
		return -EBUSY;
	}
	return 0;
}

static int
nsim_setup_prog_hw_checks(struct netdevsim *ns, struct netdev_bpf *bpf)
{
	struct nsim_bpf_bound_prog *state;

	if (!bpf->prog)
		return 0;

	if (!bpf->prog->aux->offload) {
		NSIM_EA(bpf->extack, "xdpoffload of non-bound program");
		return -EINVAL;
	}
	if (bpf->prog->aux->offload->netdev != ns->netdev) {
		NSIM_EA(bpf->extack, "program bound to different dev");
		return -EINVAL;
	}

	state = bpf->prog->aux->offload->dev_priv;
	if (WARN_ON(strcmp(state->state, "xlated"))) {
		NSIM_EA(bpf->extack, "offloading program in bad state");
		return -EINVAL;
	}
	return 0;
}

int nsim_bpf(struct net_device *dev, struct netdev_bpf *bpf)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct nsim_bpf_bound_prog *state;
	int err;

	ASSERT_RTNL();

	switch (bpf->command) {
	case BPF_OFFLOAD_VERIFIER_PREP:
		if (!ns->bpf_bind_accept)
			return -EOPNOTSUPP;

		err = nsim_bpf_create_prog(ns, bpf->verifier.prog);
		if (err)
			return err;

		bpf->verifier.ops = &nsim_bpf_analyzer_ops;
		return 0;
	case BPF_OFFLOAD_TRANSLATE:
		state = bpf->offload.prog->aux->offload->dev_priv;

		state->state = "xlated";
		return 0;
	case BPF_OFFLOAD_DESTROY:
		nsim_bpf_destroy_prog(bpf->offload.prog);
		return 0;
	case XDP_QUERY_PROG:
		bpf->prog_attached = ns->xdp_prog_mode;
		bpf->prog_id = ns->xdp_prog ? ns->xdp_prog->aux->id : 0;
		bpf->prog_flags = ns->xdp_prog ? ns->xdp_flags : 0;
		return 0;
	case XDP_SETUP_PROG:
		err = nsim_setup_prog_checks(ns, bpf);
		if (err)
			return err;

		return nsim_xdp_set_prog(ns, bpf);
	case XDP_SETUP_PROG_HW:
		err = nsim_setup_prog_hw_checks(ns, bpf);
		if (err)
			return err;

		return nsim_xdp_set_prog(ns, bpf);
	default:
		return -EINVAL;
	}
}

int nsim_bpf_init(struct netdevsim *ns)
{
	INIT_LIST_HEAD(&ns->bpf_bound_progs);

	debugfs_create_u32("bpf_offloaded_id", 0400, ns->ddir,
			   &ns->bpf_offloaded_id);

	ns->bpf_bind_accept = true;
	debugfs_create_bool("bpf_bind_accept", 0600, ns->ddir,
			    &ns->bpf_bind_accept);
	debugfs_create_u32("bpf_bind_verifier_delay", 0600, ns->ddir,
			   &ns->bpf_bind_verifier_delay);
	ns->ddir_bpf_bound_progs =
		debugfs_create_dir("bpf_bound_progs", ns->ddir);
	if (IS_ERR_OR_NULL(ns->ddir_bpf_bound_progs))
		return -ENOMEM;

	ns->bpf_tc_accept = true;
	debugfs_create_bool("bpf_tc_accept", 0600, ns->ddir,
			    &ns->bpf_tc_accept);
	debugfs_create_bool("bpf_tc_non_bound_accept", 0600, ns->ddir,
			    &ns->bpf_tc_non_bound_accept);
	ns->bpf_xdpdrv_accept = true;
	debugfs_create_bool("bpf_xdpdrv_accept", 0600, ns->ddir,
			    &ns->bpf_xdpdrv_accept);
	ns->bpf_xdpoffload_accept = true;
	debugfs_create_bool("bpf_xdpoffload_accept", 0600, ns->ddir,
			    &ns->bpf_xdpoffload_accept);

	return 0;
}

void nsim_bpf_uninit(struct netdevsim *ns)
{
	WARN_ON(!list_empty(&ns->bpf_bound_progs));
	WARN_ON(ns->xdp_prog);
	WARN_ON(ns->bpf_offloaded);
}
