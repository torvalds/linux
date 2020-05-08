// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include <linux/iommu.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include "dev.h"

#define RKCIF_VERNO_LEN		10

struct cif_match_data {
	int chip_id;
	const char * const *clks;
	const char * const *rsts;
	int clks_num;
	int rsts_num;
	const struct cif_reg *cif_regs;
};

int rkcif_debug;
module_param_named(debug, rkcif_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static char rkcif_version[RKCIF_VERNO_LEN];
module_param_string(version, rkcif_version, RKCIF_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

static DEFINE_MUTEX(rkcif_dev_mutex);
static LIST_HEAD(rkcif_device_list);

void rkcif_write_register(struct rkcif_device *dev,
			  enum cif_reg_index index, u32 val)
{
	void __iomem *base = dev->base_addr;
	const struct cif_reg *reg = &dev->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index == CIF_REG_DVP_CTRL ||
		    (index != CIF_REG_DVP_CTRL && reg->offset != 0x0))
			write_cif_reg(base, reg->offset, val);
		else
			dev_warn(dev->dev, "write reg[%d] failed, maybe useless!!!\n", index);
	}

	if (index == CIF_REG_DVP_VIR_LINE_WIDTH)
		dev_warn(dev->dev, "reg[%d] offset:0x%x, val:0x%x\n",
			 index, reg->offset, val);
}

unsigned int rkcif_read_register(struct rkcif_device *dev,
				 enum cif_reg_index index)
{
	unsigned int val = 0x0;
	void __iomem *base = dev->base_addr;
	const struct cif_reg *reg = &dev->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index == CIF_REG_DVP_CTRL ||
		    (index != CIF_REG_DVP_CTRL && reg->offset != 0x0))
			val = read_cif_reg(base, reg->offset);
		else
			dev_warn(dev->dev, "read reg[%d] failed, maybe useless!!!\n", index);
	}

	return val;
}

/**************************** pipeline operations *****************************/

static int __cif_pipeline_prepare(struct rkcif_pipeline *p,
				  struct media_entity *me)
{
	struct v4l2_subdev *sd;
	int i;

	p->num_subdevs = 0;
	memset(p->subdevs, 0, sizeof(p->subdevs));

	while (1) {
		struct media_pad *pad = NULL;

		/* Find remote source pad */
		for (i = 0; i < me->num_pads; i++) {
			struct media_pad *spad = &me->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_entity_remote_pad(spad);
			if (pad)
				break;
		}

		if (!pad)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		p->subdevs[p->num_subdevs++] = sd;
		me = &sd->entity;
		if (me->num_pads == 1)
			break;
	}

	return 0;
}

static int __cif_pipeline_s_cif_clk(struct rkcif_pipeline *p)
{
	return 0;
}

static int rkcif_pipeline_open(struct rkcif_pipeline *p,
			       struct media_entity *me,
				bool prepare)
{
	int ret;

	if (WARN_ON(!p || !me))
		return -EINVAL;
	if (atomic_inc_return(&p->power_cnt) > 1)
		return 0;

	/* go through media graphic and get subdevs */
	if (prepare)
		__cif_pipeline_prepare(p, me);

	if (!p->num_subdevs)
		return -EINVAL;

	ret = __cif_pipeline_s_cif_clk(p);
	if (ret < 0)
		return ret;

	return 0;
}

static int rkcif_pipeline_close(struct rkcif_pipeline *p)
{
	atomic_dec_return(&p->power_cnt);

	return 0;
}

/*
 * stream-on order: isp_subdev, mipi dphy, sensor
 * stream-off order: mipi dphy, sensor, isp_subdev
 */
