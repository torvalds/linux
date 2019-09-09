// SPDX-License-Identifier: GPL-2.0-only
/*
 * amd8111_edac.c, AMD8111 Hyper Transport chip EDAC kernel module
 *
 * Copyright (c) 2008 Wind River Systems, Inc.
 *
 * Authors:	Cao Qingtao <qingtao.cao@windriver.com>
 * 		Benjamin Walsh <benjamin.walsh@windriver.com>
 * 		Hu Yongqi <yongqi.hu@windriver.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/edac.h>
#include <linux/pci_ids.h>
#include <asm/io.h>

#include "edac_module.h"
#include "amd8111_edac.h"

#define AMD8111_EDAC_REVISION	" Ver: 1.0.0"
#define AMD8111_EDAC_MOD_STR	"amd8111_edac"

#define PCI_DEVICE_ID_AMD_8111_PCI	0x7460

enum amd8111_edac_devs {
	LPC_BRIDGE = 0,
};

enum amd8111_edac_pcis {
	PCI_BRIDGE = 0,
};

/* Wrapper functions for accessing PCI configuration space */
static int edac_pci_read_dword(struct pci_dev *dev, int reg, u32 *val32)
{
	int ret;

	ret = pci_read_config_dword(dev, reg, val32);
	if (ret != 0)
		printk(KERN_ERR AMD8111_EDAC_MOD_STR
			" PCI Access Read Error at 0x%x\n", reg);

	return ret;
}

static void edac_pci_read_byte(struct pci_dev *dev, int reg, u8 *val8)
{
	int ret;

	ret = pci_read_config_byte(dev, reg, val8);
	if (ret != 0)
		printk(KERN_ERR AMD8111_EDAC_MOD_STR
			" PCI Access Read Error at 0x%x\n", reg);
}

static void edac_pci_write_dword(struct pci_dev *dev, int reg, u32 val32)
{
	int ret;

	ret = pci_write_config_dword(dev, reg, val32);
	if (ret != 0)
		printk(KERN_ERR AMD8111_EDAC_MOD_STR
			" PCI Access Write Error at 0x%x\n", reg);
}

static void edac_pci_write_byte(struct pci_dev *dev, int reg, u8 val8)
{
	int ret;

	ret = pci_write_config_byte(dev, reg, val8);
	if (ret != 0)
		printk(KERN_ERR AMD8111_EDAC_MOD_STR
			" PCI Access Write Error at 0x%x\n", reg);
}

/*
 * device-specific methods for amd8111 PCI Bridge Controller
 *
 * Error Reporting and Handling for amd8111 chipset could be found
 * in its datasheet 3.1.2 section, P37
 */
