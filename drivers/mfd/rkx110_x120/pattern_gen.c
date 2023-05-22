// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 */

#include <linux/bitfield.h>
#include <linux/debugfs.h>

#include "rkx110_x120.h"
#include "rkx110_x120_display.h"
#include "hal/cru_api.h"

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

static void pattern_gen_config(struct i2c_client *client, struct pattern_gen *pattern_gen,
				struct videomode *vm)
{
	struct rk_serdes *serdes = pattern_gen->chip->serdes;

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

	serdes->i2c_write_reg(client, pattern_gen->link_src_reg,
			      BIT(pattern_gen->link_src_offset + 16) |
			      BIT(pattern_gen->link_src_offset));
}

static void pattern_stop_stream(struct pattern_gen *pattern_gen)
{
	struct rk_serdes *serdes = pattern_gen->chip->serdes;

	if (serdes->version != SERDES_V1)
		return;

	if (pattern_gen->chip != &serdes->chip[DEVICE_LOCAL])
		return;

	rk_serdes_display_video_start(serdes, pattern_gen->route, false);

	if (!strcmp(pattern_gen->name, "lvds0")) {
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_D_LVDS0_RKLINK_TX);
	} else if (!strcmp(pattern_gen->name, "lvds1")) {
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_D_LVDS1_RKLINK_TX);
	} else if (!strcmp(pattern_gen->name, "dual-lvds")) {
		rkx110_set_stream_source(serdes, RK_SERDES_RGB_RX, DEVICE_LOCAL);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_2X_LVDS_RKLINK_TX);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_D_LVDS0_RKLINK_TX);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_D_LVDS1_RKLINK_TX);
	} else if (!strcmp(pattern_gen->name, "dsi0")) {
		serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
				      0x1400140);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX111_SRST_RESETN_D_DSI_0_REC_RKLINK_TX);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_D_DSI_0_RKLINK_TX);
		rkx110_linktx_dsi_rec_start(serdes, DEVICE_LOCAL, 0, false);
	} else if (!strcmp(pattern_gen->name, "dsi1")) {
		serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
				      0x2800280);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX111_SRST_RESETN_D_DSI_1_REC_RKLINK_TX);
		hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
			    RKX110_SRST_RESETN_D_DSI_1_RKLINK_TX);
		rkx110_linktx_dsi_rec_start(serdes, DEVICE_LOCAL, 1, false);
	}
}

static void pattern_start_stream(struct pattern_gen *pattern_gen, bool is_pattern_stream)
{
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	struct videomode *vm = &pattern_gen->route->vm;
	u32 delay_length;

	if (serdes->version != SERDES_V1)
		return;

	if (pattern_gen->chip != &serdes->chip[DEVICE_LOCAL])
		return;

	if (!strcmp(pattern_gen->name, "lvds0")) {
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_D_LVDS0_RKLINK_TX);
	} else if (!strcmp(pattern_gen->name, "lvds1")) {
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_D_LVDS1_RKLINK_TX);
	} else if (!strcmp(pattern_gen->name, "dual-lvds")) {
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_2X_LVDS_RKLINK_TX);
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_D_LVDS0_RKLINK_TX);
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_D_LVDS1_RKLINK_TX);
		rkx110_set_stream_source(serdes, RK_SERDES_DUAL_LVDS_RX,
					 DEVICE_LOCAL);
	} else if (!strcmp(pattern_gen->name, "dsi0")) {
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_D_DSI_0_RKLINK_TX);
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX111_SRST_RESETN_D_DSI_0_REC_RKLINK_TX);
		serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
				      0x1400000);

		rkx110_linktx_dsi_type_select(serdes, DEVICE_LOCAL, 0,
					      is_pattern_stream ? false : true);
		if (is_pattern_stream)
			delay_length = vm->hsync_len + vm->hback_porch +
				       vm->hactive + vm->hfront_porch;
		else
			delay_length = (vm->vfront_porch + 1) * (vm->hsync_len +
					vm->hback_porch + vm->hactive + vm->hfront_porch);
		rkx110_linktx_dsi_deley_length_config(serdes, DEVICE_LOCAL, 0, delay_length);
		rkx110_linktx_dsi_rec_start(serdes, DEVICE_LOCAL, 0, true);
	} else if (!strcmp(pattern_gen->name, "dsi1")) {
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX110_SRST_RESETN_D_DSI_1_RKLINK_TX);
		hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
				     RKX111_SRST_RESETN_D_DSI_1_REC_RKLINK_TX);
		serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
				      0x2800000);

		rkx110_linktx_dsi_type_select(serdes, DEVICE_LOCAL, 1,
					      is_pattern_stream ? false : true);
		if (is_pattern_stream)
			delay_length = vm->hsync_len + vm->hback_porch +
				       vm->hactive + vm->hfront_porch;
		else
			delay_length = (vm->vfront_porch + 1) * (vm->hsync_len +
					vm->hback_porch + vm->hactive + vm->hfront_porch);
		rkx110_linktx_dsi_deley_length_config(serdes, DEVICE_LOCAL, 1, delay_length);
		rkx110_linktx_dsi_rec_start(serdes, DEVICE_LOCAL, 1, true);
	}

	rk_serdes_display_video_start(serdes, pattern_gen->route, true);
}

