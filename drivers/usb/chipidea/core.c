/*
 * core.c - ChipIdea USB IP core family device controller
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Description: ChipIdea USB IP core family device controller
 *
 * This driver is composed of several blocks:
 * - HW:     hardware interface
 * - DBG:    debug facilities (optional)
 * - UTIL:   utilities
 * - ISR:    interrupts handling
 * - ENDPT:  endpoint operations (Gadget API)
 * - GADGET: gadget operations (Gadget API)
 * - BUS:    bus glue code, bus abstraction layer
 *
 * Compile Options
 * - STALL_IN:  non-empty bulk-in pipes cannot be halted
 *              if defined mass storage compliance succeeds but with warnings
 *              => case 4: Hi >  Dn
 *              => case 5: Hi >  Di
 *              => case 8: Hi <> Do
 *              if undefined usbtest 13 fails
 * - TRACE:     enable function tracing (depends on DEBUG)
 *
 * Main Features
 * - Chapter 9 & Mass Storage Compliance with Gadget File Storage
 * - Chapter 9 Compliance with Gadget Zero (STALL_IN undefined)
 * - Normal & LPM support
 *
 * USBTEST Report
 * - OK: 0-12, 13 (STALL_IN defined) & 14
 * - Not Supported: 15 & 16 (ISO)
 *
 * TODO List
 * - Suspend & Remote Wakeup
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/extcon.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/chipidea.h>
#include <linux/usb/of.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/ehci_def.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "host.h"
#include "otg.h"
#include "otg_fsm.h"

/* Controller register map */
static const u8 ci_regs_nolpm[] = {
	[CAP_CAPLENGTH]		= 0x00U,
	[CAP_HCCPARAMS]		= 0x08U,
	[CAP_DCCPARAMS]		= 0x24U,
	[CAP_TESTMODE]		= 0x38U,
	[OP_USBCMD]		= 0x00U,
	[OP_USBSTS]		= 0x04U,
	[OP_USBINTR]		= 0x08U,
	[OP_DEVICEADDR]		= 0x14U,
	[OP_ENDPTLISTADDR]	= 0x18U,
	[OP_TTCTRL]		= 0x1CU,
	[OP_BURSTSIZE]		= 0x20U,
	[OP_ULPI_VIEWPORT]	= 0x30U,
	[OP_PORTSC]		= 0x44U,
	[OP_DEVLC]		= 0x84U,
	[OP_OTGSC]		= 0x64U,
	[OP_USBMODE]		= 0x68U,
	[OP_ENDPTSETUPSTAT]	= 0x6CU,
	[OP_ENDPTPRIME]		= 0x70U,
	[OP_ENDPTFLUSH]		= 0x74U,
	[OP_ENDPTSTAT]		= 0x78U,
	[OP_ENDPTCOMPLETE]	= 0x7CU,
	[OP_ENDPTCTRL]		= 0x80U,
};

static const u8 ci_regs_lpm[] = {
	[CAP_CAPLENGTH]		= 0x00U,
	[CAP_HCCPARAMS]		= 0x08U,
	[CAP_DCCPARAMS]		= 0x24U,
	[CAP_TESTMODE]		= 0xFCU,
	[OP_USBCMD]		= 0x00U,
	[OP_USBSTS]		= 0x04U,
	[OP_USBINTR]		= 0x08U,
	[OP_DEVICEADDR]		= 0x14U,
	[OP_ENDPTLISTADDR]	= 0x18U,
	[OP_TTCTRL]		= 0x1CU,
	[OP_BURSTSIZE]		= 0x20U,
	[OP_ULPI_VIEWPORT]	= 0x30U,
	[OP_PORTSC]		= 0x44U,
	[OP_DEVLC]		= 0x84U,
	[OP_OTGSC]		= 0xC4U,
	[OP_USBMODE]		= 0xC8U,
	[OP_ENDPTSETUPSTAT]	= 0xD8U,
	[OP_ENDPTPRIME]		= 0xDCU,
	[OP_ENDPTFLUSH]		= 0xE0U,
	[OP_ENDPTSTAT]		= 0xE4U,
	[OP_ENDPTCOMPLETE]	= 0xE8U,
	[OP_ENDPTCTRL]		= 0xECU,
};

static void hw_alloc_regmap(struct ci_hdrc *ci, bool is_lpm)
{
	int i;

	for (i = 0; i < OP_ENDPTCTRL; i++)
		ci->hw_bank.regmap[i] =
			(i <= CAP_LAST ? ci->hw_bank.cap : ci->hw_bank.op) +
			(is_lpm ? ci_regs_lpm[i] : ci_regs_nolpm[i]);

	for (; i <= OP_LAST; i++)
		ci->hw_bank.regmap[i] = ci->hw_bank.op +
			4 * (i - OP_ENDPTCTRL) +
			(is_lpm
			 ? ci_regs_lpm[OP_ENDPTCTRL]
			 : ci_regs_nolpm[OP_ENDPTCTRL]);

}

