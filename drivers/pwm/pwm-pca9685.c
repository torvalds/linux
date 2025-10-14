// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for PCA9685 16-channel 12-bit PWM LED controller
 *
 * Copyright (C) 2013 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 * Copyright (C) 2015 Clemens Gruber <clemens.gruber@pqgruber.com>
 *
 * based on the pwm-twl-led.c driver
 */

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
 * Because the PCA9685 has only one prescaler per chip, only the first channel
 * that is enabled is allowed to change the prescale register.
 * PWM channels requested afterwards must use a period that results in the same
 * prescale setting as the one set by the first requested channel.
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
#define PCA9685_OSC_CLOCK_HZ	25000000	/* Internal oscillator with 25 MHz */

/*
 * The time value of one counter tick. Note that NSEC_PER_SEC is an integer
 * multiple of PCA9685_OSC_CLOCK_HZ, so there is no rounding involved and we're
 * not loosing precision due to the early division.
 */
#define PCA9685_QUANTUM_NS(_prescale)	((NSEC_PER_SEC / PCA9685_OSC_CLOCK_HZ) * (_prescale + 1))

#define PCA9685_NUMREGS		0xFF
#define PCA9685_MAXCHAN		0x10

#define LED_FULL		BIT(4)
#define MODE1_ALLCALL		BIT(0)
#define MODE1_SUB3		BIT(1)
#define MODE1_SUB2		BIT(2)
#define MODE1_SUB1		BIT(3)
#define MODE1_SLEEP		BIT(4)
#define MODE1_AI		BIT(5)

#define MODE2_INVRT		BIT(4)
#define MODE2_OUTDRV		BIT(2)

#define LED_N_ON_H(N)	(PCA9685_LEDX_ON_H + (4 * (N)))
#define LED_N_ON_L(N)	(PCA9685_LEDX_ON_L + (4 * (N)))
#define LED_N_OFF_H(N)	(PCA9685_LEDX_OFF_H + (4 * (N)))
#define LED_N_OFF_L(N)	(PCA9685_LEDX_OFF_L + (4 * (N)))

#define REG_ON_H(C)	((C) >= PCA9685_MAXCHAN ? PCA9685_ALL_LED_ON_H : LED_N_ON_H((C)))
#define REG_ON_L(C)	((C) >= PCA9685_MAXCHAN ? PCA9685_ALL_LED_ON_L : LED_N_ON_L((C)))
#define REG_OFF_H(C)	((C) >= PCA9685_MAXCHAN ? PCA9685_ALL_LED_OFF_H : LED_N_OFF_H((C)))
#define REG_OFF_L(C)	((C) >= PCA9685_MAXCHAN ? PCA9685_ALL_LED_OFF_L : LED_N_OFF_L((C)))

struct pca9685 {
	struct regmap *regmap;
	struct mutex lock;
	DECLARE_BITMAP(pwms_enabled, PCA9685_MAXCHAN + 1);
};

