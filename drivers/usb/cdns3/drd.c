// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS DRD Driver.
 *
 * Copyright (C) 2018-2019 Cadence.
 * Copyright (C) 2019 Texas Instruments
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *         Roger Quadros <rogerq@ti.com>
 *
 *
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/usb/otg.h>

#include "gadget.h"
#include "drd.h"
#include "core.h"

/**
 * cdns3_set_mode - change mode of OTG Core
 * @cdns: pointer to context structure
 * @mode: selected mode from cdns_role
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns3_set_mode(struct cdns3 *cdns, enum usb_dr_mode mode)
{
	int ret = 0;
	u32 reg;

	switch (mode) {
	case USB_DR_MODE_PERIPHERAL:
		break;
	case USB_DR_MODE_HOST:
		break;
	case USB_DR_MODE_OTG:
		dev_dbg(cdns->dev, "Set controller to OTG mode\n");
		if (cdns->version == CDNS3_CONTROLLER_V1) {
			reg = readl(&cdns->otg_v1_regs->override);
			reg |= OVERRIDE_IDPULLUP;
			writel(reg, &cdns->otg_v1_regs->override);
		} else {
			reg = readl(&cdns->otg_v0_regs->ctrl1);
			reg |= OVERRIDE_IDPULLUP_V0;
			writel(reg, &cdns->otg_v0_regs->ctrl1);
		}

		/*
		 * Hardware specification says: "ID_VALUE must be valid within
		 * 50ms after idpullup is set to '1" so driver must wait
		 * 50ms before reading this pin.
		 */
		usleep_range(50000, 60000);
		break;
	default:
		dev_err(cdns->dev, "Unsupported mode of operation %d\n", mode);
		return -EINVAL;
	}

	return ret;
}

int cdns3_get_id(struct cdns3 *cdns)
{
	int id;

	id = readl(&cdns->otg_regs->sts) & OTGSTS_ID_VALUE;
	dev_dbg(cdns->dev, "OTG ID: %d", id);

	return id;
}

int cdns3_get_vbus(struct cdns3 *cdns)
{
	int vbus;

	vbus = !!(readl(&cdns->otg_regs->sts) & OTGSTS_VBUS_VALID);
	dev_dbg(cdns->dev, "OTG VBUS: %d", vbus);

	return vbus;
}

int cdns3_is_host(struct cdns3 *cdns)
{
	if (cdns->dr_mode == USB_DR_MODE_HOST)
		return 1;
	else if (!cdns3_get_id(cdns))
		return 1;

	return 0;
}

int cdns3_is_device(struct cdns3 *cdns)
{
	if (cdns->dr_mode == USB_DR_MODE_PERIPHERAL)
		return 1;
	else if (cdns->dr_mode == USB_DR_MODE_OTG)
		if (cdns3_get_id(cdns))
			return 1;

	return 0;
}

/**
 * cdns3_otg_disable_irq - Disable all OTG interrupts
 * @cdns: Pointer to controller context structure
 */
static void cdns3_otg_disable_irq(struct cdns3 *cdns)
{
	writel(0, &cdns->otg_regs->ien);
}

/**
 * cdns3_otg_enable_irq - enable id and sess_valid interrupts
 * @cdns: Pointer to controller context structure
 */
static void cdns3_otg_enable_irq(struct cdns3 *cdns)
{
	writel(OTGIEN_ID_CHANGE_INT | OTGIEN_VBUSVALID_RISE_INT |
	       OTGIEN_VBUSVALID_FALL_INT, &cdns->otg_regs->ien);
}

