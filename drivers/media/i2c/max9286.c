// SPDX-License-Identifier: GPL-2.0+
/*
 * Maxim MAX9286 GMSL Deserializer Driver
 *
 * Copyright (C) 2017-2019 Jacopo Mondi
 * Copyright (C) 2017-2019 Kieran Bingham
 * Copyright (C) 2017-2019 Laurent Pinchart
 * Copyright (C) 2017-2019 Niklas Söderlund
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2015 Cogent Embedded, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

/* Register 0x00 */
#define MAX9286_MSTLINKSEL_AUTO		(7 << 5)
#define MAX9286_MSTLINKSEL(n)		((n) << 5)
#define MAX9286_EN_VS_GEN		BIT(4)
#define MAX9286_LINKEN(n)		(1 << (n))
/* Register 0x01 */
#define MAX9286_FSYNCMODE_ECU		(3 << 6)
#define MAX9286_FSYNCMODE_EXT		(2 << 6)
#define MAX9286_FSYNCMODE_INT_OUT	(1 << 6)
#define MAX9286_FSYNCMODE_INT_HIZ	(0 << 6)
#define MAX9286_GPIEN			BIT(5)
#define MAX9286_ENLMO_RSTFSYNC		BIT(2)
#define MAX9286_FSYNCMETH_AUTO		(2 << 0)
#define MAX9286_FSYNCMETH_SEMI_AUTO	(1 << 0)
#define MAX9286_FSYNCMETH_MANUAL	(0 << 0)
#define MAX9286_REG_FSYNC_PERIOD_L	0x06
#define MAX9286_REG_FSYNC_PERIOD_M	0x07
#define MAX9286_REG_FSYNC_PERIOD_H	0x08
/* Register 0x0a */
#define MAX9286_FWDCCEN(n)		(1 << ((n) + 4))
#define MAX9286_REVCCEN(n)		(1 << (n))
/* Register 0x0c */
#define MAX9286_HVEN			BIT(7)
#define MAX9286_EDC_6BIT_HAMMING	(2 << 5)
#define MAX9286_EDC_6BIT_CRC		(1 << 5)
#define MAX9286_EDC_1BIT_PARITY		(0 << 5)
#define MAX9286_DESEL			BIT(4)
#define MAX9286_INVVS			BIT(3)
#define MAX9286_INVHS			BIT(2)
#define MAX9286_HVSRC_D0		(2 << 0)
#define MAX9286_HVSRC_D14		(1 << 0)
#define MAX9286_HVSRC_D18		(0 << 0)
/* Register 0x0f */
#define MAX9286_0X0F_RESERVED		BIT(3)
/* Register 0x12 */
#define MAX9286_CSILANECNT(n)		(((n) - 1) << 6)
#define MAX9286_CSIDBL			BIT(5)
#define MAX9286_DBL			BIT(4)
#define MAX9286_DATATYPE_USER_8BIT	(11 << 0)
#define MAX9286_DATATYPE_USER_YUV_12BIT	(10 << 0)
#define MAX9286_DATATYPE_USER_24BIT	(9 << 0)
#define MAX9286_DATATYPE_RAW14		(8 << 0)
#define MAX9286_DATATYPE_RAW11		(7 << 0)
#define MAX9286_DATATYPE_RAW10		(6 << 0)
#define MAX9286_DATATYPE_RAW8		(5 << 0)
#define MAX9286_DATATYPE_YUV422_10BIT	(4 << 0)
#define MAX9286_DATATYPE_YUV422_8BIT	(3 << 0)
#define MAX9286_DATATYPE_RGB555		(2 << 0)
#define MAX9286_DATATYPE_RGB565		(1 << 0)
#define MAX9286_DATATYPE_RGB888		(0 << 0)
/* Register 0x15 */
#define MAX9286_VC(n)			((n) << 5)
#define MAX9286_VCTYPE			BIT(4)
#define MAX9286_CSIOUTEN		BIT(3)
#define MAX9286_0X15_RESV		(3 << 0)
/* Register 0x1b */
#define MAX9286_SWITCHIN(n)		(1 << ((n) + 4))
#define MAX9286_ENEQ(n)			(1 << (n))
/* Register 0x27 */
#define MAX9286_LOCKED			BIT(7)
/* Register 0x31 */
#define MAX9286_FSYNC_LOCKED		BIT(6)
/* Register 0x34 */
#define MAX9286_I2CLOCACK		BIT(7)
#define MAX9286_I2CSLVSH_1046NS_469NS	(3 << 5)
#define MAX9286_I2CSLVSH_938NS_352NS	(2 << 5)
#define MAX9286_I2CSLVSH_469NS_234NS	(1 << 5)
#define MAX9286_I2CSLVSH_352NS_117NS	(0 << 5)
#define MAX9286_I2CMSTBT_837KBPS	(7 << 2)
#define MAX9286_I2CMSTBT_533KBPS	(6 << 2)
#define MAX9286_I2CMSTBT_339KBPS	(5 << 2)
#define MAX9286_I2CMSTBT_173KBPS	(4 << 2)
#define MAX9286_I2CMSTBT_105KBPS	(3 << 2)
#define MAX9286_I2CMSTBT_84KBPS		(2 << 2)
#define MAX9286_I2CMSTBT_28KBPS		(1 << 2)
#define MAX9286_I2CMSTBT_8KBPS		(0 << 2)
#define MAX9286_I2CSLVTO_NONE		(3 << 0)
#define MAX9286_I2CSLVTO_1024US		(2 << 0)
#define MAX9286_I2CSLVTO_256US		(1 << 0)
#define MAX9286_I2CSLVTO_64US		(0 << 0)
/* Register 0x3b */
#define MAX9286_REV_TRF(n)		((n) << 4)
#define MAX9286_REV_AMP(n)		((((n) - 30) / 10) << 1) /* in mV */
#define MAX9286_REV_AMP_X		BIT(0)
/* Register 0x3f */
#define MAX9286_EN_REV_CFG		BIT(6)
#define MAX9286_REV_FLEN(n)		((n) - 20)
/* Register 0x49 */
#define MAX9286_VIDEO_DETECT_MASK	0x0f
/* Register 0x69 */
#define MAX9286_LFLTBMONMASKED		BIT(7)
#define MAX9286_LOCKMONMASKED		BIT(6)
#define MAX9286_AUTOCOMBACKEN		BIT(5)
#define MAX9286_AUTOMASKEN		BIT(4)
#define MAX9286_MASKLINK(n)		((n) << 0)

