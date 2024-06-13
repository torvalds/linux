// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Cavium, Inc.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>

#include "nic.h"
#include "thunder_bgx.h"

#define DRV_NAME	"thunder_xcv"
#define DRV_VERSION	"1.0"

/* Register offsets */
#define XCV_RESET		0x00
#define   PORT_EN		BIT_ULL(63)
#define   CLK_RESET		BIT_ULL(15)
#define   DLL_RESET		BIT_ULL(11)
#define   COMP_EN		BIT_ULL(7)
#define   TX_PKT_RESET		BIT_ULL(3)
#define   TX_DATA_RESET		BIT_ULL(2)
#define   RX_PKT_RESET		BIT_ULL(1)
#define   RX_DATA_RESET		BIT_ULL(0)
#define XCV_DLL_CTL		0x10
#define   CLKRX_BYP		BIT_ULL(23)
#define   CLKTX_BYP		BIT_ULL(15)
#define XCV_COMP_CTL		0x20
#define   DRV_BYP		BIT_ULL(63)
#define XCV_CTL			0x30
#define XCV_INT			0x40
#define XCV_INT_W1S		0x48
#define XCV_INT_ENA_W1C		0x50
#define XCV_INT_ENA_W1S		0x58
#define XCV_INBND_STATUS	0x80
#define XCV_BATCH_CRD_RET	0x100

struct xcv {
	void __iomem		*reg_base;
	struct pci_dev		*pdev;
};

static struct xcv *xcv;

/* Supported devices */
static const struct pci_device_id xcv_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xA056) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Cavium Inc");
MODULE_DESCRIPTION("Cavium Thunder RGX/XCV Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, xcv_id_table);

void xcv_init_hw(void)
{
	u64  cfg;

	/* Take DLL out of reset */
	cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
	cfg &= ~DLL_RESET;
	writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);

	/* Take clock tree out of reset */
	cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
	cfg &= ~CLK_RESET;
	writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);
	/* Wait for DLL to lock */
	msleep(1);

	/* Configure DLL - enable or bypass
	 * TX no bypass, RX bypass
	 */
	cfg = readq_relaxed(xcv->reg_base + XCV_DLL_CTL);
	cfg &= ~0xFF03;
	cfg |= CLKRX_BYP;
	writeq_relaxed(cfg, xcv->reg_base + XCV_DLL_CTL);

	/* Enable compensation controller and force the
	 * write to be visible to HW by readig back.
	 */
	cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
	cfg |= COMP_EN;
	writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);
	readq_relaxed(xcv->reg_base + XCV_RESET);
	/* Wait for compensation state machine to lock */
	msleep(10);

	/* enable the XCV block */
	cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
	cfg |= PORT_EN;
	writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);

	cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
	cfg |= CLK_RESET;
	writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);
}
EXPORT_SYMBOL(xcv_init_hw);

void xcv_setup_link(bool link_up, int link_speed)
{
	u64  cfg;
	int speed = 2;

	if (!xcv) {
		pr_err("XCV init not done, probe may have failed\n");
		return;
	}

	if (link_speed == 100)
		speed = 1;
	else if (link_speed == 10)
		speed = 0;

	if (link_up) {
		/* set operating speed */
		cfg = readq_relaxed(xcv->reg_base + XCV_CTL);
		cfg &= ~0x03;
		cfg |= speed;
		writeq_relaxed(cfg, xcv->reg_base + XCV_CTL);

		/* Reset datapaths */
		cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
		cfg |= TX_DATA_RESET | RX_DATA_RESET;
		writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);

		/* Enable the packet flow */
		cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
		cfg |= TX_PKT_RESET | RX_PKT_RESET;
		writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);

		/* Return credits to RGX */
		writeq_relaxed(0x01, xcv->reg_base + XCV_BATCH_CRD_RET);
	} else {
		/* Disable packet flow */
		cfg = readq_relaxed(xcv->reg_base + XCV_RESET);
		cfg &= ~(TX_PKT_RESET | RX_PKT_RESET);
		writeq_relaxed(cfg, xcv->reg_base + XCV_RESET);
		readq_relaxed(xcv->reg_base + XCV_RESET);
	}
}
EXPORT_SYMBOL(xcv_setup_link);

static int xcv_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err;
	struct device *dev = &pdev->dev;

	xcv = devm_kzalloc(dev, sizeof(struct xcv), GFP_KERNEL);
	if (!xcv)
		return -ENOMEM;
	xcv->pdev = pdev;

	pci_set_drvdata(pdev, xcv);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		goto err_kfree;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	/* MAP configuration registers */
	xcv->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!xcv->reg_base) {
		dev_err(dev, "XCV: Cannot map CSR memory space, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	return 0;

err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_kfree:
	devm_kfree(dev, xcv);
	xcv = NULL;
	return err;
}

static void xcv_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;

	if (xcv) {
		devm_kfree(dev, xcv);
		xcv = NULL;
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver xcv_driver = {
	.name = DRV_NAME,
	.id_table = xcv_id_table,
	.probe = xcv_probe,
	.remove = xcv_remove,
};

static int __init xcv_init_module(void)
{
	pr_info("%s, ver %s\n", DRV_NAME, DRV_VERSION);

	return pci_register_driver(&xcv_driver);
}

static void __exit xcv_cleanup_module(void)
{
	pci_unregister_driver(&xcv_driver);
}

module_init(xcv_init_module);
module_exit(xcv_cleanup_module);
