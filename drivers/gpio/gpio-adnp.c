// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-2012 Avionic Design GmbH
 */

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#define GPIO_DDR(gpio) (0x00 << (gpio)->reg_shift)
#define GPIO_PLR(gpio) (0x01 << (gpio)->reg_shift)
#define GPIO_IER(gpio) (0x02 << (gpio)->reg_shift)
#define GPIO_ISR(gpio) (0x03 << (gpio)->reg_shift)
#define GPIO_PTR(gpio) (0x04 << (gpio)->reg_shift)

struct adnp {
	struct i2c_client *client;
	struct gpio_chip gpio;
	unsigned int reg_shift;

	struct mutex i2c_lock;
	struct mutex irq_lock;

	u8 *irq_enable;
	u8 *irq_level;
	u8 *irq_rise;
	u8 *irq_fall;
	u8 *irq_high;
	u8 *irq_low;
};

static int adnp_read(struct adnp *adnp, unsigned offset, uint8_t *value)
{
	int err;

	err = i2c_smbus_read_byte_data(adnp->client, offset);
	if (err < 0) {
		dev_err(adnp->gpio.parent, "%s failed: %d\n",
			"i2c_smbus_read_byte_data()", err);
		return err;
	}

	*value = err;
	return 0;
}

static int adnp_write(struct adnp *adnp, unsigned offset, uint8_t value)
{
	int err;

	err = i2c_smbus_write_byte_data(adnp->client, offset, value);
	if (err < 0) {
		dev_err(adnp->gpio.parent, "%s failed: %d\n",
			"i2c_smbus_write_byte_data()", err);
		return err;
	}

	return 0;
}

static int adnp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *adnp = gpiochip_get_data(chip);
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	u8 value;
	int err;

	err = adnp_read(adnp, GPIO_PLR(adnp) + reg, &value);
	if (err < 0)
		return err;

	return (value & BIT(pos)) ? 1 : 0;
}

static void __adnp_gpio_set(struct adnp *adnp, unsigned offset, int value)
{
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	int err;
	u8 val;

	err = adnp_read(adnp, GPIO_PLR(adnp) + reg, &val);
	if (err < 0)
		return;

	if (value)
		val |= BIT(pos);
	else
		val &= ~BIT(pos);

	adnp_write(adnp, GPIO_PLR(adnp) + reg, val);
}

static void adnp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct adnp *adnp = gpiochip_get_data(chip);

	mutex_lock(&adnp->i2c_lock);
	__adnp_gpio_set(adnp, offset, value);
	mutex_unlock(&adnp->i2c_lock);
}

static int adnp_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *adnp = gpiochip_get_data(chip);
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	u8 value;
	int err;

	mutex_lock(&adnp->i2c_lock);

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &value);
	if (err < 0)
		goto out;

	value &= ~BIT(pos);

	err = adnp_write(adnp, GPIO_DDR(adnp) + reg, value);
	if (err < 0)
		goto out;

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &value);
	if (err < 0)
		goto out;

	if (value & BIT(pos)) {
		err = -EPERM;
		goto out;
	}

	err = 0;

out:
	mutex_unlock(&adnp->i2c_lock);
	return err;
}

static int adnp_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				      int value)
{
	struct adnp *adnp = gpiochip_get_data(chip);
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	int err;
	u8 val;

	mutex_lock(&adnp->i2c_lock);

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &val);
	if (err < 0)
		goto out;

	val |= BIT(pos);

	err = adnp_write(adnp, GPIO_DDR(adnp) + reg, val);
	if (err < 0)
		goto out;

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &val);
	if (err < 0)
		goto out;

	if (!(val & BIT(pos))) {
		err = -EPERM;
		goto out;
	}

	__adnp_gpio_set(adnp, offset, value);
	err = 0;

out:
	mutex_unlock(&adnp->i2c_lock);
	return err;
}

static void adnp_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct adnp *adnp = gpiochip_get_data(chip);
	unsigned int num_regs = 1 << adnp->reg_shift, i, j;
	int err;

	for (i = 0; i < num_regs; i++) {
		u8 ddr, plr, ier, isr;

		mutex_lock(&adnp->i2c_lock);

		err = adnp_read(adnp, GPIO_DDR(adnp) + i, &ddr);
		if (err < 0)
			goto unlock;

		err = adnp_read(adnp, GPIO_PLR(adnp) + i, &plr);
		if (err < 0)
			goto unlock;

		err = adnp_read(adnp, GPIO_IER(adnp) + i, &ier);
		if (err < 0)
			goto unlock;

		err = adnp_read(adnp, GPIO_ISR(adnp) + i, &isr);
		if (err < 0)
			goto unlock;

		mutex_unlock(&adnp->i2c_lock);

		for (j = 0; j < 8; j++) {
			unsigned int bit = (i << adnp->reg_shift) + j;
			const char *direction = "input ";
			const char *level = "low ";
			const char *interrupt = "disabled";
			const char *pending = "";

			if (ddr & BIT(j))
				direction = "output";

			if (plr & BIT(j))
				level = "high";

			if (ier & BIT(j))
				interrupt = "enabled ";

			if (isr & BIT(j))
				pending = "pending";

			seq_printf(s, "%2u: %s %s IRQ %s %s\n", bit,
				   direction, level, interrupt, pending);
		}
	}

	return;

