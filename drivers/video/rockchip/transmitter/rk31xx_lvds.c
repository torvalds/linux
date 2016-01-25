/*
 * drivers/video/rockchip/transmitter/rk31xx_lvds.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 * Author: zhuangwenlong<zwl@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/rk_fb.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include "rk31xx_lvds.h"


#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v,offset)                                    \
        do {                                                    \
                writel_relaxed(v, RK_GRF_VIRT + offset);        \
		dsb(sy);                                        \
        } while (0)


static struct rk_lvds_device *rk31xx_lvds;

static int rk31xx_lvds_clk_init(struct rk_lvds_device *lvds)
{
        lvds->pclk = devm_clk_get(lvds->dev, "pclk_lvds");
	if (IS_ERR(lvds->pclk)) {
		dev_err(lvds->dev, "get pclk failed\n");
		return PTR_ERR(lvds->pclk);
	}

	lvds->ctrl_pclk = devm_clk_get(lvds->dev, "pclk_lvds_ctl");
	if (IS_ERR(lvds->ctrl_pclk)) {
		dev_err(lvds->dev, "get ctrl pclk failed\n");
		return PTR_ERR(lvds->ctrl_pclk);
	}

	if (lvds->data->soc_type == LVDS_SOC_RK312X) {
		lvds->ctrl_hclk = devm_clk_get(lvds->dev, "hclk_vio_h2p");
		if (IS_ERR(lvds->ctrl_hclk)) {
			dev_err(lvds->dev, "get ctrl hclk failed\n");
			return PTR_ERR(lvds->ctrl_hclk);
		}
	} else {
                lvds->pd = devm_clk_get(lvds->dev, "pd_lvds");
		if (IS_ERR(lvds->pd)) {
			dev_err(lvds->dev, "get pd_lvds failed\n");
			lvds->pd = NULL;
                }
        }

	return 0;	
}

static int rk31xx_lvds_clk_enable(struct rk_lvds_device *lvds)
{
	if (!lvds->clk_on) {
		clk_prepare_enable(lvds->pclk);
		clk_prepare_enable(lvds->ctrl_pclk);
		if (lvds->data->soc_type == LVDS_SOC_RK312X)
			clk_prepare_enable(lvds->ctrl_hclk);
		if (lvds->pd)
			clk_prepare_enable(lvds->pd);
		lvds->clk_on = true;
	}

	return 0;
}

static int rk31xx_lvds_clk_disable(struct rk_lvds_device *lvds)
{
	if (lvds->clk_on) {
		clk_disable_unprepare(lvds->pclk);
		if (lvds->data->soc_type == LVDS_SOC_RK312X)
			clk_disable_unprepare(lvds->ctrl_hclk);
		if (lvds->pd)
		        clk_disable_unprepare(lvds->pd);
		clk_disable_unprepare(lvds->ctrl_pclk);
		lvds->clk_on = false;
	}

	return 0;
}

static int rk31xx_lvds_pwr_on(void)
{
        struct rk_lvds_device *lvds = rk31xx_lvds;

        if (lvds->screen.type == SCREEN_LVDS) {
                /* set VOCM 900 mv and V-DIFF 350 mv */
	        lvds_msk_reg(lvds, MIPIPHY_REGE4, m_VOCM | m_DIFF_V,
			     v_VOCM(0) | v_DIFF_V(2));

                /* power up lvds pll and ldo */
	        lvds_msk_reg(lvds, MIPIPHY_REG1,
	                     m_SYNC_RST | m_LDO_PWR_DOWN | m_PLL_PWR_DOWN,
	                     v_SYNC_RST(0) | v_LDO_PWR_DOWN(0) | v_PLL_PWR_DOWN(0));

		/* enable lvds lane and power on pll */
		lvds_writel(lvds, MIPIPHY_REGEB,
			    v_LANE0_EN(1) | v_LANE1_EN(1) | v_LANE2_EN(1) |
			    v_LANE3_EN(1) | v_LANECLK_EN(1) | v_PLL_PWR_OFF(0));

	        /* enable lvds */
	        lvds_msk_reg(lvds, MIPIPHY_REGE3,
	                     m_MIPI_EN | m_LVDS_EN | m_TTL_EN,
	                     v_MIPI_EN(0) | v_LVDS_EN(1) | v_TTL_EN(0));
        } else {
                lvds_msk_reg(lvds, MIPIPHY_REGE3,
	                     m_MIPI_EN | m_LVDS_EN | m_TTL_EN,
	                     v_MIPI_EN(0) | v_LVDS_EN(0) | v_TTL_EN(1));
        }
        return 0;
}

