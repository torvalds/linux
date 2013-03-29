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
#include <linux/mw100.h>
#include <mach/iomux.h>
#include<linux/ioctl.h>

#include <linux/slab.h>
   
MODULE_LICENSE("GPL");

#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif

#define   	MW100IO	0XA1
#define	MW_IOCTL_RESET	_IO(MW100IO,0X01)

#define SLEEP 1
#define READY 0
#define MW100_RESET 0x01
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
struct rk29_mw100_data *gpdata = NULL;
static int  bp_wakeup_ap_irq = 0;

static struct wake_lock bp_wakelock;
static bool bpstatus_irq_enable = false;

static void do_wakeup(struct work_struct *work)
{
    enable_irq(bp_wakeup_ap_irq);
}

static DECLARE_DELAYED_WORK(wakeup_work, do_wakeup);
static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
      wake_lock_timeout(&bp_wakelock, 10 * HZ);
   
    return IRQ_HANDLED;
}

static int mw100_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int mw100_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long mw100_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_mw100_data *pdata = gpdata;
	switch(cmd)
	{
		case MW_IOCTL_RESET:			
		gpio_direction_output(pdata->bp_reset,GPIO_LOW);
		mdelay(120);
		gpio_set_value(pdata->bp_reset, GPIO_HIGH);
		
			break;
		default:
			break;
	}
	return 0;
}

static struct file_operations mw100_fops = {
	.owner = THIS_MODULE,
	.open = mw100_open,
	.release = mw100_release,
	.unlocked_ioctl = mw100_ioctl
};

static struct miscdevice mw100_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mw100",
	.fops = &mw100_fops
};

static int mw100_probe(struct platform_device *pdev)
{
	struct rk29_mw100_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mw100_data = NULL;
	int result, irq = 0;	

	gpio_request(pdata->bp_power,"bp_power");
	gpio_request(pdata->bp_reset,"bp_reset");
	gpio_request(pdata->bp_wakeup_ap,"bp_wakeup_ap");
	gpio_request(pdata->ap_wakeup_bp,"ap_wakeup_bp");
	gpio_set_value(pdata->modem_power_en, GPIO_HIGH);
	msleep(1000);
	gpio_direction_output(pdata->bp_reset,GPIO_LOW);
	mdelay(120);
	gpio_set_value(pdata->bp_reset, GPIO_HIGH);
	
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_HIGH);
	gpio_direction_output(pdata->ap_wakeup_bp,GPIO_HIGH);	
	
	gpio_set_value(pdata->bp_power, GPIO_HIGH);
	gpio_direction_output(pdata->bp_power,GPIO_HIGH);	
	mdelay(120);
	gpio_set_value(pdata->bp_power, GPIO_LOW);
	gpio_direction_output(pdata->bp_power,GPIO_LOW);	
	
	mw100_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(mw100_data == NULL){
		printk("failed to request mw100_data\n");
		goto err2;
	}
	platform_set_drvdata(pdev, mw100_data);	
	
	gpio_direction_input(pdata->bp_wakeup_ap);
	irq	= gpio_to_irq(pdata->bp_wakeup_ap);
	if(irq < 0){
		gpio_free(pdata->bp_wakeup_ap);
		printk("failed to request bp_wakeup_ap\n");
	}
	
	bp_wakeup_ap_irq = irq;
	
	result = request_irq(irq, detect_irq_handler, IRQ_BB_WAKEUP_AP_TRIGGER, "bp_wakeup_ap", NULL);
	if (result < 0) {
		printk("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(pdata->bp_wakeup_ap);
		goto err0;
	}

	enable_irq_wake(bp_wakeup_ap_irq); 

	wake_lock_init(&bp_wakelock, WAKE_LOCK_SUSPEND, "bp_resume");

	result = misc_register(&mw100_misc);
	if(result){
		MODEMDBG("misc_register err\n");
	}	
	return result;
err0:
	gpio_free(pdata->bp_wakeup_ap);
err2:
	kfree(mw100_data);
	return 0;
}

int mw100_suspend(struct platform_device *pdev, pm_message_t state)
{
	
	struct rk29_mw100_data *pdata = pdev->dev.platform_data;
	int irq;
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
	irq = gpio_to_irq(pdata->bp_wakeup_ap);
	if (irq < 0) {
		printk("can't get pdata->bp_statue irq \n");
	}
	else
	{
		bpstatus_irq_enable = true;
		enable_irq_wake(irq);
	}
	return 0;
}

int mw100_resume(struct platform_device *pdev)
{
	struct rk29_mw100_data *pdata = pdev->dev.platform_data;
	int irq;
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_HIGH);	
		irq = gpio_to_irq(pdata->bp_wakeup_ap);
	if (irq ) {
		disable_irq_wake(irq);
		bpstatus_irq_enable = false;
	}
	return 0;
}

void mw100_shutdown(struct platform_device *pdev)
{
	struct rk29_mw100_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mw100_data = platform_get_drvdata(pdev);
	
	gpio_set_value(pdata->bp_power, GPIO_HIGH);
	mdelay(2010);
	gpio_free(pdata->modem_power_en);
	gpio_free(pdata->bp_power);
	gpio_free(pdata->bp_reset);
	gpio_free(pdata->ap_wakeup_bp);
	gpio_free(pdata->bp_wakeup_ap);
	kfree(mw100_data);
}

static struct platform_driver mw100_driver = {
	.probe	= mw100_probe,
	.shutdown	= mw100_shutdown,
	.suspend  	= mw100_suspend,
	.resume		= mw100_resume,
	.driver	= {
		.name	= "mw100",
		.owner	= THIS_MODULE,
	},
};

static int __init mw100_init(void)
{
	return platform_driver_register(&mw100_driver);
}

static void __exit mw100_exit(void)
{
	platform_driver_unregister(&mw100_driver);
}

module_init(mw100_init);

module_exit(mw100_exit);
