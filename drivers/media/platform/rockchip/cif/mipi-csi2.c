// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip MIPI CSI2 Driver
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>

#include "mipi-csi2.h"

static int csi2_debug;
module_param_named(debug_csi2, csi2_debug, int, 0644);
MODULE_PARM_DESC(debug_csi2, "Debug level (0-1)");

/*
 * there must be 5 pads: 1 input pad from sensor, and
 * the 4 virtual channel output pads
 */
#define CSI2_SINK_PAD			0
#define CSI2_NUM_SINK_PADS		1
#define CSI2_NUM_SRC_PADS		4
#define CSI2_NUM_PADS			5
#define CSI2_NUM_PADS_SINGLE_LINK	2
#define MAX_CSI2_SENSORS		2

#define RKCIF_DEFAULT_WIDTH	640
#define RKCIF_DEFAULT_HEIGHT	480

/*
 * The default maximum bit-rate per lane in Mbps, if the
 * source subdev does not provide V4L2_CID_LINK_FREQ.
 */
#define CSI2_DEFAULT_MAX_MBPS 849

#define IMX_MEDIA_GRP_ID_CSI2      BIT(8)
#define CSIHOST_MAX_ERRINT_COUNT	10

/*
 * add new chip id in tail in time order
 * by increasing to distinguish csi2 host version
 */
enum rkcsi2_chip_id {
	CHIP_PX30_CSI2,
	CHIP_RK1808_CSI2,
	CHIP_RK3128_CSI2,
	CHIP_RK3288_CSI2,
	CHIP_RV1126_CSI2,
	CHIP_RK3568_CSI2,
};

enum csi2_pads {
	RK_CSI2_PAD_SINK = 0,
	RK_CSI2X_PAD_SOURCE0,
	RK_CSI2X_PAD_SOURCE1,
	RK_CSI2X_PAD_SOURCE2,
	RK_CSI2X_PAD_SOURCE3
};

enum csi2_err {
	RK_CSI2_ERR_SOTSYN = 0x0,
	RK_CSI2_ERR_FS_FE_MIS,
	RK_CSI2_ERR_FRM_SEQ_ERR,
	RK_CSI2_ERR_CRC_ONCE,
	RK_CSI2_ERR_CRC,
	RK_CSI2_ERR_ALL,
	RK_CSI2_ERR_MAX
};

enum host_type_t {
	RK_CSI_RXHOST,
	RK_DSI_RXHOST
};

struct csi2_match_data {
	int chip_id;
	int num_pads;
};

struct csi2_sensor {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

struct csi2_err_stats {
	unsigned int cnt;
};

struct csi2_dev {
	struct device		*dev;
	struct v4l2_subdev	sd;
	struct media_pad	pad[CSI2_NUM_PADS];
	struct clk_bulk_data	*clks_bulk;
	int			clks_num;
	struct reset_control	*rsts_bulk;

	void __iomem		*base;
	struct v4l2_async_notifier	notifier;
	struct v4l2_fwnode_bus_mipi_csi2	bus;

	/* lock to protect all members below */
	struct mutex lock;

