/*
 * Rockchip USB 3.0 PHY with Innosilicon IP block driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/phy.h>
#include <linux/uaccess.h>

#define U3PHY_PORT_NUM	2
#define U3PHY_MAX_CLKS	4
#define BIT_WRITEABLE_SHIFT	16
#define SCHEDULE_DELAY	(60 * HZ)

#define U3PHY_APB_RST	BIT(0)
#define U3PHY_POR_RST	BIT(1)
#define U3PHY_MAC_RST	BIT(2)

struct rockchip_u3phy;
struct rockchip_u3phy_port;

enum rockchip_u3phy_type {
	U3PHY_TYPE_PIPE,
	U3PHY_TYPE_UTMI,
};

enum rockchip_u3phy_pipe_pwr {
	PIPE_PWR_P0	= 0,
	PIPE_PWR_P1	= 1,
	PIPE_PWR_P2	= 2,
	PIPE_PWR_P3	= 3,
	PIPE_PWR_MAX	= 4,
};

enum rockchip_u3phy_rest_req {
	U3_POR_RSTN	= 0,
	U2_POR_RSTN	= 1,
	PIPE_MAC_RSTN	= 2,
	UTMI_MAC_RSTN	= 3,
	PIPE_APB_RSTN	= 4,
	UTMI_APB_RSTN	= 5,
	U3PHY_RESET_MAX	= 6,
};

enum rockchip_u3phy_utmi_state {
	PHY_UTMI_HS_ONLINE	= 0,
	PHY_UTMI_DISCONNECT	= 1,
	PHY_UTMI_CONNECT	= 2,
	PHY_UTMI_FS_LS_ONLINE	= 4,
};

/*
 * @rvalue: reset value
 * @dvalue: desired value
 */
struct u3phy_reg {
	unsigned int	offset;
	unsigned int	bitend;
	unsigned int	bitstart;
	unsigned int	rvalue;
	unsigned int	dvalue;
};

struct rockchip_u3phy_grfcfg {
	struct u3phy_reg	um_suspend;
	struct u3phy_reg	ls_det_en;
	struct u3phy_reg	ls_det_st;
	struct u3phy_reg	um_ls;
	struct u3phy_reg	um_hstdct;
	struct u3phy_reg	u2_only_ctrl;
	struct u3phy_reg	u3_disable;
	struct u3phy_reg	pp_pwr_st;
	struct u3phy_reg	pp_pwr_en[PIPE_PWR_MAX];
};

/**
 * struct rockchip_u3phy_apbcfg: usb3-phy apb configuration.
 * @u2_pre_emp: usb2-phy pre-emphasis tuning.
 * @u2_pre_emp_sth: usb2-phy pre-emphasis strength tuning.
 * @u2_odt_tuning: usb2-phy odt 45ohm tuning.
 */
struct rockchip_u3phy_apbcfg {
	unsigned int	u2_pre_emp;
	unsigned int	u2_pre_emp_sth;
	unsigned int	u2_odt_tuning;
};

struct rockchip_u3phy_cfg {
	unsigned int reg;
	const struct rockchip_u3phy_grfcfg grfcfg;

	int (*phy_pipe_power)(struct rockchip_u3phy *,
			      struct rockchip_u3phy_port *,
			      bool on);
	int (*phy_tuning)(struct rockchip_u3phy *,
			  struct rockchip_u3phy_port *,
			  struct device_node *);
	int (*phy_cp_test)(struct rockchip_u3phy *,
			   struct rockchip_u3phy_port *);
};

struct rockchip_u3phy_port {
	struct phy	*phy;
	void __iomem	*base;
	unsigned int	index;
	unsigned char	type;
	bool		suspended;
	bool		refclk_25m_quirk;
	struct mutex	mutex; /* mutex for updating register */
	struct delayed_work	um_sm_work;
};

struct rockchip_u3phy {
	struct device *dev;
	struct regmap *u3phy_grf;
	struct regmap *grf;
	int um_ls_irq;
	struct clk *clks[U3PHY_MAX_CLKS];
	struct dentry *root;
	struct regulator *vbus;
	struct reset_control *rsts[U3PHY_RESET_MAX];
	struct rockchip_u3phy_apbcfg apbcfg;
	const struct rockchip_u3phy_cfg *cfgs;
	struct rockchip_u3phy_port ports[U3PHY_PORT_NUM];
	struct usb_phy usb_phy;
	bool vbus_enabled;
};

static inline int param_write(void __iomem *base,
			      const struct u3phy_reg *reg, bool desired)
{
	unsigned int val, mask;
	unsigned int tmp = desired ? reg->dvalue : reg->rvalue;
	int ret = 0;

	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);
	ret = regmap_write(base, reg->offset, val);

	return ret;
}

static inline bool param_exped(void __iomem *base,
			       const struct u3phy_reg *reg,
			       unsigned int value)
{
	int ret;
	unsigned int tmp, orig;
	unsigned int mask = GENMASK(reg->bitend, reg->bitstart);

	ret = regmap_read(base, reg->offset, &orig);
	if (ret)
		return false;

	tmp = (orig & mask) >> reg->bitstart;
	return tmp == value;
}

