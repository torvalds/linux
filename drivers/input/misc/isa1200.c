// SPDX-License-Identifier: GPL-2.0+

#include <linux/array_size.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>

/*
 * System control (LDO regulator)
 *
 * LDO voltage to register mapping is linear, but it is split in two parts:
 * 2.3V - 3.0V map to 0x08 - 0x0f; 3.1V - 3.8V map to 0x00 - 0x7
 */

#define ISA1200_SCTRL			0x00
#define ISA1200_LDO_VOLTAGE_BASE	0x08
#define ISA1200_LDO_VOLTAGE_STEP	100000
#define ISA1200_LDO_VOLTAGE_2V3		23
#define ISA1200_LDO_VOLTAGE_3V1		31
#define ISA1200_LDO_VOLTAGE_MIN		2300000
#define ISA1200_LDO_VOLTAGE_MAX		3800000

/*
 * The output frequency is calculated with this formula:
 *
 *                 base clock frequency
 * fout = -----------------------------------------
 *        (128 - PWM_FREQ) * 2 * PLLDIV * PWM_PERIOD
 *
 * The base clock frequency is the clock frequency provided on the
 * clock input to the chip, divided by the value in HCTRL0
 *
 * PWM_FREQ is configured in register HCTRL4, it is common to set this
 * to 0 to get only two variables to calculate.
 *
 * PLLDIV is configured in register HCTRL3 (bits 7..4, so 0..15)
 * PWM_PERIOD is configured in register HCTRL6
 * Further the duty cycle can be configured in HCTRL5
 */

/*
 * HCTRL0 configures clock or PWM input and selects the divider for
 * the clock input.
 */
#define ISA1200_HCTRL0			0x30
#define ISA1200_HCTRL0_HAP_ENABLE	BIT(7)
#define ISA1200_HCTRL0_PWM_GEN_MODE	BIT(4)
#define ISA1200_HCTRL0_PWM_INPUT_MODE	BIT(3)
#define ISA1200_HCTRL0_CLKDIV_128	128

/*
 * HCTRL1 configures the motor type and clock sourse
 */
#define ISA1200_HCTRL1			0x31
#define ISA1200_HCTRL1_EXT_CLOCK	BIT(7)
#define ISA1200_HCTRL1_DAC_INVERT	BIT(6)
#define ISA1200_HCTRL1_MODE(n)		(((n) & 1) << 5)

/* HCTRL2 controls software reset of the chip */
#define ISA1200_HCTRL2			0x32
#define ISA1200_HCTRL2_SW_RESET		BIT(0)

/*
 * HCTRL3 controls the PLL divisor
 *
 * Bits [0,1] are always set to 1 (we don't know what they are
 * used for) and bit 4 and upward control the PLL divisor.
 */
#define ISA1200_HCTRL3			0x33
#define ISA1200_HCTRL3_DEFAULT		0x03
#define ISA1200_HCTRL3_PLLDIV(n)	(((n) & 0xf) << 4)

/* HCTRL4 controls the PWM frequency of external channel */
#define ISA1200_HCTRL4			0x34

/* HCTRL5 controls the PWM high duty cycle of internal channel */
#define ISA1200_HCTRL5			0x35

/* HCTRL6 controls the PWM period of internal channel */
#define ISA1200_HCTRL6			0x36
#define ISA1200_HCTRL6_PERIOD_SCALE	100

/* The use for these registers is unknown but they exist */
#define ISA1200_HCTRL7			0x37
#define ISA1200_HCTRL8			0x38
#define ISA1200_HCTRL9			0x39
#define ISA1200_HCTRLA			0x3a
#define ISA1200_HCTRLB			0x3b
#define ISA1200_HCTRLC			0x3c
#define ISA1200_HCTRLD			0x3d

#define ISA1200_EN_PINS_MAX		2

static const struct regulator_bulk_data isa1200_supplies[] = {
	{ .supply = "vdd" }, { .supply = "vddp" },
};

struct isa1200_config {
	u32 ldo_voltage;
	u32 mode;
	u32 clkdiv;
	u32 plldiv;
	u32 freq;
	u32 period;
	u32 duty;
};

struct isa1200 {
	struct input_dev *input;
	struct regmap *map;

	struct clk *clk;
	struct pwm_device *pwm;
	struct gpio_descs *enable_gpios;
	struct regulator_bulk_data *supplies;

