/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright 2021-2024 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _EXTRON_DA_HD_4K_PLUS_H_
#define _EXTRON_DA_HD_4K_PLUS_H_

#include <linux/kthread.h>
#include <linux/serio.h>
#include <linux/workqueue.h>
#include <media/cec.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>

#include "cec-splitter.h"

#define DATA_SIZE 256

#define PING_PERIOD	(15 * HZ)

#define NUM_MSGS CEC_MAX_MSG_RX_QUEUE_SZ

#define MAX_PORTS (1 + 6)

#define MAX_EDID_BLOCKS 2

struct extron;

struct extron_port {
	struct cec_splitter_port port;
	struct device *dev;
	struct cec_adapter *adap;
	struct video_device vdev;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *ctrl_rx_power_present;
	struct v4l2_ctrl *ctrl_tx_hotplug;
	struct v4l2_ctrl *ctrl_tx_edid_present;
	bool is_input;
	char direction;
	char name[26];
	unsigned char edid[MAX_EDID_BLOCKS * 128];
	unsigned char edid_tmp[MAX_EDID_BLOCKS * 128];
	unsigned int edid_blocks;
	bool read_edid;
	struct extron *extron;
	struct work_struct irq_work;
	struct completion cmd_done;
	const char *response;
	unsigned int cmd_error;
	struct cec_msg rx_msg[NUM_MSGS];
	unsigned int rx_msg_cur_idx, rx_msg_num;
	/* protect rx_msg_cur_idx and rx_msg_num */
	spinlock_t msg_lock;
	u32 tx_done_status;
	bool update_phys_addr;
	u16 phys_addr;
	bool cec_was_registered;
	bool disconnected;
	bool update_has_signal;
	bool has_signal;
	bool update_has_edid;
	bool has_edid;
	bool has_4kp30;
	bool has_4kp60;
	bool has_qy;
	bool has_qs;
	u8 est_i, est_ii;

	/* locks access to the video_device */
	struct mutex video_lock;
};

struct extron {
	struct cec_splitter splitter;
	struct device *dev;
	struct serio *serio;
	/* locks access to serio */
	struct mutex serio_lock;
	unsigned int num_ports;
	unsigned int num_in_ports;
	unsigned int num_out_ports;
	char unit_name[32];
	char unit_type[64];
	char unit_fw_version[32];
	char unit_cec_engine_version[32];
	struct extron_port *ports[MAX_PORTS];
	struct cec_splitter_port *splitter_ports[MAX_PORTS];
	struct v4l2_device v4l2_dev;
	bool hpd_never_low;
	struct task_struct *kthread_setup;
	struct delayed_work work_update_edid;

	/* serializes EDID reading */
	struct mutex edid_lock;
	unsigned int edid_bytes_read;
	struct extron_port *edid_port;
	struct completion edid_completion;
	bool edid_reading;
	bool is_ready;

	struct completion cmd_done;
	const char *response;
	unsigned int cmd_error;
	char data[DATA_SIZE];
	unsigned int len;
	char reply[DATA_SIZE];
	char buf[DATA_SIZE];
	unsigned int idx;
};

#endif
