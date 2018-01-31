/* SPDX-License-Identifier: GPL-2.0 */
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

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#include "rk3026_lvds.h"

#define lvds_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define lvds_writel(v,offset) 	do{ writel_relaxed(v, RK30_GRF_BASE + offset);dsb();} while (0)

static void rk3026_output_lvds(rk_screen *screen)
{
	u32 val =0;

	#if defined(CONFIG_LCDC0_RK3188)	
		val |= LVDS_DATA_SEL(0);
	#else
		val |= LVDS_DATA_SEL(1);
	#endif
	
	if(screen->lvds_format == 0 || screen->lvds_format == 1)
		val |= LVDS_CBS_COL_SEL(2);  //24bit lvds
	else
		val |= LVDS_CBS_COL_SEL(1);  //16bit lvds

	val |= ((LVDS_OUTPUT_FORMAT(screen->lvds_format))|LVDS_INPUT_FORMAT(1)|LVDS_OUTPUT_LOAD_SEL(0)|
		LVDS_CBG_PWD_EN(1)|LVDS_PLL_PWD_EN(0)|LVDS_OUTPUT_EN(0)|LVDS_SWING_SEL(0));

	val |= ((m_DATA_SEL|m_CBS_COL_SEL|m_OUTPUT_FORMAT|m_INPUT_FORMAT|m_OUTPUT_LOAD_SEL|
		m_CBG_PWD_EN|m_PLL_PWD_EN|m_OUTPUT_EN|m_SWING_SEL)<<16);

	lvds_writel(val,CRU_LVDS_CON0);
	
	return;
}

static void rk3026_output_lvttl(rk_screen *screen)
{

	u32 val =0;

	val |= (LVDS_CBG_PWD_EN(0)|LVDS_PLL_PWD_EN(1)|LVDS_OUTPUT_EN(1));
	val |= ((m_CBG_PWD_EN|m_PLL_PWD_EN|m_OUTPUT_EN)<<16);
	
	lvds_writel(val,CRU_LVDS_CON0);

	return;			
}

static void rk3026_output_disable(void)
{	

	u32 val =0;

	val |= (LVDS_CBG_PWD_EN(1)|LVDS_PLL_PWD_EN(0)|LVDS_OUTPUT_EN(0)|LVDS_CBS_COL_SEL(0));
	val |= ((m_CBG_PWD_EN|m_PLL_PWD_EN|m_OUTPUT_EN|m_CBS_COL_SEL)<<16);

	lvds_writel(val,CRU_LVDS_CON0);
	
}


static int rk3026_lvds_set_param(rk_screen *screen,bool enable)
{

	if(OUT_ENABLE == enable){
		switch(screen->type){
			case SCREEN_LVDS:
				rk3026_output_lvds(screen);                                       
				break;
			case SCREEN_RGB:
				rk3026_output_lvttl(screen);
				break;
			default:
			        printk("%s>>>>LVDS not support this screen type %d,power down LVDS\n",__func__,screen->type);
				rk3026_output_disable();
				break;
		}
	}else{
		rk3026_output_disable();
	}
	return 0;
}


static int rk3026_lvds_probe(struct platform_device *pdev)
{
	rk_screen *screen = NULL;
	screen = rk_fb_get_prmry_screen();
	if(!screen)
	{
		dev_err(&pdev->dev,"the fb prmry screen is null!\n");
		return -ENODEV;
	}
	
	rk3026_lvds_set_param(screen,OUT_ENABLE);

	return 0;
	
}

static int rk3026_lvds_remove(struct platform_device *pdev)
{	
	return 0;
}

static void rk3026_lvds_shutdown(struct platform_device *pdev)
{
	return;
}

static struct platform_driver rk3026_lvds_driver = {
	.driver		= {
		.name	= "rk3026-lvds",
		.owner	= THIS_MODULE,
	},
	.probe		= rk3026_lvds_probe,
	.remove		= rk3026_lvds_remove,
	.shutdown	= rk3026_lvds_shutdown,
};

static int __init rk3026_lvds_init(void)
{
	return platform_driver_register(&rk3026_lvds_driver);
}
fs_initcall(rk3026_lvds_init);
static void __exit rk3026_lvds_exit(void)
{
	platform_driver_unregister(&rk3026_lvds_driver);
}
module_exit(rk3026_lvds_exit);



