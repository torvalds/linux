// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
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
#include "dev.h"
#include <linux/regulator/consumer.h>
#include <linux/rk-camera-module.h>
#include "common.h"

static inline struct sditf_priv *to_sditf_priv(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct sditf_priv, sd);
}

static void sditf_buffree_work(struct work_struct *work)
{
	struct sditf_work_struct *buffree_work = container_of(work,
							      struct sditf_work_struct,
							      work);
	struct sditf_priv *priv = container_of(buffree_work,
						struct sditf_priv,
						buffree_work);
	struct rkcif_rx_buffer *rx_buf = NULL;
	unsigned long flags;
	LIST_HEAD(local_list);

	spin_lock_irqsave(&priv->cif_dev->buffree_lock, flags);
	list_replace_init(&priv->buf_free_list, &local_list);
	while (!list_empty(&local_list)) {
		rx_buf = list_first_entry(&local_list,
					  struct rkcif_rx_buffer, list_free);
		if (rx_buf) {
			list_del(&rx_buf->list_free);
			rkcif_free_reserved_mem_buf(priv->cif_dev, rx_buf);
		}
	}
	spin_unlock_irqrestore(&priv->cif_dev->buffree_lock, flags);
}

static void sditf_get_hdr_mode(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct rkmodule_hdr_cfg hdr_cfg;
	int ret = 0;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		ret = v4l2_subdev_call(cif_dev->terminal_sensor.sd,
				       core, ioctl,
				       RKMODULE_GET_HDR_CFG,
				       &hdr_cfg);
		if (!ret)
			priv->hdr_cfg = hdr_cfg;
		else
			priv->hdr_cfg.hdr_mode = NO_HDR;
	} else {
		priv->hdr_cfg.hdr_mode = NO_HDR;
	}
}

static int sditf_g_frame_interval(struct v4l2_subdev *sd,
				  struct v4l2_subdev_frame_interval *fi)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		sensor_sd = cif_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, video, g_frame_interval, fi);
	}

	return -EINVAL;
}

static int sditf_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
			       struct v4l2_mbus_config *config)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;

	if (!cif_dev->active_sensor)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->active_sensor) {
		sensor_sd = cif_dev->active_sensor->sd;
		return v4l2_subdev_call(sensor_sd, pad, get_mbus_config, 0, config);
	} else {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
		return 0;
	}

	return -EINVAL;
}

static int sditf_get_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev_selection input_sel;
	struct v4l2_pix_format_mplane pixm;
	const struct cif_output_fmt *out_fmt;
	int ret = -EINVAL;
	bool is_uncompact = false;

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		sditf_get_hdr_mode(priv);
		fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt->pad = 0;
		ret = v4l2_subdev_call(cif_dev->terminal_sensor.sd, pad, get_fmt, NULL, fmt);
		if (ret) {
			v4l2_err(&priv->sd,
				 "%s: get sensor format failed\n", __func__);
			return ret;
		}

		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
		input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		input_sel.pad = 0;
		ret = v4l2_subdev_call(cif_dev->terminal_sensor.sd,
				       pad, get_selection, NULL,
				       &input_sel);
		if (!ret) {
			fmt->format.width = input_sel.r.width;
			fmt->format.height = input_sel.r.height;
		}
		priv->cap_info.width = fmt->format.width;
		priv->cap_info.height = fmt->format.height;
		pixm.pixelformat = rkcif_mbus_pixelcode_to_v4l2(fmt->format.code);
		pixm.width = priv->cap_info.width;
		pixm.height = priv->cap_info.height;
		out_fmt = rkcif_find_output_fmt(NULL, pixm.pixelformat);
		if (priv->toisp_inf.link_mode == TOISP_UNITE &&
		    ((pixm.width / 2 - RKMOUDLE_UNITE_EXTEND_PIXEL) * out_fmt->raw_bpp / 8) & 0xf)
			is_uncompact = true;
		v4l2_dbg(3, rkcif_debug, &cif_dev->v4l2_dev,
			"%s, width %d, height %d, hdr mode %d\n",
			__func__, fmt->format.width, fmt->format.height, priv->hdr_cfg.hdr_mode);
		if (priv->hdr_cfg.hdr_mode == NO_HDR ||
		    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
			rkcif_set_fmt(&cif_dev->stream[0], &pixm, false);
		} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
			if (is_uncompact) {
				cif_dev->stream[0].is_compact = false;
				cif_dev->stream[0].is_high_align = true;
			} else {
				cif_dev->stream[0].is_compact = true;
			}
			rkcif_set_fmt(&cif_dev->stream[0], &pixm, false);
			rkcif_set_fmt(&cif_dev->stream[1], &pixm, false);
		} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
			if (is_uncompact) {
				cif_dev->stream[0].is_compact = false;
				cif_dev->stream[0].is_high_align = true;
				cif_dev->stream[1].is_compact = false;
				cif_dev->stream[1].is_high_align = true;
			} else {
				cif_dev->stream[0].is_compact = true;
				cif_dev->stream[1].is_compact = true;
			}
			rkcif_set_fmt(&cif_dev->stream[0], &pixm, false);
			rkcif_set_fmt(&cif_dev->stream[1], &pixm, false);
			rkcif_set_fmt(&cif_dev->stream[2], &pixm, false);
		}
	} else {
		if (priv->sensor_sd) {
			fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
			fmt->pad = 0;
			ret = v4l2_subdev_call(priv->sensor_sd, pad, get_fmt, NULL, fmt);
			if (ret) {
				v4l2_err(&priv->sd,
					 "%s: get sensor format failed\n", __func__);
				return ret;
			}

			input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
			input_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			input_sel.pad = 0;
			ret = v4l2_subdev_call(priv->sensor_sd,
					       pad, get_selection, NULL,
					       &input_sel);
			if (!ret) {
				fmt->format.width = input_sel.r.width;
				fmt->format.height = input_sel.r.height;
			}
			priv->cap_info.width = fmt->format.width;
			priv->cap_info.height = fmt->format.height;
			pixm.pixelformat = rkcif_mbus_pixelcode_to_v4l2(fmt->format.code);
			pixm.width = priv->cap_info.width;
			pixm.height = priv->cap_info.height;
		} else {
			fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
			fmt->pad = 0;
			fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
			fmt->format.width = 640;
			fmt->format.height = 480;
		}
	}

	return 0;
}

