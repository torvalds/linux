/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RKISP_ISP_STATS_H
#define _RKISP_ISP_STATS_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

#define RKISP_READOUT_WORK_SIZE	\
	(8 * sizeof(struct rkisp_isp_readout_work))

struct rkisp_isp_stats_vdev;

enum rkisp_isp_readout_cmd {
	RKISP_ISP_READOUT_MEAS,
	RKISP_ISP_READOUT_META,
};

struct rkisp_isp_readout_work {
	unsigned int frame_id;
	unsigned int isp_ris;
	enum rkisp_isp_readout_cmd readout;
	unsigned long long timestamp;
};

struct rkisp_stats_ops {
	void (*get_awb_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_aec_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_afc_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_hst_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_bls_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_emb_data)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
};

struct rkisp_stats_config {
	const int ae_mean_max;
	const int hist_bin_n_max;
};

/*
 * struct rkisp_isp_stats_vdev - ISP Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkisp_isp_stats_vdev {
	struct rkisp_vdev_node vnode;
	struct rkisp_device *dev;

	spinlock_t irq_lock;
	struct list_head stat;
	struct v4l2_format vdev_fmt;
	bool streamon;

	spinlock_t rd_lock;
	struct kfifo rd_kfifo;
	struct tasklet_struct rd_tasklet;

	struct rkisp_stats_ops *ops;
	struct rkisp_stats_config *config;
};

int rkisp_stats_isr(struct rkisp_isp_stats_vdev *stats_vdev, u32 isp_ris);

int rkisp_register_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct v4l2_device *v4l2_dev,
			       struct rkisp_device *dev);

void rkisp_unregister_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev);

#endif /* _RKISP_ISP_STATS_H */
