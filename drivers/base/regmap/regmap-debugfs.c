// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - debugfs
//
// Copyright 2011 Wolfson Microelectronics plc
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/list.h>

#include "internal.h"

struct regmap_debugfs_node {
	struct regmap *map;
	struct list_head link;
};

static unsigned int dummy_index;
static struct dentry *regmap_debugfs_root;
static LIST_HEAD(regmap_debugfs_early_list);
static DEFINE_MUTEX(regmap_debugfs_early_lock);

/* Calculate the length of a fixed format  */
static size_t regmap_calc_reg_len(int max_val)
{
	return snprintf(NULL, 0, "%x", max_val);
}

static ssize_t regmap_name_read_file(struct file *file,
				     char __user *user_buf, size_t count,
				     loff_t *ppos)
{
	struct regmap *map = file->private_data;
	const char *name = "nodev";
	int ret;
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (map->dev && map->dev->driver)
		name = map->dev->driver->name;

	ret = snprintf(buf, PAGE_SIZE, "%s\n", name);
	if (ret < 0) {
		kfree(buf);
		return ret;
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);
	kfree(buf);
	return ret;
}

static const struct file_operations regmap_name_fops = {
	.open = simple_open,
	.read = regmap_name_read_file,
	.llseek = default_llseek,
};

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

