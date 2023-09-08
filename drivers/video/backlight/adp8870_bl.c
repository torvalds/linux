// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Backlight driver for Analog Devices ADP8870 Backlight Devices
 *
 * Copyright 2009-2011 Analog Devices Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include <linux/platform_data/adp8870.h>
#define ADP8870_EXT_FEATURES
#define ADP8870_USE_LEDS


#define ADP8870_MFDVID	0x00  /* Manufacturer and device ID */
#define ADP8870_MDCR	0x01  /* Device mode and status */
#define ADP8870_INT_STAT 0x02  /* Interrupts status */
#define ADP8870_INT_EN	0x03  /* Interrupts enable */
#define ADP8870_CFGR	0x04  /* Configuration register */
#define ADP8870_BLSEL	0x05  /* Sink enable backlight or independent */
#define ADP8870_PWMLED	0x06  /* PWM Enable Selection Register */
#define ADP8870_BLOFF	0x07  /* Backlight off timeout */
#define ADP8870_BLDIM	0x08  /* Backlight dim timeout */
#define ADP8870_BLFR	0x09  /* Backlight fade in and out rates */
#define ADP8870_BLMX1	0x0A  /* Backlight (Brightness Level 1-daylight) maximum current */
#define ADP8870_BLDM1	0x0B  /* Backlight (Brightness Level 1-daylight) dim current */
#define ADP8870_BLMX2	0x0C  /* Backlight (Brightness Level 2-bright) maximum current */
#define ADP8870_BLDM2	0x0D  /* Backlight (Brightness Level 2-bright) dim current */
#define ADP8870_BLMX3	0x0E  /* Backlight (Brightness Level 3-office) maximum current */
#define ADP8870_BLDM3	0x0F  /* Backlight (Brightness Level 3-office) dim current */
#define ADP8870_BLMX4	0x10  /* Backlight (Brightness Level 4-indoor) maximum current */
#define ADP8870_BLDM4	0x11  /* Backlight (Brightness Level 4-indoor) dim current */
#define ADP8870_BLMX5	0x12  /* Backlight (Brightness Level 5-dark) maximum current */
#define ADP8870_BLDM5	0x13  /* Backlight (Brightness Level 5-dark) dim current */
#define ADP8870_ISCLAW	0x1A  /* Independent sink current fade law register */
#define ADP8870_ISCC	0x1B  /* Independent sink current control register */
#define ADP8870_ISCT1	0x1C  /* Independent Sink Current Timer Register LED[7:5] */
#define ADP8870_ISCT2	0x1D  /* Independent Sink Current Timer Register LED[4:1] */
#define ADP8870_ISCF	0x1E  /* Independent sink current fade register */
#define ADP8870_ISC1	0x1F  /* Independent Sink Current LED1 */
#define ADP8870_ISC2	0x20  /* Independent Sink Current LED2 */
#define ADP8870_ISC3	0x21  /* Independent Sink Current LED3 */
#define ADP8870_ISC4	0x22  /* Independent Sink Current LED4 */
#define ADP8870_ISC5	0x23  /* Independent Sink Current LED5 */
#define ADP8870_ISC6	0x24  /* Independent Sink Current LED6 */
#define ADP8870_ISC7	0x25  /* Independent Sink Current LED7 (Brightness Level 1-daylight) */
#define ADP8870_ISC7_L2	0x26  /* Independent Sink Current LED7 (Brightness Level 2-bright) */
#define ADP8870_ISC7_L3	0x27  /* Independent Sink Current LED7 (Brightness Level 3-office) */
#define ADP8870_ISC7_L4	0x28  /* Independent Sink Current LED7 (Brightness Level 4-indoor) */
#define ADP8870_ISC7_L5	0x29  /* Independent Sink Current LED7 (Brightness Level 5-dark) */
#define ADP8870_CMP_CTL	0x2D  /* ALS Comparator Control Register */
#define ADP8870_ALS1_EN	0x2E  /* Main ALS comparator level enable */
#define ADP8870_ALS2_EN	0x2F  /* Second ALS comparator level enable */
#define ADP8870_ALS1_STAT 0x30  /* Main ALS Comparator Status Register */
#define ADP8870_ALS2_STAT 0x31  /* Second ALS Comparator Status Register */
#define ADP8870_L2TRP	0x32  /* L2 comparator reference */
#define ADP8870_L2HYS	0x33  /* L2 hysteresis */
#define ADP8870_L3TRP	0x34  /* L3 comparator reference */
#define ADP8870_L3HYS	0x35  /* L3 hysteresis */
#define ADP8870_L4TRP	0x36  /* L4 comparator reference */
#define ADP8870_L4HYS	0x37  /* L4 hysteresis */
#define ADP8870_L5TRP	0x38  /* L5 comparator reference */
#define ADP8870_L5HYS	0x39  /* L5 hysteresis */
#define ADP8870_PH1LEVL	0x40  /* First phototransistor ambient light level-low byte register */
#define ADP8870_PH1LEVH	0x41  /* First phototransistor ambient light level-high byte register */
#define ADP8870_PH2LEVL	0x42  /* Second phototransistor ambient light level-low byte register */
#define ADP8870_PH2LEVH	0x43  /* Second phototransistor ambient light level-high byte register */

