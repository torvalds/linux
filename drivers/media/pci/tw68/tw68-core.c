/*
 *  tw68-core.c
 *  Core functions for the Techwell 68xx driver
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack
 *
 *  Refactored and updated to the latest v4l core frameworks:
 *
 *  Copyright (C) 2014 Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/sound.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/pm.h>

#include <media/v4l2-dev.h>
#include "tw68.h"
#include "tw68-reg.h"

MODULE_DESCRIPTION("v4l2 driver module for tw6800 based video capture cards");
MODULE_AUTHOR("William M. Brack");
MODULE_AUTHOR("Hans Verkuil <hverkuil@xs4all.nl>");
MODULE_LICENSE("GPL");

static unsigned int latency = UNSET;
module_param(latency, int, 0444);
MODULE_PARM_DESC(latency, "pci latency timer");

static unsigned int video_nr[] = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "video device number");

static unsigned int card[] = {[0 ... (TW68_MAXBOARDS - 1)] = UNSET };
module_param_array(card, int, NULL, 0444);
MODULE_PARM_DESC(card, "card type");

static atomic_t tw68_instance = ATOMIC_INIT(0);

/* ------------------------------------------------------------------ */

/*
 * Please add any new PCI IDs to: http://pci-ids.ucw.cz.  This keeps
 * the PCI ID database up to date.  Note that the entries must be
 * added under vendor 0x1797 (Techwell Inc.) as subsystem IDs.
 */
static const struct pci_device_id tw68_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6800)},
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6801)},
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6804)},
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6816_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6816_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6816_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_TECHWELL, PCI_DEVICE_ID_6816_4)},
	{0,}
};

/* ------------------------------------------------------------------ */


/*
 * The device is given a "soft reset". According to the specifications,
 * after this "all register content remain unchanged", so we also write
 * to all specified registers manually as well (mostly to manufacturer's
 * specified reset values)
 */