static inline struct pca9685 *to_pca(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

/* This function is supposed to be called with the lock mutex held */
static bool pca9685_prescaler_can_change(struct pca9685 *pca, int channel)
{
	/* No PWM enabled: Change allowed */
	if (bitmap_empty(pca->pwms_enabled, PCA9685_MAXCHAN + 1))
		return true;
	/* More than one PWM enabled: Change not allowed */
	if (bitmap_weight(pca->pwms_enabled, PCA9685_MAXCHAN + 1) > 1)
		return false;
	/*
	 * Only one PWM enabled: Change allowed if the PWM about to
	 * be changed is the one that is already enabled
	 */
	return test_bit(channel, pca->pwms_enabled);
}

static int pca9685_read_reg(struct pwm_chip *chip, unsigned int reg, unsigned int *val)
{
	struct pca9685 *pca = to_pca(chip);
	struct device *dev = pwmchip_parent(chip);
	int err;

	err = regmap_read(pca->regmap, reg, val);
	if (err)
		dev_err(dev, "regmap_read of register 0x%x failed: %pe\n", reg, ERR_PTR(err));

	return err;
}

static int pca9685_write_reg(struct pwm_chip *chip, unsigned int reg, unsigned int val)
{
	struct pca9685 *pca = to_pca(chip);
	struct device *dev = pwmchip_parent(chip);
	int err;

	err = regmap_write(pca->regmap, reg, val);
	if (err)
		dev_err(dev, "regmap_write to register 0x%x failed: %pe\n", reg, ERR_PTR(err));

	return err;
}

static int pca9685_write_4reg(struct pwm_chip *chip, unsigned int reg, u8 val[4])
{
	struct pca9685 *pca = to_pca(chip);
	struct device *dev = pwmchip_parent(chip);
	int err;

	err = regmap_bulk_write(pca->regmap, reg, val, 4);
	if (err)
		dev_err(dev, "regmap_write to register 0x%x failed: %pe\n", reg, ERR_PTR(err));

	return err;
}

static int pca9685_set_sleep_mode(struct pwm_chip *chip, bool enable)
{
	struct pca9685 *pca = to_pca(chip);
	int err;

	err = regmap_update_bits(pca->regmap, PCA9685_MODE1,
				 MODE1_SLEEP, enable ? MODE1_SLEEP : 0);
	if (err)
		return err;

	if (!enable) {
		/* Wait 500us for the oscillator to be back up */
		udelay(500);
	}

	return 0;
}

struct pca9685_waveform {
	u8 onoff[4];
	u8 prescale;
};

static int pca9685_round_waveform_tohw(struct pwm_chip *chip, struct pwm_device *pwm, const struct pwm_waveform *wf, void *_wfhw)
{
	struct pca9685_waveform *wfhw = _wfhw;
	struct pca9685 *pca = to_pca(chip);
	unsigned int best_prescale;
	u8 prescale;
	unsigned int period_ns, duty;
	int ret_tohw = 0;

	if (!wf->period_length_ns) {
		*wfhw = (typeof(*wfhw)){
			.onoff = { 0, 0, 0, LED_FULL, },
			.prescale = 0,
		};

		dev_dbg(&chip->dev, "pwm#%u: %lld/%lld [+%lld] -> [%hhx %hhx %hhx %hhx] PSC:%hhx\n",
			pwm->hwpwm, wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns,
			wfhw->onoff[0], wfhw->onoff[1], wfhw->onoff[2], wfhw->onoff[3], wfhw->prescale);

		return 0;
	}

	if (wf->period_length_ns >= PCA9685_COUNTER_RANGE * PCA9685_QUANTUM_NS(255)) {
		best_prescale = 255;
	} else if (wf->period_length_ns < PCA9685_COUNTER_RANGE * PCA9685_QUANTUM_NS(3)) {
		best_prescale = 3;
		ret_tohw = 1;
	} else {
		best_prescale = (unsigned int)wf->period_length_ns / (PCA9685_COUNTER_RANGE * (NSEC_PER_SEC / PCA9685_OSC_CLOCK_HZ)) - 1;
	}

	guard(mutex)(&pca->lock);

	if (!pca9685_prescaler_can_change(pca, pwm->hwpwm)) {
		unsigned int current_prescale;
		int ret;

		ret = regmap_read(pca->regmap, PCA9685_PRESCALE, &current_prescale);
		if (ret)
			return ret;

		if (current_prescale > best_prescale)
			ret_tohw = 1;

		prescale = current_prescale;
	} else {
		prescale = best_prescale;
	}

	period_ns = PCA9685_COUNTER_RANGE * PCA9685_QUANTUM_NS(prescale);

	duty = (unsigned)min_t(u64, wf->duty_length_ns, period_ns) / PCA9685_QUANTUM_NS(prescale);

	if (duty < PCA9685_COUNTER_RANGE) {
		unsigned int on, off;

		on = (unsigned)min_t(u64, wf->duty_offset_ns, period_ns) / PCA9685_QUANTUM_NS(prescale);
		off = (on + duty) % PCA9685_COUNTER_RANGE;

		/*
		 * With a zero duty cycle, it doesn't matter if period was
		 * rounded up
		 */
		if (!duty)
			ret_tohw = 0;

		*wfhw = (typeof(*wfhw)){
			.onoff = { on & 0xff, (on >> 8) & 0xf, off & 0xff, (off >> 8) & 0xf },
			.prescale = prescale,
		};
	} else {
		*wfhw = (typeof(*wfhw)){
			.onoff = { 0, LED_FULL, 0, 0, },
			.prescale = prescale,
		};
	}

	dev_dbg(&chip->dev, "pwm#%u: %lld/%lld [+%lld] -> %s[%hhx %hhx %hhx %hhx] PSC:%hhx\n",
		pwm->hwpwm, wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns,
		ret_tohw ? "#" : "", wfhw->onoff[0], wfhw->onoff[1], wfhw->onoff[2], wfhw->onoff[3], wfhw->prescale);

	return ret_tohw;
}

static int pca9685_round_waveform_fromhw(struct pwm_chip *chip, struct pwm_device *pwm,
					 const void *_wfhw, struct pwm_waveform *wf)
{
	const struct pca9685_waveform *wfhw = _wfhw;
	struct pca9685 *pca = to_pca(chip);
	unsigned int prescale;

	if (wfhw->prescale)
		prescale = wfhw->prescale;
	else
		scoped_guard(mutex, &pca->lock) {
			int ret;

			ret = regmap_read(pca->regmap, PCA9685_PRESCALE, &prescale);
			if (ret)
				return ret;
		}

	wf->period_length_ns = PCA9685_COUNTER_RANGE * PCA9685_QUANTUM_NS(prescale);

	if (wfhw->onoff[3] & LED_FULL) {
		wf->duty_length_ns = 0;
		wf->duty_offset_ns = 0;
	} else if (wfhw->onoff[1] & LED_FULL) {
		wf->duty_length_ns = wf->period_length_ns;
		wf->duty_offset_ns = 0;
	} else {
		unsigned int on = wfhw->onoff[0] | (wfhw->onoff[1] & 0xf) << 8;
		unsigned int off = wfhw->onoff[2] | (wfhw->onoff[3] & 0xf) << 8;

		wf->duty_length_ns = (off - on) % PCA9685_COUNTER_RANGE * PCA9685_QUANTUM_NS(prescale);
		wf->duty_offset_ns = on * PCA9685_QUANTUM_NS(prescale);
	}

	dev_dbg(&chip->dev, "pwm#%u: [%hhx %hhx %hhx %hhx] PSC:%hhx -> %lld/%lld [+%lld]\n",
		pwm->hwpwm,
		wfhw->onoff[0], wfhw->onoff[1], wfhw->onoff[2], wfhw->onoff[3], wfhw->prescale,
		wf->duty_length_ns, wf->period_length_ns, wf->duty_offset_ns);

	return 0;
}

static int pca9685_read_waveform(struct pwm_chip *chip, struct pwm_device *pwm, void *_wfhw)
{
	struct pca9685_waveform *wfhw = _wfhw;
	struct pca9685 *pca = to_pca(chip);
	unsigned int prescale;
	int ret;

	guard(mutex)(&pca->lock);

	ret = regmap_bulk_read(pca->regmap, REG_ON_L(pwm->hwpwm), &wfhw->onoff, 4);
	if (ret)
		return ret;

	ret = regmap_read(pca->regmap, PCA9685_PRESCALE, &prescale);
	if (ret)
		return ret;

	wfhw->prescale = prescale;

	return 0;
}

static int pca9685_write_waveform(struct pwm_chip *chip, struct pwm_device *pwm, const void *_wfhw)
{
	const struct pca9685_waveform *wfhw = _wfhw;
	struct pca9685 *pca = to_pca(chip);
	unsigned int current_prescale;
	int ret;

	guard(mutex)(&pca->lock);

	if (wfhw->prescale) {
		ret = regmap_read(pca->regmap, PCA9685_PRESCALE, &current_prescale);
		if (ret)
			return ret;

		if (current_prescale != wfhw->prescale) {
			if (!pca9685_prescaler_can_change(pca, pwm->hwpwm))
				return -EBUSY;

			/* Put chip into sleep mode */
			ret = pca9685_set_sleep_mode(chip, true);
			if (ret)
				return ret;

			/* Change the chip-wide output frequency */
			ret = regmap_write(pca->regmap, PCA9685_PRESCALE, wfhw->prescale);
			if (ret)
				return ret;

			/* Wake the chip up */
			ret = pca9685_set_sleep_mode(chip, false);
			if (ret)
				return ret;
		}
	}

	return regmap_bulk_write(pca->regmap, REG_ON_L(pwm->hwpwm), &wfhw->onoff, 4);
}

static int pca9685_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pca9685 *pca = to_pca(chip);

	if (pwm->hwpwm < PCA9685_MAXCHAN) {
		/* PWMs - except the "all LEDs" channel - default to enabled */
		mutex_lock(&pca->lock);
		set_bit(pwm->hwpwm, pca->pwms_enabled);
		mutex_unlock(&pca->lock);
	}

	pm_runtime_get_sync(pwmchip_parent(chip));

	return 0;
}

