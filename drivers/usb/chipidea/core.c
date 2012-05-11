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
 * - CONFIG_USB_GADGET_DEBUG_FILES: enable debug facilities
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
 * - OTG
 * - Isochronous & Interrupt Traffic
 * - Handle requests which spawns into several TDs
 * - GET_STATUS(device) - always reports 0
 * - Gadget API (majority of optional features)
 * - Suspend & Remote Wakeup
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/chipidea.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "debug.h"

/* MSM specific */
#define ABS_AHBBURST        (0x0090UL)
#define ABS_AHBMODE         (0x0098UL)
/* UDC register map */
static uintptr_t ci_regs_nolpm[] = {
	[CAP_CAPLENGTH]		= 0x000UL,
	[CAP_HCCPARAMS]		= 0x008UL,
	[CAP_DCCPARAMS]		= 0x024UL,
	[CAP_TESTMODE]		= 0x038UL,
	[OP_USBCMD]		= 0x000UL,
	[OP_USBSTS]		= 0x004UL,
	[OP_USBINTR]		= 0x008UL,
	[OP_DEVICEADDR]		= 0x014UL,
	[OP_ENDPTLISTADDR]	= 0x018UL,
	[OP_PORTSC]		= 0x044UL,
	[OP_DEVLC]		= 0x084UL,
	[OP_USBMODE]		= 0x068UL,
	[OP_ENDPTSETUPSTAT]	= 0x06CUL,
	[OP_ENDPTPRIME]		= 0x070UL,
	[OP_ENDPTFLUSH]		= 0x074UL,
	[OP_ENDPTSTAT]		= 0x078UL,
	[OP_ENDPTCOMPLETE]	= 0x07CUL,
	[OP_ENDPTCTRL]		= 0x080UL,
};

static uintptr_t ci_regs_lpm[] = {
	[CAP_CAPLENGTH]		= 0x000UL,
	[CAP_HCCPARAMS]		= 0x008UL,
	[CAP_DCCPARAMS]		= 0x024UL,
	[CAP_TESTMODE]		= 0x0FCUL,
	[OP_USBCMD]		= 0x000UL,
	[OP_USBSTS]		= 0x004UL,
	[OP_USBINTR]		= 0x008UL,
	[OP_DEVICEADDR]		= 0x014UL,
	[OP_ENDPTLISTADDR]	= 0x018UL,
	[OP_PORTSC]		= 0x044UL,
	[OP_DEVLC]		= 0x084UL,
	[OP_USBMODE]		= 0x0C8UL,
	[OP_ENDPTSETUPSTAT]	= 0x0D8UL,
	[OP_ENDPTPRIME]		= 0x0DCUL,
	[OP_ENDPTFLUSH]		= 0x0E0UL,
	[OP_ENDPTSTAT]		= 0x0E4UL,
	[OP_ENDPTCOMPLETE]	= 0x0E8UL,
	[OP_ENDPTCTRL]		= 0x0ECUL,
};

static int hw_alloc_regmap(struct ci13xxx *udc, bool is_lpm)
{
	int i;

	kfree(udc->hw_bank.regmap);

	udc->hw_bank.regmap = kzalloc((OP_LAST + 1) * sizeof(void *),
				      GFP_KERNEL);
	if (!udc->hw_bank.regmap)
		return -ENOMEM;

	for (i = 0; i < OP_ENDPTCTRL; i++)
		udc->hw_bank.regmap[i] =
			(i <= CAP_LAST ? udc->hw_bank.cap : udc->hw_bank.op) +
			(is_lpm ? ci_regs_lpm[i] : ci_regs_nolpm[i]);

	for (; i <= OP_LAST; i++)
		udc->hw_bank.regmap[i] = udc->hw_bank.op +
			4 * (i - OP_ENDPTCTRL) +
			(is_lpm
			 ? ci_regs_lpm[OP_ENDPTCTRL]
			 : ci_regs_nolpm[OP_ENDPTCTRL]);

	return 0;
}

/**
 * hw_port_test_set: writes port test mode (execute without interruption)
 * @mode: new value
 *
 * This function returns an error code
 */
int hw_port_test_set(struct ci13xxx *ci, u8 mode)
{
	const u8 TEST_MODE_MAX = 7;

	if (mode > TEST_MODE_MAX)
		return -EINVAL;

	hw_write(ci, OP_PORTSC, PORTSC_PTC, mode << ffs_nr(PORTSC_PTC));
	return 0;
}

/**
 * hw_port_test_get: reads port test mode value
 *
 * This function returns port test mode value
 */
u8 hw_port_test_get(struct ci13xxx *ci)
{
	return hw_read(ci, OP_PORTSC, PORTSC_PTC) >> ffs_nr(PORTSC_PTC);
}