static void amd8111_pci_bridge_init(struct amd8111_pci_info *pci_info)
{
	u32 val32;
	struct pci_dev *dev = pci_info->dev;

	/* First clear error detection flags on the host interface */

	/* Clear SSE/SMA/STA flags in the global status register*/
	edac_pci_read_dword(dev, REG_PCI_STSCMD, &val32);
	if (val32 & PCI_STSCMD_CLEAR_MASK)
		edac_pci_write_dword(dev, REG_PCI_STSCMD, val32);

	/* Clear CRC and Link Fail flags in HT Link Control reg */
	edac_pci_read_dword(dev, REG_HT_LINK, &val32);
	if (val32 & HT_LINK_CLEAR_MASK)
		edac_pci_write_dword(dev, REG_HT_LINK, val32);

	/* Second clear all fault on the secondary interface */

	/* Clear error flags in the memory-base limit reg. */
	edac_pci_read_dword(dev, REG_MEM_LIM, &val32);
	if (val32 & MEM_LIMIT_CLEAR_MASK)
		edac_pci_write_dword(dev, REG_MEM_LIM, val32);

	/* Clear Discard Timer Expired flag in Interrupt/Bridge Control reg */
	edac_pci_read_dword(dev, REG_PCI_INTBRG_CTRL, &val32);
	if (val32 & PCI_INTBRG_CTRL_CLEAR_MASK)
		edac_pci_write_dword(dev, REG_PCI_INTBRG_CTRL, val32);

	/* Last enable error detections */
	if (edac_op_state == EDAC_OPSTATE_POLL) {
		/* Enable System Error reporting in global status register */
		edac_pci_read_dword(dev, REG_PCI_STSCMD, &val32);
		val32 |= PCI_STSCMD_SERREN;
		edac_pci_write_dword(dev, REG_PCI_STSCMD, val32);

		/* Enable CRC Sync flood packets to HyperTransport Link */
		edac_pci_read_dword(dev, REG_HT_LINK, &val32);
		val32 |= HT_LINK_CRCFEN;
		edac_pci_write_dword(dev, REG_HT_LINK, val32);

		/* Enable SSE reporting etc in Interrupt control reg */
		edac_pci_read_dword(dev, REG_PCI_INTBRG_CTRL, &val32);
		val32 |= PCI_INTBRG_CTRL_POLL_MASK;
		edac_pci_write_dword(dev, REG_PCI_INTBRG_CTRL, val32);
	}
}

static void amd8111_pci_bridge_exit(struct amd8111_pci_info *pci_info)
{
	u32 val32;
	struct pci_dev *dev = pci_info->dev;

	if (edac_op_state == EDAC_OPSTATE_POLL) {
		/* Disable System Error reporting */
		edac_pci_read_dword(dev, REG_PCI_STSCMD, &val32);
		val32 &= ~PCI_STSCMD_SERREN;
		edac_pci_write_dword(dev, REG_PCI_STSCMD, val32);

		/* Disable CRC flood packets */
		edac_pci_read_dword(dev, REG_HT_LINK, &val32);
		val32 &= ~HT_LINK_CRCFEN;
		edac_pci_write_dword(dev, REG_HT_LINK, val32);

		/* Disable DTSERREN/MARSP/SERREN in Interrupt Control reg */
		edac_pci_read_dword(dev, REG_PCI_INTBRG_CTRL, &val32);
		val32 &= ~PCI_INTBRG_CTRL_POLL_MASK;
		edac_pci_write_dword(dev, REG_PCI_INTBRG_CTRL, val32);
	}
}

