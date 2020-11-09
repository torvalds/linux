/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Rockchip Electronics Co., Ltd. */

#ifndef _RKCIF_LUMA_H
#define _RKCIF_LUMA_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "dev.h"

#define RKCIF_LUMA_READOUT_WORK_SIZE	\
	(9 * sizeof(struct rkcif_luma_readout_work))
#define RKCIF_LUMA_YSTAT_ISR_NUM	1
#define RKCIF_RAW_MAX			3

struct rkcif_luma_vdev;
struct cif_input_fmt;

enum rkcif_luma_readout_cmd {
	RKCIF_READOUT_LUMA,
};

enum rkcif_luma_frm_mode {
	RKCIF_LUMA_ONEFRM,
	RKCIF_LUMA_TWOFRM,
	RKCIF_LUMA_THREEFRM,
};

struct rkcif_luma_readout_work {
	enum rkcif_luma_readout_cmd readout;
	unsigned long long timestamp;
	unsigned int meas_type;
	unsigned int frame_id;
	struct rkisp_mipi_luma luma[RKCIF_RAW_MAX];
};

struct rkcif_luma_node {
	struct vb2_queue buf_queue;
	/* vfd lock */
	struct mutex vlock;
	struct video_device vdev;
	struct media_pad pad;
};

/*
 * struct rkcif_luma_vdev - CIF Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkcif_luma_vdev {
	struct rkcif_luma_node vnode;
	struct rkcif_device *cifdev;
	bool enable;

	spinlock_t irq_lock;	/* tasklet queue lock */
	struct list_head stat;
	struct v4l2_format vdev_fmt;
	bool streamon;

	spinlock_t rd_lock;	/* buffer queue lock */
	struct kfifo rd_kfifo;
	struct tasklet_struct rd_tasklet;

	bool ystat_rdflg[ISP2X_MIPI_RAW_MAX];
	struct rkcif_luma_readout_work work;
};

void rkcif_start_luma(struct rkcif_luma_vdev *luma_vdev, const struct cif_input_fmt *cif_fmt_in);

void rkcif_stop_luma(struct rkcif_luma_vdev *luma_vdev);

void rkcif_luma_isr(struct rkcif_luma_vdev *luma_vdev, int isp_stat, u32 frame_id);

int rkcif_register_luma_vdev(struct rkcif_luma_vdev *luma_vdev,
			     struct v4l2_device *v4l2_dev,
			     struct rkcif_device *dev);

void rkcif_unregister_luma_vdev(struct rkcif_luma_vdev *luma_vdev);

#endif /* _RKCIF_LUMA_H */