static enum ci_revision ci_get_revision(struct ci_hdrc *ci)
{
	int ver = hw_read_id_reg(ci, ID_ID, VERSION) >> __ffs(VERSION);
	enum ci_revision rev = CI_REVISION_UNKNOWN;

	if (ver == 0x2) {
		rev = hw_read_id_reg(ci, ID_ID, REVISION)
			>> __ffs(REVISION);
		rev += CI_REVISION_20;
	} else if (ver == 0x0) {
		rev = CI_REVISION_1X;
	}

	return rev;
}

/**
 * hw_read_intr_enable: returns interrupt enable register
 *
 * @ci: the controller
 *
 * This function returns register data
 */
u32 hw_read_intr_enable(struct ci_hdrc *ci)
{
	return hw_read(ci, OP_USBINTR, ~0);
}

/**
 * hw_read_intr_status: returns interrupt status register
 *
 * @ci: the controller
 *
 * This function returns register data
 */
u32 hw_read_intr_status(struct ci_hdrc *ci)
{
	return hw_read(ci, OP_USBSTS, ~0);
}

/**
 * hw_port_test_set: writes port test mode (execute without interruption)
 * @mode: new value
 *
 * This function returns an error code
 */
int hw_port_test_set(struct ci_hdrc *ci, u8 mode)
{
	const u8 TEST_MODE_MAX = 7;

	if (mode > TEST_MODE_MAX)
		return -EINVAL;

	hw_write(ci, OP_PORTSC, PORTSC_PTC, mode << __ffs(PORTSC_PTC));
	return 0;
}

/**
 * hw_port_test_get: reads port test mode value
 *
 * @ci: the controller
 *
 * This function returns port test mode value
 */
u8 hw_port_test_get(struct ci_hdrc *ci)
{
	return hw_read(ci, OP_PORTSC, PORTSC_PTC) >> __ffs(PORTSC_PTC);
}

static void hw_wait_phy_stable(void)
{
	/*
	 * The phy needs some delay to output the stable status from low
	 * power mode. And for OTGSC, the status inputs are debounced
	 * using a 1 ms time constant, so, delay 2ms for controller to get
	 * the stable status, like vbus and id when the phy leaves low power.
	 */
	usleep_range(2000, 2500);
}

/* The PHY enters/leaves low power mode */
static void ci_hdrc_enter_lpm(struct ci_hdrc *ci, bool enable)
{
	enum ci_hw_regs reg = ci->hw_bank.lpm ? OP_DEVLC : OP_PORTSC;
	bool lpm = !!(hw_read(ci, reg, PORTSC_PHCD(ci->hw_bank.lpm)));

	if (enable && !lpm)
		hw_write(ci, reg, PORTSC_PHCD(ci->hw_bank.lpm),
				PORTSC_PHCD(ci->hw_bank.lpm));
	else if (!enable && lpm)
		hw_write(ci, reg, PORTSC_PHCD(ci->hw_bank.lpm),
				0);
}

static int hw_device_init(struct ci_hdrc *ci, void __iomem *base)
{
	u32 reg;

	/* bank is a module variable */
	ci->hw_bank.abs = base;

	ci->hw_bank.cap = ci->hw_bank.abs;
	ci->hw_bank.cap += ci->platdata->capoffset;
	ci->hw_bank.op = ci->hw_bank.cap + (ioread32(ci->hw_bank.cap) & 0xff);

	hw_alloc_regmap(ci, false);
	reg = hw_read(ci, CAP_HCCPARAMS, HCCPARAMS_LEN) >>
		__ffs(HCCPARAMS_LEN);
	ci->hw_bank.lpm  = reg;
	if (reg)
		hw_alloc_regmap(ci, !!reg);
	ci->hw_bank.size = ci->hw_bank.op - ci->hw_bank.abs;
	ci->hw_bank.size += OP_LAST;
	ci->hw_bank.size /= sizeof(u32);

	reg = hw_read(ci, CAP_DCCPARAMS, DCCPARAMS_DEN) >>
		__ffs(DCCPARAMS_DEN);
	ci->hw_ep_max = reg * 2;   /* cache hw ENDPT_MAX */

	if (ci->hw_ep_max > ENDPT_MAX)
		return -ENODEV;

	ci_hdrc_enter_lpm(ci, false);

	/* Disable all interrupts bits */
	hw_write(ci, OP_USBINTR, 0xffffffff, 0);

	/* Clear all interrupts status bits*/
	hw_write(ci, OP_USBSTS, 0xffffffff, 0xffffffff);

	ci->rev = ci_get_revision(ci);

	dev_dbg(ci->dev,
		"ChipIdea HDRC found, revision: %d, lpm: %d; cap: %p op: %p\n",
		ci->rev, ci->hw_bank.lpm, ci->hw_bank.cap, ci->hw_bank.op);

	/* setup lock mode ? */

	/* ENDPTSETUPSTAT is '0' by default */

	/* HCSPARAMS.bf.ppc SHOULD BE zero for device */

	return 0;
}