	struct v4l2_mbus_framefmt	format_mbus;
	struct v4l2_rect	crop;
	int			stream_count;
	struct v4l2_subdev	*src_sd;
	bool			sink_linked[CSI2_NUM_SRC_PADS];
	struct csi2_sensor	sensors[MAX_CSI2_SENSORS];
	const struct csi2_match_data	*match_data;
	int			num_sensors;
	atomic_t		frm_sync_seq;
	struct csi2_err_stats err_list[RK_CSI2_ERR_MAX];
};

#define DEVICE_NAME "rockchip-mipi-csi2"

/* CSI Host Registers Define */
#define CSIHOST_N_LANES		0x04
#define CSIHOST_DPHY_SHUTDOWNZ	0x08
#define CSIHOST_PHY_RSTZ	0x0c
#define CSIHOST_RESETN		0x10
#define CSIHOST_PHY_STATE	0x14
#define CSIHOST_ERR1		0x20
#define CSIHOST_ERR2		0x24
#define CSIHOST_MSK1		0x28
#define CSIHOST_MSK2		0x2c
#define CSIHOST_CONTROL		0x40

#define CSIHOST_ERR1_PHYERR_SPTSYNCHS	0x0000000f
#define CSIHOST_ERR1_ERR_BNDRY_MATCH	0x000000f0
#define CSIHOST_ERR1_ERR_SEQ		0x00000f00
#define CSIHOST_ERR1_ERR_FRM_DATA	0x0000f000
#define CSIHOST_ERR1_ERR_CRC		0x1f000000

#define CSIHOST_ERR2_PHYERR_ESC		0x0000000f
#define CSIHOST_ERR2_PHYERR_SOTHS	0x000000f0
#define CSIHOST_ERR2_ECC_CORRECTED	0x00000f00
#define CSIHOST_ERR2_ERR_ID		0x0000f000
#define CSIHOST_ERR2_PHYERR_CODEHS	0x01000000

#define SW_CPHY_EN(x)		((x) << 0)
#define SW_DSI_EN(x)		((x) << 4)
#define SW_DATATYPE_FS(x)	((x) << 8)
#define SW_DATATYPE_FE(x)	((x) << 14)
#define SW_DATATYPE_LS(x)	((x) << 20)
#define SW_DATATYPE_LE(x)	((x) << 26)

#define write_csihost_reg(base, addr, val)  writel(val, (addr) + (base))
#define read_csihost_reg(base, addr) readl((addr) + (base))

static struct csi2_dev *g_csi2_dev;
static ATOMIC_NOTIFIER_HEAD(g_csi_host_chain);

int rkcif_csi2_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&g_csi_host_chain, nb);
}

int rkcif_csi2_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&g_csi_host_chain, nb);
}

static inline struct csi2_dev *sd_to_dev(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi2_dev, sd);
}

static struct csi2_sensor *sd_to_sensor(struct csi2_dev *csi2,
					struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < csi2->num_sensors; ++i)
		if (csi2->sensors[i].sd == sd)
			return &csi2->sensors[i];

	return NULL;
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RK_CSI2_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static void csi2_update_sensor_info(struct csi2_dev *csi2)
{
	struct csi2_sensor *sensor = &csi2->sensors[0];
	struct v4l2_mbus_config mbus;
	int ret = 0;

	ret = v4l2_subdev_call(sensor->sd, video, g_mbus_config, &mbus);
	if (ret) {
		v4l2_err(&csi2->sd, "update sensor info failed!\n");
		return;
	}

	csi2->bus.flags = mbus.flags;
	switch (csi2->bus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		csi2->bus.num_data_lanes = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		csi2->bus.num_data_lanes = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		csi2->bus.num_data_lanes = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
		csi2->bus.num_data_lanes = 4;
		break;
	default:
		v4l2_warn(&csi2->sd, "lane num is invalid\n");
		csi2->bus.num_data_lanes = 0;
		break;
	}

}

static void csi2_hw_do_reset(struct csi2_dev *csi2)
{
	reset_control_assert(csi2->rsts_bulk);

	udelay(5);

	reset_control_deassert(csi2->rsts_bulk);
}

static int csi2_enable_clks(struct csi2_dev *csi2)
{
	int ret = 0;

	ret = clk_bulk_prepare_enable(csi2->clks_num, csi2->clks_bulk);
	if (ret)
		dev_err(csi2->dev, "failed to enable clks\n");

	return ret;
}

static void csi2_disable_clks(struct csi2_dev *csi2)
{
	clk_bulk_disable_unprepare(csi2->clks_num,  csi2->clks_bulk);
}

static void csi2_disable(struct csi2_dev *csi2)
{
	void __iomem *base = csi2->base;

	write_csihost_reg(base, CSIHOST_RESETN, 0);
	write_csihost_reg(base, CSIHOST_MSK1, 0xffffffff);
	write_csihost_reg(base, CSIHOST_MSK2, 0xffffffff);
}

static void csi2_enable(struct csi2_dev *csi2,
			enum host_type_t host_type)
{
	void __iomem *base = csi2->base;
	int lanes = csi2->bus.num_data_lanes;

	write_csihost_reg(base, CSIHOST_N_LANES, lanes - 1);

