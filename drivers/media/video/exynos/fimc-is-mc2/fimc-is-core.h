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

#include "fimc-is-interface.h"
#include "fimc-is-framemgr.h"
#include "fimc-is-device-sensor.h"
#include "fimc-is-device-ischain.h"

#include "fimc-is-video.h"
#include "fimc-is-mem.h"

#define FIMC_IS_MODULE_NAME			"exynos5-fimc-is"
#define FIMC_IS_SENSOR_ENTITY_NAME		"exynos5-fimc-is-sensor"
#define FIMC_IS_FRONT_ENTITY_NAME		"exynos5-fimc-is-front"
#define FIMC_IS_BACK_ENTITY_NAME		"exynos5-fimc-is-back"

#define FIMC_IS_COMMAND_TIMEOUT			(3*HZ)
#define FIMC_IS_STARTUP_TIMEOUT			(3*HZ)
#define FIMC_IS_SHUTDOWN_TIMEOUT		(10*HZ)
#define FIMC_IS_FLITE_STOP_TIMEOUT		(3*HZ)

#define FIMC_IS_SENSOR_MAX_ENTITIES		(1)
#define FIMC_IS_SENSOR_PAD_SOURCE_FRONT		(0)
#define FIMC_IS_SENSOR_PADS_NUM			(1)

#define FIMC_IS_FRONT_MAX_ENTITIES		(1)
#define FIMC_IS_FRONT_PAD_SINK			(0)
#define FIMC_IS_FRONT_PAD_SOURCE_BACK		(1)
#define FIMC_IS_FRONT_PAD_SOURCE_BAYER		(2)
#define FIMC_IS_FRONT_PAD_SOURCE_SCALERC	(3)
#define FIMC_IS_FRONT_PADS_NUM			(4)

#define FIMC_IS_BACK_MAX_ENTITIES		(1)
#define FIMC_IS_BACK_PAD_SINK			(0)
#define FIMC_IS_BACK_PAD_SOURCE_3DNR		(1)
#define FIMC_IS_BACK_PAD_SOURCE_SCALERP		(2)
#define FIMC_IS_BACK_PADS_NUM			(3)

#define FIMC_IS_MAX_SENSOR_NAME_LEN		(16)

#define FIMC_IS_A5_MEM_SIZE		(0x01300000)
#define FIMC_IS_A5_SEN_SIZE		(0x00100000)
#define FIMC_IS_REGION_SIZE		(0x00005000)
#define FIMC_IS_SETFILE_SIZE		(0x00140000)
#define FIMC_IS_DEBUG_REGION_ADDR	(0x01240000)
#define FIMC_IS_SHARED_REGION_ADDR	(0x012C0000)
#define FIMC_IS_FW_BASE_MASK		((1 << 26) - 1)

#define FW_SHARED_OFFSET		FIMC_IS_SHARED_REGION_ADDR
#define DEBUG_CNT			(0x0007D000) /* 500KB */
#define DEBUG_OFFSET			FIMC_IS_DEBUG_REGION_ADDR
#define DEBUGCTL_OFFSET			(DEBUG_OFFSET + DEBUG_CNT)

#define MAX_ODC_INTERNAL_BUF_WIDTH	(2560)  /* 4808 in HW */
#define MAX_ODC_INTERNAL_BUF_HEIGHT	(1920)  /* 3356 in HW */
#define SIZE_ODC_INTERNAL_BUF \
	(MAX_ODC_INTERNAL_BUF_WIDTH * MAX_ODC_INTERNAL_BUF_HEIGHT * 3)

#define MAX_DIS_INTERNAL_BUF_WIDTH	(2400)
#define MAX_DIS_INTERNAL_BUF_HEIGHT	(1360)
#define SIZE_DIS_INTERNAL_BUF \
	(MAX_DIS_INTERNAL_BUF_WIDTH * MAX_DIS_INTERNAL_BUF_HEIGHT * 2)

#define MAX_3DNR_INTERNAL_BUF_WIDTH	(1920)
#define MAX_3DNR_INTERNAL_BUF_HEIGHT	(1088)
#define SIZE_DNR_INTERNAL_BUF \
	(MAX_3DNR_INTERNAL_BUF_WIDTH * MAX_3DNR_INTERNAL_BUF_HEIGHT * 2)

