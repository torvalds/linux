// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip MIPI Synopsys DPHY RX0 driver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on:
 *
 * drivers/media/platform/rockchip/isp1/mipi_dphy_sy.c
 * in https://chromium.googlesource.com/chromiumos/third_party/kernel,
 * chromeos-4.4 branch.
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *   Jacob Chen <jacob2.chen@rock-chips.com>
 *   Shunqian Zheng <zhengsq@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include<linux/reset.h>

#include <linux/regulator/consumer.h>
#include "7110-m31-dphy.h"

//syscfg registers
#define SCFG_DSI_CSI_SEL	        0x2c
#define SCFG_PHY_RESETB	            0x30
#define SCFG_REFCLK_SEL	            0x34
#define SCFG_DBUS_PW_PLL_SSC_LD0	0x38
#define SCFG_GRS_CDTX_PLL       	0x3c

#define SCFG_RG_CDTX_PLL_FBK_PRE	0x44
#define SCFG_RG_CLANE_DLANE_TIME   	0x58
#define SCFG_RG_CLANE_HS_TIME   	0x58

#define SCFG_RG_EXTD_CYCLE_SEL   	0x5c

#define SCFG_L0N_L0P_HSTX	        0x60
#define SCFG_L1N_L1P_HSTX	        0x64
#define SCFG_L2N_L2P_HSTX	        0x68
#define SCFG_L3N_L3P_HSTX	        0x6c
#define SCFG_L4N_L4P_HSTX	        0x70
#define SCFG_LX_SWAP_SEL	        0x78

#define SCFG_HS_PRE_ZERO_T_D	    0xc4
#define SCFG_TXREADY_SRC_SEL_D	    0xc8
#define SCFG_HS_PRE_ZERO_T_C	    0xd4
#define SCFG_TXREADY_SRC_SEL_C	    0xd8

//reg SCFG_LX_SWAP_SEL
#define	OFFSET_CFG_L0_SWAP_SEL 	0
#define	OFFSET_CFG_L1_SWAP_SEL 	3
#define	OFFSET_CFG_L2_SWAP_SEL 	6
#define	OFFSET_CFG_L3_SWAP_SEL 	9
#define OFFSET_CFG_L4_SWAP_SEL 	12

//reg SCFG_DBUS_PW_PLL_SSC_LD0
#define OFFSET_SCFG_CFG_DATABUD16_SEL    0
#define OFFSET_SCFG_PWRON_READY_N        1
#define OFFSET_RG_CDTX_PLL_FM_EN         2
#define OFFSET_SCFG_PLLSSC_EN            3
#define OFFSET_RG_CDTX_PLL_LDO_STB_X2_EN 4

//reg SCFG_RG_CLANE_DLANE_TIME
#define OFFSET_DHS_PRE_TIME          8
#define OFFSET_DHS_TRIAL_TIME        16
#define OFFSET_DHS_ZERO_TIME         24

//reg SCFG_RG_CLANE_HS_TIME
#define OFFSET_CHS_PRE_TIME          8
#define OFFSET_CHS_TRIAL_TIME        16
#define OFFSET_CHS_ZERO_TIME         24

//dsitx registers
#define  VID_MCTL_MAIN_DATA_CTL	        0x04
#define  VID_MCTL_MAIN_PHY_CTL	        0x08
#define  VID_MCTL_MAIN_EN	            0x0c
#define  VID_MAIN_CTRL_ADDR    0xb0
#define  VID_VSIZE1_ADDR       0xb4
#define  VID_VSIZE2_ADDR       0xb8
#define  VID_HSIZE1_ADDR       0xc0
#define  VID_HSIZE2_ADDR       0xc4
#define  VID_BLKSIZE1_ADDR     0xCC
#define  VID_BLKSIZE2_ADDR     0xd0
#define  VID_PCK_TIME_ADDR     0xd8
#define  VID_DPHY_TIME_ADDR    0xdc
#define  VID_ERR_COLOR1_ADDR   0xe0
#define  VID_ERR_COLOR2_ADDR   0xe4
#define  VID_VPOS_ADDR         0xe8
#define  VID_HPOS_ADDR         0xec
#define  VID_MODE_STAT_ADDR    0xf0
#define  VID_VCA_SET1_ADDR     0xf4
#define  VID_VCA_SET2_ADDR     0xf8