/*
 * The sink and source pads are created to match the OF graph port numbers so
 * that their indexes can be used interchangeably.
 */
#define MAX9286_NUM_GMSL		4
#define MAX9286_N_SINKS			4
#define MAX9286_N_PADS			5
#define MAX9286_SRC_PAD			4

struct max9286_source {
	struct v4l2_subdev *sd;
	struct fwnode_handle *fwnode;
};

struct max9286_asd {
	struct v4l2_async_subdev base;
	struct max9286_source *source;
};

static inline struct max9286_asd *to_max9286_asd(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct max9286_asd, base);
}

struct max9286_priv {
	struct i2c_client *client;
	struct gpio_desc *gpiod_pwdn;
	struct v4l2_subdev sd;
	struct media_pad pads[MAX9286_N_PADS];
	struct regulator *regulator;

	struct gpio_chip gpio;
	u8 gpio_state;

	struct i2c_mux_core *mux;
	unsigned int mux_channel;
	bool mux_open;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *pixelrate;

	struct v4l2_mbus_framefmt fmt[MAX9286_N_SINKS];

	/* Protects controls and fmt structures */
	struct mutex mutex;

	unsigned int nsources;
	unsigned int source_mask;
	unsigned int route_mask;
	unsigned int bound_sources;
	unsigned int csi2_data_lanes;
	struct max9286_source sources[MAX9286_NUM_GMSL];
	struct v4l2_async_notifier notifier;
};

static struct max9286_source *next_source(struct max9286_priv *priv,
					  struct max9286_source *source)
{
	if (!source)
		source = &priv->sources[0];
	else
		source++;

	for (; source < &priv->sources[MAX9286_NUM_GMSL]; source++) {
		if (source->fwnode)
			return source;
	}

	return NULL;
}

#define for_each_source(priv, source) \
	for ((source) = NULL; ((source) = next_source((priv), (source))); )

#define to_index(priv, source) ((source) - &(priv)->sources[0])

static inline struct max9286_priv *sd_to_max9286(struct v4l2_subdev *sd)
{
	return container_of(sd, struct max9286_priv, sd);
}

/* -----------------------------------------------------------------------------
 * I2C IO
 */

static int max9286_read(struct max9286_priv *priv, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(priv->client, reg);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x read failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

static int max9286_write(struct max9286_priv *priv, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(priv->client, reg, val);
	if (ret < 0)
		dev_err(&priv->client->dev,
			"%s: register 0x%02x write failed (%d)\n",
			__func__, reg, ret);

	return ret;
}

/* -----------------------------------------------------------------------------
 * I2C Multiplexer
 */

static void max9286_i2c_mux_configure(struct max9286_priv *priv, u8 conf)
{
	max9286_write(priv, 0x0a, conf);

	/*
	 * We must sleep after any change to the forward or reverse channel
	 * configuration.
	 */
	usleep_range(3000, 5000);
}

static void max9286_i2c_mux_open(struct max9286_priv *priv)
{
	/* Open all channels on the MAX9286 */
	max9286_i2c_mux_configure(priv, 0xff);

	priv->mux_open = true;
}

static void max9286_i2c_mux_close(struct max9286_priv *priv)
{
	/*
	 * Ensure that both the forward and reverse channel are disabled on the
	 * mux, and that the channel ID is invalidated to ensure we reconfigure
	 * on the next max9286_i2c_mux_select() call.
	 */
	max9286_i2c_mux_configure(priv, 0x00);

	priv->mux_open = false;
	priv->mux_channel = -1;
}

static int max9286_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct max9286_priv *priv = i2c_mux_priv(muxc);

	/* Channel select is disabled when configured in the opened state. */
	if (priv->mux_open)
		return 0;

	if (priv->mux_channel == chan)
		return 0;

	priv->mux_channel = chan;

	max9286_i2c_mux_configure(priv,
				  MAX9286_FWDCCEN(chan) |
				  MAX9286_REVCCEN(chan));

	return 0;
}

