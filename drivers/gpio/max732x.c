/*
 *  max732x.c - I2C Port Expander with 8/16 I/O
 *
 *  Copyright (C) 2007 Marvell International Ltd.
 *  Copyright (C) 2008 Jack Ren <jack.ren@marvell.com>
 *  Copyright (C) 2008 Eric Miao <eric.miao@marvell.com>
 *
 *  Derived from drivers/gpio/pca953x.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/i2c/max732x.h>


/*
 * Each port of MAX732x (including MAX7319) falls into one of the
 * following three types:
 *
 *   - Push Pull Output
 *   - Input
 *   - Open Drain I/O
 *
 * designated by 'O', 'I' and 'P' individually according to MAXIM's
 * datasheets. 'I' and 'P' ports are interrupt capables, some with
 * a dedicated interrupt mask.
 *
 * There are two groups of I/O ports, each group usually includes
 * up to 8 I/O ports, and is accessed by a specific I2C address:
 *
 *   - Group A : by I2C address 0b'110xxxx
 *   - Group B : by I2C address 0b'101xxxx
 *
 * where 'xxxx' is decided by the connections of pin AD2/AD0.  The
 * address used also affects the initial state of output signals.
 *
 * Within each group of ports, there are five known combinations of
 * I/O ports: 4I4O, 4P4O, 8I, 8P, 8O, see the definitions below for
 * the detailed organization of these ports. Only Goup A is interrupt
 * capable.
 *
 * GPIO numbers start from 'gpio_base + 0' to 'gpio_base + 8/16',
 * and GPIOs from GROUP_A are numbered before those from GROUP_B
 * (if there are two groups).
 *
 * NOTE: MAX7328/MAX7329 are drop-in replacements for PCF8574/a, so
 * they are not supported by this driver.
 */

#define PORT_NONE	0x0	/* '/' No Port */
#define PORT_OUTPUT	0x1	/* 'O' Push-Pull, Output Only */
#define PORT_INPUT	0x2	/* 'I' Input Only */
#define PORT_OPENDRAIN	0x3	/* 'P' Open-Drain, I/O */

#define IO_4I4O		0x5AA5	/* O7 O6 I5 I4 I3 I2 O1 O0 */
#define IO_4P4O		0x5FF5	/* O7 O6 P5 P4 P3 P2 O1 O0 */
#define IO_8I		0xAAAA	/* I7 I6 I5 I4 I3 I2 I1 I0 */
#define IO_8P		0xFFFF	/* P7 P6 P5 P4 P3 P2 P1 P0 */
#define IO_8O		0x5555	/* O7 O6 O5 O4 O3 O2 O1 O0 */

#define GROUP_A(x)	((x) & 0xffff)	/* I2C Addr: 0b'110xxxx */
#define GROUP_B(x)	((x) << 16)	/* I2C Addr: 0b'101xxxx */

#define INT_NONE	0x0	/* No interrupt capability */
#define INT_NO_MASK	0x1	/* Has interrupts, no mask */
#define INT_INDEP_MASK	0x2	/* Has interrupts, independent mask */
#define INT_MERGED_MASK 0x3	/* Has interrupts, merged mask */

#define INT_CAPS(x)	(((uint64_t)(x)) << 32)

enum {
	MAX7319,
	MAX7320,
	MAX7321,
	MAX7322,
	MAX7323,
	MAX7324,
	MAX7325,
	MAX7326,
	MAX7327,
};

static uint64_t max732x_features[] = {
	[MAX7319] = GROUP_A(IO_8I) | INT_CAPS(INT_MERGED_MASK),
	[MAX7320] = GROUP_B(IO_8O),
	[MAX7321] = GROUP_A(IO_8P) | INT_CAPS(INT_NO_MASK),
	[MAX7322] = GROUP_A(IO_4I4O) | INT_CAPS(INT_MERGED_MASK),
	[MAX7323] = GROUP_A(IO_4P4O) | INT_CAPS(INT_INDEP_MASK),
	[MAX7324] = GROUP_A(IO_8I) | GROUP_B(IO_8O) | INT_CAPS(INT_MERGED_MASK),
	[MAX7325] = GROUP_A(IO_8P) | GROUP_B(IO_8O) | INT_CAPS(INT_NO_MASK),
	[MAX7326] = GROUP_A(IO_4I4O) | GROUP_B(IO_8O) | INT_CAPS(INT_MERGED_MASK),
	[MAX7327] = GROUP_A(IO_4P4O) | GROUP_B(IO_8O) | INT_CAPS(INT_NO_MASK),
};

