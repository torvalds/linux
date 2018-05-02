/*
 * Copyright (C) 2016-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_net_offload.c
 * Netronome network device driver: TC offload functions for PF and VF
 */

#define pr_fmt(fmt)	"NFP net bpf: " fmt

#include <linux/bpf.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/mm.h>

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>

#include "main.h"
#include "../nfp_app.h"
#include "../nfp_net_ctrl.h"
#include "../nfp_net.h"

static int
nfp_prog_prepare(struct nfp_prog *nfp_prog, const struct bpf_insn *prog,
		 unsigned int cnt)
{
	struct nfp_insn_meta *meta;
	unsigned int i;

	for (i = 0; i < cnt; i++) {
		meta = kzalloc(sizeof(*meta), GFP_KERNEL);
		if (!meta)
			return -ENOMEM;

		meta->insn = prog[i];
		meta->n = i;

		list_add_tail(&meta->l, &nfp_prog->insns);
	}

	nfp_bpf_jit_prepare(nfp_prog, cnt);

	return 0;
}

static void nfp_prog_free(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *tmp;

	list_for_each_entry_safe(meta, tmp, &nfp_prog->insns, l) {
		list_del(&meta->l);
		kfree(meta);
	}
	kfree(nfp_prog);
}

static int
nfp_bpf_verifier_prep(struct nfp_app *app, struct nfp_net *nn,
		      struct netdev_bpf *bpf)
{
	struct bpf_prog *prog = bpf->verifier.prog;
	struct nfp_prog *nfp_prog;
	int ret;

	nfp_prog = kzalloc(sizeof(*nfp_prog), GFP_KERNEL);
	if (!nfp_prog)
		return -ENOMEM;
	prog->aux->offload->dev_priv = nfp_prog;

	INIT_LIST_HEAD(&nfp_prog->insns);
	nfp_prog->type = prog->type;
	nfp_prog->bpf = app->priv;

	ret = nfp_prog_prepare(nfp_prog, prog->insnsi, prog->len);
	if (ret)
		goto err_free;

	nfp_prog->verifier_meta = nfp_prog_first_meta(nfp_prog);
	bpf->verifier.ops = &nfp_bpf_analyzer_ops;

	return 0;

err_free:
	nfp_prog_free(nfp_prog);

	return ret;
}

static int nfp_bpf_translate(struct nfp_net *nn, struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;
	unsigned int stack_size;
	unsigned int max_instr;
	int err;

	stack_size = nn_readb(nn, NFP_NET_CFG_BPF_STACK_SZ) * 64;
	if (prog->aux->stack_depth > stack_size) {
		nn_info(nn, "stack too large: program %dB > FW stack %dB\n",
			prog->aux->stack_depth, stack_size);
		return -EOPNOTSUPP;
	}
	nfp_prog->stack_depth = round_up(prog->aux->stack_depth, 4);

	max_instr = nn_readw(nn, NFP_NET_CFG_BPF_MAX_LEN);
	nfp_prog->__prog_alloc_len = max_instr * sizeof(u64);

	nfp_prog->prog = kvmalloc(nfp_prog->__prog_alloc_len, GFP_KERNEL);
	if (!nfp_prog->prog)
		return -ENOMEM;

	err = nfp_bpf_jit(nfp_prog);
	if (err)
		return err;

	prog->aux->offload->jited_len = nfp_prog->prog_len * sizeof(u64);
	prog->aux->offload->jited_image = nfp_prog->prog;

	return 0;
}

static int nfp_bpf_destroy(struct nfp_net *nn, struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;

	kvfree(nfp_prog->prog);
	nfp_prog_free(nfp_prog);

	return 0;
}

/* Atomic engine requires values to be in big endian, we need to byte swap
 * the value words used with xadd.
 */
static void nfp_map_bpf_byte_swap(struct nfp_bpf_map *nfp_map, void *value)
{
	u32 *word = value;
	unsigned int i;

	for (i = 0; i < DIV_ROUND_UP(nfp_map->offmap->map.value_size, 4); i++)
		if (nfp_map->use_map[i] == NFP_MAP_USE_ATOMIC_CNT)
			word[i] = (__force u32)cpu_to_be32(word[i]);
}