static int tw68_hw_init1(struct tw68_dev *dev)
{
	/* Assure all interrupts are disabled */
	tw_writel(TW68_INTMASK, 0);		/* 020 */
	/* Clear any pending interrupts */
	tw_writel(TW68_INTSTAT, 0xffffffff);	/* 01C */
	/* Stop risc processor, set default buffer level */
	tw_writel(TW68_DMAC, 0x1600);

	tw_writeb(TW68_ACNTL, 0x80);	/* 218	soft reset */
	msleep(100);

	tw_writeb(TW68_INFORM, 0x40);	/* 208	mux0, 27mhz xtal */
	tw_writeb(TW68_OPFORM, 0x04);	/* 20C	analog line-lock */
	tw_writeb(TW68_HSYNC, 0);	/* 210	color-killer high sens */
	tw_writeb(TW68_ACNTL, 0x42);	/* 218	int vref #2, chroma adc off */

	tw_writeb(TW68_CROP_HI, 0x02);	/* 21C	Hactive m.s. bits */
	tw_writeb(TW68_VDELAY_LO, 0x12);/* 220	Mfg specified reset value */
	tw_writeb(TW68_VACTIVE_LO, 0xf0);
	tw_writeb(TW68_HDELAY_LO, 0x0f);
	tw_writeb(TW68_HACTIVE_LO, 0xd0);

	tw_writeb(TW68_CNTRL1, 0xcd);	/* 230	Wide Chroma BPF B/W
					 *	Secam reduction, Adap comb for
					 *	NTSC, Op Mode 1 */

	tw_writeb(TW68_VSCALE_LO, 0);	/* 234 */
	tw_writeb(TW68_SCALE_HI, 0x11);	/* 238 */
	tw_writeb(TW68_HSCALE_LO, 0);	/* 23c */
	tw_writeb(TW68_BRIGHT, 0);	/* 240 */
	tw_writeb(TW68_CONTRAST, 0x5c);	/* 244 */
	tw_writeb(TW68_SHARPNESS, 0x51);/* 248 */
	tw_writeb(TW68_SAT_U, 0x80);	/* 24C */
	tw_writeb(TW68_SAT_V, 0x80);	/* 250 */
	tw_writeb(TW68_HUE, 0x00);	/* 254 */

	/* TODO - Check that none of these are set by control defaults */
	tw_writeb(TW68_SHARP2, 0x53);	/* 258	Mfg specified reset val */
	tw_writeb(TW68_VSHARP, 0x80);	/* 25C	Sharpness Coring val 8 */
	tw_writeb(TW68_CORING, 0x44);	/* 260	CTI and Vert Peak coring */
	tw_writeb(TW68_CNTRL2, 0x00);	/* 268	No power saving enabled */
	tw_writeb(TW68_SDT, 0x07);	/* 270	Enable shadow reg, auto-det */
	tw_writeb(TW68_SDTR, 0x7f);	/* 274	All stds recog, don't start */
	tw_writeb(TW68_CLMPG, 0x50);	/* 280	Clamp end at 40 sys clocks */
	tw_writeb(TW68_IAGC, 0x22);	/* 284	Mfg specified reset val */
	tw_writeb(TW68_AGCGAIN, 0xf0);	/* 288	AGC gain when loop disabled */
	tw_writeb(TW68_PEAKWT, 0xd8);	/* 28C	White peak threshold */
	tw_writeb(TW68_CLMPL, 0x3c);	/* 290	Y channel clamp level */
/*	tw_writeb(TW68_SYNCT, 0x38);*/	/* 294	Sync amplitude */
	tw_writeb(TW68_SYNCT, 0x30);	/* 294	Sync amplitude */
	tw_writeb(TW68_MISSCNT, 0x44);	/* 298	Horiz sync, VCR detect sens */
	tw_writeb(TW68_PCLAMP, 0x28);	/* 29C	Clamp pos from PLL sync */
	/* Bit DETV of VCNTL1 helps sync multi cams/chip board */
	tw_writeb(TW68_VCNTL1, 0x04);	/* 2A0 */
	tw_writeb(TW68_VCNTL2, 0);	/* 2A4 */
	tw_writeb(TW68_CKILL, 0x68);	/* 2A8	Mfg specified reset val */
	tw_writeb(TW68_COMB, 0x44);	/* 2AC	Mfg specified reset val */
	tw_writeb(TW68_LDLY, 0x30);	/* 2B0	Max positive luma delay */
	tw_writeb(TW68_MISC1, 0x14);	/* 2B4	Mfg specified reset val */
	tw_writeb(TW68_LOOP, 0xa5);	/* 2B8	Mfg specified reset val */
	tw_writeb(TW68_MISC2, 0xe0);	/* 2BC	Enable colour killer */
	tw_writeb(TW68_MVSN, 0);	/* 2C0 */
	tw_writeb(TW68_CLMD, 0x05);	/* 2CC	slice level auto, clamp med. */
	tw_writeb(TW68_IDCNTL, 0);	/* 2D0	Writing zero to this register
					 *	selects NTSC ID detection,
					 *	but doesn't change the
					 *	sensitivity (which has a reset
					 *	value of 1E).  Since we are
					 *	not doing auto-detection, it
					 *	has no real effect */
	tw_writeb(TW68_CLCNTL1, 0);	/* 2D4 */
	tw_writel(TW68_VBIC, 0x03);	/* 010 */
	tw_writel(TW68_CAP_CTL, 0x03);	/* 040	Enable both even & odd flds */
	tw_writel(TW68_DMAC, 0x2000);	/* patch set had 0x2080 */
	tw_writel(TW68_TESTREG, 0);	/* 02C */

	/*
	 * Some common boards, especially inexpensive single-chip models,
	 * use the GPIO bits 0-3 to control an on-board video-output mux.
	 * For these boards, we need to set up the GPIO register into
	 * "normal" mode, set bits 0-3 as output, and then set those bits
	 * zero.
	 *
	 * Eventually, it would be nice if we could identify these boards
	 * uniquely, and only do this initialisation if the board has been
	 * identify.  For the moment, however, it shouldn't hurt anything
	 * to do these steps.
	 */
	tw_writel(TW68_GPIOC, 0);	/* Set the GPIO to "normal", no ints */
	tw_writel(TW68_GPOE, 0x0f);	/* Set bits 0-3 to "output" */
	tw_writel(TW68_GPDATA, 0);	/* Set all bits to low state */

	/* Initialize the device control structures */
	mutex_init(&dev->lock);
	spin_lock_init(&dev->slock);

	/* Initialize any subsystems */
	tw68_video_init1(dev);
	return 0;
}

