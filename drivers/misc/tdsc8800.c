#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/slab.h>

#include <linux/workqueue.h>
#include <linux/mtk23d.h>


MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif

#define SLEEP 1
#define READY 0
#define RESET 1
struct rk2818_23d_data *gpdata = NULL;


int modem_poweron_off(int on_off)
{
	struct rk2818_23d_data *pdata = gpdata;
	
	if(on_off)
	{
		printk("tdsc8800_poweron\n");
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);  // power on enable

	}
	else
	{
		printk("tdsc8800_poweroff\n");
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
	}
	return 0;
}
static int tdsc8800_open(struct inode *inode, struct file *file)
{
	modem_poweron_off(1);
	device_init_wakeup(gpdata->dev, 1);

	return 0;
}

static int tdsc8800_release(struct inode *inode, struct file *file)
{
	MODEMDBG("tdsc8800_release\n");
	modem_poweron_off(0);

	return 0;
}
static long  tdsc8800_ioctl(struct file *file, unsigned int a, unsigned long b)
{
	switch(a){
		case RESET:
			modem_poweron_off(0);
			msleep(1000);
			modem_poweron_off(1);
			break;
		default:
			MODEMDBG("cmd error !!!\n");
			break;
	}
	return 0;
}

static struct file_operations tdsc8800_fops = {
	.owner = THIS_MODULE,
	.open =tdsc8800_open,
	.release =tdsc8800_release,
	.unlocked_ioctl = tdsc8800_ioctl
};

static struct miscdevice tdsc8800_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tdsc8800",
	.fops = &tdsc8800_fops
};

static int tdsc8800_probe(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *tdsc8800_data = NULL;
	int result = 0;	
	
	MODEMDBG("tdsc8800_probe\n");

	//pdata->io_init();

	pdata->dev = &pdev->dev;
	tdsc8800_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(NULL == tdsc8800_data)
	{
		printk("failed to request tdsc8800_data\n");
		goto err6;
	}
	platform_set_drvdata(pdev, tdsc8800_data);

	result = gpio_request(pdata->bp_power, "tdsc8800");
	if (result) {
		printk("failed to request BP_POW_EN gpio\n");
		goto err1;
	}
	
	
        gpio_direction_output(pdata->bp_power, GPIO_LOW);

	gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
	result = misc_register(&tdsc8800_misc);
	if(result)
	{
		MODEMDBG("misc_register err\n");
	}
	MODEMDBG("mtk23d_probe ok\n");
	
	return result;
err1:
	gpio_free(pdata->bp_power);
err6:
	kfree(tdsc8800_data);
	return result;
}

int tdsc8800_suspend(struct platform_device *pdev)
{
	return 0;
}

int tdsc8800_resume(struct platform_device *pdev)
{
	return 0;
}

void tdsc8800_shutdown(struct platform_device *pdev, pm_message_t state)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mt6223d_data = platform_get_drvdata(pdev);
	
	MODEMDBG("%s \n", __FUNCTION__);

	modem_poweron_off(0);  // power down
	gpio_free(pdata->bp_power);
	kfree(mt6223d_data);
}

static struct platform_driver tdsc8800_driver = {
	.probe	= tdsc8800_probe,
	.shutdown	= tdsc8800_shutdown,
	.suspend  	= tdsc8800_suspend,
	.resume		= tdsc8800_resume,
	.driver	= {
		.name	= "tdsc8800",
		.owner	= THIS_MODULE,
	},
};

static int __init tdsc8800_init(void)
{
	int ret = platform_driver_register(&tdsc8800_driver);
	MODEMDBG("tdsc8800_init ret=%d\n",ret);
	return ret;
}

static void __exit tdsc8800_exit(void)
{
	MODEMDBG("tdsc8800_exit\n");
	platform_driver_unregister(&tdsc8800_driver);
}

module_init(tdsc8800_init);
module_exit(tdsc8800_exit);
