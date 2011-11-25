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
#include <linux/workqueue.h>
#include <linux/mtk23d.h>
#include <linux/wakelock.h>
#include "../mtd/rknand/api_flash.h"
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif
#define MTK23D_POWEROFF	0X00
#define MTK23D_RESET 0x01
#define MTK23D_POWERON  0x02
#define MTK23D_POWER_HIGH 0x03
#define MTK23D_IMEI_READ  0x04
//#define BP_POW_EN	TCA6424_P02
//#define BP_STATUS    RK2818_PIN_PH7    //input  high bp sleep
//#define AP_STATUS    RK2818_PIN_PA4    //output high ap sleep

//#define BP_RESET	TCA6424_P11	//Ryan

//#define AP_BP_WAKEUP  RK2818_PIN_PF5   //output AP wake up BP used rising edge
//#define BP_AP_WAKEUP  RK2818_PIN_PE0	//input BP wake up AP

static bool bpstatus_irq_enable = false;
static bool wakelock_inited;
static struct wake_lock mtk23d_wakelock;

#define SLEEP 1
#define READY 0

//struct modem_dev *mt6223d_data = NULL;
struct rk2818_23d_data *gpdata = NULL;

static int rk29_uart_to_gpio(int uart_id)
{
	if(uart_id == 3) {
		rk29_mux_api_set(GPIO2B3_UART3SOUT_NAME, GPIO2L_GPIO2B3); 			
		rk29_mux_api_set(GPIO2B2_UART3SIN_NAME, GPIO2L_GPIO2B2); 		

		gpio_request(RK29_PIN2_PB3, NULL);
		gpio_request(RK29_PIN2_PB2, NULL);

		gpio_direction_output(RK29_PIN2_PB3, GPIO_LOW);
		gpio_direction_output(RK29_PIN2_PB2, GPIO_LOW);
	}
	else if(uart_id == 2) {
		rk29_mux_api_set(GPIO2B1_UART2SOUT_NAME, GPIO2L_GPIO2B1); 			
		rk29_mux_api_set(GPIO2B0_UART2SIN_NAME, GPIO2L_GPIO2B0); 		

		gpio_request(RK29_PIN2_PB1, NULL);
		gpio_request(RK29_PIN2_PB0, NULL);

		gpio_direction_output(RK29_PIN2_PB1, GPIO_LOW);
		gpio_direction_output(RK29_PIN2_PB0, GPIO_LOW);
	}
	else if(uart_id == 1) {
		rk29_mux_api_set(GPIO2A5_UART1SOUT_NAME, GPIO2L_GPIO2A5); 			
		rk29_mux_api_set(GPIO2A4_UART1SIN_NAME, GPIO2L_GPIO2A4); 		

		gpio_request(RK29_PIN2_PA5, NULL);
		gpio_request(RK29_PIN2_PA4, NULL);

		gpio_direction_output(RK29_PIN2_PA5, GPIO_LOW);
		gpio_direction_output(RK29_PIN2_PA4, GPIO_LOW);
	}
	else if(uart_id == 0){
		rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME, GPIO1L_GPIO1B7); 			
		gpio_request(RK29_PIN1_PB7, NULL);
		gpio_direction_output(RK29_PIN1_PB7,GPIO_LOW);
		gpio_pull_updown(RK29_PIN1_PB7, PullDisable);  // 下拉禁止

		rk29_mux_api_set(GPIO1B6_UART0SIN_NAME, GPIO1L_GPIO1B6); 		
		gpio_request(RK29_PIN1_PB6, NULL);
		gpio_direction_output(RK29_PIN1_PB6,GPIO_LOW);	
		gpio_pull_updown(RK29_PIN1_PB6, PullDisable);  // 下拉禁止

		rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_GPIO1C1); 			
		gpio_request(RK29_PIN1_PC1, NULL);
		gpio_direction_output(RK29_PIN1_PC1,GPIO_LOW);

		rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_GPIO1C0); 		
		gpio_request(RK29_PIN1_PC0, NULL);
		//gpio_direction_input(RK29_PIN1_PC0);		
		gpio_direction_output(RK29_PIN1_PC0,GPIO_LOW);
	}

	return 0;
}