static int sditf_init_buf(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	int ret = 0;

	if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		if (priv->mode.rdbk_mode == RKISP_VICAP_RDBK_AUTO) {
			if (cif_dev->is_thunderboot)
				cif_dev->resmem_size /= 2;
			ret = rkcif_init_rx_buf(&cif_dev->stream[0], priv->buf_num);
			if (cif_dev->is_thunderboot)
				cif_dev->resmem_pa += cif_dev->resmem_size;
			ret |= rkcif_init_rx_buf(&cif_dev->stream[1], priv->buf_num);
		} else {
			ret = rkcif_init_rx_buf(&cif_dev->stream[0], priv->buf_num);
		}
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		if (priv->mode.rdbk_mode == RKISP_VICAP_RDBK_AUTO) {
			if (cif_dev->is_thunderboot)
				cif_dev->resmem_size /= 3;
			ret = rkcif_init_rx_buf(&cif_dev->stream[0], priv->buf_num);
			if (cif_dev->is_thunderboot)
				cif_dev->resmem_pa += cif_dev->resmem_size;
			ret |= rkcif_init_rx_buf(&cif_dev->stream[1], priv->buf_num);
			if (cif_dev->is_thunderboot)
				cif_dev->resmem_pa += cif_dev->resmem_size;
			ret |= rkcif_init_rx_buf(&cif_dev->stream[2], priv->buf_num);
		} else {
			ret = rkcif_init_rx_buf(&cif_dev->stream[0], priv->buf_num);
			ret |= rkcif_init_rx_buf(&cif_dev->stream[1], priv->buf_num);
		}
	} else {
		if (priv->mode.rdbk_mode == RKISP_VICAP_RDBK_AUTO)
			ret = rkcif_init_rx_buf(&cif_dev->stream[0], priv->buf_num);
		else
			ret = -EINVAL;
	}
	return ret;
}

static void sditf_free_buf(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;

	if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		rkcif_free_rx_buf(&cif_dev->stream[0], priv->buf_num);
		rkcif_free_rx_buf(&cif_dev->stream[1], priv->buf_num);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		rkcif_free_rx_buf(&cif_dev->stream[0], priv->buf_num);
		rkcif_free_rx_buf(&cif_dev->stream[1], priv->buf_num);
		rkcif_free_rx_buf(&cif_dev->stream[2], priv->buf_num);
	} else {
		rkcif_free_rx_buf(&cif_dev->stream[0], priv->buf_num);
	}
	cif_dev->is_thunderboot = false;
}

static int sditf_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	return -EINVAL;
}

static void sditf_reinit_mode(struct sditf_priv *priv, struct rkisp_vicap_mode *mode)
{
	if (mode->rdbk_mode == RKISP_VICAP_RDBK_AIQ) {
		priv->toisp_inf.link_mode = TOISP_NONE;
	} else {
		if (strstr(mode->name, RKISP0_DEVNAME))
			priv->toisp_inf.link_mode = TOISP0;
		else if (strstr(mode->name, RKISP1_DEVNAME))
			priv->toisp_inf.link_mode = TOISP1;
		else if (strstr(mode->name, RKISP_UNITE_DEVNAME))
			priv->toisp_inf.link_mode = TOISP_UNITE;
		else
			priv->toisp_inf.link_mode = TOISP0;
	}
	v4l2_info(&priv->cif_dev->v4l2_dev,
		  "%s, mode->rdbk_mode %d, mode->name %s, link_mode %d\n",
		  __func__, mode->rdbk_mode, mode->name, priv->toisp_inf.link_mode);
}

