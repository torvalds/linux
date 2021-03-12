// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/iopoll.h>
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
				  struct v4l2_subdev **sensor_sd, u32 function)
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
		if (entity->function == function)
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

	memset(csi->tx_first, 0, sizeof(csi->tx_first));

	if (!IS_HDR_RDBK(dev->hdr.op_mode))
		return 0;
	if (on)
		rkisp_write(dev, CSI2RX_Y_STAT_CTRL, SW_Y_STAT_EN, true);
	else
		rkisp_write(dev, CSI2RX_Y_STAT_CTRL, 0, true);
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
	get_remote_mipi_sensor(dev, &mipi_sensor, MEDIA_ENT_F_CAM_SENSOR);
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
		rkisp_write(dev, CIF_ISP_CSI0_CTRL1, lanes - 1, true);

		/* linecnt */
		rkisp_write(dev, CIF_ISP_CSI0_CTRL2, 0x3FFF, true);

		/* Configure Data Type and Virtual Channel */
		rkisp_write(dev, CIF_ISP_CSI0_DATA_IDS_1,
			    csi->mipi_di[0] | csi->mipi_di[1] << 8, true);

		/* clear interrupts state */
		rkisp_read(dev, CIF_ISP_CSI0_ERR1, true);
		rkisp_read(dev, CIF_ISP_CSI0_ERR2, true);
		rkisp_read(dev, CIF_ISP_CSI0_ERR3, true);
		/* set interrupts mask */
		rkisp_write(dev, CIF_ISP_CSI0_MASK1, 0x1FFFFFF0, true);
		rkisp_write(dev, CIF_ISP_CSI0_MASK2, 0x03FFFFFF, true);
		rkisp_write(dev, CIF_ISP_CSI0_MASK3,
			    CIF_ISP_CSI0_IMASK_FRAME_END(0x3F) |
			    CIF_ISP_CSI0_IMASK_RAW0_OUT_V_END |
			    CIF_ISP_CSI0_IMASK_RAW1_OUT_V_END |
			    CIF_ISP_CSI0_IMASK_LINECNT, true);

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "CSI0_CTRL1 0x%08x\n"
			 "CSI0_IDS 0x%08x\n"
			 "CSI0_MASK3 0x%08x\n",
			 rkisp_read(dev, CIF_ISP_CSI0_CTRL1, true),
			 rkisp_read(dev, CIF_ISP_CSI0_DATA_IDS_1, true),
			 rkisp_read(dev, CIF_ISP_CSI0_MASK3, true));
	} else if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		bool is_feature_on = dev->hw_dev->is_feature_on;
		u64 iq_feature = dev->hw_dev->iq_feature;
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
		    (dev->isp_inp & INP_RAWRD2 || !dev->hw_dev->is_single))
			dev->hdr.op_mode = HDR_RDBK_FRAME1;

		/* op_mode update by mi_cfg_upd */
		if (!dev->hw_dev->is_mi_update)
			rkisp_write(dev, CSI2RX_CTRL0,
				    SW_IBUF_OP_MODE(dev->hdr.op_mode) |
				    SW_HDR_ESP_MODE(dev->hdr.esp_mode), true);
		rkisp_write(dev, CSI2RX_CTRL1, lanes - 1, true);
		rkisp_write(dev, CSI2RX_CTRL2, 0x3FFF, true);
		val = SW_CSI_ID1(csi->mipi_di[1]) |
		      SW_CSI_ID2(csi->mipi_di[2]) |
		      SW_CSI_ID3(csi->mipi_di[3]);
		/* CSI_ID0 is for dmarx when read back mode */
		if (dev->hw_dev->is_single) {
			val |= SW_CSI_ID0(csi->mipi_di[0]);
			rkisp_write(dev, CSI2RX_DATA_IDS_1, val, true);
		} else {
			rkisp_set_bits(dev, CSI2RX_DATA_IDS_1, 0, val, true);
			for (i = 0; i < dev->hw_dev->dev_num; i++)
				rkisp_set_bits(dev->hw_dev->isp[i],
					CSI2RX_DATA_IDS_1, 0, val, false);
		}
		val = SW_CSI_ID4(csi->mipi_di[4]);
		rkisp_write(dev, CSI2RX_DATA_IDS_2, val, true);
		/* clear interrupts state */
		rkisp_read(dev, CSI2RX_ERR_PHY, true);
		/* set interrupts mask */
		rkisp_write(dev, CSI2RX_MASK_PHY, 0xF0FFFF, true);
		rkisp_write(dev, CSI2RX_MASK_PACKET, 0xF1FFFFF, true);
		rkisp_write(dev, CSI2RX_MASK_OVERFLOW, 0x7F7FF1, true);
		rkisp_write(dev, CSI2RX_MASK_STAT, 0x7FFFFF7F, true);

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
		if (is_feature_on) {
			if ((ISP2X_MODULE_HDRMGE & ~iq_feature) && (val & SW_HDRMGE_EN)) {
				v4l2_err(&dev->v4l2_dev, "hdrmge is not supported\n");
				return -EINVAL;
			}
		}
		rkisp_write(dev, ISP_HDRMGE_BASE, val, false);

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "CSI2RX_IDS 0x%08x 0x%08x\n",
			 rkisp_read(dev, CSI2RX_DATA_IDS_1, true),
			 rkisp_read(dev, CSI2RX_DATA_IDS_2, true));
	} else {
		mipi_ctrl = CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
			    CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
			    CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
			    CIF_MIPI_CTRL_CLOCKLANE_ENA;

		rkisp_write(dev, CIF_MIPI_CTRL, mipi_ctrl, true);

		/* Configure Data Type and Virtual Channel */
		rkisp_write(dev, CIF_MIPI_IMG_DATA_SEL,
			    csi->mipi_di[0], true);

		rkisp_write(dev, CIF_MIPI_ADD_DATA_SEL_1,
			    CIF_MIPI_DATA_SEL_DT(emd_dt) |
			    CIF_MIPI_DATA_SEL_VC(emd_vc), true);
		rkisp_write(dev, CIF_MIPI_ADD_DATA_SEL_2,
			    CIF_MIPI_DATA_SEL_DT(emd_dt) |
			    CIF_MIPI_DATA_SEL_VC(emd_vc), true);
		rkisp_write(dev, CIF_MIPI_ADD_DATA_SEL_3,
			    CIF_MIPI_DATA_SEL_DT(emd_dt) |
			    CIF_MIPI_DATA_SEL_VC(emd_vc), true);
		rkisp_write(dev, CIF_MIPI_ADD_DATA_SEL_4,
			    CIF_MIPI_DATA_SEL_DT(emd_dt) |
			    CIF_MIPI_DATA_SEL_VC(emd_vc), true);

		/* Clear MIPI interrupts */
		rkisp_write(dev, CIF_MIPI_ICR, ~0, true);
		/*
		 * Disable CIF_MIPI_ERR_DPHY interrupt here temporary for
		 * isp bus may be dead when switch isp.
		 */
		rkisp_write(dev, CIF_MIPI_IMSC,
			    CIF_MIPI_FRAME_END | CIF_MIPI_ERR_CSI |
			    CIF_MIPI_ERR_DPHY | CIF_MIPI_SYNC_FIFO_OVFLW(0x0F) |
			    CIF_MIPI_ADD_DATA_OVFLW, true);

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "\n  MIPI_CTRL 0x%08x\n"
			 "  MIPI_IMG_DATA_SEL 0x%08x\n"
			 "  MIPI_STATUS 0x%08x\n"
			 "  MIPI_IMSC 0x%08x\n",
			 rkisp_read(dev, CIF_MIPI_CTRL, true),
			 rkisp_read(dev, CIF_MIPI_IMG_DATA_SEL, true),
			 rkisp_read(dev, CIF_MIPI_STATUS, true),
			 rkisp_read(dev, CIF_MIPI_IMSC, true));
	}

	return 0;
}