#define ADP8870_MANUFID		0x3  /* Analog Devices AD8870 Manufacturer and device ID */
#define ADP8870_DEVID(x)	((x) & 0xF)
#define ADP8870_MANID(x)	((x) >> 4)

/* MDCR Device mode and status */
#define D7ALSEN			(1 << 7)
#define INT_CFG			(1 << 6)
#define NSTBY			(1 << 5)
#define DIM_EN			(1 << 4)
#define GDWN_DIS		(1 << 3)
#define SIS_EN			(1 << 2)
#define CMP_AUTOEN		(1 << 1)
#define BLEN			(1 << 0)

/* ADP8870_ALS1_EN Main ALS comparator level enable */
#define L5_EN			(1 << 3)
#define L4_EN			(1 << 2)
#define L3_EN			(1 << 1)
#define L2_EN			(1 << 0)

#define CFGR_BLV_SHIFT		3
#define CFGR_BLV_MASK		0x7
#define ADP8870_FLAG_LED_MASK	0xFF

#define FADE_VAL(in, out)	((0xF & (in)) | ((0xF & (out)) << 4))
#define BL_CFGR_VAL(law, blv)	((((blv) & CFGR_BLV_MASK) << CFGR_BLV_SHIFT) | ((0x3 & (law)) << 1))
#define ALS_CMPR_CFG_VAL(filt)	((0x7 & (filt)) << 1)

struct adp8870_bl {
	struct i2c_client *client;
	struct backlight_device *bl;
	struct adp8870_led *led;
	struct adp8870_backlight_platform_data *pdata;
	struct mutex lock;
	unsigned long cached_daylight_max;
	int id;
	int revid;
	int current_brightness;
};

struct adp8870_led {
	struct led_classdev	cdev;
	struct work_struct	work;
	struct i2c_client	*client;
	enum led_brightness	new_brightness;
	int			id;
	int			flags;
};

static int adp8870_read(struct i2c_client *client, int reg, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
		return ret;
	}

	*val = ret;
	return 0;
}


static int adp8870_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret)
		dev_err(&client->dev, "failed to write\n");

	return ret;
}

static int adp8870_set_bits(struct i2c_client *client, int reg, uint8_t bit_mask)
{
	struct adp8870_bl *data = i2c_get_clientdata(client);
	uint8_t reg_val;
	int ret;

	mutex_lock(&data->lock);

	ret = adp8870_read(client, reg, &reg_val);

	if (!ret && ((reg_val & bit_mask) != bit_mask)) {
		reg_val |= bit_mask;
		ret = adp8870_write(client, reg, reg_val);
	}

	mutex_unlock(&data->lock);
	return ret;
}

