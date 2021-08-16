/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_LUMA_H
#define _RKISP_ISP_LUMA_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

#define RKISP_LUMA_READOUT_WORK_SIZE	\
	(9 * sizeof(struct rkisp_luma_readout_work))
#define RKISP_LUMA_YSTAT_ISR_NUM	4

struct rkisp_luma_vdev;

enum rkisp_luma_readout_cmd {
	RKISP_ISP_READOUT_LUMA,
};

enum rkisp_luma_frm_mode {
	RKISP_LUMA_ONEFRM,
	RKISP_LUMA_TWOFRM,
	RKISP_LUMA_THREEFRM,
};

struct rkisp_luma_readout_work {
	enum rkisp_luma_readout_cmd readout;
	unsigned long long timestamp;
	unsigned int meas_type;
	unsigned int frame_id;
	struct rkisp_mipi_luma luma[ISP2X_MIPI_RAW_MAX];
};

/*
 * struct rkisp_isp_luma_vdev - ISP Statistics device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkisp_luma_vdev {
	struct rkisp_vdev_node vnode;
	struct rkisp_device *dev;

	spinlock_t irq_lock;	/* tasklet queue lock */
	struct list_head stat;
	struct v4l2_format vdev_fmt;
	bool streamon;

	spinlock_t rd_lock;	/* buffer queue lock */
	struct kfifo rd_kfifo;
	struct tasklet_struct rd_tasklet;

	unsigned int ystat_isrcnt[ISP2X_MIPI_RAW_MAX];
	bool ystat_rdflg[ISP2X_MIPI_RAW_MAX];
	struct rkisp_luma_readout_work work;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V20)
void rkisp_luma_isr(struct rkisp_luma_vdev *luma_vdev, u32 isp_stat);

int rkisp_register_luma_vdev(struct rkisp_luma_vdev *luma_vdev,
			     struct v4l2_device *v4l2_dev,
			     struct rkisp_device *dev);

void rkisp_unregister_luma_vdev(struct rkisp_luma_vdev *luma_vdev);
#else
static inline void rkisp_unregister_luma_vdev(struct rkisp_luma_vdev *luma_vdev) {}
static inline int rkisp_register_luma_vdev(struct rkisp_luma_vdev *luma_vdev,
					   struct v4l2_device *v4l2_dev,
					   struct rkisp_device *dev)
{
	return 0;
}
#endif

#endif /* _RKISP_ISP_LUMA_H */