int hw_device_init(struct ci13xxx *udc, void __iomem *base,
		   uintptr_t cap_offset)
{
	u32 reg;

	/* bank is a module variable */
	udc->hw_bank.abs = base;

	udc->hw_bank.cap = udc->hw_bank.abs;
	udc->hw_bank.cap += cap_offset;
	udc->hw_bank.op = udc->hw_bank.cap + ioread8(udc->hw_bank.cap);

	hw_alloc_regmap(udc, false);
	reg = hw_read(udc, CAP_HCCPARAMS, HCCPARAMS_LEN) >>
		ffs_nr(HCCPARAMS_LEN);
	udc->hw_bank.lpm  = reg;
	hw_alloc_regmap(udc, !!reg);
	udc->hw_bank.size = udc->hw_bank.op - udc->hw_bank.abs;
	udc->hw_bank.size += OP_LAST;
	udc->hw_bank.size /= sizeof(u32);

	reg = hw_read(udc, CAP_DCCPARAMS, DCCPARAMS_DEN) >>
		ffs_nr(DCCPARAMS_DEN);
	udc->hw_ep_max = reg * 2;   /* cache hw ENDPT_MAX */

	if (udc->hw_ep_max == 0 || udc->hw_ep_max > ENDPT_MAX)
		return -ENODEV;

	dev_dbg(udc->dev, "ChipIdea UDC found, lpm: %d; cap: %p op: %p\n",
		udc->hw_bank.lpm, udc->hw_bank.cap, udc->hw_bank.op);

	/* setup lock mode ? */

	/* ENDPTSETUPSTAT is '0' by default */

	/* HCSPARAMS.bf.ppc SHOULD BE zero for device */

	return 0;
}

/**
 * hw_device_reset: resets chip (execute without interruption)
 * @ci: the controller
  *
 * This function returns an error code
 */
int hw_device_reset(struct ci13xxx *ci)
{
	/* should flush & stop before reset */
	hw_write(ci, OP_ENDPTFLUSH, ~0, ~0);
	hw_write(ci, OP_USBCMD, USBCMD_RS, 0);

	hw_write(ci, OP_USBCMD, USBCMD_RST, USBCMD_RST);
	while (hw_read(ci, OP_USBCMD, USBCMD_RST))
		udelay(10);		/* not RTOS friendly */


	if (ci->udc_driver->notify_event)
		ci->udc_driver->notify_event(ci,
			CI13XXX_CONTROLLER_RESET_EVENT);

	if (ci->udc_driver->flags & CI13XXX_DISABLE_STREAMING)
		hw_write(ci, OP_USBMODE, USBMODE_SDIS, USBMODE_SDIS);

	/* USBMODE should be configured step by step */
	hw_write(ci, OP_USBMODE, USBMODE_CM, USBMODE_CM_IDLE);
	hw_write(ci, OP_USBMODE, USBMODE_CM, USBMODE_CM_DEVICE);
	/* HW >= 2.3 */
	hw_write(ci, OP_USBMODE, USBMODE_SLOM, USBMODE_SLOM);

	if (hw_read(ci, OP_USBMODE, USBMODE_CM) != USBMODE_CM_DEVICE) {
		pr_err("cannot enter in device mode");
		pr_err("lpm = %i", ci->hw_bank.lpm);
		return -ENODEV;
	}

	return 0;
}

static int __devinit ci_udc_probe(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct ci13xxx_udc_driver *driver = dev->platform_data;
	struct ci13xxx	*udc;
	struct resource	*res;
	void __iomem	*base;
	int		ret;

	if (!driver) {
		dev_err(dev, "platform data missing\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing resource\n");
		return -ENODEV;
	}

	base = devm_request_and_ioremap(dev, res);
	if (!res) {
		dev_err(dev, "can't request and ioremap resource\n");
		return -ENOMEM;
	}

	ret = udc_probe(driver, dev, base, &udc);
	if (ret)
		return ret;

	udc->irq = platform_get_irq(pdev, 0);
	if (udc->irq < 0) {
		dev_err(dev, "missing IRQ\n");
		ret = -ENODEV;
		goto out;
	}

	platform_set_drvdata(pdev, udc);
	ret = request_irq(udc->irq, udc_irq, IRQF_SHARED, driver->name, udc);

out:
	if (ret)
		udc_remove(udc);

	return ret;
}

static int __devexit ci_udc_remove(struct platform_device *pdev)
{
	struct ci13xxx *udc = platform_get_drvdata(pdev);

	free_irq(udc->irq, udc);
	udc_remove(udc);

	return 0;
}

static struct platform_driver ci_udc_driver = {
	.probe	= ci_udc_probe,
	.remove	= __devexit_p(ci_udc_remove),
	.driver	= {
		.name	= "ci_udc",
	},
};

module_platform_driver(ci_udc_driver);

MODULE_ALIAS("platform:ci_udc");
MODULE_ALIAS("platform:ci13xxx");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Lopo <dlopo@chipidea.mips.com>");
MODULE_DESCRIPTION("ChipIdea UDC Driver");
