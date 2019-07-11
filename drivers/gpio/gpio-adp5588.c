/*
 * GPIO Chip driver for Analog Devices
 * ADP5588/ADP5587 I/O Expander and QWERTY Keypad Controller
 *
 * Copyright 2009-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_device.h>

#include <linux/platform_data/adp5588.h>

#define DRV_NAME	"adp5588-gpio"

/*
 * Early pre 4.0 Silicon required to delay readout by at least 25ms,
 * since the Event Counter Register updated 25ms after the interrupt
 * asserted.
 */
#define WA_DELAYED_READOUT_REVID(rev)	((rev) < 4)

struct adp5588_gpio {
	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	struct mutex lock;	/* protect cached dir, dat_out */
	/* protect serialized access to the interrupt controller bus */
	struct mutex irq_lock;
	uint8_t dat_out[3];
	uint8_t dir[3];
	uint8_t int_lvl_low[3];
	uint8_t int_lvl_high[3];
	uint8_t int_en[3];
	uint8_t irq_mask[3];
	uint8_t int_input_en[3];
};

static int adp5588_gpio_read(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "Read Error\n");

	return ret;
}

static int adp5588_gpio_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "Write Error\n");

	return ret;
}

static int adp5588_gpio_get_value(struct gpio_chip *chip, unsigned off)
{
	struct adp5588_gpio *dev = gpiochip_get_data(chip);
	unsigned bank = ADP5588_BANK(off);
	unsigned bit = ADP5588_BIT(off);
	int val;

	mutex_lock(&dev->lock);

	if (dev->dir[bank] & bit)
		val = dev->dat_out[bank];
	else
		val = adp5588_gpio_read(dev->client, GPIO_DAT_STAT1 + bank);

	mutex_unlock(&dev->lock);

	return !!(val & bit);
}

static void adp5588_gpio_set_value(struct gpio_chip *chip,
				   unsigned off, int val)
{
	unsigned bank, bit;
	struct adp5588_gpio *dev = gpiochip_get_data(chip);

	bank = ADP5588_BANK(off);
	bit = ADP5588_BIT(off);

	mutex_lock(&dev->lock);
	if (val)
		dev->dat_out[bank] |= bit;
	else
		dev->dat_out[bank] &= ~bit;

	adp5588_gpio_write(dev->client, GPIO_DAT_OUT1 + bank,
			   dev->dat_out[bank]);
	mutex_unlock(&dev->lock);
}

static int adp5588_gpio_direction_input(struct gpio_chip *chip, unsigned off)
{
	int ret;
	unsigned bank;
	struct adp5588_gpio *dev = gpiochip_get_data(chip);

	bank = ADP5588_BANK(off);

	mutex_lock(&dev->lock);
	dev->dir[bank] &= ~ADP5588_BIT(off);
	ret = adp5588_gpio_write(dev->client, GPIO_DIR1 + bank, dev->dir[bank]);
	mutex_unlock(&dev->lock);

	return ret;
}

static int adp5588_gpio_direction_output(struct gpio_chip *chip,
					 unsigned off, int val)
{
	int ret;
	unsigned bank, bit;
	struct adp5588_gpio *dev = gpiochip_get_data(chip);

	bank = ADP5588_BANK(off);
	bit = ADP5588_BIT(off);

	mutex_lock(&dev->lock);
	dev->dir[bank] |= bit;

	if (val)
		dev->dat_out[bank] |= bit;
	else
		dev->dat_out[bank] &= ~bit;

	ret = adp5588_gpio_write(dev->client, GPIO_DAT_OUT1 + bank,
				 dev->dat_out[bank]);
	ret |= adp5588_gpio_write(dev->client, GPIO_DIR1 + bank,
				 dev->dir[bank]);
	mutex_unlock(&dev->lock);

	return ret;
}

#ifdef CONFIG_GPIO_ADP5588_IRQ

static void adp5588_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_gpio *dev = gpiochip_get_data(gc);

	mutex_lock(&dev->irq_lock);
}

 /*
  * genirq core code can issue chip->mask/unmask from atomic context.
  * This doesn't work for slow busses where an access needs to sleep.
  * bus_sync_unlock() is therefore called outside the atomic context,
  * syncs the current irq mask state with the slow external controller
  * and unlocks the bus.
  */