unlock:
	mutex_unlock(&adnp->i2c_lock);
}

static irqreturn_t adnp_irq(int irq, void *data)
{
	struct adnp *adnp = data;
	unsigned int num_regs, i;

	num_regs = 1 << adnp->reg_shift;

	for (i = 0; i < num_regs; i++) {
		unsigned int base = i << adnp->reg_shift, bit;
		u8 changed, level, isr, ier;
		unsigned long pending;
		int err;

		mutex_lock(&adnp->i2c_lock);

		err = adnp_read(adnp, GPIO_PLR(adnp) + i, &level);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			continue;
		}

		err = adnp_read(adnp, GPIO_ISR(adnp) + i, &isr);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			continue;
		}

		err = adnp_read(adnp, GPIO_IER(adnp) + i, &ier);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			continue;
		}

		mutex_unlock(&adnp->i2c_lock);

		/* determine pins that changed levels */
		changed = level ^ adnp->irq_level[i];

		/* compute edge-triggered interrupts */
		pending = changed & ((adnp->irq_fall[i] & ~level) |
				     (adnp->irq_rise[i] & level));

		/* add in level-triggered interrupts */
		pending |= (adnp->irq_high[i] & level) |
			   (adnp->irq_low[i] & ~level);

		/* mask out non-pending and disabled interrupts */
		pending &= isr & ier;

		for_each_set_bit(bit, &pending, 8) {
			unsigned int child_irq;
			child_irq = irq_find_mapping(adnp->gpio.irq.domain,
						     base + bit);
			handle_nested_irq(child_irq);
		}
	}

	return IRQ_HANDLED;
}

static void adnp_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adnp *adnp = gpiochip_get_data(gc);
	unsigned int reg = d->hwirq >> adnp->reg_shift;
	unsigned int pos = d->hwirq & 7;

	adnp->irq_enable[reg] &= ~BIT(pos);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void adnp_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adnp *adnp = gpiochip_get_data(gc);
	unsigned int reg = d->hwirq >> adnp->reg_shift;
	unsigned int pos = d->hwirq & 7;

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	adnp->irq_enable[reg] |= BIT(pos);
}

static int adnp_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adnp *adnp = gpiochip_get_data(gc);
	unsigned int reg = d->hwirq >> adnp->reg_shift;
	unsigned int pos = d->hwirq & 7;

	if (type & IRQ_TYPE_EDGE_RISING)
		adnp->irq_rise[reg] |= BIT(pos);
	else
		adnp->irq_rise[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_EDGE_FALLING)
		adnp->irq_fall[reg] |= BIT(pos);
	else
		adnp->irq_fall[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_LEVEL_HIGH)
		adnp->irq_high[reg] |= BIT(pos);
	else
		adnp->irq_high[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_LEVEL_LOW)
		adnp->irq_low[reg] |= BIT(pos);
	else
		adnp->irq_low[reg] &= ~BIT(pos);

	return 0;
}

static void adnp_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adnp *adnp = gpiochip_get_data(gc);

	mutex_lock(&adnp->irq_lock);
}

static void adnp_irq_bus_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adnp *adnp = gpiochip_get_data(gc);
	unsigned int num_regs = 1 << adnp->reg_shift, i;

	mutex_lock(&adnp->i2c_lock);

	for (i = 0; i < num_regs; i++)
		adnp_write(adnp, GPIO_IER(adnp) + i, adnp->irq_enable[i]);

	mutex_unlock(&adnp->i2c_lock);
	mutex_unlock(&adnp->irq_lock);
}