static int rk29_gpio_to_uart(int uart_id)
{
	if(uart_id == 3) {
		rk29_mux_api_set(GPIO2B3_UART3SOUT_NAME, GPIO2L_UART3_SOUT);
		rk29_mux_api_set(GPIO2B2_UART3SIN_NAME, GPIO2L_UART3_SIN); 

		gpio_request(RK29_PIN2_PB3, NULL);
		gpio_request(RK29_PIN2_PB2, NULL);

		gpio_direction_output(RK29_PIN2_PB3, GPIO_HIGH);
		gpio_direction_output(RK29_PIN2_PB2, GPIO_HIGH);
	}
	else if(uart_id == 2) {
		rk29_mux_api_set(GPIO2B1_UART2SOUT_NAME, GPIO2L_UART2_SOUT); 			
		rk29_mux_api_set(GPIO2B0_UART2SIN_NAME, GPIO2L_UART2_SIN); 		

		gpio_request(RK29_PIN2_PB1, NULL);
		gpio_request(RK29_PIN2_PB0, NULL);

		gpio_direction_output(RK29_PIN2_PB1, GPIO_HIGH);
		gpio_direction_output(RK29_PIN2_PB0, GPIO_HIGH);
	}
	else if(uart_id == 1) {
		rk29_mux_api_set(GPIO2A5_UART1SOUT_NAME, GPIO2L_UART1_SOUT); 			
		rk29_mux_api_set(GPIO2A4_UART1SIN_NAME, GPIO2L_UART1_SIN); 		

		gpio_request(RK29_PIN2_PA5, NULL);
		gpio_request(RK29_PIN2_PA4, NULL);

		gpio_direction_output(RK29_PIN2_PA5, GPIO_HIGH);
		gpio_direction_output(RK29_PIN2_PA4, GPIO_HIGH);
	}
	else if(uart_id == 0){
		rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME, GPIO1L_UART0_SOUT);
		rk29_mux_api_set(GPIO1B6_UART0SIN_NAME, GPIO1L_UART0_SIN); 
		rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_UART0_RTS_N);
		rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_UART0_CTS_N); 	
	}

	return 0;

}

static int  get_bp_statue(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	
	if(gpio_get_value(pdata->bp_statue))
		return SLEEP;
	else
		return READY;
}
static void ap_sleep(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	
	MODEMDBG("ap sleep!\n");
	gpio_set_value(pdata->ap_statue,GPIO_HIGH);
}
static void ap_wakeup(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	
	MODEMDBG("ap wakeup!\n");
	gpio_set_value(pdata->ap_statue,GPIO_LOW);
}
/* */
static void ap_wakeup_bp(struct platform_device *pdev, int wake)//low to wakeup bp
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
        struct modem_dev *mt6223d_data = platform_get_drvdata(pdev);
	MODEMDBG("ap_wakeup_bp\n");

	gpio_set_value(pdata->ap_bp_wakeup, wake);  // phc
	//gpio_set_value(RK2818_PIN_PF5, wake);
}

static void bpwakeup_work_func_work(struct work_struct *work)
{
	struct modem_dev *bdata = container_of(work, struct modem_dev, work);
	
	MODEMDBG("%s\n", __FUNCTION__);
	
}
/*  */
static irqreturn_t  bpwakeup_work_func(int irq, void *data)
{
	struct modem_dev *mt6223d_data = (struct modem_dev *)data;
	
   	MODEMDBG("bpwakeup_work_func\n");
	schedule_work(&mt6223d_data->work);
	return IRQ_HANDLED;
}
static irqreturn_t  bp_apwakeup_work_func(int irq, void *data)
{
	//struct modem_dev *dev = &mtk23d_misc;
	
   	MODEMDBG("bp_apwakeup_work_func\n");
	//wake_up_interruptible(&dev->wakeup);
	return IRQ_HANDLED;
}

static irqreturn_t BBwakeup_isr(int irq, void *dev_id)
{
	struct rk2818_23d_data *pdata = dev_id;
	
	MODEMDBG("%s \n", __FUNCTION__);
	//if(irq != gpio_to_irq(RK29_PIN1_PC0))
	//{
	//		printk("irq != gpio_to_irq(RK29_PIN1_PC0) \n");
	//		return IRQ_NONE;
	//}
	
//	disable_irq_wake(irq);
	
	if(bpstatus_irq_enable == true)
	{
		MODEMDBG("mtk23d_wakelock 3s \n");
		wake_lock_timeout(&mtk23d_wakelock, 3 * HZ);
	}
		

	return IRQ_HANDLED;
}

