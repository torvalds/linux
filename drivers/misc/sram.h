/*
 * Defines for the SRAM driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SRAM_H
#define __SRAM_H

struct sram_partition {
	void __iomem *base;

	struct gen_pool *pool;
	struct bin_attribute battr;
	struct mutex lock;
	struct list_head list;
};

struct sram_dev {
	struct device *dev;
	void __iomem *virt_base;

	struct gen_pool *pool;
	struct clk *clk;

	struct sram_partition *partition;
	u32 partitions;
};

struct sram_reserve {
	struct list_head list;
	u32 start;
	u32 size;
	bool export;
	bool pool;
	const char *label;
};
#endif /* __SRAM_H */
