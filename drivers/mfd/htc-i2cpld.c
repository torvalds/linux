/*
 *  htc-i2cpld.c
 *  Chip driver for an unknown CPLD chip found on omap850 HTC devices like
 *  the HTC Wizard and HTC Herald.
 *  The cpld is located on the i2c bus and acts as an input/output GPIO
 *  extender.
 *
 *  Copyright (C) 2009 Cory Maccarrone <darkstar6262@gmail.com>
 *
 *  Based on work done in the linwizard project
 *  Copyright (C) 2008-2009 Angelo Arrifano <miknix@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/htcpld.h>
#include <linux/gpio.h>
#include <linux/slab.h>

struct htcpld_chip {
	spinlock_t              lock;

	/* chip info */
	u8                      reset;
	u8                      addr;
	struct device           *dev;
	struct i2c_client	*client;

	/* Output details */
	u8                      cache_out;
	struct gpio_chip        chip_out;

	/* Input details */
	u8                      cache_in;
	struct gpio_chip        chip_in;

	u16                     irqs_enabled;
	uint                    irq_start;
	int                     nirqs;

	unsigned int		flow_type;
	/*
	 * Work structure to allow for setting values outside of any
	 * possible interrupt context
	 */
	struct work_struct set_val_work;
};

struct htcpld_data {
	/* irq info */
	u16                irqs_enabled;
	uint               irq_start;
	int                nirqs;
	uint               chained_irq;
	unsigned int       int_reset_gpio_hi;
	unsigned int       int_reset_gpio_lo;

	/* htcpld info */
	struct htcpld_chip *chip;
	unsigned int       nchips;
};

/* There does not appear to be a way to proactively mask interrupts
 * on the htcpld chip itself.  So, we simply ignore interrupts that
 * aren't desired. */
static void htcpld_mask(struct irq_data *data)
{
	struct htcpld_chip *chip = irq_data_get_irq_chip_data(data);
	chip->irqs_enabled &= ~(1 << (data->irq - chip->irq_start));
	pr_debug("HTCPLD mask %d %04x\n", data->irq, chip->irqs_enabled);
}
static void htcpld_unmask(struct irq_data *data)
{
	struct htcpld_chip *chip = irq_data_get_irq_chip_data(data);
	chip->irqs_enabled |= 1 << (data->irq - chip->irq_start);
	pr_debug("HTCPLD unmask %d %04x\n", data->irq, chip->irqs_enabled);
}

static int htcpld_set_type(struct irq_data *data, unsigned int flags)
{
	struct htcpld_chip *chip = irq_data_get_irq_chip_data(data);

	if (flags & ~IRQ_TYPE_SENSE_MASK)
		return -EINVAL;

	/* We only allow edge triggering */
	if (flags & (IRQ_TYPE_LEVEL_LOW|IRQ_TYPE_LEVEL_HIGH))
		return -EINVAL;

	chip->flow_type = flags;
	return 0;
}

static struct irq_chip htcpld_muxed_chip = {
	.name         = "htcpld",
	.irq_mask     = htcpld_mask,
	.irq_unmask   = htcpld_unmask,
	.irq_set_type = htcpld_set_type,
};

/* To properly dispatch IRQ events, we need to read from the
 * chip.  This is an I2C action that could possibly sleep
 * (which is bad in interrupt context) -- so we use a threaded
 * interrupt handler to get around that.
 */