static void pattern_switch_clk_to_pattern(struct pattern_gen *pattern_gen, struct videomode *vm)
{
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	struct hwclk *hwclk = serdes->chip[DEVICE_LOCAL].hwclk;

	if (serdes->version != SERDES_V1)
		return;

	if (pattern_gen->chip != &serdes->chip[DEVICE_LOCAL])
		return;

	if (!strcmp(pattern_gen->name, "lvds0")) {
		hwclk_set_rate(hwclk, RKX111_CPS_CLK_D_LVDS0_PATTERN_GEN, vm->pixelclock);
		dev_info(serdes->dev, "RKX111_CPS_CLK_D_LVDS0_PATTERN_GEN:%d\n",
			 hwclk_get_rate(hwclk, RKX111_CPS_CLK_D_LVDS0_PATTERN_GEN));
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS0_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS0_RKLINK_TX_SEL_CLK_D_LVDS0_PATTERN_GEN);
	} else if (!strcmp(pattern_gen->name, "lvds1")) {
		hwclk_set_rate(hwclk, RKX111_CPS_CLK_D_LVDS1_PATTERN_GEN, vm->pixelclock);
		dev_info(serdes->dev, "RKX111_CPS_CLK_D_LVDS1_PATTERN_GEN:%d\n",
			 hwclk_get_rate(hwclk, RKX111_CPS_CLK_D_LVDS1_PATTERN_GEN));
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS1_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS1_RKLINK_TX_SEL_CLK_D_LVDS1_PATTERN_GEN);
	} else if (!strcmp(pattern_gen->name, "dual-lvds")) {
		hwclk_set_rate(hwclk, RKX111_CPS_CLK_D_LVDS0_PATTERN_GEN, vm->pixelclock);
		dev_info(serdes->dev, "RKX111_CPS_CLK_D_LVDS0_PATTERN_GEN:%d\n",
			 hwclk_get_rate(hwclk, RKX111_CPS_CLK_D_LVDS0_PATTERN_GEN));
		hwclk_set_rate(hwclk, RKX111_CPS_CLK_D_LVDS1_PATTERN_GEN, vm->pixelclock);
		dev_info(serdes->dev, "RKX111_CPS_CLK_D_LVDS1_PATTERN_GEN:%d\n",
			 hwclk_get_rate(hwclk, RKX111_CPS_CLK_D_LVDS1_PATTERN_GEN));
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS0_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS0_RKLINK_TX_SEL_CLK_D_LVDS0_PATTERN_GEN);
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS1_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS1_RKLINK_TX_SEL_CLK_D_LVDS1_PATTERN_GEN);
	} else if (!strcmp(pattern_gen->name, "dsi0")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_DSI_0_RKLINK_TX_SEL,
			      RKX111_CLK_D_DSI_0_RKLINK_TX_SEL_CLK_D_DSI_0_PATTERN_GEN);
	} else if (!strcmp(pattern_gen->name, "dsi1")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_DSI_1_RKLINK_TX_SEL,
			      RKX111_CLK_D_DSI_1_RKLINK_TX_SEL_CLK_D_DSI_1_PATTERN_GEN);
	}
}

static void pattern_switch_clk_to_stream(struct pattern_gen *pattern_gen)
{
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	struct hwclk *hwclk = serdes->chip[DEVICE_LOCAL].hwclk;

	if (serdes->version != SERDES_V1)
		return;

	if (pattern_gen->chip != &serdes->chip[DEVICE_LOCAL])
		return;

	if (!strcmp(pattern_gen->name, "lvds0")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS0_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS0_RKLINK_TX_SEL_CLK_D_LVDS0_RKLINK_TX_PRE);
	} else if (!strcmp(pattern_gen->name, "lvds1")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS1_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS1_RKLINK_TX_SEL_CLK_D_LVDS1_RKLINK_TX_PRE);
	} else if (!strcmp(pattern_gen->name, "dual-lvds")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS0_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS0_RKLINK_TX_SEL_CLK_D_LVDS0_RKLINK_TX_PRE);
		hwclk_set_mux(hwclk, RKX111_CLK_D_LVDS1_RKLINK_TX_SEL,
			      RKX111_CLK_D_LVDS1_RKLINK_TX_SEL_CLK_D_LVDS1_RKLINK_TX_PRE);
	} else if (!strcmp(pattern_gen->name, "dsi0")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_DSI_0_RKLINK_TX_SEL,
			      RKX111_CLK_D_DSI_0_RKLINK_TX_SEL_CLK_D_DSI_0_RKLINK_TX_PRE);
	} else if (!strcmp(pattern_gen->name, "dsi1")) {
		hwclk_set_mux(hwclk, RKX111_CLK_D_DSI_1_RKLINK_TX_SEL,
			      RKX111_CLK_D_DSI_1_RKLINK_TX_SEL_CLK_D_DSI_1_RKLINK_TX_PRE);
	}
}

