/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
#include <linux/pci-aspm.h>
#endif
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/videodev2.h>
#include "fthd_drv.h"
#include "fthd_hw.h"
#include "fthd_isp.h"
#include "fthd_ringbuf.h"
#include "fthd_buffer.h"
#include "fthd_v4l2.h"
#include "fthd_debugfs.h"

static int fthd_pci_reserve_mem(struct fthd_private *dev_priv)
{
	unsigned long start;
	unsigned long len;
	int ret;

	/* Reserve resources */
	ret = pci_request_region(dev_priv->pdev, FTHD_PCI_S2_IO, "S2 IO");
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to request S2 IO\n");
		return ret;
	}

	ret = pci_request_region(dev_priv->pdev, FTHD_PCI_ISP_IO, "ISP IO");
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to request ISP IO\n");
		pci_release_region(dev_priv->pdev, FTHD_PCI_S2_IO);
		return ret;
	}

	ret = pci_request_region(dev_priv->pdev, FTHD_PCI_S2_MEM, "S2 MEM");
	if (ret) {
		pci_release_region(dev_priv->pdev, FTHD_PCI_ISP_IO);
		pci_release_region(dev_priv->pdev, FTHD_PCI_S2_IO);
		return ret;
	}

	/* S2 IO */
	start = pci_resource_start(dev_priv->pdev, FTHD_PCI_S2_IO);
	len = pci_resource_len(dev_priv->pdev, FTHD_PCI_S2_IO);
	dev_priv->s2_io = ioremap(start, len);
	dev_priv->s2_io_len = len;

	/* S2 MEM */
	start = pci_resource_start(dev_priv->pdev, FTHD_PCI_S2_MEM);
	len = pci_resource_len(dev_priv->pdev, FTHD_PCI_S2_MEM);
	dev_priv->s2_mem = ioremap(start, len);
	dev_priv->s2_mem_len = len;

	/* ISP IO */
	start = pci_resource_start(dev_priv->pdev, FTHD_PCI_ISP_IO);
	len = pci_resource_len(dev_priv->pdev, FTHD_PCI_ISP_IO);
	dev_priv->isp_io = ioremap(start, len);
	dev_priv->isp_io_len = len;

	pr_debug("Allocated S2 regs (BAR %d). %u bytes at 0x%p\n",
		 FTHD_PCI_S2_IO, dev_priv->s2_io_len, dev_priv->s2_io);

	pr_debug("Allocated S2 mem (BAR %d). %u bytes at 0x%p\n",
		 FTHD_PCI_S2_MEM, dev_priv->s2_mem_len, dev_priv->s2_mem);

	pr_debug("Allocated ISP regs (BAR %d). %u bytes at 0x%p\n",
		 FTHD_PCI_ISP_IO, dev_priv->isp_io_len, dev_priv->isp_io);

	return 0;
}

static void sharedmalloc_handler(struct fthd_private *dev_priv,
				 struct fw_channel *chan,
				 u32 entry)
{
	u32 request_size, response_size, address;
	struct isp_mem_obj *obj;
	int ret;

	request_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE);
	response_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE);
	address = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS) & ~ 3;

	if (address) {
		pr_debug("Firmware wants to free memory at %08x\n", address);
		FTHD_S2_MEMCPY_FROMIO(&obj, address - 64, sizeof(obj));
		isp_mem_destroy(obj);

		ret = fthd_channel_ringbuf_send(dev_priv, chan, 0, 0, 0, NULL);
		if (ret)
			pr_err("%s: fthd_channel_ringbuf_send: %d\n", __FUNCTION__, ret);
	} else {
		if (!request_size)
			return;
		obj = isp_mem_create(dev_priv, FTHD_MEM_SHAREDMALLOC, request_size + 64);
		if (!obj)
			return;

		pr_debug("Firmware allocated %d bytes at %08lx (tag %c%c%c%c)\n", request_size, obj->offset,
			 response_size >> 24,response_size >> 16,
			 response_size >> 8, response_size);
		FTHD_S2_MEMCPY_TOIO(obj->offset, &obj, sizeof(obj));
		ret = fthd_channel_ringbuf_send(dev_priv, chan, obj->offset + 64, 0, 0, NULL);
		if (ret)
			pr_err("%s: fthd_channel_ringbuf_send: %d\n", __FUNCTION__, ret);

	}

}


