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
#include <linux/mt6229.h>
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
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_FALLING
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
#define MT6229_RESET 0x01
struct rk29_mt6229_data *gpdata = NULL;
struct class *modem_class = NULL; 
static int do_wakeup_irq = 0;
static int modem_status;
int suspend_int =0;
static void ap_wakeup_bp(struct platform_device *pdev, int wake)
{
	struct rk29_mt6229_data *pdata = pdev->dev.platform_data;

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
	struct rk29_mt6229_data *pdata = gpdata;		
  if(on_off)
  {
		MODEMDBG("------------modem_poweron\n");
		//gpio_set_value(pdata->bp_reset, GPIO_HIGH);
		//msleep(100);
		//gpio_set_value(pdata->bp_reset, GPIO_LOW);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(10);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
  }
  else
  {
		MODEMDBG("------------modem_poweroff\n");
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		gpio_set_value(pdata->ap_wakeup_bp, GPIO_HIGH);
  }
  return 0;
}
static int mt6229_open(struct inode *inode, struct file *file)
{
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	struct rk29_mt6229_data *pdata = gpdata;
//	struct platform_data *pdev = container_of(pdata, struct device, platform_data);
	device_init_wakeup(pdata->dev, 1);
	return 0;
}

static int mt6229_release(struct inode *inode, struct file *file)
{
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	return 0;
}

static long mt6229_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_mt6229_data *pdata = gpdata;
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	switch(cmd)
	{
		case MT6229_RESET:					
			//gpio_set_value(pdata->bp_reset, GPIO_HIGH);
			//msleep(100);
			//gpio_set_value(pdata->bp_reset, GPIO_LOW);
			//msleep(100);
			gpio_set_value(pdata->bp_power, GPIO_LOW);
			gpio_set_value(pdata->bp_power, GPIO_HIGH);
			msleep(10);
			gpio_set_value(pdata->bp_power, GPIO_LOW);
			gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
			break;
		default:
			break;
	}
	return 0;
}

static struct file_operations mt6229_fops = {
	.owner = THIS_MODULE,
	.open = mt6229_open,
	.release = mt6229_release,
	.unlocked_ioctl = mt6229_ioctl
};

static struct miscdevice mt6229_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODEM_NAME,
	.fops = &mt6229_fops
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
        printk("*********************mt6229____suspend\n");
		 
}
static void rk29_early_resume(struct early_suspend *h)
{
	 if(suspend_int)
	{
         printk("***************mt6229____resume\n");
        gpio_set_value(gpdata->ap_wakeup_bp, 0);
	 suspend_int = 0;
 	}
}

static struct early_suspend mt6229_early_suspend = {
	         .suspend = rk29_early_suspend,
	          .resume = rk29_early_resume,
	          .level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
	  };
static int mt6229_probe(struct platform_device *pdev)
{
	struct rk29_mt6229_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mt6229_data = NULL;
	int result, irq = 0;	
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	
	pdata->dev = &pdev->dev;
	if(pdata->io_init)
		pdata->io_init();
	gpio_set_value(pdata->modem_power_en, GPIO_HIGH);
	msleep(1000);
	modem_poweron_off(1);
	modem_status = 1;
	
	register_early_suspend(&mt6229_early_suspend);
	mt6229_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(mt6229_data == NULL)
	{
		printk("failed to request mt6229_data\n");
		goto err2;
	}
	platform_set_drvdata(pdev, mt6229_data);		
	result = gpio_request(pdata->ap_wakeup_bp, "mt6229");
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

	result = misc_register(&mt6229_misc);
	if(result)
	{
		printk("misc_register err\n");
	}	
	return result;
err0:
	cancel_work_sync(&mt6229_data->work);
	gpio_free(pdata->bp_wakeup_ap);
err1:
	gpio_free(pdata->ap_wakeup_bp);
err2:
	kfree(mt6229_data);
	return 0;
}

int mt6229_suspend(struct platform_device *pdev, pm_message_t state)
{
	suspend_int = 1;
	do_wakeup_irq = 1;
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	ap_wakeup_bp(pdev, 1);
#if defined(CONFIG_ARCH_RK29)
	rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_GPIO1C1);
	//gpio_direction_output(RK29_PIN1_PC1, 1);
#endif
#if defined(CONFIG_ARCH_RK30)
	rk30_mux_api_set(GPIO1A7_UART1RTSN_SPI0TXD_NAME, GPIO1A_GPIO1A7);
#endif	
	return 0;
}

int mt6229_resume(struct platform_device *pdev)
{
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	//ap_wakeup_bp(pdev, 0);
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

void mt6229_shutdown(struct platform_device *pdev)
{
	struct rk29_mt6229_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mt6229_data = platform_get_drvdata(pdev);
	
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	modem_poweron_off(0);

	if(pdata->io_deinit)
		pdata->io_deinit();
	cancel_work_sync(&mt6229_data->work);
	gpio_free(pdata->modem_power_en);
	gpio_free(pdata->bp_power);
	gpio_free(pdata->bp_reset);
	gpio_free(pdata->ap_wakeup_bp);
	gpio_free(pdata->bp_wakeup_ap);
	kfree(mt6229_data);
}

static struct platform_driver mt6229_driver = {
	.probe	= mt6229_probe,
	.shutdown	= mt6229_shutdown,
	.suspend  	= mt6229_suspend,
	.resume		= mt6229_resume,
	.driver	= {
		.name	= "mt6229",
		.owner	= THIS_MODULE,
	},
};

static int __init mt6229_init(void)
{
	//MODEMDBG("-------------%s\n",__FUNCTION__);
		int ret ;
	
	
	modem_class = class_create(THIS_MODULE, "rk291x_modem");
	ret =  class_create_file(modem_class, &class_attr_modem_status);
	if (ret)
	{
		printk("Fail to class rk291x_modem.\n");
	}
	return platform_driver_register(&mt6229_driver);
}

static void __exit mt6229_exit(void)
{
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	platform_driver_unregister(&mt6229_driver);
	class_remove_file(modem_class, &class_attr_modem_status);
}

module_init(mt6229_init);

module_exit(mt6229_exit);
