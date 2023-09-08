// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/core.h>
#include "rkx110_x120.h"
#include "rkx110_x120_display.h"
#include "rkx110_reg.h"
#include "rkx110_dsi_rx.h"
#include "rkx120_dsi_tx.h"
#include "hal/cru_api.h"
#include "hal/pinctrl_api.h"

static const struct mfd_cell rkx110_x120_devs[] = {
	/* 2 panel device for rkx110_x120 drm panel */
	{
		.name = "rockchip-serdes-panel",
		.of_compatible = "rockchip,serdes-panel",
	},
	{
		.name = "rockchip-serdes-panel1",
		.of_compatible = "rockchip,serdes-panel",
	},
	{
		.name = "rkx120-pwm0",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm1",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm2",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm3",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm4",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm5",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm6",
		.of_compatible = "rockchip,rkx120-pwm",
	},
	{
		.name = "rkx120-pwm7",
		.of_compatible = "rockchip,rkx120-pwm",
	},
};

static int rk_serdes_i2c_read(struct i2c_client *client, u32 addr, u32 *value)
{
	struct i2c_msg xfer[2];
	u32 reg;
	u32 data;
	int ret;

	reg = cpu_to_le32(addr);
	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 4;
	xfer[0].buf = (u8 *)&reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 4;
	xfer[1].buf = (u8 *)&data;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	*value = le32_to_cpu(data);
	dev_dbg(&client->dev, "read: 0x%08x: 0x%08x\n", addr, *value);

	return ret;
}