static int max9286_i2c_mux_init(struct max9286_priv *priv)
{
	struct max9286_source *source;
	int ret;

	if (!i2c_check_functionality(priv->client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	priv->mux = i2c_mux_alloc(priv->client->adapter, &priv->client->dev,
				  priv->nsources, 0, I2C_MUX_LOCKED,
				  max9286_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	priv->mux->priv = priv;

	for_each_source(priv, source) {
		unsigned int index = to_index(priv, source);

		ret = i2c_mux_add_adapter(priv->mux, 0, index, 0);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	i2c_mux_del_adapters(priv->mux);
	return ret;
}

static void max9286_configure_i2c(struct max9286_priv *priv, bool localack)
{
	u8 config = MAX9286_I2CSLVSH_469NS_234NS | MAX9286_I2CSLVTO_1024US |
		    MAX9286_I2CMSTBT_105KBPS;

	if (localack)
		config |= MAX9286_I2CLOCACK;

	max9286_write(priv, 0x34, config);
	usleep_range(3000, 5000);
}

/*
 * max9286_check_video_links() - Make sure video links are detected and locked
 *
 * Performs safety checks on video link status. Make sure they are detected
 * and all enabled links are locked.
 *
 * Returns 0 for success, -EIO for errors.
 */
static int max9286_check_video_links(struct max9286_priv *priv)
{
	unsigned int i;
	int ret;

	/*
	 * Make sure valid video links are detected.
	 * The delay is not characterized in de-serializer manual, wait up
	 * to 5 ms.
	 */
	for (i = 0; i < 10; i++) {
		ret = max9286_read(priv, 0x49);
		if (ret < 0)
			return -EIO;

		if ((ret & MAX9286_VIDEO_DETECT_MASK) == priv->source_mask)
			break;

		usleep_range(350, 500);
	}

	if (i == 10) {
		dev_err(&priv->client->dev,
			"Unable to detect video links: 0x%02x\n", ret);
		return -EIO;
	}

	/* Make sure all enabled links are locked (4ms max). */
	for (i = 0; i < 10; i++) {
		ret = max9286_read(priv, 0x27);
		if (ret < 0)
			return -EIO;

		if (ret & MAX9286_LOCKED)
			break;

		usleep_range(350, 450);
	}

	if (i == 10) {
		dev_err(&priv->client->dev, "Not all enabled links locked\n");
		return -EIO;
	}

	return 0;
}

/*
 * max9286_check_config_link() - Detect and wait for configuration links
 *
 * Determine if the configuration channel is up and settled for a link.
 *
 * Returns 0 for success, -EIO for errors.
 */
static int max9286_check_config_link(struct max9286_priv *priv,
				     unsigned int source_mask)
{
	unsigned int conflink_mask = (source_mask & 0x0f) << 4;
	unsigned int i;
	int ret;

	/*
	 * Make sure requested configuration links are detected.
	 * The delay is not characterized in the chip manual: wait up
	 * to 5 milliseconds.
	 */
	for (i = 0; i < 10; i++) {
		ret = max9286_read(priv, 0x49);
		if (ret < 0)
			return -EIO;

		ret &= 0xf0;
		if (ret == conflink_mask)
			break;

		usleep_range(350, 500);
	}

	if (ret != conflink_mask) {
		dev_err(&priv->client->dev,
			"Unable to detect configuration links: 0x%02x expected 0x%02x\n",
			ret, conflink_mask);
		return -EIO;
	}

	dev_info(&priv->client->dev,
		 "Successfully detected configuration links after %u loops: 0x%02x\n",
		 i, conflink_mask);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdev
 */

static int max9286_set_pixelrate(struct max9286_priv *priv)
{
	struct max9286_source *source = NULL;
	u64 pixelrate = 0;

	for_each_source(priv, source) {
		struct v4l2_ctrl *ctrl;
		u64 source_rate = 0;

		/* Pixel rate is mandatory to be reported by sources. */
		ctrl = v4l2_ctrl_find(source->sd->ctrl_handler,
				      V4L2_CID_PIXEL_RATE);
		if (!ctrl) {
			pixelrate = 0;
			break;
		}

		/* All source must report the same pixel rate. */
		source_rate = v4l2_ctrl_g_ctrl_int64(ctrl);
		if (!pixelrate) {
			pixelrate = source_rate;
		} else if (pixelrate != source_rate) {
			dev_err(&priv->client->dev,
				"Unable to calculate pixel rate\n");
			return -EINVAL;
		}
	}

	if (!pixelrate) {
		dev_err(&priv->client->dev,
			"No pixel rate control available in sources\n");
		return -EINVAL;
	}

	/*
	 * The CSI-2 transmitter pixel rate is the single source rate multiplied
	 * by the number of available sources.
	 */
	return v4l2_ctrl_s_ctrl_int64(priv->pixelrate,
				      pixelrate * priv->nsources);
}

static int max9286_notify_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct max9286_priv *priv = sd_to_max9286(notifier->sd);
	struct max9286_source *source = to_max9286_asd(asd)->source;
	unsigned int index = to_index(priv, source);
	unsigned int src_pad;
	int ret;

	ret = media_entity_get_fwnode_pad(&subdev->entity,
					  source->fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"Failed to find pad for %s\n", subdev->name);
		return ret;
	}

	priv->bound_sources |= BIT(index);
	source->sd = subdev;
	src_pad = ret;

	ret = media_create_pad_link(&source->sd->entity, src_pad,
				    &priv->sd.entity, index,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(&priv->client->dev,
			"Unable to link %s:%u -> %s:%u\n",
			source->sd->name, src_pad, priv->sd.name, index);
		return ret;
	}

	dev_dbg(&priv->client->dev, "Bound %s pad: %u on index %u\n",
		subdev->name, src_pad, index);

	/*
	 * We can only register v4l2_async_notifiers, which do not provide a
	 * means to register a complete callback. bound_sources allows us to
	 * identify when all remote serializers have completed their probe.
	 */
	if (priv->bound_sources != priv->source_mask)
		return 0;

	/*
	 * All enabled sources have probed and enabled their reverse control
	 * channels:
	 *
	 * - Verify all configuration links are properly detected
	 * - Disable auto-ack as communication on the control channel are now
	 *   stable.
	 */
	max9286_check_config_link(priv, priv->source_mask);

	/*
	 * Re-configure I2C with local acknowledge disabled after cameras have
	 * probed.
	 */
	max9286_configure_i2c(priv, false);

	return max9286_set_pixelrate(priv);
}

static void max9286_notify_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *subdev,
				  struct v4l2_async_subdev *asd)
{
	struct max9286_priv *priv = sd_to_max9286(notifier->sd);
	struct max9286_source *source = to_max9286_asd(asd)->source;
	unsigned int index = to_index(priv, source);

	source->sd = NULL;
	priv->bound_sources &= ~BIT(index);
}

static const struct v4l2_async_notifier_operations max9286_notify_ops = {
	.bound = max9286_notify_bound,
	.unbind = max9286_notify_unbind,
};

static int max9286_v4l2_notifier_register(struct max9286_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct max9286_source *source = NULL;
	int ret;

	if (!priv->nsources)
		return 0;

	v4l2_async_notifier_init(&priv->notifier);

	for_each_source(priv, source) {
		unsigned int i = to_index(priv, source);
		struct v4l2_async_subdev *asd;

		asd = v4l2_async_notifier_add_fwnode_subdev(&priv->notifier,
							    source->fwnode,
							    sizeof(*asd));
		if (IS_ERR(asd)) {
			dev_err(dev, "Failed to add subdev for source %u: %ld",
				i, PTR_ERR(asd));
			v4l2_async_notifier_cleanup(&priv->notifier);
			return PTR_ERR(asd);
		}

		to_max9286_asd(asd)->source = source;
	}

	priv->notifier.ops = &max9286_notify_ops;

	ret = v4l2_async_subdev_notifier_register(&priv->sd, &priv->notifier);
	if (ret) {
		dev_err(dev, "Failed to register subdev_notifier");
		v4l2_async_notifier_cleanup(&priv->notifier);
		return ret;
	}

	return 0;
}

static void max9286_v4l2_notifier_unregister(struct max9286_priv *priv)
{
	if (!priv->nsources)
		return;

	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_notifier_cleanup(&priv->notifier);
}

static int max9286_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct max9286_priv *priv = sd_to_max9286(sd);
	struct max9286_source *source;
	unsigned int i;
	bool sync = false;
	int ret;

	if (enable) {
		/*
		 * The frame sync between cameras is transmitted across the
		 * reverse channel as GPIO. We must open all channels while
		 * streaming to allow this synchronisation signal to be shared.
		 */
		max9286_i2c_mux_open(priv);

		/* Start all cameras. */
		for_each_source(priv, source) {
			ret = v4l2_subdev_call(source->sd, video, s_stream, 1);
			if (ret)
				return ret;
		}

		ret = max9286_check_video_links(priv);
		if (ret)
			return ret;

		/*
		 * Wait until frame synchronization is locked.
		 *
		 * Manual says frame sync locking should take ~6 VTS.
		 * From practical experience at least 8 are required. Give
		 * 12 complete frames time (~400ms at 30 fps) to achieve frame
		 * locking before returning error.
		 */
		for (i = 0; i < 40; i++) {
			if (max9286_read(priv, 0x31) & MAX9286_FSYNC_LOCKED) {
				sync = true;
				break;
			}
			usleep_range(9000, 11000);
		}

		if (!sync) {
			dev_err(&priv->client->dev,
				"Failed to get frame synchronization\n");
			return -EXDEV; /* Invalid cross-device link */
		}

		/*
		 * Enable CSI output, VC set according to link number.
		 * Bit 7 must be set (chip manual says it's 0 and reserved).
		 */
		max9286_write(priv, 0x15, 0x80 | MAX9286_VCTYPE |
			      MAX9286_CSIOUTEN | MAX9286_0X15_RESV);
	} else {
		max9286_write(priv, 0x15, MAX9286_VCTYPE | MAX9286_0X15_RESV);

		/* Stop all cameras. */
		for_each_source(priv, source)
			v4l2_subdev_call(source->sd, video, s_stream, 0);

		max9286_i2c_mux_close(priv);
	}

	return 0;
}

static int max9286_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_1X16;

	return 0;
}

