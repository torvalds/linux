// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 */

#include <linux/bitfield.h>
#include <linux/debugfs.h>

#include "rkx110_x120.h"

#define PATTERN_GEN_PATTERN_CTRL	0x0000
#define PATTERN_START_PCLK		BIT(31)
#define PATTERN_START			BIT(30)
#define PATTERN_RECTANGLE_H		GENMASK(29, 16)
#define PATTERN_RECTANGLE_V		GENMASK(13, 0)
#define PATTERN_GEN_PATERN_VH_CFG0	0x0004
#define PATTERN_HACT			GENMASK(29, 16)
#define PATTERN_VACT			GENMASK(13, 0)
#define PATTERN_GEN_PATERN_VH_CFG1	0x0008
#define PATTERN_VFP			GENMASK(29, 20)
#define PATTERN_VBP			GENMASK(19, 10)
#define PATTERN_VSA			GENMASK(9, 0)
#define PATTERN_GEN_PATERN_VH_CFG2	0x000C
#define PATTERN_HFP			GENMASK(27, 16)
#define PATTERN_HBP			GENMASK(11, 0)
#define PATTERN_GEN_PATERN_VH_CFG3	0x0010
#define PATTERN_HSA			GENMASK(11, 0)
#define PATTERN_GEN_VALUE0		0x0014
#define PATTERN_GEN_VALUE1		0x0018

static void pattern_gen_enable(struct pattern_gen *pattern_gen)
{
	struct i2c_client *client = pattern_gen->chip->client;
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	const struct videomode *vm = serdes->vm;

	serdes->i2c_update_bits(client, pattern_gen->base + PATTERN_GEN_PATTERN_CTRL,
				PATTERN_RECTANGLE_H | PATTERN_RECTANGLE_V,
				FIELD_PREP(PATTERN_RECTANGLE_H, 128) |
				FIELD_PREP(PATTERN_RECTANGLE_V, 128));

	serdes->i2c_write_reg(client, pattern_gen->base + PATTERN_GEN_PATERN_VH_CFG0,
			      FIELD_PREP(PATTERN_HACT, vm->hactive) |
			      FIELD_PREP(PATTERN_VACT, vm->vactive));
	serdes->i2c_write_reg(client, pattern_gen->base + PATTERN_GEN_PATERN_VH_CFG1,
			      FIELD_PREP(PATTERN_VFP, vm->vfront_porch) |
			      FIELD_PREP(PATTERN_VBP, vm->vback_porch) |
			      FIELD_PREP(PATTERN_VSA, vm->vsync_len));
	serdes->i2c_write_reg(client, pattern_gen->base + PATTERN_GEN_PATERN_VH_CFG2,
			      FIELD_PREP(PATTERN_HFP, vm->hfront_porch) |
			      FIELD_PREP(PATTERN_HBP, vm->hback_porch));
	serdes->i2c_write_reg(client, pattern_gen->base + PATTERN_GEN_PATERN_VH_CFG3,
			      FIELD_PREP(PATTERN_HSA, vm->hsync_len));

	serdes->i2c_update_bits(client, pattern_gen->base + PATTERN_GEN_PATTERN_CTRL,
				PATTERN_START_PCLK,
				FIELD_PREP(PATTERN_START_PCLK, 1));
	serdes->i2c_write_reg(client, pattern_gen->link_src_reg,
			      BIT(pattern_gen->link_src_offset + 16) |
			      BIT(pattern_gen->link_src_offset));
}

static void pattern_gen_disable(struct pattern_gen *pattern_gen)
{
	struct i2c_client *client = pattern_gen->chip->client;
	struct rk_serdes *serdes = pattern_gen->chip->serdes;

	serdes->i2c_write_reg(client, pattern_gen->link_src_reg,
			      BIT(pattern_gen->link_src_offset + 16));
	serdes->i2c_update_bits(client, pattern_gen->base + PATTERN_GEN_PATTERN_CTRL,
				PATTERN_START_PCLK,
				FIELD_PREP(PATTERN_START_PCLK, 0));
}

static ssize_t pattern_gen_write(struct file *file, const char __user *ubuf,
				 size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct pattern_gen *pattern_gen = m->private;
	char buf[5];

	if (len > sizeof(buf) - 1)
		return -EINVAL;

	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sysfs_streq(buf, "on"))
		pattern_gen_enable(pattern_gen);
	else if (sysfs_streq(buf, "off"))
		pattern_gen_disable(pattern_gen);
	else
		return -EINVAL;

	return len;
}

static int pattern_gen_show(struct seq_file *m, void *data)
{
	struct pattern_gen *pattern_gen = m->private;
	struct i2c_client *client = pattern_gen->chip->client;
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	u32 reg = 0;

	serdes->i2c_read_reg(client, pattern_gen->link_src_reg, &reg);
	if (reg & BIT(pattern_gen->link_src_offset))
		seq_printf(m, "%s\n", "on");
	else
		seq_printf(m, "%s\n", "off");

	return 0;
}

static int pattern_gen_open(struct inode *inode, struct file *file)
{
	struct pattern_gen *pattern_gen = inode->i_private;

	return single_open(file, pattern_gen_show, pattern_gen);
}

static const struct file_operations pattern_gen_fops = {
	.owner = THIS_MODULE,
	.open = pattern_gen_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = pattern_gen_write,
};

void rkx110_x120_pattern_gen_debugfs_create_file(struct pattern_gen *pattern_gen,
						 struct rk_serdes_chip *chip,
						 struct dentry *dentry)
{
	pattern_gen->chip = chip;

	debugfs_create_file(pattern_gen->name, 0600, dentry, pattern_gen,
			    &pattern_gen_fops);
}