static void pca9685_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pca9685 *pca = to_pca(chip);

	mutex_lock(&pca->lock);
	clear_bit(pwm->hwpwm, pca->pwms_enabled);
	mutex_unlock(&pca->lock);

	pm_runtime_put(pwmchip_parent(chip));
}

static const struct pwm_ops pca9685_pwm_ops = {
	.sizeof_wfhw = sizeof(struct pca9685_waveform),
	.round_waveform_tohw = pca9685_round_waveform_tohw,
	.round_waveform_fromhw = pca9685_round_waveform_fromhw,
	.read_waveform = pca9685_read_waveform,
	.write_waveform = pca9685_write_waveform,
	.request = pca9685_pwm_request,
	.free = pca9685_pwm_free,
};

static bool pca9685_readable_reg(struct device *dev, unsigned int reg)
{
	/* The ALL_LED registers are readable but read as zero */
	return reg <= REG_OFF_H(15) || reg >= PCA9685_PRESCALE;
}

static bool pca9685_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg <= REG_OFF_H(15) || reg >= PCA9685_ALL_LED_ON_L;
}

static bool pca9685_volatile_reg(struct device *dev, unsigned int reg)
{
	/*
	 * Writing to an ALL_LED register affects all LEDi registers, so they
	 * are not cachable. :-\
	 */
	return reg < PCA9685_PRESCALE;
}

