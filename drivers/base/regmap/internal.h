/*
 * Register map access API internal header
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _REGMAP_INTERNAL_H
#define _REGMAP_INTERNAL_H

#include <linux/regmap.h>
#include <linux/fs.h>

struct regmap;

struct regmap_format {
	size_t buf_size;
	size_t reg_bytes;
	size_t val_bytes;
	void (*format_write)(struct regmap *map,
			     unsigned int reg, unsigned int val);
	void (*format_reg)(void *buf, unsigned int reg);
	void (*format_val)(void *buf, unsigned int val);
	unsigned int (*parse_val)(void *buf);
};

struct regmap {
	struct mutex lock;

	struct device *dev; /* Device we do I/O on */
	void *work_buf;     /* Scratch buffer used to format I/O */
	struct regmap_format format;  /* Buffer format */
	const struct regmap_bus *bus;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif

	unsigned int max_register;
	bool (*writeable_reg)(struct device *dev, unsigned int reg);
	bool (*readable_reg)(struct device *dev, unsigned int reg);
	bool (*volatile_reg)(struct device *dev, unsigned int reg);
	bool (*precious_reg)(struct device *dev, unsigned int reg);
};

bool regmap_writeable(struct regmap *map, unsigned int reg);
bool regmap_readable(struct regmap *map, unsigned int reg);
bool regmap_volatile(struct regmap *map, unsigned int reg);
bool regmap_precious(struct regmap *map, unsigned int reg);

#ifdef CONFIG_DEBUG_FS
extern void regmap_debugfs_initcall(void);
extern void regmap_debugfs_init(struct regmap *map);
extern void regmap_debugfs_exit(struct regmap *map);
#else
void regmap_debugfs_initcall(void) { }
void regmap_debugfs_init(struct regmap *map) { }
void regmap_debugfs_exit(struct regmap *map) { }
#endif

#endif