	struct work_struct play_work;
	struct isa1200_config config;

	int level;
	bool suspended;
	bool active;
};

static const struct regmap_config isa1200_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ISA1200_HCTRLD,
};

static void isa1200_start(struct isa1200 *isa)
{
	struct isa1200_config *config = &isa->config;
	struct device *dev = &isa->input->dev;
	struct pwm_state state;
	u8 hctrl0 = 0, hctrl1 = 0;
	DECLARE_BITMAP(values, ISA1200_EN_PINS_MAX);
	int err;

	if (!isa->active) {
		err = regulator_bulk_enable(ARRAY_SIZE(isa1200_supplies),
					    isa->supplies);
		if (err) {
			dev_err(dev, "failed to enable supplies (%d)\n", err);
			return;
		}

		err = clk_prepare_enable(isa->clk);
		if (err) {
			dev_err(dev, "failed to enable clock (%d)\n", err);
			regulator_bulk_disable(ARRAY_SIZE(isa1200_supplies),
					       isa->supplies);
			return;
		}

		bitmap_fill(values, ISA1200_EN_PINS_MAX);
		gpiod_multi_set_value_cansleep(isa->enable_gpios, values);

		usleep_range(200, 300);
	}

	regmap_write(isa->map, ISA1200_SCTRL, config->ldo_voltage);

	if (isa->clk) {
		hctrl0 = ISA1200_HCTRL0_PWM_GEN_MODE;
		hctrl1 = ISA1200_HCTRL1_EXT_CLOCK;
	}

	if (isa->pwm) {
		hctrl0 = ISA1200_HCTRL0_PWM_INPUT_MODE;
		hctrl1 = 0;
	}

	hctrl0 |= __ffs(config->clkdiv / ISA1200_HCTRL0_CLKDIV_128);
	hctrl1 |= ISA1200_HCTRL1_DAC_INVERT;
	hctrl1 |= ISA1200_HCTRL1_MODE(config->mode);

	regmap_write(isa->map, ISA1200_HCTRL0, hctrl0);
	regmap_write(isa->map, ISA1200_HCTRL1, hctrl1);

	/* Make sure to de-assert software reset */
	regmap_write(isa->map, ISA1200_HCTRL2, 0x00);

	/* PLL divisor */
	regmap_write(isa->map, ISA1200_HCTRL3,
		     ISA1200_HCTRL3_PLLDIV(config->plldiv) |
		     ISA1200_HCTRL3_DEFAULT);

	/* Frequency */
	regmap_write(isa->map, ISA1200_HCTRL4, config->freq);
	/* Duty cycle */
	regmap_write(isa->map, ISA1200_HCTRL5, config->period >> 1);
	/* Period */
	regmap_write(isa->map, ISA1200_HCTRL6, config->period);

	hctrl0 |= ISA1200_HCTRL0_HAP_ENABLE;
	regmap_write(isa->map, ISA1200_HCTRL0, hctrl0);

	if (isa->clk)
		regmap_write(isa->map, ISA1200_HCTRL5, config->duty);

	if (isa->pwm) {
		pwm_get_state(isa->pwm, &state);
		state.duty_cycle = config->duty;
		state.enabled = true;
		pwm_apply_might_sleep(isa->pwm, &state);
	}

	isa->active = true;
}

static void isa1200_stop(struct isa1200 *isa)
{
	struct pwm_state state;
	DECLARE_BITMAP(values, ISA1200_EN_PINS_MAX);

	if (!isa->active)
		return;

	if (isa->pwm) {
		pwm_get_state(isa->pwm, &state);
		state.duty_cycle = 0;
		state.enabled = false;
		pwm_apply_might_sleep(isa->pwm, &state);
	}

	regmap_write(isa->map, ISA1200_HCTRL0, 0x00);

	bitmap_zero(values, ISA1200_EN_PINS_MAX);
	gpiod_multi_set_value_cansleep(isa->enable_gpios, values);

	clk_disable_unprepare(isa->clk);
	regulator_bulk_disable(ARRAY_SIZE(isa1200_supplies),
			       isa->supplies);

	isa->active = false;
}

static void isa1200_play_work(struct work_struct *work)
{
	struct isa1200 *isa = container_of(work, struct isa1200, play_work);

	if (!READ_ONCE(isa->suspended)) {
		if (isa->level)
			isa1200_start(isa);
		else
			isa1200_stop(isa);
	}
}