static int adp8870_clr_bits(struct i2c_client *client, int reg, uint8_t bit_mask)
{
	struct adp8870_bl *data = i2c_get_clientdata(client);
	uint8_t reg_val;
	int ret;

	mutex_lock(&data->lock);

	ret = adp8870_read(client, reg, &reg_val);

	if (!ret && (reg_val & bit_mask)) {
		reg_val &= ~bit_mask;
		ret = adp8870_write(client, reg, reg_val);
	}

	mutex_unlock(&data->lock);
	return ret;
}

/*
 * Independent sink / LED
 */
#if defined(ADP8870_USE_LEDS)
static void adp8870_led_work(struct work_struct *work)
{
	struct adp8870_led *led = container_of(work, struct adp8870_led, work);

	adp8870_write(led->client, ADP8870_ISC1 + led->id - 1,
			 led->new_brightness >> 1);
}

static void adp8870_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct adp8870_led *led;

	led = container_of(led_cdev, struct adp8870_led, cdev);
	led->new_brightness = value;
	/*
	 * Use workqueue for IO since I2C operations can sleep.
	 */
	schedule_work(&led->work);
}

static int adp8870_led_setup(struct adp8870_led *led)
{
	struct i2c_client *client = led->client;
	int ret = 0;

	ret = adp8870_write(client, ADP8870_ISC1 + led->id - 1, 0);
	if (ret)
		return ret;

	ret = adp8870_set_bits(client, ADP8870_ISCC, 1 << (led->id - 1));
	if (ret)
		return ret;

	if (led->id > 4)
		ret = adp8870_set_bits(client, ADP8870_ISCT1,
				(led->flags & 0x3) << ((led->id - 5) * 2));
	else
		ret = adp8870_set_bits(client, ADP8870_ISCT2,
				(led->flags & 0x3) << ((led->id - 1) * 2));

	return ret;
}

static int adp8870_led_probe(struct i2c_client *client)
{
	struct adp8870_backlight_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct adp8870_bl *data = i2c_get_clientdata(client);
	struct adp8870_led *led, *led_dat;
	struct led_info *cur_led;
	int ret, i;

	led = devm_kcalloc(&client->dev, pdata->num_leds, sizeof(*led),
				GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	ret = adp8870_write(client, ADP8870_ISCLAW, pdata->led_fade_law);
	if (ret)
		return ret;

	ret = adp8870_write(client, ADP8870_ISCT1,
			(pdata->led_on_time & 0x3) << 6);
	if (ret)
		return ret;

	ret = adp8870_write(client, ADP8870_ISCF,
			FADE_VAL(pdata->led_fade_in, pdata->led_fade_out));
	if (ret)
		return ret;

	for (i = 0; i < pdata->num_leds; ++i) {
		cur_led = &pdata->leds[i];
		led_dat = &led[i];

		led_dat->id = cur_led->flags & ADP8870_FLAG_LED_MASK;

		if (led_dat->id > 7 || led_dat->id < 1) {
			dev_err(&client->dev, "Invalid LED ID %d\n",
				led_dat->id);
			ret = -EINVAL;
			goto err;
		}

		if (pdata->bl_led_assign & (1 << (led_dat->id - 1))) {
			dev_err(&client->dev, "LED %d used by Backlight\n",
				led_dat->id);
			ret = -EBUSY;
			goto err;
		}

		led_dat->cdev.name = cur_led->name;
		led_dat->cdev.default_trigger = cur_led->default_trigger;
		led_dat->cdev.brightness_set = adp8870_led_set;
		led_dat->cdev.brightness = LED_OFF;
		led_dat->flags = cur_led->flags >> FLAG_OFFT_SHIFT;
		led_dat->client = client;
		led_dat->new_brightness = LED_OFF;
		INIT_WORK(&led_dat->work, adp8870_led_work);

		ret = led_classdev_register(&client->dev, &led_dat->cdev);
		if (ret) {
			dev_err(&client->dev, "failed to register LED %d\n",
				led_dat->id);
			goto err;
		}

		ret = adp8870_led_setup(led_dat);
		if (ret) {
			dev_err(&client->dev, "failed to write\n");
			i++;
			goto err;
		}
	}

	data->led = led;

	return 0;

 err:
	for (i = i - 1; i >= 0; --i) {
		led_classdev_unregister(&led[i].cdev);
		cancel_work_sync(&led[i].work);
	}

	return ret;
}

static int adp8870_led_remove(struct i2c_client *client)
{
	struct adp8870_backlight_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct adp8870_bl *data = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&data->led[i].cdev);
		cancel_work_sync(&data->led[i].work);
	}

	return 0;
}
#else
static int adp8870_led_probe(struct i2c_client *client)
{
	return 0;
}

