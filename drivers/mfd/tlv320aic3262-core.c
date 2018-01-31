/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>

#include <linux/mfd/tlv320aic3262-core.h>
#include <linux/mfd/tlv320aic3262-registers.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#define DEBUG 1
struct aic3262_gpio
{
	unsigned int reg;
	u8 mask;
	u8 shift;
};
struct aic3262_gpio  aic3262_gpio_control[] = {
	{
		.reg = AIC3262_GPIO1_IO_CNTL,
		.mask = AIC3262_GPIO_D6_D2,
		.shift = AIC3262_GPIO_D2_SHIFT,
	},
	{
		.reg = AIC3262_GPIO2_IO_CNTL,
		.mask = AIC3262_GPIO_D6_D2, 
		.shift = AIC3262_GPIO_D2_SHIFT,
	},
	{
		.reg = AIC3262_GPI1_EN,
		.mask = AIC3262_GPI1_D2_D1,
		.shift = AIC3262_GPIO_D1_SHIFT,
	},
	{
		.reg = AIC3262_GPI2_EN,		
		.mask = AIC3262_GPI2_D5_D4,
		.shift = AIC3262_GPIO_D4_SHIFT,
	},
	{
		.reg = AIC3262_GPO1_OUT_CNTL,
		.mask = AIC3262_GPO1_D4_D1,
		.shift = AIC3262_GPIO_D1_SHIFT,
	},
};
static int aic3262_read(struct aic3262 *aic3262, unsigned int reg,
		       int bytes, void *dest)
{
	int ret;
	//u8 *buf = dest;

//	BUG_ON(bytes % 2);
	BUG_ON(bytes <= 0);

	ret = aic3262->read_dev(aic3262, reg, bytes, dest);
	if (ret < 0)
		return ret;

/*	for (i = 0; i < bytes / 2; i++) {
		dev_vdbg(aic3262->dev, "Read %04x from R%d(0x%x)\n",
			 buf[i], reg + i, reg + i);
	}*/

	return ret;
}

/**
 * aic3262_reg_read: Read a single TLV320AIC3262 register.
 *
 * @aic3262: Device to read from.
 * @reg: Register to read.
 */
int aic3262_reg_read(struct aic3262 *aic3262, unsigned int reg)
{
	unsigned char val;
	int ret;

	mutex_lock(&aic3262->io_lock);

	ret = aic3262_read(aic3262, reg, 1, &val);

	mutex_unlock(&aic3262->io_lock);

	if (ret < 0)
		return ret;
	else
		return val;
}
EXPORT_SYMBOL_GPL(aic3262_reg_read);

/**
 * aic3262_bulk_read: Read multiple TLV320AIC3262 registers
 *
 * @aic3262: Device to read from
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to fill.  The data will be returned big endian.
 */
