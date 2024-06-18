// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Renesas UFS host controller driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <ufs/ufshcd.h>

#include "ufshcd-pltfrm.h"

struct ufs_renesas_priv {
	bool initialized;	/* The hardware needs initialization once */
};

enum {
	SET_PHY_INDEX_LO = 0,
	SET_PHY_INDEX_HI,
	TIMER_INDEX,
	MAX_INDEX
};

enum ufs_renesas_init_param_mode {
	MODE_RESTORE,
	MODE_SET,
	MODE_SAVE,
	MODE_POLL,
	MODE_WAIT,
	MODE_WRITE,
};

#define PARAM_RESTORE(_reg, _index) \
		{ .mode = MODE_RESTORE, .reg = _reg, .index = _index }
#define PARAM_SET(_index, _set) \
		{ .mode = MODE_SET, .index = _index, .u.set = _set }
#define PARAM_SAVE(_reg, _mask, _index) \
		{ .mode = MODE_SAVE, .reg = _reg, .mask = (u32)(_mask), \
		  .index = _index }
#define PARAM_POLL(_reg, _expected, _mask) \
		{ .mode = MODE_POLL, .reg = _reg, .u.expected = _expected, \
		  .mask = (u32)(_mask) }
#define PARAM_WAIT(_delay_us) \
		{ .mode = MODE_WAIT, .u.delay_us = _delay_us }

#define PARAM_WRITE(_reg, _val) \
		{ .mode = MODE_WRITE, .reg = _reg, .u.val = _val }

#define PARAM_WRITE_D0_D4(_d0, _d4) \
		PARAM_WRITE(0xd0, _d0),	PARAM_WRITE(0xd4, _d4)

#define PARAM_WRITE_800_80C_POLL(_addr, _data_800)		\
		PARAM_WRITE_D0_D4(0x0000080c, 0x00000100),	\
		PARAM_WRITE_D0_D4(0x00000800, ((_data_800) << 16) | BIT(8) | (_addr)), \
		PARAM_WRITE(0xd0, 0x0000080c),			\
		PARAM_POLL(0xd4, BIT(8), BIT(8))

#define PARAM_RESTORE_800_80C_POLL(_index)			\
		PARAM_WRITE_D0_D4(0x0000080c, 0x00000100),	\
		PARAM_WRITE(0xd0, 0x00000800),			\
		PARAM_RESTORE(0xd4, _index),			\
		PARAM_WRITE(0xd0, 0x0000080c),			\
		PARAM_POLL(0xd4, BIT(8), BIT(8))

#define PARAM_WRITE_804_80C_POLL(_addr, _data_804)		\
		PARAM_WRITE_D0_D4(0x0000080c, 0x00000100),	\
		PARAM_WRITE_D0_D4(0x00000804, ((_data_804) << 16) | BIT(8) | (_addr)), \
		PARAM_WRITE(0xd0, 0x0000080c),			\
		PARAM_POLL(0xd4, BIT(8), BIT(8))

#define PARAM_WRITE_828_82C_POLL(_data_828)			\
		PARAM_WRITE_D0_D4(0x0000082c, 0x0f000000),	\
		PARAM_WRITE_D0_D4(0x00000828, _data_828),	\
		PARAM_WRITE(0xd0, 0x0000082c),			\
		PARAM_POLL(0xd4, _data_828, _data_828)

#define PARAM_WRITE_PHY(_addr16, _data16)			\
		PARAM_WRITE(0xf0, 1),				\
		PARAM_WRITE_800_80C_POLL(0x16, (_addr16) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x17, ((_addr16) >> 8) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x18, (_data16) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x19, ((_data16) >> 8) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x1c, 0x01),		\
		PARAM_WRITE_828_82C_POLL(0x0f000000),		\
		PARAM_WRITE(0xf0, 0)

