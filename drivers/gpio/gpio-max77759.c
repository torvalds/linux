// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright 2020 Google Inc
// Copyright 2025 Linaro Ltd.
//
// GPIO driver for Maxim MAX77759

#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/lockdep.h>
#include <linux/mfd/max77759.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#define MAX77759_N_GPIOS   ARRAY_SIZE(max77759_gpio_line_names)
static const char * const max77759_gpio_line_names[] = { "GPIO5", "GPIO6" };

struct max77759_gpio_chip {
	struct regmap *map;
	struct max77759 *max77759;
	struct gpio_chip gc;
	struct mutex maxq_lock; /* protect MaxQ r/m/w operations */

	struct mutex irq_lock; /* protect irq bus */
	int irq_mask;
	int irq_mask_changed;
	int irq_trig;
	int irq_trig_changed;
};

#define MAX77759_GPIOx_TRIGGER(offs, val) (((val) & 1) << (offs))
#define MAX77759_GPIOx_TRIGGER_MASK(offs) MAX77759_GPIOx_TRIGGER(offs, ~0)
enum max77759_trigger_gpio_type {
	MAX77759_GPIO_TRIGGER_RISING = 0,
	MAX77759_GPIO_TRIGGER_FALLING = 1
};

#define MAX77759_GPIOx_DIR(offs, dir) (((dir) & 1) << (2 + (3 * (offs))))
#define MAX77759_GPIOx_DIR_MASK(offs) MAX77759_GPIOx_DIR(offs, ~0)
enum max77759_control_gpio_dir {
	MAX77759_GPIO_DIR_IN = 0,
	MAX77759_GPIO_DIR_OUT = 1
};

#define MAX77759_GPIOx_OUTVAL(offs, val) (((val) & 1) << (3 + (3 * (offs))))
#define MAX77759_GPIOx_OUTVAL_MASK(offs) MAX77759_GPIOx_OUTVAL(offs, ~0)

#define MAX77759_GPIOx_INVAL_MASK(offs) (BIT(4) << (3 * (offs)))

static int max77759_gpio_maxq_gpio_trigger_read(struct max77759_gpio_chip *chip)
{
	DEFINE_FLEX(struct max77759_maxq_command, cmd, cmd, length, 1);
	DEFINE_FLEX(struct max77759_maxq_response, rsp, rsp, length, 2);
	int ret;

	cmd->cmd[0] = MAX77759_MAXQ_OPCODE_GPIO_TRIGGER_READ;

	ret = max77759_maxq_command(chip->max77759, cmd, rsp);
	if (ret < 0)
		return ret;

	return rsp->rsp[1];
}

static int max77759_gpio_maxq_gpio_trigger_write(struct max77759_gpio_chip *chip,
						 u8 trigger)
{
	DEFINE_FLEX(struct max77759_maxq_command, cmd, cmd, length, 2);

	cmd->cmd[0] = MAX77759_MAXQ_OPCODE_GPIO_TRIGGER_WRITE;
	cmd->cmd[1] = trigger;

	return max77759_maxq_command(chip->max77759, cmd, NULL);
}

static int max77759_gpio_maxq_gpio_control_read(struct max77759_gpio_chip *chip)
{
	DEFINE_FLEX(struct max77759_maxq_command, cmd, cmd, length, 1);
	DEFINE_FLEX(struct max77759_maxq_response, rsp, rsp, length, 2);
	int ret;

	cmd->cmd[0] = MAX77759_MAXQ_OPCODE_GPIO_CONTROL_READ;

	ret = max77759_maxq_command(chip->max77759, cmd, rsp);
	if (ret < 0)
		return ret;

	return rsp->rsp[1];
}

static int max77759_gpio_maxq_gpio_control_write(struct max77759_gpio_chip *chip,
						 u8 ctrl)
{
	DEFINE_FLEX(struct max77759_maxq_command, cmd, cmd, length, 2);

	cmd->cmd[0] = MAX77759_MAXQ_OPCODE_GPIO_CONTROL_WRITE;
	cmd->cmd[1] = ctrl;

	return max77759_maxq_command(chip->max77759, cmd, NULL);
}