static const struct i2c_device_id max732x_id[] = {
	{ "max7319", MAX7319 },
	{ "max7320", MAX7320 },
	{ "max7321", MAX7321 },
	{ "max7322", MAX7322 },
	{ "max7323", MAX7323 },
	{ "max7324", MAX7324 },
	{ "max7325", MAX7325 },
	{ "max7326", MAX7326 },
	{ "max7327", MAX7327 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max732x_id);

struct max732x_chip {
	struct gpio_chip gpio_chip;

	struct i2c_client *client;	/* "main" client */
	struct i2c_client *client_dummy;
	struct i2c_client *client_group_a;
	struct i2c_client *client_group_b;

	unsigned int	mask_group_a;
	unsigned int	dir_input;
	unsigned int	dir_output;

	struct mutex	lock;
	uint8_t		reg_out[2];

#ifdef CONFIG_GPIO_MAX732X_IRQ
	struct mutex	irq_lock;
	int		irq_base;
	uint8_t		irq_mask;
	uint8_t		irq_mask_cur;
	uint8_t		irq_trig_raise;
	uint8_t		irq_trig_fall;
	uint8_t		irq_features;
#endif
};

static int max732x_writeb(struct max732x_chip *chip, int group_a, uint8_t val)
{
	struct i2c_client *client;
	int ret;

	client = group_a ? chip->client_group_a : chip->client_group_b;
	ret = i2c_smbus_write_byte(client, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writing\n");
		return ret;
	}

	return 0;
}

static int max732x_readb(struct max732x_chip *chip, int group_a, uint8_t *val)
{
	struct i2c_client *client;
	int ret;

	client = group_a ? chip->client_group_a : chip->client_group_b;
	ret = i2c_smbus_read_byte(client);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading\n");
		return ret;
	}

	*val = (uint8_t)ret;
	return 0;
}

static inline int is_group_a(struct max732x_chip *chip, unsigned off)
{
	return (1u << off) & chip->mask_group_a;
}

static int max732x_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct max732x_chip *chip;
	uint8_t reg_val;
	int ret;

	chip = container_of(gc, struct max732x_chip, gpio_chip);

	ret = max732x_readb(chip, is_group_a(chip, off), &reg_val);
	if (ret < 0)
		return 0;

	return reg_val & (1u << (off & 0x7));
}

static void max732x_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct max732x_chip *chip;
	uint8_t reg_out, mask = 1u << (off & 0x7);
	int ret;

	chip = container_of(gc, struct max732x_chip, gpio_chip);

	mutex_lock(&chip->lock);

	reg_out = (off > 7) ? chip->reg_out[1] : chip->reg_out[0];
	reg_out = (val) ? reg_out | mask : reg_out & ~mask;

	ret = max732x_writeb(chip, is_group_a(chip, off), reg_out);
	if (ret < 0)
		goto out;

	/* update the shadow register then */
	if (off > 7)
		chip->reg_out[1] = reg_out;
	else
		chip->reg_out[0] = reg_out;
out:
	mutex_unlock(&chip->lock);
}

static int max732x_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct max732x_chip *chip;
	unsigned int mask = 1u << off;

	chip = container_of(gc, struct max732x_chip, gpio_chip);

	if ((mask & chip->dir_input) == 0) {
		dev_dbg(&chip->client->dev, "%s port %d is output only\n",
			chip->client->name, off);
		return -EACCES;
	}

	/*
	 * Open-drain pins must be set to high impedance (which is
	 * equivalent to output-high) to be turned into an input.
	 */
	if ((mask & chip->dir_output))
		max732x_gpio_set_value(gc, off, 1);

	return 0;
}

