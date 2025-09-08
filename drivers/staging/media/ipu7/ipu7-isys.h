/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_ISYS_H
#define IPU7_ISYS_H

#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>

#include "abi/ipu7_fw_msg_abi.h"
#include "abi/ipu7_fw_isys_abi.h"

#include "ipu7.h"
#include "ipu7-isys-csi2.h"
#include "ipu7-isys-video.h"

#define IPU_ISYS_ENTITY_PREFIX		"Intel IPU7"

/* FW support max 16 streams */
#define IPU_ISYS_MAX_STREAMS		16U

/*
 * Current message queue configuration. These must be big enough
 * so that they never gets full. Queues are located in system memory
 */
#define IPU_ISYS_SIZE_RECV_QUEUE	40U
#define IPU_ISYS_SIZE_LOG_QUEUE		256U
#define IPU_ISYS_SIZE_SEND_QUEUE	40U
#define IPU_ISYS_NUM_RECV_QUEUE		1U

#define IPU_ISYS_MIN_WIDTH		2U
#define IPU_ISYS_MIN_HEIGHT		2U
#define IPU_ISYS_MAX_WIDTH		8160U
#define IPU_ISYS_MAX_HEIGHT		8190U

#define FW_CALL_TIMEOUT_JIFFIES		\
	msecs_to_jiffies(IPU_LIB_CALL_TIMEOUT_MS)

struct isys_fw_log {
	struct mutex mutex; /* protect whole struct */
	void *head;
	void *addr;
	u32 count; /* running counter of log */
	u32 size; /* actual size of log content, in bits */
};

/*
 * struct ipu7_isys
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
 * @syscom: fw communication layer context
 * @ref_count: total number of callers fw open
 * @mutex: serialise access isys video open/release related operations
 * @stream_mutex: serialise stream start and stop, queueing requests
 * @pdata: platform data pointer
 * @csi2: CSI-2 receivers
 */
struct ipu7_isys {
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct ipu7_bus_device *adev;

	int power;
	spinlock_t power_lock;	/* Serialise access to power */
	u32 isr_csi2_mask;
	u32 csi2_rx_ctrl_cached;
	spinlock_t streams_lock;
	struct ipu7_isys_stream streams[IPU_ISYS_MAX_STREAMS];
	int streams_ref_count[IPU_ISYS_MAX_STREAMS];
	u32 phy_rext_cal;
	bool icache_prefetch;
	bool csi2_cse_ipc_not_supported;
	unsigned int ref_count;
	unsigned int stream_opened;

	struct mutex mutex;	/* Serialise isys video open/release related */
	struct mutex stream_mutex;	/* Stream start, stop, queueing reqs */

	struct ipu7_isys_pdata *pdata;

	struct ipu7_isys_csi2 *csi2;
	struct isys_fw_log *fw_log;

	struct list_head requests;
	struct pm_qos_request pm_qos;
	spinlock_t listlock;	/* Protect framebuflist */
	struct list_head framebuflist;
	struct list_head framebuflist_fw;
	struct v4l2_async_notifier notifier;

	struct ipu7_insys_config *subsys_config;
	dma_addr_t subsys_config_dma_addr;
};

struct isys_fw_msgs {
	union {
		u64 dummy;
		struct ipu7_insys_buffset frame;
		struct ipu7_insys_stream_cfg stream;
	} fw_msg;
	struct list_head head;
	dma_addr_t dma_addr;
};

struct ipu7_isys_csi2_config {
	unsigned int nlanes;
	unsigned int port;
	enum v4l2_mbus_type bus_type;
};

struct sensor_async_sd {
	struct v4l2_async_connection asc;
	struct ipu7_isys_csi2_config csi2;
};

struct isys_fw_msgs *ipu7_get_fw_msg_buf(struct ipu7_isys_stream *stream);
void ipu7_put_fw_msg_buf(struct ipu7_isys *isys, uintptr_t data);
void ipu7_cleanup_fw_msg_bufs(struct ipu7_isys *isys);
int isys_isr_one(struct ipu7_bus_device *adev);
void ipu7_isys_setup_hw(struct ipu7_isys *isys);
#endif /* IPU7_ISYS_H */
