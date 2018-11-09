// SPDX-License-Identifier: GPL-2.0
/*
 * ov7725 driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define REG_CHIP_ID_H			0x0a
#define REG_CHIP_ID_L			0x0b
#define CHIP_ID_H			0x77
#define CHIP_ID_L			0x21

#define REG_NULL			0xFF

#define ov7725_XVCLK_FREQ		24000000

static const char * const ov7725_supply_names[] = {
	"avdd",
	"dovdd",
	"dvdd",
};

#define ov7725_NUM_SUPPLIES ARRAY_SIZE(ov7725_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct ov7725_mode {
	u32 width;
	u32 height;
	const struct regval *reg_list;
};

struct ov7725 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[ov7725_NUM_SUPPLIES];

	bool			streaming;
	struct mutex		mutex; /* lock to serialize v4l2 callback */
	struct v4l2_subdev	subdev;
	struct media_pad	pad;

	const struct ov7725_mode *cur_mode;
};

#define to_ov7725(sd) container_of(sd, struct ov7725, subdev)

/* 30fps at 24MHz input clock,4x maximum gain */
static struct regval ov7725_640x480_30fps[] = {
	{0x12, 0x80},
	{0x3d, 0x03},
	{0x17, 0x25}, /* Raw: 0x17,0x22 */
	{0x18, 0xa4}, /* Raw: 0x18,0xa4 */
	{0x19, 0x06}, /* Raw: 0x19,0x07 */
	{0x1a, 0xf0},
	{0x32, 0x60}, /* Raw: 0x32,0x00 */
	{0x29, 0xa0},
	{0x2c, 0xf0},
	{0x2a, 0x00},
	{0x11, 0x01},
	{0x42, 0x7f},
	{0x4d, 0x00},
	{0x63, 0xe0},
	{0x64, 0xff},
	{0x65, 0x20},
	{0x66, 0x00},
	{0x67, 0x48},
	{0x13, 0xf0},
	{0x0d, 0x41},
	{0x0f, 0xc5},

	{0x14, 0x17}, /* 0x14,0x11 */
	{0x22, 0x3f},
	{0x23, 0x07},
	{0x24, 0x44},
	{0x25, 0x3c},
	{0x26, 0xa1},
	{0x2b, 0x00},
	{0x6b, 0xaa},
	{0x13, 0xff},
	{0x90, 0x05},
	{0x91, 0x01},
	{0x92, 0x03},
	{0x93, 0x00},
	{0x94, 0x40},
	{0x95, 0x40},
	{0x96, 0x00},
	{0x97, 0x11},
	{0x98, 0x2f},
	{0x99, 0x40},
	{0x9a, 0x9e},
	{0x9b, 0x08},
	{0x9c, 0x20},
	{0x9e, 0x81},
	{0xa6, 0x06},
	{0x7e, 0x0c},
	{0x7f, 0x16},
	{0x80, 0x2a},
	{0x81, 0x4e},
	{0x82, 0x61},
	{0x83, 0x6f},
	{0x84, 0x7b},
	{0x85, 0x86},
	{0x86, 0x8e},
	{0x87, 0x97},
	{0x88, 0xa4},
	{0x89, 0xaf},
	{0x8a, 0xc5},
	{0x8b, 0xd7},
	{0x8c, 0xe8},
	{0x8d, 0x20},
	{0x33, 0x00},
	{0x22, 0x99},
	{0x23, 0x03},
	{0x4a, 0x00},
	{0x49, 0x13},
	{0x47, 0x08},
	{0x4b, 0x14},
	{0x4c, 0x17},
	{0x46, 0x05},
	{0x0e, 0x65},
	{0x0c, 0x00},
	{REG_NULL, 0x0},
};

static struct regval ov7725_1600x1200_7fps[] = {
	{REG_NULL, 0x0},
};

static const struct ov7725_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.reg_list = ov7725_640x480_30fps,
	},
	{
		.width = 1600,
		.height = 1200,
		.reg_list = ov7725_1600x1200_7fps,
	},
};

static int ov7725_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "write reg error: %d\n", ret);

	return ret;
}