static long sditf_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkisp_vicap_mode *mode;
	struct v4l2_subdev_format fmt;
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;
	int *pbuf_num = NULL;
	int ret = 0;

	switch (cmd) {
	case RKISP_VICAP_CMD_MODE:
		mode = (struct rkisp_vicap_mode *)arg;
		memcpy(&priv->mode, mode, sizeof(*mode));
		sditf_reinit_mode(priv, &priv->mode);
		mode->input.merge_num = cif_dev->sditf_cnt;
		mode->input.index = priv->combine_index;
		return 0;
	case RKISP_VICAP_CMD_INIT_BUF:
		pbuf_num = (int *)arg;
		priv->buf_num = *pbuf_num;
		sditf_get_set_fmt(&priv->sd, NULL, &fmt);
		ret = sditf_init_buf(priv);
		return ret;
	case RKMODULE_GET_HDR_CFG:
		if (!cif_dev->terminal_sensor.sd)
			rkcif_update_sensor_info(&cif_dev->stream[0]);

		if (cif_dev->terminal_sensor.sd) {
			sensor_sd = cif_dev->terminal_sensor.sd;
			return v4l2_subdev_call(sensor_sd, core, ioctl, cmd, arg);
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long sditf_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev *sensor_sd;
	struct rkisp_vicap_mode *mode;
	struct rkmodule_hdr_cfg	*hdr_cfg;
	int buf_num;
	int ret = 0;

	switch (cmd) {
	case RKISP_VICAP_CMD_MODE:
		mode = kzalloc(sizeof(*mode), GFP_KERNEL);
		if (!mode) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(mode, up, sizeof(*mode))) {
			kfree(mode);
			return -EFAULT;
		}
		ret = sditf_ioctl(sd, cmd, mode);
		kfree(mode);
		return ret;
	case RKISP_VICAP_CMD_INIT_BUF:
		if (copy_from_user(&buf_num, up, sizeof(int)))
			return -EFAULT;
		ret = sditf_ioctl(sd, cmd, &buf_num);
		return ret;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = kzalloc(sizeof(*hdr_cfg), GFP_KERNEL);
		if (!hdr_cfg) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(hdr_cfg, up, sizeof(*hdr_cfg))) {
			kfree(hdr_cfg);
			return -EFAULT;
		}
		ret = sditf_ioctl(sd, cmd, hdr_cfg);
		return ret;
	default:
		break;
	}

	if (!cif_dev->terminal_sensor.sd)
		rkcif_update_sensor_info(&cif_dev->stream[0]);

	if (cif_dev->terminal_sensor.sd) {
		sensor_sd = cif_dev->terminal_sensor.sd;
		return v4l2_subdev_call(sensor_sd, core, compat_ioctl32, cmd, arg);
	}

	return -EINVAL;
}
#endif

static int sditf_channel_enable(struct sditf_priv *priv, int user)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	unsigned int ch0 = 0, ch1 = 0, ch2 = 0;
	unsigned int ctrl_val = 0;
	unsigned int int_en = 0;
	unsigned int offset_x = 0;
	unsigned int offset_y = 0;
	unsigned int width = priv->cap_info.width;
	unsigned int height = priv->cap_info.height;

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		if (cif_dev->inf_id == RKCIF_MIPI_LVDS)
			ch0 = cif_dev->csi_host_idx * 4;
		else
			ch0 = 24;//dvp
		ctrl_val = (ch0 << 3) | 0x1;
		if (user == 0)
			int_en = CIF_TOISP0_FS(0);
		else
			int_en = CIF_TOISP1_FS(0);
		priv->toisp_inf.ch_info[0].is_valid = true;
		priv->toisp_inf.ch_info[0].id = ch0;
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		ch0 = cif_dev->csi_host_idx * 4 + 1;
		ch1 = cif_dev->csi_host_idx * 4;
		ctrl_val = (ch0 << 3) | 0x1;
		ctrl_val |= (ch1 << 11) | 0x100;
		if (user == 0)
			int_en = CIF_TOISP0_FS(0) | CIF_TOISP0_FS(1);
		else
			int_en = CIF_TOISP1_FS(0) | CIF_TOISP1_FS(1);
		priv->toisp_inf.ch_info[0].is_valid = true;
		priv->toisp_inf.ch_info[0].id = ch0;
		priv->toisp_inf.ch_info[1].is_valid = true;
		priv->toisp_inf.ch_info[1].id = ch1;
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		ch0 = cif_dev->csi_host_idx * 4 + 2;
		ch1 = cif_dev->csi_host_idx * 4 + 1;
		ch2 = cif_dev->csi_host_idx * 4;
		ctrl_val = (ch0 << 3) | 0x1;
		ctrl_val |= (ch1 << 11) | 0x100;
		ctrl_val |= (ch2 << 19) | 0x10000;
		if (user == 0)
			int_en = CIF_TOISP0_FS(0) | CIF_TOISP0_FS(1) | CIF_TOISP0_FS(2);
		else
			int_en = CIF_TOISP1_FS(0) | CIF_TOISP1_FS(1) | CIF_TOISP1_FS(2);
		priv->toisp_inf.ch_info[0].is_valid = true;
		priv->toisp_inf.ch_info[0].id = ch0;
		priv->toisp_inf.ch_info[1].is_valid = true;
		priv->toisp_inf.ch_info[1].id = ch1;
		priv->toisp_inf.ch_info[2].is_valid = true;
		priv->toisp_inf.ch_info[2].id = ch2;
	}
	if (user == 0) {
		if (priv->toisp_inf.link_mode == TOISP_UNITE)
			width = priv->cap_info.width / 2 + RKMOUDLE_UNITE_EXTEND_PIXEL;
		rkcif_write_register(cif_dev, CIF_REG_TOISP0_CTRL, ctrl_val);
		if (width && height) {
			rkcif_write_register(cif_dev, CIF_REG_TOISP0_CROP,
				offset_x | (offset_y << 16));
			rkcif_write_register(cif_dev, CIF_REG_TOISP0_SIZE,
				width | (height << 16));
		} else {
			return -EINVAL;
		}
	} else {
		if (priv->toisp_inf.link_mode == TOISP_UNITE) {
			offset_x = priv->cap_info.width / 2 - RKMOUDLE_UNITE_EXTEND_PIXEL;
			width = priv->cap_info.width / 2 + RKMOUDLE_UNITE_EXTEND_PIXEL;
		}
		rkcif_write_register(cif_dev, CIF_REG_TOISP1_CTRL, ctrl_val);
		if (width && height) {
			rkcif_write_register(cif_dev, CIF_REG_TOISP1_CROP,
				offset_x | (offset_y << 16));
			rkcif_write_register(cif_dev, CIF_REG_TOISP1_SIZE,
				width | (height << 16));
		} else {
			return -EINVAL;
		}
	}