static int
max77759_gpio_direction_from_control(int ctrl, unsigned int offset)
{
	enum max77759_control_gpio_dir dir;

	dir = !!(ctrl & MAX77759_GPIOx_DIR_MASK(offset));
	return ((dir == MAX77759_GPIO_DIR_OUT)
		? GPIO_LINE_DIRECTION_OUT
		: GPIO_LINE_DIRECTION_IN);
}

static int max77759_gpio_get_direction(struct gpio_chip *gc,
				       unsigned int offset)
{
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	int ctrl;

	ctrl = max77759_gpio_maxq_gpio_control_read(chip);
	if (ctrl < 0)
		return ctrl;

	return max77759_gpio_direction_from_control(ctrl, offset);
}

static int max77759_gpio_direction_helper(struct gpio_chip *gc,
					  unsigned int offset,
					  enum max77759_control_gpio_dir dir,
					  int value)
{
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	int ctrl, new_ctrl;

	guard(mutex)(&chip->maxq_lock);

	ctrl = max77759_gpio_maxq_gpio_control_read(chip);
	if (ctrl < 0)
		return ctrl;

	new_ctrl = ctrl & ~MAX77759_GPIOx_DIR_MASK(offset);
	new_ctrl |= MAX77759_GPIOx_DIR(offset, dir);

	if (dir == MAX77759_GPIO_DIR_OUT) {
		new_ctrl &= ~MAX77759_GPIOx_OUTVAL_MASK(offset);
		new_ctrl |= MAX77759_GPIOx_OUTVAL(offset, value);
	}

	if (new_ctrl == ctrl)
		return 0;

	return max77759_gpio_maxq_gpio_control_write(chip, new_ctrl);
}

static int max77759_gpio_direction_input(struct gpio_chip *gc,
					 unsigned int offset)
{
	return max77759_gpio_direction_helper(gc, offset,
					      MAX77759_GPIO_DIR_IN, -1);
}

static int max77759_gpio_direction_output(struct gpio_chip *gc,
					  unsigned int offset, int value)
{
	return max77759_gpio_direction_helper(gc, offset,
					      MAX77759_GPIO_DIR_OUT, value);
}

static int max77759_gpio_get_value(struct gpio_chip *gc, unsigned int offset)
{
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	int ctrl, mask;

	ctrl = max77759_gpio_maxq_gpio_control_read(chip);
	if (ctrl < 0)
		return ctrl;

	/*
	 * The input status bit doesn't reflect the pin state when the GPIO is
	 * configured as an output. Check the direction, and inspect the input
	 * or output bit accordingly.
	 */
	mask = ((max77759_gpio_direction_from_control(ctrl, offset)
		 == GPIO_LINE_DIRECTION_IN)
		? MAX77759_GPIOx_INVAL_MASK(offset)
		: MAX77759_GPIOx_OUTVAL_MASK(offset));

	return !!(ctrl & mask);
}

static int max77759_gpio_set_value(struct gpio_chip *gc,
				   unsigned int offset, int value)
{
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	int ctrl, new_ctrl;

	guard(mutex)(&chip->maxq_lock);

	ctrl = max77759_gpio_maxq_gpio_control_read(chip);
	if (ctrl < 0)
		return ctrl;

	new_ctrl = ctrl & ~MAX77759_GPIOx_OUTVAL_MASK(offset);
	new_ctrl |= MAX77759_GPIOx_OUTVAL(offset, value);

	if (new_ctrl == ctrl)
		return 0;

	return max77759_gpio_maxq_gpio_control_write(chip, new_ctrl);
}

static void max77759_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	chip->irq_mask &= ~MAX77759_MAXQ_REG_UIC_INT1_GPIOxI_MASK(hwirq);
	chip->irq_mask |= MAX77759_MAXQ_REG_UIC_INT1_GPIOxI(hwirq, 1);
	chip->irq_mask_changed |= MAX77759_MAXQ_REG_UIC_INT1_GPIOxI(hwirq, 1);

	gpiochip_disable_irq(gc, hwirq);
}

static void max77759_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);

	chip->irq_mask &= ~MAX77759_MAXQ_REG_UIC_INT1_GPIOxI_MASK(hwirq);
	chip->irq_mask |= MAX77759_MAXQ_REG_UIC_INT1_GPIOxI(hwirq, 0);
	chip->irq_mask_changed |= MAX77759_MAXQ_REG_UIC_INT1_GPIOxI(hwirq, 1);
}

