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
#include <linux/slab.h>
#include <linux/videodev2.h>
#include "solo6x10.h"
#include "tw28.h"

MODULE_DESCRIPTION("Softlogic 6x10 MP4/H.264 Encoder/Decoder V4L2/ALSA Driver");
MODULE_AUTHOR("Ben Collins <bcollins@bluecherry.net>");
MODULE_VERSION(SOLO6X10_VERSION);
MODULE_LICENSE("GPL");

void solo_irq_on(struct solo_dev *solo_dev, u32 mask)
{
	solo_dev->irq_mask |= mask;
	solo_reg_write(solo_dev, SOLO_IRQ_ENABLE, solo_dev->irq_mask);
}

void solo_irq_off(struct solo_dev *solo_dev, u32 mask)
{
	solo_dev->irq_mask &= ~mask;
	solo_reg_write(solo_dev, SOLO_IRQ_ENABLE, solo_dev->irq_mask);
}

/* XXX We should check the return value of the sub-device ISR's */
static irqreturn_t solo_isr(int irq, void *data)
{
	struct solo_dev *solo_dev = data;
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

static void free_solo_dev(struct solo_dev *solo_dev)
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
		solo_irq_off(solo_dev, ~0);
		pci_iounmap(pdev, solo_dev->reg_base);
		free_irq(pdev->irq, solo_dev);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	kfree(solo_dev);
}

static int __devinit solo_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	struct solo_dev *solo_dev;
	int ret;
	int sdram;
	u8 chip_id;
	u32 reg;

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

	ret = pci_request_regions(pdev, SOLO6X10_NAME);
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

	solo_dev->flags = id->driver_data;

	/* Disable all interrupts to start */
	solo_irq_off(solo_dev, ~0);

	reg = SOLO_SYS_CFG_SDRAM64BIT;
	/* Initial global settings */
	if (!(solo_dev->flags & FLAGS_6110))
		reg |= SOLO6010_SYS_CFG_INPUTDIV(25) |
			SOLO6010_SYS_CFG_FEEDBACKDIV((SOLO_CLOCK_MHZ * 2) - 2) |
			SOLO6010_SYS_CFG_OUTDIV(3);
	solo_reg_write(solo_dev, SOLO_SYS_CFG, reg);

        if (solo_dev->flags & FLAGS_6110) {
                u32 sys_clock_MHz = SOLO_CLOCK_MHZ;
                u32 pll_DIVQ;
                u32 pll_DIVF;

                if (sys_clock_MHz < 125) {
                        pll_DIVQ = 3;
                        pll_DIVF = (sys_clock_MHz * 4) / 3;
                } else {
                        pll_DIVQ = 2;
                        pll_DIVF = (sys_clock_MHz * 2) / 3;
                }

                solo_reg_write(solo_dev, SOLO6110_PLL_CONFIG,
			       SOLO6110_PLL_RANGE_5_10MHZ |
			       SOLO6110_PLL_DIVR(9) |
			       SOLO6110_PLL_DIVQ_EXP(pll_DIVQ) |
			       SOLO6110_PLL_DIVF(pll_DIVF) | SOLO6110_PLL_FSEN);
		mdelay(1);      // PLL Locking time (1ms)

		solo_reg_write(solo_dev, SOLO_DMA_CTRL1, 3 << 8); /* ? */
        } else
		solo_reg_write(solo_dev, SOLO_DMA_CTRL1, 1 << 8); /* ? */

	solo_reg_write(solo_dev, SOLO_TIMER_CLOCK_NUM, SOLO_CLOCK_MHZ - 1);

	/* PLL locking time of 1ms */
	mdelay(1);

	ret = request_irq(pdev->irq, solo_isr, IRQF_SHARED, SOLO6X10_NAME,
			  solo_dev);
	if (ret)
		goto fail_probe;

	/* Handle this from the start */
	solo_irq_on(solo_dev, SOLO_IRQ_PCI_ERR);

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

static void __devexit solo_pci_remove(struct pci_dev *pdev)
{
	struct solo_dev *solo_dev = pci_get_drvdata(pdev);

	free_solo_dev(solo_dev);
}

static struct pci_device_id solo_id_table[] = {
	/* 6010 based cards */
	{PCI_DEVICE(PCI_VENDOR_ID_SOFTLOGIC, PCI_DEVICE_ID_SOLO6010)},
	{PCI_DEVICE(PCI_VENDOR_ID_SOFTLOGIC, PCI_DEVICE_ID_SOLO6110),
	 .driver_data = FLAGS_6110},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_NEUSOLO_16)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_SOLO_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_SOLO_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_SOLO_16)},
	/* 6110 based cards */
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_6110_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_6110_8)},
	{PCI_DEVICE(PCI_VENDOR_ID_BLUECHERRY, PCI_DEVICE_ID_BC_6110_16)},
	{0,}
};

MODULE_DEVICE_TABLE(pci, solo_id_table);

static struct pci_driver solo_pci_driver = {
	.name = SOLO6X10_NAME,
	.id_table = solo_id_table,
	.probe = solo_pci_probe,
	.remove = solo_pci_remove,
};

static int __init solo_module_init(void)
{
	return pci_register_driver(&solo_pci_driver);
}

static void __exit solo_module_exit(void)
{
	pci_unregister_driver(&solo_pci_driver);
}

module_init(solo_module_init);
module_exit(solo_module_exit);
