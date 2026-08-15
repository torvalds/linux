/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#ifndef _ISP4_SUBDEV_H_
#define _ISP4_SUBDEV_H_

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <media/v4l2-device.h>

#include "isp4_fw_cmd_resp.h"
#include "isp4_hw_reg.h"
#include "isp4_interface.h"
#include "isp4_video.h"

/*
 * One is for none sensor specific response which is not used now.
 * Another is for sensor specific response
 */
#define ISP4SD_MAX_FW_RESP_STREAM_NUM 2

/* Indicates the ISP status */
enum isp4sd_status {
	ISP4SD_STATUS_PWR_OFF,
	ISP4SD_STATUS_PWR_ON,
	ISP4SD_STATUS_FW_RUNNING,
	ISP4SD_STATUS_MAX
};

/* Indicates sensor and output stream status */
enum isp4sd_start_status {
	ISP4SD_START_STATUS_OFF,
	ISP4SD_START_STATUS_STARTED,
	ISP4SD_START_STATUS_START_FAIL,
};

struct isp4sd_img_buf_node {
	struct list_head node;
	struct isp4if_img_buf_info buf_info;
};

/* This is ISP output after processing Bayer raw sensor input */
struct isp4sd_output_info {
	enum isp4sd_start_status start_status;
	u32 image_size;
};

/*
 * Struct for sensor info used as ISP input or source.
 * status: sensor status.
 * output_info: ISP output after processing the sensor input.
 * start_stream_cmd_sent: indicates if ISP4FW_CMD_ID_START_STREAM was sent
 * to firmware.
 * buf_sent_cnt: number of buffers sent to receive images.
 */
struct isp4sd_sensor_info {
	struct isp4sd_output_info output_info;
	enum isp4sd_start_status status;
	bool start_stream_cmd_sent;
	u32 buf_sent_cnt;
};

/*
 * The thread is created by the driver to handle firmware responses which will
 * be waken up when a firmware-to-driver response interrupt occurs.
 */
struct isp4sd_thread_handler {
	struct task_struct *thread;
	wait_queue_head_t waitq;
	bool resp_ready;
};

struct isp4_subdev_thread_param {
	u32 idx;
	struct isp4_subdev *isp_subdev;
};

struct isp4_subdev {
	struct v4l2_subdev sdev;
	struct isp4_interface ispif;
	struct isp4vid_dev isp_vdev;

	struct media_pad sdev_pad;

	enum isp4sd_status isp_status;
	/* mutex used to synchronize the operation with firmware */
	struct mutex ops_mutex;

	struct isp4sd_thread_handler
		fw_resp_thread[ISP4SD_MAX_FW_RESP_STREAM_NUM];

	u32 host2fw_seq_num;

	struct isp4sd_sensor_info sensor_info;

	/* gpio descriptor */
	struct gpio_desc *enable_gpio;
	struct device *dev;
	void __iomem *mmio;
	struct isp4_subdev_thread_param
		isp_resp_para[ISP4SD_MAX_FW_RESP_STREAM_NUM];
	int irq[ISP4SD_MAX_FW_RESP_STREAM_NUM];
	bool irq_enabled;
	/* spin lock to access ISP_SYS_INT0_EN exclusively */
	spinlock_t irq_lock;
#ifdef CONFIG_DEBUG_FS
	bool enable_fw_log;
	struct dentry *debugfs_dir;
	char *fw_log_output;
#endif
};

int isp4sd_init(struct isp4_subdev *isp_subdev, struct v4l2_device *v4l2_dev,
		int irq[ISP4SD_MAX_FW_RESP_STREAM_NUM]);
void isp4sd_deinit(struct isp4_subdev *isp_subdev);
int isp4sd_ioc_send_img_buf(struct v4l2_subdev *sd,
			    struct isp4if_img_buf_info *buf_info);
int isp4sd_pwron_and_init(struct v4l2_subdev *sd);
int isp4sd_pwroff_and_deinit(struct v4l2_subdev *sd);

#endif /* _ISP4_SUBDEV_H_ */
