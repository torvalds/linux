// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Backlight driver for Analog Devices ADP8860 Backlight Devices
 *
 * Copyright 2009-2010 Analog Devices Inc.
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
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <linux/platform_data/adp8860.h>
#define ADP8860_EXT_FEATURES
#define ADP8860_USE_LEDS

#define ADP8860_MFDVID 0x00 /* Manufacturer and device ID */
#define ADP8860_MDCR 0x01 /* Device mode and status */
#define ADP8860_MDCR2 0x02 /* Device mode and Status Register 2 */
#define ADP8860_INTR_EN 0x03 /* Interrupts enable */
#define ADP8860_CFGR 0x04 /* Configuration register */
#define ADP8860_BLSEN 0x05 /* Sink enable backlight or independent */
#define ADP8860_BLOFF 0x06 /* Backlight off timeout */
#define ADP8860_BLDIM 0x07 /* Backlight dim timeout */
#define ADP8860_BLFR 0x08 /* Backlight fade in and out rates */
#define ADP8860_BLMX1 0x09 /* Backlight (Brightness Level 1-daylight) maximum current */
#define ADP8860_BLDM1 0x0A /* Backlight (Brightness Level 1-daylight) dim current */
#define ADP8860_BLMX2 0x0B /* Backlight (Brightness Level 2-office) maximum current */
#define ADP8860_BLDM2 0x0C /* Backlight (Brightness Level 2-office) dim current */
#define ADP8860_BLMX3 0x0D /* Backlight (Brightness Level 3-dark) maximum current */
#define ADP8860_BLDM3 0x0E /* Backlight (Brightness Level 3-dark) dim current */
#define ADP8860_ISCFR 0x0F /* Independent sink current fade control register */
#define ADP8860_ISCC 0x10 /* Independent sink current control register */
#define ADP8860_ISCT1 0x11 /* Independent Sink Current Timer Register LED[7:5] */
#define ADP8860_ISCT2 0x12 /* Independent Sink Current Timer Register LED[4:1] */
#define ADP8860_ISCF 0x13 /* Independent sink current fade register */
#define ADP8860_ISC7 0x14 /* Independent Sink Current LED7 */
#define ADP8860_ISC6 0x15 /* Independent Sink Current LED6 */
#define ADP8860_ISC5 0x16 /* Independent Sink Current LED5 */
#define ADP8860_ISC4 0x17 /* Independent Sink Current LED4 */
#define ADP8860_ISC3 0x18 /* Independent Sink Current LED3 */
#define ADP8860_ISC2 0x19 /* Independent Sink Current LED2 */
#define ADP8860_ISC1 0x1A /* Independent Sink Current LED1 */
#define ADP8860_CCFG 0x1B /* Comparator configuration */
#define ADP8860_CCFG2 0x1C /* Second comparator configuration */
#define ADP8860_L2_TRP 0x1D /* L2 comparator reference */
#define ADP8860_L2_HYS 0x1E /* L2 hysteresis */
#define ADP8860_L3_TRP 0x1F /* L3 comparator reference */
#define ADP8860_L3_HYS 0x20 /* L3 hysteresis */
#define ADP8860_PH1LEVL 0x21 /* First phototransistor ambient light level-low byte register */
#define ADP8860_PH1LEVH 0x22 /* First phototransistor ambient light level-high byte register */
#define ADP8860_PH2LEVL 0x23 /* Second phototransistor ambient light level-low byte register */
#define ADP8860_PH2LEVH 0x24 /* Second phototransistor ambient light level-high byte register */

#define ADP8860_MANUFID		0x0  /* Analog Devices ADP8860 Manufacturer ID */
#define ADP8861_MANUFID		0x4  /* Analog Devices ADP8861 Manufacturer ID */
#define ADP8863_MANUFID		0x2  /* Analog Devices ADP8863 Manufacturer ID */

#define ADP8860_DEVID(x)	((x) & 0xF)
#define ADP8860_MANID(x)	((x) >> 4)

