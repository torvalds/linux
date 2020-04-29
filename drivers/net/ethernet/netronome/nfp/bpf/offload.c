// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2016-2018 Netronome Systems, Inc. */

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
#include "../ccm.h"
#include "../nfp_app.h"
#include "../nfp_net_ctrl.h"
#include "../nfp_net.h"

static int
nfp_map_ptr_record(struct nfp_app_bpf *bpf, struct nfp_prog *nfp_prog,
		   struct bpf_map *map)
{
	struct nfp_bpf_neutral_map *record;
	int err;

	/* Reuse path - other offloaded program is already tracking this map. */
	record = rhashtable_lookup_fast(&bpf->maps_neutral, &map->id,
					nfp_bpf_maps_neutral_params);
	if (record) {
		nfp_prog->map_records[nfp_prog->map_records_cnt++] = record;
		record->count++;
		return 0;
	}

	/* Grab a single ref to the map for our record.  The prog destroy ndo
	 * happens after free_used_maps().
	 */
	bpf_map_inc(map);

	record = kmalloc(sizeof(*record), GFP_KERNEL);
	if (!record) {
		err = -ENOMEM;
		goto err_map_put;
	}

	record->ptr = map;
	record->map_id = map->id;
	record->count = 1;

	err = rhashtable_insert_fast(&bpf->maps_neutral, &record->l,
				     nfp_bpf_maps_neutral_params);
	if (err)
		goto err_free_rec;

	nfp_prog->map_records[nfp_prog->map_records_cnt++] = record;

	return 0;

err_free_rec:
	kfree(record);
err_map_put:
	bpf_map_put(map);
	return err;
}

static void
nfp_map_ptrs_forget(struct nfp_app_bpf *bpf, struct nfp_prog *nfp_prog)
{
	bool freed = false;
	int i;

	for (i = 0; i < nfp_prog->map_records_cnt; i++) {
		if (--nfp_prog->map_records[i]->count) {
			nfp_prog->map_records[i] = NULL;
			continue;
		}

		WARN_ON(rhashtable_remove_fast(&bpf->maps_neutral,
					       &nfp_prog->map_records[i]->l,
					       nfp_bpf_maps_neutral_params));
		freed = true;
	}

	if (freed) {
		synchronize_rcu();

		for (i = 0; i < nfp_prog->map_records_cnt; i++)
			if (nfp_prog->map_records[i]) {
				bpf_map_put(nfp_prog->map_records[i]->ptr);
				kfree(nfp_prog->map_records[i]);
			}
	}

	kfree(nfp_prog->map_records);
	nfp_prog->map_records = NULL;
	nfp_prog->map_records_cnt = 0;
}

static int
nfp_map_ptrs_record(struct nfp_app_bpf *bpf, struct nfp_prog *nfp_prog,
		    struct bpf_prog *prog)
{
	int i, cnt, err;

	/* Quickly count the maps we will have to remember */
	cnt = 0;
	for (i = 0; i < prog->aux->used_map_cnt; i++)
		if (bpf_map_offload_neutral(prog->aux->used_maps[i]))
			cnt++;
	if (!cnt)
		return 0;

	nfp_prog->map_records = kmalloc_array(cnt,
					      sizeof(nfp_prog->map_records[0]),
					      GFP_KERNEL);
	if (!nfp_prog->map_records)
		return -ENOMEM;

	for (i = 0; i < prog->aux->used_map_cnt; i++)
		if (bpf_map_offload_neutral(prog->aux->used_maps[i])) {
			err = nfp_map_ptr_record(bpf, nfp_prog,
						 prog->aux->used_maps[i]);
			if (err) {
				nfp_map_ptrs_forget(bpf, nfp_prog);
				return err;
			}
		}
	WARN_ON(cnt != nfp_prog->map_records_cnt);

	return 0;
}

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
		if (is_mbpf_alu(meta)) {
			meta->umin_src = U64_MAX;
			meta->umin_dst = U64_MAX;
		}

		list_add_tail(&meta->l, &nfp_prog->insns);
	}
	nfp_prog->n_insns = cnt;

	nfp_bpf_jit_prepare(nfp_prog);

	return 0;
}