static irqreturn_t tw68_irq(int irq, void *dev_id)
{
	struct tw68_dev *dev = dev_id;
	u32 status, orig;
	int loop;

	status = orig = tw_readl(TW68_INTSTAT) & dev->pci_irqmask;
	/* Check if anything to do */
	if (0 == status)
		return IRQ_NONE;	/* Nope - return */
	for (loop = 0; loop < 10; loop++) {
		if (status & dev->board_virqmask)	/* video interrupt */
			tw68_irq_video_done(dev, status);
		status = tw_readl(TW68_INTSTAT) & dev->pci_irqmask;
		if (0 == status)
			return IRQ_HANDLED;
	}
	dev_dbg(&dev->pci->dev, "%s: **** INTERRUPT NOT HANDLED - clearing mask (orig 0x%08x, cur 0x%08x)",
			dev->name, orig, tw_readl(TW68_INTSTAT));
	dev_dbg(&dev->pci->dev, "%s: pci_irqmask 0x%08x; board_virqmask 0x%08x ****\n",
			dev->name, dev->pci_irqmask, dev->board_virqmask);
	tw_clearl(TW68_INTMASK, dev->pci_irqmask);
	return IRQ_HANDLED;
}

static int tw68_initdev(struct pci_dev *pci_dev,
				     const struct pci_device_id *pci_id)
{
	struct tw68_dev *dev;
	int vidnr = -1;
	int err;

	dev = devm_kzalloc(&pci_dev->dev, sizeof(*dev), GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;

	dev->instance = v4l2_device_set_name(&dev->v4l2_dev, "tw68",
						&tw68_instance);

	err = v4l2_device_register(&pci_dev->dev, &dev->v4l2_dev);
	if (err)
		return err;

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail1;
	}

	dev->name = dev->v4l2_dev.name;

	if (UNSET != latency) {
		pr_info("%s: setting pci latency timer to %d\n",
		       dev->name, latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, latency);
	}

	/* print pci info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
	pr_info("%s: found at %s, rev: %d, irq: %d, latency: %d, mmio: 0x%llx\n",
		dev->name, pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
		dev->pci_lat, (u64)pci_resource_start(pci_dev, 0));
	pci_set_master(pci_dev);
	if (!pci_dma_supported(pci_dev, DMA_BIT_MASK(32))) {
		pr_info("%s: Oops: no 32bit PCI DMA ???\n", dev->name);
		err = -EIO;
		goto fail1;
	}

	switch (pci_id->device) {
	case PCI_DEVICE_ID_6800:	/* TW6800 */
		dev->vdecoder = TW6800;
		dev->board_virqmask = TW68_VID_INTS;
		break;
	case PCI_DEVICE_ID_6801:	/* Video decoder for TW6802 */
		dev->vdecoder = TW6801;
		dev->board_virqmask = TW68_VID_INTS | TW68_VID_INTSX;
		break;
	case PCI_DEVICE_ID_6804:	/* Video decoder for TW6804 */
		dev->vdecoder = TW6804;
		dev->board_virqmask = TW68_VID_INTS | TW68_VID_INTSX;
		break;
	default:
		dev->vdecoder = TWXXXX;	/* To be announced */
		dev->board_virqmask = TW68_VID_INTS | TW68_VID_INTSX;
		break;
	}

	/* get mmio */
	if (!request_mem_region(pci_resource_start(pci_dev, 0),
				pci_resource_len(pci_dev, 0),
				dev->name)) {
		err = -EBUSY;
		pr_err("%s: can't get MMIO memory @ 0x%llx\n",
			dev->name,
			(unsigned long long)pci_resource_start(pci_dev, 0));
		goto fail1;
	}
	dev->lmmio = ioremap(pci_resource_start(pci_dev, 0),
			     pci_resource_len(pci_dev, 0));
	dev->bmmio = (__u8 __iomem *)dev->lmmio;
	if (NULL == dev->lmmio) {
		err = -EIO;
		pr_err("%s: can't ioremap() MMIO memory\n",
		       dev->name);
		goto fail2;
	}
	/* initialize hardware #1 */
	/* Then do any initialisation wanted before interrupts are on */
	tw68_hw_init1(dev);

	dev->alloc_ctx = vb2_dma_sg_init_ctx(&pci_dev->dev);
	if (IS_ERR(dev->alloc_ctx)) {
		err = PTR_ERR(dev->alloc_ctx);
		goto fail3;
	}

	/* get irq */
	err = devm_request_irq(&pci_dev->dev, pci_dev->irq, tw68_irq,
			  IRQF_SHARED, dev->name, dev);
	if (err < 0) {
		pr_err("%s: can't get IRQ %d\n",
		       dev->name, pci_dev->irq);
		goto fail4;
	}

	/*
	 *  Now do remainder of initialisation, first for
	 *  things unique for this card, then for general board
	 */
	if (dev->instance < TW68_MAXBOARDS)
		vidnr = video_nr[dev->instance];
	/* initialise video function first */
	err = tw68_video_init2(dev, vidnr);
	if (err < 0) {
		pr_err("%s: can't register video device\n",
		       dev->name);
		goto fail5;
	}
	tw_setl(TW68_INTMASK, dev->pci_irqmask);

	pr_info("%s: registered device %s\n",
	       dev->name, video_device_node_name(&dev->vdev));

	return 0;

