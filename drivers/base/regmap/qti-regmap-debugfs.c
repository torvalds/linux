// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2011 Wolfson Microelectronics plc
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/qti-regmap-debugfs.h>

#include "internal.h"

struct regmap_qti_debugfs {
	struct list_head	list;
	struct regmap		*regmap;
	struct device		*dev;
	unsigned int		dump_address;
	unsigned int		dump_count;
};

static DEFINE_MUTEX(regmap_qti_debugfs_lock);
static LIST_HEAD(regmap_qti_debugfs_list);

static size_t regmap_calc_reg_len(int max_val)
{
	return snprintf(NULL, 0, "%x", max_val);
}

static inline void regmap_calc_tot_len(struct regmap *map,
				       void *buf, size_t count)
{
	/* Calculate the length of a fixed format  */
	if (!map->debugfs_tot_len) {
		map->debugfs_reg_len = regmap_calc_reg_len(map->max_register),
		map->debugfs_val_len = 2 * map->format.val_bytes;
		map->debugfs_tot_len = map->debugfs_reg_len +
			map->debugfs_val_len + 3;      /* : \n */
	}
}

static bool _regmap_volatile(struct regmap *map, unsigned int reg);

static int _regcache_read(struct regmap *map,
		  unsigned int reg, unsigned int *value)
{
	if (map->cache_type == REGCACHE_NONE)
		return -ENODEV;

	if (WARN_ON(!map->cache_ops))
		return -EINVAL;

	if (!_regmap_volatile(map, reg))
		return map->cache_ops->read(map, reg, value);

	return -EINVAL;
}

static bool _regmap_cached(struct regmap *map, unsigned int reg)
{
	int ret;
	unsigned int val;

	if (map->cache_type == REGCACHE_NONE)
		return false;

	if (!map->cache_ops)
		return false;

	if (map->max_register && reg > map->max_register)
		return false;

	map->lock(map->lock_arg);
	ret = _regcache_read(map, reg, &val);
	map->unlock(map->lock_arg);
	if (ret)
		return false;

	return true;
}

static bool _regmap_readable(struct regmap *map, unsigned int reg)
{
	if (!map->reg_read)
		return false;

	if (map->max_register && reg > map->max_register)
		return false;

	if (map->format.format_write)
		return false;

	if (map->readable_reg)
		return map->readable_reg(map->dev, reg);

	if (map->rd_table)
		return regmap_check_range_table(map, reg, map->rd_table);

	return true;
}

static bool _regmap_volatile(struct regmap *map, unsigned int reg)
{
	if (!map->format.format_write && !_regmap_readable(map, reg))
		return false;

	if (map->volatile_reg)
		return map->volatile_reg(map->dev, reg);

	if (map->volatile_table)
		return regmap_check_range_table(map, reg, map->volatile_table);

	if (map->cache_ops)
		return false;
	else
		return true;
}

static bool _regmap_precious(struct regmap *map, unsigned int reg)
{
	if (!_regmap_readable(map, reg))
		return false;

	if (map->precious_reg)
		return map->precious_reg(map->dev, reg);

	if (map->precious_table)
		return regmap_check_range_table(map, reg, map->precious_table);

	return false;
}

static bool regmap_printable(struct regmap *map, unsigned int reg)
{
	if (_regmap_precious(map, reg))
		return false;

	if (!_regmap_readable(map, reg) && !_regmap_cached(map, reg))
		return false;

	return true;
}

static void regmap_debugfs_free_dump_cache(struct regmap *map)
{
	struct regmap_debugfs_off_cache *c;

	while (!list_empty(&map->debugfs_off_cache)) {
		c = list_first_entry(&map->debugfs_off_cache,
				     struct regmap_debugfs_off_cache,
				     list);
		list_del(&c->list);
		kfree(c);
	}
}

