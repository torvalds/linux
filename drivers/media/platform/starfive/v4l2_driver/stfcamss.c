// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_graph.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/mfd/syscon.h>

#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-fwnode.h>
#include <linux/debugfs.h>

#include "stfcamss.h"

#ifdef STF_DEBUG
unsigned int stdbg_level = ST_DEBUG;
unsigned int stdbg_mask = 0x7F;
#else
unsigned int stdbg_level = ST_ERR;
unsigned int stdbg_mask = 0x7F;
#endif
EXPORT_SYMBOL_GPL(stdbg_level);
EXPORT_SYMBOL_GPL(stdbg_mask);

static const struct reg_name mem_reg_name[] = {
	{"csi2rx"},
	{"vclk"},
	{"vrst"},
	{"sctrl"},
	{"isp"},
	{"trst"},
	{"pmu"},
	{"syscrg"},
};

static struct clk_bulk_data stfcamss_clocks[] = {
	{ .id = "clk_apb_func" },
	{ .id = "clk_pclk" },
	{ .id = "clk_sys_clk" },
	{ .id = "clk_wrapper_clk_c" },
	{ .id = "clk_dvp_inv" },
	{ .id = "clk_axiwr" },
	{ .id = "clk_mipi_rx0_pxl" },
	{ .id = "clk_pixel_clk_if0" },
	{ .id = "clk_pixel_clk_if1" },
	{ .id = "clk_pixel_clk_if2" },
	{ .id = "clk_pixel_clk_if3" },
	{ .id = "clk_m31dphy_cfgclk_in" },
	{ .id = "clk_m31dphy_refclk_in" },
	{ .id = "clk_m31dphy_txclkesc_lan0" },
	{ .id = "clk_ispcore_2x" },
	{ .id = "clk_isp_axi" },
};

static struct reset_control_bulk_data stfcamss_resets[] = {
	{ .id = "rst_wrapper_p" },
	{ .id = "rst_wrapper_c" },
	{ .id = "rst_pclk" },
	{ .id = "rst_sys_clk" },
	{ .id = "rst_axird" },
	{ .id = "rst_axiwr" },
	{ .id = "rst_pixel_clk_if0" },
	{ .id = "rst_pixel_clk_if1" },
	{ .id = "rst_pixel_clk_if2" },
	{ .id = "rst_pixel_clk_if3" },
	{ .id = "rst_m31dphy_hw" },
	{ .id = "rst_m31dphy_b09_always_on" },
	{ .id = "rst_isp_top_n" },
	{ .id = "rst_isp_top_axi" },
};

int stfcamss_get_mem_res(struct platform_device *pdev, struct stf_vin_dev *vin)
{
	struct device *dev = &pdev->dev;
	struct resource	*res;
	char *name;
	int i;

	for (i = 0; i < ARRAY_SIZE(mem_reg_name); i++) {
		name = (char *)(&mem_reg_name[i]);
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);

		if (!res)
			return -EINVAL;

		if (!strcmp(name, "csi2rx")) {
			vin->csi2rx_base = devm_ioremap_resource(dev, res);
			if (IS_ERR(vin->csi2rx_base))
				return PTR_ERR(vin->csi2rx_base);
		} else if (!strcmp(name, "vclk")) {
			vin->clkgen_base = ioremap(res->start, resource_size(res));
			if (!vin->clkgen_base)
				return -ENOMEM;
		} else if (!strcmp(name, "vrst")) {
			vin->rstgen_base = devm_ioremap_resource(dev, res);
			if (IS_ERR(vin->rstgen_base))
				return PTR_ERR(vin->rstgen_base);
		} else if (!strcmp(name, "sctrl")) {
			vin->sysctrl_base = devm_ioremap_resource(dev, res);
			if (IS_ERR(vin->sysctrl_base))
				return PTR_ERR(vin->sysctrl_base);
		} else if (!strcmp(name, "isp")) {
			vin->isp_base = devm_ioremap_resource(dev, res);
			if (IS_ERR(vin->isp_base))
				return PTR_ERR(vin->isp_base);
		} else if (!strcmp(name, "trst")) {
			vin->vin_top_rstgen_base = devm_ioremap_resource(dev, res);
			if (IS_ERR(vin->vin_top_rstgen_base))
				return PTR_ERR(vin->vin_top_rstgen_base);
		} else if (!strcmp(name, "pmu")) {
			vin->pmu_test = ioremap(res->start, resource_size(res));
			if (!vin->pmu_test)
				return -ENOMEM;
		} else if (!strcmp(name, "syscrg")) {
			vin->sys_crg = ioremap(res->start, resource_size(res));
			if (!vin->sys_crg)
				return -ENOMEM;
		} else {
			st_err(ST_CAMSS, "Could not match resource name\n");
		}
	}

	return 0;
}

