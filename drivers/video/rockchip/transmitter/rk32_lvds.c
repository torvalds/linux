#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/rk_fb.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include "rk32_lvds.h"


static struct rk32_lvds *rk32_lvds;
static int rk32_lvds_disable(void)
{
	struct rk32_lvds *lvds = rk32_lvds;
	writel_relaxed(0x80008000, RK_GRF_VIRT + RK3288_GRF_SOC_CON7);
	writel_relaxed(0x00, lvds->regs + LVDS_CFG_REG_21); /*disable tx*/
	writel_relaxed(0xff, lvds->regs + LVDS_CFG_REG_c); /*disable pll*/
	return 0;
}

static int rk32_lvds_en(void)
{
	struct rk32_lvds *lvds = rk32_lvds;
	struct rk_screen *screen = &lvds->screen;
	u32 h_bp = screen->mode.hsync_len + screen->mode.left_margin;
	u32 val ;

	if (screen->lcdc_id == 0)
		val = LVDS_SEL_VOP_LIT | (LVDS_SEL_VOP_LIT << 16);
	else
		val = LVDS_SEL_VOP_LIT << 16;
	writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);

	val = screen->lvds_format;
	if (screen->type == SCREEN_DUAL_LVDS)
		val |= LVDS_DUAL | LVDS_CH0_EN | LVDS_CH1_EN;
	else 
		val |= LVDS_CH0_EN;

	if (screen->type == SCREEN_RGB)
		val |= LVDS_TTL_EN;

	if (h_bp & 0x01)
		val |= LVDS_START_PHASE_RST_1;

	val |= (screen->pin_dclk << 8) | (screen->pin_hsync << 9) |
		(screen->pin_den << 10);
	val |= 0xffff << 16;
	writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_SOC_CON7);

	
	if (screen->type == SCREEN_RGB)
		val = 0xbf;
	else
		val = 0x7f;
	lvds_writel(lvds, LVDS_CH0_REG_0, val);
	lvds_writel(lvds, LVDS_CH0_REG_1, 0x3f);
	lvds_writel(lvds, LVDS_CH0_REG_2, 0x7e);

	if (screen->type == SCREEN_RGB)
		val = 0x1f;
	else
		val = 0x00;
	lvds_writel(lvds, LVDS_CH0_REG_4, val);
	lvds_writel(lvds, LVDS_CH0_REG_5, val);
	lvds_writel(lvds, LVDS_CH0_REG_3, 0x46);
	lvds_writel(lvds, LVDS_CH0_REG_d, 0x05);
	lvds_writel(lvds, LVDS_CH0_REG_20,0x44);
	writel_relaxed(0x00, lvds->regs + LVDS_CFG_REG_c); /*eanble pll*/
	writel_relaxed(0x92, lvds->regs + LVDS_CFG_REG_21); /*enable tx*/

	return 0;
}



static struct rk_fb_trsm_ops trsm_lvds_ops = {
	.enable = rk32_lvds_en,
	.disable = rk32_lvds_disable,
};

static int rk32_lvds_probe(struct platform_device *pdev)
{
	struct rk32_lvds *lvds;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}

	lvds = devm_kzalloc(&pdev->dev, sizeof(struct rk32_lvds), GFP_KERNEL);
	if (!lvds) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}
	lvds->dev = &pdev->dev;
	rk_fb_get_prmry_screen(&lvds->screen);
	if ((lvds->screen.type != SCREEN_RGB) && 
		(lvds->screen.type != SCREEN_LVDS) &&
		(lvds->screen.type != SCREEN_DUAL_LVDS)) {
		dev_err(&pdev->dev, "screen is not lvds/rgb!\n");
		return -EINVAL;
	}
	platform_set_drvdata(pdev, lvds);
	dev_set_name(lvds->dev, "rk32-lvds");
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lvds->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lvds->regs)) {
		dev_err(&pdev->dev, "ioremap reg failed\n");
		return PTR_ERR(lvds->regs);
	}

	rk32_lvds = lvds;
	rk_fb_trsm_ops_register(&trsm_lvds_ops,SCREEN_LVDS);
	dev_info(&pdev->dev, "rk32 lvds driver probe success\n");

	return 0;
}

static void rk32_lvds_shutdown(struct platform_device *pdev)
{

}

#if defined(CONFIG_OF)
static const struct of_device_id rk32_lvds_dt_ids[] = {
	{.compatible = "rockchip, rk32-lvds",},
	{}
};

MODULE_DEVICE_TABLE(of, rk32_lvds_dt_ids);
#endif

static struct platform_driver rk32_lvds_driver = {
	.probe = rk32_lvds_probe,
	.driver = {
		   .name = "rk32-lvds",
		   .owner = THIS_MODULE,
#if defined(CONFIG_OF)
		   .of_match_table = of_match_ptr(rk32_lvds_dt_ids),
#endif
	},
	.shutdown = rk32_lvds_shutdown,
};

static int __init rk32_lvds_module_init(void)
{
	return platform_driver_register(&rk32_lvds_driver);
}

static void __exit rk32_lvds_module_exit(void)
{

}

fs_initcall(rk32_lvds_module_init);
module_exit(rk32_lvds_module_exit);

