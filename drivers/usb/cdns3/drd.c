// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS and USBSSP DRD Driver.
 *
 * Copyright (C) 2018-2020 Cadence.
 * Copyright (C) 2019 Texas Instruments
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *         Roger Quadros <rogerq@ti.com>
 *
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/usb/otg.h>

#include "drd.h"
#include "core.h"

/**
 * cdns_set_mode - change mode of OTG Core
 * @cdns: pointer to context structure
 * @mode: selected mode from cdns_role
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns_set_mode(struct cdns *cdns, enum usb_dr_mode mode)
{
	void __iomem  *override_reg;
	u32 reg;

	switch (mode) {
	case USB_DR_MODE_PERIPHERAL:
		break;
	case USB_DR_MODE_HOST:
		break;
	case USB_DR_MODE_OTG:
		dev_dbg(cdns->dev, "Set controller to OTG mode\n");

		if (cdns->version == CDNSP_CONTROLLER_V2)
			override_reg = &cdns->otg_cdnsp_regs->override;
		else if (cdns->version == CDNS3_CONTROLLER_V1)
			override_reg = &cdns->otg_v1_regs->override;
		else
			override_reg = &cdns->otg_v0_regs->ctrl1;

		reg = readl(override_reg);

		if (cdns->version != CDNS3_CONTROLLER_V0)
			reg |= OVERRIDE_IDPULLUP;
		else
			reg |= OVERRIDE_IDPULLUP_V0;

		writel(reg, override_reg);

		if (cdns->version == CDNS3_CONTROLLER_V1) {
			/*
			 * Enable work around feature built into the
			 * controller to address issue with RX Sensitivity
			 * est (EL_17) for USB2 PHY. The issue only occures
			 * for 0x0002450D controller version.
			 */
			if (cdns->phyrst_a_enable) {
				reg = readl(&cdns->otg_v1_regs->phyrst_cfg);
				reg |= PHYRST_CFG_PHYRST_A_ENABLE;
				writel(reg, &cdns->otg_v1_regs->phyrst_cfg);
			}
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

	return 0;
}

int cdns_get_id(struct cdns *cdns)
{
	int id;

	id = readl(&cdns->otg_regs->sts) & OTGSTS_ID_VALUE;
	dev_dbg(cdns->dev, "OTG ID: %d", id);

	return id;
}

int cdns_get_vbus(struct cdns *cdns)
{
	int vbus;

	vbus = !!(readl(&cdns->otg_regs->sts) & OTGSTS_VBUS_VALID);
	dev_dbg(cdns->dev, "OTG VBUS: %d", vbus);

	return vbus;
}

void cdns_clear_vbus(struct cdns *cdns)
{
	u32 reg;

	if (cdns->version != CDNSP_CONTROLLER_V2)
		return;

	reg = readl(&cdns->otg_cdnsp_regs->override);
	reg |= OVERRIDE_SESS_VLD_SEL;
	writel(reg, &cdns->otg_cdnsp_regs->override);
}
EXPORT_SYMBOL_GPL(cdns_clear_vbus);

void cdns_set_vbus(struct cdns *cdns)
{
	u32 reg;

	if (cdns->version != CDNSP_CONTROLLER_V2)
		return;

	reg = readl(&cdns->otg_cdnsp_regs->override);
	reg &= ~OVERRIDE_SESS_VLD_SEL;
	writel(reg, &cdns->otg_cdnsp_regs->override);
}
EXPORT_SYMBOL_GPL(cdns_set_vbus);

bool cdns_is_host(struct cdns *cdns)
{
	if (cdns->dr_mode == USB_DR_MODE_HOST)
		return true;
	else if (cdns_get_id(cdns) == CDNS3_ID_HOST)
		return true;

	return false;
}

bool cdns_is_device(struct cdns *cdns)
{
	if (cdns->dr_mode == USB_DR_MODE_PERIPHERAL)
		return true;
	else if (cdns->dr_mode == USB_DR_MODE_OTG)
		if (cdns_get_id(cdns) == CDNS3_ID_PERIPHERAL)
			return true;

	return false;
}