static const struct irq_chip adnp_irq_chip = {
	.name = "gpio-adnp",
	.irq_mask = adnp_irq_mask,
	.irq_unmask = adnp_irq_unmask,
	.irq_set_type = adnp_irq_set_type,
	.irq_bus_lock = adnp_irq_bus_lock,
	.irq_bus_sync_unlock = adnp_irq_bus_unlock,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int adnp_irq_setup(struct adnp *adnp)
{
	unsigned int num_regs = 1 << adnp->reg_shift, i;
	struct gpio_chip *chip = &adnp->gpio;
	int err;

	mutex_init(&adnp->irq_lock);

	/*
	 * Allocate memory to keep track of the current level and trigger
	 * modes of the interrupts. To avoid multiple allocations, a single
	 * large buffer is allocated and pointers are setup to point at the
	 * corresponding offsets. For consistency, the layout of the buffer
	 * is chosen to match the register layout of the hardware in that
	 * each segment contains the corresponding bits for all interrupts.
	 */
	adnp->irq_enable = devm_kcalloc(chip->parent, num_regs, 6,
					GFP_KERNEL);
	if (!adnp->irq_enable)
		return -ENOMEM;

	adnp->irq_level = adnp->irq_enable + (num_regs * 1);
	adnp->irq_rise = adnp->irq_enable + (num_regs * 2);
	adnp->irq_fall = adnp->irq_enable + (num_regs * 3);
	adnp->irq_high = adnp->irq_enable + (num_regs * 4);
	adnp->irq_low = adnp->irq_enable + (num_regs * 5);

	for (i = 0; i < num_regs; i++) {
		/*
		 * Read the initial level of all pins to allow the emulation
		 * of edge triggered interrupts.
		 */
		err = adnp_read(adnp, GPIO_PLR(adnp) + i, &adnp->irq_level[i]);
		if (err < 0)
			return err;

		/* disable all interrupts */
		err = adnp_write(adnp, GPIO_IER(adnp) + i, 0);
		if (err < 0)
			return err;

		adnp->irq_enable[i] = 0x00;
	}

	err = devm_request_threaded_irq(chip->parent, adnp->client->irq,
					NULL, adnp_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(chip->parent), adnp);
	if (err != 0) {
		dev_err(chip->parent, "can't request IRQ#%d: %d\n",
			adnp->client->irq, err);
		return err;
	}

	return 0;
}

static int adnp_gpio_setup(struct adnp *adnp, unsigned int num_gpios,
			   bool is_irq_controller)
{
	struct gpio_chip *chip = &adnp->gpio;
	int err;

	adnp->reg_shift = get_count_order(num_gpios) - 3;

	chip->direction_input = adnp_gpio_direction_input;
	chip->direction_output = adnp_gpio_direction_output;
	chip->get = adnp_gpio_get;
	chip->set = adnp_gpio_set;
	chip->can_sleep = true;

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		chip->dbg_show = adnp_gpio_dbg_show;

	chip->base = -1;
	chip->ngpio = num_gpios;
	chip->label = adnp->client->name;
	chip->parent = &adnp->client->dev;
	chip->owner = THIS_MODULE;

	if (is_irq_controller) {
		struct gpio_irq_chip *girq;

		err = adnp_irq_setup(adnp);
		if (err)
			return err;

		girq = &chip->irq;
		gpio_irq_chip_set_chip(girq, &adnp_irq_chip);

		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
		girq->threaded = true;
	}

	err = devm_gpiochip_add_data(&adnp->client->dev, chip, adnp);
	if (err)
		return err;

	return 0;
}

static int adnp_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct adnp *adnp;
	u32 num_gpios;
	int err;

	err = device_property_read_u32(dev, "nr-gpios", &num_gpios);
	if (err < 0)
		return err;

	adnp = devm_kzalloc(&client->dev, sizeof(*adnp), GFP_KERNEL);
	if (!adnp)
		return -ENOMEM;

	mutex_init(&adnp->i2c_lock);
	adnp->client = client;

	err = adnp_gpio_setup(adnp, num_gpios, device_property_read_bool(dev, "interrupt-controller"));
	if (err)
		return err;

	i2c_set_clientdata(client, adnp);

	return 0;
}

static const struct i2c_device_id adnp_i2c_id[] = {
	{ "gpio-adnp" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, adnp_i2c_id);

static const struct of_device_id adnp_of_match[] = {
	{ .compatible = "ad,gpio-adnp", },
	{ },
};
MODULE_DEVICE_TABLE(of, adnp_of_match);

static struct i2c_driver adnp_i2c_driver = {
	.driver = {
		.name = "gpio-adnp",
		.of_match_table = adnp_of_match,
	},
	.probe_new = adnp_i2c_probe,
	.id_table = adnp_i2c_id,
};
module_i2c_driver(adnp_i2c_driver);

MODULE_DESCRIPTION("Avionic Design N-bit GPIO expander");
MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_LICENSE("GPL");