static int rockchip_set_vbus_power(struct rockchip_u3phy *u3phy, bool en)
{
	int ret = 0;

	if (!u3phy->vbus)
		return 0;

	if (en && !u3phy->vbus_enabled) {
		ret = regulator_enable(u3phy->vbus);
		if (ret)
			dev_err(u3phy->dev,
				"Failed to enable VBUS supply\n");
	} else if (!en && u3phy->vbus_enabled) {
		ret = regulator_disable(u3phy->vbus);
	}

	if (ret == 0)
		u3phy->vbus_enabled = en;

	return ret;
}

static int rockchip_u3phy_usb2_only_show(struct seq_file *s, void *unused)
{
	struct rockchip_u3phy	*u3phy = s->private;

	if (param_exped(u3phy->u3phy_grf, &u3phy->cfgs->grfcfg.u2_only_ctrl, 1))
		dev_info(u3phy->dev, "u2\n");
	else
		dev_info(u3phy->dev, "u3\n");

	return 0;
}

static int rockchip_u3phy_usb2_only_open(struct inode *inode,
					 struct file *file)
{
	return single_open(file, rockchip_u3phy_usb2_only_show,
			   inode->i_private);
}

static ssize_t rockchip_u3phy_usb2_only_write(struct file *file,
					      const char __user *ubuf,
					      size_t count, loff_t *ppos)
{
	struct seq_file			*s = file->private_data;
	struct rockchip_u3phy		*u3phy = s->private;
	struct rockchip_u3phy_port	*u3phy_port;
	char				buf[32];
	u8				index;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "u3", 2) &&
	    param_exped(u3phy->u3phy_grf,
			&u3phy->cfgs->grfcfg.u2_only_ctrl, 1)) {
		dev_info(u3phy->dev, "Set usb3.0 and usb2.0 mode successfully\n");

		rockchip_set_vbus_power(u3phy, false);

		param_write(u3phy->grf,
			    &u3phy->cfgs->grfcfg.u3_disable, false);
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.u2_only_ctrl, false);

		for (index = 0; index < U3PHY_PORT_NUM; index++) {
			u3phy_port = &u3phy->ports[index];
			/* enable u3 rx termimation */
			if (u3phy_port->type == U3PHY_TYPE_PIPE)
				writel(0x30, u3phy_port->base + 0xd8);
		}

		atomic_notifier_call_chain(&u3phy->usb_phy.notifier, 0, NULL);

		rockchip_set_vbus_power(u3phy, true);
	} else if (!strncmp(buf, "u2", 2) &&
		   param_exped(u3phy->u3phy_grf,
			       &u3phy->cfgs->grfcfg.u2_only_ctrl, 0)) {
		dev_info(u3phy->dev, "Set usb2.0 only mode successfully\n");

		rockchip_set_vbus_power(u3phy, false);

		param_write(u3phy->grf,
			    &u3phy->cfgs->grfcfg.u3_disable, true);
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.u2_only_ctrl, true);

		for (index = 0; index < U3PHY_PORT_NUM; index++) {
			u3phy_port = &u3phy->ports[index];
			/* disable u3 rx termimation */
			if (u3phy_port->type == U3PHY_TYPE_PIPE)
				writel(0x20, u3phy_port->base + 0xd8);
		}

		atomic_notifier_call_chain(&u3phy->usb_phy.notifier, 0, NULL);

		rockchip_set_vbus_power(u3phy, true);
	} else {
		dev_info(u3phy->dev, "Same or illegal mode\n");
	}

	return count;
}

