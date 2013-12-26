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
#include <linux/earlysuspend.h>

#include "rk319x_lvds.h"

#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v,offset) 	do{ writel_relaxed(v, RK30_GRF_BASE + offset);dsb();} while (0)

#define lvds_readl(addr)	readl_relaxed(addr)
#define lvds_writel(v,addr) 	do{ writel_relaxed(v, addr);dsb();} while (0)

static	struct early_suspend early_suspend;
static char *virt_reg_addr;

void rk_dump_clock_info(void);
void  rk30_clk_dump_regs(void);


static void rk319x_output_lvds(rk_screen *screen)
{
	u32 val =0;

	/*reset lvds*/
	//lvds_writel(0x0,virt_reg_addr+MIPIPHY_REGE0);
	//msleep(500);

	val = 0;
	val |= LVDS_MODE(1); /*enable lvds mode*/
	val |= LVDS_SEL_LCDC(screen->lcdc_id); /* configure lvds source*/
	val |= LVDS_OUTPUT_FORMAT(screen->lvds_format); /*configure lvds_format*/
	val |= LVDS_MSBSEL(1);//LSB mode	
	grf_writel(val,GRF_SOC_CON4);	


	lvds_writel(0x7c,virt_reg_addr+MIPIPHY_REG0);//enable lvds lane
	lvds_writel(0x2,virt_reg_addr+MIPIPHY_REG3);//set lvds pll prediv fbdiv[8]
	lvds_writel(0x1c,virt_reg_addr+MIPIPHY_REG4);//set lvds pll fbdiv[7:0]
	lvds_writel(0x25,virt_reg_addr+MIPIPHY_REGE0);//lvds mode
	lvds_writel(0xa0,virt_reg_addr+MIPIPHY_REGE2);//timing
	lvds_writel(0xfc,virt_reg_addr+MIPIPHY_REGE7);//phase
	lvds_writel(0xf8,virt_reg_addr+MIPIPHY_REGEA);//power up lvds_pllpd

	/* enable lvds*/
	val = 0;
	val |= LVDS_ENABLE(1);
	grf_writel(val,GRF_SOC_CON8);
	
	return;
}

static void rk319x_output_lvttl(rk_screen *screen)
{
	return;			
}

static void rk319x_output_disable(void)
{	
	u32 val =0;

	val = 0;
	val |= LVDS_MODE(0); 
	grf_writel(val,GRF_SOC_CON4);

	val = 0;	
	val |= LVDS_ENABLE(0);
	grf_writel(val,GRF_SOC_CON8);	

	return;
}


static int rk319x_lvds_set_param(rk_screen *screen,bool enable)
{

	if(OUT_ENABLE == enable){
		switch(screen->type){
			case SCREEN_LVDS:
			        printk("%s>>>>LVDS Enable %d,power down LVDS\n",__func__,screen->type);
				rk319x_output_lvds(screen);                                       
				break;
			case SCREEN_RGB:
				rk319x_output_lvttl(screen);
				break;
			default:
			        printk("%s>>>>LVDS not support this screen type %d,power down LVDS\n",__func__,screen->type);
				rk319x_output_disable();
				break;
		}
	}else{
			        printk("%s>>>>LVDS  %d,power down LVDS\n",__func__,screen->type);
		rk319x_output_disable();
	}
	return 0;
}

static void lvds_early_suspend(struct early_suspend *handler)
{
	rk_screen *screen = NULL;
	screen = rk_fb_get_prmry_screen();
	if(!screen)
	{
		printk(KERN_ERR,"the fb prmry screen is null!\n");
		return -ENODEV;
	}
	
	rk319x_lvds_set_param(screen,OUT_DISABLE);

        return;

}
static void lvds_late_resume(struct early_suspend *handler)
{
	rk_screen *screen = NULL;
	screen = rk_fb_get_prmry_screen();
	if(!screen)
	{
		printk(KERN_ERR,"the fb prmry screen is null!\n");
		return -ENODEV;
	}
	
	rk319x_lvds_set_param(screen,OUT_ENABLE);

return;
}

static int init_clk()
{
	int ret = 0;
	struct clk *refclk,*dsi_pclk,*dsi_pd,*mipiphy_dsi;

	refclk = clk_get(NULL, "mipi_ref");
        if (unlikely(IS_ERR(refclk))) {
                ret = PTR_ERR(refclk);
		printk("%s %d \n",__func__,__LINE__);
		return -1;
        }

	dsi_pclk = clk_get(NULL, "pclk_mipi_dsi");
        if (unlikely(IS_ERR(dsi_pclk))) {
                ret = PTR_ERR(dsi_pclk);
		printk("%s %d \n",__func__,__LINE__);
		return -1;
        }

	mipiphy_dsi = clk_get(NULL, "pclk_mipiphy_dsi");
	if (unlikely(IS_ERR(mipiphy_dsi))) {
                ret = PTR_ERR(mipiphy_dsi);
		printk("%s %d \n",__func__,__LINE__);
		return -1;
        }
	
        dsi_pd = clk_get(NULL, "pd_mipi_dsi");
        if (unlikely(IS_ERR(dsi_pd))) {
                ret = PTR_ERR(dsi_pd);
		printk("%s %d \n",__func__,__LINE__);
		return -1;
        }

        clk_enable(dsi_pd);
        clk_enable(dsi_pclk);
	clk_enable(mipiphy_dsi);
	clk_enable(refclk);

	return 0;	
}


static int rk319x_lvds_probe(struct platform_device *pdev)
{
	int ret = 0;
	rk_screen *screen = NULL;
	
	screen = rk_fb_get_prmry_screen();
	if(!screen)
	{
		dev_err(&pdev->dev,"the fb prmry screen is null!\n");
		return -ENODEV;
	}

	ret = init_clk();
	if(ret < 0)
		return -1;

	virt_reg_addr = ioremap(RK319X_MIPI_DSI_PHY_PHYS,RK319X_MIPI_DSI_PHY_SIZE);
	if(virt_reg_addr == NULL)
		return -1;	


	rk319x_lvds_set_param(screen,OUT_ENABLE);

	early_suspend.suspend = lvds_early_suspend,
	early_suspend.resume = lvds_late_resume,
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 20,
	register_early_suspend(&early_suspend);

	return 0;
	
}

static int rk319x_lvds_remove(struct platform_device *pdev)
{	
	return 0;
}

static void rk319x_lvds_shutdown(struct platform_device *pdev)
{
	return;
}

static struct platform_driver rk319x_lvds_driver = {
	.driver		= {
		.name	= "rk319x-lvds",
		.owner	= THIS_MODULE,
	},
	.probe		= rk319x_lvds_probe,
	.remove		= rk319x_lvds_remove,
	.shutdown	= rk319x_lvds_shutdown,
};

static int __init rk319x_lvds_init(void)
{
	return platform_driver_register(&rk319x_lvds_driver);
}
fs_initcall(rk319x_lvds_init);
static void __exit rk319x_lvds_exit(void)
{
	platform_driver_unregister(&rk319x_lvds_driver);
}
module_exit(rk319x_lvds_exit);



