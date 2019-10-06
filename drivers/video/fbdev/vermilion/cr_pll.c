// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) Intel Corp. 2007.
 * All Rights Reserved.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 * This file is part of the Carillo Ranch video subsystem driver.
 *
 * Authors:
 *   Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *   Alan Hourihane <alanh-at-tungstengraphics-dot-com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include "vermilion.h"

/* The PLL Clock register sits on Host bridge */
#define CRVML_DEVICE_MCH   0x5001
#define CRVML_REG_MCHBAR   0x44
#define CRVML_REG_MCHEN    0x54
#define CRVML_MCHEN_BIT    (1 << 28)
#define CRVML_MCHMAP_SIZE  4096
#define CRVML_REG_CLOCK    0xc3c
#define CRVML_CLOCK_SHIFT  8
#define CRVML_CLOCK_MASK   0x00000f00

static struct pci_dev *mch_dev;
static u32 mch_bar;
static void __iomem *mch_regs_base;
static u32 saved_clock;

static const unsigned crvml_clocks[] = {
	6750,
	13500,
	27000,
	29700,
	37125,
	54000,
	59400,
	74250,
	120000
	    /*
	     * There are more clocks, but they are disabled on the CR board.
	     */
};

static const u32 crvml_clock_bits[] = {
	0x0a,
	0x09,
	0x08,
	0x07,
	0x06,
	0x05,
	0x04,
	0x03,
	0x0b
};

static const unsigned crvml_num_clocks = ARRAY_SIZE(crvml_clocks);

static int crvml_sys_restore(struct vml_sys *sys)
{
	void __iomem *clock_reg = mch_regs_base + CRVML_REG_CLOCK;

	iowrite32(saved_clock, clock_reg);
	ioread32(clock_reg);

	return 0;
}

static int crvml_sys_save(struct vml_sys *sys)
{
	void __iomem *clock_reg = mch_regs_base + CRVML_REG_CLOCK;

	saved_clock = ioread32(clock_reg);

	return 0;
}

static int crvml_nearest_index(const struct vml_sys *sys, int clock)
{
	int i;
	int cur_index = 0;
	int cur_diff;
	int diff;

	cur_diff = clock - crvml_clocks[0];
	cur_diff = (cur_diff < 0) ? -cur_diff : cur_diff;
	for (i = 1; i < crvml_num_clocks; ++i) {
		diff = clock - crvml_clocks[i];
		diff = (diff < 0) ? -diff : diff;
		if (diff < cur_diff) {
			cur_index = i;
			cur_diff = diff;
		}
	}
	return cur_index;
}

static int crvml_nearest_clock(const struct vml_sys *sys, int clock)
{
	return crvml_clocks[crvml_nearest_index(sys, clock)];
}

static int crvml_set_clock(struct vml_sys *sys, int clock)
{
	void __iomem *clock_reg = mch_regs_base + CRVML_REG_CLOCK;
	int index;
	u32 clock_val;

	index = crvml_nearest_index(sys, clock);

	if (crvml_clocks[index] != clock)
		return -EINVAL;

	clock_val = ioread32(clock_reg) & ~CRVML_CLOCK_MASK;
	clock_val = crvml_clock_bits[index] << CRVML_CLOCK_SHIFT;
	iowrite32(clock_val, clock_reg);
	ioread32(clock_reg);

	return 0;
}

static struct vml_sys cr_pll_ops = {
	.name = "Carillo Ranch",
	.save = crvml_sys_save,
	.restore = crvml_sys_restore,
	.set_clock = crvml_set_clock,
	.nearest_clock = crvml_nearest_clock,
};

static int __init cr_pll_init(void)
{
	int err;
	u32 dev_en;

	mch_dev = pci_get_device(PCI_VENDOR_ID_INTEL,
					CRVML_DEVICE_MCH, NULL);
	if (!mch_dev) {
		printk(KERN_ERR
		       "Could not find Carillo Ranch MCH device.\n");
		return -ENODEV;
	}

	pci_read_config_dword(mch_dev, CRVML_REG_MCHEN, &dev_en);
	if (!(dev_en & CRVML_MCHEN_BIT)) {
		printk(KERN_ERR
		       "Carillo Ranch MCH device was not enabled.\n");
		pci_dev_put(mch_dev);
		return -ENODEV;
	}

	pci_read_config_dword(mch_dev, CRVML_REG_MCHBAR,
			      &mch_bar);
	mch_regs_base =
	    ioremap_nocache(mch_bar, CRVML_MCHMAP_SIZE);
	if (!mch_regs_base) {
		printk(KERN_ERR
		       "Carillo Ranch MCH device was not enabled.\n");
		pci_dev_put(mch_dev);
		return -ENODEV;
	}

	err = vmlfb_register_subsys(&cr_pll_ops);
	if (err) {
		printk(KERN_ERR
		       "Carillo Ranch failed to initialize vml_sys.\n");
		iounmap(mch_regs_base);
		pci_dev_put(mch_dev);
		return err;
	}

	return 0;
}

static void __exit cr_pll_exit(void)
{
	vmlfb_unregister_subsys(&cr_pll_ops);

	iounmap(mch_regs_base);
	pci_dev_put(mch_dev);
}

module_init(cr_pll_init);
module_exit(cr_pll_exit);

MODULE_AUTHOR("Tungsten Graphics Inc.");
MODULE_DESCRIPTION("Carillo Ranch PLL Driver");
MODULE_LICENSE("GPL");
