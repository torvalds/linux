/*
 * linux/drivers/video/backlight/pwm_bl.c
 *
 * simple PWM based backlight control, board code has to setup
 * 1) pin configuration so PWM waveforms can output
 * 2) platform_data being correctly configured
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct pwm_bl_data {
	struct pwm_device	*pwm;
	struct device		*dev;
	unsigned int		period;
	unsigned int		lth_brightness;
	unsigned int		*levels;
	bool			enabled;
	struct regulator	*power_supply;
	struct gpio_desc	*enable_gpio;
	unsigned int		scale;
	bool			legacy;
	unsigned int		post_pwm_on_delay;
	unsigned int		pwm_off_delay;
	int			(*notify)(struct device *,
					  int brightness);
	void			(*notify_after)(struct device *,
					int brightness);
	int			(*check_fb)(struct device *, struct fb_info *);
	void			(*exit)(struct device *);
};

static void pwm_backlight_power_on(struct pwm_bl_data *pb, int brightness)
{
	int err;

	if (pb->enabled)
		return;

	err = regulator_enable(pb->power_supply);
	if (err < 0)
		dev_err(pb->dev, "failed to enable power supply\n");

	pwm_enable(pb->pwm);

	if (pb->post_pwm_on_delay)
		msleep(pb->post_pwm_on_delay);

	if (pb->enable_gpio)
		gpiod_set_value_cansleep(pb->enable_gpio, 1);

	pb->enabled = true;
}

static void pwm_backlight_power_off(struct pwm_bl_data *pb)
{
	if (!pb->enabled)
		return;

	if (pb->enable_gpio)
		gpiod_set_value_cansleep(pb->enable_gpio, 0);

	if (pb->pwm_off_delay)
		msleep(pb->pwm_off_delay);

	pwm_config(pb->pwm, 0, pb->period);
	pwm_disable(pb->pwm);

	regulator_disable(pb->power_supply);
	pb->enabled = false;
}

static int compute_duty_cycle(struct pwm_bl_data *pb, int brightness)
{
	unsigned int lth = pb->lth_brightness;
	u64 duty_cycle;

	if (pb->levels)
		duty_cycle = pb->levels[brightness];
	else
		duty_cycle = brightness;

	duty_cycle *= pb->period - lth;
	do_div(duty_cycle, pb->scale);

	return duty_cycle + lth;
}

static int pwm_backlight_update_status(struct backlight_device *bl)
{
	struct pwm_bl_data *pb = bl_get_data(bl);
	int brightness = bl->props.brightness;
	int duty_cycle;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	if (pb->notify)
		brightness = pb->notify(pb->dev, brightness);

	if (brightness > 0) {
		duty_cycle = compute_duty_cycle(pb, brightness);
		pwm_config(pb->pwm, duty_cycle, pb->period);
		pwm_backlight_power_on(pb, brightness);
	} else
		pwm_backlight_power_off(pb);

	if (pb->notify_after)
		pb->notify_after(pb->dev, brightness);

	return 0;
}

static int pwm_backlight_check_fb(struct backlight_device *bl,
				  struct fb_info *info)
{
	struct pwm_bl_data *pb = bl_get_data(bl);

	return !pb->check_fb || pb->check_fb(pb->dev, info);
}

static const struct backlight_ops pwm_backlight_ops = {
	.update_status	= pwm_backlight_update_status,
	.check_fb	= pwm_backlight_check_fb,
};

#ifdef CONFIG_OF
#define PWM_LUMINANCE_SCALE	10000 /* luminance scale */

/* An integer based power function */
static u64 int_pow(u64 base, int exp)
{
	u64 result = 1;

	while (exp) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		base *= base;
	}

	return result;
}

/*
 * CIE lightness to PWM conversion.
 *
 * The CIE 1931 lightness formula is what actually describes how we perceive
 * light:
 *          Y = (L* / 902.3)           if L* ≤ 0.08856
 *          Y = ((L* + 16) / 116)^3    if L* > 0.08856
 *
 * Where Y is the luminance, the amount of light coming out of the screen, and
 * is a number between 0.0 and 1.0; and L* is the lightness, how bright a human
 * perceives the screen to be, and is a number between 0 and 100.
 *
 * The following function does the fixed point maths needed to implement the
 * above formula.
 */
static u64 cie1931(unsigned int lightness, unsigned int scale)
{
	u64 retval;

	lightness *= 100;
	if (lightness <= (8 * scale)) {
		retval = DIV_ROUND_CLOSEST_ULL(lightness * 10, 9023);
	} else {
		retval = int_pow((lightness + (16 * scale)) / 116, 3);
		retval = DIV_ROUND_CLOSEST_ULL(retval, (scale * scale));
	}

	return retval;
}

