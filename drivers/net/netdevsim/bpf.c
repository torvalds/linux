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
#include <linux/mutex.h>
#include <linux/rtnetlink.h>
#include <net/pkt_cls.h>

#include "netdevsim.h"

#define pr_vlog(env, fmt, ...)	\
	bpf_verifier_log_write(env, "[netdevsim] " fmt, ##__VA_ARGS__)

struct nsim_bpf_bound_prog {
	struct nsim_dev *nsim_dev;
	struct bpf_prog *prog;
	struct dentry *ddir;
	const char *state;
	bool is_loaded;
	struct list_head l;
};

#define NSIM_BPF_MAX_KEYS		2

struct nsim_bpf_bound_map {
	struct netdevsim *ns;
	struct bpf_offloaded_map *map;
	struct mutex mutex;
	struct nsim_map_entry {
		void *key;
		void *value;
	} entry[NSIM_BPF_MAX_KEYS];
	struct list_head l;
};

static int nsim_bpf_string_show(struct seq_file *file, void *data)
{
	const char **str = file->private;

	if (*str)
		seq_printf(file, "%s\n", *str);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(nsim_bpf_string);

static int
nsim_bpf_verify_insn(struct bpf_verifier_env *env, int insn_idx, int prev_insn)
{
	struct nsim_bpf_bound_prog *state;
	int ret = 0;

	state = env->prog->aux->offload->dev_priv;
	if (state->nsim_dev->bpf_bind_verifier_delay && !insn_idx)
		msleep(state->nsim_dev->bpf_bind_verifier_delay);

	if (insn_idx == env->prog->len - 1) {
		pr_vlog(env, "Hello from netdevsim!\n");

		if (!state->nsim_dev->bpf_bind_verifier_accept)
			ret = -EOPNOTSUPP;
	}

	return ret;
}

static int nsim_bpf_finalize(struct bpf_verifier_env *env)
{
	return 0;
}

static bool nsim_xdp_offload_active(struct netdevsim *ns)
{
	return ns->xdp_hw.prog;
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

	if (type != TC_SETUP_CLSBPF) {
		NSIM_EA(cls_bpf->common.extack,
			"only offload of BPF classifiers supported");
		return -EOPNOTSUPP;
	}

	if (!tc_cls_can_offload_and_chain0(ns->netdev, &cls_bpf->common))
		return -EOPNOTSUPP;

	if (cls_bpf->common.protocol != htons(ETH_P_ALL)) {
		NSIM_EA(cls_bpf->common.extack,
			"only ETH_P_ALL supported as filter protocol");
		return -EOPNOTSUPP;
	}

	if (!ns->bpf_tc_accept) {
		NSIM_EA(cls_bpf->common.extack,
			"netdevsim configured to reject BPF TC offload");
		return -EOPNOTSUPP;
	}
	/* Note: progs without skip_sw will probably not be dev bound */
	if (prog && !prog->aux->offload && !ns->bpf_tc_non_bound_accept) {
		NSIM_EA(cls_bpf->common.extack,
			"netdevsim configured to reject unbound programs");
		return -EOPNOTSUPP;
	}

	if (cls_bpf->command != TC_CLSBPF_OFFLOAD)
		return -EOPNOTSUPP;

	oldprog = cls_bpf->oldprog;

	/* Don't remove if oldprog doesn't match driver's state */
	if (ns->bpf_offloaded != oldprog) {
		oldprog = NULL;
		if (!cls_bpf->prog)
			return 0;
		if (ns->bpf_offloaded) {
			NSIM_EA(cls_bpf->common.extack,
				"driver and netdev offload states mismatch");
			return -EBUSY;
		}
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

static int
nsim_xdp_set_prog(struct netdevsim *ns, struct netdev_bpf *bpf,
		  struct xdp_attachment_info *xdp)
{
	int err;

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

	xdp_attachment_setup(xdp, bpf);

	return 0;
}

static int nsim_bpf_create_prog(struct nsim_dev *nsim_dev,
				struct bpf_prog *prog)
{
	struct nsim_bpf_bound_prog *state;
	char name[16];
	int ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->nsim_dev = nsim_dev;
	state->prog = prog;
	state->state = "verify";

	/* Program id is not populated yet when we create the state. */
	sprintf(name, "%u", nsim_dev->prog_id_gen++);
	state->ddir = debugfs_create_dir(name, nsim_dev->ddir_bpf_bound_progs);
	if (IS_ERR(state->ddir)) {
		ret = PTR_ERR(state->ddir);
		kfree(state);
		return ret;
	}

	debugfs_create_u32("id", 0400, state->ddir, &prog->aux->id);
	debugfs_create_file("state", 0400, state->ddir,
			    &state->state, &nsim_bpf_string_fops);
	debugfs_create_bool("loaded", 0400, state->ddir, &state->is_loaded);

	list_add_tail(&state->l, &nsim_dev->bpf_bound_progs);

	prog->aux->offload->dev_priv = state;

	return 0;
}

static int nsim_bpf_verifier_prep(struct bpf_prog *prog)
{
	struct nsim_dev *nsim_dev =
			bpf_offload_dev_priv(prog->aux->offload->offdev);

	if (!nsim_dev->bpf_bind_accept)
		return -EOPNOTSUPP;

	return nsim_bpf_create_prog(nsim_dev, prog);
}

static int nsim_bpf_translate(struct bpf_prog *prog)
{
	struct nsim_bpf_bound_prog *state = prog->aux->offload->dev_priv;

	state->state = "xlated";
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

static const struct bpf_prog_offload_ops nsim_bpf_dev_ops = {
	.insn_hook	= nsim_bpf_verify_insn,
	.finalize	= nsim_bpf_finalize,
	.prepare	= nsim_bpf_verifier_prep,
	.translate	= nsim_bpf_translate,
	.destroy	= nsim_bpf_destroy_prog,
};

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

	state = bpf->prog->aux->offload->dev_priv;
	if (WARN_ON(strcmp(state->state, "xlated"))) {
		NSIM_EA(bpf->extack, "offloading program in bad state");
		return -EINVAL;
	}
	return 0;
}

static bool
nsim_map_key_match(struct bpf_map *map, struct nsim_map_entry *e, void *key)
{
	return e->key && !memcmp(key, e->key, map->key_size);
}

static int nsim_map_key_find(struct bpf_offloaded_map *offmap, void *key)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(nmap->entry); i++)
		if (nsim_map_key_match(&offmap->map, &nmap->entry[i], key))
			return i;

	return -ENOENT;
}

static int
nsim_map_alloc_elem(struct bpf_offloaded_map *offmap, unsigned int idx)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;

	nmap->entry[idx].key = kmalloc(offmap->map.key_size,
				       GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (!nmap->entry[idx].key)
		return -ENOMEM;
	nmap->entry[idx].value = kmalloc(offmap->map.value_size,
					 GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (!nmap->entry[idx].value) {
		kfree(nmap->entry[idx].key);
		nmap->entry[idx].key = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int
nsim_map_get_next_key(struct bpf_offloaded_map *offmap,
		      void *key, void *next_key)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;
	int idx = -ENOENT;

	mutex_lock(&nmap->mutex);

	if (key)
		idx = nsim_map_key_find(offmap, key);
	if (idx == -ENOENT)
		idx = 0;
	else
		idx++;

	for (; idx < ARRAY_SIZE(nmap->entry); idx++) {
		if (nmap->entry[idx].key) {
			memcpy(next_key, nmap->entry[idx].key,
			       offmap->map.key_size);
			break;
		}
	}

	mutex_unlock(&nmap->mutex);

	if (idx == ARRAY_SIZE(nmap->entry))
		return -ENOENT;
	return 0;
}

static int
nsim_map_lookup_elem(struct bpf_offloaded_map *offmap, void *key, void *value)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;
	int idx;

	mutex_lock(&nmap->mutex);

	idx = nsim_map_key_find(offmap, key);
	if (idx >= 0)
		memcpy(value, nmap->entry[idx].value, offmap->map.value_size);

	mutex_unlock(&nmap->mutex);

	return idx < 0 ? idx : 0;
}

static int
nsim_map_update_elem(struct bpf_offloaded_map *offmap,
		     void *key, void *value, u64 flags)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;
	int idx, err = 0;

	mutex_lock(&nmap->mutex);

	idx = nsim_map_key_find(offmap, key);
	if (idx < 0 && flags == BPF_EXIST) {
		err = idx;
		goto exit_unlock;
	}
	if (idx >= 0 && flags == BPF_NOEXIST) {
		err = -EEXIST;
		goto exit_unlock;
	}

	if (idx < 0) {
		for (idx = 0; idx < ARRAY_SIZE(nmap->entry); idx++)
			if (!nmap->entry[idx].key)
				break;
		if (idx == ARRAY_SIZE(nmap->entry)) {
			err = -E2BIG;
			goto exit_unlock;
		}

		err = nsim_map_alloc_elem(offmap, idx);
		if (err)
			goto exit_unlock;
	}

	memcpy(nmap->entry[idx].key, key, offmap->map.key_size);
	memcpy(nmap->entry[idx].value, value, offmap->map.value_size);
exit_unlock:
	mutex_unlock(&nmap->mutex);

	return err;
}

static int nsim_map_delete_elem(struct bpf_offloaded_map *offmap, void *key)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;
	int idx;

	if (offmap->map.map_type == BPF_MAP_TYPE_ARRAY)
		return -EINVAL;

	mutex_lock(&nmap->mutex);

	idx = nsim_map_key_find(offmap, key);
	if (idx >= 0) {
		kfree(nmap->entry[idx].key);
		kfree(nmap->entry[idx].value);
		memset(&nmap->entry[idx], 0, sizeof(nmap->entry[idx]));
	}

	mutex_unlock(&nmap->mutex);

	return idx < 0 ? idx : 0;
}

static const struct bpf_map_dev_ops nsim_bpf_map_ops = {
	.map_get_next_key	= nsim_map_get_next_key,
	.map_lookup_elem	= nsim_map_lookup_elem,
	.map_update_elem	= nsim_map_update_elem,
	.map_delete_elem	= nsim_map_delete_elem,
};

static int
nsim_bpf_map_alloc(struct netdevsim *ns, struct bpf_offloaded_map *offmap)
{
	struct nsim_bpf_bound_map *nmap;
	int i, err;

	if (WARN_ON(offmap->map.map_type != BPF_MAP_TYPE_ARRAY &&
		    offmap->map.map_type != BPF_MAP_TYPE_HASH))
		return -EINVAL;
	if (offmap->map.max_entries > NSIM_BPF_MAX_KEYS)
		return -ENOMEM;
	if (offmap->map.map_flags)
		return -EINVAL;

	nmap = kzalloc(sizeof(*nmap), GFP_KERNEL_ACCOUNT);
	if (!nmap)
		return -ENOMEM;

	offmap->dev_priv = nmap;
	nmap->ns = ns;
	nmap->map = offmap;
	mutex_init(&nmap->mutex);

	if (offmap->map.map_type == BPF_MAP_TYPE_ARRAY) {
		for (i = 0; i < ARRAY_SIZE(nmap->entry); i++) {
			u32 *key;

			err = nsim_map_alloc_elem(offmap, i);
			if (err)
				goto err_free;
			key = nmap->entry[i].key;
			*key = i;
			memset(nmap->entry[i].value, 0, offmap->map.value_size);
		}
	}

	offmap->dev_ops = &nsim_bpf_map_ops;
	list_add_tail(&nmap->l, &ns->nsim_dev->bpf_bound_maps);

	return 0;

err_free:
	while (--i >= 0) {
		kfree(nmap->entry[i].key);
		kfree(nmap->entry[i].value);
	}
	kfree(nmap);
	return err;
}

static void nsim_bpf_map_free(struct bpf_offloaded_map *offmap)
{
	struct nsim_bpf_bound_map *nmap = offmap->dev_priv;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(nmap->entry); i++) {
		kfree(nmap->entry[i].key);
		kfree(nmap->entry[i].value);
	}
	list_del_init(&nmap->l);
	mutex_destroy(&nmap->mutex);
	kfree(nmap);
}

