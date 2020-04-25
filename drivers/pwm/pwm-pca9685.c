// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for PCA9685 16-channel 12-bit PWM LED controller
 *
 * Copyright (C) 2013 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 * Copyright (C) 2015 Clemens Gruber <clemens.gruber@pqgruber.com>
 *
 * based on the pwm-twl-led.c driver
 */

#include <linux/acpi.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/bitmap.h>

/*
 * Because the PCA9685 has only one prescaler per chip, changing the period of
 * one channel affects the period of all 16 PWM outputs!
 * However, the ratio between each configured duty cycle and the chip-wide
 * period remains constant, because the OFF time is set in proportion to the
 * counter range.
 */

#define PCA9685_MODE1		0x00
#define PCA9685_MODE2		0x01
#define PCA9685_SUBADDR1	0x02
#define PCA9685_SUBADDR2	0x03
#define PCA9685_SUBADDR3	0x04
#define PCA9685_ALLCALLADDR	0x05
#define PCA9685_LEDX_ON_L	0x06
#define PCA9685_LEDX_ON_H	0x07
#define PCA9685_LEDX_OFF_L	0x08
#define PCA9685_LEDX_OFF_H	0x09

#define PCA9685_ALL_LED_ON_L	0xFA
#define PCA9685_ALL_LED_ON_H	0xFB
#define PCA9685_ALL_LED_OFF_L	0xFC
#define PCA9685_ALL_LED_OFF_H	0xFD
#define PCA9685_PRESCALE	0xFE

#define PCA9685_PRESCALE_MIN	0x03	/* => max. frequency of 1526 Hz */
#define PCA9685_PRESCALE_MAX	0xFF	/* => min. frequency of 24 Hz */

#define PCA9685_COUNTER_RANGE	4096
#define PCA9685_DEFAULT_PERIOD	5000000	/* Default period_ns = 1/200 Hz */
#define PCA9685_OSC_CLOCK_MHZ	25	/* Internal oscillator with 25 MHz */

#define PCA9685_NUMREGS		0xFF
#define PCA9685_MAXCHAN		0x10

#define LED_FULL		(1 << 4)
#define MODE1_SLEEP		(1 << 4)
#define MODE2_INVRT		(1 << 4)
#define MODE2_OUTDRV		(1 << 2)

#define LED_N_ON_H(N)	(PCA9685_LEDX_ON_H + (4 * (N)))
#define LED_N_ON_L(N)	(PCA9685_LEDX_ON_L + (4 * (N)))
#define LED_N_OFF_H(N)	(PCA9685_LEDX_OFF_H + (4 * (N)))
#define LED_N_OFF_L(N)	(PCA9685_LEDX_OFF_L + (4 * (N)))

struct pca9685 {
	struct pwm_chip chip;
	struct regmap *regmap;
	int period_ns;
#if IS_ENABLED(CONFIG_GPIOLIB)
	struct mutex lock;
	struct gpio_chip gpio;
	DECLARE_BITMAP(pwms_inuse, PCA9685_MAXCHAN + 1);
#endif
};

static inline struct pca9685 *to_pca(struct pwm_chip *chip)
{
	return container_of(chip, struct pca9685, chip);
}

#if IS_ENABLED(CONFIG_GPIOLIB)
static bool pca9685_pwm_test_and_set_inuse(struct pca9685 *pca, int pwm_idx)
{
	bool is_inuse;

	mutex_lock(&pca->lock);
	if (pwm_idx >= PCA9685_MAXCHAN) {
		/*
		 * "all LEDs" channel:
		 * pretend already in use if any of the PWMs are requested
		 */
		if (!bitmap_empty(pca->pwms_inuse, PCA9685_MAXCHAN)) {
			is_inuse = true;
			goto out;
		}
	} else {
		/*
		 * regular channel:
		 * pretend already in use if the "all LEDs" channel is requested
		 */
		if (test_bit(PCA9685_MAXCHAN, pca->pwms_inuse)) {
			is_inuse = true;
			goto out;
		}
	}
	is_inuse = test_and_set_bit(pwm_idx, pca->pwms_inuse);
out:
	mutex_unlock(&pca->lock);
	return is_inuse;
}

static void pca9685_pwm_clear_inuse(struct pca9685 *pca, int pwm_idx)
{
	mutex_lock(&pca->lock);
	clear_bit(pwm_idx, pca->pwms_inuse);
	mutex_unlock(&pca->lock);
}