static int isa1200_vibrator_play_effect(struct input_dev *input, void *data,
					struct ff_effect *effect)
{
	struct isa1200 *isa = input_get_drvdata(input);
	int level;

	/*
	 * TODO: we currently only support rumble.
	 * The ISA1200 can control two motors and some devices
	 * also have two motors mounted.
	 */
	level = effect->u.rumble.strong_magnitude;
	if (!level)
		level = effect->u.rumble.weak_magnitude;

	dev_dbg(&input->dev, "FF effect type %d level %d\n",
		effect->type, level);

	if (isa->level != level) {
		isa->level = level;
		if (!READ_ONCE(isa->suspended))
			schedule_work(&isa->play_work);
	}

	return 0;
}

static void isa1200_vibrator_close(struct input_dev *input)
{
	struct isa1200 *isa = input_get_drvdata(input);

	cancel_work_sync(&isa->play_work);
	isa1200_stop(isa);
	isa->level = 0;
}

static int isa1200_of_probe(struct i2c_client *client)
{
	struct isa1200 *isa = i2c_get_clientdata(client);
	struct isa1200_config *config = &isa->config;
	struct device *dev = &client->dev;
	struct fwnode_handle *ldo_node;
	int err;

	isa->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(isa->clk))
		return dev_err_probe(dev, PTR_ERR(isa->clk),
				     "failed to get clock\n");

	isa->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(isa->pwm)) {
		err = PTR_ERR(isa->pwm);
		if (err == -ENODEV || err == -EINVAL)
			isa->pwm = NULL;
		else
			return dev_err_probe(dev, err, "getting PWM\n");
	}

	if (!isa->clk && !isa->pwm)
		return dev_err_probe(dev, -EINVAL,
				     "clock or PWM are required, none were provided\n");

	err = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(isa1200_supplies),
					    isa1200_supplies, &isa->supplies);
	if (err)
		return dev_err_probe(dev, err, "failed to get supplies\n");

	isa->enable_gpios = devm_gpiod_get_array_optional(dev, "control",
							  GPIOD_OUT_LOW);
	if (IS_ERR(isa->enable_gpios))
		return dev_err_probe(dev, PTR_ERR(isa->enable_gpios),
				     "failed to get enable gpios\n");

	if (isa->enable_gpios && isa->enable_gpios->ndescs > ISA1200_EN_PINS_MAX)
		return dev_err_probe(dev, -EINVAL, "too many enable gpios\n");

	ldo_node = device_get_named_child_node(dev, "ldo");
	if (!ldo_node)
		return dev_err_probe(dev, -ENODEV,
				     "failed to get embedded LDO node\n");

	err = fwnode_property_read_u32(ldo_node, "regulator-min-microvolt",
				       &config->ldo_voltage);
	fwnode_handle_put(ldo_node);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to get ldo voltage\n");

	config->ldo_voltage = clamp(config->ldo_voltage,
				    ISA1200_LDO_VOLTAGE_MIN,
				    ISA1200_LDO_VOLTAGE_MAX);

	config->ldo_voltage /= ISA1200_LDO_VOLTAGE_STEP;
	if (config->ldo_voltage < ISA1200_LDO_VOLTAGE_3V1)
		config->ldo_voltage = config->ldo_voltage -
				      ISA1200_LDO_VOLTAGE_2V3 +
				      ISA1200_LDO_VOLTAGE_BASE;
	else
		config->ldo_voltage -= ISA1200_LDO_VOLTAGE_3V1;

	config->mode = 0; /* LRA_MODE */
	device_property_read_u32(dev, "imagis,mode", &config->mode);

	config->clkdiv = ISA1200_HCTRL0_CLKDIV_128;
	device_property_read_u32(dev, "imagis,clk-div", &config->clkdiv);
	if (!config->clkdiv)
		return dev_err_probe(dev, -EINVAL, "clk-div cannot be zero\n");

	config->clkdiv = clamp(config->clkdiv, ISA1200_HCTRL0_CLKDIV_128,
			       ISA1200_HCTRL0_CLKDIV_128 << 3);

	err = device_property_read_u32(dev, "imagis,pll-div", &config->plldiv);
	if (err || !config->plldiv)
		config->plldiv = 1;

	config->period = 0;
	config->freq = 0;
	config->duty = 0;

	if (isa->clk) {
		err = device_property_read_u32(dev, "imagis,period-ns",
					       &config->period);
		if (err)
			return dev_err_probe(dev, err,
					     "failed to get period\n");

		/*
		 * TODO: The scale value is arbitrary, but it fits observations
		 * quite well, and the exact conversion method is unknown.
		 * The period property value returned above is the HCTRL6
		 * register value set by the vendor code, multiplied by 100.
		 */
		config->period /= ISA1200_HCTRL6_PERIOD_SCALE;
		config->duty = config->period >> 1;
	}

	if (isa->pwm) {
		struct pwm_state state;

		pwm_init_state(isa->pwm, &state);

		if (!state.period)
			return dev_err_probe(dev, -EINVAL,
					     "PWM period cannot be zero\n");

		config->freq = div64_u64(NANO, state.period * config->clkdiv);
		config->duty = state.period >> 1;

		err = pwm_apply_might_sleep(isa->pwm, &state);
		if (err)
			return dev_err_probe(dev, err,
					     "failed to apply initial PWM state\n");
	}

	/*
	 * TODO: If device is using a clock, this property should return the
	 * value written to the HCTRL5 register by downstrem code. It likely
	 * needs to be converted into a meaningful duty cycle value, though
	 * unfortunately the exact conversion mechanism is unknown. If the
	 * device uses PWM, this property will return the correct duty cycle
	 * in nanoseconds.
	 */
	device_property_read_u32(dev, "imagis,duty-cycle-ns", &config->duty);

	return 0;
}

