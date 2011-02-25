#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/types.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>
#include "rk29_gps.h"
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#define ENABLE  1
#define DISABLE 0

static struct rk29_gps_data *pgps;
static struct early_suspend gps_early_suspend;

static void rk29_gps_early_suspend(struct early_suspend *h)
{
	struct rk29_gps_data *pdata = pgps;
	if(!pdata)
	return;
		
	if(pdata->uart_id == 3)
	{
		rk29_mux_api_set(GPIO2B3_UART3SOUT_NAME, GPIO2L_GPIO2B3); 			
		gpio_request(RK29_PIN2_PB3, NULL);
		gpio_direction_output(RK29_PIN2_PB3,GPIO_LOW);

		rk29_mux_api_set(GPIO2B2_UART3SIN_NAME, GPIO2L_GPIO2B2); 		
		gpio_request(RK29_PIN2_PB2, NULL);
		gpio_direction_output(RK29_PIN2_PB2,GPIO_LOW);

	}
	else
	{
		//to do

	}

	if(pdata->powerflag == 1)
	{
		pdata->power_down();	
		pdata->powerflag = 0;
	}
	
	printk("%s\n",__FUNCTION__);
	
}

static void rk29_gps_early_resume(struct early_suspend *h)
{
	struct rk29_gps_data *pdata = pgps;
	if(!pdata)
	return;
	
	if(pdata->uart_id == 3)
	{
		 rk29_mux_api_set(GPIO2B3_UART3SOUT_NAME, GPIO2L_UART3_SOUT);
		 rk29_mux_api_set(GPIO2B2_UART3SIN_NAME, GPIO2L_UART3_SIN); 
	}
	else
	{
		//to do

	}

	if(pdata->powerflag == 0)
	{
		  pdata->power_up();
		  pdata->powerflag = 1;
	}
	
	printk("%s\n",__FUNCTION__);
	
}
int rk29_gps_open(struct inode *inode, struct file *filp)
{
    	DBG("rk29_gps_open\n");

	return 0;
}

int rk29_gps_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    	int ret = 0;
    
    	DBG("rk29_gps_ioctl: cmd = %d\n",cmd);

    	switch (cmd){
		case ENABLE:
			pgps->power_up();
			pgps->powerflag = 1;
			break;
	        
		case DISABLE:
			pgps->power_down();
			pgps->powerflag = 0;
			break;
	        
		default:
			printk("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	return ret;
}


int rk29_gps_release(struct inode *inode, struct file *filp)
{
    	DBG("rk29_gps_release\n");
    
	return 0;
}

static struct file_operations rk29_gps_fops = {
	.owner   = THIS_MODULE,
	.open    = rk29_gps_open,
	.ioctl   = rk29_gps_ioctl,
	.release = rk29_gps_release,
};

static struct miscdevice rk29_gps_dev = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "rk29_gps",
    .fops = &rk29_gps_fops,
};

static int rk29_gps_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rk29_gps_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;
		
	ret = misc_register(&rk29_gps_dev);
	if (ret < 0){
		printk("rk29 gps register err!\n");
		return ret;
	}
	
	if(pdata->power_up)
	pdata->power_up();
	pgps = pdata;

#ifdef CONFIG_HAS_EARLYSUSPEND
	gps_early_suspend.suspend = rk29_gps_early_suspend;
	gps_early_suspend.resume = rk29_gps_early_resume;
	gps_early_suspend.level = ~0x0;
	register_early_suspend(&gps_early_suspend);
#endif

	printk("%s:rk29 GPS initialized\n",__FUNCTION__);

	return ret;
}

static struct platform_driver rk29_gps_driver = {
	.probe	= rk29_gps_probe,
	.driver	= {
		.name	= "rk29_gps",
		.owner	= THIS_MODULE,
	},
};

static int __init rk29_gps_init(void)
{
	return platform_driver_register(&rk29_gps_driver);
}

static void __exit rk29_gps_exit(void)
{
	platform_driver_unregister(&rk29_gps_driver);
}

module_init(rk29_gps_init);
module_exit(rk29_gps_exit);
MODULE_DESCRIPTION ("rk29 gps driver");
MODULE_LICENSE("GPL");