static int
nfp_bpf_map_lookup_entry(struct bpf_offloaded_map *offmap,
			 void *key, void *value)
{
	int err;

	err = nfp_bpf_ctrl_lookup_entry(offmap, key, value);
	if (err)
		return err;

	nfp_map_bpf_byte_swap(offmap->dev_priv, value);
	return 0;
}

static int
nfp_bpf_map_update_entry(struct bpf_offloaded_map *offmap,
			 void *key, void *value, u64 flags)
{
	nfp_map_bpf_byte_swap(offmap->dev_priv, value);
	return nfp_bpf_ctrl_update_entry(offmap, key, value, flags);
}

static int
nfp_bpf_map_get_next_key(struct bpf_offloaded_map *offmap,
			 void *key, void *next_key)
{
	if (!key)
		return nfp_bpf_ctrl_getfirst_entry(offmap, next_key);
	return nfp_bpf_ctrl_getnext_entry(offmap, key, next_key);
}

static int
nfp_bpf_map_delete_elem(struct bpf_offloaded_map *offmap, void *key)
{
	if (offmap->map.map_type == BPF_MAP_TYPE_ARRAY)
		return -EINVAL;
	return nfp_bpf_ctrl_del_entry(offmap, key);
}

static const struct bpf_map_dev_ops nfp_bpf_map_ops = {
	.map_get_next_key	= nfp_bpf_map_get_next_key,
	.map_lookup_elem	= nfp_bpf_map_lookup_entry,
	.map_update_elem	= nfp_bpf_map_update_entry,
	.map_delete_elem	= nfp_bpf_map_delete_elem,
};

static int
nfp_bpf_map_alloc(struct nfp_app_bpf *bpf, struct bpf_offloaded_map *offmap)
{
	struct nfp_bpf_map *nfp_map;
	unsigned int use_map_size;
	long long int res;

	if (!bpf->maps.types)
		return -EOPNOTSUPP;

	if (offmap->map.map_flags ||
	    offmap->map.numa_node != NUMA_NO_NODE) {
		pr_info("map flags are not supported\n");
		return -EINVAL;
	}

	if (!(bpf->maps.types & 1 << offmap->map.map_type)) {
		pr_info("map type not supported\n");
		return -EOPNOTSUPP;
	}
	if (bpf->maps.max_maps == bpf->maps_in_use) {
		pr_info("too many maps for a device\n");
		return -ENOMEM;
	}
	if (bpf->maps.max_elems - bpf->map_elems_in_use <
	    offmap->map.max_entries) {
		pr_info("map with too many elements: %u, left: %u\n",
			offmap->map.max_entries,
			bpf->maps.max_elems - bpf->map_elems_in_use);
		return -ENOMEM;
	}
	if (offmap->map.key_size > bpf->maps.max_key_sz ||
	    offmap->map.value_size > bpf->maps.max_val_sz ||
	    round_up(offmap->map.key_size, 8) +
	    round_up(offmap->map.value_size, 8) > bpf->maps.max_elem_sz) {
		pr_info("elements don't fit in device constraints\n");
		return -ENOMEM;
	}

	use_map_size = DIV_ROUND_UP(offmap->map.value_size, 4) *
		       FIELD_SIZEOF(struct nfp_bpf_map, use_map[0]);

	nfp_map = kzalloc(sizeof(*nfp_map) + use_map_size, GFP_USER);
	if (!nfp_map)
		return -ENOMEM;

	offmap->dev_priv = nfp_map;
	nfp_map->offmap = offmap;
	nfp_map->bpf = bpf;

	res = nfp_bpf_ctrl_alloc_map(bpf, &offmap->map);
	if (res < 0) {
		kfree(nfp_map);
		return res;
	}

	nfp_map->tid = res;
	offmap->dev_ops = &nfp_bpf_map_ops;
	bpf->maps_in_use++;
	bpf->map_elems_in_use += offmap->map.max_entries;
	list_add_tail(&nfp_map->l, &bpf->map_list);

	return 0;
}

static int
nfp_bpf_map_free(struct nfp_app_bpf *bpf, struct bpf_offloaded_map *offmap)
{
	struct nfp_bpf_map *nfp_map = offmap->dev_priv;

	nfp_bpf_ctrl_free_map(bpf, nfp_map);
	list_del_init(&nfp_map->l);
	bpf->map_elems_in_use -= offmap->map.max_entries;
	bpf->maps_in_use--;
	kfree(nfp_map);

	return 0;
}