static int regmap_next_readable_reg(struct regmap *map, int reg)
{
	struct regmap_debugfs_off_cache *c;
	int ret = -EINVAL;

	if (regmap_printable(map, reg + map->reg_stride)) {
		ret = reg + map->reg_stride;
	} else {
		mutex_lock(&map->cache_lock);
		list_for_each_entry(c, &map->debugfs_off_cache, list) {
			if (reg > c->max_reg)
				continue;
			if (reg < c->base_reg) {
				ret = c->base_reg;
				break;
			}
		}
		mutex_unlock(&map->cache_lock);
	}
	return ret;
}

static int regmap_debugfs_generate_cache(struct regmap *map)
{
	struct regmap_debugfs_off_cache *c = NULL;
	loff_t p = 0;
	unsigned int i = 0;

	mutex_lock(&map->cache_lock);

	if (list_empty(&map->debugfs_off_cache)) {
		for (i = 0; i <= map->max_register; i += map->reg_stride) {
			/* Skip unprinted registers, closing off cache entry */
			if (!regmap_printable(map, i)) {
				if (c) {
					c->max = p - 1;
					c->max_reg = i - map->reg_stride;
					list_add_tail(&c->list,
						      &map->debugfs_off_cache);
					c = NULL;
				}

				continue;
			}

			/* No cache entry?  Start a new one */
			if (!c) {
				c = kzalloc(sizeof(*c), GFP_KERNEL);
				if (!c) {
					regmap_debugfs_free_dump_cache(map);
					mutex_unlock(&map->cache_lock);
					return -ENOMEM;
				}
				c->min = p;
				c->base_reg = i;
			}

			p += map->debugfs_tot_len;
		}
	}

	/* Close the last entry off if we didn't scan beyond it */
	if (c) {
		c->max = p - 1;
		c->max_reg = i - map->reg_stride;
		list_add_tail(&c->list,
			      &map->debugfs_off_cache);
	}

	mutex_unlock(&map->cache_lock);

	return 0;
}

/*
 * Work out where the start offset maps into register numbers, bearing
 * in mind that we suppress hidden registers.
 */
static unsigned int regmap_debugfs_get_dump_start(struct regmap *map,
						  unsigned int base,
						  loff_t from,
						  loff_t *pos)
{
	struct regmap_debugfs_off_cache *c = NULL;
	unsigned int ret;
	unsigned int fpos_offset;
	unsigned int reg_offset;

	/* Suppress the cache if we're using a subrange */
	if (base)
		return base;

	/*
	 * If we don't have a cache build one so we don't have to do a
	 * linear scan each time.
	 */
	if (regmap_debugfs_generate_cache(map) < 0)
		return base;

	mutex_lock(&map->cache_lock);
	/*
	 * This should never happen; we return above if we fail to
	 * allocate and we should never be in this code if there are
	 * no registers at all.
	 */
	WARN_ON(list_empty(&map->debugfs_off_cache));
	ret = base;

	/* Find the relevant block:offset */
	list_for_each_entry(c, &map->debugfs_off_cache, list) {
		if (from >= c->min && from <= c->max) {
			fpos_offset = from - c->min;
			reg_offset = fpos_offset / map->debugfs_tot_len;
			*pos = c->min + (reg_offset * map->debugfs_tot_len);
			mutex_unlock(&map->cache_lock);
			return c->base_reg + (reg_offset * map->reg_stride);
		}

		*pos = c->max;
		ret = c->max_reg;
	}
	mutex_unlock(&map->cache_lock);

	return ret;
}

