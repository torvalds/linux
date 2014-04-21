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

//#define TTL_TO_LVDS 1
static struct rk32_lvds *rk32_lvds;
static int rk32_lvds_disable(void)
{
	struct rk32_lvds *lvds = rk32_lvds;
	writel_relaxed(0xffff8000, RK_GRF_VIRT + RK3288_GRF_SOC_CON7);
	writel_relaxed(0x00, lvds->regs + LVDS_CFG_REG_21); /*disable tx*/
	writel_relaxed(0xff, lvds->regs + LVDS_CFG_REG_c); /*disable pll*/
	clk_disable_unprepare(lvds->clk);
	clk_disable_unprepare(lvds->pd);
	return 0;
}

static int rk32_lvds_en(void)
{
	struct rk32_lvds *lvds = rk32_lvds;
	struct rk_screen *screen = &lvds->screen;
	u32 h_bp = screen->mode.hsync_len + screen->mode.left_margin;
	u32 i,j, val ;

	clk_prepare_enable(lvds->clk);
	clk_prepare_enable(lvds->pd);
	screen->type = SCREEN_RGB;
	
	screen->lcdc_id = 1;
	if (screen->lcdc_id == 1) /*lcdc1 = vop little,lcdc0 = vop big*/
		val = LVDS_SEL_VOP_LIT | (LVDS_SEL_VOP_LIT << 16);
	else
		val = LVDS_SEL_VOP_LIT << 16;
	writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);

	val = screen->lvds_format;
	if (screen->type == SCREEN_DUAL_LVDS)
		val |= LVDS_DUAL | LVDS_CH0_EN | LVDS_CH1_EN;
	else if(screen->type == SCREEN_LVDS)
		val |= LVDS_CH0_EN;

		//val |= LVDS_MSB;
	else if (screen->type == SCREEN_RGB)
		val |= LVDS_TTL_EN | LVDS_CH0_EN | LVDS_CH1_EN;

	if (h_bp & 0x01)
		val |= LVDS_START_PHASE_RST_1;

	val |= (screen->pin_dclk << 8) | (screen->pin_hsync << 9) |
		(screen->pin_den << 10);
	val |= 0xffff << 16;
	//val = 0x08010801;
	writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_SOC_CON7);

	if (screen->type == SCREEN_LVDS)
		val = 0xbf;
	else
		val = 0x7f;
#if 0
	for(i=0;i<0x200;){
		val  = readl_relaxed(lvds->regs + i);
		printk("0x%08x:0x%08x  ",i,val);
		i += 4;
		if(i % 16 == 0)
			printk("\n");
	}
#endif	
	#ifdef  TTL_TO_LVDS // 0 ttl  1 lvds
	val = 0x007f007f;//0x1<<6 |0x1 <<4;
	writel_relaxed(val, RK_GRF_VIRT + 0xc);

	
	lvds_writel(lvds, LVDS_CH0_REG_0, 0x7f);
	lvds_writel(lvds, LVDS_CH0_REG_1, 0x40);
	lvds_writel(lvds, LVDS_CH0_REG_2, 0x00);

	if (screen->type == SCREEN_RGB)
		val = 0x1f;
	else
		val = 0x00;
	lvds_writel(lvds, LVDS_CH0_REG_4, 0x3f);
	lvds_writel(lvds, LVDS_CH0_REG_5, 0x3f);
	lvds_writel(lvds, LVDS_CH0_REG_3, 0x46);
	lvds_writel(lvds, LVDS_CH0_REG_d, 0x0a);
	lvds_writel(lvds, LVDS_CH0_REG_20,0x44);/* 44:LSB  45:MSB*/
	writel_relaxed(0x00, lvds->regs + LVDS_CFG_REG_c); /*eanble pll*/
	writel_relaxed(0x92, lvds->regs + LVDS_CFG_REG_21); /*enable tx*/

	lvds_writel(lvds, 0x100, 0x7f);
	lvds_writel(lvds, 0x104, 0x40);
	lvds_writel(lvds, 0x108, 0x00);
	lvds_writel(lvds, 0x10c, 0x46);
	lvds_writel(lvds, 0x110, 0x3f);
	lvds_writel(lvds, 0x114, 0x3f);
	lvds_writel(lvds, 0x134, 0x0a);
	#else
	val  = readl_relaxed(lvds->regs + 0x88);
	printk("0x88:0x%x\n",val);

	lvds_writel(lvds, LVDS_CH0_REG_0, 0xbf);
	lvds_writel(lvds, LVDS_CH0_REG_1, 0x3f);//  3f
	lvds_writel(lvds, LVDS_CH0_REG_2, 0xfe);
	lvds_writel(lvds, LVDS_CH0_REG_3, 0x46);//0x46
	lvds_writel(lvds, LVDS_CH0_REG_4, 0x00);
	//lvds_writel(lvds, LVDS_CH0_REG_9, 0x20);
	//lvds_writel(lvds, LVDS_CH0_REG_d, 0x4b);
	//lvds_writel(lvds, LVDS_CH0_REG_f, 0x0d);
	lvds_writel(lvds, LVDS_CH0_REG_d, 0x0a);//0a
	lvds_writel(lvds, LVDS_CH0_REG_20,0x44);/* 44:LSB  45:MSB*/
	//lvds_writel(lvds, 0x24,0x20);
	//writel_relaxed(0x23, lvds->regs + 0x88);
	writel_relaxed(0x00, lvds->regs + LVDS_CFG_REG_c); /*eanble pll*/
	writel_relaxed(0x92, lvds->regs + LVDS_CFG_REG_21); /*enable tx*/


	//lvds_writel(lvds, 0x100, 0xbf);
	//lvds_writel(lvds, 0x104, 0x3f);
	//lvds_writel(lvds, 0x108, 0xfe);
	//lvds_writel(lvds, 0x10c, 0x46); //0x46
	//lvds_writel(lvds, 0x110, 0x00);
	//lvds_writel(lvds, 0x114, 0x00);
	//lvds_writel(lvds, 0x134, 0x0a);

	#endif