/* MDCR Device mode and status */
#define INT_CFG			(1 << 6)
#define NSTBY			(1 << 5)
#define DIM_EN			(1 << 4)
#define GDWN_DIS		(1 << 3)
#define SIS_EN			(1 << 2)
#define CMP_AUTOEN		(1 << 1)
#define BLEN			(1 << 0)

/* ADP8860_CCFG Main ALS comparator level enable */
#define L3_EN			(1 << 1)
#define L2_EN			(1 << 0)

#define CFGR_BLV_SHIFT		3
#define CFGR_BLV_MASK		0x3
#define ADP8860_FLAG_LED_MASK	0xFF

#define FADE_VAL(in, out)	((0xF & (in)) | ((0xF & (out)) << 4))
#define BL_CFGR_VAL(law, blv)	((((blv) & CFGR_BLV_MASK) << CFGR_BLV_SHIFT) | ((0x3 & (law)) << 1))
#define ALS_CCFG_VAL(filt)	((0x7 & filt) << 5)

enum {
	adp8860,
	adp8861,
	adp8863
};

struct adp8860_led {
	struct led_classdev	cdev;
	struct work_struct	work;
	struct i2c_client	*client;
	enum led_brightness	new_brightness;
	int			id;
	int			flags;
};

struct adp8860_bl {
	struct i2c_client *client;
	struct backlight_device *bl;
	struct adp8860_led *led;
	struct adp8860_backlight_platform_data *pdata;
	struct mutex lock;
	unsigned long cached_daylight_max;
	int id;
	int revid;
	int current_brightness;
	unsigned en_ambl_sens:1;
	unsigned gdwn_dis:1;
};

static int adp8860_read(struct i2c_client *client, int reg, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
		return ret;
	}

	*val = (uint8_t)ret;
	return 0;
}

static int adp8860_write(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

static int adp8860_set_bits(struct i2c_client *client, int reg, uint8_t bit_mask)
{
	struct adp8860_bl *data = i2c_get_clientdata(client);
	uint8_t reg_val;
	int ret;

	mutex_lock(&data->lock);

	ret = adp8860_read(client, reg, &reg_val);

	if (!ret && ((reg_val & bit_mask) != bit_mask)) {
		reg_val |= bit_mask;
		ret = adp8860_write(client, reg, reg_val);
	}

	mutex_unlock(&data->lock);
	return ret;
}

static int adp8860_clr_bits(struct i2c_client *client, int reg, uint8_t bit_mask)
{
	struct adp8860_bl *data = i2c_get_clientdata(client);
	uint8_t reg_val;
	int ret;

	mutex_lock(&data->lock);

	ret = adp8860_read(client, reg, &reg_val);

	if (!ret && (reg_val & bit_mask)) {
		reg_val &= ~bit_mask;
		ret = adp8860_write(client, reg, reg_val);
	}

	mutex_unlock(&data->lock);
	return ret;
}

/*
 * Independent sink / LED
 */
#if defined(ADP8860_USE_LEDS)
static void adp8860_led_work(struct work_struct *work)
{
	struct adp8860_led *led = container_of(work, struct adp8860_led, work);

	adp8860_write(led->client, ADP8860_ISC1 - led->id + 1,
			 led->new_brightness >> 1);
}

static void adp8860_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct adp8860_led *led;

	led = container_of(led_cdev, struct adp8860_led, cdev);
	led->new_brightness = value;
	schedule_work(&led->work);
}

static int adp8860_led_setup(struct adp8860_led *led)
{
	struct i2c_client *client = led->client;
	int ret = 0;

	ret = adp8860_write(client, ADP8860_ISC1 - led->id + 1, 0);
	ret |= adp8860_set_bits(client, ADP8860_ISCC, 1 << (led->id - 1));

	if (led->id > 4)
		ret |= adp8860_set_bits(client, ADP8860_ISCT1,
				(led->flags & 0x3) << ((led->id - 5) * 2));
	else
		ret |= adp8860_set_bits(client, ADP8860_ISCT2,
				(led->flags & 0x3) << ((led->id - 1) * 2));

	return ret;
}