#define PARAM_SET_PHY(_addr16, _data16)				\
		PARAM_WRITE(0xf0, 1),				\
		PARAM_WRITE_800_80C_POLL(0x16, (_addr16) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x17, ((_addr16) >> 8) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x1c, 0x01),		\
		PARAM_WRITE_828_82C_POLL(0x0f000000),		\
		PARAM_WRITE_804_80C_POLL(0x1a, 0),		\
		PARAM_WRITE(0xd0, 0x00000808),			\
		PARAM_SAVE(0xd4, 0xff, SET_PHY_INDEX_LO),	\
		PARAM_WRITE_804_80C_POLL(0x1b, 0),		\
		PARAM_WRITE(0xd0, 0x00000808),			\
		PARAM_SAVE(0xd4, 0xff, SET_PHY_INDEX_HI),	\
		PARAM_WRITE_828_82C_POLL(0x0f000000),		\
		PARAM_WRITE(0xf0, 0),				\
		PARAM_WRITE(0xf0, 1),				\
		PARAM_WRITE_800_80C_POLL(0x16, (_addr16) & 0xff), \
		PARAM_WRITE_800_80C_POLL(0x17, ((_addr16) >> 8) & 0xff), \
		PARAM_SET(SET_PHY_INDEX_LO, ((_data16 & 0xff) << 16) | BIT(8) | 0x18), \
		PARAM_RESTORE_800_80C_POLL(SET_PHY_INDEX_LO),	\
		PARAM_SET(SET_PHY_INDEX_HI, (((_data16 >> 8) & 0xff) << 16) | BIT(8) | 0x19), \
		PARAM_RESTORE_800_80C_POLL(SET_PHY_INDEX_HI),	\
		PARAM_WRITE_800_80C_POLL(0x1c, 0x01),		\
		PARAM_WRITE_828_82C_POLL(0x0f000000),		\
		PARAM_WRITE(0xf0, 0)

#define PARAM_INDIRECT_WRITE(_gpio, _addr, _data_800)		\
		PARAM_WRITE(0xf0, _gpio),			\
		PARAM_WRITE_800_80C_POLL(_addr, _data_800),	\
		PARAM_WRITE_828_82C_POLL(0x0f000000),		\
		PARAM_WRITE(0xf0, 0)

#define PARAM_INDIRECT_POLL(_gpio, _addr, _expected, _mask)	\
		PARAM_WRITE(0xf0, _gpio),			\
		PARAM_WRITE_800_80C_POLL(_addr, 0),		\
		PARAM_WRITE(0xd0, 0x00000808),			\
		PARAM_POLL(0xd4, _expected, _mask),		\
		PARAM_WRITE(0xf0, 0)

struct ufs_renesas_init_param {
	enum ufs_renesas_init_param_mode mode;
	u32 reg;
	union {
		u32 expected;
		u32 delay_us;
		u32 set;
		u32 val;
	} u;
	u32 mask;
	u32 index;
};

