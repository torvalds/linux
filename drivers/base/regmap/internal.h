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
struct regcache_ops;

struct regmap_format {
	size_t buf_size;
	size_t reg_bytes;
	size_t pad_bytes;
	size_t val_bytes;
	void (*format_write)(struct regmap *map,
			     unsigned int reg, unsigned int val);
	void (*format_reg)(void *buf, unsigned int reg, unsigned int shift);
	void (*format_val)(void *buf, unsigned int val, unsigned int shift);
	unsigned int (*parse_val)(void *buf);
};

typedef void (*regmap_lock)(struct regmap *map);
typedef void (*regmap_unlock)(struct regmap *map);

struct regmap {
	struct mutex mutex;
	spinlock_t spinlock;
	regmap_lock lock;
	regmap_unlock unlock;

	struct device *dev; /* Device we do I/O on */
	void *work_buf;     /* Scratch buffer used to format I/O */
	struct regmap_format format;  /* Buffer format */
	const struct regmap_bus *bus;
	void *bus_context;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
	const char *debugfs_name;
#endif

	unsigned int max_register;
	bool (*writeable_reg)(struct device *dev, unsigned int reg);
	bool (*readable_reg)(struct device *dev, unsigned int reg);
	bool (*volatile_reg)(struct device *dev, unsigned int reg);
	bool (*precious_reg)(struct device *dev, unsigned int reg);

	u8 read_flag_mask;
	u8 write_flag_mask;

	/* number of bits to (left) shift the reg value when formatting*/
	int reg_shift;
	int reg_stride;

	/* regcache specific members */
	const struct regcache_ops *cache_ops;
	enum regcache_type cache_type;

	/* number of bytes in reg_defaults_raw */
	unsigned int cache_size_raw;
	/* number of bytes per word in reg_defaults_raw */
	unsigned int cache_word_size;
	/* number of entries in reg_defaults */
	unsigned int num_reg_defaults;
	/* number of entries in reg_defaults_raw */
	unsigned int num_reg_defaults_raw;

	/* if set, only the cache is modified not the HW */
	u32 cache_only;
	/* if set, only the HW is modified not the cache */
	u32 cache_bypass;
	/* if set, remember to free reg_defaults_raw */
	bool cache_free;

	struct reg_default *reg_defaults;
	const void *reg_defaults_raw;
	void *cache;
	u32 cache_dirty;

	struct reg_default *patch;
	int patch_regs;
};

struct regcache_ops {
	const char *name;
	enum regcache_type type;
	int (*init)(struct regmap *map);
	int (*exit)(struct regmap *map);
	int (*read)(struct regmap *map, unsigned int reg, unsigned int *value);
	int (*write)(struct regmap *map, unsigned int reg, unsigned int value);
	int (*sync)(struct regmap *map, unsigned int min, unsigned int max);
};

bool regmap_writeable(struct regmap *map, unsigned int reg);
bool regmap_readable(struct regmap *map, unsigned int reg);
bool regmap_volatile(struct regmap *map, unsigned int reg);
bool regmap_precious(struct regmap *map, unsigned int reg);

int _regmap_write(struct regmap *map, unsigned int reg,
		  unsigned int val);

#ifdef CONFIG_DEBUG_FS
extern void regmap_debugfs_initcall(void);
extern void regmap_debugfs_init(struct regmap *map, const char *name);
extern void regmap_debugfs_exit(struct regmap *map);
#else
static inline void regmap_debugfs_initcall(void) { }
static inline void regmap_debugfs_init(struct regmap *map, const char *name) { }
static inline void regmap_debugfs_exit(struct regmap *map) { }
#endif

/* regcache core declarations */
int regcache_init(struct regmap *map, const struct regmap_config *config);
void regcache_exit(struct regmap *map);
int regcache_read(struct regmap *map,
		       unsigned int reg, unsigned int *value);
int regcache_write(struct regmap *map,
			unsigned int reg, unsigned int value);
int regcache_sync(struct regmap *map);

unsigned int regcache_get_val(const void *base, unsigned int idx,
			      unsigned int word_size);
bool regcache_set_val(void *base, unsigned int idx,
		      unsigned int val, unsigned int word_size);
int regcache_lookup_reg(struct regmap *map, unsigned int reg);

extern struct regcache_ops regcache_rbtree_ops;
extern struct regcache_ops regcache_lzo_ops;

#endif
