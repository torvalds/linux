/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/i2c/sx150x.h>

#define NO_UPDATE_PENDING	-1

struct sx150x_device_data {
	u8 reg_pullup;
	u8 reg_pulldn;
	u8 reg_drain;
	u8 reg_polarity;
	u8 reg_dir;
	u8 reg_data;
	u8 reg_irq_mask;
	u8 reg_irq_src;
	u8 reg_sense;
	u8 reg_clock;
	u8 reg_misc;
	u8 reg_reset;
	u8 ngpios;
};

struct sx150x_chip {
	struct gpio_chip                 gpio_chip;
	struct i2c_client               *client;
	const struct sx150x_device_data *dev_cfg;
	int                              irq_summary;
	int                              irq_base;
	int				 irq_update;
	u32                              irq_sense;
	u32				 irq_masked;
	u32				 dev_sense;
	u32				 dev_masked;
	struct irq_chip                  irq_chip;
	struct mutex                     lock;
};

static const struct sx150x_device_data sx150x_devices[] = {
	[0] = { /* sx1508q */
		.reg_pullup   = 0x03,
		.reg_pulldn   = 0x04,
		.reg_drain    = 0x05,
		.reg_polarity = 0x06,
		.reg_dir      = 0x07,
		.reg_data     = 0x08,
		.reg_irq_mask = 0x09,
		.reg_irq_src  = 0x0c,
		.reg_sense    = 0x0b,
		.reg_clock    = 0x0f,
		.reg_misc     = 0x10,
		.reg_reset    = 0x7d,
		.ngpios       = 8
	},
	[1] = { /* sx1509q */
		.reg_pullup   = 0x07,
		.reg_pulldn   = 0x09,
		.reg_drain    = 0x0b,
		.reg_polarity = 0x0d,
		.reg_dir      = 0x0f,
		.reg_data     = 0x11,
		.reg_irq_mask = 0x13,
		.reg_irq_src  = 0x19,
		.reg_sense    = 0x17,
		.reg_clock    = 0x1e,
		.reg_misc     = 0x1f,
		.reg_reset    = 0x7d,
		.ngpios       = 16
	},
};

static const struct i2c_device_id sx150x_id[] = {
	{"sx1508q", 0},
	{"sx1509q", 1},
	{}
};
MODULE_DEVICE_TABLE(i2c, sx150x_id);

static s32 sx150x_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	s32 err = i2c_smbus_write_byte_data(client, reg, val);

	if (err < 0)
		dev_warn(&client->dev,
			"i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, err);
	return err;
}

static s32 sx150x_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 err = i2c_smbus_read_byte_data(client, reg);

	if (err >= 0)
		*val = err;
	else
		dev_warn(&client->dev,
			"i2c read fail: can't read from %02x: %d\n",
			reg, err);
	return err;
}

static inline bool offset_is_oscio(struct sx150x_chip *chip, unsigned offset)
{
	return (chip->dev_cfg->ngpios == offset);
}

/*
 * These utility functions solve the common problem of locating and setting
 * configuration bits.  Configuration bits are grouped into registers
 * whose indexes increase downwards.  For example, with eight-bit registers,
 * sixteen gpios would have their config bits grouped in the following order:
 * REGISTER N-1 [ f e d c b a 9 8 ]
 *          N   [ 7 6 5 4 3 2 1 0 ]
 *
 * For multi-bit configurations, the pattern gets wider:
 * REGISTER N-3 [ f f e e d d c c ]
 *          N-2 [ b b a a 9 9 8 8 ]
 *          N-1 [ 7 7 6 6 5 5 4 4 ]
 *          N   [ 3 3 2 2 1 1 0 0 ]
 *
 * Given the address of the starting register 'N', the index of the gpio
 * whose configuration we seek to change, and the width in bits of that
 * configuration, these functions allow us to locate the correct
 * register and mask the correct bits.
 */
static inline void sx150x_find_cfg(u8 offset, u8 width,
				u8 *reg, u8 *mask, u8 *shift)
{
	*reg   -= offset * width / 8;
	*mask   = (1 << width) - 1;
	*shift  = (offset * width) % 8;
	*mask <<= *shift;
}

static s32 sx150x_write_cfg(struct sx150x_chip *chip,
			u8 offset, u8 width, u8 reg, u8 val)
{
	u8  mask;
	u8  data;
	u8  shift;
	s32 err;

	sx150x_find_cfg(offset, width, &reg, &mask, &shift);
	err = sx150x_i2c_read(chip->client, reg, &data);
	if (err < 0)
		return err;

	data &= ~mask;
	data |= (val << shift) & mask;
	return sx150x_i2c_write(chip->client, reg, data);
}

