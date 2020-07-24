// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include "dev.h"
#include "regs.h"

static void get_remote_mipi_sensor(struct rkisp_device *dev,
				  struct v4l2_subdev **sensor_sd)
{
	struct media_graph graph;
	struct media_entity *entity = &dev->isp_sdev.sd.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	int ret;

	/* Walk the graph to locate sensor nodes. */
	mutex_lock(&mdev->graph_mutex);
	ret = media_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		*sensor_sd = NULL;
		return;
	}

	media_graph_walk_start(&graph, entity);
	while ((entity = media_graph_walk_next(&graph))) {
		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			break;
	}
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);

	if (entity)
		*sensor_sd = media_entity_to_v4l2_subdev(entity);
	else
		*sensor_sd = NULL;
}

static struct v4l2_subdev *get_remote_subdev(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct v4l2_subdev *remote_sd = NULL;

	local = &sd->entity.pads[CSI_SINK];
	if (!local)
		goto end;
	remote = media_entity_remote_pad(local);
	if (!remote)
		goto end;

	remote_sd = media_entity_to_v4l2_subdev(remote->entity);
end:
	return remote_sd;
}

static int rkisp_csi_link_setup(struct media_entity *entity,
				 const struct media_pad *local,
				 const struct media_pad *remote,
				 u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct rkisp_csi_device *csi;
	struct rkisp_stream *stream = NULL;
	int ret = 0;
	u8 id;

	if (!sd)
		return -ENODEV;

	csi = v4l2_get_subdevdata(sd);
	if (local->flags & MEDIA_PAD_FL_SOURCE) {
		id = local->index - 1;
		if (id && id < RKISP_STREAM_DMATX3)
			stream = &csi->ispdev->cap_dev.stream[id + 1];
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi->sink[id].linked) {
				ret = -EBUSY;
				goto out;
			}
			csi->sink[id].linked = true;
			csi->sink[id].index = 1 << id;
		} else {
			csi->sink[id].linked = false;
			csi->sink[id].index = 0;
		}
		if (stream)
			stream->linked = csi->sink[id].linked;
	}

	return 0;
out:
	v4l2_err(sd, "pad%d is already linked\n", local->index);
	return ret;
}

static int rkisp_csi_g_mbus_config(struct v4l2_subdev *sd,
				  struct v4l2_mbus_config *config)
{
	struct v4l2_subdev *remote_sd;

	if (!sd)
		return -ENODEV;
	remote_sd = get_remote_subdev(sd);
	return v4l2_subdev_call(remote_sd, video, g_mbus_config, config);
}

static int rkisp_csi_get_set_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *remote_sd;

	if (fmt->pad != CSI_SINK)
		fmt->pad -= 1;

	if (!sd)
		return -ENODEV;
	remote_sd = get_remote_subdev(sd);
	return v4l2_subdev_call(remote_sd, pad, get_fmt, NULL, fmt);
}

static int rkisp_csi_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp_csi_device *csi = v4l2_get_subdevdata(sd);
	struct rkisp_device *dev = csi->ispdev;
	void __iomem *base = dev->base_addr;

	memset(csi->tx_first, 0, sizeof(csi->tx_first));
	memset(csi->filt_state, 0, sizeof(csi->filt_state));
	if (on)
		writel(SW_Y_STAT_EN, base + CSI2RX_Y_STAT_CTRL);
	else
		writel(0, base + CSI2RX_Y_STAT_CTRL);
	return 0;
}

