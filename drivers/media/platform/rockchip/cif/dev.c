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
#include "procfs.h"

#define RKCIF_VERNO_LEN		10

int rkcif_debug;
module_param_named(debug, rkcif_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static char rkcif_version[RKCIF_VERNO_LEN];
module_param_string(version, rkcif_version, RKCIF_VERNO_LEN, 0444);
MODULE_PARM_DESC(version, "version number");

static DEFINE_MUTEX(rkcif_dev_mutex);
static LIST_HEAD(rkcif_device_list);

/* show the compact mode of each stream in stream index order,
 * 1 for compact, 0 for 16bit
 */
static ssize_t rkcif_show_compact_mode(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct rkcif_device *cif_dev = (struct rkcif_device *)dev_get_drvdata(dev);
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d %d %d %d\n",
		       cif_dev->stream[0].is_compact ? 1 : 0,
		       cif_dev->stream[1].is_compact ? 1 : 0,
		       cif_dev->stream[2].is_compact ? 1 : 0,
		       cif_dev->stream[3].is_compact ? 1 : 0);
	return ret;
}

static ssize_t rkcif_store_compact_mode(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t len)
{
	struct rkcif_device *cif_dev = (struct rkcif_device *)dev_get_drvdata(dev);
	int i, index;
	char val[4];

	if (buf) {
		index = 0;
		for (i = 0; i < len; i++) {
			if (buf[i] == ' ') {
				continue;
			} else if (buf[i] == '\0') {
				break;
			} else {
				val[index] = buf[i];
				index++;
				if (index == 4)
					break;
			}
		}

		for (i = 0; i < index; i++) {
			if (val[i] - '0' == 0)
				cif_dev->stream[i].is_compact = false;
			else
				cif_dev->stream[i].is_compact = true;
		}
	}

	return len;
}
static DEVICE_ATTR(compact_test, S_IWUSR | S_IRUSR,
		   rkcif_show_compact_mode, rkcif_store_compact_mode);

static struct attribute *dev_attrs[] = {
	&dev_attr_compact_test.attr,
	NULL,
};

static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};

struct rkcif_match_data {
	int inf_id;
};

void rkcif_write_register(struct rkcif_device *dev,
			  enum cif_reg_index index, u32 val)
{
	void __iomem *base = dev->hw_dev->base_addr;
	const struct cif_reg *reg = &dev->hw_dev->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index == CIF_REG_DVP_CTRL ||
		    (index != CIF_REG_DVP_CTRL && reg->offset != 0x0))
			write_cif_reg(base, reg->offset, val);
		else
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "write reg[%d]:0x%x failed, maybe useless!!!\n",
				 index, val);
	}
}

void rkcif_write_register_or(struct rkcif_device *dev,
			     enum cif_reg_index index, u32 val)
{
	unsigned int reg_val = 0x0;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct cif_reg *reg = &dev->hw_dev->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index == CIF_REG_DVP_CTRL ||
		    (index != CIF_REG_DVP_CTRL && reg->offset != 0x0)) {
			reg_val = read_cif_reg(base, reg->offset);
			reg_val |= val;
			write_cif_reg(base, reg->offset, reg_val);
		} else {
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "write reg[%d]:0x%x with OR failed, maybe useless!!!\n",
				 index, val);
		}
	}
}

void rkcif_write_register_and(struct rkcif_device *dev,
			      enum cif_reg_index index, u32 val)
{
	unsigned int reg_val = 0x0;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct cif_reg *reg = &dev->hw_dev->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index == CIF_REG_DVP_CTRL ||
		    (index != CIF_REG_DVP_CTRL && reg->offset != 0x0)) {
			reg_val = read_cif_reg(base, reg->offset);
			reg_val &= val;
			write_cif_reg(base, reg->offset, reg_val);
		} else {
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "write reg[%d]:0x%x with OR failed, maybe useless!!!\n",
				 index, val);
		}
	}
}

unsigned int rkcif_read_register(struct rkcif_device *dev,
				 enum cif_reg_index index)
{
	unsigned int val = 0x0;
	void __iomem *base = dev->hw_dev->base_addr;
	const struct cif_reg *reg = &dev->hw_dev->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index == CIF_REG_DVP_CTRL ||
		    (index != CIF_REG_DVP_CTRL && reg->offset != 0x0))
			val = read_cif_reg(base, reg->offset);
		else
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "read reg[%d] failed, maybe useless!!!\n",
				 index);
	}

	return val;
}