	if (host_type == RK_DSI_RXHOST) {
		write_csihost_reg(base, CSIHOST_CONTROL,
				  SW_CPHY_EN(0) | SW_DSI_EN(1) |
				  SW_DATATYPE_FS(0x01) | SW_DATATYPE_FE(0x11) |
				  SW_DATATYPE_LS(0x21) | SW_DATATYPE_LE(0x31));
		/* Disable some error interrupt when HOST work on DSI RX mode */
		write_csihost_reg(base, CSIHOST_MSK1, 0xe00000f0);
		write_csihost_reg(base, CSIHOST_MSK2, 0xff00);
	} else {
		write_csihost_reg(base, CSIHOST_CONTROL,
				  SW_CPHY_EN(0) | SW_DSI_EN(0));
		write_csihost_reg(base, CSIHOST_MSK1, 0);
		write_csihost_reg(base, CSIHOST_MSK2, 0);
	}

	write_csihost_reg(base, CSIHOST_RESETN, 1);
}

static int csi2_start(struct csi2_dev *csi2)
{
	enum host_type_t host_type;
	int ret, i;

	atomic_set(&csi2->frm_sync_seq, 0);

	csi2_hw_do_reset(csi2);
	ret = csi2_enable_clks(csi2);
	if (ret) {
		v4l2_err(&csi2->sd, "%s: enable clks failed\n", __func__);
		return ret;
	}

	csi2_update_sensor_info(csi2);

	if (csi2->format_mbus.code == MEDIA_BUS_FMT_RGB888_1X24)
		host_type = RK_DSI_RXHOST;
	else
		host_type = RK_CSI_RXHOST;

	csi2_enable(csi2, host_type);

	pr_debug("stream sd: %s\n", csi2->src_sd->name);
	ret = v4l2_subdev_call(csi2->src_sd, video, s_stream, 1);
	ret = (ret && ret != -ENOIOCTLCMD) ? ret : 0;
	if (ret)
		goto err_assert_reset;

	for (i = 0; i < RK_CSI2_ERR_MAX; i++)
		csi2->err_list[i].cnt = 0;

	return 0;

err_assert_reset:
	csi2_disable(csi2);
	csi2_disable_clks(csi2);

	return ret;
}

static void csi2_stop(struct csi2_dev *csi2)
{
	/* stop upstream */
	v4l2_subdev_call(csi2->src_sd, video, s_stream, 0);

	csi2_disable(csi2);
	csi2_disable_clks(csi2);
}

/*
 * V4L2 subdev operations.
 */

static int csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int ret = 0;

	mutex_lock(&csi2->lock);

	dev_err(csi2->dev, "stream %s, src_sd: %p, sd_name:%s\n",
		enable ? "on" : "off",
		csi2->src_sd, csi2->src_sd->name);

	/*
	 * enable/disable streaming only if stream_count is
	 * going from 0 to 1 / 1 to 0.
	 */
	if (csi2->stream_count != !enable)
		goto update_count;

	dev_err(csi2->dev, "stream %s\n", enable ? "ON" : "OFF");

	if (enable)
		ret = csi2_start(csi2);
	else
		csi2_stop(csi2);
	if (ret)
		goto out;

update_count:
	csi2->stream_count += enable ? 1 : -1;
	if (csi2->stream_count < 0)
		csi2->stream_count = 0;
out:
	mutex_unlock(&csi2->lock);

	return ret;
}

static int csi2_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	remote_sd = media_entity_to_v4l2_subdev(remote->entity);

	mutex_lock(&csi2->lock);

	if (local->flags & MEDIA_PAD_FL_SOURCE) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->sink_linked[local->index - 1]) {
				ret = -EBUSY;
				goto out;
			}
			csi2->sink_linked[local->index - 1] = true;
		} else {
			csi2->sink_linked[local->index - 1] = false;
		}
	} else {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi2->src_sd) {
				ret = -EBUSY;
				goto out;
			}
			csi2->src_sd = remote_sd;
		} else {
			csi2->src_sd = NULL;
		}
	}

out:
	mutex_unlock(&csi2->lock);
	return ret;
}

static int csi2_media_init(struct v4l2_subdev *sd)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	int i = 0, num_pads = 0;

	num_pads = csi2->match_data->num_pads;

	for (i = 0; i < num_pads; i++) {
		csi2->pad[i].flags = (i == CSI2_SINK_PAD) ?
		MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
	}

	csi2->pad[RK_CSI2X_PAD_SOURCE0].flags =
		MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;
	csi2->pad[RK_CSI2_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	/* set a default mbus format  */
	csi2->format_mbus.code =  MEDIA_BUS_FMT_UYVY8_2X8;
	csi2->format_mbus.field = V4L2_FIELD_NONE;
	csi2->format_mbus.width = RKCIF_DEFAULT_WIDTH;
	csi2->format_mbus.height = RKCIF_DEFAULT_HEIGHT;
	csi2->crop.top = 0;
	csi2->crop.left = 0;
	csi2->crop.width = RKCIF_DEFAULT_WIDTH;
	csi2->crop.height = RKCIF_DEFAULT_HEIGHT;

	return media_entity_pads_init(&sd->entity, num_pads, csi2->pad);
}

