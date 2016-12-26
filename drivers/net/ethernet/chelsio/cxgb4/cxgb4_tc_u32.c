/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>

#include "cxgb4.h"
#include "cxgb4_tc_u32_parse.h"
#include "cxgb4_tc_u32.h"

/* Fill ch_filter_specification with parsed match value/mask pair. */
static int fill_match_fields(struct adapter *adap,
			     struct ch_filter_specification *fs,
			     struct tc_cls_u32_offload *cls,
			     const struct cxgb4_match_field *entry,
			     bool next_header)
{
	unsigned int i, j;
	u32 val, mask;
	int off, err;
	bool found;

	for (i = 0; i < cls->knode.sel->nkeys; i++) {
		off = cls->knode.sel->keys[i].off;
		val = cls->knode.sel->keys[i].val;
		mask = cls->knode.sel->keys[i].mask;

		if (next_header) {
			/* For next headers, parse only keys with offmask */
			if (!cls->knode.sel->keys[i].offmask)
				continue;
		} else {
			/* For the remaining, parse only keys without offmask */
			if (cls->knode.sel->keys[i].offmask)
				continue;
		}

		found = false;

		for (j = 0; entry[j].val; j++) {
			if (off == entry[j].off) {
				found = true;
				err = entry[j].val(fs, val, mask);
				if (err)
					return err;
				break;
			}
		}

		if (!found)
			return -EINVAL;
	}

	return 0;
}

/* Fill ch_filter_specification with parsed action. */
static int fill_action_fields(struct adapter *adap,
			      struct ch_filter_specification *fs,
			      struct tc_cls_u32_offload *cls)
{
	unsigned int num_actions = 0;
	const struct tc_action *a;
	struct tcf_exts *exts;
	LIST_HEAD(actions);

	exts = cls->knode.exts;
	if (tc_no_actions(exts))
		return -EINVAL;

	tcf_exts_to_list(exts, &actions);
	list_for_each_entry(a, &actions, list) {
		/* Don't allow more than one action per rule. */
		if (num_actions)
			return -EINVAL;

		/* Drop in hardware. */
		if (is_tcf_gact_shot(a)) {
			fs->action = FILTER_DROP;
			num_actions++;
			continue;
		}

		/* Re-direct to specified port in hardware. */
		if (is_tcf_mirred_egress_redirect(a)) {
			struct net_device *n_dev;
			unsigned int i, index;
			bool found = false;

			index = tcf_mirred_ifindex(a);
			for_each_port(adap, i) {
				n_dev = adap->port[i];
				if (index == n_dev->ifindex) {
					fs->action = FILTER_SWITCH;
					fs->eport = i;
					found = true;
					break;
				}
			}

			/* Interface doesn't belong to any port of
			 * the underlying hardware.
			 */
			if (!found)
				return -EINVAL;

			num_actions++;
			continue;
		}

		/* Un-supported action. */
		return -EINVAL;
	}

	return 0;
}

int cxgb4_config_knode(struct net_device *dev, __be16 protocol,
		       struct tc_cls_u32_offload *cls)
{
	const struct cxgb4_match_field *start, *link_start = NULL;
	struct adapter *adapter = netdev2adap(dev);
	struct ch_filter_specification fs;
	struct cxgb4_tc_u32_table *t;
	struct cxgb4_link *link;
	unsigned int filter_id;
	u32 uhtid, link_uhtid;
	bool is_ipv6 = false;
	int ret;

	if (!can_tc_u32_offload(dev))
		return -EOPNOTSUPP;

	if (protocol != htons(ETH_P_IP) && protocol != htons(ETH_P_IPV6))
		return -EOPNOTSUPP;

	/* Fetch the location to insert the filter. */
	filter_id = cls->knode.handle & 0xFFFFF;

	if (filter_id > adapter->tids.nftids) {
		dev_err(adapter->pdev_dev,
			"Location %d out of range for insertion. Max: %d\n",
			filter_id, adapter->tids.nftids);
		return -ERANGE;
	}

	t = adapter->tc_u32;
	uhtid = TC_U32_USERHTID(cls->knode.handle);
	link_uhtid = TC_U32_USERHTID(cls->knode.link_handle);

	/* Ensure that uhtid is either root u32 (i.e. 0x800)
	 * or a a valid linked bucket.
	 */
	if (uhtid != 0x800 && uhtid >= t->size)
		return -EINVAL;