static void terminal_handler(struct fthd_private *dev_priv,
				 struct fw_channel *chan,
				 u32 entry)
{
	u32 request_size, response_size, address;
	char buf[512];

	request_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE);
	response_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE);
	address = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS) & ~ 3;

	if (!address || !request_size)
		return;

	if (request_size > 512)
		request_size = 512;
	FTHD_S2_MEMCPY_FROMIO(buf, address, request_size);
	pr_info("FWMSG: %.*s", request_size, buf);
}

static void buf_t2h_handler(struct fthd_private *dev_priv,
			    struct fw_channel *chan,
			    u32 entry)
{
	u32 request_size, response_size, address;
	int ret;
	request_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_REQUEST_SIZE);
	response_size = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_RESPONSE_SIZE);
	address = FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS);

	if (address & 1)
		return;


	fthd_buffer_return_handler(dev_priv, address & ~3, request_size);
	ret = fthd_channel_ringbuf_send(dev_priv, chan, (response_size & 0x10000000) ? address : 0,
					0, 0x80000000, NULL);
	if (ret)
		pr_err("%s: fthd_channel_ringbuf_send: %d\n", __FUNCTION__, ret);

}

static void io_t2h_handler(struct fthd_private *dev_priv,
				 struct fw_channel *chan,
				 u32 entry)
{
	int ret = fthd_channel_ringbuf_send(dev_priv, chan, 0, 0, 0, NULL);
	if (ret)
		pr_err("%s: fthd_channel_ringbuf_send: %d\n", __FUNCTION__, ret);

}

static void fthd_handle_irq(struct fthd_private *dev_priv, struct fw_channel *chan)
{
	u32 entry;
	int ret;

	if (chan == dev_priv->channel_io) {
		pr_debug("IO channel ready\n");
		wake_up_interruptible(&chan->wq);
		return;
	}

	if (chan == dev_priv->channel_buf_h2t) {
		pr_debug("H2T channel ready\n");
		wake_up_interruptible(&chan->wq);
		return;
	}

	if (chan == dev_priv->channel_debug) {
		pr_debug("DEBUG channel ready\n");
		wake_up_interruptible(&chan->wq);
		return;
	}

	while((entry = fthd_channel_ringbuf_receive(dev_priv, chan)) != (u32)-1) {
		pr_debug("channel %s: message available, address %08x\n", chan->name, FTHD_S2_MEM_READ(entry + FTHD_RINGBUF_ADDRESS_FLAGS));
		if (chan == dev_priv->channel_shared_malloc) {
			sharedmalloc_handler(dev_priv, chan, entry);
		} else if (chan == dev_priv->channel_terminal) {
			terminal_handler(dev_priv, chan, entry);
			ret = fthd_channel_ringbuf_send(dev_priv, chan, 0, 0, 0, NULL);
			if (ret)
				pr_err("%s: fthd_channel_ringbuf_send: %d\n", __FUNCTION__, ret);
		} else if (chan == dev_priv->channel_buf_t2h) {
			buf_t2h_handler(dev_priv, chan, entry);
		} else if (chan == dev_priv->channel_io_t2h) {
			io_t2h_handler(dev_priv, chan, entry);
		}
	}
}

static void fthd_irq_uninstall(struct fthd_private *dev_priv)
{
	free_irq(dev_priv->pdev->irq, dev_priv);
}

static void fthd_irq_work(struct work_struct *work)
{
	struct fthd_private *dev_priv = container_of(work, struct fthd_private, irq_work);
	struct fw_channel *chan;

	u32 pending;
	int i = 0;

	while(i++ < 500) {
		spin_lock_irq(&dev_priv->io_lock);
		pending = FTHD_ISP_REG_READ(ISP_IRQ_STATUS);
		spin_unlock_irq(&dev_priv->io_lock);

		if (!(pending & 0xf0))
			break;

		pci_write_config_dword(dev_priv->pdev, 0x94, 0);
		spin_lock_irq(&dev_priv->io_lock);
		FTHD_ISP_REG_WRITE(pending, ISP_IRQ_CLEAR);
		spin_unlock_irq(&dev_priv->io_lock);
		pci_write_config_dword(dev_priv->pdev, 0x90, 0x200);

		for(i = 0; i < dev_priv->num_channels; i++) {
			chan = dev_priv->channels[i];


			BUG_ON(chan->source > 3);
			if (!((0x10 << chan->source) & pending))
				continue;
			fthd_handle_irq(dev_priv, chan);
		}
	}

	if (i >= 500) {
		dev_err(&dev_priv->pdev->dev, "irq stuck, disabling\n");
		fthd_irq_uninstall(dev_priv);
	}
	pci_write_config_dword(dev_priv->pdev, 0x94, 0x200);
}