static void amd8111_pci_bridge_check(struct edac_pci_ctl_info *edac_dev)
{
	struct amd8111_pci_info *pci_info = edac_dev->pvt_info;
	struct pci_dev *dev = pci_info->dev;
	u32 val32;

	/* Check out PCI Bridge Status and Command Register */
	edac_pci_read_dword(dev, REG_PCI_STSCMD, &val32);
	if (val32 & PCI_STSCMD_CLEAR_MASK) {
		printk(KERN_INFO "Error(s) in PCI bridge status and command"
			"register on device %s\n", pci_info->ctl_name);
		printk(KERN_INFO "SSE: %d, RMA: %d, RTA: %d\n",
			(val32 & PCI_STSCMD_SSE) != 0,
			(val32 & PCI_STSCMD_RMA) != 0,
			(val32 & PCI_STSCMD_RTA) != 0);

		val32 |= PCI_STSCMD_CLEAR_MASK;
		edac_pci_write_dword(dev, REG_PCI_STSCMD, val32);

		edac_pci_handle_npe(edac_dev, edac_dev->ctl_name);
	}

	/* Check out HyperTransport Link Control Register */
	edac_pci_read_dword(dev, REG_HT_LINK, &val32);
	if (val32 & HT_LINK_LKFAIL) {
		printk(KERN_INFO "Error(s) in hypertransport link control"
			"register on device %s\n", pci_info->ctl_name);
		printk(KERN_INFO "LKFAIL: %d\n",
			(val32 & HT_LINK_LKFAIL) != 0);

		val32 |= HT_LINK_LKFAIL;
		edac_pci_write_dword(dev, REG_HT_LINK, val32);

		edac_pci_handle_npe(edac_dev, edac_dev->ctl_name);
	}

	/* Check out PCI Interrupt and Bridge Control Register */
	edac_pci_read_dword(dev, REG_PCI_INTBRG_CTRL, &val32);
	if (val32 & PCI_INTBRG_CTRL_DTSTAT) {
		printk(KERN_INFO "Error(s) in PCI interrupt and bridge control"
			"register on device %s\n", pci_info->ctl_name);
		printk(KERN_INFO "DTSTAT: %d\n",
			(val32 & PCI_INTBRG_CTRL_DTSTAT) != 0);

		val32 |= PCI_INTBRG_CTRL_DTSTAT;
		edac_pci_write_dword(dev, REG_PCI_INTBRG_CTRL, val32);

		edac_pci_handle_npe(edac_dev, edac_dev->ctl_name);
	}

	/* Check out PCI Bridge Memory Base-Limit Register */
	edac_pci_read_dword(dev, REG_MEM_LIM, &val32);
	if (val32 & MEM_LIMIT_CLEAR_MASK) {
		printk(KERN_INFO
			"Error(s) in mem limit register on %s device\n",
			pci_info->ctl_name);
		printk(KERN_INFO "DPE: %d, RSE: %d, RMA: %d\n"
			"RTA: %d, STA: %d, MDPE: %d\n",
			(val32 & MEM_LIMIT_DPE)  != 0,
			(val32 & MEM_LIMIT_RSE)  != 0,
			(val32 & MEM_LIMIT_RMA)  != 0,
			(val32 & MEM_LIMIT_RTA)  != 0,
			(val32 & MEM_LIMIT_STA)  != 0,
			(val32 & MEM_LIMIT_MDPE) != 0);

		val32 |= MEM_LIMIT_CLEAR_MASK;
		edac_pci_write_dword(dev, REG_MEM_LIM, val32);

		edac_pci_handle_npe(edac_dev, edac_dev->ctl_name);
	}
}

static struct resource *legacy_io_res;
static int at_compat_reg_broken;
#define LEGACY_NR_PORTS	1

/* device-specific methods for amd8111 LPC Bridge device */
static void amd8111_lpc_bridge_init(struct amd8111_dev_info *dev_info)
{
	u8 val8;
	struct pci_dev *dev = dev_info->dev;

	/* First clear REG_AT_COMPAT[SERR, IOCHK] if necessary */
	legacy_io_res = request_region(REG_AT_COMPAT, LEGACY_NR_PORTS,
					AMD8111_EDAC_MOD_STR);
	if (!legacy_io_res)
		printk(KERN_INFO "%s: failed to request legacy I/O region "
			"start %d, len %d\n", __func__,
			REG_AT_COMPAT, LEGACY_NR_PORTS);
	else {
		val8 = __do_inb(REG_AT_COMPAT);
		if (val8 == 0xff) { /* buggy port */
			printk(KERN_INFO "%s: port %d is buggy, not supported"
				" by hardware?\n", __func__, REG_AT_COMPAT);
			at_compat_reg_broken = 1;
			release_region(REG_AT_COMPAT, LEGACY_NR_PORTS);
			legacy_io_res = NULL;
		} else {
			u8 out8 = 0;
			if (val8 & AT_COMPAT_SERR)
				out8 = AT_COMPAT_CLRSERR;
			if (val8 & AT_COMPAT_IOCHK)
				out8 |= AT_COMPAT_CLRIOCHK;
			if (out8 > 0)
				__do_outb(out8, REG_AT_COMPAT);
		}
	}

	/* Second clear error flags on LPC bridge */
	edac_pci_read_byte(dev, REG_IO_CTRL_1, &val8);
	if (val8 & IO_CTRL_1_CLEAR_MASK)
		edac_pci_write_byte(dev, REG_IO_CTRL_1, val8);
}