static void nfp_prog_free(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *tmp;

	kfree(nfp_prog->subprog);

	list_for_each_entry_safe(meta, tmp, &nfp_prog->insns, l) {
		list_del(&meta->l);
		kfree(meta);
	}
	kfree(nfp_prog);
}

static int nfp_bpf_verifier_prep(struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog;
	int ret;

	nfp_prog = kzalloc(sizeof(*nfp_prog), GFP_KERNEL);
	if (!nfp_prog)
		return -ENOMEM;
	prog->aux->offload->dev_priv = nfp_prog;

	INIT_LIST_HEAD(&nfp_prog->insns);
	nfp_prog->type = prog->type;
	nfp_prog->bpf = bpf_offload_dev_priv(prog->aux->offload->offdev);

	ret = nfp_prog_prepare(nfp_prog, prog->insnsi, prog->len);
	if (ret)
		goto err_free;

	nfp_prog->verifier_meta = nfp_prog_first_meta(nfp_prog);

	return 0;

err_free:
	nfp_prog_free(nfp_prog);

	return ret;
}

static int nfp_bpf_translate(struct bpf_prog *prog)
{
	struct nfp_net *nn = netdev_priv(prog->aux->offload->netdev);
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;
	unsigned int max_instr;
	int err;

	/* We depend on dead code elimination succeeding */
	if (prog->aux->offload->opt_failed)
		return -EINVAL;

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

	return nfp_map_ptrs_record(nfp_prog->bpf, nfp_prog, prog);
}

static void nfp_bpf_destroy(struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;

	kvfree(nfp_prog->prog);
	nfp_map_ptrs_forget(nfp_prog->bpf, nfp_prog);
	nfp_prog_free(nfp_prog);
}

/* Atomic engine requires values to be in big endian, we need to byte swap
 * the value words used with xadd.
 */
static void nfp_map_bpf_byte_swap(struct nfp_bpf_map *nfp_map, void *value)
{
	u32 *word = value;
	unsigned int i;

	for (i = 0; i < DIV_ROUND_UP(nfp_map->offmap->map.value_size, 4); i++)
		if (nfp_map->use_map[i].type == NFP_MAP_USE_ATOMIC_CNT)
			word[i] = (__force u32)cpu_to_be32(word[i]);
}

/* Mark value as unsafely initialized in case it becomes atomic later
 * and we didn't byte swap something non-byte swap neutral.
 */
static void
nfp_map_bpf_byte_swap_record(struct nfp_bpf_map *nfp_map, void *value)
{
	u32 *word = value;
	unsigned int i;

	for (i = 0; i < DIV_ROUND_UP(nfp_map->offmap->map.value_size, 4); i++)
		if (nfp_map->use_map[i].type == NFP_MAP_UNUSED &&
		    word[i] != (__force u32)cpu_to_be32(word[i]))
			nfp_map->use_map[i].non_zero_update = 1;
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
	nfp_map_bpf_byte_swap_record(offmap->dev_priv, value);
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

	if (round_up(offmap->map.key_size, 8) +
	    round_up(offmap->map.value_size, 8) > bpf->maps.max_elem_sz) {
		pr_info("map elements too large: %u, FW max element size (key+value): %u\n",
			round_up(offmap->map.key_size, 8) +
			round_up(offmap->map.value_size, 8),
			bpf->maps.max_elem_sz);
		return -ENOMEM;
	}
	if (offmap->map.key_size > bpf->maps.max_key_sz) {
		pr_info("map key size %u, FW max is %u\n",
			offmap->map.key_size, bpf->maps.max_key_sz);
		return -ENOMEM;
	}
	if (offmap->map.value_size > bpf->maps.max_val_sz) {
		pr_info("map value size %u, FW max is %u\n",
			offmap->map.value_size, bpf->maps.max_val_sz);
		return -ENOMEM;
	}

	use_map_size = DIV_ROUND_UP(offmap->map.value_size, 4) *
		       sizeof_field(struct nfp_bpf_map, use_map[0]);

	nfp_map = kzalloc(sizeof(*nfp_map) + use_map_size, GFP_USER);
	if (!nfp_map)
		return -ENOMEM;

	offmap->dev_priv = nfp_map;
	nfp_map->offmap = offmap;
	nfp_map->bpf = bpf;
	spin_lock_init(&nfp_map->cache_lock);

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
	dev_consume_skb_any(nfp_map->cache);
	WARN_ON_ONCE(nfp_map->cache_blockers);
	list_del_init(&nfp_map->l);
	bpf->map_elems_in_use -= offmap->map.max_entries;
	bpf->maps_in_use--;
	kfree(nfp_map);

	return 0;
}

