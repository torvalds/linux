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
#define PC_DISPLAY_MODE 2
#define ANDROID_DISPLAY_MODE   1

#define USB_SWITCH_IOCTL_BASE 'u'
#define USB_SWITCH_IOCTL_SET_USB_SWITCH_MODE		_IOW(USB_SWITCH_IOCTL_BASE, 0x01, int)
#define USB_SWITCH_IOCTL_GET_PC_STATE		_IOR(USB_SWITCH_IOCTL_BASE, 0x02, int)

static struct ac_usb_switch_platform_data *ac_usb_switch;


static int setUsbSwitchMode(int mode){
	struct ac_usb_switch_platform_data *pdata = ac_usb_switch;
	if(pdata->usb_switch_pin == INVALID_GPIO){
		DBG(" error ac_usb_switch_pin is null !!!\n");
		return -1;
	}
	switch(mode){
		case PC_DISPLAY_MODE:
			gpio_set_value(pdata->usb_switch_pin,GPIO_LOW);			
			break;
		case ANDROID_DISPLAY_MODE:
			gpio_set_value(pdata->usb_switch_pin,GPIO_HIGH);
			break;
		default:
		break;
	}
	return 0;
}
static int getPCstate(){
	return 1;
}

static int ac_usb_switch_open(struct inode *inode, struct file *filp)
{
    DBG("ac_usb_switch_open\n");

	return 0;
}

static ssize_t ac_usb_switch_read(struct file *filp, char __user *ptr, size_t size, loff_t *pos)
{
	if (ptr == NULL)
		printk("%s: user space address is NULL\n", __func__);
	return sizeof(int);
}

static long ac_usb_switch_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;	
	int mode;
	struct ac_usb_switch_platform_data *pdata = ac_usb_switch;

	DBG("ac_usb_switch_ioctl: cmd = %d arg = %ld\n",cmd, arg);
	
	switch (cmd){
		case USB_SWITCH_IOCTL_SET_USB_SWITCH_MODE:
			DBG("ac_usb_switch_ioctl: USB_SWITCH_IOCTL_SET_USB_SWITCH_MODE\n");
			mode = arg;
			setUsbSwitchMode(mode);
			break;
		case USB_SWITCH_IOCTL_GET_PC_STATE:
			DBG("ac_usb_switch_ioctl: USB_SWITCH_IOCTL_GET_PC_STATE\n");
			return getPCstate();
			
		default:
			printk("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}
	return ret;
}

static int ac_usb_switch_release(struct inode *inode, struct file *filp)
{
    DBG("ac_usb_switch_release\n");
    
	return 0;
}

static struct file_operations ac_usb_switch_fops = {
	.owner   = THIS_MODULE,
	.open    = ac_usb_switch_open,
	.read    = ac_usb_switch_read,
	.unlocked_ioctl   = ac_usb_switch_ioctl,
	.release = ac_usb_switch_release,
};

static struct miscdevice ac_usb_switch_dev = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ac_usb_switch",
    .fops = &ac_usb_switch_fops,
};

static int ac_usb_switch_probe(struct platform_device *pdev)
{
	int ret = 0;
	int result;
	struct ac_usb_switch_platform_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;
	ac_usb_switch = pdata;
	ret = misc_register(&ac_usb_switch_dev);
	if (ret < 0){
		printk("ac_usb_switch register err!\n");
		return ret;
	}
	
	if(pdata->usb_switch_pin != INVALID_GPIO){
		result = gpio_request(pdata->usb_switch_pin, "ac_usb_switch");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, pdata->usb_switch_pin);
			//return -1;
		}else{
			gpio_direction_output(pdata->usb_switch_pin,GPIO_HIGH);
		}
	}

	if(pdata->pc_state_pin != INVALID_GPIO){
		result = gpio_request(pdata->pc_state_pin, "ac_usb_switch");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, pdata->pc_state_pin);
			//return -1;
		}else{
			gpio_direction_input(pdata->pc_state_pin);
		}
	}
	printk("%s: ok ...... \n", __func__);

	return ret;
}

static int ac_usb_switch_suspend(struct platform_device *pdev,  pm_message_t state)
{
	struct ac_usb_switch_platform_data *pdata = pdev->dev.platform_data;

	if(!pdata) {
		printk("%s: pdata = NULL ...... \n", __func__);
		return -1;
	}
	printk("%s\n",__FUNCTION__);
	return 0;	
}

static int ac_usb_switch_resume(struct platform_device *pdev)
{
	struct ac_usb_switch_platform_data *pdata = pdev->dev.platform_data;

	if(!pdata) {
		printk("%s: pdata = NULL ...... \n", __func__);
		return -1;
	}
	printk("%s\n",__FUNCTION__);
	return 0;
}

static int ac_usb_switch_remove(struct platform_device *pdev)
{
	struct ac_usb_switch_platform_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;

	misc_deregister(&ac_usb_switch_dev);

	return 0;
}

static struct platform_driver ac_usb_switch_driver = {
	.probe	= ac_usb_switch_probe,
	.remove = ac_usb_switch_remove,
	.suspend  	= ac_usb_switch_suspend,
	.resume		= ac_usb_switch_resume,
	.driver	= {
		.name	= "ac_usb_switch",
		.owner	= THIS_MODULE,
	},
};

static int __init ac_usb_switch_init(void)
{
	return platform_driver_register(&ac_usb_switch_driver);
}

static void __exit ac_usb_switch_exit(void)
{
	platform_driver_unregister(&ac_usb_switch_driver);
}

module_init(ac_usb_switch_init);
module_exit(ac_usb_switch_exit);
MODULE_DESCRIPTION ("ac usb switch driver");
MODULE_LICENSE("GPL");