/* csi2 accepts all fmt/size from sensor */
static int csi2_get_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	int ret;
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
	if (!ret)
		csi2->format_mbus = fmt->format;

	return ret;
}

static struct v4l2_rect *mipi_csi2_get_crop(struct csi2_dev *csi2,
						 struct v4l2_subdev_pad_config *cfg,
						 enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&csi2->sd, cfg, RK_CSI2_PAD_SINK);
	else
		return &csi2->crop;
}

static int csi2_get_selection(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_selection *sel)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	struct v4l2_subdev_format fmt;
	int ret = 0;

	if (!sel) {
		v4l2_dbg(1, csi2_debug, &csi2->sd, "sel is null\n");
		goto err;
	}

	if (sel->pad > RK_CSI2X_PAD_SOURCE3) {
		v4l2_dbg(1, csi2_debug, &csi2->sd, "pad[%d] isn't matched\n", sel->pad);
		goto err;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			ret = v4l2_subdev_call(sensor, pad, get_selection,
					       cfg, sel);
			if (ret) {
				fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
				ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, &fmt);
				if (!ret) {
					csi2->format_mbus = fmt.format;
					sel->r.top = 0;
					sel->r.left = 0;
					sel->r.width = csi2->format_mbus.width;
					sel->r.height = csi2->format_mbus.height;
					csi2->crop = sel->r;
				} else {
					sel->r = csi2->crop;
				}
			} else {
				csi2->crop = sel->r;
			}
		} else {
			sel->r = *v4l2_subdev_get_try_crop(&csi2->sd, cfg, sel->pad);
		}
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *mipi_csi2_get_crop(csi2, cfg, sel->which);
		break;

	default:
		return -EINVAL;
	}

	return 0;
err:
	return -EINVAL;
}

static int csi2_set_selection(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_selection *sel)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *sensor = get_remote_sensor(sd);
	int ret = 0;

	ret = v4l2_subdev_call(sensor, pad, set_selection,
			       cfg, sel);
	if (!ret)
		csi2->crop = sel->r;

	return ret;
}

static int csi2_g_mbus_config(struct v4l2_subdev *sd,
			      struct v4l2_mbus_config *mbus)
{
	struct csi2_dev *csi2 = sd_to_dev(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	int ret;

	ret = v4l2_subdev_call(sensor_sd, video, g_mbus_config, mbus);
	if (ret) {
		mbus->type = V4L2_MBUS_CSI2;
		mbus->flags = csi2->bus.flags;
		mbus->flags |= BIT(csi2->bus.num_data_lanes - 1);
	}

	return 0;
}

static const struct media_entity_operations csi2_entity_ops = {
	.link_setup = csi2_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

void rkcif_csi2_event_inc_sof(void)
{
	if (g_csi2_dev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_FRAME_SYNC,
			.u.frame_sync.frame_sequence =
				atomic_inc_return(&g_csi2_dev->frm_sync_seq) - 1,
		};
		v4l2_event_queue(g_csi2_dev->sd.devnode, &event);
	}
}

u32 rkcif_csi2_get_sof(void)
{
	if (g_csi2_dev) {
		return atomic_read(&g_csi2_dev->frm_sync_seq) - 1;
	}

	return 0;
}

void rkcif_csi2_set_sof(u32 seq)
{
	if (g_csi2_dev) {
		atomic_set(&g_csi2_dev->frm_sync_seq, seq);
	}
}

static int rkcif_csi2_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
					     struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static const struct v4l2_subdev_core_ops csi2_core_ops = {
	.subscribe_event = rkcif_csi2_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops csi2_video_ops = {
	.g_mbus_config = csi2_g_mbus_config,
	.s_stream = csi2_s_stream,
};

static const struct v4l2_subdev_pad_ops csi2_pad_ops = {
	.get_fmt = csi2_get_set_fmt,
	.set_fmt = csi2_get_set_fmt,
	.get_selection = csi2_get_selection,
	.set_selection = csi2_set_selection,
};

static const struct v4l2_subdev_ops csi2_subdev_ops = {
	.core = &csi2_core_ops,
	.video = &csi2_video_ops,
	.pad = &csi2_pad_ops,
};

static int csi2_parse_endpoint(struct device *dev,
			       struct v4l2_fwnode_endpoint *vep,
			       struct v4l2_async_subdev *asd)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct csi2_dev *csi2 = sd_to_dev(sd);

	if (vep->base.port != 0) {
		dev_err(dev, "The csi host node needs to parse port 0\n");
		return -EINVAL;
	}

	csi2->bus = vep->bus.mipi_csi2;

	return 0;
}

/* The .bound() notifier callback when a match is found */
static int
csi2_notifier_bound(struct v4l2_async_notifier *notifier,
		    struct v4l2_subdev *sd,
		    struct v4l2_async_subdev *asd)
{
	struct csi2_dev *csi2 = container_of(notifier,
			struct csi2_dev,
			notifier);
	struct csi2_sensor *sensor;
	struct media_link *link;
	unsigned int pad, ret;