static int pca9685_pwm_gpio_request(struct gpio_chip *gpio, unsigned int offset)
{
	struct pca9685 *pca = gpiochip_get_data(gpio);

	if (pca9685_pwm_test_and_set_inuse(pca, offset))
		return -EBUSY;
	pm_runtime_get_sync(pca->chip.dev);
	return 0;
}

static int pca9685_pwm_gpio_get(struct gpio_chip *gpio, unsigned int offset)
{
	struct pca9685 *pca = gpiochip_get_data(gpio);
	struct pwm_device *pwm = &pca->chip.pwms[offset];
	unsigned int value;

	regmap_read(pca->regmap, LED_N_ON_H(pwm->hwpwm), &value);

	return value & LED_FULL;
}

static void pca9685_pwm_gpio_set(struct gpio_chip *gpio, unsigned int offset,
				 int value)
{
	struct pca9685 *pca = gpiochip_get_data(gpio);
	struct pwm_device *pwm = &pca->chip.pwms[offset];
	unsigned int on = value ? LED_FULL : 0;

	/* Clear both OFF registers */
	regmap_write(pca->regmap, LED_N_OFF_L(pwm->hwpwm), 0);
	regmap_write(pca->regmap, LED_N_OFF_H(pwm->hwpwm), 0);

	/* Set the full ON bit */
	regmap_write(pca->regmap, LED_N_ON_H(pwm->hwpwm), on);
}

static void pca9685_pwm_gpio_free(struct gpio_chip *gpio, unsigned int offset)
{
	struct pca9685 *pca = gpiochip_get_data(gpio);

	pca9685_pwm_gpio_set(gpio, offset, 0);
	pm_runtime_put(pca->chip.dev);
	pca9685_pwm_clear_inuse(pca, offset);
}

static int pca9685_pwm_gpio_get_direction(struct gpio_chip *chip,
					  unsigned int offset)
{
	/* Always out */
	return GPIO_LINE_DIRECTION_OUT;
}

static int pca9685_pwm_gpio_direction_input(struct gpio_chip *gpio,
					    unsigned int offset)
{
	return -EINVAL;
}

static int pca9685_pwm_gpio_direction_output(struct gpio_chip *gpio,
					     unsigned int offset, int value)
{
	pca9685_pwm_gpio_set(gpio, offset, value);

	return 0;
}

/*
 * The PCA9685 has a bit for turning the PWM output full off or on. Some
 * boards like Intel Galileo actually uses these as normal GPIOs so we
 * expose a GPIO chip here which can exclusively take over the underlying
 * PWM channel.
 */
static int pca9685_pwm_gpio_probe(struct pca9685 *pca)
{
	struct device *dev = pca->chip.dev;

	mutex_init(&pca->lock);

	pca->gpio.label = dev_name(dev);
	pca->gpio.parent = dev;
	pca->gpio.request = pca9685_pwm_gpio_request;
	pca->gpio.free = pca9685_pwm_gpio_free;
	pca->gpio.get_direction = pca9685_pwm_gpio_get_direction;
	pca->gpio.direction_input = pca9685_pwm_gpio_direction_input;
	pca->gpio.direction_output = pca9685_pwm_gpio_direction_output;
	pca->gpio.get = pca9685_pwm_gpio_get;
	pca->gpio.set = pca9685_pwm_gpio_set;
	pca->gpio.base = -1;
	pca->gpio.ngpio = PCA9685_MAXCHAN;
	pca->gpio.can_sleep = true;

	return devm_gpiochip_add_data(dev, &pca->gpio, pca);
}
#else
static inline bool pca9685_pwm_test_and_set_inuse(struct pca9685 *pca,
						  int pwm_idx)
{
	return false;
}

static inline void
pca9685_pwm_clear_inuse(struct pca9685 *pca, int pwm_idx)
{
}

static inline int pca9685_pwm_gpio_probe(struct pca9685 *pca)
{
	return 0;
}
#endif

static void pca9685_set_sleep_mode(struct pca9685 *pca, bool enable)
{
	regmap_update_bits(pca->regmap, PCA9685_MODE1,
			   MODE1_SLEEP, enable ? MODE1_SLEEP : 0);
	if (!enable) {
		/* Wait 500us for the oscillator to be back up */
		udelay(500);
	}
}