static int rk31xx_lvds_pwr_off(void)
{
        struct rk_lvds_device *lvds = rk31xx_lvds;

	/* disable lvds lane and power off pll */
	lvds_writel(lvds, MIPIPHY_REGEB,
		    v_LANE0_EN(0) | v_LANE1_EN(0) | v_LANE2_EN(0) |
		    v_LANE3_EN(0) | v_LANECLK_EN(0) | v_PLL_PWR_OFF(1));

	/* power down lvds pll and bandgap */
	lvds_msk_reg(lvds, MIPIPHY_REG1,
	             m_SYNC_RST | m_LDO_PWR_DOWN | m_PLL_PWR_DOWN,
	             v_SYNC_RST(1) | v_LDO_PWR_DOWN(1) | v_PLL_PWR_DOWN(1));

	/* disable lvds */
	lvds_msk_reg(lvds, MIPIPHY_REGE3, m_LVDS_EN | m_TTL_EN,
	             v_LVDS_EN(0) | v_TTL_EN(0));
        return 0;
}

static int rk31xx_lvds_disable(void)
{
	struct rk_lvds_device *lvds = rk31xx_lvds;
	u32 val;

        if (unlikely(!lvds) || !lvds->sys_state)
                return 0;
	if (lvds->data->soc_type == LVDS_SOC_RK3368) {
		val = v_RK3368_LVDSMODE_EN(0) | v_RK3368_MIPIPHY_TTL_EN(0);
		lvds_grf_writel(lvds, RK3368_GRF_SOC_CON7_LVDS, val);
	} else {
		grf_writel(v_LVDSMODE_EN(0) | v_MIPIPHY_TTL_EN(0), RK312X_GRF_LVDS_CON0);
	}

	rk31xx_lvds_pwr_off();
	rk31xx_lvds_clk_disable(lvds);

#if !defined(CONFIG_RK_FPGA)
#ifdef CONFIG_PINCTRL
        if (lvds->screen.type == SCREEN_RGB) {
                if (lvds->dev->pins) {
                        pinctrl_select_state(lvds->dev->pins->p,
                                             lvds->dev->pins->sleep_state);
                } else if (lvds->pins && !IS_ERR(lvds->pins->sleep_state)) {
                        pinctrl_select_state(lvds->pins->p,
                                             lvds->pins->sleep_state);
                }
        }
#endif
#endif
        lvds->sys_state = false;
	return 0;
}

static void rk31xx_output_lvds(struct rk_lvds_device *lvds,
                               struct rk_screen *screen)
{
	u32 val = 0;
	u32 delay_times = 20;

        /* if LVDS transmitter source from VOP, vop_dclk need get invert
         * set iomux in dts pinctrl
         */
	if ((lvds->data->soc_type == LVDS_SOC_RK3368) ||
	    (lvds->data->soc_type == LVDS_SOC_RK3366)) {
		/* enable lvds mode */
		val |= v_RK3368_LVDSMODE_EN(1) | v_RK3368_MIPIPHY_TTL_EN(0);
		/* config data source */
		/*val |= v_LVDS_DATA_SEL(LVDS_DATA_FROM_LCDC); */
		/* config lvds_format */
		val |= v_RK3368_LVDS_OUTPUT_FORMAT(screen->lvds_format);
		/* LSB receive mode */
		val |= v_RK3368_LVDS_MSBSEL(LVDS_MSB_D7);
		val |= v_RK3368_MIPIPHY_LANE0_EN(1) |
		       v_RK3368_MIPIDPI_FORCEX_EN(1);
		/*rk3368  RK3368_GRF_SOC_CON7 = 0X0041C*/
		/*grf_writel(val, 0x0041C);*/
		if (lvds->data->soc_type == LVDS_SOC_RK3368)
			lvds_grf_writel(lvds, RK3368_GRF_SOC_CON7_LVDS, val);
		else
			lvds_grf_writel(lvds, RK3366_GRF_SOC_CON5_LVDS, val);
	} else {
		/* enable lvds mode */
		val |= v_LVDSMODE_EN(1) | v_MIPIPHY_TTL_EN(0);
		/* config data source */
		val |= v_LVDS_DATA_SEL(LVDS_DATA_FROM_LCDC);
		/* config lvds_format */
		val |= v_LVDS_OUTPUT_FORMAT(screen->lvds_format);
		/* LSB receive mode */
		val |= v_LVDS_MSBSEL(LVDS_MSB_D7);
		val |= v_MIPIPHY_LANE0_EN(1) | v_MIPIDPI_FORCEX_EN(1);
		/*rk312x  RK312X_GRF_LVDS_CON0 = 0X00150*/
		grf_writel(val, 0X00150);
	}
	/* digital internal disable */
	lvds_msk_reg(lvds, MIPIPHY_REGE1, m_DIG_INTER_EN, v_DIG_INTER_EN(0));