static irqreturn_t htcpld_handler(int irq, void *dev)
{
	struct htcpld_data *htcpld = dev;
	unsigned int i;
	unsigned long flags;
	int irqpin;

	if (!htcpld) {
		pr_debug("htcpld is null in ISR\n");
		return IRQ_HANDLED;
	}

	/*
	 * For each chip, do a read of the chip and trigger any interrupts
	 * desired.  The interrupts will be triggered from LSB to MSB (i.e.
	 * bit 0 first, then bit 1, etc.)
	 *
	 * For chips that have no interrupt range specified, just skip 'em.
	 */
	for (i = 0; i < htcpld->nchips; i++) {
		struct htcpld_chip *chip = &htcpld->chip[i];
		struct i2c_client *client;
		int val;
		unsigned long uval, old_val;

		if (!chip) {
			pr_debug("chip %d is null in ISR\n", i);
			continue;
		}

		if (chip->nirqs == 0)
			continue;

		client = chip->client;
		if (!client) {
			pr_debug("client %d is null in ISR\n", i);
			continue;
		}

		/* Scan the chip */
		val = i2c_smbus_read_byte_data(client, chip->cache_out);
		if (val < 0) {
			/* Throw a warning and skip this chip */
			dev_warn(chip->dev, "Unable to read from chip: %d\n",
				 val);
			continue;
		}

		uval = (unsigned long)val;

		spin_lock_irqsave(&chip->lock, flags);

		/* Save away the old value so we can compare it */
		old_val = chip->cache_in;

		/* Write the new value */
		chip->cache_in = uval;

		spin_unlock_irqrestore(&chip->lock, flags);

		/*
		 * For each bit in the data (starting at bit 0), trigger
		 * associated interrupts.
		 */
		for (irqpin = 0; irqpin < chip->nirqs; irqpin++) {
			unsigned oldb, newb, type = chip->flow_type;

			irq = chip->irq_start + irqpin;

			/* Run the IRQ handler, but only if the bit value
			 * changed, and the proper flags are set */
			oldb = (old_val >> irqpin) & 1;
			newb = (uval >> irqpin) & 1;

			if ((!oldb && newb && (type & IRQ_TYPE_EDGE_RISING)) ||
			    (oldb && !newb && (type & IRQ_TYPE_EDGE_FALLING))) {
				pr_debug("fire IRQ %d\n", irqpin);
				generic_handle_irq(irq);
			}
		}
	}

	/*
	 * In order to continue receiving interrupts, the int_reset_gpio must
	 * be asserted.
	 */
	if (htcpld->int_reset_gpio_hi)
		gpio_set_value(htcpld->int_reset_gpio_hi, 1);
	if (htcpld->int_reset_gpio_lo)
		gpio_set_value(htcpld->int_reset_gpio_lo, 0);

	return IRQ_HANDLED;
}

/*
 * The GPIO set routines can be called from interrupt context, especially if,
 * for example they're attached to the led-gpio framework and a trigger is
 * enabled.  As such, we declared work above in the htcpld_chip structure,
 * and that work is scheduled in the set routine.  The kernel can then run
 * the I2C functions, which will sleep, in process context.
 */
static void htcpld_chip_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct i2c_client *client;
	struct htcpld_chip *chip_data;
	unsigned long flags;

	chip_data = container_of(chip, struct htcpld_chip, chip_out);
	if (!chip_data)
		return;

	client = chip_data->client;
	if (client == NULL)
		return;

	spin_lock_irqsave(&chip_data->lock, flags);
	if (val)
		chip_data->cache_out |= (1 << offset);
	else
		chip_data->cache_out &= ~(1 << offset);
	spin_unlock_irqrestore(&chip_data->lock, flags);

	schedule_work(&(chip_data->set_val_work));
}

static void htcpld_chip_set_ni(struct work_struct *work)
{
	struct htcpld_chip *chip_data;
	struct i2c_client *client;

	chip_data = container_of(work, struct htcpld_chip, set_val_work);
	client = chip_data->client;
	i2c_smbus_read_byte_data(client, chip_data->cache_out);
}

static int htcpld_chip_get(struct gpio_chip *chip, unsigned offset)
{
	struct htcpld_chip *chip_data;
	int val = 0;
	int is_input = 0;

	/* Try out first */
	chip_data = container_of(chip, struct htcpld_chip, chip_out);
	if (!chip_data) {
		/* Try in */
		is_input = 1;
		chip_data = container_of(chip, struct htcpld_chip, chip_in);
		if (!chip_data)
			return -EINVAL;
	}

	/* Determine if this is an input or output GPIO */
	if (!is_input)
		/* Use the output cache */
		val = (chip_data->cache_out >> offset) & 1;
	else
		/* Use the input cache */
		val = (chip_data->cache_in >> offset) & 1;

	if (val)
		return 1;
	else
		return 0;
}