static int rk_serdes_i2c_write(struct i2c_client *client, u32 addr, u32 value)
{
	struct i2c_msg xfer;
	u32 reg;
	u32 data;
	u8 buf[8];
	int ret;

	reg = cpu_to_le32(addr);
	data = cpu_to_le32(value);
	memcpy(&buf[0], &reg, 4);
	memcpy(&buf[4], &data, 4);

	/* Write address & data */
	xfer.addr = client->addr;
	xfer.flags = 0;
	xfer.len = 8;
	xfer.buf = buf;

	dev_dbg(&client->dev, "write: 0x%08x: 0x%08x\n", addr, value);
	ret = i2c_transfer(client->adapter, &xfer, 1);
	if (ret == 1)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rk_serdes_i2c_update_bits(struct i2c_client *client, u32 reg, u32 mask, u32 val)
{
	u32 value;
	int ret;

	ret = rk_serdes_i2c_read(client, reg, &value);
	if (ret)
		return ret;

	value &= ~mask;
	value |= (val & mask);
	ret = rk_serdes_i2c_write(client, reg, value);
	if (ret)
		return ret;

	return 0;
}

static bool rk_serdes_debug_mode(struct rk_serdes *serdes)
{
	return serdes->rkx110_debug || serdes->rkx120_debug;
}

static int rk_serdes_wait_link_ready(struct rk_serdes *serdes)
{
	int ret;

	if (serdes->stream_type == STREAM_DISPLAY) {
		ret = rkx110_linktx_wait_link_ready(serdes, 0);
		if (ret)
			return ret;

		if (serdes->lane_nr == 2) {
			rkx110_ser_pma_enable(serdes, true, 1, DEVICE_LOCAL);
			if (!(serdes->remote_nr == 2))
				rkx120_des_pma_enable(serdes, true, 1, DEVICE_REMOTE0);
			ret = rkx110_linktx_wait_link_ready(serdes, 1);
			if (ret)
				return ret;
		}

	} else {
		ret = rkx120_linkrx_wait_link_ready(serdes, 0);
		if (ret)
			return ret;

		if (serdes->lane_nr == 2) {
			rkx120_des_pma_enable(serdes, true, 1, DEVICE_LOCAL);
			if (!(serdes->remote_nr == 2))
				rkx110_ser_pma_enable(serdes, true, 1, DEVICE_REMOTE0);
			ret = rkx120_linkrx_wait_link_ready(serdes, 1);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void rk_serdes_print_rate(struct rk_serdes *serdes, enum rk_serdes_rate rate)
{
	switch (rate) {
	case RATE_4GBPS_83M:
		dev_info(serdes->dev, "serdes set rate: 4Gbps, backward: 83Mbps\n");
		break;
	case RATE_4GBPS_125M:
		dev_info(serdes->dev, "serdes set rate: 4Gbps, backward: 125Mbps\n");
		break;
	case RATE_4GBPS_250M:
		dev_info(serdes->dev, "serdes set rate: 4Gbps, backward: 250Mbps\n");
		break;
	case RATE_4_5GBPS_140M:
		dev_info(serdes->dev, "serdes set rate: 4.5Gbps, backward: 140Mbps\n");
		break;
	case RATE_4_8GBPS_150M:
		dev_info(serdes->dev, "serdes set rate: 4.8Gbps, backward: 150Mbps\n");
		break;
#if 0
	case RATE_5GBPS_156M:
		dev_info(serdes->dev, "serdes set rate: 5Gbps, backward: 156Mbps\n");
		break;
	case RATE_6GBPS_187M:
		dev_info(serdes->dev, "serdes set rate: 6Gbps, backward: 187Mbps\n");
		break;
#endif
	case RATE_2GBPS_83M:
		dev_info(serdes->dev, "serdes set rate: 2Gbps, backward: 83Mbps\n");
		break;
	default:
		dev_info(serdes->dev, "serdes set rate: Unknown rate\n");
		break;
	}
}

static void rk_serdes_set_rate(struct rk_serdes *serdes, enum rk_serdes_rate rate)
{
	struct rk_serdes_pma_pll rkx110_pll, rkx120_pll;

	if (serdes->rate == rate)
		return;

	memset(&rkx110_pll, 0, sizeof(rkx110_pll));
	memset(&rkx120_pll, 0, sizeof(rkx120_pll));

	rk_serdes_print_rate(serdes, rate);

	switch (rate) {
	case RATE_4GBPS_83M:
		rkx110_pll.rate_mode = FDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 21330;
		rkx110_pll.clk_div = 5;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = false;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 21330;
		rkx120_pll.clk_div = 23;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
	case RATE_4GBPS_125M:
		rkx110_pll.rate_mode = FDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 21330;
		rkx110_pll.clk_div = 1;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 21330;
		rkx120_pll.clk_div = 15;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
	case RATE_4GBPS_250M:
		rkx110_pll.rate_mode = FDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 21330;
		rkx110_pll.clk_div = 0;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 21330;
		rkx120_pll.clk_div = 7;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
	case RATE_4_5GBPS_140M:
		rkx110_pll.rate_mode = FDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 24000;
		rkx110_pll.clk_div = 1;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 12000;
		rkx120_pll.clk_div = 7;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
	case RATE_4_8GBPS_150M:
		rkx110_pll.rate_mode = FDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 26000;
		rkx110_pll.clk_div = 1;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 13000;
		rkx120_pll.clk_div = 7;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
#if 0
	case RATE_5GBPS_156M:
		rkx110_pll.rate_mode = FDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 26667;
		rkx110_pll.clk_div = 3;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = 1;
		rkx120_pll.pll_div = 26667;
		rkx120_pll.clk_div = 31;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
	case RATE_6GBPS_187M:
		rkx110_pll.rate_mode = QDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 29000;
		rkx110_pll.clk_div = 1;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 29000;
		rkx120_pll.clk_div = 15;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
#endif
	case RATE_2GBPS_83M:
		rkx110_pll.rate_mode = HDR_RATE_MODE;
		rkx110_pll.pll_div4 = true;
		rkx110_pll.pll_div = 21330;
		rkx110_pll.clk_div = 2;
		rkx110_pll.pll_refclk_div = 0;
		rkx110_pll.pll_fck_vco_div2 = true;

		rkx120_pll.pll_div4 = true;
		rkx120_pll.pll_div = 21330;
		rkx120_pll.clk_div = 23;
		rkx120_pll.pll_refclk_div = 0;
		rkx120_pll.pll_fck_vco_div2 = true;
		break;
	default:
		return;
	}

	if (serdes->stream_type == STREAM_DISPLAY) {
		rkx110_pma_set_rate(serdes, &rkx110_pll, 0, DEVICE_LOCAL);
		rkx120_pma_set_rate(serdes, &rkx120_pll, 0, DEVICE_REMOTE0);
		if (serdes->lane_nr == 2) {
			rkx110_pma_set_rate(serdes, &rkx110_pll, 1, DEVICE_LOCAL);
			if (serdes->remote_nr == 2)
				rkx120_pma_set_rate(serdes, &rkx120_pll, 0, DEVICE_REMOTE1);
			else
				rkx120_pma_set_rate(serdes, &rkx120_pll, 1, DEVICE_REMOTE0);
		}
		rkx110_pcs_enable(serdes, 0, 0, DEVICE_LOCAL);
		usleep_range(1000, 2000);
		rkx110_pcs_enable(serdes, 1, 0, DEVICE_LOCAL);
	} else {
		rkx120_pma_set_rate(serdes, &rkx120_pll, 0, DEVICE_LOCAL);
		rkx110_pma_set_rate(serdes, &rkx110_pll, 0, DEVICE_REMOTE0);
		if (serdes->lane_nr == 2) {
			rkx120_pma_set_rate(serdes, &rkx120_pll, 1, DEVICE_LOCAL);
			if (serdes->remote_nr == 2)
				rkx110_pma_set_rate(serdes, &rkx110_pll, 0, DEVICE_REMOTE1);
			else
				rkx110_pma_set_rate(serdes, &rkx110_pll, 1, DEVICE_REMOTE0);
		}
		rkx120_pcs_enable(serdes, 0, 0, DEVICE_LOCAL);
		usleep_range(1000, 2000);
		rkx120_pcs_enable(serdes, 1, 0, DEVICE_LOCAL);
	}

	rk_serdes_wait_link_ready(serdes);

	serdes->rate = rate;
}

static int rk_serdes_set_hwpin(struct rk_serdes *serdes, struct i2c_client *client,
			       int pintype, int bank, uint32_t mpins, uint32_t param)
{
	struct xferpin xfer;
	char name[16];

	snprintf(name, sizeof(name), "0x%x", client->addr);

	xfer.name = name;
	xfer.client = client;
	xfer.type = pintype;
	xfer.bank = bank;
	xfer.mpins = mpins;
	xfer.param = param;
	xfer.read = serdes->i2c_read_reg;
	xfer.write = serdes->i2c_write_reg;

	return hwpin_set(xfer);
}

static void rk_serdes_add_callback(struct rk_serdes *serdes)
{
	serdes->i2c_read_reg = rk_serdes_i2c_read;
	serdes->i2c_write_reg = rk_serdes_i2c_write;
	serdes->i2c_update_bits = rk_serdes_i2c_update_bits;
	serdes->set_hwpin = rk_serdes_set_hwpin;

	if (rk_serdes_debug_mode(serdes))
		return;

	if (serdes->stream_type == STREAM_DISPLAY) {
		serdes->route_prepare = rk_serdes_display_route_prepare;
		serdes->route_enable = rk_serdes_display_route_enable;
		serdes->route_disable = rk_serdes_display_route_disable;
		serdes->route_unprepare = rk_serdes_display_route_unprepare;
	}
}

static int rk_serdes_passthrough_init(struct rk_serdes *serdes)
{
	struct device_node *np;
	u32 *configs;
	char name[30] = "rk-serdes,pt";
	int length, i, ret;
	u32 devicerx_id, devicetx_id, func_id;

	/* rk-serdes,passthrough = <devicerx_id devicetx_id passthrough_func>; */
	for_each_child_of_node(serdes->dev->of_node, np) {
		length = of_property_count_u32_elems(np, name);
		if (length < 0)
			continue;
		if (length % 3) {
			dev_err(serdes->dev, "Invalid count for passthrough %s\n", np->name);
			return -EINVAL;
		}
		configs = kmalloc_array(length, sizeof(u32), GFP_KERNEL);
		if (!configs)
			return -ENOMEM;

		ret = of_property_read_u32_array(np, name, configs, length);
		if (ret) {
			dev_err(serdes->dev, "get %s passthrough configs data error\n", np->name);
			kfree(configs);
			return -EINVAL;
		}
		for (i = 0; i < length; i += 3) {
			devicerx_id = configs[i];
			devicetx_id = configs[i + 1];
			func_id = configs[i + 2];

			if (serdes->stream_type == STREAM_DISPLAY) {
				if (devicerx_id == DEVICE_LOCAL) {
					/* soc out->rkx110 in->rkx120 out->device in */
					rkx110_linktx_passthrough_cfg(serdes, devicerx_id, func_id,
								      true);
					rkx120_linkrx_passthrough_cfg(serdes, devicetx_id, func_id,
								      false);
				} else {
					/* device out->rkx120 in->rkx110 out->soc in */
					rkx110_linktx_passthrough_cfg(serdes, devicetx_id, func_id,
								      false);
					rkx120_linkrx_passthrough_cfg(serdes, devicerx_id, func_id,
								      true);
				}
			} else {
				if (devicerx_id == DEVICE_LOCAL) {
					/* soc out->rkx120 in->rkx110 out->device in */
					rkx110_linktx_passthrough_cfg(serdes, devicetx_id, func_id,
								      false);
					rkx120_linkrx_passthrough_cfg(serdes, devicerx_id, func_id,
								      true);
				} else {
					/* device out->rkx110 in->rkx120 out->soc in */
					rkx110_linktx_passthrough_cfg(serdes, devicerx_id, func_id,
								      true);
					rkx120_linkrx_passthrough_cfg(serdes, devicetx_id, func_id,
								      false);
				}
			}
			dev_info(serdes->dev, "%s: devicerx_id %x, devicetx_id %x, func_id %x\n",
				 np->name, devicerx_id, devicetx_id, func_id);
		}

		kfree(configs);
	}

	/* config irq passthrough */
	if (serdes->stream_type == STREAM_DISPLAY) {
		rkx110_linktx_passthrough_cfg(serdes, DEVICE_LOCAL, RK_SERDES_PASSTHROUGH_IRQ,
					      false);
		rkx120_linkrx_passthrough_cfg(serdes, DEVICE_REMOTE0, RK_SERDES_PASSTHROUGH_IRQ,
					      true);
		if (serdes->remote_nr == 2)
			rkx120_linkrx_passthrough_cfg(serdes, DEVICE_REMOTE1,
						      RK_SERDES_PASSTHROUGH_IRQ, true);
	} else {
		rkx120_linkrx_passthrough_cfg(serdes, DEVICE_LOCAL, RK_SERDES_PASSTHROUGH_IRQ,
					      false);
		rkx110_linktx_passthrough_cfg(serdes, DEVICE_REMOTE0, RK_SERDES_PASSTHROUGH_IRQ,
					      true);
		if (serdes->remote_nr == 2)
			rkx110_linktx_passthrough_cfg(serdes, DEVICE_REMOTE1,
						      RK_SERDES_PASSTHROUGH_IRQ, true);
	}

	return 0;
}

static int rk_serdes_clk_show(struct seq_file *s, void *v)
{
	hwclk_dump_tree(CLK_ALL);

	return 0;
}

static int rk_serdes_clk_open(struct inode *inode, struct file *file)
{

	return single_open(file, rk_serdes_clk_show, NULL);
}

static const struct file_operations rk_serdes_clk_fops = {
	.owner          = THIS_MODULE,
	.open           = rk_serdes_clk_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int rk_serdes_rate_show(struct seq_file *s, void *v)
{
	struct rk_serdes *serdes = s->private;

	seq_printf(s, "serdes current rate: %d\n", serdes->rate);

	seq_printf(s, "%d: 2Gbps, backward: 83Mbps\n", RATE_2GBPS_83M);
	seq_printf(s, "%d: 4Gbps, backward: 83Mbps\n", RATE_4GBPS_83M);
	seq_printf(s, "%d: 4Gbps, backward: 125Mbps\n", RATE_4GBPS_125M);
	seq_printf(s, "%d: 4Gbps, backward: 250Mbps\n", RATE_4GBPS_250M);
	seq_printf(s, "%d: 4.5Gbps, backward: 140Mbps\n", RATE_4_5GBPS_140M);
	seq_printf(s, "%d: 4.8Gbps, backward: 150Mbps\n", RATE_4_8GBPS_150M);
	seq_printf(s, "%d: 5Gbps, backward: 156Mbps\n", RATE_5GBPS_156M);
//	seq_printf(s, "%d: 6Gbps, backward: 187Mbps\n", RATE_6GBPS_187M);

	return 0;
}

static ssize_t rk_serdes_rate_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct rk_serdes *serdes = file->f_path.dentry->d_inode->i_private;
	u32 rate;
	char kbuf[25];
	int ret;

	if (count >= sizeof(kbuf))
		return -ENOSPC;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	ret = kstrtou32(kbuf, 10, &rate);
	if (ret < 0)
		return ret;

	rk_serdes_set_rate(serdes, rate);

	return count;
}

static int rk_serdes_rate_open(struct inode *inode, struct file *file)
{
	struct rk_serdes *serdes = inode->i_private;

	return single_open(file, rk_serdes_rate_show, serdes);
}

static const struct file_operations rk_serdes_rate_fops = {
	.owner          = THIS_MODULE,
	.open           = rk_serdes_rate_open,
	.read           = seq_read,
	.write          = rk_serdes_rate_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void rk_serdes_function_debugfs_init(struct rk_serdes *serdes)
{
	serdes->debugfs_rate = debugfs_create_file("rate", 0400, serdes->debugfs_root,
						   serdes, &rk_serdes_rate_fops);
	serdes->debugfs_rate = debugfs_create_file("clk", 0400, serdes->debugfs_root,
						   NULL, &rk_serdes_clk_fops);
}

static void rk_serdes_debugfs_init(struct rk_serdes *serdes)
{
#if defined(CONFIG_DEBUG_FS)
	serdes->debugfs_root =
		debugfs_create_dir(dev_name(serdes->dev), debugfs_lookup("rkserdes", NULL));
	serdes->debugfs_local = debugfs_create_dir("local", serdes->debugfs_root);

	if (rk_serdes_debug_mode(serdes)) {
		if (serdes->rkx110_debug)
			rkx110_debugfs_init(&serdes->chip[DEVICE_LOCAL], serdes->debugfs_local);
		else
			rkx120_debugfs_init(&serdes->chip[DEVICE_LOCAL], serdes->debugfs_local);
	} else {
		serdes->debugfs_remote0 = debugfs_create_dir("remote0", serdes->debugfs_root);
		serdes->debugfs_remote1 = debugfs_create_dir("remote1", serdes->debugfs_root);

		if (serdes->stream_type == STREAM_DISPLAY) {
			rkx110_debugfs_init(&serdes->chip[DEVICE_LOCAL], serdes->debugfs_local);
			rkx120_debugfs_init(&serdes->chip[DEVICE_REMOTE0], serdes->debugfs_remote0);
			if (serdes->remote_nr == 2)
				rkx120_debugfs_init(&serdes->chip[DEVICE_REMOTE1],
						    serdes->debugfs_remote1);
		} else {
			rkx120_debugfs_init(&serdes->chip[DEVICE_LOCAL], serdes->debugfs_local);
			rkx110_debugfs_init(&serdes->chip[DEVICE_REMOTE0], serdes->debugfs_remote0);
			if (serdes->remote_nr == 2)
				rkx110_debugfs_init(&serdes->chip[DEVICE_REMOTE1],
						    serdes->debugfs_remote1);
		}

		rk_serdes_function_debugfs_init(serdes);
	}
#endif
}

static void rk_serdes_read_chip_id(struct rk_serdes *serdes)
{
	struct i2c_client *client;
	int i;
	u32 chip_id, local_id_reg, remote_id_reg, reg;
	u32 version = 0;

	if (serdes->stream_type == STREAM_DISPLAY) {
		local_id_reg = SER_GRF_CHIP_ID;
		remote_id_reg = DES_GRF_CHIP_ID;
	} else {
		local_id_reg = DES_GRF_CHIP_ID;
		remote_id_reg = SER_GRF_CHIP_ID;
	}

	for (i = 0; i <= serdes->remote_nr; i++) {
		client = serdes->chip[i].client;
		reg = i > 0 ? remote_id_reg : local_id_reg;

		serdes->i2c_read_reg(client, reg, &chip_id);
		if (i == 0)
			version = chip_id;
		dev_info(&client->dev, "device%d chip_id: 0x%x\n", i, chip_id);
	}

	if (version == SERDES_VERSION_V1(serdes->stream_type))
		serdes->version = SERDES_V1;
	else
		serdes->version = SERDES_V0;
}

static struct hwclk *rk_serdes_register_hwclk(struct rk_serdes *serdes, struct i2c_client *client,
					      int idx, int clktype)
{
	struct xferclk xfer;
	char name[16];

	snprintf(name, sizeof(name), "0x%x", client->addr);

	xfer.name = name;
	xfer.type = clktype;
	xfer.client = client;
	xfer.read = serdes->i2c_read_reg;
	xfer.write = serdes->i2c_write_reg;
	xfer.version = serdes->version;

	return hwclk_register(xfer);
}

static int rk_serdes_add_hwclk(struct rk_serdes *serdes)
{
	struct i2c_client *client;
	struct hwclk *hwclk;
	int clktype, local_clktype, remote_clktype;
	int i;

	if (serdes->stream_type == STREAM_DISPLAY) {
		local_clktype = CLK_RKX110;
		remote_clktype = CLK_RKX120;
	} else {
		local_clktype = CLK_RKX120;
		remote_clktype = CLK_RKX110;
	}

	for (i = 0; i <= serdes->remote_nr; i++) {
		client = serdes->chip[i].client;
		clktype = i > 0 ? remote_clktype : local_clktype;
		hwclk = rk_serdes_register_hwclk(serdes, client, i, clktype);
		if (!hwclk) {
			dev_err(serdes->dev, "Register hwclk for device%d clktype:%d failed:\n",
				i, clktype);
			return -EINVAL;
		}
		serdes->chip[i].hwclk = hwclk;
	}

	return 0;
}

static int rk_serdes_add_remote_i2c_device(struct rk_serdes *serdes)
{
	struct i2c_client *local_client = serdes->chip[DEVICE_LOCAL].client;
	struct i2c_client *client;
	int ret;
	u32 i2c_addr;

	ret = of_property_read_u32(serdes->dev->of_node, "remote0-addr", &i2c_addr);
	if (!ret) {
		client = devm_i2c_new_dummy_device(serdes->dev, local_client->adapter, i2c_addr);
		if (IS_ERR(client)) {
			dev_err(serdes->dev,
				"failed to alloc i2c client for remote0 i2c_addr:0x%x\n", i2c_addr);
			return -PTR_ERR(client);
		}

		serdes->chip[DEVICE_REMOTE0].client = client;
		serdes->chip[DEVICE_REMOTE0].is_remote = true;
		serdes->chip[DEVICE_REMOTE0].serdes = serdes;
		serdes->remote_nr++;
		i2c_set_clientdata(client, serdes);
	}

	ret = of_property_read_u32(serdes->dev->of_node, "remote1-addr", &i2c_addr);
	if (!ret) {
		if (serdes->remote_nr > 0) {
			u32 remote0_addr = serdes->chip[DEVICE_REMOTE0].client->addr;

			if (i2c_addr == remote0_addr) {
				dev_err(serdes->dev, "remote devices i2c addr must be different\n");
				return -EINVAL;
			}
		}

		client = devm_i2c_new_dummy_device(serdes->dev, local_client->adapter, i2c_addr);
		if (IS_ERR(client)) {
			dev_err(serdes->dev, "failed to alloc i2c device\n");
			return -PTR_ERR(client);
		}

		serdes->chip[DEVICE_REMOTE1].client = client;
		serdes->chip[DEVICE_REMOTE1].is_remote = true;
		serdes->chip[DEVICE_REMOTE1].serdes = serdes;
		serdes->remote_nr++;
		i2c_set_clientdata(client, serdes);
	}

	if (serdes->remote_nr == 2)
		serdes->lane_nr = 2;

	if (serdes->remote_nr == 0)
		return -ENODEV;

	return 0;
}

static int rk_serdes_pinctrl_init(struct rk_serdes *serdes)
{
	struct device_node *np;
	struct i2c_client *client;
	u32 *configs;
	char name[20] = "rk-serdes,pins";
	int length, i, ret;
	u32 client_id, bank, pins, pin_configs, pin_type;

	/* rk-serdes,pins = <client_id bank_id pins pin_configs>; */
	for_each_child_of_node(serdes->dev->of_node, np) {
		length = of_property_count_u32_elems(np, name);
		if (length < 0)
			continue;
		if (length % 4) {
			dev_err(serdes->dev, "Invalid count for pinctrl %s\n", np->name);
			return -EINVAL;
		}
		configs = kmalloc_array(length, sizeof(u32), GFP_KERNEL);
		if (!configs)
			return -ENOMEM;

		ret = of_property_read_u32_array(np, name, configs, length);
		if (ret) {
			dev_err(serdes->dev, "get %s configs data error\n", np->name);
			kfree(configs);
			return -EINVAL;
		}
		for (i = 0; i < length; i += 4) {
			client_id = configs[i];
			bank = configs[i + 1];
			pins = configs[i + 2];
			pin_configs = configs[i + 3];
			pin_type = client_id ? PIN_RKX120 : PIN_RKX110;
			client = serdes->chip[client_id].client;
			serdes->set_hwpin(serdes, client, pin_type, bank, pins, pin_configs);
			dev_dbg(serdes->dev, "%s: client_id %x, bank %x, pins %x pin_configs %x\n",
				np->name, client_id, bank, pins, pin_configs);
		}

		kfree(configs);
	}

	/* config irq pinctrl */
	if (serdes->stream_type == STREAM_DISPLAY) {
		serdes->set_hwpin(serdes, serdes->chip[DEVICE_LOCAL].client, PIN_RKX110,
				  RK_SERDES_SER_GPIO_BANK0, RK_SERDES_GPIO_PIN_A4,
				  RK_SERDES_PIN_CONFIG_MUX_FUNC2);
		serdes->set_hwpin(serdes, serdes->chip[DEVICE_REMOTE0].client, PIN_RKX120,
				  RK_SERDES_DES_GPIO_BANK0, RK_SERDES_GPIO_PIN_A4,
				  RK_SERDES_PIN_CONFIG_MUX_FUNC0);
		if (serdes->remote_nr == 2)
			serdes->set_hwpin(serdes, serdes->chip[DEVICE_REMOTE1].client, PIN_RKX120,
					  RK_SERDES_DES_GPIO_BANK0, RK_SERDES_GPIO_PIN_A4,
					  RK_SERDES_PIN_CONFIG_MUX_FUNC0);
	} else {
		serdes->set_hwpin(serdes, serdes->chip[DEVICE_REMOTE0].client, PIN_RKX110,
				  RK_SERDES_SER_GPIO_BANK0, RK_SERDES_GPIO_PIN_A4,
				  RK_SERDES_PIN_CONFIG_MUX_FUNC0);
		serdes->set_hwpin(serdes, serdes->chip[DEVICE_LOCAL].client, PIN_RKX120,
				  RK_SERDES_DES_GPIO_BANK0, RK_SERDES_GPIO_PIN_A4,
				  RK_SERDES_PIN_CONFIG_MUX_FUNC2);
		if (serdes->remote_nr == 2)
			serdes->set_hwpin(serdes, serdes->chip[DEVICE_REMOTE1].client, PIN_RKX110,
					  RK_SERDES_SER_GPIO_BANK0, RK_SERDES_GPIO_PIN_A4,
					  RK_SERDES_PIN_CONFIG_MUX_FUNC2);
	}

	return 0;
}

static int rk_serdes_irq_enable(struct rk_serdes *serdes)
{
	if (serdes->stream_type == STREAM_DISPLAY)
		rkx110_irq_enable(serdes, DEVICE_LOCAL);
	else
		rkx120_irq_enable(serdes, DEVICE_LOCAL);

	return 0;
}

__maybe_unused static int rk_serdes_irq_disable(struct rk_serdes *serdes)
{
	if (serdes->stream_type == STREAM_DISPLAY)
		rkx110_irq_disable(serdes, DEVICE_LOCAL);
	else
		rkx120_irq_disable(serdes, DEVICE_LOCAL);

	return 0;
}

static irqreturn_t rk_serdes_irq_handler(int irq, void *arg)
{
	struct rk_serdes *serdes = arg;

	if (serdes->stream_type == STREAM_DISPLAY)
		rkx110_irq_handler(serdes, DEVICE_LOCAL);
	else
		rkx120_irq_handler(serdes, DEVICE_LOCAL);

	return IRQ_HANDLED;
}

static int rk_serdes_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *disp_np;
	struct rk_serdes *serdes;
	int ret;
	bool dual_lane;

	serdes = devm_kzalloc(dev, sizeof(*serdes), GFP_KERNEL);
	if (!serdes)
		return -ENOMEM;

	serdes->dev = dev;
	serdes->chip[DEVICE_LOCAL].client = client;
	serdes->chip[DEVICE_LOCAL].is_remote = false;
	serdes->chip[DEVICE_LOCAL].serdes = serdes;
	i2c_set_clientdata(client, serdes);

	rk_serdes_add_callback(serdes);

	if (of_device_is_compatible(dev->of_node, "rockchip,rkx110-debug")) {
		serdes->rkx110_debug = true;
		dev_info(dev, "rkx110 debug mode");
	}

	if (of_device_is_compatible(dev->of_node, "rockchip,rkx120-debug")) {
		serdes->rkx120_debug = true;
		dev_info(dev, "rkx120 debug mode");
	}

	if (rk_serdes_debug_mode(serdes))
		goto out;

	serdes->rate = RATE_2GBPS_83M;

	serdes->supply = devm_regulator_get_optional(dev, "power");
	if (IS_ERR(serdes->supply)) {
		ret = PTR_ERR(serdes->supply);

		if (ret != -ENODEV)
			return dev_err_probe(dev, ret, "failed to request regulator\n");

		serdes->supply = NULL;
	}

	serdes->enable = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(serdes->enable)) {
		ret = PTR_ERR(serdes->enable);
		dev_err(dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	serdes->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(serdes->reset)) {
		ret = PTR_ERR(serdes->reset);
		dev_err(dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	serdes->irq_gpio = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
	if (IS_ERR(serdes->irq_gpio)) {
		ret = PTR_ERR(serdes->irq_gpio);
		dev_err(dev, "failed to request irq GPIO: %d\n", ret);
		return ret;
	}
	if (serdes->irq_gpio) {
		serdes->irq = gpiod_to_irq(serdes->irq_gpio);
		if (serdes->irq < 0)
			return dev_err_probe(dev, serdes->irq, "failed to get irq\n");

		irq_set_status_flags(serdes->irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(dev, serdes->irq, NULL,
						rk_serdes_irq_handler,
						IRQF_TRIGGER_LOW |
						IRQF_ONESHOT, "serdes-irq", serdes);
		if (ret) {
			dev_err(dev, "failed to request serdes interrupt\n");
			return ret;
		}
	} else {
		dev_warn(dev, "no support serdes irq function\n");
	}

	disp_np = of_get_child_by_name(dev->of_node, "serdes-panel");
	if (disp_np) {
		serdes->stream_type = STREAM_DISPLAY;
		of_node_put(disp_np);
		dev_info(dev, "serdes display stream");
	} else {
		serdes->stream_type = STREAM_CAMERA;
		dev_info(dev, "serdes camera stream");
	}

	ret = rk_serdes_add_remote_i2c_device(serdes);
	if (ret)
		return ret;

	if (serdes->remote_nr != 2) {
		dual_lane = device_property_read_bool(dev, "dual-lane");
		serdes->lane_nr = dual_lane ? 2 : 1;
	}

	ret = mfd_add_devices(dev, -1, rkx110_x120_devs, ARRAY_SIZE(rkx110_x120_devs),
			      NULL, 0, NULL);
	if (ret) {
		dev_err(dev, "failed to add subdev: %d\n", ret);
		return ret;
	}

	if (serdes->supply) {
		ret = regulator_enable(serdes->supply);
		if (ret < 0) {
			dev_err(serdes->dev, "failed to enable supply: %d\n", ret);
			return ret;
		}
	}

	gpiod_set_value(serdes->enable, 1);

	gpiod_set_value(serdes->reset, 1);
	usleep_range(10000, 11000);
	gpiod_set_value(serdes->reset, 0);

	msleep(20);

	rk_serdes_wait_link_ready(serdes);

	rk_serdes_read_chip_id(serdes);

	ret = rk_serdes_add_hwclk(serdes);
	if (ret < 0)
		goto err;

	rk_serdes_set_rate(serdes, RATE_4GBPS_83M);
	rk_serdes_pinctrl_init(serdes);
	rk_serdes_passthrough_init(serdes);
	rk_serdes_irq_enable(serdes);
	enable_irq(serdes->irq);

	if (serdes->stream_type == STREAM_DISPLAY)
		rk_serdes_display_route_init(serdes);

out:
	rk_serdes_debugfs_init(serdes);

	return 0;

err:
	if (serdes->supply)
		ret = regulator_disable(serdes->supply);

	return ret;
}

static int rk_serdes_i2c_remove(struct i2c_client *client)
{
	struct rk_serdes *rk_serdes = i2c_get_clientdata(client);

	if (rk_serdes->supply)
		regulator_disable(rk_serdes->supply);

	mfd_remove_devices(rk_serdes->dev);

	return 0;
}

static const struct of_device_id rk_serdes_of_match[] = {
	{ .compatible = "rockchip,rkx110", },
	{ .compatible = "rockchip,rkx110-debug", },
	{ .compatible = "rockchip,rkx120", },
	{ .compatible = "rockchip,rkx120-debug", },
	{}
};
MODULE_DEVICE_TABLE(of, rk_serdes_of_match);

static const struct i2c_device_id rk_serdes_i2c_id[] = {
	{ "rk_serdes", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rk_serdes_i2c_id);

static struct i2c_driver rk_serdes_i2c_driver = {
	.driver = {
		.name = "rk_serdes",
		.of_match_table = of_match_ptr(rk_serdes_of_match),
	},
	.probe = rk_serdes_i2c_probe,
	.remove = rk_serdes_i2c_remove,
	.id_table = rk_serdes_i2c_id,
};

static int __init rk_serdes_i2c_driver_init(void)
{
	debugfs_create_dir("rkserdes", NULL);
	return i2c_add_driver(&rk_serdes_i2c_driver);
}
module_init(rk_serdes_i2c_driver_init);

static void __exit rk_serdes_i2c_driver_exit(void)
{
	debugfs_remove_recursive(debugfs_lookup("rkserdes", NULL));
	i2c_del_driver(&rk_serdes_i2c_driver);
}
module_exit(rk_serdes_i2c_driver_exit);

MODULE_AUTHOR("Zhang Yubing <yubing.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK Parus MFD driver");
MODULE_LICENSE("GPL");