        /* set pll prediv and fbdiv */
	lvds_writel(lvds, MIPIPHY_REG3, v_PREDIV(2) | v_FBDIV_MSB(0));
	lvds_writel(lvds, MIPIPHY_REG4, v_FBDIV_LSB(28));

	lvds_writel(lvds, MIPIPHY_REGE8, 0xfc);

        /* set lvds mode and reset phy config */
	lvds_msk_reg(lvds, MIPIPHY_REGE0,
                     m_MSB_SEL | m_DIG_INTER_RST,
                     v_MSB_SEL(1) | v_DIG_INTER_RST(1));

	/* power on pll and enable lane */
	rk31xx_lvds_pwr_on();

	/* delay for waitting pll lock on */
	while (delay_times--) {
		if (lvds_phy_lockon(lvds)) {
			msleep(1);
			break;
		}
		udelay(100);
	}
	/* digital internal enable */
        lvds_msk_reg(lvds, MIPIPHY_REGE1, m_DIG_INTER_EN, v_DIG_INTER_EN(1));

#if 0
        lvds_writel(lvds, MIPIPHY_REGE2, 0xa0); /* timing */
        lvds_writel(lvds, MIPIPHY_REGE7, 0xfc); /* phase */
#endif

}

static void rk31xx_output_lvttl(struct rk_lvds_device *lvds,
                                struct rk_screen *screen)
{
        u32 val = 0;

	if ((lvds->data->soc_type == LVDS_SOC_RK3368) ||
	    (lvds->data->soc_type == LVDS_SOC_RK3366)) {
		/* iomux to lcdc */
#ifdef CONFIG_PINCTRL
		if (lvds->pins && !IS_ERR(lvds->pins->default_state))
			pinctrl_select_state(lvds->pins->p,
					     lvds->pins->default_state);
#endif
                lvds_dsi_writel(lvds, 0x0, 0x4);/*set clock lane enable*/
		/* enable lvds mode */
		val |= v_RK3368_LVDSMODE_EN(0) | v_RK3368_MIPIPHY_TTL_EN(1) |
			v_RK3368_MIPIPHY_LANE0_EN(1) |
			v_RK3368_MIPIDPI_FORCEX_EN(1);
		if (lvds->data->soc_type == LVDS_SOC_RK3368) {
			lvds_grf_writel(lvds, RK3368_GRF_SOC_CON7_LVDS, val);
			val = v_RK3368_FORCE_JETAG(0);
			lvds_grf_writel(lvds, RK3368_GRF_SOC_CON15_LVDS, val);
		} else {
			lvds_grf_writel(lvds, RK3366_GRF_SOC_CON5_LVDS, val);
			val = v_RK3368_FORCE_JETAG(0);
			lvds_grf_writel(lvds, RK3366_GRF_SOC_CON6_LVDS, val);
		}
		/*val = v_MIPITTL_CLK_EN(1) | v_MIPITTL_LANE0_EN(1) |
		v_MIPITTL_LANE1_EN(1) | v_MIPITTL_LANE2_EN(1) |
		v_MIPITTL_LANE3_EN(1);
		grf_writel(val, RK312X_GRF_SOC_CON1);*/
	} else {
		/* iomux to lcdc */
#if defined(CONFIG_RK_FPGA)
		grf_writel(0xffff5555, RK312X_GRF_GPIO2B_IOMUX);
		grf_writel(0x00ff0055, RK312X_GRF_GPIO2C_IOMUX);
		grf_writel(0x77771111, 0x00e8); /* RK312X_GRF_GPIO2C_IOMUX2 */
		grf_writel(0x700c1004, RK312X_GRF_GPIO2D_IOMUX);
#else
#ifdef CONFIG_PINCTRL
		if (lvds->pins && !IS_ERR(lvds->pins->default_state))
			pinctrl_select_state(lvds->pins->p,
					     lvds->pins->default_state);
#endif
#endif
		/* enable lvds mode */
		val |= v_LVDSMODE_EN(0) | v_MIPIPHY_TTL_EN(1);
		/* config data source */
		val |= v_LVDS_DATA_SEL(LVDS_DATA_FROM_LCDC);
		grf_writel(0xffff0380, RK312X_GRF_LVDS_CON0);

		val = v_MIPITTL_CLK_EN(1) | v_MIPITTL_LANE0_EN(1) |
			v_MIPITTL_LANE1_EN(1) | v_MIPITTL_LANE2_EN(1) |
			v_MIPITTL_LANE3_EN(1);
		grf_writel(val, RK312X_GRF_SOC_CON1);
	}
        /* enable lane */
        lvds_writel(lvds, MIPIPHY_REG0, 0x7f);
        val = v_LANE0_EN(1) | v_LANE1_EN(1) | v_LANE2_EN(1) | v_LANE3_EN(1) |
                v_LANECLK_EN(1) | v_PLL_PWR_OFF(1);
        lvds_writel(lvds, MIPIPHY_REGEB, val);