#if IS_ENABLED(CONFIG_CPU_RV1106)
	rv1106_sdmmc_get_lock();
#endif
	rkcif_write_register_or(cif_dev, CIF_REG_GLB_INTEN, int_en);
#if IS_ENABLED(CONFIG_CPU_RV1106)
	rv1106_sdmmc_put_lock();
#endif
	return 0;
}

static void sditf_channel_disable(struct sditf_priv *priv, int user)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	unsigned int ctrl_val = 0;

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		if (user == 0)
			ctrl_val = CIF_TOISP0_FE(0);
		else
			ctrl_val = CIF_TOISP1_FE(0);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		if (user == 0)
			ctrl_val = CIF_TOISP0_FE(0) | CIF_TOISP0_FE(1);
		else
			ctrl_val = CIF_TOISP1_FE(0) | CIF_TOISP1_FE(1);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		if (user == 0)
			ctrl_val = CIF_TOISP0_FE(0) | CIF_TOISP0_FE(1) | CIF_TOISP0_FE(2);
		else
			ctrl_val = CIF_TOISP1_FE(0) | CIF_TOISP1_FE(1) | CIF_TOISP1_FE(2);
	}
#if IS_ENABLED(CONFIG_CPU_RV1106)
	rv1106_sdmmc_get_lock();
#endif
	rkcif_write_register_or(cif_dev, CIF_REG_GLB_INTEN, ctrl_val);
#if IS_ENABLED(CONFIG_CPU_RV1106)
	rv1106_sdmmc_put_lock();
#endif
	priv->toisp_inf.ch_info[0].is_valid = false;
	priv->toisp_inf.ch_info[1].is_valid = false;
	priv->toisp_inf.ch_info[2].is_valid = false;
}

void sditf_change_to_online(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;

	priv->mode.rdbk_mode = RKISP_VICAP_ONLINE;
	if (priv->toisp_inf.link_mode == TOISP0) {
		sditf_channel_enable(priv, 0);
	} else if (priv->toisp_inf.link_mode == TOISP1) {
		sditf_channel_enable(priv, 1);
	} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
		sditf_channel_enable(priv, 0);
		sditf_channel_enable(priv, 1);
	}
	if (priv->hdr_cfg.hdr_mode == NO_HDR) {
		rkcif_free_rx_buf(&cif_dev->stream[0], priv->buf_num);
		cif_dev->stream[0].is_line_wake_up = false;
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		rkcif_free_rx_buf(&cif_dev->stream[1], priv->buf_num);
		cif_dev->stream[0].is_line_wake_up = false;
		cif_dev->stream[1].is_line_wake_up = false;
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		rkcif_free_rx_buf(&cif_dev->stream[2], priv->buf_num);
		cif_dev->stream[0].is_line_wake_up = false;
		cif_dev->stream[1].is_line_wake_up = false;
		cif_dev->stream[2].is_line_wake_up = false;
	}
	cif_dev->wait_line_cache = 0;
	cif_dev->wait_line = 0;
	cif_dev->wait_line_bak = 0;
}

static void sditf_check_capture_mode(struct rkcif_device *cif_dev)
{
	struct rkcif_device *dev = NULL;
	int i = 0;
	int toisp_cnt = 0;

	for (i = 0; i < cif_dev->hw_dev->dev_num; i++) {
		dev = cif_dev->hw_dev->cif_dev[i];
		if (dev && dev->sditf_cnt)
			toisp_cnt++;
	}
	if (cif_dev->is_thunderboot && toisp_cnt == 1)
		cif_dev->is_rdbk_to_online = true;
	else
		cif_dev->is_rdbk_to_online = false;
}