int rkisp_csi_config_patch(struct rkisp_device *dev)
{
	int val = 0, ret = 0;
	struct v4l2_subdev *mipi_sensor;
	bool is_feature_on = dev->hw_dev->is_feature_on;
	u64 iq_feature = dev->hw_dev->iq_feature;

	if (dev->isp_inp & INP_CSI) {
		dev->hw_dev->mipi_dev_id = dev->dev_id;
		ret = csi_config(&dev->csi_dev);
	} else {
		if (dev->isp_inp & INP_CIF) {
			struct rkmodule_hdr_cfg hdr_cfg;

			get_remote_mipi_sensor(dev, &mipi_sensor, MEDIA_ENT_F_PROC_VIDEO_COMPOSER);
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
			if (dev->hdr.op_mode == HDR_NORMAL)
				dev->hdr.op_mode = HDR_RDBK_FRAME1;
		} else {
			switch (dev->isp_inp & 0x7) {
			case INP_RAWRD2 | INP_RAWRD0:
				dev->hdr.op_mode = HDR_RDBK_FRAME2;
				break;
			case INP_RAWRD2 | INP_RAWRD1 | INP_RAWRD0:
				dev->hdr.op_mode = HDR_RDBK_FRAME3;
				break;
			default: //INP_RAWRD2
				dev->hdr.op_mode = HDR_RDBK_FRAME1;
			}
		}

		if (dev->hdr.op_mode == HDR_RDBK_FRAME2)
			val = SW_HDRMGE_EN | SW_HDRMGE_MODE_FRAMEX2;
		else if (dev->hdr.op_mode == HDR_RDBK_FRAME3)
			val = SW_HDRMGE_EN | SW_HDRMGE_MODE_FRAMEX3;

		if (!dev->hw_dev->is_mi_update)
			rkisp_write(dev, CSI2RX_CTRL0,
				    SW_IBUF_OP_MODE(dev->hdr.op_mode), true);

		if (is_feature_on) {
			if ((ISP2X_MODULE_HDRMGE & ~iq_feature) && (val & SW_HDRMGE_EN)) {
				v4l2_err(&dev->v4l2_dev, "hdrmge is not supported\n");
				return -EINVAL;
			}
		}
		rkisp_write(dev, ISP_HDRMGE_BASE, val, false);
		rkisp_write(dev, CSI2RX_MASK_STAT, 0x7FFFFF7F, true);
	}

	if (IS_HDR_RDBK(dev->hdr.op_mode)) {
		rkisp_set_bits(dev, CTRL_SWS_CFG,
			       0, SW_MPIP_DROP_FRM_DIS, true);
	}
	memset(dev->csi_dev.filt_state, 0, sizeof(dev->csi_dev.filt_state));
	dev->csi_dev.frame_cnt = -1;
	dev->csi_dev.frame_cnt_x1 = -1;
	dev->csi_dev.frame_cnt_x2 = -1;
	dev->csi_dev.frame_cnt_x3 = -1;
	dev->csi_dev.rd_mode = dev->hdr.op_mode;
	return ret;
}