int modem_poweron_off(int on_off)
{
	struct rk2818_23d_data *pdata = gpdata;
	int result, error = 0, irq = 0;	
	
  if(on_off)
	{
		printk("modem_poweron\n");

		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);  // power on enable
		mdelay(300);
		gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);  // release reset
		msleep(3000);
		gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);  // power on relase

		rk29_gpio_to_uart(0);
		
		gpio_direction_input(pdata->bp_statue);
		if(pdata->bp_ap_wakeup) // SDK板中，该口没有引出
		gpio_direction_input(pdata->bp_ap_wakeup);

		/* 初始化BP唤醒AP的功能 */
		wakelock_inited = false;
		irq = gpio_to_irq(pdata->bp_statue);
		if (irq < 0) {
			printk("can't get pdata->bp_statue irq \n");
		}
		else
		{
			error = request_irq(irq, BBwakeup_isr,IRQF_TRIGGER_FALLING, "mtk23d", pdata);
			if (error) {
				printk("mtk23d_probe bp_statue request_irq error!!! \n");
			}
		}
		if (!wakelock_inited) {
			wake_lock_init(&mtk23d_wakelock, WAKE_LOCK_SUSPEND, "23d_resume");
			wakelock_inited = true;
		}
	}
  else
  {
			printk("modem_poweroff\n");
			rk29_uart_to_gpio(0);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
			msleep(2500);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
			msleep(500);
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH); 
  }
}
static int power_on =1;
static int mtk23d_open(struct inode *inode, struct file *file)
{
	//struct rk2818_23d_data *pdata = gpdata;
	//struct rk2818_23d_data *pdata = gpdata = pdev->dev.platform_data;
	//struct platform_data *pdev = container_of(pdata, struct device, platform_data);

	MODEMDBG("modem_open\n");

	int ret = 0;
	if(power_on)
	{
		power_on = 0;
		modem_poweron_off(1);
	}
	device_init_wakeup(&pdev->dev, 1);

	return 0;
}

static int mtk23d_release(struct inode *inode, struct file *file)
{
	MODEMDBG("mtk23d_release\n");

	//gpio_free(pdata->bp_power);
	return 0;
}

//extern char imei_value[16]; // phc, no find 'imei_value' in rk29 project
//char imei_value[16] = {0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5};

static int mtk23d_ioctl(struct inode *inode,struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rk2818_23d_data *pdata = gpdata;
	int i,ret;
	void __user *argp = (void __user *)arg;
	
	char SectorBuffer[512];
	
	printk("mtk23d_ioctl\n");
	ret = down_interruptible(&pdata->power_sem);
	if (ret < 0) {
		printk("%s: down power_sem error ret = %d\n", __func__, ret);
		return ret;
	}
	
	switch(cmd)
	{

		case MTK23D_POWEROFF:
			printk("MTK23D_POWEROFF\n");
			rk29_uart_to_gpio(0);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
			msleep(2500);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
			msleep(500);
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
			break;

		case MTK23D_RESET:		
			printk("MTK23D_RESET\n");
			/****power off 23d and uart to gpio***/
			rk29_uart_to_gpio(0);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
			msleep(2500);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
			msleep(500);
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
			
			/****power on 23d***/
			msleep(100);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
			rk29_gpio_to_uart(0);
			msleep(300);
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
			msleep(2500);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
			break;

		case MTK23D_POWERON:		
			/****power on 23d***/
			printk("MTK23D_POWERON\n");
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
			msleep(100);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
			rk29_gpio_to_uart(0);
			msleep(300);
			gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
			msleep(2500);
			gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
			break;
			
		case MTK23D_IMEI_READ:
			printk("MTK23D_IMEI_READ\n");
			
			GetSNSectorInfo(SectorBuffer); // phc,20110624
			
			if(copy_to_user(argp, &(SectorBuffer[451]), 16))  // IMEI后从451偏移开始的16bytes，第一个byte为长度固定为15
			{
				printk("ERROR: copy_to_user---%s\n", __FUNCTION__);
				up(&pdata->power_sem);
				return -EFAULT;
			}
			//printk("IMEI:%d %d %d %d\n", SectorBuffer[451], SectorBuffer[452], SectorBuffer[453], SectorBuffer[454]);
			break;
		default:
			break;
	}

	up(&pdata->power_sem);
	
	return 0;
}

static struct file_operations mtk23d_fops = {
	.owner = THIS_MODULE,
	.open = mtk23d_open,
	.release = mtk23d_release,
	.unlocked_ioctl = mtk23d_ioctl
};

static struct miscdevice mtk23d_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODEM_NAME,
	.fops = &mtk23d_fops
};

