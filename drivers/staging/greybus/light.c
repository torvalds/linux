/*
 * Greybus Lights protocol driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "greybus.h"
#include "greybus_protocols.h"

#define NAMES_MAX	32

struct gb_channel {
	u8				id;
	u32				flags;
	u32				color;
	char				*color_name;
	u8				fade_in;
	u8				fade_out;
	u32				mode;
	char				*mode_name;
	struct attribute		**attrs;
	struct attribute_group		*attr_group;
	const struct attribute_group	**attr_groups;
	struct work_struct		work_brightness_set;
	struct led_classdev		*led;
#ifdef LED_HAVE_FLASH
	struct led_classdev_flash	fled;
	struct led_flash_setting	intensity_uA;
	struct led_flash_setting	timeout_us;
#else
	struct led_classdev		cled;
#endif
	struct gb_light			*light;
	bool				is_registered;
	bool				releasing;
	bool				strobe_state;
};

struct gb_light {
	u8			id;
	char			*name;
	struct gb_lights	*glights;
	u32			flags;
	u8			channels_count;
	struct gb_channel	*channels;
	bool			has_flash;
#ifdef V4L2_HAVE_FLASH
	struct v4l2_flash	*v4l2_flash;
#endif
};

struct gb_lights {
	struct gb_connection	*connection;
	u8			lights_count;
	struct gb_light		*lights;
	struct mutex		lights_lock;
};

static void gb_lights_channel_free(struct gb_channel *channel);

static struct gb_connection *get_conn_from_channel(struct gb_channel *channel)
{
	return channel->light->glights->connection;
}

static struct gb_connection *get_conn_from_light(struct gb_light *light)
{
	return light->glights->connection;
}

static bool is_channel_flash(struct gb_channel *channel)
{
	return !!(channel->mode & (GB_CHANNEL_MODE_FLASH | GB_CHANNEL_MODE_TORCH
				   | GB_CHANNEL_MODE_INDICATOR));
}

#ifdef LED_HAVE_FLASH
static struct gb_channel *get_channel_from_cdev(struct led_classdev *cdev)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(cdev);

	return container_of(fled_cdev, struct gb_channel, fled);
}

static struct led_classdev *get_channel_cdev(struct gb_channel *channel)
{
	return &channel->fled.led_cdev;
}

static struct gb_channel *get_channel_from_mode(struct gb_light *light,
						u32 mode)
{
	struct gb_channel *channel = NULL;
	int i;

	for (i = 0; i < light->channels_count; i++) {
		channel = &light->channels[i];
		if (channel && channel->mode == mode)
			break;
	}
	return channel;
}

static int __gb_lights_flash_intensity_set(struct gb_channel *channel,
					   u32 intensity)
{
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_set_flash_intensity_request req;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.intensity_uA = cpu_to_le32(intensity);

	return gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_FLASH_INTENSITY,
				 &req, sizeof(req), NULL, 0);
}

static int __gb_lights_flash_brightness_set(struct gb_channel *channel)
{
	u32 intensity;

	/* If the channel is flash we need to get the attached torch channel */
	if (channel->mode & GB_CHANNEL_MODE_FLASH)
		channel = get_channel_from_mode(channel->light,
						GB_CHANNEL_MODE_TORCH);

	/* For not flash we need to convert brightness to intensity */
	intensity = channel->intensity_uA.min +
			(channel->intensity_uA.step * channel->led->brightness);

	return __gb_lights_flash_intensity_set(channel, intensity);
}
#else /* LED_HAVE_FLASH */
static struct gb_channel *get_channel_from_cdev(struct led_classdev *cdev)
{
	return container_of(cdev, struct gb_channel, cled);
}

static struct led_classdev *get_channel_cdev(struct gb_channel *channel)
{
	return &channel->cled;
}

static int __gb_lights_flash_brightness_set(struct gb_channel *channel)
{
	return 0;
}
#endif /* !LED_HAVE_FLASH */

#ifdef LED_HAVE_GROUPS
static int gb_lights_color_set(struct gb_channel *channel, u32 color);
static int gb_lights_fade_set(struct gb_channel *channel);