static int adp8870_led_remove(struct i2c_client *client)
{
	return 0;
}
#endif

static int adp8870_bl_set(struct backlight_device *bl, int brightness)
{
	struct adp8870_bl *data = bl_get_data(bl);
	struct i2c_client *client = data->client;
	int ret = 0;

	if (data->pdata->en_ambl_sens) {
		if ((brightness > 0) && (brightness < ADP8870_MAX_BRIGHTNESS)) {
			/* Disable Ambient Light auto adjust */
			ret = adp8870_clr_bits(client, ADP8870_MDCR,
					CMP_AUTOEN);
			if (ret)
				return ret;
			ret = adp8870_write(client, ADP8870_BLMX1, brightness);
			if (ret)
				return ret;
		} else {
			/*
			 * MAX_BRIGHTNESS -> Enable Ambient Light auto adjust
			 * restore daylight l1 sysfs brightness
			 */
			ret = adp8870_write(client, ADP8870_BLMX1,
					 data->cached_daylight_max);
			if (ret)
				return ret;

			ret = adp8870_set_bits(client, ADP8870_MDCR,
					 CMP_AUTOEN);
			if (ret)
				return ret;
		}
	} else {
		ret = adp8870_write(client, ADP8870_BLMX1, brightness);
		if (ret)
			return ret;
	}

	if (data->current_brightness && brightness == 0)
		ret = adp8870_set_bits(client,
				ADP8870_MDCR, DIM_EN);
	else if (data->current_brightness == 0 && brightness)
		ret = adp8870_clr_bits(client,
				ADP8870_MDCR, DIM_EN);

	if (!ret)
		data->current_brightness = brightness;

	return ret;
}

static int adp8870_bl_update_status(struct backlight_device *bl)
{
	return adp8870_bl_set(bl, backlight_get_brightness(bl));
}

static int adp8870_bl_get_brightness(struct backlight_device *bl)
{
	struct adp8870_bl *data = bl_get_data(bl);

	return data->current_brightness;
}

static const struct backlight_ops adp8870_bl_ops = {
	.update_status	= adp8870_bl_update_status,
	.get_brightness	= adp8870_bl_get_brightness,
};

