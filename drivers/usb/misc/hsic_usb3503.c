/*
 * hsic_usb3503.c - HSIC to USB for ODROID
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/usb/hsic_usb3503.h>

#include <plat/gpio-cfg.h>
#include <plat/devs.h>
#include <plat/ehci.h>

#define USB3503_VERSION  "1.0.0"
#define USB3503_NAME "usb3503"

extern int s5p_ehci_port_power_on(struct platform_device *pdev);
extern int s5p_ohci_port_power_on(struct platform_device *pdev);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/*
 * driver private data
 */
struct usb3503_chip {
	struct i2c_client *i2c;
	struct usb3503_platform_data *pdata;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static irqreturn_t irq_handler(int irq, void *dev_id)
{
	printk(" %s [%d]...\n",__func__,__LINE__);
	return IRQ_HANDLED;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int usb3503_hw_init(struct usb3503_chip *chip)
{
	struct i2c_client *client = chip->i2c;
	unsigned val=0;

	gpio_request(chip->pdata->irq_gpio, "irq_gpio");
	gpio_request(chip->pdata->gpio_hub_con, "gpio_hub_con");
	gpio_request(chip->pdata->gpio_reset, "gpio_reset");
	s3c_gpio_setpull(chip->pdata->gpio_reset, S3C_GPIO_PULL_NONE);

	/* Start */
	gpio_direction_output(chip->pdata->gpio_reset, 0);
	mdelay(1);
#if defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)
	/* RefCLK 24MHz */
	gpio_direction_output(chip->pdata->irq_gpio, 0); 
#else
	/* RefCLK 26MHz */
	gpio_direction_output(chip->pdata->irq_gpio, 1);
#endif
	gpio_direction_output(chip->pdata->gpio_hub_con, 0);
		mdelay(10);
		gpio_set_value(chip->pdata->gpio_reset, 1);

	// Hub Wait RefClk stage
	mdelay(10);

	// Hub Configuration Stage
	val = i2c_smbus_read_byte_data(client, USB3503_SP_ILOCK);
	printk("%s : read val = 0x%02x\n",__func__,val);
	i2c_smbus_write_byte_data(client, USB3503_INT_MASK, 0x0C); 

	// SMSC VID/PID/DID
	i2c_smbus_write_byte_data(client, USB3503_VIDL, 0x24); 
	i2c_smbus_write_byte_data(client, USB3503_VIDM, 0x04); 
	i2c_smbus_write_byte_data(client, USB3503_PIDL, 0x03); 
	i2c_smbus_write_byte_data(client, USB3503_PIDM, 0x35); 
	i2c_smbus_write_byte_data(client, USB3503_DIDL, 0xA0); 
	i2c_smbus_write_byte_data(client, USB3503_DIDM, 0xA1); 

	i2c_smbus_write_byte_data(client, USB3503_CFG1, 0x80); 
	i2c_smbus_write_byte_data(client, USB3503_CFG2, 0x28); 
	i2c_smbus_write_byte_data(client, USB3503_CFG3, 0x03); 

	i2c_smbus_write_byte_data(client, USB3503_NRD, 	0x04); 
//	i2c_smbus_write_byte_data(client, USB3503_PDS, 	0x00); 
//	i2c_smbus_write_byte_data(client, USB3503_PDB, 	0x0E); 

//	i2c_smbus_write_byte_data(client, USB3503_MAXPS, 	0xFA); 
//	i2c_smbus_write_byte_data(client, USB3503_MAXPB, 	0x00); 
//	i2c_smbus_write_byte_data(client, USB3503_HCMCS, 	0x32); 
//	i2c_smbus_write_byte_data(client, USB3503_HCMCB, 	0x00); 
//	i2c_smbus_write_byte_data(client, USB3503_PWRT, 	0x00); 

//	i2c_smbus_write_byte_data(client, USB3503_LANGIDH, 	0x04); 
//	i2c_smbus_write_byte_data(client, USB3503_LANGIDL, 	0x09); 

//	i2c_smbus_write_byte_data(client, USB3503_OCS, 		0x00); 

	i2c_smbus_write_byte_data(client, USB3503_VSNSUP3, 0x06); 
	i2c_smbus_write_byte_data(client, USB3503_VSNS21, 0x66); 
	i2c_smbus_write_byte_data(client, USB3503_BSTUP3, 0x06); 
	i2c_smbus_write_byte_data(client, USB3503_BST21, 0x66); 

//	i2c_smbus_write_byte_data(client, USB3503_PRTSP, 	0x02); 

//	i2c_smbus_write_byte_data(client, USB3503_SP_ILOCK, 0x32); //Device will remain Hub Mode, PRTPWR Output

//	i2c_smbus_write_byte_data(client, USB3503_MFRSL, 1); 
//	i2c_smbus_write_byte_data(client, USB3503_PRDSL, 1); 
//	i2c_smbus_write_byte_data(client, USB3503_SERSL, 1); 
//	i2c_smbus_write_byte_data(client, USB3503_MANSTR, 'a'); 
//	i2c_smbus_write_byte_data(client, USB3503_PRDSTR, 'b'); 
//	i2c_smbus_write_byte_data(client, USB3503_SERSTR, 'c'); 

	mdelay(100);

	gpio_set_value(chip->pdata->gpio_hub_con, 1);
	mdelay(10);

	i2c_smbus_write_byte_data(client, USB3503_SP_ILOCK, val|0x01); //Device will remain Hub Mode, PRTPWR Output

	gpio_direction_input(chip->pdata->irq_gpio);
	gpio_free(chip->pdata->gpio_hub_con);
	gpio_free(chip->pdata->gpio_reset);
	gpio_free(chip->pdata->irq_gpio);

	return 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int usb3503_i2c_probe (struct i2c_client *client, const struct i2c_device_id *id)
{
	struct usb3503_chip *chip;
	struct device 	*dev = &client->dev;
	int ret=0;
	
	/* setup i2c client */
	if (!i2c_check_functionality (client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c byte data not supported\n");
		return -EIO;
	}
	if (!client->dev.platform_data) {
		dev_err(&client->dev, "pdata is not available\n");
		return -EINVAL;
	}

	chip = kzalloc (sizeof (struct usb3503_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->pdata = client->dev.platform_data;
	chip->i2c = client;
	i2c_set_clientdata (client, chip);

	usb3503_hw_init(chip);

	ret = request_irq(chip->pdata->sys_irq, irq_handler, IRQF_DISABLED, "usb3503-irq", NULL);
	if (ret < 0) {
		printk(KERN_ERR "USB3503 : Failed to register sys_irq.\n");
		return -EIO;
	}
	irq_set_irq_type(chip->pdata->sys_irq, IRQ_TYPE_EDGE_BOTH);

	dev_set_drvdata(dev, chip);

	return 0;
	
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int usb3503_i2c_remove (struct i2c_client *client)
{
	struct usb3503_chip *usb3503 = i2c_get_clientdata (client);
	
	i2c_set_clientdata (client, NULL);
	kfree (usb3503);
	
	return 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
static void usb3503_early_suspend(struct early_suspend *h)
{
	return;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void usb3503_late_resume(struct early_suspend *h)
{
	return;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static struct early_suspend usb3503_early_suspend_desc = {
     .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
     .suspend = usb3503_early_suspend,
     .resume = usb3503_late_resume
};
#endif //CONFIG_HAS_EARLYSUSPEND
#endif //CONFIG_PM

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static const struct i2c_device_id usb3503_id[] = {
  {USB3503_NAME, 0},
  {},
};
MODULE_DEVICE_TABLE (i2c, usb3503_id);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
struct i2c_driver usb3503_driver = {
	.driver = {
	     .name = USB3503_NAME,
	     .owner = THIS_MODULE,
     },
	.probe =    usb3503_i2c_probe,
	.remove =   usb3503_i2c_remove,
	.id_table = usb3503_id,
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static int __init usb3503_init (void)
{
	int ret;

	ret = i2c_add_driver (&usb3503_driver);
	if (ret) {
		printk(KERN_ERR "i2c add driver Failed %d\n", ret);
		return -1;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&usb3503_early_suspend_desc);
#endif
	return 0;
}
module_init (usb3503_init);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static void __exit usb3503_exit (void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&usb3503_early_suspend_desc);
#endif
	i2c_del_driver (&usb3503_driver);
}
module_exit (usb3503_exit);


MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("USB3503 linear virbrator driver");
MODULE_VERSION (USB3503_VERSION);
