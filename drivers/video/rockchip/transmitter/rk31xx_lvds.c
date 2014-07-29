/*
 * drivers/video/rockchip/lcdc/rk31xx_lcdc.c
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
                dsb();                                          \
        } while (0)


static struct rk_lvds_device *rk31xx_lvds;

static int rk31xx_lvds_clk_init(struct rk_lvds_device *lvds)
{
        lvds->pclk = devm_clk_get(lvds->dev, "pclk_lvds");
	if (IS_ERR(lvds->pclk)) {
		dev_err(lvds->dev, "get clk failed\n");
		return PTR_ERR(lvds->pclk);
	}
 
	lvds->pd = devm_clk_get(lvds->dev,"pd_lvds");
	if (IS_ERR(lvds->pd)) {
		dev_err(lvds->dev, "get clk failed\n");
		return PTR_ERR(lvds->pd);
	}	
	return 0;	
}

static int rk31xx_lvds_clk_enable(struct rk_lvds_device *lvds)
{
	if (!lvds->clk_on) {
		clk_prepare_enable(lvds->pd);
		clk_prepare_enable(lvds->pclk);
		lvds->clk_on = true;
	}

	return 0;
}

static int rk31xx_lvds_clk_disable(struct rk_lvds_device *lvds)
{
	if (lvds->clk_on) {
		clk_disable_unprepare(lvds->pclk);
		clk_disable_unprepare(lvds->pd);
		lvds->clk_on = false;
	}

	return 0;
}

static int rk31xx_lvds_pwr_on(void)
{
        struct rk_lvds_device *lvds = rk31xx_lvds;

        if (lvds->screen.type == SCREEN_LVDS) {
                /* power up lvds pll and bandgap */
	        lvds_msk_reg(lvds, MIPIPHY_REGEA,
	                     m_BG_POWER_DOWN | m_PLL_POWER_DOWN,
	                     v_BG_POWER_DOWN(0) | v_PLL_POWER_DOWN(0));

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

	/* power down lvds pll and bandgap */
	lvds_msk_reg(lvds, MIPIPHY_REGEA, m_BG_POWER_DOWN | m_PLL_POWER_DOWN,
	             v_BG_POWER_DOWN(1) | v_PLL_POWER_DOWN(1));
	/* disable lvds */
	lvds_msk_reg(lvds, MIPIPHY_REGE3, m_LVDS_EN | m_TTL_EN,
	             v_LVDS_EN(0) | v_TTL_EN(0));
        return 0;
}

static int rk31xx_lvds_disable(void)
{
	struct rk_lvds_device *lvds = rk31xx_lvds;

        if (!lvds->sys_state)
                return 0;

	grf_writel(v_LVDSMODE_EN(0) | v_MIPIPHY_TTL_EN(0), RK31XX_GRF_LVDS_CON0);

        rk31xx_lvds_pwr_off();
	rk31xx_lvds_clk_disable(lvds);
        lvds->sys_state = false;
	return 0;
}

static void rk31xx_output_lvds(struct rk_lvds_device *lvds,
                               struct rk_screen *screen)
{
	u32 val = 0;

        /* if LVDS transmitter source from VOP, vop_dclk need get invert
         * set iomux in dts pinctrl
         */
	val = 0;
	val |= v_LVDSMODE_EN(1) | v_MIPIPHY_TTL_EN(0);      /* enable lvds mode */
	val |= v_LVDS_DATA_SEL(LVDS_DATA_FROM_LCDC);    /* config data source */
	val |= v_LVDS_OUTPUT_FORMAT(screen->lvds_format); /* config lvds_format */
	val |= v_LVDS_MSBSEL(LVDS_MSB_D7);      /* LSB receive mode */
	grf_writel(val, RK31XX_GRF_LVDS_CON0);

        /* enable lvds lane */
        val = v_LANE0_EN(1) | v_LANE1_EN(1) | v_LANE2_EN(1) | v_LANE3_EN(1) |
                v_LANECLK_EN(1);
	lvds_writel(lvds, MIPIPHY_REG0, val);

        /* set pll prediv and fbdiv */
	lvds_writel(lvds, MIPIPHY_REG3, v_PREDIV(1) | v_FBDIV_MSB(0));
	lvds_writel(lvds, MIPIPHY_REG4, v_FBDIV_LSB(7));

        /* set lvds mode and reset phy config */
        val = v_LVDS_MODE_EN(1) | v_TTL_MODE_EN(0) | v_MIPI_MODE_EN(0) |
                v_MSB_SEL(1) | v_DIG_INTER_RST(1);
	lvds_writel(lvds, MIPIPHY_REGE0, val);

        lvds_writel(lvds, MIPIPHY_REGE1, 0x92);
#if 0        
	lvds_writel(lvds, MIPIPHY_REGE2, 0xa0); /* timing */
	lvds_writel(lvds, MIPIPHY_REGE7, 0xfc); /* phase */
#endif
        rk31xx_lvds_pwr_on();

}

static void rk31xx_output_lvttl(struct rk_lvds_device *lvds,
                                struct rk_screen *screen)
{
        u32 val = 0;

	val |= v_LVDSMODE_EN(0) | v_MIPIPHY_TTL_EN(1);      /* enable lvds mode */
	val |= v_LVDS_DATA_SEL(LVDS_DATA_FROM_LCDC);    /* config data source */
	grf_writel(val, RK31XX_GRF_LVDS_CON0);

        /* set pll prediv and fbdiv */
	lvds_writel(lvds, MIPIPHY_REG3, v_PREDIV(1) | v_FBDIV_MSB(0));
	lvds_writel(lvds, MIPIPHY_REG4, v_FBDIV_LSB(7));

        /* set ttl mode and reset phy config */
        val = v_LVDS_MODE_EN(0) | v_TTL_MODE_EN(1) | v_MIPI_MODE_EN(0) |
                v_MSB_SEL(1) | v_DIG_INTER_RST(1);
	lvds_writel(lvds, MIPIPHY_REGE0, val);

        lvds_writel(lvds, MIPIPHY_REGE1, 0x92);

        /* enable ttl */
	rk31xx_lvds_pwr_on();
		
}

static int rk31xx_lvds_en(void)
{
	struct rk_lvds_device *lvds = rk31xx_lvds;
	struct rk_screen *screen = &lvds->screen;

        if (lvds->sys_state)
                return 0;

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

static int rk31xx_lvds_probe(struct platform_device *pdev)
{
        struct rk_lvds_device *lvds;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
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

	rk_fb_get_prmry_screen(&lvds->screen);
        if ((lvds->screen.type != SCREEN_RGB) && 
		(lvds->screen.type != SCREEN_LVDS)) {
		dev_err(&pdev->dev, "screen is not lvds/rgb!\n");
		ret = -EINVAL;
                goto err_screen_type;
	}

	platform_set_drvdata(pdev, lvds);
	dev_set_name(lvds->dev, "rk31xx-lvds");

        /* lvds regs on MIPIPHY_REG */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lvds->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lvds->regbase)) {
		dev_err(&pdev->dev, "ioremap reg failed\n");
		return PTR_ERR(lvds->regbase);
	}

	ret = rk31xx_lvds_clk_init(lvds);
	if(ret < 0)
		goto err_clk_init;

        if (support_uboot_display())
		rk31xx_lvds_clk_enable(lvds);

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

#if defined(CONFIG_OF)
static const struct of_device_id rk31xx_lvds_dt_ids[] = {
	{.compatible = "rockchip,rk31xx-lvds",},
        {}
};
#endif

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