	if (csi2->num_sensors == ARRAY_SIZE(csi2->sensors)) {
		v4l2_err(&csi2->sd,
			 "%s: the num of sd is beyond:%d\n",
			 __func__, csi2->num_sensors);
		return -EBUSY;
	}
	sensor = &csi2->sensors[csi2->num_sensors++];
	sensor->sd = sd;

	for (pad = 0; pad < sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags
					& MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(csi2->dev,
			"failed to find src pad for %s\n",
			sd->name);

		return -ENXIO;
	}

	ret = media_create_pad_link(&sensor->sd->entity, pad,
				    &csi2->sd.entity, RK_CSI2_PAD_SINK,
				    0/* csi2->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED */);
	if (ret) {
		dev_err(csi2->dev,
			"failed to create link for %s\n",
			sd->name);
		return ret;
	}

	link = list_first_entry(&csi2->sd.entity.links, struct media_link, list);
	ret = media_entity_setup_link(link, MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi2->dev,
			"failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	return 0;
}

/* The .unbind callback */
static void csi2_notifier_unbind(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_subdev *asd)
{
	struct csi2_dev *csi2 = container_of(notifier,
						  struct csi2_dev,
						  notifier);
	struct csi2_sensor *sensor = sd_to_sensor(csi2, sd);

	sensor->sd = NULL;

}

static const struct
v4l2_async_notifier_operations csi2_async_ops = {
	.bound = csi2_notifier_bound,
	.unbind = csi2_notifier_unbind,
};

static irqreturn_t rk_csirx_irq1_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct csi2_dev *csi2 = sd_to_dev(dev_get_drvdata(dev));
	struct csi2_err_stats *err_list = NULL;
	unsigned long err_stat = 0;
	u32 val;

	val = read_csihost_reg(csi2->base, CSIHOST_ERR1);
	if (val) {
		write_csihost_reg(csi2->base,
				  CSIHOST_ERR1, 0x0);

		if (val & CSIHOST_ERR1_PHYERR_SPTSYNCHS) {
			err_list = &csi2->err_list[RK_CSI2_ERR_SOTSYN];
			err_list->cnt++;
			v4l2_err(&csi2->sd,
				 "ERR1: start of transmission error(no synchronization achieved), reg: 0x%x,cnt:%d\n",
				 val, err_list->cnt);
		}

		if (val & CSIHOST_ERR1_ERR_BNDRY_MATCH) {
			err_list = &csi2->err_list[RK_CSI2_ERR_FS_FE_MIS];
			err_list->cnt++;
			v4l2_err(&csi2->sd,
				 "ERR1: error matching frame start with frame end, reg: 0x%x,cnt:%d\n",
				 val, err_list->cnt);
		}

		if (val & CSIHOST_ERR1_ERR_SEQ) {
			err_list = &csi2->err_list[RK_CSI2_ERR_FRM_SEQ_ERR];
			err_list->cnt++;
			v4l2_err(&csi2->sd,
				 "ERR1: incorrect frame sequence detected, reg: 0x%x,cnt:%d\n",
				 val, err_list->cnt);
		}

		if (val & CSIHOST_ERR1_ERR_FRM_DATA) {
			err_list = &csi2->err_list[RK_CSI2_ERR_CRC_ONCE];
			err_list->cnt++;
			v4l2_dbg(1, csi2_debug, &csi2->sd,
				 "ERR1: at least one crc error, reg: 0x%x\n,cnt:%d", val, err_list->cnt);
		}

		if (val & CSIHOST_ERR1_ERR_CRC) {
			err_list = &csi2->err_list[RK_CSI2_ERR_CRC];
			err_list->cnt++;
			v4l2_err(&csi2->sd,
				 "ERR1: crc errors, reg: 0x%x, cnt:%d\n",
				 val, err_list->cnt);
		}

		csi2->err_list[RK_CSI2_ERR_ALL].cnt++;
		err_stat = ((csi2->err_list[RK_CSI2_ERR_FS_FE_MIS].cnt & 0xff) << 8) |
			    ((csi2->err_list[RK_CSI2_ERR_ALL].cnt) & 0xff);

		atomic_notifier_call_chain(&g_csi_host_chain,
					   err_stat,
					   NULL);

	}

