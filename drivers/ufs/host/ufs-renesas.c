// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Renesas UFS host controller driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
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

static void ufs_renesas_dbg_register_dump(struct ufs_hba *hba)
{
	ufshcd_dump_regs(hba, 0xc0, 0x40, "regs: 0xc0 + ");
}

static void ufs_renesas_poll(struct ufs_hba *hba, u32 reg, u32 expected, u32 mask)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout_atomic(hba->mmio_base + reg,
					val, (val & mask) == expected,
					10, 1000);
	if (ret)
		dev_err(hba->dev, "%s: poll failed %d (%08x, %08x, %08x)\n",
			__func__, ret, val, mask, expected);
}

static u32 ufs_renesas_read(struct ufs_hba *hba, u32 reg)
{
	return ufshcd_readl(hba, reg);
}

static void ufs_renesas_write(struct ufs_hba *hba, u32 reg, u32 value)
{
	ufshcd_writel(hba, value, reg);
}

static void ufs_renesas_write_d0_d4(struct ufs_hba *hba, u32 data_d0, u32 data_d4)
{
	ufs_renesas_write(hba, 0xd0, data_d0);
	ufs_renesas_write(hba, 0xd4, data_d4);
}

static void ufs_renesas_write_800_80c_poll(struct ufs_hba *hba, u32 addr,
					   u32 data_800)
{
	ufs_renesas_write_d0_d4(hba, 0x0000080c, 0x00000100);
	ufs_renesas_write_d0_d4(hba, 0x00000800, (data_800 << 16) | BIT(8) | addr);
	ufs_renesas_write(hba, 0xd0, 0x0000080c);
	ufs_renesas_poll(hba, 0xd4, BIT(8), BIT(8));
}

static void ufs_renesas_write_804_80c_poll(struct ufs_hba *hba, u32 addr, u32 data_804)
{
	ufs_renesas_write_d0_d4(hba, 0x0000080c, 0x00000100);
	ufs_renesas_write_d0_d4(hba, 0x00000804, (data_804 << 16) | BIT(8) | addr);
	ufs_renesas_write(hba, 0xd0, 0x0000080c);
	ufs_renesas_poll(hba, 0xd4, BIT(8), BIT(8));
}

static void ufs_renesas_write_828_82c_poll(struct ufs_hba *hba, u32 data_828)
{
	ufs_renesas_write_d0_d4(hba, 0x0000082c, 0x0f000000);
	ufs_renesas_write_d0_d4(hba, 0x00000828, data_828);
	ufs_renesas_write(hba, 0xd0, 0x0000082c);
	ufs_renesas_poll(hba, 0xd4, data_828, data_828);
}

static void ufs_renesas_write_phy(struct ufs_hba *hba, u32 addr16, u32 data16)
{
	ufs_renesas_write(hba, 0xf0, 1);
	ufs_renesas_write_800_80c_poll(hba, 0x16, addr16 & 0xff);
	ufs_renesas_write_800_80c_poll(hba, 0x17, (addr16 >> 8) & 0xff);
	ufs_renesas_write_800_80c_poll(hba, 0x18, data16 & 0xff);
	ufs_renesas_write_800_80c_poll(hba, 0x19, (data16 >> 8) & 0xff);
	ufs_renesas_write_800_80c_poll(hba, 0x1c, 0x01);
	ufs_renesas_write_828_82c_poll(hba, 0x0f000000);
	ufs_renesas_write(hba, 0xf0, 0);
}

static void ufs_renesas_set_phy(struct ufs_hba *hba, u32 addr16, u32 data16)
{
	u32 low, high;

	ufs_renesas_write(hba, 0xf0, 1);
	ufs_renesas_write_800_80c_poll(hba, 0x16, addr16 & 0xff);
	ufs_renesas_write_800_80c_poll(hba, 0x17, (addr16 >> 8) & 0xff);
	ufs_renesas_write_800_80c_poll(hba, 0x1c, 0x01);
	ufs_renesas_write_828_82c_poll(hba, 0x0f000000);
	ufs_renesas_write_804_80c_poll(hba, 0x1a, 0);
	ufs_renesas_write(hba, 0xd0, 0x00000808);
	low = ufs_renesas_read(hba, 0xd4) & 0xff;
	ufs_renesas_write_804_80c_poll(hba, 0x1b, 0);
	ufs_renesas_write(hba, 0xd0, 0x00000808);
	high = ufs_renesas_read(hba, 0xd4) & 0xff;
	ufs_renesas_write_828_82c_poll(hba, 0x0f000000);
	ufs_renesas_write(hba, 0xf0, 0);

	data16 |= (high << 8) | low;
	ufs_renesas_write_phy(hba, addr16, data16);
}

static void ufs_renesas_indirect_write(struct ufs_hba *hba, u32 gpio, u32 addr,
				       u32 data_800)
{
	ufs_renesas_write(hba, 0xf0, gpio);
	ufs_renesas_write_800_80c_poll(hba, addr, data_800);
	ufs_renesas_write_828_82c_poll(hba, 0x0f000000);
	ufs_renesas_write(hba, 0xf0, 0);
}