/* This setting is for SERIES B */
static const struct ufs_renesas_init_param ufs_param[] = {
	PARAM_WRITE(0xc0, 0x49425308),
	PARAM_WRITE_D0_D4(0x00000104, 0x00000002),
	PARAM_WAIT(1),
	PARAM_WRITE_D0_D4(0x00000828, 0x00000200),
	PARAM_WAIT(1),
	PARAM_WRITE_D0_D4(0x00000828, 0x00000000),
	PARAM_WRITE_D0_D4(0x00000104, 0x00000001),
	PARAM_WRITE_D0_D4(0x00000940, 0x00000001),
	PARAM_WAIT(1),
	PARAM_WRITE_D0_D4(0x00000940, 0x00000000),

	PARAM_WRITE(0xc0, 0x49425308),
	PARAM_WRITE(0xc0, 0x41584901),

	PARAM_WRITE_D0_D4(0x0000080c, 0x00000100),
	PARAM_WRITE_D0_D4(0x00000804, 0x00000000),
	PARAM_WRITE(0xd0, 0x0000080c),
	PARAM_POLL(0xd4, BIT(8), BIT(8)),

	PARAM_WRITE(REG_CONTROLLER_ENABLE, 0x00000001),

	PARAM_WRITE(0xd0, 0x00000804),
	PARAM_POLL(0xd4, BIT(8) | BIT(6) | BIT(0), BIT(8) | BIT(6) | BIT(0)),

	PARAM_WRITE(0xd0, 0x00000d00),
	PARAM_SAVE(0xd4, 0x0000ffff, TIMER_INDEX),
	PARAM_WRITE(0xd4, 0x00000000),
	PARAM_WRITE_D0_D4(0x0000082c, 0x0f000000),
	PARAM_WRITE_D0_D4(0x00000828, 0x08000000),
	PARAM_WRITE(0xd0, 0x0000082c),
	PARAM_POLL(0xd4, BIT(27), BIT(27)),
	PARAM_WRITE(0xd0, 0x00000d2c),
	PARAM_POLL(0xd4, BIT(0), BIT(0)),

	/* phy setup */
	PARAM_INDIRECT_WRITE(1, 0x01, 0x001f),
	PARAM_INDIRECT_WRITE(7, 0x5d, 0x0014),
	PARAM_INDIRECT_WRITE(7, 0x5e, 0x0014),
	PARAM_INDIRECT_WRITE(7, 0x0d, 0x0003),
	PARAM_INDIRECT_WRITE(7, 0x0e, 0x0007),
	PARAM_INDIRECT_WRITE(7, 0x5f, 0x0003),
	PARAM_INDIRECT_WRITE(7, 0x60, 0x0003),
	PARAM_INDIRECT_WRITE(7, 0x5b, 0x00a6),
	PARAM_INDIRECT_WRITE(7, 0x5c, 0x0003),

	PARAM_INDIRECT_POLL(7, 0x3c, 0, BIT(7)),
	PARAM_INDIRECT_POLL(7, 0x4c, 0, BIT(4)),

	PARAM_INDIRECT_WRITE(1, 0x32, 0x0080),
	PARAM_INDIRECT_WRITE(1, 0x1f, 0x0001),
	PARAM_INDIRECT_WRITE(0, 0x2c, 0x0001),
	PARAM_INDIRECT_WRITE(0, 0x32, 0x0087),

	PARAM_INDIRECT_WRITE(1, 0x4d, 0x0061),
	PARAM_INDIRECT_WRITE(4, 0x9b, 0x0009),
	PARAM_INDIRECT_WRITE(4, 0xa6, 0x0005),
	PARAM_INDIRECT_WRITE(4, 0xa5, 0x0058),
	PARAM_INDIRECT_WRITE(1, 0x39, 0x0027),
	PARAM_INDIRECT_WRITE(1, 0x47, 0x004c),

	PARAM_INDIRECT_WRITE(7, 0x0d, 0x0002),
	PARAM_INDIRECT_WRITE(7, 0x0e, 0x0007),

	PARAM_WRITE_PHY(0x0028, 0x0061),
	PARAM_WRITE_PHY(0x4014, 0x0061),
	PARAM_SET_PHY(0x401c, BIT(2)),
	PARAM_WRITE_PHY(0x4000, 0x0000),
	PARAM_WRITE_PHY(0x4001, 0x0000),

	PARAM_WRITE_PHY(0x10ae, 0x0001),
	PARAM_WRITE_PHY(0x10ad, 0x0000),
	PARAM_WRITE_PHY(0x10af, 0x0001),
	PARAM_WRITE_PHY(0x10b6, 0x0001),
	PARAM_WRITE_PHY(0x10ae, 0x0000),

	PARAM_WRITE_PHY(0x10ae, 0x0001),
	PARAM_WRITE_PHY(0x10ad, 0x0000),
	PARAM_WRITE_PHY(0x10af, 0x0002),
	PARAM_WRITE_PHY(0x10b6, 0x0001),
	PARAM_WRITE_PHY(0x10ae, 0x0000),

	PARAM_WRITE_PHY(0x10ae, 0x0001),
	PARAM_WRITE_PHY(0x10ad, 0x0080),
	PARAM_WRITE_PHY(0x10af, 0x0000),
	PARAM_WRITE_PHY(0x10b6, 0x0001),
	PARAM_WRITE_PHY(0x10ae, 0x0000),

	PARAM_WRITE_PHY(0x10ae, 0x0001),
	PARAM_WRITE_PHY(0x10ad, 0x0080),
	PARAM_WRITE_PHY(0x10af, 0x001a),
	PARAM_WRITE_PHY(0x10b6, 0x0001),
	PARAM_WRITE_PHY(0x10ae, 0x0000),

	PARAM_INDIRECT_WRITE(7, 0x70, 0x0016),
	PARAM_INDIRECT_WRITE(7, 0x71, 0x0016),
	PARAM_INDIRECT_WRITE(7, 0x72, 0x0014),
	PARAM_INDIRECT_WRITE(7, 0x73, 0x0014),
	PARAM_INDIRECT_WRITE(7, 0x74, 0x0000),
	PARAM_INDIRECT_WRITE(7, 0x75, 0x0000),
	PARAM_INDIRECT_WRITE(7, 0x76, 0x0010),
	PARAM_INDIRECT_WRITE(7, 0x77, 0x0010),
	PARAM_INDIRECT_WRITE(7, 0x78, 0x00ff),
	PARAM_INDIRECT_WRITE(7, 0x79, 0x0000),

	PARAM_INDIRECT_WRITE(7, 0x19, 0x0007),

	PARAM_INDIRECT_WRITE(7, 0x1a, 0x0007),

	PARAM_INDIRECT_WRITE(7, 0x24, 0x000c),

	PARAM_INDIRECT_WRITE(7, 0x25, 0x000c),

	PARAM_INDIRECT_WRITE(7, 0x62, 0x0000),
	PARAM_INDIRECT_WRITE(7, 0x63, 0x0000),
	PARAM_INDIRECT_WRITE(7, 0x5d, 0x0014),
	PARAM_INDIRECT_WRITE(7, 0x5e, 0x0017),
	PARAM_INDIRECT_WRITE(7, 0x5d, 0x0004),
	PARAM_INDIRECT_WRITE(7, 0x5e, 0x0017),
	PARAM_INDIRECT_POLL(7, 0x55, 0, BIT(6)),
	PARAM_INDIRECT_POLL(7, 0x41, 0, BIT(7)),
	/* end of phy setup */

	PARAM_WRITE(0xf0, 0),
	PARAM_WRITE(0xd0, 0x00000d00),
	PARAM_RESTORE(0xd4, TIMER_INDEX),
};