/*
 * Create a default correction table for PWM values to create linear brightness
 * for LED based backlights using the CIE1931 algorithm.
 */
static
int pwm_backlight_brightness_default(struct device *dev,
				     struct platform_pwm_backlight_data *data,
				     unsigned int period)
{
	unsigned int counter = 0;
	unsigned int i, n;
	u64 retval;

	/*
	 * Count the number of bits needed to represent the period number. The
	 * number of bits is used to calculate the number of levels used for the
	 * brightness-levels table, the purpose of this calculation is have a
	 * pre-computed table with enough levels to get linear brightness
	 * perception. The period is divided by the number of bits so for a
	 * 8-bit PWM we have 255 / 8 = 32 brightness levels or for a 16-bit PWM
	 * we have 65535 / 16 = 4096 brightness levels.
	 *
	 * Note that this method is based on empirical testing on different
	 * devices with PWM of 8 and 16 bits of resolution.
	 */
	n = period;
	while (n) {
		counter += n % 2;
		n >>= 1;
	}

	data->max_brightness = DIV_ROUND_UP(period, counter);
	data->levels = devm_kcalloc(dev, data->max_brightness,
				    sizeof(*data->levels), GFP_KERNEL);
	if (!data->levels)
		return -ENOMEM;

	/* Fill the table using the cie1931 algorithm */
	for (i = 0; i < data->max_brightness; i++) {
		retval = cie1931((i * PWM_LUMINANCE_SCALE) /
				 data->max_brightness, PWM_LUMINANCE_SCALE) *
				 period;
		retval = DIV_ROUND_CLOSEST_ULL(retval, PWM_LUMINANCE_SCALE);
		if (retval > UINT_MAX)
			return -EINVAL;
		data->levels[i] = (unsigned int)retval;
	}

	data->dft_brightness = data->max_brightness / 2;
	data->max_brightness--;

	return 0;
}

static int pwm_backlight_parse_dt(struct device *dev,
				  struct platform_pwm_backlight_data *data)
{
	struct device_node *node = dev->of_node;
	unsigned int num_levels = 0;
	unsigned int levels_count;
	unsigned int num_steps = 0;
	struct property *prop;
	unsigned int *table;
	int length;
	u32 value;
	int ret;

	if (!node)
		return -ENODEV;

	memset(data, 0, sizeof(*data));

	/*
	 * Determine the number of brightness levels, if this property is not
	 * set a default table of brightness levels will be used.
	 */
	prop = of_find_property(node, "brightness-levels", &length);
	if (!prop)
		return 0;

	data->max_brightness = length / sizeof(u32);

	/* read brightness levels from DT property */
	if (data->max_brightness > 0) {
		size_t size = sizeof(*data->levels) * data->max_brightness;
		unsigned int i, j, n = 0;

		data->levels = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!data->levels)
			return -ENOMEM;

		ret = of_property_read_u32_array(node, "brightness-levels",
						 data->levels,
						 data->max_brightness);
		if (ret < 0)
			return ret;

		ret = of_property_read_u32(node, "default-brightness-level",
					   &value);
		if (ret < 0)
			return ret;

		data->dft_brightness = value;

		/*
		 * This property is optional, if is set enables linear
		 * interpolation between each of the values of brightness levels
		 * and creates a new pre-computed table.
		 */
		of_property_read_u32(node, "num-interpolated-steps",
				     &num_steps);

		/*
		 * Make sure that there is at least two entries in the
		 * brightness-levels table, otherwise we can't interpolate
		 * between two points.
		 */
		if (num_steps) {
			if (data->max_brightness < 2) {
				dev_err(dev, "can't interpolate\n");
				return -EINVAL;
			}

			/*
			 * Recalculate the number of brightness levels, now
			 * taking in consideration the number of interpolated
			 * steps between two levels.
			 */
			for (i = 0; i < data->max_brightness - 1; i++) {
				if ((data->levels[i + 1] - data->levels[i]) /
				   num_steps)
					num_levels += num_steps;
				else
					num_levels++;
			}
			num_levels++;
			dev_dbg(dev, "new number of brightness levels: %d\n",
				num_levels);

			/*
			 * Create a new table of brightness levels with all the
			 * interpolated steps.
			 */
			size = sizeof(*table) * num_levels;
			table = devm_kzalloc(dev, size, GFP_KERNEL);
			if (!table)
				return -ENOMEM;

			/* Fill the interpolated table. */
			levels_count = 0;
			for (i = 0; i < data->max_brightness - 1; i++) {
				value = data->levels[i];
				n = (data->levels[i + 1] - value) / num_steps;
				if (n > 0) {
					for (j = 0; j < num_steps; j++) {
						table[levels_count] = value;
						value += n;
						levels_count++;
					}
				} else {
					table[levels_count] = data->levels[i];
					levels_count++;
				}
			}
			table[levels_count] = data->levels[i];

			/*
			 * As we use interpolation lets remove current
			 * brightness levels table and replace for the
			 * new interpolated table.
			 */
			devm_kfree(dev, data->levels);
			data->levels = table;

			/*
			 * Reassign max_brightness value to the new total number
			 * of brightness levels.
			 */
			data->max_brightness = num_levels;
		}