static int rkisp_csi_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static const struct media_entity_operations rkisp_csi_media_ops = {
	.link_setup = rkisp_csi_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_pad_ops rkisp_csi_pad_ops = {
	.set_fmt = rkisp_csi_get_set_fmt,
	.get_fmt = rkisp_csi_get_set_fmt,
};

static const struct v4l2_subdev_video_ops rkisp_csi_video_ops = {
	.g_mbus_config = rkisp_csi_g_mbus_config,
	.s_stream = rkisp_csi_s_stream,
};

static const struct v4l2_subdev_core_ops rkisp_csi_core_ops = {
	.s_power = rkisp_csi_s_power,
};

static struct v4l2_subdev_ops rkisp_csi_ops = {
	.core = &rkisp_csi_core_ops,
	.video = &rkisp_csi_video_ops,
	.pad = &rkisp_csi_pad_ops,
};

static int csi_config(struct rkisp_csi_device *csi)
{
	struct rkisp_device *dev = csi->ispdev;
	void __iomem *base = dev->base_addr;
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	struct v4l2_subdev *mipi_sensor;
	struct v4l2_ctrl *ctrl;
	u32 emd_vc, emd_dt, mipi_ctrl;
	int lanes, ret, i;

	/*
	 * sensor->mbus is set in isp or d-phy notifier_bound function
	 */
	switch (sensor->mbus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_4_LANE:
		lanes = 4;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		lanes = 3;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		lanes = 2;
		break;
	case V4L2_MBUS_CSI2_1_LANE:
		lanes = 1;
		break;
	default:
		return -EINVAL;
	}

	emd_vc = 0xFF;
	emd_dt = 0;
	dev->hdr.sensor = NULL;
	get_remote_mipi_sensor(dev, &mipi_sensor);
	if (mipi_sensor) {
		ctrl = v4l2_ctrl_find(mipi_sensor->ctrl_handler,
				      CIFISP_CID_EMB_VC);
		if (ctrl)
			emd_vc = v4l2_ctrl_g_ctrl(ctrl);

		ctrl = v4l2_ctrl_find(mipi_sensor->ctrl_handler,
				      CIFISP_CID_EMB_DT);
		if (ctrl)
			emd_dt = v4l2_ctrl_g_ctrl(ctrl);
		dev->hdr.sensor = mipi_sensor;
	}

	dev->emd_dt = emd_dt;
	dev->emd_vc = emd_vc;
	dev->emd_data_idx = 0;
	if (emd_vc <= CIF_ISP_ADD_DATA_VC_MAX) {
		for (i = 0; i < RKISP_EMDDATA_FIFO_MAX; i++) {
			ret = kfifo_alloc(&dev->emd_data_fifo[i].mipi_kfifo,
					  CIFISP_ADD_DATA_FIFO_SIZE,
					  GFP_ATOMIC);
			if (ret) {
				v4l2_err(&dev->v4l2_dev,
					 "kfifo_alloc failed with error %d\n",
					 ret);
				return ret;
			}
		}
	}

	if (dev->isp_ver == ISP_V13 ||
	    dev->isp_ver == ISP_V12) {
		/* lanes */
		writel(lanes - 1, base + CIF_ISP_CSI0_CTRL1);

		/* linecnt */
		writel(0x3FFF, base + CIF_ISP_CSI0_CTRL2);

		/* Configure Data Type and Virtual Channel */
		writel(csi->mipi_di[0] | csi->mipi_di[1] << 8,
		       base + CIF_ISP_CSI0_DATA_IDS_1);

		/* clear interrupts state */
		readl(base + CIF_ISP_CSI0_ERR1);
		readl(base + CIF_ISP_CSI0_ERR2);
		readl(base + CIF_ISP_CSI0_ERR3);
		/* set interrupts mask */
		writel(0x1FFFFFF0, base + CIF_ISP_CSI0_MASK1);
		writel(0x03FFFFFF, base + CIF_ISP_CSI0_MASK2);
		writel(CIF_ISP_CSI0_IMASK_FRAME_END(0x3F) |
		       CIF_ISP_CSI0_IMASK_RAW0_OUT_V_END |
		       CIF_ISP_CSI0_IMASK_RAW1_OUT_V_END |
		       CIF_ISP_CSI0_IMASK_LINECNT,
		       base + CIF_ISP_CSI0_MASK3);

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "CSI0_CTRL1 0x%08x\n"
			 "CSI0_IDS 0x%08x\n"
			 "CSI0_MASK3 0x%08x\n",
			 readl(base + CIF_ISP_CSI0_CTRL1),
			 readl(base + CIF_ISP_CSI0_DATA_IDS_1),
			 readl(base + CIF_ISP_CSI0_MASK3));
	} else if (dev->isp_ver == ISP_V20) {
		struct rkmodule_hdr_cfg hdr_cfg;
		u32 val;

		dev->hdr.op_mode = HDR_NORMAL;
		dev->hdr.esp_mode = HDR_NORMAL_VC;
		if (mipi_sensor) {
			ret = v4l2_subdev_call(mipi_sensor,
					       core, ioctl,
					       RKMODULE_GET_HDR_CFG,
					       &hdr_cfg);
			if (!ret) {
				dev->hdr.op_mode = hdr_cfg.hdr_mode;
				dev->hdr.esp_mode = hdr_cfg.esp.mode;
			}
		}

		/* normal read back mode */
		if (dev->hdr.op_mode == HDR_NORMAL &&
		    (dev->isp_inp & 0x7) == INP_RAWRD2)
			dev->hdr.op_mode = HDR_RDBK_FRAME1;

		writel(SW_IBUF_OP_MODE(dev->hdr.op_mode) |
		       SW_HDR_ESP_MODE(dev->hdr.esp_mode),
		       base + CSI2RX_CTRL0);
		writel(lanes - 1, base + CSI2RX_CTRL1);
		writel(0x3FFF, base + CSI2RX_CTRL2);
		val = SW_CSI_ID0(csi->mipi_di[0]) |
		      SW_CSI_ID1(csi->mipi_di[1]) |
		      SW_CSI_ID2(csi->mipi_di[2]) |
		      SW_CSI_ID3(csi->mipi_di[3]);
		writel(val, base + CSI2RX_DATA_IDS_1);
		val = SW_CSI_ID4(csi->mipi_di[4]);
		writel(val, base + CSI2RX_DATA_IDS_2);
		/* clear interrupts state */
		readl(base + CSI2RX_ERR_PHY);
		/* set interrupts mask */
		writel(0xF0FFFF, base + CSI2RX_MASK_PHY);
		writel(0xF1FFFFF, base + CSI2RX_MASK_PACKET);
		writel(0x7F7FF1, base + CSI2RX_MASK_OVERFLOW);
		writel(0x7FFFFF7F, base + CSI2RX_MASK_STAT);

		/* hdr merge */
		switch (dev->hdr.op_mode) {
		case HDR_RDBK_FRAME2:
		case HDR_FRAMEX2_DDR:
		case HDR_LINEX2_DDR:
		case HDR_LINEX2_NO_DDR:
			val = SW_HDRMGE_EN |
			      SW_HDRMGE_MODE_FRAMEX2;
			break;
		case HDR_RDBK_FRAME3:
		case HDR_FRAMEX3_DDR:
		case HDR_LINEX3_DDR:
			val = SW_HDRMGE_EN |
			      SW_HDRMGE_MODE_FRAMEX3;
			break;
		default:
			val = 0;
		}
		writel(val, base + ISP_HDRMGE_BASE);
		writel(val & SW_HDRMGE_EN, base + ISP_HDRTMO_BASE);

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "CSI2RX_IDS 0x%08x 0x%08x\n",
			 readl(base + CSI2RX_DATA_IDS_1),
			 readl(base + CSI2RX_DATA_IDS_2));
	} else {
		mipi_ctrl = CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
			    CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
			    CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
			    CIF_MIPI_CTRL_CLOCKLANE_ENA;

		writel(mipi_ctrl, base + CIF_MIPI_CTRL);

		/* Configure Data Type and Virtual Channel */
		writel(csi->mipi_di[0],
		       base + CIF_MIPI_IMG_DATA_SEL);

		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) |
		       CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_1);
		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) |
		       CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_2);
		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) |
		       CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_3);
		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) |
		       CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_4);

		/* Clear MIPI interrupts */
		writel(~0, base + CIF_MIPI_ICR);
		/*
		 * Disable CIF_MIPI_ERR_DPHY interrupt here temporary for
		 * isp bus may be dead when switch isp.
		 */
		writel(CIF_MIPI_FRAME_END | CIF_MIPI_ERR_CSI |
		       CIF_MIPI_ERR_DPHY | CIF_MIPI_SYNC_FIFO_OVFLW(0x0F) |
		       CIF_MIPI_ADD_DATA_OVFLW, base + CIF_MIPI_IMSC);

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "\n  MIPI_CTRL 0x%08x\n"
			 "  MIPI_IMG_DATA_SEL 0x%08x\n"
			 "  MIPI_STATUS 0x%08x\n"
			 "  MIPI_IMSC 0x%08x\n",
			 readl(base + CIF_MIPI_CTRL),
			 readl(base + CIF_MIPI_IMG_DATA_SEL),
			 readl(base + CIF_MIPI_STATUS),
			 readl(base + CIF_MIPI_IMSC));
	}

	return 0;
}