	return IRQ_HANDLED;
}

static irqreturn_t rk_csirx_irq2_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct csi2_dev *csi2 = sd_to_dev(dev_get_drvdata(dev));
	u32 val;

	val = read_csihost_reg(csi2->base, CSIHOST_ERR2);
	if (val) {
		if (val & CSIHOST_ERR2_PHYERR_ESC)
			v4l2_err(&csi2->sd, "ERR2: escape entry error(ULPM), reg: 0x%x\n", val);
		if (val & CSIHOST_ERR2_PHYERR_SOTHS)
			v4l2_err(&csi2->sd,
				 "ERR2: start of transmission error(synchronization can still be achieved), reg: 0x%x\n",
				 val);
		if (val & CSIHOST_ERR2_ECC_CORRECTED)
			v4l2_dbg(1, csi2_debug, &csi2->sd,
				 "ERR2: header error detected and corrected, reg: 0x%x\n",
				 val);
		if (val & CSIHOST_ERR2_ERR_ID)
			v4l2_err(&csi2->sd,
				 "ERR2: unrecognized or unimplemented data type detected, reg: 0x%x\n",
				 val);
		if (val & CSIHOST_ERR2_PHYERR_CODEHS)
			v4l2_err(&csi2->sd, "ERR2: receiv error code, reg: 0x%x\n", val);
	}

	return IRQ_HANDLED;
}

static int csi2_notifier(struct csi2_dev *csi2)
{
	struct v4l2_async_notifier *ntf = &csi2->notifier;
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(csi2->dev,
								 &csi2->notifier,
								 sizeof(struct v4l2_async_subdev), 0,
								 csi2_parse_endpoint);
	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	csi2->sd.subdev_notifier = &csi2->notifier;
	csi2->notifier.ops = &csi2_async_ops;
	ret = v4l2_async_subdev_notifier_register(&csi2->sd, &csi2->notifier);
	if (ret) {
		v4l2_err(&csi2->sd,
			 "failed to register async notifier : %d\n",
			 ret);
		v4l2_async_notifier_cleanup(&csi2->notifier);
		return ret;
	}

	ret = v4l2_async_register_subdev(&csi2->sd);

	return ret;
}

static const struct csi2_match_data rk1808_csi2_match_data = {
	.chip_id = CHIP_RK1808_CSI2,
	.num_pads = CSI2_NUM_PADS,
};

static const struct csi2_match_data rk3288_csi2_match_data = {
	.chip_id = CHIP_RK3288_CSI2,
	.num_pads = CSI2_NUM_PADS_SINGLE_LINK,
};

static const struct csi2_match_data rv1126_csi2_match_data = {
	.chip_id = CHIP_RV1126_CSI2,
	.num_pads = CSI2_NUM_PADS,
};

static const struct csi2_match_data rk3568_csi2_match_data = {
	.chip_id = CHIP_RK3568_CSI2,
	.num_pads = CSI2_NUM_PADS,
};