int nsim_bpf(struct net_device *dev, struct netdev_bpf *bpf)
{
	struct netdevsim *ns = netdev_priv(dev);
	int err;

	ASSERT_RTNL();

	switch (bpf->command) {
	case XDP_SETUP_PROG:
		err = nsim_setup_prog_checks(ns, bpf);
		if (err)
			return err;

		return nsim_xdp_set_prog(ns, bpf, &ns->xdp);
	case XDP_SETUP_PROG_HW:
		err = nsim_setup_prog_hw_checks(ns, bpf);
		if (err)
			return err;

		return nsim_xdp_set_prog(ns, bpf, &ns->xdp_hw);
	case BPF_OFFLOAD_MAP_ALLOC:
		if (!ns->bpf_map_accept)
			return -EOPNOTSUPP;

		return nsim_bpf_map_alloc(ns, bpf->offmap);
	case BPF_OFFLOAD_MAP_FREE:
		nsim_bpf_map_free(bpf->offmap);
		return 0;
	default:
		return -EINVAL;
	}
}

int nsim_bpf_dev_init(struct nsim_dev *nsim_dev)
{
	int err;

	INIT_LIST_HEAD(&nsim_dev->bpf_bound_progs);
	INIT_LIST_HEAD(&nsim_dev->bpf_bound_maps);

	nsim_dev->ddir_bpf_bound_progs = debugfs_create_dir("bpf_bound_progs",
							    nsim_dev->ddir);
	if (IS_ERR(nsim_dev->ddir_bpf_bound_progs))
		return PTR_ERR(nsim_dev->ddir_bpf_bound_progs);

	nsim_dev->bpf_dev = bpf_offload_dev_create(&nsim_bpf_dev_ops, nsim_dev);
	err = PTR_ERR_OR_ZERO(nsim_dev->bpf_dev);
	if (err)
		return err;

	nsim_dev->bpf_bind_accept = true;
	debugfs_create_bool("bpf_bind_accept", 0600, nsim_dev->ddir,
			    &nsim_dev->bpf_bind_accept);
	debugfs_create_u32("bpf_bind_verifier_delay", 0600, nsim_dev->ddir,
			   &nsim_dev->bpf_bind_verifier_delay);
	nsim_dev->bpf_bind_verifier_accept = true;
	debugfs_create_bool("bpf_bind_verifier_accept", 0600, nsim_dev->ddir,
			    &nsim_dev->bpf_bind_verifier_accept);
	return 0;
}

