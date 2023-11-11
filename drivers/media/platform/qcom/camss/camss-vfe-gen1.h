/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss-vfe.h
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module
 *
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#ifndef QC_MSM_CAMSS_VFE_GEN1_H
#define QC_MSM_CAMSS_VFE_GEN1_H

#include "camss-vfe.h"

enum vfe_line_id;
struct vfe_device;
struct vfe_line;
struct vfe_output;

struct vfe_hw_ops_gen1 {
	void (*bus_connect_wm_to_rdi)(struct vfe_device *vfe, u8 wm, enum vfe_line_id id);
	void (*bus_disconnect_wm_from_rdi)(struct vfe_device *vfe, u8 wm, enum vfe_line_id id);
	void (*bus_enable_wr_if)(struct vfe_device *vfe, u8 enable);
	void (*bus_reload_wm)(struct vfe_device *vfe, u8 wm);
	int (*camif_wait_for_stop)(struct vfe_device *vfe, struct device *dev);
	void (*enable_irq_common)(struct vfe_device *vfe);
	void (*enable_irq_wm_line)(struct vfe_device *vfe, u8 wm, enum vfe_line_id line_id,
				   u8 enable);
	void (*enable_irq_pix_line)(struct vfe_device *vfe, u8 comp, enum vfe_line_id line_id,
				    u8 enable);
	u16 (*get_ub_size)(u8 vfe_id);
	void (*halt_clear)(struct vfe_device *vfe);
	void (*halt_request)(struct vfe_device *vfe);
	void (*set_camif_cfg)(struct vfe_device *vfe, struct vfe_line *line);
	void (*set_camif_cmd)(struct vfe_device *vfe, u8 enable);
	void (*set_cgc_override)(struct vfe_device *vfe, u8 wm, u8 enable);
	void (*set_clamp_cfg)(struct vfe_device *vfe);
	void (*set_crop_cfg)(struct vfe_device *vfe, struct vfe_line *line);
	void (*set_demux_cfg)(struct vfe_device *vfe, struct vfe_line *line);
	void (*set_ds)(struct vfe_device *vfe);
	void (*set_module_cfg)(struct vfe_device *vfe, u8 enable);
	void (*set_scale_cfg)(struct vfe_device *vfe, struct vfe_line *line);
	void (*set_rdi_cid)(struct vfe_device *vfe, enum vfe_line_id id, u8 cid);
	void (*set_realign_cfg)(struct vfe_device *vfe, struct vfe_line *line, u8 enable);
	void (*set_qos)(struct vfe_device *vfe);
	void (*set_xbar_cfg)(struct vfe_device *vfe, struct vfe_output *output, u8 enable);
	void (*wm_frame_based)(struct vfe_device *vfe, u8 wm, u8 enable);
	void (*wm_line_based)(struct vfe_device *vfe, u32 wm, struct v4l2_pix_format_mplane *pix,
			      u8 plane, u32 enable);
	void (*wm_set_ub_cfg)(struct vfe_device *vfe, u8 wm, u16 offset, u16 depth);
	void (*wm_set_subsample)(struct vfe_device *vfe, u8 wm);
	void (*wm_set_framedrop_period)(struct vfe_device *vfe, u8 wm, u8 per);
	void (*wm_set_framedrop_pattern)(struct vfe_device *vfe, u8 wm, u32 pattern);
	void (*wm_set_ping_addr)(struct vfe_device *vfe, u8 wm, u32 addr);
	void (*wm_set_pong_addr)(struct vfe_device *vfe, u8 wm, u32 addr);
	int (*wm_get_ping_pong_status)(struct vfe_device *vfe, u8 wm);
	void (*wm_enable)(struct vfe_device *vfe, u8 wm, u8 enable);
};

/*
 * vfe_calc_interp_reso - Calculate interpolation mode
 * @input: Input resolution
 * @output: Output resolution
 *
 * Return interpolation mode
 */
static inline u8 vfe_calc_interp_reso(u16 input, u16 output)
{
	if (input / output >= 16)
		return 0;

	if (input / output >= 8)
		return 1;

	if (input / output >= 4)
		return 2;

	return 3;
}

/*
 * vfe_gen1_disable - Disable streaming on VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
int vfe_gen1_disable(struct vfe_line *line);

/*
 * vfe_gen1_enable - Enable VFE module
 * @line: VFE line
 *
 * Return 0 on success
 */
int vfe_gen1_enable(struct vfe_line *line);

/*
 * vfe_gen1_enable - Halt VFE module
 * @vfe: VFE device
 *
 * Return 0 on success
 */
int vfe_gen1_halt(struct vfe_device *vfe);

/*
 * vfe_word_per_line - Calculate number of words per frame width
 * @format: V4L2 format
 * @width: Frame width
 *
 * Return number of words per frame width
 */
int vfe_word_per_line(u32 format, u32 width);

extern const struct vfe_isr_ops vfe_isr_ops_gen1;
extern const struct camss_video_ops vfe_video_ops_gen1;

#endif /* QC_MSM_CAMSS_VFE_GEN1_H */
