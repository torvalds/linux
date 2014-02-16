/*
 * Baytrail IOSF-SB MailBox Interface Driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 *
 * The IOSF-SB is a fabric bus available on Atom based SOC's that uses a
 * mailbox interface (MBI) to communicate with mutiple devices. This
 * driver implements BayTrail-specific access to this interface.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include "intel_baytrail.h"

static DEFINE_SPINLOCK(iosf_mbi_lock);

static inline u32 iosf_mbi_form_mcr(u8 op, u8 port, u8 offset)
{
	return (op << 24) | (port << 16) | (offset << 8) | BT_MBI_ENABLE;
}

static struct pci_dev *mbi_pdev;	/* one mbi device */

/* Hold lock before calling */
static int iosf_mbi_pci_read_mdr(u32 mcrx, u32 mcr, u32 *mdr)
{
	int result;

	if (!mbi_pdev)
		return -ENODEV;

	if (mcrx) {
		result = pci_write_config_dword(mbi_pdev,
						BT_MBI_MCRX_OFFSET, mcrx);
		if (result < 0)
			goto iosf_mbi_read_err;
	}

	result = pci_write_config_dword(mbi_pdev,
					BT_MBI_MCR_OFFSET, mcr);
	if (result < 0)
		goto iosf_mbi_read_err;

	result = pci_read_config_dword(mbi_pdev,
				       BT_MBI_MDR_OFFSET, mdr);
	if (result < 0)
		goto iosf_mbi_read_err;

	return 0;

iosf_mbi_read_err:
	dev_err(&mbi_pdev->dev, "error: PCI config operation returned %d\n",
		result);
	return result;
}

/* Hold lock before calling */
static int iosf_mbi_pci_write_mdr(u32 mcrx, u32 mcr, u32 mdr)
{
	int result;

	if (!mbi_pdev)
		return -ENODEV;

	result = pci_write_config_dword(mbi_pdev,
					BT_MBI_MDR_OFFSET, mdr);
	if (result < 0)
		goto iosf_mbi_write_err;

	if (mcrx) {
		result = pci_write_config_dword(mbi_pdev,
			 BT_MBI_MCRX_OFFSET, mcrx);
		if (result < 0)
			goto iosf_mbi_write_err;
	}

	result = pci_write_config_dword(mbi_pdev,
					BT_MBI_MCR_OFFSET, mcr);
	if (result < 0)
		goto iosf_mbi_write_err;

	return 0;

iosf_mbi_write_err:
	dev_err(&mbi_pdev->dev, "error: PCI config operation returned %d\n",
		result);
	return result;
}

int bt_mbi_read(u8 port, u8 opcode, u32 offset, u32 *mdr)
{
	u32 mcr, mcrx;
	unsigned long flags;
	int ret;

	/*Access to the GFX unit is handled by GPU code */
	BUG_ON(port == BT_MBI_UNIT_GFX);

	mcr = iosf_mbi_form_mcr(opcode, port, offset & BT_MBI_MASK_LO);
	mcrx = offset & BT_MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);
	ret = iosf_mbi_pci_read_mdr(mcrx, mcr, mdr);
	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(bt_mbi_read);

int bt_mbi_write(u8 port, u8 opcode, u32 offset, u32 mdr)
{
	u32 mcr, mcrx;
	unsigned long flags;
	int ret;

	/*Access to the GFX unit is handled by GPU code */
	BUG_ON(port == BT_MBI_UNIT_GFX);

	mcr = iosf_mbi_form_mcr(opcode, port, offset & BT_MBI_MASK_LO);
	mcrx = offset & BT_MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);
	ret = iosf_mbi_pci_write_mdr(mcrx, mcr, mdr);
	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(bt_mbi_write);

int bt_mbi_modify(u8 port, u8 opcode, u32 offset, u32 mdr, u32 mask)
{
	u32 mcr, mcrx;
	u32 value;
	unsigned long flags;
	int ret;

	/*Access to the GFX unit is handled by GPU code */
	BUG_ON(port == BT_MBI_UNIT_GFX);

	mcr = iosf_mbi_form_mcr(opcode, port, offset & BT_MBI_MASK_LO);
	mcrx = offset & BT_MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);

	/* Read current mdr value */
	ret = iosf_mbi_pci_read_mdr(mcrx, mcr & BT_MBI_RD_MASK, &value);
	if (ret < 0) {
		spin_unlock_irqrestore(&iosf_mbi_lock, flags);
		return ret;
	}

	/* Apply mask */
	value &= ~mask;
	mdr &= mask;
	value |= mdr;

	/* Write back */
	ret = iosf_mbi_pci_write_mdr(mcrx, mcr | BT_MBI_WR_MASK, value);

	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(bt_mbi_modify);

static int iosf_mbi_probe(struct pci_dev *pdev,
			  const struct pci_device_id *unused)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error: could not enable device\n");
		return ret;
	}

	mbi_pdev = pci_dev_get(pdev);
	return 0;
}

static DEFINE_PCI_DEVICE_TABLE(iosf_mbi_pci_ids) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0F00) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, iosf_mbi_pci_ids);

static struct pci_driver iosf_mbi_pci_driver = {
	.name		= "iosf_mbi_pci",
	.probe		= iosf_mbi_probe,
	.id_table	= iosf_mbi_pci_ids,
};

static int __init bt_mbi_init(void)
{
	return pci_register_driver(&iosf_mbi_pci_driver);
}

static void __exit bt_mbi_exit(void)
{
	pci_unregister_driver(&iosf_mbi_pci_driver);
	if (mbi_pdev) {
		pci_dev_put(mbi_pdev);
		mbi_pdev = NULL;
	}
}

module_init(bt_mbi_init);
module_exit(bt_mbi_exit);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("BayTrail Mailbox Interface accessor");
MODULE_LICENSE("GPL v2");