#ifdef LED_HAVE_LOCK
static void led_lock(struct led_classdev *cdev)
{
	mutex_lock(&cdev->led_access);
}

static void led_unlock(struct led_classdev *cdev)
{
	mutex_unlock(&cdev->led_access);
}
#else
static void led_lock(struct led_classdev *cdev)
{
}

static void led_unlock(struct led_classdev *cdev)
{
}
#endif /* !LED_HAVE_LOCK */

#define gb_lights_fade_attr(__dir)					\
static ssize_t fade_##__dir##_show(struct device *dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
{									\
	struct led_classdev *cdev = dev_get_drvdata(dev);		\
	struct gb_channel *channel = get_channel_from_cdev(cdev);	\
									\
	return sprintf(buf, "%u\n", channel->fade_##__dir);		\
}									\
									\
static ssize_t fade_##__dir##_store(struct device *dev,			\
				    struct device_attribute *attr,	\
				    const char *buf, size_t size)	\
{									\
	struct led_classdev *cdev = dev_get_drvdata(dev);		\
	struct gb_channel *channel = get_channel_from_cdev(cdev);	\
	u8 fade;							\
	int ret;							\
									\
	led_lock(cdev);							\
	if (led_sysfs_is_disabled(cdev)) {				\
		ret = -EBUSY;						\
		goto unlock;						\
	}								\
									\
	ret = kstrtou8(buf, 0, &fade);					\
	if (ret < 0) {							\
		dev_err(dev, "could not parse fade value %d\n", ret);	\
		goto unlock;						\
	}								\
	if (channel->fade_##__dir == fade)				\
		goto unlock;						\
	channel->fade_##__dir = fade;					\
									\
	ret = gb_lights_fade_set(channel);				\
	if (ret < 0)							\
		goto unlock;						\
									\
	ret = size;							\
unlock:									\
	led_unlock(cdev);						\
	return ret;							\
}									\
static DEVICE_ATTR_RW(fade_##__dir)

gb_lights_fade_attr(in);
gb_lights_fade_attr(out);

static ssize_t color_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct gb_channel *channel = get_channel_from_cdev(cdev);

	return sprintf(buf, "0x%08x\n", channel->color);
}

static ssize_t color_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct gb_channel *channel = get_channel_from_cdev(cdev);
	u32 color;
	int ret;

	led_lock(cdev);
	if (led_sysfs_is_disabled(cdev)) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = kstrtou32(buf, 0, &color);
	if (ret < 0) {
		dev_err(dev, "could not parse color value %d\n", ret);
		goto unlock;
	}

	ret = gb_lights_color_set(channel, color);
	if (ret < 0)
		goto unlock;

	channel->color = color;
	ret = size;
unlock:
	led_unlock(cdev);
	return ret;
}
static DEVICE_ATTR_RW(color);

static int channel_attr_groups_set(struct gb_channel *channel,
				   struct led_classdev *cdev)
{
	int attr = 0;
	int size = 0;

	if (channel->flags & GB_LIGHT_CHANNEL_MULTICOLOR)
		size++;
	if (channel->flags & GB_LIGHT_CHANNEL_FADER)
		size++;

	if (!size)
		return 0;

	/* Set attributes based in the channel flags */
	channel->attrs = kcalloc(size, sizeof(**channel->attrs), GFP_KERNEL);
	if (!channel->attrs)
		return -ENOMEM;
	channel->attr_group = kcalloc(1, sizeof(*channel->attr_group),
				      GFP_KERNEL);
	if (!channel->attr_group)
		return -ENOMEM;
	channel->attr_groups = kcalloc(2, sizeof(*channel->attr_groups),
				       GFP_KERNEL);
	if (!channel->attr_groups)
		return -ENOMEM;

	if (channel->flags & GB_LIGHT_CHANNEL_MULTICOLOR)
		channel->attrs[attr++] = &dev_attr_color.attr;
	if (channel->flags & GB_LIGHT_CHANNEL_FADER) {
		channel->attrs[attr++] = &dev_attr_fade_in.attr;
		channel->attrs[attr++] = &dev_attr_fade_out.attr;
	}

