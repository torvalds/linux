/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_CORE_H
#define FIMC_IS_CORE_H

/*#define DEBUG			1*/
/*#define FIMC_IS_A5_DEBUG_ON	1*/
#define FRAME_RATE_ENABLE	1
/*#define ODC_ENABLE		1*/
/*#define TDNR_ENABLE		1*/
/*#define DZOOM_ENABLE		1*/
/*#define DIS_ENABLE		1*/

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/exynos_fimc_is.h>
#include <media/v4l2-ioctl.h>
#include <media/exynos_mc.h>
#include <media/videobuf2-core.h>
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif
#include "fimc-is-param.h"

#define FIMC_IS_MODULE_NAME			"exynos5-fimc-is"
#define FIMC_IS_SENSOR_ENTITY_NAME		"exynos5-fimc-is-sensor"
#define FIMC_IS_FRONT_ENTITY_NAME		"exynos5-fimc-is-front"
#define FIMC_IS_BACK_ENTITY_NAME		"exynos5-fimc-is-back"
#define FIMC_IS_VIDEO_BAYER_NAME		"exynos5-fimc-is-bayer"
#define FIMC_IS_VIDEO_SCALERC_NAME		"exynos5-fimc-is-scalerc"
#define FIMC_IS_VIDEO_3DNR_NAME			"exynos5-fimc-is-3dnr"
#define FIMC_IS_VIDEO_SCALERP_NAME		"exynos5-fimc-is-scalerp"

#define FIMC_IS_A5_DEBUG_LEVEL			3

#define MAX_I2H_ARG				(4)

#define FIMC_IS_FW				"fimc_is_fw.bin"
#define FIMC_IS_SETFILE				"setfile.bin"

#define FIMC_IS_SHUTDOWN_TIMEOUT		(10*HZ)
#define FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR		(3*HZ)

#define FIMC_IS_A5_MEM_SIZE			(0x00A00000)
#define FIMC_IS_REGION_SIZE			(0x5000)
#define FIMC_IS_SETFILE_SIZE			(0xc0d8)
#define DRC_SETFILE_SIZE			(0x140)
#define FD_SETFILE_SIZE				(0x88*2)
#define FIMC_IS_FW_BASE_MASK			((1 << 26) - 1)
#define FIMC_IS_TDNR_MEM_SIZE			(1920*1080*4)
#define FIMC_IS_DEBUG_REGION_ADDR		(0x00840000)
#define FIMC_IS_SHARED_REGION_ADDR		(0x008C0000)

#define FIMC_IS_MAX_BUF_NUM			(16)
#define FIMC_IS_MAX_BUf_PLANE_NUM		(3)

#define FIMC_IS_SENSOR_MAX_ENTITIES		(1)
#define FIMC_IS_SENSOR_PAD_SOURCE_FRONT	(0)
#define FIMC_IS_SENSOR_PADS_NUM		(1)

#define FIMC_IS_FRONT_MAX_ENTITIES		(1)
#define FIMC_IS_FRONT_PAD_SINK			(0)
#define FIMC_IS_FRONT_PAD_SOURCE_BACK		(1)
#define FIMC_IS_FRONT_PAD_SOURCE_BAYER		(2)
#define FIMC_IS_FRONT_PAD_SOURCE_SCALERC	(3)
#define FIMC_IS_FRONT_PADS_NUM			(4)

#define FIMC_IS_BACK_MAX_ENTITIES		(1)
#define FIMC_IS_BACK_PAD_SINK			(0)
#define FIMC_IS_BACK_PAD_SOURCE_3DNR		(1)
#define FIMC_IS_BACK_PAD_SOURCE_SCALERP	(2)
#define FIMC_IS_BACK_PADS_NUM			(3)

#define MAX_ISP_INTERNAL_BUF_WIDTH		(2560)  /* 4808 in HW */
#define MAX_ISP_INTERNAL_BUF_HEIGHT		(1920)  /* 3356 in HW */
#define SIZE_ISP_INTERNAL_BUF \
	(MAX_ISP_INTERNAL_BUF_WIDTH * MAX_ISP_INTERNAL_BUF_HEIGHT * 3)