static int pca9685_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	struct pca9685 *pca = to_pca(chip);
	unsigned long long duty;
	unsigned int reg;
	int prescale;

	if (period_ns != pca->period_ns) {
		prescale = DIV_ROUND_CLOSEST(PCA9685_OSC_CLOCK_MHZ * period_ns,
					     PCA9685_COUNTER_RANGE * 1000) - 1;

		if (prescale >= PCA9685_PRESCALE_MIN &&
			prescale <= PCA9685_PRESCALE_MAX) {
			/*
			 * putting the chip briefly into SLEEP mode
			 * at this point won't interfere with the
			 * pm_runtime framework, because the pm_runtime
			 * state is guaranteed active here.
			 */
			/* Put chip into sleep mode */
			pca9685_set_sleep_mode(pca, true);

			/* Change the chip-wide output frequency */
			regmap_write(pca->regmap, PCA9685_PRESCALE, prescale);

			/* Wake the chip up */
			pca9685_set_sleep_mode(pca, false);

			pca->period_ns = period_ns;
		} else {
			dev_err(chip->dev,
				"prescaler not set: period out of bounds!\n");
			return -EINVAL;
		}
	}

	if (duty_ns < 1) {
		if (pwm->hwpwm >= PCA9685_MAXCHAN)
			reg = PCA9685_ALL_LED_OFF_H;
		else
			reg = LED_N_OFF_H(pwm->hwpwm);

		regmap_write(pca->regmap, reg, LED_FULL);

		return 0;
	}

	if (duty_ns == period_ns) {
		/* Clear both OFF registers */
		if (pwm->hwpwm >= PCA9685_MAXCHAN)
			reg = PCA9685_ALL_LED_OFF_L;
		else
			reg = LED_N_OFF_L(pwm->hwpwm);

		regmap_write(pca->regmap, reg, 0x0);

		if (pwm->hwpwm >= PCA9685_MAXCHAN)
			reg = PCA9685_ALL_LED_OFF_H;
		else
			reg = LED_N_OFF_H(pwm->hwpwm);

		regmap_write(pca->regmap, reg, 0x0);

		/* Set the full ON bit */
		if (pwm->hwpwm >= PCA9685_MAXCHAN)
			reg = PCA9685_ALL_LED_ON_H;
		else
			reg = LED_N_ON_H(pwm->hwpwm);

		regmap_write(pca->regmap, reg, LED_FULL);

		return 0;
	}

	duty = PCA9685_COUNTER_RANGE * (unsigned long long)duty_ns;
	duty = DIV_ROUND_UP_ULL(duty, period_ns);

	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_OFF_L;
	else
		reg = LED_N_OFF_L(pwm->hwpwm);

	regmap_write(pca->regmap, reg, (int)duty & 0xff);

	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_OFF_H;
	else
		reg = LED_N_OFF_H(pwm->hwpwm);

	regmap_write(pca->regmap, reg, ((int)duty >> 8) & 0xf);

	/* Clear the full ON bit, otherwise the set OFF time has no effect */
	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_ON_H;
	else
		reg = LED_N_ON_H(pwm->hwpwm);

	regmap_write(pca->regmap, reg, 0);

	return 0;
}

static int pca9685_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pca9685 *pca = to_pca(chip);
	unsigned int reg;

	/*
	 * The PWM subsystem does not support a pre-delay.
	 * So, set the ON-timeout to 0
	 */
	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_ON_L;
	else
		reg = LED_N_ON_L(pwm->hwpwm);

	regmap_write(pca->regmap, reg, 0);

	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_ON_H;
	else
		reg = LED_N_ON_H(pwm->hwpwm);

	regmap_write(pca->regmap, reg, 0);

	/*
	 * Clear the full-off bit.
	 * It has precedence over the others and must be off.
	 */
	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_OFF_H;
	else
		reg = LED_N_OFF_H(pwm->hwpwm);

	regmap_update_bits(pca->regmap, reg, LED_FULL, 0x0);

	return 0;
}

static void pca9685_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pca9685 *pca = to_pca(chip);
	unsigned int reg;

	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_OFF_H;
	else
		reg = LED_N_OFF_H(pwm->hwpwm);

	regmap_write(pca->regmap, reg, LED_FULL);

	/* Clear the LED_OFF counter. */
	if (pwm->hwpwm >= PCA9685_MAXCHAN)
		reg = PCA9685_ALL_LED_OFF_L;
	else
		reg = LED_N_OFF_L(pwm->hwpwm);

	regmap_write(pca->regmap, reg, 0x0);
}

static int pca9685_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pca9685 *pca = to_pca(chip);

	if (pca9685_pwm_test_and_set_inuse(pca, pwm->hwpwm))
		return -EBUSY;
	pm_runtime_get_sync(chip->dev);

	return 0;
}

static void pca9685_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pca9685 *pca = to_pca(chip);

	pca9685_pwm_disable(chip, pwm);
	pm_runtime_put(chip->dev);
	pca9685_pwm_clear_inuse(pca, pwm->hwpwm);
}

