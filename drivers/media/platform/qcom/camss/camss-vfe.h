/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss-vfe.h
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module
 *
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#ifndef QC_MSM_CAMSS_VFE_H
#define QC_MSM_CAMSS_VFE_H

#include <linux/clk.h>
#include <linux/spinlock_types.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "camss-video.h"
#include "camss-vfe-gen1.h"

#define MSM_VFE_PAD_SINK 0
#define MSM_VFE_PAD_SRC 1
#define MSM_VFE_PADS_NUM 2

#define MSM_VFE_IMAGE_MASTERS_NUM 7
#define MSM_VFE_COMPOSITE_IRQ_NUM 4

/* VFE halt timeout */
#define VFE_HALT_TIMEOUT_MS 100
/* Frame drop value. VAL + UPDATES - 1 should not exceed 31 */
#define VFE_FRAME_DROP_VAL 30

#define vfe_line_array(ptr_line)	\
	((const struct vfe_line (*)[]) &(ptr_line)[-(ptr_line)->id])

#define to_vfe(ptr_line)	\
	container_of(vfe_line_array(ptr_line), struct vfe_device, line)

enum vfe_output_state {
	VFE_OUTPUT_OFF,
	VFE_OUTPUT_RESERVED,
	VFE_OUTPUT_SINGLE,
	VFE_OUTPUT_CONTINUOUS,
	VFE_OUTPUT_IDLE,
	VFE_OUTPUT_STOPPING,
	VFE_OUTPUT_ON,
};

enum vfe_line_id {
	VFE_LINE_NONE = -1,
	VFE_LINE_RDI0 = 0,
	VFE_LINE_RDI1 = 1,
	VFE_LINE_RDI2 = 2,
	VFE_LINE_PIX = 3,
	VFE_LINE_NUM_MAX = 4
};

struct vfe_output {
	u8 wm_num;
	u8 wm_idx[3];

	struct camss_buffer *buf[2];
	struct camss_buffer *last_buffer;
	struct list_head pending_bufs;

	unsigned int drop_update_idx;

	union {
		struct {
			int active_buf;
			int wait_sof;
		} gen1;
		struct {
			int active_num;
		} gen2;
	};
	enum vfe_output_state state;
	unsigned int sequence;

	int wait_reg_update;
	struct completion sof;
	struct completion reg_update;
};

struct vfe_line {
	enum vfe_line_id id;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_VFE_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[MSM_VFE_PADS_NUM];
	struct v4l2_rect compose;
	struct v4l2_rect crop;
	struct camss_video video_out;
	struct vfe_output output;
	const struct vfe_format *formats;
	unsigned int nformats;
};

struct vfe_device;

struct vfe_hw_ops {
	void (*enable_irq_common)(struct vfe_device *vfe);
	void (*global_reset)(struct vfe_device *vfe);
	u32 (*hw_version)(struct vfe_device *vfe);
	irqreturn_t (*isr)(int irq, void *dev);
	void (*isr_read)(struct vfe_device *vfe, u32 *value0, u32 *value1);
	void (*pm_domain_off)(struct vfe_device *vfe);
	int (*pm_domain_on)(struct vfe_device *vfe);
	void (*reg_update)(struct vfe_device *vfe, enum vfe_line_id line_id);
	void (*reg_update_clear)(struct vfe_device *vfe,
				 enum vfe_line_id line_id);
	void (*subdev_init)(struct device *dev, struct vfe_device *vfe);
	int (*vfe_disable)(struct vfe_line *line);
	int (*vfe_enable)(struct vfe_line *line);
	int (*vfe_halt)(struct vfe_device *vfe);
	void (*violation_read)(struct vfe_device *vfe);
	void (*vfe_wm_stop)(struct vfe_device *vfe, u8 wm);
};

