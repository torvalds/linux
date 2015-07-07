/*
 * Copyright © 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __BRCMNAND_H__
#define __BRCMNAND_H__

#include <linux/types.h>
#include <linux/io.h>

struct platform_device;
struct dev_pm_ops;

struct brcmnand_soc {
	struct platform_device *pdev;
	void *priv;
	bool (*ctlrdy_ack)(struct brcmnand_soc *soc);
	void (*ctlrdy_set_enabled)(struct brcmnand_soc *soc, bool en);
	void (*prepare_data_bus)(struct brcmnand_soc *soc, bool prepare);
};

static inline void brcmnand_soc_data_bus_prepare(struct brcmnand_soc *soc)
{
	if (soc && soc->prepare_data_bus)
		soc->prepare_data_bus(soc, true);
}

static inline void brcmnand_soc_data_bus_unprepare(struct brcmnand_soc *soc)
{
	if (soc && soc->prepare_data_bus)
		soc->prepare_data_bus(soc, false);
}

static inline u32 brcmnand_readl(void __iomem *addr)
{
	/*
	 * MIPS endianness is configured by boot strap, which also reverses all
	 * bus endianness (i.e., big-endian CPU + big endian bus ==> native
	 * endian I/O).
	 *
	 * Other architectures (e.g., ARM) either do not support big endian, or
	 * else leave I/O in little endian mode.
	 */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(__BIG_ENDIAN))
		return __raw_readl(addr);
	else
		return readl_relaxed(addr);
}

static inline void brcmnand_writel(u32 val, void __iomem *addr)
{
	/* See brcmnand_readl() comments */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(__BIG_ENDIAN))
		__raw_writel(val, addr);
	else
		writel_relaxed(val, addr);
}

int brcmnand_probe(struct platform_device *pdev, struct brcmnand_soc *soc);
int brcmnand_remove(struct platform_device *pdev);

extern const struct dev_pm_ops brcmnand_pm_ops;

#endif /* __BRCMNAND_H__ */