static void ufs_renesas_indirect_poll(struct ufs_hba *hba, u32 gpio, u32 addr,
				      u32 expected, u32 mask)
{
	ufs_renesas_write(hba, 0xf0, gpio);
	ufs_renesas_write_800_80c_poll(hba, addr, 0);
	ufs_renesas_write(hba, 0xd0, 0x00000808);
	ufs_renesas_poll(hba, 0xd4, expected, mask);
	ufs_renesas_write(hba, 0xf0, 0);
}

static void ufs_renesas_init_step1_to_3(struct ufs_hba *hba)
{
	ufs_renesas_write(hba, 0xc0, 0x49425308);
	ufs_renesas_write_d0_d4(hba, 0x00000104, 0x00000002);
	udelay(1);
	ufs_renesas_write_d0_d4(hba, 0x00000828, 0x00000200);
	udelay(1);
	ufs_renesas_write_d0_d4(hba, 0x00000828, 0x00000000);
	ufs_renesas_write_d0_d4(hba, 0x00000104, 0x00000001);
	ufs_renesas_write_d0_d4(hba, 0x00000940, 0x00000001);
	udelay(1);
	ufs_renesas_write_d0_d4(hba, 0x00000940, 0x00000000);

	ufs_renesas_write(hba, 0xc0, 0x49425308);
	ufs_renesas_write(hba, 0xc0, 0x41584901);
}

static void ufs_renesas_init_step4_to_6(struct ufs_hba *hba)
{
	ufs_renesas_write_d0_d4(hba, 0x0000080c, 0x00000100);
	ufs_renesas_write_d0_d4(hba, 0x00000804, 0x00000000);
	ufs_renesas_write(hba, 0xd0, 0x0000080c);
	ufs_renesas_poll(hba, 0xd4, BIT(8), BIT(8));

	ufs_renesas_write(hba, REG_CONTROLLER_ENABLE, 0x00000001);

	ufs_renesas_write(hba, 0xd0, 0x00000804);
	ufs_renesas_poll(hba, 0xd4, BIT(8) | BIT(6) | BIT(0), BIT(8) | BIT(6) | BIT(0));
}

static u32 ufs_renesas_init_disable_timer(struct ufs_hba *hba)
{
	u32 timer_val;

	ufs_renesas_write(hba, 0xd0, 0x00000d00);
	timer_val = ufs_renesas_read(hba, 0xd4) & 0x0000ffff;
	ufs_renesas_write(hba, 0xd4, 0x00000000);
	ufs_renesas_write_d0_d4(hba, 0x0000082c, 0x0f000000);
	ufs_renesas_write_d0_d4(hba, 0x00000828, 0x08000000);
	ufs_renesas_write(hba, 0xd0, 0x0000082c);
	ufs_renesas_poll(hba, 0xd4, BIT(27), BIT(27));
	ufs_renesas_write(hba, 0xd0, 0x00000d2c);
	ufs_renesas_poll(hba, 0xd4, BIT(0), BIT(0));

	return timer_val;
}

static void ufs_renesas_init_enable_timer(struct ufs_hba *hba, u32 timer_val)
{
	ufs_renesas_write(hba, 0xf0, 0);
	ufs_renesas_write(hba, 0xd0, 0x00000d00);
	ufs_renesas_write(hba, 0xd4, timer_val);
}

static void ufs_renesas_write_phy_10ad_10af(struct ufs_hba *hba,
					    u32 data_10ad, u32 data_10af)
{
	ufs_renesas_write_phy(hba, 0x10ae, 0x0001);
	ufs_renesas_write_phy(hba, 0x10ad, data_10ad);
	ufs_renesas_write_phy(hba, 0x10af, data_10af);
	ufs_renesas_write_phy(hba, 0x10b6, 0x0001);
	ufs_renesas_write_phy(hba, 0x10ae, 0x0000);
}

static void ufs_renesas_init_compensation_and_slicers(struct ufs_hba *hba)
{
	ufs_renesas_write_phy_10ad_10af(hba, 0x0000, 0x0001);
	ufs_renesas_write_phy_10ad_10af(hba, 0x0000, 0x0002);
	ufs_renesas_write_phy_10ad_10af(hba, 0x0080, 0x0000);
	ufs_renesas_write_phy_10ad_10af(hba, 0x0080, 0x001a);
}