int rkisp_csi_config_patch(struct rkisp_device *dev)
{
	int val = 0, ret = 0;

	if (dev->isp_inp & INP_CSI) {
		ret = csi_config(&dev->csi_dev);
	} else {
		switch (dev->isp_inp & 0x7) {
		case INP_RAWRD2 | INP_RAWRD0:
			dev->hdr.op_mode = HDR_RDBK_FRAME2;
			val = SW_HDRMGE_EN |
				SW_HDRMGE_MODE_FRAMEX2;
			break;
		case INP_RAWRD2 | INP_RAWRD1 | INP_RAWRD0:
			dev->hdr.op_mode = HDR_RDBK_FRAME3;
			val = SW_HDRMGE_EN |
				SW_HDRMGE_MODE_FRAMEX3;
			break;
		default: //INP_RAWRD2
			dev->hdr.op_mode = HDR_RDBK_FRAME1;
		}
		writel(SW_IBUF_OP_MODE(dev->hdr.op_mode),
		       dev->base_addr + CSI2RX_CTRL0);
		writel(val, dev->base_addr + ISP_HDRMGE_BASE);
		writel(val & SW_HDRMGE_EN, dev->base_addr + ISP_HDRTMO_BASE);
		writel(0x7FFFFF7F, dev->base_addr + CSI2RX_MASK_STAT);
		writel(0, dev->base_addr + CSI2RX_MASK_PACKET);
		writel(0, dev->base_addr + CSI2RX_MASK_PHY);
	}

	if (IS_HDR_RDBK(dev->hdr.op_mode))
		isp_set_bits(dev->base_addr + CTRL_SWS_CFG,
			     0, SW_MPIP_DROP_FRM_DIS);
	dev->csi_dev.is_isp_end = true;
	return ret;
}