/*
 * for hdr read back mode, rawrd read back data
 * this will update rawrd base addr to shadow.
 */
void rkisp_trigger_read_back(struct rkisp_csi_device *csi, u8 dma2frm, u32 mode)
{
	struct rkisp_device *dev = csi->ispdev;
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	u32 val, cur_frame_id, tmp, rd_mode;
	bool is_feature_on = hw->is_feature_on;
	u64 iq_feature = hw->iq_feature;
	bool is_upd = false, is_3dlut_upd = false;

	if (dev->isp_ver == ISP_V21)
		dma2frm = 0;
	hw->cur_dev_id = dev->dev_id;
	rkisp_dmarx_get_frame(dev, &cur_frame_id, NULL, NULL, true);
	if (dma2frm > 2)
		dma2frm = 2;
	if (dma2frm == 2)
		csi->frame_cnt_x3++;
	else if (dma2frm == 1)
		csi->frame_cnt_x2++;
	else
		csi->frame_cnt_x1++;
	csi->frame_cnt++;

	val = 0;
	if (mode & T_START_X1) {
		rd_mode = HDR_RDBK_FRAME1;
	} else if (mode & T_START_X2) {
		rd_mode = HDR_RDBK_FRAME2;
		val = SW_HDRMGE_EN | SW_HDRMGE_MODE_FRAMEX2;
	} else if (mode & T_START_X3) {
		rd_mode = HDR_RDBK_FRAME3;
		val = SW_HDRMGE_EN | SW_HDRMGE_MODE_FRAMEX3;
	} else {
		rd_mode = csi->rd_mode;
		val = rkisp_read(dev, ISP_HDRMGE_BASE, false) & 0xf;
	}

	if (is_feature_on) {
		if ((ISP2X_MODULE_HDRMGE & ~iq_feature) && (val & SW_HDRMGE_EN)) {
			v4l2_err(&dev->v4l2_dev, "hdrmge is not supported\n");
			return;
		}
	}

	tmp = rkisp_read(dev, ISP_HDRMGE_BASE, false) & 0xf;
	if (val != tmp) {
		rkisp_write(dev, ISP_HDRMGE_BASE, val, false);
		dev->skip_frame = 2;
		is_upd = true;
	}
	if (dev->isp_ver == ISP_V20 && dev->dmarx_dev.trigger == T_MANUAL) {
		if (csi->rd_mode != rd_mode && dev->br_dev.en) {
			tmp = dev->isp_sdev.in_crop.height;
			val = rkisp_read(dev, CIF_DUAL_CROP_CTRL, false);
			if (rd_mode == HDR_RDBK_FRAME1) {
				val |= CIF_DUAL_CROP_MP_MODE_YUV;
				tmp += RKMODULE_EXTEND_LINE;
			} else {
				val &= ~CIF_DUAL_CROP_MP_MODE_YUV;
			}
			rkisp_write(dev, CIF_DUAL_CROP_CTRL, val, false);
			rkisp_write(dev, CIF_ISP_ACQ_V_SIZE, tmp, false);
			rkisp_write(dev, CIF_ISP_OUT_V_SIZE, tmp, false);
		}
		csi->rd_mode = rd_mode;
		rkisp_rawrd_set_pic_size(dev,
			dev->dmarx_dev.stream[RKISP_STREAM_RAWRD2].out_fmt.width,
			dev->dmarx_dev.stream[RKISP_STREAM_RAWRD2].out_fmt.height);
	}
	csi->rd_mode = rd_mode;

	/* configure hdr params in rdbk mode */
	if (is_upd)
		rkisp_params_first_cfg(&dev->params_vdev,
				       &dev->isp_sdev.in_fmt,
				       dev->isp_sdev.quantization);
	rkisp_params_cfg(params_vdev, cur_frame_id, dma2frm + 1);

	if (!hw->is_single) {
		rkisp_update_regs(dev, CTRL_VI_ISP_PATH, SUPER_IMP_COLOR_CR);
		rkisp_update_regs(dev, DUAL_CROP_M_H_OFFS, DUAL_CROP_S_V_SIZE);
		rkisp_update_regs(dev, ISP_ACQ_PROP, DUAL_CROP_CTRL);
		rkisp_update_regs(dev, MAIN_RESIZE_SCALE_HY, MI_WR_CTRL);
		rkisp_update_regs(dev, SELF_RESIZE_SCALE_HY, MAIN_RESIZE_CTRL);
		rkisp_update_regs(dev, ISP_GAMMA_OUT_CTRL, SELF_RESIZE_CTRL);
		rkisp_update_regs(dev, MI_RD_CTRL2, ISP_LSC_CTRL);
		rkisp_update_regs(dev, MI_MP_WR_Y_BASE, MI_MP_WR_Y_LLENGTH);
		rkisp_update_regs(dev, ISP_LSC_XGRAD_01, ISP_RAWAWB_RAM_DATA);
		if (dev->isp_ver == ISP_V21) {
			val = rkisp_read(dev, MI_WR_CTRL2, false);
			rkisp_set_bits(dev, MI_WR_CTRL2, 0, val, true);
			rkisp_write(dev, MI_WR_INIT, ISP21_SP_FORCE_UPD | ISP21_MP_FORCE_UPD, true);
			/* sensor mode & index */
			rkisp_set_bits(dev, ISP_ACQ_H_OFFS, ISP21_SENSOR_MODE(3) | ISP21_SENSOR_INDEX(3),
				       ISP21_SENSOR_MODE(hw->dev_num >= 3 ? 2 : hw->dev_num - 1) |
				       ISP21_SENSOR_INDEX(dev->dev_id), true);
		}
		is_upd = true;
	}

	if (IS_HDR_RDBK(dev->csi_dev.rd_mode))
		rkisp_params_cfgsram(params_vdev);

	/* read 3d lut at frame end */
	if (hw->is_single && is_upd &&
	    rkisp_read_reg_cache(dev, ISP_3DLUT_UPDATE) & 0x1) {
		rkisp_write(dev, ISP_3DLUT_UPDATE, 0, true);
		is_3dlut_upd = true;
	}
	if (is_upd) {
		val = rkisp_read(dev, ISP_CTRL, false);
		val |= CIF_ISP_CTRL_ISP_CFG_UPD;
		rkisp_write(dev, ISP_CTRL, val, true);
	}
	if (is_3dlut_upd)
		rkisp_write(dev, ISP_3DLUT_UPDATE, 1, true);

	memset(csi->filt_state, 0, sizeof(csi->filt_state));
	csi->filt_state[CSI_F_VS] = dma2frm;

	val = rkisp_read(dev, CSI2RX_CTRL0, true);
	val &= ~SW_IBUF_OP_MODE(0xf);
	tmp = SW_IBUF_OP_MODE(csi->rd_mode);
	val |= tmp | SW_CSI2RX_EN | SW_DMA_2FRM_MODE(dma2frm);
	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "readback frame:%d time:%d 0x%x\n",
		 cur_frame_id, dma2frm + 1, val);
	if (!hw->is_shutdown)
		rkisp_write(dev, CSI2RX_CTRL0, val, true);
}

