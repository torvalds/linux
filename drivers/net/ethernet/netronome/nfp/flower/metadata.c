// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/hash.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <linux/math64.h>
#include <linux/vmalloc.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_app.h"

struct nfp_mask_id_table {
	struct hlist_node link;
	u32 hash_key;
	u32 ref_cnt;
	u8 mask_id;
};

struct nfp_fl_flow_table_cmp_arg {
	struct net_device *netdev;
	unsigned long cookie;
};

struct nfp_fl_stats_ctx_to_flow {
	struct rhash_head ht_node;
	u32 stats_cxt;
	struct nfp_fl_payload *flow;
};

static const struct rhashtable_params stats_ctx_table_params = {
	.key_offset	= offsetof(struct nfp_fl_stats_ctx_to_flow, stats_cxt),
	.head_offset	= offsetof(struct nfp_fl_stats_ctx_to_flow, ht_node),
	.key_len	= sizeof(u32),
};

static int nfp_release_stats_entry(struct nfp_app *app, u32 stats_context_id)
{
	struct nfp_flower_priv *priv = app->priv;
	struct circ_buf *ring;

	ring = &priv->stats_ids.free_list;
	/* Check if buffer is full. */
	if (!CIRC_SPACE(ring->head, ring->tail,
			priv->stats_ring_size * NFP_FL_STATS_ELEM_RS -
			NFP_FL_STATS_ELEM_RS + 1))
		return -ENOBUFS;

	memcpy(&ring->buf[ring->head], &stats_context_id, NFP_FL_STATS_ELEM_RS);
	ring->head = (ring->head + NFP_FL_STATS_ELEM_RS) %
		     (priv->stats_ring_size * NFP_FL_STATS_ELEM_RS);

	return 0;
}

static int nfp_get_stats_entry(struct nfp_app *app, u32 *stats_context_id)
{
	struct nfp_flower_priv *priv = app->priv;
	u32 freed_stats_id, temp_stats_id;
	struct circ_buf *ring;

	ring = &priv->stats_ids.free_list;
	freed_stats_id = priv->stats_ring_size;
	/* Check for unallocated entries first. */
	if (priv->stats_ids.init_unalloc > 0) {
		if (priv->active_mem_unit == priv->total_mem_units) {
			priv->stats_ids.init_unalloc--;
			priv->active_mem_unit = 0;
		}

		*stats_context_id =
			FIELD_PREP(NFP_FL_STAT_ID_STAT,
				   priv->stats_ids.init_unalloc - 1) |
			FIELD_PREP(NFP_FL_STAT_ID_MU_NUM,
				   priv->active_mem_unit);
		priv->active_mem_unit++;
		return 0;
	}

	/* Check if buffer is empty. */
	if (ring->head == ring->tail) {
		*stats_context_id = freed_stats_id;
		return -ENOENT;
	}

	memcpy(&temp_stats_id, &ring->buf[ring->tail], NFP_FL_STATS_ELEM_RS);
	*stats_context_id = temp_stats_id;
	memcpy(&ring->buf[ring->tail], &freed_stats_id, NFP_FL_STATS_ELEM_RS);
	ring->tail = (ring->tail + NFP_FL_STATS_ELEM_RS) %
		     (priv->stats_ring_size * NFP_FL_STATS_ELEM_RS);

	return 0;
}

/* Must be called with either RTNL or rcu_read_lock */
struct nfp_fl_payload *
nfp_flower_search_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie,
			   struct net_device *netdev)
{
	struct nfp_fl_flow_table_cmp_arg flower_cmp_arg;
	struct nfp_flower_priv *priv = app->priv;

	flower_cmp_arg.netdev = netdev;
	flower_cmp_arg.cookie = tc_flower_cookie;

	return rhashtable_lookup_fast(&priv->flow_table, &flower_cmp_arg,
				      nfp_flower_table_params);
}