static int adp8860_led_probe(struct i2c_client *client)
{
	struct adp8860_backlight_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct adp8860_bl *data = i2c_get_clientdata(client);
	struct adp8860_led *led, *led_dat;
	struct led_info *cur_led;
	int ret, i;

	led = devm_kcalloc(&client->dev, pdata->num_leds, sizeof(*led),
				GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	ret = adp8860_write(client, ADP8860_ISCFR, pdata->led_fade_law);
	ret = adp8860_write(client, ADP8860_ISCT1,
			(pdata->led_on_time & 0x3) << 6);
	ret |= adp8860_write(client, ADP8860_ISCF,
			FADE_VAL(pdata->led_fade_in, pdata->led_fade_out));

	if (ret) {
		dev_err(&client->dev, "failed to write\n");
		return ret;
	}

	for (i = 0; i < pdata->num_leds; ++i) {
		cur_led = &pdata->leds[i];
		led_dat = &led[i];

		led_dat->id = cur_led->flags & ADP8860_FLAG_LED_MASK;

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
		led_dat->cdev.brightness_set = adp8860_led_set;
		led_dat->cdev.brightness = LED_OFF;
		led_dat->flags = cur_led->flags >> FLAG_OFFT_SHIFT;
		led_dat->client = client;
		led_dat->new_brightness = LED_OFF;
		INIT_WORK(&led_dat->work, adp8860_led_work);

		ret = led_classdev_register(&client->dev, &led_dat->cdev);
		if (ret) {
			dev_err(&client->dev, "failed to register LED %d\n",
				led_dat->id);
			goto err;
		}

		ret = adp8860_led_setup(led_dat);
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

static int adp8860_led_remove(struct i2c_client *client)
{
	struct adp8860_backlight_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct adp8860_bl *data = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&data->led[i].cdev);
		cancel_work_sync(&data->led[i].work);
	}

	return 0;
}
#else
static int adp8860_led_probe(struct i2c_client *client)
{
	return 0;
}

static int adp8860_led_remove(struct i2c_client *client)
{
	return 0;
}
#endif

static int adp8860_bl_set(struct backlight_device *bl, int brightness)
{
	struct adp8860_bl *data = bl_get_data(bl);
	struct i2c_client *client = data->client;
	int ret = 0;

	if (data->en_ambl_sens) {
		if ((brightness > 0) && (brightness < ADP8860_MAX_BRIGHTNESS)) {
			/* Disable Ambient Light auto adjust */
			ret |= adp8860_clr_bits(client, ADP8860_MDCR,
					CMP_AUTOEN);
			ret |= adp8860_write(client, ADP8860_BLMX1, brightness);
		} else {
			/*
			 * MAX_BRIGHTNESS -> Enable Ambient Light auto adjust
			 * restore daylight l1 sysfs brightness
			 */
			ret |= adp8860_write(client, ADP8860_BLMX1,
					 data->cached_daylight_max);
			ret |= adp8860_set_bits(client, ADP8860_MDCR,
					 CMP_AUTOEN);
		}
	} else
		ret |= adp8860_write(client, ADP8860_BLMX1, brightness);

	if (data->current_brightness && brightness == 0)
		ret |= adp8860_set_bits(client,
				ADP8860_MDCR, DIM_EN);
	else if (data->current_brightness == 0 && brightness)
		ret |= adp8860_clr_bits(client,
				ADP8860_MDCR, DIM_EN);

	if (!ret)
		data->current_brightness = brightness;

	return ret;
}

static int adp8860_bl_update_status(struct backlight_device *bl)
{
	return adp8860_bl_set(bl, backlight_get_brightness(bl));
}

static int adp8860_bl_get_brightness(struct backlight_device *bl)
{
	struct adp8860_bl *data = bl_get_data(bl);

	return data->current_brightness;
}

static const struct backlight_ops adp8860_bl_ops = {
	.update_status	= adp8860_bl_update_status,
	.get_brightness	= adp8860_bl_get_brightness,
};

static int adp8860_bl_setup(struct backlight_device *bl)
{
	struct adp8860_bl *data = bl_get_data(bl);
	struct i2c_client *client = data->client;
	struct adp8860_backlight_platform_data *pdata = data->pdata;
	int ret = 0;

	ret |= adp8860_write(client, ADP8860_BLSEN, ~pdata->bl_led_assign);
	ret |= adp8860_write(client, ADP8860_BLMX1, pdata->l1_daylight_max);
	ret |= adp8860_write(client, ADP8860_BLDM1, pdata->l1_daylight_dim);

	if (data->en_ambl_sens) {
		data->cached_daylight_max = pdata->l1_daylight_max;
		ret |= adp8860_write(client, ADP8860_BLMX2,
						pdata->l2_office_max);
		ret |= adp8860_write(client, ADP8860_BLDM2,
						pdata->l2_office_dim);
		ret |= adp8860_write(client, ADP8860_BLMX3,
						pdata->l3_dark_max);
		ret |= adp8860_write(client, ADP8860_BLDM3,
						pdata->l3_dark_dim);

		ret |= adp8860_write(client, ADP8860_L2_TRP, pdata->l2_trip);
		ret |= adp8860_write(client, ADP8860_L2_HYS, pdata->l2_hyst);
		ret |= adp8860_write(client, ADP8860_L3_TRP, pdata->l3_trip);
		ret |= adp8860_write(client, ADP8860_L3_HYS, pdata->l3_hyst);
		ret |= adp8860_write(client, ADP8860_CCFG, L2_EN | L3_EN |
						ALS_CCFG_VAL(pdata->abml_filt));
	}

	ret |= adp8860_write(client, ADP8860_CFGR,
			BL_CFGR_VAL(pdata->bl_fade_law, 0));

	ret |= adp8860_write(client, ADP8860_BLFR, FADE_VAL(pdata->bl_fade_in,
			pdata->bl_fade_out));

	ret |= adp8860_set_bits(client, ADP8860_MDCR, BLEN | DIM_EN | NSTBY |
			(data->gdwn_dis ? GDWN_DIS : 0));

	return ret;
}

static ssize_t adp8860_show(struct device *dev, char *buf, int reg)
{
	struct adp8860_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;

	mutex_lock(&data->lock);
	error = adp8860_read(data->client, reg, &reg_val);
	mutex_unlock(&data->lock);

	if (error < 0)
		return error;

	return sprintf(buf, "%u\n", reg_val);
}

static ssize_t adp8860_store(struct device *dev, const char *buf,
			 size_t count, int reg)
{
	struct adp8860_bl *data = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->lock);
	adp8860_write(data->client, reg, val);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t adp8860_bl_l3_dark_max_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return adp8860_show(dev, buf, ADP8860_BLMX3);
}

