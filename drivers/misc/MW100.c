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
#include <linux/mu509.h>
#include <mach/iomux.h>
#include<linux/ioctl.h>
   
MODULE_LICENSE("GPL");

//#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif

#define   	MW100IO	0XA1
#define	MW_IOCTL_RESET	_IO(MW100IO,0X01)

#define SLEEP 1
#define READY 0
#define MU509_RESET 0x01
static struct wake_lock modem_wakelock;
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
struct rk29_mu509_data *gpdata = NULL;
static int  bp_wakeup_ap_irq = 0;


static void ap_wakeup_bp(struct platform_device *pdev, int wake)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	MODEMDBG("ap_wakeup_bp\n");

	gpio_set_value(pdata->ap_wakeup_bp, wake);  

}
extern void rk28_send_wakeup_key(void);

static void do_wakeup(struct work_struct *work)
{
    MODEMDBG("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    rk28_send_wakeup_key();
    enable_irq(bp_wakeup_ap_irq);
}

static DECLARE_DELAYED_WORK(wakeup_work, do_wakeup);
static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
   	disable_irq_nosync( irq);
        printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
        schedule_delayed_work(&wakeup_work, HZ / 10);
   
    return IRQ_HANDLED;
}
int modem_poweron_off(int on_off)
{
	struct rk29_mu509_data *pdata = gpdata;		
  if(on_off)
  {
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
/*	
	gpio_set_value(pdata->bp_power, GPIO_LOW);
	msleep(1000);
	gpio_set_value(pdata->bp_power, GPIO_HIGH);
	msleep(700);
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
*/
  }
  else
  {
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
/*	
	gpio_set_value(pdata->bp_power, GPIO_LOW);
	mdelay(2500);
	gpio_set_value(pdata->bp_power, GPIO_HIGH);
*/
  }
  return 0;
}
static int mu509_open(struct inode *inode, struct file *file)
{
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	//modem_poweron_off(1);
	return 0;
}

static int mu509_release(struct inode *inode, struct file *file)
{
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	//modem_poweron_off(0);
	return 0;
}

static int mu509_ioctl(struct inode *inode,struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_mu509_data *pdata = gpdata;
	switch(cmd)
	{
		case MW_IOCTL_RESET:			
		printk("%s::%d--bruins--ioctl  mw100 reset\n",__func__,__LINE__);
		gpio_direction_output(pdata->bp_reset,GPIO_LOW);
		mdelay(120);
		gpio_set_value(pdata->bp_reset, GPIO_HIGH);
		
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
	.ioctl = mu509_ioctl
};

static struct miscdevice mu509_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mw100",
	.fops = &mu509_fops
};

static int mu509_probe(struct platform_device *pdev)
{
	struct rk29_mu509_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mu509_data = NULL;
	int result, irq = 0;	

	gpio_request(pdata->bp_power,"bp_power");
	gpio_request(pdata->bp_reset,"bp_reset");
	gpio_request(pdata->bp_wakeup_ap,"bp_wakeup_ap");
	gpio_request(pdata->ap_wakeup_bp,"ap_wakeup_bp");
	
	rk29_mux_api_set(GPIO6C76_CPUTRACEDATA76_NAME, GPIO4H_GPIO6C76);

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
	
	
	//±£Áô
/*	gpio_set_value(pdata->bp_reset, GPIO_LOW);
	gpio_direction_output(pdata->bp_reset,GPIO_LOW);
	mdelay(120);
	gpio_set_value(pdata->bp_reset, GPIO_HIGH);
	gpio_direction_output(pdata->bp_reset,GPIO_HIGH);
*/
	mu509_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(mu509_data == NULL){
		printk("failed to request mu509_data\n");
		goto err2;
	}
	platform_set_drvdata(pdev, mu509_data);	
	
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

	result = misc_register(&mu509_misc);
	if(result){
		MODEMDBG("misc_register err\n");
	}	
	return result;
err0:
	gpio_free(pdata->bp_wakeup_ap);
err1:
	gpio_free(pdata->ap_wakeup_bp);
err2:
	kfree(mu509_data);
	return 0;
}

int mu509_suspend(struct platform_device *pdev)
{
	
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	MODEMDBG("%s::%d--\n",__func__,__LINE__);
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_LOW);
	return 0;
}

int mu509_resume(struct platform_device *pdev)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	gpio_set_value(pdata->ap_wakeup_bp, GPIO_HIGH);	
	return 0;
}

void mu509_shutdown(struct platform_device *pdev, pm_message_t state)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mu509_data = platform_get_drvdata(pdev);
	
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	gpio_set_value(pdata->bp_power, GPIO_HIGH);
	mdelay(2010);

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
		.name	= "MW100",
		.owner	= THIS_MODULE,
	},
};

static int __init mu509_init(void)
{
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	return platform_driver_register(&mu509_driver);
}

static void __exit mu509_exit(void)
{
	MODEMDBG("%s::%d--bruins--\n",__func__,__LINE__);
	platform_driver_unregister(&mu509_driver);
}

module_init(mu509_init);

module_exit(mu509_exit);
