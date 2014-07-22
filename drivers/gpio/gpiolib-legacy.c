#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>

#include <linux/gpio.h>

#include "gpiolib.h"

/* Warn when drivers omit gpio_request() calls -- legal but ill-advised
 * when setting direction, and otherwise illegal.  Until board setup code
 * and drivers use explicit requests everywhere (which won't happen when
 * those calls have no teeth) we can't avoid autorequesting.  This nag
 * message should motivate switching to explicit requests... so should
 * the weaker cleanup after faults, compared to gpio_request().
 *
 * NOTE: the autorequest mechanism is going away; at this point it's
 * only "legal" in the sense that (old) code using it won't break yet,
 * but instead only triggers a WARN() stack dump.
 */
static int gpio_ensure_requested(struct gpio_desc *desc)
{
	struct gpio_chip *chip = desc->chip;
	unsigned long flags;
	bool request = false;
	int err = 0;

	spin_lock_irqsave(&gpio_lock, flags);

	if (WARN(test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0,
			"autorequest GPIO-%d\n", desc_to_gpio(desc))) {
		if (!try_module_get(chip->owner)) {
			gpiod_err(desc, "%s: module can't be gotten\n",
					__func__);
			clear_bit(FLAG_REQUESTED, &desc->flags);
			/* lose */
			err = -EIO;
			goto end;
		}
		desc->label = "[auto]";
		/* caller must chip->request() w/o spinlock */
		if (chip->request)
			request = true;
	}

end:
	spin_unlock_irqrestore(&gpio_lock, flags);

	if (request) {
		might_sleep_if(chip->can_sleep);
		err = chip->request(chip, gpio_chip_hwgpio(desc));

		if (err < 0) {
			gpiod_dbg(desc, "%s: chip request fail, %d\n",
					__func__, err);
			spin_lock_irqsave(&gpio_lock, flags);

			desc->label = NULL;
			clear_bit(FLAG_REQUESTED, &desc->flags);

			spin_unlock_irqrestore(&gpio_lock, flags);
		}
	}

	return err;
}

void gpio_free(unsigned gpio)
{
	gpiod_free(gpio_to_desc(gpio));
}
EXPORT_SYMBOL_GPL(gpio_free);

/**
 * gpio_request_one - request a single GPIO with initial configuration
 * @gpio:	the GPIO number
 * @flags:	GPIO configuration as specified by GPIOF_*
 * @label:	a literal description string of this GPIO
 */
int gpio_request_one(unsigned gpio, unsigned long flags, const char *label)
{
	struct gpio_desc *desc;
	int err;

	desc = gpio_to_desc(gpio);

	err = gpiod_request(desc, label);
	if (err)
		return err;

	if (flags & GPIOF_OPEN_DRAIN)
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);

	if (flags & GPIOF_OPEN_SOURCE)
		set_bit(FLAG_OPEN_SOURCE, &desc->flags);

	if (flags & GPIOF_ACTIVE_LOW)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);

	if (flags & GPIOF_DIR_IN)
		err = gpiod_direction_input(desc);
	else
		err = gpiod_direction_output_raw(desc,
				(flags & GPIOF_INIT_HIGH) ? 1 : 0);

	if (err)
		goto free_gpio;

	if (flags & GPIOF_EXPORT) {
		err = gpiod_export(desc, flags & GPIOF_EXPORT_CHANGEABLE);
		if (err)
			goto free_gpio;
	}

	return 0;

 free_gpio:
	gpiod_free(desc);
	return err;
}
EXPORT_SYMBOL_GPL(gpio_request_one);

int gpio_request(unsigned gpio, const char *label)
{
	return gpiod_request(gpio_to_desc(gpio), label);
}
EXPORT_SYMBOL_GPL(gpio_request);

/**
 * gpio_request_array - request multiple GPIOs in a single call
 * @array:	array of the 'struct gpio'
 * @num:	how many GPIOs in the array
 */
int gpio_request_array(const struct gpio *array, size_t num)
{
	int i, err;

	for (i = 0; i < num; i++, array++) {
		err = gpio_request_one(array->gpio, array->flags, array->label);
		if (err)
			goto err_free;
	}
	return 0;

err_free:
	while (i--)
		gpio_free((--array)->gpio);
	return err;
}
EXPORT_SYMBOL_GPL(gpio_request_array);

/**
 * gpio_free_array - release multiple GPIOs in a single call
 * @array:	array of the 'struct gpio'
 * @num:	how many GPIOs in the array
 */
void gpio_free_array(const struct gpio *array, size_t num)
{
	while (num--)
		gpio_free((array++)->gpio);
}
EXPORT_SYMBOL_GPL(gpio_free_array);

int gpio_direction_input(unsigned gpio)
{
	struct gpio_desc *desc = gpio_to_desc(gpio);
	int err;

	if (!desc)
		return -EINVAL;

	err = gpio_ensure_requested(desc);
	if (err < 0)
		return err;

	return gpiod_direction_input(desc);
}
EXPORT_SYMBOL_GPL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	struct gpio_desc *desc = gpio_to_desc(gpio);
	int err;

	if (!desc)
		return -EINVAL;

	err = gpio_ensure_requested(desc);
	if (err < 0)
		return err;

	return gpiod_direction_output_raw(desc, value);
}
EXPORT_SYMBOL_GPL(gpio_direction_output);

int gpio_set_debounce(unsigned gpio, unsigned debounce)
{
	struct gpio_desc *desc = gpio_to_desc(gpio);
	int err;

	if (!desc)
		return -EINVAL;

	err = gpio_ensure_requested(desc);
	if (err < 0)
		return err;

	return gpiod_set_debounce(desc, debounce);
}
EXPORT_SYMBOL_GPL(gpio_set_debounce);