/*
 * for hdr read back mode, rawrd read back data
 * this will update rawrd base addr to shadow.
 */
void rkisp_trigger_read_back(struct rkisp_csi_device *csi, u8 dma2frm)
{
	struct rkisp_device *dev = csi->ispdev;
	void __iomem *addr = dev->base_addr + CSI2RX_CTRL0;
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	u32 cur_frame_id;

	rkisp_dmarx_get_frame(dev, &cur_frame_id, NULL, true);
	if (dma2frm > 2)
		dma2frm = 2;
	memset(csi->filt_state, 0, sizeof(csi->filt_state));
	switch (dev->hdr.op_mode) {
	case HDR_RDBK_FRAME3://is rawrd1 rawrd0 rawrd2
		csi->filt_state[CSI_F_RD1] = dma2frm;
	case HDR_RDBK_FRAME2://is rawrd0 and rawrd2
		csi->filt_state[CSI_F_RD0] = dma2frm;
	case HDR_RDBK_FRAME1://only rawrd2
		csi->filt_state[CSI_F_RD2] = dma2frm;
		break;
	default://other no support readback
		return;
	}

	/* configure hdr params in rdbk mode */
	rkisp_params_cfg(params_vdev, cur_frame_id, dma2frm + 1);

	/* not using isp V_START irq to generate sof event */
	csi->filt_state[CSI_F_VS] = dma2frm + 1;
	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "isp readback frame:%d time:%d\n",
		 cur_frame_id, dma2frm + 1);
	writel(SW_DMA_2FRM_MODE(dma2frm) | SW_IBUF_OP_MODE(dev->hdr.op_mode) |
		   SW_CSI2RX_EN | readl(addr), addr);
}

/* handle read back event from user or isp idle isr */
int rkisp_csi_trigger_event(struct rkisp_csi_device *csi, void *arg)
{
	struct rkisp_device *dev = csi->ispdev;
	struct kfifo *fifo = &csi->rdbk_kfifo;
	struct isp2x_csi_trigger *trigger =
		(struct isp2x_csi_trigger *)arg;
	struct isp2x_csi_trigger t;
	unsigned long lock_flags = 0;
	int times = -1;

	if (!IS_HDR_RDBK(dev->hdr.op_mode))
		return 0;

	spin_lock_irqsave(&csi->rdbk_lock, lock_flags);
	if (!trigger)
		csi->is_isp_end = true;

	/* isp doesn't ready to read back */
	if (!(dev->isp_state & ISP_START)) {
		if (trigger)
			kfifo_in(fifo, trigger, sizeof(*trigger));
		csi->is_isp_end = true;
		goto end;
	}

	if (trigger &&
	    (csi->is_isp_end && kfifo_is_empty(fifo))) {
		/* isp idle and no event in queue
		 * start read back direct
		 */
		dev->dmarx_dev.pre_frame = dev->dmarx_dev.cur_frame;
		dev->dmarx_dev.cur_frame.id = trigger->frame_id;
		dev->dmarx_dev.cur_frame.timestamp = trigger->frame_timestamp;
		times = trigger->times;
		csi->is_isp_end = false;
	} else if (csi->is_isp_end && !kfifo_is_empty(fifo)) {
		/* isp idle and events in queue
		 * out fifo then start read back
		 * new event in fifo
		 */
		if (kfifo_out(fifo, &t, sizeof(t))) {
			dev->dmarx_dev.pre_frame = dev->dmarx_dev.cur_frame;
			dev->dmarx_dev.cur_frame.id = t.frame_id;
			dev->dmarx_dev.cur_frame.timestamp = t.frame_timestamp;
			times = t.times;
		}
		if (trigger)
			kfifo_in(fifo, trigger, sizeof(*trigger));
		csi->is_isp_end = false;
	} else if (!csi->is_isp_end && trigger) {
		/* isp on idle, new event in fifo */
		if (!kfifo_is_full(fifo))
			kfifo_in(fifo, trigger, sizeof(*trigger));
		else
			v4l2_err(&dev->v4l2_dev,
				 "csi trigger fifo is full\n");
	}
end:
	spin_unlock_irqrestore(&csi->rdbk_lock, lock_flags);

	if (times >= 0)
		rkisp_trigger_read_back(csi, times);
	return 0;
}

