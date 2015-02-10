/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
 *
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_x100.h"

static const char mic_driver_name[] = "mic";

static struct mic_driver g_drv;

/**
 * mic_read_spad - read from the scratchpad register
 * @mdev: pointer to mic_device instance
 * @idx: index to scratchpad register, 0 based
 *
 * This function allows reading of the 32bit scratchpad register.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
u32 mic_read_spad(struct mic_device *mdev, unsigned int idx)
{
	return mic_mmio_read(&mdev->mmio,
		MIC_X100_SBOX_BASE_ADDRESS +
		MIC_X100_SBOX_SPAD0 + idx * 4);
}

/**
 * __mic_send_intr - Send interrupt to Host.
 * @mdev: pointer to mic_device instance
 * @doorbell: Doorbell number.
 */
void mic_send_intr(struct mic_device *mdev, int doorbell)
{
	struct mic_mw *mw = &mdev->mmio;

	if (doorbell > MIC_X100_MAX_DOORBELL_IDX)
		return;
	/* Ensure that the interrupt is ordered w.r.t previous stores. */
	wmb();
	mic_mmio_write(mw, MIC_X100_SBOX_SDBIC0_DBREQ_BIT,
		       MIC_X100_SBOX_BASE_ADDRESS +
		       (MIC_X100_SBOX_SDBIC0 + (4 * doorbell)));
}

/**
 * mic_ack_interrupt - Device specific interrupt handling.
 * @mdev: pointer to mic_device instance
 *
 * Returns: bitmask of doorbell events triggered.
 */
u32 mic_ack_interrupt(struct mic_device *mdev)
{
	return 0;
}

static inline int mic_get_sbox_irq(int db)
{
	return MIC_X100_IRQ_BASE + db;
}

static inline int mic_get_rdmasr_irq(int index)
{
	return  MIC_X100_RDMASR_IRQ_BASE + index;
}

/**
 * mic_hw_intr_init - Initialize h/w specific interrupt
 * information.
 * @mdrv: pointer to mic_driver
 */
void mic_hw_intr_init(struct mic_driver *mdrv)
{
	mdrv->intr_info.num_intr = MIC_X100_NUM_SBOX_IRQ +
				MIC_X100_NUM_RDMASR_IRQ;
}

/**
 * mic_db_to_irq - Retrieve irq number corresponding to a doorbell.
 * @mdrv: pointer to mic_driver
 * @db: The doorbell obtained for which the irq is needed. Doorbell
 * may correspond to an sbox doorbell or an rdmasr index.
 *
 * Returns the irq corresponding to the doorbell.
 */
int mic_db_to_irq(struct mic_driver *mdrv, int db)
{
	int rdmasr_index;
	if (db < MIC_X100_NUM_SBOX_IRQ) {
		return mic_get_sbox_irq(db);
	} else {
		rdmasr_index = db - MIC_X100_NUM_SBOX_IRQ +
			MIC_X100_RDMASR_IRQ_BASE;
		return mic_get_rdmasr_irq(rdmasr_index);
	}
}

/*
 * mic_card_map - Allocate virtual address for a remote memory region.
 * @mdev: pointer to mic_device instance.
 * @addr: Remote DMA address.
 * @size: Size of the region.
 *
 * Returns: Virtual address backing the remote memory region.
 */
void __iomem *
mic_card_map(struct mic_device *mdev, dma_addr_t addr, size_t size)
{
	return ioremap(addr, size);
}

/*
 * mic_card_unmap - Unmap the virtual address for a remote memory region.
 * @mdev: pointer to mic_device instance.
 * @addr: Virtual address for remote memory region.
 *
 * Returns: None.
 */
void mic_card_unmap(struct mic_device *mdev, void __iomem *addr)
{
	iounmap(addr);
}

static inline struct mic_driver *mbdev_to_mdrv(struct mbus_device *mbdev)
{
	return dev_get_drvdata(mbdev->dev.parent);
}

static struct mic_irq *
_mic_request_threaded_irq(struct mbus_device *mbdev,
			  irq_handler_t handler, irq_handler_t thread_fn,
			  const char *name, void *data, int intr_src)
{
	int rc = 0;
	unsigned int irq = intr_src;
	unsigned long cookie = irq;

	rc  = request_threaded_irq(irq, handler, thread_fn, 0, name, data);
	if (rc) {
		dev_err(mbdev_to_mdrv(mbdev)->dev,
			"request_threaded_irq failed rc = %d\n", rc);
		return ERR_PTR(rc);
	}
	return (struct mic_irq *)cookie;
}

