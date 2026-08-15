// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/irq.h>
#include <linux/pm_runtime.h>
#include <linux/vmalloc.h>
#include <media/v4l2-ioctl.h>

#include "isp4.h"
#include "isp4_debug.h"
#include "isp4_hw_reg.h"

#define ISP4_DRV_NAME "amd_isp_capture"
#define ISP4_FW_RESP_RB_IRQ_STATUS_MASK \
	(ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT9_INT_MASK  | \
	 ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT12_INT_MASK)

static const struct {
	const char *name;
	u32 status_mask;
	u32 en_mask;
	u32 ack_mask;
	u32 rb_int_num;
} isp4_irq[ISP4SD_MAX_FW_RESP_STREAM_NUM] = {
	/* The IRQ order is aligned with the isp4_subdev.fw_resp_thread order */
	{
		.name = "isp_irq_global",
		.status_mask =
		ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT12_INT_MASK,
		.en_mask = ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT12_EN_MASK,
		.ack_mask = ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT12_ACK_MASK,
		.rb_int_num = 4, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT12 */
	},
	{
		.name = "isp_irq_stream1",
		.status_mask =
		ISP_SYS_INT0_STATUS__SYS_INT_RINGBUFFER_WPT9_INT_MASK,
		.en_mask = ISP_SYS_INT0_EN__SYS_INT_RINGBUFFER_WPT9_EN_MASK,
		.ack_mask = ISP_SYS_INT0_ACK__SYS_INT_RINGBUFFER_WPT9_ACK_MASK,
		.rb_int_num = 0, /* ISP_4_1__SRCID__ISP_RINGBUFFER_WPT9 */
	},
};

void isp4_intr_enable(struct isp4_subdev *isp_subdev, u32 index, bool enable)
{
	u32 intr_en;

	/* Synchronize ISP_SYS_INT0_EN writes with the IRQ handler's writes */
	spin_lock_irq(&isp_subdev->irq_lock);
	intr_en = isp4hw_rreg(isp_subdev->mmio, ISP_SYS_INT0_EN);
	if (enable)
		intr_en |= isp4_irq[index].en_mask;
	else
		intr_en &= ~isp4_irq[index].en_mask;

	isp4hw_wreg(isp_subdev->mmio, ISP_SYS_INT0_EN, intr_en);
	spin_unlock_irq(&isp_subdev->irq_lock);
}

static void isp4_wake_up_resp_thread(struct isp4_subdev *isp_subdev, u32 index)
{
	struct isp4sd_thread_handler *thread_ctx =
			&isp_subdev->fw_resp_thread[index];

	thread_ctx->resp_ready = true;
	wake_up_interruptible(&thread_ctx->waitq);
}

static irqreturn_t isp4_irq_handler(int irq, void *arg)
{
	struct isp4_subdev *isp_subdev = arg;
	u32 intr_ack = 0, intr_en = 0, intr_status;
	int seen = 0;

	/* Get the ISP_SYS interrupt status */
	intr_status = isp4hw_rreg(isp_subdev->mmio, ISP_SYS_INT0_STATUS);
	intr_status &= ISP4_FW_RESP_RB_IRQ_STATUS_MASK;

	/* Find which ISP_SYS interrupts fired */
	for (size_t i = 0; i < ARRAY_SIZE(isp4_irq); i++) {
		if (intr_status & isp4_irq[i].status_mask) {
			intr_ack |= isp4_irq[i].ack_mask;
			intr_en |= isp4_irq[i].en_mask;
			seen |= BIT(i);
		}
	}

	/*
	 * Disable the ISP_SYS interrupts that fired. Must be done before waking
	 * the response threads, since they re-enable interrupts when finished.
	 * The lock synchronizes RMW of INT0_EN with isp4_enable_interrupt().
	 */
	spin_lock(&isp_subdev->irq_lock);
	intr_en = isp4hw_rreg(isp_subdev->mmio, ISP_SYS_INT0_EN) & ~intr_en;
	isp4hw_wreg(isp_subdev->mmio, ISP_SYS_INT0_EN, intr_en);
	spin_unlock(&isp_subdev->irq_lock);

	/*
	 * Clear the ISP_SYS interrupts. This must be done after the interrupts
	 * are disabled, so that ISP FW won't flag any new interrupts on these
	 * streams, and thus we don't need to clear interrupts again before
	 * re-enabling them in the response thread.
	 */
	isp4hw_wreg(isp_subdev->mmio, ISP_SYS_INT0_ACK, intr_ack);

	/*
	 * The operation `(seen >> i) << i` is logically equivalent to
	 * `seen &= ~BIT(i)`, with fewer instructions after compilation.
	 */
	for (int i; (i = ffs(seen)); seen = (seen >> i) << i)
		isp4_wake_up_resp_thread(isp_subdev, i - 1);

	return IRQ_HANDLED;
}