static void rkisp_dev_trigger_handle(struct rkisp_device *dev, u32 cmd)
{
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_device *isp = NULL;
	struct isp2x_csi_trigger t = { 0 };
	unsigned long lock_flags = 0;
	int i, times = -1, max = 0, id = 0;
	int len[DEV_MAX] = { 0 };
	u32 mode = 0;

	spin_lock_irqsave(&hw->rdbk_lock, lock_flags);
	if (cmd == T_CMD_END)
		hw->is_idle = true;
	if (hw->is_shutdown)
		hw->is_idle = false;
	if (!hw->is_idle)
		goto end;
	for (i = 0; i < hw->dev_num; i++) {
		isp = hw->isp[i];
		if (!(isp->isp_state & ISP_START))
			continue;
		rkisp_csi_trigger_event(isp, T_CMD_LEN, &len[i]);
		if (max < len[i]) {
			max = len[i];
			id = i;
		}
	}

	if (max) {
		v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
			 "handle isp%d, trigger fifo len:%d\n",
			 id, max);
		isp = hw->isp[id];
		rkisp_csi_trigger_event(isp, T_CMD_DEQUEUE, &t);
		isp->dmarx_dev.pre_frame = isp->dmarx_dev.cur_frame;
		isp->dmarx_dev.cur_frame.id = t.frame_id;
		isp->dmarx_dev.cur_frame.sof_timestamp = t.sof_timestamp;
		isp->dmarx_dev.cur_frame.timestamp = t.frame_timestamp;
		mode = t.mode;
		times = t.times;
		hw->cur_dev_id = id;
		hw->is_idle = false;
	}