void hw_phymode_configure(struct ci_hdrc *ci)
{
	u32 portsc, lpm, sts = 0;

	switch (ci->platdata->phy_mode) {
	case USBPHY_INTERFACE_MODE_UTMI:
		portsc = PORTSC_PTS(PTS_UTMI);
		lpm = DEVLC_PTS(PTS_UTMI);
		break;
	case USBPHY_INTERFACE_MODE_UTMIW:
		portsc = PORTSC_PTS(PTS_UTMI) | PORTSC_PTW;
		lpm = DEVLC_PTS(PTS_UTMI) | DEVLC_PTW;
		break;
	case USBPHY_INTERFACE_MODE_ULPI:
		portsc = PORTSC_PTS(PTS_ULPI);
		lpm = DEVLC_PTS(PTS_ULPI);
		break;
	case USBPHY_INTERFACE_MODE_SERIAL:
		portsc = PORTSC_PTS(PTS_SERIAL);
		lpm = DEVLC_PTS(PTS_SERIAL);
		sts = 1;
		break;
	case USBPHY_INTERFACE_MODE_HSIC:
		portsc = PORTSC_PTS(PTS_HSIC);
		lpm = DEVLC_PTS(PTS_HSIC);
		break;
	default:
		return;
	}

	if (ci->hw_bank.lpm) {
		hw_write(ci, OP_DEVLC, DEVLC_PTS(7) | DEVLC_PTW, lpm);
		if (sts)
			hw_write(ci, OP_DEVLC, DEVLC_STS, DEVLC_STS);
	} else {
		hw_write(ci, OP_PORTSC, PORTSC_PTS(7) | PORTSC_PTW, portsc);
		if (sts)
			hw_write(ci, OP_PORTSC, PORTSC_STS, PORTSC_STS);
	}
}
EXPORT_SYMBOL_GPL(hw_phymode_configure);

/**
 * _ci_usb_phy_init: initialize phy taking in account both phy and usb_phy
 * interfaces
 * @ci: the controller
 *
 * This function returns an error code if the phy failed to init
 */
static int _ci_usb_phy_init(struct ci_hdrc *ci)
{
	int ret;

	if (ci->phy) {
		ret = phy_init(ci->phy);
		if (ret)
			return ret;

		ret = phy_power_on(ci->phy);
		if (ret) {
			phy_exit(ci->phy);
			return ret;
		}
	} else {
		ret = usb_phy_init(ci->usb_phy);
	}

	return ret;
}

/**
 * _ci_usb_phy_exit: deinitialize phy taking in account both phy and usb_phy
 * interfaces
 * @ci: the controller
 */
static void ci_usb_phy_exit(struct ci_hdrc *ci)
{
	if (ci->platdata->flags & CI_HDRC_OVERRIDE_PHY_CONTROL)
		return;

	if (ci->phy) {
		phy_power_off(ci->phy);
		phy_exit(ci->phy);
	} else {
		usb_phy_shutdown(ci->usb_phy);
	}
}

/**
 * ci_usb_phy_init: initialize phy according to different phy type
 * @ci: the controller
 *
 * This function returns an error code if usb_phy_init has failed
 */
static int ci_usb_phy_init(struct ci_hdrc *ci)
{
	int ret;

	if (ci->platdata->flags & CI_HDRC_OVERRIDE_PHY_CONTROL)
		return 0;

	switch (ci->platdata->phy_mode) {
	case USBPHY_INTERFACE_MODE_UTMI:
	case USBPHY_INTERFACE_MODE_UTMIW:
	case USBPHY_INTERFACE_MODE_HSIC:
		ret = _ci_usb_phy_init(ci);
		if (!ret)
			hw_wait_phy_stable();
		else
			return ret;
		hw_phymode_configure(ci);
		break;
	case USBPHY_INTERFACE_MODE_ULPI:
	case USBPHY_INTERFACE_MODE_SERIAL:
		hw_phymode_configure(ci);
		ret = _ci_usb_phy_init(ci);
		if (ret)
			return ret;
		break;
	default:
		ret = _ci_usb_phy_init(ci);
		if (!ret)
			hw_wait_phy_stable();
	}

	return ret;
}


/**
 * ci_platform_configure: do controller configure
 * @ci: the controller
 *
 */
