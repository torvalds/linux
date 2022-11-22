// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */
#include <linux/debugfs.h>
#include "hal/pinctrl_api.h"
#include "rkx110_x120.h"
#include "rkx110_reg.h"
#include "serdes_combphy.h"

#if defined(CONFIG_DEBUG_FS)
static struct pattern_gen rkx110_pattern_gen[] = {
	{
		.name = "dsi0",
		.base = RKX110_PATTERN_GEN_DSI0_BASE,
		.link_src_reg = SER_GRF_SOC_CON4,
		.link_src_offset = 12,
	}, {
		.name = "dsi1",
		.base = RKX110_PATTERN_GEN_DSI1_BASE,
		.link_src_reg = SER_GRF_SOC_CON4,
		.link_src_offset = 13,
	}, {
		.name = "lvds0",
		.base = RKX110_PATTERN_GEN_LVDS0_BASE,
		.link_src_reg = SER_GRF_SOC_CON4,
		.link_src_offset = 14,
	}, {
		.name = "lvds1",
		.base = RKX110_PATTERN_GEN_LVDS1_BASE,
		.link_src_reg = SER_GRF_SOC_CON4,
		.link_src_offset = 15,
	},
	{ /* sentinel */ }
};

static const struct rk_serdes_reg rkx110_regs[] = {
	{
		.name = "cru",
		.reg_base = RKX110_SER_CRU_BASE,
		.reg_len = 0xF04,
	},
	{
		.name = "grf",
		.reg_base = RKX110_SER_GRF_BASE,
		.reg_len = 0x220,

	},
	{
		.name = "grf_mipi0",
		.reg_base = RKX110_GRF_MIPI0_BASE,
		.reg_len = 0x600,
	},
	{
		.name = "grf_mipi1",
		.reg_base = RKX110_GRF_MIPI1_BASE,
		.reg_len = 0x600,
	},
	{
		.name = "mipi_lvds_phy0",
		.reg_base = RKX110_MIPI_LVDS_RX_PHY0_BASE,
		.reg_len = 0xb0,
	},
	{
		.name = "mipi_lvds_phy1",
		.reg_base = RKX110_MIPI_LVDS_RX_PHY1_BASE,
		.reg_len = 0xb0,
	},

	{
		.name = "host0",
		.reg_base = RKX110_CSI2HOST0_BASE,
		.reg_len = 0x60,
	},
	{
		.name = "host1",
		.reg_base = RKX110_CSI2HOST1_BASE,
		.reg_len = 0x60,
	},
	{
		.name = "vicap",
		.reg_base = RKX110_VICAP_BASE,
		.reg_len = 0x220,
	},
	{
		.name = "gpio0",
		.reg_base = RKX110_GPIO0_BASE,
		.reg_len = 0x80,
	},
	{
		.name = "gpio1",
		.reg_base = RKX110_GPIO1_BASE,
		.reg_len = 0x80,
	},
	{
		.name = "dsi0",
		.reg_base = RKX110_DSI_RX0_BASE,
		.reg_len = 0x1D0,
	},
	{
		.name = "dsi1",
		.reg_base = RKX110_DSI_RX1_BASE,
		.reg_len = 0x1D0,
	},
	{
		.name = "rklink",
		.reg_base = RKX110_SER_RKLINK_BASE,
		.reg_len = 0xD4,
	},
	{
		.name = "pcs0",
		.reg_base = RKX110_SER_PCS0_BASE,
		.reg_len = 0x1c0,
	},
	{
		.name = "pcs1",
		.reg_base = RKX110_SER_PCS1_BASE,
		.reg_len = 0x1c0,
	},
	{
		.name = "pma0",
		.reg_base = RKX110_SER_PMA0_BASE,
		.reg_len = 0x100,
	},
	{
		.name = "pma1",
		.reg_base = RKX110_SER_PMA1_BASE,
		.reg_len = 0x100,
	},
	{
		.name = "dsi0_pattern_gen",
		.reg_base = RKX110_PATTERN_GEN_DSI0_BASE,
		.reg_len = 0x18,
	},
	{
		.name = "dsi1_pattern_gen",
		.reg_base = RKX110_PATTERN_GEN_DSI1_BASE,
		.reg_len = 0x18,
	},
	{
		.name = "lvds0_pattern_gen",
		.reg_base = RKX110_PATTERN_GEN_LVDS0_BASE,
		.reg_len = 0x18,
	},
	{
		.name = "lvds1_pattern_gen",
		.reg_base = RKX110_PATTERN_GEN_LVDS1_BASE,
		.reg_len = 0x18,
	},
	{ /* sentinel */ }
};

static int rkx110_reg_show(struct seq_file *s, void *v)
{
	const struct rk_serdes_reg *regs = rkx110_regs;
	struct rk_serdes_chip *chip = s->private;
	struct rk_serdes *serdes = chip->serdes;
	struct i2c_client *client = chip->client;
	int i;
	u32 val = 0;

	seq_printf(s, "rkx110_%s:\n", file_dentry(s->file)->d_iname);

	while (regs->name) {
		if (!strcmp(regs->name, file_dentry(s->file)->d_iname))
			break;
		regs++;
	}

	if (!regs->name)
		return -ENODEV;

	for (i = 0; i <= regs->reg_len; i += 4) {
		serdes->i2c_read_reg(client, regs->reg_base + i, &val);

		if (i % 16 == 0)
			seq_printf(s, "\n0x%04x:", i);
		seq_printf(s, " %08x", val);
	}
	seq_puts(s, "\n");

	return 0;
}