static struct v4l2_mbus_framefmt *
max9286_get_pad_format(struct max9286_priv *priv,
		       struct v4l2_subdev_pad_config *cfg,
		       unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&priv->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &priv->fmt[pad];
	default:
		return NULL;
	}
}

static int max9286_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct max9286_priv *priv = sd_to_max9286(sd);
	struct v4l2_mbus_framefmt *cfg_fmt;

	if (format->pad == MAX9286_SRC_PAD)
		return -EINVAL;

	/* Refuse non YUV422 formats as we hardcode DT to 8 bit YUV422 */
	switch (format->format.code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
		break;
	default:
		format->format.code = MEDIA_BUS_FMT_UYVY8_1X16;
		break;
	}

	cfg_fmt = max9286_get_pad_format(priv, cfg, format->pad, format->which);
	if (!cfg_fmt)
		return -EINVAL;

	mutex_lock(&priv->mutex);
	*cfg_fmt = format->format;
	mutex_unlock(&priv->mutex);

	return 0;
}

static int max9286_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct max9286_priv *priv = sd_to_max9286(sd);
	struct v4l2_mbus_framefmt *cfg_fmt;
	unsigned int pad = format->pad;

	/*
	 * Multiplexed Stream Support: Support link validation by returning the
	 * format of the first bound link. All links must have the same format,
	 * as we do not support mixing and matching of cameras connected to the
	 * max9286.
	 */
	if (pad == MAX9286_SRC_PAD)
		pad = __ffs(priv->bound_sources);

	cfg_fmt = max9286_get_pad_format(priv, cfg, pad, format->which);
	if (!cfg_fmt)
		return -EINVAL;

	mutex_lock(&priv->mutex);
	format->format = *cfg_fmt;
	mutex_unlock(&priv->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops max9286_video_ops = {
	.s_stream	= max9286_s_stream,
};

static const struct v4l2_subdev_pad_ops max9286_pad_ops = {
	.enum_mbus_code = max9286_enum_mbus_code,
	.get_fmt	= max9286_get_fmt,
	.set_fmt	= max9286_set_fmt,
};

static const struct v4l2_subdev_ops max9286_subdev_ops = {
	.video		= &max9286_video_ops,
	.pad		= &max9286_pad_ops,
};

static void max9286_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width		= 1280;
	fmt->height		= 800;
	fmt->code		= MEDIA_BUS_FMT_UYVY8_1X16;
	fmt->colorspace		= V4L2_COLORSPACE_SRGB;
	fmt->field		= V4L2_FIELD_NONE;
	fmt->ycbcr_enc		= V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization	= V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func		= V4L2_XFER_FUNC_DEFAULT;
}