static void ufs_renesas_pre_init(struct ufs_hba *hba)
{
	u32 timer_val;

	/* This setting is for SERIES B */
	ufs_renesas_init_step1_to_3(hba);

	ufs_renesas_init_step4_to_6(hba);

	timer_val = ufs_renesas_init_disable_timer(hba);

	/* phy setup */
	ufs_renesas_indirect_write(hba, 1, 0x01, 0x001f);
	ufs_renesas_indirect_write(hba, 7, 0x5d, 0x0014);
	ufs_renesas_indirect_write(hba, 7, 0x5e, 0x0014);
	ufs_renesas_indirect_write(hba, 7, 0x0d, 0x0003);
	ufs_renesas_indirect_write(hba, 7, 0x0e, 0x0007);
	ufs_renesas_indirect_write(hba, 7, 0x5f, 0x0003);
	ufs_renesas_indirect_write(hba, 7, 0x60, 0x0003);
	ufs_renesas_indirect_write(hba, 7, 0x5b, 0x00a6);
	ufs_renesas_indirect_write(hba, 7, 0x5c, 0x0003);

	ufs_renesas_indirect_poll(hba, 7, 0x3c, 0, BIT(7));
	ufs_renesas_indirect_poll(hba, 7, 0x4c, 0, BIT(4));

	ufs_renesas_indirect_write(hba, 1, 0x32, 0x0080);
	ufs_renesas_indirect_write(hba, 1, 0x1f, 0x0001);
	ufs_renesas_indirect_write(hba, 0, 0x2c, 0x0001);
	ufs_renesas_indirect_write(hba, 0, 0x32, 0x0087);

	ufs_renesas_indirect_write(hba, 1, 0x4d, 0x0061);
	ufs_renesas_indirect_write(hba, 4, 0x9b, 0x0009);
	ufs_renesas_indirect_write(hba, 4, 0xa6, 0x0005);
	ufs_renesas_indirect_write(hba, 4, 0xa5, 0x0058);
	ufs_renesas_indirect_write(hba, 1, 0x39, 0x0027);
	ufs_renesas_indirect_write(hba, 1, 0x47, 0x004c);

	ufs_renesas_indirect_write(hba, 7, 0x0d, 0x0002);
	ufs_renesas_indirect_write(hba, 7, 0x0e, 0x0007);

	ufs_renesas_write_phy(hba, 0x0028, 0x0061);
	ufs_renesas_write_phy(hba, 0x4014, 0x0061);
	ufs_renesas_set_phy(hba, 0x401c, BIT(2));
	ufs_renesas_write_phy(hba, 0x4000, 0x0000);
	ufs_renesas_write_phy(hba, 0x4001, 0x0000);

	ufs_renesas_init_compensation_and_slicers(hba);

	ufs_renesas_indirect_write(hba, 7, 0x70, 0x0016);
	ufs_renesas_indirect_write(hba, 7, 0x71, 0x0016);
	ufs_renesas_indirect_write(hba, 7, 0x72, 0x0014);
	ufs_renesas_indirect_write(hba, 7, 0x73, 0x0014);
	ufs_renesas_indirect_write(hba, 7, 0x74, 0x0000);
	ufs_renesas_indirect_write(hba, 7, 0x75, 0x0000);
	ufs_renesas_indirect_write(hba, 7, 0x76, 0x0010);
	ufs_renesas_indirect_write(hba, 7, 0x77, 0x0010);
	ufs_renesas_indirect_write(hba, 7, 0x78, 0x00ff);
	ufs_renesas_indirect_write(hba, 7, 0x79, 0x0000);

	ufs_renesas_indirect_write(hba, 7, 0x19, 0x0007);
	ufs_renesas_indirect_write(hba, 7, 0x1a, 0x0007);
	ufs_renesas_indirect_write(hba, 7, 0x24, 0x000c);
	ufs_renesas_indirect_write(hba, 7, 0x25, 0x000c);
	ufs_renesas_indirect_write(hba, 7, 0x62, 0x0000);
	ufs_renesas_indirect_write(hba, 7, 0x63, 0x0000);
	ufs_renesas_indirect_write(hba, 7, 0x5d, 0x0014);
	ufs_renesas_indirect_write(hba, 7, 0x5e, 0x0017);
	ufs_renesas_indirect_write(hba, 7, 0x5d, 0x0004);
	ufs_renesas_indirect_write(hba, 7, 0x5e, 0x0017);
	ufs_renesas_indirect_poll(hba, 7, 0x55, 0, BIT(6));
	ufs_renesas_indirect_poll(hba, 7, 0x41, 0, BIT(7));
	/* end of phy setup */

	ufs_renesas_init_enable_timer(hba, timer_val);
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

	hba->quirks |= UFSHCD_QUIRK_HIBERN_FASTAUTO;

	return 0;
}

static int ufs_renesas_set_dma_mask(struct ufs_hba *hba)
{
	return dma_set_mask_and_coherent(hba->dev, DMA_BIT_MASK(32));
}

static const struct ufs_hba_variant_ops ufs_renesas_vops = {
	.name		= "renesas",
	.init		= ufs_renesas_init,
	.set_dma_mask	= ufs_renesas_set_dma_mask,
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
	ufshcd_pltfrm_remove(pdev);
}

static struct platform_driver ufs_renesas_platform = {
	.probe	= ufs_renesas_probe,
	.remove = ufs_renesas_remove,
	.driver	= {
		.name	= "ufshcd-renesas",
		.of_match_table	= of_match_ptr(ufs_renesas_of_match),
	},
};
module_platform_driver(ufs_renesas_platform);

MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
MODULE_DESCRIPTION("Renesas UFS host controller driver");
MODULE_LICENSE("Dual MIT/GPL");