void nfp_flower_rx_flow_stats(struct nfp_app *app, struct sk_buff *skb)
{
	unsigned int msg_len = nfp_flower_cmsg_get_data_len(skb);
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_stats_frame *stats;
	unsigned char *msg;
	u32 ctx_id;
	int i;

	msg = nfp_flower_cmsg_get_data(skb);

	spin_lock(&priv->stats_lock);
	for (i = 0; i < msg_len / sizeof(*stats); i++) {
		stats = (struct nfp_fl_stats_frame *)msg + i;
		ctx_id = be32_to_cpu(stats->stats_con_id);
		priv->stats[ctx_id].pkts += be32_to_cpu(stats->pkt_count);
		priv->stats[ctx_id].bytes += be64_to_cpu(stats->byte_count);
		priv->stats[ctx_id].used = jiffies;
	}
	spin_unlock(&priv->stats_lock);
}

static int nfp_release_mask_id(struct nfp_app *app, u8 mask_id)
{
	struct nfp_flower_priv *priv = app->priv;
	struct circ_buf *ring;

	ring = &priv->mask_ids.mask_id_free_list;
	/* Checking if buffer is full. */
	if (CIRC_SPACE(ring->head, ring->tail, NFP_FLOWER_MASK_ENTRY_RS) == 0)
		return -ENOBUFS;

	memcpy(&ring->buf[ring->head], &mask_id, NFP_FLOWER_MASK_ELEMENT_RS);
	ring->head = (ring->head + NFP_FLOWER_MASK_ELEMENT_RS) %
		     (NFP_FLOWER_MASK_ENTRY_RS * NFP_FLOWER_MASK_ELEMENT_RS);

	priv->mask_ids.last_used[mask_id] = ktime_get();

	return 0;
}

static int nfp_mask_alloc(struct nfp_app *app, u8 *mask_id)
{
	struct nfp_flower_priv *priv = app->priv;
	ktime_t reuse_timeout;
	struct circ_buf *ring;
	u8 temp_id, freed_id;

	ring = &priv->mask_ids.mask_id_free_list;
	freed_id = NFP_FLOWER_MASK_ENTRY_RS - 1;
	/* Checking for unallocated entries first. */
	if (priv->mask_ids.init_unallocated > 0) {
		*mask_id = priv->mask_ids.init_unallocated;
		priv->mask_ids.init_unallocated--;
		return 0;
	}

	/* Checking if buffer is empty. */
	if (ring->head == ring->tail)
		goto err_not_found;

	memcpy(&temp_id, &ring->buf[ring->tail], NFP_FLOWER_MASK_ELEMENT_RS);
	*mask_id = temp_id;

	reuse_timeout = ktime_add_ns(priv->mask_ids.last_used[*mask_id],
				     NFP_FL_MASK_REUSE_TIME_NS);

	if (ktime_before(ktime_get(), reuse_timeout))
		goto err_not_found;

	memcpy(&ring->buf[ring->tail], &freed_id, NFP_FLOWER_MASK_ELEMENT_RS);
	ring->tail = (ring->tail + NFP_FLOWER_MASK_ELEMENT_RS) %
		     (NFP_FLOWER_MASK_ENTRY_RS * NFP_FLOWER_MASK_ELEMENT_RS);

	return 0;

err_not_found:
	*mask_id = freed_id;
	return -ENOENT;
}

static int
nfp_add_mask_table(struct nfp_app *app, char *mask_data, u32 mask_len)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_mask_id_table *mask_entry;
	unsigned long hash_key;
	u8 mask_id;

	if (nfp_mask_alloc(app, &mask_id))
		return -ENOENT;

	mask_entry = kmalloc(sizeof(*mask_entry), GFP_KERNEL);
	if (!mask_entry) {
		nfp_release_mask_id(app, mask_id);
		return -ENOMEM;
	}

	INIT_HLIST_NODE(&mask_entry->link);
	mask_entry->mask_id = mask_id;
	hash_key = jhash(mask_data, mask_len, priv->mask_id_seed);
	mask_entry->hash_key = hash_key;
	mask_entry->ref_cnt = 1;
	hash_add(priv->mask_table, &mask_entry->link, hash_key);

	return mask_id;
}