fail5:
	video_unregister_device(&dev->vdev);
fail4:
	vb2_dma_sg_cleanup_ctx(dev->alloc_ctx);
fail3:
	iounmap(dev->lmmio);
fail2:
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));
fail1:
	v4l2_device_unregister(&dev->v4l2_dev);
	return err;
}

static void tw68_finidev(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw68_dev *dev =
		container_of(v4l2_dev, struct tw68_dev, v4l2_dev);

	/* shutdown subsystems */
	tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
	tw_writel(TW68_INTMASK, 0);

	/* unregister */
	video_unregister_device(&dev->vdev);
	v4l2_ctrl_handler_free(&dev->hdl);
	vb2_dma_sg_cleanup_ctx(dev->alloc_ctx);

	/* release resources */
	iounmap(dev->lmmio);
	release_mem_region(pci_resource_start(pci_dev, 0),
			   pci_resource_len(pci_dev, 0));

	v4l2_device_unregister(&dev->v4l2_dev);
}

#ifdef CONFIG_PM

static int tw68_suspend(struct pci_dev *pci_dev , pm_message_t state)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw68_dev *dev = container_of(v4l2_dev,
				struct tw68_dev, v4l2_dev);

	tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
	dev->pci_irqmask &= ~TW68_VID_INTS;
	tw_writel(TW68_INTMASK, 0);

	synchronize_irq(pci_dev->irq);

	pci_save_state(pci_dev);
	pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state));
	vb2_discard_done(&dev->vidq);

	return 0;
}

static int tw68_resume(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct tw68_dev *dev = container_of(v4l2_dev,
					    struct tw68_dev, v4l2_dev);
	struct tw68_buf *buf;
	unsigned long flags;

	pci_set_power_state(pci_dev, PCI_D0);
	pci_restore_state(pci_dev);

	/* Do things that are done in tw68_initdev ,
		except of initializing memory structures.*/

	msleep(100);

	tw68_set_tvnorm_hw(dev);

	/*resume unfinished buffer(s)*/
	spin_lock_irqsave(&dev->slock, flags);
	buf = container_of(dev->active.next, struct tw68_buf, list);

	tw68_video_start_dma(dev, buf);

	spin_unlock_irqrestore(&dev->slock, flags);

	return 0;
}
#endif

/* ----------------------------------------------------------- */

static struct pci_driver tw68_pci_driver = {
	.name	  = "tw68",
	.id_table = tw68_pci_tbl,
	.probe	  = tw68_initdev,
	.remove	  = tw68_finidev,
#ifdef CONFIG_PM
	.suspend  = tw68_suspend,
	.resume   = tw68_resume
#endif
};

module_pci_driver(tw68_pci_driver);