static int rkcif_pipeline_set_stream(struct rkcif_pipeline *p, bool on)
{
	int i, ret;

	if ((on && atomic_inc_return(&p->stream_cnt) > 1) ||
	    (!on && atomic_dec_return(&p->stream_cnt) > 0))
		return 0;

	if (on)
		rockchip_set_system_status(SYS_STATUS_CIF0);

	/* phy -> sensor */
	for (i = p->num_subdevs - 1; i > -1; --i) {
		ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
		if (on && ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			goto err_stream_off;
	}

	if (!on)
		rockchip_clear_system_status(SYS_STATUS_CIF0);

	return 0;

err_stream_off:
	for (--i; i >= 0; --i)
		v4l2_subdev_call(p->subdevs[i], video, s_stream, false);
	rockchip_clear_system_status(SYS_STATUS_CIF0);
	return ret;
}

/***************************** media controller *******************************/
static int rkcif_create_links(struct rkcif_device *dev)
{
	int ret;
	u32 flags;
	unsigned int s, pad, id, stream_num = 0;

	if (dev->chip_id == CHIP_RK1808_CIF)
		stream_num = RKCIF_MULTI_STREAMS_NUM;
	else
		stream_num = RKCIF_SINGLE_STREAM;

	/* sensor links(or mipi-phy) */
	for (s = 0; s < dev->num_sensors; ++s) {
		struct rkcif_sensor_info *sensor = &dev->sensors[s];
		struct media_entity *source_entity, *sink_entity;

		for (pad = 0; pad < sensor->sd->entity.num_pads; pad++) {
			if (sensor->sd->entity.pads[pad].flags &
				MEDIA_PAD_FL_SOURCE) {
				if (pad == sensor->sd->entity.num_pads) {
					dev_err(dev->dev,
						"failed to find src pad for %s\n",
						sensor->sd->name);

					break;
				}

				if ((sensor->mbus.type == V4L2_MBUS_BT656 ||
				     sensor->mbus.type == V4L2_MBUS_PARALLEL) &&
				    dev->chip_id == CHIP_RK1808_CIF) {
					source_entity = &sensor->sd->entity;
					sink_entity = &dev->stream[RKCIF_STREAM_DVP].vnode.vdev.entity;

					ret = media_create_pad_link(source_entity,
								    pad,
								    sink_entity,
								    0,
								    MEDIA_LNK_FL_ENABLED);
					if (ret)
						dev_err(dev->dev, "failed to create link for %s\n",
							sensor->sd->name);
					break;
				}

				for (id = 0; id < stream_num; id++) {
					source_entity = &sensor->sd->entity;
					sink_entity = &dev->stream[id].vnode.vdev.entity;

					(dev->chip_id != CHIP_RK1808_CIF) | (id == pad - 1) ?
					(flags = MEDIA_LNK_FL_ENABLED) : (flags = 0);

					ret = media_create_pad_link(source_entity,
								    pad,
								    sink_entity,
								    0,
								    flags);
					if (ret) {
						dev_err(dev->dev,
							"failed to create link for %s\n",
							sensor->sd->name);
						break;
					}
				}
			}
		}
	}

	return 0;
}

static int _set_pipeline_default_fmt(struct rkcif_device *dev)
{
	return 0;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkcif_device *dev;
	int ret;

	dev = container_of(notifier, struct rkcif_device, notifier);

	/* mutex_lock(&dev->media_dev.graph_mutex); */

	ret = rkcif_create_links(dev);
	if (ret < 0)
		goto unlock;

	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unlock;

	ret = _set_pipeline_default_fmt(dev);
	if (ret < 0)
		goto unlock;
	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");

unlock:
	/* mutex_unlock(&dev->media_dev.graph_mutex); */
	return ret;
}

struct rkcif_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct rkcif_device *cif_dev = container_of(notifier,
					struct rkcif_device, notifier);
	struct rkcif_async_subdev *s_asd = container_of(asd,
					struct rkcif_async_subdev, asd);

	if (cif_dev->num_sensors == ARRAY_SIZE(cif_dev->sensors))
		return -EBUSY;

	cif_dev->sensors[cif_dev->num_sensors].lanes = s_asd->lanes;
	cif_dev->sensors[cif_dev->num_sensors].mbus = s_asd->mbus;
	cif_dev->sensors[cif_dev->num_sensors].sd = subdev;
	++cif_dev->num_sensors;

	v4l2_dbg(1, rkcif_debug, subdev, "Async registered subdev\n");

	return 0;
}

static int rkcif_fwnode_parse(struct device *dev,
			      struct v4l2_fwnode_endpoint *vep,
			      struct v4l2_async_subdev *asd)
{
	struct rkcif_async_subdev *rk_asd =
			container_of(asd, struct rkcif_async_subdev, asd);
	struct v4l2_fwnode_bus_parallel *bus = &vep->bus.parallel;

	if (vep->bus_type != V4L2_MBUS_BT656 &&
	    vep->bus_type != V4L2_MBUS_PARALLEL &&
	    vep->bus_type != V4L2_MBUS_CSI2)
		return 0;

	rk_asd->mbus.type = vep->bus_type;

	if (vep->bus_type == V4L2_MBUS_CSI2) {
		rk_asd->mbus.flags = vep->bus.mipi_csi2.flags;
		rk_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
	} else {
		rk_asd->mbus.flags = bus->flags;
	}

	return 0;
}

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.complete = subdev_notifier_complete,
};

