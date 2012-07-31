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
#include <linux/mi700.h>
#include <mach/iomux.h>
#include<linux/ioctl.h>
#include <linux/slab.h>
   
MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif

#define   	MW100IO	0XA1
#define	MW_IOCTL_RESET	_IO(MW100IO,0X01)

#define SLEEP 1
#define READY 0
#define MI700_RESET 0x01
static struct wake_lock modem_wakelock;
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
struct rk29_mi700_data *gpdata = NULL;
static int  bp_wakeup_ap_irq = 0;
struct class *modem_class = NULL; 
static int do_wakeup_irq = 1;
static int modem_status;
static int online = 0;

static void ap_wakeup_bp(struct platform_device *pdev, int wake)
{
	struct rk29_mi700_data *pdata = pdev->dev.platform_data;
	MODEMDBG("ap_wakeup_bp\n");

	gpio_set_value(pdata->ap_wakeup_bp, wake);  

}
extern void rk28_send_wakeup_key(void);

static void do_wakeup(struct work_struct *work)
{
    MODEMDBG("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    //rk28_send_wakeup_key();
}

static DECLARE_DELAYED_WORK(wakeup_work, do_wakeup);
static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
    if(do_wakeup_irq)
    {
        do_wakeup_irq = 0;
        printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
        wake_lock_timeout(&modem_wakelock, 10 * HZ);
        schedule_delayed_work(&wakeup_work, HZ / 10);
    } else
        printk("%s: already wakeup\n", __FUNCTION__);

    return IRQ_HANDLED;
}
int modem_poweron_off(int on_off)
{
	struct rk29_mi700_data *pdata = gpdata;	
	
  	mutex_lock(&pdata->bp_mutex);
	if(on_off)
	{
		MODEMDBG("------------modem_poweron\n");
		gpio_set_value(pdata->bp_reset, GPIO_LOW);
		msleep(100);
		gpio_set_value(pdata->bp_reset, GPIO_HIGH);
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(1000);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		msleep(700);
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
	}
	else
	{
		MODEMDBG("------------modem_poweroff\n");
		gpio_set_value(pdata->bp_power, GPIO_LOW);
		gpio_set_value(pdata->bp_power, GPIO_HIGH);
		msleep(2500);
		gpio_set_value(pdata->bp_power, GPIO_LOW);
	}
  	mutex_unlock(&pdata->bp_mutex);
        return 0;
}
static int mi700_open(struct inode *inode, struct file *file)
{
	//MODEMDBG("-------------%s\n",__FUNCTION__);
	struct rk29_mi700_data *pdata = gpdata;
//	struct platform_data *pdev = container_of(pdata, struct device, platform_data);
	device_init_wakeup(pdata->dev, 1);
	return 0;
}

static int mi700_release(struct inode *inode, struct file *file)
{
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	//modem_poweron_off(0);
	return 0;
}

static long mi700_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_mi700_data *pdata = gpdata;
	switch(cmd)
	{
		case MI700_RESET:					
			gpio_set_value(pdata->bp_reset, GPIO_LOW);
			msleep(100);
			gpio_set_value(pdata->bp_reset, GPIO_HIGH);
			msleep(100);
			gpio_set_value(pdata->bp_power, GPIO_HIGH);
			msleep(1000);
			gpio_set_value(pdata->bp_power, GPIO_LOW);
			msleep(700);
			gpio_set_value(pdata->bp_power, GPIO_HIGH);
			break;
		default:
			break;
	}
	return 0;
}

static struct file_operations mi700_fops = {
	.owner = THIS_MODULE,
	.open = mi700_open,
	.release = mi700_release,
	.unlocked_ioctl = mi700_ioctl
};