static int sx150x_get_io(struct sx150x_chip *chip, unsigned offset)
{
	u8  reg = chip->dev_cfg->reg_data;
	u8  mask;
	u8  data;
	u8  shift;
	s32 err;

	sx150x_find_cfg(offset, 1, &reg, &mask, &shift);
	err = sx150x_i2c_read(chip->client, reg, &data);
	if (err >= 0)
		err = (data & mask) != 0 ? 1 : 0;

	return err;
}

static void sx150x_set_oscio(struct sx150x_chip *chip, int val)
{
	sx150x_i2c_write(chip->client,
			chip->dev_cfg->reg_clock,
			(val ? 0x1f : 0x10));
}

static void sx150x_set_io(struct sx150x_chip *chip, unsigned offset, int val)
{
	sx150x_write_cfg(chip,
			offset,
			1,
			chip->dev_cfg->reg_data,
			(val ? 1 : 0));
}

static int sx150x_io_input(struct sx150x_chip *chip, unsigned offset)
{
	return sx150x_write_cfg(chip,
				offset,
				1,
				chip->dev_cfg->reg_dir,
				1);
}

static int sx150x_io_output(struct sx150x_chip *chip, unsigned offset, int val)
{
	int err;

	err = sx150x_write_cfg(chip,
			offset,
			1,
			chip->dev_cfg->reg_data,
			(val ? 1 : 0));
	if (err >= 0)
		err = sx150x_write_cfg(chip,
				offset,
				1,
				chip->dev_cfg->reg_dir,
				0);
	return err;
}

static int sx150x_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct sx150x_chip *chip;
	int status = -EINVAL;

	chip = container_of(gc, struct sx150x_chip, gpio_chip);

	if (!offset_is_oscio(chip, offset)) {
		mutex_lock(&chip->lock);
		status = sx150x_get_io(chip, offset);
		mutex_unlock(&chip->lock);
	}

	return status;
}

static void sx150x_gpio_set(struct gpio_chip *gc, unsigned offset, int val)
{
	struct sx150x_chip *chip;

	chip = container_of(gc, struct sx150x_chip, gpio_chip);

	mutex_lock(&chip->lock);
	if (offset_is_oscio(chip, offset))
		sx150x_set_oscio(chip, val);
	else
		sx150x_set_io(chip, offset, val);
	mutex_unlock(&chip->lock);
}

static int sx150x_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct sx150x_chip *chip;
	int status = -EINVAL;

	chip = container_of(gc, struct sx150x_chip, gpio_chip);

	if (!offset_is_oscio(chip, offset)) {
		mutex_lock(&chip->lock);
		status = sx150x_io_input(chip, offset);
		mutex_unlock(&chip->lock);
	}
	return status;
}

static int sx150x_gpio_direction_output(struct gpio_chip *gc,
					unsigned offset,
					int val)
{
	struct sx150x_chip *chip;
	int status = 0;

	chip = container_of(gc, struct sx150x_chip, gpio_chip);

	if (!offset_is_oscio(chip, offset)) {
		mutex_lock(&chip->lock);
		status = sx150x_io_output(chip, offset, val);
		mutex_unlock(&chip->lock);
	}
	return status;
}

static int sx150x_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct sx150x_chip *chip;

	chip = container_of(gc, struct sx150x_chip, gpio_chip);

	if (offset >= chip->dev_cfg->ngpios)
		return -EINVAL;

	if (chip->irq_base < 0)
		return -EINVAL;

	return chip->irq_base + offset;
}

static void sx150x_irq_mask(struct irq_data *d)
{
	struct sx150x_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned n;

	n = d->irq - chip->irq_base;
	chip->irq_masked |= (1 << n);
	chip->irq_update = n;
}

static void sx150x_irq_unmask(struct irq_data *d)
{
	struct sx150x_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned n;

	n = d->irq - chip->irq_base;
	chip->irq_masked &= ~(1 << n);
	chip->irq_update = n;
}

static int sx150x_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct sx150x_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned n, val = 0;

	if (flow_type & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		return -EINVAL;

	n = d->irq - chip->irq_base;

	if (flow_type & IRQ_TYPE_EDGE_RISING)
		val |= 0x1;
	if (flow_type & IRQ_TYPE_EDGE_FALLING)
		val |= 0x2;

	chip->irq_sense &= ~(3UL << (n * 2));
	chip->irq_sense |= val << (n * 2);
	chip->irq_update = n;
	return 0;
}

