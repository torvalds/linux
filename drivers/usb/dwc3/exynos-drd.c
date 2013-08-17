/*
 * exynos-drd.c - Samsung EXYNOS USB3.0 DRD core
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/usb/exynos_usb3_drd.h>
#include <linux/platform_data/dwc3-exynos.h>

#include "exynos-drd.h"
#include "exynos-drd-switch.h"

static struct resource xhci_resources[] = {
	{
		.flags = IORESOURCE_MEM,
	},
	{
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource device_resources[] = {
	{
		.flags = IORESOURCE_MEM,
	},
	{
		.flags = IORESOURCE_IRQ,
	},
};

static void exynos_drd_change_mode(struct exynos_drd_core *core, bool host)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);

	__bic32(drd->regs + EXYNOS_USB3_GCTL,
			    EXYNOS_USB3_GCTL_PrtCapDir_MASK);

	if (host) {
		__orr32(drd->regs + EXYNOS_USB3_GCTL,
				    EXYNOS_USB3_GCTL_PrtCapDir(0x1));
	} else {
		__orr32(drd->regs + EXYNOS_USB3_GCTL,
				    EXYNOS_USB3_GCTL_PrtCapDir(0x2));
	}

	dev_dbg(drd->dev, "DRD acts as %s\n", host ? "host" : "device");
}

static u32 exynos_drd_get_evntcount(struct exynos_drd_core *core)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);

	return readl(drd->regs + EXYNOS_USB3_GEVNTCOUNT(0)) &
			EXYNOS_USB3_GEVNTCOUNTx_EVNTCount_MASK;
}

static void exynos_drd_ack_evntcount(struct exynos_drd_core *core, u32 val)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);

	writel(val, drd->regs + EXYNOS_USB3_GEVNTCOUNT(0));
}

static void exynos_drd_events_enable(struct exynos_drd_core *core, int on)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);

	if (on)
		__bic32(drd->regs + EXYNOS_USB3_GEVNTSIZ(0),
			EXYNOS_USB3_GEVNTSIZx_EvntIntMask);
	else
		__orr32(drd->regs + EXYNOS_USB3_GEVNTSIZ(0),
			EXYNOS_USB3_GEVNTSIZx_EvntIntMask);
}

static void exynos_drd_set_event_buff(struct exynos_drd_core *core,
				      dma_addr_t event_buff_dma,
				      int size)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);
	u32 reg;

	/* Event Buffer address */
	writel(0, drd->regs + EXYNOS_USB3_GEVNTADR_63_32(0));
	writel(event_buff_dma, drd->regs + EXYNOS_USB3_GEVNTADR_31_0(0));
	/* Event Buffer size */
	writel(size, drd->regs + EXYNOS_USB3_GEVNTSIZ(0));

	/* Flush any pending events */
	exynos_drd_events_enable(core, 0);

	reg = readl(drd->regs + EXYNOS_USB3_GEVNTCOUNT(0));
	writel(reg, drd->regs + EXYNOS_USB3_GEVNTCOUNT(0));

	exynos_drd_events_enable(core, 1);
}

static void exynos_drd_phy20_suspend(struct exynos_drd_core *core, int suspend)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);
	struct dwc3_exynos_data *pdata = drd->pdata;

	if (suspend && (pdata->quirks & EXYNOS_PHY20_NO_SUSPEND))
		return;

	if (suspend)
		__orr32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
			EXYNOS_USB3_GUSB2PHYCFGx_SusPHY);
	else
		__bic32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
			EXYNOS_USB3_GUSB2PHYCFGx_SusPHY);
}

static void exynos_drd_phy30_suspend(struct exynos_drd_core *core, int suspend)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);

	if (suspend)
		__orr32(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
			EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy);
	else
		__bic32(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
			EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy);
}

/*
 * exynos_drd_phy_set - intitialize the controller PHY interface
 */
