/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>

#include "solo6010.h"
#include "solo6010-tw28.h"

MODULE_DESCRIPTION("Softlogic 6010 MP4 Encoder/Decoder V4L2/ALSA Driver");
MODULE_AUTHOR("Ben Collins <bcollins@bluecherry.net>");
MODULE_VERSION(SOLO6010_VERSION);
MODULE_LICENSE("GPL");

void solo6010_irq_on(struct solo6010_dev *solo_dev, u32 mask)
{
	solo_dev->irq_mask |= mask;
	solo_reg_write(solo_dev, SOLO_IRQ_ENABLE, solo_dev->irq_mask);
}

void solo6010_irq_off(struct solo6010_dev *solo_dev, u32 mask)
{
	solo_dev->irq_mask &= ~mask;
	solo_reg_write(solo_dev, SOLO_IRQ_ENABLE, solo_dev->irq_mask);
}

/* XXX We should check the return value of the sub-device ISR's */
static irqreturn_t solo6010_isr(int irq, void *data)
{
	struct solo6010_dev *solo_dev = data;
	u32 status;
	int i;

	status = solo_reg_read(solo_dev, SOLO_IRQ_STAT);
	if (!status)
		return IRQ_NONE;

	if (status & ~solo_dev->irq_mask) {
		solo_reg_write(solo_dev, SOLO_IRQ_STAT,
			       status & ~solo_dev->irq_mask);
		status &= solo_dev->irq_mask;
	}

	if (status & SOLO_IRQ_PCI_ERR) {
		u32 err = solo_reg_read(solo_dev, SOLO_PCI_ERR);
		solo_p2m_error_isr(solo_dev, err);
		solo_reg_write(solo_dev, SOLO_IRQ_STAT, SOLO_IRQ_PCI_ERR);
	}

	for (i = 0; i < SOLO_NR_P2M; i++)
		if (status & SOLO_IRQ_P2M(i))
			solo_p2m_isr(solo_dev, i);

	if (status & SOLO_IRQ_IIC)
		solo_i2c_isr(solo_dev);

	if (status & SOLO_IRQ_VIDEO_IN)
		solo_video_in_isr(solo_dev);

	/* Call this first so enc gets detected flag set */
	if (status & SOLO_IRQ_MOTION)
		solo_motion_isr(solo_dev);

	if (status & SOLO_IRQ_ENCODER)
		solo_enc_v4l2_isr(solo_dev);

	if (status & SOLO_IRQ_G723)
		solo_g723_isr(solo_dev);

	return IRQ_HANDLED;
}

