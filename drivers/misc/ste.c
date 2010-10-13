#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/ste.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include <mach/spi_fpga.h>
#include <linux/delay.h>

#if 1
#define D(x...) printk(x)
#else
#define D(x...)
#endif

static int misc_opened;

#define AP_TD_UNDEFINED_GBIN5		FPGA_PIO2_02
#define AP_RESET_TD 				FPGA_PIO2_04
#define AP_SHUTDOWN_TD_PMU 		FPGA_PIO2_05
#define AP_PW_EN_TD 				FPGA_PIO2_03

#define PIN_BPSEND_ACK				RK2818_PIN_PE0
#define PIN_APSEND_ACK				RK2818_PIN_PF7

static int bp_power_on(void)
{
	int ret=0;
	
	ret = gpio_request(AP_TD_UNDEFINED_GBIN5, NULL);
	if (ret) {
		printk("%s:failed to request fpga s %d\n",__FUNCTION__,__LINE__);
		goto err;
	}
	ret = gpio_request(AP_RESET_TD, NULL);
	if (ret) {
		printk("%s:failed to request fpga s %d\n",__FUNCTION__,__LINE__);
		goto err0;
	}
	

	ret = gpio_request(AP_SHUTDOWN_TD_PMU, NULL);
	if (ret) {
		printk("%s:failed to request fpga %d\n",__FUNCTION__,__LINE__);
		goto err1;
	}

	ret = gpio_request(AP_PW_EN_TD, NULL);
	if (ret) {
		printk("%s:failed to request fpga  %d\n",__FUNCTION__,__LINE__);
		goto err2;
	}

	gpio_set_value(AP_TD_UNDEFINED_GBIN5, 1);
       gpio_direction_output(AP_TD_UNDEFINED_GBIN5, 1);   
	gpio_direction_input(AP_RESET_TD);

	 gpio_set_value(AP_SHUTDOWN_TD_PMU, 0);
        gpio_direction_output(AP_SHUTDOWN_TD_PMU, 0);  

	gpio_set_value(AP_PW_EN_TD, 0);
	gpio_direction_output(AP_PW_EN_TD, 0);  
	mdelay(1);
	gpio_set_value(AP_PW_EN_TD, 1);
	mdelay(1200);
	gpio_set_value(AP_PW_EN_TD, 0);

	return true;
err2:
	gpio_free(AP_SHUTDOWN_TD_PMU);
err1:
	gpio_free(AP_RESET_TD);
err0:
	gpio_free(AP_TD_UNDEFINED_GBIN5);
err:	
	return false;
}



static int bp_power_off(void)
{
	D("+++--++++++%s_________ \r\n",__FUNCTION__);

	 gpio_set_value(AP_TD_UNDEFINED_GBIN5, 0);
	
	gpio_set_value(AP_PW_EN_TD, 0);
	//gpio_direction_output(AP_PW_EN_TD, 0);  
	mdelay(1);
	gpio_set_value(AP_PW_EN_TD, 1);
	mdelay(1200);
	gpio_set_value(AP_PW_EN_TD, 0);

	mdelay(5000);
	 gpio_set_value(AP_SHUTDOWN_TD_PMU, 1);
	mdelay(1200);
	// gpio_free(AP_PW_EN_TD);
	D("++++--+++++%s   ok_________\r\n",__FUNCTION__);
	 return 0;
}
//add end

static int ste_open(struct inode *inode, struct file *file)
{
	D("%s\n", __func__);
	if (misc_opened)
		return -EBUSY;
	misc_opened = 1;
	return 0;
}

static int ste_release(struct inode *inode, struct file *file)
{
	D("%s\n", __func__);
	misc_opened = 0;
	return 0;
}

static int ste_ioctl(struct inode *inode,struct file *file, unsigned int cmd, unsigned long arg)
{
	int val;
	D("%s cmd %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case STE_IOCTL_EN_APSEND_ACK:
		D("%s:STE_IOCTL_EN_APSEND_ACK\n");
		gpio_direction_output(PIN_APSEND_ACK,GPIO_LOW);
		msleep(50); 
		gpio_direction_output(PIN_APSEND_ACK,GPIO_HIGH);
		msleep(50); 
		break;
	case STE_IOCTL_GET_ACK:
		val = gpio_get_value(PIN_BPSEND_ACK);
		D("%s:STE_IOCTL_GET_ACK pin status is %d\n",__func__,val);
		return put_user(val, (unsigned long __user *)arg);
		break;
	case STE_IOCTL_POWER_ON:
		D("%s:STE_IOCTL_POWER_ON\n",__func__);
		bp_power_on();
		break;
	case STE_IOCTL_POWER_OFF:
		D("%s:STE_IOCTL_POWER_OFF\n",__func__);
		bp_power_off();
		break;
	default:
		pr_err("%s: invalid cmd %d\n", __func__, _IOC_NR(cmd));
		return -EINVAL;
	}
	return 0;
}

static struct file_operations ste_fops = {
	.owner = THIS_MODULE,
	.open = ste_open,
	.release = ste_release,
	.ioctl = ste_ioctl
};

static struct miscdevice ste_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = STE_NAME,
	.fops = &ste_fops
};

static int ste_probe(struct platform_device *pdev)
{
	int rc = -EIO;
	D("%s-----------\n",__FUNCTION__);

	rc = misc_register(&ste_misc);
	if (rc < 0) {
		pr_err("%s: could not register misc device\n", __func__);
	//	goto err_unregister_input_device;
	}
	return rc;
}

static struct platform_driver ste_driver = {
	.probe = ste_probe,
	.driver = {
		.name = "ste",
		.owner = THIS_MODULE
	},
};

static int __init ste_init(void)
{
	return platform_driver_register(&ste_driver);
}

static void __exit ste_exit(void)
{
	platform_driver_unregister(&ste_driver);
}

module_init(ste_init);
module_exit(ste_exit);
