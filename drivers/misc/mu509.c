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
//#include <mach/spi_fpga.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
//#include <linux/android_power.h>
//#include <asm/arch/gpio_extend.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/mu509.h>

MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif
#define SLEEP 1
#define READY 0
#define MU509_RESET 0x01
/*
struct rk29_mu509_data rk29_mu509_info = {
	.io_init = mu509_io_init,
  .io_deinit = mu509_io_deinit,
	.bp_power = RK29_PIN6_PB1,//RK29_PIN0_PB4,
	.bp_power_active_low = 1,
	.bp_reset = RK29_PIN6_PC7,//RK29_PIN0_PB3,
	.bp_reset_active_low = 1,
	.bp_wakeup_ap = RK29_PIN0_PA4,//RK29_PIN0_PC2,
	.ap_wakeup_bp = RK29_PIN2_PB3,//RK29_PIN0_PB0, 
};
*/
static struct wake_lock modem_wakelock;
#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_FALLING
//#define IRQ_BB_WAKEUP_AP_TRIGGER    IRQF_TRIGGER_RISING
struct rk29_mu509_data *gpdata = NULL;
static int do_wakeup_irq = 0;

static void ap_wakeup_bp(struct platform_device *pdev, int wake)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
        //struct modem_dev *mu509_data = platform_get_drvdata(pdev);
	MODEMDBG("ap_wakeup_bp\n");

	gpio_set_value(pdata->ap_wakeup_bp, wake);  

}
/*
static void bpwakeup_work_func_work(struct work_struct *work)
{
	struct modem_dev *bdata = container_of(work, struct modem_dev, work);
	wake_lock_timeout(&modem_wakelock, 10 * HZ);
	MODEMDBG("%s\n", __FUNCTION__);
	
}*/
/*static irqreturn_t  bpwakeup_work_func(int irq, void *data)
{
	struct modem_dev *mu509_data = (struct modem_dev *)data;
	
   	MODEMDBG("bpwakeup_work_func\n");
	wake_lock_timeout(&modem_wakelock, 10 * HZ);
	return IRQ_HANDLED;
}*/
/*static irqreturn_t  bp_apwakeup_work_func(int irq, void *data)
{
   	MODEMDBG("bp_apwakeup_work_func\n");
	return IRQ_HANDLED;
}*/
extern void rk28_send_wakeup_key(void);

static void do_wakeup(struct work_struct *work)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    rk28_send_wakeup_key();
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
    }
    return IRQ_HANDLED;
}
int modem_poweron_off(int on_off)
{
	struct rk29_mu509_data *pdata = gpdata;
	//gpio_set_value(pdata->bp_power,0);
		
  if(on_off)
  {
		printk("modem_poweron\n");
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
		mdelay(300);
		gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
		msleep(4000);
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
  }
  else
  {
		printk("modem_poweroff\n");
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
		mdelay(100);
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
  }
  return 0;
}
static int power_on =1;
static int mu509_open(struct inode *inode, struct file *file)
{
	//struct rk29_mu509_data *pdata = gpdata;
	//struct platform_data *pdev = container_of(pdata, struct device, platform_data);

	MODEMDBG("modem_open\n");
	modem_poweron_off(1);
	//int ret = 0;
/*	if(power_on)
	{
		power_on = 0;
		modem_poweron_off(1);
		#if 1 
		rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME, GPIO1L_UART0_SOUT);
		rk29_mux_api_set(GPIO1B6_UART0SIN_NAME, GPIO1L_UART0_SIN); 
		rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_UART0_RTS_N);
		rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_UART0_CTS_N); 	
		#endif
	}
	device_init_wakeup(&pdev, 1);
*/
	printk("%s\n",__FUNCTION__);
	return 0;
}

static int mu509_release(struct inode *inode, struct file *file)
{
	MODEMDBG("mu509_release\n");
	return 0;
}

static int mu509_ioctl(struct inode *inode,struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk29_mu509_data *pdata = gpdata;
	//int i;
	//void __user *argp = (void __user *)arg;
	printk("mu509_ioctl\n");
	switch(cmd)
	{
		case MU509_RESET:		
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
			mdelay(100);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
			mdelay(300);
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
			msleep(4000);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
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
	.name = MODEM_NAME,
	.fops = &mu509_fops
};

static int mu509_probe(struct platform_device *pdev)
{
	struct rk29_mu509_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mu509_data = NULL;
	int result, irq = 0;	
	
	MODEMDBG("mu509_probe\n");
	power_on =1;
	modem_poweron_off(1);
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
	gpio_direction_output(pdata->ap_wakeup_bp, 1);
	gpio_direction_input(pdata->bp_wakeup_ap);
       gpio_pull_updown(pdata->bp_wakeup_ap, 1);	
	result = request_irq(irq, detect_irq_handler, IRQ_BB_WAKEUP_AP_TRIGGER, "bp_wakeup_ap", NULL);
	if (result < 0) {
		printk("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(pdata->bp_wakeup_ap);
		goto err0;
	}
	enable_irq_wake(gpio_to_irq(pdata->bp_wakeup_ap)); 
	printk("%s: request_irq(%d) success\n", __func__, irq);
	result = misc_register(&mu509_misc);
	if(result)
	{
		MODEMDBG("misc_register err\n");
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

int mu509_suspend(struct platform_device *pdev)
{
	do_wakeup_irq = 1;
	//struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	printk("------------mu509_suspend");
	MODEMDBG("%s \n", __FUNCTION__);
	ap_wakeup_bp(pdev, 0);
	return 0;
}

int mu509_resume(struct platform_device *pdev)
{
	//struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	printk("-------------mu509_resume");
	MODEMDBG("%s \n", __FUNCTION__);
	ap_wakeup_bp(pdev, 1);
	return 0;
}

void mu509_shutdown(struct platform_device *pdev, pm_message_t state)
{
	struct rk29_mu509_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mu509_data = platform_get_drvdata(pdev);
	
	MODEMDBG("%s \n", __FUNCTION__);

	cancel_work_sync(&mu509_data->work);
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
	printk("-----%s----------zzc\n",__FUNCTION__);
	return platform_driver_register(&mu509_driver);
}

static void __exit mu509_exit(void)
{
	MODEMDBG("mu509_exit\n");
	platform_driver_unregister(&mu509_driver);
}

module_init(mu509_init);

module_exit(mu509_exit);