static irqreturn_t sx150x_irq_thread_fn(int irq, void *dev_id)
{
	struct sx150x_chip *chip = (struct sx150x_chip *)dev_id;
	unsigned nhandled = 0;
	unsigned sub_irq;
	unsigned n;
	s32 err;
	u8 val;
	int i;

	for (i = (chip->dev_cfg->ngpios / 8) - 1; i >= 0; --i) {
		err = sx150x_i2c_read(chip->client,
				      chip->dev_cfg->reg_irq_src - i,
				      &val);
		if (err < 0)
			continue;

		sx150x_i2c_write(chip->client,
				chip->dev_cfg->reg_irq_src - i,
				val);
		for (n = 0; n < 8; ++n) {
			if (val & (1 << n)) {
				sub_irq = chip->irq_base + (i * 8) + n;
				handle_nested_irq(sub_irq);
				++nhandled;
			}
		}
	}

	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static void sx150x_irq_bus_lock(struct irq_data *d)
{
	struct sx150x_chip *chip = irq_data_get_irq_chip_data(d);

	mutex_lock(&chip->lock);
}

static void sx150x_irq_bus_sync_unlock(struct irq_data *d)
{
	struct sx150x_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned n;

	if (chip->irq_update == NO_UPDATE_PENDING)
		goto out;

	n = chip->irq_update;
	chip->irq_update = NO_UPDATE_PENDING;

	/* Avoid updates if nothing changed */
	if (chip->dev_sense == chip->irq_sense &&
	    chip->dev_sense == chip->irq_masked)
		goto out;

	chip->dev_sense = chip->irq_sense;
	chip->dev_masked = chip->irq_masked;

	if (chip->irq_masked & (1 << n)) {
		sx150x_write_cfg(chip, n, 1, chip->dev_cfg->reg_irq_mask, 1);
		sx150x_write_cfg(chip, n, 2, chip->dev_cfg->reg_sense, 0);
	} else {
		sx150x_write_cfg(chip, n, 1, chip->dev_cfg->reg_irq_mask, 0);
		sx150x_write_cfg(chip, n, 2, chip->dev_cfg->reg_sense,
				 chip->irq_sense >> (n * 2));
	}
out:
	mutex_unlock(&chip->lock);
}

static void sx150x_init_chip(struct sx150x_chip *chip,
			struct i2c_client *client,
			kernel_ulong_t driver_data,
			struct sx150x_platform_data *pdata)
{
	mutex_init(&chip->lock);

	chip->client                     = client;
	chip->dev_cfg                    = &sx150x_devices[driver_data];
	chip->gpio_chip.label            = client->name;
	chip->gpio_chip.direction_input  = sx150x_gpio_direction_input;
	chip->gpio_chip.direction_output = sx150x_gpio_direction_output;
	chip->gpio_chip.get              = sx150x_gpio_get;
	chip->gpio_chip.set              = sx150x_gpio_set;
	chip->gpio_chip.to_irq           = sx150x_gpio_to_irq;
	chip->gpio_chip.base             = pdata->gpio_base;
	chip->gpio_chip.can_sleep        = 1;
	chip->gpio_chip.ngpio            = chip->dev_cfg->ngpios;
	if (pdata->oscio_is_gpo)
		++chip->gpio_chip.ngpio;

	chip->irq_chip.name                = client->name;
	chip->irq_chip.irq_mask            = sx150x_irq_mask;
	chip->irq_chip.irq_unmask          = sx150x_irq_unmask;
	chip->irq_chip.irq_set_type        = sx150x_irq_set_type;
	chip->irq_chip.irq_bus_lock        = sx150x_irq_bus_lock;
	chip->irq_chip.irq_bus_sync_unlock = sx150x_irq_bus_sync_unlock;
	chip->irq_summary                  = -1;
	chip->irq_base                     = -1;
	chip->irq_masked                   = ~0;
	chip->irq_sense                    = 0;
	chip->dev_masked                   = ~0;
	chip->dev_sense                    = 0;
	chip->irq_update		   = NO_UPDATE_PENDING;
}

static int sx150x_init_io(struct sx150x_chip *chip, u8 base, u16 cfg)
{
	int err = 0;
	unsigned n;

	for (n = 0; err >= 0 && n < (chip->dev_cfg->ngpios / 8); ++n)
		err = sx150x_i2c_write(chip->client, base - n, cfg >> (n * 8));
	return err;
}

static int sx150x_reset(struct sx150x_chip *chip)
{
	int err;

	err = i2c_smbus_write_byte_data(chip->client,
					chip->dev_cfg->reg_reset,
					0x12);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(chip->client,
					chip->dev_cfg->reg_reset,
					0x34);
	return err;
}

static int sx150x_init_hw(struct sx150x_chip *chip,
			struct sx150x_platform_data *pdata)
{
	int err = 0;

	if (pdata->reset_during_probe) {
		err = sx150x_reset(chip);
		if (err < 0)
			return err;
	}

	err = sx150x_i2c_write(chip->client,
			chip->dev_cfg->reg_misc,
			0x01);
	if (err < 0)
		return err;

	err = sx150x_init_io(chip, chip->dev_cfg->reg_pullup,
			pdata->io_pullup_ena);
	if (err < 0)
		return err;

	err = sx150x_init_io(chip, chip->dev_cfg->reg_pulldn,
			pdata->io_pulldn_ena);
	if (err < 0)
		return err;

	err = sx150x_init_io(chip, chip->dev_cfg->reg_drain,
			pdata->io_open_drain_ena);
	if (err < 0)
		return err;

	err = sx150x_init_io(chip, chip->dev_cfg->reg_polarity,
			pdata->io_polarity);
	if (err < 0)
		return err;

	if (pdata->oscio_is_gpo)
		sx150x_set_oscio(chip, 0);

	return err;
}

static int sx150x_install_irq_chip(struct sx150x_chip *chip,
				int irq_summary,
				int irq_base)
{
	int err;
	unsigned n;
	unsigned irq;

	chip->irq_summary = irq_summary;
	chip->irq_base    = irq_base;

	for (n = 0; n < chip->dev_cfg->ngpios; ++n) {
		irq = irq_base + n;
		irq_set_chip_data(irq, chip);
		irq_set_chip_and_handler(irq, &chip->irq_chip, handle_edge_irq);
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	err = devm_request_threaded_irq(&chip->client->dev,
				irq_summary,
				NULL,
				sx150x_irq_thread_fn,
				IRQF_SHARED | IRQF_TRIGGER_FALLING,
				chip->irq_chip.name,
				chip);
	if (err < 0) {
		chip->irq_summary = -1;
		chip->irq_base    = -1;
	}

	return err;
}

static void sx150x_remove_irq_chip(struct sx150x_chip *chip)
{
	unsigned n;
	unsigned irq;

	for (n = 0; n < chip->dev_cfg->ngpios; ++n) {
		irq = chip->irq_base + n;
		irq_set_chip_and_handler(irq, NULL, NULL);
	}
}

static int sx150x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	static const u32 i2c_funcs = I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WRITE_WORD_DATA;
	struct sx150x_platform_data *pdata;
	struct sx150x_chip *chip;
	int rc;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		return -EINVAL;

	if (!i2c_check_functionality(client->adapter, i2c_funcs))
		return -ENOSYS;

	chip = devm_kzalloc(&client->dev,
		sizeof(struct sx150x_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	sx150x_init_chip(chip, client, id->driver_data, pdata);
	rc = sx150x_init_hw(chip, pdata);
	if (rc < 0)
		return rc;

	rc = gpiochip_add(&chip->gpio_chip);
	if (rc)
		return rc;

	if (pdata->irq_summary >= 0) {
		rc = sx150x_install_irq_chip(chip,
					pdata->irq_summary,
					pdata->irq_base);
		if (rc < 0)
			goto probe_fail_post_gpiochip_add;
	}

	i2c_set_clientdata(client, chip);

	return 0;
probe_fail_post_gpiochip_add:
	WARN_ON(gpiochip_remove(&chip->gpio_chip) < 0);
	return rc;
}

static int sx150x_remove(struct i2c_client *client)
{
	struct sx150x_chip *chip;
	int rc;

	chip = i2c_get_clientdata(client);
	rc = gpiochip_remove(&chip->gpio_chip);
	if (rc < 0)
		return rc;

	if (chip->irq_summary >= 0)
		sx150x_remove_irq_chip(chip);

	return 0;
}

static struct i2c_driver sx150x_driver = {
	.driver = {
		.name = "sx150x",
		.owner = THIS_MODULE
	},
	.probe    = sx150x_probe,
	.remove   = sx150x_remove,
	.id_table = sx150x_id,
};

static int __init sx150x_init(void)
{
	return i2c_add_driver(&sx150x_driver);
}
subsys_initcall(sx150x_init);

static void __exit sx150x_exit(void)
{
	return i2c_del_driver(&sx150x_driver);
}
module_exit(sx150x_exit);

MODULE_AUTHOR("Gregory Bean <gbean@codeaurora.org>");
MODULE_DESCRIPTION("Driver for Semtech SX150X I2C GPIO Expanders");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:sx150x");
