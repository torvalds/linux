// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include "cgx.h"

#define DRV_NAME	"octeontx2-cgx"
#define DRV_STRING      "Marvell OcteonTX2 CGX/MAC Driver"

/**
 * struct lmac
 * @wq_cmd_cmplt:	waitq to keep the process blocked until cmd completion
 * @cmd_lock:		Lock to serialize the command interface
 * @resp:		command response
 * @event_cb:		callback for linkchange events
 * @cmd_pend:		flag set before new command is started
 *			flag cleared after command response is received
 * @cgx:		parent cgx port
 * @lmac_id:		lmac port id
 * @name:		lmac port name
 */
struct lmac {
	wait_queue_head_t wq_cmd_cmplt;
	struct mutex cmd_lock;
	u64 resp;
	struct cgx_event_cb event_cb;
	bool cmd_pend;
	struct cgx *cgx;
	u8 lmac_id;
	char *name;
};

struct cgx {
	void __iomem		*reg_base;
	struct pci_dev		*pdev;
	u8			cgx_id;
	u8			lmac_count;
	struct lmac		*lmac_idmap[MAX_LMAC_PER_CGX];
	struct list_head	cgx_list;
};

static LIST_HEAD(cgx_list);

/* Supported devices */
static const struct pci_device_id cgx_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_CGX) },
	{ 0, }  /* end of table */
};

MODULE_DEVICE_TABLE(pci, cgx_id_table);

static void cgx_write(struct cgx *cgx, u64 lmac, u64 offset, u64 val)
{
	writeq(val, cgx->reg_base + (lmac << 18) + offset);
}

static u64 cgx_read(struct cgx *cgx, u64 lmac, u64 offset)
{
	return readq(cgx->reg_base + (lmac << 18) + offset);
}

static inline struct lmac *lmac_pdata(u8 lmac_id, struct cgx *cgx)
{
	if (!cgx || lmac_id >= MAX_LMAC_PER_CGX)
		return NULL;

	return cgx->lmac_idmap[lmac_id];
}

int cgx_get_cgx_cnt(void)
{
	struct cgx *cgx_dev;
	int count = 0;

	list_for_each_entry(cgx_dev, &cgx_list, cgx_list)
		count++;

	return count;
}
EXPORT_SYMBOL(cgx_get_cgx_cnt);

int cgx_get_lmac_cnt(void *cgxd)
{
	struct cgx *cgx = cgxd;

	if (!cgx)
		return -ENODEV;

	return cgx->lmac_count;
}
EXPORT_SYMBOL(cgx_get_lmac_cnt);

void *cgx_get_pdata(int cgx_id)
{
	struct cgx *cgx_dev;

	list_for_each_entry(cgx_dev, &cgx_list, cgx_list) {
		if (cgx_dev->cgx_id == cgx_id)
			return cgx_dev;
	}
	return NULL;
}
EXPORT_SYMBOL(cgx_get_pdata);

/* CGX Firmware interface low level support */
static int cgx_fwi_cmd_send(u64 req, u64 *resp, struct lmac *lmac)
{
	struct cgx *cgx = lmac->cgx;
	struct device *dev;
	int err = 0;
	u64 cmd;

	/* Ensure no other command is in progress */
	err = mutex_lock_interruptible(&lmac->cmd_lock);
	if (err)
		return err;

	/* Ensure command register is free */
	cmd = cgx_read(cgx, lmac->lmac_id,  CGX_COMMAND_REG);
	if (FIELD_GET(CMDREG_OWN, cmd) != CGX_CMD_OWN_NS) {
		err = -EBUSY;
		goto unlock;
	}

	/* Update ownership in command request */
	req = FIELD_SET(CMDREG_OWN, CGX_CMD_OWN_FIRMWARE, req);

	/* Mark this lmac as pending, before we start */
	lmac->cmd_pend = true;

	/* Start command in hardware */
	cgx_write(cgx, lmac->lmac_id, CGX_COMMAND_REG, req);

	/* Ensure command is completed without errors */
	if (!wait_event_timeout(lmac->wq_cmd_cmplt, !lmac->cmd_pend,
				msecs_to_jiffies(CGX_CMD_TIMEOUT))) {
		dev = &cgx->pdev->dev;
		dev_err(dev, "cgx port %d:%d cmd timeout\n",
			cgx->cgx_id, lmac->lmac_id);
		err = -EIO;
		goto unlock;
	}

	/* we have a valid command response */
	smp_rmb(); /* Ensure the latest updates are visible */
	*resp = lmac->resp;

unlock:
	mutex_unlock(&lmac->cmd_lock);

	return err;
}