#define MAX_ODC_INTERNAL_BUF_WIDTH		(2560)  /* 4808 in HW */
#define MAX_ODC_INTERNAL_BUF_HEIGHT		(1920)  /* 3356 in HW */
#define SIZE_ODC_INTERNAL_BUF \
	(MAX_ODC_INTERNAL_BUF_WIDTH * MAX_ODC_INTERNAL_BUF_HEIGHT * 3)

#define MAX_DIS_INTERNAL_BUF_WIDTH		(2400)
#define MAX_DIS_INTERNAL_BUF_HEIGHT		(1360)
#define SIZE_DIS_INTERNAL_BUF \
	(MAX_DIS_INTERNAL_BUF_WIDTH * MAX_DIS_INTERNAL_BUF_HEIGHT * 2)

#define MAX_3DNR_INTERNAL_BUF_WIDTH		(1920)
#define MAX_3DNR_INTERNAL_BUF_HEIGHT		(1088)
#define SIZE_3DNR_INTERNAL_BUF \
	(MAX_3DNR_INTERNAL_BUF_WIDTH * MAX_3DNR_INTERNAL_BUF_HEIGHT * 2)

#define NUM_ISP_INTERNAL_BUF			(3)
#define NUM_ODC_INTERNAL_BUF			(2)
#define NUM_DIS_INTERNAL_BUF			(3)
#define NUM_3DNR_INTERNAL_BUF			(2)

#define is_af_use(dev)				((dev->af.use_af) ? 1 : 0)

#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
#define FIMC_IS_FREQ_MIF		(800)
#define FIMC_IS_FREQ_INT		(267)
#endif