#define NUM_ODC_INTERNAL_BUF		(2)
#define NUM_DIS_INTERNAL_BUF		(1)
#define NUM_DNR_INTERNAL_BUF		(2)

#define GATE_IP_ISP			(0)
#define GATE_IP_DRC			(1)
#define GATE_IP_FD			(2)
#define GATE_IP_SCC			(3)
#define GATE_IP_SCP			(4)
#define GATE_IP_ODC			(0)
#define GATE_IP_DIS			(1)
#define GATE_IP_DNR			(2)
#define DVFS_L0				(800000)
#define DVFS_L1				(700000)
#define DVFS_L1_1			(650000)
#define DVFS_L1_2			(600000)
#define DVFS_L1_3			(550000)
#define DVFS_L1_2_1			(650001) /* for rear recording */
#define DVFS_L1_3_1			(650001) /* for VT-call */
#define I2C_L0				(108000000)
#define I2C_L1				(36000000)
#define I2C_L1_1			(54000000)
#define I2C_L2				(21600000)
#define DVFS_SKIP_FRAME_NUM		(5)

/* configuration - default post processing */
#define ENABLE_SETFILE
/* #define ENABLE_DRC */
/* #define ENABLE_ODC */
/* #define ENABLE_VDIS */
#define ENABLE_TDNR
#define ENABLE_FD
#define ENABLE_CLOCK_GATE
#define ENABLE_DVFS
/* #define ENABLE_CACHE */
#define ENABLE_FAST_SHOT
#define USE_OWN_FAULT_HANDLER
#define ENABLE_MIF_400

/*
 * -----------------------------------------------------------------------------
 * Debug Message Configuration
 * -----------------------------------------------------------------------------
 */

/* #define DEBUG */
#define DBG_VIDEO
#define DBG_DEVICE
/* #define DBG_STREAMING */
#define DEBUG_INSTANCE 0xF
#define BUG_ON_ENABLE
/* #define FIXED_FPS_DEBUG */
#define FIXED_FPS_VALUE 24
/* #define DBG_FLITEISR */
#define FW_DEBUG
#define RESERVED_MEM
#define USE_FRAME_SYNC
#define USE_OTF_INTERFACE
#define BAYER_CROP_DZOOM
/* #define SCALER_CROP_DZOOM */
/* #define USE_ADVANCED_DZOOM */
/* #define TASKLET_MSG */
/* #define PRINT_CAPABILITY */
/* #define PRINT_BUFADDR */
/* #define PRINT_DZOOM */
#define ISDRV_VERSION 226

#if (defined(BAYER_CROP_DZOOM) && defined(SCALER_CROP_DZOOM))
#error BAYER_CROP_DZOOM and SCALER_CROP_DZOOM can''t be enable together
#endif

/*
 * driver version extension
 */
#ifdef ENABLE_CLOCK_GATE
#define get_drv_clock_gate() 0x1
#else
#define get_drv_clock_gate() 0x0
#endif
#ifdef ENABLE_DVFS
#define get_drv_dvfs() 0x2
#else
#define get_drv_dvfs() 0x0
#endif

