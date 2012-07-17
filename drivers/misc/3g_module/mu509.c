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
#include <linux/mu509.h>
#include <linux/slab.h>
#include <linux/earlysuspend.h>

MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif
#define SLEEP 1
#define READY 0
static struct wake_lock modem_wakelock;
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_FALLING
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
#define MU509_RESET 0x01
struct rk29_mu509_data *gpdata = NULL;
struct class *modem_class = NULL; 
static int do_wakeup_irq = 0;
static int modem_status;
int suspend_int =0;
static void ap_wakeup_bp(struct platform_device *pdev, int wake)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;

	gpio_set_value(pdata->ap_wakeup_bp, wake);  

}
extern void rk28_send_wakeup_key(void);

static void do_wakeup(struct work_struct *work)
{
      if(suspend_int)
         {
             gpio_set_value(gpdata->ap_wakeup_bp, 0);
             suspend_int = 0;
         }

}

static DECLARE_DELAYED_WORK(wakeup_work, do_wakeup);
static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
    if(do_wakeup_irq)
    {
        do_wakeup_irq = 0;
  //      MODEMDBG("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
        wake_lock_timeout(&modem_wakelock, 10 * HZ);
        schedule_delayed_work(&wakeup_work, 2*HZ);
    }
    return IRQ_HANDLED;
}
int modem_poweron_off(int on_off)
{
	struct rk29_mu509_data *pdata = gpdata;		
  if(on_off)
  {
		gpio_set_value(pdata->bp_reset, GPIO_HIGH);
		msleep(100);
		gpio_set_value(pdata->bp_reset, GPIO_LOW);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		msleep(1000);
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(700);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
  }
  else
  {
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(2500);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
  }
  return 0;
}
static int mu509_open(struct inode *inode, struct file *file)
{
	struct rk29_mu509_data *pdata = gpdata;
	device_init_wakeup(pdata->dev, 1);
	return 0;
}

static int mu509_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long mu509_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_mu509_data *pdata = gpdata;
	switch(cmd)
	{
		case MU509_RESET:					
			gpio_set_value(pdata->bp_reset, GPIO_HIGH);
			msleep(100);
			gpio_set_value(pdata->bp_reset, GPIO_LOW);
			msleep(100);
			gpio_set_value(pdata->bp_power, GPIO_LOW);
			msleep(1000);
			gpio_set_value(pdata->bp_power, GPIO_HIGH);
			msleep(700);
			gpio_set_value(pdata->bp_power, GPIO_LOW);
			gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
			break;
		default:
			break;
	}
	return 0;
}

static struct file_operations mu509_fops = {
	.owner = THIS_MODULE,
	.open = mu509_open,
	.release = mu509_release,
	.unlocked_ioctl = mu509_ioctl
};