/* Determine the file offset where a register appears */
static int regmap_debugfs_get_reg_offset(struct regmap *map,
					 unsigned int reg,
					 loff_t *pos)
{
	struct regmap_debugfs_off_cache *c = NULL;
	unsigned int reg_offset;
	int ret;

	regmap_calc_tot_len(map, NULL, 0);
	ret = regmap_debugfs_generate_cache(map);
	if (ret < 0)
		return ret;

	mutex_lock(&map->cache_lock);
	list_for_each_entry(c, &map->debugfs_off_cache, list) {
		if (reg >= c->base_reg && reg <= c->max_reg) {
			reg_offset = (reg - c->base_reg) / map->reg_stride;
			*pos = c->min + (reg_offset * map->debugfs_tot_len);
			mutex_unlock(&map->cache_lock);
			return 0;
		}
	}
	mutex_unlock(&map->cache_lock);

	return -EINVAL;
}

static ssize_t regmap_read_debugfs(struct regmap *map, unsigned int from,
				   unsigned int to, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	size_t buf_pos = 0;
	loff_t p = *ppos;
	ssize_t ret;
	int i;
	char *buf;
	unsigned int val, start_reg;

	if (*ppos < 0 || !count)
		return -EINVAL;

	if (count > (PAGE_SIZE << (MAX_ORDER - 1)))
		count = PAGE_SIZE << (MAX_ORDER - 1);

	buf = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	regmap_calc_tot_len(map, buf, count);

	/* Work out which register we're starting at */
	start_reg = regmap_debugfs_get_dump_start(map, from, *ppos, &p);

	for (i = start_reg; i >= 0 && i <= to;
	     i = regmap_next_readable_reg(map, i)) {

		/* If we're in the region the user is trying to read */
		if (p >= *ppos) {
			/* ...but not beyond it */
			if (buf_pos + map->debugfs_tot_len > count)
				break;

			/* Format the register */
			snprintf(buf + buf_pos, count - buf_pos, "%.*x: ",
				 map->debugfs_reg_len, i - from);
			buf_pos += map->debugfs_reg_len + 2;

			/* Format the value, write all X if we can't read */
			ret = regmap_read(map, i, &val);
			if (ret == 0)
				snprintf(buf + buf_pos, count - buf_pos,
					 "%.*x", map->debugfs_val_len, val);
			else
				memset(buf + buf_pos, 'X',
				       map->debugfs_val_len);
			buf_pos += 2 * map->format.val_bytes;

			buf[buf_pos++] = '\n';
		}
		p += map->debugfs_tot_len;
	}

	ret = buf_pos;

	if (copy_to_user(user_buf, buf, buf_pos)) {
		ret = -EFAULT;
		goto out;
	}

	*ppos += buf_pos;

out:
	kfree(buf);
	return ret;
}

static ssize_t regmap_data_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct regmap_qti_debugfs *debug_map = file->private_data;
	loff_t reg_pos = 0;
	unsigned int max_reg;
	ssize_t ret;

	ret = regmap_debugfs_get_reg_offset(debug_map->regmap,
					    debug_map->dump_address, &reg_pos);
	if (ret < 0)
		return ret;

	/* Treat the file position of dump_address as 0 */
	*ppos += reg_pos;
	max_reg = debug_map->dump_address +
		  (debug_map->dump_count ? (debug_map->dump_count - 1) : 0) *
		  (debug_map->regmap->reg_stride ?: 1);

	ret = regmap_read_debugfs(debug_map->regmap, 0, max_reg, user_buf,
				  count, ppos);
	if (*ppos < reg_pos)
		return -EINVAL;
	*ppos -= reg_pos;

	return ret;
}

#ifdef CONFIG_REGMAP_QTI_DEBUGFS_ALLOW_WRITE

static ssize_t regmap_data_write_file(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	char *start = buf;
	unsigned long value;
	struct regmap_qti_debugfs *debug_map = file->private_data;
	int ret;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;

	if (kstrtoul(start, 16, &value))
		return -EINVAL;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	ret = regmap_write(debug_map->regmap, debug_map->dump_address, value);
	if (ret < 0)
		return ret;

	return buf_size;
}

#define QTI_DEBUGFS_FILE_MODE 0600

#else

#define regmap_data_write_file NULL
#define QTI_DEBUGFS_FILE_MODE 0400

