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
#include <linux/sew868.h>
#include<linux/ioctl.h>
#include<linux/slab.h>

MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif
#define SLEEP 1
#define READY 0
#define SEW868_RESET 0x01
#define SEW868_POWON 0x02
#define SEW868_POWOFF 0x03
static struct wake_lock modem_wakelock;
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_FALLING
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
struct rk30_sew868_data *gpdata = NULL;
static int do_wakeup_irq = 0;

extern void rk28_send_wakeup_key(void);

static void do_wakeup(struct work_struct *work)
{
    rk28_send_wakeup_key();
}

static DECLARE_DELAYED_WORK(wakeup_work, do_wakeup);
static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
    printk("%s\n", __FUNCTION__);
    if(do_wakeup_irq)
    {
        do_wakeup_irq = 0;
        wake_lock_timeout(&modem_wakelock, 10 * HZ);
        schedule_delayed_work(&wakeup_work, HZ / 10);
    }
    return IRQ_HANDLED;
}
int modem_poweron_off(int on_off)
{
	struct rk30_sew868_data *pdata = gpdata;		
  if(on_off)
  {
		gpio_direction_output(pdata->bp_sys, GPIO_HIGH);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		msleep(200);//for charge
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(4000);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		msleep(200);
  }
  else
  {
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(4000);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		gpio_set_value(pdata->bp_sys, GPIO_LOW);
		msleep(50);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
  }
  return 0;
}
static int sew868_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int sew868_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long sew868_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk30_sew868_data *pdata = gpdata;
	switch(cmd)
	{
		case SEW868_RESET:					
			gpio_set_value(pdata->bp_reset, GPIO_HIGH);
			mdelay(100);
			gpio_set_value(pdata->bp_reset, GPIO_LOW);
			mdelay(200);
			modem_poweron_off(1);
			break;
		case SEW868_POWON:
			modem_poweron_off(1);
			break;
		case SEW868_POWOFF:
			modem_poweron_off(0);
			break;
		default:
			break;
	}
	return 0;
}

static struct file_operations sew868_fops = {
	.owner = THIS_MODULE,
	.open = sew868_open,
	.release = sew868_release,
	.unlocked_ioctl = sew868_ioctl
};

static struct miscdevice sew868_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODEM_NAME,
	.fops = &sew868_fops
};

static int sew868_probe(struct platform_device *pdev)
{
	struct rk30_sew868_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *sew868_data = NULL;
	int result, irq = 0;	
	
	if(pdata->io_init)
		pdata->io_init();

	modem_poweron_off(1);
	sew868_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(sew868_data == NULL)
	{
		printk("failed to request sew868_data\n");
		goto err1;
	}
	platform_set_drvdata(pdev, sew868_data);
	
	irq = gpio_to_irq(pdata->bp_wakeup_ap);
	if(irq < 0)
	{
		gpio_free(pdata->bp_wakeup_ap);
		printk("failed to request bp_wakeup_ap\n");
	}

	wake_lock_init(&modem_wakelock, WAKE_LOCK_SUSPEND, "bp_wakeup_ap");
	gpio_direction_input(pdata->bp_wakeup_ap);
    	gpio_pull_updown(pdata->bp_wakeup_ap, GPIONormal);	
	result = request_irq(irq, detect_irq_handler, IRQ_BB_WAKEUP_AP_TRIGGER, "bp_wakeup_ap", NULL);
	if (result < 0) {
		printk("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(pdata->bp_wakeup_ap);
		goto err0;
	}
	enable_irq_wake(gpio_to_irq(pdata->bp_wakeup_ap)); 
	result = misc_register(&sew868_misc);
	if(result)
	{
		MODEMDBG("misc_register err\n");
	}	

	return result;
err0:
	cancel_work_sync(&sew868_data->work);
err1:
	kfree(sew868_data);
	return 0;
}

int sew868_suspend(struct platform_device *pdev, pm_message_t state)
{
	do_wakeup_irq = 1;
	return 0;
}

int sew868_resume(struct platform_device *pdev)
{
	return 0;
}

void sew868_shutdown(struct platform_device *pdev)
{
	struct rk30_sew868_data *pdata = pdev->dev.platform_data;
	struct modem_dev *sew868_data = platform_get_drvdata(pdev);
	modem_poweron_off(0);
	if(pdata->io_deinit)
		pdata->io_deinit();
	cancel_work_sync(&sew868_data->work);
	kfree(sew868_data);
}

static struct platform_driver sew868_driver = {
	.probe	= sew868_probe,
	.shutdown	= sew868_shutdown,
	.suspend  	= sew868_suspend,
	.resume		= sew868_resume,
	.driver	= {
		.name	= "sew868",
		.owner	= THIS_MODULE,
	},
};

static int __init sew868_init(void)
{
	return platform_driver_register(&sew868_driver);
}

static void __exit sew868_exit(void)
{
	platform_driver_unregister(&sew868_driver);
}

module_init(sew868_init);

module_exit(sew868_exit);