	channel->attr_group->attrs = channel->attrs;

	channel->attr_groups[0] = channel->attr_group;

	cdev->groups = channel->attr_groups;

	return 0;
}

static int gb_lights_fade_set(struct gb_channel *channel)
{
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_set_fade_request req;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.fade_in = channel->fade_in;
	req.fade_out = channel->fade_out;
	return gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_FADE,
				 &req, sizeof(req), NULL, 0);
}

static int gb_lights_color_set(struct gb_channel *channel, u32 color)
{
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_set_color_request req;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.color = cpu_to_le32(color);
	return gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_COLOR,
				 &req, sizeof(req), NULL, 0);
}
#else /* LED_HAVE_GROUPS */
static int channel_attr_groups_set(struct gb_channel *channel,
				   struct led_classdev *cdev)
{
	return 0;
}
#endif /* !LED_HAVE_GROUPS */

static int __gb_lights_led_brightness_set(struct gb_channel *channel)
{
	struct gb_lights_set_brightness_request req;
	struct gb_connection *connection = get_conn_from_channel(channel);

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.brightness = (u8)channel->led->brightness;

	return gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_BRIGHTNESS,
				 &req, sizeof(req), NULL, 0);
}

static int __gb_lights_brightness_set(struct gb_channel *channel)
{
	int ret;

	if (channel->releasing)
		return 0;

	if (is_channel_flash(channel))
		ret = __gb_lights_flash_brightness_set(channel);
	else
		ret = __gb_lights_led_brightness_set(channel);

	return ret;
}

static void gb_brightness_set_work(struct work_struct *work)
{
	struct gb_channel *channel = container_of(work, struct gb_channel,
						  work_brightness_set);

	__gb_lights_brightness_set(channel);
}

#ifdef LED_HAVE_SET_SYNC
static int gb_brightness_set_sync(struct led_classdev *cdev,
				  enum led_brightness value)
{
	struct gb_channel *channel = get_channel_from_cdev(cdev);

	channel->led->brightness = value;

	return __gb_lights_brightness_set(channel);
}
#endif

static void gb_brightness_set(struct led_classdev *cdev,
			      enum led_brightness value)
{
	struct gb_channel *channel = get_channel_from_cdev(cdev);

	if (channel->releasing)
		return;

	cdev->brightness = value;
	schedule_work(&channel->work_brightness_set);
}

static enum led_brightness gb_brightness_get(struct led_classdev *cdev)

{
	struct gb_channel *channel = get_channel_from_cdev(cdev);

	return channel->led->brightness;
}

static int gb_blink_set(struct led_classdev *cdev, unsigned long *delay_on,
			unsigned long *delay_off)
{
	struct gb_channel *channel = get_channel_from_cdev(cdev);
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_blink_request req;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.time_on_ms = cpu_to_le16(*delay_on);
	req.time_off_ms = cpu_to_le16(*delay_off);

	return gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_BLINK, &req,
				 sizeof(req), NULL, 0);
}

static void gb_lights_led_operations_set(struct gb_channel *channel,
					 struct led_classdev *cdev)
{
	cdev->brightness_set = gb_brightness_set;
	cdev->brightness_get = gb_brightness_get;
#ifdef LED_HAVE_SET_SYNC
	cdev->brightness_set_sync = gb_brightness_set_sync;
#endif
	INIT_WORK(&channel->work_brightness_set, gb_brightness_set_work);

	if (channel->flags & GB_LIGHT_CHANNEL_BLINK)
		cdev->blink_set = gb_blink_set;
}

#ifdef V4L2_HAVE_FLASH
/* V4L2 specific helpers */
static const struct v4l2_flash_ops v4l2_flash_ops;

static void __gb_lights_channel_v4l2_config(struct led_flash_setting *channel_s,
					    struct led_flash_setting *v4l2_s)
{
	v4l2_s->min = channel_s->min;
	v4l2_s->max = channel_s->max;
	v4l2_s->step = channel_s->step;
	/* For v4l2 val is the default value */
	v4l2_s->val = channel_s->max;
}

