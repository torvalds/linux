/*
 * Copyright (C) 2017 Netronome Systems, Inc.
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

#include <linux/hash.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
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

static int nfp_release_stats_entry(struct nfp_app *app, u32 stats_context_id)
{
	struct nfp_flower_priv *priv = app->priv;
	struct circ_buf *ring;

	ring = &priv->stats_ids.free_list;
	/* Check if buffer is full. */
	if (!CIRC_SPACE(ring->head, ring->tail, NFP_FL_STATS_ENTRY_RS *
			NFP_FL_STATS_ELEM_RS -
			NFP_FL_STATS_ELEM_RS + 1))
		return -ENOBUFS;

	memcpy(&ring->buf[ring->head], &stats_context_id, NFP_FL_STATS_ELEM_RS);
	ring->head = (ring->head + NFP_FL_STATS_ELEM_RS) %
		     (NFP_FL_STATS_ENTRY_RS * NFP_FL_STATS_ELEM_RS);

	return 0;
}

static int nfp_get_stats_entry(struct nfp_app *app, u32 *stats_context_id)
{
	struct nfp_flower_priv *priv = app->priv;
	u32 freed_stats_id, temp_stats_id;
	struct circ_buf *ring;

	ring = &priv->stats_ids.free_list;
	freed_stats_id = NFP_FL_STATS_ENTRY_RS;
	/* Check for unallocated entries first. */
	if (priv->stats_ids.init_unalloc > 0) {
		*stats_context_id = priv->stats_ids.init_unalloc - 1;
		priv->stats_ids.init_unalloc--;
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
		     (NFP_FL_STATS_ENTRY_RS * NFP_FL_STATS_ELEM_RS);

	return 0;
}

/* Must be called with either RTNL or rcu_read_lock */
struct nfp_fl_payload *
nfp_flower_search_fl_table(struct nfp_app *app, unsigned long tc_flower_cookie)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *flower_entry;

	hash_for_each_possible_rcu(priv->flow_table, flower_entry, link,
				   tc_flower_cookie)
		if (flower_entry->tc_flower_cookie == tc_flower_cookie)
			return flower_entry;

	return NULL;
}

static void
nfp_flower_update_stats(struct nfp_app *app, struct nfp_fl_stats_frame *stats)
{
	struct nfp_fl_payload *nfp_flow;
	unsigned long flower_cookie;

	flower_cookie = be64_to_cpu(stats->stats_cookie);

	rcu_read_lock();
	nfp_flow = nfp_flower_search_fl_table(app, flower_cookie);
	if (!nfp_flow)
		goto exit_rcu_unlock;

	if (nfp_flow->meta.host_ctx_id != stats->stats_con_id)
		goto exit_rcu_unlock;

	spin_lock(&nfp_flow->lock);
	nfp_flow->stats.pkts += be32_to_cpu(stats->pkt_count);
	nfp_flow->stats.bytes += be64_to_cpu(stats->byte_count);
	nfp_flow->stats.used = jiffies;
	spin_unlock(&nfp_flow->lock);

exit_rcu_unlock:
	rcu_read_unlock();
}

void nfp_flower_rx_flow_stats(struct nfp_app *app, struct sk_buff *skb)
{
	unsigned int msg_len = nfp_flower_cmsg_get_data_len(skb);
	struct nfp_fl_stats_frame *stats_frame;
	unsigned char *msg;
	int i;

	msg = nfp_flower_cmsg_get_data(skb);

	stats_frame = (struct nfp_fl_stats_frame *)msg;
	for (i = 0; i < msg_len / sizeof(*stats_frame); i++)
		nfp_flower_update_stats(app, stats_frame + i);
}

static int nfp_release_mask_id(struct nfp_app *app, u8 mask_id)
{
	struct nfp_flower_priv *priv = app->priv;
	struct circ_buf *ring;
	struct timespec64 now;

	ring = &priv->mask_ids.mask_id_free_list;
	/* Checking if buffer is full. */
	if (CIRC_SPACE(ring->head, ring->tail, NFP_FLOWER_MASK_ENTRY_RS) == 0)
		return -ENOBUFS;

	memcpy(&ring->buf[ring->head], &mask_id, NFP_FLOWER_MASK_ELEMENT_RS);
	ring->head = (ring->head + NFP_FLOWER_MASK_ELEMENT_RS) %
		     (NFP_FLOWER_MASK_ENTRY_RS * NFP_FLOWER_MASK_ELEMENT_RS);

	getnstimeofday64(&now);
	priv->mask_ids.last_used[mask_id] = now;

	return 0;
}