#define  VID_MODE_STAT_CLR_ADDR    0x160
#define  VID_MODE_STAT_FLAG_ADDR   0x180

#define  TVG_CTRL_ADDR      0x0fc
#define  TVG_IMG_SIZE_ADDR  0x100
#define  TVG_COLOR1_ADDR    0x104
#define  TVG_COLOR1BIT_ADDR 0x108
#define  TVG_COLOR2_ADDR    0x10c
#define  TVG_COLOR2BIT_ADDR 0x110
#define  TVG_STAT_ADDR      0x114
#define  TVG_STAT_CTRL_ADDR 0x144
#define  TVG_STAT_CLR_ADDR  0x164
#define  TVG_STAT_FLAG_ADDR 0x184

#define  DPI_IRQ_EN_ADDR   0x1a0
#define  DPI_IRQ_CLR_ADDR  0x1a4
#define  DPI_IRQ_STAT_ADDR 0x1a4
#define  DPI_CFG_ADDR      0x1ac


//sysrst registers
#define SRST_ASSERT0	    0x00
#define SRST_STATUS0    	0x04
/* Definition controller bit for syd rst registers */
#define BIT_RST_DSI_DPI_PIX		17

struct sf_dphy {
	struct device *dev;
	void __iomem *topsys;

	struct clk_bulk_data *clks;

	struct clk * txesc_clk;
    struct reset_control *sys_rst;
	struct reset_control *txbytehs_rst;

	void __iomem *aonsys;//7110 aonsys con

	struct phy_configure_opts_mipi_dphy config;

	u8 hsfreq;

	struct regulator *mipitx_1p8;
	struct regulator *mipitx_0p9;

	struct phy *phy;
};

static int sf_dphy_clkrst_get(struct device *dev, struct sf_dphy *dphy)
{
	dphy->txesc_clk = devm_clk_get(dev, "dphy_txesc");
	if (IS_ERR(dphy->txesc_clk)){
		dev_err(dev, "===txesc_clk get error\n");
		return PTR_ERR(dphy->txesc_clk);
	}
	dphy->sys_rst = reset_control_get_exclusive(dev, "dphy_sys");
	if (IS_ERR(dphy->sys_rst)){
		dev_err(dev, "===sys_rst get error\n");
		return PTR_ERR(dphy->sys_rst);
	}

	return 0;
}
static int sf_dphy_clkrst_ena_deas(struct device *dev, struct sf_dphy *dphy)
{
	int ret;
	
	ret = clk_prepare_enable(dphy->txesc_clk);
	if (ret) {
        dev_err(dev, "failed to prepare/enable txesc_clk\n");
        return ret;
    }
	ret = reset_control_deassert(dphy->sys_rst);
	if (ret < 0) {
        dev_err(dev, "failed to deassert sys_rst\n");
        return ret;
    }

	return ret;
}

static int sf_dphy_clkrst_disa_assert(struct device *dev, struct sf_dphy *dphy)
{
	int ret;
	ret = reset_control_assert(dphy->sys_rst);
	if (ret < 0) {
        dev_err(dev, "failed to assert sys_rst\n");
        return ret;
    }

	clk_disable_unprepare(dphy->txesc_clk);

	return ret;
}

static int is_pll_locked(struct sf_dphy *dphy)
{
	int tmp = sf_dphy_get_reg(dphy->topsys + 0x8,
								RGS_CDTX_PLL_UNLOCK_SHIFT, RGS_CDTX_PLL_UNLOCK_MASK);
    return !tmp;
}
static void reset(int assert, struct sf_dphy *dphy)
{
	dev_info(dphy->dev, "1 SET_U0_MIPITX_DPHY_RESETB\n");
	sf_dphy_set_reg(dphy->topsys + 0x64, (!assert), RESETB_SHIFT, RESETB_MASK);
	dev_info(dphy->dev, "2 SET_U0_MIPITX_DPHY_RESETB\n");

    if (!assert) {
		while(!is_pll_locked(dphy));
		dev_info(dphy->dev, "MIPI dphy-tx # PLL Locked\n");
    }
}