static int htcpld_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	htcpld_chip_set(chip, offset, value);
	return 0;
}

static int htcpld_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	/*
	 * No-op: this function can only be called on the input chip.
	 * We do however make sure the offset is within range.
	 */
	return (offset < chip->ngpio) ? 0 : -EINVAL;
}

static int htcpld_chip_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct htcpld_chip *chip_data;

	chip_data = container_of(chip, struct htcpld_chip, chip_in);

	if (offset < chip_data->nirqs)
		return chip_data->irq_start + offset;
	else
		return -EINVAL;
}

static void htcpld_chip_reset(struct i2c_client *client)
{
	struct htcpld_chip *chip_data = i2c_get_clientdata(client);
	if (!chip_data)
		return;

	i2c_smbus_read_byte_data(
		client, (chip_data->cache_out = chip_data->reset));
}

static int htcpld_setup_chip_irq(
		struct platform_device *pdev,
		int chip_index)
{
	struct htcpld_data *htcpld;
	struct htcpld_chip *chip;
	unsigned int irq, irq_end;
	int ret = 0;

	/* Get the platform and driver data */
	htcpld = platform_get_drvdata(pdev);
	chip = &htcpld->chip[chip_index];

	/* Setup irq handlers */
	irq_end = chip->irq_start + chip->nirqs;
	for (irq = chip->irq_start; irq < irq_end; irq++) {
		irq_set_chip_and_handler(irq, &htcpld_muxed_chip,
					 handle_simple_irq);
		irq_set_chip_data(irq, chip);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
#else
		irq_set_probe(irq);
#endif
	}

	return ret;
}

static int htcpld_register_chip_i2c(
		struct platform_device *pdev,
		int chip_index)
{
	struct htcpld_data *htcpld;
	struct device *dev = &pdev->dev;
	struct htcpld_core_platform_data *pdata;
	struct htcpld_chip *chip;
	struct htcpld_chip_platform_data *plat_chip_data;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info info;

	/* Get the platform and driver data */
	pdata = dev_get_platdata(dev);
	htcpld = platform_get_drvdata(pdev);
	chip = &htcpld->chip[chip_index];
	plat_chip_data = &pdata->chip[chip_index];

	adapter = i2c_get_adapter(pdata->i2c_adapter_id);
	if (adapter == NULL) {
		/* Eek, no such I2C adapter!  Bail out. */
		dev_warn(dev, "Chip at i2c address 0x%x: Invalid i2c adapter %d\n",
			 plat_chip_data->addr, pdata->i2c_adapter_id);
		return -ENODEV;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
		dev_warn(dev, "i2c adapter %d non-functional\n",
			 pdata->i2c_adapter_id);
		return -EINVAL;
	}

	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = plat_chip_data->addr;
	strlcpy(info.type, "htcpld-chip", I2C_NAME_SIZE);
	info.platform_data = chip;

	/* Add the I2C device.  This calls the probe() function. */
	client = i2c_new_device(adapter, &info);
	if (!client) {
		/* I2C device registration failed, contineu with the next */
		dev_warn(dev, "Unable to add I2C device for 0x%x\n",
			 plat_chip_data->addr);
		return -ENODEV;
	}

	i2c_set_clientdata(client, chip);
	snprintf(client->name, I2C_NAME_SIZE, "Chip_0x%x", client->addr);
	chip->client = client;

	/* Reset the chip */
	htcpld_chip_reset(client);
	chip->cache_in = i2c_smbus_read_byte_data(client, chip->cache_out);

	return 0;
}

static void htcpld_unregister_chip_i2c(
		struct platform_device *pdev,
		int chip_index)
{
	struct htcpld_data *htcpld;
	struct htcpld_chip *chip;

	/* Get the platform and driver data */
	htcpld = platform_get_drvdata(pdev);
	chip = &htcpld->chip[chip_index];

	if (chip->client)
		i2c_unregister_device(chip->client);
}