		data->max_brightness--;
	}

	/*
	 * These values are optional and set as 0 by default, the out values
	 * are modified only if a valid u32 value can be decoded.
	 */
	of_property_read_u32(node, "post-pwm-on-delay-ms",
			     &data->post_pwm_on_delay);
	of_property_read_u32(node, "pwm-off-delay-ms", &data->pwm_off_delay);

	data->enable_gpio = -EINVAL;
	return 0;
}

static const struct of_device_id pwm_backlight_of_match[] = {
	{ .compatible = "pwm-backlight" },
	{ }
};

MODULE_DEVICE_TABLE(of, pwm_backlight_of_match);
#else
static int pwm_backlight_parse_dt(struct device *dev,
				  struct platform_pwm_backlight_data *data)
{
	return -ENODEV;
}

static
int pwm_backlight_brightness_default(struct device *dev,
				     struct platform_pwm_backlight_data *data,
				     unsigned int period)
{
	return -ENODEV;
}
#endif

static int pwm_backlight_initial_power_state(const struct pwm_bl_data *pb)
{
	struct device_node *node = pb->dev->of_node;

	/* Not booted with device tree or no phandle link to the node */
	if (!node || !node->phandle)
		return FB_BLANK_UNBLANK;

	/*
	 * If the driver is probed from the device tree and there is a
	 * phandle link pointing to the backlight node, it is safe to
	 * assume that another driver will enable the backlight at the
	 * appropriate time. Therefore, if it is disabled, keep it so.
	 */

	/* if the enable GPIO is disabled, do not enable the backlight */
	if (pb->enable_gpio && gpiod_get_value(pb->enable_gpio) == 0)
		return FB_BLANK_POWERDOWN;

	/* The regulator is disabled, do not enable the backlight */
	if (!regulator_is_enabled(pb->power_supply))
		return FB_BLANK_POWERDOWN;

	/* The PWM is disabled, keep it like this */
	if (!pwm_is_enabled(pb->pwm))
		return FB_BLANK_POWERDOWN;

	return FB_BLANK_UNBLANK;
}