static int nfp_mask_alloc(struct nfp_app *app, u8 *mask_id)
{
	struct nfp_flower_priv *priv = app->priv;
	struct timespec64 delta, now;
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

	getnstimeofday64(&now);
	delta = timespec64_sub(now, priv->mask_ids.last_used[*mask_id]);

	if (timespec64_to_ns(&delta) < NFP_FL_MASK_REUSE_TIME_NS)
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

	if (meta_flags)
		*meta_flags &= ~NFP_FL_META_FLAG_MANAGE_MASK;

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
			      struct tc_cls_flower_offload *flow,
			      struct nfp_fl_payload *nfp_flow)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_fl_payload *check_entry;
	u8 new_mask_id;
	u32 stats_cxt;

	if (nfp_get_stats_entry(app, &stats_cxt))
		return -ENOENT;

	nfp_flow->meta.host_ctx_id = cpu_to_be32(stats_cxt);
	nfp_flow->meta.host_cookie = cpu_to_be64(flow->cookie);

	new_mask_id = 0;
	if (!nfp_check_mask_add(app, nfp_flow->mask_data,
				nfp_flow->meta.mask_len,
				&nfp_flow->meta.flags, &new_mask_id)) {
		if (nfp_release_stats_entry(app, stats_cxt))
			return -EINVAL;
		return -ENOENT;
	}

	nfp_flow->meta.flow_version = cpu_to_be64(priv->flower_version);
	priv->flower_version++;

	/* Update flow payload with mask ids. */
	nfp_flow->unmasked_data[NFP_FL_MASK_ID_LOCATION] = new_mask_id;
	nfp_flow->stats.pkts = 0;
	nfp_flow->stats.bytes = 0;
	nfp_flow->stats.used = jiffies;

	check_entry = nfp_flower_search_fl_table(app, flow->cookie);
	if (check_entry) {
		if (nfp_release_stats_entry(app, stats_cxt))
			return -EINVAL;

		if (!nfp_check_mask_remove(app, nfp_flow->mask_data,
					   nfp_flow->meta.mask_len,
					   NULL, &new_mask_id))
			return -EINVAL;

		return -EEXIST;
	}

	return 0;
}

int nfp_modify_flow_metadata(struct nfp_app *app,
			     struct nfp_fl_payload *nfp_flow)
{
	struct nfp_flower_priv *priv = app->priv;
	u8 new_mask_id = 0;
	u32 temp_ctx_id;

	nfp_check_mask_remove(app, nfp_flow->mask_data,
			      nfp_flow->meta.mask_len, &nfp_flow->meta.flags,
			      &new_mask_id);

	nfp_flow->meta.flow_version = cpu_to_be64(priv->flower_version);
	priv->flower_version++;

	/* Update flow payload with mask ids. */
	nfp_flow->unmasked_data[NFP_FL_MASK_ID_LOCATION] = new_mask_id;

	/* Release the stats ctx id. */
	temp_ctx_id = be32_to_cpu(nfp_flow->meta.host_ctx_id);

	return nfp_release_stats_entry(app, temp_ctx_id);
}

int nfp_flower_metadata_init(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;

	hash_init(priv->mask_table);
	hash_init(priv->flow_table);
	get_random_bytes(&priv->mask_id_seed, sizeof(priv->mask_id_seed));

	/* Init ring buffer and unallocated mask_ids. */
	priv->mask_ids.mask_id_free_list.buf =
		kmalloc_array(NFP_FLOWER_MASK_ENTRY_RS,
			      NFP_FLOWER_MASK_ELEMENT_RS, GFP_KERNEL);
	if (!priv->mask_ids.mask_id_free_list.buf)
		return -ENOMEM;

	priv->mask_ids.init_unallocated = NFP_FLOWER_MASK_ENTRY_RS - 1;

	/* Init timestamps for mask id*/
	priv->mask_ids.last_used =
		kmalloc_array(NFP_FLOWER_MASK_ENTRY_RS,
			      sizeof(*priv->mask_ids.last_used), GFP_KERNEL);
	if (!priv->mask_ids.last_used)
		goto err_free_mask_id;

	/* Init ring buffer and unallocated stats_ids. */
	priv->stats_ids.free_list.buf =
		vmalloc(NFP_FL_STATS_ENTRY_RS * NFP_FL_STATS_ELEM_RS);
	if (!priv->stats_ids.free_list.buf)
		goto err_free_last_used;

	priv->stats_ids.init_unalloc = NFP_FL_REPEATED_HASH_MAX;

	return 0;

err_free_last_used:
	kfree(priv->mask_ids.last_used);
err_free_mask_id:
	kfree(priv->mask_ids.mask_id_free_list.buf);
	return -ENOMEM;
}

void nfp_flower_metadata_cleanup(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;

	if (!priv)
		return;

	kfree(priv->mask_ids.mask_id_free_list.buf);
	kfree(priv->mask_ids.last_used);
	vfree(priv->stats_ids.free_list.buf);
}