static int sys_m31_dphy_tx_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct sf_dphy *dphy;
	uint32_t bitrate;
	unsigned long alignment;
	int i;
	const struct m31_dphy_config *p;
	const uint32_t AON_POWER_READY_N_active = 0;
	dphy = phy_get_drvdata(phy);	
	//bitrate = 680000000;//1228M 60fps
	//bitrate = 750000000;//1188M 60fps
	//  bitrate = 490000000;//1188M 60fps
	bitrate = opts->mipi_dphy.hs_clk_rate;//1188M 60fps
	printk("sys_m31_dphy_tx_configure opts->mipi_dphy.hs_clk_rate = %ld\n",opts->mipi_dphy.hs_clk_rate);


	sf_dphy_set_reg(dphy->topsys + 0x8, 0x10, 
					RG_CDTX_L0N_HSTX_RES_SHIFT, RG_CDTX_L0N_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0xC, 0x10,
					RG_CDTX_L0N_HSTX_RES_SHIFT, RG_CDTX_L0N_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0xC, 0x10,
					RG_CDTX_L2N_HSTX_RES_SHIFT, RG_CDTX_L2N_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0xC, 0x10,
					RG_CDTX_L3N_HSTX_RES_SHIFT, RG_CDTX_L3N_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0x10, 0x10,
					RG_CDTX_L4N_HSTX_RES_SHIFT, RG_CDTX_L4N_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0x8, 0x10,
					RG_CDTX_L0P_HSTX_RES_SHIFT, RG_CDTX_L0P_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0xC, 0x10,
					RG_CDTX_L1P_HSTX_RES_SHIFT, RG_CDTX_L1P_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0xC, 0x10,
					RG_CDTX_L2P_HSTX_RES_SHIFT, RG_CDTX_L2P_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0xC, 0x10,
					RG_CDTX_L3P_HSTX_RES_SHIFT, RG_CDTX_L3P_HSTX_RES_MASK);
	sf_dphy_set_reg(dphy->topsys + 0x10, 0x10,
					RG_CDTX_L4P_HSTX_RES_SHIFT, RG_CDTX_L4P_HSTX_RES_MASK);

	dev_info(dphy->dev,"request dphy hs_rate %dMbps\n", bitrate/1000000);
	if (is_pll_locked(dphy))
		dev_info(dphy->dev, "Error: MIPI dphy-tx # PLL is not supposed to be LOCKED\n");
	else
		dev_info(dphy->dev, "MIPI dphy-tx # PLL is not LOCKED\n");

    alignment = M31_DPHY_BITRATE_ALIGN;
    if (bitrate % alignment) {
        bitrate += alignment - (bitrate % alignment);
    }

    dev_info(dphy->dev, "want dphy hs_rate %dMbps\n", bitrate/1000000);

    p = m31_dphy_configs;
    for (i = 0; i < ARRAY_SIZE(m31_dphy_configs); i++, p++) {
        if (p->bitrate == bitrate) {
            dev_info(dphy->dev, "config dphy hs_rate %dMbps\n", bitrate/1000000);

			sf_dphy_set_reg(dphy->topsys + 0x64, M31_DPHY_REFCLK, REFCLK_IN_SEL_SHIFT, REFCLK_IN_SEL_MASK);

            dev_info(dphy->dev, "MIPI dphy-tx # AON_POWER_READY_N active(%d)\n", AON_POWER_READY_N_active);


			sf_dphy_set_reg(dphy->topsys, AON_POWER_READY_N_active,
							AON_POWER_READY_N_SHIFT, AON_POWER_READY_N_MASK);

			sf_dphy_set_reg(dphy->topsys, 0x0,
							CFG_L0_SWAP_SEL_SHIFT, CFG_L0_SWAP_SEL_MASK);//Lane setting
			sf_dphy_set_reg(dphy->topsys, 0x1,
							CFG_L1_SWAP_SEL_SHIFT, CFG_L1_SWAP_SEL_MASK);
			sf_dphy_set_reg(dphy->topsys, 0x2,
							CFG_L2_SWAP_SEL_SHIFT, CFG_L2_SWAP_SEL_MASK);
			sf_dphy_set_reg(dphy->topsys, 0x3,
							CFG_L3_SWAP_SEL_SHIFT, CFG_L3_SWAP_SEL_MASK);
			sf_dphy_set_reg(dphy->topsys, 0x4,
							CFG_L4_SWAP_SEL_SHIFT, CFG_L4_SWAP_SEL_MASK);
			//PLL setting
			sf_dphy_set_reg(dphy->topsys + 0x1c, 0x0,
							RG_CDTX_PLL_SSC_EN_SHIFT, RG_CDTX_PLL_SSC_EN_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x18, 0x1,
							RG_CDTX_PLL_LDO_STB_X2_EN_SHIFT, RG_CDTX_PLL_LDO_STB_X2_EN_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x18, 0x1,
							RG_CDTX_PLL_FM_EN_SHIFT, RG_CDTX_PLL_FM_EN_MASK);

			sf_dphy_set_reg(dphy->topsys + 0x18, p->pll_prev_div,
							RG_CDTX_PLL_PRE_DIV_SHIFT, RG_CDTX_PLL_PRE_DIV_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x18, p->pll_fbk_int,
							RG_CDTX_PLL_FBK_INT_SHIFT, RG_CDTX_PLL_FBK_INT_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x14, p->pll_fbk_fra,
							RG_CDTX_PLL_FBK_FRA_SHIFT, RG_CDTX_PLL_FBK_FRA_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x28, p->extd_cycle_sel,
							RG_EXTD_CYCLE_SEL_SHIFT, RG_EXTD_CYCLE_SEL_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x24, p->dlane_hs_pre_time,
							RG_DLANE_HS_PRE_TIME_SHIFT, RG_DLANE_HS_PRE_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x24, p->dlane_hs_pre_time,
							RG_DLANE_HS_PRE_TIME_SHIFT, RG_DLANE_HS_PRE_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x24, p->dlane_hs_zero_time,
							RG_DLANE_HS_ZERO_TIME_SHIFT, RG_DLANE_HS_ZERO_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x24, p->dlane_hs_trail_time,
							RG_DLANE_HS_TRAIL_TIME_SHIFT, RG_DLANE_HS_TRAIL_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x20, p->clane_hs_pre_time,
							RG_CLANE_HS_PRE_TIME_SHIFT, RG_CLANE_HS_PRE_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x24, p->clane_hs_zero_time,
							RG_CLANE_HS_ZERO_TIME_SHIFT, RG_CLANE_HS_ZERO_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x20, p->clane_hs_trail_time,
							RG_CLANE_HS_TRAIL_TIME_SHIFT, RG_CLANE_HS_TRAIL_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x20, p->clane_hs_clk_pre_time,
							RG_CLANE_HS_CLK_PRE_TIME_SHIFT, RG_CLANE_HS_CLK_PRE_TIME_MASK);
			sf_dphy_set_reg(dphy->topsys + 0x20, p->clane_hs_clk_post_time,
							RG_CLANE_HS_CLK_POST_TIME_SHIFT, RG_CLANE_HS_CLK_POST_TIME_MASK);

			break;
		}
	}

	return -ENOTSUPP;
}