static int max9286_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	for (i = 0; i < MAX9286_N_SINKS; i++) {
		format = v4l2_subdev_get_try_format(subdev, fh->pad, i);
		max9286_init_format(format);
	}

	return 0;
}

static const struct v4l2_subdev_internal_ops max9286_subdev_internal_ops = {
	.open = max9286_open,
};

static int max9286_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops max9286_ctrl_ops = {
	.s_ctrl = max9286_s_ctrl,
};

static int max9286_v4l2_register(struct max9286_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct fwnode_handle *ep;
	int ret;
	int i;

	/* Register v4l2 async notifiers for connected Camera subdevices */
	ret = max9286_v4l2_notifier_register(priv);
	if (ret) {
		dev_err(dev, "Unable to register V4L2 async notifiers\n");
		return ret;
	}

	/* Configure V4L2 for the MAX9286 itself */

	for (i = 0; i < MAX9286_N_SINKS; i++)
		max9286_init_format(&priv->fmt[i]);

	v4l2_i2c_subdev_init(&priv->sd, priv->client, &max9286_subdev_ops);
	priv->sd.internal_ops = &max9286_subdev_internal_ops;
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	v4l2_ctrl_handler_init(&priv->ctrls, 1);
	priv->pixelrate = v4l2_ctrl_new_std(&priv->ctrls,
					    &max9286_ctrl_ops,
					    V4L2_CID_PIXEL_RATE,
					    1, INT_MAX, 1, 50000000);

	priv->sd.ctrl_handler = &priv->ctrls;
	ret = priv->ctrls.error;
	if (ret)
		goto err_async;

	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;

	priv->pads[MAX9286_SRC_PAD].flags = MEDIA_PAD_FL_SOURCE;
	for (i = 0; i < MAX9286_SRC_PAD; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&priv->sd.entity, MAX9286_N_PADS,
				     priv->pads);
	if (ret)
		goto err_async;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), MAX9286_SRC_PAD,
					     0, 0);
	if (!ep) {
		dev_err(dev, "Unable to retrieve endpoint on \"port@4\"\n");
		ret = -ENOENT;
		goto err_async;
	}
	priv->sd.fwnode = ep;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret < 0) {
		dev_err(dev, "Unable to register subdevice\n");
		goto err_put_node;
	}

	return 0;