static const struct of_device_id csi2_dt_ids[] = {
	{
		.compatible = "rockchip,rk1808-mipi-csi2",
		.data = &rk1808_csi2_match_data,
	},
	{
		.compatible = "rockchip,rk3288-mipi-csi2",
		.data = &rk3288_csi2_match_data,
	},
	{
		.compatible = "rockchip,rk3568-mipi-csi2",
		.data = &rk3568_csi2_match_data,
	},
	{
		.compatible = "rockchip,rv1126-mipi-csi2",
		.data = &rv1126_csi2_match_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, csi2_dt_ids);

static int csi2_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct csi2_dev *csi2 = NULL;
	struct resource *res;
	const struct csi2_match_data *data;
	int ret, irq;

	match = of_match_node(csi2_dt_ids, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
	data = match->data;

	csi2 = devm_kzalloc(&pdev->dev, sizeof(*csi2), GFP_KERNEL);
	if (!csi2)
		return -ENOMEM;

	csi2->dev = &pdev->dev;
	csi2->match_data = data;

	v4l2_subdev_init(&csi2->sd, &csi2_subdev_ops);
	v4l2_set_subdevdata(&csi2->sd, &pdev->dev);
	csi2->sd.entity.ops = &csi2_entity_ops;
	csi2->sd.dev = &pdev->dev;
	csi2->sd.owner = THIS_MODULE;
	csi2->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	ret = strscpy(csi2->sd.name, DEVICE_NAME, sizeof(csi2->sd.name));
	if (ret < 0)
		v4l2_err(&csi2->sd, "failed to copy name\n");
	platform_set_drvdata(pdev, &csi2->sd);

	csi2->clks_num = devm_clk_bulk_get_all(dev, &csi2->clks_bulk);
	if (csi2->clks_num < 0)
		dev_err(dev, "failed to get csi2 clks\n");

	csi2->rsts_bulk = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(csi2->rsts_bulk)) {
		if (PTR_ERR(csi2->rsts_bulk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get csi2 reset\n");
		return PTR_ERR(csi2->rsts_bulk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	csi2->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(csi2->base)) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		dev_warn(&pdev->dev, "avoid secondary mipi resource check!\n");

		csi2->base = devm_ioremap(&pdev->dev, offset, size);
		if (IS_ERR(csi2->base)) {
			dev_err(&pdev->dev, "Failed to ioremap resource\n");

			return PTR_ERR(csi2->base);
		}
	}

	irq = platform_get_irq_byname(pdev, "csi-intr1");
	if (irq > 0) {
		ret = devm_request_irq(&pdev->dev, irq,
				       rk_csirx_irq1_handler, 0,
				       dev_driver_string(&pdev->dev),
				       &pdev->dev);
		if (ret < 0)
			v4l2_err(&csi2->sd, "request csi-intr1 irq failed: %d\n",
				 ret);
	} else {
		v4l2_err(&csi2->sd, "No found irq csi-intr1\n");
	}

	irq = platform_get_irq_byname(pdev, "csi-intr2");
	if (irq > 0) {
		ret = devm_request_irq(&pdev->dev, irq,
				       rk_csirx_irq2_handler, 0,
				       dev_driver_string(&pdev->dev),
				       &pdev->dev);
		if (ret < 0)
			v4l2_err(&csi2->sd, "request csi-intr2 failed: %d\n",
				 ret);
	} else {
		v4l2_err(&csi2->sd, "No found irq csi-intr2\n");
	}

	mutex_init(&csi2->lock);

	ret = csi2_media_init(&csi2->sd);
	if (ret < 0)
		goto rmmutex;
	ret = csi2_notifier(csi2);
	if (ret)
		goto rmmutex;

	csi2_hw_do_reset(csi2);

	g_csi2_dev = csi2;

	v4l2_info(&csi2->sd, "probe success, v4l2_dev:%s!\n", csi2->sd.v4l2_dev->name);

	return 0;

rmmutex:
	mutex_destroy(&csi2->lock);
	return ret;
}

static int csi2_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct csi2_dev *csi2 = sd_to_dev(sd);

	v4l2_async_unregister_subdev(sd);
	mutex_destroy(&csi2->lock);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static struct platform_driver csi2_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = csi2_dt_ids,
	},
	.probe = csi2_probe,
	.remove = csi2_remove,
};

#ifdef MODULE
int __init rkcif_csi2_plat_drv_init(void)
{
	return platform_driver_register(&csi2_driver);
}
#else
module_platform_driver(csi2_driver);
#endif

MODULE_DESCRIPTION("Rockchip MIPI CSI2 driver");
MODULE_AUTHOR("Macrofly.xu <xuhf@rock-chips.com>");
MODULE_LICENSE("GPL");
