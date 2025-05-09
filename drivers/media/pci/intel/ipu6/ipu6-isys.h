/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU6_ISYS_H
#define IPU6_ISYS_H

#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>

#include "ipu6.h"
#include "ipu6-fw-isys.h"
#include "ipu6-isys-csi2.h"
#include "ipu6-isys-video.h"

struct ipu6_bus_device;

#define IPU6_ISYS_ENTITY_PREFIX		"Intel IPU6"
/* FW support max 16 streams */
#define IPU6_ISYS_MAX_STREAMS		16
#define ISYS_UNISPART_IRQS	(IPU6_ISYS_UNISPART_IRQ_SW |	\
				 IPU6_ISYS_UNISPART_IRQ_CSI0 |	\
				 IPU6_ISYS_UNISPART_IRQ_CSI1)

/*
 * Current message queue configuration. These must be big enough
 * so that they never gets full. Queues are located in system memory
 */
#define IPU6_ISYS_SIZE_RECV_QUEUE 40
#define IPU6_ISYS_SIZE_SEND_QUEUE 40
#define IPU6_ISYS_SIZE_PROXY_RECV_QUEUE 5
#define IPU6_ISYS_SIZE_PROXY_SEND_QUEUE 5
#define IPU6_ISYS_NUM_RECV_QUEUE 1

#define IPU6_ISYS_MIN_WIDTH		2U
#define IPU6_ISYS_MIN_HEIGHT		1U
#define IPU6_ISYS_MAX_WIDTH		4672U
#define IPU6_ISYS_MAX_HEIGHT		3416U

/* the threshold granularity is 2KB on IPU6 */
#define IPU6_SRAM_GRANULARITY_SHIFT	11
#define IPU6_SRAM_GRANULARITY_SIZE	2048
/* the threshold granularity is 1KB on IPU6SE */
#define IPU6SE_SRAM_GRANULARITY_SHIFT	10
#define IPU6SE_SRAM_GRANULARITY_SIZE	1024
/* IS pixel buffer is 256KB, MaxSRAMSize is 200KB on IPU6 */
#define IPU6_MAX_SRAM_SIZE			(200 << 10)
/* IS pixel buffer is 128KB, MaxSRAMSize is 96KB on IPU6SE */
#define IPU6SE_MAX_SRAM_SIZE			(96 << 10)

#define IPU6EP_LTR_VALUE			200
#define IPU6EP_MIN_MEMOPEN_TH			0x4
#define IPU6EP_MTL_LTR_VALUE			1023
#define IPU6EP_MTL_MIN_MEMOPEN_TH		0xc

struct ltr_did {
	union {
		u32 value;
		struct {
			u8 val0;
			u8 val1;
			u8 val2;
			u8 val3;
		} bits;
	} lut_ltr;
	union {
		u32 value;
		struct {
			u8 th0;
			u8 th1;
			u8 th2;
			u8 th3;
		} bits;
	} lut_fill_time;
};

struct isys_iwake_watermark {
	bool iwake_enabled;
	bool force_iwake_disable;
	u32 iwake_threshold;
	u64 isys_pixelbuffer_datarate;
	struct ltr_did ltrdid;
	struct mutex mutex; /* protect whole struct */
	struct ipu6_isys *isys;
	struct list_head video_list;
};

struct ipu6_isys_csi2_config {
	u32 nlanes;
	u32 port;
};

struct sensor_async_sd {
	struct v4l2_async_connection asc;
	struct ipu6_isys_csi2_config csi2;
};

/*
 * struct ipu6_isys
 *
 * @media_dev: Media device
 * @v4l2_dev: V4L2 device
 * @adev: ISYS bus device
 * @power: Is ISYS powered on or not?
 * @isr_bits: Which bits does the ISR handle?
 * @power_lock: Serialise access to power (power state in general)
 * @csi2_rx_ctrl_cached: cached shared value between all CSI2 receivers
 * @streams_lock: serialise access to streams
 * @streams: streams per firmware stream ID
 * @fwcom: fw communication layer private pointer
 *         or optional external library private pointer
 * @phy_termcal_val: the termination calibration value, only used for DWC PHY
 * @need_reset: Isys requires d0i0->i3 transition
 * @ref_count: total number of callers fw open
 * @mutex: serialise access isys video open/release related operations
 * @stream_mutex: serialise stream start and stop, queueing requests
 * @pdata: platform data pointer
 * @csi2: CSI-2 receivers
 */
struct ipu6_isys {
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct ipu6_bus_device *adev;

	int power;
	spinlock_t power_lock;
	u32 isr_csi2_bits;
	u32 csi2_rx_ctrl_cached;
	spinlock_t streams_lock;
	struct ipu6_isys_stream streams[IPU6_ISYS_MAX_STREAMS];
	int streams_ref_count[IPU6_ISYS_MAX_STREAMS];
	void *fwcom;
	u32 phy_termcal_val;
	bool need_reset;
	bool icache_prefetch;
	bool csi2_cse_ipc_not_supported;
	unsigned int ref_count;
	unsigned int stream_opened;
	unsigned int sensor_type;

	struct mutex mutex;
	struct mutex stream_mutex;

	struct ipu6_isys_pdata *pdata;

	int (*phy_set_power)(struct ipu6_isys *isys,
			     struct ipu6_isys_csi2_config *cfg,
			     const struct ipu6_isys_csi2_timing *timing,
			     bool on);

	struct ipu6_isys_csi2 *csi2;

	struct pm_qos_request pm_qos;
	spinlock_t listlock;	/* Protect framebuflist */
	struct list_head framebuflist;
	struct list_head framebuflist_fw;
	struct v4l2_async_notifier notifier;
	struct isys_iwake_watermark iwake_watermark;
};

struct isys_fw_msgs {
	union {
		u64 dummy;
		struct ipu6_fw_isys_frame_buff_set_abi frame;
		struct ipu6_fw_isys_stream_cfg_data_abi stream;
	} fw_msg;
	struct list_head head;
	dma_addr_t dma_addr;
};

struct isys_fw_msgs *ipu6_get_fw_msg_buf(struct ipu6_isys_stream *stream);
void ipu6_put_fw_msg_buf(struct ipu6_isys *isys, uintptr_t data);
void ipu6_cleanup_fw_msg_bufs(struct ipu6_isys *isys);

extern const struct v4l2_ioctl_ops ipu6_isys_ioctl_ops;

void isys_setup_hw(struct ipu6_isys *isys);
irqreturn_t isys_isr(struct ipu6_bus_device *adev);
void update_watermark_setting(struct ipu6_isys *isys);

int ipu6_isys_mcd_phy_set_power(struct ipu6_isys *isys,
				struct ipu6_isys_csi2_config *cfg,
				const struct ipu6_isys_csi2_timing *timing,
				bool on);

int ipu6_isys_dwc_phy_set_power(struct ipu6_isys *isys,
				struct ipu6_isys_csi2_config *cfg,
				const struct ipu6_isys_csi2_timing *timing,
				bool on);

int ipu6_isys_jsl_phy_set_power(struct ipu6_isys *isys,
				struct ipu6_isys_csi2_config *cfg,
				const struct ipu6_isys_csi2_timing *timing,
				bool on);
#endif /* IPU6_ISYS_H */