/**
 * cdns3_drd_switch_host - start/stop host
 * @cdns: Pointer to controller context structure
 * @on: 1 for start, 0 for stop
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns3_drd_switch_host(struct cdns3 *cdns, int on)
{
	int ret, val;
	u32 reg = OTGCMD_OTG_DIS;

	/* switch OTG core */
	if (on) {
		writel(OTGCMD_HOST_BUS_REQ | reg, &cdns->otg_regs->cmd);

		dev_dbg(cdns->dev, "Waiting till Host mode is turned on\n");
		ret = readl_poll_timeout_atomic(&cdns->otg_regs->sts, val,
						val & OTGSTS_XHCI_READY,
						1, 100000);
		if (ret) {
			dev_err(cdns->dev, "timeout waiting for xhci_ready\n");
			return ret;
		}
	} else {
		writel(OTGCMD_HOST_BUS_DROP | OTGCMD_DEV_BUS_DROP |
		       OTGCMD_DEV_POWER_OFF | OTGCMD_HOST_POWER_OFF,
		       &cdns->otg_regs->cmd);
		/* Waiting till H_IDLE state.*/
		readl_poll_timeout_atomic(&cdns->otg_regs->state, val,
					  !(val & OTGSTATE_HOST_STATE_MASK),
					  1, 2000000);
	}

	return 0;
}

/**
 * cdns3_drd_switch_gadget - start/stop gadget
 * @cdns: Pointer to controller context structure
 * @on: 1 for start, 0 for stop
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns3_drd_switch_gadget(struct cdns3 *cdns, int on)
{
	int ret, val;
	u32 reg = OTGCMD_OTG_DIS;

	/* switch OTG core */
	if (on) {
		writel(OTGCMD_DEV_BUS_REQ | reg, &cdns->otg_regs->cmd);

		dev_dbg(cdns->dev, "Waiting till Device mode is turned on\n");

		ret = readl_poll_timeout_atomic(&cdns->otg_regs->sts, val,
						val & OTGSTS_DEV_READY,
						1, 100000);
		if (ret) {
			dev_err(cdns->dev, "timeout waiting for dev_ready\n");
			return ret;
		}
	} else {
		/*
		 * driver should wait at least 10us after disabling Device
		 * before turning-off Device (DEV_BUS_DROP)
		 */
		usleep_range(20, 30);
		writel(OTGCMD_HOST_BUS_DROP | OTGCMD_DEV_BUS_DROP |
		       OTGCMD_DEV_POWER_OFF | OTGCMD_HOST_POWER_OFF,
		       &cdns->otg_regs->cmd);
		/* Waiting till DEV_IDLE state.*/
		readl_poll_timeout_atomic(&cdns->otg_regs->state, val,
					  !(val & OTGSTATE_DEV_STATE_MASK),
					  1, 2000000);
	}

	return 0;
}

/**
 * cdns3_init_otg_mode - initialize drd controller
 * @cdns: Pointer to controller context structure
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns3_init_otg_mode(struct cdns3 *cdns)
{
	int ret = 0;

	cdns3_otg_disable_irq(cdns);
	/* clear all interrupts */
	writel(~0, &cdns->otg_regs->ivect);

	ret = cdns3_set_mode(cdns, USB_DR_MODE_OTG);
	if (ret)
		return ret;

	cdns3_otg_enable_irq(cdns);
	return ret;
}

/**
 * cdns3_drd_update_mode - initialize mode of operation
 * @cdns: Pointer to controller context structure
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns3_drd_update_mode(struct cdns3 *cdns)
{
	int ret = 0;

	switch (cdns->dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		ret = cdns3_set_mode(cdns, USB_DR_MODE_PERIPHERAL);
		break;
	case USB_DR_MODE_HOST:
		ret = cdns3_set_mode(cdns, USB_DR_MODE_HOST);
		break;
	case USB_DR_MODE_OTG:
		ret = cdns3_init_otg_mode(cdns);
		break;
	default:
		dev_err(cdns->dev, "Unsupported mode of operation %d\n",
			cdns->dr_mode);
		return -EINVAL;
	}

	return ret;
}

static irqreturn_t cdns3_drd_thread_irq(int irq, void *data)
{
	struct cdns3 *cdns = data;

	cdns3_hw_role_switch(cdns);

	return IRQ_HANDLED;
}

/**
 * cdns3_drd_irq - interrupt handler for OTG events
 *
 * @irq: irq number for cdns3 core device
 * @data: structure of cdns3
 *
 * Returns IRQ_HANDLED or IRQ_NONE
 */