#ifdef err
#undef err
#endif
#define err(fmt, args...) \
	printk(KERN_ERR "[ERR]%s:%d: " fmt "\n", __func__, __LINE__, ##args)

#define merr(fmt, this, args...) \
	printk(KERN_ERR "[ERR:%d]%s:%d: " fmt "\n", \
		this->instance, __func__, __LINE__, ##args)

#ifdef warn
#undef warn
#endif
#define warn(fmt, args...) \
	printk(KERN_WARNING "[WRN] " fmt "\n", ##args)

#define mwarn(fmt, this, args...) \
	printk(KERN_WARNING "[WRN:%d] " fmt "\n", this->instance, ##args)

#define mdbg_common(prefix, fmt, instance, args...)			\
	do {								\
		if ((1<<instance) & DEBUG_INSTANCE)			\
			printk(KERN_INFO prefix fmt, instance, ##args);	\
	} while (0)

#if (defined(DEBUG) && defined(DBG_VIDEO))
#define dbg(fmt, args...)

#define dbg_warning(fmt, args...) \
	printk(KERN_INFO "%s[WAR] Warning! " fmt, __func__, ##args)

/* debug message for video node */
#define mdbgv_vid(fmt, this, args...) \
	mdbg_common("[COM:V:%d] ", fmt, this->instance, ##args)

#define dbg_sensor(fmt, args...) \
	printk(KERN_INFO "[SEN] " fmt, ##args)

#define mdbgv_ss0(fmt, this, args...) \
	mdbg_common("[SS0:V:%d] ", fmt, this->instance, ##args)

#define mdbgv_ss1(fmt, this, args...) \
	mdbg_common("[SS1:V:%d] ", fmt, this->instance, ##args)

#define mdbgv_3a0(fmt, this, args...) \
	mdbg_common("[3A0:V:%d] ", fmt, this->instance, ##args)

#define mdbgv_3a1(fmt, this, args...) \
	mdbg_common("[3A1:V:%d] ", fmt, this->instance, ##args)

#define dbg_isp(fmt, args...) \
	printk(KERN_INFO "[ISP] " fmt, ##args)

#define mdbgv_isp(fmt, this, args...) \
	mdbg_common("[ISP:V:%d] ", fmt, this->instance, ##args)

#define dbg_scp(fmt, args...) \
	printk(KERN_INFO "[SCP] " fmt, ##args)

#define mdbgv_scp(fmt, this, args...) \
	mdbg_common("[SCP:V:%d] ", fmt, this->instance, ##args)

#define dbg_scc(fmt, args...) \
	printk(KERN_INFO "[SCC] " fmt, ##args)

#define mdbgv_scc(fmt, this, args...) \
	mdbg_common("[SCC:V:%d] ", fmt, this->instance, ##args)

#define dbg_vdisc(fmt, args...) \
	printk(KERN_INFO "[VDC] " fmt, ##args)

#define mdbgv_vdc(fmt, this, args...) \
	mdbg_common("[VDC:V:%d] ", fmt, this->instance, ##args)

#define dbg_vdiso(fmt, args...) \
	printk(KERN_INFO "[VDO] " fmt, ##args)

#define mdbgv_vdo(fmt, this, args...) \
	mdbg_common("[VDO:V:%d] ", fmt, this->instance, ##args)
#else
#define dbg(fmt, args...)

/* debug message for video node */
#define mdbgv_vid(fmt, this, args...)
#define dbg_sensor(fmt, args...)
#define mdbgv_ss0(fmt, this, args...)
#define mdbgv_ss1(fmt, this, args...)
#define mdbgv_3a0(fmt, this, args...)
#define mdbgv_3a1(fmt, this, args...)
#define dbg_isp(fmt, args...)
#define mdbgv_isp(fmt, this, args...)
#define dbg_scp(fmt, args...)
#define mdbgv_scp(fmt, this, args...)
#define dbg_scc(fmt, args...)
#define mdbgv_scc(fmt, this, args...)
#define dbg_vdisc(fmt, args...)
#define mdbgv_vdc(fmt, this, args...)
#define dbg_vdiso(fmt, args...)
#define mdbgv_vdo(fmt, this, args...)
#endif

#if (defined(DEBUG) && defined(DBG_DEVICE))
/* debug message for device */
#define mdbgd_sensor(fmt, this, args...) \
	mdbg_common("[SEN:D:%d] ", fmt, this->instance, ##args)

#define dbg_front(fmt, args...) \
	printk(KERN_INFO "[FRT] " fmt, ##args)

#define dbg_back(fmt, args...) \
	printk(KERN_INFO "[BAK] " fmt, ##args)

#define mdbgd_3a0(fmt, this, args...) \
	printk(KERN_INFO "[3A0:D:%d] " fmt, this->instance, ##args)

#define mdbgd_3a1(fmt, this, args...) \
	printk(KERN_INFO "[3A1:D:%d] " fmt, this->instance, ##args)

#define mdbgd_isp(fmt, this, args...) \
	printk(KERN_INFO "[ISP:D:%d] " fmt, this->instance, ##args)

#define dbg_ischain(fmt, args...) \
	printk(KERN_INFO "[ISC] " fmt, ##args)

#define mdbgd_ischain(fmt, this, args...) \
	printk(KERN_INFO "[ISC:D:%d] " fmt, this->instance, ##args)

#define dbg_core(fmt, args...) \
	printk(KERN_INFO "[COR] " fmt, ##args)
#else
/* debug message for device */
#define mdbgd_sensor(fmt, this, args...)
#define dbg_front(fmt, args...)
#define dbg_back(fmt, args...)
#define mdbgd_isp(fmt, this, args...)
#define dbg_ischain(fmt, args...)
#define mdbgd_ischain(fmt, this, args...)
#define dbg_core(fmt, args...)
#define dbg_interface(fmt, args...)
#define dbg_frame(fmt, args...)
#endif

#if (defined(DEBUG) && defined(DBG_STREAMING))
#define dbg_interface(fmt, args...) \
	printk(KERN_INFO "[ITF] " fmt, ##args)
#define dbg_frame(fmt, args...) \
	printk(KERN_INFO "[FRM] " fmt, ##args)
#else
#define dbg_interface(fmt, args...)
#define dbg_frame(fmt, args...)
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

struct fimc_is_core;

struct fimc_is_sensor_dev {
	struct v4l2_subdev		sd;
	struct media_pad		pads;
	struct v4l2_mbus_framefmt	mbus_fmt;
	enum fimc_is_sensor_output_entity	output;
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

struct fimc_is_clock {
	unsigned long				msk_state;
	u32					msk_cnt[GROUP_ID_MAX];
	bool					state_3a0;
	int					dvfs_level;
	int					dvfs_mif_level;
	int					dvfs_skipcnt;
	unsigned long				dvfs_state;
};

struct fimc_is_core {
	struct platform_device			*pdev;
	struct resource				*regs_res;
	void __iomem				*regs;
	int					irq;
	u32					id;
	u32					debug_cnt;
	atomic_t				rsccount;
	unsigned long				state;

	/* depended on isp */
	struct exynos5_platform_fimc_is		*pdata;
	struct exynos_md			*mdev;


	struct fimc_is_groupmgr			groupmgr;
	struct fimc_is_clock			clock;

	struct fimc_is_minfo                    minfo;
	struct fimc_is_mem			mem;
	struct fimc_is_interface		interface;

	struct fimc_is_device_sensor		sensor[FIMC_IS_MAX_NODES];
	struct fimc_is_device_ischain		ischain[FIMC_IS_MAX_NODES];

	struct fimc_is_sensor_dev		dev_sensor;
	struct fimc_is_front_dev		front;
	struct fimc_is_back_dev			back;

	/* 0-bayer, 1-scalerC, 2-3DNR, 3-scalerP */
	struct fimc_is_video			video_ss0;
	struct fimc_is_video			video_ss1;
	struct fimc_is_video			video_3a0;
	struct fimc_is_video			video_3a1;
	struct fimc_is_video			video_isp;
	struct fimc_is_video			video_scc;
	struct fimc_is_video			video_scp;
	struct fimc_is_video			video_vdc;
	struct fimc_is_video			video_vdo;

	spinlock_t				slock_clock_gate;
};

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct fimc_is_vb2 fimc_is_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct fimc_is_vb2 fimc_is_vb2_ion;
#endif

struct device *get_is_dev(void);

void fimc_is_mem_suspend(void *alloc_ctxes);
void fimc_is_mem_resume(void *alloc_ctxes);
void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size);
void fimc_is_mem_cache_inv(const void *start_addr, unsigned long size);
int fimc_is_pipeline_s_stream_preview
	(struct media_entity *start_entity, int on);
int fimc_is_init_set(struct fimc_is_core *dev , u32 val);
int fimc_is_load_fw(struct fimc_is_core *dev);
int fimc_is_load_setfile(struct fimc_is_core *dev);
int fimc_is_clock_set(struct fimc_is_core *dev,	int group_id, bool on);
int fimc_is_set_dvfs(struct fimc_is_core *core,
			struct fimc_is_device_ischain *ischain,
			int group_id, int level, int i2c_clk);
int fimc_is_resource_get(struct fimc_is_core *core);
int fimc_is_resource_put(struct fimc_is_core *core);
int fimc_is_otf_close(struct fimc_is_device_ischain *ischain);
int fimc_is_spi_reset(void *buf, u32 rx_addr, size_t size);
int fimc_is_spi_read(void *buf, u32 rx_addr, size_t size);
int fimc_is_runtime_suspend(struct device *dev);
int fimc_is_runtime_resume(struct device *dev);
#endif /* FIMC_IS_CORE_H_ */