int vin_parse_dt(struct device *dev, struct stf_vin_dev *vin)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	if (!np)
		return -EINVAL;

	return ret;
}

struct media_entity *stfcamss_find_sensor(struct media_entity *entity)
{
	struct media_pad *pad;

	while (1) {
		if (!entity->pads)
			return NULL;

		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			return NULL;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			return NULL;

		entity = pad->entity;

		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			return entity;
	}
}

static int stfcamss_of_parse_endpoint_node(struct device *dev,
				struct device_node *node,
				struct stfcamss_async_subdev *csd)
{
	struct v4l2_fwnode_endpoint vep = { { 0 } };
	struct v4l2_fwnode_bus_parallel *parallel_bus = &vep.bus.parallel;
	struct v4l2_fwnode_bus_mipi_csi2 *csi2_bus = &vep.bus.mipi_csi2;
	struct dvp_cfg *dvp = &csd->interface.dvp;
	struct csi2phy_cfg *csiphy = &csd->interface.csiphy;

	v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &vep);
	st_debug(ST_CAMSS, "%s: vep.base.port = 0x%x, id = 0x%x\n",
			__func__, vep.base.port, vep.base.id);

	csd->port = vep.base.port;
	switch (csd->port) {
	case DVP_SENSOR_PORT_NUMBER:
		st_debug(ST_CAMSS, "%s, flags = 0x%x\n", __func__,
				parallel_bus->flags);
		dvp->flags = parallel_bus->flags;
		dvp->bus_width = parallel_bus->bus_width;
		dvp->data_shift = parallel_bus->data_shift;
		break;
	case CSI2RX_SENSOR_PORT_NUMBER:
		st_debug(ST_CAMSS, "%s, CSI2 flags = 0x%x\n",
				__func__, parallel_bus->flags);
		csiphy->flags = csi2_bus->flags;
		memcpy(csiphy->data_lanes,
				csi2_bus->data_lanes, csi2_bus->num_data_lanes);
		csiphy->clock_lane = csi2_bus->clock_lane;
		csiphy->num_data_lanes = csi2_bus->num_data_lanes;
		memcpy(csiphy->lane_polarities,
				csi2_bus->lane_polarities,
				csi2_bus->num_data_lanes + 1);
		break;
	default:
		break;
	};

	return 0;
}

static int stfcamss_of_parse_ports(struct stfcamss *stfcamss)
{
	struct device *dev = stfcamss->dev;
	struct device_node *node = NULL;
	struct device_node *remote = NULL;
	int ret, num_subdevs = 0;

	for_each_endpoint_of_node(dev->of_node, node) {
		struct stfcamss_async_subdev *csd;

		if (!of_device_is_available(node))
			continue;

		remote = of_graph_get_remote_port_parent(node);
		if (!remote) {
			st_err(ST_CAMSS, "Cannot get remote parent\n");
			ret = -EINVAL;
			goto err_cleanup;
		}

		csd = v4l2_async_notifier_add_fwnode_subdev(
			&stfcamss->notifier, of_fwnode_handle(remote),
			struct stfcamss_async_subdev);
		of_node_put(remote);
		if (IS_ERR(csd)) {
			ret = PTR_ERR(csd);
			goto err_cleanup;
		}

		ret = stfcamss_of_parse_endpoint_node(dev, node, csd);
		if (ret < 0)
			goto err_cleanup;

		num_subdevs++;
	}

	return num_subdevs;

err_cleanup:
	of_node_put(node);
	return ret;
}

static int stfcamss_init_subdevices(struct stfcamss *stfcamss)
{
	int ret;

	ret = stf_dvp_subdev_init(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to init stf_dvp sub-device: %d\n",
			ret);
		return ret;
	}

	ret = stf_csiphy_subdev_init(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to init stf_csiphy sub-device: %d\n",
			ret);
		return ret;
	}

	ret = stf_csi_subdev_init(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to init stf_csi sub-device: %d\n",
			ret);
		return ret;
	}

	ret = stf_isp_subdev_init(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to init stf_isp sub-device: %d\n",
			ret);
		return ret;
	}

	ret = stf_vin_subdev_init(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to init stf_vin sub-device: %d\n",
			ret);
		return ret;
	}
	return ret;
}