static int ov7725_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov7725_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static inline u8 ov7725_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int ov7725_get_reso_dist(const struct ov7725_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct ov7725_mode *
ov7725_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov7725_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov7725_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct ov7725 *ov7725 = to_ov7725(sd);
	const struct ov7725_mode *mode;

	mutex_lock(&ov7725->mutex);

	mode = ov7725_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_JPEG;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov7725->mutex);
		return -ENOTTY;
#endif
	} else {
		ov7725->cur_mode = mode;
	}

	mutex_unlock(&ov7725->mutex);

	return 0;
}

static int ov7725_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct ov7725 *ov7725 = to_ov7725(sd);
	const struct ov7725_mode *mode = ov7725->cur_mode;

	mutex_lock(&ov7725->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov7725->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		fmt->format.field = V4L2_FIELD_NONE;
		fmt->format.colorspace = V4L2_COLORSPACE_JPEG;
	}
	mutex_unlock(&ov7725->mutex);

	return 0;
}

static int ov7725_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;

	return 0;
}

static int ov7725_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	u32 index = fse->index;

	if (index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fse->code = MEDIA_BUS_FMT_UYVY8_2X8;

	fse->min_width  = supported_modes[index].width;
	fse->max_width  = supported_modes[index].width;
	fse->max_height = supported_modes[index].height;
	fse->min_height = supported_modes[index].height;

	return 0;
}

static int __ov7725_power_on(struct ov7725 *ov7725)
{
	int ret;
	struct device *dev = &ov7725->client->dev;

	if (!IS_ERR(ov7725->reset_gpio))
		gpiod_set_value_cansleep(ov7725->reset_gpio, 0);

	ret = regulator_bulk_enable(ov7725_NUM_SUPPLIES, ov7725->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		return ret;
	}

	if (!IS_ERR(ov7725->xvclk)) {
		ret = clk_prepare_enable(ov7725->xvclk);
		if (ret < 0) {
			dev_err(dev, "Failed to enable xvclk\n");
			return ret;
		}
	}

	if (!IS_ERR(ov7725->pwdn_gpio))
		gpiod_set_value_cansleep(ov7725->pwdn_gpio, 0);

	if (!IS_ERR(ov7725->reset_gpio))
		gpiod_set_value_cansleep(ov7725->reset_gpio, 1);

	return 0;
}

static void __ov7725_power_off(struct ov7725 *ov7725)
{
	if (!IS_ERR(ov7725->reset_gpio))
		gpiod_set_value_cansleep(ov7725->reset_gpio, 0);
	if (!IS_ERR(ov7725->pwdn_gpio))
		gpiod_set_value_cansleep(ov7725->pwdn_gpio, 1);

	if (!IS_ERR(ov7725->xvclk))
		clk_disable_unprepare(ov7725->xvclk);

	regulator_bulk_disable(ov7725_NUM_SUPPLIES, ov7725->supplies);
}

static int ov7725_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov7725 *ov7725 = to_ov7725(sd);
	struct i2c_client *client = ov7725->client;
	int ret = 0;

	mutex_lock(&ov7725->mutex);

	on = !!on;
	if (on == ov7725->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&ov7725->client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov7725_write_array(ov7725->client,
					  ov7725->cur_mode->reg_list);
		if (ret) {
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}

	} else {
		pm_runtime_put(&client->dev);
	}

	ov7725->streaming = on;

unlock_and_return:
	mutex_unlock(&ov7725->mutex);

	return ret;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov7725_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov7725 *ov7725 = to_ov7725(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov7725_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov7725->mutex);

	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	try_fmt->field = V4L2_FIELD_NONE;
	try_fmt->colorspace = V4L2_COLORSPACE_JPEG;

	mutex_unlock(&ov7725->mutex);

	return 0;
}
#endif

static int ov7725_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7725 *ov7725 = to_ov7725(sd);

	return __ov7725_power_on(ov7725);
}

static int ov7725_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7725 *ov7725 = to_ov7725(sd);

	__ov7725_power_off(ov7725);

	return 0;
}

static const struct dev_pm_ops ov7725_pm_ops = {
	SET_RUNTIME_PM_OPS(ov7725_runtime_suspend,
			   ov7725_runtime_resume, NULL)
};