int nfp_ndo_bpf(struct nfp_app *app, struct nfp_net *nn, struct netdev_bpf *bpf)
{
	switch (bpf->command) {
	case BPF_OFFLOAD_MAP_ALLOC:
		return nfp_bpf_map_alloc(app->priv, bpf->offmap);
	case BPF_OFFLOAD_MAP_FREE:
		return nfp_bpf_map_free(app->priv, bpf->offmap);
	default:
		return -EINVAL;
	}
}

static unsigned long
nfp_bpf_perf_event_copy(void *dst, const void *src,
			unsigned long off, unsigned long len)
{
	memcpy(dst, src + off, len);
	return 0;
}

int nfp_bpf_event_output(struct nfp_app_bpf *bpf, const void *data,
			 unsigned int len)
{
	struct cmsg_bpf_event *cbe = (void *)data;
	struct nfp_bpf_neutral_map *record;
	u32 pkt_size, data_size, map_id;
	u64 map_id_full;

	if (len < sizeof(struct cmsg_bpf_event))
		return -EINVAL;

	pkt_size = be32_to_cpu(cbe->pkt_size);
	data_size = be32_to_cpu(cbe->data_size);
	map_id_full = be64_to_cpu(cbe->map_ptr);
	map_id = map_id_full;

	if (len < sizeof(struct cmsg_bpf_event) + pkt_size + data_size)
		return -EINVAL;
	if (cbe->hdr.ver != NFP_CCM_ABI_VERSION)
		return -EINVAL;

	rcu_read_lock();
	record = rhashtable_lookup(&bpf->maps_neutral, &map_id,
				   nfp_bpf_maps_neutral_params);
	if (!record || map_id_full > U32_MAX) {
		rcu_read_unlock();
		cmsg_warn(bpf, "perf event: map id %lld (0x%llx) not recognized, dropping event\n",
			  map_id_full, map_id_full);
		return -EINVAL;
	}

	bpf_event_output(record->ptr, be32_to_cpu(cbe->cpu_id),
			 &cbe->data[round_up(pkt_size, 4)], data_size,
			 cbe->data, pkt_size, nfp_bpf_perf_event_copy);
	rcu_read_unlock();

	return 0;
}

static int
nfp_net_bpf_load(struct nfp_net *nn, struct bpf_prog *prog,
		 struct netlink_ext_ack *extack)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;
	unsigned int fw_mtu, pkt_off, max_stack, max_prog_len;
	dma_addr_t dma_addr;
	void *img;
	int err;

	fw_mtu = nn_readb(nn, NFP_NET_CFG_BPF_INL_MTU) * 64 - 32;
	pkt_off = min(prog->aux->max_pkt_offset, nn->dp.netdev->mtu);
	if (fw_mtu < pkt_off) {
		NL_SET_ERR_MSG_MOD(extack, "BPF offload not supported with potential packet access beyond HW packet split boundary");
		return -EOPNOTSUPP;
	}

	max_stack = nn_readb(nn, NFP_NET_CFG_BPF_STACK_SZ) * 64;
	if (nfp_prog->stack_size > max_stack) {
		NL_SET_ERR_MSG_MOD(extack, "stack too large");
		return -EOPNOTSUPP;
	}

	max_prog_len = nn_readw(nn, NFP_NET_CFG_BPF_MAX_LEN);
	if (nfp_prog->prog_len > max_prog_len) {
		NL_SET_ERR_MSG_MOD(extack, "program too long");
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

	if (prog && !bpf_offload_dev_match(prog, nn->dp.netdev))
		return -EINVAL;

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

const struct bpf_prog_offload_ops nfp_bpf_dev_ops = {
	.insn_hook	= nfp_verify_insn,
	.finalize	= nfp_bpf_finalize,
	.replace_insn	= nfp_bpf_opt_replace_insn,
	.remove_insns	= nfp_bpf_opt_remove_insns,
	.prepare	= nfp_bpf_verifier_prep,
	.translate	= nfp_bpf_translate,
	.destroy	= nfp_bpf_destroy,
};