static irqreturn_t fthd_irq_handler(int irq, void *arg)
{
	struct fthd_private *dev_priv = arg;
	u32 pending;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->io_lock, flags);
	pending = FTHD_ISP_REG_READ(ISP_IRQ_STATUS);
	spin_unlock_irqrestore(&dev_priv->io_lock, flags);

	if (!(pending & 0xf0))
		return IRQ_NONE;

	schedule_work(&dev_priv->irq_work);

	return IRQ_HANDLED;
}

static int fthd_irq_install(struct fthd_private *dev_priv)
{
	int ret;

	ret = request_irq(dev_priv->pdev->irq, fthd_irq_handler, IRQF_SHARED,
			  KBUILD_MODNAME, (void *)dev_priv);

	if (ret)
		dev_err(&dev_priv->pdev->dev, "Failed to request IRQ\n");

	return ret;
}

static int fthd_pci_set_dma_mask(struct fthd_private *dev_priv,
				 unsigned int mask)
{
	int ret;

	ret = dma_set_mask_and_coherent(&dev_priv->pdev->dev, DMA_BIT_MASK(mask));
	if (ret) {
		dev_err(&dev_priv->pdev->dev, "Failed to set %u pci dma mask\n",
			mask);
		return ret;
	}

	dev_priv->dma_mask = mask;

	return 0;
}

static void fthd_stop_firmware(struct fthd_private *dev_priv)
{
		fthd_isp_cmd_stop(dev_priv);
	isp_powerdown(dev_priv);
}

static void fthd_pci_remove(struct pci_dev *pdev)
{
	struct fthd_private *dev_priv;

	dev_priv = pci_get_drvdata(pdev);
	if (!dev_priv)
		goto out;

	fthd_debugfs_exit(dev_priv);

	fthd_v4l2_unregister(dev_priv);

	fthd_stop_firmware(dev_priv);

	fthd_irq_uninstall(dev_priv);

	cancel_work_sync(&dev_priv->irq_work);

	isp_uninit(dev_priv);

	fthd_hw_deinit(dev_priv);

	fthd_buffer_exit(dev_priv);

	pci_disable_msi(pdev);

	if (dev_priv->s2_io)
		iounmap(dev_priv->s2_io);
	if (dev_priv->s2_mem)
		iounmap(dev_priv->s2_mem);
	if (dev_priv->isp_io)
		iounmap(dev_priv->isp_io);

	pci_release_region(pdev, FTHD_PCI_S2_IO);
	pci_release_region(pdev, FTHD_PCI_S2_MEM);
	pci_release_region(pdev, FTHD_PCI_ISP_IO);
out:
	pci_disable_device(pdev);
}

static int fthd_pci_init(struct fthd_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->pdev;
	int ret;


	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable device\n");
		return ret;
	}

	/* ASPM must be disabled on the device or it hangs while streaming */
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
			       PCIE_LINK_STATE_CLKPM);

	ret = fthd_pci_reserve_mem(dev_priv);
	if (ret)
		goto fail_enable;

	ret = pci_enable_msi(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable MSI\n");
		goto fail_reserve;
	}

	ret = fthd_irq_install(dev_priv);
	if (ret)
		goto fail_msi;

	ret = fthd_pci_set_dma_mask(dev_priv, 64);
	if (ret)
		ret = fthd_pci_set_dma_mask(dev_priv, 32);

	if (ret)
		goto fail_irq;

	dev_info(&pdev->dev, "Setting %ubit DMA mask\n", dev_priv->dma_mask);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0)
	pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(dev_priv->dma_mask));
#else
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(dev_priv->dma_mask));
#endif

	pci_set_master(pdev);
	pci_set_drvdata(pdev, dev_priv);
	return 0;

fail_irq:
	fthd_irq_uninstall(dev_priv);