static int htcpld_register_chip_gpio(
		struct platform_device *pdev,
		int chip_index)
{
	struct htcpld_data *htcpld;
	struct device *dev = &pdev->dev;
	struct htcpld_core_platform_data *pdata;
	struct htcpld_chip *chip;
	struct htcpld_chip_platform_data *plat_chip_data;
	struct gpio_chip *gpio_chip;
	int ret = 0;

	/* Get the platform and driver data */
	pdata = dev_get_platdata(dev);
	htcpld = platform_get_drvdata(pdev);
	chip = &htcpld->chip[chip_index];
	plat_chip_data = &pdata->chip[chip_index];

	/* Setup the GPIO chips */
	gpio_chip = &(chip->chip_out);
	gpio_chip->label           = "htcpld-out";
	gpio_chip->dev             = dev;
	gpio_chip->owner           = THIS_MODULE;
	gpio_chip->get             = htcpld_chip_get;
	gpio_chip->set             = htcpld_chip_set;
	gpio_chip->direction_input = NULL;
	gpio_chip->direction_output = htcpld_direction_output;
	gpio_chip->base            = plat_chip_data->gpio_out_base;
	gpio_chip->ngpio           = plat_chip_data->num_gpios;

	gpio_chip = &(chip->chip_in);
	gpio_chip->label           = "htcpld-in";
	gpio_chip->dev             = dev;
	gpio_chip->owner           = THIS_MODULE;
	gpio_chip->get             = htcpld_chip_get;
	gpio_chip->set             = NULL;
	gpio_chip->direction_input = htcpld_direction_input;
	gpio_chip->direction_output = NULL;
	gpio_chip->to_irq          = htcpld_chip_to_irq;
	gpio_chip->base            = plat_chip_data->gpio_in_base;
	gpio_chip->ngpio           = plat_chip_data->num_gpios;

	/* Add the GPIO chips */
	ret = gpiochip_add(&(chip->chip_out));
	if (ret) {
		dev_warn(dev, "Unable to register output GPIOs for 0x%x: %d\n",
			 plat_chip_data->addr, ret);
		return ret;
	}

	ret = gpiochip_add(&(chip->chip_in));
	if (ret) {
		dev_warn(dev, "Unable to register input GPIOs for 0x%x: %d\n",
			 plat_chip_data->addr, ret);
		gpiochip_remove(&(chip->chip_out));
		return ret;
	}

	return 0;
}

static int htcpld_setup_chips(struct platform_device *pdev)
{
	struct htcpld_data *htcpld;
	struct device *dev = &pdev->dev;
	struct htcpld_core_platform_data *pdata;
	int i;

	/* Get the platform and driver data */
	pdata = dev_get_platdata(dev);
	htcpld = platform_get_drvdata(pdev);

	/* Setup each chip's output GPIOs */
	htcpld->nchips = pdata->num_chip;
	htcpld->chip = devm_kzalloc(dev, sizeof(struct htcpld_chip) * htcpld->nchips,
				    GFP_KERNEL);
	if (!htcpld->chip) {
		dev_warn(dev, "Unable to allocate memory for chips\n");
		return -ENOMEM;
	}

	/* Add the chips as best we can */
	for (i = 0; i < htcpld->nchips; i++) {
		int ret;

		/* Setup the HTCPLD chips */
		htcpld->chip[i].reset = pdata->chip[i].reset;
		htcpld->chip[i].cache_out = pdata->chip[i].reset;
		htcpld->chip[i].cache_in = 0;
		htcpld->chip[i].dev = dev;
		htcpld->chip[i].irq_start = pdata->chip[i].irq_base;
		htcpld->chip[i].nirqs = pdata->chip[i].num_irqs;

		INIT_WORK(&(htcpld->chip[i].set_val_work), &htcpld_chip_set_ni);
		spin_lock_init(&(htcpld->chip[i].lock));

		/* Setup the interrupts for the chip */
		if (htcpld->chained_irq) {
			ret = htcpld_setup_chip_irq(pdev, i);
			if (ret)
				continue;
		}

		/* Register the chip with I2C */
		ret = htcpld_register_chip_i2c(pdev, i);
		if (ret)
			continue;


		/* Register the chips with the GPIO subsystem */
		ret = htcpld_register_chip_gpio(pdev, i);
		if (ret) {
			/* Unregister the chip from i2c and continue */
			htcpld_unregister_chip_i2c(pdev, i);
			continue;
		}

		dev_info(dev, "Registered chip at 0x%x\n", pdata->chip[i].addr);
	}

	return 0;
}

