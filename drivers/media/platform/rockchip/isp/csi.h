/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_CSI_H
#define _RKISP_CSI_H

#include <linux/kfifo.h>

#define CSI_DEV_NAME DRIVER_NAME "-csi-subdev"

#define HDR_MAX_DUMMY_BUF 3
/* define max dmatx to use for hdr */
#define HDR_DMA_MAX 3
#define HDR_DMA0 0
#define HDR_DMA1 1
#define HDR_DMA2 2

#define IS_HDR_RDBK(x) ({ \
	typeof(x) __x = (x); \
	(__x == HDR_RDBK_FRAME1 || \
	 __x == HDR_RDBK_FRAME2 || \
	 __x == HDR_RDBK_FRAME3); \
})

enum {
	T_CMD_QUEUE,
	T_CMD_DEQUEUE,
	T_CMD_LEN,
	T_CMD_END,
};

enum hdr_op_mode {
	HDR_NORMAL = 0,
	HDR_RDBK_FRAME1 = 4,
	HDR_RDBK_FRAME2 = 5,
	HDR_RDBK_FRAME3 = 6,
	HDR_FRAMEX2_DDR = 8,
	HDR_LINEX2_DDR = 9,
	HDR_LINEX2_NO_DDR = 10,
	HDR_FRAMEX3_DDR = 12,
	HDR_LINEX3_DDR = 13,
};

enum rkisp_csi_pad {
	CSI_SINK = 0,
	CSI_SRC_CH0,
	CSI_SRC_CH1,
	CSI_SRC_CH2,
	CSI_SRC_CH3,
	CSI_SRC_CH4,
	CSI_PAD_MAX
};

struct sink_info {
	u8 index;
	u8 linked;
};

/*
 * struct rkisp_csi_device
 * sink: csi link enable flags
 * mipi_di: Data Identifier (vc[7:6],dt[5:0])
 * tx_first: flags for dmatx first Y_STATE irq
 * memory: compact or big/little endian byte order for tx/rx
 */
struct rkisp_csi_device {
	struct rkisp_device *ispdev;
	struct v4l2_subdev sd;
	struct media_pad pads[CSI_PAD_MAX];
	struct sink_info sink[CSI_PAD_MAX - 1];
	int max_pad;
	u32 err_cnt;
	u32 irq_cnt;
	u8 mipi_di[CSI_PAD_MAX - 1];
	u8 tx_first[HDR_DMA_MAX];
	u8 memory;
};

int rkisp_register_csi_subdev(struct rkisp_device *dev,
			      struct v4l2_device *v4l2_dev);
void rkisp_unregister_csi_subdev(struct rkisp_device *dev);

int rkisp_csi_config_patch(struct rkisp_device *dev);
void rkisp_csi_sof(struct rkisp_device *dev, u8 id);
#endif