static int cif_subdev_notifier(struct rkcif_device *cif_dev)
{
	struct v4l2_async_notifier *ntf = &cif_dev->notifier;
	struct device *dev = cif_dev->dev;
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints(dev, ntf,
							 sizeof(struct rkcif_async_subdev), rkcif_fwnode_parse);

	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	ntf->ops = &subdev_notifier_ops;

	return v4l2_async_notifier_register(&cif_dev->v4l2_dev, ntf);
}

/***************************** platform deive *******************************/

static int rkcif_register_platform_subdevs(struct rkcif_device *cif_dev)
{
	int stream_num = 0, ret;

	if (cif_dev->chip_id == CHIP_RK1808_CIF) {
		stream_num = RKCIF_MULTI_STREAMS_NUM;
		ret = rkcif_register_stream_vdevs(cif_dev, stream_num,
						  true);
	} else {
		stream_num = RKCIF_SINGLE_STREAM;
		ret = rkcif_register_stream_vdevs(cif_dev, stream_num,
						  false);
	}

	if (ret < 0) {
			dev_err(cif_dev->dev, "cif register stream[%d] failed!\n", stream_num);
			return -EINVAL;
	}

	ret = cif_subdev_notifier(cif_dev);
	if (ret < 0) {
		v4l2_err(&cif_dev->v4l2_dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		goto err_unreg_stream_vdev;
	}

	return 0;
err_unreg_stream_vdev:
	rkcif_unregister_stream_vdevs(cif_dev, stream_num);

	return ret;
}

static const struct cif_reg px30_cif_regs[] = {
	[CIF_REG_DVP_CTRL] = CIF_REG(CIF_CTRL),
	[CIF_REG_DVP_INTEN] = CIF_REG(CIF_INTEN),
	[CIF_REG_DVP_INTSTAT] = CIF_REG(CIF_INTSTAT),
	[CIF_REG_DVP_FOR] = CIF_REG(CIF_FOR),
	[CIF_REG_DVP_LINE_NUM_ADDR] = CIF_REG(CIF_LINE_NUM_ADDR),
	[CIF_REG_DVP_FRM0_ADDR_Y] = CIF_REG(CIF_FRM0_ADDR_Y),
	[CIF_REG_DVP_FRM0_ADDR_UV] = CIF_REG(CIF_FRM0_ADDR_UV),
	[CIF_REG_DVP_FRM1_ADDR_Y] = CIF_REG(CIF_FRM1_ADDR_Y),
	[CIF_REG_DVP_FRM1_ADDR_UV] = CIF_REG(CIF_FRM1_ADDR_UV),
	[CIF_REG_DVP_VIR_LINE_WIDTH] = CIF_REG(CIF_VIR_LINE_WIDTH),
	[CIF_REG_DVP_SET_SIZE] = CIF_REG(CIF_SET_SIZE),
	[CIF_REG_DVP_SCM_ADDR_Y] = CIF_REG(CIF_SCM_ADDR_Y),
	[CIF_REG_DVP_SCM_ADDR_U] = CIF_REG(CIF_SCM_ADDR_U),
	[CIF_REG_DVP_SCM_ADDR_V] = CIF_REG(CIF_SCM_ADDR_V),
	[CIF_REG_DVP_WB_UP_FILTER] = CIF_REG(CIF_WB_UP_FILTER),
	[CIF_REG_DVP_WB_LOW_FILTER] = CIF_REG(CIF_WB_LOW_FILTER),
	[CIF_REG_DVP_WBC_CNT] = CIF_REG(CIF_WBC_CNT),
	[CIF_REG_DVP_CROP] = CIF_REG(CIF_CROP),
	[CIF_REG_DVP_SCL_CTRL] = CIF_REG(CIF_SCL_CTRL),
	[CIF_REG_DVP_SCL_DST] = CIF_REG(CIF_SCL_DST),
	[CIF_REG_DVP_SCL_FCT] = CIF_REG(CIF_SCL_FCT),
	[CIF_REG_DVP_SCL_VALID_NUM] = CIF_REG(CIF_SCL_VALID_NUM),
	[CIF_REG_DVP_LINE_LOOP_CTRL] = CIF_REG(CIF_LINE_LOOP_CTR),
	[CIF_REG_DVP_FRAME_STATUS] = CIF_REG(CIF_FRAME_STATUS),
	[CIF_REG_DVP_CUR_DST] = CIF_REG(CIF_CUR_DST),
	[CIF_REG_DVP_LAST_LINE] = CIF_REG(CIF_LAST_LINE),
	[CIF_REG_DVP_LAST_PIX] = CIF_REG(CIF_LAST_PIX),
};

static const char * const px30_cif_clks[] = {
	"aclk_cif",
	"hclk_cif",
	"pclk_cif",
	"cif_out",
};

static const char * const px30_cif_rsts[] = {
	"rst_cif_a",
	"rst_cif_h",
	"rst_cif_pclkin",
};

/* TODO: define registers related to mipi */
static const struct cif_reg rk1808_cif_regs[] = {
	[CIF_REG_DVP_CTRL] = CIF_REG(CIF_CTRL),
	[CIF_REG_DVP_INTEN] = CIF_REG(CIF_INTEN),
	[CIF_REG_DVP_INTSTAT] = CIF_REG(CIF_INTSTAT),
	[CIF_REG_DVP_FOR] = CIF_REG(CIF_FOR),
	[CIF_REG_DVP_DMA_IDLE_REQ] = CIF_REG(CIF_DMA_IDLE_REQ),
	[CIF_REG_DVP_FRM0_ADDR_Y] = CIF_REG(CIF_FRM0_ADDR_Y),
	[CIF_REG_DVP_FRM0_ADDR_UV] = CIF_REG(CIF_FRM0_ADDR_UV),
	[CIF_REG_DVP_FRM1_ADDR_Y] = CIF_REG(CIF_FRM1_ADDR_Y),
	[CIF_REG_DVP_FRM1_ADDR_UV] = CIF_REG(CIF_FRM1_ADDR_UV),
	[CIF_REG_DVP_VIR_LINE_WIDTH] = CIF_REG(CIF_VIR_LINE_WIDTH),
	[CIF_REG_DVP_SET_SIZE] = CIF_REG(CIF_SET_SIZE),
	[CIF_REG_DVP_LINE_INT_NUM] = CIF_REG(CIF_LINE_INT_NUM),
	[CIF_REG_DVP_CROP] = CIF_REG(CIF_CROP),
	[CIF_REG_DVP_PATH_SEL] = CIF_REG(CIF_PATH_SEL),
	[CIF_REG_DVP_FIFO_ENTRY] = CIF_REG(CIF_FIFO_ENTRY),
	[CIF_REG_DVP_FRAME_STATUS] = CIF_REG(CIF_FRAME_STATUS),
	[CIF_REG_DVP_CUR_DST] = CIF_REG(CIF_CUR_DST),
	[CIF_REG_DVP_LAST_LINE] = CIF_REG(CIF_LAST_LINE),
	[CIF_REG_DVP_LAST_PIX] = CIF_REG(CIF_LAST_PIX),
};

static const char * const rk1808_cif_clks[] = {
	"aclk_cif",
	"dclk_cif",
	"hclk_cif",
	"sclk_cif_out",
	/* "pclk_csi2host" */
};

static const char * const rk1808_cif_rsts[] = {
	"rst_cif_a",
	"rst_cif_h",
	"rst_cif_i",
	"rst_cif_d",
	"rst_cif_pclkin",
};

static const struct cif_reg rk3128_cif_regs[] = {
	[CIF_REG_DVP_CTRL] = CIF_REG(CIF_CTRL),
	[CIF_REG_DVP_INTEN] = CIF_REG(CIF_INTEN),
	[CIF_REG_DVP_INTSTAT] = CIF_REG(CIF_INTSTAT),
	[CIF_REG_DVP_FOR] = CIF_REG(CIF_FOR),
	[CIF_REG_DVP_FRM0_ADDR_Y] = CIF_REG(CIF_FRM0_ADDR_Y),
	[CIF_REG_DVP_FRM0_ADDR_UV] = CIF_REG(CIF_FRM0_ADDR_UV),
	[CIF_REG_DVP_FRM1_ADDR_Y] = CIF_REG(CIF_FRM1_ADDR_Y),
	[CIF_REG_DVP_FRM1_ADDR_UV] = CIF_REG(CIF_FRM1_ADDR_UV),
	[CIF_REG_DVP_VIR_LINE_WIDTH] = CIF_REG(CIF_VIR_LINE_WIDTH),
	[CIF_REG_DVP_SET_SIZE] = CIF_REG(CIF_SET_SIZE),
	[CIF_REG_DVP_CROP] = CIF_REG(CIF_CROP),
	[CIF_REG_DVP_SCL_CTRL] = CIF_REG(CIF_SCL_CTRL),
	[CIF_REG_DVP_FIFO_ENTRY] = CIF_REG(CIF_FIFO_ENTRY),
	[CIF_REG_DVP_FRAME_STATUS] = CIF_REG(CIF_FRAME_STATUS),
	[CIF_REG_DVP_CUR_DST] = CIF_REG(CIF_CUR_DST),
	[CIF_REG_DVP_LAST_LINE] = CIF_REG(CIF_LAST_LINE),
	[CIF_REG_DVP_LAST_PIX] = CIF_REG(CIF_LAST_PIX),
};

static const char * const rk3128_cif_clks[] = {
	"aclk_cif",
	"hclk_cif",
	"sclk_cif_out",
};

static const char * const rk3128_cif_rsts[] = {
	"rst_cif",
};

static const struct cif_reg rk3288_cif_regs[] = {
	[CIF_REG_DVP_CTRL] = CIF_REG(CIF_CTRL),
	[CIF_REG_DVP_INTEN] = CIF_REG(CIF_INTEN),
	[CIF_REG_DVP_INTSTAT] = CIF_REG(CIF_INTSTAT),
	[CIF_REG_DVP_FOR] = CIF_REG(CIF_FOR),
	[CIF_REG_DVP_FRM0_ADDR_Y] = CIF_REG(CIF_FRM0_ADDR_Y),
	[CIF_REG_DVP_FRM0_ADDR_UV] = CIF_REG(CIF_FRM0_ADDR_UV),
	[CIF_REG_DVP_FRM1_ADDR_Y] = CIF_REG(CIF_FRM1_ADDR_Y),
	[CIF_REG_DVP_FRM1_ADDR_UV] = CIF_REG(CIF_FRM1_ADDR_UV),
	[CIF_REG_DVP_VIR_LINE_WIDTH] = CIF_REG(CIF_VIR_LINE_WIDTH),
	[CIF_REG_DVP_SET_SIZE] = CIF_REG(CIF_SET_SIZE),
	[CIF_REG_DVP_CROP] = CIF_REG(CIF_CROP),
	[CIF_REG_DVP_SCL_CTRL] = CIF_REG(CIF_SCL_CTRL),
	[CIF_REG_DVP_FIFO_ENTRY] = CIF_REG(CIF_FIFO_ENTRY),
	[CIF_REG_DVP_FRAME_STATUS] = CIF_REG(CIF_FRAME_STATUS),
	[CIF_REG_DVP_CUR_DST] = CIF_REG(CIF_CUR_DST),
	[CIF_REG_DVP_LAST_LINE] = CIF_REG(CIF_LAST_LINE),
	[CIF_REG_DVP_LAST_PIX] = CIF_REG(CIF_LAST_PIX),
};

static const char * const rk3288_cif_clks[] = {
	"aclk_cif0",
	"hclk_cif0",
	"cif0_in",
};

static const char * const rk3288_cif_rsts[] = {
	"rst_cif",
};

static const struct cif_reg rk3328_cif_regs[] = {
	[CIF_REG_DVP_CTRL] = CIF_REG(CIF_CTRL),
	[CIF_REG_DVP_INTEN] = CIF_REG(CIF_INTEN),
	[CIF_REG_DVP_INTSTAT] = CIF_REG(CIF_INTSTAT),
	[CIF_REG_DVP_FOR] = CIF_REG(CIF_FOR),
	[CIF_REG_DVP_FRM0_ADDR_Y] = CIF_REG(CIF_FRM0_ADDR_Y),
	[CIF_REG_DVP_FRM0_ADDR_UV] = CIF_REG(CIF_FRM0_ADDR_UV),
	[CIF_REG_DVP_FRM1_ADDR_Y] = CIF_REG(CIF_FRM1_ADDR_Y),
	[CIF_REG_DVP_FRM1_ADDR_UV] = CIF_REG(CIF_FRM1_ADDR_UV),
	[CIF_REG_DVP_VIR_LINE_WIDTH] = CIF_REG(CIF_VIR_LINE_WIDTH),
	[CIF_REG_DVP_SET_SIZE] = CIF_REG(CIF_SET_SIZE),
	[CIF_REG_DVP_CROP] = CIF_REG(CIF_CROP),
	[CIF_REG_DVP_SCL_CTRL] = CIF_REG(CIF_SCL_CTRL),
	[CIF_REG_DVP_FIFO_ENTRY] = CIF_REG(CIF_FIFO_ENTRY),
	[CIF_REG_DVP_FRAME_STATUS] = CIF_REG(CIF_FRAME_STATUS),
	[CIF_REG_DVP_CUR_DST] = CIF_REG(CIF_CUR_DST),
	[CIF_REG_DVP_LAST_LINE] = CIF_REG(CIF_LAST_LINE),
	[CIF_REG_DVP_LAST_PIX] = CIF_REG(CIF_LAST_PIX),
};

static const char * const rk3328_cif_clks[] = {
	"aclk_cif",
	"hclk_cif",
};

static const char * const rk3328_cif_rsts[] = {
	"rst_cif_a",
	"rst_cif_p",
	"rst_cif_h",
};

static const struct cif_match_data px30_cif_match_data = {
	.chip_id = CHIP_PX30_CIF,
	.clks = px30_cif_clks,
	.clks_num = ARRAY_SIZE(px30_cif_clks),
	.rsts = px30_cif_rsts,
	.rsts_num = ARRAY_SIZE(px30_cif_rsts),
	.cif_regs = px30_cif_regs,
};

static const struct cif_match_data rk1808_cif_match_data = {
	.chip_id = CHIP_RK1808_CIF,
	.clks = rk1808_cif_clks,
	.clks_num = ARRAY_SIZE(rk1808_cif_clks),
	.rsts = rk1808_cif_rsts,
	.rsts_num = ARRAY_SIZE(rk1808_cif_rsts),
	.cif_regs = rk1808_cif_regs,
};

static const struct cif_match_data rk3128_cif_match_data = {
	.chip_id = CHIP_RK3128_CIF,
	.clks = rk3128_cif_clks,
	.clks_num = ARRAY_SIZE(rk3128_cif_clks),
	.rsts = rk3128_cif_rsts,
	.rsts_num = ARRAY_SIZE(rk3128_cif_rsts),
	.cif_regs = rk3128_cif_regs,
};

static const struct cif_match_data rk3288_cif_match_data = {
	.chip_id = CHIP_RK3288_CIF,
	.clks = rk3288_cif_clks,
	.clks_num = ARRAY_SIZE(rk3288_cif_clks),
	.rsts = rk3288_cif_rsts,
	.rsts_num = ARRAY_SIZE(rk3288_cif_rsts),
	.cif_regs = rk3288_cif_regs,
};

static const struct cif_match_data rk3328_cif_match_data = {
	.chip_id = CHIP_RK3328_CIF,
	.clks = rk3328_cif_clks,
	.clks_num = ARRAY_SIZE(rk3328_cif_clks),
	.rsts = rk3328_cif_rsts,
	.rsts_num = ARRAY_SIZE(rk3328_cif_rsts),
	.cif_regs = rk3328_cif_regs,
};

static const struct of_device_id rkcif_plat_of_match[] = {
	{
		.compatible = "rockchip,px30-cif",
		.data = &px30_cif_match_data,
	},
	{
		.compatible = "rockchip,rk1808-cif",
		.data = &rk1808_cif_match_data,
	},
	{
		.compatible = "rockchip,rk3128-cif",
		.data = &rk3128_cif_match_data,
	},
	{
		.compatible = "rockchip,rk3288-cif",
		.data = &rk3288_cif_match_data,
	},
	{
		.compatible = "rockchip,rk3328-cif",
		.data = &rk3328_cif_match_data,
	},
	{},
};

static irqreturn_t rkcif_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);

	if (cif_dev->workmode == RKCIF_WORKMODE_PINGPONG)
		rkcif_irq_pingpong(cif_dev);
	else
		rkcif_irq_oneframe(cif_dev);

	return IRQ_HANDLED;
}