static int adp8870_bl_setup(struct backlight_device *bl)
{
	struct adp8870_bl *data = bl_get_data(bl);
	struct i2c_client *client = data->client;
	struct adp8870_backlight_platform_data *pdata = data->pdata;
	int ret = 0;

	ret = adp8870_write(client, ADP8870_BLSEL, ~pdata->bl_led_assign);
	if (ret)
		return ret;

	ret = adp8870_write(client, ADP8870_PWMLED, pdata->pwm_assign);
	if (ret)
		return ret;

	ret = adp8870_write(client, ADP8870_BLMX1, pdata->l1_daylight_max);
	if (ret)
		return ret;

	ret = adp8870_write(client, ADP8870_BLDM1, pdata->l1_daylight_dim);
	if (ret)
		return ret;

	if (pdata->en_ambl_sens) {
		data->cached_daylight_max = pdata->l1_daylight_max;
		ret = adp8870_write(client, ADP8870_BLMX2,
						pdata->l2_bright_max);
		if (ret)
			return ret;
		ret = adp8870_write(client, ADP8870_BLDM2,
						pdata->l2_bright_dim);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_BLMX3,
						pdata->l3_office_max);
		if (ret)
			return ret;
		ret = adp8870_write(client, ADP8870_BLDM3,
						pdata->l3_office_dim);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_BLMX4,
						pdata->l4_indoor_max);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_BLDM4,
						pdata->l4_indor_dim);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_BLMX5,
						pdata->l5_dark_max);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_BLDM5,
						pdata->l5_dark_dim);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L2TRP, pdata->l2_trip);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L2HYS, pdata->l2_hyst);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L3TRP, pdata->l3_trip);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L3HYS, pdata->l3_hyst);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L4TRP, pdata->l4_trip);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L4HYS, pdata->l4_hyst);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L5TRP, pdata->l5_trip);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_L5HYS, pdata->l5_hyst);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_ALS1_EN, L5_EN | L4_EN |
						L3_EN | L2_EN);
		if (ret)
			return ret;

		ret = adp8870_write(client, ADP8870_CMP_CTL,
			ALS_CMPR_CFG_VAL(pdata->abml_filt));
		if (ret)
			return ret;
	}

	ret = adp8870_write(client, ADP8870_CFGR,
			BL_CFGR_VAL(pdata->bl_fade_law, 0));
	if (ret)
		return ret;

	ret = adp8870_write(client, ADP8870_BLFR, FADE_VAL(pdata->bl_fade_in,
			pdata->bl_fade_out));
	if (ret)
		return ret;
	/*
	 * ADP8870 Rev0 requires GDWN_DIS bit set
	 */

	ret = adp8870_set_bits(client, ADP8870_MDCR, BLEN | DIM_EN | NSTBY |
			(data->revid == 0 ? GDWN_DIS : 0));

	return ret;
}

static ssize_t adp8870_show(struct device *dev, char *buf, int reg)
{
	struct adp8870_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;

	mutex_lock(&data->lock);
	error = adp8870_read(data->client, reg, &reg_val);
	mutex_unlock(&data->lock);

	if (error < 0)
		return error;

	return sprintf(buf, "%u\n", reg_val);
}

static ssize_t adp8870_store(struct device *dev, const char *buf,
			 size_t count, int reg)
{
	struct adp8870_bl *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->lock);
	adp8870_write(data->client, reg, val);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t adp8870_bl_l5_dark_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLMX5);
}

static ssize_t adp8870_bl_l5_dark_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLMX5);
}
static DEVICE_ATTR(l5_dark_max, 0664, adp8870_bl_l5_dark_max_show,
			adp8870_bl_l5_dark_max_store);


static ssize_t adp8870_bl_l4_indoor_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLMX4);
}

static ssize_t adp8870_bl_l4_indoor_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLMX4);
}
static DEVICE_ATTR(l4_indoor_max, 0664, adp8870_bl_l4_indoor_max_show,
			adp8870_bl_l4_indoor_max_store);


static ssize_t adp8870_bl_l3_office_max_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLMX3);
}

static ssize_t adp8870_bl_l3_office_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLMX3);
}

static DEVICE_ATTR(l3_office_max, 0664, adp8870_bl_l3_office_max_show,
			adp8870_bl_l3_office_max_store);

static ssize_t adp8870_bl_l2_bright_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLMX2);
}

static ssize_t adp8870_bl_l2_bright_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLMX2);
}
static DEVICE_ATTR(l2_bright_max, 0664, adp8870_bl_l2_bright_max_show,
			adp8870_bl_l2_bright_max_store);

static ssize_t adp8870_bl_l1_daylight_max_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLMX1);
}

static ssize_t adp8870_bl_l1_daylight_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct adp8870_bl *data = dev_get_drvdata(dev);
	int ret = kstrtoul(buf, 10, &data->cached_daylight_max);

	if (ret)
		return ret;

	return adp8870_store(dev, buf, count, ADP8870_BLMX1);
}
static DEVICE_ATTR(l1_daylight_max, 0664, adp8870_bl_l1_daylight_max_show,
			adp8870_bl_l1_daylight_max_store);

static ssize_t adp8870_bl_l5_dark_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLDM5);
}

