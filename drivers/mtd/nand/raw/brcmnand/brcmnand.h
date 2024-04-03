/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2015 Broadcom Corporation
 */

#ifndef __BRCMNAND_H__
#define __BRCMNAND_H__

#include <linux/types.h>
#include <linux/io.h>

struct platform_device;
struct dev_pm_ops;
struct brcmnand_io_ops;

/* Special register offset constant to intercept a non-MMIO access
 * to the flash cache register space. This is intentionally large
 * not to overlap with an existing offset.
 */
#define BRCMNAND_NON_MMIO_FC_ADDR	0xffffffff

struct brcmnand_soc {
	bool (*ctlrdy_ack)(struct brcmnand_soc *soc);
	void (*ctlrdy_set_enabled)(struct brcmnand_soc *soc, bool en);
	void (*prepare_data_bus)(struct brcmnand_soc *soc, bool prepare,
				 bool is_param);
	void (*read_data_bus)(struct brcmnand_soc *soc, void __iomem *flash_cache,
			      u32 *buffer, int fc_words);
	const struct brcmnand_io_ops *ops;
};

struct brcmnand_io_ops {
	u32 (*read_reg)(struct brcmnand_soc *soc, u32 offset);
	void (*write_reg)(struct brcmnand_soc *soc, u32 val, u32 offset);
};

static inline void brcmnand_soc_data_bus_prepare(struct brcmnand_soc *soc,
						 bool is_param)
{
	if (soc && soc->prepare_data_bus)
		soc->prepare_data_bus(soc, true, is_param);
}

static inline void brcmnand_soc_data_bus_unprepare(struct brcmnand_soc *soc,
						   bool is_param)
{
	if (soc && soc->prepare_data_bus)
		soc->prepare_data_bus(soc, false, is_param);
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
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(addr);
	else
		return readl_relaxed(addr);
}

static inline void brcmnand_writel(u32 val, void __iomem *addr)
{
	/* See brcmnand_readl() comments */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, addr);
	else
		writel_relaxed(val, addr);
}

static inline bool brcmnand_soc_has_ops(struct brcmnand_soc *soc)
{
	return soc && soc->ops && soc->ops->read_reg && soc->ops->write_reg;
}

static inline u32 brcmnand_soc_read(struct brcmnand_soc *soc, u32 offset)
{
	return soc->ops->read_reg(soc, offset);
}

static inline void brcmnand_soc_write(struct brcmnand_soc *soc, u32 val,
				      u32 offset)
{
	soc->ops->write_reg(soc, val, offset);
}

int brcmnand_probe(struct platform_device *pdev, struct brcmnand_soc *soc);
void brcmnand_remove(struct platform_device *pdev);

extern const struct dev_pm_ops brcmnand_pm_ops;

#endif /* __BRCMNAND_H__ */