#define err(fmt, args...) \
	printk(KERN_ERR "%s:%d: " fmt "\n", __func__, __LINE__, ##args)

#ifdef DEBUG
#define dbg(fmt, args...) \
	printk(KERN_DEBUG "%s:%d: " fmt "\n", __func__, __LINE__, ##args)
#else
#define dbg(fmt, args...)
#endif

enum fimc_is_debug_device {
	FIMC_IS_DEBUG_MAIN = 0,
	FIMC_IS_DEBUG_EC,
	FIMC_IS_DEBUG_SENSOR,
	FIMC_IS_DEBUG_ISP,
	FIMC_IS_DEBUG_DRC,
	FIMC_IS_DEBUG_FD,
	FIMC_IS_DEBUG_SDK,
	FIMC_IS_DEBUG_SCALERC,
	FIMC_IS_DEBUG_ODC,
	FIMC_IS_DEBUG_DIS,
	FIMC_IS_DEBUG_TDNR,
	FIMC_IS_DEBUG_SCALERP
};

enum fimc_is_debug_target {
	FIMC_IS_DEBUG_UART = 0,
	FIMC_IS_DEBUG_MEMORY,
	FIMC_IS_DEBUG_DCC3
};

enum fimc_is_sensor_output_entity {
	FIMC_IS_SENSOR_OUTPUT_NONE = 0,
	FIMC_IS_SENSOR_OUTPUT_FRONT,
};

enum fimc_is_front_input_entity {
	FIMC_IS_FRONT_INPUT_NONE = 0,
	FIMC_IS_FRONT_INPUT_SENSOR,
};

enum fimc_is_front_output_entity {
	FIMC_IS_FRONT_OUTPUT_NONE = 0,
	FIMC_IS_FRONT_OUTPUT_BACK,
	FIMC_IS_FRONT_OUTPUT_BAYER,
	FIMC_IS_FRONT_OUTPUT_SCALERC,
};

enum fimc_is_back_input_entity {
	FIMC_IS_BACK_INPUT_NONE = 0,
	FIMC_IS_BACK_INPUT_FRONT,
};

enum fimc_is_back_output_entity {
	FIMC_IS_BACK_OUTPUT_NONE = 0,
	FIMC_IS_BACK_OUTPUT_3DNR,
	FIMC_IS_BACK_OUTPUT_SCALERP,
};

enum fimc_is_front_state {
	FIMC_IS_FRONT_ST_POWERED = 0,
	FIMC_IS_FRONT_ST_STREAMING,
	FIMC_IS_FRONT_ST_SUSPENDED,
};

enum fimc_is_video_dev_num {
	FIMC_IS_VIDEO_NUM_BAYER = 0,
	FIMC_IS_VIDEO_NUM_SCALERC,
	FIMC_IS_VIDEO_NUM_3DNR,
	FIMC_IS_VIDEO_NUM_SCALERP,
	FIMC_IS_VIDEO_MAX_NUM,
};

enum fimc_is_pipe_state {
	FIMC_IS_STATE_IDLE = 0,
	FIMC_IS_STATE_FW_DOWNLOADED,
	FIMC_IS_STATE_SENSOR_INITIALIZED,
	FIMC_IS_STATE_HW_STREAM_ON,
	FIMC_IS_STATE_SCALERC_STREAM_ON,
	FIMC_IS_STATE_SCALERP_STREAM_ON,
	FIMC_IS_STATE_3DNR_STREAM_ON,
	FIMC_IS_STATE_BAYER_STREAM_ON,
	FIMC_IS_STATE_SCALERC_BUFFER_PREPARED,
	FIMC_IS_STATE_SCALERP_BUFFER_PREPARED,
	FIMC_IS_STATE_3DNR_BUFFER_PREPARED,
	FIMC_IS_STATE_BAYER_BUFFER_PREPARED,

};

enum fimc_is_state {
	IS_ST_IDLE = 0,
	IS_ST_PWR_ON,
	IS_ST_FW_DOWNLOADED,
	IS_ST_OPEN_SENSOR,
	IS_ST_SETFILE_LOADED,
	IS_ST_INIT_PREVIEW_STILL,
	IS_ST_INIT_PREVIEW_VIDEO,
	IS_ST_INIT_CAPTURE_STILL,
	IS_ST_INIT_CAPTURE_VIDEO,
	IS_ST_RUN,
	IS_ST_STREAM_ON,
	IS_ST_STREAM_OFF,
	IS_ST_CHANGE_MODE,
	IS_ST_SET_PARAM,
	IS_ST_PEND,
	IS_ST_BLOCKED,
	IS_ST_CHANGE_MODE_DONE,
	IS_ST_SCALERP_FRAME_DONE,
	IS_ST_BLOCK_CMD_CLEARED,
	IS_ST_SCALERP_MASK_DONE,
	IS_ST_SCALERC_MASK_DONE,
	IS_ST_TDNR_MASK_DONE,
	IS_ST_END,
};

enum af_state {
	FIMC_IS_AF_IDLE		= 0,
	FIMC_IS_AF_SETCONFIG	= 1,
	FIMC_IS_AF_RUNNING	= 2,
	FIMC_IS_AF_LOCK		= 3,
	FIMC_IS_AF_ABORT	= 4,
};

enum af_lock_state {
	FIMC_IS_AF_UNLOCKED	= 0,
	FIMC_IS_AF_LOCKED	= 0x02
};

enum ae_lock_state {
	FIMC_IS_AE_UNLOCKED	= 0,
	FIMC_IS_AE_LOCKED	= 1
};

enum awb_lock_state {
	FIMC_IS_AWB_UNLOCKED	= 0,
	FIMC_IS_AWB_LOCKED	= 1
};

enum sensor_list {
	SENSOR_S5K3H2_CSI_A	= 1,
	SENSOR_S5K6A3_CSI_A	= 2,
	SENSOR_S5K4E5_CSI_A	= 3,
	SENSOR_S5K3H7_CSI_A	= 4,
	SENSOR_S5K3H2_CSI_B	= 101,
	SENSOR_S5K6A3_CSI_B	= 102,
	SENSOR_S5K4E5_CSI_B	= 103,
	SENSOR_S5K3H7_CSI_B	= 104,
};

enum fimc_is_power {
	FIMC_IS_PWR_ST_BASE = 0,
	FIMC_IS_PWR_ST_POWER_ON_OFF,
	FIMC_IS_PWR_ST_STREAMING,
	FIMC_IS_PWR_ST_SUSPENDED,
	FIMC_IS_PWR_ST_RESUMED,
};

struct fimc_is_dev;

struct fimc_is_fmt {
	enum v4l2_mbus_pixelcode mbus_code;
	char	*name;
	u32	pixelformat;
	u16	num_planes;
};

struct fimc_is_frame {
	struct fimc_is_fmt		format;
	u16	width;
	u16 height;
};

struct fimc_is_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct fimc_is_dev *isp);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);

	int (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	int (*cache_flush)(struct vb2_buffer *vb, u32 num_planes);
	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
};

