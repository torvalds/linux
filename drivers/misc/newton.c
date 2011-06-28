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
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <mach/board.h>




#if 1
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif






int rk29_newton_open(struct inode *inode, struct file *filp)
{
    DBG("%s\n",__FUNCTION__);

	return 0;
}

ssize_t rk29_newton_read(struct file *filp, char __user *ptr, size_t size, loff_t *pos)
{
    DBG("%s\n",__FUNCTION__);
	return sizeof(int);
}

int rk29_newton_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct rk29_newton_data *pdata ;//= pgps;
    DBG("%s\n",__FUNCTION__);
	return ret;
}


int rk29_newton_release(struct inode *inode, struct file *filp)
{
    DBG("%s\n",__FUNCTION__);
    
	return 0;
}


static struct file_operations rk29_newton_fops = {
	.owner   = THIS_MODULE,
	.open    = rk29_newton_open,
	.read    = rk29_newton_read,
	.ioctl   = rk29_newton_ioctl,
	.release = rk29_newton_release,
};


static struct miscdevice rk29_newton_dev = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "newton",
    .fops = &rk29_newton_fops,
};


static int rk29_newton_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rk29_newton_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;
	DBG("%s",__FUNCTION__);
	ret = misc_register(&rk29_newton_dev);
	if (ret < 0){
		printk("rk29 newton register err!\n");
		return ret;
	}
	#if 0
	init_MUTEX(&pdata->power_sem);
	pdata->wq = create_freezeable_workqueue("rk29_gps");
	INIT_WORK(&pdata->work, rk29_gps_delay_power_downup);
	pdata->power_flag = 0;

	//gps power down
	rk29_gps_uart_to_gpio(pdata->uart_id);
	if (pdata->power_down)
		pdata->power_down();
	if (pdata->reset)
		pdata->reset(GPIO_LOW);

	pgps = pdata;
#endif

	DBG("%s:rk29 newton initialized\n",__FUNCTION__);

	return ret;
}

static int rk29_newton_remove(struct platform_device *pdev)
{
	misc_deregister(&rk29_newton_dev);
	return 0;
}


int rk29_newton_suspend(struct platform_device *pdev,  pm_message_t state)
{
	return 0;	
}

int rk29_newton_resume(struct platform_device *pdev)
{
	return 0;
}


static struct platform_driver rk29_newton_driver = {
	.probe	    = rk29_newton_probe,
	.remove     = rk29_newton_remove,
	.suspend  	= rk29_newton_suspend,
	.resume		= rk29_newton_resume,
	.driver	    = {
		.name	= "rk29_newton",
		.owner	= THIS_MODULE,
	},
};

static int __init rk29_newton_init(void)
{
	return platform_driver_register(&rk29_newton_driver);
}

static void __exit rk29_newton_exit(void)
{
	platform_driver_unregister(&rk29_newton_driver);
}

module_init(rk29_newton_init);
module_exit(rk29_newton_exit);
MODULE_DESCRIPTION ("rk29 newton misc driver");
MODULE_LICENSE("GPL");

