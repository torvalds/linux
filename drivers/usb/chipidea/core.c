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
 * - CONFIG_USB_CHIPIDEA_DEBUG: enable debug facilities
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
#include <linux/phy.h>
#include <linux/regulator/consumer.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "host.h"
#include "debug.h"
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

static int hw_alloc_regmap(struct ci_hdrc *ci, bool is_lpm)
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

	return 0;
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

	dev_dbg(ci->dev, "ChipIdea HDRC found, lpm: %d; cap: %p op: %p\n",
		ci->hw_bank.lpm, ci->hw_bank.cap, ci->hw_bank.op);

	/* setup lock mode ? */

	/* ENDPTSETUPSTAT is '0' by default */

	/* HCSPARAMS.bf.ppc SHOULD BE zero for device */

	return 0;
}

static void hw_phymode_configure(struct ci_hdrc *ci)
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

	if (ci->platdata->notify_event)
		ci->platdata->notify_event(ci,
			CI_HDRC_CONTROLLER_RESET_EVENT);

	if (ci->platdata->flags & CI_HDRC_DISABLE_STREAMING)
		hw_write(ci, OP_USBMODE, USBMODE_CI_SDIS, USBMODE_CI_SDIS);

	if (ci->platdata->flags & CI_HDRC_FORCE_FULLSPEED) {
		if (ci->hw_bank.lpm)
			hw_write(ci, OP_DEVLC, DEVLC_PFSC, DEVLC_PFSC);
		else
			hw_write(ci, OP_PORTSC, PORTSC_PFSC, PORTSC_PFSC);
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

	return 0;
}

/**
 * hw_wait_reg: wait the register value
 *
 * Sometimes, it needs to wait register value before going on.
 * Eg, when switch to device mode, the vbus value should be lower
 * than OTGSC_BSV before connects to host.
 *
 * @ci: the controller
 * @reg: register index
 * @mask: mast bit
 * @value: the bit value to wait
 * @timeout_ms: timeout in millisecond
 *
 * This function returns an error code if timeout
 */
int hw_wait_reg(struct ci_hdrc *ci, enum ci_hw_regs reg, u32 mask,
				u32 value, unsigned int timeout_ms)
{
	unsigned long elapse = jiffies + msecs_to_jiffies(timeout_ms);

	while (hw_read(ci, reg, mask) != value) {
		if (time_after(jiffies, elapse)) {
			dev_err(ci->dev, "timeout waiting for %08x in %d\n",
					mask, reg);
			return -ETIMEDOUT;
		}
		msleep(20);
	}

	return 0;
}

static irqreturn_t ci_irq(int irq, void *data)
{
	struct ci_hdrc *ci = data;
	irqreturn_t ret = IRQ_NONE;
	u32 otgsc = 0;

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

static int ci_get_platdata(struct device *dev,
		struct ci_hdrc_platform_data *platdata)
{
	if (!platdata->phy_mode)
		platdata->phy_mode = of_usb_get_phy_mode(dev->of_node);

	if (!platdata->dr_mode)
		platdata->dr_mode = of_usb_get_dr_mode(dev->of_node);

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

	if (of_usb_get_maximum_speed(dev->of_node) == USB_SPEED_FULL)
		platdata->flags |= CI_HDRC_FORCE_FULLSPEED;

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
	pdev->dev.dma_mask = dev->dma_mask;
	pdev->dev.dma_parms = dev->dma_parms;
	dma_set_coherent_mask(&pdev->dev, dev->coherent_dma_mask);

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
	if (ci->is_otg)
		dev_dbg(ci->dev, "It is OTG capable controller\n");
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

	ci->dev = dev;
	ci->platdata = dev_get_platdata(dev);
	ci->imx28_write_fix = !!(ci->platdata->flags &
		CI_HDRC_IMX28_WRITE_FIX);

	ret = hw_device_init(ci, base);
	if (ret < 0) {
		dev_err(dev, "can't initialize hardware\n");
		return -ENODEV;
	}

	if (ci->platdata->phy) {
		ci->phy = ci->platdata->phy;
	} else if (ci->platdata->usb_phy) {
		ci->usb_phy = ci->platdata->usb_phy;
	} else {
		ci->phy = devm_phy_get(dev->parent, "usb-phy");
		ci->usb_phy = devm_usb_get_phy(dev->parent, USB_PHY_TYPE_USB2);

		/* if both generic PHY and USB PHY layers aren't enabled */
		if (PTR_ERR(ci->phy) == -ENOSYS &&
				PTR_ERR(ci->usb_phy) == -ENXIO)
			return -ENXIO;

		if (IS_ERR(ci->phy) && IS_ERR(ci->usb_phy))
			return -EPROBE_DEFER;

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
		/* Disable and clear all OTG irq */
		hw_write_otgsc(ci, OTGSC_INT_EN_BITS | OTGSC_INT_STATUS_BITS,
							OTGSC_INT_STATUS_BITS);
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

	/* only update vbus status for peripheral */
	if (ci->role == CI_ROLE_GADGET)
		ci_handle_vbus_change(ci);

	if (!ci_otg_is_fsm_mode(ci)) {
		ret = ci_role_start(ci, ci->role);
		if (ret) {
			dev_err(dev, "can't start %s role\n",
						ci_role(ci)->name);
			goto stop;
		}
	}

	platform_set_drvdata(pdev, ci);
	ret = devm_request_irq(dev, ci->irq, ci_irq, IRQF_SHARED,
			ci->platdata->name, ci);
	if (ret)
		goto stop;

	if (ci_otg_is_fsm_mode(ci))
		ci_hdrc_otg_fsm_start(ci);

	ret = dbg_create_files(ci);
	if (!ret)
		return 0;

stop:
	ci_role_destroy(ci);
deinit_phy:
	ci_usb_phy_exit(ci);

	return ret;
}

static int ci_hdrc_remove(struct platform_device *pdev)
{
	struct ci_hdrc *ci = platform_get_drvdata(pdev);

	dbg_remove_files(ci);
	ci_role_destroy(ci);
	ci_hdrc_enter_lpm(ci, true);
	ci_usb_phy_exit(ci);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void ci_controller_suspend(struct ci_hdrc *ci)
{
	ci_hdrc_enter_lpm(ci, true);

	if (ci->usb_phy)
		usb_phy_set_suspend(ci->usb_phy, 1);
}

static int ci_controller_resume(struct device *dev)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	dev_dbg(dev, "at %s\n", __func__);

	ci_hdrc_enter_lpm(ci, false);

	if (ci->usb_phy) {
		usb_phy_set_suspend(ci->usb_phy, 0);
		usb_phy_set_wakeup(ci->usb_phy, false);
		hw_wait_phy_stable();
	}

	return 0;
}

static int ci_suspend(struct device *dev)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);

	if (ci->wq)
		flush_workqueue(ci->wq);

	ci_controller_suspend(ci);

	return 0;
}

static int ci_resume(struct device *dev)
{
	return ci_controller_resume(dev);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops ci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ci_suspend, ci_resume)
};
static struct platform_driver ci_hdrc_driver = {
	.probe	= ci_hdrc_probe,
	.remove	= ci_hdrc_remove,
	.driver	= {
		.name	= "ci_hdrc",
		.pm	= &ci_pm_ops,
	},
};

module_platform_driver(ci_hdrc_driver);

MODULE_ALIAS("platform:ci_hdrc");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Lopo <dlopo@chipidea.mips.com>");
MODULE_DESCRIPTION("ChipIdea HDRC Driver");