static int sf_dphy_power_on(struct phy *phy)
{

	struct sf_dphy *dphy = phy_get_drvdata(phy);
	int ret;

	reset(0, dphy);
	sf_dphy_set_reg(dphy->topsys + 0x30, 0,
					SCFG_PPI_C_READY_SEL_SHIFT, SCFG_PPI_C_READY_SEL_MASK);
	sf_dphy_set_reg(dphy->topsys + 0x30, 0,
					SCFG_DSI_TXREADY_ESC_SEL_SHIFT, SCFG_DSI_TXREADY_ESC_SEL_MASK);
	sf_dphy_set_reg(dphy->topsys + 0x2c, 0x30,
					SCFG_C_HS_PRE_ZERO_TIME_SHIFT, SCFG_C_HS_PRE_ZERO_TIME_MASK);

	ret = sf_dphy_clkrst_ena_deas(dphy->dev, dphy);//clk rst interface enable and deassert

	return 0;
}

static int sf_dphy_power_off(struct phy *phy)
{
	struct sf_dphy *dphy = phy_get_drvdata(phy);

	sf_dphy_clkrst_disa_assert(dphy->dev, dphy);
	reset(1, dphy);
	return 0;
}

static int sf_dphy_init(struct phy *phy)
{
	struct sf_dphy *dphy = phy_get_drvdata(phy);
	uint32_t temp;
	int ret;

	temp = 0;
	temp = sf_dphy_get_reg(dphy->aonsys, AON_GP_REG_SHIFT,AON_GP_REG_MASK);
	dev_info(dphy->dev, "GET_AON_GP_REG\n");

	if (!(temp & DPHY_TX_PSW_EN_MASK)) {
		temp |= DPHY_TX_PSW_EN_MASK;
		sf_dphy_set_reg(dphy->aonsys, temp,AON_GP_REG_SHIFT,AON_GP_REG_MASK);
	}
	dev_info(dphy->dev, "control ECO\n");

	//pmic turn on
	ret = regulator_enable(dphy->mipitx_0p9);
	if (ret) {
		dev_err(dphy->dev, "Cannot enable mipitx_0p9 regulator\n");
		//goto err_reg_0p9;
	}
	udelay(100);	
	ret = regulator_enable(dphy->mipitx_1p8);
	if (ret) {
		dev_err(dphy->dev, "Cannot enable mipitx_1p8 regulator\n");
		//goto err_reg_1p8;
	}
	udelay(100);
	//mipi_pmic setting

	return 0;
}

