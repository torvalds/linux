/**
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (C) 2011-2016 Chelsio Communications.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written and Maintained by:
 * Manoj Malviya (manojmalviya@chelsio.com)
 * Atul Gupta (atul.gupta@chelsio.com)
 * Jitendra Lulla (jlulla@chelsio.com)
 * Yeshaswi M R Gowda (yeshaswi@chelsio.com)
 * Harsh Jain (harsh@chelsio.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>

#include <crypto/aes.h>
#include <crypto/hash.h>

#include "t4_msg.h"
#include "chcr_core.h"
#include "cxgb4_uld.h"

static struct chcr_driver_data drv_data;

typedef int (*chcr_handler_func)(struct chcr_dev *dev, unsigned char *input);
static int cpl_fw6_pld_handler(struct chcr_dev *dev, unsigned char *input);
static void *chcr_uld_add(const struct cxgb4_lld_info *lld);
static int chcr_uld_state_change(void *handle, enum cxgb4_state state);

static chcr_handler_func work_handlers[NUM_CPL_CMDS] = {
	[CPL_FW6_PLD] = cpl_fw6_pld_handler,
};

static struct cxgb4_uld_info chcr_uld_info = {
	.name = DRV_MODULE_NAME,
	.nrxq = MAX_ULD_QSETS,
	/* Max ntxq will be derived from fw config file*/
	.rxq_size = 1024,
	.add = chcr_uld_add,
	.state_change = chcr_uld_state_change,
	.rx_handler = chcr_uld_rx_handler,
#ifdef CONFIG_CHELSIO_IPSEC_INLINE
	.tx_handler = chcr_uld_tx_handler,
#endif /* CONFIG_CHELSIO_IPSEC_INLINE */
};

static void detach_work_fn(struct work_struct *work)
{
	struct chcr_dev *dev;

	dev = container_of(work, struct chcr_dev, detach_work.work);

	if (atomic_read(&dev->inflight)) {
		dev->wqretry--;
		if (dev->wqretry) {
			pr_debug("Request Inflight Count %d\n",
				atomic_read(&dev->inflight));

			schedule_delayed_work(&dev->detach_work, WQ_DETACH_TM);
		} else {
			WARN(1, "CHCR:%d request Still Pending\n",
				atomic_read(&dev->inflight));
			complete(&dev->detach_comp);
		}
	} else {
		complete(&dev->detach_comp);
	}
}

struct uld_ctx *assign_chcr_device(void)
{
	struct uld_ctx *u_ctx = NULL;

	/*
	 * When multiple devices are present in system select
	 * device in round-robin fashion for crypto operations
	 * Although One session must use the same device to
	 * maintain request-response ordering.
	 */
	mutex_lock(&drv_data.drv_mutex);
	if (!list_empty(&drv_data.act_dev)) {
		u_ctx = drv_data.last_dev;
		if (list_is_last(&drv_data.last_dev->entry, &drv_data.act_dev))
			drv_data.last_dev = list_first_entry(&drv_data.act_dev,
						  struct uld_ctx, entry);
		else
			drv_data.last_dev =
				list_next_entry(drv_data.last_dev, entry);
	}
	mutex_unlock(&drv_data.drv_mutex);
	return u_ctx;
}

static void chcr_dev_add(struct uld_ctx *u_ctx)
{
	struct chcr_dev *dev;

	dev = &u_ctx->dev;
	dev->state = CHCR_ATTACH;
	atomic_set(&dev->inflight, 0);
	mutex_lock(&drv_data.drv_mutex);
	list_move(&u_ctx->entry, &drv_data.act_dev);
	if (!drv_data.last_dev)
		drv_data.last_dev = u_ctx;
	mutex_unlock(&drv_data.drv_mutex);
}

static void chcr_dev_init(struct uld_ctx *u_ctx)
{
	struct chcr_dev *dev;

	dev = &u_ctx->dev;
	spin_lock_init(&dev->lock_chcr_dev);
	INIT_DELAYED_WORK(&dev->detach_work, detach_work_fn);
	init_completion(&dev->detach_comp);
	dev->state = CHCR_INIT;
	dev->wqretry = WQ_RETRY;
	atomic_inc(&drv_data.dev_count);
	atomic_set(&dev->inflight, 0);
	mutex_lock(&drv_data.drv_mutex);
	list_add_tail(&u_ctx->entry, &drv_data.inact_dev);
	if (!drv_data.last_dev)
		drv_data.last_dev = u_ctx;
	mutex_unlock(&drv_data.drv_mutex);
}

static int chcr_dev_move(struct uld_ctx *u_ctx)
{
	 mutex_lock(&drv_data.drv_mutex);
	if (drv_data.last_dev == u_ctx) {
		if (list_is_last(&drv_data.last_dev->entry, &drv_data.act_dev))
			drv_data.last_dev = list_first_entry(&drv_data.act_dev,
						  struct uld_ctx, entry);
		else
			drv_data.last_dev =
				list_next_entry(drv_data.last_dev, entry);
	}
	list_move(&u_ctx->entry, &drv_data.inact_dev);
	if (list_empty(&drv_data.act_dev))
		drv_data.last_dev = NULL;
	atomic_dec(&drv_data.dev_count);
	mutex_unlock(&drv_data.drv_mutex);

	return 0;
}