/**
 * cdns_otg_disable_irq - Disable all OTG interrupts
 * @cdns: Pointer to controller context structure
 */
static void cdns_otg_disable_irq(struct cdns *cdns)
{
	if (cdns->version)
		writel(0, &cdns->otg_irq_regs->ien);
}

/**
 * cdns_otg_enable_irq - enable id and sess_valid interrupts
 * @cdns: Pointer to controller context structure
 */
static void cdns_otg_enable_irq(struct cdns *cdns)
{
	writel(OTGIEN_ID_CHANGE_INT | OTGIEN_VBUSVALID_RISE_INT |
	       OTGIEN_VBUSVALID_FALL_INT, &cdns->otg_irq_regs->ien);
}

/**
 * cdns_drd_host_on - start host.
 * @cdns: Pointer to controller context structure.
 *
 * Returns 0 on success otherwise negative errno.
 */
int cdns_drd_host_on(struct cdns *cdns)
{
	u32 val, ready_bit;
	int ret;

	/* Enable host mode. */
	writel(OTGCMD_HOST_BUS_REQ | OTGCMD_OTG_DIS,
	       &cdns->otg_regs->cmd);

	if (cdns->version == CDNSP_CONTROLLER_V2)
		ready_bit = OTGSTS_CDNSP_XHCI_READY;
	else
		ready_bit = OTGSTS_CDNS3_XHCI_READY;

	dev_dbg(cdns->dev, "Waiting till Host mode is turned on\n");
	ret = readl_poll_timeout_atomic(&cdns->otg_regs->sts, val,
					val & ready_bit, 1, 100000);

	if (ret)
		dev_err(cdns->dev, "timeout waiting for xhci_ready\n");

	phy_set_mode(cdns->usb3_phy, PHY_MODE_USB_HOST);
	return ret;
}

/**
 * cdns_drd_host_off - stop host.
 * @cdns: Pointer to controller context structure.
 */
void cdns_drd_host_off(struct cdns *cdns)
{
	u32 val;

	writel(OTGCMD_HOST_BUS_DROP | OTGCMD_DEV_BUS_DROP |
	       OTGCMD_DEV_POWER_OFF | OTGCMD_HOST_POWER_OFF,
	       &cdns->otg_regs->cmd);

	/* Waiting till H_IDLE state.*/
	readl_poll_timeout_atomic(&cdns->otg_regs->state, val,
				  !(val & OTGSTATE_HOST_STATE_MASK),
				  1, 2000000);
	phy_set_mode(cdns->usb3_phy, PHY_MODE_INVALID);
}

/**
 * cdns_drd_gadget_on - start gadget.
 * @cdns: Pointer to controller context structure.
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns_drd_gadget_on(struct cdns *cdns)
{
	u32 reg = OTGCMD_OTG_DIS;
	u32 ready_bit;
	int ret, val;

	/* switch OTG core */
	writel(OTGCMD_DEV_BUS_REQ | reg, &cdns->otg_regs->cmd);

	dev_dbg(cdns->dev, "Waiting till Device mode is turned on\n");

	if (cdns->version == CDNSP_CONTROLLER_V2)
		ready_bit = OTGSTS_CDNSP_DEV_READY;
	else
		ready_bit = OTGSTS_CDNS3_DEV_READY;

	ret = readl_poll_timeout_atomic(&cdns->otg_regs->sts, val,
					val & ready_bit, 1, 100000);
	if (ret) {
		dev_err(cdns->dev, "timeout waiting for dev_ready\n");
		return ret;
	}

	phy_set_mode(cdns->usb3_phy, PHY_MODE_USB_DEVICE);
	return 0;
}
EXPORT_SYMBOL_GPL(cdns_drd_gadget_on);

/**
 * cdns_drd_gadget_off - stop gadget.
 * @cdns: Pointer to controller context structure.
 */
