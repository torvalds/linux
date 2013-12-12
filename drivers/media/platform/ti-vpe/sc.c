/*
 * Scaler library
 *
 * Copyright (c) 2013 Texas Instruments Inc.
 *
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Archit Taneja, <archit@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "sc.h"

void sc_set_regs_bypass(struct sc_data *sc, u32 *sc_reg0)
{
	*sc_reg0 |= CFG_SC_BYPASS;
}

void sc_dump_regs(struct sc_data *sc)
{
	struct device *dev = &sc->pdev->dev;

	u32 read_reg(struct sc_data *sc, int offset)
	{
		return ioread32(sc->base + offset);
	}

#define DUMPREG(r) dev_dbg(dev, "%-35s %08x\n", #r, read_reg(sc, CFG_##r))

	DUMPREG(SC0);
	DUMPREG(SC1);
	DUMPREG(SC2);
	DUMPREG(SC3);
	DUMPREG(SC4);
	DUMPREG(SC5);
	DUMPREG(SC6);
	DUMPREG(SC8);
	DUMPREG(SC9);
	DUMPREG(SC10);
	DUMPREG(SC11);
	DUMPREG(SC12);
	DUMPREG(SC13);
	DUMPREG(SC17);
	DUMPREG(SC18);
	DUMPREG(SC19);
	DUMPREG(SC20);
	DUMPREG(SC21);
	DUMPREG(SC22);
	DUMPREG(SC23);
	DUMPREG(SC24);
	DUMPREG(SC25);

#undef DUMPREG
}

struct sc_data *sc_create(struct platform_device *pdev)
{
	struct sc_data *sc;

	dev_dbg(&pdev->dev, "sc_create\n");

	sc = devm_kzalloc(&pdev->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc) {
		dev_err(&pdev->dev, "couldn't alloc sc_data\n");
		return ERR_PTR(-ENOMEM);
	}

	sc->pdev = pdev;

	sc->res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sc");
	if (!sc->res) {
		dev_err(&pdev->dev, "missing platform resources data\n");
		return ERR_PTR(-ENODEV);
	}

	sc->base = devm_ioremap_resource(&pdev->dev, sc->res);
	if (!sc->base) {
		dev_err(&pdev->dev, "failed to ioremap\n");
		return ERR_PTR(-ENOMEM);
	}

	return sc;
}
