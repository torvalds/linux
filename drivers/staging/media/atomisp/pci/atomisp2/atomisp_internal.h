/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#ifndef __ATOMISP_INTERNAL_H__
#define __ATOMISP_INTERNAL_H__

#include "../../include/linux/atomisp_platform.h"
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/pm_qos.h>
#include <linux/idr.h>

#include <media/media-device.h>
#include <media/v4l2-subdev.h>

#ifndef ISP2401
#include "ia_css_types.h"
#include "sh_css_legacy.h"
#else
/*#include "ia_css_types.h"*/
/*#include "sh_css_legacy.h"*/
#endif

#include "atomisp_csi2.h"
#include "atomisp_file.h"
#include "atomisp_subdev.h"
#include "atomisp_tpg.h"
#include "atomisp_compat.h"

#include "gp_device.h"
#include "irq.h"
#include <linux/vmalloc.h>

#define V4L2_EVENT_FRAME_END          5

#define IS_HWREVISION(isp, rev) \
	(((isp)->media_dev.hw_revision & ATOMISP_HW_REVISION_MASK) == \
	 ((rev) << ATOMISP_HW_REVISION_SHIFT))

#define MAX_STREAM_NUM	2

#define ATOMISP_PCI_DEVICE_SOC_MASK	0xfff8
/* MRFLD with 0x1178: ISP freq can burst to 457MHz */
#define ATOMISP_PCI_DEVICE_SOC_MRFLD	0x1178
/* MRFLD with 0x1179: max ISP freq limited to 400MHz */
#define ATOMISP_PCI_DEVICE_SOC_MRFLD_1179	0x1179
/* MRFLD with 0x117a: max ISP freq is 400MHz and max freq at Vmin is 200MHz */
#define ATOMISP_PCI_DEVICE_SOC_MRFLD_117A	0x117a
#define ATOMISP_PCI_DEVICE_SOC_BYT	0x0f38
#define ATOMISP_PCI_DEVICE_SOC_ANN	0x1478
#define ATOMISP_PCI_DEVICE_SOC_CHT	0x22b8

#define ATOMISP_PCI_REV_MRFLD_A0_MAX	0
#define ATOMISP_PCI_REV_BYT_A0_MAX	4

#define ATOM_ISP_STEP_WIDTH	2
#define ATOM_ISP_STEP_HEIGHT	2

#define ATOM_ISP_MIN_WIDTH	4
#define ATOM_ISP_MIN_HEIGHT	4
#define ATOM_ISP_MAX_WIDTH	UINT_MAX
#define ATOM_ISP_MAX_HEIGHT	UINT_MAX

/* sub-QCIF resolution */
#define ATOM_RESOLUTION_SUBQCIF_WIDTH	128
#define ATOM_RESOLUTION_SUBQCIF_HEIGHT	96

#define ATOM_ISP_MAX_WIDTH_TMP	1280
#define ATOM_ISP_MAX_HEIGHT_TMP	720

#define ATOM_ISP_I2C_BUS_1	4
#define ATOM_ISP_I2C_BUS_2	5

#define ATOM_ISP_POWER_DOWN	0
#define ATOM_ISP_POWER_UP	1

#define ATOM_ISP_MAX_INPUTS	4

#define ATOMISP_SC_TYPE_SIZE	2

#define ATOMISP_ISP_TIMEOUT_DURATION		(2 * HZ)
#define ATOMISP_EXT_ISP_TIMEOUT_DURATION        (6 * HZ)
#define ATOMISP_ISP_FILE_TIMEOUT_DURATION	(60 * HZ)
#define ATOMISP_WDT_KEEP_CURRENT_DELAY          0
#define ATOMISP_ISP_MAX_TIMEOUT_COUNT	2
#define ATOMISP_CSS_STOP_TIMEOUT_US	200000

#define ATOMISP_CSS_Q_DEPTH	3
#define ATOMISP_CSS_EVENTS_MAX  16
#define ATOMISP_CONT_RAW_FRAMES 15
#define ATOMISP_METADATA_QUEUE_DEPTH_FOR_HAL	8
#define ATOMISP_S3A_BUF_QUEUE_DEPTH_FOR_HAL	8

#define ATOMISP_DELAYED_INIT_NOT_QUEUED	0
#define ATOMISP_DELAYED_INIT_QUEUED	1
#define ATOMISP_DELAYED_INIT_DONE	2

#define ATOMISP_CALC_CSS_PREV_OVERLAP(lines) \
	((lines) * 38 / 100 & 0xfffffe)

/*
 * Define how fast CPU should be able to serve ISP interrupts.
 * The bigger the value, the higher risk that the ISP is not
 * triggered sufficiently fast for it to process image during
 * vertical blanking time, increasing risk of dropped frames.
 * 1000 us is a reasonable value considering that the processing
 * time is typically ~2000 us.
 */
#define ATOMISP_MAX_ISR_LATENCY	1000

/* Add new YUVPP pipe for SOC sensor. */
#define ATOMISP_CSS_SUPPORT_YUVPP     1

#define ATOMISP_CSS_OUTPUT_SECOND_INDEX     1
#define ATOMISP_CSS_OUTPUT_DEFAULT_INDEX    0

/*
 * ATOMISP_SOC_CAMERA
 * This is to differentiate between ext-isp and soc camera in
 * Moorefield/Baytrail platform.
 */
#define ATOMISP_SOC_CAMERA(asd)  \
	(asd->isp->inputs[asd->input_curr].type == SOC_CAMERA \
	&& asd->isp->inputs[asd->input_curr].camera_caps-> \
	   sensor[asd->sensor_curr].stream_num == 1)

