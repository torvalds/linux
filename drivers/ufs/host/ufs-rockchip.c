// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip UFS Host Controller driver
 *
 * Copyright (C) 2025 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <ufs/ufshcd.h>
#include <ufs/unipro.h>
#include "ufshcd-pltfrm.h"
#include "ufs-rockchip.h"

static int ufs_rockchip_hce_enable_notify(struct ufs_hba *hba,
					 enum ufs_notify_change_status status)
{
	int err = 0;

	if (status == POST_CHANGE) {
		err = ufshcd_dme_reset(hba);
		if (err)
			return err;

		err = ufshcd_dme_enable(hba);
		if (err)
			return err;

		return ufshcd_vops_phy_initialization(hba);
	}

	return 0;
}

static void ufs_rockchip_set_pm_lvl(struct ufs_hba *hba)
{
	hba->rpm_lvl = UFS_PM_LVL_5;
	hba->spm_lvl = UFS_PM_LVL_5;
}

static int ufs_rockchip_rk3576_phy_init(struct ufs_hba *hba)
{
	struct ufs_rockchip_host *host = ufshcd_get_variant(hba);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(PA_LOCAL_TX_LCC_ENABLE, 0x0), 0x0);
	/* enable the mphy DME_SET cfg */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(MPHY_CFG, 0x0), MPHY_CFG_ENABLE);
	for (int i = 0; i < 2; i++) {
		/* Configuration M - TX */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_CLK_PRD, SEL_TX_LANE0 + i), 0x06);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_CLK_PRD_EN, SEL_TX_LANE0 + i), 0x02);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_LINERESET_VALUE, SEL_TX_LANE0 + i), 0x44);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_LINERESET_PVALUE1, SEL_TX_LANE0 + i), 0xe6);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_LINERESET_PVALUE2, SEL_TX_LANE0 + i), 0x07);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_TASE_VALUE, SEL_TX_LANE0 + i), 0x93);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_BASE_NVALUE, SEL_TX_LANE0 + i), 0xc9);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_TX_POWER_SAVING_CTRL, SEL_TX_LANE0 + i), 0x00);
		/* Configuration M - RX */
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_CLK_PRD, SEL_RX_LANE0 + i), 0x06);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_CLK_PRD_EN, SEL_RX_LANE0 + i), 0x00);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_LINERESET_VALUE, SEL_RX_LANE0 + i), 0x58);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_LINERESET_PVALUE1, SEL_RX_LANE0 + i), 0x8c);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_LINERESET_PVALUE2, SEL_RX_LANE0 + i), 0x02);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_LINERESET_OPTION, SEL_RX_LANE0 + i), 0xf6);
		ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(VND_RX_POWER_SAVING_CTRL, SEL_RX_LANE0 + i), 0x69);
	}

	/* disable the mphy DME_SET cfg */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(MPHY_CFG, 0x0), MPHY_CFG_DISABLE);

	ufs_sys_writel(host->mphy_base, 0x80, CMN_REG23);
	ufs_sys_writel(host->mphy_base, 0xB5, TRSV0_REG14);
	ufs_sys_writel(host->mphy_base, 0xB5, TRSV1_REG14);

	ufs_sys_writel(host->mphy_base, 0x03, TRSV0_REG15);
	ufs_sys_writel(host->mphy_base, 0x03, TRSV1_REG15);

	ufs_sys_writel(host->mphy_base, 0x38, TRSV0_REG08);
	ufs_sys_writel(host->mphy_base, 0x38, TRSV1_REG08);

	ufs_sys_writel(host->mphy_base, 0x50, TRSV0_REG29);
	ufs_sys_writel(host->mphy_base, 0x50, TRSV1_REG29);

	ufs_sys_writel(host->mphy_base, 0x80, TRSV0_REG2E);
	ufs_sys_writel(host->mphy_base, 0x80, TRSV1_REG2E);

	ufs_sys_writel(host->mphy_base, 0x18, TRSV0_REG3C);
	ufs_sys_writel(host->mphy_base, 0x18, TRSV1_REG3C);

	ufs_sys_writel(host->mphy_base, 0x03, TRSV0_REG16);
	ufs_sys_writel(host->mphy_base, 0x03, TRSV1_REG16);

	ufs_sys_writel(host->mphy_base, 0x20, TRSV0_REG17);
	ufs_sys_writel(host->mphy_base, 0x20, TRSV1_REG17);

	ufs_sys_writel(host->mphy_base, 0xC0, TRSV0_REG18);
	ufs_sys_writel(host->mphy_base, 0xC0, TRSV1_REG18);

	ufs_sys_writel(host->mphy_base, 0x03, CMN_REG25);

	ufs_sys_writel(host->mphy_base, 0x03, TRSV0_REG3D);
	ufs_sys_writel(host->mphy_base, 0x03, TRSV1_REG3D);

	ufs_sys_writel(host->mphy_base, 0xC0, CMN_REG23);
	udelay(1);
	ufs_sys_writel(host->mphy_base, 0x00, CMN_REG23);

	usleep_range(200, 250);
	/* start link up */
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(MIB_T_DBG_CPORT_TX_ENDIAN, 0), 0x0);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(MIB_T_DBG_CPORT_RX_ENDIAN, 0), 0x0);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(N_DEVICEID, 0), 0x0);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(N_DEVICEID_VALID, 0), 0x1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(T_PEERDEVICEID, 0), 0x1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(T_CONNECTIONSTATE, 0), 0x1);

	return 0;
}