static struct nfp_mask_id_table *
nfp_search_mask_table(struct nfp_app *app, char *mask_data, u32 mask_len)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_mask_id_table *mask_entry;
	unsigned long hash_key;

	hash_key = jhash(mask_data, mask_len, priv->mask_id_seed);

	hash_for_each_possible(priv->mask_table, mask_entry, link, hash_key)
		if (mask_entry->hash_key == hash_key)
			return mask_entry;

	return NULL;
}

static int
nfp_find_in_mask_table(struct nfp_app *app, char *mask_data, u32 mask_len)
{
	struct nfp_mask_id_table *mask_entry;

	mask_entry = nfp_search_mask_table(app, mask_data, mask_len);
	if (!mask_entry)
		return -ENOENT;

	mask_entry->ref_cnt++;

	/* Casting u8 to int for later use. */
	return mask_entry->mask_id;
}

static bool
nfp_check_mask_add(struct nfp_app *app, char *mask_data, u32 mask_len,
		   u8 *meta_flags, u8 *mask_id)
{
	int id;

	id = nfp_find_in_mask_table(app, mask_data, mask_len);
	if (id < 0) {
		id = nfp_add_mask_table(app, mask_data, mask_len);
		if (id < 0)
			return false;
		*meta_flags |= NFP_FL_META_FLAG_MANAGE_MASK;
	}
	*mask_id = id;

	return true;
}

static bool
nfp_check_mask_remove(struct nfp_app *app, char *mask_data, u32 mask_len,
		      u8 *meta_flags, u8 *mask_id)
{
	struct nfp_mask_id_table *mask_entry;

	mask_entry = nfp_search_mask_table(app, mask_data, mask_len);
	if (!mask_entry)
		return false;

	*mask_id = mask_entry->mask_id;
	mask_entry->ref_cnt--;
	if (!mask_entry->ref_cnt) {
		hash_del(&mask_entry->link);
		nfp_release_mask_id(app, *mask_id);
		kfree(mask_entry);
		if (meta_flags)
			*meta_flags |= NFP_FL_META_FLAG_MANAGE_MASK;
	}

	return true;
}

int nfp_compile_flow_metadata(struct nfp_app *app,
			      struct flow_cls_offload *flow,
			      struct nfp_fl_payload *nfp_flow,
			      struct net_device *netdev,
			      struct netlink_ext_ack *extack)
{
	struct nfp_fl_stats_ctx_to_flow *ctx_entry;
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *check_entry;
	u8 new_mask_id;
	u32 stats_cxt;
	int err;

	err = nfp_get_stats_entry(app, &stats_cxt);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot allocate new stats context");
		return err;
	}

	nfp_flow->meta.host_ctx_id = cpu_to_be32(stats_cxt);
	nfp_flow->meta.host_cookie = cpu_to_be64(flow->cookie);
	nfp_flow->ingress_dev = netdev;

	ctx_entry = kzalloc(sizeof(*ctx_entry), GFP_KERNEL);
	if (!ctx_entry) {
		err = -ENOMEM;
		goto err_release_stats;
	}

	ctx_entry->stats_cxt = stats_cxt;
	ctx_entry->flow = nfp_flow;

	if (rhashtable_insert_fast(&priv->stats_ctx_table, &ctx_entry->ht_node,
				   stats_ctx_table_params)) {
		err = -ENOMEM;
		goto err_free_ctx_entry;
	}

	new_mask_id = 0;
	if (!nfp_check_mask_add(app, nfp_flow->mask_data,
				nfp_flow->meta.mask_len,
				&nfp_flow->meta.flags, &new_mask_id)) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot allocate a new mask id");
		if (nfp_release_stats_entry(app, stats_cxt)) {
			NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot release stats context");
			err = -EINVAL;
			goto err_remove_rhash;
		}
		err = -ENOENT;
		goto err_remove_rhash;
	}

	nfp_flow->meta.flow_version = cpu_to_be64(priv->flower_version);
	priv->flower_version++;

	/* Update flow payload with mask ids. */
	nfp_flow->unmasked_data[NFP_FL_MASK_ID_LOCATION] = new_mask_id;
	priv->stats[stats_cxt].pkts = 0;
	priv->stats[stats_cxt].bytes = 0;
	priv->stats[stats_cxt].used = jiffies;

	check_entry = nfp_flower_search_fl_table(app, flow->cookie, netdev);
	if (check_entry) {
		NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot offload duplicate flow entry");
		if (nfp_release_stats_entry(app, stats_cxt)) {
			NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot release stats context");
			err = -EINVAL;
			goto err_remove_mask;
		}

		if (!nfp_check_mask_remove(app, nfp_flow->mask_data,
					   nfp_flow->meta.mask_len,
					   NULL, &new_mask_id)) {
			NL_SET_ERR_MSG_MOD(extack, "invalid entry: cannot release mask id");
			err = -EINVAL;
			goto err_remove_mask;
		}

		err = -EEXIST;
		goto err_remove_mask;
	}

	return 0;