static int sditf_start_stream(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_subdev_format fmt;
	unsigned int mode = RKCIF_STREAM_MODE_TOISP;

	sditf_check_capture_mode(cif_dev);
	sditf_get_set_fmt(&priv->sd, NULL, &fmt);
	if (priv->mode.rdbk_mode == RKISP_VICAP_ONLINE) {
		if (priv->toisp_inf.link_mode == TOISP0) {
			sditf_channel_enable(priv, 0);
		} else if (priv->toisp_inf.link_mode == TOISP1) {
			sditf_channel_enable(priv, 1);
		} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
			sditf_channel_enable(priv, 0);
			sditf_channel_enable(priv, 1);
		}
		mode = RKCIF_STREAM_MODE_TOISP;
	} else if (priv->mode.rdbk_mode == RKISP_VICAP_RDBK_AUTO) {
		mode = RKCIF_STREAM_MODE_TOISP_RDBK;
	}

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		rkcif_do_start_stream(&cif_dev->stream[0], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		rkcif_do_start_stream(&cif_dev->stream[0], mode);
		rkcif_do_start_stream(&cif_dev->stream[1], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		rkcif_do_start_stream(&cif_dev->stream[0], mode);
		rkcif_do_start_stream(&cif_dev->stream[1], mode);
		rkcif_do_start_stream(&cif_dev->stream[2], mode);
	}
	INIT_LIST_HEAD(&priv->buf_free_list);
	return 0;
}

static int sditf_stop_stream(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	unsigned int mode = RKCIF_STREAM_MODE_TOISP;

	if (priv->toisp_inf.link_mode == TOISP0) {
		sditf_channel_disable(priv, 0);
	} else if (priv->toisp_inf.link_mode == TOISP1) {
		sditf_channel_disable(priv, 1);
	} else if (priv->toisp_inf.link_mode == TOISP_UNITE) {
		sditf_channel_disable(priv, 0);
		sditf_channel_disable(priv, 1);
	}

	if (priv->mode.rdbk_mode == RKISP_VICAP_ONLINE)
		mode = RKCIF_STREAM_MODE_TOISP;
	else if (priv->mode.rdbk_mode == RKISP_VICAP_RDBK_AUTO)
		mode = RKCIF_STREAM_MODE_TOISP_RDBK;

	if (priv->hdr_cfg.hdr_mode == NO_HDR ||
	    priv->hdr_cfg.hdr_mode == HDR_COMPR) {
		rkcif_do_stop_stream(&cif_dev->stream[0], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X2) {
		rkcif_do_stop_stream(&cif_dev->stream[0], mode);
		rkcif_do_stop_stream(&cif_dev->stream[1], mode);
	} else if (priv->hdr_cfg.hdr_mode == HDR_X3) {
		rkcif_do_stop_stream(&cif_dev->stream[0], mode);
		rkcif_do_stop_stream(&cif_dev->stream[1], mode);
		rkcif_do_stop_stream(&cif_dev->stream[2], mode);
	}
	return 0;
}

static int sditf_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	int ret = 0;

	if (!on && atomic_dec_return(&priv->stream_cnt))
		return 0;

	if (on && atomic_inc_return(&priv->stream_cnt) > 1)
		return 0;

	if (cif_dev->chip_id >= CHIP_RK3588_CIF) {
		if (priv->mode.rdbk_mode == RKISP_VICAP_RDBK_AIQ)
			return 0;
		v4l2_dbg(3, rkcif_debug, &cif_dev->v4l2_dev,
			"%s, toisp mode %d, hdr %d, stream on %d\n",
			__func__, priv->toisp_inf.link_mode, priv->hdr_cfg.hdr_mode, on);
		if (on) {
			ret = sditf_start_stream(priv);
		} else {
			ret = sditf_stop_stream(priv);
			sditf_free_buf(priv);
		}

	}
	return ret;
}

static int sditf_s_power(struct v4l2_subdev *sd, int on)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct rkcif_vdev_node *node = &cif_dev->stream[0].vnode;
	int ret = 0;

	if (!on && atomic_dec_return(&priv->power_cnt))
		return 0;

	if (on && atomic_inc_return(&priv->power_cnt) > 1)
		return 0;

	if (cif_dev->chip_id >= CHIP_RK3588_CIF) {
		v4l2_dbg(3, rkcif_debug, &cif_dev->v4l2_dev,
			"%s, toisp mode %d, hdr %d, set power %d\n",
			__func__, priv->toisp_inf.link_mode, priv->hdr_cfg.hdr_mode, on);
		if (on) {
			ret = pm_runtime_resume_and_get(cif_dev->dev);
			ret |= v4l2_pipeline_pm_get(&node->vdev.entity);
		} else {
			v4l2_pipeline_pm_put(&node->vdev.entity);
			pm_runtime_put_sync(cif_dev->dev);
		}
	}
	return ret;
}