static void _mic_free_irq(struct mbus_device *mbdev,
			  struct mic_irq *cookie, void *data)
{
	unsigned long irq = (unsigned long)cookie;
	free_irq(irq, data);
}

static void _mic_ack_interrupt(struct mbus_device *mbdev, int num)
{
	mic_ack_interrupt(&mbdev_to_mdrv(mbdev)->mdev);
}

static struct mbus_hw_ops mbus_hw_ops = {
	.request_threaded_irq = _mic_request_threaded_irq,
	.free_irq = _mic_free_irq,
	.ack_interrupt = _mic_ack_interrupt,
};

static int __init mic_probe(struct platform_device *pdev)
{
	struct mic_driver *mdrv = &g_drv;
	struct mic_device *mdev = &mdrv->mdev;
	int rc = 0;

	mdrv->dev = &pdev->dev;
	snprintf(mdrv->name, sizeof(mic_driver_name), mic_driver_name);

	mdev->mmio.pa = MIC_X100_MMIO_BASE;
	mdev->mmio.len = MIC_X100_MMIO_LEN;
	mdev->mmio.va = devm_ioremap(&pdev->dev, MIC_X100_MMIO_BASE,
				     MIC_X100_MMIO_LEN);
	if (!mdev->mmio.va) {
		dev_err(&pdev->dev, "Cannot remap MMIO BAR\n");
		rc = -EIO;
		goto done;
	}
	mic_hw_intr_init(mdrv);
	platform_set_drvdata(pdev, mdrv);
	mdrv->dma_mbdev = mbus_register_device(mdrv->dev, MBUS_DEV_DMA_MIC,
					       NULL, &mbus_hw_ops,
					       mdrv->mdev.mmio.va);
	if (IS_ERR(mdrv->dma_mbdev)) {
		rc = PTR_ERR(mdrv->dma_mbdev);
		dev_err(&pdev->dev, "mbus_add_device failed rc %d\n", rc);
		goto done;
	}
	rc = mic_driver_init(mdrv);
	if (rc) {
		dev_err(&pdev->dev, "mic_driver_init failed rc %d\n", rc);
		goto remove_dma;
	}
done:
	return rc;
remove_dma:
	mbus_unregister_device(mdrv->dma_mbdev);
	return rc;
}

static int mic_remove(struct platform_device *pdev)
{
	struct mic_driver *mdrv = &g_drv;

	mic_driver_uninit(mdrv);
	mbus_unregister_device(mdrv->dma_mbdev);
	return 0;
}

static void mic_platform_shutdown(struct platform_device *pdev)
{
	mic_remove(pdev);
}

static struct platform_device mic_platform_dev = {
	.name = mic_driver_name,
	.id   = 0,
	.num_resources = 0,
};

static struct platform_driver __refdata mic_platform_driver = {
	.probe = mic_probe,
	.remove = mic_remove,
	.shutdown = mic_platform_shutdown,
	.driver         = {
		.name   = mic_driver_name,
	},
};

static int __init mic_init(void)
{
	int ret;
	struct cpuinfo_x86 *c = &cpu_data(0);

	if (!(c->x86 == 11 && c->x86_model == 1)) {
		ret = -ENODEV;
		pr_err("%s not running on X100 ret %d\n", __func__, ret);
		goto done;
	}

	mic_init_card_debugfs();
	ret = platform_device_register(&mic_platform_dev);
	if (ret) {
		pr_err("platform_device_register ret %d\n", ret);
		goto cleanup_debugfs;
	}
	ret = platform_driver_register(&mic_platform_driver);
	if (ret) {
		pr_err("platform_driver_register ret %d\n", ret);
		goto device_unregister;
	}
	return ret;

device_unregister:
	platform_device_unregister(&mic_platform_dev);
cleanup_debugfs:
	mic_exit_card_debugfs();
done:
	return ret;
}

static void __exit mic_exit(void)
{
	platform_driver_unregister(&mic_platform_driver);
	platform_device_unregister(&mic_platform_dev);
	mic_exit_card_debugfs();
}

module_init(mic_init);
module_exit(mic_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) MIC X100 Card driver");
MODULE_LICENSE("GPL v2");