static void rkcif_disable_sys_clk(struct rkcif_device *cif_dev)
{
	int i;

	for (i = cif_dev->clk_size - 1; i >= 0; i--)
		clk_disable_unprepare(cif_dev->clks[i]);
}

static int rkcif_enable_sys_clk(struct rkcif_device *cif_dev)
{
	int i, ret = -EINVAL;

	for (i = 0; i < cif_dev->clk_size; i++) {
		ret = clk_prepare_enable(cif_dev->clks[i]);

		if (ret < 0)
			goto err;
	}

	write_cif_reg_and(cif_dev->base_addr, CIF_CSI_INTEN, 0x0);
	return 0;

err:
	for (--i; i >= 0; --i)
		clk_disable_unprepare(cif_dev->clks[i]);

	return ret;
}

static void rkcif_iommu_cleanup(struct rkcif_device *cif_dev)
{
	struct iommu_domain *domain;

	dev_err(cif_dev->dev, "%s enter\n", __func__);

	domain = iommu_get_domain_for_dev(cif_dev->dev);
	if (domain) {
#ifdef CONFIG_IOMMU_API
		domain->ops->detach_dev(domain, cif_dev->dev);
		domain->ops->attach_dev(domain, cif_dev->dev);
#endif
	}
}

static inline bool is_iommu_enable(struct device *dev)
{
	struct device_node *iommu;

	iommu = of_parse_phandle(dev->of_node, "iommus", 0);
	if (!iommu) {
		dev_info(dev, "no iommu attached, using non-iommu buffers\n");
		return false;
	} else if (!of_device_is_available(iommu)) {
		dev_info(dev, "iommu is disabled, using non-iommu buffers\n");
		of_node_put(iommu);
		return false;
	}
	of_node_put(iommu);

	return true;
}