static struct miscdevice mu509_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODEM_NAME,
	.fops = &mu509_fops
};
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t modem_status_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t modem_status_read(struct class *cls, char *_buf)
#endif
{

	return sprintf(_buf, "%d\n", modem_status);
	
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t modem_status_write(struct class *cls, struct class_attribute *attr, const char *_buf, size_t _count)
#else
static ssize_t modem_status_write(struct class *cls, const char *_buf, size_t _count)
#endif
{
    int new_state = simple_strtoul(_buf, NULL, 16);
   if(new_state == modem_status) return _count;
   if (new_state == 1){
     printk("%s, c(%d), open modem \n", __FUNCTION__, new_state);
	 modem_poweron_off(1);
   }else if(new_state == 0){
     printk("%s, c(%d), close modem \n", __FUNCTION__, new_state);
	 modem_poweron_off(0);
   }else{
     printk("%s, invalid parameter \n", __FUNCTION__);
   }
	modem_status = new_state;
    return _count; 
}
static CLASS_ATTR(modem_status, 0777, modem_status_read, modem_status_write);
static void rk29_early_suspend(struct early_suspend *h)
{
		 
}
static void rk29_early_resume(struct early_suspend *h)
{
	 if(suspend_int)
	{
        gpio_set_value(gpdata->ap_wakeup_bp, 0);
	 suspend_int = 0;
 	}
}

static struct early_suspend mu509_early_suspend = {
	         .suspend = rk29_early_suspend,
	          .resume = rk29_early_resume,
	          .level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
	  };
static int mu509_probe(struct platform_device *pdev)
{
	struct rk29_mu509_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mu509_data = NULL;
	int result, irq = 0;	
	
	pdata->dev = &pdev->dev;
	if(pdata->io_init)
		pdata->io_init();
	gpio_set_value(pdata->modem_power_en, GPIO_HIGH);
	msleep(1000);
	modem_poweron_off(1);
	modem_status = 1;
	
	register_early_suspend(&mu509_early_suspend);
	mu509_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(mu509_data == NULL)
	{
		printk("failed to request mu509_data\n");
		goto err2;
	}
	platform_set_drvdata(pdev, mu509_data);		
	result = gpio_request(pdata->ap_wakeup_bp, "mu509");
	if (result) {
		printk("failed to request AP_BP_WAKEUP gpio\n");
		goto err1;
	}	
	irq	= gpio_to_irq(pdata->bp_wakeup_ap);
	enable_irq_wake(irq);
	if(irq < 0)
	{
		gpio_free(pdata->bp_wakeup_ap);
		printk("failed to request bp_wakeup_ap\n");
	}
	result = gpio_request(pdata->bp_wakeup_ap, "bp_wakeup_ap");
	if (result < 0) {
		printk("%s: gpio_request(%d) failed\n", __func__, pdata->bp_wakeup_ap);
	}
	wake_lock_init(&modem_wakelock, WAKE_LOCK_SUSPEND, "bp_wakeup_ap");
	gpio_direction_input(pdata->bp_wakeup_ap);
    gpio_pull_updown(pdata->bp_wakeup_ap, 1);	
	result = request_irq(irq, detect_irq_handler, IRQ_BB_WAKEUP_AP_TRIGGER, "bp_wakeup_ap", NULL);
	if (result < 0) {
		printk("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(pdata->bp_wakeup_ap);
		goto err0;
	}
	enable_irq_wake(gpio_to_irq(pdata->bp_wakeup_ap)); 

	result = misc_register(&mu509_misc);
	if(result)
	{
		printk("misc_register err\n");
	}	
	return result;
err0:
	cancel_work_sync(&mu509_data->work);
	gpio_free(pdata->bp_wakeup_ap);
err1:
	gpio_free(pdata->ap_wakeup_bp);
err2:
	kfree(mu509_data);
	return 0;
}

int mu509_suspend(struct platform_device *pdev, pm_message_t state)
{
	suspend_int = 1;
	do_wakeup_irq = 1;
	ap_wakeup_bp(pdev, 1);
#if defined(CONFIG_ARCH_RK29)
	rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_GPIO1C1);
#endif
#if defined(CONFIG_ARCH_RK30)
	rk30_mux_api_set(GPIO1A7_UART1RTSN_SPI0TXD_NAME, GPIO1A_GPIO1A7);
#endif	
	return 0;
}

int mu509_resume(struct platform_device *pdev)
{
#if defined(CONFIG_ARCH_RK29)
	rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_UART0_RTS_N);
#endif
#if defined(CONFIG_ARCH_RK30)
	rk30_mux_api_set(GPIO1A7_UART1RTSN_SPI0TXD_NAME, GPIO1A_UART1_RTS_N);
#endif
	if(gpio_get_value(gpdata->bp_wakeup_ap))
	{
		schedule_delayed_work(&wakeup_work, 2*HZ);
	}
	return 0;
}

void mu509_shutdown(struct platform_device *pdev)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mu509_data = platform_get_drvdata(pdev);
	
	modem_poweron_off(0);

	if(pdata->io_deinit)
		pdata->io_deinit();
	cancel_work_sync(&mu509_data->work);
	gpio_free(pdata->modem_power_en);
	gpio_free(pdata->bp_power);
	gpio_free(pdata->bp_reset);
	gpio_free(pdata->ap_wakeup_bp);
	gpio_free(pdata->bp_wakeup_ap);
	kfree(mu509_data);
}

static struct platform_driver mu509_driver = {
	.probe	= mu509_probe,
	.shutdown	= mu509_shutdown,
	.suspend  	= mu509_suspend,
	.resume		= mu509_resume,
	.driver	= {
		.name	= "mu509",
		.owner	= THIS_MODULE,
	},
};

static int __init mu509_init(void)
{
	int ret ;
	modem_class = class_create(THIS_MODULE, "rk291x_modem");
	ret =  class_create_file(modem_class, &class_attr_modem_status);
	if (ret)
	{
		printk("Fail to class rk291x_modem.\n");
	}
	return platform_driver_register(&mu509_driver);
}

static void __exit mu509_exit(void)
{
	platform_driver_unregister(&mu509_driver);
	class_remove_file(modem_class, &class_attr_modem_status);
}

module_init(mu509_init);

module_exit(mu509_exit);