static int htcpld_core_probe(struct platform_device *pdev)
{
	struct htcpld_data *htcpld;
	struct device *dev = &pdev->dev;
	struct htcpld_core_platform_data *pdata;
	struct resource *res;
	int ret = 0;

	if (!dev)
		return -ENODEV;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		dev_warn(dev, "Platform data not found for htcpld core!\n");
		return -ENXIO;
	}

	htcpld = devm_kzalloc(dev, sizeof(struct htcpld_data), GFP_KERNEL);
	if (!htcpld)
		return -ENOMEM;

	/* Find chained irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res) {
		int flags;
		htcpld->chained_irq = res->start;

		/* Setup the chained interrupt handler */
		flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING;
		ret = request_threaded_irq(htcpld->chained_irq,
					   NULL, htcpld_handler,
					   flags, pdev->name, htcpld);
		if (ret) {
			dev_warn(dev, "Unable to setup chained irq handler: %d\n", ret);
			return ret;
		} else
			device_init_wakeup(dev, 0);
	}

	/* Set the driver data */
	platform_set_drvdata(pdev, htcpld);

	/* Setup the htcpld chips */
	ret = htcpld_setup_chips(pdev);
	if (ret)
		return ret;

	/* Request the GPIO(s) for the int reset and set them up */
	if (pdata->int_reset_gpio_hi) {
		ret = gpio_request(pdata->int_reset_gpio_hi, "htcpld-core");
		if (ret) {
			/*
			 * If it failed, that sucks, but we can probably
			 * continue on without it.
			 */
			dev_warn(dev, "Unable to request int_reset_gpio_hi -- interrupts may not work\n");
			htcpld->int_reset_gpio_hi = 0;
		} else {
			htcpld->int_reset_gpio_hi = pdata->int_reset_gpio_hi;
			gpio_set_value(htcpld->int_reset_gpio_hi, 1);
		}
	}

	if (pdata->int_reset_gpio_lo) {
		ret = gpio_request(pdata->int_reset_gpio_lo, "htcpld-core");
		if (ret) {
			/*
			 * If it failed, that sucks, but we can probably
			 * continue on without it.
			 */
			dev_warn(dev, "Unable to request int_reset_gpio_lo -- interrupts may not work\n");
			htcpld->int_reset_gpio_lo = 0;
		} else {
			htcpld->int_reset_gpio_lo = pdata->int_reset_gpio_lo;
			gpio_set_value(htcpld->int_reset_gpio_lo, 0);
		}
	}

	dev_info(dev, "Initialized successfully\n");
	return 0;
}

/* The I2C Driver -- used internally */
static const struct i2c_device_id htcpld_chip_id[] = {
	{ "htcpld-chip", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, htcpld_chip_id);


static struct i2c_driver htcpld_chip_driver = {
	.driver = {
		.name	= "htcpld-chip",
	},
	.id_table = htcpld_chip_id,
};

/* The Core Driver */
static struct platform_driver htcpld_core_driver = {
	.driver = {
		.name = "i2c-htcpld",
	},
};

static int __init htcpld_core_init(void)
{
	int ret;

	/* Register the I2C Chip driver */
	ret = i2c_add_driver(&htcpld_chip_driver);
	if (ret)
		return ret;

	/* Probe for our chips */
	return platform_driver_probe(&htcpld_core_driver, htcpld_core_probe);
}

static void __exit htcpld_core_exit(void)
{
	i2c_del_driver(&htcpld_chip_driver);
	platform_driver_unregister(&htcpld_core_driver);
}

module_init(htcpld_core_init);
module_exit(htcpld_core_exit);

MODULE_AUTHOR("Cory Maccarrone <darkstar6262@gmail.com>");
MODULE_DESCRIPTION("I2C HTC PLD Driver");
MODULE_LICENSE("GPL");