static irqreturn_t cdns3_drd_irq(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;
	struct cdns3 *cdns = data;
	u32 reg;

	if (cdns->dr_mode != USB_DR_MODE_OTG)
		return ret;

	reg = readl(&cdns->otg_regs->ivect);

	if (!reg)
		return ret;

	if (reg & OTGIEN_ID_CHANGE_INT) {
		dev_dbg(cdns->dev, "OTG IRQ: new ID: %d\n",
			cdns3_get_id(cdns));

		ret = IRQ_WAKE_THREAD;
	}

	if (reg & (OTGIEN_VBUSVALID_RISE_INT | OTGIEN_VBUSVALID_FALL_INT)) {
		dev_dbg(cdns->dev, "OTG IRQ: new VBUS: %d\n",
			cdns3_get_vbus(cdns));

		ret = IRQ_WAKE_THREAD;
	}

	writel(~0, &cdns->otg_regs->ivect);
	return ret;
}

int cdns3_drd_init(struct cdns3 *cdns)
{
	void __iomem *regs;
	int ret = 0;
	u32 state;

	regs = devm_ioremap_resource(cdns->dev, &cdns->otg_res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	/* Detection of DRD version. Controller has been released
	 * in two versions. Both are similar, but they have same changes
	 * in register maps.
	 * The first register in old version is command register and it's read
	 * only, so driver should read 0 from it. On the other hand, in v1
	 * the first register contains device ID number which is not set to 0.
	 * Driver uses this fact to detect the proper version of
	 * controller.
	 */
	cdns->otg_v0_regs = regs;
	if (!readl(&cdns->otg_v0_regs->cmd)) {
		cdns->version  = CDNS3_CONTROLLER_V0;
		cdns->otg_v1_regs = NULL;
		cdns->otg_regs = regs;
		writel(1, &cdns->otg_v0_regs->simulate);
		dev_info(cdns->dev, "DRD version v0 (%08x)\n",
			 readl(&cdns->otg_v0_regs->version));
	} else {
		cdns->otg_v0_regs = NULL;
		cdns->otg_v1_regs = regs;
		cdns->otg_regs = (void *)&cdns->otg_v1_regs->cmd;
		cdns->version  = CDNS3_CONTROLLER_V1;
		writel(1, &cdns->otg_v1_regs->simulate);
		dev_info(cdns->dev, "DRD version v1 (ID: %08x, rev: %08x)\n",
			 readl(&cdns->otg_v1_regs->did),
			 readl(&cdns->otg_v1_regs->rid));
	}

	state = OTGSTS_STRAP(readl(&cdns->otg_regs->sts));

	/* Update dr_mode according to STRAP configuration. */
	cdns->dr_mode = USB_DR_MODE_OTG;
	if (state == OTGSTS_STRAP_HOST) {
		dev_dbg(cdns->dev, "Controller strapped to HOST\n");
		cdns->dr_mode = USB_DR_MODE_HOST;
	} else if (state == OTGSTS_STRAP_GADGET) {
		dev_dbg(cdns->dev, "Controller strapped to PERIPHERAL\n");
		cdns->dr_mode = USB_DR_MODE_PERIPHERAL;
	}

	ret = devm_request_threaded_irq(cdns->dev, cdns->otg_irq,
					cdns3_drd_irq,
					cdns3_drd_thread_irq,
					IRQF_SHARED,
					dev_name(cdns->dev), cdns);

	if (ret) {
		dev_err(cdns->dev, "couldn't get otg_irq\n");
		return ret;
	}

	state = readl(&cdns->otg_regs->sts);
	if (OTGSTS_OTG_NRDY(state) != 0) {
		dev_err(cdns->dev, "Cadence USB3 OTG device not ready\n");
		return -ENODEV;
	}

	return ret;
}

int cdns3_drd_exit(struct cdns3 *cdns)
{
	cdns3_otg_disable_irq(cdns);
	return 0;
}