static int isa1200_probe(struct i2c_client *client)
{
	struct isa1200 *isa;
	struct device *dev = &client->dev;
	int err;

	isa = devm_kzalloc(dev, sizeof(*isa), GFP_KERNEL);
	if (!isa)
		return -ENOMEM;

	isa->input = devm_input_allocate_device(dev);
	if (!isa->input)
		return -ENOMEM;

	i2c_set_clientdata(client, isa);

	err = isa1200_of_probe(client);
	if (err)
		return err;

	isa->map = devm_regmap_init_i2c(client, &isa1200_regmap_config);
	if (IS_ERR(isa->map))
		return dev_err_probe(dev, PTR_ERR(isa->map),
				     "failed to initialize register map\n");

	INIT_WORK(&isa->play_work, isa1200_play_work);

	isa->input->name = "isa1200-haptic";
	isa->input->id.bustype = BUS_I2C;
	isa->input->close = isa1200_vibrator_close;

	isa->active = false;

	input_set_drvdata(isa->input, isa);

	/* TODO: this hardware can likely support more than rumble */
	input_set_capability(isa->input, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(isa->input, NULL,
				      isa1200_vibrator_play_effect);
	if (err)
		return dev_err_probe(dev, err, "failed to create FF dev\n");

	err = input_register_device(isa->input);
	if (err)
		return dev_err_probe(dev, err, "failed to register input dev\n");

	return 0;
}

static int isa1200_suspend(struct device *dev)
{
	struct isa1200 *isa = dev_get_drvdata(dev);

	guard(mutex)(&isa->input->mutex);

	if (input_device_enabled(isa->input)) {
		WRITE_ONCE(isa->suspended, true);
		cancel_work_sync(&isa->play_work);
		isa1200_stop(isa);
	}

	return 0;
}

static int isa1200_resume(struct device *dev)
{
	struct isa1200 *isa = dev_get_drvdata(dev);

	guard(mutex)(&isa->input->mutex);

	if (input_device_enabled(isa->input)) {
		WRITE_ONCE(isa->suspended, false);
		if (isa->level)
			schedule_work(&isa->play_work);
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(isa1200_pm_ops, isa1200_suspend, isa1200_resume);

static const struct of_device_id isa1200_of_match[] = {
	{ .compatible = "imagis,isa1200" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, isa1200_of_match);

static struct i2c_driver isa1200_i2c_driver = {
	.driver = {
		.name = "isa1200",
		.of_match_table = isa1200_of_match,
		.pm = pm_sleep_ptr(&isa1200_pm_ops),
	},
	.probe = isa1200_probe,
};
module_i2c_driver(isa1200_i2c_driver);

MODULE_AUTHOR("Linus Walleij <linusw@kernel.org>");
MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Imagis ISA1200 haptic feedback unit");
MODULE_LICENSE("GPL");