static int max732x_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
	struct max732x_chip *chip;
	unsigned int mask = 1u << off;

	chip = container_of(gc, struct max732x_chip, gpio_chip);

	if ((mask & chip->dir_output) == 0) {
		dev_dbg(&chip->client->dev, "%s port %d is input only\n",
			chip->client->name, off);
		return -EACCES;
	}

	max732x_gpio_set_value(gc, off, val);
	return 0;
}

#ifdef CONFIG_GPIO_MAX732X_IRQ
static int max732x_writew(struct max732x_chip *chip, uint16_t val)
{
	int ret;

	val = cpu_to_le16(val);

	ret = i2c_master_send(chip->client_group_a, (char *)&val, 2);
	if (ret < 0) {
		dev_err(&chip->client_group_a->dev, "failed writing\n");
		return ret;
	}

	return 0;
}

static int max732x_readw(struct max732x_chip *chip, uint16_t *val)
{
	int ret;

	ret = i2c_master_recv(chip->client_group_a, (char *)val, 2);
	if (ret < 0) {
		dev_err(&chip->client_group_a->dev, "failed reading\n");
		return ret;
	}

	*val = le16_to_cpu(*val);
	return 0;
}

static void max732x_irq_update_mask(struct max732x_chip *chip)
{
	uint16_t msg;

	if (chip->irq_mask == chip->irq_mask_cur)
		return;

	chip->irq_mask = chip->irq_mask_cur;

	if (chip->irq_features == INT_NO_MASK)
		return;

	mutex_lock(&chip->lock);

	switch (chip->irq_features) {
	case INT_INDEP_MASK:
		msg = (chip->irq_mask << 8) | chip->reg_out[0];
		max732x_writew(chip, msg);
		break;

	case INT_MERGED_MASK:
		msg = chip->irq_mask | chip->reg_out[0];
		max732x_writeb(chip, 1, (uint8_t)msg);
		break;
	}

	mutex_unlock(&chip->lock);
}

static int max732x_gpio_to_irq(struct gpio_chip *gc, unsigned off)
{
	struct max732x_chip *chip;

	chip = container_of(gc, struct max732x_chip, gpio_chip);
	return chip->irq_base + off;
}

static void max732x_irq_mask(unsigned int irq)
{
	struct max732x_chip *chip = get_irq_chip_data(irq);

	chip->irq_mask_cur &= ~(1 << (irq - chip->irq_base));
}

static void max732x_irq_unmask(unsigned int irq)
{
	struct max732x_chip *chip = get_irq_chip_data(irq);

	chip->irq_mask_cur |= 1 << (irq - chip->irq_base);
}

static void max732x_irq_bus_lock(unsigned int irq)
{
	struct max732x_chip *chip = get_irq_chip_data(irq);

	mutex_lock(&chip->irq_lock);
	chip->irq_mask_cur = chip->irq_mask;
}

static void max732x_irq_bus_sync_unlock(unsigned int irq)
{
	struct max732x_chip *chip = get_irq_chip_data(irq);

	max732x_irq_update_mask(chip);
	mutex_unlock(&chip->irq_lock);
}

static int max732x_irq_set_type(unsigned int irq, unsigned int type)
{
	struct max732x_chip *chip = get_irq_chip_data(irq);
	uint16_t off = irq - chip->irq_base;
	uint16_t mask = 1 << off;

	if (!(mask & chip->dir_input)) {
		dev_dbg(&chip->client->dev, "%s port %d is output only\n",
			chip->client->name, off);
		return -EACCES;
	}

	if (!(type & IRQ_TYPE_EDGE_BOTH)) {
		dev_err(&chip->client->dev, "irq %d: unsupported type %d\n",
			irq, type);
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_FALLING)
		chip->irq_trig_fall |= mask;
	else
		chip->irq_trig_fall &= ~mask;

	if (type & IRQ_TYPE_EDGE_RISING)
		chip->irq_trig_raise |= mask;
	else
		chip->irq_trig_raise &= ~mask;

	return max732x_gpio_direction_input(&chip->gpio_chip, off);
}

static struct irq_chip max732x_irq_chip = {
	.name			= "max732x",
	.mask			= max732x_irq_mask,
	.unmask			= max732x_irq_unmask,
	.bus_lock		= max732x_irq_bus_lock,
	.bus_sync_unlock	= max732x_irq_bus_sync_unlock,
	.set_type		= max732x_irq_set_type,
};