static int stfcamss_register_subdevices(struct stfcamss *stfcamss)
{
	int ret;
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	struct stf_dvp_dev *dvp_dev = stfcamss->dvp_dev;
	struct stf_csiphy_dev *csiphy_dev = stfcamss->csiphy_dev;
	struct stf_csi_dev *csi_dev = stfcamss->csi_dev;
	struct stf_isp_dev *isp_dev = stfcamss->isp_dev;

	ret = stf_dvp_register(dvp_dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to register stf dvp%d entity: %d\n",
			0, ret);
		goto err_reg_dvp;
	}

	ret = stf_csiphy_register(csiphy_dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to register stf csiphy%d entity: %d\n",
			0, ret);
		goto err_reg_csiphy;
	}

	ret = stf_csi_register(csi_dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to register stf csi%d entity: %d\n",
			0, ret);
		goto err_reg_csi;
	}

	ret = stf_isp_register(isp_dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to register stf isp%d entity: %d\n",
			0, ret);
		goto err_reg_isp;
	}

	ret = stf_vin_register(vin_dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to register vin entity: %d\n",
			 ret);
		goto err_reg_vin;
	}

	ret = media_create_pad_link(
		&dvp_dev->subdev.entity,
		STF_DVP_PAD_SRC,
		&vin_dev->line[VIN_LINE_WR].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->vin entities: %d\n",
			dvp_dev->subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&csi_dev->subdev.entity,
		STF_CSI_PAD_SRC,
		&vin_dev->line[VIN_LINE_WR].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->vin entities: %d\n",
			csi_dev->subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&csiphy_dev->subdev.entity,
		STF_CSIPHY_PAD_SRC,
		&csi_dev->subdev.entity,
		STF_CSI_PAD_SINK,
		MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			csiphy_dev->subdev.entity.name,
			csi_dev->subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC,
		&vin_dev->line[VIN_LINE_ISP].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP]
			.subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC_SS0,
		&vin_dev->line[VIN_LINE_ISP_SS0].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP_SS0]
			.subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC_SS1,
		&vin_dev->line[VIN_LINE_ISP_SS1].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP_SS1]
			.subdev.entity.name,
			ret);
		goto err_link;
	}

#ifndef	STF_CAMSS_SKIP_ITI
	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC_ITIW,
		&vin_dev->line[VIN_LINE_ISP_ITIW].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP_ITIW]
			.subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC_ITIR,
		&vin_dev->line[VIN_LINE_ISP_ITIR].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP_ITIR]
			.subdev.entity.name,
			ret);
		goto err_link;
	}
#endif

	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC_RAW,
		&vin_dev->line[VIN_LINE_ISP_RAW].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP_RAW]
			.subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SRC_SCD_Y,
		&vin_dev->line[VIN_LINE_ISP_SCD_Y].subdev.entity,
		STF_VIN_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			isp_dev->subdev.entity.name,
			vin_dev->line[VIN_LINE_ISP_SCD_Y]
			.subdev.entity.name,
			ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&dvp_dev->subdev.entity,
		STF_DVP_PAD_SRC,
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			dvp_dev->subdev.entity.name,
			isp_dev->subdev.entity.name,
		ret);
		goto err_link;
	}

	ret = media_create_pad_link(
		&csi_dev->subdev.entity,
		STF_CSI_PAD_SRC,
		&isp_dev->subdev.entity,
		STF_ISP_PAD_SINK,
		0);
	if (ret < 0) {
		st_err(ST_CAMSS,
			"Failed to link %s->%s entities: %d\n",
			csi_dev->subdev.entity.name,
			isp_dev->subdev.entity.name,
			ret);
		goto err_link;
	}

	return ret;

err_link:
	stf_vin_unregister(stfcamss->vin_dev);
err_reg_vin:
	stf_isp_unregister(stfcamss->isp_dev);
err_reg_isp:
	stf_csi_unregister(stfcamss->csi_dev);
err_reg_csi:
	stf_csiphy_unregister(stfcamss->csiphy_dev);
err_reg_csiphy:
	stf_dvp_unregister(stfcamss->dvp_dev);
err_reg_dvp:
	return ret;
}

static void stfcamss_unregister_subdevices(struct stfcamss *stfcamss)
{
	stf_dvp_unregister(stfcamss->dvp_dev);
	stf_csiphy_unregister(stfcamss->csiphy_dev);
	stf_csi_unregister(stfcamss->csi_dev);
	stf_isp_unregister(stfcamss->isp_dev);
	stf_vin_unregister(stfcamss->vin_dev);
}

static int stfcamss_register_mediadevice_subdevnodes(
		struct v4l2_async_notifier *async,
		struct v4l2_subdev *sd)
{
	struct stfcamss *stfcamss =
		container_of(async, struct stfcamss, notifier);
	int ret;