int nfp_ndo_bpf(struct nfp_app *app, struct nfp_net *nn, struct netdev_bpf *bpf)
{
	switch (bpf->command) {
	case BPF_OFFLOAD_VERIFIER_PREP:
		return nfp_bpf_verifier_prep(app, nn, bpf);
	case BPF_OFFLOAD_TRANSLATE:
		return nfp_bpf_translate(nn, bpf->offload.prog);
	case BPF_OFFLOAD_DESTROY:
		return nfp_bpf_destroy(nn, bpf->offload.prog);
	case BPF_OFFLOAD_MAP_ALLOC:
		return nfp_bpf_map_alloc(app->priv, bpf->offmap);
	case BPF_OFFLOAD_MAP_FREE:
		return nfp_bpf_map_free(app->priv, bpf->offmap);
	default:
		return -EINVAL;
	}
}

static int
nfp_net_bpf_load(struct nfp_net *nn, struct bpf_prog *prog,
		 struct netlink_ext_ack *extack)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;
	unsigned int max_mtu;
	dma_addr_t dma_addr;
	void *img;
	int err;

	max_mtu = nn_readb(nn, NFP_NET_CFG_BPF_INL_MTU) * 64 - 32;
	if (max_mtu < nn->dp.netdev->mtu) {
		NL_SET_ERR_MSG_MOD(extack, "BPF offload not supported with MTU larger than HW packet split boundary");
		return -EOPNOTSUPP;
	}

	img = nfp_bpf_relo_for_vnic(nfp_prog, nn->app_priv);
	if (IS_ERR(img))
		return PTR_ERR(img);

	dma_addr = dma_map_single(nn->dp.dev, img,
				  nfp_prog->prog_len * sizeof(u64),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(nn->dp.dev, dma_addr)) {
		kfree(img);
		return -ENOMEM;
	}

	nn_writew(nn, NFP_NET_CFG_BPF_SIZE, nfp_prog->prog_len);
	nn_writeq(nn, NFP_NET_CFG_BPF_ADDR, dma_addr);

	/* Load up the JITed code */
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_BPF);
	if (err)
		NL_SET_ERR_MSG_MOD(extack,
				   "FW command error while loading BPF");

	dma_unmap_single(nn->dp.dev, dma_addr, nfp_prog->prog_len * sizeof(u64),
			 DMA_TO_DEVICE);
	kfree(img);

	return err;
}

static void
nfp_net_bpf_start(struct nfp_net *nn, struct netlink_ext_ack *extack)
{
	int err;

	/* Enable passing packets through BPF function */
	nn->dp.ctrl |= NFP_NET_CFG_CTRL_BPF;
	nn_writel(nn, NFP_NET_CFG_CTRL, nn->dp.ctrl);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
	if (err)
		NL_SET_ERR_MSG_MOD(extack,
				   "FW command error while enabling BPF");
}

static int nfp_net_bpf_stop(struct nfp_net *nn)
{
	if (!(nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF))
		return 0;

	nn->dp.ctrl &= ~NFP_NET_CFG_CTRL_BPF;
	nn_writel(nn, NFP_NET_CFG_CTRL, nn->dp.ctrl);

	return nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
}

int nfp_net_bpf_offload(struct nfp_net *nn, struct bpf_prog *prog,
			bool old_prog, struct netlink_ext_ack *extack)
{
	int err;

	if (prog) {
		struct bpf_prog_offload *offload = prog->aux->offload;

		if (!offload)
			return -EINVAL;
		if (offload->netdev != nn->dp.netdev)
			return -EINVAL;
	}

	if (prog && old_prog) {
		u8 cap;

		cap = nn_readb(nn, NFP_NET_CFG_BPF_CAP);
		if (!(cap & NFP_NET_BPF_CAP_RELO)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "FW does not support live reload");
			return -EBUSY;
		}
	}

	/* Something else is loaded, different program type? */
	if (!old_prog && nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF)
		return -EBUSY;

	if (old_prog && !prog)
		return nfp_net_bpf_stop(nn);

	err = nfp_net_bpf_load(nn, prog, extack);
	if (err)
		return err;

	if (!old_prog)
		nfp_net_bpf_start(nn, extack);

	return 0;
}