static ssize_t adp8870_bl_l5_dark_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLDM5);
}
static DEVICE_ATTR(l5_dark_dim, 0664, adp8870_bl_l5_dark_dim_show,
			adp8870_bl_l5_dark_dim_store);

static ssize_t adp8870_bl_l4_indoor_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLDM4);
}

static ssize_t adp8870_bl_l4_indoor_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLDM4);
}
static DEVICE_ATTR(l4_indoor_dim, 0664, adp8870_bl_l4_indoor_dim_show,
			adp8870_bl_l4_indoor_dim_store);


static ssize_t adp8870_bl_l3_office_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLDM3);
}

static ssize_t adp8870_bl_l3_office_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLDM3);
}
static DEVICE_ATTR(l3_office_dim, 0664, adp8870_bl_l3_office_dim_show,
			adp8870_bl_l3_office_dim_store);

static ssize_t adp8870_bl_l2_bright_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLDM2);
}

static ssize_t adp8870_bl_l2_bright_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLDM2);
}
static DEVICE_ATTR(l2_bright_dim, 0664, adp8870_bl_l2_bright_dim_show,
			adp8870_bl_l2_bright_dim_store);

static ssize_t adp8870_bl_l1_daylight_dim_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return adp8870_show(dev, buf, ADP8870_BLDM1);
}

static ssize_t adp8870_bl_l1_daylight_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8870_store(dev, buf, count, ADP8870_BLDM1);
}
static DEVICE_ATTR(l1_daylight_dim, 0664, adp8870_bl_l1_daylight_dim_show,
			adp8870_bl_l1_daylight_dim_store);

#ifdef ADP8870_EXT_FEATURES
static ssize_t adp8870_bl_ambient_light_level_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct adp8870_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;
	uint16_t ret_val;

	mutex_lock(&data->lock);
	error = adp8870_read(data->client, ADP8870_PH1LEVL, &reg_val);
	if (error < 0) {
		mutex_unlock(&data->lock);
		return error;
	}
	ret_val = reg_val;
	error = adp8870_read(data->client, ADP8870_PH1LEVH, &reg_val);
	mutex_unlock(&data->lock);

	if (error < 0)
		return error;

	/* Return 13-bit conversion value for the first light sensor */
	ret_val += (reg_val & 0x1F) << 8;

	return sprintf(buf, "%u\n", ret_val);
}
static DEVICE_ATTR(ambient_light_level, 0444,
		adp8870_bl_ambient_light_level_show, NULL);

static ssize_t adp8870_bl_ambient_light_zone_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct adp8870_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;

	mutex_lock(&data->lock);
	error = adp8870_read(data->client, ADP8870_CFGR, &reg_val);
	mutex_unlock(&data->lock);

	if (error < 0)
		return error;

	return sprintf(buf, "%u\n",
		((reg_val >> CFGR_BLV_SHIFT) & CFGR_BLV_MASK) + 1);
}

static ssize_t adp8870_bl_ambient_light_zone_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adp8870_bl *data = dev_get_drvdata(dev);
	unsigned long val;
	uint8_t reg_val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val == 0) {
		/* Enable automatic ambient light sensing */
		adp8870_set_bits(data->client, ADP8870_MDCR, CMP_AUTOEN);
	} else if ((val > 0) && (val < 6)) {
		/* Disable automatic ambient light sensing */
		adp8870_clr_bits(data->client, ADP8870_MDCR, CMP_AUTOEN);

		/* Set user supplied ambient light zone */
		mutex_lock(&data->lock);
		ret = adp8870_read(data->client, ADP8870_CFGR, &reg_val);
		if (!ret) {
			reg_val &= ~(CFGR_BLV_MASK << CFGR_BLV_SHIFT);
			reg_val |= (val - 1) << CFGR_BLV_SHIFT;
			adp8870_write(data->client, ADP8870_CFGR, reg_val);
		}
		mutex_unlock(&data->lock);
	}

	return count;
}
static DEVICE_ATTR(ambient_light_zone, 0664,
		adp8870_bl_ambient_light_zone_show,
		adp8870_bl_ambient_light_zone_store);
