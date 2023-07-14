// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/netdevice.h>
#include <linux/dynamic_debug.h>
#include <linux/etherdevice.h>
#include <linux/list.h>

#include "ionic.h"
#include "ionic_lif.h"
#include "ionic_rx_filter.h"

void ionic_rx_filter_free(struct ionic_lif *lif, struct ionic_rx_filter *f)
{
	struct device *dev = lif->ionic->dev;

	hlist_del(&f->by_id);
	hlist_del(&f->by_hash);
	devm_kfree(dev, f);
}

void ionic_rx_filter_replay(struct ionic_lif *lif)
{
	struct ionic_rx_filter_add_cmd *ac;
	struct hlist_head new_id_list;
	struct ionic_admin_ctx ctx;
	struct ionic_rx_filter *f;
	struct hlist_head *head;
	struct hlist_node *tmp;
	unsigned int key;
	unsigned int i;
	int err;

	INIT_HLIST_HEAD(&new_id_list);
	ac = &ctx.cmd.rx_filter_add;

	for (i = 0; i < IONIC_RX_FILTER_HLISTS; i++) {
		head = &lif->rx_filters.by_id[i];
		hlist_for_each_entry_safe(f, tmp, head, by_id) {
			ctx.work = COMPLETION_INITIALIZER_ONSTACK(ctx.work);
			memcpy(ac, &f->cmd, sizeof(f->cmd));
			dev_dbg(&lif->netdev->dev, "replay filter command:\n");
			dynamic_hex_dump("cmd ", DUMP_PREFIX_OFFSET, 16, 1,
					 &ctx.cmd, sizeof(ctx.cmd), true);

			err = ionic_adminq_post_wait(lif, &ctx);
			if (err) {
				switch (le16_to_cpu(ac->match)) {
				case IONIC_RX_FILTER_MATCH_VLAN:
					netdev_info(lif->netdev, "Replay failed - %d: vlan %d\n",
						    err,
						    le16_to_cpu(ac->vlan.vlan));
					break;
				case IONIC_RX_FILTER_MATCH_MAC:
					netdev_info(lif->netdev, "Replay failed - %d: mac %pM\n",
						    err, ac->mac.addr);
					break;
				case IONIC_RX_FILTER_MATCH_MAC_VLAN:
					netdev_info(lif->netdev, "Replay failed - %d: vlan %d mac %pM\n",
						    err,
						    le16_to_cpu(ac->vlan.vlan),
						    ac->mac.addr);
					break;
				}
				spin_lock_bh(&lif->rx_filters.lock);
				ionic_rx_filter_free(lif, f);
				spin_unlock_bh(&lif->rx_filters.lock);

				continue;
			}

			/* remove from old id list, save new id in tmp list */
			spin_lock_bh(&lif->rx_filters.lock);
			hlist_del(&f->by_id);
			spin_unlock_bh(&lif->rx_filters.lock);
			f->filter_id = le32_to_cpu(ctx.comp.rx_filter_add.filter_id);
			hlist_add_head(&f->by_id, &new_id_list);
		}
	}

	/* rebuild the by_id hash lists with the new filter ids */
	spin_lock_bh(&lif->rx_filters.lock);
	hlist_for_each_entry_safe(f, tmp, &new_id_list, by_id) {
		key = f->filter_id & IONIC_RX_FILTER_HLISTS_MASK;
		head = &lif->rx_filters.by_id[key];
		hlist_add_head(&f->by_id, head);
	}
	spin_unlock_bh(&lif->rx_filters.lock);
}

int ionic_rx_filters_init(struct ionic_lif *lif)
{
	unsigned int i;

	spin_lock_init(&lif->rx_filters.lock);

	spin_lock_bh(&lif->rx_filters.lock);
	for (i = 0; i < IONIC_RX_FILTER_HLISTS; i++) {
		INIT_HLIST_HEAD(&lif->rx_filters.by_hash[i]);
		INIT_HLIST_HEAD(&lif->rx_filters.by_id[i]);
	}
	spin_unlock_bh(&lif->rx_filters.lock);

	return 0;
}