	if (sd->host_priv) {
		struct media_entity *sensor = &sd->entity;
		struct media_entity *input = sd->host_priv;
		unsigned int i;

		for (i = 0; i < sensor->num_pads; i++) {
			if (sensor->pads[i].flags & MEDIA_PAD_FL_SOURCE)
				break;
		}
		if (i == sensor->num_pads) {
			st_err(ST_CAMSS,
				"No source pad in external entity\n");
			return -EINVAL;
		}

		ret = media_create_pad_link(sensor, i,
			input, STF_PAD_SINK,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
		if (ret < 0) {
			st_err(ST_CAMSS,
				"Failed to link %s->%s entities: %d\n",
				sensor->name, input->name, ret);
			return ret;
		}
	}

	ret = v4l2_device_register_subdev_nodes(&stfcamss->v4l2_dev);
	if (ret < 0)
		return ret;

	if (stfcamss->media_dev.devnode)
		return ret;

	st_debug(ST_CAMSS, "stfcamss register media device\n");
	return media_device_register(&stfcamss->media_dev);
}

static int stfcamss_subdev_notifier_bound(struct v4l2_async_notifier *async,
					struct v4l2_subdev *subdev,
					struct v4l2_async_subdev *asd)
{
	struct stfcamss *stfcamss =
		container_of(async, struct stfcamss, notifier);
	struct stfcamss_async_subdev *csd =
		container_of(asd, struct stfcamss_async_subdev, asd);
	enum port_num port = csd->port;
	struct stf_dvp_dev *dvp_dev = stfcamss->dvp_dev;
	struct stf_csiphy_dev *csiphy_dev = stfcamss->csiphy_dev;

	switch (port) {
	case DVP_SENSOR_PORT_NUMBER:
		dvp_dev->dvp = &csd->interface.dvp;
		subdev->host_priv = &dvp_dev->subdev.entity;
		break;
	case CSI2RX_SENSOR_PORT_NUMBER:
		csiphy_dev->csiphy = &csd->interface.csiphy;
		subdev->host_priv = &csiphy_dev->subdev.entity;
		break;
	default:
		break;
	};

	stfcamss_register_mediadevice_subdevnodes(async, subdev);

	return 0;
}

#ifdef UNUSED_CODE
static int stfcamss_subdev_notifier_complete(
		struct v4l2_async_notifier *async)
{
	struct stfcamss *stfcamss =
		container_of(async, struct stfcamss, notifier);
	struct v4l2_device *v4l2_dev = &stfcamss->v4l2_dev;
	struct v4l2_subdev *sd;
	int ret;

