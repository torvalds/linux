// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/dmaengine.h>
#include <linux/kmod.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "mic_device.h"

static struct mic_driver *g_drv;

static int __init mic_dp_init(void)
{
	struct mic_driver *mdrv = g_drv;
	struct mic_device *mdev = &mdrv->mdev;
	struct mic_bootparam __iomem *bootparam;
	u64 lo, hi, dp_dma_addr;
	u32 magic;

	lo = mic_read_spad(&mdrv->mdev, MIC_DPLO_SPAD);
	hi = mic_read_spad(&mdrv->mdev, MIC_DPHI_SPAD);

	dp_dma_addr = lo | (hi << 32);
	mdrv->dp = mic_card_map(mdev, dp_dma_addr, MIC_DP_SIZE);
	if (!mdrv->dp) {
		dev_err(mdrv->dev, "Cannot remap Aperture BAR\n");
		return -ENOMEM;
	}
	bootparam = mdrv->dp;
	magic = ioread32(&bootparam->magic);
	if (MIC_MAGIC != magic) {
		dev_err(mdrv->dev, "bootparam magic mismatch 0x%x\n", magic);
		return -EIO;
	}
	return 0;
}

/* Uninitialize the device page */
static void mic_dp_uninit(void)
{
	mic_card_unmap(&g_drv->mdev, g_drv->dp);
}

/**
 * mic_request_card_irq - request an irq.
 *
 * @handler: interrupt handler passed to request_threaded_irq.
 * @thread_fn: thread fn. passed to request_threaded_irq.
 * @name: The ASCII name of the callee requesting the irq.
 * @data: private data that is returned back when calling the
 * function handler.
 * @index: The doorbell index of the requester.
 *
 * returns: The cookie that is transparent to the caller. Passed
 * back when calling mic_free_irq. An appropriate error code
 * is returned on failure. Caller needs to use IS_ERR(return_val)
 * to check for failure and PTR_ERR(return_val) to obtained the
 * error code.
 *
 */
struct mic_irq *
mic_request_card_irq(irq_handler_t handler,
		     irq_handler_t thread_fn, const char *name,
		     void *data, int index)
{
	int rc = 0;
	unsigned long cookie;
	struct mic_driver *mdrv = g_drv;

	rc  = request_threaded_irq(mic_db_to_irq(mdrv, index), handler,
				   thread_fn, 0, name, data);
	if (rc) {
		dev_err(mdrv->dev, "request_threaded_irq failed rc = %d\n", rc);
		goto err;
	}
	mdrv->irq_info.irq_usage_count[index]++;
	cookie = index;
	return (struct mic_irq *)cookie;
err:
	return ERR_PTR(rc);
}

/**
 * mic_free_card_irq - free irq.
 *
 * @cookie: cookie obtained during a successful call to mic_request_threaded_irq
 * @data: private data specified by the calling function during the
 * mic_request_threaded_irq
 *
 * returns: none.
 */
void mic_free_card_irq(struct mic_irq *cookie, void *data)
{
	int index;
	struct mic_driver *mdrv = g_drv;

	index = (unsigned long)cookie & 0xFFFFU;
	free_irq(mic_db_to_irq(mdrv, index), data);
	mdrv->irq_info.irq_usage_count[index]--;
}

/**
 * mic_next_card_db - Get the doorbell with minimum usage count.
 *
 * Returns the irq index.
 */
int mic_next_card_db(void)
{
	int i;
	int index = 0;
	struct mic_driver *mdrv = g_drv;

	for (i = 0; i < mdrv->intr_info.num_intr; i++) {
		if (mdrv->irq_info.irq_usage_count[i] <
			mdrv->irq_info.irq_usage_count[index])
			index = i;
	}

	return index;
}

/**
 * mic_init_irq - Initialize irq information.
 *
 * Returns 0 in success. Appropriate error code on failure.
 */
static int mic_init_irq(void)
{
	struct mic_driver *mdrv = g_drv;

	mdrv->irq_info.irq_usage_count = kzalloc((sizeof(u32) *
			mdrv->intr_info.num_intr),
			GFP_KERNEL);
	if (!mdrv->irq_info.irq_usage_count)
		return -ENOMEM;
	return 0;
}