static inline int cgx_fwi_cmd_generic(u64 req, u64 *resp,
				      struct cgx *cgx, int lmac_id)
{
	struct lmac *lmac;
	int err;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return -ENODEV;

	err = cgx_fwi_cmd_send(req, resp, lmac);

	/* Check for valid response */
	if (!err) {
		if (FIELD_GET(EVTREG_STAT, *resp) == CGX_STAT_FAIL)
			return -EIO;
		else
			return 0;
	}

	return err;
}

/* Hardware event handlers */
static inline void cgx_link_change_handler(u64 lstat,
					   struct lmac *lmac)
{
	struct cgx *cgx = lmac->cgx;
	struct cgx_link_event event;
	struct device *dev;

	dev = &cgx->pdev->dev;

	event.lstat.link_up = FIELD_GET(RESP_LINKSTAT_UP, lstat);
	event.lstat.full_duplex = FIELD_GET(RESP_LINKSTAT_FDUPLEX, lstat);
	event.lstat.speed = FIELD_GET(RESP_LINKSTAT_SPEED, lstat);
	event.lstat.err_type = FIELD_GET(RESP_LINKSTAT_ERRTYPE, lstat);

	event.cgx_id = cgx->cgx_id;
	event.lmac_id = lmac->lmac_id;

	if (!lmac->event_cb.notify_link_chg) {
		dev_dbg(dev, "cgx port %d:%d Link change handler null",
			cgx->cgx_id, lmac->lmac_id);
		if (event.lstat.err_type != CGX_ERR_NONE) {
			dev_err(dev, "cgx port %d:%d Link error %d\n",
				cgx->cgx_id, lmac->lmac_id,
				event.lstat.err_type);
		}
		dev_info(dev, "cgx port %d:%d Link status %s, speed %x\n",
			 cgx->cgx_id, lmac->lmac_id,
			event.lstat.link_up ? "UP" : "DOWN",
			event.lstat.speed);
		return;
	}

	if (lmac->event_cb.notify_link_chg(&event, lmac->event_cb.data))
		dev_err(dev, "event notification failure\n");
}

static inline bool cgx_cmdresp_is_linkevent(u64 event)
{
	u8 id;

	id = FIELD_GET(EVTREG_ID, event);
	if (id == CGX_CMD_LINK_BRING_UP ||
	    id == CGX_CMD_LINK_BRING_DOWN)
		return true;
	else
		return false;
}

static inline bool cgx_event_is_linkevent(u64 event)
{
	if (FIELD_GET(EVTREG_ID, event) == CGX_EVT_LINK_CHANGE)
		return true;
	else
		return false;
}

static irqreturn_t cgx_fwi_event_handler(int irq, void *data)
{
	struct lmac *lmac = data;
	struct cgx *cgx;
	u64 event;

	cgx = lmac->cgx;

	event = cgx_read(cgx, lmac->lmac_id, CGX_EVENT_REG);

	if (!FIELD_GET(EVTREG_ACK, event))
		return IRQ_NONE;

	switch (FIELD_GET(EVTREG_EVT_TYPE, event)) {
	case CGX_EVT_CMD_RESP:
		/* Copy the response. Since only one command is active at a
		 * time, there is no way a response can get overwritten
		 */
		lmac->resp = event;
		/* Ensure response is updated before thread context starts */
		smp_wmb();

		/* There wont be separate events for link change initiated from
		 * software; Hence report the command responses as events
		 */
		if (cgx_cmdresp_is_linkevent(event))
			cgx_link_change_handler(event, lmac);

		/* Release thread waiting for completion  */
		lmac->cmd_pend = false;
		wake_up_interruptible(&lmac->wq_cmd_cmplt);
		break;
	case CGX_EVT_ASYNC:
		if (cgx_event_is_linkevent(event))
			cgx_link_change_handler(event, lmac);
		break;
	}

	/* Any new event or command response will be posted by firmware
	 * only after the current status is acked.
	 * Ack the interrupt register as well.
	 */
	cgx_write(lmac->cgx, lmac->lmac_id, CGX_EVENT_REG, 0);
	cgx_write(lmac->cgx, lmac->lmac_id, CGXX_CMRX_INT, FW_CGX_INT);

	return IRQ_HANDLED;
}

/* APIs for PHY management using CGX firmware interface */

/* callback registration for hardware events like link change */
int cgx_lmac_evh_register(struct cgx_event_cb *cb, void *cgxd, int lmac_id)
{
	struct cgx *cgx = cgxd;
	struct lmac *lmac;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return -ENODEV;

	lmac->event_cb = *cb;

	return 0;
}
EXPORT_SYMBOL(cgx_lmac_evh_register);

