// SPDX-License-Identifier: GPL-2.0
/*
 *  Functions for dibs loopback/loopback-ism device.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dibs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "dibs_loopback.h"

#define DIBS_LO_SUPPORT_NOCOPY	0x1
#define DIBS_DMA_ADDR_INVALID	(~(dma_addr_t)0)

static const char dibs_lo_dev_name[] = "lo";
/* global loopback device */
static struct dibs_lo_dev *lo_dev;

static u16 dibs_lo_get_fabric_id(struct dibs_dev *dibs)
{
	return DIBS_LOOPBACK_FABRIC;
}

static int dibs_lo_query_rgid(struct dibs_dev *dibs, const uuid_t *rgid,
			      u32 vid_valid, u32 vid)
{
	/* rgid should be the same as lgid */
	if (!uuid_equal(rgid, &dibs->gid))
		return -ENETUNREACH;
	return 0;
}

static int dibs_lo_max_dmbs(void)
{
	return DIBS_LO_MAX_DMBS;
}

static int dibs_lo_register_dmb(struct dibs_dev *dibs, struct dibs_dmb *dmb,
				struct dibs_client *client)
{
	struct dibs_lo_dmb_node *dmb_node, *tmp_node;
	struct dibs_lo_dev *ldev;
	struct folio *folio;
	unsigned long flags;
	int sba_idx, rc;

	ldev = dibs->drv_priv;
	sba_idx = dmb->idx;
	/* check space for new dmb */
	for_each_clear_bit(sba_idx, ldev->sba_idx_mask, DIBS_LO_MAX_DMBS) {
		if (!test_and_set_bit(sba_idx, ldev->sba_idx_mask))
			break;
	}
	if (sba_idx == DIBS_LO_MAX_DMBS)
		return -ENOSPC;

	dmb_node = kzalloc(sizeof(*dmb_node), GFP_KERNEL);
	if (!dmb_node) {
		rc = -ENOMEM;
		goto err_bit;
	}

	dmb_node->sba_idx = sba_idx;
	dmb_node->len = dmb->dmb_len;

	/* not critical; fail under memory pressure and fallback to TCP */
	folio = folio_alloc(GFP_KERNEL | __GFP_NOWARN | __GFP_NOMEMALLOC |
			    __GFP_NORETRY | __GFP_ZERO,
			    get_order(dmb_node->len));
	if (!folio) {
		rc = -ENOMEM;
		goto err_node;
	}
	dmb_node->cpu_addr = folio_address(folio);
	dmb_node->dma_addr = DIBS_DMA_ADDR_INVALID;
	refcount_set(&dmb_node->refcnt, 1);

again:
	/* add new dmb into hash table */
	get_random_bytes(&dmb_node->token, sizeof(dmb_node->token));
	write_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb_node->token) {
		if (tmp_node->token == dmb_node->token) {
			write_unlock_bh(&ldev->dmb_ht_lock);
			goto again;
		}
	}
	hash_add(ldev->dmb_ht, &dmb_node->list, dmb_node->token);
	write_unlock_bh(&ldev->dmb_ht_lock);
	atomic_inc(&ldev->dmb_cnt);

	dmb->idx = dmb_node->sba_idx;
	dmb->dmb_tok = dmb_node->token;
	dmb->cpu_addr = dmb_node->cpu_addr;
	dmb->dma_addr = dmb_node->dma_addr;
	dmb->dmb_len = dmb_node->len;

	spin_lock_irqsave(&dibs->lock, flags);
	dibs->dmb_clientid_arr[sba_idx] = client->id;
	spin_unlock_irqrestore(&dibs->lock, flags);

	return 0;

err_node:
	kfree(dmb_node);
err_bit:
	clear_bit(sba_idx, ldev->sba_idx_mask);
	return rc;
}

static void __dibs_lo_unregister_dmb(struct dibs_lo_dev *ldev,
				     struct dibs_lo_dmb_node *dmb_node)
{
	/* remove dmb from hash table */
	write_lock_bh(&ldev->dmb_ht_lock);
	hash_del(&dmb_node->list);
	write_unlock_bh(&ldev->dmb_ht_lock);

	clear_bit(dmb_node->sba_idx, ldev->sba_idx_mask);
	folio_put(virt_to_folio(dmb_node->cpu_addr));
	kfree(dmb_node);

	if (atomic_dec_and_test(&ldev->dmb_cnt))
		wake_up(&ldev->ldev_release);
}

static int dibs_lo_unregister_dmb(struct dibs_dev *dibs, struct dibs_dmb *dmb)
{
	struct dibs_lo_dmb_node *dmb_node = NULL, *tmp_node;
	struct dibs_lo_dev *ldev;
	unsigned long flags;

	ldev = dibs->drv_priv;

	/* find dmb from hash table */
	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb->dmb_tok) {
		if (tmp_node->token == dmb->dmb_tok) {
			dmb_node = tmp_node;
			break;
		}
	}
	read_unlock_bh(&ldev->dmb_ht_lock);
	if (!dmb_node)
		return -EINVAL;

	if (refcount_dec_and_test(&dmb_node->refcnt)) {
		spin_lock_irqsave(&dibs->lock, flags);
		dibs->dmb_clientid_arr[dmb_node->sba_idx] = NO_DIBS_CLIENT;
		spin_unlock_irqrestore(&dibs->lock, flags);

		__dibs_lo_unregister_dmb(ldev, dmb_node);
	}
	return 0;
}

static int dibs_lo_support_dmb_nocopy(struct dibs_dev *dibs)
{
	return DIBS_LO_SUPPORT_NOCOPY;
}