/**
 * mic_uninit_irq - Uninitialize irq information.
 *
 * None.
 */
static void mic_uninit_irq(void)
{
	struct mic_driver *mdrv = g_drv;

	kfree(mdrv->irq_info.irq_usage_count);
}

static inline struct mic_driver *scdev_to_mdrv(struct scif_hw_dev *scdev)
{
	return dev_get_drvdata(scdev->dev.parent);
}

static struct mic_irq *
___mic_request_irq(struct scif_hw_dev *scdev,
		   irqreturn_t (*func)(int irq, void *data),
				       const char *name, void *data,
				       int db)
{
	return mic_request_card_irq(func, NULL, name, data, db);
}

static void
___mic_free_irq(struct scif_hw_dev *scdev,
		struct mic_irq *cookie, void *data)
{
	return mic_free_card_irq(cookie, data);
}

static void ___mic_ack_interrupt(struct scif_hw_dev *scdev, int num)
{
	struct mic_driver *mdrv = scdev_to_mdrv(scdev);

	mic_ack_interrupt(&mdrv->mdev);
}

static int ___mic_next_db(struct scif_hw_dev *scdev)
{
	return mic_next_card_db();
}

static void ___mic_send_intr(struct scif_hw_dev *scdev, int db)
{
	struct mic_driver *mdrv = scdev_to_mdrv(scdev);

	mic_send_intr(&mdrv->mdev, db);
}

static void ___mic_send_p2p_intr(struct scif_hw_dev *scdev, int db,
				 struct mic_mw *mw)
{
	mic_send_p2p_intr(db, mw);
}

static void __iomem *
___mic_ioremap(struct scif_hw_dev *scdev,
	       phys_addr_t pa, size_t len)
{
	struct mic_driver *mdrv = scdev_to_mdrv(scdev);

	return mic_card_map(&mdrv->mdev, pa, len);
}

static void ___mic_iounmap(struct scif_hw_dev *scdev, void __iomem *va)
{
	struct mic_driver *mdrv = scdev_to_mdrv(scdev);

	mic_card_unmap(&mdrv->mdev, va);
}

static struct scif_hw_ops scif_hw_ops = {
	.request_irq = ___mic_request_irq,
	.free_irq = ___mic_free_irq,
	.ack_interrupt = ___mic_ack_interrupt,
	.next_db = ___mic_next_db,
	.send_intr = ___mic_send_intr,
	.send_p2p_intr = ___mic_send_p2p_intr,
	.remap = ___mic_ioremap,
	.unmap = ___mic_iounmap,
};

static inline struct mic_driver *vpdev_to_mdrv(struct vop_device *vpdev)
{
	return dev_get_drvdata(vpdev->dev.parent);
}

static struct mic_irq *
__mic_request_irq(struct vop_device *vpdev,
		  irqreturn_t (*func)(int irq, void *data),
		   const char *name, void *data, int intr_src)
{
	return mic_request_card_irq(func, NULL, name, data, intr_src);
}

static void __mic_free_irq(struct vop_device *vpdev,
			   struct mic_irq *cookie, void *data)
{
	return mic_free_card_irq(cookie, data);
}

static void __mic_ack_interrupt(struct vop_device *vpdev, int num)
{
	struct mic_driver *mdrv = vpdev_to_mdrv(vpdev);

	mic_ack_interrupt(&mdrv->mdev);
}

static int __mic_next_db(struct vop_device *vpdev)
{
	return mic_next_card_db();
}

static void __iomem *__mic_get_remote_dp(struct vop_device *vpdev)
{
	struct mic_driver *mdrv = vpdev_to_mdrv(vpdev);

	return mdrv->dp;
}

static void __mic_send_intr(struct vop_device *vpdev, int db)
{
	struct mic_driver *mdrv = vpdev_to_mdrv(vpdev);

	mic_send_intr(&mdrv->mdev, db);
}

static void __iomem *__mic_ioremap(struct vop_device *vpdev,
				   dma_addr_t pa, size_t len)
{
	struct mic_driver *mdrv = vpdev_to_mdrv(vpdev);

	return mic_card_map(&mdrv->mdev, pa, len);
}