err_put_node:
	fwnode_handle_put(ep);
err_async:
	max9286_v4l2_notifier_unregister(priv);

	return ret;
}

static void max9286_v4l2_unregister(struct max9286_priv *priv)
{
	fwnode_handle_put(priv->sd.fwnode);
	v4l2_async_unregister_subdev(&priv->sd);
	max9286_v4l2_notifier_unregister(priv);
}

/* -----------------------------------------------------------------------------
 * Probe/Remove
 */

static int max9286_setup(struct max9286_priv *priv)
{
	/*
	 * Link ordering values for all enabled links combinations. Orders must
	 * be assigned sequentially from 0 to the number of enabled links
	 * without leaving any hole for disabled links. We thus assign orders to
	 * enabled links first, and use the remaining order values for disabled
	 * links are all links must have a different order value;
	 */
	static const u8 link_order[] = {
		(3 << 6) | (2 << 4) | (1 << 2) | (0 << 0), /* xxxx */
		(3 << 6) | (2 << 4) | (1 << 2) | (0 << 0), /* xxx0 */
		(3 << 6) | (2 << 4) | (0 << 2) | (1 << 0), /* xx0x */
		(3 << 6) | (2 << 4) | (1 << 2) | (0 << 0), /* xx10 */
		(3 << 6) | (0 << 4) | (2 << 2) | (1 << 0), /* x0xx */
		(3 << 6) | (1 << 4) | (2 << 2) | (0 << 0), /* x1x0 */
		(3 << 6) | (1 << 4) | (0 << 2) | (2 << 0), /* x10x */
		(3 << 6) | (1 << 4) | (1 << 2) | (0 << 0), /* x210 */
		(0 << 6) | (3 << 4) | (2 << 2) | (1 << 0), /* 0xxx */
		(1 << 6) | (3 << 4) | (2 << 2) | (0 << 0), /* 1xx0 */
		(1 << 6) | (3 << 4) | (0 << 2) | (2 << 0), /* 1x0x */
		(2 << 6) | (3 << 4) | (1 << 2) | (0 << 0), /* 2x10 */
		(1 << 6) | (0 << 4) | (3 << 2) | (2 << 0), /* 10xx */
		(2 << 6) | (1 << 4) | (3 << 2) | (0 << 0), /* 21x0 */
		(2 << 6) | (1 << 4) | (0 << 2) | (3 << 0), /* 210x */
		(3 << 6) | (2 << 4) | (1 << 2) | (0 << 0), /* 3210 */
	};

	/*
	 * Set the I2C bus speed.
	 *
	 * Enable I2C Local Acknowledge during the probe sequences of the camera
	 * only. This should be disabled after the mux is initialised.
	 */
	max9286_configure_i2c(priv, true);

	/*
	 * Reverse channel setup.
	 *
	 * - Enable custom reverse channel configuration (through register 0x3f)
	 *   and set the first pulse length to 35 clock cycles.
	 * - Increase the reverse channel amplitude to 170mV to accommodate the
	 *   high threshold enabled by the serializer driver.
	 */
	max9286_write(priv, 0x3f, MAX9286_EN_REV_CFG | MAX9286_REV_FLEN(35));
	max9286_write(priv, 0x3b, MAX9286_REV_TRF(1) | MAX9286_REV_AMP(70) |
		      MAX9286_REV_AMP_X);
	usleep_range(2000, 2500);

	/*
	 * Enable GMSL links, mask unused ones and autodetect link
	 * used as CSI clock source.
	 */
	max9286_write(priv, 0x00, MAX9286_MSTLINKSEL_AUTO | priv->route_mask);
	max9286_write(priv, 0x0b, link_order[priv->route_mask]);
	max9286_write(priv, 0x69, (0xf & ~priv->route_mask));

	/*
	 * Video format setup:
	 * Disable CSI output, VC is set according to Link number.
	 */
	max9286_write(priv, 0x15, MAX9286_VCTYPE | MAX9286_0X15_RESV);

	/* Enable CSI-2 Lane D0-D3 only, DBL mode, YUV422 8-bit. */
	max9286_write(priv, 0x12, MAX9286_CSIDBL | MAX9286_DBL |
		      MAX9286_CSILANECNT(priv->csi2_data_lanes) |
		      MAX9286_DATATYPE_YUV422_8BIT);

	/* Automatic: FRAMESYNC taken from the slowest Link. */
	max9286_write(priv, 0x01, MAX9286_FSYNCMODE_INT_HIZ |
		      MAX9286_FSYNCMETH_AUTO);

	/* Enable HS/VS encoding, use D14/15 for HS/VS, invert VS. */
	max9286_write(priv, 0x0c, MAX9286_HVEN | MAX9286_INVVS |
		      MAX9286_HVSRC_D14);

	/*
	 * The overlap window seems to provide additional validation by tracking
	 * the delay between vsync and frame sync, generating an error if the
	 * delay is bigger than the programmed window, though it's not yet clear
	 * what value should be set.
	 *
	 * As it's an optional value and can be disabled, we do so by setting
	 * a 0 overlap value.
	 */
	max9286_write(priv, 0x63, 0);
	max9286_write(priv, 0x64, 0);

	/*
	 * Wait for 2ms to allow the link to resynchronize after the
	 * configuration change.
	 */
	usleep_range(2000, 5000);

	return 0;
}