static int gb_lights_light_v4l2_register(struct gb_light *light)
{
	struct gb_connection *connection = get_conn_from_light(light);
	struct device *dev = &connection->bundle->dev;
	struct v4l2_flash_config *sd_cfg;
	struct led_classdev_flash *fled;
	struct led_classdev_flash *iled = NULL;
	struct gb_channel *channel_torch, *channel_ind, *channel_flash;
	int ret = 0;

	sd_cfg = kcalloc(1, sizeof(*sd_cfg), GFP_KERNEL);
	if (!sd_cfg)
		return -ENOMEM;

	channel_torch = get_channel_from_mode(light, GB_CHANNEL_MODE_TORCH);
	if (channel_torch)
		__gb_lights_channel_v4l2_config(&channel_torch->intensity_uA,
						&sd_cfg->torch_intensity);

	channel_ind = get_channel_from_mode(light, GB_CHANNEL_MODE_INDICATOR);
	if (channel_ind) {
		__gb_lights_channel_v4l2_config(&channel_ind->intensity_uA,
						&sd_cfg->indicator_intensity);
		iled = &channel_ind->fled;
	}

	channel_flash = get_channel_from_mode(light, GB_CHANNEL_MODE_FLASH);
	WARN_ON(!channel_flash);

	fled = &channel_flash->fled;

	snprintf(sd_cfg->dev_name, sizeof(sd_cfg->dev_name), "%s", light->name);

	/* Set the possible values to faults, in our case all faults */
	sd_cfg->flash_faults = LED_FAULT_OVER_VOLTAGE | LED_FAULT_TIMEOUT |
		LED_FAULT_OVER_TEMPERATURE | LED_FAULT_SHORT_CIRCUIT |
		LED_FAULT_OVER_CURRENT | LED_FAULT_INDICATOR |
		LED_FAULT_UNDER_VOLTAGE | LED_FAULT_INPUT_VOLTAGE |
		LED_FAULT_LED_OVER_TEMPERATURE;

	light->v4l2_flash = v4l2_flash_init(dev, NULL, fled, iled,
					    &v4l2_flash_ops, sd_cfg);
	if (IS_ERR_OR_NULL(light->v4l2_flash)) {
		ret = PTR_ERR(light->v4l2_flash);
		goto out_free;
	}

	return ret;

out_free:
	kfree(sd_cfg);
	return ret;
}

static void gb_lights_light_v4l2_unregister(struct gb_light *light)
{
	v4l2_flash_release(light->v4l2_flash);
}
#else
static int gb_lights_light_v4l2_register(struct gb_light *light)
{
	struct gb_connection *connection = get_conn_from_light(light);

	dev_err(&connection->bundle->dev, "no support for v4l2 subdevices\n");
	return 0;
}

static void gb_lights_light_v4l2_unregister(struct gb_light *light)
{
}
#endif

#ifdef LED_HAVE_FLASH
/* Flash specific operations */
static int gb_lights_flash_intensity_set(struct led_classdev_flash *fcdev,
					 u32 brightness)
{
	struct gb_channel *channel = container_of(fcdev, struct gb_channel,
						  fled);
	int ret;

	ret = __gb_lights_flash_intensity_set(channel, brightness);
	if (ret < 0)
		return ret;

	fcdev->brightness.val = brightness;

	return 0;
}

static int gb_lights_flash_intensity_get(struct led_classdev_flash *fcdev,
					 u32 *brightness)
{
	*brightness = fcdev->brightness.val;

	return 0;
}

static int gb_lights_flash_strobe_set(struct led_classdev_flash *fcdev,
				      bool state)
{
	struct gb_channel *channel = container_of(fcdev, struct gb_channel,
						  fled);
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_set_flash_strobe_request req;
	int ret;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.state = state ? 1 : 0;

	ret = gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_FLASH_STROBE,
				&req, sizeof(req), NULL, 0);
	if (ret < 0)
		return ret;
	channel->strobe_state = state;

	return 0;
}

static int gb_lights_flash_strobe_get(struct led_classdev_flash *fcdev,
				      bool *state)
{
	struct gb_channel *channel = container_of(fcdev, struct gb_channel,
						  fled);

	*state = channel->strobe_state;
	return 0;
}