static inline int cgx_fwi_read_version(u64 *resp, struct cgx *cgx)
{
	u64 req = 0;

	req = FIELD_SET(CMDREG_ID, CGX_CMD_GET_FW_VER, req);
	return cgx_fwi_cmd_generic(req, resp, cgx, 0);
}

static int cgx_lmac_verify_fwi_version(struct cgx *cgx)
{
	struct device *dev = &cgx->pdev->dev;
	int major_ver, minor_ver;
	u64 resp;
	int err;

	if (!cgx->lmac_count)
		return 0;

	err = cgx_fwi_read_version(&resp, cgx);
	if (err)
		return err;

	major_ver = FIELD_GET(RESP_MAJOR_VER, resp);
	minor_ver = FIELD_GET(RESP_MINOR_VER, resp);
	dev_dbg(dev, "Firmware command interface version = %d.%d\n",
		major_ver, minor_ver);
	if (major_ver != CGX_FIRMWARE_MAJOR_VER ||
	    minor_ver != CGX_FIRMWARE_MINOR_VER)
		return -EIO;
	else
		return 0;
}

static int cgx_lmac_init(struct cgx *cgx)
{
	struct lmac *lmac;
	int i, err;

	cgx->lmac_count = cgx_read(cgx, 0, CGXX_CMRX_RX_LMACS) & 0x7;
	if (cgx->lmac_count > MAX_LMAC_PER_CGX)
		cgx->lmac_count = MAX_LMAC_PER_CGX;

	for (i = 0; i < cgx->lmac_count; i++) {
		lmac = kcalloc(1, sizeof(struct lmac), GFP_KERNEL);
		if (!lmac)
			return -ENOMEM;
		lmac->name = kcalloc(1, sizeof("cgx_fwi_xxx_yyy"), GFP_KERNEL);
		if (!lmac->name)
			return -ENOMEM;
		sprintf(lmac->name, "cgx_fwi_%d_%d", cgx->cgx_id, i);
		lmac->lmac_id = i;
		lmac->cgx = cgx;
		init_waitqueue_head(&lmac->wq_cmd_cmplt);
		mutex_init(&lmac->cmd_lock);
		err = request_irq(pci_irq_vector(cgx->pdev,
						 CGX_LMAC_FWI + i * 9),
				   cgx_fwi_event_handler, 0, lmac->name, lmac);
		if (err)
			return err;

		/* Enable interrupt */
		cgx_write(cgx, lmac->lmac_id, CGXX_CMRX_INT_ENA_W1S,
			  FW_CGX_INT);

		/* Add reference */
		cgx->lmac_idmap[i] = lmac;
	}

	return cgx_lmac_verify_fwi_version(cgx);
}

static int cgx_lmac_exit(struct cgx *cgx)
{
	struct lmac *lmac;
	int i;

	/* Free all lmac related resources */
	for (i = 0; i < cgx->lmac_count; i++) {
		lmac = cgx->lmac_idmap[i];
		if (!lmac)
			continue;
		free_irq(pci_irq_vector(cgx->pdev, CGX_LMAC_FWI + i * 9), lmac);
		kfree(lmac->name);
		kfree(lmac);
	}

	return 0;
}

static int cgx_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct cgx *cgx;
	int err, nvec;

	cgx = devm_kzalloc(dev, sizeof(*cgx), GFP_KERNEL);
	if (!cgx)
		return -ENOMEM;
	cgx->pdev = pdev;

	pci_set_drvdata(pdev, cgx);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	/* MAP configuration registers */
	cgx->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!cgx->reg_base) {
		dev_err(dev, "CGX: Cannot map CSR memory space, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	nvec = CGX_NVEC;
	err = pci_alloc_irq_vectors(pdev, nvec, nvec, PCI_IRQ_MSIX);
	if (err < 0 || err != nvec) {
		dev_err(dev, "Request for %d msix vectors failed, err %d\n",
			nvec, err);
		goto err_release_regions;
	}

	list_add(&cgx->cgx_list, &cgx_list);
	cgx->cgx_id = cgx_get_cgx_cnt() - 1;

	err = cgx_lmac_init(cgx);
	if (err)
		goto err_release_lmac;

	return 0;

err_release_lmac:
	cgx_lmac_exit(cgx);
	list_del(&cgx->cgx_list);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void cgx_remove(struct pci_dev *pdev)
{
	struct cgx *cgx = pci_get_drvdata(pdev);

	cgx_lmac_exit(cgx);
	list_del(&cgx->cgx_list);
	pci_free_irq_vectors(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

struct pci_driver cgx_driver = {
	.name = DRV_NAME,
	.id_table = cgx_id_table,
	.probe = cgx_probe,
	.remove = cgx_remove,
};