void ci_platform_configure(struct ci_hdrc *ci)
{
	bool is_device_mode, is_host_mode;

	is_device_mode = hw_read(ci, OP_USBMODE, USBMODE_CM) == USBMODE_CM_DC;
	is_host_mode = hw_read(ci, OP_USBMODE, USBMODE_CM) == USBMODE_CM_HC;

	if (is_device_mode) {
		phy_set_mode(ci->phy, PHY_MODE_USB_DEVICE);

		if (ci->platdata->flags & CI_HDRC_DISABLE_DEVICE_STREAMING)
			hw_write(ci, OP_USBMODE, USBMODE_CI_SDIS,
				 USBMODE_CI_SDIS);
	}

	if (is_host_mode) {
		phy_set_mode(ci->phy, PHY_MODE_USB_HOST);

		if (ci->platdata->flags & CI_HDRC_DISABLE_HOST_STREAMING)
			hw_write(ci, OP_USBMODE, USBMODE_CI_SDIS,
				 USBMODE_CI_SDIS);
	}

	if (ci->platdata->flags & CI_HDRC_FORCE_FULLSPEED) {
		if (ci->hw_bank.lpm)
			hw_write(ci, OP_DEVLC, DEVLC_PFSC, DEVLC_PFSC);
		else
			hw_write(ci, OP_PORTSC, PORTSC_PFSC, PORTSC_PFSC);
	}

	if (ci->platdata->flags & CI_HDRC_SET_NON_ZERO_TTHA)
		hw_write(ci, OP_TTCTRL, TTCTRL_TTHA_MASK, TTCTRL_TTHA);

	hw_write(ci, OP_USBCMD, 0xff0000, ci->platdata->itc_setting << 16);

	if (ci->platdata->flags & CI_HDRC_OVERRIDE_AHB_BURST)
		hw_write_id_reg(ci, ID_SBUSCFG, AHBBRST_MASK,
			ci->platdata->ahb_burst_config);

	/* override burst size, take effect only when ahb_burst_config is 0 */
	if (!hw_read_id_reg(ci, ID_SBUSCFG, AHBBRST_MASK)) {
		if (ci->platdata->flags & CI_HDRC_OVERRIDE_TX_BURST)
			hw_write(ci, OP_BURSTSIZE, TX_BURST_MASK,
			ci->platdata->tx_burst_size << __ffs(TX_BURST_MASK));

		if (ci->platdata->flags & CI_HDRC_OVERRIDE_RX_BURST)
			hw_write(ci, OP_BURSTSIZE, RX_BURST_MASK,
				ci->platdata->rx_burst_size);
	}
}

/**
 * hw_controller_reset: do controller reset
 * @ci: the controller
  *
 * This function returns an error code
 */
static int hw_controller_reset(struct ci_hdrc *ci)
{
	int count = 0;

	hw_write(ci, OP_USBCMD, USBCMD_RST, USBCMD_RST);
	while (hw_read(ci, OP_USBCMD, USBCMD_RST)) {
		udelay(10);
		if (count++ > 1000)
			return -ETIMEDOUT;
	}

	return 0;
}

/**
 * hw_device_reset: resets chip (execute without interruption)
 * @ci: the controller
 *
 * This function returns an error code
 */
int hw_device_reset(struct ci_hdrc *ci)
{
	int ret;

	/* should flush & stop before reset */
	hw_write(ci, OP_ENDPTFLUSH, ~0, ~0);
	hw_write(ci, OP_USBCMD, USBCMD_RS, 0);

	ret = hw_controller_reset(ci);
	if (ret) {
		dev_err(ci->dev, "error resetting controller, ret=%d\n", ret);
		return ret;
	}

	if (ci->platdata->notify_event) {
		ret = ci->platdata->notify_event(ci,
			CI_HDRC_CONTROLLER_RESET_EVENT);
		if (ret)
			return ret;
	}

	/* USBMODE should be configured step by step */
	hw_write(ci, OP_USBMODE, USBMODE_CM, USBMODE_CM_IDLE);
	hw_write(ci, OP_USBMODE, USBMODE_CM, USBMODE_CM_DC);
	/* HW >= 2.3 */
	hw_write(ci, OP_USBMODE, USBMODE_SLOM, USBMODE_SLOM);

	if (hw_read(ci, OP_USBMODE, USBMODE_CM) != USBMODE_CM_DC) {
		pr_err("cannot enter in %s device mode", ci_role(ci)->name);
		pr_err("lpm = %i", ci->hw_bank.lpm);
		return -ENODEV;
	}

	ci_platform_configure(ci);

	return 0;
}

static irqreturn_t ci_irq(int irq, void *data)
{
	struct ci_hdrc *ci = data;
	irqreturn_t ret = IRQ_NONE;
	u32 otgsc = 0;

	if (ci->in_lpm) {
		disable_irq_nosync(irq);
		ci->wakeup_int = true;
		pm_runtime_get(ci->dev);
		return IRQ_HANDLED;
	}

	if (ci->is_otg) {
		otgsc = hw_read_otgsc(ci, ~0);
		if (ci_otg_is_fsm_mode(ci)) {
			ret = ci_otg_fsm_irq(ci);
			if (ret == IRQ_HANDLED)
				return ret;
		}
	}

	/*
	 * Handle id change interrupt, it indicates device/host function
	 * switch.
	 */
	if (ci->is_otg && (otgsc & OTGSC_IDIE) && (otgsc & OTGSC_IDIS)) {
		ci->id_event = true;
		/* Clear ID change irq status */
		hw_write_otgsc(ci, OTGSC_IDIS, OTGSC_IDIS);
		ci_otg_queue_work(ci);
		return IRQ_HANDLED;
	}

	/*
	 * Handle vbus change interrupt, it indicates device connection
	 * and disconnection events.
	 */
	if (ci->is_otg && (otgsc & OTGSC_BSVIE) && (otgsc & OTGSC_BSVIS)) {
		ci->b_sess_valid_event = true;
		/* Clear BSV irq */
		hw_write_otgsc(ci, OTGSC_BSVIS, OTGSC_BSVIS);
		ci_otg_queue_work(ci);
		return IRQ_HANDLED;
	}

	/* Handle device/host interrupt */
	if (ci->role != CI_ROLE_END)
		ret = ci_role(ci)->irq(ci);

	return ret;
}