int aic3262_bulk_read(struct aic3262 *aic3262, unsigned int reg,
		     int count, u8 *buf)
{
	int ret;

	mutex_lock(&aic3262->io_lock);

	ret = aic3262_read(aic3262, reg, count, buf);

	mutex_unlock(&aic3262->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(aic3262_bulk_read);

static int aic3262_write(struct aic3262 *aic3262, unsigned int reg,
			int bytes, const void *src)
{
	//const u8 *buf = src;
	

//	BUG_ON(bytes % 2);
	BUG_ON(bytes <= 0);

/*	for (i = 0; i < bytes / 2; i++) {
		dev_vdbg(aic3262->dev, "Write %04x to R%d(0x%x)\n",
			buf[i], reg + i, reg + i);
	}*/

	return aic3262->write_dev(aic3262, reg, bytes, src);
}

/**
 * aic3262_reg_write: Write a single TLV320AIC3262 register.
 *
 * @aic3262: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 */
int aic3262_reg_write(struct aic3262 *aic3262, unsigned int reg,
		     unsigned char val)
{
	int ret;

//	val = cpu_to_be16(val);

	mutex_lock(&aic3262->io_lock);

	ret = aic3262_write(aic3262, reg, 1, &val);

	mutex_unlock(&aic3262->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(aic3262_reg_write);

/**
 * aic3262_bulk_write: Write multiple TLV320AIC3262 registers
 *
 * @aic3262: Device to write to
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to write from.  Data must be big-endian formatted.
 */
int aic3262_bulk_write(struct aic3262 *aic3262, unsigned int reg,
		      int count, const u8 *buf)
{
	int ret;

	mutex_lock(&aic3262->io_lock);

	ret = aic3262_write(aic3262, reg, count, buf);

	mutex_unlock(&aic3262->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(aic3262_bulk_write);

/**
 * aic3262_set_bits: Set the value of a bitfield in a TLV320AIC3262 register
 *
 * @aic3262: Device to write to.
 * @reg: Register to write to.
 * @mask: Mask of bits to set.
 * @val: Value to set (unshifted)
 */
int aic3262_set_bits(struct aic3262 *aic3262, unsigned int reg,
		    unsigned char mask, unsigned char val)
{
	int ret;
	u8 r;

	mutex_lock(&aic3262->io_lock);

	ret = aic3262_read(aic3262, reg, 1, &r);
	if (ret < 0)
		goto out;


	r &= ~mask;
	r |= (val & mask);

	ret = aic3262_write(aic3262, reg, 1, &r);

out:
	mutex_unlock(&aic3262->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(aic3262_set_bits);
/* to be changed -- Mukund*/ 
static struct resource aic3262_codec_resources[] = {
	{
		.start = AIC3262_IRQ_HEADSET_DETECT,
		.end   = AIC3262_IRQ_SPEAKER_OVER_TEMP,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource aic3262_gpio_resources[] = {
	{
		.start = AIC3262_GPIO1,
		.end   = AIC3262_GPO1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell aic3262_devs[] = {
	{
		.name = "tlv320aic3262-codec",
		.num_resources = ARRAY_SIZE(aic3262_codec_resources),
		.resources = aic3262_codec_resources,
	},

	{
		.name = "tlv320aic3262-gpio",
		.num_resources = ARRAY_SIZE(aic3262_gpio_resources),
		.resources = aic3262_gpio_resources,
		.pm_runtime_no_callbacks = true,
	},
};


#ifdef CONFIG_PM
static int aic3262_suspend(struct device *dev)
{
	struct aic3262 *aic3262 = dev_get_drvdata(dev);
	

	/* Don't actually go through with the suspend if the CODEC is
	 * still active (eg, for audio passthrough from CP. */
//	ret = aic3262_reg_read(aic3262, 20AIC3262_POWER_MANAGEMENT_1);
/*	if (ret < 0) {
		dev_err(dev, "Failed to read power status: %d\n", ret);
	} else if (ret & TLV320AIC3262_VMID_SEL_MASK) {
		dev_dbg(dev, "CODEC still active, ignoring suspend\n");
		return 0;
	}
*/
	/* GPIO configuration state is saved here since we may be configuring
	 * the GPIO alternate functions even if we're not using the gpiolib
	 * driver for them.
	 */
//	ret = aic3262_read(aic3262, TLV320AIC3262_GPIO_1, TLV320AIC3262_NUM_GPIO_REGS * 2,
//			  &aic3262->gpio_regs);
/*	if (ret < 0)
		dev_err(dev, "Failed to save GPIO registers: %d\n", ret);*/


	/* Explicitly put the device into reset in case regulators
	 * don't get disabled in order to ensure consistent restart.
	 */
//	aic3262_reg_write(aic3262, TLV320AIC3262_SOFTWARE_RESET, 0x8994);

	aic3262->suspended = true;

	return 0;
}

static int aic3262_resume(struct device *dev)
{
	struct aic3262 *aic3262 = dev_get_drvdata(dev);


	/* We may have lied to the PM core about suspending */
/*	if (!aic3262->suspended)
		return 0;

	ret = aic3262_write(aic3262, TLV320AIC3262_INTERRUPT_STATUS_1_MASK,
			   TLV320AIC3262_NUM_IRQ_REGS * 2, &aic3262->irq_masks_cur);
	if (ret < 0)
		dev_err(dev, "Failed to restore interrupt masks: %d\n", ret);


	ret = aic3262_write(aic3262, TLV320AIC3262_GPIO_1, TLV320AIC3262_NUM_GPIO_REGS * 2,
			   &aic3262->gpio_regs);
	if (ret < 0)
		dev_err(dev, "Failed to restore GPIO registers: %d\n", ret);
*/
	aic3262->suspended = false;

	return 0;
}
#endif


/*
 * Instantiate the generic non-control parts of the device.
 */
static int aic3262_device_init(struct aic3262 *aic3262, int irq)
{
	struct aic3262_pdata *pdata = aic3262->dev->platform_data;
	const char *devname;
	int ret, i;
	u8 revID, pgID;
	unsigned int naudint = 0;
	u8 resetVal = 1;
	//printk("aic3262_device_init beginning\n");

	mutex_init(&aic3262->io_lock);
	dev_set_drvdata(aic3262->dev, aic3262);
	if(pdata){
		if(pdata->gpio_reset){
			ret = gpio_request(pdata->gpio_reset,"aic3262-reset-pin");
			if(ret != 0){
				dev_err(aic3262->dev,"not able to acquire gpio %d for reseting the AIC3262 \n", pdata->gpio_reset);
				goto err_return;
			}
			rk30_mux_api_set(GPIO0B7_I2S8CHSDO3_NAME, GPIO0B_GPIO0B7);  
			gpio_direction_output(pdata->gpio_reset, 1);
			msleep(5);
			gpio_direction_output(pdata->gpio_reset, 0);
			msleep(5);
			gpio_direction_output(pdata->gpio_reset, 1);
//			gpio_set_value(pdata->gpio_reset, 0);
			msleep(5);	
		}	
	}


	/* run the codec through software reset */	
	ret = aic3262_reg_write(aic3262, AIC3262_RESET_REG, resetVal);
	if (ret < 0) {
		dev_err(aic3262->dev, "Could not write to AIC3262 register\n");
		goto err_return;
	}
		
	msleep(10);
		

	ret = aic3262_reg_read(aic3262, AIC3262_REV_PG_ID);
	if (ret < 0) {
		dev_err(aic3262->dev, "Failed to read ID register\n");
		goto err_return;
	}
	revID = (ret & AIC3262_REV_MASK) >> AIC3262_REV_SHIFT;
	pgID = (ret & AIC3262_PG_MASK) >> AIC3262_PG_SHIFT;
	switch (revID ) {
	case 3:
		devname = "TLV320AIC3262";
		if (aic3262->type != TLV320AIC3262)
			dev_warn(aic3262->dev, "Device registered as type %d\n",
				 aic3262->type);
		aic3262->type = TLV320AIC3262;
		break;
	default:
		dev_err(aic3262->dev, "Device is not a TLV320AIC3262, ID is %x\n",
			ret);
		ret = -EINVAL;
		goto err_return;
	
	}

	dev_info(aic3262->dev, "%s revision %c\n", devname, 'D' + ret);

	printk("aic3262_device_init %s revision %c\n", devname, 'D' + ret);

	if (pdata) {
		if(pdata->gpio_irq == 1) {
			naudint = gpio_to_irq(pdata->naudint_irq);
			gpio_request(pdata->naudint_irq,"aic3262-gpio-irq");
			gpio_direction_input(pdata->naudint_irq);
		}
		else {
			naudint = pdata->naudint_irq;
		}
		aic3262->irq = naudint;
		aic3262->irq_base = pdata->irq_base;
		for(i = 0; i < AIC3262_NUM_GPIO;i++)
		{	
			if(pdata->gpio[i].used)
			{
				if(pdata->gpio[i].in) // direction is input
				{	
					// set direction to input for GPIO, and enable for GPI 
					aic3262_set_bits(aic3262, aic3262_gpio_control[i].reg, 
						aic3262_gpio_control[i].mask, 0x1 << aic3262_gpio_control[i].shift); 
					if(pdata->gpio[i].in_reg) // Some input modes, does not need extra registers to be written
						aic3262_set_bits(aic3262, pdata->gpio[i].in_reg, 
						pdata->gpio[i].in_reg_bitmask, 
						pdata->gpio[i].value << pdata->gpio[i].in_reg_shift);	
				}
				else	// direction is output
				{
					
					aic3262_set_bits(aic3262, aic3262_gpio_control[i].reg, 
						aic3262_gpio_control[i].mask, pdata->gpio[i].value << aic3262_gpio_control[i].shift);
				}
			}
			else // Disable the gpio/gpi/gpo
				aic3262_set_bits(aic3262, aic3262_gpio_control[i].reg, aic3262_gpio_control[i].mask, 0x0);
		}	
		

	}


	if(naudint) {
		/* codec interrupt */
		ret = aic3262_irq_init(aic3262);
		if(ret)
			goto err_irq;
	}

	ret = mfd_add_devices(aic3262->dev, -1,
			      aic3262_devs, ARRAY_SIZE(aic3262_devs),
			      NULL, 0);
	if (ret != 0) {
		dev_err(aic3262->dev, "Failed to add children: %d\n", ret);
		goto err_irq;
	}

	printk("aic3262_device_init added mfd devices \n");
	pm_runtime_enable(aic3262->dev);
	pm_runtime_resume(aic3262->dev);

	return 0;

err_irq:
	aic3262_irq_exit(aic3262);
//err:
//	mfd_remove_devices(aic3262->dev);
err_return:
	kfree(aic3262);
	return ret;
}

static void aic3262_device_exit(struct aic3262 *aic3262)
{
	pm_runtime_disable(aic3262->dev);
	mfd_remove_devices(aic3262->dev);
	aic3262_irq_exit(aic3262);
	kfree(aic3262);
}

static int aic3262_i2c_read_device(struct aic3262 *aic3262, unsigned int reg,
				  int bytes, void *dest)
{
	struct i2c_client *i2c = aic3262->control_data;
	aic326x_reg_union *aic_reg = (aic326x_reg_union *) &reg;	
	char *value;
	int ret;
	u8 buf[2];
	u8 page, book, offset;
	page = aic_reg->aic326x_register.page;
	book = aic_reg->aic326x_register.book;
	offset = aic_reg->aic326x_register.offset;
	if(aic3262->book_no != book) // change in book required.
	{
		// We should change to page 0.
		// Change the book by writing to offset 127 of page 0
		// Change the page back to whatever was set before change page
		buf[0] = 0x0;
		buf[1] = 0x0;
		ret = i2c_master_send(i2c, (unsigned char *)buf, 2);
		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
		buf[0] = 127;
		buf[1] = book;
		ret = i2c_master_send(i2c, (unsigned char *)buf, 2);
		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
		aic3262->book_no = book;
		aic3262->page_no = 0x0;// To force a page change in the following if
	}

	if (aic3262->page_no != page) {
		buf[0] = 0x0;
		buf[1] = page;
		ret = i2c_master_send(i2c, (unsigned char *) buf, 2);

		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
		aic3262->page_no = page;
	}
	// Send the required offset 
	buf[0] = offset ;
	ret = i2c_master_send(i2c, (unsigned char *)buf, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	ret = i2c_master_recv(i2c, dest, bytes);
	value = dest; 
	if (ret < 0)
		return ret;
	if (ret != bytes)
		return -EIO;
	return ret;
}

static int aic3262_i2c_write_device(struct aic3262 *aic3262, unsigned int reg,
				   int bytes, const void *src)
{
	struct i2c_client *i2c = aic3262->control_data;
	int ret;
	
	//char *value;
	aic326x_reg_union *aic_reg = (aic326x_reg_union *) &reg;	
	
	//struct i2c_msg xfer[2];
	u8 buf[2];
	u8 write_buf[bytes + 1];
	u8 page, book, offset;
	page = aic_reg->aic326x_register.page;
	book = aic_reg->aic326x_register.book;
	offset = aic_reg->aic326x_register.offset;
	if(aic3262->book_no != book) // change in book required.
	{
		// We should change to page 0.
		// Change the book by writing to offset 127 of page 0
		// Change the page back to whatever was set before change page
		buf[0] = 0x0;
		buf[1] = 0x0;
		ret = i2c_master_send(i2c, (unsigned char *)buf, 2);
		
		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
		buf[0] = 127;
		buf[1] = book;
		ret = i2c_master_send(i2c, (unsigned char *)buf, 2);
		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
		aic3262->book_no = book;
		aic3262->page_no = 0x0;// To force a page change in the following if
	}

	if (aic3262->page_no != page) {
		buf[0] = 0x0;
		buf[1] = page;
		ret = i2c_master_send(i2c, (unsigned char *) buf, 2);
		if (ret < 0)
			return ret;
		if (ret != 2)
			return -EIO;
		aic3262->page_no = page;
	}
//	value = (char *) src;
	//ret = i2c_transfer(i2c->adapter, xfer, 2);
	//printk("%s:write  ret = %d\n",__func__, ret);
	// send the offset as first message
/*	xfer[0].addr = i2c->addr;
	xfer[0].flags = i2c->flags & I2C_M_TEN;
	xfer[0].len = 1;
	xfer[0].buf = (char *)&offset;
	// Send the values in bulk
	xfer[1].addr = i2c->addr;
	xfer[1].flags = i2c->flags & I2C_M_TEN;
	xfer[1].len = bytes;
	xfer[1].buf = (char *)src;

	ret = i2c_transfer(i2c->adapter, xfer, 2);*/
/*	buf[0] = offset;
	memcpy(&buf[1], src, bytes);*/
//	ret = i2c_master_send(i2c, (unsigned char *)buf, 1);
//	printk("%s:write offset ret = %d\n",__func__, ret);
//	ret = i2c_master_send(i2c, (unsigned char *)src, bytes); 
/*	value = (char *) src;
	buf[0] = offset;
	buf[1] = *value; 
	ret = i2c_master_send(i2c, buf, 2); */
	write_buf[0] = offset;
	memcpy(&write_buf[1], src, bytes);
	ret = i2c_master_send(i2c, write_buf, bytes + 1);
	if (ret < 0)
		return ret;
	if (ret != (bytes + 1) )
		return -EIO;

	return 0;
}

static int aic3262_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct aic3262 *aic3262;

	printk("%s: entered \n", __FUNCTION__);
	aic3262 = kzalloc(sizeof(struct aic3262), GFP_KERNEL);
	if (aic3262 == NULL)
		return -ENOMEM;

	printk("%s: allocated memory \n", __FUNCTION__);
	i2c_set_clientdata(i2c, aic3262);
	aic3262->dev = &i2c->dev;
	aic3262->control_data = i2c;
	aic3262->read_dev = aic3262_i2c_read_device;
	aic3262->write_dev = aic3262_i2c_write_device;
//	aic3262->irq = i2c->irq;
	aic3262->type = id->driver_data;
	aic3262->book_no = 255;
	aic3262->page_no = 255;

	return aic3262_device_init(aic3262, i2c->irq);
}

static int aic3262_i2c_remove(struct i2c_client *i2c)
{
	struct aic3262 *aic3262 = i2c_get_clientdata(i2c);

	aic3262_device_exit(aic3262);

	return 0;
}

static const struct i2c_device_id aic3262_i2c_id[] = {
	{ "tlv320aic3262", TLV320AIC3262 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aic3262_i2c_id);

static UNIVERSAL_DEV_PM_OPS(aic3262_pm_ops, aic3262_suspend, aic3262_resume,
			    NULL);

static struct i2c_driver aic3262_i2c_driver = {
	.driver = {
		.name = "tlv320aic3262",
		.owner = THIS_MODULE,
		.pm = &aic3262_pm_ops,
	},
	.probe = aic3262_i2c_probe,
	.remove = aic3262_i2c_remove,
	.id_table = aic3262_i2c_id,
};

static int __init aic3262_i2c_init(void)
{
	int ret;
	printk("aic3262_mfd i2c_init \n");	
	ret = i2c_add_driver(&aic3262_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register aic3262 I2C driver: %d\n", ret);

	return ret;
}
module_init(aic3262_i2c_init);

static void __exit aic3262_i2c_exit(void)
{
	i2c_del_driver(&aic3262_i2c_driver);
}
module_exit(aic3262_i2c_exit);

MODULE_DESCRIPTION("Core support for the TLV320AIC3262 audio CODEC");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mukund Navada <navada@ti.comm>");