        /* set ttl mode and reset phy config */
        val = v_LVDS_MODE_EN(0) | v_TTL_MODE_EN(1) | v_MIPI_MODE_EN(0) |
                v_MSB_SEL(1) | v_DIG_INTER_RST(1);
	lvds_writel(lvds, MIPIPHY_REGE0, val);

	rk31xx_lvds_pwr_on();
		
}

static int rk31xx_lvds_en(void)
{
	struct rk_lvds_device *lvds = rk31xx_lvds;
	struct rk_screen *screen;

        if (unlikely(!lvds))//|| lvds->sys_state)
                return 0;

        screen = &lvds->screen;
	rk_fb_get_prmry_screen(screen);

	/* enable clk */
	rk31xx_lvds_clk_enable(lvds);

	switch (screen->type) {
        case SCREEN_LVDS:
		rk31xx_output_lvds(lvds, screen);
                break;
        case SCREEN_RGB:
		rk31xx_output_lvttl(lvds, screen);
                break;
        default:
                printk("unsupport screen type\n");
                break;
	}

        lvds->sys_state = true;
	return 0;
}

static struct rk_fb_trsm_ops trsm_lvds_ops = {
	.enable = rk31xx_lvds_en,
	.disable = rk31xx_lvds_disable,
	.dsp_pwr_on = rk31xx_lvds_pwr_on,
	.dsp_pwr_off = rk31xx_lvds_pwr_off,
};
#if defined(CONFIG_OF)
static struct rk_lvds_drvdata rk31xx_lvds_drvdata = {
	.soc_type =  LVDS_SOC_RK312X,
};

static struct rk_lvds_drvdata rk3368_lvds_drvdata = {
	.soc_type =  LVDS_SOC_RK3368,
};

static struct rk_lvds_drvdata rk3366_lvds_drvdata = {
	.soc_type =  LVDS_SOC_RK3366,
};

static const struct of_device_id rk31xx_lvds_dt_ids[] = {
	{.compatible = "rockchip,rk31xx-lvds",
	 .data = (void *)&rk31xx_lvds_drvdata,},
	{.compatible = "rockchip,rk3368-lvds",
	 .data = (void *)&rk3368_lvds_drvdata,},
	{.compatible = "rockchip,rk3366-lvds",
	 .data = (void *)&rk3366_lvds_drvdata,},
	{}
};

/*MODULE_DEVICE_TABLE(of, rk31xx_lvds_dt_ids);*/

#endif

