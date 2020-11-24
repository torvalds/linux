/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_STATS_H
#define _RKISP_ISP_STATS_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

#define RKISP_STATS_DDR_BUF_NUM		1
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
	unsigned int isp3a_ris;
	enum rkisp_isp_readout_cmd readout;
	unsigned long long timestamp;
};

struct rkisp_isp_stats_ops {
	void (*isr_hdl)(struct rkisp_isp_stats_vdev *stats_vdev,
			u32 isp_mis, u32 isp3a_ris);
	void (*send_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp_isp_readout_work *meas_work);
	void (*rdbk_enable)(struct rkisp_isp_stats_vdev *stats_vdev, bool en);
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

	struct rkisp_isp_stats_ops *ops;
	void *priv_ops;
	void *priv_cfg;

	struct rkisp_dummy_buffer stats_buf[RKISP_STATS_DDR_BUF_NUM];
	u32 rd_buf_idx;
	u32 wr_buf_idx;
	bool rd_stats_from_ddr;

	bool rdbk_mode;
	u32 isp_rdbk;
	u32 isp3a_rdbk;

	struct rkisp_dummy_buffer tmp_statsbuf;
	struct rkisp_buffer *cur_buf;
};

void rkisp_stats_rdbk_enable(struct rkisp_isp_stats_vdev *stats_vdev, bool en);

void rkisp_stats_first_ddr_config(struct rkisp_isp_stats_vdev *stats_vdev);

void rkisp_stats_isr(struct rkisp_isp_stats_vdev *stats_vdev,
		     u32 isp_ris, u32 isp3a_ris);

int rkisp_register_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct v4l2_device *v4l2_dev,
			       struct rkisp_device *dev);

void rkisp_unregister_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev);

#endif /* _RKISP_ISP_STATS_H */