static void exynos_drd_phy_set(struct exynos_drd_core *core)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);
	struct platform_device *pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data *pdata = drd->pdata;
	int err;

	/*
	 *	The reset values:
	 *	GUSB2PHYCFG(0)	= 0x00002400
	 *	GUSB3PIPECTL(0)	= 0x00260002
	 */

	__orr32(drd->regs + EXYNOS_USB3_GCTL, EXYNOS_USB3_GCTL_CoreSoftReset);
	__orr32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_PHYSoftRst);
	__orr32(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
		EXYNOS_USB3_GUSB3PIPECTLx_PHYSoftRst);

	/* PHY initialization */
	if (pdata->phy_init) {
		err = pdata->phy_init(pdev, pdata->phy_type);
		if (err)
			dev_warn(drd->dev, "PHY init failed!\n");
	} else {
		dev_warn(drd->dev, "PHY init routine is N/A\n");
	}

	if (drd->active_child && pdata->phy_tune)
		pdata->phy_tune(drd->active_child, pdata->phy_type);

	__bic32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_PHYSoftRst);
	__bic32(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
		EXYNOS_USB3_GUSB3PIPECTLx_PHYSoftRst);
	__bic32(drd->regs + EXYNOS_USB3_GCTL, EXYNOS_USB3_GCTL_CoreSoftReset);


	__bic32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_SusPHY |
		EXYNOS_USB3_GUSB2PHYCFGx_EnblSlpM |
		EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim_MASK);
	__orr32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_USBTrdTim(9));

	__bic32(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
		EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy);

	dev_vdbg(drd->dev, "GUSB2PHYCFG(0)=0x%08x, GUSB3PIPECTL(0)=0x%08x",
			    readl(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0)),
			    readl(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0)));
}

/*
 * exynos_drd_phy_unset - disable the controller PHY interface
 */
static void exynos_drd_phy_unset(struct exynos_drd_core *core)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);
	struct platform_device *pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data *pdata = drd->pdata;
	int err;

	__orr32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
		EXYNOS_USB3_GUSB2PHYCFGx_SusPHY |
		EXYNOS_USB3_GUSB2PHYCFGx_EnblSlpM);
	__orr32(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0),
		EXYNOS_USB3_GUSB3PIPECTLx_SuspSSPhy);

	if (pdata->phy_exit) {
		err = pdata->phy_exit(pdev, pdata->phy_type);
		if (err)
			dev_warn(drd->dev, "PHY exit failed!\n");
	} else {
		dev_warn(drd->dev, "PHY exit routine is N/A\n");
	}

	dev_vdbg(drd->dev, "GUSB2PHYCFG(0)=0x%08x, GUSB3PIPECTL(0)=0x%08x",
			    readl(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0)),
			    readl(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0)));
}

