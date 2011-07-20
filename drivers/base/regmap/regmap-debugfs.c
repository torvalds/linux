/*
 * Register map access API - debugfs
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "internal.h"

static struct dentry *regmap_debugfs_root;

static int regmap_map_open_file(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t regmap_map_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	size_t reg_len, val_len, tot_len;
	size_t buf_pos = 0;
	loff_t p = 0;
	ssize_t ret;
	int i;
	struct regmap *map = file->private_data;
	char *buf;
	unsigned int val;

	if (*ppos < 0 || !count)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Calculate the length of a fixed format  */
	snprintf(buf, count, "%x", map->max_register);
	reg_len = strlen(buf);
	val_len = 2 * map->format.val_bytes;
	tot_len = reg_len + val_len + 3;      /* : \n */

	for (i = 0; i < map->max_register; i++) {
		if (map->readable_reg &&
		    !map->readable_reg(map->dev, i))
			continue;

		/* If we're in the region the user is trying to read */
		if (p >= *ppos) {
			/* ...but not beyond it */
			if (buf_pos >= count - 1 - tot_len)
				break;

			/* Format the register */
			snprintf(buf + buf_pos, count - buf_pos, "%.*x: ",
				 reg_len, i);
			buf_pos += reg_len + 2;

			/* Format the value, write all X if we can't read */
			ret = regmap_read(map, i, &val);
			if (ret == 0)
				snprintf(buf + buf_pos, count - buf_pos,
					 "%.*x", val_len, val);
			else
				memset(buf + buf_pos, 'X', val_len);
			buf_pos += 2 * map->format.val_bytes;

			buf[buf_pos++] = '\n';
		}
		p += tot_len;
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

static const struct file_operations regmap_map_fops = {
	.open = regmap_map_open_file,
	.read = regmap_map_read_file,
	.llseek = default_llseek,
};


void regmap_debugfs_init(struct regmap *map)
{
	map->debugfs = debugfs_create_dir(dev_name(map->dev),
					  regmap_debugfs_root);
	if (!map->debugfs) {
		dev_warn(map->dev, "Failed to create debugfs directory\n");
		return;
	}

	if (map->max_register)
		debugfs_create_file("registers", 0400, map->debugfs,
				    map, &regmap_map_fops);
}

void regmap_debugfs_exit(struct regmap *map)
{
	debugfs_remove_recursive(map->debugfs);
}

void regmap_debugfs_initcall(void)
{
	regmap_debugfs_root = debugfs_create_dir("regmap", NULL);
	if (!regmap_debugfs_root) {
		pr_warn("regmap: Failed to create debugfs root\n");
		return;
	}
}
