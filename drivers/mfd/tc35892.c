/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 * Author: Hanumath Prasad <hanumath.prasad@stericsson.com> for ST-Ericsson
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tc35892.h>

/**
 * tc35892_reg_read() - read a single TC35892 register
 * @tc35892:	Device to read from
 * @reg:	Register to read
 */
int tc35892_reg_read(struct tc35892 *tc35892, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(tc35892->i2c, reg);
	if (ret < 0)
		dev_err(tc35892->dev, "failed to read reg %#x: %d\n",
			reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(tc35892_reg_read);

/**
 * tc35892_reg_read() - write a single TC35892 register
 * @tc35892:	Device to write to
 * @reg:	Register to read
 * @data:	Value to write
 */
int tc35892_reg_write(struct tc35892 *tc35892, u8 reg, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(tc35892->i2c, reg, data);
	if (ret < 0)
		dev_err(tc35892->dev, "failed to write reg %#x: %d\n",
			reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(tc35892_reg_write);

/**
 * tc35892_block_read() - read multiple TC35892 registers
 * @tc35892:	Device to read from
 * @reg:	First register
 * @length:	Number of registers
 * @values:	Buffer to write to
 */
int tc35892_block_read(struct tc35892 *tc35892, u8 reg, u8 length, u8 *values)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(tc35892->i2c, reg, length, values);
	if (ret < 0)
		dev_err(tc35892->dev, "failed to read regs %#x: %d\n",
			reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(tc35892_block_read);

/**
 * tc35892_block_write() - write multiple TC35892 registers
 * @tc35892:	Device to write to
 * @reg:	First register
 * @length:	Number of registers
 * @values:	Values to write
 */
int tc35892_block_write(struct tc35892 *tc35892, u8 reg, u8 length,
			const u8 *values)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(tc35892->i2c, reg, length,
					     values);
	if (ret < 0)
		dev_err(tc35892->dev, "failed to write regs %#x: %d\n",
			reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(tc35892_block_write);

/**
 * tc35892_set_bits() - set the value of a bitfield in a TC35892 register
 * @tc35892:	Device to write to
 * @reg:	Register to write
 * @mask:	Mask of bits to set
 * @values:	Value to set
 */
int tc35892_set_bits(struct tc35892 *tc35892, u8 reg, u8 mask, u8 val)
{
	int ret;

	mutex_lock(&tc35892->lock);

	ret = tc35892_reg_read(tc35892, reg);
	if (ret < 0)
		goto out;

	ret &= ~mask;
	ret |= val;

	ret = tc35892_reg_write(tc35892, reg, ret);

out:
	mutex_unlock(&tc35892->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tc35892_set_bits);

static struct resource gpio_resources[] = {
	{
		.start	= TC35892_INT_GPIIRQ,
		.end	= TC35892_INT_GPIIRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell tc35892_devs[] = {
	{
		.name		= "tc35892-gpio",
		.num_resources	= ARRAY_SIZE(gpio_resources),
		.resources	= &gpio_resources[0],
	},
};

static irqreturn_t tc35892_irq(int irq, void *data)
{
	struct tc35892 *tc35892 = data;
	int status;

	status = tc35892_reg_read(tc35892, TC35892_IRQST);
	if (status < 0)
		return IRQ_NONE;

	while (status) {
		int bit = __ffs(status);

		handle_nested_irq(tc35892->irq_base + bit);
		status &= ~(1 << bit);
	}

	/*
	 * A dummy read or write (to any register) appears to be necessary to
	 * have the last interrupt clear (for example, GPIO IC write) take
	 * effect.
	 */
	tc35892_reg_read(tc35892, TC35892_IRQST);

	return IRQ_HANDLED;
}

static void tc35892_irq_dummy(unsigned int irq)
{
	/* No mask/unmask at this level */
}

static struct irq_chip tc35892_irq_chip = {
	.name	= "tc35892",
	.mask	= tc35892_irq_dummy,
	.unmask	= tc35892_irq_dummy,
};

static int tc35892_irq_init(struct tc35892 *tc35892)
{
	int base = tc35892->irq_base;
	int irq;

	for (irq = base; irq < base + TC35892_NR_INTERNAL_IRQS; irq++) {
		set_irq_chip_data(irq, tc35892);
		set_irq_chip_and_handler(irq, &tc35892_irq_chip,
					 handle_edge_irq);
		set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		set_irq_noprobe(irq);
#endif
	}

	return 0;
}

static void tc35892_irq_remove(struct tc35892 *tc35892)
{
	int base = tc35892->irq_base;
	int irq;

	for (irq = base; irq < base + TC35892_NR_INTERNAL_IRQS; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		set_irq_chip_and_handler(irq, NULL, NULL);
		set_irq_chip_data(irq, NULL);
	}
}

static int tc35892_chip_init(struct tc35892 *tc35892)
{
	int manf, ver, ret;

	manf = tc35892_reg_read(tc35892, TC35892_MANFCODE);
	if (manf < 0)
		return manf;

	ver = tc35892_reg_read(tc35892, TC35892_VERSION);
	if (ver < 0)
		return ver;

	if (manf != TC35892_MANFCODE_MAGIC) {
		dev_err(tc35892->dev, "unknown manufacturer: %#x\n", manf);
		return -EINVAL;
	}

	dev_info(tc35892->dev, "manufacturer: %#x, version: %#x\n", manf, ver);

	/* Put everything except the IRQ module into reset */
	ret = tc35892_reg_write(tc35892, TC35892_RSTCTRL,
				TC35892_RSTCTRL_TIMRST
				| TC35892_RSTCTRL_ROTRST
				| TC35892_RSTCTRL_KBDRST
				| TC35892_RSTCTRL_GPIRST);
	if (ret < 0)
		return ret;

	/* Clear the reset interrupt. */
	return tc35892_reg_write(tc35892, TC35892_RSTINTCLR, 0x1);
}

static int __devinit tc35892_probe(struct i2c_client *i2c,
				   const struct i2c_device_id *id)
{
	struct tc35892_platform_data *pdata = i2c->dev.platform_data;
	struct tc35892 *tc35892;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA
				     | I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	tc35892 = kzalloc(sizeof(struct tc35892), GFP_KERNEL);
	if (!tc35892)
		return -ENOMEM;

	mutex_init(&tc35892->lock);

	tc35892->dev = &i2c->dev;
	tc35892->i2c = i2c;
	tc35892->pdata = pdata;
	tc35892->irq_base = pdata->irq_base;
	tc35892->num_gpio = id->driver_data;

	i2c_set_clientdata(i2c, tc35892);

	ret = tc35892_chip_init(tc35892);
	if (ret)
		goto out_free;

	ret = tc35892_irq_init(tc35892);
	if (ret)
		goto out_free;

	ret = request_threaded_irq(tc35892->i2c->irq, NULL, tc35892_irq,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "tc35892", tc35892);
	if (ret) {
		dev_err(tc35892->dev, "failed to request IRQ: %d\n", ret);
		goto out_removeirq;
	}

	ret = mfd_add_devices(tc35892->dev, -1, tc35892_devs,
			      ARRAY_SIZE(tc35892_devs), NULL,
			      tc35892->irq_base);
	if (ret) {
		dev_err(tc35892->dev, "failed to add children\n");
		goto out_freeirq;
	}

	return 0;

out_freeirq:
	free_irq(tc35892->i2c->irq, tc35892);
out_removeirq:
	tc35892_irq_remove(tc35892);
out_free:
	kfree(tc35892);
	return ret;
}

static int __devexit tc35892_remove(struct i2c_client *client)
{
	struct tc35892 *tc35892 = i2c_get_clientdata(client);

	mfd_remove_devices(tc35892->dev);

	free_irq(tc35892->i2c->irq, tc35892);
	tc35892_irq_remove(tc35892);

	kfree(tc35892);

	return 0;
}

static const struct i2c_device_id tc35892_id[] = {
	{ "tc35892", 24 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tc35892_id);

static struct i2c_driver tc35892_driver = {
	.driver.name	= "tc35892",
	.driver.owner	= THIS_MODULE,
	.probe		= tc35892_probe,
	.remove		= __devexit_p(tc35892_remove),
	.id_table	= tc35892_id,
};

static int __init tc35892_init(void)
{
	return i2c_add_driver(&tc35892_driver);
}
subsys_initcall(tc35892_init);

static void __exit tc35892_exit(void)
{
	i2c_del_driver(&tc35892_driver);
}
module_exit(tc35892_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TC35892 MFD core driver");
MODULE_AUTHOR("Hanumath Prasad, Rabin Vincent");