static const struct file_operations rockchip_u3phy_usb2_only_fops = {
	.open			= rockchip_u3phy_usb2_only_open,
	.write			= rockchip_u3phy_usb2_only_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

int rockchip_u3phy_debugfs_init(struct rockchip_u3phy *u3phy)
{
	struct dentry		*root;
	struct dentry		*file;
	int			ret;

	root = debugfs_create_dir(dev_name(u3phy->dev), NULL);
	if (!root) {
		ret = -ENOMEM;
		goto err0;
	}

	u3phy->root = root;

	file = debugfs_create_file("u3phy_mode", 0644, root,
				   u3phy, &rockchip_u3phy_usb2_only_fops);
	if (!file) {
		ret = -ENOMEM;
		goto err1;
	}
	return 0;

err1:
	debugfs_remove_recursive(root);
err0:
	return ret;
}

static const char *get_rest_name(enum rockchip_u3phy_rest_req rst)
{
	switch (rst) {
	case U2_POR_RSTN:
		return "u3phy-u2-por";
	case U3_POR_RSTN:
		return "u3phy-u3-por";
	case PIPE_MAC_RSTN:
		return "u3phy-pipe-mac";
	case UTMI_MAC_RSTN:
		return "u3phy-utmi-mac";
	case UTMI_APB_RSTN:
		return "u3phy-utmi-apb";
	case PIPE_APB_RSTN:
		return "u3phy-pipe-apb";
	default:
		return "invalid";
	}
}

static void rockchip_u3phy_rest_deassert(struct rockchip_u3phy *u3phy,
					 unsigned int flag)
{
	int rst;

	if (flag & U3PHY_APB_RST) {
		dev_dbg(u3phy->dev, "deassert APB bus interface reset\n");
		for (rst = PIPE_APB_RSTN; rst <= UTMI_APB_RSTN; rst++) {
			if (u3phy->rsts[rst])
				reset_control_deassert(u3phy->rsts[rst]);
		}
	}

	if (flag & U3PHY_POR_RST) {
		usleep_range(12, 15);
		dev_dbg(u3phy->dev, "deassert u2 and u3 phy power on reset\n");
		for (rst = U3_POR_RSTN; rst <= U2_POR_RSTN; rst++) {
			if (u3phy->rsts[rst])
				reset_control_deassert(u3phy->rsts[rst]);
		}
	}

	if (flag & U3PHY_MAC_RST) {
		usleep_range(1200, 1500);
		dev_dbg(u3phy->dev, "deassert pipe and utmi MAC reset\n");
		for (rst = PIPE_MAC_RSTN; rst <= UTMI_MAC_RSTN; rst++)
			if (u3phy->rsts[rst])
				reset_control_deassert(u3phy->rsts[rst]);
	}
}

static void rockchip_u3phy_rest_assert(struct rockchip_u3phy *u3phy)
{
	int rst;

	dev_dbg(u3phy->dev, "assert u3phy reset\n");
	for (rst = 0; rst < U3PHY_RESET_MAX; rst++)
		if (u3phy->rsts[rst])
			reset_control_assert(u3phy->rsts[rst]);
}

static int rockchip_u3phy_clk_enable(struct rockchip_u3phy *u3phy)
{
	int ret, clk;

	for (clk = 0; clk < U3PHY_MAX_CLKS && u3phy->clks[clk]; clk++) {
		ret = clk_prepare_enable(u3phy->clks[clk]);
		if (ret)
			goto err_disable_clks;
	}
	return 0;

err_disable_clks:
	while (--clk >= 0)
		clk_disable_unprepare(u3phy->clks[clk]);
	return ret;
}

static void rockchip_u3phy_clk_disable(struct rockchip_u3phy *u3phy)
{
	int clk;

	for (clk = U3PHY_MAX_CLKS - 1; clk >= 0; clk--)
		if (u3phy->clks[clk])
			clk_disable_unprepare(u3phy->clks[clk]);
}

static int rockchip_u3phy_init(struct phy *phy)
{
	return 0;
}

static int rockchip_u3phy_exit(struct phy *phy)
{
	return 0;
}

static int rockchip_u3phy_power_on(struct phy *phy)
{
	struct rockchip_u3phy_port *u3phy_port = phy_get_drvdata(phy);
	struct rockchip_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);
	int ret;

	dev_info(&u3phy_port->phy->dev, "u3phy %s power on\n",
		 (u3phy_port->type == U3PHY_TYPE_UTMI) ? "u2" : "u3");

	if (!u3phy_port->suspended)
		return 0;

	ret = rockchip_u3phy_clk_enable(u3phy);
	if (ret)
		return ret;

	if (u3phy_port->type == U3PHY_TYPE_UTMI) {
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.um_suspend, false);
	} else {
		/* current in p2 ? */
		if (param_exped(u3phy->u3phy_grf,
				&u3phy->cfgs->grfcfg.pp_pwr_st, PIPE_PWR_P2))
			goto done;

		if (u3phy->cfgs->phy_pipe_power) {
			dev_dbg(u3phy->dev, "do pipe power up\n");
			u3phy->cfgs->phy_pipe_power(u3phy, u3phy_port, true);
		}

		/* exit to p0 */
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P0], true);
		usleep_range(90, 100);

		/* enter to p2 from p0 */
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P2],
			    false);
		udelay(3);
	}

done:
	rockchip_set_vbus_power(u3phy, true);
	u3phy_port->suspended = false;
	return 0;
}

static int rockchip_u3phy_power_off(struct phy *phy)
{
	struct rockchip_u3phy_port *u3phy_port = phy_get_drvdata(phy);
	struct rockchip_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);

	dev_info(&u3phy_port->phy->dev, "u3phy %s power off\n",
		 (u3phy_port->type == U3PHY_TYPE_UTMI) ? "u2" : "u3");

	if (u3phy_port->suspended)
		return 0;

	if (u3phy_port->type == U3PHY_TYPE_UTMI) {
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.um_suspend, true);
	} else {
		/* current in p3 ? */
		if (param_exped(u3phy->u3phy_grf,
				&u3phy->cfgs->grfcfg.pp_pwr_st, PIPE_PWR_P3))
			goto done;

		/* exit to p0 */
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P0], true);
		udelay(2);

		/* enter to p3 from p0 */
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P3], true);
		udelay(6);

		if (u3phy->cfgs->phy_pipe_power) {
			dev_dbg(u3phy->dev, "do pipe power down\n");
			u3phy->cfgs->phy_pipe_power(u3phy, u3phy_port, false);
		}
	}