static int max77759_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	chip->irq_trig &= ~MAX77759_GPIOx_TRIGGER_MASK(hwirq);
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		chip->irq_trig |= MAX77759_GPIOx_TRIGGER(hwirq,
						MAX77759_GPIO_TRIGGER_RISING);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		chip->irq_trig |= MAX77759_GPIOx_TRIGGER(hwirq,
						MAX77759_GPIO_TRIGGER_FALLING);
		break;

	default:
		return -EINVAL;
	}

	chip->irq_trig_changed |= MAX77759_GPIOx_TRIGGER(hwirq, 1);

	return 0;
}

static void max77759_gpio_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->irq_lock);
}

static int max77759_gpio_bus_sync_unlock_helper(struct gpio_chip *gc,
						struct max77759_gpio_chip *chip)
					       __must_hold(&chip->maxq_lock)
{
	int ctrl, trigger, new_trigger, new_ctrl;
	unsigned long irq_trig_changed;
	int offset;
	int ret;

	lockdep_assert_held(&chip->maxq_lock);

	ctrl = max77759_gpio_maxq_gpio_control_read(chip);
	trigger = max77759_gpio_maxq_gpio_trigger_read(chip);
	if (ctrl < 0 || trigger < 0) {
		dev_err(gc->parent, "failed to read current state: %d / %d\n",
			ctrl, trigger);
		return (ctrl < 0) ? ctrl : trigger;
	}

	new_trigger = trigger & ~chip->irq_trig_changed;
	new_trigger |= (chip->irq_trig & chip->irq_trig_changed);

	/* change GPIO direction if required */
	new_ctrl = ctrl;
	irq_trig_changed = chip->irq_trig_changed;
	for_each_set_bit(offset, &irq_trig_changed, MAX77759_N_GPIOS) {
		new_ctrl &= ~MAX77759_GPIOx_DIR_MASK(offset);
		new_ctrl |= MAX77759_GPIOx_DIR(offset, MAX77759_GPIO_DIR_IN);
	}

	if (new_trigger != trigger) {
		ret = max77759_gpio_maxq_gpio_trigger_write(chip, new_trigger);
		if (ret) {
			dev_err(gc->parent,
				"failed to write new trigger: %d\n", ret);
			return ret;
		}
	}

	if (new_ctrl != ctrl) {
		ret = max77759_gpio_maxq_gpio_control_write(chip, new_ctrl);
		if (ret) {
			dev_err(gc->parent,
				"failed to write new control: %d\n", ret);
			return ret;
		}
	}

	chip->irq_trig_changed = 0;

	return 0;
}

static void max77759_gpio_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct max77759_gpio_chip *chip = gpiochip_get_data(gc);
	int ret;

	scoped_guard(mutex, &chip->maxq_lock) {
		ret = max77759_gpio_bus_sync_unlock_helper(gc, chip);
		if (ret)
			goto out_unlock;
	}

	ret = regmap_update_bits(chip->map,
				 MAX77759_MAXQ_REG_UIC_INT1_M,
				 chip->irq_mask_changed, chip->irq_mask);
	if (ret) {
		dev_err(gc->parent,
			"failed to update UIC_INT1 irq mask: %d\n", ret);
		goto out_unlock;
	}

	chip->irq_mask_changed = 0;

out_unlock:
	mutex_unlock(&chip->irq_lock);
}

static void max77759_gpio_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	seq_puts(p, dev_name(gc->parent));
}