void ionic_rx_filters_deinit(struct ionic_lif *lif)
{
	struct ionic_rx_filter *f;
	struct hlist_head *head;
	struct hlist_node *tmp;
	unsigned int i;

	spin_lock_bh(&lif->rx_filters.lock);
	for (i = 0; i < IONIC_RX_FILTER_HLISTS; i++) {
		head = &lif->rx_filters.by_id[i];
		hlist_for_each_entry_safe(f, tmp, head, by_id)
			ionic_rx_filter_free(lif, f);
	}
	spin_unlock_bh(&lif->rx_filters.lock);
}

int ionic_rx_filter_save(struct ionic_lif *lif, u32 flow_id, u16 rxq_index,
			 u32 hash, struct ionic_admin_ctx *ctx,
			 enum ionic_filter_state state)
{
	struct device *dev = lif->ionic->dev;
	struct ionic_rx_filter_add_cmd *ac;
	struct ionic_rx_filter *f = NULL;
	struct hlist_head *head;
	unsigned int key;

	ac = &ctx->cmd.rx_filter_add;

	switch (le16_to_cpu(ac->match)) {
	case IONIC_RX_FILTER_MATCH_VLAN:
		key = le16_to_cpu(ac->vlan.vlan);
		f = ionic_rx_filter_by_vlan(lif, le16_to_cpu(ac->vlan.vlan));
		break;
	case IONIC_RX_FILTER_MATCH_MAC:
		key = *(u32 *)ac->mac.addr;
		f = ionic_rx_filter_by_addr(lif, ac->mac.addr);
		break;
	case IONIC_RX_FILTER_MATCH_MAC_VLAN:
		key = le16_to_cpu(ac->mac_vlan.vlan);
		break;
	case IONIC_RX_FILTER_STEER_PKTCLASS:
		key = 0;
		break;
	default:
		return -EINVAL;
	}

	if (f) {
		/* remove from current linking so we can refresh it */
		hlist_del(&f->by_id);
		hlist_del(&f->by_hash);
	} else {
		f = devm_kzalloc(dev, sizeof(*f), GFP_ATOMIC);
		if (!f)
			return -ENOMEM;
	}

	f->flow_id = flow_id;
	f->filter_id = le32_to_cpu(ctx->comp.rx_filter_add.filter_id);
	f->state = state;
	f->rxq_index = rxq_index;
	memcpy(&f->cmd, ac, sizeof(f->cmd));
	netdev_dbg(lif->netdev, "rx_filter add filter_id %d\n", f->filter_id);

	INIT_HLIST_NODE(&f->by_hash);
	INIT_HLIST_NODE(&f->by_id);

	key = hash_32(key, IONIC_RX_FILTER_HASH_BITS);
	head = &lif->rx_filters.by_hash[key];
	hlist_add_head(&f->by_hash, head);

	key = f->filter_id & IONIC_RX_FILTER_HLISTS_MASK;
	head = &lif->rx_filters.by_id[key];
	hlist_add_head(&f->by_id, head);

	return 0;
}

struct ionic_rx_filter *ionic_rx_filter_by_vlan(struct ionic_lif *lif, u16 vid)
{
	struct ionic_rx_filter *f;
	struct hlist_head *head;
	unsigned int key;

	key = hash_32(vid, IONIC_RX_FILTER_HASH_BITS);
	head = &lif->rx_filters.by_hash[key];

	hlist_for_each_entry(f, head, by_hash) {
		if (le16_to_cpu(f->cmd.match) != IONIC_RX_FILTER_MATCH_VLAN)
			continue;
		if (le16_to_cpu(f->cmd.vlan.vlan) == vid)
			return f;
	}

	return NULL;
}