static void max9286_gpio_set(struct gpio_chip *chip,
			     unsigned int offset, int value)
{
	struct max9286_priv *priv = gpiochip_get_data(chip);

	if (value)
		priv->gpio_state |= BIT(offset);
	else
		priv->gpio_state &= ~BIT(offset);

	max9286_write(priv, 0x0f, MAX9286_0X0F_RESERVED | priv->gpio_state);
}

static int max9286_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct max9286_priv *priv = gpiochip_get_data(chip);

	return priv->gpio_state & BIT(offset);
}

static int max9286_register_gpio(struct max9286_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct gpio_chip *gpio = &priv->gpio;
	int ret;

	/* Configure the GPIO */
	gpio->label = dev_name(dev);
	gpio->parent = dev;
	gpio->owner = THIS_MODULE;
	gpio->of_node = dev->of_node;
	gpio->ngpio = 2;
	gpio->base = -1;
	gpio->set = max9286_gpio_set;
	gpio->get = max9286_gpio_get;
	gpio->can_sleep = true;

	/* GPIO values default to high */
	priv->gpio_state = BIT(0) | BIT(1);

	ret = devm_gpiochip_add_data(dev, gpio, priv);
	if (ret)
		dev_err(dev, "Unable to create gpio_chip\n");

	return ret;
}

static int max9286_init(struct device *dev)
{
	struct max9286_priv *priv;
	struct i2c_client *client;
	int ret;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	/* Enable the bus power. */
	ret = regulator_enable(priv->regulator);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to turn PoC on\n");
		return ret;
	}

	ret = max9286_setup(priv);
	if (ret) {
		dev_err(dev, "Unable to setup max9286\n");
		goto err_regulator;
	}

	/*
	 * Register all V4L2 interactions for the MAX9286 and notifiers for
	 * any subdevices connected.
	 */
	ret = max9286_v4l2_register(priv);
	if (ret) {
		dev_err(dev, "Failed to register with V4L2\n");
		goto err_regulator;
	}

	ret = max9286_i2c_mux_init(priv);
	if (ret) {
		dev_err(dev, "Unable to initialize I2C multiplexer\n");
		goto err_v4l2_register;
	}

	/* Leave the mux channels disabled until they are selected. */
	max9286_i2c_mux_close(priv);

	return 0;

err_v4l2_register:
	max9286_v4l2_unregister(priv);
err_regulator:
	regulator_disable(priv->regulator);

	return ret;
}

static void max9286_cleanup_dt(struct max9286_priv *priv)
{
	struct max9286_source *source;

	for_each_source(priv, source) {
		fwnode_handle_put(source->fwnode);
		source->fwnode = NULL;
	}
}

