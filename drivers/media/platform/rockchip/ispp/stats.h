/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_STATS_H
#define _RKISPP_STATS_H

#include <linux/rkispp-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

#define RKISPP_STATS_READOUT_WORK_SIZE	\
	(9 * sizeof(struct rkispp_stats_readout_work))

struct rkispp_stats_vdev;

enum rkispp_stats_readout_cmd {
	RKISPP_READOUT_STATS,
};

struct rkispp_stats_readout_work {
	enum rkispp_stats_readout_cmd readout;
	unsigned long long timestamp;
	unsigned int meas_type;
	unsigned int frame_id;
};

/*
 * struct rkispp_stats_vdev - ISPP Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkispp_stats_vdev {
	struct rkispp_vdev_node vnode;
	struct rkispp_device *dev;

	spinlock_t irq_lock;
	struct list_head stat;
	struct rkispp_buffer *curr_buf;
	struct rkispp_buffer *next_buf;
	struct v4l2_format vdev_fmt;
	bool streamon;
};

void rkispp_stats_isr(struct rkispp_stats_vdev *stats_vdev, u32 mis);

int rkispp_register_stats_vdev(struct rkispp_device *dev);

void rkispp_unregister_stats_vdev(struct rkispp_device *dev);

#endif /* _RKISPP_STATS_H */