done:
	rockchip_u3phy_clk_disable(u3phy);
	u3phy_port->suspended = true;
	return 0;
}

static int rockchip_u3phy_cp_test(struct phy *phy)
{
	struct rockchip_u3phy_port *u3phy_port = phy_get_drvdata(phy);
	struct rockchip_u3phy *u3phy = dev_get_drvdata(phy->dev.parent);
	int ret;

	if (u3phy->cfgs->phy_cp_test) {
		/*
		 * When do USB3 compliance test, we may connect the oscilloscope
		 * front panel Aux Out to the DUT SSRX+, the Aux Out of the
		 * oscilloscope outputs a negative pulse whose width is between
		 * 300- 400 ns which may trigger some DUTs to change the CP test
		 * pattern.
		 *
		 * The Inno USB3 PHY disable the function to detect the negative
		 * pulse in SSRX+ by default, so we need to enable the function
		 * to toggle the CP test pattern before do USB3 compliance test.
		 */
		dev_dbg(u3phy->dev, "prepare for u3phy compliance test\n");
		ret = u3phy->cfgs->phy_cp_test(u3phy, u3phy_port);
		if (ret)
			return ret;
	}

	return 0;
}

static __maybe_unused
struct phy *rockchip_u3phy_xlate(struct device *dev,
				 struct of_phandle_args *args)
{
	struct rockchip_u3phy *u3phy = dev_get_drvdata(dev);
	struct rockchip_u3phy_port *u3phy_port = NULL;
	struct device_node *phy_np = args->np;
	int index;

	if (args->args_count != 1) {
		dev_err(dev, "invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	for (index = 0; index < U3PHY_PORT_NUM; index++) {
		if (phy_np == u3phy->ports[index].phy->dev.of_node) {
			u3phy_port = &u3phy->ports[index];
			break;
		}
	}

	if (!u3phy_port) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	return u3phy_port->phy;
}

static struct phy_ops rockchip_u3phy_ops = {
	.init		= rockchip_u3phy_init,
	.exit		= rockchip_u3phy_exit,
	.power_on	= rockchip_u3phy_power_on,
	.power_off	= rockchip_u3phy_power_off,
	.cp_test	= rockchip_u3phy_cp_test,
	.owner		= THIS_MODULE,
};

/*
 * The function manage host-phy port state and suspend/resume phy port
 * to save power automatically.
 *
 * we rely on utmi_linestate and utmi_hostdisconnect to identify whether
 * devices is disconnect or not. Besides, we do not need care it is FS/LS
 * disconnected or HS disconnected, actually, we just only need get the
 * device is disconnected at last through rearm the delayed work,
 * to suspend the phy port in _PHY_STATE_DISCONNECT_ case.
 */
static void rockchip_u3phy_um_sm_work(struct work_struct *work)
{
	struct rockchip_u3phy_port *u3phy_port =
		container_of(work, struct rockchip_u3phy_port, um_sm_work.work);
	struct rockchip_u3phy *u3phy =
		dev_get_drvdata(u3phy_port->phy->dev.parent);
	unsigned int sh = u3phy->cfgs->grfcfg.um_hstdct.bitend -
			u3phy->cfgs->grfcfg.um_hstdct.bitstart + 1;
	unsigned int ul, uhd, state;
	unsigned int ul_mask, uhd_mask;
	int ret;

	mutex_lock(&u3phy_port->mutex);

	ret = regmap_read(u3phy->u3phy_grf,
			  u3phy->cfgs->grfcfg.um_ls.offset, &ul);
	if (ret < 0)
		goto next_schedule;

	ret = regmap_read(u3phy->u3phy_grf,
			  u3phy->cfgs->grfcfg.um_hstdct.offset, &uhd);
	if (ret < 0)
		goto next_schedule;

	uhd_mask = GENMASK(u3phy->cfgs->grfcfg.um_hstdct.bitend,
			   u3phy->cfgs->grfcfg.um_hstdct.bitstart);
	ul_mask = GENMASK(u3phy->cfgs->grfcfg.um_ls.bitend,
			  u3phy->cfgs->grfcfg.um_ls.bitstart);

	/* stitch on um_ls and um_hstdct as phy state */
	state = ((uhd & uhd_mask) >> u3phy->cfgs->grfcfg.um_hstdct.bitstart) |
		(((ul & ul_mask) >> u3phy->cfgs->grfcfg.um_ls.bitstart) << sh);

	switch (state) {
	case PHY_UTMI_HS_ONLINE:
		dev_dbg(&u3phy_port->phy->dev, "HS online\n");
		break;
	case PHY_UTMI_FS_LS_ONLINE:
		/*
		 * For FS/LS device, the online state share with connect state
		 * from um_ls and um_hstdct register, so we distinguish
		 * them via suspended flag.
		 *
		 * Plus, there are two cases, one is D- Line pull-up, and D+
		 * line pull-down, the state is 4; another is D+ line pull-up,
		 * and D- line pull-down, the state is 2.
		 */
		if (!u3phy_port->suspended) {
			/* D- line pull-up, D+ line pull-down */
			dev_dbg(&u3phy_port->phy->dev, "FS/LS online\n");
			break;
		}
		/* fall through */
	case PHY_UTMI_CONNECT:
		if (u3phy_port->suspended) {
			dev_dbg(&u3phy_port->phy->dev, "Connected\n");
			rockchip_u3phy_power_on(u3phy_port->phy);
			u3phy_port->suspended = false;
		} else {
			/* D+ line pull-up, D- line pull-down */
			dev_dbg(&u3phy_port->phy->dev, "FS/LS online\n");
		}
		break;
	case PHY_UTMI_DISCONNECT:
		if (!u3phy_port->suspended) {
			dev_dbg(&u3phy_port->phy->dev, "Disconnected\n");
			rockchip_u3phy_power_off(u3phy_port->phy);
			u3phy_port->suspended = true;
		}

		/*
		 * activate the linestate detection to get the next device
		 * plug-in irq.
		 */
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.ls_det_st, true);
		param_write(u3phy->u3phy_grf,
			    &u3phy->cfgs->grfcfg.ls_det_en, true);

		/*
		 * we don't need to rearm the delayed work when the phy port
		 * is suspended.
		 */
		mutex_unlock(&u3phy_port->mutex);
		return;
	default:
		dev_dbg(&u3phy_port->phy->dev, "unknown phy state\n");
		break;
	}

next_schedule:
	mutex_unlock(&u3phy_port->mutex);
	schedule_delayed_work(&u3phy_port->um_sm_work, SCHEDULE_DELAY);
}