static int sditf_s_rx_buffer(struct v4l2_subdev *sd,
			     void *buf, unsigned int *size)
{
	struct sditf_priv *priv = to_sditf_priv(sd);
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct rkcif_sensor_info *sensor = &cif_dev->terminal_sensor;
	struct rkcif_stream *stream = NULL;
	struct rkisp_rx_buf *dbufs;
	struct rkcif_rx_buffer *rx_buf = NULL;
	unsigned long flags, buffree_flags;
	u32 diff_time = 1000000;
	u32 early_time = 0;
	bool is_free = false;

	if (!buf) {
		v4l2_err(&cif_dev->v4l2_dev, "buf is NULL\n");
		return -EINVAL;
	}

	dbufs = buf;
	if (cif_dev->hdr.hdr_mode == NO_HDR) {
		if (dbufs->type == BUF_SHORT)
			stream = &cif_dev->stream[0];
		else
			return -EINVAL;
	} else if (cif_dev->hdr.hdr_mode == HDR_X2) {
		if (dbufs->type == BUF_SHORT)
			stream = &cif_dev->stream[1];
		else if (dbufs->type == BUF_MIDDLE)
			stream = &cif_dev->stream[0];
		else
			return -EINVAL;
	} else if (cif_dev->hdr.hdr_mode == HDR_X3) {
		if (dbufs->type == BUF_SHORT)
			stream = &cif_dev->stream[2];
		else if (dbufs->type == BUF_MIDDLE)
			stream = &cif_dev->stream[1];
		else if (dbufs->type == BUF_LONG)
			stream = &cif_dev->stream[0];
		else
			return -EINVAL;
	}

	if (!stream)
		return -EINVAL;

	rx_buf = to_cif_rx_buf(dbufs);

	spin_lock_irqsave(&stream->vbq_lock, flags);
	stream->buf_num_toisp++;
	stream->last_rx_buf_idx = dbufs->sequence + 1;

	if (!list_empty(&stream->rx_buf_head) &&
	    cif_dev->is_thunderboot &&
	    (dbufs->type == BUF_SHORT ||
	     (dbufs->type != BUF_SHORT && (!dbufs->is_switch)))) {
		spin_lock_irqsave(&cif_dev->buffree_lock, buffree_flags);
		list_add_tail(&rx_buf->list_free, &priv->buf_free_list);
		spin_unlock_irqrestore(&cif_dev->buffree_lock, buffree_flags);
		schedule_work(&priv->buffree_work.work);
		is_free = true;
	}

	if (!is_free && (!dbufs->is_switch)) {
		list_add_tail(&rx_buf->list, &stream->rx_buf_head);
		rkcif_assign_check_buffer_update_toisp(stream);
		if (cif_dev->rdbk_debug) {
			u32 offset = 0;

			offset = rx_buf->dummy.size - stream->pixm.plane_fmt[0].bytesperline * 3;
			memset(rx_buf->dummy.vaddr + offset,
			       0x00, stream->pixm.plane_fmt[0].bytesperline * 3);
			if (cif_dev->is_thunderboot)
				dma_sync_single_for_device(cif_dev->dev,
							   rx_buf->dummy.dma_addr + rx_buf->dummy.size -
							   stream->pixm.plane_fmt[0].bytesperline * 3,
							   stream->pixm.plane_fmt[0].bytesperline * 3,
							   DMA_FROM_DEVICE);
			else
				cif_dev->hw_dev->mem_ops->prepare(rx_buf->dummy.mem_priv);
		}
	}

	if (dbufs->is_switch && dbufs->type == BUF_SHORT) {
		if (stream->is_in_vblank)
			sditf_change_to_online(priv);
		else
			stream->is_change_toisp = true;
		v4l2_dbg(3, rkcif_debug, &cif_dev->v4l2_dev,
			 "switch to online mode\n");
	}
	spin_unlock_irqrestore(&stream->vbq_lock, flags);

	if (dbufs->runtime_us && cif_dev->early_line == 0) {
		if (!cif_dev->sensor_linetime)
			cif_dev->sensor_linetime = rkcif_get_linetime(stream);
		cif_dev->isp_runtime_max = dbufs->runtime_us;
		if (cif_dev->is_thunderboot)
			diff_time = 200000;
		else
			diff_time = 1000000;
		if (dbufs->runtime_us * 1000 + cif_dev->sensor_linetime > diff_time)
			early_time = dbufs->runtime_us * 1000 - diff_time;
		else
			early_time = diff_time;
		cif_dev->early_line = div_u64(early_time, cif_dev->sensor_linetime);
		cif_dev->wait_line_cache = sensor->raw_rect.height - cif_dev->early_line;
		if (cif_dev->rdbk_debug &&
		    dbufs->sequence < 15)
			v4l2_info(&cif_dev->v4l2_dev,
				  "%s, isp runtime %d, line time %d, early_line %d, line_intr_cnt %d, seq %d, type %d, dma_addr %x\n",
				  __func__, dbufs->runtime_us, cif_dev->sensor_linetime,
				  cif_dev->early_line, cif_dev->wait_line_cache,
				  dbufs->sequence, dbufs->type, (u32)rx_buf->dummy.dma_addr);
	} else {
		if (dbufs->runtime_us < cif_dev->isp_runtime_max) {
			cif_dev->isp_runtime_max = dbufs->runtime_us;
			if (cif_dev->is_thunderboot)
				diff_time = 200000;
			else
				diff_time = 1000000;
			if (dbufs->runtime_us * 1000 + cif_dev->sensor_linetime > diff_time)
				early_time = dbufs->runtime_us * 1000 - diff_time;
			else
				early_time = diff_time;
			cif_dev->early_line = div_u64(early_time, cif_dev->sensor_linetime);
			cif_dev->wait_line_cache = sensor->raw_rect.height - cif_dev->early_line;
		}
		if (cif_dev->rdbk_debug &&
		    dbufs->sequence < 15)
			v4l2_info(&cif_dev->v4l2_dev,
				  "isp runtime %d, seq %d, type %d, early_line %d, dma addr %x\n",
				  dbufs->runtime_us, dbufs->sequence, dbufs->type,
				  cif_dev->early_line, (u32)rx_buf->dummy.dma_addr);
	}
	return 0;
}