	list_for_each_entry(sd, &v4l2_dev->subdevs, list) {
		if (sd->host_priv) {
			struct media_entity *sensor = &sd->entity;
			struct media_entity *input = sd->host_priv;
			unsigned int i;

			for (i = 0; i < sensor->num_pads; i++) {
				if (sensor->pads[i].flags & MEDIA_PAD_FL_SOURCE)
					break;
			}
			if (i == sensor->num_pads) {
				st_err(ST_CAMSS,
					"No source pad in external entity\n");
				return -EINVAL;
			}

			ret = media_create_pad_link(sensor, i,
				input, STF_PAD_SINK,
				MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
			if (ret < 0) {
				st_err(ST_CAMSS,
					"Failed to link %s->%s entities: %d\n",
					sensor->name, input->name, ret);
				return ret;
			}
		}
	}

	ret = v4l2_device_register_subdev_nodes(&stfcamss->v4l2_dev);
	if (ret < 0)
		return ret;

	return media_device_register(&stfcamss->media_dev);
}
#endif

static const struct v4l2_async_notifier_operations
stfcamss_subdev_notifier_ops = {
	.bound = stfcamss_subdev_notifier_bound,
};

static const struct media_device_ops stfcamss_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

#ifdef CONFIG_DEBUG_FS
enum module_id {
	VIN_MODULE = 0,
	ISP_MODULE,
	CSI_MODULE,
	CSIPHY_MODULE,
	DVP_MODULE,
	CLK_MODULE,
};

static enum module_id id_num = ISP_MODULE;

void dump_clk_reg(void __iomem *reg_base)
{
	int i;

	st_info(ST_CAMSS, "DUMP Clk register:\n");
	for (i = 0; i <= CLK_C_ISP_CTRL; i += 4)
		print_reg(ST_CAMSS, reg_base, i);
}

static ssize_t vin_debug_read(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	void __iomem *reg_base;
	struct stfcamss *stfcamss = dev_get_drvdata(dev);
	struct stf_vin_dev *vin = stfcamss->vin;
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	struct stf_isp_dev *isp_dev = stfcamss->isp_dev;
	struct stf_csi_dev *csi0_dev = stfcamss->csi_dev;

	switch (id_num) {
	case VIN_MODULE:
	case CSIPHY_MODULE:
	case DVP_MODULE:
		mutex_lock(&vin_dev->power_lock);
		if (vin_dev->power_count > 0) {
			reg_base = vin->sysctrl_base;
			dump_vin_reg(reg_base);
		}
		mutex_unlock(&vin_dev->power_lock);
		break;
	case ISP_MODULE:
		mutex_lock(&isp_dev->stream_lock);
		if (isp_dev->stream_count > 0) {
			reg_base = vin->isp_base;
			dump_isp_reg(reg_base);
		}
		mutex_unlock(&isp_dev->stream_lock);
		break;
	case CSI_MODULE:
		mutex_lock(&csi0_dev->stream_lock);
		if (csi0_dev->stream_count > 0) {
			reg_base = vin->csi2rx_base;
			dump_csi_reg(reg_base);
		}
		mutex_unlock(&csi0_dev->stream_lock);
		break;
	case CLK_MODULE:
		mutex_lock(&vin_dev->power_lock);
		if (vin_dev->power_count > 0) {
			reg_base = vin->clkgen_base;
			dump_clk_reg(reg_base);
		}
		mutex_unlock(&vin_dev->power_lock);
		break;
	default:
		break;
	}

	return 0;
}

static void set_reg_val(struct stfcamss *stfcamss, int id, u32 offset, u32 val)
{
	struct stf_vin_dev *vin = stfcamss->vin;
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	struct stf_isp_dev *isp_dev = stfcamss->isp_dev;
	struct stf_csi_dev *csi_dev = stfcamss->csi_dev;
	void __iomem *reg_base;

	switch (id) {
	case VIN_MODULE:
	case CSIPHY_MODULE:
	case DVP_MODULE:
		mutex_lock(&vin_dev->power_lock);
		if (vin_dev->power_count > 0) {
			reg_base = vin->sysctrl_base;
			print_reg(ST_VIN, reg_base, offset);
			reg_write(reg_base, offset, val);
			print_reg(ST_VIN, reg_base, offset);
		}
		mutex_unlock(&vin_dev->power_lock);
		break;
	case ISP_MODULE:
		mutex_lock(&isp_dev->stream_lock);
		if (isp_dev->stream_count > 0) {
			reg_base = vin->isp_base;
			print_reg(ST_ISP, reg_base, offset);
			reg_write(reg_base, offset, val);
			print_reg(ST_ISP, reg_base, offset);
		}
		mutex_unlock(&isp_dev->stream_lock);
		break;
	case CSI_MODULE:
		mutex_lock(&csi_dev->stream_lock);
		if (csi_dev->stream_count > 0) {
			reg_base = vin->csi2rx_base;
			print_reg(ST_CSI, reg_base, offset);
			reg_write(reg_base, offset, val);
			print_reg(ST_CSI, reg_base, offset);
		}
		mutex_unlock(&csi_dev->stream_lock);
		break;
	case CLK_MODULE:
		mutex_lock(&vin_dev->power_lock);
		if (vin_dev->power_count > 0) {
			reg_base = vin->clkgen_base;
			print_reg(ST_CAMSS, reg_base, offset);
			reg_write(reg_base, offset, val);
			print_reg(ST_CAMSS, reg_base, offset);
		}
		mutex_unlock(&vin_dev->power_lock);
		break;
	default:
		break;

	}
}

static u32 atoi(const char *s)
{
	u32 ret = 0, d = 0;
	char ch;
	int hex = 0;

	if ((*s == '0') && (*(s+1) == 'x')) {
		hex = 1;
		s += 2;
	}

	while (1) {
		if (!hex) {
			d = (*s++) - '0';
			if (d > 9)
				break;
			ret *= 10;
			ret += d;
		} else {
			ch = tolower(*s++);
			if (isdigit(ch))
				d = ch - '0';
			else if (islower(ch))
				d = ch - 'a' + 10;
			else
				break;
			if (d > 15)
				break;
			ret *= 16;
			ret += d;
		}
	}

	return ret;
}

static ssize_t vin_debug_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	struct stfcamss *stfcamss = dev_get_drvdata(dev);
	char *buf;
	char *line;
	char *p;
	static const char *delims = " \t\r";
	char *token;
	u32 offset, val;