void rkcif_soft_reset(struct rkcif_device *cif_dev, bool is_rst_iommu)
{
	unsigned int i;

	if (cif_dev->iommu_en && is_rst_iommu)
		rkcif_iommu_cleanup(cif_dev);

	for (i = 0; i < ARRAY_SIZE(cif_dev->cif_rst); i++)
		if (cif_dev->cif_rst[i])
			reset_control_assert(cif_dev->cif_rst[i]);
	udelay(5);
	for (i = 0; i < ARRAY_SIZE(cif_dev->cif_rst); i++)
		if (cif_dev->cif_rst[i])
			reset_control_deassert(cif_dev->cif_rst[i]);
}

static int rkcif_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkcif_device *cif_dev;
	const struct cif_match_data *data;
	struct resource *res;
	int i, ret, irq;

	sprintf(rkcif_version, "v%02x.%02x.%02x",
		RKCIF_DRIVER_VERSION >> 16,
		(RKCIF_DRIVER_VERSION & 0xff00) >> 8,
		RKCIF_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkcif driver version: %s\n", rkcif_version);

	match = of_match_node(rkcif_plat_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);

	cif_dev = devm_kzalloc(dev, sizeof(*cif_dev), GFP_KERNEL);
	if (!cif_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, cif_dev);
	cif_dev->dev = dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rkcif_irq_handler, IRQF_SHARED,
			       dev_driver_string(dev), dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	cif_dev->irq = irq;
	data = match->data;
	cif_dev->chip_id = data->chip_id;
	if (data->chip_id == CHIP_RK1808_CIF) {
		res = platform_get_resource_byname(pdev,
						   IORESOURCE_MEM,
						   "cif_regs");
		cif_dev->base_addr = devm_ioremap_resource(dev, res);
		if (PTR_ERR(cif_dev->base_addr) == -EBUSY) {
			resource_size_t offset = res->start;
			resource_size_t size = resource_size(res);

			cif_dev->base_addr = devm_ioremap(dev, offset, size);
			if (IS_ERR(cif_dev->base_addr)) {
				dev_err(dev, "ioremap failed\n");
				return PTR_ERR(cif_dev->base_addr);
			}
		}
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		cif_dev->base_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(cif_dev->base_addr))
			return PTR_ERR(cif_dev->base_addr);
	}

	if (data->clks_num > RKCIF_MAX_BUS_CLK ||
	    data->rsts_num > RKCIF_MAX_RESET) {
		dev_err(dev, "out of range: clks(%d %d) rsts(%d %d)\n",
			data->clks_num, RKCIF_MAX_BUS_CLK,
			data->rsts_num, RKCIF_MAX_RESET);
		return -EINVAL;
	}

	for (i = 0; i < data->clks_num; i++) {
		struct clk *clk = devm_clk_get(dev, data->clks[i]);

		if (IS_ERR(clk)) {
			dev_err(dev, "failed to get %s\n", data->clks[i]);
			return PTR_ERR(clk);
		}
		cif_dev->clks[i] = clk;
	}
	cif_dev->clk_size = data->clks_num;

	for (i = 0; i < data->rsts_num; i++) {
		struct reset_control *rst = NULL;

		if (data->rsts[i])
			rst = devm_reset_control_get(dev, data->rsts[i]);
		if (IS_ERR(rst)) {
			dev_err(dev, "failed to get %s\n", data->rsts[i]);
			return PTR_ERR(rst);
		}
		cif_dev->cif_rst[i] = rst;
	}

	cif_dev->cif_regs = data->cif_regs;

	mutex_init(&cif_dev->stream_lock);
	atomic_set(&cif_dev->pipe.power_cnt, 0);
	atomic_set(&cif_dev->pipe.stream_cnt, 0);
	atomic_set(&cif_dev->fh_cnt, 0);
	cif_dev->pipe.open = rkcif_pipeline_open;
	cif_dev->pipe.close = rkcif_pipeline_close;
	cif_dev->pipe.set_stream = rkcif_pipeline_set_stream;

	if (data->chip_id == CHIP_RK1808_CIF) {
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID0);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID1);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID2);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID3);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_DVP);
	} else {
		rkcif_stream_init(cif_dev, RKCIF_STREAM_CIF);
	}