struct fimc_is_sensor_dev {
	struct v4l2_subdev		sd;
	struct media_pad		pads;
	struct v4l2_mbus_framefmt	mbus_fmt;
	enum fimc_is_sensor_output_entity	output;
	int id_dual;			/* for dual camera scenario */
	int id_position;		/* 0 : rear camera, 1: front camera */
	enum sensor_list sensor_type;
	u32 width;
	u32 height;
	u32 offset_x;
	u32 offset_y;
	int framerate_update;
};

struct fimc_is_front_dev {
	struct v4l2_subdev		sd;
	struct media_pad		pads[FIMC_IS_FRONT_PADS_NUM];
	struct v4l2_mbus_framefmt	mbus_fmt[FIMC_IS_FRONT_PADS_NUM];
	enum fimc_is_front_input_entity	input;
	enum fimc_is_front_output_entity	output;
	u32 width;
	u32 height;

};

struct fimc_is_back_dev {
	struct v4l2_subdev		sd;
	struct media_pad		pads[FIMC_IS_BACK_PADS_NUM];
	struct v4l2_mbus_framefmt	mbus_fmt[FIMC_IS_BACK_PADS_NUM];
	enum fimc_is_back_input_entity	input;
	enum fimc_is_back_output_entity	output;
	int	dis_on;
	int	odc_on;
	int	tdnr_on;
	u32 width;
	u32 height;
	u32 dis_width;
	u32 dis_height;
};

struct fimc_is_video_dev {
	struct video_device		vd;
	struct media_pad		pads;
	struct vb2_queue		vbq;
	struct fimc_is_dev		*dev;
	struct fimc_is_frame		frame;
	unsigned int			num_buf;
	unsigned int			buf_ref_cnt;
	unsigned int			buf_mask;

	dma_addr_t buf[FIMC_IS_MAX_BUF_NUM][FIMC_IS_MAX_BUf_PLANE_NUM];
};

struct is_meminfo {
	dma_addr_t	base;		/* buffer base */
	size_t		size;		/* total length */
	dma_addr_t	vaddr_base;	/* buffer base */
	dma_addr_t	vaddr_curr;	/* current addr */
	void		*bitproc_buf;
	size_t		dvaddr;
	unsigned char	*kvaddr;
	unsigned char	*dvaddr_shared;
	unsigned char	*kvaddr_shared;
	unsigned char	*dvaddr_odc;
	unsigned char	*kvaddr_odc;
	unsigned char	*dvaddr_dis;
	unsigned char	*kvaddr_dis;
	unsigned char	*dvaddr_3dnr;
	unsigned char	*kvaddr_3dnr;
	unsigned char	*dvaddr_isp;
	unsigned char	*kvaddr_isp;
	void		*fw_cookie;

};

struct is_fw {
	const struct firmware	*info;
	int			state;
	int			ver;
};

struct is_setfile {
	const struct firmware	*info;
	int			state;
	u32			sub_index;
	u32			base;
	u32			size;
};