static bool regmap_printable(struct regmap *map, unsigned int reg)
{
	if (regmap_precious(map, reg))
		return false;

	if (!regmap_readable(map, reg) && !regmap_cached(map, reg))
		return false;

	return true;
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
	loff_t p = 0;
	unsigned int i, ret;
	unsigned int fpos_offset;
	unsigned int reg_offset;

	/* Suppress the cache if we're using a subrange */
	if (base)
		return base;

	/*
	 * If we don't have a cache build one so we don't have to do a
	 * linear scan each time.
	 */
	mutex_lock(&map->cache_lock);
	i = base;
	if (list_empty(&map->debugfs_off_cache)) {
		for (; i <= map->max_register; i += map->reg_stride) {
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
					return base;
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

static inline void regmap_calc_tot_len(struct regmap *map,
				       void *buf, size_t count)
{
	/* Calculate the length of a fixed format  */
	if (!map->debugfs_tot_len) {
		map->debugfs_reg_len = regmap_calc_reg_len(map->max_register);
		map->debugfs_val_len = 2 * map->format.val_bytes;
		map->debugfs_tot_len = map->debugfs_reg_len +
			map->debugfs_val_len + 3;      /* : \n */
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

	if (count > (PAGE_SIZE << MAX_ORDER))
		count = PAGE_SIZE << MAX_ORDER;

	buf = kmalloc(count, GFP_KERNEL);
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

static ssize_t regmap_map_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct regmap *map = file->private_data;

	return regmap_read_debugfs(map, 0, map->max_register, user_buf,
				   count, ppos);
}

#undef REGMAP_ALLOW_WRITE_DEBUGFS
#ifdef REGMAP_ALLOW_WRITE_DEBUGFS
/*
 * This can be dangerous especially when we have clients such as
 * PMICs, therefore don't provide any real compile time configuration option
 * for this feature, people who want to use this will need to modify
 * the source code directly.
 */
static ssize_t regmap_map_write_file(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	char *start = buf;
	unsigned long reg, value;
	struct regmap *map = file->private_data;
	int ret;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	reg = simple_strtoul(start, &start, 16);
	while (*start == ' ')
		start++;
	if (kstrtoul(start, 16, &value))
		return -EINVAL;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	ret = regmap_write(map, reg, value);
	if (ret < 0)
		return ret;
	return buf_size;
}
#else
#define regmap_map_write_file NULL
#endif

static const struct file_operations regmap_map_fops = {
	.open = simple_open,
	.read = regmap_map_read_file,
	.write = regmap_map_write_file,
	.llseek = default_llseek,
};

static ssize_t regmap_range_read_file(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct regmap_range_node *range = file->private_data;
	struct regmap *map = range->map;

	return regmap_read_debugfs(map, range->range_min, range->range_max,
				   user_buf, count, ppos);
}

static const struct file_operations regmap_range_fops = {
	.open = simple_open,
	.read = regmap_range_read_file,
	.llseek = default_llseek,
};

static ssize_t regmap_reg_ranges_read_file(struct file *file,
					   char __user *user_buf, size_t count,
					   loff_t *ppos)
{
	struct regmap *map = file->private_data;
	struct regmap_debugfs_off_cache *c;
	loff_t p = 0;
	size_t buf_pos = 0;
	char *buf;
	char *entry;
	int ret;
	unsigned int entry_len;

	if (*ppos < 0 || !count)
		return -EINVAL;

	if (count > (PAGE_SIZE << MAX_ORDER))
		count = PAGE_SIZE << MAX_ORDER;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	entry = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!entry) {
		kfree(buf);
		return -ENOMEM;
	}

	/* While we are at it, build the register dump cache
	 * now so the read() operation on the `registers' file
	 * can benefit from using the cache.  We do not care
	 * about the file position information that is contained
	 * in the cache, just about the actual register blocks */
	regmap_calc_tot_len(map, buf, count);
	regmap_debugfs_get_dump_start(map, 0, *ppos, &p);

	/* Reset file pointer as the fixed-format of the `registers'
	 * file is not compatible with the `range' file */
	p = 0;
	mutex_lock(&map->cache_lock);
	list_for_each_entry(c, &map->debugfs_off_cache, list) {
		entry_len = snprintf(entry, PAGE_SIZE, "%x-%x\n",
				     c->base_reg, c->max_reg);
		if (p >= *ppos) {
			if (buf_pos + entry_len > count)
				break;
			memcpy(buf + buf_pos, entry, entry_len);
			buf_pos += entry_len;
		}
		p += entry_len;
	}
	mutex_unlock(&map->cache_lock);

	kfree(entry);
	ret = buf_pos;

	if (copy_to_user(user_buf, buf, buf_pos)) {
		ret = -EFAULT;
		goto out_buf;
	}

	*ppos += buf_pos;
out_buf:
	kfree(buf);
	return ret;
}

static const struct file_operations regmap_reg_ranges_fops = {
	.open = simple_open,
	.read = regmap_reg_ranges_read_file,
	.llseek = default_llseek,
};

static int regmap_access_show(struct seq_file *s, void *ignored)
{
	struct regmap *map = s->private;
	int i, reg_len;

	reg_len = regmap_calc_reg_len(map->max_register);

	for (i = 0; i <= map->max_register; i += map->reg_stride) {
		/* Ignore registers which are neither readable nor writable */
		if (!regmap_readable(map, i) && !regmap_writeable(map, i))
			continue;

		/* Format the register */
		seq_printf(s, "%.*x: %c %c %c %c\n", reg_len, i,
			   regmap_readable(map, i) ? 'y' : 'n',
			   regmap_writeable(map, i) ? 'y' : 'n',
			   regmap_volatile(map, i) ? 'y' : 'n',
			   regmap_precious(map, i) ? 'y' : 'n');
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(regmap_access);

static ssize_t regmap_cache_only_write_file(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct regmap *map = container_of(file->private_data,
					  struct regmap, cache_only);
	bool new_val, require_sync = false;
	int err;

	err = kstrtobool_from_user(user_buf, count, &new_val);
	/* Ignore malforned data like debugfs_write_file_bool() */
	if (err)
		return count;

	err = debugfs_file_get(file->f_path.dentry);
	if (err)
		return err;

	map->lock(map->lock_arg);

	if (new_val && !map->cache_only) {
		dev_warn(map->dev, "debugfs cache_only=Y forced\n");
		add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	} else if (!new_val && map->cache_only) {
		dev_warn(map->dev, "debugfs cache_only=N forced: syncing cache\n");
		require_sync = true;
	}
	map->cache_only = new_val;

	map->unlock(map->lock_arg);
	debugfs_file_put(file->f_path.dentry);

	if (require_sync) {
		err = regcache_sync(map);
		if (err)
			dev_err(map->dev, "Failed to sync cache %d\n", err);
	}

	return count;
}

static const struct file_operations regmap_cache_only_fops = {
	.open = simple_open,
	.read = debugfs_read_file_bool,
	.write = regmap_cache_only_write_file,
};

static ssize_t regmap_cache_bypass_write_file(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct regmap *map = container_of(file->private_data,
					  struct regmap, cache_bypass);
	bool new_val;
	int err;

	err = kstrtobool_from_user(user_buf, count, &new_val);
	/* Ignore malforned data like debugfs_write_file_bool() */
	if (err)
		return count;

	err = debugfs_file_get(file->f_path.dentry);
	if (err)
		return err;

	map->lock(map->lock_arg);

	if (new_val && !map->cache_bypass) {
		dev_warn(map->dev, "debugfs cache_bypass=Y forced\n");
		add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	} else if (!new_val && map->cache_bypass) {
		dev_warn(map->dev, "debugfs cache_bypass=N forced\n");
	}
	map->cache_bypass = new_val;

	map->unlock(map->lock_arg);
	debugfs_file_put(file->f_path.dentry);

	return count;
}

static const struct file_operations regmap_cache_bypass_fops = {
	.open = simple_open,
	.read = debugfs_read_file_bool,
	.write = regmap_cache_bypass_write_file,
};

void regmap_debugfs_init(struct regmap *map)
{
	struct rb_node *next;
	struct regmap_range_node *range_node;
	const char *devname = "dummy";
	const char *name = map->name;

	/*
	 * Userspace can initiate reads from the hardware over debugfs.
	 * Normally internal regmap structures and buffers are protected with
	 * a mutex or a spinlock, but if the regmap owner decided to disable
	 * all locking mechanisms, this is no longer the case. For safety:
	 * don't create the debugfs entries if locking is disabled.
	 */
	if (map->debugfs_disable) {
		dev_dbg(map->dev, "regmap locking disabled - not creating debugfs entries\n");
		return;
	}

	/* If we don't have the debugfs root yet, postpone init */
	if (!regmap_debugfs_root) {
		struct regmap_debugfs_node *node;
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			return;
		node->map = map;
		mutex_lock(&regmap_debugfs_early_lock);
		list_add(&node->link, &regmap_debugfs_early_list);
		mutex_unlock(&regmap_debugfs_early_lock);
		return;
	}

	INIT_LIST_HEAD(&map->debugfs_off_cache);
	mutex_init(&map->cache_lock);

	if (map->dev)
		devname = dev_name(map->dev);

	if (name) {
		if (!map->debugfs_name) {
			map->debugfs_name = kasprintf(GFP_KERNEL, "%s-%s",
					      devname, name);
			if (!map->debugfs_name)
				return;
		}
		name = map->debugfs_name;
	} else {
		name = devname;
	}

	if (!strcmp(name, "dummy")) {
		kfree(map->debugfs_name);
		map->debugfs_name = kasprintf(GFP_KERNEL, "dummy%d",
						dummy_index);
		if (!map->debugfs_name)
			return;
		name = map->debugfs_name;
		dummy_index++;
	}

	map->debugfs = debugfs_create_dir(name, regmap_debugfs_root);

	debugfs_create_file("name", 0400, map->debugfs,
			    map, &regmap_name_fops);

	debugfs_create_file("range", 0400, map->debugfs,
			    map, &regmap_reg_ranges_fops);

	if (map->max_register || regmap_readable(map, 0)) {
		umode_t registers_mode;

#if defined(REGMAP_ALLOW_WRITE_DEBUGFS)
		registers_mode = 0600;
#else
		registers_mode = 0400;
#endif

		debugfs_create_file("registers", registers_mode, map->debugfs,
				    map, &regmap_map_fops);
		debugfs_create_file("access", 0400, map->debugfs,
				    map, &regmap_access_fops);
	}

	if (map->cache_type) {
		debugfs_create_file("cache_only", 0600, map->debugfs,
				    &map->cache_only, &regmap_cache_only_fops);
		debugfs_create_bool("cache_dirty", 0400, map->debugfs,
				    &map->cache_dirty);
		debugfs_create_file("cache_bypass", 0600, map->debugfs,
				    &map->cache_bypass,
				    &regmap_cache_bypass_fops);
	}

	/*
	 * This could interfere with driver operation. Therefore, don't provide
	 * any real compile time configuration option for this feature. One will
	 * have to modify the source code directly in order to use it.
	 */
#undef REGMAP_ALLOW_FORCE_WRITE_FIELD_DEBUGFS
#ifdef REGMAP_ALLOW_FORCE_WRITE_FIELD_DEBUGFS
	debugfs_create_bool("force_write_field", 0600, map->debugfs,
			    &map->force_write_field);
#endif

	next = rb_first(&map->range_tree);
	while (next) {
		range_node = rb_entry(next, struct regmap_range_node, node);

		if (range_node->name)
			debugfs_create_file(range_node->name, 0400,
					    map->debugfs, range_node,
					    &regmap_range_fops);

		next = rb_next(&range_node->node);
	}

	if (map->cache_ops && map->cache_ops->debugfs_init)
		map->cache_ops->debugfs_init(map);
}

void regmap_debugfs_exit(struct regmap *map)
{
	if (map->debugfs) {
		debugfs_remove_recursive(map->debugfs);
		mutex_lock(&map->cache_lock);
		regmap_debugfs_free_dump_cache(map);
		mutex_unlock(&map->cache_lock);
		kfree(map->debugfs_name);
		map->debugfs_name = NULL;
	} else {
		struct regmap_debugfs_node *node, *tmp;

		mutex_lock(&regmap_debugfs_early_lock);
		list_for_each_entry_safe(node, tmp, &regmap_debugfs_early_list,
					 link) {
			if (node->map == map) {
				list_del(&node->link);
				kfree(node);
			}
		}
		mutex_unlock(&regmap_debugfs_early_lock);
	}
}

void regmap_debugfs_initcall(void)
{
	struct regmap_debugfs_node *node, *tmp;

	regmap_debugfs_root = debugfs_create_dir("regmap", NULL);

	mutex_lock(&regmap_debugfs_early_lock);
	list_for_each_entry_safe(node, tmp, &regmap_debugfs_early_list, link) {
		regmap_debugfs_init(node->map);
		list_del(&node->link);
		kfree(node);
	}
	mutex_unlock(&regmap_debugfs_early_lock);
}