static int gb_lights_flash_timeout_set(struct led_classdev_flash *fcdev,
				       u32 timeout)
{
	struct gb_channel *channel = container_of(fcdev, struct gb_channel,
						  fled);
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_set_flash_timeout_request req;
	int ret;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;
	req.timeout_us = cpu_to_le32(timeout);

	ret = gb_operation_sync(connection, GB_LIGHTS_TYPE_SET_FLASH_TIMEOUT,
				&req, sizeof(req), NULL, 0);
	if (ret < 0)
		return ret;
	fcdev->timeout.val = timeout;

	return 0;
}

static int gb_lights_flash_fault_get(struct led_classdev_flash *fcdev,
				     u32 *fault)
{
	struct gb_channel *channel = container_of(fcdev, struct gb_channel,
						  fled);
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_get_flash_fault_request req;
	struct gb_lights_get_flash_fault_response resp;
	int ret;

	if (channel->releasing)
		return -ESHUTDOWN;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;

	ret = gb_operation_sync(connection, GB_LIGHTS_TYPE_GET_FLASH_FAULT,
				&req, sizeof(req), &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	*fault = le32_to_cpu(resp.fault);

	return 0;
}

static const struct led_flash_ops gb_lights_flash_ops = {
	.flash_brightness_set	= gb_lights_flash_intensity_set,
	.flash_brightness_get	= gb_lights_flash_intensity_get,
	.strobe_set		= gb_lights_flash_strobe_set,
	.strobe_get		= gb_lights_flash_strobe_get,
	.timeout_set		= gb_lights_flash_timeout_set,
	.fault_get		= gb_lights_flash_fault_get,
};

static int __gb_lights_channel_torch_attach(struct gb_channel *channel,
					    struct gb_channel *channel_torch)
{
	char *name;

	/* we can only attach torch to a flash channel */
	if (!(channel->mode & GB_CHANNEL_MODE_FLASH))
		return 0;

	/* Move torch brightness to the destination */
	channel->led->max_brightness = channel_torch->led->max_brightness;

	/* append mode name to flash name */
	name = kasprintf(GFP_KERNEL, "%s_%s", channel->led->name,
			 channel_torch->mode_name);
	if (!name)
		return -ENOMEM;
	kfree(channel->led->name);
	channel->led->name = name;

	/* free original torch channel resources */
	gb_lights_channel_free(channel_torch);

	channel_torch->led = channel->led;

	return 0;
}

static int __gb_lights_flash_led_register(struct gb_channel *channel)
{
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct led_classdev_flash *fled = &channel->fled;
	struct led_flash_setting *fset;
	struct gb_channel *channel_torch;
	int ret;

	fled->ops = &gb_lights_flash_ops;

	fled->led_cdev.flags |= LED_DEV_CAP_FLASH;

	fset = &fled->brightness;
	fset->min = channel->intensity_uA.min;
	fset->max = channel->intensity_uA.max;
	fset->step = channel->intensity_uA.step;

	/* Only the flash mode have the timeout constraints settings */
	if (channel->mode & GB_CHANNEL_MODE_FLASH) {
		fset = &fled->timeout;
		fset->min = channel->timeout_us.min;
		fset->max = channel->timeout_us.max;
		fset->step = channel->timeout_us.step;
	}

	/*
	 * If light have torch mode channel, this channel will be the led
	 * classdev of the registered above flash classdev
	 */
	channel_torch = get_channel_from_mode(channel->light,
					      GB_CHANNEL_MODE_TORCH);
	if (channel_torch) {
		ret = __gb_lights_channel_torch_attach(channel, channel_torch);
		if (ret < 0)
			goto fail;
	}

	ret = led_classdev_flash_register(&connection->bundle->dev, fled);
	if (ret < 0)
		goto fail;

	channel->is_registered = true;
	return 0;
fail:
	channel->led = NULL;
	return ret;
}

static void __gb_lights_flash_led_unregister(struct gb_channel *channel)
{
	if (!channel->is_registered)
		return;

	led_classdev_flash_unregister(&channel->fled);
}

static int gb_lights_channel_flash_config(struct gb_channel *channel)
{
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct gb_lights_get_channel_flash_config_request req;
	struct gb_lights_get_channel_flash_config_response conf;
	struct led_flash_setting *fset;
	int ret;

	req.light_id = channel->light->id;
	req.channel_id = channel->id;

	ret = gb_operation_sync(connection,
				GB_LIGHTS_TYPE_GET_CHANNEL_FLASH_CONFIG,
				&req, sizeof(req), &conf, sizeof(conf));
	if (ret < 0)
		return ret;

	/*
	 * Intensity constraints for flash related modes: flash, torch,
	 * indicator.  They will be needed for v4l2 registration.
	 */
	fset = &channel->intensity_uA;
	fset->min = le32_to_cpu(conf.intensity_min_uA);
	fset->max = le32_to_cpu(conf.intensity_max_uA);
	fset->step = le32_to_cpu(conf.intensity_step_uA);

	/*
	 * On flash type, max brightness is set as the number of intensity steps
	 * available.
	 */
	channel->led->max_brightness = (fset->max - fset->min) / fset->step;

	/* Only the flash mode have the timeout constraints settings */
	if (channel->mode & GB_CHANNEL_MODE_FLASH) {
		fset = &channel->timeout_us;
		fset->min = le32_to_cpu(conf.timeout_min_us);
		fset->max = le32_to_cpu(conf.timeout_max_us);
		fset->step = le32_to_cpu(conf.timeout_step_us);
	}

	return 0;
}
#else
static int gb_lights_channel_flash_config(struct gb_channel *channel)
{
	struct gb_connection *connection = get_conn_from_channel(channel);

	dev_err(&connection->bundle->dev, "no support for flash devices\n");
	return 0;
}

static int __gb_lights_flash_led_register(struct gb_channel *channel)
{
	return 0;
}

static void __gb_lights_flash_led_unregister(struct gb_channel *channel)
{
}

#endif /* LED_HAVE_FLASH */

static int __gb_lights_led_register(struct gb_channel *channel)
{
	struct gb_connection *connection = get_conn_from_channel(channel);
	struct led_classdev *cdev = get_channel_cdev(channel);
	int ret;

	ret = led_classdev_register(&connection->bundle->dev, cdev);
	if (ret < 0)
		channel->led = NULL;
	else
		channel->is_registered = true;
	return ret;
}

static int gb_lights_channel_register(struct gb_channel *channel)
{
	/* Normal LED channel, just register in led classdev and we are done */
	if (!is_channel_flash(channel))
		return __gb_lights_led_register(channel);

	/*
	 * Flash Type need more work, register flash classdev, indicator as
	 * flash classdev, torch will be led classdev of the flash classdev.
	 */
	if (!(channel->mode & GB_CHANNEL_MODE_TORCH))
		return __gb_lights_flash_led_register(channel);

	return 0;
}

static void __gb_lights_led_unregister(struct gb_channel *channel)
{
	struct led_classdev *cdev = get_channel_cdev(channel);

	if (!channel->is_registered)
		return;

	led_classdev_unregister(cdev);
	channel->led = NULL;
}

static void gb_lights_channel_unregister(struct gb_channel *channel)
{
	/* The same as register, handle channels differently */
	if (!is_channel_flash(channel)) {
		__gb_lights_led_unregister(channel);
		return;
	}

	if (channel->mode & GB_CHANNEL_MODE_TORCH)
		__gb_lights_led_unregister(channel);
	else
		__gb_lights_flash_led_unregister(channel);
}

static int gb_lights_channel_config(struct gb_light *light,
				    struct gb_channel *channel)
{
	struct gb_lights_get_channel_config_response conf;
	struct gb_lights_get_channel_config_request req;
	struct gb_connection *connection = get_conn_from_light(light);
	struct led_classdev *cdev = get_channel_cdev(channel);
	char *name;
	int ret;

	req.light_id = light->id;
	req.channel_id = channel->id;

	ret = gb_operation_sync(connection, GB_LIGHTS_TYPE_GET_CHANNEL_CONFIG,
				&req, sizeof(req), &conf, sizeof(conf));
	if (ret < 0)
		return ret;

	channel->light = light;
	channel->mode = le32_to_cpu(conf.mode);
	channel->flags = le32_to_cpu(conf.flags);
	channel->color = le32_to_cpu(conf.color);
	channel->color_name = kstrndup(conf.color_name, NAMES_MAX, GFP_KERNEL);
	if (!channel->color_name)
		return -ENOMEM;
	channel->mode_name = kstrndup(conf.mode_name, NAMES_MAX, GFP_KERNEL);
	if (!channel->mode_name)
		return -ENOMEM;

	channel->led = cdev;

	name = kasprintf(GFP_KERNEL, "%s:%s:%s", light->name,
			 channel->color_name, channel->mode_name);
	if (!name)
		return -ENOMEM;

	cdev->name = name;

	cdev->max_brightness = conf.max_brightness;

	ret = channel_attr_groups_set(channel, cdev);
	if (ret < 0)
		return ret;

	gb_lights_led_operations_set(channel, cdev);

	/*
	 * If it is not a flash related channel (flash, torch or indicator) we
	 * are done here. If not, continue and fetch flash related
	 * configurations.
	 */
	if (!is_channel_flash(channel))
		return ret;

	light->has_flash = true;

	ret = gb_lights_channel_flash_config(channel);
	if (ret < 0)
		return ret;

	return ret;
}

static int gb_lights_light_config(struct gb_lights *glights, u8 id)
{
	struct gb_light *light = &glights->lights[id];
	struct gb_lights_get_light_config_request req;
	struct gb_lights_get_light_config_response conf;
	int ret;
	int i;

	light->glights = glights;
	light->id = id;

	req.id = id;

	ret = gb_operation_sync(glights->connection,
				GB_LIGHTS_TYPE_GET_LIGHT_CONFIG,
				&req, sizeof(req), &conf, sizeof(conf));
	if (ret < 0)
		return ret;

	if (!conf.channel_count)
		return -EINVAL;
	if (!strlen(conf.name))
		return -EINVAL;

	light->channels_count = conf.channel_count;
	light->name = kstrndup(conf.name, NAMES_MAX, GFP_KERNEL);

	light->channels = kzalloc(light->channels_count *
				  sizeof(struct gb_channel), GFP_KERNEL);
	if (!light->channels)
		return -ENOMEM;

	/* First we collect all the configurations for all channels */
	for (i = 0; i < light->channels_count; i++) {
		light->channels[i].id = i;
		ret = gb_lights_channel_config(light, &light->channels[i]);
		if (ret < 0)
			return ret;
	}

	/*
	 * Then, if everything went ok in getting configurations, we register
	 * the classdev, flash classdev and v4l2 subsystem, if a flash device is
	 * found.
	 */
	for (i = 0; i < light->channels_count; i++) {
		ret = gb_lights_channel_register(&light->channels[i]);
		if (ret < 0)
			return ret;
	}

	if (light->has_flash) {
		ret = gb_lights_light_v4l2_register(light);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void gb_lights_channel_free(struct gb_channel *channel)
{
	if (&channel->work_brightness_set)
		flush_work(&channel->work_brightness_set);
	kfree(channel->attrs);
	kfree(channel->attr_group);
	kfree(channel->attr_groups);
	kfree(channel->color_name);
	kfree(channel->mode_name);
}

static void gb_lights_channel_release(struct gb_channel *channel)
{
	if (!channel)
		return;

	channel->releasing = true;

	gb_lights_channel_unregister(channel);

	gb_lights_channel_free(channel);
}

static void gb_lights_light_release(struct gb_light *light)
{
	int i;
	int count;

	if (!light)
		return;

	count = light->channels_count;

	if (light->has_flash)
		gb_lights_light_v4l2_unregister(light);

	for (i = 0; i < count; i++) {
		gb_lights_channel_release(&light->channels[i]);
		light->channels_count--;
	}
	kfree(light->channels);
	kfree(light->name);
}

static void gb_lights_release(struct gb_lights *glights)
{
	int i;

	if (!glights)
		return;

	mutex_lock(&glights->lights_lock);
	if (!glights->lights)
		goto free_glights;

	for (i = 0; i < glights->lights_count; i++)
		gb_lights_light_release(&glights->lights[i]);

	kfree(glights->lights);

free_glights:
	mutex_unlock(&glights->lights_lock);
	mutex_destroy(&glights->lights_lock);
	kfree(glights);
}

static int gb_lights_get_count(struct gb_lights *glights)
{
	struct gb_lights_get_lights_response resp;
	int ret;

	ret = gb_operation_sync(glights->connection, GB_LIGHTS_TYPE_GET_LIGHTS,
				NULL, 0, &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	if (!resp.lights_count)
		return -EINVAL;

	glights->lights_count = resp.lights_count;

	return 0;
}

static int gb_lights_setup(struct gb_lights *glights)
{
	struct gb_connection *connection = glights->connection;
	int ret;
	int i;

	mutex_lock(&glights->lights_lock);
	ret = gb_lights_get_count(glights);
	if (ret < 0)
		goto out;

	glights->lights = kzalloc(glights->lights_count *
				  sizeof(struct gb_light), GFP_KERNEL);
	if (!glights->lights) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < glights->lights_count; i++) {
		ret = gb_lights_light_config(glights, i);
		if (ret < 0) {
			dev_err(&connection->bundle->dev,
				"Fail to configure lights device\n");
			goto out;
		}
	}

out:
	mutex_unlock(&glights->lights_lock);
	return ret;
}

static int gb_lights_event_recv(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct device *dev = &connection->bundle->dev;
	struct gb_lights *glights = connection->private;
	struct gb_message *request;
	struct gb_lights_event_request *payload;
	int ret =  0;
	u8 light_id;
	u8 event;

	if (type != GB_LIGHTS_TYPE_EVENT) {
		dev_err(dev, "Unsupported unsolicited event: %u\n", type);
		return -EINVAL;
	}

	request = op->request;

	if (request->payload_size < sizeof(*payload)) {
		dev_err(dev, "Wrong event size received (%zu < %zu)\n",
			request->payload_size, sizeof(*payload));
		return -EINVAL;
	}

	payload = request->payload;
	light_id = payload->light_id;

	if (light_id >= glights->lights_count || !&glights->lights[light_id]) {
		dev_err(dev, "Event received for unconfigured light id: %d\n",
			light_id);
		return -EINVAL;
	}

	event = payload->event;

	if (event & GB_LIGHTS_LIGHT_CONFIG) {
		mutex_lock(&glights->lights_lock);
		gb_lights_light_release(&glights->lights[light_id]);
		ret = gb_lights_light_config(glights, light_id);
		if (ret < 0)
			gb_lights_light_release(&glights->lights[light_id]);
		mutex_unlock(&glights->lights_lock);
	}

	return ret;
}

static int gb_lights_connection_init(struct gb_connection *connection)
{
	struct gb_lights *glights;
	int ret;

	glights = kzalloc(sizeof(*glights), GFP_KERNEL);
	if (!glights)
		return -ENOMEM;

	glights->connection = connection;
	connection->private = glights;

	mutex_init(&glights->lights_lock);

	/*
	 * Setup all the lights devices over this connection, if anything goes
	 * wrong tear down all lights
	 */
	ret = gb_lights_setup(glights);
	if (ret < 0)
		goto out;

	return 0;

out:
	gb_lights_release(glights);
	return ret;
}

static void gb_lights_connection_exit(struct gb_connection *connection)
{
	struct gb_lights *glights = connection->private;

	gb_lights_release(glights);
}

static struct gb_protocol lights_protocol = {
	.name			= "lights",
	.id			= GREYBUS_PROTOCOL_LIGHTS,
	.major			= GB_LIGHTS_VERSION_MAJOR,
	.minor			= GB_LIGHTS_VERSION_MINOR,
	.connection_init	= gb_lights_connection_init,
	.connection_exit	= gb_lights_connection_exit,
	.request_recv		= gb_lights_event_recv,
};

gb_protocol_driver(&lights_protocol);

MODULE_LICENSE("GPL v2");