struct vfe_isr_ops {
	void (*reset_ack)(struct vfe_device *vfe);
	void (*halt_ack)(struct vfe_device *vfe);
	void (*reg_update)(struct vfe_device *vfe, enum vfe_line_id line_id);
	void (*sof)(struct vfe_device *vfe, enum vfe_line_id line_id);
	void (*comp_done)(struct vfe_device *vfe, u8 comp);
	void (*wm_done)(struct vfe_device *vfe, u8 wm);
};

struct vfe_device {
	struct camss *camss;
	u8 id;
	void __iomem *base;
	u32 irq;
	char irq_name[30];
	struct camss_clock *clock;
	int nclocks;
	struct completion reset_complete;
	struct completion halt_complete;
	struct mutex power_lock;
	int power_count;
	struct mutex stream_lock;
	int stream_count;
	spinlock_t output_lock;
	enum vfe_line_id wm_output_map[MSM_VFE_IMAGE_MASTERS_NUM];
	struct vfe_line line[VFE_LINE_NUM_MAX];
	u8 line_num;
	u32 reg_update;
	u8 was_streaming;
	const struct vfe_hw_ops *ops;
	const struct vfe_hw_ops_gen1 *ops_gen1;
	struct vfe_isr_ops isr_ops;
	struct camss_video_ops video_ops;
	struct device *genpd;
	struct device_link *genpd_link;
};

struct camss_subdev_resources;

int msm_vfe_subdev_init(struct camss *camss, struct vfe_device *vfe,
			const struct camss_subdev_resources *res, u8 id);

void msm_vfe_genpd_cleanup(struct vfe_device *vfe);

int msm_vfe_register_entities(struct vfe_device *vfe,
			      struct v4l2_device *v4l2_dev);

void msm_vfe_unregister_entities(struct vfe_device *vfe);

/*
 * vfe_buf_add_pending - Add output buffer to list of pending
 * @output: VFE output
 * @buffer: Video buffer
 */
void vfe_buf_add_pending(struct vfe_output *output, struct camss_buffer *buffer);

struct camss_buffer *vfe_buf_get_pending(struct vfe_output *output);

int vfe_flush_buffers(struct camss_video *vid, enum vb2_buffer_state state);

/*
 * vfe_isr_comp_done - Process composite image done interrupt
 * @vfe: VFE Device
 * @comp: Composite image id
 */
void vfe_isr_comp_done(struct vfe_device *vfe, u8 comp);

void vfe_isr_reset_ack(struct vfe_device *vfe);
int vfe_put_output(struct vfe_line *line);
int vfe_release_wm(struct vfe_device *vfe, u8 wm);
int vfe_reserve_wm(struct vfe_device *vfe, enum vfe_line_id line_id);

/*
 * vfe_reset - Trigger reset on VFE module and wait to complete
 * @vfe: VFE device
 *
 * Return 0 on success or a negative error code otherwise
 */
int vfe_reset(struct vfe_device *vfe);

/*
 * vfe_disable - Disable streaming on VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
int vfe_disable(struct vfe_line *line);

/*
 * vfe_pm_domain_off - Disable power domains specific to this VFE.
 * @vfe: VFE Device
 */
void vfe_pm_domain_off(struct vfe_device *vfe);

/*
 * vfe_pm_domain_on - Enable power domains specific to this VFE.
 * @vfe: VFE Device
 */
int vfe_pm_domain_on(struct vfe_device *vfe);

extern const struct vfe_hw_ops vfe_ops_4_1;
extern const struct vfe_hw_ops vfe_ops_4_7;
extern const struct vfe_hw_ops vfe_ops_4_8;
extern const struct vfe_hw_ops vfe_ops_170;
extern const struct vfe_hw_ops vfe_ops_480;

int vfe_get(struct vfe_device *vfe);
void vfe_put(struct vfe_device *vfe);

/*
 * vfe_is_lite - Return if VFE is VFE lite.
 * @vfe: VFE Device
 *
 * Some VFE lites have a different register layout.
 *
 * Return whether VFE is VFE lite
 */
bool vfe_is_lite(struct vfe_device *vfe);

#endif /* QC_MSM_CAMSS_VFE_H */
