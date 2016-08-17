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

static LIST_HEAD(uld_ctx_list);
static DEFINE_MUTEX(dev_mutex);
static atomic_t dev_count;

typedef int (*chcr_handler_func)(struct chcr_dev *dev, unsigned char *input);
static int cpl_fw6_pld_handler(struct chcr_dev *dev, unsigned char *input);
static void *chcr_uld_add(const struct cxgb4_lld_info *lld);
static int chcr_uld_state_change(void *handle, enum cxgb4_state state);

static chcr_handler_func work_handlers[NUM_CPL_CMDS] = {
	[CPL_FW6_PLD] = cpl_fw6_pld_handler,
};

static struct cxgb4_pci_uld_info chcr_uld_info = {
	.name = DRV_MODULE_NAME,
	.nrxq = 4,
	.rxq_size = 1024,
	.nciq = 0,
	.ciq_size = 0,
	.add = chcr_uld_add,
	.state_change = chcr_uld_state_change,
	.rx_handler = chcr_uld_rx_handler,
};

int assign_chcr_device(struct chcr_dev **dev)
{
	struct uld_ctx *u_ctx;

	/*
	 * Which device to use if multiple devices are available TODO
	 * May be select the device based on round robin. One session
	 * must go to the same device to maintain the ordering.
	 */
	mutex_lock(&dev_mutex); /* TODO ? */
	u_ctx = list_first_entry(&uld_ctx_list, struct uld_ctx, entry);
	if (!u_ctx) {
		mutex_unlock(&dev_mutex);
		return -ENXIO;
	}

	*dev = u_ctx->dev;
	mutex_unlock(&dev_mutex);
	return 0;
}

static int chcr_dev_add(struct uld_ctx *u_ctx)
{
	struct chcr_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENXIO;

	spin_lock_init(&dev->lock_chcr_dev);
	u_ctx->dev = dev;
	dev->u_ctx = u_ctx;
	atomic_inc(&dev_count);
	return 0;
}

static int chcr_dev_remove(struct uld_ctx *u_ctx)
{
	kfree(u_ctx->dev);
	u_ctx->dev = NULL;
	atomic_dec(&dev_count);
	return 0;
}

static int cpl_fw6_pld_handler(struct chcr_dev *dev,
			       unsigned char *input)
{
	struct crypto_async_request *req;
	struct cpl_fw6_pld *fw6_pld;
	u32 ack_err_status = 0;
	int error_status = 0;

	fw6_pld = (struct cpl_fw6_pld *)input;
	req = (struct crypto_async_request *)(uintptr_t)be64_to_cpu(
						    fw6_pld->data[1]);

	ack_err_status =
		ntohl(*(__be32 *)((unsigned char *)&fw6_pld->data[0] + 4));
	if (ack_err_status) {
		if (CHK_MAC_ERR_BIT(ack_err_status) ||
		    CHK_PAD_ERR_BIT(ack_err_status))
			error_status = -EINVAL;
	}
	/* call completion callback with failure status */
	if (req) {
		if (!chcr_handle_resp(req, input, error_status))
			req->complete(req, error_status);
		else
			return -EINVAL;
	} else {
		pr_err("Incorrect request address from the firmware\n");
		return -EFAULT;
	}
	return 0;
}

int chcr_send_wr(struct sk_buff *skb)
{
	return cxgb4_ofld_send(skb->dev, skb);
}

static void *chcr_uld_add(const struct cxgb4_lld_info *lld)
{
	struct uld_ctx *u_ctx;

	/* Create the device and add it in the device list */
	u_ctx = kzalloc(sizeof(*u_ctx), GFP_KERNEL);
	if (!u_ctx) {
		u_ctx = ERR_PTR(-ENOMEM);
		goto out;
	}
	u_ctx->lldi = *lld;
	mutex_lock(&dev_mutex);
	list_add_tail(&u_ctx->entry, &uld_ctx_list);
	mutex_unlock(&dev_mutex);
out:
	return u_ctx;
}

int chcr_uld_rx_handler(void *handle, const __be64 *rsp,
			const struct pkt_gl *pgl)
{
	struct uld_ctx *u_ctx = (struct uld_ctx *)handle;
	struct chcr_dev *dev = u_ctx->dev;
	const struct cpl_act_establish *rpl = (struct cpl_act_establish
					       *)rsp;

	if (rpl->ot.opcode != CPL_FW6_PLD) {
		pr_err("Unsupported opcode\n");
		return 0;
	}

	if (!pgl)
		work_handlers[rpl->ot.opcode](dev, (unsigned char *)&rsp[1]);
	else
		work_handlers[rpl->ot.opcode](dev, pgl->va);
	return 0;
}

static int chcr_uld_state_change(void *handle, enum cxgb4_state state)
{
	struct uld_ctx *u_ctx = handle;
	int ret = 0;

	switch (state) {
	case CXGB4_STATE_UP:
		if (!u_ctx->dev) {
			ret = chcr_dev_add(u_ctx);
			if (ret != 0)
				return ret;
		}
		if (atomic_read(&dev_count) == 1)
			ret = start_crypto();
		break;

	case CXGB4_STATE_DETACH:
		if (u_ctx->dev) {
			mutex_lock(&dev_mutex);
			chcr_dev_remove(u_ctx);
			mutex_unlock(&dev_mutex);
		}
		if (!atomic_read(&dev_count))
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
	if (cxgb4_register_pci_uld(CXGB4_PCI_ULD1, &chcr_uld_info)) {
		pr_err("ULD register fail: No chcr crypto support in cxgb4");
		return -1;
	}

	return 0;
}

static void __exit chcr_crypto_exit(void)
{
	struct uld_ctx *u_ctx, *tmp;

	if (atomic_read(&dev_count))
		stop_crypto();

	/* Remove all devices from list */
	mutex_lock(&dev_mutex);
	list_for_each_entry_safe(u_ctx, tmp, &uld_ctx_list, entry) {
		if (u_ctx->dev)
			chcr_dev_remove(u_ctx);
		kfree(u_ctx);
	}
	mutex_unlock(&dev_mutex);
	cxgb4_unregister_pci_uld(CXGB4_PCI_ULD1);
}

module_init(chcr_crypto_init);
module_exit(chcr_crypto_exit);

MODULE_DESCRIPTION("Crypto Co-processor for Chelsio Terminator cards.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chelsio Communications");
MODULE_VERSION(DRV_VERSION);