static const struct pwm_ops pca9685_pwm_ops = {
	.enable = pca9685_pwm_enable,
	.disable = pca9685_pwm_disable,
	.config = pca9685_pwm_config,
	.request = pca9685_pwm_request,
	.free = pca9685_pwm_free,
	.owner = THIS_MODULE,
};

static const struct regmap_config pca9685_regmap_i2c_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PCA9685_NUMREGS,
	.cache_type = REGCACHE_NONE,
};

static int pca9685_pwm_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pca9685 *pca;
	int ret;
	int mode2;

	pca = devm_kzalloc(&client->dev, sizeof(*pca), GFP_KERNEL);
	if (!pca)
		return -ENOMEM;

	pca->regmap = devm_regmap_init_i2c(client, &pca9685_regmap_i2c_config);
	if (IS_ERR(pca->regmap)) {
		ret = PTR_ERR(pca->regmap);
		dev_err(&client->dev, "Failed to initialize register map: %d\n",
			ret);
		return ret;
	}
	pca->period_ns = PCA9685_DEFAULT_PERIOD;

	i2c_set_clientdata(client, pca);

	regmap_read(pca->regmap, PCA9685_MODE2, &mode2);

	if (device_property_read_bool(&client->dev, "invert"))
		mode2 |= MODE2_INVRT;
	else
		mode2 &= ~MODE2_INVRT;

	if (device_property_read_bool(&client->dev, "open-drain"))
		mode2 &= ~MODE2_OUTDRV;
	else
		mode2 |= MODE2_OUTDRV;

	regmap_write(pca->regmap, PCA9685_MODE2, mode2);

	/* clear all "full off" bits */
	regmap_write(pca->regmap, PCA9685_ALL_LED_OFF_L, 0);
	regmap_write(pca->regmap, PCA9685_ALL_LED_OFF_H, 0);

	pca->chip.ops = &pca9685_pwm_ops;
	/* add an extra channel for ALL_LED */
	pca->chip.npwm = PCA9685_MAXCHAN + 1;

	pca->chip.dev = &client->dev;
	pca->chip.base = -1;

	ret = pwmchip_add(&pca->chip);
	if (ret < 0)
		return ret;

	ret = pca9685_pwm_gpio_probe(pca);
	if (ret < 0) {
		pwmchip_remove(&pca->chip);
		return ret;
	}

	/* the chip comes out of power-up in the active state */
	pm_runtime_set_active(&client->dev);
	/*
	 * enable will put the chip into suspend, which is what we
	 * want as all outputs are disabled at this point
	 */
	pm_runtime_enable(&client->dev);

	return 0;
}

static int pca9685_pwm_remove(struct i2c_client *client)
{
	struct pca9685 *pca = i2c_get_clientdata(client);
	int ret;

	ret = pwmchip_remove(&pca->chip);
	if (ret)
		return ret;
	pm_runtime_disable(&client->dev);
	return 0;
}

static int __maybe_unused pca9685_pwm_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca9685 *pca = i2c_get_clientdata(client);

	pca9685_set_sleep_mode(pca, true);
	return 0;
}

static int __maybe_unused pca9685_pwm_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pca9685 *pca = i2c_get_clientdata(client);

	pca9685_set_sleep_mode(pca, false);
	return 0;
}

static const struct i2c_device_id pca9685_id[] = {
	{ "pca9685", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, pca9685_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id pca9685_acpi_ids[] = {
	{ "INT3492", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(acpi, pca9685_acpi_ids);
#endif

#ifdef CONFIG_OF
static const struct of_device_id pca9685_dt_ids[] = {
	{ .compatible = "nxp,pca9685-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pca9685_dt_ids);
#endif

static const struct dev_pm_ops pca9685_pwm_pm = {
	SET_RUNTIME_PM_OPS(pca9685_pwm_runtime_suspend,
			   pca9685_pwm_runtime_resume, NULL)
};

static struct i2c_driver pca9685_i2c_driver = {
	.driver = {
		.name = "pca9685-pwm",
		.acpi_match_table = ACPI_PTR(pca9685_acpi_ids),
		.of_match_table = of_match_ptr(pca9685_dt_ids),
		.pm = &pca9685_pwm_pm,
	},
	.probe = pca9685_pwm_probe,
	.remove = pca9685_pwm_remove,
	.id_table = pca9685_id,
};

module_i2c_driver(pca9685_i2c_driver);

MODULE_AUTHOR("Steffen Trumtrar <s.trumtrar@pengutronix.de>");
MODULE_DESCRIPTION("PWM driver for PCA9685");
MODULE_LICENSE("GPL");
