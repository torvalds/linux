#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "rk616_lvds.h"


struct rk616_lvds *g_lvds;


static int rk616_lvds_cfg(struct mfd_rk616 *rk616,rk_screen *screen)
{
	struct rk616_route *route = &rk616->route;
	u32 val = 0;
	int ret;
	int odd = (screen->left_margin&0x01)?0:1;
	
	if(!route->lvds_en)  //lvds port is not used ,power down lvds
	{
		val &= ~(LVDS_CH1TTL_EN | LVDS_CH0TTL_EN | LVDS_CH1_PWR_EN |
			LVDS_CH0_PWR_EN | LVDS_CBG_PWR_EN);
		val |= LVDS_PLL_PWR_DN | (LVDS_CH1TTL_EN << 16) | (LVDS_CH0TTL_EN << 16) |
			(LVDS_CH1_PWR_EN << 16) | (LVDS_CH0_PWR_EN << 16) |
			(LVDS_CBG_PWR_EN << 16) | (LVDS_PLL_PWR_DN << 16);
		ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

		if(!route->lcd1_input)  //set lcd1 port for output as RGB interface
		{
			val = (LCD1_INPUT_EN << 16);
			ret = rk616->write_dev(rk616,CRU_IO_CON0,&val);
		}
	}
	else
	{
		if(route->lvds_mode)  //lvds mode
		{

			if(route->lvds_ch_nr == 2) //dual lvds channel
			{
				val = 0;
				val &= ~(LVDS_CH0TTL_EN | LVDS_CH1TTL_EN | LVDS_PLL_PWR_DN);
				val = (LVDS_DCLK_INV)|(LVDS_CH1_PWR_EN) |(LVDS_CH0_PWR_EN) | LVDS_HBP_ODD(odd) |
					(LVDS_CBG_PWR_EN) | (LVDS_CH_SEL) | (LVDS_OUT_FORMAT(screen->lvds_format)) | 
					(LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH1_PWR_EN << 16) | 
					(LVDS_CH0_PWR_EN << 16) | (LVDS_CBG_PWR_EN << 16) | (LVDS_CH_SEL << 16) | 
					(LVDS_OUT_FORMAT_MASK) | (LVDS_DCLK_INV << 16) | (LVDS_PLL_PWR_DN << 16) |
					(LVDS_HBP_ODD_MASK);
				ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);
				
				rk616_dbg(rk616->dev,"rk616 use dual lvds channel.......\n");
			}
			else //single lvds channel
			{
				val = 0;
				val &= ~(LVDS_CH0TTL_EN | LVDS_CH1TTL_EN | LVDS_CH1_PWR_EN | LVDS_PLL_PWR_DN | LVDS_CH_SEL); //use channel 0
				val |= (LVDS_CH0_PWR_EN) |(LVDS_CBG_PWR_EN) | (LVDS_OUT_FORMAT(screen->lvds_format)) | 
				      (LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH0_PWR_EN << 16) | 
				       (LVDS_DCLK_INV ) | (LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH0_PWR_EN << 16) | 
				        (LVDS_CBG_PWR_EN << 16)|(LVDS_CH_SEL << 16) | (LVDS_PLL_PWR_DN << 16)| 
				       (LVDS_OUT_FORMAT_MASK) | (LVDS_DCLK_INV << 16);
				ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

				rk616_dbg(rk616->dev,"rk616 use single lvds channel.......\n");
				
			}

		}
		else //mux lvds port to RGB mode
		{
			val &= ~(LVDS_CBG_PWR_EN| LVDS_CH1_PWR_EN | LVDS_CH0_PWR_EN);
			val |= (LVDS_CH0TTL_EN)|(LVDS_CH1TTL_EN )|(LVDS_PLL_PWR_DN)|
				(LVDS_CH0TTL_EN<< 16)|(LVDS_CH1TTL_EN<< 16)|(LVDS_CH1_PWR_EN << 16) | 
				(LVDS_CH0_PWR_EN << 16)|(LVDS_CBG_PWR_EN << 16)|(LVDS_PLL_PWR_DN << 16);
			ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

			val &= ~(LVDS_OUT_EN);
			val |= (LVDS_OUT_EN << 16);
			ret = rk616->write_dev(rk616,CRU_IO_CON0,&val);
			rk616_dbg(rk616->dev,"rk616 use RGB output.....\n");
			
		}
	}

	return 0;
	
}


