/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register map access API internal header
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef _REGMAP_INTERNAL_H
#define _REGMAP_INTERNAL_H

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/wait.h>

struct regmap;
struct regcache_ops;

struct regmap_debugfs_off_cache {
	struct list_head list;
	off_t min;
	off_t max;
	unsigned int base_reg;
	unsigned int max_reg;
};

struct regmap_format {
	size_t buf_size;
	size_t reg_bytes;
	size_t pad_bytes;
	size_t val_bytes;
	s8 reg_shift;
	void (*format_write)(struct regmap *map,
			     unsigned int reg, unsigned int val);
	void (*format_reg)(void *buf, unsigned int reg, unsigned int shift);
	void (*format_val)(void *buf, unsigned int val, unsigned int shift);
	unsigned int (*parse_val)(const void *buf);
	void (*parse_inplace)(void *buf);
};

struct regmap_async {
	struct list_head list;
	struct regmap *map;
	void *work_buf;
};

struct regmap {
	union {
		struct mutex mutex;
		struct {
			spinlock_t spinlock;
			unsigned long spinlock_flags;
		};
		struct {
			raw_spinlock_t raw_spinlock;
			unsigned long raw_spinlock_flags;
		};
	};
	regmap_lock lock;
	regmap_unlock unlock;
	void *lock_arg; /* This is passed to lock/unlock functions */
	gfp_t alloc_flags;
	unsigned int reg_base;

	struct device *dev; /* Device we do I/O on */
	void *work_buf;     /* Scratch buffer used to format I/O */
	struct regmap_format format;  /* Buffer format */
	const struct regmap_bus *bus;
	void *bus_context;
	const char *name;

	bool async;
	spinlock_t async_lock;
	wait_queue_head_t async_waitq;
	struct list_head async_list;
	struct list_head async_free;
	int async_ret;

#ifdef CONFIG_DEBUG_FS
	bool debugfs_disable;
	struct dentry *debugfs;
	const char *debugfs_name;

	unsigned int debugfs_reg_len;
	unsigned int debugfs_val_len;
	unsigned int debugfs_tot_len;

	struct list_head debugfs_off_cache;
	struct mutex cache_lock;
#endif

	unsigned int max_register;
	bool max_register_is_set;
	bool (*writeable_reg)(struct device *dev, unsigned int reg);
	bool (*readable_reg)(struct device *dev, unsigned int reg);
	bool (*volatile_reg)(struct device *dev, unsigned int reg);
	bool (*precious_reg)(struct device *dev, unsigned int reg);
	bool (*writeable_noinc_reg)(struct device *dev, unsigned int reg);
	bool (*readable_noinc_reg)(struct device *dev, unsigned int reg);
	const struct regmap_access_table *wr_table;
	const struct regmap_access_table *rd_table;
	const struct regmap_access_table *volatile_table;
	const struct regmap_access_table *precious_table;
	const struct regmap_access_table *wr_noinc_table;
	const struct regmap_access_table *rd_noinc_table;

	int (*reg_read)(void *context, unsigned int reg, unsigned int *val);
	int (*reg_write)(void *context, unsigned int reg, unsigned int val);
	int (*reg_update_bits)(void *context, unsigned int reg,
			       unsigned int mask, unsigned int val);
	/* Bulk read/write */
	int (*read)(void *context, const void *reg_buf, size_t reg_size,
		    void *val_buf, size_t val_size);
	int (*write)(void *context, const void *data, size_t count);

	bool defer_caching;

	unsigned long read_flag_mask;
	unsigned long write_flag_mask;

	/* number of bits to (left) shift the reg value when formatting*/
	int reg_shift;
	int reg_stride;
	int reg_stride_order;

	/* If set, will always write field to HW. */
	bool force_write_field;

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
	bool cache_only;
	/* if set, only the HW is modified not the cache */
	bool cache_bypass;
	/* if set, remember to free reg_defaults_raw */
	bool cache_free;

	struct reg_default *reg_defaults;
	const void *reg_defaults_raw;
	void *cache;
	/* if set, the cache contains newer data than the HW */
	bool cache_dirty;
	/* if set, the HW registers are known to match map->reg_defaults */
	bool no_sync_defaults;

	struct reg_sequence *patch;
	int patch_regs;

	/* if set, converts bulk read to single read */
	bool use_single_read;
	/* if set, converts bulk write to single write */
	bool use_single_write;
	/* if set, the device supports multi write mode */
	bool can_multi_write;

	/* if set, raw reads/writes are limited to this size */
	size_t max_raw_read;
	size_t max_raw_write;

	struct rb_root range_tree;
	void *selector_work_buf;	/* Scratch buffer used for selector */

	struct hwspinlock *hwlock;

	/* if set, the regmap core can sleep */
	bool can_sleep;
};

struct regcache_ops {
	const char *name;
	enum regcache_type type;
	int (*init)(struct regmap *map);
	int (*exit)(struct regmap *map);
#ifdef CONFIG_DEBUG_FS
	void (*debugfs_init)(struct regmap *map);
#endif
	int (*read)(struct regmap *map, unsigned int reg, unsigned int *value);
	int (*write)(struct regmap *map, unsigned int reg, unsigned int value);
	int (*sync)(struct regmap *map, unsigned int min, unsigned int max);
	int (*drop)(struct regmap *map, unsigned int min, unsigned int max);
};