void rkcif_write_grf_reg(struct rkcif_device *dev,
			 enum cif_reg_index index, u32 val)
{
	struct rkcif_hw *cif_hw = dev->hw_dev;
	const struct cif_reg *reg = &cif_hw->cif_regs[index];

	if (index < CIF_REG_INDEX_MAX) {
		if (index > CIF_REG_DVP_CTRL) {
			if (!IS_ERR(cif_hw->grf))
				regmap_write(cif_hw->grf, reg->offset, val);
		} else {
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "write reg[%d]:0x%x failed, maybe useless!!!\n",
				 index, val);
		}
	}
}

u32 rkcif_read_grf_reg(struct rkcif_device *dev, enum cif_reg_index index)
{
	struct rkcif_hw *cif_hw = dev->hw_dev;
	const struct cif_reg *reg = &cif_hw->cif_regs[index];
	u32 val = 0xffff;

	if (index < CIF_REG_INDEX_MAX) {
		if (index > CIF_REG_DVP_CTRL) {
			if (!IS_ERR(cif_hw->grf))
				regmap_read(cif_hw->grf, reg->offset, &val);
		} else {
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "read reg[%d] failed, maybe useless!!!\n",
				 index);
		}
	}

	return val;
}

void rkcif_config_dvp_clk_sampling_edge(struct rkcif_device *dev,
					enum rkcif_clk_edge edge)
{
	struct rkcif_hw *cif_hw = dev->hw_dev;
	u32 val = 0x0;

	if (!IS_ERR(cif_hw->grf)) {
		if (dev->chip_id == CHIP_RV1126_CIF) {
			if (edge == RKCIF_CLK_RISING)
				val = CIF_PCLK_SAMPLING_EDGE_RISING;
			else
				val = CIF_PCLK_SAMPLING_EDGE_FALLING;
		}

		if (dev->chip_id == CHIP_RK3568_CIF) {
			if (edge == RKCIF_CLK_RISING)
				val = RK3568_CIF_PCLK_SAMPLING_EDGE_RISING;
			else
				val = RK3568_CIF_PCLK_SAMPLING_EDGE_FALLING;
		}
		rkcif_write_grf_reg(dev, CIF_REG_GRF_CIFIO_CON, val);
	}
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
	struct rkcif_device *cif_dev = container_of(p, struct rkcif_device, pipe);
	bool can_be_set = false;
	int i, ret;