	buf = memdup_user_nul(user_buf, min_t(size_t, PAGE_SIZE, count));
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	p = buf;
	st_debug(ST_CAMSS, "dup buf: %s, len: %lu, count: %lu\n", p, strlen(p), count);
	while (p && *p) {
		p = skip_spaces(p);
		line = strsep(&p, "\n");
		if (!*line || *line == '#')
			break;
		token = strsep(&line, delims);
		if (!token)
			goto out;
		id_num = atoi(token);
		token = strsep(&line, delims);
		if (!token)
			goto out;
		offset = atoi(token);
		token = strsep(&line, delims);
		if (!token)
			goto out;
		val = atoi(token);
	}
	set_reg_val(stfcamss, id_num, offset, val);
out:
	kfree(buf);
	st_info(ST_CAMSS, "id_num = %d, offset = 0x%x, 0x%x\n", id_num, offset, val);
	return count;
}

static const struct file_operations vin_debug_fops = {
	.open = simple_open,
	.read = vin_debug_read,
	.write = vin_debug_write,
};
#endif /* CONFIG_DEBUG_FS */


static int stfcamss_probe(struct platform_device *pdev)
{
	struct stfcamss *stfcamss;
	struct stf_vin_dev *vin;
	struct device *dev = &pdev->dev;
	struct of_phandle_args args;
	int ret = 0, num_subdevs;

	dev_info(dev, "stfcamss probe enter!\n");

	stfcamss = devm_kzalloc(dev, sizeof(struct stfcamss), GFP_KERNEL);
	if (!stfcamss)
		return -ENOMEM;

	stfcamss->dvp_dev = devm_kzalloc(dev,
		sizeof(*stfcamss->dvp_dev), GFP_KERNEL);
	if (!stfcamss->dvp_dev) {
		ret = -ENOMEM;
		goto err_cam;
	}

	stfcamss->csiphy_dev = devm_kzalloc(dev,
		sizeof(*stfcamss->csiphy_dev),
		GFP_KERNEL);
	if (!stfcamss->csiphy_dev) {
		ret = -ENOMEM;
		goto err_cam;
	}

	stfcamss->csi_dev = devm_kzalloc(dev,
		sizeof(*stfcamss->csi_dev),
		GFP_KERNEL);
	if (!stfcamss->csi_dev) {
		ret = -ENOMEM;
		goto err_cam;
	}

	stfcamss->isp_dev = devm_kzalloc(dev,
		sizeof(*stfcamss->isp_dev),
		GFP_KERNEL);
	if (!stfcamss->isp_dev) {
		ret = -ENOMEM;
		goto err_cam;
	}

	stfcamss->vin_dev = devm_kzalloc(dev,
		sizeof(*stfcamss->vin_dev),
		GFP_KERNEL);
	if (!stfcamss->vin_dev) {
		ret = -ENOMEM;
		goto err_cam;
	}

	stfcamss->vin = devm_kzalloc(dev,
		sizeof(struct stf_vin_dev),
		GFP_KERNEL);
	if (!stfcamss->vin) {
		ret = -ENOMEM;
		goto err_cam;
	}

	vin = stfcamss->vin;

	vin->irq = platform_get_irq(pdev, 0);
	if (vin->irq <= 0) {
		st_err(ST_CAMSS, "Could not get irq\n");
		goto err_cam;
	}

	vin->isp_irq = platform_get_irq(pdev, 1);
	if (vin->isp_irq <= 0) {
		st_err(ST_CAMSS, "Could not get isp irq\n");
		goto err_cam;
	}

	vin->isp_csi_irq = platform_get_irq(pdev, 2);
	if (vin->isp_csi_irq <= 0) {
		st_err(ST_CAMSS, "Could not get isp csi irq\n");
		goto err_cam;
	}

	vin->isp_scd_irq = platform_get_irq(pdev, 3);
	if (vin->isp_scd_irq <= 0) {
		st_err(ST_CAMSS, "Could not get isp scd irq\n");
		goto err_cam;
	}

	vin->isp_irq_csiline = platform_get_irq(pdev, 4);
	if (vin->isp_irq_csiline <= 0) {
		st_err(ST_CAMSS, "Could not get isp irq csiline\n");
		goto err_cam;
	}

	pm_runtime_enable(dev);

	stfcamss->nclks = ARRAY_SIZE(stfcamss_clocks);
	stfcamss->sys_clk = stfcamss_clocks;

	ret = devm_clk_bulk_get(dev, stfcamss->nclks, stfcamss->sys_clk);
	if (ret) {
		st_err(ST_CAMSS, "Failed to get clk controls\n");
		return ret;
	}

	stfcamss->nrsts = ARRAY_SIZE(stfcamss_resets);
	stfcamss->sys_rst = stfcamss_resets;

	ret = devm_reset_control_bulk_get_shared(dev, stfcamss->nrsts,
		stfcamss->sys_rst);
	if (ret) {
		st_err(ST_CAMSS, "Failed to get reset controls\n");
		return ret;
	}

	ret = of_parse_phandle_with_fixed_args(dev->of_node,
			"starfive,aon-syscon", 1, 0, &args);
	if (ret < 0) {
		st_err(ST_CAMSS, "Failed to parse starfive,aon-syscon\n");
		return -EINVAL;
	}

	stfcamss->stf_aon_syscon = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(stfcamss->stf_aon_syscon))
		return PTR_ERR(stfcamss->stf_aon_syscon);

	stfcamss->aon_gp_reg = args.args[0];

	ret = stfcamss_get_mem_res(pdev, vin);
	if (ret) {
		st_err(ST_CAMSS, "Could not map registers\n");
		goto err_cam;
	}

	ret = vin_parse_dt(dev, vin);
	if (ret)
		goto err_cam;

	vin->dev = dev;
	stfcamss->dev = dev;
	platform_set_drvdata(pdev, stfcamss);

	v4l2_async_notifier_init(&stfcamss->notifier);