static int dibs_lo_attach_dmb(struct dibs_dev *dibs, struct dibs_dmb *dmb)
{
	struct dibs_lo_dmb_node *dmb_node = NULL, *tmp_node;
	struct dibs_lo_dev *ldev;

	ldev = dibs->drv_priv;

	/* find dmb_node according to dmb->dmb_tok */
	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb->dmb_tok) {
		if (tmp_node->token == dmb->dmb_tok) {
			dmb_node = tmp_node;
			break;
		}
	}
	if (!dmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (!refcount_inc_not_zero(&dmb_node->refcnt))
		/* the dmb is being unregistered, but has
		 * not been removed from the hash table.
		 */
		return -EINVAL;

	/* provide dmb information */
	dmb->idx = dmb_node->sba_idx;
	dmb->dmb_tok = dmb_node->token;
	dmb->cpu_addr = dmb_node->cpu_addr;
	dmb->dma_addr = dmb_node->dma_addr;
	dmb->dmb_len = dmb_node->len;
	return 0;
}

static int dibs_lo_detach_dmb(struct dibs_dev *dibs, u64 token)
{
	struct dibs_lo_dmb_node *dmb_node = NULL, *tmp_node;
	struct dibs_lo_dev *ldev;

	ldev = dibs->drv_priv;

	/* find dmb_node according to dmb->dmb_tok */
	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, token) {
		if (tmp_node->token == token) {
			dmb_node = tmp_node;
			break;
		}
	}
	if (!dmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (refcount_dec_and_test(&dmb_node->refcnt))
		__dibs_lo_unregister_dmb(ldev, dmb_node);
	return 0;
}

static int dibs_lo_move_data(struct dibs_dev *dibs, u64 dmb_tok,
			     unsigned int idx, bool sf, unsigned int offset,
			     void *data, unsigned int size)
{
	struct dibs_lo_dmb_node *rmb_node = NULL, *tmp_node;
	struct dibs_lo_dev *ldev;
	u16 s_mask;
	u8 client_id;
	u32 sba_idx;

	ldev = dibs->drv_priv;

	read_lock_bh(&ldev->dmb_ht_lock);
	hash_for_each_possible(ldev->dmb_ht, tmp_node, list, dmb_tok) {
		if (tmp_node->token == dmb_tok) {
			rmb_node = tmp_node;
			break;
		}
	}
	if (!rmb_node) {
		read_unlock_bh(&ldev->dmb_ht_lock);
		return -EINVAL;
	}
	memcpy((char *)rmb_node->cpu_addr + offset, data, size);
	sba_idx = rmb_node->sba_idx;
	read_unlock_bh(&ldev->dmb_ht_lock);

	if (!sf)
		return 0;

	spin_lock(&dibs->lock);
	client_id = dibs->dmb_clientid_arr[sba_idx];
	s_mask = ror16(0x1000, idx);
	if (likely(client_id != NO_DIBS_CLIENT && dibs->subs[client_id]))
		dibs->subs[client_id]->ops->handle_irq(dibs, sba_idx, s_mask);
	spin_unlock(&dibs->lock);

	return 0;
}

static const struct dibs_dev_ops dibs_lo_ops = {
	.get_fabric_id = dibs_lo_get_fabric_id,
	.query_remote_gid = dibs_lo_query_rgid,
	.max_dmbs = dibs_lo_max_dmbs,
	.register_dmb = dibs_lo_register_dmb,
	.unregister_dmb = dibs_lo_unregister_dmb,
	.move_data = dibs_lo_move_data,
	.support_mmapped_rdmb = dibs_lo_support_dmb_nocopy,
	.attach_dmb = dibs_lo_attach_dmb,
	.detach_dmb = dibs_lo_detach_dmb,
};

static void dibs_lo_dev_init(struct dibs_lo_dev *ldev)
{
	rwlock_init(&ldev->dmb_ht_lock);
	hash_init(ldev->dmb_ht);
	atomic_set(&ldev->dmb_cnt, 0);
	init_waitqueue_head(&ldev->ldev_release);
}

static void dibs_lo_dev_exit(struct dibs_lo_dev *ldev)
{
	if (atomic_read(&ldev->dmb_cnt))
		wait_event(ldev->ldev_release, !atomic_read(&ldev->dmb_cnt));
}

static int dibs_lo_dev_probe(void)
{
	struct dibs_lo_dev *ldev;
	struct dibs_dev *dibs;
	int ret;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	dibs = dibs_dev_alloc();
	if (!dibs) {
		kfree(ldev);
		return -ENOMEM;
	}

	ldev->dibs = dibs;
	dibs->drv_priv = ldev;
	dibs_lo_dev_init(ldev);
	uuid_gen(&dibs->gid);
	dibs->ops = &dibs_lo_ops;

	dibs->dev.parent = NULL;
	dev_set_name(&dibs->dev, "%s", dibs_lo_dev_name);

	ret = dibs_dev_add(dibs);
	if (ret)
		goto err_reg;
	lo_dev = ldev;
	return 0;

err_reg:
	kfree(dibs->dmb_clientid_arr);
	/* pairs with dibs_dev_alloc() */
	put_device(&dibs->dev);
	kfree(ldev);

	return ret;
}

static void dibs_lo_dev_remove(void)
{
	if (!lo_dev)
		return;

	dibs_dev_del(lo_dev->dibs);
	dibs_lo_dev_exit(lo_dev);
	/* pairs with dibs_dev_alloc() */
	put_device(&lo_dev->dibs->dev);
	kfree(lo_dev);
	lo_dev = NULL;
}

int dibs_loopback_init(void)
{
	return dibs_lo_dev_probe();
}

void dibs_loopback_exit(void)
{
	dibs_lo_dev_remove();
}