fail_msi:
	pci_disable_msi(pdev);
fail_reserve:
	pci_release_region(pdev, FTHD_PCI_S2_IO);
	pci_release_region(pdev, FTHD_PCI_S2_MEM);
	pci_release_region(pdev, FTHD_PCI_ISP_IO);
fail_enable:
	pci_disable_device(pdev);
	return ret;
}

static int fthd_firmware_start(struct fthd_private *dev_priv)
{
	int ret;

	ret = fthd_isp_cmd_start(dev_priv);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_print_enable(dev_priv, 1);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_camera_config(dev_priv);
	if (ret)
		return ret;

	ret = fthd_isp_cmd_channel_info(dev_priv);
	if (ret)
		return ret;

	return fthd_isp_cmd_set_loadfile(dev_priv);

}

static int fthd_pci_probe(struct pci_dev *pdev,
			  const struct pci_device_id *entry)
{
	struct fthd_private *dev_priv;
	int ret;

	dev_info(&pdev->dev, "Found FaceTime HD camera with device id: %x\n",
		 pdev->device);

	dev_priv = kzalloc(sizeof(struct fthd_private), GFP_KERNEL);
	if (!dev_priv) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	dev_priv->ddr_model = 4;
	dev_priv->ddr_speed = 450;
	dev_priv->frametime = 40; /* 25 fps */

	spin_lock_init(&dev_priv->io_lock);
	mutex_init(&dev_priv->vb2_queue_lock);

	mutex_init(&dev_priv->ioctl_lock);
	INIT_LIST_HEAD(&dev_priv->buffer_queue);
	INIT_WORK(&dev_priv->irq_work, fthd_irq_work);

	dev_priv->pdev = pdev;

	ret = fthd_pci_init(dev_priv);
	if (ret)
		goto fail_work;

	ret = fthd_buffer_init(dev_priv);
	if (ret)
		goto fail_pci;

	ret = fthd_hw_init(dev_priv);
	if (ret)
		goto fail_buffer;

	ret = fthd_firmware_start(dev_priv);
	if (ret)
		goto fail_hw;

	ret = fthd_v4l2_register(dev_priv);
	if (ret)
		goto fail_firmware;

	ret = fthd_debugfs_init(dev_priv);
	if (ret)
		goto fail_v4l2;
	return 0;
fail_v4l2:
	fthd_v4l2_unregister(dev_priv);
fail_firmware:
	fthd_stop_firmware(dev_priv);
fail_hw:
	fthd_hw_deinit(dev_priv);
fail_buffer:
	fthd_buffer_exit(dev_priv);
fail_pci:
	fthd_irq_uninstall(dev_priv);
	pci_disable_msi(pdev);
	pci_release_region(pdev, FTHD_PCI_S2_IO);
	pci_release_region(pdev, FTHD_PCI_S2_MEM);
	pci_release_region(pdev, FTHD_PCI_ISP_IO);
	pci_disable_device(pdev);

fail_work:
	cancel_work_sync(&dev_priv->irq_work);
	kfree(dev_priv);
	return ret;
}

#ifdef CONFIG_PM
static int fthd_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	fthd_pci_remove(pdev);

	return 0;
}

static int fthd_pci_resume(struct pci_dev *pdev)
{
	fthd_pci_probe(pdev, NULL);

	return 0;
}
#endif /* CONFIG_PM */

static const struct pci_device_id fthd_pci_id_table[] = {
	{ PCI_DEVICE(0x14e4, 0x1570), 4 },
	{ 0, },
};

static struct pci_driver fthd_pci_driver = {
	.name = KBUILD_MODNAME,
	.probe = fthd_pci_probe,
	.remove = fthd_pci_remove,
	.shutdown = fthd_pci_remove,
	.id_table = fthd_pci_id_table,
#ifdef CONFIG_PM
	.suspend = fthd_pci_suspend,
	.resume = fthd_pci_resume,
#endif
};

module_pci_driver(fthd_pci_driver);

MODULE_FIRMWARE("facetimehd/firmware.bin");
MODULE_DEVICE_TABLE(pci, fthd_pci_id_table);
MODULE_AUTHOR("Patrik Jakobsson <patrik.r.jakobsson@gmail.com>");
MODULE_DESCRIPTION("FacetimeHD camera driver");
MODULE_LICENSE("GPL");
