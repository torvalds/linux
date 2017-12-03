/*
 * IOSF-SB MailBox Interface Driver
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
 * driver implements access to this interface for those platforms that can
 * enumerate the device using PCI.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/capability.h>

#include <asm/iosf_mbi.h>

#define PCI_DEVICE_ID_BAYTRAIL		0x0F00
#define PCI_DEVICE_ID_BRASWELL		0x2280
#define PCI_DEVICE_ID_QUARK_X1000	0x0958
#define PCI_DEVICE_ID_TANGIER		0x1170

static struct pci_dev *mbi_pdev;
static DEFINE_SPINLOCK(iosf_mbi_lock);
static DEFINE_MUTEX(iosf_mbi_punit_mutex);
static BLOCKING_NOTIFIER_HEAD(iosf_mbi_pmic_bus_access_notifier);

static inline u32 iosf_mbi_form_mcr(u8 op, u8 port, u8 offset)
{
	return (op << 24) | (port << 16) | (offset << 8) | MBI_ENABLE;
}

static int iosf_mbi_pci_read_mdr(u32 mcrx, u32 mcr, u32 *mdr)
{
	int result;

	if (!mbi_pdev)
		return -ENODEV;

	if (mcrx) {
		result = pci_write_config_dword(mbi_pdev, MBI_MCRX_OFFSET,
						mcrx);
		if (result < 0)
			goto fail_read;
	}

	result = pci_write_config_dword(mbi_pdev, MBI_MCR_OFFSET, mcr);
	if (result < 0)
		goto fail_read;

	result = pci_read_config_dword(mbi_pdev, MBI_MDR_OFFSET, mdr);
	if (result < 0)
		goto fail_read;

	return 0;

fail_read:
	dev_err(&mbi_pdev->dev, "PCI config access failed with %d\n", result);
	return result;
}

static int iosf_mbi_pci_write_mdr(u32 mcrx, u32 mcr, u32 mdr)
{
	int result;

	if (!mbi_pdev)
		return -ENODEV;

	result = pci_write_config_dword(mbi_pdev, MBI_MDR_OFFSET, mdr);
	if (result < 0)
		goto fail_write;

	if (mcrx) {
		result = pci_write_config_dword(mbi_pdev, MBI_MCRX_OFFSET,
						mcrx);
		if (result < 0)
			goto fail_write;
	}

	result = pci_write_config_dword(mbi_pdev, MBI_MCR_OFFSET, mcr);
	if (result < 0)
		goto fail_write;

	return 0;

fail_write:
	dev_err(&mbi_pdev->dev, "PCI config access failed with %d\n", result);
	return result;
}

int iosf_mbi_read(u8 port, u8 opcode, u32 offset, u32 *mdr)
{
	u32 mcr, mcrx;
	unsigned long flags;
	int ret;

	/* Access to the GFX unit is handled by GPU code */
	if (port == BT_MBI_UNIT_GFX) {
		WARN_ON(1);
		return -EPERM;
	}

	mcr = iosf_mbi_form_mcr(opcode, port, offset & MBI_MASK_LO);
	mcrx = offset & MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);
	ret = iosf_mbi_pci_read_mdr(mcrx, mcr, mdr);
	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_read);

int iosf_mbi_write(u8 port, u8 opcode, u32 offset, u32 mdr)
{
	u32 mcr, mcrx;
	unsigned long flags;
	int ret;

	/* Access to the GFX unit is handled by GPU code */
	if (port == BT_MBI_UNIT_GFX) {
		WARN_ON(1);
		return -EPERM;
	}

	mcr = iosf_mbi_form_mcr(opcode, port, offset & MBI_MASK_LO);
	mcrx = offset & MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);
	ret = iosf_mbi_pci_write_mdr(mcrx, mcr, mdr);
	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_write);

int iosf_mbi_modify(u8 port, u8 opcode, u32 offset, u32 mdr, u32 mask)
{
	u32 mcr, mcrx;
	u32 value;
	unsigned long flags;
	int ret;

	/* Access to the GFX unit is handled by GPU code */
	if (port == BT_MBI_UNIT_GFX) {
		WARN_ON(1);
		return -EPERM;
	}

	mcr = iosf_mbi_form_mcr(opcode, port, offset & MBI_MASK_LO);
	mcrx = offset & MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);

	/* Read current mdr value */
	ret = iosf_mbi_pci_read_mdr(mcrx, mcr & MBI_RD_MASK, &value);
	if (ret < 0) {
		spin_unlock_irqrestore(&iosf_mbi_lock, flags);
		return ret;
	}

	/* Apply mask */
	value &= ~mask;
	mdr &= mask;
	value |= mdr;

	/* Write back */
	ret = iosf_mbi_pci_write_mdr(mcrx, mcr | MBI_WR_MASK, value);

	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_modify);

bool iosf_mbi_available(void)
{
	/* Mbi isn't hot-pluggable. No remove routine is provided */
	return mbi_pdev;
}
EXPORT_SYMBOL(iosf_mbi_available);

void iosf_mbi_punit_acquire(void)
{
	mutex_lock(&iosf_mbi_punit_mutex);
}
EXPORT_SYMBOL(iosf_mbi_punit_acquire);