struct ionic_rx_filter *ionic_rx_filter_by_addr(struct ionic_lif *lif,
						const u8 *addr)
{
	struct ionic_rx_filter *f;
	struct hlist_head *head;
	unsigned int key;

	key = hash_32(*(u32 *)addr, IONIC_RX_FILTER_HASH_BITS);
	head = &lif->rx_filters.by_hash[key];

	hlist_for_each_entry(f, head, by_hash) {
		if (le16_to_cpu(f->cmd.match) != IONIC_RX_FILTER_MATCH_MAC)
			continue;
		if (memcmp(addr, f->cmd.mac.addr, ETH_ALEN) == 0)
			return f;
	}

	return NULL;
}

struct ionic_rx_filter *ionic_rx_filter_rxsteer(struct ionic_lif *lif)
{
	struct ionic_rx_filter *f;
	struct hlist_head *head;
	unsigned int key;

	key = hash_32(0, IONIC_RX_FILTER_HASH_BITS);
	head = &lif->rx_filters.by_hash[key];

	hlist_for_each_entry(f, head, by_hash) {
		if (le16_to_cpu(f->cmd.match) != IONIC_RX_FILTER_STEER_PKTCLASS)
			continue;
		return f;
	}

	return NULL;
}

static struct ionic_rx_filter *ionic_rx_filter_find(struct ionic_lif *lif,
						    struct ionic_rx_filter_add_cmd *ac)
{
	switch (le16_to_cpu(ac->match)) {
	case IONIC_RX_FILTER_MATCH_VLAN:
		return ionic_rx_filter_by_vlan(lif, le16_to_cpu(ac->vlan.vlan));
	case IONIC_RX_FILTER_MATCH_MAC:
		return ionic_rx_filter_by_addr(lif, ac->mac.addr);
	default:
		netdev_err(lif->netdev, "unsupported filter match %d",
			   le16_to_cpu(ac->match));
		return NULL;
	}
}

int ionic_lif_list_addr(struct ionic_lif *lif, const u8 *addr, bool mode)
{
	struct ionic_rx_filter *f;
	int err;

	spin_lock_bh(&lif->rx_filters.lock);

	f = ionic_rx_filter_by_addr(lif, addr);
	if (mode == ADD_ADDR && !f) {
		struct ionic_admin_ctx ctx = {
			.work = COMPLETION_INITIALIZER_ONSTACK(ctx.work),
			.cmd.rx_filter_add = {
				.opcode = IONIC_CMD_RX_FILTER_ADD,
				.lif_index = cpu_to_le16(lif->index),
				.match = cpu_to_le16(IONIC_RX_FILTER_MATCH_MAC),
			},
		};

		memcpy(ctx.cmd.rx_filter_add.mac.addr, addr, ETH_ALEN);
		err = ionic_rx_filter_save(lif, 0, IONIC_RXQ_INDEX_ANY, 0, &ctx,
					   IONIC_FILTER_STATE_NEW);
		if (err) {
			spin_unlock_bh(&lif->rx_filters.lock);
			return err;
		}

	} else if (mode == ADD_ADDR && f) {
		if (f->state == IONIC_FILTER_STATE_OLD)
			f->state = IONIC_FILTER_STATE_SYNCED;

	} else if (mode == DEL_ADDR && f) {
		if (f->state == IONIC_FILTER_STATE_NEW)
			ionic_rx_filter_free(lif, f);
		else if (f->state == IONIC_FILTER_STATE_SYNCED)
			f->state = IONIC_FILTER_STATE_OLD;
	} else if (mode == DEL_ADDR && !f) {
		spin_unlock_bh(&lif->rx_filters.lock);
		return -ENOENT;
	}

	spin_unlock_bh(&lif->rx_filters.lock);

	set_bit(IONIC_LIF_F_FILTER_SYNC_NEEDED, lif->state);

	return 0;
}

static int ionic_lif_filter_add(struct ionic_lif *lif,
				struct ionic_rx_filter_add_cmd *ac)
{
	struct ionic_admin_ctx ctx = {
		.work = COMPLETION_INITIALIZER_ONSTACK(ctx.work),
	};
	struct ionic_rx_filter *f;
	int nfilters;
	int err = 0;