#if defined(CONFIG_ROCKCHIP_CIF_WORKMODE_PINGPONG)
	cif_dev->workmode = RKCIF_WORKMODE_PINGPONG;
#elif defined(CONFIG_ROCKCHIP_CIF_WORKMODE_ONEFRAME)
	cif_dev->workmode = RKCIF_WORKMODE_ONEFRAME;
#else
	cif_dev->workmode = RKCIF_WORKMODE_PINGPONG;
#endif

	strlcpy(cif_dev->media_dev.model, "rkcif",
		sizeof(cif_dev->media_dev.model));
	cif_dev->media_dev.dev = &pdev->dev;
	v4l2_dev = &cif_dev->v4l2_dev;
	v4l2_dev->mdev = &cif_dev->media_dev;
	strlcpy(v4l2_dev->name, "rkcif", sizeof(v4l2_dev->name));
	v4l2_ctrl_handler_init(&cif_dev->ctrl_handler, 8);
	v4l2_dev->ctrl_handler = &cif_dev->ctrl_handler;

	ret = v4l2_device_register(cif_dev->dev, &cif_dev->v4l2_dev);
	if (ret < 0)
		return ret;

	media_device_init(&cif_dev->media_dev);
	ret = media_device_register(&cif_dev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n",
			 ret);
		goto err_unreg_v4l2_dev;
	}

	/* create & register platefom subdev (from of_node) */
	ret = rkcif_register_platform_subdevs(cif_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	cif_dev->iommu_en = is_iommu_enable(dev);
	if (!cif_dev->iommu_en) {
		ret = of_reserved_mem_device_init(dev);
		if (ret)
			v4l2_warn(v4l2_dev,
				  "No reserved memory region assign to CIF\n");
	}

	pm_runtime_enable(&pdev->dev);

	mutex_lock(&rkcif_dev_mutex);
	list_add_tail(&cif_dev->list, &rkcif_device_list);
	mutex_unlock(&rkcif_dev_mutex);

	rkcif_soft_reset(cif_dev, true);

	return 0;

err_unreg_media_dev:
	media_device_unregister(&cif_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&cif_dev->v4l2_dev);
	return ret;
}