static int cpl_fw6_pld_handler(struct chcr_dev *dev,
			       unsigned char *input)
{
	struct crypto_async_request *req;
	struct cpl_fw6_pld *fw6_pld;
	u32 ack_err_status = 0;
	int error_status = 0;
	struct adapter *adap = padap(dev);

	fw6_pld = (struct cpl_fw6_pld *)input;
	req = (struct crypto_async_request *)(uintptr_t)be64_to_cpu(
						    fw6_pld->data[1]);

	ack_err_status =
		ntohl(*(__be32 *)((unsigned char *)&fw6_pld->data[0] + 4));
	if (CHK_MAC_ERR_BIT(ack_err_status) || CHK_PAD_ERR_BIT(ack_err_status))
		error_status = -EBADMSG;
	/* call completion callback with failure status */
	if (req) {
		error_status = chcr_handle_resp(req, input, error_status);
	} else {
		pr_err("Incorrect request address from the firmware\n");
		return -EFAULT;
	}
	if (error_status)
		atomic_inc(&adap->chcr_stats.error);

	return 0;
}

int chcr_send_wr(struct sk_buff *skb)
{
	return cxgb4_crypto_send(skb->dev, skb);
}

static void *chcr_uld_add(const struct cxgb4_lld_info *lld)
{
	struct uld_ctx *u_ctx;

	/* Create the device and add it in the device list */
	pr_info_once("%s - version %s\n", DRV_DESC, DRV_VERSION);
	if (!(lld->ulp_crypto & ULP_CRYPTO_LOOKASIDE))
		return ERR_PTR(-EOPNOTSUPP);

	/* Create the device and add it in the device list */
	u_ctx = kzalloc(sizeof(*u_ctx), GFP_KERNEL);
	if (!u_ctx) {
		u_ctx = ERR_PTR(-ENOMEM);
		goto out;
	}
	u_ctx->lldi = *lld;
	chcr_dev_init(u_ctx);
#ifdef CONFIG_CHELSIO_IPSEC_INLINE
	if (lld->crypto & ULP_CRYPTO_IPSEC_INLINE)
		chcr_add_xfrmops(lld);
#endif /* CONFIG_CHELSIO_IPSEC_INLINE */
out:
	return u_ctx;
}

int chcr_uld_rx_handler(void *handle, const __be64 *rsp,
			const struct pkt_gl *pgl)
{
	struct uld_ctx *u_ctx = (struct uld_ctx *)handle;
	struct chcr_dev *dev = &u_ctx->dev;
	const struct cpl_fw6_pld *rpl = (struct cpl_fw6_pld *)rsp;

	if (rpl->opcode != CPL_FW6_PLD) {
		pr_err("Unsupported opcode\n");
		return 0;
	}

	if (!pgl)
		work_handlers[rpl->opcode](dev, (unsigned char *)&rsp[1]);
	else
		work_handlers[rpl->opcode](dev, pgl->va);
	return 0;
}

#ifdef CONFIG_CHELSIO_IPSEC_INLINE
int chcr_uld_tx_handler(struct sk_buff *skb, struct net_device *dev)
{
	return chcr_ipsec_xmit(skb, dev);
}
#endif /* CONFIG_CHELSIO_IPSEC_INLINE */

static void chcr_detach_device(struct uld_ctx *u_ctx)
{
	struct chcr_dev *dev = &u_ctx->dev;

	if (dev->state == CHCR_DETACH) {
		pr_debug("Detached Event received for already detach device\n");
		return;
	}
	dev->state = CHCR_DETACH;
	if (atomic_read(&dev->inflight) != 0) {
		schedule_delayed_work(&dev->detach_work, WQ_DETACH_TM);
		wait_for_completion(&dev->detach_comp);
	}

	// Move u_ctx to inactive_dev list
	chcr_dev_move(u_ctx);
}

static int chcr_uld_state_change(void *handle, enum cxgb4_state state)
{
	struct uld_ctx *u_ctx = handle;
	int ret = 0;

	switch (state) {
	case CXGB4_STATE_UP:
		if (u_ctx->dev.state != CHCR_INIT) {
			// ALready Initialised.
			return 0;
		}
		chcr_dev_add(u_ctx);
		ret = start_crypto();
		break;

	case CXGB4_STATE_DETACH:
		chcr_detach_device(u_ctx);
		if (!atomic_read(&drv_data.dev_count))
			stop_crypto();
		break;

	case CXGB4_STATE_START_RECOVERY:
	case CXGB4_STATE_DOWN:
	default:
		break;
	}
	return ret;
}

static int __init chcr_crypto_init(void)
{
	INIT_LIST_HEAD(&drv_data.act_dev);
	INIT_LIST_HEAD(&drv_data.inact_dev);
	atomic_set(&drv_data.dev_count, 0);
	mutex_init(&drv_data.drv_mutex);
	drv_data.last_dev = NULL;
	cxgb4_register_uld(CXGB4_ULD_CRYPTO, &chcr_uld_info);

	return 0;
}

static void __exit chcr_crypto_exit(void)
{
	struct uld_ctx *u_ctx, *tmp;
	struct adapter *adap;

	stop_crypto();
	cxgb4_unregister_uld(CXGB4_ULD_CRYPTO);
	/* Remove all devices from list */
	mutex_lock(&drv_data.drv_mutex);
	list_for_each_entry_safe(u_ctx, tmp, &drv_data.act_dev, entry) {
		adap = padap(&u_ctx->dev);
		memset(&adap->chcr_stats, 0, sizeof(adap->chcr_stats));
		list_del(&u_ctx->entry);
		kfree(u_ctx);
	}
	list_for_each_entry_safe(u_ctx, tmp, &drv_data.inact_dev, entry) {
		adap = padap(&u_ctx->dev);
		memset(&adap->chcr_stats, 0, sizeof(adap->chcr_stats));
		list_del(&u_ctx->entry);
		kfree(u_ctx);
	}
	mutex_unlock(&drv_data.drv_mutex);
}

module_init(chcr_crypto_init);
module_exit(chcr_crypto_exit);

MODULE_DESCRIPTION("Crypto Co-processor for Chelsio Terminator cards.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chelsio Communications");
MODULE_VERSION(DRV_VERSION);
