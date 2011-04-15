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

MODULE_LICENSE("GPL");

#define DEBUG
#ifdef DEBUG
#define MODEMDBG(x...) printk(x)
#else
#define MODEMDBG(fmt,argss...)
#endif

//#define BP_POW_EN	TCA6424_P02
//#define BP_STATUS    RK2818_PIN_PH7    //input  high bp sleep
//#define AP_STATUS    RK2818_PIN_PA4    //output high ap sleep

//#define BP_RESET	TCA6424_P11	//Ryan

//#define AP_BP_WAKEUP  RK2818_PIN_PF5   //output AP wake up BP used rising edge
//#define BP_AP_WAKEUP  RK2818_PIN_PE0	//input BP wake up AP

#define SLEEP 1
#define READY 0

//struct modem_dev *mt6223d_data = NULL;
struct rk2818_23d_data *gpdata = NULL;

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

static int mtk23d_open(struct inode *inode, struct file *file)
{
	struct rk2818_23d_data *pdata = gpdata;
	//struct rk2818_23d_data *pdata = gpdata = pdev->dev.platform_data;
	struct platform_data *pdev = container_of(pdata, struct device, platform_data);

	MODEMDBG("modem_open\n");

	int ret = 0;

	//gpio_direction_output(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH); // phc
	
	gpio_direction_input(pdata->bp_statue);
	
	//rk2818_mux_api_set(CXGPIO_HSADC_SEL_NAME, 0);
	gpio_direction_output(pdata->ap_statue, GPIO_LOW);
	
	//rk2818_mux_api_set(GPIOF5_APWM3_DPWM3_NAME,0);
	gpio_direction_output(pdata->ap_bp_wakeup, GPIO_LOW);
	mdelay(100);
	//rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL_NAME, IOMUXA_GPIO1_A3B7);
	gpio_direction_output(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
	mdelay(100);
	gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
	mdelay(10);
	gpio_direction_output(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
	mdelay(2000);
	gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
	
	gpio_set_value(pdata->ap_bp_wakeup, GPIO_HIGH);
	
	#if 1 // phc
	rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME, GPIO1L_UART0_SOUT);
	rk29_mux_api_set(GPIO1B6_UART0SIN_NAME, GPIO1L_UART0_SIN); 
	rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_UART0_RTS_N);
	rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_UART0_CTS_N); 	
	#endif
	
	//INIT_WORK(&mt6223d_data->work, bpwakeup_work_func_work);
	device_init_wakeup(&pdev, 1);

	printk("%s\n",__FUNCTION__);
	return 0;
}

static int mtk23d_release(struct inode *inode, struct file *file)
{
	MODEMDBG("mtk23d_release\n");

	//gpio_free(pdata->bp_power);
	return 0;
}

static int mtk23d_ioctl(struct inode *inode,struct file *file, unsigned int cmd, unsigned long arg)
{
	MODEMDBG("mtk23d_ioctl\n");
	return 0;
}

static struct file_operations mtk23d_fops = {
	.owner = THIS_MODULE,
	.open = mtk23d_open,
	.release = mtk23d_release,
	.ioctl = mtk23d_ioctl
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
	int result, irq = 0;	
	
	MODEMDBG("mtk23d_probe\n");

#if 1	
	rk29_mux_api_set(GPIO1B7_UART0SOUT_NAME, GPIO1L_GPIO1B7); 			
	gpio_request(RK29_PIN1_PB7, NULL);
	gpio_direction_output(RK29_PIN1_PB7,GPIO_LOW);
	
	rk29_mux_api_set(GPIO1B6_UART0SIN_NAME, GPIO1L_GPIO1B6); 		
	gpio_request(RK29_PIN1_PB6, NULL);
	gpio_direction_output(RK29_PIN1_PB6,GPIO_LOW);	
	
	rk29_mux_api_set(GPIO1C1_UART0RTSN_SDMMC1WRITEPRT_NAME, GPIO1H_GPIO1C1); 			
	gpio_request(RK29_PIN1_PC1, NULL);
	gpio_direction_output(RK29_PIN1_PC1,GPIO_LOW);
	
	rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_GPIO1C0); 		
	gpio_request(RK29_PIN1_PC0, NULL);
	gpio_direction_output(RK29_PIN1_PC0,GPIO_LOW);		
#endif

	mt6223d_data = kzalloc(sizeof(struct modem_dev), GFP_KERNEL);
	if(NULL == mt6223d_data)
	{
		printk("failed to request mt6223d_data\n");
		goto err6;
	}
	platform_set_drvdata(pdev, mt6223d_data);

	pdata->io_init();

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
	
#if 1 // phc
	gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
	gpio_direction_output(pdata->ap_statue, GPIO_LOW);
	gpio_direction_output(pdata->ap_bp_wakeup, GPIO_LOW);
	gpio_direction_output(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
	mdelay(100);
	gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
#endif	
	
#if 0 
	gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_HIGH:GPIO_LOW);
	
	gpio_direction_input(pdata->bp_statue);
	
	//rk2818_mux_api_set(CXGPIO_HSADC_SEL_NAME, 0);
	gpio_direction_output(pdata->ap_statue, GPIO_LOW);
	
	//rk2818_mux_api_set(GPIOF5_APWM3_DPWM3_NAME,0);
	gpio_direction_output(pdata->ap_bp_wakeup, GPIO_LOW);
	mdelay(100);
	//rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL_NAME, IOMUXA_GPIO1_A3B7);
	gpio_direction_output(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_LOW:GPIO_HIGH);
	mdelay(100);
	gpio_set_value(pdata->bp_reset, pdata->bp_reset_active_low? GPIO_HIGH:GPIO_LOW);
	
	mdelay(2000);
	gpio_set_value(pdata->bp_power, pdata->bp_power_active_low? GPIO_LOW:GPIO_HIGH);
	
	gpio_set_value(pdata->ap_bp_wakeup, GPIO_HIGH);
	printk("%s:power up modem\n",__FUNCTION__);
#endif

	INIT_WORK(&mt6223d_data->work, bpwakeup_work_func_work);

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
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	//int irq = gpio_to_irq(pdata->bp_ap_wakeup);
	
	MODEMDBG("%s \n", __FUNCTION__);
	//enable_irq_wake(irq);
	ap_sleep(pdev);
	ap_wakeup_bp(pdev, 0);
	return 0;
}

int mtk23d_resume(struct platform_device *pdev)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	//int irq = gpio_to_irq(pdata->bp_ap_wakeup);
	
	MODEMDBG("%s \n", __FUNCTION__);
	//disable_irq_wake(irq);
	ap_wakeup(pdev);
	ap_wakeup_bp(pdev, 1);
	return 0;
}

void mtk23d_shutdown(struct platform_device *pdev, pm_message_t state)
{
	struct rk2818_23d_data *pdata = pdev->dev.platform_data;
	struct modem_dev *mt6223d_data = platform_get_drvdata(pdev);
	
	MODEMDBG("%s \n", __FUNCTION__);

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