static int rkcif_plat_remove(struct platform_device *pdev)
{
	int stream_num = 0;
	struct rkcif_device *cif_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	if (cif_dev->iommu_en)
		rkcif_iommu_cleanup(cif_dev);

	media_device_unregister(&cif_dev->media_dev);
	v4l2_device_unregister(&cif_dev->v4l2_dev);
	if (cif_dev->chip_id == CHIP_RK1808_CIF)
		stream_num = RKCIF_MULTI_STREAMS_NUM;
	else
		stream_num = RKCIF_SINGLE_STREAM;
	rkcif_unregister_stream_vdevs(cif_dev, stream_num);

	return 0;
}

static int __maybe_unused rkcif_runtime_suspend(struct device *dev)
{
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);

	rkcif_disable_sys_clk(cif_dev);

	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused rkcif_runtime_resume(struct device *dev)
{
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	rkcif_enable_sys_clk(cif_dev);

	return 0;
}

static int __init rkcif_clr_unready_dev(void)
{
	struct rkcif_device *cif_dev;

	mutex_lock(&rkcif_dev_mutex);
	list_for_each_entry(cif_dev, &rkcif_device_list, list)
		v4l2_async_notifier_clr_unready_dev(&cif_dev->notifier);
	mutex_unlock(&rkcif_dev_mutex);

	return 0;
}
late_initcall_sync(rkcif_clr_unready_dev);

static const struct dev_pm_ops rkcif_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkcif_runtime_suspend, rkcif_runtime_resume, NULL)
};

static struct platform_driver rkcif_plat_drv = {
	.driver = {
		   .name = CIF_DRIVER_NAME,
		   .of_match_table = of_match_ptr(rkcif_plat_of_match),
		   .pm = &rkcif_plat_pm_ops,
	},
	.probe = rkcif_plat_probe,
	.remove = rkcif_plat_remove,
};

module_platform_driver(rkcif_plat_drv);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip CIF platform driver");
MODULE_LICENSE("GPL v2");