static const struct v4l2_subdev_pad_ops sditf_subdev_pad_ops = {
	.set_fmt = sditf_get_set_fmt,
	.get_fmt = sditf_get_set_fmt,
	.get_selection = sditf_get_selection,
	.get_mbus_config = sditf_g_mbus_config,
};

static const struct v4l2_subdev_video_ops sditf_video_ops = {
	.g_frame_interval = sditf_g_frame_interval,
	.s_stream = sditf_s_stream,
	.s_rx_buffer = sditf_s_rx_buffer,
};

static const struct v4l2_subdev_core_ops sditf_core_ops = {
	.ioctl = sditf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sditf_compat_ioctl32,
#endif
	.s_power = sditf_s_power,
};

static const struct v4l2_subdev_ops sditf_subdev_ops = {
	.core = &sditf_core_ops,
	.video = &sditf_video_ops,
	.pad = &sditf_subdev_pad_ops,
};

static int rkcif_sditf_attach_cifdev(struct sditf_priv *sditf)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rkcif_device *cif_dev;

	np = of_parse_phandle(sditf->dev->of_node, "rockchip,cif", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(sditf->dev, "failed to get cif dev node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(sditf->dev, "failed to get cif dev from node\n");
		return -ENODEV;
	}

	cif_dev = platform_get_drvdata(pdev);
	if (!cif_dev) {
		dev_err(sditf->dev, "failed attach cif dev\n");
		return -EINVAL;
	}

	cif_dev->sditf[cif_dev->sditf_cnt] = sditf;
	sditf->cif_dev = cif_dev;
	cif_dev->sditf_cnt++;

	return 0;
}

struct sensor_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

static int sditf_fwnode_parse(struct device *dev,
					  struct v4l2_fwnode_endpoint *vep,
					  struct v4l2_async_subdev *asd)
{
	struct sensor_async_subdev *s_asd =
			container_of(asd, struct sensor_async_subdev, asd);
	struct v4l2_mbus_config *config = &s_asd->mbus;

	if (vep->base.port != 0) {
		dev_err(dev, "sditf has only port 0\n");
		return -EINVAL;
	}

	if (vep->bus_type == V4L2_MBUS_CSI2_DPHY ||
	    vep->bus_type == V4L2_MBUS_CSI2_CPHY) {
		config->type = vep->bus_type;
		config->flags = vep->bus.mipi_csi2.flags;
		s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
	} else if (vep->bus_type == V4L2_MBUS_CCP2) {
		config->type = vep->bus_type;
		s_asd->lanes = vep->bus.mipi_csi1.data_lane;
	} else {
		dev_err(dev, "type is not supported\n");
		return -EINVAL;
	}