static const struct v4l2_subdev_video_ops ov7725_video_ops = {
	.s_stream = ov7725_s_stream,
};

static const struct v4l2_subdev_pad_ops ov7725_pad_ops = {
	.enum_mbus_code = ov7725_enum_mbus_code,
	.enum_frame_size = ov7725_enum_frame_sizes,
	.get_fmt = ov7725_get_fmt,
	.set_fmt = ov7725_set_fmt,
};

static const struct v4l2_subdev_ops ov7725_subdev_ops = {
	.video	= &ov7725_video_ops,
	.pad	= &ov7725_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov7725_internal_ops = {
	.open = ov7725_open,
};
#endif

static int ov7725_check_sensor_id(struct ov7725 *ov7725,
				  struct i2c_client *client)
{
	struct device *dev = &ov7725->client->dev;
	u8 id_h = 0, id_l = 0;

	id_h = ov7725_read_reg(client, REG_CHIP_ID_H);
	id_l = ov7725_read_reg(client, REG_CHIP_ID_L);
	if (id_h != CHIP_ID_H && id_l != CHIP_ID_L) {
		dev_err(dev, "Wrong camera sensor id(0x%02x%02x)\n",
			id_h, id_l);
		return -EINVAL;
	}

	dev_info(dev, "Detected ov7725 (0x%02x%02x) sensor\n",
		CHIP_ID_H, CHIP_ID_L);

	return 0;
}

static int ov7725_configure_regulators(struct ov7725 *ov7725)
{
	u32 i;

	for (i = 0; i < ov7725_NUM_SUPPLIES; i++)
		ov7725->supplies[i].supply = ov7725_supply_names[i];

	return devm_regulator_bulk_get(&ov7725->client->dev,
				       ov7725_NUM_SUPPLIES,
				       ov7725->supplies);
}

static int ov7725_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ov7725 *ov7725;
	int ret;

	ov7725 = devm_kzalloc(dev, sizeof(*ov7725), GFP_KERNEL);
	if (!ov7725)
		return -ENOMEM;

	ov7725->client = client;
	ov7725->cur_mode = &supported_modes[0];

	ov7725->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov7725->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(ov7725->xvclk, ov7725_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(ov7725->xvclk) != ov7725_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ov7725->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov7725->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov7725->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov7725->pwdn_gpio))
		dev_warn(dev, "Failed to get ov7725-gpios\n");

	ret = ov7725_configure_regulators(ov7725);
	if (ret) {
		dev_warn(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov7725->mutex);
	v4l2_i2c_subdev_init(&ov7725->subdev, client, &ov7725_subdev_ops);

	ret = __ov7725_power_on(ov7725);
	if (ret)
		goto err_destroy_mutex;

	ret = ov7725_check_sensor_id(ov7725, client);
	if (ret)
		goto err_power_off;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	ov7725->subdev.internal_ops = &ov7725_internal_ops;
	ov7725->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov7725->pad.flags = MEDIA_PAD_FL_SOURCE;
	ov7725->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&ov7725->subdev.entity, 1, &ov7725->pad, 0);
	if (ret < 0)
		goto err_power_off;
#endif

	ret = v4l2_async_register_subdev(&ov7725->subdev);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&ov7725->subdev.entity);
#endif
err_power_off:
	__ov7725_power_off(ov7725);
err_destroy_mutex:
	mutex_destroy(&ov7725->mutex);

	return ret;
}

static int ov7725_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7725 *ov7725 = to_ov7725(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&ov7725->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov7725_power_off(ov7725);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov7725_of_match[] = {
	{ .compatible = "ovti,ov7725" },
	{},
};
MODULE_DEVICE_TABLE(of, ov7725_of_match);
#endif

static const struct i2c_device_id ov7725_match_id[] = {
	{"ovti,ov7251", 0},
	{},
};

static struct i2c_driver ov7725_i2c_driver = {
	.driver = {
		.name = "ov7725",
		.pm = &ov7725_pm_ops,
		.of_match_table = of_match_ptr(ov7725_of_match),
	},
	.probe		= ov7725_probe,
	.remove		= ov7725_remove,
	.id_table	= ov7725_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov7725_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov7725_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov7725 sensor driver");
MODULE_LICENSE("GPL v2");