static int ufs_rockchip_common_init(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_rockchip_host *host;
	int err;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->ufs_sys_ctrl = devm_platform_ioremap_resource_byname(pdev, "hci_grf");
	if (IS_ERR(host->ufs_sys_ctrl))
		return dev_err_probe(dev, PTR_ERR(host->ufs_sys_ctrl),
				"Failed to map HCI system control registers\n");

	host->ufs_phy_ctrl = devm_platform_ioremap_resource_byname(pdev, "mphy_grf");
	if (IS_ERR(host->ufs_phy_ctrl))
		return dev_err_probe(dev, PTR_ERR(host->ufs_phy_ctrl),
				"Failed to map mphy system control registers\n");

	host->mphy_base = devm_platform_ioremap_resource_byname(pdev, "mphy");
	if (IS_ERR(host->mphy_base))
		return dev_err_probe(dev, PTR_ERR(host->mphy_base),
				"Failed to map mphy base registers\n");

	host->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(host->rst))
		return dev_err_probe(dev, PTR_ERR(host->rst),
				"failed to get reset control\n");

	reset_control_assert(host->rst);
	udelay(1);
	reset_control_deassert(host->rst);

	host->ref_out_clk = devm_clk_get_enabled(dev, "ref_out");
	if (IS_ERR(host->ref_out_clk))
		return dev_err_probe(dev, PTR_ERR(host->ref_out_clk),
				"ref_out clock unavailable\n");

	host->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(host->rst_gpio))
		return dev_err_probe(dev, PTR_ERR(host->rst_gpio),
				"failed to get reset gpio\n");

	err = devm_clk_bulk_get_all_enabled(dev, &host->clks);
	if (err < 0)
		return dev_err_probe(dev, err, "failed to enable clocks\n");

	host->hba = hba;

	ufshcd_set_variant(hba, host);

	return 0;
}

static int ufs_rockchip_rk3576_init(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	int ret;

	hba->quirks = UFSHCD_QUIRK_SKIP_DEF_UNIPRO_TIMEOUT_SETTING;

	/* Enable BKOPS when suspend */
	hba->caps |= UFSHCD_CAP_AUTO_BKOPS_SUSPEND;
	/* Enable putting device into deep sleep */
	hba->caps |= UFSHCD_CAP_DEEPSLEEP;
	/* Enable devfreq of UFS */
	hba->caps |= UFSHCD_CAP_CLK_SCALING;
	/* Enable WriteBooster */
	hba->caps |= UFSHCD_CAP_WB_EN;

	/* Set the default desired pm level in case no users set via sysfs */
	ufs_rockchip_set_pm_lvl(hba);

	ret = ufs_rockchip_common_init(hba);
	if (ret)
		return dev_err_probe(dev, ret, "ufs common init fail\n");

	return 0;
}