static void amd8111_lpc_bridge_exit(struct amd8111_dev_info *dev_info)
{
	if (legacy_io_res)
		release_region(REG_AT_COMPAT, LEGACY_NR_PORTS);
}

static void amd8111_lpc_bridge_check(struct edac_device_ctl_info *edac_dev)
{
	struct amd8111_dev_info *dev_info = edac_dev->pvt_info;
	struct pci_dev *dev = dev_info->dev;
	u8 val8;

	edac_pci_read_byte(dev, REG_IO_CTRL_1, &val8);
	if (val8 & IO_CTRL_1_CLEAR_MASK) {
		printk(KERN_INFO
			"Error(s) in IO control register on %s device\n",
			dev_info->ctl_name);
		printk(KERN_INFO "LPC ERR: %d, PW2LPC: %d\n",
			(val8 & IO_CTRL_1_LPC_ERR) != 0,
			(val8 & IO_CTRL_1_PW2LPC) != 0);

		val8 |= IO_CTRL_1_CLEAR_MASK;
		edac_pci_write_byte(dev, REG_IO_CTRL_1, val8);

		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
	}

	if (at_compat_reg_broken == 0) {
		u8 out8 = 0;
		val8 = __do_inb(REG_AT_COMPAT);
		if (val8 & AT_COMPAT_SERR)
			out8 = AT_COMPAT_CLRSERR;
		if (val8 & AT_COMPAT_IOCHK)
			out8 |= AT_COMPAT_CLRIOCHK;
		if (out8 > 0) {
			__do_outb(out8, REG_AT_COMPAT);
			edac_device_handle_ue(edac_dev, 0, 0,
						edac_dev->ctl_name);
		}
	}
}

/* General devices represented by edac_device_ctl_info */
static struct amd8111_dev_info amd8111_devices[] = {
	[LPC_BRIDGE] = {
		.err_dev = PCI_DEVICE_ID_AMD_8111_LPC,
		.ctl_name = "lpc",
		.init = amd8111_lpc_bridge_init,
		.exit = amd8111_lpc_bridge_exit,
		.check = amd8111_lpc_bridge_check,
	},
	{0},
};

/* PCI controllers represented by edac_pci_ctl_info */
static struct amd8111_pci_info amd8111_pcis[] = {
	[PCI_BRIDGE] = {
		.err_dev = PCI_DEVICE_ID_AMD_8111_PCI,
		.ctl_name = "AMD8111_PCI_Controller",
		.init = amd8111_pci_bridge_init,
		.exit = amd8111_pci_bridge_exit,
		.check = amd8111_pci_bridge_check,
	},
	{0},
};