static ssize_t rkx110_reg_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	const struct rk_serdes_reg *regs = rkx110_regs;
	struct rk_serdes_chip *chip = file->f_path.dentry->d_inode->i_private;
	struct rk_serdes *serdes = chip->serdes;
	struct i2c_client *client = chip->client;
	u32 addr;
	u32 val;
	char kbuf[25];
	int ret;

	if (count >= sizeof(kbuf))
		return -ENOSPC;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	ret = sscanf(kbuf, "%x%x", &addr, &val);
	if (ret != 2)
		return -EINVAL;

	while (regs->name) {
		if (!strcmp(regs->name, file_dentry(file)->d_iname))
			break;
		regs++;
	}

	if (!regs->name)
		return -ENODEV;

	addr += regs->reg_base;

	serdes->i2c_write_reg(client, addr, val);

	return count;
}

static int rkx110_reg_open(struct inode *inode, struct file *file)
{
	struct rk_serdes_chip *chip = inode->i_private;

	return single_open(file, rkx110_reg_show, chip);
}

static const struct file_operations rkx110_reg_fops = {
	.owner          = THIS_MODULE,
	.open           = rkx110_reg_open,
	.read           = seq_read,
	.write          = rkx110_reg_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

void rkx110_debugfs_init(struct rk_serdes_chip *chip, struct dentry *dentry)
{
	struct pattern_gen *pattern_gen = rkx110_pattern_gen;
	const struct rk_serdes_reg *regs = rkx110_regs;
	struct dentry *dir;

	dir = debugfs_create_dir("registers", dentry);
	if (!IS_ERR(dir)) {
		while (regs->name) {
			debugfs_create_file(regs->name, 0600, dir, chip, &rkx110_reg_fops);
			regs++;
		}
	}

	dir = debugfs_create_dir("pattern_gen", dentry);
	if (!IS_ERR(dir)) {
		while (pattern_gen->name) {
			rkx110_x120_pattern_gen_debugfs_create_file(pattern_gen, chip, dir);
			pattern_gen++;
		}
	}
}
#endif

static int rkx110_rgb_rx_iomux_cfg(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	struct i2c_client *client = serdes->chip[DEVICE_LOCAL].client;
	uint32_t pins;

	pins = RK_SERDES_GPIO_PIN_C0 | RK_SERDES_GPIO_PIN_C1 | RK_SERDES_GPIO_PIN_C2 |
	       RK_SERDES_GPIO_PIN_C3 | RK_SERDES_GPIO_PIN_C4 | RK_SERDES_GPIO_PIN_C5 |
	       RK_SERDES_GPIO_PIN_C6 | RK_SERDES_GPIO_PIN_C7;
	serdes->set_hwpin(serdes, client, PIN_RKX110, RK_SERDES_SER_GPIO_BANK0, pins,
			  RK_SERDES_PIN_CONFIG_MUX_FUNC1);

	pins = RK_SERDES_GPIO_PIN_A0 | RK_SERDES_GPIO_PIN_A1 | RK_SERDES_GPIO_PIN_A2 |
	       RK_SERDES_GPIO_PIN_A3 | RK_SERDES_GPIO_PIN_A4 | RK_SERDES_GPIO_PIN_A5 |
	       RK_SERDES_GPIO_PIN_A6 | RK_SERDES_GPIO_PIN_A7 | RK_SERDES_GPIO_PIN_B0 |
	       RK_SERDES_GPIO_PIN_B1 | RK_SERDES_GPIO_PIN_B2 | RK_SERDES_GPIO_PIN_B3 |
	       RK_SERDES_GPIO_PIN_B4 | RK_SERDES_GPIO_PIN_B5 | RK_SERDES_GPIO_PIN_B6 |
	       RK_SERDES_GPIO_PIN_B7 | RK_SERDES_GPIO_PIN_C0 | RK_SERDES_GPIO_PIN_C1 |
	       RK_SERDES_GPIO_PIN_C2 | RK_SERDES_GPIO_PIN_C3;
	serdes->set_hwpin(serdes, client, PIN_RKX110, RK_SERDES_SER_GPIO_BANK1, pins,
			  RK_SERDES_PIN_CONFIG_MUX_FUNC1);

	return 0;
}

int rkx110_rgb_rx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	rkx110_rgb_rx_iomux_cfg(serdes, route);

	return 0;
}

int rkx110_lvds_rx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route, int id)
{
	rkx110_combrxphy_set_mode(serdes, COMBRX_PHY_MODE_VIDEO_LVDS);

	rkx110_combrxphy_power_on(serdes, id ? COMBPHY_1 : COMBPHY_0);

	return 0;
}