static void exynos_drd_config(struct exynos_drd_core *core)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);
	struct platform_device *pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data *pdata = drd->pdata;
	u32 reg;

	/* AHB bus configuration */
	writel(EXYNOS_USB3_GSBUSCFG0_INCR16BrstEna |
	       EXYNOS_USB3_GSBUSCFG0_INCRBrstEna,
	       drd->regs + EXYNOS_USB3_GSBUSCFG0);
	writel(EXYNOS_USB3_GSBUSCFG1_BREQLIMIT(3),
	       drd->regs + EXYNOS_USB3_GSBUSCFG1);

	/*
	 * WORKAROUND: DWC3 revisions from 1.90a to 2.10a have a bug
	 * For ss bulk-in data packet, when the host detects
	 * a DPP error or the internal buffer becomes full,
	 * it retries with an ACK TP Retry=1. Under the following
	 * conditions, the Retry=1 is falsely carried over to the next burst
	 * - There is only single active asynchronous SS EP at the time.
	 * - The active asynchronous EP is a Bulk IN EP.
	 * - The burst with the correctly Retry=1 ACK TP and
	 *   the next burst belong to the same transfer.
	 */
	if (drd->core.release >= 0x190a && drd->core.release <= 0x210a) {
		__orr32(drd->regs + EXYNOS_USB3_GUCTL,
			EXYNOS_USB3_GUCTL_USBHstInAutoRetryEn);

		reg = readl(drd->regs + EXYNOS_USB3_GRXTHRCFG);
		reg &= ~(EXYNOS_USB3_GRXTHRCFG_USBRxPktCnt_MASK |
			EXYNOS_USB3_GRXTHRCFG_USBMaxRxBurstSize_MASK);
		reg |= EXYNOS_USB3_GRXTHRCFG_USBRxPktCntSel |
		      EXYNOS_USB3_GRXTHRCFG_USBRxPktCnt(3) |
		      EXYNOS_USB3_GRXTHRCFG_USBMaxRxBurstSize(3);
		writel(reg, drd->regs + EXYNOS_USB3_GRXTHRCFG);
	}

	/*
	 * WORKAROUND: DWC3 revisions 2.10a and earlier have a bug
	 * The delay of the entry to a low power state such that
	 * for applications where the link stays in a non-U0 state
	 * for a short duration(< 1 microsecond),
	 * the local PHY does not enter the low power state prior
	 * to receiving a potential LFPS wakeup.
	 * This causes the PHY CDR (Clock and Data Recovery) operation
	 * to be unstable for some Synopsys PHYs.
	 * The proposal now is to change the default and the recommended value
	 * for GUSB3PIPECTL[21:19] in the RTL from 3'b100 to a minimum of 3'b001
	 */
	if (drd->core.release <= 0x210a) {
		reg = readl(drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0));
		reg &= ~(EXYNOS_USB3_GUSB3PIPECTLx_delay_p1p2p3_MASK);
		reg |= EXYNOS_USB3_GUSB3PIPECTLx_delay_p1p2p3(1);
		writel(reg, drd->regs + EXYNOS_USB3_GUSB3PIPECTL(0));
	}

	/*
	 * WORKAROUND: DWC3 revisions 2.10a and earlier have a bug
	 * Race Condition in PORTSC Write Followed by Read
	 * If the software quickly does a read to the PORTSC,
	 * some fields (port status change related fields
	 * like OCC, etc.) may not have correct value
	 * due to the current way of handling these bits.
	 * After clearing the status register (for example, OCC) bit
	 * by writing PORTSC tregister, software can insert some delay
	 * (for example, 5 mac2_clk -> UTMI clock = 60 MHz ->
	 * (16.66 ns x 5 = 84ns)) before reading the PORTSC to check status.
	 */
	if (drd->core.release <= 0x210a)
		__orr32(drd->regs + EXYNOS_USB3_GUSB2PHYCFG(0),
				EXYNOS_USB3_GUSB2PHYCFGx_PHYIf);

	/* los_bias configuration */
	if (pdata->phy_crport_ctrl) {
		pdata->phy_crport_ctrl(pdev, 0x15, 0xA409);
		pdata->phy_crport_ctrl(pdev, 0x12, 0xA000);
	}
}

static void exynos_drd_init(struct exynos_drd_core *core)
{
	struct exynos_drd *drd = container_of(core, struct exynos_drd, core);
	struct clk *sclk_usbdrd30;
	u32 susp_clk_freq;
	u32 reg;

	reg = readl(drd->regs + EXYNOS_USB3_GSNPSID);
	drd->core.release = reg & 0xffff;
	/*
	 * WORKAROUND: core revision 1.80a has a wrong release number
	 * in GSNPSID register
	 */
	if (drd->core.release == 0x131a)
		drd->core.release = 0x180a;
	dev_info(drd->dev, "Core ID Number: 0x%04x\n", reg >> 16);
	dev_info(drd->dev, "Release Number: 0x%04x\n", drd->core.release);

	/*
	 * WORKAROUND: DWC3 revisions <1.90a have a bug
	 * when The device fails to connect at SuperSpeed
	 * and falls back to high-speed mode which causes
	 * the device to enter in a Connect/Disconnect loop
	 */
	if (drd->core.release < 0x190a)
		__orr32(drd->regs + EXYNOS_USB3_GCTL,
			EXYNOS_USB3_GCTL_U2RSTECN);

	reg = readl(drd->regs + EXYNOS_USB3_GCTL);
	reg &= ~(EXYNOS_USB3_GCTL_PwrDnScale_MASK |
		EXYNOS_USB3_GCTL_DisScramble |
		EXYNOS_USB3_GCTL_RAMClkSel_MASK);

	sclk_usbdrd30 = clk_get(drd->dev, "sclk_usbdrd30");
	if (IS_ERR(sclk_usbdrd30)) {
		dev_err(drd->dev, "Failed to get sclk_usbdrd30 clock\n");
	} else {
		susp_clk_freq = clk_get_rate(sclk_usbdrd30);
		/* suspend clk should be set between 32 kHz and 125 MHz */
		if (susp_clk_freq < 32000 || susp_clk_freq > 125000000)
			dev_err(drd->dev,
				"sclk_usbdrd3 clock rate is out of range %d\n",
				susp_clk_freq);
		else
			/* Power Down Scale = suspend_clk_freq / 16kHz */
			reg |= EXYNOS_USB3_GCTL_PwrDnScale(susp_clk_freq/16000);
	}

	reg |= EXYNOS_USB3_GCTL_U2RSTECN;

	writel(reg, drd->regs + EXYNOS_USB3_GCTL);
}