static void free_solo_dev(struct solo6010_dev *solo_dev)
{
	struct pci_dev *pdev;

	if (!solo_dev)
		return;

	pdev = solo_dev->pdev;

	/* If we never initialized the PCI device, then nothing else
	 * below here needs cleanup */
	if (!pdev) {
		kfree(solo_dev);
		return;
	}

	/* Bring down the sub-devices first */
	solo_g723_exit(solo_dev);
	solo_enc_v4l2_exit(solo_dev);
	solo_enc_exit(solo_dev);
	solo_v4l2_exit(solo_dev);
	solo_disp_exit(solo_dev);
	solo_gpio_exit(solo_dev);
	solo_p2m_exit(solo_dev);
	solo_i2c_exit(solo_dev);

	/* Now cleanup the PCI device */
	if (solo_dev->reg_base) {
		solo6010_irq_off(solo_dev, ~0);
		pci_iounmap(pdev, solo_dev->reg_base);
		free_irq(pdev->irq, solo_dev);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	kfree(solo_dev);
}

static int __devinit solo6010_pci_probe(struct pci_dev *pdev,
					const struct pci_device_id *id)
{
	struct solo6010_dev *solo_dev;
	int ret;
	int sdram;
	u8 chip_id;
	solo_dev = kzalloc(sizeof(*solo_dev), GFP_KERNEL);
	if (solo_dev == NULL)
		return -ENOMEM;

	solo_dev->pdev = pdev;
	spin_lock_init(&solo_dev->reg_io_lock);
	pci_set_drvdata(pdev, solo_dev);

	ret = pci_enable_device(pdev);
	if (ret)
		goto fail_probe;

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, SOLO6010_NAME);
	if (ret)
		goto fail_probe;

	solo_dev->reg_base = pci_ioremap_bar(pdev, 0);
	if (solo_dev->reg_base == NULL) {
		ret = -ENOMEM;
		goto fail_probe;
	}

	chip_id = solo_reg_read(solo_dev, SOLO_CHIP_OPTION) &
					SOLO_CHIP_ID_MASK;
	switch (chip_id) {
		case 7:
			solo_dev->nr_chans = 16;
			solo_dev->nr_ext = 5;
			break;
		case 6:
			solo_dev->nr_chans = 8;
			solo_dev->nr_ext = 2;
			break;
		default:
			dev_warn(&pdev->dev, "Invalid chip_id 0x%02x, "
				 "defaulting to 4 channels\n",
				 chip_id);
		case 5:
			solo_dev->nr_chans = 4;
			solo_dev->nr_ext = 1;
	}

	/* Disable all interrupts to start */
	solo6010_irq_off(solo_dev, ~0);

	/* Initial global settings */
	solo_reg_write(solo_dev, SOLO_SYS_CFG, SOLO_SYS_CFG_SDRAM64BIT |
		       SOLO_SYS_CFG_INPUTDIV(25) |
		       SOLO_SYS_CFG_FEEDBACKDIV((SOLO_CLOCK_MHZ * 2) - 2) |
		       SOLO_SYS_CFG_OUTDIV(3));
	solo_reg_write(solo_dev, SOLO_TIMER_CLOCK_NUM, SOLO_CLOCK_MHZ - 1);

	/* PLL locking time of 1ms */
	mdelay(1);

	ret = request_irq(pdev->irq, solo6010_isr, IRQF_SHARED, SOLO6010_NAME,
			  solo_dev);
	if (ret)
		goto fail_probe;

	/* Handle this from the start */
	solo6010_irq_on(solo_dev, SOLO_IRQ_PCI_ERR);

	ret = solo_i2c_init(solo_dev);
	if (ret)
		goto fail_probe;

	/* Setup the DMA engine */
	sdram = (solo_dev->nr_chans >= 8) ? 2 : 1;
	solo_reg_write(solo_dev, SOLO_DMA_CTRL,
		       SOLO_DMA_CTRL_REFRESH_CYCLE(1) |
		       SOLO_DMA_CTRL_SDRAM_SIZE(sdram) |
		       SOLO_DMA_CTRL_SDRAM_CLK_INVERT |
		       SOLO_DMA_CTRL_READ_CLK_SELECT |
		       SOLO_DMA_CTRL_LATENCY(1));

	ret = solo_p2m_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_disp_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_gpio_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_tw28_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_v4l2_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_enc_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_enc_v4l2_init(solo_dev);
	if (ret)
		goto fail_probe;

	ret = solo_g723_init(solo_dev);
	if (ret)
		goto fail_probe;

	return 0;

fail_probe:
	free_solo_dev(solo_dev);
	return ret;
}

static void __devexit solo6010_pci_remove(struct pci_dev *pdev)
{
	struct solo6010_dev *solo_dev = pci_get_drvdata(pdev);

	free_solo_dev(solo_dev);
}

static struct pci_device_id solo6010_id_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_SOFTLOGIC, PCI_DEVICE_ID_SOLO6010)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_16)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_COMMSOLO_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_COMMSOLO_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_COMMSOLO_16)},
	{0,}
};

MODULE_DEVICE_TABLE(pci, solo6010_id_table);

static struct pci_driver solo6010_pci_driver = {
	.name = SOLO6010_NAME,
	.id_table = solo6010_id_table,
	.probe = solo6010_pci_probe,
	.remove = solo6010_pci_remove,
};

static int __init solo6010_module_init(void)
{
	return pci_register_driver(&solo6010_pci_driver);
}

static void __exit solo6010_module_exit(void)
{
	pci_unregister_driver(&solo6010_pci_driver);
}

module_init(solo6010_module_init);
module_exit(solo6010_module_exit);