	num_subdevs = stfcamss_of_parse_ports(stfcamss);
	if (num_subdevs < 0) {
		ret = num_subdevs;
		goto err_cam_noti;
	}

	ret = stfcamss_init_subdevices(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS, "Failed to init subdevice: %d\n", ret);
		goto err_cam_noti;
	}

	stfcamss->media_dev.dev = stfcamss->dev;
	strscpy(stfcamss->media_dev.model, "Starfive Camera Subsystem",
		sizeof(stfcamss->media_dev.model));
	strscpy(stfcamss->media_dev.serial, "0123456789ABCDEF",
		sizeof(stfcamss->media_dev.serial));
	snprintf(stfcamss->media_dev.bus_info, sizeof(stfcamss->media_dev.bus_info),
			"%s:%s", dev_bus_name(dev), pdev->name);
	stfcamss->media_dev.hw_revision = 0x01;
	stfcamss->media_dev.ops = &stfcamss_media_ops;
	media_device_init(&stfcamss->media_dev);

	stfcamss->v4l2_dev.mdev = &stfcamss->media_dev;

	ret = v4l2_device_register(stfcamss->dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		st_err(ST_CAMSS, "Failed to register V4L2 device: %d\n", ret);
		goto err_cam_noti_med;
	}

	ret = stfcamss_register_subdevices(stfcamss);
	if (ret < 0) {
		st_err(ST_CAMSS, "Failed to register subdevice: %d\n", ret);
		goto err_cam_noti_med_vreg;
	}

	if (num_subdevs) {
		stfcamss->notifier.ops = &stfcamss_subdev_notifier_ops;
		ret = v4l2_async_notifier_register(&stfcamss->v4l2_dev,
				&stfcamss->notifier);
		if (ret) {
			st_err(ST_CAMSS,
				"Failed to register async subdev nodes: %d\n",
				ret);
			goto err_cam_noti_med_vreg_sub;
		}
	} else {
		ret = v4l2_device_register_subdev_nodes(&stfcamss->v4l2_dev);
		if (ret < 0) {
			st_err(ST_CAMSS,
				"Failed to register subdev nodes: %d\n",
				ret);
			goto err_cam_noti_med_vreg_sub;
		}

		ret = media_device_register(&stfcamss->media_dev);
		if (ret < 0) {
			st_err(ST_CAMSS, "Failed to register media device: %d\n",
					ret);
			goto err_cam_noti_med_vreg_sub_medreg;
		}
	}

#ifdef CONFIG_DEBUG_FS
	stfcamss->debugfs_entry = debugfs_create_dir("stfcamss", NULL);
	stfcamss->vin_debugfs = debugfs_create_file("stf_vin",
			0644, stfcamss->debugfs_entry,
			(void *)dev, &vin_debug_fops);
	debugfs_create_u32("dbg_level",
			0644, stfcamss->debugfs_entry,
			&stdbg_level);
	debugfs_create_u32("dbg_mask",
			0644, stfcamss->debugfs_entry,
			&stdbg_mask);
#endif
	dev_info(dev, "stfcamss probe success!\n");

	return 0;

#ifdef CONFIG_DEBUG_FS
	debugfs_remove(stfcamss->vin_debugfs);
	debugfs_remove_recursive(stfcamss->debugfs_entry);
	stfcamss->debugfs_entry = NULL;
#endif

err_cam_noti_med_vreg_sub_medreg:
err_cam_noti_med_vreg_sub:
	stfcamss_unregister_subdevices(stfcamss);
err_cam_noti_med_vreg:
	v4l2_device_unregister(&stfcamss->v4l2_dev);
err_cam_noti_med:
	media_device_cleanup(&stfcamss->media_dev);
err_cam_noti:
	v4l2_async_notifier_cleanup(&stfcamss->notifier);
err_cam:
	// kfree(stfcamss);
	return ret;
}

static int stfcamss_remove(struct platform_device *pdev)
{
	struct stfcamss *stfcamss = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "remove done\n");

#ifdef CONFIG_DEBUG_FS
	debugfs_remove(stfcamss->vin_debugfs);
	debugfs_remove_recursive(stfcamss->debugfs_entry);
	stfcamss->debugfs_entry = NULL;
#endif

	stfcamss_unregister_subdevices(stfcamss);
	v4l2_device_unregister(&stfcamss->v4l2_dev);
	media_device_cleanup(&stfcamss->media_dev);
	pm_runtime_disable(&pdev->dev);

	kfree(stfcamss);

	return 0;
}

static const struct of_device_id stfcamss_of_match[] = {
	{ .compatible = "starfive,jh7110-vin" },
	{ /* end node */ },
};

MODULE_DEVICE_TABLE(of, stfcamss_of_match);

