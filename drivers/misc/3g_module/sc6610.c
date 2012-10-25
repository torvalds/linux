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
#include <asm/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/sc6610.h>

#include <linux/slab.h>
#include <linux/earlysuspend.h>

MODULE_LICENSE("GPL");

#define DEBUG 1
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif
#define MODEM_RESET 2	
#define MODEM_ON 1
#define MODEM_OFF 0
struct rk29_sc6610_data *s_gpdata = NULL;

struct class *modem_class = NULL; 
static int do_wakeup_irq = 0;
static struct wake_lock modem_wakelock;
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_FALLING
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
int modem_poweron_off(int on_off)
{
	struct rk29_sc6610_data *pdata = s_gpdata;		
	if(on_off){	
		gpio_set_value(pdata->bp_power, GPIO_HIGH);		
		
	}else{
		gpio_set_value(pdata->bp_power, GPIO_LOW);		
	}
	return 0;
}

static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
    if(do_wakeup_irq)
    {
        do_wakeup_irq = 0;
	
        wake_lock_timeout(&modem_wakelock, 10 * HZ);
    }
    return IRQ_HANDLED;
}
static int sc6610_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int sc6610_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long sc6610_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_sc6610_data *pdata = s_gpdata;
	switch(cmd)
	{
		case MODEM_RESET:					
			gpio_set_value(pdata->bp_reset, GPIO_LOW);
			msleep(2000);
			gpio_set_value(pdata->bp_reset, GPIO_HIGH);			
			break;
		case MODEM_ON:	
			modem_poweron_off(MODEM_ON);
			break;
		case MODEM_OFF:	
			modem_poweron_off(MODEM_OFF);
			break;
		default:
			break;
	}
	return 0;
}

static struct file_operations sc6610_fops = {
	.owner = THIS_MODULE,
	.open = sc6610_open,
	.release = sc6610_release,
	.unlocked_ioctl = sc6610_ioctl
};

static struct miscdevice sc6610_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODEM_NAME,
	.fops = &sc6610_fops
};

static int sc6610_probe(struct platform_device *pdev)
{
	struct rk29_sc6610_data *pdata = s_gpdata = pdev->dev.platform_data;
	struct modem_dev *sc6610_data = NULL;
	int result;	
	int irq;
	pdata->dev = &pdev->dev;

	if(pdata->io_init)
		pdata->io_init();	
	result = gpio_request(pdata->ap_wakeup_bp, "ap_wakeup_bp");
	if (result) {
		printk("failed to request ap_wakeup_bp gpio\n");
		goto err0;
	}
	result = gpio_request(pdata->bp_power, "bp_power");
	if (result) {
		printk("failed to request bp_power gpio\n");
		goto err1;
	}	
	result = gpio_request(pdata->bp_wakeup_ap, "bp_wakeup_ap");
	if (result) {
		printk("failed to request bp_wakeup_ap gpio\n");
		goto err2;
	}
		
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_HIGH);
	
	irq = gpio_to_irq(pdata->bp_wakeup_ap);
	
	result = request_irq(irq, detect_irq_handler, IRQ_BB_WAKEUP_AP_TRIGGER, "bp_wakeup_ap", NULL);
	if (result < 0) {
		printk("%s: request_irq(%d) failed\n", __func__, irq);
		goto err2;
	}
	enable_irq_wake(irq); 
	wake_lock_init(&modem_wakelock, WAKE_LOCK_SUSPEND, "bp_wakeup_ap");
	sc6610_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(sc6610_data == NULL)
	{
		printk("failed to request sc6610_data\n");
		goto err3;
	}
	platform_set_drvdata(pdev, sc6610_data);		
	result = misc_register(&sc6610_misc);
	if(result)
	{
		printk("misc_register err\n");
	}
	modem_poweron_off(MODEM_ON);
	return result;

err0:
	gpio_free(pdata->ap_wakeup_bp);
err1:
	gpio_free(pdata->bp_power);
err2:
	gpio_free(pdata->bp_wakeup_ap);
err3:
	kfree(sc6610_data);
	return 0;
}

int c6610_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk29_sc6610_data *pdata = s_gpdata = pdev->dev.platform_data;
	pdata->dev = &pdev->dev;
	do_wakeup_irq = 1;
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
	return 0;
}

int c6610_resume(struct platform_device *pdev)
{
	struct rk29_sc6610_data *pdata = s_gpdata = pdev->dev.platform_data;
	pdata->dev = &pdev->dev;
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_HIGH);
	return 0;
}

void c6610_shutdown(struct platform_device *pdev)
{
	struct rk29_sc6610_data *pdata = pdev->dev.platform_data;
	struct modem_dev *sc6610_data = platform_get_drvdata(pdev);
	modem_poweron_off(MODEM_OFF);
	if(pdata->io_deinit)
		pdata->io_deinit();
	cancel_work_sync(&sc6610_data->work);	
	gpio_free(pdata->bp_power);
	gpio_free(pdata->ap_wakeup_bp);
	gpio_free(pdata->bp_wakeup_ap);
	kfree(sc6610_data);
}

static struct platform_driver sc6610_driver = {
	.probe	= sc6610_probe,
	.shutdown	= c6610_shutdown,
	.suspend  	= c6610_suspend,
	.resume		= c6610_resume,
	.driver	= {
		.name	= "SC6610",
		.owner	= THIS_MODULE,
	},
};

static int __init sc6610_init(void)
{
	
	return platform_driver_register(&sc6610_driver);
}

static void __exit sc6610_exit(void)
{
	platform_driver_unregister(&sc6610_driver);
	
}

module_init(sc6610_init);

module_exit(sc6610_exit);