static irqreturn_t rockchip_u3phy_um_ls_irq(int irq, void *data)
{
	struct rockchip_u3phy_port *u3phy_port = data;
	struct rockchip_u3phy *u3phy =
		dev_get_drvdata(u3phy_port->phy->dev.parent);

	if (!param_exped(u3phy->u3phy_grf,
			 &u3phy->cfgs->grfcfg.ls_det_st,
			 u3phy->cfgs->grfcfg.ls_det_st.dvalue))
		return IRQ_NONE;

	dev_dbg(u3phy->dev, "utmi linestate interrupt\n");
	mutex_lock(&u3phy_port->mutex);

	/* disable linestate detect irq and clear its status */
	param_write(u3phy->u3phy_grf, &u3phy->cfgs->grfcfg.ls_det_en, false);
	param_write(u3phy->u3phy_grf, &u3phy->cfgs->grfcfg.ls_det_st, true);

	mutex_unlock(&u3phy_port->mutex);

	/*
	 * In this case for host phy, a new device is plugged in, meanwhile,
	 * if the phy port is suspended, we need rearm the work to resume it
	 * and mange its states; otherwise, we just return irq handled.
	 */
	if (u3phy_port->suspended) {
		dev_dbg(u3phy->dev, "schedule utmi sm work\n");
		rockchip_u3phy_um_sm_work(&u3phy_port->um_sm_work.work);
	}

	return IRQ_HANDLED;
}

static int rockchip_u3phy_parse_dt(struct rockchip_u3phy *u3phy,
				   struct platform_device *pdev)

{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret, i, clk;

	u3phy->um_ls_irq = platform_get_irq_byname(pdev, "linestate");
	if (u3phy->um_ls_irq < 0) {
		dev_err(dev, "get utmi linestate irq failed\n");
		return -ENXIO;
	}

	/* Get Vbus regulators */
	u3phy->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(u3phy->vbus)) {
		ret = PTR_ERR(u3phy->vbus);
		if (ret == -EPROBE_DEFER)
			return ret;

		dev_warn(dev, "Failed to get VBUS supply regulator\n");
		u3phy->vbus = NULL;
	}

	for (clk = 0; clk < U3PHY_MAX_CLKS; clk++) {
		u3phy->clks[clk] = of_clk_get(np, clk);
		if (IS_ERR(u3phy->clks[clk])) {
			ret = PTR_ERR(u3phy->clks[clk]);
			if (ret == -EPROBE_DEFER)
				goto err_put_clks;
			u3phy->clks[clk] = NULL;
			break;
		}
	}

	for (i = 0; i < U3PHY_RESET_MAX; i++) {
		u3phy->rsts[i] = devm_reset_control_get(dev, get_rest_name(i));
		if (IS_ERR(u3phy->rsts[i])) {
			dev_info(dev, "no %s reset control specified\n",
				 get_rest_name(i));
			u3phy->rsts[i] = NULL;
		}
	}

	return 0;

err_put_clks:
	while (--clk >= 0)
		clk_put(u3phy->clks[clk]);
	return ret;
}

static int rockchip_u3phy_port_init(struct rockchip_u3phy *u3phy,
				    struct rockchip_u3phy_port *u3phy_port,
				    struct device_node *child_np)
{
	struct resource res;
	struct phy *phy;
	int ret;

	dev_dbg(u3phy->dev, "u3phy port initialize\n");

	mutex_init(&u3phy_port->mutex);
	u3phy_port->suspended = true; /* initial status */