static const struct irq_chip max77759_gpio_irq_chip = {
	.irq_mask		= max77759_gpio_irq_mask,
	.irq_unmask		= max77759_gpio_irq_unmask,
	.irq_set_type		= max77759_gpio_set_irq_type,
	.irq_bus_lock		= max77759_gpio_bus_lock,
	.irq_bus_sync_unlock	= max77759_gpio_bus_sync_unlock,
	.irq_print_chip		= max77759_gpio_irq_print_chip,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t max77759_gpio_irqhandler(int irq, void *data)
{
	struct max77759_gpio_chip *chip = data;
	struct gpio_chip *gc = &chip->gc;
	bool handled = false;

	/* iterate until no interrupt is pending */
	while (true) {
		unsigned int uic_int1;
		int ret;
		unsigned long pending;
		int offset;

		ret = regmap_read(chip->map, MAX77759_MAXQ_REG_UIC_INT1,
				  &uic_int1);
		if (ret < 0) {
			dev_err_ratelimited(gc->parent,
					    "failed to read IRQ status: %d\n",
					    ret);
			/*
			 * If !handled, we have looped not even once, which
			 * means we should return IRQ_NONE in that case (and
			 * of course IRQ_HANDLED otherwise).
			 */
			return IRQ_RETVAL(handled);
		}

		pending = uic_int1;
		pending &= (MAX77759_MAXQ_REG_UIC_INT1_GPIO6I
			    | MAX77759_MAXQ_REG_UIC_INT1_GPIO5I);
		if (!pending)
			break;

		for_each_set_bit(offset, &pending, MAX77759_N_GPIOS) {
			/*
			 * ACK interrupt by writing 1 to bit 'offset', all
			 * others need to be written as 0. This needs to be
			 * done unconditionally hence regmap_set_bits() is
			 * inappropriate here.
			 */
			regmap_write(chip->map, MAX77759_MAXQ_REG_UIC_INT1,
				     BIT(offset));

			handle_nested_irq(irq_find_mapping(gc->irq.domain,
							   offset));

			handled = true;
		}
	}

	return IRQ_RETVAL(handled);
}

static int max77759_gpio_probe(struct platform_device *pdev)
{
	struct max77759_gpio_chip *chip;
	int irq;
	struct gpio_irq_chip *girq;
	int ret;
	unsigned long irq_flags;
	struct irq_data *irqd;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->map = dev_get_regmap(pdev->dev.parent, "maxq");
	if (!chip->map)
		return dev_err_probe(&pdev->dev, -ENODEV, "Missing regmap\n");

	irq = platform_get_irq_byname(pdev, "GPI");
	if (irq < 0)
		return dev_err_probe(&pdev->dev, irq, "Failed to get IRQ\n");

	chip->max77759 = dev_get_drvdata(pdev->dev.parent);
	ret = devm_mutex_init(&pdev->dev, &chip->maxq_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&pdev->dev, &chip->irq_lock);
	if (ret)
		return ret;

	chip->gc.base = -1;
	chip->gc.label = dev_name(&pdev->dev);
	chip->gc.parent = &pdev->dev;
	chip->gc.can_sleep = true;

	chip->gc.names = max77759_gpio_line_names;
	chip->gc.ngpio = MAX77759_N_GPIOS;
	chip->gc.get_direction = max77759_gpio_get_direction;
	chip->gc.direction_input = max77759_gpio_direction_input;
	chip->gc.direction_output = max77759_gpio_direction_output;
	chip->gc.get = max77759_gpio_get_value;
	chip->gc.set = max77759_gpio_set_value;

	girq = &chip->gc.irq;
	gpio_irq_chip_set_chip(girq, &max77759_gpio_irq_chip);
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	girq->threaded = true;

	ret = devm_gpiochip_add_data(&pdev->dev, &chip->gc, chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to add GPIO chip\n");

	irq_flags = IRQF_ONESHOT | IRQF_SHARED;
	irqd = irq_get_irq_data(irq);
	if (irqd)
		irq_flags |= irqd_get_trigger_type(irqd);

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					max77759_gpio_irqhandler, irq_flags,
					dev_name(&pdev->dev), chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request IRQ\n");

	return ret;
}

static const struct of_device_id max77759_gpio_of_id[] = {
	{ .compatible = "maxim,max77759-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, max77759_gpio_of_id);

static const struct platform_device_id max77759_gpio_platform_id[] = {
	{ "max77759-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(platform, max77759_gpio_platform_id);

static struct platform_driver max77759_gpio_driver = {
	.driver = {
		.name = "max77759-gpio",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = max77759_gpio_of_id,
	},
	.probe = max77759_gpio_probe,
	.id_table = max77759_gpio_platform_id,
};

module_platform_driver(max77759_gpio_driver);

MODULE_AUTHOR("André Draszik <andre.draszik@linaro.org>");
MODULE_DESCRIPTION("GPIO driver for Maxim MAX77759");
MODULE_LICENSE("GPL");