static void adp5588_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_gpio *dev = gpiochip_get_data(gc);
	int i;

	for (i = 0; i <= ADP5588_BANK(ADP5588_MAXGPIO); i++) {
		if (dev->int_input_en[i]) {
			mutex_lock(&dev->lock);
			dev->dir[i] &= ~dev->int_input_en[i];
			dev->int_input_en[i] = 0;
			adp5588_gpio_write(dev->client, GPIO_DIR1 + i,
					   dev->dir[i]);
			mutex_unlock(&dev->lock);
		}

		if (dev->int_en[i] ^ dev->irq_mask[i]) {
			dev->int_en[i] = dev->irq_mask[i];
			adp5588_gpio_write(dev->client, GPI_EM1 + i,
					   dev->int_en[i]);
		}
	}

	mutex_unlock(&dev->irq_lock);
}

static void adp5588_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_gpio *dev = gpiochip_get_data(gc);

	dev->irq_mask[ADP5588_BANK(d->hwirq)] &= ~ADP5588_BIT(d->hwirq);
}

static void adp5588_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_gpio *dev = gpiochip_get_data(gc);

	dev->irq_mask[ADP5588_BANK(d->hwirq)] |= ADP5588_BIT(d->hwirq);
}

static int adp5588_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_gpio *dev = gpiochip_get_data(gc);
	uint16_t gpio = d->hwirq;
	unsigned bank, bit;

	bank = ADP5588_BANK(gpio);
	bit = ADP5588_BIT(gpio);

	dev->int_lvl_low[bank] &= ~bit;
	dev->int_lvl_high[bank] &= ~bit;

	if (type & IRQ_TYPE_EDGE_BOTH || type & IRQ_TYPE_LEVEL_HIGH)
		dev->int_lvl_high[bank] |= bit;

	if (type & IRQ_TYPE_EDGE_BOTH || type & IRQ_TYPE_LEVEL_LOW)
		dev->int_lvl_low[bank] |= bit;

	dev->int_input_en[bank] |= bit;

	return 0;
}

static struct irq_chip adp5588_irq_chip = {
	.name			= "adp5588",
	.irq_mask		= adp5588_irq_mask,
	.irq_unmask		= adp5588_irq_unmask,
	.irq_bus_lock		= adp5588_irq_bus_lock,
	.irq_bus_sync_unlock	= adp5588_irq_bus_sync_unlock,
	.irq_set_type		= adp5588_irq_set_type,
};

static irqreturn_t adp5588_irq_handler(int irq, void *devid)
{
	struct adp5588_gpio *dev = devid;
	int status = adp5588_gpio_read(dev->client, INT_STAT);

	if (status & ADP5588_KE_INT) {
		int ev_cnt = adp5588_gpio_read(dev->client, KEY_LCK_EC_STAT);

		if (ev_cnt > 0) {
			int i;

			for (i = 0; i < (ev_cnt & ADP5588_KEC); i++) {
				int key = adp5588_gpio_read(dev->client,
							    Key_EVENTA + i);
				/* GPIN events begin at 97,
				 * bit 7 indicates logic level
				 */
				int gpio = (key & 0x7f) - 97;
				int lvl = key & (1 << 7);
				int bank = ADP5588_BANK(gpio);
				int bit = ADP5588_BIT(gpio);

				if ((lvl && dev->int_lvl_high[bank] & bit) ||
				    (!lvl && dev->int_lvl_low[bank] & bit))
					handle_nested_irq(irq_find_mapping(
					      dev->gpio_chip.irq.domain, gpio));
			}
		}
	}

	adp5588_gpio_write(dev->client, INT_STAT, status); /* Status is W1C */

	return IRQ_HANDLED;
}

static int adp5588_irq_setup(struct adp5588_gpio *dev)
{
	struct i2c_client *client = dev->client;
	int ret;
	struct adp5588_gpio_platform_data *pdata =
			dev_get_platdata(&client->dev);
	int irq_base = pdata ? pdata->irq_base : 0;

	adp5588_gpio_write(client, CFG, ADP5588_AUTO_INC);
	adp5588_gpio_write(client, INT_STAT, -1); /* status is W1C */

	mutex_init(&dev->irq_lock);

	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, adp5588_irq_handler, IRQF_ONESHOT
					| IRQF_TRIGGER_FALLING | IRQF_SHARED,
					dev_name(&client->dev), dev);
	if (ret) {
		dev_err(&client->dev, "failed to request irq %d\n",
			client->irq);
		return ret;
	}
	ret = gpiochip_irqchip_add_nested(&dev->gpio_chip,
					  &adp5588_irq_chip, irq_base,
					  handle_simple_irq,
					  IRQ_TYPE_NONE);
	if (ret) {
		dev_err(&client->dev,
			"could not connect irqchip to gpiochip\n");
		return ret;
	}
	gpiochip_set_nested_irqchip(&dev->gpio_chip,
				    &adp5588_irq_chip,
				    client->irq);

	adp5588_gpio_write(client, CFG,
		ADP5588_AUTO_INC | ADP5588_INT_CFG | ADP5588_KE_IEN);

	return 0;
}