	phy = devm_phy_create(u3phy->dev, child_np, &rockchip_u3phy_ops);
	if (IS_ERR(phy)) {
		dev_err(u3phy->dev, "failed to create phy\n");
		return PTR_ERR(phy);
	}

	u3phy_port->phy = phy;

	ret = of_address_to_resource(child_np, 0, &res);
	if (ret) {
		dev_err(u3phy->dev, "failed to get address resource(np-%s)\n",
			child_np->name);
		return ret;
	}

	u3phy_port->base = devm_ioremap_resource(&u3phy_port->phy->dev, &res);
	if (IS_ERR(u3phy_port->base)) {
		dev_err(u3phy->dev, "failed to remap phy regs\n");
		return PTR_ERR(u3phy_port->base);
	}

	if (!of_node_cmp(child_np->name, "pipe")) {
		u3phy_port->type = U3PHY_TYPE_PIPE;
		u3phy_port->refclk_25m_quirk =
			of_property_read_bool(child_np,
					      "rockchip,refclk-25m-quirk");
	} else {
		u3phy_port->type = U3PHY_TYPE_UTMI;
		INIT_DELAYED_WORK(&u3phy_port->um_sm_work,
				  rockchip_u3phy_um_sm_work);

		ret = devm_request_threaded_irq(u3phy->dev, u3phy->um_ls_irq,
						NULL, rockchip_u3phy_um_ls_irq,
						IRQF_ONESHOT, "rockchip_u3phy",
						u3phy_port);
		if (ret) {
			dev_err(u3phy->dev, "failed to request utmi linestate irq handle\n");
			return ret;
		}
	}

	if (u3phy->cfgs->phy_tuning) {
		dev_dbg(u3phy->dev, "do u3phy tuning\n");
		ret = u3phy->cfgs->phy_tuning(u3phy, u3phy_port, child_np);
		if (ret)
			return ret;
	}

	phy_set_drvdata(u3phy_port->phy, u3phy_port);
	return 0;
}

static int rockchip_u3phy_on_init(struct usb_phy *usb_phy)
{
	struct rockchip_u3phy *u3phy =
		container_of(usb_phy, struct rockchip_u3phy, usb_phy);

	rockchip_u3phy_rest_deassert(u3phy, U3PHY_POR_RST | U3PHY_MAC_RST);
	return 0;
}

static void rockchip_u3phy_on_shutdown(struct usb_phy *usb_phy)
{
	struct rockchip_u3phy *u3phy =
		container_of(usb_phy, struct rockchip_u3phy, usb_phy);
	int rst;

	for (rst = 0; rst < U3PHY_RESET_MAX; rst++)
		if (u3phy->rsts[rst] && rst != UTMI_APB_RSTN &&
		    rst != PIPE_APB_RSTN)
			reset_control_assert(u3phy->rsts[rst]);
	udelay(1);
}

static int rockchip_u3phy_on_disconnect(struct usb_phy *usb_phy,
					enum usb_device_speed speed)
{
	struct rockchip_u3phy *u3phy =
		container_of(usb_phy, struct rockchip_u3phy, usb_phy);

	dev_info(u3phy->dev, "%s device has disconnected\n",
		 (speed == USB_SPEED_SUPER) ? "U3" : "UW/U2/U1.1/U1");

	if (speed == USB_SPEED_SUPER)
		atomic_notifier_call_chain(&usb_phy->notifier, 0, NULL);

	return 0;
}