int rk616_scaler_set_param(rk_screen *screen,bool enable )//enable:0 bypass 1: scale
{
	int ret;
	struct mfd_rk616 *rk616 = g_lvds->rk616;
	if(!rk616)
	{
		printk(KERN_ERR "%s:mfd rk616 is null!\n",__func__);
		return -1;
	}
	ret = rk616_display_router_cfg(rk616,screen,enable);
	ret = rk616_lvds_cfg(rk616,screen);
	return ret;
}


static int rk616_lvds_init_cfg(struct mfd_rk616 *rk616,rk_screen *screen)
{
	int ret ;
	ret = rk616_display_router_cfg(rk616,screen,0);
	ret = rk616_lvds_cfg(rk616,screen);
	return ret;
}

#if	defined(CONFIG_HAS_EARLYSUSPEND)
static void rk616_lvds_early_suspend(struct early_suspend *h)
{
	struct rk616_lvds *lvds = container_of(h, struct rk616_lvds,early_suspend);
	struct mfd_rk616 *rk616 = lvds->rk616;
	u32 val = 0;
	int ret = 0;

	val &= ~(LVDS_CH1_PWR_EN | LVDS_CH0_PWR_EN | LVDS_CBG_PWR_EN);
	val |= LVDS_PLL_PWR_DN |(LVDS_CH1_PWR_EN << 16) | (LVDS_CH0_PWR_EN << 16) |
		(LVDS_CBG_PWR_EN << 16) | (LVDS_PLL_PWR_DN << 16);
	ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

	val = LCD1_INPUT_EN | (LCD1_INPUT_EN << 16);
	ret = rk616->write_dev(rk616,CRU_IO_CON0,&val);
	
	
}

static void rk616_lvds_late_resume(struct early_suspend *h)
{
	struct rk616_lvds *lvds = container_of(h, struct rk616_lvds,early_suspend);
	struct mfd_rk616 *rk616 = lvds->rk616;
	rk616_lvds_init_cfg(rk616,lvds->screen);
}

#endif

static int rk616_lvds_probe(struct platform_device *pdev)
{
	struct rk616_lvds *lvds = NULL; 
	struct mfd_rk616 *rk616 = NULL;
	rk_screen *screen = NULL;
	lvds = kzalloc(sizeof(struct rk616_lvds),GFP_KERNEL);
	if(!lvds)
	{
		printk(KERN_ALERT "alloc for struct rk616_lvds fail\n");
		return  -ENOMEM;
	}

	rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616)
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}
	else
		g_lvds = lvds;
	lvds->rk616 = rk616;
	
	screen = rk_fb_get_prmry_screen();
	if(!screen)
	{
		dev_err(&pdev->dev,"the fb prmry screen is null!\n");
		return -ENODEV;
	}
	lvds->screen = screen;
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)
	screen->sscreen_set = rk616_scaler_set_param;
#endif
 	rk616_lvds_init_cfg(rk616,screen);
#ifdef CONFIG_HAS_EARLYSUSPEND
	lvds->early_suspend.suspend = rk616_lvds_early_suspend;
	lvds->early_suspend.resume = rk616_lvds_late_resume;
    	lvds->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&lvds->early_suspend);
#endif
	

	dev_info(&pdev->dev,"rk616 lvds probe success!\n");

	return 0;
	
}

static int rk616_lvds_remove(struct platform_device *pdev)
{
	
	return 0;
}

static void rk616_lvds_shutdown(struct platform_device *pdev)
{
	
	return;
}

static struct platform_driver rk616_lvds_driver = {
	.driver		= {
		.name	= "rk616-lvds",
		.owner	= THIS_MODULE,
	},
	.probe		= rk616_lvds_probe,
	.remove		= rk616_lvds_remove,
	.shutdown	= rk616_lvds_shutdown,
};

static int __init rk616_lvds_init(void)
{
	return platform_driver_register(&rk616_lvds_driver);
}
fs_initcall(rk616_lvds_init);
static void __exit rk616_lvds_exit(void)
{
	platform_driver_unregister(&rk616_lvds_driver);
}
module_exit(rk616_lvds_exit);