static int pattern_get_route(struct pattern_gen *pattern_gen)
{
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	struct rk_serdes_route *route;
	int i;

	for (i = 0; i < serdes->route_nr; i++) {
		route = serdes->route[i];

		if (pattern_gen->chip == &serdes->chip[DEVICE_LOCAL]) {
			if ((pattern_gen->type == route->local_port0) ||
			    (pattern_gen->type == route->local_port1)) {
				pattern_gen->route = route;
				break;
			}
		}
		if (pattern_gen->chip == &serdes->chip[DEVICE_REMOTE0]) {
			if ((pattern_gen->type == route->remote0_port0) ||
			    (pattern_gen->type == route->remote0_port1)) {
				pattern_gen->route = route;
				break;
			}
		}

		if (pattern_gen->chip == &serdes->chip[DEVICE_REMOTE1]) {
			if ((pattern_gen->type == route->remote1_port0) ||
			    (pattern_gen->type == route->remote1_port1)) {
				pattern_gen->route = route;
				break;
			}
		}
	}

	if (i >= serdes->route_nr) {
		dev_info(serdes->dev, "can't find the %s in route\n", pattern_gen->name);
		return -EINVAL;
	}

	return 0;
}

static void pattern_gen_enable(struct pattern_gen *pattern_gen)
{
	struct i2c_client *client = pattern_gen->chip->client;
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	struct videomode vm;
	int ret;

	ret = pattern_get_route(pattern_gen);
	if (ret)
		return;

	memcpy(&vm, &pattern_gen->route->vm, sizeof(vm));

	pattern_stop_stream(pattern_gen);
	if (!strcmp(pattern_gen->name, "dual-lvds")) {
		struct pattern_gen *lvds0_pat = pattern_gen + 1;
		struct pattern_gen *lvds1_pat = pattern_gen + 2;

		vm.hactive /= 2;
		vm.hfront_porch /= 2;
		vm.hback_porch /= 2;
		vm.hsync_len /= 2;
		vm.pixelclock /= 2;

		pattern_switch_clk_to_pattern(pattern_gen, &vm);
		pattern_gen_config(client, lvds0_pat, &vm);
		pattern_gen_config(client, lvds1_pat, &vm);
		serdes->i2c_write_reg(client, pattern_gen->link_src_reg,
				      BIT(pattern_gen->link_src_offset + 16) |
				      BIT(pattern_gen->link_src_offset));
	} else {
		pattern_switch_clk_to_pattern(pattern_gen, &vm);
		pattern_gen_config(client, pattern_gen, &vm);
		serdes->i2c_update_bits(client, pattern_gen->base + PATTERN_GEN_PATTERN_CTRL,
					PATTERN_START_PCLK,
					FIELD_PREP(PATTERN_START_PCLK, 1));
	}

	pattern_start_stream(pattern_gen, true);
}

static void pattern_gen_disable(struct pattern_gen *pattern_gen)
{
	struct i2c_client *client = pattern_gen->chip->client;
	struct rk_serdes *serdes = pattern_gen->chip->serdes;
	int ret;

	ret = pattern_get_route(pattern_gen);
	if (ret)
		return;

	pattern_stop_stream(pattern_gen);
	if (!strcmp(pattern_gen->name, "dual-lvds")) {
		struct pattern_gen *lvds0_pat = pattern_gen + 1;
		struct pattern_gen *lvds1_pat = pattern_gen + 2;

		serdes->i2c_write_reg(client, lvds0_pat->link_src_reg,
				      BIT(lvds0_pat->link_src_offset + 16));
		serdes->i2c_write_reg(client, lvds1_pat->link_src_reg,
				      BIT(lvds1_pat->link_src_offset + 16));
		serdes->i2c_write_reg(client, pattern_gen->link_src_reg,
				      BIT(pattern_gen->link_src_offset + 16));
	} else {
		serdes->i2c_write_reg(client, pattern_gen->link_src_reg,
				      BIT(pattern_gen->link_src_offset + 16));
		serdes->i2c_update_bits(client, pattern_gen->base + PATTERN_GEN_PATTERN_CTRL,
					PATTERN_START_PCLK,
					FIELD_PREP(PATTERN_START_PCLK, 0));
	}

	pattern_switch_clk_to_stream(pattern_gen);
	pattern_start_stream(pattern_gen, false);
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