#else
static int adp5588_irq_setup(struct adp5588_gpio *dev)
{
	struct i2c_client *client = dev->client;
	dev_warn(&client->dev, "interrupt support not compiled in\n");

	return 0;
}

#endif /* CONFIG_GPIO_ADP5588_IRQ */

static int adp5588_gpio_probe(struct i2c_client *client)
{
	struct adp5588_gpio_platform_data *pdata =
			dev_get_platdata(&client->dev);
	struct adp5588_gpio *dev;
	struct gpio_chip *gc;
	int ret, i, revid;
	unsigned int pullup_dis_mask = 0;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->client = client;

	gc = &dev->gpio_chip;
	gc->direction_input = adp5588_gpio_direction_input;
	gc->direction_output = adp5588_gpio_direction_output;
	gc->get = adp5588_gpio_get_value;
	gc->set = adp5588_gpio_set_value;
	gc->can_sleep = true;
	gc->base = -1;
	gc->parent = &client->dev;

	if (pdata) {
		gc->base = pdata->gpio_start;
		gc->names = pdata->names;
		pullup_dis_mask = pdata->pullup_dis_mask;
	}

	gc->ngpio = ADP5588_MAXGPIO;
	gc->label = client->name;
	gc->owner = THIS_MODULE;

	mutex_init(&dev->lock);

	ret = adp5588_gpio_read(dev->client, DEV_ID);
	if (ret < 0)
		return ret;

	revid = ret & ADP5588_DEVICE_ID_MASK;

	for (i = 0, ret = 0; i <= ADP5588_BANK(ADP5588_MAXGPIO); i++) {
		dev->dat_out[i] = adp5588_gpio_read(client, GPIO_DAT_OUT1 + i);
		dev->dir[i] = adp5588_gpio_read(client, GPIO_DIR1 + i);
		ret |= adp5588_gpio_write(client, KP_GPIO1 + i, 0);
		ret |= adp5588_gpio_write(client, GPIO_PULL1 + i,
				(pullup_dis_mask >> (8 * i)) & 0xFF);
		ret |= adp5588_gpio_write(client, GPIO_INT_EN1 + i, 0);
		if (ret)
			return ret;
	}

	if (client->irq) {
		if (WA_DELAYED_READOUT_REVID(revid)) {
			dev_warn(&client->dev, "GPIO int not supported\n");
		} else {
			ret = adp5588_irq_setup(dev);
			if (ret)
				return ret;
		}
	}

	ret = devm_gpiochip_add_data(&client->dev, &dev->gpio_chip, dev);
	if (ret)
		return ret;

	if (pdata && pdata->setup) {
		ret = pdata->setup(client, gc->base, gc->ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, dev);

	return 0;
}

static int adp5588_gpio_remove(struct i2c_client *client)
{
	struct adp5588_gpio_platform_data *pdata =
			dev_get_platdata(&client->dev);
	struct adp5588_gpio *dev = i2c_get_clientdata(client);
	int ret;

	if (pdata && pdata->teardown) {
		ret = pdata->teardown(client,
				      dev->gpio_chip.base, dev->gpio_chip.ngpio,
				      pdata->context);
		if (ret < 0) {
			dev_err(&client->dev, "teardown failed %d\n", ret);
			return ret;
		}
	}

	if (dev->client->irq)
		free_irq(dev->client->irq, dev);

	return 0;
}

static const struct i2c_device_id adp5588_gpio_id[] = {
	{DRV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, adp5588_gpio_id);

#ifdef CONFIG_OF
static const struct of_device_id adp5588_gpio_of_id[] = {
	{ .compatible = "adi," DRV_NAME, },
	{},
};
MODULE_DEVICE_TABLE(of, adp5588_gpio_of_id);
#endif

static struct i2c_driver adp5588_gpio_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(adp5588_gpio_of_id),
	},
	.probe_new = adp5588_gpio_probe,
	.remove = adp5588_gpio_remove,
	.id_table = adp5588_gpio_id,
};

module_i2c_driver(adp5588_gpio_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("GPIO ADP5588 Driver");
MODULE_LICENSE("GPL");