void cdns_drd_gadget_off(struct cdns *cdns)
{
	u32 val;

	/*
	 * Driver should wait at least 10us after disabling Device
	 * before turning-off Device (DEV_BUS_DROP).
	 */
	usleep_range(20, 30);
	writel(OTGCMD_HOST_BUS_DROP | OTGCMD_DEV_BUS_DROP |
	       OTGCMD_DEV_POWER_OFF | OTGCMD_HOST_POWER_OFF,
	       &cdns->otg_regs->cmd);
	/* Waiting till DEV_IDLE state.*/
	readl_poll_timeout_atomic(&cdns->otg_regs->state, val,
				  !(val & OTGSTATE_DEV_STATE_MASK),
				  1, 2000000);
	phy_set_mode(cdns->usb3_phy, PHY_MODE_INVALID);
}
EXPORT_SYMBOL_GPL(cdns_drd_gadget_off);

/**
 * cdns_init_otg_mode - initialize drd controller
 * @cdns: Pointer to controller context structure
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns_init_otg_mode(struct cdns *cdns)
{
	int ret;

	cdns_otg_disable_irq(cdns);
	/* clear all interrupts */
	writel(~0, &cdns->otg_irq_regs->ivect);

	ret = cdns_set_mode(cdns, USB_DR_MODE_OTG);
	if (ret)
		return ret;

	cdns_otg_enable_irq(cdns);

	return 0;
}

/**
 * cdns_drd_update_mode - initialize mode of operation
 * @cdns: Pointer to controller context structure
 *
 * Returns 0 on success otherwise negative errno
 */
int cdns_drd_update_mode(struct cdns *cdns)
{
	int ret;

	switch (cdns->dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		ret = cdns_set_mode(cdns, USB_DR_MODE_PERIPHERAL);
		break;
	case USB_DR_MODE_HOST:
		ret = cdns_set_mode(cdns, USB_DR_MODE_HOST);
		break;
	case USB_DR_MODE_OTG:
		ret = cdns_init_otg_mode(cdns);
		break;
	default:
		dev_err(cdns->dev, "Unsupported mode of operation %d\n",
			cdns->dr_mode);
		return -EINVAL;
	}

	return ret;
}

static irqreturn_t cdns_drd_thread_irq(int irq, void *data)
{
	struct cdns *cdns = data;

	cdns_hw_role_switch(cdns);

	return IRQ_HANDLED;
}

/**
 * cdns_drd_irq - interrupt handler for OTG events
 *
 * @irq: irq number for cdns core device
 * @data: structure of cdns
 *
 * Returns IRQ_HANDLED or IRQ_NONE
 */
static irqreturn_t cdns_drd_irq(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;
	struct cdns *cdns = data;
	u32 reg;

	if (cdns->dr_mode != USB_DR_MODE_OTG)
		return IRQ_NONE;

	if (cdns->in_lpm)
		return ret;

	reg = readl(&cdns->otg_irq_regs->ivect);

	if (!reg)
		return IRQ_NONE;

	if (reg & OTGIEN_ID_CHANGE_INT) {
		dev_dbg(cdns->dev, "OTG IRQ: new ID: %d\n",
			cdns_get_id(cdns));

		ret = IRQ_WAKE_THREAD;
	}

	if (reg & (OTGIEN_VBUSVALID_RISE_INT | OTGIEN_VBUSVALID_FALL_INT)) {
		dev_dbg(cdns->dev, "OTG IRQ: new VBUS: %d\n",
			cdns_get_vbus(cdns));

		ret = IRQ_WAKE_THREAD;
	}

	writel(~0, &cdns->otg_irq_regs->ivect);
	return ret;
}