static uint8_t max732x_irq_pending(struct max732x_chip *chip)
{
	uint8_t cur_stat;
	uint8_t old_stat;
	uint8_t trigger;
	uint8_t pending;
	uint16_t status;
	int ret;

	ret = max732x_readw(chip, &status);
	if (ret)
		return 0;

	trigger = status >> 8;
	trigger &= chip->irq_mask;

	if (!trigger)
		return 0;

	cur_stat = status & 0xFF;
	cur_stat &= chip->irq_mask;

	old_stat = cur_stat ^ trigger;

	pending = (old_stat & chip->irq_trig_fall) |
		  (cur_stat & chip->irq_trig_raise);
	pending &= trigger;

	return pending;
}

static irqreturn_t max732x_irq_handler(int irq, void *devid)
{
	struct max732x_chip *chip = devid;
	uint8_t pending;
	uint8_t level;

	pending = max732x_irq_pending(chip);

	if (!pending)
		return IRQ_HANDLED;

	do {
		level = __ffs(pending);
		handle_nested_irq(level + chip->irq_base);

		pending &= ~(1 << level);
	} while (pending);

	return IRQ_HANDLED;
}

static int max732x_irq_setup(struct max732x_chip *chip,
			     const struct i2c_device_id *id)
{
	struct i2c_client *client = chip->client;
	struct max732x_platform_data *pdata = client->dev.platform_data;
	int has_irq = max732x_features[id->driver_data] >> 32;
	int ret;

	if (pdata->irq_base && has_irq != INT_NONE) {
		int lvl;

		chip->irq_base = pdata->irq_base;
		chip->irq_features = has_irq;
		mutex_init(&chip->irq_lock);

		for (lvl = 0; lvl < chip->gpio_chip.ngpio; lvl++) {
			int irq = lvl + chip->irq_base;

			if (!(chip->dir_input & (1 << lvl)))
				continue;

			set_irq_chip_data(irq, chip);
			set_irq_chip_and_handler(irq, &max732x_irq_chip,
						 handle_edge_irq);
			set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
			set_irq_flags(irq, IRQF_VALID);
#else
			set_irq_noprobe(irq);
#endif
		}

		ret = request_threaded_irq(client->irq,
					   NULL,
					   max732x_irq_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   dev_name(&client->dev), chip);
		if (ret) {
			dev_err(&client->dev, "failed to request irq %d\n",
				client->irq);
			goto out_failed;
		}

		chip->gpio_chip.to_irq = max732x_gpio_to_irq;
	}

	return 0;

out_failed:
	chip->irq_base = 0;
	return ret;
}

static void max732x_irq_teardown(struct max732x_chip *chip)
{
	if (chip->irq_base)
		free_irq(chip->client->irq, chip);
}
#else /* CONFIG_GPIO_MAX732X_IRQ */
static int max732x_irq_setup(struct max732x_chip *chip,
			     const struct i2c_device_id *id)
{
	struct i2c_client *client = chip->client;
	struct max732x_platform_data *pdata = client->dev.platform_data;
	int has_irq = max732x_features[id->driver_data] >> 32;

	if (pdata->irq_base && has_irq != INT_NONE)
		dev_warn(&client->dev, "interrupt support not compiled in\n");

	return 0;
}

static void max732x_irq_teardown(struct max732x_chip *chip)
{
}
#endif