#if 0
	for(i=0;i<100;i++){
		mdelay(1000);
		mdelay(1000);
		mdelay(1000);
		mdelay(1000);
		mdelay(1000);
		printk("write LVDS_CH0_REG_20 :0x40\n");
		//writel_relaxed(0x10, lvds->regs + LVDS_CFG_REG_c);
		lvds_writel(lvds, LVDS_CH0_REG_20,0x40);/* 44:LSB  45:MSB*/
		val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_20);
		printk("read back LVDS_CH0_REG_20:0x%x\n",val);
		mdelay(1000);
		mdelay(1000);
		mdelay(1000);
		mdelay(1000);
		mdelay(1000);
		printk("write LVDS_CH0_REG_20 :0x44\n");
		lvds_writel(lvds, LVDS_CH0_REG_20,0x44);/* 44:LSB  45:MSB*/
		val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_20);
		printk("read back LVDS_CH0_REG_20:0x%x\n",val);
	}
#endif	
	//while(1)
#if 0
	{
	val  = readl_relaxed(RK_GRF_VIRT + RK3288_GRF_SOC_CON6);
	printk("RK3288_GRF_SOC_CON6:0x%x\n",val);
	val  = readl_relaxed(RK_GRF_VIRT + RK3288_GRF_SOC_CON7);
	printk("RK3288_GRF_SOC_CON7:0x%x\n",val);
	val  = readl_relaxed(RK_GRF_VIRT + RK3288_GRF_SOC_CON15);
	printk("RK3288_GRF_SOC_CON15:0x%x\n",val);
	

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_0);
	printk("LVDS_CH0_REG_0:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_1);
	printk("LVDS_CH0_REG_1:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_2);
	printk("LVDS_CH0_REG_2:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_3);
	printk("LVDS_CH0_REG_3:0x%x\n",val);
	
	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_4);
	printk("LVDS_CH0_REG_4:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_5);
	printk("LVDS_CH0_REG_5:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_d);
	printk("LVDS_CH0_REG_d:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CH0_REG_f);
		printk("LVDS_CH0_REG_f:0x%x\n",val);

	
	val  = readl_relaxed(lvds->regs + LVDS_CFG_REG_c);
	printk("LVDS_CFG_REG_c:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + LVDS_CFG_REG_21);
	printk("LVDS_CFG_REG_21:0x%x\n",val);
	val  = readl_relaxed(lvds->regs + 0x100);
	printk("0x100:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + 0x104);
	printk("0x104:0x%x\n",val);
		val  = readl_relaxed(lvds->regs + 0x108);
	printk("0x108:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + 0x10c);
	printk("0x10c:0x%x\n",val);
	
	val  = readl_relaxed(lvds->regs + 0x110);
	printk("0x110:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + 0x114);
	printk("0x114:0x%x\n",val);
		val  = readl_relaxed(lvds->regs + 0x118);
	printk("0x118:0x%x\n",val);

	val  = readl_relaxed(lvds->regs + 0x11c);
	printk("0x11c:0x%x\n",val);	
	mdelay(1000);
		}

	for(i=0;i<0x200;){
		val  = readl_relaxed(lvds->regs + i);
		printk("0x%08x:0x%08x  ",i,val);
		i += 4;
		if(i % 16 == 0)
			printk("\n");
	}
#endif
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
		writel_relaxed(0xffff8000, RK_GRF_VIRT + RK3288_GRF_SOC_CON7);
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
	lvds->clk = devm_clk_get(&pdev->dev,"pclk_lvds");
	if (IS_ERR(lvds->clk)) {
		dev_err(&pdev->dev, "get clk failed\n");
		return PTR_ERR(lvds->clk);
	}
	lvds->pd = devm_clk_get(&pdev->dev,"pd_lvds");
	if (IS_ERR(lvds->pd)) {
		dev_err(&pdev->dev, "get clk failed\n");
		return PTR_ERR(lvds->pd);
	}	
	if (support_uboot_display()) {
		clk_prepare_enable(lvds->clk);
		clk_prepare_enable(lvds->pd);
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
	{.compatible = "rockchip,rk32-lvds",},
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