static int ufs_rockchip_device_reset(struct ufs_hba *hba)
{
	struct ufs_rockchip_host *host = ufshcd_get_variant(hba);

	gpiod_set_value_cansleep(host->rst_gpio, 1);
	usleep_range(20, 25);

	gpiod_set_value_cansleep(host->rst_gpio, 0);
	usleep_range(20, 25);

	return 0;
}

static const struct ufs_hba_variant_ops ufs_hba_rk3576_vops = {
	.name = "rk3576",
	.init = ufs_rockchip_rk3576_init,
	.device_reset = ufs_rockchip_device_reset,
	.hce_enable_notify = ufs_rockchip_hce_enable_notify,
	.phy_initialization = ufs_rockchip_rk3576_phy_init,
};

static const struct of_device_id ufs_rockchip_of_match[] = {
	{ .compatible = "rockchip,rk3576-ufshc", .data = &ufs_hba_rk3576_vops },
	{ },
};
MODULE_DEVICE_TABLE(of, ufs_rockchip_of_match);

static int ufs_rockchip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct ufs_hba_variant_ops *vops;
	int err;

	vops = device_get_match_data(dev);
	if (!vops)
		return dev_err_probe(dev, -ENODATA, "ufs_hba_variant_ops not defined.\n");

	err = ufshcd_pltfrm_init(pdev, vops);
	if (err)
		return dev_err_probe(dev, err, "ufshcd_pltfrm_init failed\n");

	return 0;
}

static void ufs_rockchip_remove(struct platform_device *pdev)
{
	ufshcd_pltfrm_remove(pdev);
}

#ifdef CONFIG_PM
static int ufs_rockchip_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_rockchip_host *host = ufshcd_get_variant(hba);

	clk_disable_unprepare(host->ref_out_clk);

	/* Do not power down the genpd if rpm_lvl is less than level 5 */
	dev_pm_genpd_rpm_always_on(dev, hba->rpm_lvl < UFS_PM_LVL_5);

	return ufshcd_runtime_suspend(dev);
}

static int ufs_rockchip_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_rockchip_host *host = ufshcd_get_variant(hba);
	int err;

	err = clk_prepare_enable(host->ref_out_clk);
	if (err) {
		dev_err(hba->dev, "failed to enable ref_out clock %d\n", err);
		return err;
	}

	reset_control_assert(host->rst);
	udelay(1);
	reset_control_deassert(host->rst);

	return ufshcd_runtime_resume(dev);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int ufs_rockchip_system_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_rockchip_host *host = ufshcd_get_variant(hba);
	int err;

	/*
	 * If spm_lvl is less than level 5, it means we need to keep the host
	 * controller in powered-on state. So device_set_awake_path() is
	 * calling pm core to notify the genpd provider to meet this requirement
	 */
	if (hba->spm_lvl < UFS_PM_LVL_5)
		device_set_awake_path(dev);

	err = ufshcd_system_suspend(dev);
	if (err) {
		dev_err(hba->dev, "UFSHCD system suspend failed %d\n", err);
		return err;
	}

	clk_disable_unprepare(host->ref_out_clk);

	return 0;
}

static int ufs_rockchip_system_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_rockchip_host *host = ufshcd_get_variant(hba);
	int err;

	err = clk_prepare_enable(host->ref_out_clk);
	if (err) {
		dev_err(hba->dev, "failed to enable ref_out clock %d\n", err);
		return err;
	}

	return ufshcd_system_resume(dev);
}
#endif

static const struct dev_pm_ops ufs_rockchip_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufs_rockchip_system_suspend, ufs_rockchip_system_resume)
	SET_RUNTIME_PM_OPS(ufs_rockchip_runtime_suspend, ufs_rockchip_runtime_resume, NULL)
	.prepare	 = ufshcd_suspend_prepare,
	.complete	 = ufshcd_resume_complete,
};

static struct platform_driver ufs_rockchip_pltform = {
	.probe = ufs_rockchip_probe,
	.remove = ufs_rockchip_remove,
	.driver = {
		.name = "ufshcd-rockchip",
		.pm = &ufs_rockchip_pm_ops,
		.of_match_table = ufs_rockchip_of_match,
	},
};
module_platform_driver(ufs_rockchip_pltform);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rockchip UFS Host Driver");