static int rk31xx_lvds_probe(struct platform_device *pdev)
{
        struct rk_lvds_device *lvds;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
        int ret = 0;

	if (!np) {
		dev_err(&pdev->dev, "Don't find lvds device tree node.\n");
		return -EINVAL;
	}	

        lvds = devm_kzalloc(&pdev->dev, sizeof(struct rk_lvds_device), GFP_KERNEL);
	if (!lvds) {
		dev_err(&pdev->dev, "kzalloc rk31xx lvds failed\n");
		return -ENOMEM;
	}
	lvds->dev = &pdev->dev;
	match = of_match_node(rk31xx_lvds_dt_ids, np);
	lvds->data = (struct rk_lvds_drvdata *)match->data;
	dev_info(lvds->dev, "%s,type=%d\n",
		 __func__, lvds->data->soc_type);

	rk_fb_get_prmry_screen(&lvds->screen);
        if ((lvds->screen.type != SCREEN_RGB) && 
		(lvds->screen.type != SCREEN_LVDS)) {
		dev_err(&pdev->dev, "screen is not lvds/rgb!\n");
		ret = -EINVAL;
                goto err_screen_type;
	}

	platform_set_drvdata(pdev, lvds);
	dev_set_name(lvds->dev, "rk31xx-lvds");

#ifdef CONFIG_PINCTRL
        if (lvds->dev->pins == NULL && lvds->screen.type == SCREEN_RGB) {
                lvds->pins = devm_kzalloc(lvds->dev, sizeof(*(lvds->pins)),
                                          GFP_KERNEL);
                if (!lvds->pins) {
                        dev_err(lvds->dev, "kzalloc lvds pins failed\n");
                        return -ENOMEM;
                }

                lvds->pins->p = devm_pinctrl_get(lvds->dev);
                if (IS_ERR(lvds->pins->p)) {
                        dev_info(lvds->dev, "no pinctrl handle\n");
                        devm_kfree(lvds->dev, lvds->pins);
                        lvds->pins = NULL;
                } else {
                        lvds->pins->default_state =
                                pinctrl_lookup_state(lvds->pins->p, "lcdc");
                        lvds->pins->sleep_state =
                                pinctrl_lookup_state(lvds->pins->p, "sleep");
                        if (IS_ERR(lvds->pins->default_state)) {
                                dev_info(lvds->dev, "no default pinctrl state\n");
                                devm_kfree(lvds->dev, lvds->pins);
                                lvds->pins = NULL;
                        }
                }
        }

#endif
        /* lvds regs on MIPIPHY_REG */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mipi_lvds_phy");
	lvds->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lvds->regbase)) {
		dev_err(&pdev->dev, "ioremap mipi-lvds phy reg failed\n");
		return PTR_ERR(lvds->regbase);
	}

	/* pll lock on status reg that is MIPICTRL Register */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mipi_lvds_ctl");
	lvds->ctrl_reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lvds->ctrl_reg)) {
		dev_err(&pdev->dev, "ioremap mipi-lvds ctl reg failed\n");
		return PTR_ERR(lvds->ctrl_reg);
	}
#ifdef CONFIG_MFD_SYSCON
	if ((lvds->data->soc_type == LVDS_SOC_RK3368) ||
	    (lvds->data->soc_type == LVDS_SOC_RK3366)) {
		lvds->grf_lvds_base =
			syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (IS_ERR(lvds->grf_lvds_base)) {
			dev_err(&pdev->dev, "can't find rockchip,grf property\n");
			return PTR_ERR(lvds->grf_lvds_base);
		}
	}
#endif
	ret = rk31xx_lvds_clk_init(lvds);
	if(ret < 0)
		goto err_clk_init;

	if (support_uboot_display()) {
		rk31xx_lvds_clk_enable(lvds);
		/*lvds->sys_state = true;*/
	}

	rk31xx_lvds = lvds;
	rk_fb_trsm_ops_register(&trsm_lvds_ops, SCREEN_LVDS);
	dev_info(&pdev->dev, "rk31xx lvds driver probe success\n");

	return 0;

err_clk_init:
err_screen_type:
        devm_kfree(&pdev->dev, lvds);
        lvds = NULL;
        return ret;	
}

static int rk31xx_lvds_remove(struct platform_device *pdev)
{	
	return 0;
}

static void rk31xx_lvds_shutdown(struct platform_device *pdev)
{
	return;
}


static struct platform_driver rk31xx_lvds_driver = {
	.driver		= {
		.name	= "rk31xx-lvds",
		.owner	= THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(rk31xx_lvds_dt_ids),
#endif
	},
	.probe		= rk31xx_lvds_probe,
	.remove		= rk31xx_lvds_remove,
	.shutdown	= rk31xx_lvds_shutdown,
};

static int __init rk31xx_lvds_init(void)
{
	return platform_driver_register(&rk31xx_lvds_driver);
}

static void __exit rk31xx_lvds_exit(void)
{
	platform_driver_unregister(&rk31xx_lvds_driver);
}

fs_initcall(rk31xx_lvds_init);
module_exit(rk31xx_lvds_exit);