static int max9286_parse_dt(struct max9286_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *i2c_mux;
	struct device_node *node = NULL;
	unsigned int i2c_mux_mask = 0;

	/* Balance the of_node_put() performed by of_find_node_by_name(). */
	of_node_get(dev->of_node);
	i2c_mux = of_find_node_by_name(dev->of_node, "i2c-mux");
	if (!i2c_mux) {
		dev_err(dev, "Failed to find i2c-mux node\n");
		return -EINVAL;
	}

	/* Identify which i2c-mux channels are enabled */
	for_each_child_of_node(i2c_mux, node) {
		u32 id = 0;

		of_property_read_u32(node, "reg", &id);
		if (id >= MAX9286_NUM_GMSL)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled I2C bus port %u\n", id);
			continue;
		}

		i2c_mux_mask |= BIT(id);
	}
	of_node_put(node);
	of_node_put(i2c_mux);

	/* Parse the endpoints */
	for_each_endpoint_of_node(dev->of_node, node) {
		struct max9286_source *source;
		struct of_endpoint ep;

		of_graph_parse_endpoint(node, &ep);
		dev_dbg(dev, "Endpoint %pOF on port %d",
			ep.local_node, ep.port);

		if (ep.port > MAX9286_NUM_GMSL) {
			dev_err(dev, "Invalid endpoint %s on port %d",
				of_node_full_name(ep.local_node), ep.port);
			continue;
		}

		/* For the source endpoint just parse the bus configuration. */
		if (ep.port == MAX9286_SRC_PAD) {
			struct v4l2_fwnode_endpoint vep = {
				.bus_type = V4L2_MBUS_CSI2_DPHY
			};
			int ret;

			ret = v4l2_fwnode_endpoint_parse(
					of_fwnode_handle(node), &vep);
			if (ret) {
				of_node_put(node);
				return ret;
			}

			priv->csi2_data_lanes =
				vep.bus.mipi_csi2.num_data_lanes;

			continue;
		}

		/* Skip if the corresponding GMSL link is unavailable. */
		if (!(i2c_mux_mask & BIT(ep.port)))
			continue;

		if (priv->sources[ep.port].fwnode) {
			dev_err(dev,
				"Multiple port endpoints are not supported: %d",
				ep.port);

			continue;
		}

		source = &priv->sources[ep.port];
		source->fwnode = fwnode_graph_get_remote_endpoint(
						of_fwnode_handle(node));
		if (!source->fwnode) {
			dev_err(dev,
				"Endpoint %pOF has no remote endpoint connection\n",
				ep.local_node);

			continue;
		}

		priv->source_mask |= BIT(ep.port);
		priv->nsources++;
	}
	of_node_put(node);

	priv->route_mask = priv->source_mask;

	return 0;
}

static int max9286_probe(struct i2c_client *client)
{
	struct max9286_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->mutex);

	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->gpiod_pwdn = devm_gpiod_get_optional(&client->dev, "enable",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn))
		return PTR_ERR(priv->gpiod_pwdn);

	gpiod_set_consumer_name(priv->gpiod_pwdn, "max9286-pwdn");
	gpiod_set_value_cansleep(priv->gpiod_pwdn, 1);

	/* Wait at least 4ms before the I2C lines latch to the address */
	if (priv->gpiod_pwdn)
		usleep_range(4000, 5000);

	/*
	 * The MAX9286 starts by default with all ports enabled, we disable all
	 * ports early to ensure that all channels are disabled if we error out
	 * and keep the bus consistent.
	 */
	max9286_i2c_mux_close(priv);

	/*
	 * The MAX9286 initialises with auto-acknowledge enabled by default.
	 * This can be invasive to other transactions on the same bus, so
	 * disable it early. It will be enabled only as and when needed.
	 */
	max9286_configure_i2c(priv, false);

	ret = max9286_register_gpio(priv);
	if (ret)
		goto err_powerdown;

	priv->regulator = devm_regulator_get(&client->dev, "poc");
	if (IS_ERR(priv->regulator)) {
		if (PTR_ERR(priv->regulator) != -EPROBE_DEFER)
			dev_err(&client->dev,
				"Unable to get PoC regulator (%ld)\n",
				PTR_ERR(priv->regulator));
		ret = PTR_ERR(priv->regulator);
		goto err_powerdown;
	}

	ret = max9286_parse_dt(priv);
	if (ret)
		goto err_powerdown;

	ret = max9286_init(&client->dev);
	if (ret < 0)
		goto err_cleanup_dt;

	return 0;

err_cleanup_dt:
	max9286_cleanup_dt(priv);
err_powerdown:
	gpiod_set_value_cansleep(priv->gpiod_pwdn, 0);

	return ret;
}

static int max9286_remove(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);

	i2c_mux_del_adapters(priv->mux);

	max9286_v4l2_unregister(priv);

	regulator_disable(priv->regulator);

	gpiod_set_value_cansleep(priv->gpiod_pwdn, 0);

	max9286_cleanup_dt(priv);

	return 0;
}

static const struct of_device_id max9286_dt_ids[] = {
	{ .compatible = "maxim,max9286" },
	{},
};
MODULE_DEVICE_TABLE(of, max9286_dt_ids);

static struct i2c_driver max9286_i2c_driver = {
	.driver	= {
		.name		= "max9286",
		.of_match_table	= of_match_ptr(max9286_dt_ids),
	},
	.probe_new	= max9286_probe,
	.remove		= max9286_remove,
};

module_i2c_driver(max9286_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX9286 GMSL Deserializer Driver");
MODULE_AUTHOR("Jacopo Mondi, Kieran Bingham, Laurent Pinchart, Niklas Söderlund, Vladimir Barinov");
MODULE_LICENSE("GPL");