#endif

static const struct file_operations regmap_data_fops = {
	.open = simple_open,
	.read = regmap_data_read_file,
	.write = regmap_data_write_file,
	.llseek = default_llseek,
};

/**
 * regmap_qti_debugfs_add() - register extra debugfs files for a regmap
 * @dev:		Device pointer of regmap owner
 * @regmap:		regmap pointer
 *
 * This function adds various debugfs files for the specified regmap which
 * provide a mechanism for userspace to read and write regmap register values
 * with easy.
 *
 * Returns a valid pointer on success or ERR_PTR() on failure.
 */
static struct regmap_qti_debugfs *regmap_qti_debugfs_add(struct device *dev,
							 struct regmap *regmap)
{
	struct regmap_qti_debugfs *debug_map = NULL;

	if (!dev || !regmap) {
		pr_err("%s: dev or regmap is NULL\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&regmap_qti_debugfs_lock);
	list_for_each_entry(debug_map, &regmap_qti_debugfs_list, list) {
		if (debug_map->regmap == regmap) {
			debug_map = ERR_PTR(-EINVAL);
			dev_err(dev, "%s: qti debugfs files already registered for regmap\n",
				__func__);
			goto done;
		}
	}

	debug_map = kzalloc(sizeof(*debug_map), GFP_KERNEL);
	if (!debug_map) {
		debug_map = ERR_PTR(-ENOMEM);
		goto done;
	}

	debug_map->dev = dev;
	debug_map->regmap = regmap;
	debug_map->dump_count = 1;

	list_add(&debug_map->list, &regmap_qti_debugfs_list);

	debugfs_create_x32("address", 0600, regmap->debugfs,
			   &debug_map->dump_address);

	debugfs_create_u32("count", 0600, regmap->debugfs,
			   &debug_map->dump_count);

	debugfs_create_file_unsafe("data", QTI_DEBUGFS_FILE_MODE,
				regmap->debugfs, debug_map, &regmap_data_fops);

done:
	mutex_unlock(&regmap_qti_debugfs_lock);

	return debug_map;
}

/**
 * regmap_qti_debugfs_register() - register extra debugfs files for a regmap
 * @dev:		Device pointer of regmap owner
 * @regmap:		regmap pointer
 *
 * This function calls regmap_qti_debugfs_add() which adds several debugfs files
 * for the specified regmap which allow for userspace register read and write
 * access.
 *
 * Returns 0 on success or an errno on failure.
 */
int regmap_qti_debugfs_register(struct device *dev, struct regmap *regmap)
{
	return PTR_ERR_OR_ZERO(regmap_qti_debugfs_add(dev, regmap));
}
EXPORT_SYMBOL(regmap_qti_debugfs_register);

/* regmap_qti_debugfs_lock must be held by caller. */
static void regmap_qti_debugfs_remove(struct regmap_qti_debugfs *debug_map)
{
	struct dentry *file;

	file = debugfs_lookup("address", debug_map->regmap->debugfs);
	dput(file);
	debugfs_remove(file);

	file = debugfs_lookup("count", debug_map->regmap->debugfs);
	dput(file);
	debugfs_remove(file);

	file = debugfs_lookup("data", debug_map->regmap->debugfs);
	dput(file);
	debugfs_remove(file);

	list_del(&debug_map->list);
	kfree(debug_map);
}

/**
 * regmap_qti_debugfs_unregister() - remove extra debugfs files associated with
 *				     a regmap
 * @regmap:		regmap pointer
 *
 * This function removes the extra debugfs files registered for 'regmap' and
 * then frees the regmap debug resources.
 */
void regmap_qti_debugfs_unregister(struct regmap *regmap)
{
	struct regmap_qti_debugfs *debug_map, *temp;

	if (IS_ERR_OR_NULL(regmap)) {
		pr_err("%s: invalid regmap pointer\n", __func__);
		return;
	}

	mutex_lock(&regmap_qti_debugfs_lock);
	list_for_each_entry_safe(debug_map, temp, &regmap_qti_debugfs_list,
				 list) {
		if (debug_map->regmap == regmap) {
			regmap_qti_debugfs_remove(debug_map);
			break;
		}
	}
	mutex_unlock(&regmap_qti_debugfs_lock);
}
EXPORT_SYMBOL(regmap_qti_debugfs_unregister);

/* regmap_qti_debugfs_lock must be held by caller. */
static void _devm_regmap_qti_debugfs_release(struct device *dev, void *res)
{
	struct regmap_qti_debugfs *debug_map, *temp;
	bool found = false;

	debug_map = *(struct regmap_qti_debugfs **)res;
	list_for_each_entry(temp, &regmap_qti_debugfs_list, list) {
		if (temp == debug_map) {
			found = true;
			break;
		}
	}

	if (found)
		regmap_qti_debugfs_remove(debug_map);
}

static void devm_regmap_qti_debugfs_release(struct device *dev, void *res)
{
	mutex_lock(&regmap_qti_debugfs_lock);
	_devm_regmap_qti_debugfs_release(dev, res);
	mutex_unlock(&regmap_qti_debugfs_lock);
}

/**
 * devm_regmap_qti_debugfs_register() - resource managed version of
 *					regmap_qti_debugfs_register()
 * @dev:		Device pointer of regmap owner
 * @regmap:		regmap pointer
 *
 * This is a resource managed version of regmap_qti_debugfs_register().
 * The debugfs files added via this call are automatically removed on driver
 * detach.
 *
 * Returns 0 on success or an errno on failure.
 */
int devm_regmap_qti_debugfs_register(struct device *dev, struct regmap *regmap)
{
	struct regmap_qti_debugfs *debug_map;
	struct regmap_qti_debugfs **ptr;

	ptr = devres_alloc(devm_regmap_qti_debugfs_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	debug_map = regmap_qti_debugfs_add(dev, regmap);
	if (IS_ERR(debug_map)) {
		devres_free(ptr);
		return PTR_ERR(debug_map);
	}

	*ptr = debug_map;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_regmap_qti_debugfs_register);

static int devm_regmap_qti_debugfs_match(struct device *dev, void *res,
					 void *data)
{
	struct regmap_qti_debugfs **debug_map = res;

	if (!debug_map || !*debug_map) {
		WARN_ON(!debug_map || !*debug_map);
		return 0;
	}

	return *debug_map == data;
}

/**
 * devm_regmap_qti_debugfs_unregister() - resource managed version of
 *					  regmap_qti_debugfs_unregister()
 * @regmap:		regmap pointer
 *
 * Deallocate the debug regmap data allocated for 'regmap' with
 * devm_regmap_qti_debugfs_register().  Normally this function will not
 * need to be called and the resource management code will ensure that the
 * resource is freed.
 */
void devm_regmap_qti_debugfs_unregister(struct regmap *regmap)
{
	struct regmap_qti_debugfs *debug_map, *temp;

	if (IS_ERR_OR_NULL(regmap)) {
		pr_err("%s: invalid regmap pointer\n", __func__);
		return;
	}

	mutex_lock(&regmap_qti_debugfs_lock);
	list_for_each_entry_safe(debug_map, temp, &regmap_qti_debugfs_list,
				 list) {
		if (debug_map->regmap == regmap)
			devres_release(debug_map->dev,
				_devm_regmap_qti_debugfs_release,
				devm_regmap_qti_debugfs_match, debug_map);
	}
	mutex_unlock(&regmap_qti_debugfs_lock);
}
EXPORT_SYMBOL(devm_regmap_qti_debugfs_unregister);

MODULE_DESCRIPTION("Regmap QTI debugfs library");
MODULE_LICENSE("GPL v2");