void iosf_mbi_punit_release(void)
{
	mutex_unlock(&iosf_mbi_punit_mutex);
}
EXPORT_SYMBOL(iosf_mbi_punit_release);

int iosf_mbi_register_pmic_bus_access_notifier(struct notifier_block *nb)
{
	int ret;

	/* Wait for the bus to go inactive before registering */
	mutex_lock(&iosf_mbi_punit_mutex);
	ret = blocking_notifier_chain_register(
				&iosf_mbi_pmic_bus_access_notifier, nb);
	mutex_unlock(&iosf_mbi_punit_mutex);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_register_pmic_bus_access_notifier);

int iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(
	struct notifier_block *nb)
{
	iosf_mbi_assert_punit_acquired();

	return blocking_notifier_chain_unregister(
				&iosf_mbi_pmic_bus_access_notifier, nb);
}
EXPORT_SYMBOL(iosf_mbi_unregister_pmic_bus_access_notifier_unlocked);

int iosf_mbi_unregister_pmic_bus_access_notifier(struct notifier_block *nb)
{
	int ret;

	/* Wait for the bus to go inactive before unregistering */
	mutex_lock(&iosf_mbi_punit_mutex);
	ret = iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(nb);
	mutex_unlock(&iosf_mbi_punit_mutex);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_unregister_pmic_bus_access_notifier);

int iosf_mbi_call_pmic_bus_access_notifier_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(
				&iosf_mbi_pmic_bus_access_notifier, val, v);
}
EXPORT_SYMBOL(iosf_mbi_call_pmic_bus_access_notifier_chain);

void iosf_mbi_assert_punit_acquired(void)
{
	WARN_ON(!mutex_is_locked(&iosf_mbi_punit_mutex));
}
EXPORT_SYMBOL(iosf_mbi_assert_punit_acquired);

#ifdef CONFIG_IOSF_MBI_DEBUG
static u32	dbg_mdr;
static u32	dbg_mcr;
static u32	dbg_mcrx;

static int mcr_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}

static int mcr_set(void *data, u64 val)
{
	u8 command = ((u32)val & 0xFF000000) >> 24,
	   port	   = ((u32)val & 0x00FF0000) >> 16,
	   offset  = ((u32)val & 0x0000FF00) >> 8;
	int err;

	*(u32 *)data = val;

	if (!capable(CAP_SYS_RAWIO))
		return -EACCES;

	if (command & 1u)
		err = iosf_mbi_write(port,
			       command,
			       dbg_mcrx | offset,
			       dbg_mdr);
	else
		err = iosf_mbi_read(port,
			      command,
			      dbg_mcrx | offset,
			      &dbg_mdr);

	return err;
}
DEFINE_SIMPLE_ATTRIBUTE(iosf_mcr_fops, mcr_get, mcr_set , "%llx\n");

static struct dentry *iosf_dbg;

static void iosf_sideband_debug_init(void)
{
	struct dentry *d;

	iosf_dbg = debugfs_create_dir("iosf_sb", NULL);
	if (IS_ERR_OR_NULL(iosf_dbg))
		return;

	/* mdr */
	d = debugfs_create_x32("mdr", 0660, iosf_dbg, &dbg_mdr);
	if (!d)
		goto cleanup;

	/* mcrx */
	d = debugfs_create_x32("mcrx", 0660, iosf_dbg, &dbg_mcrx);
	if (!d)
		goto cleanup;

	/* mcr - initiates mailbox tranaction */
	d = debugfs_create_file("mcr", 0660, iosf_dbg, &dbg_mcr, &iosf_mcr_fops);
	if (!d)
		goto cleanup;

	return;

cleanup:
	debugfs_remove_recursive(d);
}

static void iosf_debugfs_init(void)
{
	iosf_sideband_debug_init();
}

static void iosf_debugfs_remove(void)
{
	debugfs_remove_recursive(iosf_dbg);
}
#else
static inline void iosf_debugfs_init(void) { }
static inline void iosf_debugfs_remove(void) { }
#endif /* CONFIG_IOSF_MBI_DEBUG */

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

static const struct pci_device_id iosf_mbi_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_BAYTRAIL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_BRASWELL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_QUARK_X1000) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_TANGIER) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, iosf_mbi_pci_ids);

static struct pci_driver iosf_mbi_pci_driver = {
	.name		= "iosf_mbi_pci",
	.probe		= iosf_mbi_probe,
	.id_table	= iosf_mbi_pci_ids,
};

static int __init iosf_mbi_init(void)
{
	iosf_debugfs_init();

	return pci_register_driver(&iosf_mbi_pci_driver);
}

static void __exit iosf_mbi_exit(void)
{
	iosf_debugfs_remove();

	pci_unregister_driver(&iosf_mbi_pci_driver);
	pci_dev_put(mbi_pdev);
	mbi_pdev = NULL;
}

module_init(iosf_mbi_init);
module_exit(iosf_mbi_exit);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("IOSF Mailbox Interface accessor");
MODULE_LICENSE("GPL v2");