void nsim_bpf_dev_exit(struct nsim_dev *nsim_dev)
{
	WARN_ON(!list_empty(&nsim_dev->bpf_bound_progs));
	WARN_ON(!list_empty(&nsim_dev->bpf_bound_maps));
	bpf_offload_dev_destroy(nsim_dev->bpf_dev);
}

int nsim_bpf_init(struct netdevsim *ns)
{
	struct dentry *ddir = ns->nsim_dev_port->ddir;
	int err;

	err = bpf_offload_dev_netdev_register(ns->nsim_dev->bpf_dev,
					      ns->netdev);
	if (err)
		return err;

	debugfs_create_u32("bpf_offloaded_id", 0400, ddir,
			   &ns->bpf_offloaded_id);

	ns->bpf_tc_accept = true;
	debugfs_create_bool("bpf_tc_accept", 0600, ddir,
			    &ns->bpf_tc_accept);
	debugfs_create_bool("bpf_tc_non_bound_accept", 0600, ddir,
			    &ns->bpf_tc_non_bound_accept);
	ns->bpf_xdpdrv_accept = true;
	debugfs_create_bool("bpf_xdpdrv_accept", 0600, ddir,
			    &ns->bpf_xdpdrv_accept);
	ns->bpf_xdpoffload_accept = true;
	debugfs_create_bool("bpf_xdpoffload_accept", 0600, ddir,
			    &ns->bpf_xdpoffload_accept);

	ns->bpf_map_accept = true;
	debugfs_create_bool("bpf_map_accept", 0600, ddir,
			    &ns->bpf_map_accept);

	return 0;
}

void nsim_bpf_uninit(struct netdevsim *ns)
{
	WARN_ON(ns->xdp.prog);
	WARN_ON(ns->xdp_hw.prog);
	WARN_ON(ns->bpf_offloaded);
	bpf_offload_dev_netdev_unregister(ns->nsim_dev->bpf_dev, ns->netdev);
}