bool regmap_cached(struct regmap *map, unsigned int reg);
bool regmap_writeable(struct regmap *map, unsigned int reg);
bool regmap_readable(struct regmap *map, unsigned int reg);
bool regmap_volatile(struct regmap *map, unsigned int reg);
bool regmap_precious(struct regmap *map, unsigned int reg);
bool regmap_writeable_noinc(struct regmap *map, unsigned int reg);
bool regmap_readable_noinc(struct regmap *map, unsigned int reg);

int _regmap_write(struct regmap *map, unsigned int reg,
		  unsigned int val);

struct regmap_range_node {
	struct rb_node node;
	const char *name;
	struct regmap *map;

	unsigned int range_min;
	unsigned int range_max;

	unsigned int selector_reg;
	unsigned int selector_mask;
	int selector_shift;

	unsigned int window_start;
	unsigned int window_len;
};

struct regmap_field {
	struct regmap *regmap;
	unsigned int mask;
	/* lsb */
	unsigned int shift;
	unsigned int reg;

	unsigned int id_size;
	unsigned int id_offset;
};

#ifdef CONFIG_DEBUG_FS
extern void regmap_debugfs_initcall(void);
extern void regmap_debugfs_init(struct regmap *map);
extern void regmap_debugfs_exit(struct regmap *map);

static inline void regmap_debugfs_disable(struct regmap *map)
{
	map->debugfs_disable = true;
}

#else
static inline void regmap_debugfs_initcall(void) { }
static inline void regmap_debugfs_init(struct regmap *map) { }
static inline void regmap_debugfs_exit(struct regmap *map) { }
static inline void regmap_debugfs_disable(struct regmap *map) { }
#endif

/* regcache core declarations */
int regcache_init(struct regmap *map, const struct regmap_config *config);
void regcache_exit(struct regmap *map);
int regcache_read(struct regmap *map,
		       unsigned int reg, unsigned int *value);
int regcache_write(struct regmap *map,
			unsigned int reg, unsigned int value);
int regcache_sync(struct regmap *map);
int regcache_sync_block(struct regmap *map, void *block,
			unsigned long *cache_present,
			unsigned int block_base, unsigned int start,
			unsigned int end);
bool regcache_reg_needs_sync(struct regmap *map, unsigned int reg,
			     unsigned int val);

static inline const void *regcache_get_val_addr(struct regmap *map,
						const void *base,
						unsigned int idx)
{
	return base + (map->cache_word_size * idx);
}

unsigned int regcache_get_val(struct regmap *map, const void *base,
			      unsigned int idx);
void regcache_set_val(struct regmap *map, void *base, unsigned int idx,
		      unsigned int val);
int regcache_lookup_reg(struct regmap *map, unsigned int reg);
int regcache_sync_val(struct regmap *map, unsigned int reg, unsigned int val);

int _regmap_raw_write(struct regmap *map, unsigned int reg,
		      const void *val, size_t val_len, bool noinc);

void regmap_async_complete_cb(struct regmap_async *async, int ret);

enum regmap_endian regmap_get_val_endian(struct device *dev,
					 const struct regmap_bus *bus,
					 const struct regmap_config *config);

extern struct regcache_ops regcache_rbtree_ops;
extern struct regcache_ops regcache_maple_ops;
extern struct regcache_ops regcache_flat_ops;

static inline const char *regmap_name(const struct regmap *map)
{
	if (map->dev)
		return dev_name(map->dev);

	return map->name;
}

static inline unsigned int regmap_get_offset(const struct regmap *map,
					     unsigned int index)
{
	if (map->reg_stride_order >= 0)
		return index << map->reg_stride_order;
	else
		return index * map->reg_stride;
}

static inline unsigned int regcache_get_index_by_order(const struct regmap *map,
						       unsigned int reg)
{
	return reg >> map->reg_stride_order;
}

struct regmap_ram_data {
	unsigned int *vals;  /* Allocatd by caller */
	bool *read;
	bool *written;
	enum regmap_endian reg_endian;
	bool (*noinc_reg)(struct regmap_ram_data *data, unsigned int reg);
};

/*
 * Create a test register map with data stored in RAM, not intended
 * for practical use.
 */
struct regmap *__regmap_init_ram(struct device *dev,
				 const struct regmap_config *config,
				 struct regmap_ram_data *data,
				 struct lock_class_key *lock_key,
				 const char *lock_name);

#define regmap_init_ram(dev, config, data)					\
	__regmap_lockdep_wrapper(__regmap_init_ram, #dev, dev, config, data)

struct regmap *__regmap_init_raw_ram(struct device *dev,
				     const struct regmap_config *config,
				     struct regmap_ram_data *data,
				     struct lock_class_key *lock_key,
				     const char *lock_name);

#define regmap_init_raw_ram(dev, config, data)				\
	__regmap_lockdep_wrapper(__regmap_init_raw_ram, #dev, dev, config, data)

#endif