static int pwm_backlight_probe(struct platform_device *pdev)
{
	struct platform_pwm_backlight_data *data = dev_get_platdata(&pdev->dev);
	struct platform_pwm_backlight_data defdata;
	struct backlight_properties props;
	struct backlight_device *bl;
	struct device_node *node = pdev->dev.of_node;
	struct pwm_bl_data *pb;
	struct pwm_state state;
	struct pwm_args pargs;
	unsigned int i;
	int ret;

	if (!data) {
		ret = pwm_backlight_parse_dt(&pdev->dev, &defdata);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to find platform data\n");
			return ret;
		}

		data = &defdata;
	}

	if (data->init) {
		ret = data->init(&pdev->dev);
		if (ret < 0)
			return ret;
	}

	pb = devm_kzalloc(&pdev->dev, sizeof(*pb), GFP_KERNEL);
	if (!pb) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	pb->notify = data->notify;
	pb->notify_after = data->notify_after;
	pb->check_fb = data->check_fb;
	pb->exit = data->exit;
	pb->dev = &pdev->dev;
	pb->enabled = false;
	pb->post_pwm_on_delay = data->post_pwm_on_delay;
	pb->pwm_off_delay = data->pwm_off_delay;

	pb->enable_gpio = devm_gpiod_get_optional(&pdev->dev, "enable",
						  GPIOD_ASIS);
	if (IS_ERR(pb->enable_gpio)) {
		ret = PTR_ERR(pb->enable_gpio);
		goto err_alloc;
	}

	/*
	 * Compatibility fallback for drivers still using the integer GPIO
	 * platform data. Must go away soon.
	 */
	if (!pb->enable_gpio && gpio_is_valid(data->enable_gpio)) {
		ret = devm_gpio_request_one(&pdev->dev, data->enable_gpio,
					    GPIOF_OUT_INIT_HIGH, "enable");
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request GPIO#%d: %d\n",
				data->enable_gpio, ret);
			goto err_alloc;
		}

		pb->enable_gpio = gpio_to_desc(data->enable_gpio);
	}

	/*
	 * If the GPIO is not known to be already configured as output, that
	 * is, if gpiod_get_direction returns either 1 or -EINVAL, change the
	 * direction to output and set the GPIO as active.
	 * Do not force the GPIO to active when it was already output as it
	 * could cause backlight flickering or we would enable the backlight too
	 * early. Leave the decision of the initial backlight state for later.
	 */
	if (pb->enable_gpio &&
	    gpiod_get_direction(pb->enable_gpio) != 0)
		gpiod_direction_output(pb->enable_gpio, 1);

	pb->power_supply = devm_regulator_get(&pdev->dev, "power");
	if (IS_ERR(pb->power_supply)) {
		ret = PTR_ERR(pb->power_supply);
		goto err_alloc;
	}

	pb->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(pb->pwm) && PTR_ERR(pb->pwm) != -EPROBE_DEFER && !node) {
		dev_err(&pdev->dev, "unable to request PWM, trying legacy API\n");
		pb->legacy = true;
		pb->pwm = pwm_request(data->pwm_id, "pwm-backlight");
	}

	if (IS_ERR(pb->pwm)) {
		ret = PTR_ERR(pb->pwm);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to request PWM\n");
		goto err_alloc;
	}

	dev_dbg(&pdev->dev, "got pwm for backlight\n");

	if (!data->levels) {
		/* Get the PWM period (in nanoseconds) */
		pwm_get_state(pb->pwm, &state);

		ret = pwm_backlight_brightness_default(&pdev->dev, data,
						       state.period);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"failed to setup default brightness table\n");
			goto err_alloc;
		}
	}

	for (i = 0; i <= data->max_brightness; i++) {
		if (data->levels[i] > pb->scale)
			pb->scale = data->levels[i];

		pb->levels = data->levels;
	}

	/*
	 * FIXME: pwm_apply_args() should be removed when switching to
	 * the atomic PWM API.
	 */
	pwm_apply_args(pb->pwm);

	/*
	 * The DT case will set the pwm_period_ns field to 0 and store the
	 * period, parsed from the DT, in the PWM device. For the non-DT case,
	 * set the period from platform data if it has not already been set
	 * via the PWM lookup table.
	 */
	pwm_get_args(pb->pwm, &pargs);
	pb->period = pargs.period;
	if (!pb->period && (data->pwm_period_ns > 0))
		pb->period = data->pwm_period_ns;

	pb->lth_brightness = data->lth_brightness * (pb->period / pb->scale);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = data->max_brightness;
	bl = backlight_device_register(dev_name(&pdev->dev), &pdev->dev, pb,
				       &pwm_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		ret = PTR_ERR(bl);
		if (pb->legacy)
			pwm_free(pb->pwm);
		goto err_alloc;
	}

	if (data->dft_brightness > data->max_brightness) {
		dev_warn(&pdev->dev,
			 "invalid default brightness level: %u, using %u\n",
			 data->dft_brightness, data->max_brightness);
		data->dft_brightness = data->max_brightness;
	}

	bl->props.brightness = data->dft_brightness;
	bl->props.power = pwm_backlight_initial_power_state(pb);
	backlight_update_status(bl);

	platform_set_drvdata(pdev, bl);
	return 0;

err_alloc:
	if (data->exit)
		data->exit(&pdev->dev);
	return ret;
}

static int pwm_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	backlight_device_unregister(bl);
	pwm_backlight_power_off(pb);

	if (pb->exit)
		pb->exit(&pdev->dev);
	if (pb->legacy)
		pwm_free(pb->pwm);

	return 0;
}

static void pwm_backlight_shutdown(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	pwm_backlight_power_off(pb);
}

#ifdef CONFIG_PM_SLEEP
static int pwm_backlight_suspend(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	if (pb->notify)
		pb->notify(pb->dev, 0);

	pwm_backlight_power_off(pb);

	if (pb->notify_after)
		pb->notify_after(pb->dev, 0);

	return 0;
}

static int pwm_backlight_resume(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);

	backlight_update_status(bl);

	return 0;
}
#endif

static const struct dev_pm_ops pwm_backlight_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = pwm_backlight_suspend,
	.resume = pwm_backlight_resume,
	.poweroff = pwm_backlight_suspend,
	.restore = pwm_backlight_resume,
#endif
};

static struct platform_driver pwm_backlight_driver = {
	.driver		= {
		.name		= "pwm-backlight",
		.pm		= &pwm_backlight_pm_ops,
		.of_match_table	= of_match_ptr(pwm_backlight_of_match),
	},
	.probe		= pwm_backlight_probe,
	.remove		= pwm_backlight_remove,
	.shutdown	= pwm_backlight_shutdown,
};

module_platform_driver(pwm_backlight_driver);

MODULE_DESCRIPTION("PWM based Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pwm-backlight");