struct is_to_host_cmd {
	u32	cmd;
	u32	sensor_id;
	u16	num_valid_args;
	u32	arg[MAX_I2H_ARG];
};

struct is_fd_result_header {
	u32 offset;
	u32 count;
	u32 index;
	u32 target_addr;
	s32 width;
	s32 height;
};

struct is_af_info {
	u16 mode;
	u32 af_state;
	u32 af_lock_state;
	u32 ae_lock_state;
	u32 awb_lock_state;
	u16 pos_x;
	u16 pos_y;
	u16 prev_pos_x;
	u16 prev_pos_y;
	u16 use_af;
};

struct flite_frame {
	u32 o_width;
	u32 o_height;
	u32 width;
	u32 height;
	u32 offs_h;
	u32 offs_v;
};

struct fimc_is_dev {
	struct platform_device			*pdev;
	struct exynos5_platform_fimc_is		*pdata; /* depended on isp */
	struct exynos_md			*mdev;
	spinlock_t				slock;
	struct mutex				vb_lock;
	struct mutex				lock;

	struct fimc_is_sensor_dev		sensor;
	struct fimc_is_front_dev		front;
	struct fimc_is_back_dev			back;
	/* 0-bayer, 1-scalerC, 2-3DNR, 3-scalerP */
	struct fimc_is_video_dev		video[FIMC_IS_VIDEO_MAX_NUM];
	struct vb2_alloc_ctx			*alloc_ctx;

	struct resource				*regs_res;
	void __iomem				*regs;
	int					irq;
	unsigned long				state;
	unsigned long				power;
	unsigned long				pipe_state;
	wait_queue_head_t			irq_queue;
	u32					id;
	struct is_fw				fw;
	struct is_setfile			setfile;
	struct is_meminfo			mem;
	struct is_to_host_cmd			i2h_cmd;
	struct is_fd_result_header		fd_header;

	/* Shared parameter region */
#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_CPU_EXYNOS5250)
	struct device				*bus_dev;
	struct mutex				busfreq_lock;
	int					busfreq_num;
#endif
	atomic_t				p_region_num;
	unsigned long				p_region_index1;
	unsigned long				p_region_index2;
	struct is_region			*is_p_region;
	struct is_share_region			*is_shared_region;
	u32					scenario_id;
	u32					frame_count;
	u32					sensor_num;
	struct is_af_info			af;
	int					low_power_mode;

	const struct fimc_is_vb2		*vb2;
};

extern const struct v4l2_file_operations fimc_is_bayer_video_fops;
extern const struct v4l2_ioctl_ops fimc_is_bayer_video_ioctl_ops;
extern const struct vb2_ops fimc_is_bayer_qops;

extern const struct v4l2_file_operations fimc_is_scalerc_video_fops;
extern const struct v4l2_ioctl_ops fimc_is_scalerc_video_ioctl_ops;
extern const struct vb2_ops fimc_is_scalerc_qops;

extern const struct v4l2_file_operations fimc_is_scalerp_video_fops;
extern const struct v4l2_ioctl_ops fimc_is_scalerp_video_ioctl_ops;
extern const struct vb2_ops fimc_is_scalerp_qops;

extern const struct v4l2_file_operations fimc_is_3dnr_video_fops;
extern const struct v4l2_ioctl_ops fimc_is_3dnr_video_ioctl_ops;
extern const struct vb2_ops fimc_is_3dnr_qops;

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct fimc_is_vb2 fimc_is_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct fimc_is_vb2 fimc_is_vb2_ion;
#endif

void fimc_is_mem_suspend(void *alloc_ctxes);
void fimc_is_mem_resume(void *alloc_ctxes);
void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size);
void fimc_is_mem_cache_inv(const void *start_addr, unsigned long size);
int fimc_is_pipeline_s_stream_preview
	(struct media_entity *start_entity, int on);
int fimc_is_init_set(struct fimc_is_dev *dev , u32 val);
int fimc_is_load_fw(struct fimc_is_dev *dev);

#endif /* FIMC_IS_CORE_H_ */
