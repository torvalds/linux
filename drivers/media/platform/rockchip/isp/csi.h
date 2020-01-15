/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_CSI_H
#define _RKISP_CSI_H

#define CSI_DEV_NAME DRIVER_NAME "-csi-subdev"

#define RKISP_HDR_DBG_MODE 0

#define HDR_MAX_DUMMY_BUF 3
/* define max dmatx to use for hdr */
#define HDR_DMA_MAX 3
#define HDR_DMA0 0
#define HDR_DMA1 1
#define HDR_DMA2 2

#define IS_HDR_DBG(x) ({ \
	typeof(x) __x = (x); \
	(__x == HDR_DBG_FRAME1 || \
	 __x == HDR_DBG_FRAME2 || \
	 __x == HDR_DBG_FRAME3); \
})

enum hdr_op_mode {
	HDR_NORMAL = 0,
	HDR_DBG_FRAME1 = 4,
	HDR_DBG_FRAME2 = 5,
	HDR_DBG_FRAME3 = 6,
	HDR_FRAMEX2_DDR = 8,
	HDR_LINEX2_DDR = 9,
	HDR_LINEX2_NO_DDR = 10,
	HDR_FRAMEX3_DDR = 12,
	HDR_LINEX3_DDR = 13,
	HDR_LINEX3_NO_DDR = 14,
};

enum rkisp_csi_pad {
	CSI_SINK,
	CSI_SRC_CH0,
	CSI_SRC_CH1,
	CSI_SRC_CH2,
	CSI_SRC_CH3,
	CSI_SRC_CH4,
	CSI_PAD_MAX
};

enum rkisp_csi_filt {
	CSI_F_VS,
	CSI_F_RD0,
	CSI_F_RD1,
	CSI_F_RD2,
	CSI_F_MAX
};

struct sink_info {
	u8 index;
	u8 linked;
};

/*
 * struct rkisp_csi_device
 * sink: csi link enable flags
 * mipi_di: Data Identifier (vc[7:6],dt[5:0])
 * filt_state: multiframe read back mode to filt irq event
 * tx_first: flags for dmatx first Y_STATE irq
 */
struct rkisp_csi_device {
	struct rkisp_device *ispdev;
	struct v4l2_subdev sd;
	struct media_pad pads[CSI_PAD_MAX];
	int max_pad;
	struct sink_info sink[CSI_PAD_MAX - 1];
	u8 mipi_di[CSI_PAD_MAX - 1];
	u8 filt_state[CSI_F_MAX];
	u8 tx_first[HDR_DMA_MAX];
};

int rkisp_register_csi_subdev(struct rkisp_device *dev,
			      struct v4l2_device *v4l2_dev);
void rkisp_unregister_csi_subdev(struct rkisp_device *dev);

int rkisp_csi_config_patch(struct rkisp_device *dev);
void rkisp_trigger_read_back(struct rkisp_csi_device *csi, u8 dma2frm);
void rkisp_csi_sof(struct rkisp_device *dev, u8 id);
#endif
