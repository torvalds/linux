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
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <mach/iomux.h>
#include<linux/ioctl.h>

#include <linux/slab.h>
   
MODULE_LICENSE("GPL");

struct rk29_audio_switch_data {
	struct device *dev;
	unsigned int gpio_switch_fm_ap;
	unsigned int gpio_switch_bb_ap;
	int state;
};

struct rk29_audio_switch_data *gpdata = NULL;

#define AUDIO_SWTICH_IO	0XA2
#define	AS_IOCTL_AP	_IO(AUDIO_SWTICH_IO,0X01)
#define	AS_IOCTL_BP	_IO(AUDIO_SWTICH_IO,0X02)
#define	AS_IOCTL_FM	_IO(AUDIO_SWTICH_IO,0X03)


static int as_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int as_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long as_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    
	switch(cmd)
	{
		case AS_IOCTL_AP:	
		    gpio_set_value(gpdata->gpio_switch_fm_ap, GPIO_LOW);
		    break;
		case AS_IOCTL_BP:
		    break;
		case AS_IOCTL_FM:
		    gpio_set_value(gpdata->gpio_switch_fm_ap, GPIO_HIGH);
		    break;
		default:
			break;
	}
	return 0;
}

static struct file_operations as_fops = {
	.owner = THIS_MODULE,
	.open = as_open,
	.release = as_release,
	.unlocked_ioctl = as_ioctl
};

static struct miscdevice as_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "audio_switch",
	.fops = &as_fops
};

static int as_probe(struct platform_device *pdev)
{
    int result=0;
    gpdata = kzalloc(sizeof(struct rk29_audio_switch_data), GFP_KERNEL);

    rk30_mux_api_set(GPIO1B0_SPI_CLK_UART1_CTSN_NAME, GPIO1B_GPIO1B0);
    gpdata->gpio_switch_fm_ap = gpio_request(RK2928_PIN1_PB0,"switch_bb_ap");
	gpio_direction_output(gpdata->gpio_switch_fm_ap,GPIO_LOW);	
	
	platform_set_drvdata(pdev, gpdata);	

	result = misc_register(&as_misc);
	if(result){
		gpio_free(gpdata->gpio_switch_fm_ap);
		kfree(gpdata);
	}	
	return result;
}

int as_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

int as_resume(struct platform_device *pdev)
{
	return 0;
}

void as_shutdown(struct platform_device *pdev)
{
    gpio_free(gpdata->gpio_switch_fm_ap);
    kfree(gpdata);
}

static struct platform_driver as_driver = {
	.probe	= as_probe,
	.shutdown	= as_shutdown,
	.suspend  	= as_suspend,
	.resume		= as_resume,
	.driver	= {
		.name	= "audio_switch",
		.owner	= THIS_MODULE,
	},
};

static int __init audio_switch_init(void)
{
	return platform_driver_register(&as_driver);
}

static void __exit audio_switch_exit(void)
{
	platform_driver_unregister(&as_driver);
}

module_init(audio_switch_init);

module_exit(audio_switch_exit);