static struct exynos_drd_core_ops core_ops = {
	.core_init	= exynos_drd_init,
	.config		= exynos_drd_config,
	.change_mode	= exynos_drd_change_mode,
	.phy_set	= exynos_drd_phy_set,
	.phy_unset	= exynos_drd_phy_unset,
	.phy20_suspend	= exynos_drd_phy20_suspend,
	.phy30_suspend	= exynos_drd_phy30_suspend,
	.set_event_buff	= exynos_drd_set_event_buff,
	.events_enable	= exynos_drd_events_enable,
	.get_evntcount	= exynos_drd_get_evntcount,
	.ack_evntcount	= exynos_drd_ack_evntcount,
};

void exynos_drd_put(struct platform_device *child)
{
	struct device *dev = child->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_drd *drd = platform_get_drvdata(pdev);

	spin_lock(&drd->lock);
	drd->active_child = NULL;
	spin_unlock(&drd->lock);

	dev_dbg(dev, "DRD released by %s\n", dev_name(&child->dev));
}

int exynos_drd_try_get(struct platform_device *child)
{
	struct device *dev = child->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_drd *drd = platform_get_drvdata(pdev);
	int ret = 0;

	spin_lock(&drd->lock);
	if (!drd->active_child)
		drd->active_child = child;
	else
		ret = -EBUSY;
	spin_unlock(&drd->lock);

	dev_dbg(dev, "DRD %s %s\n",
		ret < 0 ? "busy for" : "acquired by", dev_name(&child->dev));

	return ret;
}

struct exynos_drd_core *exynos_drd_bind(struct platform_device *child)
{
	struct device *dev = child->dev.parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_drd *drd = platform_get_drvdata(pdev);

	return &drd->core;
}

static int exynos_drd_create_udc(struct exynos_drd *drd)
{
	struct platform_device	*pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data	*pdata = drd->pdata;
	struct platform_device	*udc;
	const char		*udc_name;
	int			ret = -ENOMEM;

	if (pdata->quirks & SKIP_UDC)
		return 0;

	if (pdata->udc_name) {
		udc_name = pdata->udc_name;
	} else {
		dev_err(drd->dev, "udc name is not available\n");
		return -EINVAL;
	}

	udc = platform_device_alloc(udc_name, pdev->id);
	if (!udc) {
		dev_err(drd->dev, "couldn't allocate udc device (%d)\n",
				    pdev->id);
		goto err0;
	}

	drd->udc = udc;

	dma_set_coherent_mask(&udc->dev, pdev->dev.coherent_dma_mask);
	udc->dev.platform_data = pdev->dev.platform_data;
	udc->dev.parent = &pdev->dev;
	udc->dev.dma_mask = pdev->dev.dma_mask;
	udc->dev.dma_parms = pdev->dev.dma_parms;

	device_resources[0].start = drd->res->start + EXYNOS_USB3_DEV_REG_START;
	device_resources[0].end = drd->res->start + EXYNOS_USB3_DEV_REG_END;
	device_resources[1].start = drd->irq;

	ret = platform_device_add_resources(udc, device_resources,
			ARRAY_SIZE(device_resources));
	if (ret) {
		dev_err(&pdev->dev, "couldn't add resources to udc device\n");
		goto err1;
	}

	ret = platform_device_add(udc);
	if (ret) {
		dev_err(&pdev->dev, "failed to register udc device\n");
		goto err1;
	}

	dev_dbg(drd->dev, "udc %s created successfully\n", dev_name(&udc->dev));

	return 0;

err1:
	platform_device_put(udc);
err0:
	return ret;
}