static int __devinit max732x_setup_gpio(struct max732x_chip *chip,
					const struct i2c_device_id *id,
					unsigned gpio_start)
{
	struct gpio_chip *gc = &chip->gpio_chip;
	uint32_t id_data = (uint32_t)max732x_features[id->driver_data];
	int i, port = 0;

	for (i = 0; i < 16; i++, id_data >>= 2) {
		unsigned int mask = 1 << port;

		switch (id_data & 0x3) {
		case PORT_OUTPUT:
			chip->dir_output |= mask;
			break;
		case PORT_INPUT:
			chip->dir_input |= mask;
			break;
		case PORT_OPENDRAIN:
			chip->dir_output |= mask;
			chip->dir_input |= mask;
			break;
		default:
			continue;
		}

		if (i < 8)
			chip->mask_group_a |= mask;
		port++;
	}

	if (chip->dir_input)
		gc->direction_input = max732x_gpio_direction_input;
	if (chip->dir_output) {
		gc->direction_output = max732x_gpio_direction_output;
		gc->set = max732x_gpio_set_value;
	}
	gc->get = max732x_gpio_get_value;
	gc->can_sleep = 1;

	gc->base = gpio_start;
	gc->ngpio = port;
	gc->label = chip->client->name;
	gc->owner = THIS_MODULE;

	return port;
}

static int __devinit max732x_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct max732x_platform_data *pdata;
	struct max732x_chip *chip;
	struct i2c_client *c;
	uint16_t addr_a, addr_b;
	int ret, nr_port;

	pdata = client->dev.platform_data;
	if (pdata == NULL) {
		dev_dbg(&client->dev, "no platform data\n");
		return -EINVAL;
	}

	chip = kzalloc(sizeof(struct max732x_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->client = client;

	nr_port = max732x_setup_gpio(chip, id, pdata->gpio_base);

	addr_a = (client->addr & 0x0f) | 0x60;
	addr_b = (client->addr & 0x0f) | 0x50;

	switch (client->addr & 0x70) {
	case 0x60:
		chip->client_group_a = client;
		if (nr_port > 8) {
			c = i2c_new_dummy(client->adapter, addr_b);
			chip->client_group_b = chip->client_dummy = c;
		}
		break;
	case 0x50:
		chip->client_group_b = client;
		if (nr_port > 8) {
			c = i2c_new_dummy(client->adapter, addr_a);
			chip->client_group_a = chip->client_dummy = c;
		}
		break;
	default:
		dev_err(&client->dev, "invalid I2C address specified %02x\n",
				client->addr);
		ret = -EINVAL;
		goto out_failed;
	}

	mutex_init(&chip->lock);

	max732x_readb(chip, is_group_a(chip, 0), &chip->reg_out[0]);
	if (nr_port > 8)
		max732x_readb(chip, is_group_a(chip, 8), &chip->reg_out[1]);

	ret = max732x_irq_setup(chip, id);
	if (ret)
		goto out_failed;

	ret = gpiochip_add(&chip->gpio_chip);
	if (ret)
		goto out_failed;

	if (pdata->setup) {
		ret = pdata->setup(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, chip);
	return 0;

out_failed:
	max732x_irq_teardown(chip);
	kfree(chip);
	return ret;
}

static int __devexit max732x_remove(struct i2c_client *client)
{
	struct max732x_platform_data *pdata = client->dev.platform_data;
	struct max732x_chip *chip = i2c_get_clientdata(client);
	int ret;

	if (pdata->teardown) {
		ret = pdata->teardown(client, chip->gpio_chip.base,
				chip->gpio_chip.ngpio, pdata->context);
		if (ret < 0) {
			dev_err(&client->dev, "%s failed, %d\n",
					"teardown", ret);
			return ret;
		}
	}

	ret = gpiochip_remove(&chip->gpio_chip);
	if (ret) {
		dev_err(&client->dev, "%s failed, %d\n",
				"gpiochip_remove()", ret);
		return ret;
	}

	max732x_irq_teardown(chip);

	/* unregister any dummy i2c_client */
	if (chip->client_dummy)
		i2c_unregister_device(chip->client_dummy);

	kfree(chip);
	return 0;
}

static struct i2c_driver max732x_driver = {
	.driver = {
		.name	= "max732x",
		.owner	= THIS_MODULE,
	},
	.probe		= max732x_probe,
	.remove		= __devexit_p(max732x_remove),
	.id_table	= max732x_id,
};

static int __init max732x_init(void)
{
	return i2c_add_driver(&max732x_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(max732x_init);

static void __exit max732x_exit(void)
{
	i2c_del_driver(&max732x_driver);
}
module_exit(max732x_exit);

MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>");
MODULE_DESCRIPTION("GPIO expander driver for MAX732X");
MODULE_LICENSE("GPL");