err_remove_mask:
	nfp_check_mask_remove(app, nfp_flow->mask_data, nfp_flow->meta.mask_len,
			      NULL, &new_mask_id);
err_remove_rhash:
	WARN_ON_ONCE(rhashtable_remove_fast(&priv->stats_ctx_table,
					    &ctx_entry->ht_node,
					    stats_ctx_table_params));
err_free_ctx_entry:
	kfree(ctx_entry);
err_release_stats:
	nfp_release_stats_entry(app, stats_cxt);

	return err;
}

void __nfp_modify_flow_metadata(struct nfp_flower_priv *priv,
				struct nfp_fl_payload *nfp_flow)
{
	nfp_flow->meta.flags &= ~NFP_FL_META_FLAG_MANAGE_MASK;
	nfp_flow->meta.flow_version = cpu_to_be64(priv->flower_version);
	priv->flower_version++;
}

int nfp_modify_flow_metadata(struct nfp_app *app,
			     struct nfp_fl_payload *nfp_flow)
{
	struct nfp_fl_stats_ctx_to_flow *ctx_entry;
	struct nfp_flower_priv *priv = app->priv;
	u8 new_mask_id = 0;
	u32 temp_ctx_id;

	__nfp_modify_flow_metadata(priv, nfp_flow);

	nfp_check_mask_remove(app, nfp_flow->mask_data,
			      nfp_flow->meta.mask_len, &nfp_flow->meta.flags,
			      &new_mask_id);

	/* Update flow payload with mask ids. */
	nfp_flow->unmasked_data[NFP_FL_MASK_ID_LOCATION] = new_mask_id;

	/* Release the stats ctx id and ctx to flow table entry. */
	temp_ctx_id = be32_to_cpu(nfp_flow->meta.host_ctx_id);

	ctx_entry = rhashtable_lookup_fast(&priv->stats_ctx_table, &temp_ctx_id,
					   stats_ctx_table_params);
	if (!ctx_entry)
		return -ENOENT;

	WARN_ON_ONCE(rhashtable_remove_fast(&priv->stats_ctx_table,
					    &ctx_entry->ht_node,
					    stats_ctx_table_params));
	kfree(ctx_entry);

	return nfp_release_stats_entry(app, temp_ctx_id);
}

struct nfp_fl_payload *
nfp_flower_get_fl_payload_from_ctx(struct nfp_app *app, u32 ctx_id)
{
	struct nfp_fl_stats_ctx_to_flow *ctx_entry;
	struct nfp_flower_priv *priv = app->priv;

	ctx_entry = rhashtable_lookup_fast(&priv->stats_ctx_table, &ctx_id,
					   stats_ctx_table_params);
	if (!ctx_entry)
		return NULL;

	return ctx_entry->flow;
}

static int nfp_fl_obj_cmpfn(struct rhashtable_compare_arg *arg,
			    const void *obj)
{
	const struct nfp_fl_flow_table_cmp_arg *cmp_arg = arg->key;
	const struct nfp_fl_payload *flow_entry = obj;

	if (flow_entry->ingress_dev == cmp_arg->netdev)
		return flow_entry->tc_flower_cookie != cmp_arg->cookie;

	return 1;
}

static u32 nfp_fl_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct nfp_fl_payload *flower_entry = data;

	return jhash2((u32 *)&flower_entry->tc_flower_cookie,
		      sizeof(flower_entry->tc_flower_cookie) / sizeof(u32),
		      seed);
}