end:
	spin_unlock_irqrestore(&hw->rdbk_lock, lock_flags);
	if (times >= 0)
		rkisp_trigger_read_back(&isp->csi_dev, times, mode);
}

/* handle read back event from user or isp idle isr */
int rkisp_csi_trigger_event(struct rkisp_device *dev, u32 cmd, void *arg)
{
	struct rkisp_csi_device *csi = &dev->csi_dev;
	struct kfifo *fifo = &csi->rdbk_kfifo;
	struct isp2x_csi_trigger *trigger = NULL;
	unsigned long lock_flags = 0;
	int val, ret = 0;

	if (dev->dmarx_dev.trigger != T_MANUAL)
		return 0;

	spin_lock_irqsave(&csi->rdbk_lock, lock_flags);
	switch (cmd) {
	case T_CMD_QUEUE:
		trigger = arg;
		if (!trigger)
			break;
		if (!kfifo_is_full(fifo))
			kfifo_in(fifo, trigger, sizeof(*trigger));
		else
			v4l2_err(&dev->v4l2_dev, "csi fifo is full\n");
		break;
	case T_CMD_DEQUEUE:
		if (!kfifo_is_empty(fifo))
			ret = kfifo_out(fifo, arg,
					sizeof(struct isp2x_csi_trigger));
		if (!ret)
			ret = -EINVAL;
		break;
	case T_CMD_LEN:
		val = kfifo_len(fifo) / sizeof(struct isp2x_csi_trigger);
		*(u32 *)arg = val;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&csi->rdbk_lock, lock_flags);

	if (cmd == T_CMD_QUEUE || cmd == T_CMD_END)
		rkisp_dev_trigger_handle(dev, cmd);
	return ret;
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
	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
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