static int isp4_capture_probe(struct platform_device *pdev)
{
	int irq[ISP4SD_MAX_FW_RESP_STREAM_NUM];
	struct device *dev = &pdev->dev;
	struct isp4_subdev *isp_subdev;
	struct isp4_device *isp_dev;
	int ret;

	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	dev->init_name = ISP4_DRV_NAME;

	isp_subdev = &isp_dev->isp_subdev;
	isp_subdev->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(isp_subdev->mmio))
		return dev_err_probe(dev, PTR_ERR(isp_subdev->mmio),
				     "isp ioremap fail\n");

	for (size_t i = 0; i < ARRAY_SIZE(isp4_irq); i++) {
		irq[i] = platform_get_irq(pdev, isp4_irq[i].rb_int_num);
		if (irq[i] < 0)
			return dev_err_probe(dev, irq[i],
					     "fail to get irq %d\n",
					     isp4_irq[i].rb_int_num);

		ret = devm_request_irq(dev, irq[i], isp4_irq_handler,
				       IRQF_NO_AUTOEN, isp4_irq[i].name,
				       isp_subdev);
		if (ret)
			return dev_err_probe(dev, ret, "fail to req irq %d\n",
					     irq[i]);
	}

	isp_dev->v4l2_dev.mdev = &isp_dev->mdev;

	strscpy(isp_dev->mdev.model, "amd_isp41_mdev",
		sizeof(isp_dev->mdev.model));
	isp_dev->mdev.dev = dev;
	media_device_init(&isp_dev->mdev);

	snprintf(isp_dev->v4l2_dev.name, sizeof(isp_dev->v4l2_dev.name),
		 "AMD-V4L2-ROOT");
	ret = v4l2_device_register(dev, &isp_dev->v4l2_dev);
	if (ret) {
		dev_err_probe(dev, ret, "fail register v4l2 device\n");
		goto err_clean_media;
	}

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);
	spin_lock_init(&isp_subdev->irq_lock);
	ret = isp4sd_init(&isp_dev->isp_subdev, &isp_dev->v4l2_dev, irq);
	if (ret) {
		dev_err_probe(dev, ret, "fail init isp4 sub dev\n");
		goto err_pm_disable;
	}

	ret = media_create_pad_link(&isp_dev->isp_subdev.sdev.entity,
				    0,
				    &isp_dev->isp_subdev.isp_vdev.vdev.entity,
				    0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err_probe(dev, ret, "fail to create pad link\n");
		goto err_isp4_deinit;
	}

	ret = media_device_register(&isp_dev->mdev);
	if (ret) {
		dev_err_probe(dev, ret, "fail to register media device\n");
		goto err_isp4_deinit;
	}

	platform_set_drvdata(pdev, isp_dev);
	isp_debugfs_create(isp_dev);

	return 0;

err_isp4_deinit:
	isp4sd_deinit(&isp_dev->isp_subdev);
err_pm_disable:
	pm_runtime_disable(dev);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
err_clean_media:
	media_device_cleanup(&isp_dev->mdev);

	return ret;
}

static void isp4_capture_remove(struct platform_device *pdev)
{
	struct isp4_device *isp_dev = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	isp_debugfs_remove(isp_dev);

	media_device_unregister(&isp_dev->mdev);
	isp4sd_deinit(&isp_dev->isp_subdev);
	pm_runtime_disable(dev);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	media_device_cleanup(&isp_dev->mdev);
}

static struct platform_driver isp4_capture_drv = {
	.probe = isp4_capture_probe,
	.remove = isp4_capture_remove,
	.driver = {
		.name = ISP4_DRV_NAME,
	}
};

module_platform_driver(isp4_capture_drv);

MODULE_ALIAS("platform:" ISP4_DRV_NAME);
MODULE_IMPORT_NS("DMA_BUF");

MODULE_DESCRIPTION("AMD ISP4 Driver");
MODULE_AUTHOR("Bin Du <bin.du@amd.com>");
MODULE_AUTHOR("Pratap Nirujogi <pratap.nirujogi@amd.com>");
MODULE_LICENSE("GPL");