	ctx.cmd.rx_filter_add = *ac;
	ctx.cmd.rx_filter_add.opcode = IONIC_CMD_RX_FILTER_ADD,
	ctx.cmd.rx_filter_add.lif_index = cpu_to_le16(lif->index),

	spin_lock_bh(&lif->rx_filters.lock);
	f = ionic_rx_filter_find(lif, &ctx.cmd.rx_filter_add);
	if (f) {
		/* don't bother if we already have it and it is sync'd */
		if (f->state == IONIC_FILTER_STATE_SYNCED) {
			spin_unlock_bh(&lif->rx_filters.lock);
			return 0;
		}

		/* mark preemptively as sync'd to block any parallel attempts */
		f->state = IONIC_FILTER_STATE_SYNCED;
	} else {
		/* save as SYNCED to catch any DEL requests while processing */
		err = ionic_rx_filter_save(lif, 0, IONIC_RXQ_INDEX_ANY, 0, &ctx,
					   IONIC_FILTER_STATE_SYNCED);
	}
	spin_unlock_bh(&lif->rx_filters.lock);
	if (err)
		return err;

	/* Don't bother with the write to FW if we know there's no room,
	 * we can try again on the next sync attempt.
	 * Since the FW doesn't have a way to tell us the vlan limit,
	 * we start max_vlans at 0 until we hit the ENOSPC error.
	 */
	switch (le16_to_cpu(ctx.cmd.rx_filter_add.match)) {
	case IONIC_RX_FILTER_MATCH_VLAN:
		netdev_dbg(lif->netdev, "%s: rx_filter add VLAN %d\n",
			   __func__, ctx.cmd.rx_filter_add.vlan.vlan);
		if (lif->max_vlans && lif->nvlans >= lif->max_vlans)
			err = -ENOSPC;
		break;
	case IONIC_RX_FILTER_MATCH_MAC:
		netdev_dbg(lif->netdev, "%s: rx_filter add ADDR %pM\n",
			   __func__, ctx.cmd.rx_filter_add.mac.addr);
		nfilters = le32_to_cpu(lif->identity->eth.max_ucast_filters);
		if ((lif->nucast + lif->nmcast) >= nfilters)
			err = -ENOSPC;
		break;
	}

	if (err != -ENOSPC)
		err = ionic_adminq_post_wait_nomsg(lif, &ctx);

	spin_lock_bh(&lif->rx_filters.lock);

	if (err && err != -EEXIST) {
		/* set the state back to NEW so we can try again later */
		f = ionic_rx_filter_find(lif, &ctx.cmd.rx_filter_add);
		if (f && f->state == IONIC_FILTER_STATE_SYNCED) {
			f->state = IONIC_FILTER_STATE_NEW;

			/* If -ENOSPC we won't waste time trying to sync again
			 * until there is a delete that might make room
			 */
			if (err != -ENOSPC)
				set_bit(IONIC_LIF_F_FILTER_SYNC_NEEDED, lif->state);
		}

		spin_unlock_bh(&lif->rx_filters.lock);

		/* store the max_vlans limit that we found */
		if (err == -ENOSPC &&
		    le16_to_cpu(ctx.cmd.rx_filter_add.match) == IONIC_RX_FILTER_MATCH_VLAN)
			lif->max_vlans = lif->nvlans;

		/* Prevent unnecessary error messages on recoverable
		 * errors as the filter will get retried on the next
		 * sync attempt.
		 */
		switch (err) {
		case -ENOSPC:
		case -ENXIO:
		case -ETIMEDOUT:
		case -EAGAIN:
		case -EBUSY:
			return 0;
		default:
			break;
		}

		ionic_adminq_netdev_err_print(lif, ctx.cmd.cmd.opcode,
					      ctx.comp.comp.status, err);
		switch (le16_to_cpu(ctx.cmd.rx_filter_add.match)) {
		case IONIC_RX_FILTER_MATCH_VLAN:
			netdev_info(lif->netdev, "rx_filter add failed: VLAN %d\n",
				    ctx.cmd.rx_filter_add.vlan.vlan);
			break;
		case IONIC_RX_FILTER_MATCH_MAC:
			netdev_info(lif->netdev, "rx_filter add failed: ADDR %pM\n",
				    ctx.cmd.rx_filter_add.mac.addr);
			break;
		}

		return err;
	}

