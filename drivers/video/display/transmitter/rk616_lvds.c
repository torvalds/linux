#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rk616.h>
#include <linux/rk_fb.h>

struct mfd_rk616 *g_rk616;


/*rk616 video interface config*/
static int rk616_vif_cfg(struct mfd_rk616 *rk616,rk_screen *screen,int id)
{
	int ret = 0;
	u32 val = 0;
	int offset = 0;
	if(id == 0) //video interface 0
	{
		offset = 0;
	}
	else       //vide0 interface 1
	{
		offset = 0x18;
	}
	
	val &= ((~VIF0_DDR_CLK_EN) & (~VIF0_DDR_PHASEN_EN) & (~VIF0_DDR_MODE_EN))|
		(VIF0_EN);
	val |= (VIF0_DDR_CLK_EN <<16) | (VIF0_DDR_PHASEN_EN << 16) | (VIF0_DDR_MODE_EN << 16)|
		(VIF0_EN <<16);
	
	ret = rk616->write_dev(rk616,VIF0_REG0 + offset,&val);	

	val = (screen->hsync_len + screen->left_margin) | ((screen->vsync_len + screen->upper_margin)<<16);
	ret = rk616->write_dev(rk616,VIF0_REG1 + offset,&val);

	val = (screen->hsync_len << 16) | (screen->hsync_len + screen->left_margin + 
		screen->right_margin + screen->x_res);
	ret = rk616->write_dev(rk616,VIF0_REG2 + offset,&val);

	
	val = ((screen->hsync_len + screen->left_margin + screen->x_res)<<16) |
		(screen->hsync_len + screen->left_margin);
	ret = rk616->write_dev(rk616,VIF0_REG3 + offset,&val);

	val = (screen->vsync_len << 16) | (screen->vsync_len + screen->upper_margin + 
		screen->lower_margin + screen->y_res);
	ret = rk616->write_dev(rk616,VIF0_REG4 + offset,&val);


	val = ((screen->vsync_len + screen->upper_margin + screen->y_res)<<16) |
		(screen->vsync_len + screen->upper_margin);
	ret = rk616->write_dev(rk616,VIF0_REG5 + offset,&val);
	
	return ret;
	
}

static int rk616_vif_bypass(struct mfd_rk616 *rk616,int id)
{
	int ret;
	u32 val = 0;

	val &= (~VIF0_EN);
	val |= (VIF0_EN << 16);
	if(id == 0)
	{
		ret = rk616->write_dev(rk616,VIF0_REG0,&val);
	}
	else
	{
		ret = rk616->write_dev(rk616,VIF1_REG0,&val);
	}

	return ret;
	
}
static int rk616_scaler_bypass(struct mfd_rk616 *rk616)
{
	u32 val = 0;
	int ret;

	val &= (~SCL_EN);	//disable scaler
	val |= (SCL_EN<<16);
	ret = rk616->write_dev(rk616,SCL_REG0,&val);
	
	return 0;
	
}

int rk610_lcd_scaler_set_param(rk_screen *screen,bool enable )//enable:0 bypass 1: scale
{
	int ret;
	struct mfd_rk616 *rk616 = g_rk616;
	if(!rk616)
	{
		printk(KERN_ERR "%s:mfd rk616 is null!\n",__func__);
		return -1;
	}
	rk616_vif_bypass(rk616,0);
	rk616_vif_bypass(rk616,1);
	rk616_scaler_bypass(rk616);
	
	return ret;
}

static int rk616_lvds_probe(struct platform_device *pdev)
{

	struct mfd_rk616 *rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616)
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}
	else
		g_rk616 = rk616;

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
subsys_initcall_sync(rk616_lvds_init);

static void __exit rk616_lvds_exit(void)
{
	platform_driver_unregister(&rk616_lvds_driver);
}
module_exit(rk616_lvds_exit);