static int amd8111_dev_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	struct amd8111_dev_info *dev_info = &amd8111_devices[id->driver_data];
	int ret = -ENODEV;

	dev_info->dev = pci_get_device(PCI_VENDOR_ID_AMD,
					dev_info->err_dev, NULL);

	if (!dev_info->dev) {
		printk(KERN_ERR "EDAC device not found:"
			"vendor %x, device %x, name %s\n",
			PCI_VENDOR_ID_AMD, dev_info->err_dev,
			dev_info->ctl_name);
		goto err;
	}

	if (pci_enable_device(dev_info->dev)) {
		printk(KERN_ERR "failed to enable:"
			"vendor %x, device %x, name %s\n",
			PCI_VENDOR_ID_AMD, dev_info->err_dev,
			dev_info->ctl_name);
		goto err_dev_put;
	}

	/*
	 * we do not allocate extra private structure for
	 * edac_device_ctl_info, but make use of existing
	 * one instead.
	*/
	dev_info->edac_idx = edac_device_alloc_index();
	dev_info->edac_dev =
		edac_device_alloc_ctl_info(0, dev_info->ctl_name, 1,
					   NULL, 0, 0,
					   NULL, 0, dev_info->edac_idx);
	if (!dev_info->edac_dev) {
		ret = -ENOMEM;
		goto err_dev_put;
	}

	dev_info->edac_dev->pvt_info = dev_info;
	dev_info->edac_dev->dev = &dev_info->dev->dev;
	dev_info->edac_dev->mod_name = AMD8111_EDAC_MOD_STR;
	dev_info->edac_dev->ctl_name = dev_info->ctl_name;
	dev_info->edac_dev->dev_name = dev_name(&dev_info->dev->dev);

	if (edac_op_state == EDAC_OPSTATE_POLL)
		dev_info->edac_dev->edac_check = dev_info->check;

	if (dev_info->init)
		dev_info->init(dev_info);

	if (edac_device_add_device(dev_info->edac_dev) > 0) {
		printk(KERN_ERR "failed to add edac_dev for %s\n",
			dev_info->ctl_name);
		goto err_edac_free_ctl;
	}

	printk(KERN_INFO "added one edac_dev on AMD8111 "
		"vendor %x, device %x, name %s\n",
		PCI_VENDOR_ID_AMD, dev_info->err_dev,
		dev_info->ctl_name);

	return 0;

err_edac_free_ctl:
	edac_device_free_ctl_info(dev_info->edac_dev);
err_dev_put:
	pci_dev_put(dev_info->dev);
err:
	return ret;
}

static void amd8111_dev_remove(struct pci_dev *dev)
{
	struct amd8111_dev_info *dev_info;

	for (dev_info = amd8111_devices; dev_info->err_dev; dev_info++)
		if (dev_info->dev->device == dev->device)
			break;

	if (!dev_info->err_dev)	/* should never happen */
		return;

	if (dev_info->edac_dev) {
		edac_device_del_device(dev_info->edac_dev->dev);
		edac_device_free_ctl_info(dev_info->edac_dev);
	}

	if (dev_info->exit)
		dev_info->exit(dev_info);

	pci_dev_put(dev_info->dev);
}

static int amd8111_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	struct amd8111_pci_info *pci_info = &amd8111_pcis[id->driver_data];
	int ret = -ENODEV;

	pci_info->dev = pci_get_device(PCI_VENDOR_ID_AMD,
					pci_info->err_dev, NULL);

	if (!pci_info->dev) {
		printk(KERN_ERR "EDAC device not found:"
			"vendor %x, device %x, name %s\n",
			PCI_VENDOR_ID_AMD, pci_info->err_dev,
			pci_info->ctl_name);
		goto err;
	}

	if (pci_enable_device(pci_info->dev)) {
		printk(KERN_ERR "failed to enable:"
			"vendor %x, device %x, name %s\n",
			PCI_VENDOR_ID_AMD, pci_info->err_dev,
			pci_info->ctl_name);
		goto err_dev_put;
	}

	/*
	 * we do not allocate extra private structure for
	 * edac_pci_ctl_info, but make use of existing
	 * one instead.
	*/
	pci_info->edac_idx = edac_pci_alloc_index();
	pci_info->edac_dev = edac_pci_alloc_ctl_info(0, pci_info->ctl_name);
	if (!pci_info->edac_dev) {
		ret = -ENOMEM;
		goto err_dev_put;
	}

	pci_info->edac_dev->pvt_info = pci_info;
	pci_info->edac_dev->dev = &pci_info->dev->dev;
	pci_info->edac_dev->mod_name = AMD8111_EDAC_MOD_STR;
	pci_info->edac_dev->ctl_name = pci_info->ctl_name;
	pci_info->edac_dev->dev_name = dev_name(&pci_info->dev->dev);

	if (edac_op_state == EDAC_OPSTATE_POLL)
		pci_info->edac_dev->edac_check = pci_info->check;

	if (pci_info->init)
		pci_info->init(pci_info);

	if (edac_pci_add_device(pci_info->edac_dev, pci_info->edac_idx) > 0) {
		printk(KERN_ERR "failed to add edac_pci for %s\n",
			pci_info->ctl_name);
		goto err_edac_free_ctl;
	}

	printk(KERN_INFO "added one edac_pci on AMD8111 "
		"vendor %x, device %x, name %s\n",
		PCI_VENDOR_ID_AMD, pci_info->err_dev,
		pci_info->ctl_name);

	return 0;