void rkisp_csi_sof(struct rkisp_device *dev, u8 id)
{
	/* to get long frame vc_start */
	switch (dev->hdr.op_mode) {
	case HDR_RDBK_FRAME1:
		if (id != HDR_DMA2)
			return;
		break;
	case HDR_RDBK_FRAME2:
	case HDR_FRAMEX2_DDR:
	case HDR_LINEX2_DDR:
		if (id != HDR_DMA0)
			return;
		break;
	case HDR_RDBK_FRAME3:
	case HDR_FRAMEX3_DDR:
	case HDR_LINEX3_DDR:
		if (id != HDR_DMA1)
			return;
		break;
	default:
		return;
	}

	rkisp_isp_queue_event_sof(&dev->isp_sdev);
}

int rkisp_register_csi_subdev(struct rkisp_device *dev,
			       struct v4l2_device *v4l2_dev)
{
	struct rkisp_csi_device *csi_dev = &dev->csi_dev;
	struct v4l2_subdev *sd;
	int ret;

	memset(csi_dev, 0, sizeof(*csi_dev));
	csi_dev->ispdev = dev;
	sd = &csi_dev->sd;

	v4l2_subdev_init(sd, &rkisp_csi_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &rkisp_csi_media_ops;
	sd->entity.function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
	snprintf(sd->name, sizeof(sd->name), CSI_DEV_NAME);

	csi_dev->pads[CSI_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	csi_dev->pads[CSI_SRC_CH0].flags =
		MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;

	csi_dev->max_pad = CSI_SRC_CH0 + 1;
	if (dev->isp_ver == ISP_V12 ||
	    dev->isp_ver == ISP_V13) {
		csi_dev->max_pad = CSI_SRC_CH1 + 1;
		csi_dev->pads[CSI_SRC_CH1].flags = MEDIA_PAD_FL_SOURCE;
	} else if (dev->isp_ver == ISP_V20) {
		csi_dev->max_pad = CSI_PAD_MAX;
		csi_dev->pads[CSI_SRC_CH1].flags = MEDIA_PAD_FL_SOURCE;
		csi_dev->pads[CSI_SRC_CH2].flags = MEDIA_PAD_FL_SOURCE;
		csi_dev->pads[CSI_SRC_CH3].flags = MEDIA_PAD_FL_SOURCE;
		csi_dev->pads[CSI_SRC_CH4].flags = MEDIA_PAD_FL_SOURCE;
	}

	ret = media_entity_pads_init(&sd->entity, csi_dev->max_pad,
				     csi_dev->pads);
	if (ret < 0)
		return ret;

	spin_lock_init(&csi_dev->rdbk_lock);
	ret = kfifo_alloc(&csi_dev->rdbk_kfifo,
			  16 * sizeof(struct isp2x_csi_trigger),
			  GFP_KERNEL);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to alloc csi kfifo %d", ret);
		goto free_media;
	}

	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, csi_dev);
	sd->grp_id = GRP_ID_CSI;
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register csi subdev\n");
		goto free_kfifo;
	}

	return 0;
free_kfifo:
	kfifo_free(&csi_dev->rdbk_kfifo);
free_media:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void rkisp_unregister_csi_subdev(struct rkisp_device *dev)
{
	struct v4l2_subdev *sd = &dev->csi_dev.sd;

	kfifo_free(&dev->csi_dev.rdbk_kfifo);
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}