static int rockchip_u3phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct phy_provider *provider;
	struct rockchip_u3phy *u3phy;
	const struct rockchip_u3phy_cfg *phy_cfgs;
	const struct of_device_id *match;
	unsigned int reg[2];
	int index, ret;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "phy-cfgs are not assigned!\n");
		return -EINVAL;
	}

	u3phy = devm_kzalloc(dev, sizeof(*u3phy), GFP_KERNEL);
	if (!u3phy)
		return -ENOMEM;

	u3phy->u3phy_grf =
		syscon_regmap_lookup_by_phandle(np, "rockchip,u3phygrf");
	if (IS_ERR(u3phy->u3phy_grf))
		return PTR_ERR(u3phy->u3phy_grf);

	u3phy->grf =
		syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(u3phy->grf)) {
		dev_err(dev, "Missing rockchip,grf property\n");
		return PTR_ERR(u3phy->grf);
	}

	if (of_property_read_u32_array(np, "reg", reg, 2)) {
		dev_err(dev, "the reg property is not assigned in %s node\n",
			np->name);
		return -EINVAL;
	}

	u3phy->dev = dev;
	u3phy->vbus_enabled = false;
	phy_cfgs = match->data;
	platform_set_drvdata(pdev, u3phy);

	/* find out a proper config which can be matched with dt. */
	index = 0;
	while (phy_cfgs[index].reg) {
		if (phy_cfgs[index].reg == reg[1]) {
			u3phy->cfgs = &phy_cfgs[index];
			break;
		}

		++index;
	}

	if (!u3phy->cfgs) {
		dev_err(dev, "no phy-cfgs can be matched with %s node\n",
			np->name);
		return -EINVAL;
	}

	ret = rockchip_u3phy_parse_dt(u3phy, pdev);
	if (ret) {
		dev_err(dev, "parse dt failed, ret(%d)\n", ret);
		return ret;
	}

	ret = rockchip_u3phy_clk_enable(u3phy);
	if (ret) {
		dev_err(dev, "clk enable failed, ret(%d)\n", ret);
		return ret;
	}

	rockchip_u3phy_rest_assert(u3phy);
	rockchip_u3phy_rest_deassert(u3phy, U3PHY_APB_RST | U3PHY_POR_RST);

	index = 0;
	for_each_available_child_of_node(np, child_np) {
		struct rockchip_u3phy_port *u3phy_port = &u3phy->ports[index];

		u3phy_port->index = index;
		ret = rockchip_u3phy_port_init(u3phy, u3phy_port, child_np);
		if (ret) {
			dev_err(dev, "u3phy port init failed,ret(%d)\n", ret);
			goto put_child;
		}

		/* to prevent out of boundary */
		if (++index >= U3PHY_PORT_NUM)
			break;
	}

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR_OR_NULL(provider))
		goto put_child;

	rockchip_u3phy_rest_deassert(u3phy, U3PHY_MAC_RST);
	rockchip_u3phy_clk_disable(u3phy);

	u3phy->usb_phy.dev = dev;
	u3phy->usb_phy.init = rockchip_u3phy_on_init;
	u3phy->usb_phy.shutdown = rockchip_u3phy_on_shutdown;
	u3phy->usb_phy.notify_disconnect = rockchip_u3phy_on_disconnect;
	usb_add_phy(&u3phy->usb_phy, USB_PHY_TYPE_USB3);
	ATOMIC_INIT_NOTIFIER_HEAD(&u3phy->usb_phy.notifier);

	rockchip_u3phy_debugfs_init(u3phy);

	dev_info(dev, "Rockchip u3phy initialized successfully\n");
	return 0;

put_child:
	of_node_put(child_np);
	return ret;
}

static int rk3328_u3phy_pipe_power(struct rockchip_u3phy *u3phy,
				   struct rockchip_u3phy_port *u3phy_port,
				   bool on)
{
	unsigned int reg;

	if (on) {
		reg = readl(u3phy_port->base + 0x1a8);
		reg &= ~BIT(4); /* ldo power up */
		writel(reg, u3phy_port->base + 0x1a8);

		reg = readl(u3phy_port->base + 0x044);
		reg &= ~BIT(4); /* bg power on */
		writel(reg, u3phy_port->base + 0x044);

		reg = readl(u3phy_port->base + 0x150);
		reg |= BIT(6); /* tx bias enable */
		writel(reg, u3phy_port->base + 0x150);

		reg = readl(u3phy_port->base + 0x080);
		reg &= ~BIT(2); /* tx cm power up */
		writel(reg, u3phy_port->base + 0x080);

		reg = readl(u3phy_port->base + 0x0c0);
		/* tx obs enable and rx cm enable */
		reg |= (BIT(3) | BIT(4));
		writel(reg, u3phy_port->base + 0x0c0);

		udelay(1);
	} else {
		reg = readl(u3phy_port->base + 0x1a8);
		reg |= BIT(4); /* ldo power down */
		writel(reg, u3phy_port->base + 0x1a8);

		reg = readl(u3phy_port->base + 0x044);
		reg |= BIT(4); /* bg power down */
		writel(reg, u3phy_port->base + 0x044);

		reg = readl(u3phy_port->base + 0x150);
		reg &= ~BIT(6); /* tx bias disable */
		writel(reg, u3phy_port->base + 0x150);

		reg = readl(u3phy_port->base + 0x080);
		reg |= BIT(2); /* tx cm power down */
		writel(reg, u3phy_port->base + 0x080);

		reg = readl(u3phy_port->base + 0x0c0);
		/* tx obs disable and rx cm disable */
		reg &= ~(BIT(3) | BIT(4));
		writel(reg, u3phy_port->base + 0x0c0);
	}

	return 0;
}

