// SPDX-License-Identifier: GPL-2.0
/*
 * ulpi.c - DesignWare USB3 Controller's ULPI PHY interface
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/ulpi/regs.h>

#include "core.h"
#include "io.h"

#define DWC3_ULPI_ADDR(a) \
		((a >= ULPI_EXT_VENDOR_SPECIFIC) ? \
		DWC3_GUSB2PHYACC_ADDR(ULPI_ACCESS_EXTENDED) | \
		DWC3_GUSB2PHYACC_EXTEND_ADDR(a) : DWC3_GUSB2PHYACC_ADDR(a))

static int dwc3_ulpi_busyloop(struct dwc3 *dwc)
{
	unsigned int count = 1000;
	u32 reg;

	while (count--) {
		reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYACC(0));
		if (!(reg & DWC3_GUSB2PHYACC_BUSY))
			return 0;
		cpu_relax();
	}

	return -ETIMEDOUT;
}

static int dwc3_ulpi_read(struct device *dev, u8 addr)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	u32 reg;
	int ret;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
	if (reg & DWC3_GUSB2PHYCFG_SUSPHY) {
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
		dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
	}

	reg = DWC3_GUSB2PHYACC_NEWREGREQ | DWC3_ULPI_ADDR(addr);
	dwc3_writel(dwc->regs, DWC3_GUSB2PHYACC(0), reg);

	ret = dwc3_ulpi_busyloop(dwc);
	if (ret)
		return ret;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYACC(0));

	return DWC3_GUSB2PHYACC_DATA(reg);
}

static int dwc3_ulpi_write(struct device *dev, u8 addr, u8 val)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GUSB2PHYCFG(0));
	if (reg & DWC3_GUSB2PHYCFG_SUSPHY) {
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
		dwc3_writel(dwc->regs, DWC3_GUSB2PHYCFG(0), reg);
	}

	reg = DWC3_GUSB2PHYACC_NEWREGREQ | DWC3_ULPI_ADDR(addr);
	reg |= DWC3_GUSB2PHYACC_WRITE | val;
	dwc3_writel(dwc->regs, DWC3_GUSB2PHYACC(0), reg);

	return dwc3_ulpi_busyloop(dwc);
}

static const struct ulpi_ops dwc3_ulpi_ops = {
	.read = dwc3_ulpi_read,
	.write = dwc3_ulpi_write,
};

int dwc3_ulpi_init(struct dwc3 *dwc)
{
	/* Register the interface */
	dwc->ulpi = ulpi_register_interface(dwc->dev, &dwc3_ulpi_ops);
	if (IS_ERR(dwc->ulpi)) {
		dev_err(dwc->dev, "failed to register ULPI interface");
		return PTR_ERR(dwc->ulpi);
	}

	return 0;
}

void dwc3_ulpi_exit(struct dwc3 *dwc)
{
	if (dwc->ulpi) {
		ulpi_unregister_interface(dwc->ulpi);
		dwc->ulpi = NULL;
	}
}