static int sf_dphy_validate(struct phy *phy, enum phy_mode mode, int submode,
			union phy_configure_opts *opts)
{
	return 0;
}

static int sf_dphy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	return 0;
}


static int sf_dphy_exit(struct phy *phy)
{
	return 0;
}

static const struct phy_ops sf_dphy_ops = {
	.power_on	= sf_dphy_power_on,
	.power_off	= sf_dphy_power_off,
	.init		= sf_dphy_init,
	.exit		= sf_dphy_exit,
	//.configure	= sf_dphy_configure,
	.configure	= sys_m31_dphy_tx_configure,
	.validate  = sf_dphy_validate,
	.set_mode  = sf_dphy_set_mode,
	.owner		= THIS_MODULE,
};

static const struct of_device_id sf_dphy_dt_ids[] = {
	{
		.compatible = "m31,mipi-dphy-tx",
	},
	{}
};
MODULE_DEVICE_TABLE(of, sf_dphy_dt_ids);

static int sf_dphy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct sf_dphy *dphy;
	struct resource *res;
	int ret;

	dev_info(&pdev->dev, "sf_dphy_probe begin\n");
	dphy = devm_kzalloc(&pdev->dev, sizeof(*dphy), GFP_KERNEL);
	if (!dphy)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, dphy);

	dev_info(&pdev->dev, "===> %s enter, %d \n", __func__, __LINE__);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dphy->topsys = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dphy->topsys))
		return PTR_ERR(dphy->topsys);


	dphy->phy = devm_phy_create(&pdev->dev, NULL, &sf_dphy_ops);
	if (IS_ERR(dphy->phy)) {
		dev_err(&pdev->dev, "failed to create phy\n");
		return PTR_ERR(dphy->phy);
	}
	phy_set_drvdata(dphy->phy, dphy);

	dphy->dev = &pdev->dev;
	// this power switch control bit was added in ECO, check ECO item "aon psw_en" for detail
	dev_info(dphy->dev, "control ECO\n");
	dphy->aonsys = ioremap(0x17010000, 0x10000);

	//mipi_pmic setting
	dphy->mipitx_1p8 = devm_regulator_get(&pdev->dev, "mipi_1p8");
	if (IS_ERR(dphy->mipitx_1p8))
		return PTR_ERR(dphy->mipitx_1p8);

	dphy->mipitx_0p9 = devm_regulator_get(&pdev->dev, "mipi_0p9");
	if (IS_ERR(dphy->mipitx_0p9))
		return PTR_ERR(dphy->mipitx_0p9);


	ret = sf_dphy_clkrst_get(&pdev->dev, dphy);

	phy_provider = devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);

	dev_info(&pdev->dev, "sf_dphy_probe end\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver sf_dphy_driver = {
	.probe = sf_dphy_probe,
	.driver = {
		.name	= "sf-mipi-dphy-tx",
		.of_match_table = sf_dphy_dt_ids,
	},
};
module_platform_driver(sf_dphy_driver);

MODULE_AUTHOR("Ezequiel Garcia <ezequiel@collabora.com>");
MODULE_DESCRIPTION("sf MIPI  DPHY TX0 driver");
MODULE_LICENSE("Dual MIT/GPL");