	/* Ensure link handle uhtid is sane, if specified. */
	if (link_uhtid >= t->size)
		return -EINVAL;

	memset(&fs, 0, sizeof(fs));

	if (protocol == htons(ETH_P_IPV6)) {
		start = cxgb4_ipv6_fields;
		is_ipv6 = true;
	} else {
		start = cxgb4_ipv4_fields;
		is_ipv6 = false;
	}

	if (uhtid != 0x800) {
		/* Link must exist from root node before insertion. */
		if (!t->table[uhtid - 1].link_handle)
			return -EINVAL;

		/* Link must have a valid supported next header. */
		link_start = t->table[uhtid - 1].match_field;
		if (!link_start)
			return -EINVAL;
	}

	/* Parse links and record them for subsequent jumps to valid
	 * next headers.
	 */
	if (link_uhtid) {
		const struct cxgb4_next_header *next;
		bool found = false;
		unsigned int i, j;
		u32 val, mask;
		int off;

		if (t->table[link_uhtid - 1].link_handle) {
			dev_err(adapter->pdev_dev,
				"Link handle exists for: 0x%x\n",
				link_uhtid);
			return -EINVAL;
		}

		next = is_ipv6 ? cxgb4_ipv6_jumps : cxgb4_ipv4_jumps;

		/* Try to find matches that allow jumps to next header. */
		for (i = 0; next[i].jump; i++) {
			if (next[i].offoff != cls->knode.sel->offoff ||
			    next[i].shift != cls->knode.sel->offshift ||
			    next[i].mask != cls->knode.sel->offmask ||
			    next[i].offset != cls->knode.sel->off)
				continue;

			/* Found a possible candidate.  Find a key that
			 * matches the corresponding offset, value, and
			 * mask to jump to next header.
			 */
			for (j = 0; j < cls->knode.sel->nkeys; j++) {
				off = cls->knode.sel->keys[j].off;
				val = cls->knode.sel->keys[j].val;
				mask = cls->knode.sel->keys[j].mask;

				if (next[i].match_off == off &&
				    next[i].match_val == val &&
				    next[i].match_mask == mask) {
					found = true;
					break;
				}
			}

			if (!found)
				continue; /* Try next candidate. */

			/* Candidate to jump to next header found.
			 * Translate all keys to internal specification
			 * and store them in jump table. This spec is copied
			 * later to set the actual filters.
			 */
			ret = fill_match_fields(adapter, &fs, cls,
						start, false);
			if (ret)
				goto out;

			link = &t->table[link_uhtid - 1];
			link->match_field = next[i].jump;
			link->link_handle = cls->knode.handle;
			memcpy(&link->fs, &fs, sizeof(fs));
			break;
		}

		/* No candidate found to jump to next header. */
		if (!found)
			return -EINVAL;

		return 0;
	}

	/* Fill ch_filter_specification match fields to be shipped to hardware.
	 * Copy the linked spec (if any) first.  And then update the spec as
	 * needed.
	 */
	if (uhtid != 0x800 && t->table[uhtid - 1].link_handle) {
		/* Copy linked ch_filter_specification */
		memcpy(&fs, &t->table[uhtid - 1].fs, sizeof(fs));
		ret = fill_match_fields(adapter, &fs, cls,
					link_start, true);
		if (ret)
			goto out;
	}

	ret = fill_match_fields(adapter, &fs, cls, start, false);
	if (ret)
		goto out;

	/* Fill ch_filter_specification action fields to be shipped to
	 * hardware.
	 */
	ret = fill_action_fields(adapter, &fs, cls);
	if (ret)
		goto out;

	/* The filter spec has been completely built from the info
	 * provided from u32.  We now set some default fields in the
	 * spec for sanity.
	 */

	/* Match only packets coming from the ingress port where this
	 * filter will be created.
	 */
	fs.val.iport = netdev2pinfo(dev)->port_id;
	fs.mask.iport = ~0;

	/* Enable filter hit counts. */
	fs.hitcnts = 1;

	/* Set type of filter - IPv6 or IPv4 */
	fs.type = is_ipv6 ? 1 : 0;

	/* Set the filter */
	ret = cxgb4_set_filter(dev, filter_id, &fs);
	if (ret)
		goto out;

