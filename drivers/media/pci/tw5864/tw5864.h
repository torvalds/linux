/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  TW5864 driver  - common header file
 *
 *  Copyright (C) 2016 Bluecherry, LLC <maintainers@bluecherrydvr.com>
 */

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-sg.h>

#include "tw5864-reg.h"

#define PCI_DEVICE_ID_TECHWELL_5864 0x5864

#define TW5864_NORMS V4L2_STD_ALL

/* ----------------------------------------------------------- */
/* card configuration   */

#define TW5864_INPUTS 4

/* The TW5864 uses 192 (16x12) detection cells in full screen for motion
 * detection. Each detection cell is composed of 44 pixels and 20 lines for
 * NTSC and 24 lines for PAL.
 */
#define MD_CELLS_HOR 16
#define MD_CELLS_VERT 12
#define MD_CELLS (MD_CELLS_HOR * MD_CELLS_VERT)

#define H264_VLC_BUF_SIZE 0x80000
#define H264_MV_BUF_SIZE 0x2000 /* device writes 5396 bytes */
#define QP_VALUE 28
#define MAX_GOP_SIZE 255
#define GOP_SIZE MAX_GOP_SIZE

enum resolution {
	D1 = 1,
	HD1 = 2, /* half d1 - 360x(240|288) */
	CIF = 3,
	QCIF = 4,
};

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

struct tw5864_dev; /* forward delclaration */

/* buffer for one video/vbi/ts frame */
struct tw5864_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;

	unsigned int size;
};

struct tw5864_dma_buf {
	void *addr;
	dma_addr_t dma_addr;
};

enum tw5864_vid_std {
	STD_NTSC = 0, /* NTSC (M) */
	STD_PAL = 1, /* PAL (B, D, G, H, I) */
	STD_SECAM = 2, /* SECAM */
	STD_NTSC443 = 3, /* NTSC4.43 */
	STD_PAL_M = 4, /* PAL (M) */
	STD_PAL_CN = 5, /* PAL (CN) */
	STD_PAL_60 = 6, /* PAL 60 */
	STD_INVALID = 7,
	STD_AUTO = 7,
};

struct tw5864_input {
	int nr; /* input number */
	struct tw5864_dev *root;
	struct mutex lock; /* used for vidq and vdev */
	spinlock_t slock; /* used for sync between ISR, tasklet & V4L2 API */
	struct video_device vdev;
	struct v4l2_ctrl_handler hdl;
	struct vb2_queue vidq;
	struct list_head active;
	enum resolution resolution;
	unsigned int width, height;
	unsigned int frame_seqno;
	unsigned int frame_gop_seqno;
	unsigned int h264_idr_pic_id;
	int enabled;
	enum tw5864_vid_std std;
	v4l2_std_id v4l2_std;
	int tail_nb_bits;
	u8 tail;
	u8 *buf_cur_ptr;
	int buf_cur_space_left;

	u32 reg_interlacing;
	u32 reg_vlc;
	u32 reg_dsp_codec;
	u32 reg_dsp;
	u32 reg_emu;
	u32 reg_dsp_qp;
	u32 reg_dsp_ref_mvp_lambda;
	u32 reg_dsp_i4x4_weight;
	u32 buf_id;

	struct tw5864_buf *vb;

	struct v4l2_ctrl *md_threshold_grid_ctrl;
	u16 md_threshold_grid_values[12 * 16];
	int qp;
	int gop;

	/*
	 * In (1/MAX_FPS) units.
	 * For max FPS (default), set to 1.
	 * For 1 FPS, set to e.g. 32.
	 */
	int frame_interval;
	unsigned long new_frame_deadline;
};

struct tw5864_h264_frame {
	struct tw5864_dma_buf vlc;
	struct tw5864_dma_buf mv;
	int vlc_len;
	u32 checksum;
	struct tw5864_input *input;
	u64 timestamp;
	unsigned int seqno;
	unsigned int gop_seqno;
};

/* global device status */
struct tw5864_dev {
	spinlock_t slock; /* used for sync between ISR, tasklet & V4L2 API */
	struct v4l2_device v4l2_dev;
	struct tw5864_input inputs[TW5864_INPUTS];
#define H264_BUF_CNT 4
	struct tw5864_h264_frame h264_buf[H264_BUF_CNT];
	int h264_buf_r_index;
	int h264_buf_w_index;

	struct tasklet_struct tasklet;

	int encoder_busy;
	/* Input number to check next for ready raw picture (in RR fashion) */
	int next_input;

	/* pci i/o */
	char name[64];
	struct pci_dev *pci;
	void __iomem *mmio;
	u32 irqmask;
};

#define tw_readl(reg) readl(dev->mmio + reg)
#define tw_mask_readl(reg, mask) \
	(tw_readl(reg) & (mask))
#define tw_mask_shift_readl(reg, mask, shift) \
	(tw_mask_readl((reg), ((mask) << (shift))) >> (shift))

#define tw_writel(reg, value) writel((value), dev->mmio + reg)
#define tw_mask_writel(reg, mask, value) \
	tw_writel(reg, (tw_readl(reg) & ~(mask)) | ((value) & (mask)))
#define tw_mask_shift_writel(reg, mask, shift, value) \
	tw_mask_writel((reg), ((mask) << (shift)), ((value) << (shift)))

#define tw_setl(reg, bit) tw_writel((reg), tw_readl(reg) | (bit))
#define tw_clearl(reg, bit) tw_writel((reg), tw_readl(reg) & ~(bit))

u8 tw5864_indir_readb(struct tw5864_dev *dev, u16 addr);
#define tw_indir_readb(addr) tw5864_indir_readb(dev, addr)
void tw5864_indir_writeb(struct tw5864_dev *dev, u16 addr, u8 data);
#define tw_indir_writeb(addr, data) tw5864_indir_writeb(dev, addr, data)

void tw5864_irqmask_apply(struct tw5864_dev *dev);
int tw5864_video_init(struct tw5864_dev *dev, int *video_nr);
void tw5864_video_fini(struct tw5864_dev *dev);
void tw5864_prepare_frame_headers(struct tw5864_input *input);
void tw5864_h264_put_stream_header(u8 **buf, size_t *space_left, int qp,
				   int width, int height);
void tw5864_h264_put_slice_header(u8 **buf, size_t *space_left,
				  unsigned int idr_pic_id,
				  unsigned int frame_gop_seqno,
				  int *tail_nb_bits, u8 *tail);
void tw5864_request_encoded_frame(struct tw5864_input *input);