static int exynos_drd_create_xhci(struct exynos_drd *drd)
{
	struct platform_device	*pdev = to_platform_device(drd->dev);
	struct dwc3_exynos_data	*pdata = drd->pdata;
	struct platform_device	*xhci;
	const char		*xhci_name;
	int			ret = -ENOMEM;

	if (pdata->quirks & SKIP_XHCI)
		return 0;

	if (pdata->xhci_name) {
		xhci_name = pdata->xhci_name;
	} else {
		dev_err(drd->dev, "xhci device name is not available\n");
		return -EINVAL;
	}

	xhci = platform_device_alloc(xhci_name, pdev->id);
	if (!xhci) {
		dev_err(&pdev->dev, "couldn't allocate xhci device (%d)\n",
				    pdev->id);
		goto err0;
	}

	drd->xhci = xhci;

	dma_set_coherent_mask(&xhci->dev, pdev->dev.coherent_dma_mask);
	xhci->dev.platform_data = pdev->dev.platform_data;
	xhci->dev.parent = &pdev->dev;
	xhci->dev.dma_mask = pdev->dev.dma_mask;
	xhci->dev.dma_parms = pdev->dev.dma_parms;

	xhci_resources[0].start = drd->res->start + EXYNOS_USB3_XHCI_REG_START;
	xhci_resources[0].end = drd->res->start + EXYNOS_USB3_XHCI_REG_END;
	xhci_resources[1].start = drd->irq;

	ret = platform_device_add_resources(xhci, xhci_resources,
			ARRAY_SIZE(xhci_resources));
	if (ret) {
		dev_err(&pdev->dev, "couldn't add resources to xhci device\n");
		goto err1;
	}

	ret = platform_device_add(xhci);
	if (ret) {
		dev_err(&pdev->dev, "failed to register xhci device\n");
		goto err1;
	}

	dev_dbg(drd->dev, "xhci %s created successfully\n", dev_name(&xhci->dev));

	return 0;

err1:
	platform_device_put(xhci);
err0:
	return ret;
}

static int __devinit exynos_drd_probe(struct platform_device *pdev)
{
	struct dwc3_exynos_data *pdata = pdev->dev.platform_data;
	struct device		*dev = &pdev->dev;
	struct exynos_drd	*drd;
	int			ret = -ENOMEM;

	if (!pdata) {
		dev_err(dev, "cannot get platform data\n");
		return -ENODEV;
	}

	drd = devm_kzalloc(dev, sizeof(*drd), GFP_KERNEL);
	if (!drd) {
		dev_err(dev, "not enough memory\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, drd);
	drd->dev = dev;
	drd->pdata = pdata;
	drd->core.ops = &core_ops;

	spin_lock_init(&drd->lock);

	drd->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!drd->res) {
		dev_err(dev, "cannot find register resource 0\n");
		return -ENXIO;
	}

	ret = platform_get_irq(pdev, 0);
	if (drd->irq < 0) {
		dev_err(dev, "cannot find irq\n");
		return ret;
	}

	drd->irq = ret;

	/* Set global resources */

	drd->glob_res.start = drd->res->start + EXYNOS_USB3_GLOB_REG_START;
	drd->glob_res.end = drd->res->start + EXYNOS_USB3_GLOB_REG_END;
	drd->glob_res.flags = IORESOURCE_MEM;

	if (!devm_request_mem_region(dev, drd->glob_res.start,
				     resource_size(&drd->glob_res),
				     dev_name(dev))) {
		dev_err(dev, "cannot reserve registers\n");
		return -ENOENT;
	}

	drd->regs = devm_ioremap_nocache(dev, drd->glob_res.start,
					 resource_size(&drd->glob_res));
	if (!drd->regs) {
		dev_err(dev, "cannot map registers\n");
		return -ENXIO;
	}
	drd->regs -= EXYNOS_USB3_GLOB_REG_START;

	drd->clk = clk_get(dev, "usbdrd30");
	if (IS_ERR(drd->clk)) {
		dev_err(dev, "cannot get DRD clock\n");
		return -EINVAL;
	}

	clk_enable(drd->clk);
	exynos_drd_init(&drd->core);
	exynos_drd_phy_set(&drd->core);

	if (pdata->quirks & DUMMY_DRD) {
		dev_info(dev, "dummy\n");
		goto done;
	}

	ret = exynos_drd_switch_init(drd);
	if (ret < 0)
		goto err_sw;

	ret = exynos_drd_create_udc(drd);
	if (ret < 0)
		goto err_udc;

	ret = exynos_drd_create_xhci(drd);
	if (ret < 0)
		goto err_xhci;

done:
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

err_xhci:
	platform_device_unregister(drd->udc);
err_udc:
	exynos_drd_switch_exit(drd);
err_sw:
	clk_disable(drd->clk);
	clk_put(drd->clk);

	return ret;
}

static int __devexit exynos_drd_remove(struct platform_device *pdev)
{
	struct exynos_drd	*drd = platform_get_drvdata(pdev);

	/*
	 * Currently this driver doesn't support module removing and
	 * must be compiled as build-in driver. The module usage count
	 * control should be implemented to enable rmmod.
	 */

	platform_device_unregister(drd->xhci);
	platform_device_unregister(drd->udc);
	exynos_drd_switch_exit(drd);

	exynos_drd_phy_unset(&drd->core);
	clk_disable(drd->clk);
	clk_put(drd->clk);

	return 0;
}

static const struct platform_device_id exynos_drd_ids[] = {
	{ "exynos-drd", 0 },
	{ "exynos-dwc3", 0 },
	{ }
};

#ifdef CONFIG_PM
static int exynos_drd_suspend(struct device *dev)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);