static u32 nfp_fl_key_hashfn(const void *data, u32 len, u32 seed)
{
	const struct nfp_fl_flow_table_cmp_arg *cmp_arg = data;

	return jhash2((u32 *)&cmp_arg->cookie,
		      sizeof(cmp_arg->cookie) / sizeof(u32), seed);
}

const struct rhashtable_params nfp_flower_table_params = {
	.head_offset		= offsetof(struct nfp_fl_payload, fl_node),
	.hashfn			= nfp_fl_key_hashfn,
	.obj_cmpfn		= nfp_fl_obj_cmpfn,
	.obj_hashfn		= nfp_fl_obj_hashfn,
	.automatic_shrinking	= true,
};

int nfp_flower_metadata_init(struct nfp_app *app, u64 host_ctx_count,
			     unsigned int host_num_mems)
{
	struct nfp_flower_priv *priv = app->priv;
	int err, stats_size;

	hash_init(priv->mask_table);

	err = rhashtable_init(&priv->flow_table, &nfp_flower_table_params);
	if (err)
		return err;

	err = rhashtable_init(&priv->stats_ctx_table, &stats_ctx_table_params);
	if (err)
		goto err_free_flow_table;

	get_random_bytes(&priv->mask_id_seed, sizeof(priv->mask_id_seed));

	/* Init ring buffer and unallocated mask_ids. */
	priv->mask_ids.mask_id_free_list.buf =
		kmalloc_array(NFP_FLOWER_MASK_ENTRY_RS,
			      NFP_FLOWER_MASK_ELEMENT_RS, GFP_KERNEL);
	if (!priv->mask_ids.mask_id_free_list.buf)
		goto err_free_stats_ctx_table;

	priv->mask_ids.init_unallocated = NFP_FLOWER_MASK_ENTRY_RS - 1;

	/* Init timestamps for mask id*/
	priv->mask_ids.last_used =
		kmalloc_array(NFP_FLOWER_MASK_ENTRY_RS,
			      sizeof(*priv->mask_ids.last_used), GFP_KERNEL);
	if (!priv->mask_ids.last_used)
		goto err_free_mask_id;

	/* Init ring buffer and unallocated stats_ids. */
	priv->stats_ids.free_list.buf =
		vmalloc(array_size(NFP_FL_STATS_ELEM_RS,
				   priv->stats_ring_size));
	if (!priv->stats_ids.free_list.buf)
		goto err_free_last_used;

	priv->stats_ids.init_unalloc = div_u64(host_ctx_count, host_num_mems);

	stats_size = FIELD_PREP(NFP_FL_STAT_ID_STAT, host_ctx_count) |
		     FIELD_PREP(NFP_FL_STAT_ID_MU_NUM, host_num_mems - 1);
	priv->stats = kvmalloc_array(stats_size, sizeof(struct nfp_fl_stats),
				     GFP_KERNEL);
	if (!priv->stats)
		goto err_free_ring_buf;

	spin_lock_init(&priv->stats_lock);

	return 0;

err_free_ring_buf:
	vfree(priv->stats_ids.free_list.buf);
err_free_last_used:
	kfree(priv->mask_ids.last_used);
err_free_mask_id:
	kfree(priv->mask_ids.mask_id_free_list.buf);
err_free_stats_ctx_table:
	rhashtable_destroy(&priv->stats_ctx_table);
err_free_flow_table:
	rhashtable_destroy(&priv->flow_table);
	return -ENOMEM;
}

void nfp_flower_metadata_cleanup(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;

	if (!priv)
		return;

	rhashtable_free_and_destroy(&priv->flow_table,
				    nfp_check_rhashtable_empty, NULL);
	rhashtable_free_and_destroy(&priv->stats_ctx_table,
				    nfp_check_rhashtable_empty, NULL);
	kvfree(priv->stats);
	kfree(priv->mask_ids.mask_id_free_list.buf);
	kfree(priv->mask_ids.last_used);
	vfree(priv->stats_ids.free_list.buf);
}