static ssize_t adp8860_bl_l3_dark_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return adp8860_store(dev, buf, count, ADP8860_BLMX3);
}

static DEVICE_ATTR(l3_dark_max, 0664, adp8860_bl_l3_dark_max_show,
			adp8860_bl_l3_dark_max_store);

static ssize_t adp8860_bl_l2_office_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return adp8860_show(dev, buf, ADP8860_BLMX2);
}

static ssize_t adp8860_bl_l2_office_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return adp8860_store(dev, buf, count, ADP8860_BLMX2);
}
static DEVICE_ATTR(l2_office_max, 0664, adp8860_bl_l2_office_max_show,
			adp8860_bl_l2_office_max_store);

static ssize_t adp8860_bl_l1_daylight_max_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8860_show(dev, buf, ADP8860_BLMX1);
}

static ssize_t adp8860_bl_l1_daylight_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct adp8860_bl *data = dev_get_drvdata(dev);
	int ret = kstrtoul(buf, 10, &data->cached_daylight_max);

	if (ret)
		return ret;

	return adp8860_store(dev, buf, count, ADP8860_BLMX1);
}
static DEVICE_ATTR(l1_daylight_max, 0664, adp8860_bl_l1_daylight_max_show,
			adp8860_bl_l1_daylight_max_store);