static void ufs_renesas_dbg_register_dump(struct ufs_hba *hba)
{
	ufshcd_dump_regs(hba, 0xc0, 0x40, "regs: 0xc0 + ");
}

static void ufs_renesas_reg_control(struct ufs_hba *hba,
				    const struct ufs_renesas_init_param *p)
{
	static u32 save[MAX_INDEX];
	int ret;
	u32 val;

	WARN_ON(p->index >= MAX_INDEX);

	switch (p->mode) {
	case MODE_RESTORE:
		ufshcd_writel(hba, save[p->index], p->reg);
		break;
	case MODE_SET:
		save[p->index] |= p->u.set;
		break;
	case MODE_SAVE:
		save[p->index] = ufshcd_readl(hba, p->reg) & p->mask;
		break;
	case MODE_POLL:
		ret = readl_poll_timeout_atomic(hba->mmio_base + p->reg,
						val,
						(val & p->mask) == p->u.expected,
						10, 1000);
		if (ret)
			dev_err(hba->dev, "%s: poll failed %d (%08x, %08x, %08x)\n",
				__func__, ret, val, p->mask, p->u.expected);
		break;
	case MODE_WAIT:
		if (p->u.delay_us > 1000)
			mdelay(DIV_ROUND_UP(p->u.delay_us, 1000));
		else
			udelay(p->u.delay_us);
		break;
	case MODE_WRITE:
		ufshcd_writel(hba, p->u.val, p->reg);
		break;
	default:
		break;
	}
}

static void ufs_renesas_pre_init(struct ufs_hba *hba)
{
	const struct ufs_renesas_init_param *p = ufs_param;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ufs_param); i++)
		ufs_renesas_reg_control(hba, &p[i]);
}

static int ufs_renesas_hce_enable_notify(struct ufs_hba *hba,
					 enum ufs_notify_change_status status)
{
	struct ufs_renesas_priv *priv = ufshcd_get_variant(hba);

	if (priv->initialized)
		return 0;

	if (status == PRE_CHANGE)
		ufs_renesas_pre_init(hba);

	priv->initialized = true;

	return 0;
}

static int ufs_renesas_setup_clocks(struct ufs_hba *hba, bool on,
				    enum ufs_notify_change_status status)
{
	if (on && status == PRE_CHANGE)
		pm_runtime_get_sync(hba->dev);
	else if (!on && status == POST_CHANGE)
		pm_runtime_put(hba->dev);

	return 0;
}

static int ufs_renesas_init(struct ufs_hba *hba)
{
	struct ufs_renesas_priv *priv;

	priv = devm_kzalloc(hba->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	ufshcd_set_variant(hba, priv);

	hba->quirks |= UFSHCD_QUIRK_BROKEN_64BIT_ADDRESS | UFSHCD_QUIRK_HIBERN_FASTAUTO;

	return 0;
}

static const struct ufs_hba_variant_ops ufs_renesas_vops = {
	.name		= "renesas",
	.init		= ufs_renesas_init,
	.setup_clocks	= ufs_renesas_setup_clocks,
	.hce_enable_notify = ufs_renesas_hce_enable_notify,
	.dbg_register_dump = ufs_renesas_dbg_register_dump,
};

static const struct of_device_id __maybe_unused ufs_renesas_of_match[] = {
	{ .compatible = "renesas,r8a779f0-ufs" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ufs_renesas_of_match);

static int ufs_renesas_probe(struct platform_device *pdev)
{
	return ufshcd_pltfrm_init(pdev, &ufs_renesas_vops);
}

static void ufs_renesas_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);

	ufshcd_remove(hba);
}

static struct platform_driver ufs_renesas_platform = {
	.probe	= ufs_renesas_probe,
	.remove_new = ufs_renesas_remove,
	.driver	= {
		.name	= "ufshcd-renesas",
		.of_match_table	= of_match_ptr(ufs_renesas_of_match),
	},
};
module_platform_driver(ufs_renesas_platform);

MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
MODULE_DESCRIPTION("Renesas UFS host controller driver");
MODULE_LICENSE("Dual MIT/GPL");