static struct miscdevice mi700_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mi700",
	.fops = &mi700_fops
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
      if(new_state == modem_status) 
            return _count;
   
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t online_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t online_read(struct class *cls, char *_buf)
#endif
{
	return sprintf(_buf, "%d\n", online);
	
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t online_write(struct class *cls, struct class_attribute *attr, const char *_buf, size_t _count)
#else
static ssize_t online_write(struct class *cls, const char *_buf, size_t _count)
#endif
{
   int new_value = simple_strtoul(_buf, NULL, 16);
   if(new_value == online) return _count;
	online = new_value;
    return _count; 
}
static CLASS_ATTR(online, 0777, online_read, online_write);
static int mi700_probe(struct platform_device *pdev)
{
	struct rk29_mi700_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mi700_data = NULL;
	int result, irq = 0;	
	MODEMDBG("-------------%s\n",__FUNCTION__);
	
	pdata->dev = &pdev->dev;
	if(pdata->io_init)
		pdata->io_init();
	
	mi700_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(mi700_data == NULL)
	{
		printk("failed to request mi700_data\n");
		goto err2;
	}
	platform_set_drvdata(pdev, mi700_data);
        #if 0
	result = gpio_request(pdata->ap_wakeup_bp, "mi700");
	if (result) {
		printk("failed to request AP_BP_WAKEUP gpio\n");
		goto err1;
	}
        #endif
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
	
  	mutex_init(&pdata->bp_mutex);

	modem_poweron_off(1);
	modem_status = 1;
		
	result = misc_register(&mi700_misc);
	if(result)
	{
		printk("misc_register err\n");
	}	
	return result;
err0:
	cancel_work_sync(&mi700_data->work);
	gpio_free(pdata->bp_wakeup_ap);
err1:
	//gpio_free(pdata->ap_wakeup_bp);
err2:
	kfree(mi700_data);
	return 0;
}

int mi700_suspend(struct platform_device *pdev)
{
	
	struct rk29_mi700_data *pdata = pdev->dev.platform_data;
        do_wakeup_irq = 1;
	MODEMDBG("%s::%d--\n",__func__,__LINE__);
	//gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
	return 0;
}

int mi700_resume(struct platform_device *pdev)
{
	MODEMDBG("-------------%s\n",__FUNCTION__);
	//ap_wakeup_bp(pdev, 0);
	//rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_UART0_RTS_N);
	return 0;
}

void mi700_shutdown(struct platform_device *pdev, pm_message_t state)
{
	struct rk29_mi700_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mi700_data = platform_get_drvdata(pdev);
	
	MODEMDBG("-------------%s\n",__FUNCTION__);
	modem_poweron_off(0);

	if(pdata->io_deinit)
		pdata->io_deinit();
	cancel_work_sync(&mi700_data->work);
	//gpio_free(pdata->bp_power);
	//gpio_free(pdata->bp_reset);
	//gpio_free(pdata->ap_wakeup_bp);
	gpio_free(pdata->bp_wakeup_ap);
	kfree(mi700_data);
}

static struct platform_driver mi700_driver = {
	.probe	        = mi700_probe,
	.shutdown	= mi700_shutdown,
	.suspend  	= mi700_suspend,
	.resume		= mi700_resume,
	.driver	= {
		.name	= "MW100",
		.owner	= THIS_MODULE,
	},
};

static int __init mi700_init(void)
{
	MODEMDBG("-------------%s\n",__FUNCTION__);
	int ret ;
	
	modem_class = class_create(THIS_MODULE, "rk291x_modem");
	ret =  class_create_file(modem_class, &class_attr_modem_status);
	ret =  class_create_file(modem_class, &class_attr_online);
	if (ret)
	{
		printk("Fail to class rk291x_modem.\n");
	}
	return platform_driver_register(&mi700_driver);
}

static void __exit mi700_exit(void)
{
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	platform_driver_unregister(&mi700_driver);
	class_remove_file(modem_class, &class_attr_modem_status);
	class_remove_file(modem_class, &class_attr_online);
}

module_init(mi700_init);
module_exit(mi700_exit);