static const struct regmap_config pca9685_regmap_i2c_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pca9685_readable_reg,
	.writeable_reg = pca9685_writeable_reg,
	.volatile_reg = pca9685_volatile_reg,

	.max_register = PCA9685_NUMREGS,
	.cache_type = REGCACHE_MAPLE,
};

static int pca9685_pwm_probe(struct i2c_client *client)
{
	struct pwm_chip *chip;
	struct pca9685 *pca;
	unsigned int reg;
	int ret;

	/* Add an extra channel for ALL_LED */
	chip = devm_pwmchip_alloc(&client->dev, PCA9685_MAXCHAN + 1, sizeof(*pca));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pca = to_pca(chip);

	pca->regmap = devm_regmap_init_i2c(client, &pca9685_regmap_i2c_config);
	if (IS_ERR(pca->regmap)) {
		ret = PTR_ERR(pca->regmap);
		dev_err(&client->dev, "Failed to initialize register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(client, chip);

	mutex_init(&pca->lock);

	/* clear MODE2_OCH */
	reg = 0;

	if (device_property_read_bool(&client->dev, "invert"))
		reg |= MODE2_INVRT;
	else
		reg &= ~MODE2_INVRT;

	if (device_property_read_bool(&client->dev, "open-drain"))
		reg &= ~MODE2_OUTDRV;
	else
		reg |= MODE2_OUTDRV;

	ret = pca9685_write_reg(chip, PCA9685_MODE2, reg);
	if (ret)
		return ret;

	/*
	 * Disable all LED ALLCALL and SUBx addresses to avoid bus collisions,
	 * enable Auto-Increment.
	 */
	pca9685_read_reg(chip, PCA9685_MODE1, &reg);
	reg &= ~(MODE1_ALLCALL | MODE1_SUB1 | MODE1_SUB2 | MODE1_SUB3);
	reg |= MODE1_AI;
	pca9685_write_reg(chip, PCA9685_MODE1, reg);

	/* Reset OFF/ON registers to POR default */
	ret = pca9685_write_4reg(chip, PCA9685_ALL_LED_ON_L, (u8[]){ 0, LED_FULL, 0, LED_FULL });
	if (ret < 0)
		return dev_err_probe(&client->dev, ret, "Failed to reset ON/OFF registers\n");

	chip->ops = &pca9685_pwm_ops;

	ret = pwmchip_add(chip);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&client->dev);

	if (pm_runtime_enabled(&client->dev)) {
		/*
		 * Although the chip comes out of power-up in the sleep state,
		 * we force it to sleep in case it was woken up before
		 */
		pca9685_set_sleep_mode(chip, true);
		pm_runtime_set_suspended(&client->dev);
	} else {
		/* Wake the chip up if runtime PM is disabled */
		pca9685_set_sleep_mode(chip, false);
	}

	return 0;
}

static void pca9685_pwm_remove(struct i2c_client *client)
{
	struct pwm_chip *chip = i2c_get_clientdata(client);

	pwmchip_remove(chip);

	if (!pm_runtime_enabled(&client->dev)) {
		/* Put chip in sleep state if runtime PM is disabled */
		pca9685_set_sleep_mode(chip, true);
	}

	pm_runtime_disable(&client->dev);
}

static int __maybe_unused pca9685_pwm_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pwm_chip *chip = i2c_get_clientdata(client);

	pca9685_set_sleep_mode(chip, true);
	return 0;
}

static int __maybe_unused pca9685_pwm_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pwm_chip *chip = i2c_get_clientdata(client);

	pca9685_set_sleep_mode(chip, false);
	return 0;
}

static const struct i2c_device_id pca9685_id[] = {
	{ "pca9685" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, pca9685_id);

static const struct acpi_device_id pca9685_acpi_ids[] = {
	{ "INT3492", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(acpi, pca9685_acpi_ids);

static const struct of_device_id pca9685_dt_ids[] = {
	{ .compatible = "nxp,pca9685-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pca9685_dt_ids);

static const struct dev_pm_ops pca9685_pwm_pm = {
	SET_RUNTIME_PM_OPS(pca9685_pwm_runtime_suspend,
			   pca9685_pwm_runtime_resume, NULL)
};

static struct i2c_driver pca9685_i2c_driver = {
	.driver = {
		.name = "pca9685-pwm",
		.acpi_match_table = pca9685_acpi_ids,
		.of_match_table = pca9685_dt_ids,
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