#define ATOMISP_USE_YUVPP(asd)  \
	(ATOMISP_SOC_CAMERA(asd) && ATOMISP_CSS_SUPPORT_YUVPP && \
	!asd->copy_mode)

#define ATOMISP_DEPTH_SENSOR_STREAMON_COUNT 2

#define ATOMISP_DEPTH_DEFAULT_MASTER_SENSOR 0
#define ATOMISP_DEPTH_DEFAULT_SLAVE_SENSOR 1

#ifdef ISP2401
#define ATOMISP_ION_DEVICE_FD_OFFSET   16
#define ATOMISP_ION_SHARED_FD_MASK     (0xFFFF)
#define ATOMISP_ION_DEVICE_FD_MASK     (~ATOMISP_ION_SHARED_FD_MASK)
#define ION_FD_UNSET (-1)

#endif
#define DIV_NEAREST_STEP(n, d, step) \
	round_down((2 * (n) + (d) * (step))/(2 * (d)), (step))

struct atomisp_input_subdev {
	unsigned int type;
	enum atomisp_camera_port port;
	struct v4l2_subdev *camera;
	struct v4l2_subdev *motor;
	struct v4l2_frmsizeenum frame_size;

	/*
	 * To show this resource is used by
	 * which stream, in ISP multiple stream mode
	 */
	struct atomisp_sub_device *asd;

	const struct atomisp_camera_caps *camera_caps;
	int sensor_index;
};

enum atomisp_dfs_mode {
	ATOMISP_DFS_MODE_AUTO = 0,
	ATOMISP_DFS_MODE_LOW,
	ATOMISP_DFS_MODE_MAX,
};

struct atomisp_regs {
	/* PCI config space info */
	u16 pcicmdsts;
	u32 ispmmadr;
	u32 msicap;
	u32 msi_addr;
	u16 msi_data;
	u8 intr;
	u32 interrupt_control;
	u32 pmcs;
	u32 cg_dis;
	u32 i_control;

	/* I-Unit PHY related info */
	u32 csi_rcomp_config;
	u32 csi_afe_dly;
	u32 csi_control;

	/* New for MRFLD */
	u32 csi_afe_rcomp_config;
	u32 csi_afe_hs_control;
	u32 csi_deadline_control;
	u32 csi_access_viol;
};

struct atomisp_sw_contex {
	bool file_input;
	int power_state;
	int running_freq;
};


#define ATOMISP_DEVICE_STREAMING_DISABLED	0
#define ATOMISP_DEVICE_STREAMING_ENABLED	1
#define ATOMISP_DEVICE_STREAMING_STOPPING	2

/*
 * ci device struct
 */
struct atomisp_device {
	struct pci_dev *pdev;
	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct atomisp_platform_data *pdata;
	void *mmu_l1_base;
	const struct firmware *firmware;

	struct pm_qos_request pm_qos;
	s32 max_isr_latency;

	/*
	 * ISP modules
	 * Multiple streams are represents by multiple
	 * atomisp_sub_device instances
	 */
	struct atomisp_sub_device *asd;
	/*
	 * this will be assiged dyanamically.
	 * For Merr/BTY(ISP2400), 2 streams are supported.
	 */
	unsigned int num_of_streams;

	struct atomisp_mipi_csi2_device csi2_port[ATOMISP_CAMERA_NR_PORTS];
	struct atomisp_tpg_device tpg;
	struct atomisp_file_device file_dev;

	/* Purpose of mutex is to protect and serialize use of isp data
	 * structures and css API calls. */
	struct rt_mutex mutex;
	/*
	 * Serialise streamoff: mutex is dropped during streamoff to
	 * cancel the watchdog queue. MUST be acquired BEFORE
	 * "mutex".
	 */
	struct mutex streamoff_mutex;

	unsigned int input_cnt;
	struct atomisp_input_subdev inputs[ATOM_ISP_MAX_INPUTS];
	struct v4l2_subdev *flash;
	struct v4l2_subdev *motor;

	struct atomisp_regs saved_regs;
	struct atomisp_sw_contex sw_contex;
	struct atomisp_css_env css_env;

	/* isp timeout status flag */
	bool isp_timeout;
	bool isp_fatal_error;
	struct workqueue_struct *wdt_work_queue;
	struct work_struct wdt_work;
#ifndef ISP2401
	atomic_t wdt_count;
#endif
	atomic_t wdt_work_queued;

	spinlock_t lock; /* Just for streaming below */

	bool need_gfx_throttle;

	unsigned int mipi_frame_size;
	const struct atomisp_dfs_config *dfs;
	unsigned int hpll_freq;

	bool css_initialized;
};

#define v4l2_dev_to_atomisp_device(dev) \
	container_of(dev, struct atomisp_device, v4l2_dev)

extern struct device *atomisp_dev;

#define atomisp_is_wdt_running(a) timer_pending(&(a)->wdt)
#ifdef ISP2401
extern void atomisp_wdt_refresh_pipe(struct atomisp_video_pipe *pipe,
					unsigned int delay);
#endif
extern void atomisp_wdt_refresh(struct atomisp_sub_device *asd, unsigned int delay);
#ifndef ISP2401
extern void atomisp_wdt_start(struct atomisp_sub_device *asd);
#else
extern void atomisp_wdt_start(struct atomisp_video_pipe *pipe);
extern void atomisp_wdt_stop_pipe(struct atomisp_video_pipe *pipe, bool sync);
#endif
extern void atomisp_wdt_stop(struct atomisp_sub_device *asd, bool sync);

#endif /* __ATOMISP_INTERNAL_H__ */