static int ci_cable_notifier(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct ci_hdrc_cable *cbl = container_of(nb, struct ci_hdrc_cable, nb);
	struct ci_hdrc *ci = cbl->ci;

	cbl->connected = event;
	cbl->changed = true;

	ci_irq(ci->irq, ci);
	return NOTIFY_DONE;
}

static int ci_get_platdata(struct device *dev,
		struct ci_hdrc_platform_data *platdata)
{
	struct extcon_dev *ext_vbus, *ext_id;
	struct ci_hdrc_cable *cable;
	int ret;

	if (!platdata->phy_mode)
		platdata->phy_mode = of_usb_get_phy_mode(dev->of_node);

	if (!platdata->dr_mode)
		platdata->dr_mode = usb_get_dr_mode(dev);

	if (platdata->dr_mode == USB_DR_MODE_UNKNOWN)
		platdata->dr_mode = USB_DR_MODE_OTG;

	if (platdata->dr_mode != USB_DR_MODE_PERIPHERAL) {
		/* Get the vbus regulator */
		platdata->reg_vbus = devm_regulator_get(dev, "vbus");
		if (PTR_ERR(platdata->reg_vbus) == -EPROBE_DEFER) {
			return -EPROBE_DEFER;
		} else if (PTR_ERR(platdata->reg_vbus) == -ENODEV) {
			/* no vbus regulator is needed */
			platdata->reg_vbus = NULL;
		} else if (IS_ERR(platdata->reg_vbus)) {
			dev_err(dev, "Getting regulator error: %ld\n",
				PTR_ERR(platdata->reg_vbus));
			return PTR_ERR(platdata->reg_vbus);
		}
		/* Get TPL support */
		if (!platdata->tpl_support)
			platdata->tpl_support =
				of_usb_host_tpl_support(dev->of_node);
	}

	if (platdata->dr_mode == USB_DR_MODE_OTG) {
		/* We can support HNP and SRP of OTG 2.0 */
		platdata->ci_otg_caps.otg_rev = 0x0200;
		platdata->ci_otg_caps.hnp_support = true;
		platdata->ci_otg_caps.srp_support = true;

		/* Update otg capabilities by DT properties */
		ret = of_usb_update_otg_caps(dev->of_node,
					&platdata->ci_otg_caps);
		if (ret)
			return ret;
	}

	if (usb_get_maximum_speed(dev) == USB_SPEED_FULL)
		platdata->flags |= CI_HDRC_FORCE_FULLSPEED;

	of_property_read_u32(dev->of_node, "phy-clkgate-delay-us",
				     &platdata->phy_clkgate_delay_us);

	platdata->itc_setting = 1;

	of_property_read_u32(dev->of_node, "itc-setting",
					&platdata->itc_setting);