int cdns_drd_init(struct cdns *cdns)
{
	void __iomem *regs;
	u32 state;
	int ret;

	regs = devm_ioremap_resource(cdns->dev, &cdns->otg_res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	/* Detection of DRD version. Controller has been released
	 * in three versions. All are very similar and are software compatible,
	 * but they have same changes in register maps.
	 * The first register in oldest version is command register and it's
	 * read only. Driver should read 0 from it. On the other hand, in v1
	 * and v2 the first register contains device ID number which is not
	 * set to 0. Driver uses this fact to detect the proper version of
	 * controller.
	 */
	cdns->otg_v0_regs = regs;
	if (!readl(&cdns->otg_v0_regs->cmd)) {
		cdns->version  = CDNS3_CONTROLLER_V0;
		cdns->otg_v1_regs = NULL;
		cdns->otg_cdnsp_regs = NULL;
		cdns->otg_regs = regs;
		cdns->otg_irq_regs = (struct cdns_otg_irq_regs __iomem  *)
				     &cdns->otg_v0_regs->ien;
		writel(1, &cdns->otg_v0_regs->simulate);
		dev_dbg(cdns->dev, "DRD version v0 (%08x)\n",
			 readl(&cdns->otg_v0_regs->version));
	} else {
		cdns->otg_v0_regs = NULL;
		cdns->otg_v1_regs = regs;
		cdns->otg_cdnsp_regs = regs;

		cdns->otg_regs = (void __iomem *)&cdns->otg_v1_regs->cmd;

		state = readl(&cdns->otg_cdnsp_regs->did);

		if (OTG_CDNSP_CHECK_DID(state)) {
			cdns->otg_irq_regs = (struct cdns_otg_irq_regs __iomem *)
					      &cdns->otg_cdnsp_regs->ien;
			cdns->version  = CDNSP_CONTROLLER_V2;
		} else if (OTG_CDNS3_CHECK_DID(state)) {
			cdns->otg_irq_regs = (struct cdns_otg_irq_regs __iomem *)
					      &cdns->otg_v1_regs->ien;
			writel(1, &cdns->otg_v1_regs->simulate);
			cdns->version  = CDNS3_CONTROLLER_V1;
		} else {
			dev_err(cdns->dev, "not supporte DID=0x%08x\n", state);
			return -EINVAL;
		}

		dev_dbg(cdns->dev, "DRD version v1 (ID: %08x, rev: %08x)\n",
			 readl(&cdns->otg_v1_regs->did),
			 readl(&cdns->otg_v1_regs->rid));
	}

	state = OTGSTS_STRAP(readl(&cdns->otg_regs->sts));

	/* Update dr_mode according to STRAP configuration. */
	cdns->dr_mode = USB_DR_MODE_OTG;

	if ((cdns->version == CDNSP_CONTROLLER_V2 &&
	     state == OTGSTS_CDNSP_STRAP_HOST) ||
	    (cdns->version != CDNSP_CONTROLLER_V2 &&
	     state == OTGSTS_STRAP_HOST)) {
		dev_dbg(cdns->dev, "Controller strapped to HOST\n");
		cdns->dr_mode = USB_DR_MODE_HOST;
	} else if ((cdns->version == CDNSP_CONTROLLER_V2 &&
		    state == OTGSTS_CDNSP_STRAP_GADGET) ||
		   (cdns->version != CDNSP_CONTROLLER_V2 &&
		    state == OTGSTS_STRAP_GADGET)) {
		dev_dbg(cdns->dev, "Controller strapped to PERIPHERAL\n");
		cdns->dr_mode = USB_DR_MODE_PERIPHERAL;
	}

	ret = devm_request_threaded_irq(cdns->dev, cdns->otg_irq,
					cdns_drd_irq,
					cdns_drd_thread_irq,
					IRQF_SHARED,
					dev_name(cdns->dev), cdns);
	if (ret) {
		dev_err(cdns->dev, "couldn't get otg_irq\n");
		return ret;
	}

	state = readl(&cdns->otg_regs->sts);
	if (OTGSTS_OTG_NRDY(state)) {
		dev_err(cdns->dev, "Cadence USB3 OTG device not ready\n");
		return -ENODEV;
	}

	return 0;
}

int cdns_drd_exit(struct cdns *cdns)
{
	cdns_otg_disable_irq(cdns);

	return 0;
}

/* Indicate the cdns3 core was power lost before */
bool cdns_power_is_lost(struct cdns *cdns)
{
	if (cdns->version == CDNS3_CONTROLLER_V0) {
		if (!(readl(&cdns->otg_v0_regs->simulate) & BIT(0)))
			return true;
	} else {
		if (!(readl(&cdns->otg_v1_regs->simulate) & BIT(0)))
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(cdns_power_is_lost);