static int rk3328_u3phy_tuning(struct rockchip_u3phy *u3phy,
			       struct rockchip_u3phy_port *u3phy_port,
			       struct device_node *child_np)
{
	if (u3phy_port->type == U3PHY_TYPE_UTMI) {
		/*
		 * For rk3328 SoC, pre-emphasis and pre-emphasis strength must
		 * be written as one fixed value as below.
		 *
		 * Dissimilarly, the odt 45ohm value should be flexibly tuninged
		 * for the different boards to adjust HS eye height, so its
		 * value can be assigned in DT in code design.
		 */

		/* {bits[2:0]=111}: always enable pre-emphasis */
		u3phy->apbcfg.u2_pre_emp = 0x0f;

		/* {bits[5:3]=000}: pre-emphasis strength as the weakest */
		u3phy->apbcfg.u2_pre_emp_sth = 0x41;

		/* {bits[4:0]=10101}: odt 45ohm tuning */
		u3phy->apbcfg.u2_odt_tuning = 0xb5;
		/* optional override of the odt 45ohm tuning */
		of_property_read_u32(child_np, "rockchip,odt-val-tuning",
				     &u3phy->apbcfg.u2_odt_tuning);

		writel(u3phy->apbcfg.u2_pre_emp, u3phy_port->base + 0x030);
		writel(u3phy->apbcfg.u2_pre_emp_sth, u3phy_port->base + 0x040);
		writel(u3phy->apbcfg.u2_odt_tuning, u3phy_port->base + 0x11c);
	} else if (u3phy_port->type == U3PHY_TYPE_PIPE) {
		if (u3phy_port->refclk_25m_quirk) {
			dev_dbg(u3phy->dev, "switch to 25m refclk\n");
			/* ref clk switch to 25M */
			writel(0x64, u3phy_port->base + 0x11c);
			writel(0x64, u3phy_port->base + 0x028);
			writel(0x01, u3phy_port->base + 0x020);
			writel(0x21, u3phy_port->base + 0x030);
			writel(0x06, u3phy_port->base + 0x108);
			writel(0x00, u3phy_port->base + 0x118);
		} else {
			/* configure for 24M ref clk */
			writel(0x80, u3phy_port->base + 0x10c);
			writel(0x01, u3phy_port->base + 0x118);
			writel(0x38, u3phy_port->base + 0x11c);
			writel(0x83, u3phy_port->base + 0x020);
			writel(0x02, u3phy_port->base + 0x108);
		}

		/* Enable SSC */
		udelay(3);
		writel(0x08, u3phy_port->base + 0x000);
		writel(0x0c, u3phy_port->base + 0x120);

		/* Tuning Rx for compliance RJTL test */
		writel(0x70, u3phy_port->base + 0x150);
		writel(0x12, u3phy_port->base + 0x0c8);
		writel(0x05, u3phy_port->base + 0x148);
		writel(0x08, u3phy_port->base + 0x068);
		writel(0xf0, u3phy_port->base + 0x1c4);
		writel(0xff, u3phy_port->base + 0x070);
		writel(0x0f, u3phy_port->base + 0x06c);
		writel(0xe0, u3phy_port->base + 0x060);

		/*
		 * Tuning Tx to increase the bias current
		 * used in TX driver and RX EQ, it can
		 * also increase the voltage of LFPS.
		 */
		writel(0x08, u3phy_port->base + 0x180);
	} else {
		dev_err(u3phy->dev, "invalid u3phy port type\n");
		return -EINVAL;
	}

	return 0;
}

static int rk322xh_u3phy_cp_test_enable(struct rockchip_u3phy *u3phy,
					struct rockchip_u3phy_port *u3phy_port)
{
	if (u3phy_port->type == U3PHY_TYPE_PIPE) {
		writel(0x0c, u3phy_port->base + 0x408);
	} else {
		dev_err(u3phy->dev, "The u3phy type is not pipe\n");
		return -EINVAL;
	}

	return 0;
}

static const struct rockchip_u3phy_cfg rk3328_u3phy_cfgs[] = {
	{
		.reg		= 0xff470000,
		.grfcfg		= {
			.um_suspend	= { 0x0004, 15, 0, 0x1452, 0x15d1 },
			.u2_only_ctrl	= { 0x0020, 15, 15, 0, 1 },
			.um_ls		= { 0x0030, 5, 4, 0, 1 },
			.um_hstdct	= { 0x0030, 7, 7, 0, 1 },
			.ls_det_en	= { 0x0040, 0, 0, 0, 1 },
			.ls_det_st	= { 0x0044, 0, 0, 0, 1 },
			.pp_pwr_st	= { 0x0034, 14, 13, 0, 0},
			.pp_pwr_en	= { {0x0020, 14, 0, 0x0014, 0x0005},
					    {0x0020, 14, 0, 0x0014, 0x000d},
					    {0x0020, 14, 0, 0x0014, 0x0015},
					    {0x0020, 14, 0, 0x0014, 0x001d} },
			.u3_disable	= { 0x04c4, 15, 0, 0x1100, 0x101},
		},
		.phy_pipe_power	= rk3328_u3phy_pipe_power,
		.phy_tuning	= rk3328_u3phy_tuning,
		.phy_cp_test	= rk322xh_u3phy_cp_test_enable,
	},
	{ /* sentinel */ }
};

static const struct of_device_id rockchip_u3phy_dt_match[] = {
	{ .compatible = "rockchip,rk3328-u3phy", .data = &rk3328_u3phy_cfgs },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_u3phy_dt_match);

static struct platform_driver rockchip_u3phy_driver = {
	.probe		= rockchip_u3phy_probe,
	.driver		= {
		.name	= "rockchip-u3phy",
		.of_match_table = rockchip_u3phy_dt_match,
	},
};
module_platform_driver(rockchip_u3phy_driver);

MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
MODULE_AUTHOR("William Wu <william.wu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 3.0 PHY driver");
MODULE_LICENSE("GPL v2");