#endif

static struct attribute *adp8870_bl_attributes[] = {
	&dev_attr_l5_dark_max.attr,
	&dev_attr_l5_dark_dim.attr,
	&dev_attr_l4_indoor_max.attr,
	&dev_attr_l4_indoor_dim.attr,
	&dev_attr_l3_office_max.attr,
	&dev_attr_l3_office_dim.attr,
	&dev_attr_l2_bright_max.attr,
	&dev_attr_l2_bright_dim.attr,
	&dev_attr_l1_daylight_max.attr,
	&dev_attr_l1_daylight_dim.attr,
#ifdef ADP8870_EXT_FEATURES
	&dev_attr_ambient_light_level.attr,
	&dev_attr_ambient_light_zone.attr,
#endif
	NULL
};

static const struct attribute_group adp8870_bl_attr_group = {
	.attrs = adp8870_bl_attributes,
};

static int adp8870_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct backlight_properties props;
	struct backlight_device *bl;
	struct adp8870_bl *data;
	struct adp8870_backlight_platform_data *pdata =
		dev_get_platdata(&client->dev);
	uint8_t reg_val;
	int ret;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	if (!pdata) {
		dev_err(&client->dev, "no platform data?\n");
		return -EINVAL;
	}

	ret = adp8870_read(client, ADP8870_MFDVID, &reg_val);
	if (ret < 0)
		return -EIO;

	if (ADP8870_MANID(reg_val) != ADP8870_MANUFID) {
		dev_err(&client->dev, "failed to probe\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->revid = ADP8870_DEVID(reg_val);
	data->client = client;
	data->pdata = pdata;
	data->id = id->driver_data;
	data->current_brightness = 0;
	i2c_set_clientdata(client, data);

	mutex_init(&data->lock);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = props.brightness = ADP8870_MAX_BRIGHTNESS;
	bl = devm_backlight_device_register(&client->dev,
				dev_driver_string(&client->dev),
				&client->dev, data, &adp8870_bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&client->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	data->bl = bl;

	if (pdata->en_ambl_sens) {
		ret = sysfs_create_group(&bl->dev.kobj,
			&adp8870_bl_attr_group);
		if (ret) {
			dev_err(&client->dev, "failed to register sysfs\n");
			return ret;
		}
	}

	ret = adp8870_bl_setup(bl);
	if (ret) {
		ret = -EIO;
		goto out;
	}

	backlight_update_status(bl);

	dev_info(&client->dev, "Rev.%d Backlight\n", data->revid);

	if (pdata->num_leds)
		adp8870_led_probe(client);

	return 0;

out:
	if (data->pdata->en_ambl_sens)
		sysfs_remove_group(&data->bl->dev.kobj,
			&adp8870_bl_attr_group);

	return ret;
}

static void adp8870_remove(struct i2c_client *client)
{
	struct adp8870_bl *data = i2c_get_clientdata(client);

	adp8870_clr_bits(client, ADP8870_MDCR, NSTBY);

	if (data->led)
		adp8870_led_remove(client);

	if (data->pdata->en_ambl_sens)
		sysfs_remove_group(&data->bl->dev.kobj,
			&adp8870_bl_attr_group);
}

#ifdef CONFIG_PM_SLEEP
static int adp8870_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	adp8870_clr_bits(client, ADP8870_MDCR, NSTBY);

	return 0;
}

static int adp8870_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	adp8870_set_bits(client, ADP8870_MDCR, NSTBY | BLEN);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(adp8870_i2c_pm_ops, adp8870_i2c_suspend,
			adp8870_i2c_resume);

static const struct i2c_device_id adp8870_id[] = {
	{ "adp8870", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp8870_id);

static struct i2c_driver adp8870_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &adp8870_i2c_pm_ops,
	},
	.probe_new = adp8870_probe,
	.remove   = adp8870_remove,
	.id_table = adp8870_id,
};

module_i2c_driver(adp8870_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("ADP8870 Backlight driver");
