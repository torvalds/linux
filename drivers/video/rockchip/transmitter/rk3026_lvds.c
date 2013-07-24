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
	
	/*
		Choise LVDS transmitter source from LCDC or EBC
	*/
#if defined(CONFIG_LCDC0_RK3188)	
	val |= LVDS_DATA_SEL;	
#else
	val &= ~(LVDS_DATA_SEL);
#endif
	/* 
		When LVDS output format   is  8bit mode format-1/8bit mode format-2,  configure 
		grf_lvds_con0 in 24-bit color mode.
		When LVDS output format is 8bit mode format-3/6bit mode, configure 
	 	grf_lvds_con0 in 18-bit color mode. 
	*/
	if(screen->lvds_format == 0 || screen->lvds_format == 1)
		val |= LVDS_CBS_COL_SEL(2);
	else
		val |= LVDS_CBS_COL_SEL(1);
	
		 
	val &= ~(LVDS_CBG_PWR_EN | LVDS_OUTPUT_EN | LVDS_SWING_SEL);
	val |= (LVDS_OUTPUT_FORMAT(screen->lvds_format) | LVDS_PLL_PWR_EN | LVDS_SWING_SEL);

	val |= (LVDS_DATA_SEL | LVDS_OUTPUT_FORMAT(3) | LVDS_CBG_PWR_EN | LVDS_PLL_PWR_EN |
			LVDS_OUTPUT_EN | LVDS_CBS_COL_SEL(3) | LVDS_SWING_SEL) << 16;	

	lvds_writel(val,CRU_LVDS_CON0);
	
	return;
}

static void rk3026_output_lvttl(rk_screen *screen)
{
	u32 val =0;

	/*
		Choise LVDS transmitter source from LCDC or EBC
	*/
#if defined(CONFIG_LCDC0_RK3188)	
	val |= LVDS_DATA_SEL;	
#else
	val &= ~(LVDS_DATA_SEL);
#endif
	/* 
		When LVDS output format   is  8bit mode format-1/8bit mode format-2,  configure 
		grf_lvds_con0 in 24-bit color mode.
		When LVDS output format is 8bit mode format-3/6bit mode, configure 
	 	grf_lvds_con0 in 18-bit color mode. 
	*/
	if(screen->lvds_format == 0 || screen->lvds_format == 1)
		val |= LVDS_CBS_COL_SEL(2);
	else
		val |= LVDS_CBS_COL_SEL(1);
		
	val &= ~(LVDS_DATA_SEL | LVDS_CBG_PWR_EN | LVDS_SWING_SEL);
	val |= (LVDS_OUTPUT_FORMAT(screen->lvds_format) | LVDS_PLL_PWR_EN | LVDS_OUTPUT_EN | LVDS_CBS_COL_SEL(3));

	val |= ((LVDS_DATA_SEL | LVDS_CBG_PWR_EN | LVDS_SWING_SEL | LVDS_OUTPUT_FORMAT(screen->lvds_format) | 
		LVDS_PLL_PWR_EN | LVDS_OUTPUT_EN | LVDS_CBS_COL_SEL(3)) << 16);

	lvds_writel(val,CRU_LVDS_CON0);

	return;			
}

static void rk3026_output_disable(void)
{	
	u32 val =0;
	
	val |= ((LVDS_CBG_PWR_EN | LVDS_PLL_PWR_EN | LVDS_CBS_COL_SEL(3)) << 16);
	val &= ~(LVDS_CBG_PWR_EN);
	val |= (LVDS_PLL_PWR_EN | LVDS_CBS_COL_SEL(3)); 		
	
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