	ret = of_property_read_u32(dev->of_node, "ahb-burst-config",
				&platdata->ahb_burst_config);
	if (!ret) {
		platdata->flags |= CI_HDRC_OVERRIDE_AHB_BURST;
	} else if (ret != -EINVAL) {
		dev_err(dev, "failed to get ahb-burst-config\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "tx-burst-size-dword",
				&platdata->tx_burst_size);
	if (!ret) {
		platdata->flags |= CI_HDRC_OVERRIDE_TX_BURST;
	} else if (ret != -EINVAL) {
		dev_err(dev, "failed to get tx-burst-size-dword\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "rx-burst-size-dword",
				&platdata->rx_burst_size);
	if (!ret) {
		platdata->flags |= CI_HDRC_OVERRIDE_RX_BURST;
	} else if (ret != -EINVAL) {
		dev_err(dev, "failed to get rx-burst-size-dword\n");
		return ret;
	}

	if (of_find_property(dev->of_node, "non-zero-ttctrl-ttha", NULL))
		platdata->flags |= CI_HDRC_SET_NON_ZERO_TTHA;

	ext_id = ERR_PTR(-ENODEV);
	ext_vbus = ERR_PTR(-ENODEV);
	if (of_property_read_bool(dev->of_node, "extcon")) {
		/* Each one of them is not mandatory */
		ext_vbus = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(ext_vbus) && PTR_ERR(ext_vbus) != -ENODEV)
			return PTR_ERR(ext_vbus);

		ext_id = extcon_get_edev_by_phandle(dev, 1);
		if (IS_ERR(ext_id) && PTR_ERR(ext_id) != -ENODEV)
			return PTR_ERR(ext_id);
	}

	cable = &platdata->vbus_extcon;
	cable->nb.notifier_call = ci_cable_notifier;
	cable->edev = ext_vbus;

	if (!IS_ERR(ext_vbus)) {
		ret = extcon_get_state(cable->edev, EXTCON_USB);
		if (ret)
			cable->connected = true;
		else
			cable->connected = false;
	}

	cable = &platdata->id_extcon;
	cable->nb.notifier_call = ci_cable_notifier;
	cable->edev = ext_id;

	if (!IS_ERR(ext_id)) {
		ret = extcon_get_state(cable->edev, EXTCON_USB_HOST);
		if (ret)
			cable->connected = true;
		else
			cable->connected = false;
	}
	return 0;
}

static int ci_extcon_register(struct ci_hdrc *ci)
{
	struct ci_hdrc_cable *id, *vbus;
	int ret;

	id = &ci->platdata->id_extcon;
	id->ci = ci;
	if (!IS_ERR(id->edev)) {
		ret = devm_extcon_register_notifier(ci->dev, id->edev,
						EXTCON_USB_HOST, &id->nb);
		if (ret < 0) {
			dev_err(ci->dev, "register ID failed\n");
			return ret;
		}
	}

	vbus = &ci->platdata->vbus_extcon;
	vbus->ci = ci;
	if (!IS_ERR(vbus->edev)) {
		ret = devm_extcon_register_notifier(ci->dev, vbus->edev,
						EXTCON_USB, &vbus->nb);
		if (ret < 0) {
			dev_err(ci->dev, "register VBUS failed\n");
			return ret;
		}
	}

	return 0;
}

static DEFINE_IDA(ci_ida);

struct platform_device *ci_hdrc_add_device(struct device *dev,
			struct resource *res, int nres,
			struct ci_hdrc_platform_data *platdata)
{
	struct platform_device *pdev;
	int id, ret;

	ret = ci_get_platdata(dev, platdata);
	if (ret)
		return ERR_PTR(ret);