	switch (s_asd->lanes) {
	case 1:
		config->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		config->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		config->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		config->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rkcif_sditf_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sditf_priv *priv = container_of(ctrl->handler,
					       struct sditf_priv,
					       ctrl_handler);
	struct v4l2_ctrl *sensor_ctrl = NULL;

	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
		if (priv->cif_dev->terminal_sensor.sd) {
			sensor_ctrl = v4l2_ctrl_find(priv->cif_dev->terminal_sensor.sd->ctrl_handler, V4L2_CID_PIXEL_RATE);
			if (sensor_ctrl) {
				ctrl->val = v4l2_ctrl_g_ctrl_int64(sensor_ctrl);
				__v4l2_ctrl_s_ctrl_int64(priv->pixel_rate, ctrl->val);
				v4l2_dbg(3, rkcif_debug, &priv->cif_dev->v4l2_dev,
					"%s, %s pixel rate %d\n",
					__func__, priv->cif_dev->terminal_sensor.sd->name, ctrl->val);
				return 0;
			} else {
				return -EINVAL;
			}
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops rkcif_sditf_ctrl_ops = {
	.g_volatile_ctrl = rkcif_sditf_get_ctrl,
};

static int sditf_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct sditf_priv *sditf = container_of(notifier,
					struct sditf_priv, notifier);
	struct media_entity *source_entity, *sink_entity;
	int ret = 0;

	sditf->sensor_sd = subdev;

	if (sditf->num_sensors == 1) {
		v4l2_err(subdev,
			 "%s: the num of subdev is beyond %d\n",
			 __func__, sditf->num_sensors);
		return -EBUSY;
	}

	if (sditf->sd.entity.pads[0].flags & MEDIA_PAD_FL_SINK) {
		source_entity = &subdev->entity;
		sink_entity = &sditf->sd.entity;

		ret = media_create_pad_link(source_entity,
					    0,
					    sink_entity,
					    0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			v4l2_err(&sditf->sd, "failed to create link for %s\n",
				 sditf->sensor_sd->name);
	}
	sditf->sensor_sd = subdev;
	++sditf->num_sensors;

	v4l2_err(subdev, "Async registered subdev\n");

	return 0;
}

static void sditf_notifier_unbind(struct v4l2_async_notifier *notifier,
				       struct v4l2_subdev *sd,
				       struct v4l2_async_subdev *asd)
{
	struct sditf_priv *sditf = container_of(notifier,
						struct sditf_priv,
						notifier);

	sditf->sensor_sd = NULL;
}

static const struct v4l2_async_notifier_operations sditf_notifier_ops = {
	.bound = sditf_notifier_bound,
	.unbind = sditf_notifier_unbind,
};

static int sditf_subdev_notifier(struct sditf_priv *sditf)
{
	struct v4l2_async_notifier *ntf = &sditf->notifier;
	int ret;

	v4l2_async_notifier_init(ntf);

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(
			sditf->dev, &sditf->notifier,
			sizeof(struct sensor_async_subdev), 0,
			sditf_fwnode_parse);
		if (ret < 0)
			return ret;

	sditf->sd.subdev_notifier = &sditf->notifier;
	sditf->notifier.ops = &sditf_notifier_ops;

	ret = v4l2_async_subdev_notifier_register(&sditf->sd, &sditf->notifier);
	if (ret) {
		v4l2_err(&sditf->sd,
			 "failed to register async notifier : %d\n",
			 ret);
		v4l2_async_notifier_cleanup(&sditf->notifier);
		return ret;
	}

	return v4l2_async_register_subdev(&sditf->sd);
}

static int rkcif_subdev_media_init(struct sditf_priv *priv)
{
	struct rkcif_device *cif_dev = priv->cif_dev;
	struct v4l2_ctrl_handler *handler = &priv->ctrl_handler;
	unsigned long flags = V4L2_CTRL_FLAG_VOLATILE;
	int ret;
	int pad_num = 0;

	if (priv->is_combine_mode) {
		priv->pads[0].flags = MEDIA_PAD_FL_SINK;
		priv->pads[1].flags = MEDIA_PAD_FL_SOURCE;
		pad_num = 2;
	} else {
		priv->pads[0].flags = MEDIA_PAD_FL_SOURCE;
		pad_num = 1;
	}
	priv->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_COMPOSER;
	ret = media_entity_pads_init(&priv->sd.entity, pad_num, priv->pads);
	if (ret < 0)
		return ret;

	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;
	priv->pixel_rate = v4l2_ctrl_new_std(handler, &rkcif_sditf_ctrl_ops,
					     V4L2_CID_PIXEL_RATE,
					     0, SDITF_PIXEL_RATE_MAX,
					     1, SDITF_PIXEL_RATE_MAX);
	if (priv->pixel_rate)
		priv->pixel_rate->flags |= flags;
	priv->sd.ctrl_handler = handler;
	if (handler->error) {
		v4l2_ctrl_handler_free(handler);
		return handler->error;
	}

	strncpy(priv->sd.name, dev_name(cif_dev->dev), sizeof(priv->sd.name));
	priv->cap_info.width = 0;
	priv->cap_info.height = 0;
	priv->mode.rdbk_mode = RKISP_VICAP_RDBK_AIQ;
	priv->toisp_inf.link_mode = TOISP_NONE;
	priv->toisp_inf.ch_info[0].is_valid = false;
	priv->toisp_inf.ch_info[1].is_valid = false;
	priv->toisp_inf.ch_info[2].is_valid = false;
	if (priv->is_combine_mode)
		sditf_subdev_notifier(priv);
	atomic_set(&priv->power_cnt, 0);
	atomic_set(&priv->stream_cnt, 0);
	INIT_WORK(&priv->buffree_work.work, sditf_buffree_work);
	INIT_LIST_HEAD(&priv->buf_free_list);
	return 0;
}

static int rkcif_subdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd;
	struct sditf_priv *priv;
	struct device_node *node = dev->of_node;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	sd = &priv->sd;
	v4l2_subdev_init(sd, &sditf_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "rockchip-cif-sditf");
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	ret = rkcif_sditf_attach_cifdev(priv);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node,
				   "rockchip,combine-index",
				   &priv->combine_index);
	if (ret) {
		priv->is_combine_mode = false;
		priv->combine_index = 0;
	} else {
		priv->is_combine_mode = true;
	}
	ret = rkcif_subdev_media_init(priv);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&pdev->dev);
	return 0;
}

static int rkcif_subdev_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);

	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int sditf_runtime_suspend(struct device *dev)
{
	return 0;
}

static int sditf_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops rkcif_subdev_pm_ops = {
	SET_RUNTIME_PM_OPS(sditf_runtime_suspend,
			   sditf_runtime_resume, NULL)
};

static const struct of_device_id rkcif_subdev_match_id[] = {
	{
		.compatible = "rockchip,rkcif-sditf",
	},
	{}
};
MODULE_DEVICE_TABLE(of, rkcif_subdev_match_id);

struct platform_driver rkcif_subdev_driver = {
	.probe = rkcif_subdev_probe,
	.remove = rkcif_subdev_remove,
	.driver = {
		.name = "rkcif_sditf",
		.pm = &rkcif_subdev_pm_ops,
		.of_match_table = rkcif_subdev_match_id,
	},
};
EXPORT_SYMBOL(rkcif_subdev_driver);

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip CIF platform driver");
MODULE_LICENSE("GPL v2");