static void __mic_iounmap(struct vop_device *vpdev, void __iomem *va)
{
	struct mic_driver *mdrv = vpdev_to_mdrv(vpdev);

	mic_card_unmap(&mdrv->mdev, va);
}

static struct vop_hw_ops vop_hw_ops = {
	.request_irq = __mic_request_irq,
	.free_irq = __mic_free_irq,
	.ack_interrupt = __mic_ack_interrupt,
	.next_db = __mic_next_db,
	.get_remote_dp = __mic_get_remote_dp,
	.send_intr = __mic_send_intr,
	.remap = __mic_ioremap,
	.unmap = __mic_iounmap,
};

static int mic_request_dma_chans(struct mic_driver *mdrv)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	do {
		chan = dma_request_channel(mask, NULL, NULL);
		if (chan) {
			mdrv->dma_ch[mdrv->num_dma_ch++] = chan;
			if (mdrv->num_dma_ch >= MIC_MAX_DMA_CHAN)
				break;
		}
	} while (chan);
	dev_info(mdrv->dev, "DMA channels # %d\n", mdrv->num_dma_ch);
	return mdrv->num_dma_ch;
}

static void mic_free_dma_chans(struct mic_driver *mdrv)
{
	int i = 0;

	for (i = 0; i < mdrv->num_dma_ch; i++) {
		dma_release_channel(mdrv->dma_ch[i]);
		mdrv->dma_ch[i] = NULL;
	}
	mdrv->num_dma_ch = 0;
}

/*
 * mic_driver_init - MIC driver initialization tasks.
 *
 * Returns 0 in success. Appropriate error code on failure.
 */
int __init mic_driver_init(struct mic_driver *mdrv)
{
	int rc;
	struct mic_bootparam __iomem *bootparam;
	u8 node_id;

	g_drv = mdrv;
	/* Unloading the card module is not supported. */
	if (!try_module_get(mdrv->dev->driver->owner)) {
		rc = -ENODEV;
		goto done;
	}
	rc = mic_dp_init();
	if (rc)
		goto put;
	rc = mic_init_irq();
	if (rc)
		goto dp_uninit;
	if (!mic_request_dma_chans(mdrv)) {
		rc = -ENODEV;
		goto irq_uninit;
	}
	mdrv->vpdev = vop_register_device(mdrv->dev, VOP_DEV_TRNSP,
					  NULL, &vop_hw_ops, 0,
					  NULL, mdrv->dma_ch[0]);
	if (IS_ERR(mdrv->vpdev)) {
		rc = PTR_ERR(mdrv->vpdev);
		goto dma_free;
	}
	bootparam = mdrv->dp;
	node_id = ioread8(&bootparam->node_id);
	mdrv->scdev = scif_register_device(mdrv->dev, MIC_SCIF_DEV,
					   NULL, &scif_hw_ops,
					   0, node_id, &mdrv->mdev.mmio, NULL,
					   NULL, mdrv->dp, mdrv->dma_ch,
					   mdrv->num_dma_ch, true);
	if (IS_ERR(mdrv->scdev)) {
		rc = PTR_ERR(mdrv->scdev);
		goto vop_remove;
	}
	mic_create_card_debug_dir(mdrv);
done:
	return rc;
vop_remove:
	vop_unregister_device(mdrv->vpdev);
dma_free:
	mic_free_dma_chans(mdrv);
irq_uninit:
	mic_uninit_irq();
dp_uninit:
	mic_dp_uninit();
put:
	module_put(mdrv->dev->driver->owner);
	return rc;
}

/*
 * mic_driver_uninit - MIC driver uninitialization tasks.
 *
 * Returns None
 */
void mic_driver_uninit(struct mic_driver *mdrv)
{
	mic_delete_card_debug_dir(mdrv);
	scif_unregister_device(mdrv->scdev);
	vop_unregister_device(mdrv->vpdev);
	mic_free_dma_chans(mdrv);
	mic_uninit_irq();
	mic_dp_uninit();
	module_put(mdrv->dev->driver->owner);
}