	id = ida_simple_get(&ci_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	pdev = platform_device_alloc("ci_hdrc", id);
	if (!pdev) {
		ret = -ENOMEM;
		goto put_id;
	}

	pdev->dev.parent = dev;

	ret = platform_device_add_resources(pdev, res, nres);
	if (ret)
		goto err;

	ret = platform_device_add_data(pdev, platdata, sizeof(*platdata));
	if (ret)
		goto err;

	ret = platform_device_add(pdev);
	if (ret)
		goto err;

	return pdev;

err:
	platform_device_put(pdev);
put_id:
	ida_simple_remove(&ci_ida, id);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(ci_hdrc_add_device);

void ci_hdrc_remove_device(struct platform_device *pdev)
{
	int id = pdev->id;
	platform_device_unregister(pdev);
	ida_simple_remove(&ci_ida, id);
}
EXPORT_SYMBOL_GPL(ci_hdrc_remove_device);

static inline void ci_role_destroy(struct ci_hdrc *ci)
{
	ci_hdrc_gadget_destroy(ci);
	ci_hdrc_host_destroy(ci);
	if (ci->is_otg)
		ci_hdrc_otg_destroy(ci);
}

static void ci_get_otg_capable(struct ci_hdrc *ci)
{
	if (ci->platdata->flags & CI_HDRC_DUAL_ROLE_NOT_OTG)
		ci->is_otg = false;
	else
		ci->is_otg = (hw_read(ci, CAP_DCCPARAMS,
				DCCPARAMS_DC | DCCPARAMS_HC)
					== (DCCPARAMS_DC | DCCPARAMS_HC));
	if (ci->is_otg) {
		dev_dbg(ci->dev, "It is OTG capable controller\n");
		/* Disable and clear all OTG irq */
		hw_write_otgsc(ci, OTGSC_INT_EN_BITS | OTGSC_INT_STATUS_BITS,
							OTGSC_INT_STATUS_BITS);
	}
}

static int ci_hdrc_probe(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct ci_hdrc	*ci;
	struct resource	*res;
	void __iomem	*base;
	int		ret;
	enum usb_dr_mode dr_mode;

	if (!dev_get_platdata(dev)) {
		dev_err(dev, "platform data missing\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ci = devm_kzalloc(dev, sizeof(*ci), GFP_KERNEL);
	if (!ci)
		return -ENOMEM;

	spin_lock_init(&ci->lock);
	ci->dev = dev;
	ci->platdata = dev_get_platdata(dev);
	ci->imx28_write_fix = !!(ci->platdata->flags &
		CI_HDRC_IMX28_WRITE_FIX);
	ci->supports_runtime_pm = !!(ci->platdata->flags &
		CI_HDRC_SUPPORTS_RUNTIME_PM);
	platform_set_drvdata(pdev, ci);

	ret = hw_device_init(ci, base);
	if (ret < 0) {
		dev_err(dev, "can't initialize hardware\n");
		return -ENODEV;
	}

	ret = ci_ulpi_init(ci);
	if (ret)
		return ret;

	if (ci->platdata->phy) {
		ci->phy = ci->platdata->phy;
	} else if (ci->platdata->usb_phy) {
		ci->usb_phy = ci->platdata->usb_phy;
	} else {
		ci->phy = devm_phy_get(dev->parent, "usb-phy");
		ci->usb_phy = devm_usb_get_phy(dev->parent, USB_PHY_TYPE_USB2);

		/* if both generic PHY and USB PHY layers aren't enabled */
		if (PTR_ERR(ci->phy) == -ENOSYS &&
				PTR_ERR(ci->usb_phy) == -ENXIO) {
			ret = -ENXIO;
			goto ulpi_exit;
		}

		if (IS_ERR(ci->phy) && IS_ERR(ci->usb_phy)) {
			ret = -EPROBE_DEFER;
			goto ulpi_exit;
		}

		if (IS_ERR(ci->phy))
			ci->phy = NULL;
		else if (IS_ERR(ci->usb_phy))
			ci->usb_phy = NULL;
	}

	ret = ci_usb_phy_init(ci);
	if (ret) {
		dev_err(dev, "unable to init phy: %d\n", ret);
		return ret;
	}

	ci->hw_bank.phys = res->start;

	ci->irq = platform_get_irq(pdev, 0);
	if (ci->irq < 0) {
		dev_err(dev, "missing IRQ\n");
		ret = ci->irq;
		goto deinit_phy;
	}

	ci_get_otg_capable(ci);

	dr_mode = ci->platdata->dr_mode;
	/* initialize role(s) before the interrupt is requested */
	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_HOST) {
		ret = ci_hdrc_host_init(ci);
		if (ret)
			dev_info(dev, "doesn't support host\n");
	}

	if (dr_mode == USB_DR_MODE_OTG || dr_mode == USB_DR_MODE_PERIPHERAL) {
		ret = ci_hdrc_gadget_init(ci);
		if (ret)
			dev_info(dev, "doesn't support gadget\n");
	}

	if (!ci->roles[CI_ROLE_HOST] && !ci->roles[CI_ROLE_GADGET]) {
		dev_err(dev, "no supported roles\n");
		ret = -ENODEV;
		goto deinit_phy;
	}

	if (ci->is_otg && ci->roles[CI_ROLE_GADGET]) {
		ret = ci_hdrc_otg_init(ci);
		if (ret) {
			dev_err(dev, "init otg fails, ret = %d\n", ret);
			goto stop;
		}
	}

	if (ci->roles[CI_ROLE_HOST] && ci->roles[CI_ROLE_GADGET]) {
		if (ci->is_otg) {
			ci->role = ci_otg_role(ci);
			/* Enable ID change irq */
			hw_write_otgsc(ci, OTGSC_IDIE, OTGSC_IDIE);
		} else {
			/*
			 * If the controller is not OTG capable, but support
			 * role switch, the defalt role is gadget, and the
			 * user can switch it through debugfs.
			 */
			ci->role = CI_ROLE_GADGET;
		}
	} else {
		ci->role = ci->roles[CI_ROLE_HOST]
			? CI_ROLE_HOST
			: CI_ROLE_GADGET;
	}

	if (!ci_otg_is_fsm_mode(ci)) {
		/* only update vbus status for peripheral */
		if (ci->role == CI_ROLE_GADGET)
			ci_handle_vbus_change(ci);

		ret = ci_role_start(ci, ci->role);
		if (ret) {
			dev_err(dev, "can't start %s role\n",
						ci_role(ci)->name);
			goto stop;
		}
	}

	ret = devm_request_irq(dev, ci->irq, ci_irq, IRQF_SHARED,
			ci->platdata->name, ci);
	if (ret)
		goto stop;

	ret = ci_extcon_register(ci);
	if (ret)
		goto stop;

	if (ci->supports_runtime_pm) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
		pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
		pm_runtime_mark_last_busy(ci->dev);
		pm_runtime_use_autosuspend(&pdev->dev);
	}

	if (ci_otg_is_fsm_mode(ci))
		ci_hdrc_otg_fsm_start(ci);

	device_set_wakeup_capable(&pdev->dev, true);

	ret = dbg_create_files(ci);
	if (!ret)
		return 0;

stop:
	ci_role_destroy(ci);
deinit_phy:
	ci_usb_phy_exit(ci);
ulpi_exit:
	ci_ulpi_exit(ci);

	return ret;
}

static int ci_hdrc_remove(struct platform_device *pdev)
{
	struct ci_hdrc *ci = platform_get_drvdata(pdev);

	if (ci->supports_runtime_pm) {
		pm_runtime_get_sync(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);
	}

	dbg_remove_files(ci);
	ci_role_destroy(ci);
	ci_hdrc_enter_lpm(ci, true);
	ci_usb_phy_exit(ci);
	ci_ulpi_exit(ci);

	return 0;
}

#ifdef CONFIG_PM
/* Prepare wakeup by SRP before suspend */
static void ci_otg_fsm_suspend_for_srp(struct ci_hdrc *ci)
{
	if ((ci->fsm.otg->state == OTG_STATE_A_IDLE) &&
				!hw_read_otgsc(ci, OTGSC_ID)) {
		hw_write(ci, OP_PORTSC, PORTSC_W1C_BITS | PORTSC_PP,
								PORTSC_PP);
		hw_write(ci, OP_PORTSC, PORTSC_W1C_BITS | PORTSC_WKCN,
								PORTSC_WKCN);
	}
}

/* Handle SRP when wakeup by data pulse */
static void ci_otg_fsm_wakeup_by_srp(struct ci_hdrc *ci)
{
	if ((ci->fsm.otg->state == OTG_STATE_A_IDLE) &&
		(ci->fsm.a_bus_drop == 1) && (ci->fsm.a_bus_req == 0)) {
		if (!hw_read_otgsc(ci, OTGSC_ID)) {
			ci->fsm.a_srp_det = 1;
			ci->fsm.a_bus_drop = 0;
		} else {
			ci->fsm.id = 1;
		}
		ci_otg_queue_work(ci);
	}
}

static void ci_controller_suspend(struct ci_hdrc *ci)
{
	disable_irq(ci->irq);
	ci_hdrc_enter_lpm(ci, true);
	if (ci->platdata->phy_clkgate_delay_us)
		usleep_range(ci->platdata->phy_clkgate_delay_us,
			     ci->platdata->phy_clkgate_delay_us + 50);
	usb_phy_set_suspend(ci->usb_phy, 1);
	ci->in_lpm = true;
	enable_irq(ci->irq);
}

static int ci_controller_resume(struct device *dev)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "at %s\n", __func__);