static ssize_t adp8860_bl_l3_dark_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8860_show(dev, buf, ADP8860_BLDM3);
}

static ssize_t adp8860_bl_l3_dark_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8860_store(dev, buf, count, ADP8860_BLDM3);
}
static DEVICE_ATTR(l3_dark_dim, 0664, adp8860_bl_l3_dark_dim_show,
			adp8860_bl_l3_dark_dim_store);

static ssize_t adp8860_bl_l2_office_dim_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return adp8860_show(dev, buf, ADP8860_BLDM2);
}

static ssize_t adp8860_bl_l2_office_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8860_store(dev, buf, count, ADP8860_BLDM2);
}
static DEVICE_ATTR(l2_office_dim, 0664, adp8860_bl_l2_office_dim_show,
			adp8860_bl_l2_office_dim_store);

static ssize_t adp8860_bl_l1_daylight_dim_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return adp8860_show(dev, buf, ADP8860_BLDM1);
}

static ssize_t adp8860_bl_l1_daylight_dim_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return adp8860_store(dev, buf, count, ADP8860_BLDM1);
}
static DEVICE_ATTR(l1_daylight_dim, 0664, adp8860_bl_l1_daylight_dim_show,
			adp8860_bl_l1_daylight_dim_store);

#ifdef ADP8860_EXT_FEATURES
static ssize_t adp8860_bl_ambient_light_level_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct adp8860_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;
	uint16_t ret_val;

	mutex_lock(&data->lock);
	error = adp8860_read(data->client, ADP8860_PH1LEVL, &reg_val);
	if (!error) {
		ret_val = reg_val;
		error = adp8860_read(data->client, ADP8860_PH1LEVH, &reg_val);
	}
	mutex_unlock(&data->lock);

	if (error)
		return error;

	/* Return 13-bit conversion value for the first light sensor */
	ret_val += (reg_val & 0x1F) << 8;

	return sprintf(buf, "%u\n", ret_val);
}
static DEVICE_ATTR(ambient_light_level, 0444,
		adp8860_bl_ambient_light_level_show, NULL);

static ssize_t adp8860_bl_ambient_light_zone_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct adp8860_bl *data = dev_get_drvdata(dev);
	int error;
	uint8_t reg_val;

	mutex_lock(&data->lock);
	error = adp8860_read(data->client, ADP8860_CFGR, &reg_val);
	mutex_unlock(&data->lock);

	if (error < 0)
		return error;

	return sprintf(buf, "%u\n",
		((reg_val >> CFGR_BLV_SHIFT) & CFGR_BLV_MASK) + 1);
}

static ssize_t adp8860_bl_ambient_light_zone_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adp8860_bl *data = dev_get_drvdata(dev);
	unsigned long val;
	uint8_t reg_val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val == 0) {
		/* Enable automatic ambient light sensing */
		adp8860_set_bits(data->client, ADP8860_MDCR, CMP_AUTOEN);
	} else if ((val > 0) && (val <= 3)) {
		/* Disable automatic ambient light sensing */
		adp8860_clr_bits(data->client, ADP8860_MDCR, CMP_AUTOEN);

		/* Set user supplied ambient light zone */
		mutex_lock(&data->lock);
		ret = adp8860_read(data->client, ADP8860_CFGR, &reg_val);
		if (!ret) {
			reg_val &= ~(CFGR_BLV_MASK << CFGR_BLV_SHIFT);
			reg_val |= (val - 1) << CFGR_BLV_SHIFT;
			adp8860_write(data->client, ADP8860_CFGR, reg_val);
		}
		mutex_unlock(&data->lock);
	}

	return count;
}
static DEVICE_ATTR(ambient_light_zone, 0664,
		adp8860_bl_ambient_light_zone_show,
		adp8860_bl_ambient_light_zone_store);
#endif