	if (cif_dev->hdr.mode == NO_HDR) {
		if ((on && atomic_inc_return(&p->stream_cnt) > 1) ||
		    (!on && atomic_dec_return(&p->stream_cnt) > 0))
			return 0;

		if (on) {
			rockchip_set_system_status(SYS_STATUS_CIF0);
			cif_dev->irq_stats.csi_overflow_cnt = 0;
			cif_dev->irq_stats.csi_bwidth_lack_cnt = 0;
			cif_dev->irq_stats.dvp_bus_err_cnt = 0;
			cif_dev->irq_stats.dvp_line_err_cnt = 0;
			cif_dev->irq_stats.dvp_overflow_cnt = 0;
			cif_dev->irq_stats.dvp_pix_err_cnt = 0;
			cif_dev->irq_stats.all_err_cnt = 0;
			cif_dev->irq_stats.all_frm_end_cnt = 0;
			cif_dev->reset_watchdog_timer.is_triggered = false;
			cif_dev->reset_watchdog_timer.is_running = false;
			cif_dev->reset_watchdog_timer.last_buf_wakeup_cnt = 0;
			cif_dev->reset_watchdog_timer.run_cnt = 0;
			cif_dev->buf_wake_up_cnt = 0;
		}

		/* phy -> sensor */
		for (i = 0; i < p->num_subdevs; i++) {
			ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
			if (on && ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
				goto err_stream_off;
		}
	} else {
		if (!on && atomic_dec_return(&p->stream_cnt) > 0)
			return 0;

		if (on) {
			atomic_inc(&p->stream_cnt);
			if (cif_dev->hdr.mode == HDR_X2) {
				if (atomic_read(&p->stream_cnt) == 1) {
					rockchip_set_system_status(SYS_STATUS_CIF0);
					can_be_set = false;
				} else if (atomic_read(&p->stream_cnt) == 2) {
					can_be_set = true;
				}
			} else if (cif_dev->hdr.mode == HDR_X3) {
				if (atomic_read(&p->stream_cnt) == 1) {
					rockchip_set_system_status(SYS_STATUS_CIF0);
					can_be_set = false;
				} else if (atomic_read(&p->stream_cnt) == 3) {
					can_be_set = true;
				}
			}
		}

		if ((on && can_be_set) || !on) {
			if (on) {
				cif_dev->irq_stats.csi_overflow_cnt = 0;
				cif_dev->irq_stats.csi_bwidth_lack_cnt = 0;
				cif_dev->irq_stats.dvp_bus_err_cnt = 0;
				cif_dev->irq_stats.dvp_line_err_cnt = 0;
				cif_dev->irq_stats.dvp_overflow_cnt = 0;
				cif_dev->irq_stats.dvp_pix_err_cnt = 0;
				cif_dev->irq_stats.all_err_cnt = 0;
				cif_dev->irq_stats.all_frm_end_cnt = 0;
				cif_dev->is_start_hdr = true;
				cif_dev->reset_watchdog_timer.is_triggered = false;
				cif_dev->reset_watchdog_timer.is_running = false;
				cif_dev->reset_watchdog_timer.last_buf_wakeup_cnt = 0;
				cif_dev->reset_watchdog_timer.run_cnt = 0;
				cif_dev->buf_wake_up_cnt = 0;
			}

			/* phy -> sensor */
			for (i = 0; i < p->num_subdevs; i++) {
				ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);

				if (on && ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
					goto err_stream_off;
			}
		}
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
	bool mipi_lvds_linked = false;

	if (dev->inf_id == RKCIF_MIPI_LVDS)
		stream_num = RKCIF_MAX_STREAM_MIPI;
	else
		stream_num = RKCIF_SINGLE_STREAM;

	/* sensor links(or mipi-phy) */
	for (s = 0; s < dev->num_sensors; ++s) {
		struct rkcif_sensor_info *sensor = &dev->sensors[s];
		struct rkcif_sensor_info linked_sensor;
		struct media_entity *source_entity, *sink_entity;

		linked_sensor.lanes = sensor->lanes;

		if (sensor->mbus.type == V4L2_MBUS_CCP2) {
			linked_sensor.sd = &dev->lvds_subdev.sd;
			dev->lvds_subdev.sensor_self.sd = &dev->lvds_subdev.sd;
			dev->lvds_subdev.sensor_self.lanes = sensor->lanes;
			memcpy(&dev->lvds_subdev.sensor_self.mbus, &sensor->mbus,
			       sizeof(struct v4l2_mbus_config));
		} else {
			linked_sensor.sd = sensor->sd;
		}

		memcpy(&linked_sensor.mbus, &sensor->mbus,
		       sizeof(struct v4l2_mbus_config));

		for (pad = 0; pad < linked_sensor.sd->entity.num_pads; pad++) {
			if (linked_sensor.sd->entity.pads[pad].flags &
				MEDIA_PAD_FL_SOURCE) {
				if (pad == linked_sensor.sd->entity.num_pads) {
					dev_err(dev->dev,
						"failed to find src pad for %s\n",
						linked_sensor.sd->name);

					break;
				}

				if ((linked_sensor.mbus.type == V4L2_MBUS_BT656 ||
				     linked_sensor.mbus.type == V4L2_MBUS_PARALLEL) &&
				    (dev->chip_id == CHIP_RK1808_CIF ||
				     dev->chip_id == CHIP_RV1126_CIF ||
				     dev->chip_id == CHIP_RK3568_CIF)) {
					source_entity = &linked_sensor.sd->entity;
					sink_entity = &dev->stream[RKCIF_STREAM_CIF].vnode.vdev.entity;

					ret = media_create_pad_link(source_entity,
								    pad,
								    sink_entity,
								    0,
								    MEDIA_LNK_FL_ENABLED);
					if (ret)
						dev_err(dev->dev, "failed to create link for %s\n",
							linked_sensor.sd->name);
					break;
				}

				for (id = 0; id < stream_num; id++) {
					source_entity = &linked_sensor.sd->entity;
					sink_entity = &dev->stream[id].vnode.vdev.entity;

					if ((dev->chip_id != CHIP_RK1808_CIF &&
					     dev->chip_id != CHIP_RV1126_CIF &&
					     dev->chip_id != CHIP_RV1126_CIF_LITE &&
					     dev->chip_id != CHIP_RK3568_CIF) ||
					    (id == pad - 1 && !mipi_lvds_linked))
						flags = MEDIA_LNK_FL_ENABLED;
					else
						flags = 0;

					ret = media_create_pad_link(source_entity,
								    pad,
								    sink_entity,
								    0,
								    flags);
					if (ret) {
						dev_err(dev->dev,
							"failed to create link for %s\n",
							linked_sensor.sd->name);
						break;
					}
				}
			}
		}

		if (sensor->mbus.type == V4L2_MBUS_CCP2) {
			source_entity = &sensor->sd->entity;
			sink_entity = &linked_sensor.sd->entity;
			ret = media_create_pad_link(source_entity,
						    1,
						    sink_entity,
						    0,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				dev_err(dev->dev, "failed to create link between %s and %s\n",
					linked_sensor.sd->name,
					sensor->sd->name);
		}

		if (linked_sensor.mbus.type != V4L2_MBUS_BT656 &&
		    linked_sensor.mbus.type != V4L2_MBUS_PARALLEL)
			mipi_lvds_linked = true;
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
	struct rkcif_sensor_info *sensor;
	int ret, index;

	dev = container_of(notifier, struct rkcif_device, notifier);

	for (index = 0; index < dev->num_sensors; index++) {
		sensor = &dev->sensors[index];
		if (sensor->mbus.type == V4L2_MBUS_CCP2) {
			ret = rkcif_register_lvds_subdev(dev);
			if (ret < 0) {
				v4l2_err(&dev->v4l2_dev,
					 "Err: register lvds subdev failed!!!\n");
				goto notifier_end;
			}
			break;
		}

		if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		    sensor->mbus.type == V4L2_MBUS_BT656) {
			ret = rkcif_register_dvp_sof_subdev(dev);
			if (ret < 0) {
				v4l2_err(&dev->v4l2_dev,
					 "Err: register dvp sof subdev failed!!!\n");
				goto notifier_end;
			}
			break;
		}
	}

	ret = rkcif_create_links(dev);
	if (ret < 0)
		goto unregister_lvds;

	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unregister_lvds;

	ret = _set_pipeline_default_fmt(dev);
	if (ret < 0)
		goto unregister_lvds;

	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");

	return ret;

unregister_lvds:
	rkcif_unregister_lvds_subdev(dev);
	rkcif_unregister_dvp_sof_subdev(dev);
notifier_end:
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
	    vep->bus_type != V4L2_MBUS_CSI2 &&
	    vep->bus_type != V4L2_MBUS_CCP2)
		return 0;

	rk_asd->mbus.type = vep->bus_type;

	if (vep->bus_type == V4L2_MBUS_CSI2) {
		rk_asd->mbus.flags = vep->bus.mipi_csi2.flags;
		rk_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
	} else if (vep->bus_type == V4L2_MBUS_CCP2) {
		rk_asd->lanes = vep->bus.mipi_csi1.data_lane;
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

	ret = v4l2_async_notifier_parse_fwnode_endpoints(
		dev, ntf, sizeof(struct rkcif_async_subdev), rkcif_fwnode_parse);

	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	ntf->ops = &subdev_notifier_ops;

	ret = v4l2_async_notifier_register(&cif_dev->v4l2_dev, ntf);

	return ret;
}

/***************************** platform deive *******************************/

static int rkcif_register_platform_subdevs(struct rkcif_device *cif_dev)
{
	int stream_num = 0, ret;

	if (cif_dev->inf_id == RKCIF_MIPI_LVDS) {
		stream_num = RKCIF_MAX_STREAM_MIPI;
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

static irqreturn_t rkcif_irq_handler(int irq, struct rkcif_device *cif_dev)
{
	if (cif_dev->workmode == RKCIF_WORKMODE_PINGPONG)
		rkcif_irq_pingpong(cif_dev);
	else
		rkcif_irq_oneframe(cif_dev);

	return IRQ_HANDLED;
}

static irqreturn_t rkcif_irq_lite_handler(int irq, struct rkcif_device *cif_dev)
{
	rkcif_irq_lite_lvds(cif_dev);

	return IRQ_HANDLED;
}

void rkcif_soft_reset(struct rkcif_device *cif_dev, bool is_rst_iommu)
{
	struct rkcif_hw *hw_dev = cif_dev->hw_dev;
	bool can_reset = true;
	int i;

	if (!cif_dev->hw_dev)
		return;

	for (i = 0; i < hw_dev->dev_num; i++)
		if (atomic_read(&hw_dev->cif_dev[i]->pipe.stream_cnt) != 0) {
			can_reset = false;
			break;
		}

	if (can_reset)
		rkcif_hw_soft_reset(cif_dev->hw_dev, is_rst_iommu);
}

int rkcif_attach_hw(struct rkcif_device *cif_dev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rkcif_hw *hw;

	if (cif_dev->hw_dev)
		return 0;

	cif_dev->chip_id = CHIP_RV1126_CIF_LITE;
	np = of_parse_phandle(cif_dev->dev->of_node, "rockchip,hw", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(cif_dev->dev, "failed to get cif hw node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(cif_dev->dev, "failed to get cif hw from node\n");
		return -ENODEV;
	}

	hw = platform_get_drvdata(pdev);
	if (!hw) {
		dev_err(cif_dev->dev, "failed attach cif hw\n");
		return -EINVAL;
	}

	hw->cif_dev[hw->dev_num] = cif_dev;
	hw->dev_num++;
	cif_dev->hw_dev = hw;
	cif_dev->chip_id = hw->chip_id;
	dev_info(cif_dev->dev, "attach to cif hw node\n");

	return 0;
}

static int rkcif_detach_hw(struct rkcif_device *cif_dev)
{
	struct rkcif_hw *hw = cif_dev->hw_dev;
	int i;

	for (i = 0; i < hw->dev_num; i++) {
		if (hw->cif_dev[i] == cif_dev) {
			if ((i + 1) < hw->dev_num) {
				hw->cif_dev[i] = hw->cif_dev[i + 1];
				hw->cif_dev[i + 1] = NULL;
			} else {
				hw->cif_dev[i] = NULL;
			}

			hw->dev_num--;
			dev_info(cif_dev->dev, "detach to cif hw node\n");
			break;
		}
	}

	return 0;
}

static char *rkcif_get_monitor_mode(enum rkcif_monitor_mode mode)
{
	switch (mode) {
	case RKCIF_MONITOR_MODE_IDLE:
		return "idle";
	case RKCIF_MONITOR_MODE_CONTINUE:
		return "continue";
	case RKCIF_MONITOR_MODE_TRIGGER:
		return "trigger";
	case RKCIF_MONITOR_MODE_HOTPLUG:
		return "hotplug";
	default:
		return "unknown";
	}
}

static void rkcif_init_reset_monitor(struct rkcif_device *dev)
{
	struct device_node *node = dev->dev->of_node;
	struct rkcif_timer *timer = &dev->reset_watchdog_timer;
	struct notifier_block *notifier = &dev->reset_notifier;
	u32 para[8];
	int i;

	if (!of_property_read_u32_array(node,
					OF_CIF_MONITOR_PARA,
					para,
					CIF_MONITOR_PARA_NUM)) {
		for (i = 0; i < CIF_MONITOR_PARA_NUM; i++) {
			if (i == 0) {
				timer->monitor_mode = para[0];
				v4l2_info(&dev->v4l2_dev,
					  "%s: timer monitor mode:%s\n",
					  __func__, rkcif_get_monitor_mode(timer->monitor_mode));
			}

			if (i == 1) {
				timer->triggered_frame_num = para[1];
				v4l2_info(&dev->v4l2_dev,
					  "timer triggered frm num:%d\n",
					  timer->triggered_frame_num);
			}

			if (i == 2) {
				timer->frm_num_of_monitor_cycle = para[2];
				v4l2_info(&dev->v4l2_dev,
					  "timer frm num of monitor cycle:%d\n",
					  timer->frm_num_of_monitor_cycle);
			}

			if (i == 3) {
				timer->err_time_interval = para[3];
				v4l2_info(&dev->v4l2_dev,
					  "timer err time for keeping:%d ms\n",
					  timer->err_time_interval);
			}

			if (i == 4) {
				timer->csi2_err_ref_cnt = para[4];
				v4l2_info(&dev->v4l2_dev,
					  "timer csi2 err ref val for resetting:%d\n",
					  timer->csi2_err_ref_cnt);
			}
		}
	} else {
		timer->monitor_mode = RKCIF_MONITOR_MODE_IDLE;
		timer->err_time_interval = 0xffffffff;
		timer->frm_num_of_monitor_cycle = 0xffffffff;
		timer->triggered_frame_num =  0xffffffff;
		timer->csi2_err_ref_cnt = 0xffffffff;
	}

	timer->is_running = false;
	timer->is_triggered = false;
	timer->is_buf_stop_update = false;
	timer->csi2_err_cnt_even = 0;
	timer->csi2_err_cnt_odd = 0;
	timer->csi2_err_fs_fe_cnt = 0;
	timer->csi2_err_fs_fe_detect_cnt = 0;

	timer_setup(&timer->timer, rkcif_reset_watchdog_timer_handler, 0);

	notifier->priority = 1;
	notifier->notifier_call = rkcif_reset_notifier;
	rkcif_csi2_register_notifier(notifier);

}

int rkcif_plat_init(struct rkcif_device *cif_dev, struct device_node *node, int inf_id)
{
	struct device *dev = cif_dev->dev;
	struct v4l2_device *v4l2_dev;
	int ret;

	cif_dev->hdr.mode = NO_HDR;
	cif_dev->inf_id = inf_id;

	mutex_init(&cif_dev->stream_lock);
	spin_lock_init(&cif_dev->hdr_lock);
	spin_lock_init(&cif_dev->reset_watchdog_timer.timer_lock);
	spin_lock_init(&cif_dev->reset_watchdog_timer.csi2_err_lock);
	atomic_set(&cif_dev->pipe.power_cnt, 0);
	atomic_set(&cif_dev->pipe.stream_cnt, 0);
	atomic_set(&cif_dev->fh_cnt, 0);
	cif_dev->is_start_hdr = false;
	cif_dev->pipe.open = rkcif_pipeline_open;
	cif_dev->pipe.close = rkcif_pipeline_close;
	cif_dev->pipe.set_stream = rkcif_pipeline_set_stream;
	cif_dev->isr_hdl = rkcif_irq_handler;
	if (cif_dev->chip_id == CHIP_RV1126_CIF_LITE)
		cif_dev->isr_hdl = rkcif_irq_lite_handler;

	if (cif_dev->inf_id == RKCIF_MIPI_LVDS) {
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID0);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID1);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID2);
		rkcif_stream_init(cif_dev, RKCIF_STREAM_MIPI_ID3);
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

	strlcpy(cif_dev->media_dev.model, dev_name(dev),
		sizeof(cif_dev->media_dev.model));
	cif_dev->media_dev.dev = dev;
	v4l2_dev = &cif_dev->v4l2_dev;
	v4l2_dev->mdev = &cif_dev->media_dev;
	strlcpy(v4l2_dev->name, dev_name(dev), sizeof(v4l2_dev->name));

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

	if (cif_dev->chip_id == CHIP_RV1126_CIF ||
	    cif_dev->chip_id == CHIP_RV1126_CIF_LITE ||
	    cif_dev->chip_id == CHIP_RK3568_CIF)
		rkcif_register_luma_vdev(&cif_dev->luma_vdev, v4l2_dev, cif_dev);

	mutex_lock(&rkcif_dev_mutex);
	list_add_tail(&cif_dev->list, &rkcif_device_list);
	mutex_unlock(&rkcif_dev_mutex);

	return 0;

err_unreg_media_dev:
	media_device_unregister(&cif_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&cif_dev->v4l2_dev);
	return ret;
}

int rkcif_plat_uninit(struct rkcif_device *cif_dev)
{
	int stream_num = 0;

	if (cif_dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)
		rkcif_unregister_lvds_subdev(cif_dev);
	if (cif_dev->active_sensor->mbus.type == V4L2_MBUS_BT656 ||
	    cif_dev->active_sensor->mbus.type == V4L2_MBUS_PARALLEL)
		rkcif_unregister_dvp_sof_subdev(cif_dev);

	media_device_unregister(&cif_dev->media_dev);
	v4l2_device_unregister(&cif_dev->v4l2_dev);
	if (cif_dev->inf_id == RKCIF_MIPI_LVDS)
		stream_num = RKCIF_MAX_STREAM_MIPI;
	else
		stream_num = RKCIF_SINGLE_STREAM;
	rkcif_unregister_stream_vdevs(cif_dev, stream_num);

	return 0;
}

static const struct rkcif_match_data rkcif_dvp_match_data = {
	.inf_id = RKCIF_DVP,
};

static const struct rkcif_match_data rkcif_mipi_lvds_match_data = {
	.inf_id = RKCIF_MIPI_LVDS,
};

static const struct of_device_id rkcif_plat_of_match[] = {
	{
		.compatible = "rockchip,rkcif-dvp",
		.data = &rkcif_dvp_match_data,
	},
	{
		.compatible = "rockchip,rkcif-mipi-lvds",
		.data = &rkcif_mipi_lvds_match_data,
	},
	{},
};

static int rkcif_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct rkcif_device *cif_dev;
	const struct rkcif_match_data *data;
	int ret;

	sprintf(rkcif_version, "v%02x.%02x.%02x",
		RKCIF_DRIVER_VERSION >> 16,
		(RKCIF_DRIVER_VERSION & 0xff00) >> 8,
		RKCIF_DRIVER_VERSION & 0x00ff);

	dev_info(dev, "rkcif driver version: %s\n", rkcif_version);

	match = of_match_node(rkcif_plat_of_match, node);
	if (IS_ERR(match))
		return PTR_ERR(match);
	data = match->data;

	cif_dev = devm_kzalloc(dev, sizeof(*cif_dev), GFP_KERNEL);
	if (!cif_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, cif_dev);
	cif_dev->dev = dev;

	if (sysfs_create_group(&pdev->dev.kobj, &dev_attr_grp))
		return -ENODEV;

	rkcif_attach_hw(cif_dev);

	ret = rkcif_plat_init(cif_dev, node, data->inf_id);
	if (ret) {
		rkcif_detach_hw(cif_dev);
		return ret;
	}

	if (rkcif_proc_init(cif_dev))
		dev_warn(dev, "dev:%s create proc failed\n", dev_name(dev));

	rkcif_init_reset_monitor(cif_dev);
	rkcif_soft_reset(cif_dev, false);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int rkcif_plat_remove(struct platform_device *pdev)
{
	struct rkcif_device *cif_dev = platform_get_drvdata(pdev);

	rkcif_plat_uninit(cif_dev);
	rkcif_detach_hw(cif_dev);
	rkcif_proc_cleanup(cif_dev);
	rkcif_csi2_unregister_notifier(&cif_dev->reset_notifier);
	del_timer_sync(&cif_dev->reset_watchdog_timer.timer);

	return 0;
}

static int __maybe_unused rkcif_runtime_suspend(struct device *dev)
{
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);

	if (atomic_dec_return(&cif_dev->hw_dev->power_cnt))
		return 0;

	return pm_runtime_put(cif_dev->hw_dev->dev);
}

static int __maybe_unused rkcif_runtime_resume(struct device *dev)
{
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);

	if (atomic_inc_return(&cif_dev->hw_dev->power_cnt) > 1)
		return 0;

	return pm_runtime_get_sync(cif_dev->hw_dev->dev);
}

static int __maybe_unused __rkcif_clr_unready_dev(void)
{
	struct rkcif_device *cif_dev;

	mutex_lock(&rkcif_dev_mutex);
	list_for_each_entry(cif_dev, &rkcif_device_list, list)
		v4l2_async_notifier_clr_unready_dev(&cif_dev->notifier);
	mutex_unlock(&rkcif_dev_mutex);

	return 0;
}

static int rkcif_clr_unready_dev_param_set(const char *val, const struct kernel_param *kp)
{
#ifdef MODULE
	__rkcif_clr_unready_dev();
#endif

	return 0;
}

module_param_call(clr_unready_dev, rkcif_clr_unready_dev_param_set, NULL, NULL, 0200);
MODULE_PARM_DESC(clr_unready_dev, "clear unready devices");

#ifndef MODULE
static int __init rkcif_clr_unready_dev(void)
{
	__rkcif_clr_unready_dev();

	return 0;
}
late_initcall_sync(rkcif_clr_unready_dev);
#endif

static const struct dev_pm_ops rkcif_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkcif_runtime_suspend, rkcif_runtime_resume, NULL)
};

struct platform_driver rkcif_plat_drv = {
	.driver = {
		.name = CIF_DRIVER_NAME,
		.of_match_table = of_match_ptr(rkcif_plat_of_match),
		.pm = &rkcif_plat_pm_ops,
	},
	.probe = rkcif_plat_probe,
	.remove = rkcif_plat_remove,
};
EXPORT_SYMBOL(rkcif_plat_drv);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip CIF platform driver");
MODULE_LICENSE("GPL v2");