	switch (le16_to_cpu(ctx.cmd.rx_filter_add.match)) {
	case IONIC_RX_FILTER_MATCH_VLAN:
		lif->nvlans++;
		break;
	case IONIC_RX_FILTER_MATCH_MAC:
		if (is_multicast_ether_addr(ctx.cmd.rx_filter_add.mac.addr))
			lif->nmcast++;
		else
			lif->nucast++;
		break;
	}

	f = ionic_rx_filter_find(lif, &ctx.cmd.rx_filter_add);
	if (f && f->state == IONIC_FILTER_STATE_OLD) {
		/* Someone requested a delete while we were adding
		 * so update the filter info with the results from the add
		 * and the data will be there for the delete on the next
		 * sync cycle.
		 */
		err = ionic_rx_filter_save(lif, 0, IONIC_RXQ_INDEX_ANY, 0, &ctx,
					   IONIC_FILTER_STATE_OLD);
	} else {
		err = ionic_rx_filter_save(lif, 0, IONIC_RXQ_INDEX_ANY, 0, &ctx,
					   IONIC_FILTER_STATE_SYNCED);
	}

	spin_unlock_bh(&lif->rx_filters.lock);

	return err;
}

int ionic_lif_addr_add(struct ionic_lif *lif, const u8 *addr)
{
	struct ionic_rx_filter_add_cmd ac = {
		.match = cpu_to_le16(IONIC_RX_FILTER_MATCH_MAC),
	};

	memcpy(&ac.mac.addr, addr, ETH_ALEN);

	return ionic_lif_filter_add(lif, &ac);
}

int ionic_lif_vlan_add(struct ionic_lif *lif, const u16 vid)
{
	struct ionic_rx_filter_add_cmd ac = {
		.match = cpu_to_le16(IONIC_RX_FILTER_MATCH_VLAN),
		.vlan.vlan = cpu_to_le16(vid),
	};

	return ionic_lif_filter_add(lif, &ac);
}

static int ionic_lif_filter_del(struct ionic_lif *lif,
				struct ionic_rx_filter_add_cmd *ac)
{
	struct ionic_admin_ctx ctx = {
		.work = COMPLETION_INITIALIZER_ONSTACK(ctx.work),
		.cmd.rx_filter_del = {
			.opcode = IONIC_CMD_RX_FILTER_DEL,
			.lif_index = cpu_to_le16(lif->index),
		},
	};
	struct ionic_rx_filter *f;
	int state;
	int err;

	spin_lock_bh(&lif->rx_filters.lock);
	f = ionic_rx_filter_find(lif, ac);
	if (!f) {
		spin_unlock_bh(&lif->rx_filters.lock);
		return -ENOENT;
	}

	switch (le16_to_cpu(ac->match)) {
	case IONIC_RX_FILTER_MATCH_VLAN:
		netdev_dbg(lif->netdev, "%s: rx_filter del VLAN %d id %d\n",
			   __func__, ac->vlan.vlan, f->filter_id);
		lif->nvlans--;
		break;
	case IONIC_RX_FILTER_MATCH_MAC:
		netdev_dbg(lif->netdev, "%s: rx_filter del ADDR %pM id %d\n",
			   __func__, ac->mac.addr, f->filter_id);
		if (is_multicast_ether_addr(ac->mac.addr) && lif->nmcast)
			lif->nmcast--;
		else if (!is_multicast_ether_addr(ac->mac.addr) && lif->nucast)
			lif->nucast--;
		break;
	}