static struct attribute *adp8860_bl_attributes[] = {
	&dev_attr_l3_dark_max.attr,
	&dev_attr_l3_dark_dim.attr,
	&dev_attr_l2_office_max.attr,
	&dev_attr_l2_office_dim.attr,
	&dev_attr_l1_daylight_max.attr,
	&dev_attr_l1_daylight_dim.attr,
#ifdef ADP8860_EXT_FEATURES
	&dev_attr_ambient_light_level.attr,
	&dev_attr_ambient_light_zone.attr,
#endif
	NULL
};

static const struct attribute_group adp8860_bl_attr_group = {
	.attrs = adp8860_bl_attributes,
};

static int adp8860_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct backlight_device *bl;
	struct adp8860_bl *data;
	struct adp8860_backlight_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct backlight_properties props;
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

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	ret = adp8860_read(client, ADP8860_MFDVID, &reg_val);
	if (ret < 0)
		return ret;

	switch (ADP8860_MANID(reg_val)) {
	case ADP8863_MANUFID:
		data->gdwn_dis = !!pdata->gdwn_dis;
		fallthrough;
	case ADP8860_MANUFID:
		data->en_ambl_sens = !!pdata->en_ambl_sens;
		break;
	case ADP8861_MANUFID:
		data->gdwn_dis = !!pdata->gdwn_dis;
		break;
	default:
		dev_err(&client->dev, "failed to probe\n");
		return -ENODEV;
	}

	/* It's confirmed that the DEVID field is actually a REVID */

	data->revid = ADP8860_DEVID(reg_val);
	data->client = client;
	data->pdata = pdata;
	data->id = id->driver_data;
	data->current_brightness = 0;
	i2c_set_clientdata(client, data);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = ADP8860_MAX_BRIGHTNESS;

	mutex_init(&data->lock);

	bl = devm_backlight_device_register(&client->dev,
				dev_driver_string(&client->dev),
				&client->dev, data, &adp8860_bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&client->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = ADP8860_MAX_BRIGHTNESS;

	data->bl = bl;

	if (data->en_ambl_sens)
		ret = sysfs_create_group(&bl->dev.kobj,
			&adp8860_bl_attr_group);

	if (ret) {
		dev_err(&client->dev, "failed to register sysfs\n");
		return ret;
	}

	ret = adp8860_bl_setup(bl);
	if (ret) {
		ret = -EIO;
		goto out;
	}

	backlight_update_status(bl);

	dev_info(&client->dev, "%s Rev.%d Backlight\n",
		client->name, data->revid);

	if (pdata->num_leds)
		adp8860_led_probe(client);

	return 0;

out:
	if (data->en_ambl_sens)
		sysfs_remove_group(&data->bl->dev.kobj,
			&adp8860_bl_attr_group);

	return ret;
}

static void adp8860_remove(struct i2c_client *client)
{
	struct adp8860_bl *data = i2c_get_clientdata(client);

	adp8860_clr_bits(client, ADP8860_MDCR, NSTBY);

	if (data->led)
		adp8860_led_remove(client);

	if (data->en_ambl_sens)
		sysfs_remove_group(&data->bl->dev.kobj,
			&adp8860_bl_attr_group);
}

#ifdef CONFIG_PM_SLEEP
static int adp8860_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	adp8860_clr_bits(client, ADP8860_MDCR, NSTBY);

	return 0;
}

static int adp8860_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	adp8860_set_bits(client, ADP8860_MDCR, NSTBY | BLEN);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(adp8860_i2c_pm_ops, adp8860_i2c_suspend,
			adp8860_i2c_resume);

static const struct i2c_device_id adp8860_id[] = {
	{ "adp8860", adp8860 },
	{ "adp8861", adp8861 },
	{ "adp8863", adp8863 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp8860_id);

static struct i2c_driver adp8860_driver = {
	.driver = {
		.name	= KBUILD_MODNAME,
		.pm	= &adp8860_i2c_pm_ops,
	},
	.probe = adp8860_probe,
	.remove = adp8860_remove,
	.id_table = adp8860_id,
};

module_i2c_driver(adp8860_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("ADP8860 Backlight driver");