#ifdef CONFIG_PM_RUNTIME
	dev_dbg(dev, "%s: usage_count = %d\n",
		      __func__, atomic_read(&dev->power.usage_count));

	if (pm_runtime_suspended(dev)) {
		dev_dbg(dev, "DRD is runtime suspended\n");
		return 0;
	}
#endif
	disable_irq(drd->irq);
	exynos_drd_phy_unset(&drd->core);
	clk_disable(drd->clk);

	return 0;
}

static int exynos_drd_resume(struct device *dev)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);

#ifdef CONFIG_PM_RUNTIME
	dev_dbg(dev, "%s: usage_count = %d\n",
		      __func__, atomic_read(&dev->power.usage_count));
#endif

	clk_enable(drd->clk);
	exynos_drd_phy_set(&drd->core);
	exynos_drd_init(&drd->core);
	/* We are starting from scratch */
	drd->active_child = NULL;
	exynos_drd_switch_reset(drd, 1);

	/* Update runtime PM status and clear runtime_error */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	enable_irq(drd->irq);

	return 0;
}
#else
#define exynos_drd_suspend NULL
#define exynos_drd_resume NULL
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME
static int exynos_drd_runtime_suspend(struct device *dev)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	disable_irq(drd->irq);
	exynos_drd_phy_unset(&drd->core);
	clk_disable(drd->clk);
	return 0;
}

static int exynos_drd_runtime_resume(struct device *dev)
{
	struct exynos_drd *drd = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	if (dev->power.is_suspended) {
		dev_dbg(dev, "DRD is system suspended\n");
		return 0;
	}
	clk_enable(drd->clk);
	exynos_drd_phy_set(&drd->core);
	enable_irq(drd->irq);

	return 0;
}
#else
#define exynos_drd_runtime_suspend NULL
#define exynos_drd_runtime_resume NULL
#endif

static const struct dev_pm_ops exynos_drd_pm_ops = {
	.suspend		= exynos_drd_suspend,
	.resume			= exynos_drd_resume,
	.runtime_suspend	= exynos_drd_runtime_suspend,
	.runtime_resume		= exynos_drd_runtime_resume,
};

static struct platform_driver exynos_drd_driver = {
	.probe		= exynos_drd_probe,
	.remove		= __devexit_p(exynos_drd_remove),
	.driver		= {
		.name	= "exynos-drd",
		.owner	= THIS_MODULE,
		.pm	= &exynos_drd_pm_ops,
	},
	.id_table	= exynos_drd_ids,
};

module_platform_driver(exynos_drd_driver);

MODULE_AUTHOR("Anton Tikhomirov <av.tikhomirov@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung Exynos USB3.0 DRD core");