	/* If this is a linked bucket, then set the corresponding
	 * entry in the bitmap to mark it as belonging to this linked
	 * bucket.
	 */
	if (uhtid != 0x800 && t->table[uhtid - 1].link_handle)
		set_bit(filter_id, t->table[uhtid - 1].tid_map);

out:
	return ret;
}

int cxgb4_delete_knode(struct net_device *dev, __be16 protocol,
		       struct tc_cls_u32_offload *cls)
{
	struct adapter *adapter = netdev2adap(dev);
	unsigned int filter_id, max_tids, i, j;
	struct cxgb4_link *link = NULL;
	struct cxgb4_tc_u32_table *t;
	u32 handle, uhtid;
	int ret;

	if (!can_tc_u32_offload(dev))
		return -EOPNOTSUPP;

	/* Fetch the location to delete the filter. */
	filter_id = cls->knode.handle & 0xFFFFF;

	if (filter_id > adapter->tids.nftids) {
		dev_err(adapter->pdev_dev,
			"Location %d out of range for deletion. Max: %d\n",
			filter_id, adapter->tids.nftids);
		return -ERANGE;
	}

	t = adapter->tc_u32;
	handle = cls->knode.handle;
	uhtid = TC_U32_USERHTID(cls->knode.handle);

	/* Ensure that uhtid is either root u32 (i.e. 0x800)
	 * or a a valid linked bucket.
	 */
	if (uhtid != 0x800 && uhtid >= t->size)
		return -EINVAL;

	/* Delete the specified filter */
	if (uhtid != 0x800) {
		link = &t->table[uhtid - 1];
		if (!link->link_handle)
			return -EINVAL;

		if (!test_bit(filter_id, link->tid_map))
			return -EINVAL;
	}

	ret = cxgb4_del_filter(dev, filter_id);
	if (ret)
		goto out;

	if (link)
		clear_bit(filter_id, link->tid_map);

	/* If a link is being deleted, then delete all filters
	 * associated with the link.
	 */
	max_tids = adapter->tids.nftids;
	for (i = 0; i < t->size; i++) {
		link = &t->table[i];

		if (link->link_handle == handle) {
			for (j = 0; j < max_tids; j++) {
				if (!test_bit(j, link->tid_map))
					continue;

				ret = __cxgb4_del_filter(dev, j, NULL);
				if (ret)
					goto out;

				clear_bit(j, link->tid_map);
			}

			/* Clear the link state */
			link->match_field = NULL;
			link->link_handle = 0;
			memset(&link->fs, 0, sizeof(link->fs));
			break;
		}
	}

out:
	return ret;
}

void cxgb4_cleanup_tc_u32(struct adapter *adap)
{
	struct cxgb4_tc_u32_table *t;
	unsigned int i;

	if (!adap->tc_u32)
		return;

	/* Free up all allocated memory. */
	t = adap->tc_u32;
	for (i = 0; i < t->size; i++) {
		struct cxgb4_link *link = &t->table[i];

		t4_free_mem(link->tid_map);
	}
	t4_free_mem(adap->tc_u32);
}

struct cxgb4_tc_u32_table *cxgb4_init_tc_u32(struct adapter *adap,
					     unsigned int size)
{
	struct cxgb4_tc_u32_table *t;
	unsigned int i;

	if (!size)
		return NULL;

	t = t4_alloc_mem(sizeof(*t) +
			 (size * sizeof(struct cxgb4_link)));
	if (!t)
		return NULL;

	t->size = size;

	for (i = 0; i < t->size; i++) {
		struct cxgb4_link *link = &t->table[i];
		unsigned int bmap_size;
		unsigned int max_tids;

		max_tids = adap->tids.nftids;
		bmap_size = BITS_TO_LONGS(max_tids);
		link->tid_map = t4_alloc_mem(sizeof(unsigned long) * bmap_size);
		if (!link->tid_map)
			goto out_no_mem;
		bitmap_zero(link->tid_map, max_tids);
	}

	return t;

out_no_mem:
	for (i = 0; i < t->size; i++) {
		struct cxgb4_link *link = &t->table[i];

		if (link->tid_map)
			t4_free_mem(link->tid_map);
	}

	if (t)
		t4_free_mem(t);

	return NULL;
}
