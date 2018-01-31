/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/types.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include "rk29_gps.h"
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#define ENABLE  1
#define DISABLE 0

static struct rk29_gps_data *pgps;

static int rk29_gps_uart_to_gpio(int uart_id)
{
	if(uart_id == 3) {
		rk29_mux_api_set(GPIO2B3_UART3SOUT_NAME, GPIO2L_GPIO2B3); 			
		rk29_mux_api_set(GPIO2B2_UART3SIN_NAME, GPIO2L_GPIO2B2); 		

		gpio_request(RK29_PIN2_PB3, NULL);
		gpio_request(RK29_PIN2_PB2, NULL);

		gpio_pull_updown(RK29_PIN2_PB3, PullDisable);
		gpio_pull_updown(RK29_PIN2_PB2, PullDisable);

		gpio_direction_output(RK29_PIN2_PB3, GPIO_LOW);
		gpio_direction_output(RK29_PIN2_PB2, GPIO_LOW);

		gpio_free(RK29_PIN2_PB3);
		gpio_free(RK29_PIN2_PB2);
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
	else {
		//to do
	}

	return 0;
}

static int rk29_gps_gpio_to_uart(int uart_id)
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
	else {
		//to do
	}

	return 0;

}

static int rk29_gps_suspend(struct platform_device *pdev,  pm_message_t state)
{
	struct rk29_gps_data *pdata = pdev->dev.platform_data;

	if(!pdata) {
		printk("%s: pdata = NULL ...... \n", __func__);
		return -1;
	}
		
	if(pdata->power_flag == 1)
	{
		rk29_gps_uart_to_gpio(pdata->uart_id);
		pdata->power_down();	
		pdata->reset(GPIO_LOW);
	}
	
	printk("%s\n",__FUNCTION__);
	return 0;	
}

static int rk29_gps_resume(struct platform_device *pdev)
{
	struct rk29_gps_data *pdata = pdev->dev.platform_data;

	if(!pdata) {
		printk("%s: pdata = NULL ...... \n", __func__);
		return -1;
	}
	
	if(pdata->power_flag == 1)
	{
		queue_work(pdata->wq, &pdata->work);
	}
	
	printk("%s\n",__FUNCTION__);
	return 0;
}

static void rk29_gps_delay_power_downup(struct work_struct *work)
{
	struct rk29_gps_data *pdata = container_of(work, struct rk29_gps_data, work);
	if (pdata == NULL) {
		printk("%s: pdata = NULL\n", __func__);
		return;
	}

	//DBG("%s: suspend=%d\n", __func__, pdata->suspend);

	down(&pdata->power_sem);
	
	pdata->reset(GPIO_LOW);
	mdelay(5);
	pdata->power_up();
	msleep(500);
	pdata->reset(GPIO_HIGH);
	rk29_gps_gpio_to_uart(pdata->uart_id);

	up(&pdata->power_sem);
}

static int rk29_gps_open(struct inode *inode, struct file *filp)
{
    DBG("rk29_gps_open\n");

	return 0;
}

static ssize_t rk29_gps_read(struct file *filp, char __user *ptr, size_t size, loff_t *pos)
{
	if (ptr == NULL)
		printk("%s: user space address is NULL\n", __func__);

	if (pgps == NULL) {
		printk("%s: pgps addr is NULL\n", __func__);
		return -1;
	}

	put_user(pgps->uart_id, ptr);
	
	return sizeof(int);
}

static long rk29_gps_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct rk29_gps_data *pdata = pgps;

	DBG("rk29_gps_ioctl: cmd = %d arg = %ld\n",cmd, arg);

	ret = down_interruptible(&pdata->power_sem);
	if (ret < 0) {
		printk("%s: down power_sem error ret = %ld\n", __func__, ret);
		return ret;
	}

	switch (cmd){
		case ENABLE:
			pdata->reset(GPIO_LOW);
			mdelay(5);
			pdata->power_up();
			mdelay(5);
			rk29_gps_gpio_to_uart(pdata->uart_id);
			mdelay(500);
			pdata->reset(GPIO_HIGH);
			pdata->power_flag = 1;
			break;
			
		case DISABLE:
			rk29_gps_uart_to_gpio(pdata->uart_id);
			pdata->power_down();
			pdata->reset(GPIO_LOW);
			pdata->power_flag = 0;
			break;

		default:
			printk("unknown ioctl cmd!\n");
			up(&pdata->power_sem);
			ret = -EINVAL;
			break;
	}

	up(&pdata->power_sem);

	return ret;
}


static int rk29_gps_release(struct inode *inode, struct file *filp)
{
    DBG("rk29_gps_release\n");
    
	return 0;
}

static struct file_operations rk29_gps_fops = {
	.owner   = THIS_MODULE,
	.open    = rk29_gps_open,
	.read    = rk29_gps_read,
	.unlocked_ioctl   = rk29_gps_ioctl,
	.release = rk29_gps_release,
};

static struct miscdevice rk29_gps_dev = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "rk29_gps",
    .fops = &rk29_gps_fops,
};

static int rk29_gps_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rk29_gps_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;
		
	ret = misc_register(&rk29_gps_dev);
	if (ret < 0){
		printk("rk29 gps register err!\n");
		return ret;
	}
	
	sema_init(&pdata->power_sem,1);
	pdata->wq = create_freezable_workqueue("rk29_gps");
	INIT_WORK(&pdata->work, rk29_gps_delay_power_downup);
	pdata->power_flag = 0;

	//gps power down
	rk29_gps_uart_to_gpio(pdata->uart_id);
	if (pdata->power_down)
		pdata->power_down();
	if (pdata->reset)
		pdata->reset(GPIO_LOW);

	pgps = pdata;


	printk("%s:rk29 GPS initialized\n",__FUNCTION__);

	return ret;
}

static int rk29_gps_remove(struct platform_device *pdev)
{
	struct rk29_gps_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;

	misc_deregister(&rk29_gps_dev);
	destroy_workqueue(pdata->wq);

	return 0;
}

static struct platform_driver rk29_gps_driver = {
	.probe	= rk29_gps_probe,
	.remove = rk29_gps_remove,
	.suspend  	= rk29_gps_suspend,
	.resume		= rk29_gps_resume,
	.driver	= {
		.name	= "rk29_gps",
		.owner	= THIS_MODULE,
	},
};

static int __init rk29_gps_init(void)
{
	return platform_driver_register(&rk29_gps_driver);
}

static void __exit rk29_gps_exit(void)
{
	platform_driver_unregister(&rk29_gps_driver);
}

module_init(rk29_gps_init);
module_exit(rk29_gps_exit);
MODULE_DESCRIPTION ("rk29 gps driver");
MODULE_LICENSE("GPL");