err_edac_free_ctl:
	edac_pci_free_ctl_info(pci_info->edac_dev);
err_dev_put:
	pci_dev_put(pci_info->dev);
err:
	return ret;
}

static void amd8111_pci_remove(struct pci_dev *dev)
{
	struct amd8111_pci_info *pci_info;

	for (pci_info = amd8111_pcis; pci_info->err_dev; pci_info++)
		if (pci_info->dev->device == dev->device)
			break;

	if (!pci_info->err_dev)	/* should never happen */
		return;

	if (pci_info->edac_dev) {
		edac_pci_del_device(pci_info->edac_dev->dev);
		edac_pci_free_ctl_info(pci_info->edac_dev);
	}

	if (pci_info->exit)
		pci_info->exit(pci_info);

	pci_dev_put(pci_info->dev);
}

/* PCI Device ID talbe for general EDAC device */
static const struct pci_device_id amd8111_edac_dev_tbl[] = {
	{
	PCI_VEND_DEV(AMD, 8111_LPC),
	.subvendor = PCI_ANY_ID,
	.subdevice = PCI_ANY_ID,
	.class = 0,
	.class_mask = 0,
	.driver_data = LPC_BRIDGE,
	},
	{
	0,
	}			/* table is NULL-terminated */
};
MODULE_DEVICE_TABLE(pci, amd8111_edac_dev_tbl);

static struct pci_driver amd8111_edac_dev_driver = {
	.name = "AMD8111_EDAC_DEV",
	.probe = amd8111_dev_probe,
	.remove = amd8111_dev_remove,
	.id_table = amd8111_edac_dev_tbl,
};

/* PCI Device ID table for EDAC PCI controller */
static const struct pci_device_id amd8111_edac_pci_tbl[] = {
	{
	PCI_VEND_DEV(AMD, 8111_PCI),
	.subvendor = PCI_ANY_ID,
	.subdevice = PCI_ANY_ID,
	.class = 0,
	.class_mask = 0,
	.driver_data = PCI_BRIDGE,
	},
	{
	0,
	}			/* table is NULL-terminated */
};
MODULE_DEVICE_TABLE(pci, amd8111_edac_pci_tbl);

static struct pci_driver amd8111_edac_pci_driver = {
	.name = "AMD8111_EDAC_PCI",
	.probe = amd8111_pci_probe,
	.remove = amd8111_pci_remove,
	.id_table = amd8111_edac_pci_tbl,
};

static int __init amd8111_edac_init(void)
{
	int val;

	printk(KERN_INFO "AMD8111 EDAC driver "	AMD8111_EDAC_REVISION "\n");
	printk(KERN_INFO "\t(c) 2008 Wind River Systems, Inc.\n");

	/* Only POLL mode supported so far */
	edac_op_state = EDAC_OPSTATE_POLL;

	val = pci_register_driver(&amd8111_edac_dev_driver);
	val |= pci_register_driver(&amd8111_edac_pci_driver);

	return val;
}

static void __exit amd8111_edac_exit(void)
{
	pci_unregister_driver(&amd8111_edac_pci_driver);
	pci_unregister_driver(&amd8111_edac_dev_driver);
}


module_init(amd8111_edac_init);
module_exit(amd8111_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cao Qingtao <qingtao.cao@windriver.com>\n");
MODULE_DESCRIPTION("AMD8111 HyperTransport I/O Hub EDAC kernel module");