	state = f->state;
	ctx.cmd.rx_filter_del.filter_id = cpu_to_le32(f->filter_id);
	ionic_rx_filter_free(lif, f);

	spin_unlock_bh(&lif->rx_filters.lock);

	if (state != IONIC_FILTER_STATE_NEW) {
		err = ionic_adminq_post_wait_nomsg(lif, &ctx);

		switch (err) {
			/* ignore these errors */
		case -EEXIST:
		case -ENXIO:
		case -ETIMEDOUT:
		case -EAGAIN:
		case -EBUSY:
		case 0:
			break;
		default:
			ionic_adminq_netdev_err_print(lif, ctx.cmd.cmd.opcode,
						      ctx.comp.comp.status, err);
			return err;
		}
	}

	return 0;
}

int ionic_lif_addr_del(struct ionic_lif *lif, const u8 *addr)
{
	struct ionic_rx_filter_add_cmd ac = {
		.match = cpu_to_le16(IONIC_RX_FILTER_MATCH_MAC),
	};

	memcpy(&ac.mac.addr, addr, ETH_ALEN);

	return ionic_lif_filter_del(lif, &ac);
}

int ionic_lif_vlan_del(struct ionic_lif *lif, const u16 vid)
{
	struct ionic_rx_filter_add_cmd ac = {
		.match = cpu_to_le16(IONIC_RX_FILTER_MATCH_VLAN),
		.vlan.vlan = cpu_to_le16(vid),
	};

	return ionic_lif_filter_del(lif, &ac);
}

struct sync_item {
	struct list_head list;
	struct ionic_rx_filter f;
};

void ionic_rx_filter_sync(struct ionic_lif *lif)
{
	struct device *dev = lif->ionic->dev;
	struct list_head sync_add_list;
	struct list_head sync_del_list;
	struct sync_item *sync_item;
	struct ionic_rx_filter *f;
	struct hlist_head *head;
	struct hlist_node *tmp;
	struct sync_item *spos;
	unsigned int i;

	INIT_LIST_HEAD(&sync_add_list);
	INIT_LIST_HEAD(&sync_del_list);

	clear_bit(IONIC_LIF_F_FILTER_SYNC_NEEDED, lif->state);

	/* Copy the filters to be added and deleted
	 * into a separate local list that needs no locking.
	 */
	spin_lock_bh(&lif->rx_filters.lock);
	for (i = 0; i < IONIC_RX_FILTER_HLISTS; i++) {
		head = &lif->rx_filters.by_id[i];
		hlist_for_each_entry_safe(f, tmp, head, by_id) {
			if (f->state == IONIC_FILTER_STATE_NEW ||
			    f->state == IONIC_FILTER_STATE_OLD) {
				sync_item = devm_kzalloc(dev, sizeof(*sync_item),
							 GFP_ATOMIC);
				if (!sync_item)
					goto loop_out;

				sync_item->f = *f;

				if (f->state == IONIC_FILTER_STATE_NEW)
					list_add(&sync_item->list, &sync_add_list);
				else
					list_add(&sync_item->list, &sync_del_list);
			}
		}
	}
loop_out:
	spin_unlock_bh(&lif->rx_filters.lock);

	/* If the add or delete fails, it won't get marked as sync'd
	 * and will be tried again in the next sync action.
	 * Do the deletes first in case we're in an overflow state and
	 * they can clear room for some new filters
	 */
	list_for_each_entry_safe(sync_item, spos, &sync_del_list, list) {
		ionic_lif_filter_del(lif, &sync_item->f.cmd);

		list_del(&sync_item->list);
		devm_kfree(dev, sync_item);
	}

	list_for_each_entry_safe(sync_item, spos, &sync_add_list, list) {
		ionic_lif_filter_add(lif, &sync_item->f.cmd);

		list_del(&sync_item->list);
		devm_kfree(dev, sync_item);
	}
}