static int mtk23d_probe(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = gpdata = pdev->dev.platform_data;
	struct modem_dev *mt6223d_data = NULL;
	int result, error = 0, irq = 0;	
	
	MODEMDBG("mtk23d_probe\n");

	//pdata->io_init();
	pdata->dev = &pdev->dev;

	mt6223d_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(NULL == mt6223d_data)
	{
		printk("failed to request mt6223d_data\n");
		goto err6;
	}
	platform_set_drvdata(pdev, mt6223d_data);

	result = gpio_request(pdata->bp_statue, "mtk23d");
	if (result) {
		printk("failed to request BP_STATUS gpio\n");
		goto err5;
	}
	
	result = gpio_request(pdata->ap_statue, "mtk23d");
	if (result) {
		printk("failed to request AP_STATUS gpio\n");
		goto err4;
	}	
	
	result = gpio_request(pdata->ap_bp_wakeup, "mtk23d");
	if (result) {
		printk("failed to request AP_BP_WAKEUP gpio\n");
		goto err3;
	}	
	result = gpio_request(pdata->bp_reset, "mtk23d");
	if (result) {
		printk("failed to request BP_RESET gpio\n");
		goto err2;
	}		
	result = gpio_request(pdata->bp_power, "mtk23d");
	if (result) {
		printk("failed to request BP_POW_EN gpio\n");
		goto err1;
	}
	
	if(pdata->bp_ap_wakeup) // SDK板中，该口没有引出
	{
		result = gpio_request(pdata->bp_ap_wakeup, "mtk23d");
		if (result) {
			printk("failed to request BP_AP_WAKEUP gpio\n");
			goto err0;
		}		
	}
	
#if 1 // GPIO初始化，并且防止漏电
	rk29_uart_to_gpio(0);

	/***power off 23d***/
	gpio_direction_output(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
	
	gpio_direction_output(pdata->ap_statue, GPIO_LOW);
	gpio_direction_output(pdata->ap_bp_wakeup, GPIO_LOW);   
	//gpio_direction_output(pdata->bp_statue,GPIO_LOW);
	gpio_direction_input(pdata->bp_statue);
	if(pdata->bp_ap_wakeup) // SDK板中，该口没有引出
	{
		//gpio_direction_output(pdata->bp_ap_wakeup,GPIO_LOW);
		gpio_direction_input(pdata->bp_ap_wakeup);
	}

	/*复位BP*/
	gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
	//mdelay(200);
	//gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
#endif	

	INIT_WORK(&mt6223d_data->work, bpwakeup_work_func_work);
	sema_init(&pdata->power_sem,1);
  	power_on = 1;
	result = misc_register(&mtk23d_misc);
	if(result)
	{
		MODEMDBG("misc_register err\n");
	}
	MODEMDBG("mtk23d_probe ok\n");
	
	return result;
err0:
	cancel_work_sync(&mt6223d_data->work);
	gpio_free(pdata->bp_ap_wakeup);
err1:
	gpio_free(pdata->bp_power);
err2:
	gpio_free(pdata->bp_reset);
err3:
	gpio_free(pdata->ap_bp_wakeup);
err4:
	gpio_free(pdata->ap_statue);
err5:
	gpio_free(pdata->bp_statue);
err6:
	kfree(mt6223d_data);
ret:
	return result;
}

int mtk23d_suspend(struct platform_device *pdev)
{
	int irq, error;
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	
	MODEMDBG("%s \n", __FUNCTION__);
	
	//enable_irq_wake(irq);
	ap_sleep(pdev);
	ap_wakeup_bp(pdev, 0);

	irq = gpio_to_irq(pdata->bp_statue);
	if (irq < 0) {
		printk("can't get pdata->bp_statue irq \n");
	}
	else
	{
		printk("enable pdata->bp_statue irq_wake!! \n");
		bpstatus_irq_enable = true;
		enable_irq_wake(irq);
	}
	
	return 0;
}

int mtk23d_resume(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	int irq = 0;
	
	MODEMDBG("%s \n", __FUNCTION__);
	
	irq = gpio_to_irq(pdata->bp_statue);
	if(irq)
	{
		printk("disable pdata->bp_statue irq_wake!! \n");
		bpstatus_irq_enable = false;
		disable_irq_wake(irq);
	}
	
	ap_wakeup(pdev);
	ap_wakeup_bp(pdev, 1);
	
	return 0;
}

void mtk23d_shutdown(struct platform_device *pdev, pm_message_t state)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mt6223d_data = platform_get_drvdata(pdev);
	
	MODEMDBG("%s \n", __FUNCTION__);

	modem_poweron_off(0);  // power down

	cancel_work_sync(&mt6223d_data->work);
	gpio_free(pdata->bp_ap_wakeup);
	gpio_free(pdata->bp_power);
	gpio_free(pdata->bp_reset);
	gpio_free(pdata->ap_bp_wakeup);
	gpio_free(pdata->ap_statue);
	gpio_free(pdata->bp_statue);
	kfree(mt6223d_data);
}

static struct platform_driver mtk23d_driver = {
	.probe	= mtk23d_probe,
	.shutdown	= mtk23d_shutdown,
	.suspend  	= mtk23d_suspend,
	.resume		= mtk23d_resume,
	.driver	= {
		.name	= "mtk23d",
		.owner	= THIS_MODULE,
	},
};

static int __init mtk23d_init(void)
{
	MODEMDBG("mtk23d_init ret=%d\n");
	return platform_driver_register(&mtk23d_driver);
}

static void __exit mtk23d_exit(void)
{
	MODEMDBG("mtk23d_exit\n");
	platform_driver_unregister(&mtk23d_driver);
}

module_init(mtk23d_init);
//late_initcall_sync(mtk23d_init);
module_exit(mtk23d_exit);