#ifdef CONFIG_PM_SLEEP
static int stfcamss_suspend(struct device *dev)
{
	struct stfcamss *stfcamss = dev_get_drvdata(dev);
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct stfcamss_video *video;
	struct video_device *vdev;
	int i = 0;
	int pm_power_count;
	int pm_stream_count;

	for (i = 0; i < VIN_LINE_MAX; i++) {
		video = &vin_dev->line[i].video_out;
		vdev = &vin_dev->line[i].video_out.vdev;
		vin_dev->line[i].pm_power_count = vin_dev->line[i].power_count;
		vin_dev->line[i].pm_stream_count = vin_dev->line[i].stream_count;
		pm_power_count = vin_dev->line[i].pm_power_count;
		pm_stream_count = vin_dev->line[i].pm_stream_count;

		if (pm_stream_count) {
			while (pm_stream_count--) {
				entity = &vdev->entity;
				while (1) {
					pad = &entity->pads[0];
					if (!(pad->flags & MEDIA_PAD_FL_SINK))
						break;

					pad = media_entity_remote_pad(pad);
					if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
						break;

					entity = pad->entity;
					subdev = media_entity_to_v4l2_subdev(entity);

					v4l2_subdev_call(subdev, video, s_stream, 0);
				}
			}
			media_pipeline_stop(&vdev->entity);
			video->ops->flush_buffers(video, VB2_BUF_STATE_ERROR);
		}

		if (!pm_power_count)
			continue;

		v4l2_pipeline_pm_put(&vdev->entity);
	}

	return pm_runtime_force_suspend(dev);
}

static int stfcamss_resume(struct device *dev)
{
	struct stfcamss *stfcamss = dev_get_drvdata(dev);
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct stfcamss_video *video;
	struct video_device *vdev;
	int i = 0;
	int pm_power_count;
	int pm_stream_count;
	int ret = 0;

	pm_runtime_force_resume(dev);

	for (i = 0; i < VIN_LINE_MAX; i++) {
		video = &vin_dev->line[i].video_out;
		vdev = &vin_dev->line[i].video_out.vdev;
		pm_power_count = vin_dev->line[i].pm_power_count;
		pm_stream_count = vin_dev->line[i].pm_stream_count;

		if (!pm_power_count)
			continue;

		ret = v4l2_pipeline_pm_get(&vdev->entity);
		if (ret < 0)
			goto err;

		if (pm_stream_count) {
			ret = media_pipeline_start(&vdev->entity, &video->stfcamss->pipe);
			if (ret < 0)
				goto err_pm_put;

			while (pm_stream_count--) {
				entity = &vdev->entity;
				while (1) {
					pad = &entity->pads[0];
					if (!(pad->flags & MEDIA_PAD_FL_SINK))
						break;

					pad = media_entity_remote_pad(pad);
					if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
						break;

					entity = pad->entity;
					subdev = media_entity_to_v4l2_subdev(entity);

					ret = v4l2_subdev_call(subdev, video, s_stream, 1);
					if (ret < 0 && ret != -ENOIOCTLCMD)
						goto err_pipeline_stop;
				}
			}
		}
	}

	return 0;

err_pipeline_stop:
	media_pipeline_stop(&vdev->entity);
err_pm_put:
	v4l2_pipeline_pm_put(&vdev->entity);
err:
	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int stfcamss_runtime_suspend(struct device *dev)
{
	struct stfcamss *stfcamss = dev_get_drvdata(dev);

	reset_control_assert(stfcamss->sys_rst[STFRST_ISP_TOP_AXI].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_ISP_TOP_N].rstc);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_ISP_AXI].clk);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_ISPCORE_2X].clk);

	return 0;
}

static int stfcamss_runtime_resume(struct device *dev)
{
	struct stfcamss *stfcamss = dev_get_drvdata(dev);

	clk_prepare_enable(stfcamss->sys_clk[STFCLK_ISPCORE_2X].clk);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_ISP_AXI].clk);
	reset_control_deassert(stfcamss->sys_rst[STFRST_ISP_TOP_N].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_ISP_TOP_AXI].rstc);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops stfcamss_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stfcamss_suspend, stfcamss_resume)
	SET_RUNTIME_PM_OPS(stfcamss_runtime_suspend, stfcamss_runtime_resume, NULL)
};

static struct platform_driver stfcamss_driver = {
	.probe = stfcamss_probe,
	.remove = stfcamss_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = &stfcamss_pm_ops,
		.of_match_table = of_match_ptr(stfcamss_of_match),
	},
};

static int __init stfcamss_init(void)
{
	return platform_driver_register(&stfcamss_driver);
}

static void __exit stfcamss_cleanup(void)
{
	platform_driver_unregister(&stfcamss_driver);
}

module_init(stfcamss_init);
//fs_initcall(stfcamss_init);
module_exit(stfcamss_cleanup);

MODULE_LICENSE("GPL");