	if (!ci->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	ci_hdrc_enter_lpm(ci, false);

	ret = ci_ulpi_resume(ci);
	if (ret)
		return ret;

	if (ci->usb_phy) {
		usb_phy_set_suspend(ci->usb_phy, 0);
		usb_phy_set_wakeup(ci->usb_phy, false);
		hw_wait_phy_stable();
	}

	ci->in_lpm = false;
	if (ci->wakeup_int) {
		ci->wakeup_int = false;
		pm_runtime_mark_last_busy(ci->dev);
		pm_runtime_put_autosuspend(ci->dev);
		enable_irq(ci->irq);
		if (ci_otg_is_fsm_mode(ci))
			ci_otg_fsm_wakeup_by_srp(ci);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ci_suspend(struct device *dev)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	if (ci->wq)
		flush_workqueue(ci->wq);
	/*
	 * Controller needs to be active during suspend, otherwise the core
	 * may run resume when the parent is at suspend if other driver's
	 * suspend fails, it occurs before parent's suspend has not started,
	 * but the core suspend has finished.
	 */
	if (ci->in_lpm)
		pm_runtime_resume(dev);

	if (ci->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	if (device_may_wakeup(dev)) {
		if (ci_otg_is_fsm_mode(ci))
			ci_otg_fsm_suspend_for_srp(ci);

		usb_phy_set_wakeup(ci->usb_phy, true);
		enable_irq_wake(ci->irq);
	}

	ci_controller_suspend(ci);

	return 0;
}

static int ci_resume(struct device *dev)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret;

	if (device_may_wakeup(dev))
		disable_irq_wake(ci->irq);

	ret = ci_controller_resume(dev);
	if (ret)
		return ret;

	if (ci->supports_runtime_pm) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static int ci_runtime_suspend(struct device *dev)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	dev_dbg(dev, "at %s\n", __func__);

	if (ci->in_lpm) {
		WARN_ON(1);
		return 0;
	}

	if (ci_otg_is_fsm_mode(ci))
		ci_otg_fsm_suspend_for_srp(ci);

	usb_phy_set_wakeup(ci->usb_phy, true);
	ci_controller_suspend(ci);

	return 0;
}

static int ci_runtime_resume(struct device *dev)
{
	return ci_controller_resume(dev);
}

#endif /* CONFIG_PM */
static const struct dev_pm_ops ci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ci_suspend, ci_resume)
	SET_RUNTIME_PM_OPS(ci_runtime_suspend, ci_runtime_resume, NULL)
};

static struct platform_driver ci_hdrc_driver = {
	.probe	= ci_hdrc_probe,
	.remove	= ci_hdrc_remove,
	.driver	= {
		.name	= "ci_hdrc",
		.pm	= &ci_pm_ops,
	},
};

static int __init ci_hdrc_platform_register(void)
{
	ci_hdrc_host_driver_init();
	return platform_driver_register(&ci_hdrc_driver);
}
module_init(ci_hdrc_platform_register);

static void __exit ci_hdrc_platform_unregister(void)
{
	platform_driver_unregister(&ci_hdrc_driver);
}
module_exit(ci_hdrc_platform_unregister);

MODULE_ALIAS("platform:ci_hdrc");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Lopo <dlopo@chipidea.mips.com>");
MODULE_DESCRIPTION("ChipIdea HDRC Driver");
