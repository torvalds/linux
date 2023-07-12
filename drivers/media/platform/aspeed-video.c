// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2020 IBM Corp.
// Copyright (c) 2019-2020 Intel Corporation

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <uapi/linux/aspeed-video.h>

#define ASPEED_VIDEO_V4L2_MIN_BUF_REQ 3

#define DEVICE_NAME			"aspeed-video"

#define ASPEED_VIDEO_JPEG_NUM_QUALITIES	12
#define ASPEED_VIDEO_JPEG_HEADER_SIZE	10
#define ASPEED_VIDEO_JPEG_QUANT_SIZE	116
#define ASPEED_VIDEO_JPEG_DCT_SIZE	34

#define MAX_FRAME_RATE			60
#define MAX_HEIGHT			1200
#define MAX_WIDTH			1920
#define MIN_HEIGHT			480
#define MIN_WIDTH			640

#define NUM_POLARITY_CHECKS		10
#define INVALID_RESOLUTION_RETRIES	2
#define INVALID_RESOLUTION_DELAY	msecs_to_jiffies(250)
#define RESOLUTION_CHANGE_DELAY		msecs_to_jiffies(500)
#define MODE_DETECT_TIMEOUT		msecs_to_jiffies(500)
#define STOP_TIMEOUT			msecs_to_jiffies(1000)
#define DIRECT_FETCH_THRESHOLD		0x0c0000 /* 1024 * 768 */

#define VE_MAX_SRC_BUFFER_SIZE		0x8ca000 /* 1920 * 1200, 32bpp */
#define VE_JPEG_HEADER_SIZE		0x006000 /* 512 * 12 * 4 */
#define VE_BCD_BUFF_SIZE		0x9000 /* (1920/8) * (1200/8) */

#define VE_PROTECTION_KEY		0x000
#define  VE_PROTECTION_KEY_UNLOCK	0x1a038aa8

#define VE_SEQ_CTRL			0x004
#define  VE_SEQ_CTRL_TRIG_MODE_DET	BIT(0)
#define  VE_SEQ_CTRL_TRIG_CAPTURE	BIT(1)
#define  VE_SEQ_CTRL_FORCE_IDLE		BIT(2)
#define  VE_SEQ_CTRL_MULT_FRAME		BIT(3)
#define  VE_SEQ_CTRL_TRIG_COMP		BIT(4)
#define  VE_SEQ_CTRL_AUTO_COMP		BIT(5)
#define  VE_SEQ_CTRL_EN_WATCHDOG	BIT(7)
#define  VE_SEQ_CTRL_YUV420		BIT(10)
#define  VE_SEQ_CTRL_COMP_FMT		GENMASK(11, 10)
#define  VE_SEQ_CTRL_HALT		BIT(12)
#define  VE_SEQ_CTRL_EN_WATCHDOG_COMP	BIT(14)
#define  VE_SEQ_CTRL_TRIG_JPG		BIT(15)
#define  VE_SEQ_CTRL_CAP_BUSY		BIT(16)
#define  VE_SEQ_CTRL_COMP_BUSY		BIT(18)

#define AST2500_VE_SEQ_CTRL_JPEG_MODE	BIT(13)
#define AST2400_VE_SEQ_CTRL_JPEG_MODE	BIT(8)

#define VE_CTRL				0x008
#define  VE_CTRL_HSYNC_POL		BIT(0)
#define  VE_CTRL_VSYNC_POL		BIT(1)
#define  VE_CTRL_SOURCE			BIT(2)
#define  VE_CTRL_INT_DE			BIT(4)
#define  VE_CTRL_DIRECT_FETCH		BIT(5)
#define  VE_CTRL_CAPTURE_FMT		GENMASK(7, 6)
#define  VE_CTRL_AUTO_OR_CURSOR		BIT(8)
#define  VE_CTRL_CLK_INVERSE		BIT(11)
#define  VE_CTRL_CLK_DELAY		GENMASK(11, 9)
#define  VE_CTRL_INTERLACE		BIT(14)
#define  VE_CTRL_HSYNC_POL_CTRL		BIT(15)
#define  VE_CTRL_FRC			GENMASK(23, 16)
#define AST2600_VE_CTRL_EN_COMPARE_ONLY	BIT(31)

#define VE_TGS_0			0x00c
#define VE_TGS_1			0x010
#define  VE_TGS_FIRST			GENMASK(28, 16)
#define  VE_TGS_LAST			GENMASK(12, 0)

#define VE_SCALING_FACTOR		0x014
#define VE_SCALING_FILTER0		0x018
#define VE_SCALING_FILTER1		0x01c
#define VE_SCALING_FILTER2		0x020
#define VE_SCALING_FILTER3		0x024

#define VE_BCD_CTRL			0x02C
#define  VE_BCD_CTRL_EN_BCD		BIT(0)
#define  VE_BCD_CTRL_EN_ABCD		BIT(1)
#define  VE_BCD_CTRL_EN_CB		BIT(2)
#define  VE_BCD_CTRL_THR		GENMASK(23, 16)
#define  VE_BCD_CTRL_ABCD_THR		GENMASK(31, 24)

#define VE_CAP_WINDOW			0x030
#define VE_COMP_WINDOW			0x034
#define VE_COMP_PROC_OFFSET		0x038
#define VE_COMP_OFFSET			0x03c
#define VE_JPEG_ADDR			0x040
#define VE_SRC0_ADDR			0x044
#define VE_SRC_SCANLINE_OFFSET		0x048
#define VE_SRC1_ADDR			0x04c
#define VE_BCD_ADDR			0x050
#define VE_COMP_ADDR			0x054

#define VE_STREAM_BUF_SIZE		0x058
#define  VE_STREAM_BUF_SIZE_N_PACKETS	GENMASK(5, 3)
#define  VE_STREAM_BUF_SIZE_P_SIZE	GENMASK(2, 0)

#define VE_COMP_CTRL			0x060
#define  VE_COMP_CTRL_VQ_DCT_ONLY	BIT(0)
#define  VE_COMP_CTRL_VQ_4COLOR		BIT(1)
#define  VE_COMP_CTRL_QUANTIZE		BIT(2)
#define  VE_COMP_CTRL_EN_BQ		BIT(4)
#define  VE_COMP_CTRL_EN_CRYPTO		BIT(5)
#define  VE_COMP_CTRL_DCT_CHR		GENMASK(10, 6)
#define  VE_COMP_CTRL_DCT_LUM		GENMASK(15, 11)
#define  VE_COMP_CTRL_EN_HQ		BIT(16)
#define  VE_COMP_CTRL_RSVD		BIT(19)
#define  VE_COMP_CTRL_ENCODE		GENMASK(21, 20)
#define  VE_COMP_CTRL_HQ_DCT_CHR	GENMASK(26, 22)
#define  VE_COMP_CTRL_HQ_DCT_LUM	GENMASK(31, 27)

#define VE_CB_ADDR			0x06C

#define AST2400_VE_COMP_SIZE_READ_BACK	0x078
#define AST2600_VE_COMP_SIZE_READ_BACK	0x084

#define VE_SRC_LR_EDGE_DET		0x090
#define  VE_SRC_LR_EDGE_DET_LEFT	GENMASK(11, 0)
#define  VE_SRC_LR_EDGE_DET_NO_V	BIT(12)
#define  VE_SRC_LR_EDGE_DET_NO_H	BIT(13)
#define  VE_SRC_LR_EDGE_DET_NO_DISP	BIT(14)
#define  VE_SRC_LR_EDGE_DET_NO_CLK	BIT(15)
#define  VE_SRC_LR_EDGE_DET_RT		GENMASK(27, 16)
#define  VE_SRC_LR_EDGE_DET_INTERLACE	BIT(31)

#define VE_SRC_TB_EDGE_DET		0x094
#define  VE_SRC_TB_EDGE_DET_TOP		GENMASK(12, 0)
#define  VE_SRC_TB_EDGE_DET_BOT		GENMASK(28, 16)

#define VE_MODE_DETECT_STATUS		0x098
#define  VE_MODE_DETECT_H_PERIOD	GENMASK(11, 0)
#define  VE_MODE_DETECT_EXTSRC_ADC	BIT(12)
#define  VE_MODE_DETECT_H_STABLE	BIT(13)
#define  VE_MODE_DETECT_V_STABLE	BIT(14)
#define  VE_MODE_DETECT_V_LINES		GENMASK(27, 16)
#define  VE_MODE_DETECT_STATUS_VSYNC	BIT(28)
#define  VE_MODE_DETECT_STATUS_HSYNC	BIT(29)
#define  VE_MODE_DETECT_VSYNC_RDY	BIT(30)
#define  VE_MODE_DETECT_HSYNC_RDY	BIT(31)

#define VE_SYNC_STATUS			0x09c
#define  VE_SYNC_STATUS_HSYNC		GENMASK(11, 0)
#define  VE_SYNC_STATUS_VSYNC		GENMASK(27, 16)

#define VE_H_TOTAL_PIXELS		0x0A0

#define AST2600_VE_BOUNDING_X		0x0D4
#define AST2600_VE_BOUNDING_Y		0x0D8

#define VE_INTERRUPT_CTRL		0x304
#define VE_INTERRUPT_STATUS		0x308
#define  VE_INTERRUPT_MODE_DETECT_WD	BIT(0)
#define  VE_INTERRUPT_CAPTURE_COMPLETE	BIT(1)
#define  VE_INTERRUPT_COMP_READY	BIT(2)
#define  VE_INTERRUPT_COMP_COMPLETE	BIT(3)
#define  VE_INTERRUPT_MODE_DETECT	BIT(4)
#define  VE_INTERRUPT_FRAME_COMPLETE	BIT(5)
#define  VE_INTERRUPT_DECODE_ERR	BIT(6)
#define  VE_INTERRUPT_HALT_READY	BIT(8)
#define  VE_INTERRUPT_HANG_WD		BIT(9)
#define  VE_INTERRUPT_STREAM_DESC	BIT(10)
#define  VE_INTERRUPT_VSYNC_DESC	BIT(11)

#define VE_MODE_DETECT			0x30c
#define  VE_MODE_DT_HOR_TOLER		GENMASK(31, 28)
#define  VE_MODE_DT_VER_TOLER		GENMASK(27, 24)
#define  VE_MODE_DT_HOR_STABLE		GENMASK(23, 20)
#define  VE_MODE_DT_VER_STABLE		GENMASK(19, 16)
#define  VE_MODE_DT_EDG_THROD		GENMASK(15, 8)

#define VE_MEM_RESTRICT_START		0x310
#define VE_MEM_RESTRICT_END		0x314

#define SCU_MISC_CTRL			0xC0
#define  SCU_DPLL_SOURCE		BIT(20)

#define GFX_CTRL			0x60
#define  GFX_CTRL_ENABLE		BIT(0)
#define  GFX_CTRL_FMT			GENMASK(9, 7)

#define GFX_H_DISPLAY			0x70
#define  GFX_H_DISPLAY_DE		GENMASK(28, 16)
#define  GFX_H_DISPLAY_TOTAL		GENMASK(12, 0)

#define GFX_V_DISPLAY			0x78
#define  GFX_V_DISPLAY_DE		GENMASK(27, 16)
#define  GFX_V_DISPLAY_TOTAL		GENMASK(11, 0)

#define GFX_DISPLAY_ADDR		0x80

/*
 * @VIDEO_MODE_DETECT_DONE:	a flag raised if signal lock
 * @VIDEO_RES_CHANGE:		a flag raised if res_change work on-going
 * @VIDEO_RES_DETECT:		a flag raised if res. detection on-going
 * @VIDEO_STREAMING:		a flag raised if user requires stream-on
 * @VIDEO_FRAME_INPRG:		a flag raised if hw working on a frame
 * @VIDEO_STOPPED:		a flag raised if device release
 * @VIDEO_CLOCKS_ON:		a flag raised if clk is on
 * @VIDEO_BOUNDING_BOX:		a flag raised if box-finding for partial-jpeg
 */
enum {
	VIDEO_MODE_DETECT_DONE,
	VIDEO_RES_CHANGE,
	VIDEO_RES_DETECT,
	VIDEO_STREAMING,
	VIDEO_FRAME_INPRG,
	VIDEO_STOPPED,
	VIDEO_CLOCKS_ON,
	VIDEO_BOUNDING_BOX,
};

enum aspeed_video_format {
	VIDEO_FMT_STANDARD = 0,
	VIDEO_FMT_ASPEED,
	VIDEO_FMT_PARTIAL,
	VIDEO_FMT_MAX = VIDEO_FMT_PARTIAL
};

// for VE_CTRL_CAPTURE_FMT
enum aspeed_video_capture_format {
	VIDEO_CAP_FMT_YUV_STUDIO_SWING = 0,
	VIDEO_CAP_FMT_YUV_FULL_SWING,
	VIDEO_CAP_FMT_RGB,
	VIDEO_CAP_FMT_GRAY,
	VIDEO_CAP_FMT_MAX
};

struct aspeed_video_addr {
	unsigned int size;
	dma_addr_t dma;
	void *virt;
};

struct aspeed_video_box {
	struct v4l2_rect box;
	struct list_head link;
};

struct aspeed_video_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head link;
};

struct aspeed_video_perf {
	ktime_t last_sample;
	u32 totaltime;
	u32 duration;
	u32 duration_min;
	u32 duration_max;
};

#define to_aspeed_video_buffer(x) \
	container_of((x), struct aspeed_video_buffer, vb)

/**
 * struct aspeed_video - driver data
 *
 * @res_work:		holds the delayed_work for res-detection if unlock
 * @buffers:		holds the list of buffer queued from user
 * @flags:		holds the state of video
 * @sequence:		holds the last number of frame completed
 * @max_compressed_size:holds max compressed stream's size
 * @srcs:		holds the buffer information for srcs
 * @jpeg:		holds the buffer information for jpeg header
 * @bcd:		holds the buffer information for bcd work
 * @dbg_src:		holds the buffer information for debug input
 * @yuv420:		a flag raised if JPEG subsampling is 420
 * @format:		holds the video format
 * @hq_mode:		a flag raised if HQ is enabled. Only for VIDEO_FMT_ASPEED
 * @input:		holds the video input
 * @frame_rate:		holds the frame_rate
 * @jpeg_quality:	holds jpeq's quality (0~11)
 * @jpeg_hq_quality:	holds hq's quality (1~12) only if hq_mode enabled
 * @frame_bottom:	end position of video data in vertical direction
 * @frame_left:		start position of video data in horizontal direction
 * @frame_right:	end position of video data in horizontal direction
 * @frame_top:		start position of video data in vertical direction
 * @perf:		holds the statistics primary for debugfs
 * @bounding_box:	holds the video rect for partial-jpeg
 * @boxes:			holds the list of video-rect info for each partial-jpeg
 */
struct aspeed_video {
	void __iomem *base;
	struct clk *eclk;
	struct clk *vclk;
	struct reset_control *reset;

	struct device *dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_device v4l2_dev;
	struct v4l2_pix_format pix_fmt;
	struct v4l2_bt_timings active_timings;
	struct v4l2_bt_timings detected_timings;
	u32 v4l2_input_status;
	struct vb2_queue queue;
	struct video_device vdev;
	struct mutex video_lock;	/* v4l2 and videobuf2 lock */

	struct regmap *scu;
	struct regmap *gfx;

	u32 version;
	u32 jpeg_mode;
	u32 comp_size_read;
	u32 compare_only;

	wait_queue_head_t wait;
	spinlock_t lock;		/* buffer list lock */
	struct delayed_work res_work;
	struct list_head buffers;
	unsigned long flags;
	unsigned int sequence;

	unsigned int max_compressed_size;
	struct aspeed_video_addr srcs[2];
	struct aspeed_video_addr jpeg;
	struct aspeed_video_addr bcd;
	struct aspeed_video_addr dbg_src;

	bool yuv420;
	enum aspeed_video_format format;
	bool hq_mode;
	enum aspeed_video_input input;
	unsigned int frame_rate;
	unsigned int jpeg_quality;
	unsigned int jpeg_hq_quality;

	unsigned int frame_bottom;
	unsigned int frame_left;
	unsigned int frame_right;
	unsigned int frame_top;

	struct aspeed_video_perf perf;
	struct v4l2_rect bounding_box;
	struct list_head boxes;
};

#define to_aspeed_video(x) container_of((x), struct aspeed_video, v4l2_dev)

struct aspeed_video_config {
	u32 version;
	u32 jpeg_mode;
	u32 comp_size_read;
	u32 compare_only;
};

static const struct aspeed_video_config ast2400_config = {
	.version = 4,
	.jpeg_mode = AST2400_VE_SEQ_CTRL_JPEG_MODE,
	.comp_size_read = AST2400_VE_COMP_SIZE_READ_BACK,
	.compare_only = 0,
};

static const struct aspeed_video_config ast2500_config = {
	.version = 5,
	.jpeg_mode = AST2500_VE_SEQ_CTRL_JPEG_MODE,
	.comp_size_read = AST2400_VE_COMP_SIZE_READ_BACK,
	.compare_only = 0,
};

static const struct aspeed_video_config ast2600_config = {
	.version = 6,
	.jpeg_mode = AST2500_VE_SEQ_CTRL_JPEG_MODE,
	.comp_size_read = AST2600_VE_COMP_SIZE_READ_BACK,
	.compare_only = AST2600_VE_CTRL_EN_COMPARE_ONLY,
};

static const u32 aspeed_video_jpeg_header[ASPEED_VIDEO_JPEG_HEADER_SIZE] = {
	0xe0ffd8ff, 0x464a1000, 0x01004649, 0x60000101, 0x00006000, 0x0f00feff,
	0x00002d05, 0x00000000, 0x00000000, 0x00dbff00
};

static const u32 aspeed_video_jpeg_quant[ASPEED_VIDEO_JPEG_QUANT_SIZE] = {
	0x081100c0, 0x00000000, 0x00110103, 0x03011102, 0xc4ff0111, 0x00001f00,
	0x01010501, 0x01010101, 0x00000000, 0x00000000, 0x04030201, 0x08070605,
	0xff0b0a09, 0x10b500c4, 0x03010200, 0x03040203, 0x04040505, 0x7d010000,
	0x00030201, 0x12051104, 0x06413121, 0x07615113, 0x32147122, 0x08a19181,
	0xc1b14223, 0xf0d15215, 0x72623324, 0x160a0982, 0x1a191817, 0x28272625,
	0x35342a29, 0x39383736, 0x4544433a, 0x49484746, 0x5554534a, 0x59585756,
	0x6564635a, 0x69686766, 0x7574736a, 0x79787776, 0x8584837a, 0x89888786,
	0x9493928a, 0x98979695, 0xa3a29a99, 0xa7a6a5a4, 0xb2aaa9a8, 0xb6b5b4b3,
	0xbab9b8b7, 0xc5c4c3c2, 0xc9c8c7c6, 0xd4d3d2ca, 0xd8d7d6d5, 0xe2e1dad9,
	0xe6e5e4e3, 0xeae9e8e7, 0xf4f3f2f1, 0xf8f7f6f5, 0xc4fffaf9, 0x00011f00,
	0x01010103, 0x01010101, 0x00000101, 0x00000000, 0x04030201, 0x08070605,
	0xff0b0a09, 0x11b500c4, 0x02010200, 0x04030404, 0x04040507, 0x77020100,
	0x03020100, 0x21050411, 0x41120631, 0x71610751, 0x81322213, 0x91421408,
	0x09c1b1a1, 0xf0523323, 0xd1726215, 0x3424160a, 0x17f125e1, 0x261a1918,
	0x2a292827, 0x38373635, 0x44433a39, 0x48474645, 0x54534a49, 0x58575655,
	0x64635a59, 0x68676665, 0x74736a69, 0x78777675, 0x83827a79, 0x87868584,
	0x928a8988, 0x96959493, 0x9a999897, 0xa5a4a3a2, 0xa9a8a7a6, 0xb4b3b2aa,
	0xb8b7b6b5, 0xc3c2bab9, 0xc7c6c5c4, 0xd2cac9c8, 0xd6d5d4d3, 0xdad9d8d7,
	0xe5e4e3e2, 0xe9e8e7e6, 0xf4f3f2ea, 0xf8f7f6f5, 0xdafffaf9, 0x01030c00,
	0x03110200, 0x003f0011
};

static const u32 aspeed_video_jpeg_dct[ASPEED_VIDEO_JPEG_NUM_QUALITIES]
				      [ASPEED_VIDEO_JPEG_DCT_SIZE] = {
	{ 0x0d140043, 0x0c0f110f, 0x11101114, 0x17141516, 0x1e20321e,
	  0x3d1e1b1b, 0x32242e2b, 0x4b4c3f48, 0x44463f47, 0x61735a50,
	  0x566c5550, 0x88644644, 0x7a766c65, 0x4d808280, 0x8c978d60,
	  0x7e73967d, 0xdbff7b80, 0x1f014300, 0x272d2121, 0x3030582d,
	  0x697bb958, 0xb8b9b97b, 0xb9b8a6a6, 0xb9b9b9b9, 0xb9b9b9b9,
	  0xb9b9b9b9, 0xb9b9b9b9, 0xb9b9b9b9, 0xb9b9b9b9, 0xb9b9b9b9,
	  0xb9b9b9b9, 0xb9b9b9b9, 0xb9b9b9b9, 0xffb9b9b9 },
	{ 0x0c110043, 0x0a0d0f0d, 0x0f0e0f11, 0x14111213, 0x1a1c2b1a,
	  0x351a1818, 0x2b1f2826, 0x4142373f, 0x3c3d373e, 0x55644e46,
	  0x4b5f4a46, 0x77573d3c, 0x6b675f58, 0x43707170, 0x7a847b54,
	  0x6e64836d, 0xdbff6c70, 0x1b014300, 0x22271d1d, 0x2a2a4c27,
	  0x5b6ba04c, 0xa0a0a06b, 0xa0a0a0a0, 0xa0a0a0a0, 0xa0a0a0a0,
	  0xa0a0a0a0, 0xa0a0a0a0, 0xa0a0a0a0, 0xa0a0a0a0, 0xa0a0a0a0,
	  0xa0a0a0a0, 0xa0a0a0a0, 0xa0a0a0a0, 0xffa0a0a0 },
	{ 0x090e0043, 0x090a0c0a, 0x0c0b0c0e, 0x110e0f10, 0x15172415,
	  0x2c151313, 0x241a211f, 0x36372e34, 0x31322e33, 0x4653413a,
	  0x3e4e3d3a, 0x62483231, 0x58564e49, 0x385d5e5d, 0x656d6645,
	  0x5b536c5a, 0xdbff595d, 0x16014300, 0x1c201818, 0x22223f20,
	  0x4b58853f, 0x85858558, 0x85858585, 0x85858585, 0x85858585,
	  0x85858585, 0x85858585, 0x85858585, 0x85858585, 0x85858585,
	  0x85858585, 0x85858585, 0x85858585, 0xff858585 },
	{ 0x070b0043, 0x07080a08, 0x0a090a0b, 0x0d0b0c0c, 0x11121c11,
	  0x23110f0f, 0x1c141a19, 0x2b2b2429, 0x27282428, 0x3842332e,
	  0x313e302e, 0x4e392827, 0x46443e3a, 0x2c4a4a4a, 0x50565137,
	  0x48425647, 0xdbff474a, 0x12014300, 0x161a1313, 0x1c1c331a,
	  0x3d486c33, 0x6c6c6c48, 0x6c6c6c6c, 0x6c6c6c6c, 0x6c6c6c6c,
	  0x6c6c6c6c, 0x6c6c6c6c, 0x6c6c6c6c, 0x6c6c6c6c, 0x6c6c6c6c,
	  0x6c6c6c6c, 0x6c6c6c6c, 0x6c6c6c6c, 0xff6c6c6c },
	{ 0x06090043, 0x05060706, 0x07070709, 0x0a09090a, 0x0d0e160d,
	  0x1b0d0c0c, 0x16101413, 0x21221c20, 0x1e1f1c20, 0x2b332824,
	  0x26302624, 0x3d2d1f1e, 0x3735302d, 0x22393a39, 0x3f443f2b,
	  0x38334338, 0xdbff3739, 0x0d014300, 0x11130e0e, 0x15152613,
	  0x2d355026, 0x50505035, 0x50505050, 0x50505050, 0x50505050,
	  0x50505050, 0x50505050, 0x50505050, 0x50505050, 0x50505050,
	  0x50505050, 0x50505050, 0x50505050, 0xff505050 },
	{ 0x04060043, 0x03040504, 0x05040506, 0x07060606, 0x09090f09,
	  0x12090808, 0x0f0a0d0d, 0x16161315, 0x14151315, 0x1d221b18,
	  0x19201918, 0x281e1514, 0x2423201e, 0x17262726, 0x2a2d2a1c,
	  0x25222d25, 0xdbff2526, 0x09014300, 0x0b0d0a0a, 0x0e0e1a0d,
	  0x1f25371a, 0x37373725, 0x37373737, 0x37373737, 0x37373737,
	  0x37373737, 0x37373737, 0x37373737, 0x37373737, 0x37373737,
	  0x37373737, 0x37373737, 0x37373737, 0xff373737 },
	{ 0x02030043, 0x01020202, 0x02020203, 0x03030303, 0x04040704,
	  0x09040404, 0x07050606, 0x0b0b090a, 0x0a0a090a, 0x0e110d0c,
	  0x0c100c0c, 0x140f0a0a, 0x1211100f, 0x0b131313, 0x1516150e,
	  0x12111612, 0xdbff1213, 0x04014300, 0x05060505, 0x07070d06,
	  0x0f121b0d, 0x1b1b1b12, 0x1b1b1b1b, 0x1b1b1b1b, 0x1b1b1b1b,
	  0x1b1b1b1b, 0x1b1b1b1b, 0x1b1b1b1b, 0x1b1b1b1b, 0x1b1b1b1b,
	  0x1b1b1b1b, 0x1b1b1b1b, 0x1b1b1b1b, 0xff1b1b1b },
	{ 0x01020043, 0x01010101, 0x01010102, 0x02020202, 0x03030503,
	  0x06030202, 0x05030404, 0x07070607, 0x06070607, 0x090b0908,
	  0x080a0808, 0x0d0a0706, 0x0c0b0a0a, 0x070c0d0c, 0x0e0f0e09,
	  0x0c0b0f0c, 0xdbff0c0c, 0x03014300, 0x03040303, 0x04040804,
	  0x0a0c1208, 0x1212120c, 0x12121212, 0x12121212, 0x12121212,
	  0x12121212, 0x12121212, 0x12121212, 0x12121212, 0x12121212,
	  0x12121212, 0x12121212, 0x12121212, 0xff121212 },
	{ 0x01020043, 0x01010101, 0x01010102, 0x02020202, 0x03030503,
	  0x06030202, 0x05030404, 0x07070607, 0x06070607, 0x090b0908,
	  0x080a0808, 0x0d0a0706, 0x0c0b0a0a, 0x070c0d0c, 0x0e0f0e09,
	  0x0c0b0f0c, 0xdbff0c0c, 0x02014300, 0x03030202, 0x04040703,
	  0x080a0f07, 0x0f0f0f0a, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f,
	  0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f,
	  0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0xff0f0f0f },
	{ 0x01010043, 0x01010101, 0x01010101, 0x01010101, 0x02020302,
	  0x04020202, 0x03020303, 0x05050405, 0x05050405, 0x07080606,
	  0x06080606, 0x0a070505, 0x09080807, 0x05090909, 0x0a0b0a07,
	  0x09080b09, 0xdbff0909, 0x02014300, 0x02030202, 0x03030503,
	  0x07080c05, 0x0c0c0c08, 0x0c0c0c0c, 0x0c0c0c0c, 0x0c0c0c0c,
	  0x0c0c0c0c, 0x0c0c0c0c, 0x0c0c0c0c, 0x0c0c0c0c, 0x0c0c0c0c,
	  0x0c0c0c0c, 0x0c0c0c0c, 0x0c0c0c0c, 0xff0c0c0c },
	{ 0x01010043, 0x01010101, 0x01010101, 0x01010101, 0x01010201,
	  0x03010101, 0x02010202, 0x03030303, 0x03030303, 0x04050404,
	  0x04050404, 0x06050303, 0x06050505, 0x03060606, 0x07070704,
	  0x06050706, 0xdbff0606, 0x01014300, 0x01020101, 0x02020402,
	  0x05060904, 0x09090906, 0x09090909, 0x09090909, 0x09090909,
	  0x09090909, 0x09090909, 0x09090909, 0x09090909, 0x09090909,
	  0x09090909, 0x09090909, 0x09090909, 0xff090909 },
	{ 0x01010043, 0x01010101, 0x01010101, 0x01010101, 0x01010101,
	  0x01010101, 0x01010101, 0x01010101, 0x01010101, 0x02020202,
	  0x02020202, 0x03020101, 0x03020202, 0x01030303, 0x03030302,
	  0x03020303, 0xdbff0403, 0x01014300, 0x01010101, 0x01010201,
	  0x03040602, 0x06060604, 0x06060606, 0x06060606, 0x06060606,
	  0x06060606, 0x06060606, 0x06060606, 0x06060606, 0x06060606,
	  0x06060606, 0x06060606, 0x06060606, 0xff060606 }
};

static const struct v4l2_dv_timings_cap aspeed_video_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.min_width = MIN_WIDTH,
		.max_width = MAX_WIDTH,
		.min_height = MIN_HEIGHT,
		.max_height = MAX_HEIGHT,
		.min_pixelclock = 6574080, /* 640 x 480 x 24Hz */
		.max_pixelclock = 138240000, /* 1920 x 1200 x 60Hz */
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_CVT | V4L2_DV_BT_STD_GTF,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM,
	},
};

static const char * const format_str[] = {"Standard JPEG",
	"Aspeed JPEG", "Partial JPEG"};
static const char * const input_str[] = {"GFX", "BMC GFX", "MEMORY"};

static unsigned int debug;

static bool aspeed_video_alloc_buf(struct aspeed_video *video,
				   struct aspeed_video_addr *addr,
				   unsigned int size);

static void aspeed_video_free_buf(struct aspeed_video *video,
				  struct aspeed_video_addr *addr);

static void aspeed_video_init_jpeg_table(u32 *table, bool yuv420)
{
	int i;
	unsigned int base;

	for (i = 0; i < ASPEED_VIDEO_JPEG_NUM_QUALITIES; i++) {
		base = 256 * i;	/* AST HW requires this header spacing */
		memcpy(&table[base], aspeed_video_jpeg_header,
		       sizeof(aspeed_video_jpeg_header));

		base += ASPEED_VIDEO_JPEG_HEADER_SIZE;
		memcpy(&table[base], aspeed_video_jpeg_dct[i],
		       sizeof(aspeed_video_jpeg_dct[i]));

		base += ASPEED_VIDEO_JPEG_DCT_SIZE;
		memcpy(&table[base], aspeed_video_jpeg_quant,
		       sizeof(aspeed_video_jpeg_quant));

		if (yuv420)
			table[base + 2] = 0x00220103;
	}
}

// just update jpeg dct table per 420/444
static void aspeed_video_update_jpeg_table(u32 *table, bool yuv420)
{
	int i;
	unsigned int base;

	for (i = 0; i < ASPEED_VIDEO_JPEG_NUM_QUALITIES; i++) {
		base = 256 * i;	/* AST HW requires this header spacing */
		base += ASPEED_VIDEO_JPEG_HEADER_SIZE +
			ASPEED_VIDEO_JPEG_DCT_SIZE;

		table[base + 2] = (yuv420) ? 0x00220103 : 0x00110103;
	}
}

static void aspeed_video_update(struct aspeed_video *video, u32 reg, u32 clear,
				u32 bits)
{
	u32 t = readl(video->base + reg);
	u32 before = t;

	t &= ~clear;
	t |= bits;
	writel(t, video->base + reg);
	v4l2_dbg(3, debug, &video->v4l2_dev, "update %03x[%08x -> %08x]\n",
		 reg, before, readl(video->base + reg));
}

static u32 aspeed_video_read(struct aspeed_video *video, u32 reg)
{
	u32 t = readl(video->base + reg);

	v4l2_dbg(3, debug, &video->v4l2_dev, "read %03x[%08x]\n", reg, t);
	return t;
}

static void aspeed_video_write(struct aspeed_video *video, u32 reg, u32 val)
{
	writel(val, video->base + reg);
	v4l2_dbg(3, debug, &video->v4l2_dev, "write %03x[%08x]\n", reg,
		 readl(video->base + reg));
}

static void update_perf(struct aspeed_video_perf *p)
{
	struct aspeed_video *v = container_of(p, struct aspeed_video,
					      perf);

	p->duration =
		ktime_to_ms(ktime_sub(ktime_get(),  p->last_sample));
	p->totaltime += p->duration;

	p->duration_max = max(p->duration, p->duration_max);
	p->duration_min = min(p->duration, p->duration_min);
	v4l2_dbg(2, debug, &v->v4l2_dev, "time consumed: %d ms\n",
		 p->duration);
}

static void aspeed_video_partial_jpeg_update_regs(struct aspeed_video *v)
{
	if (test_bit(VIDEO_BOUNDING_BOX, &v->flags)) {
		aspeed_video_update(v, VE_SEQ_CTRL,
				    v->jpeg_mode,
				    VE_SEQ_CTRL_AUTO_COMP);
		aspeed_video_update(v, VE_BCD_CTRL, 0,
				    VE_BCD_CTRL_EN_BCD);
		aspeed_video_write(v, VE_COMP_WINDOW,
				   v->pix_fmt.width << 16 |
				   v->pix_fmt.height);
		v4l2_dbg(1, debug, &v->v4l2_dev,
			 "%s: BCD enabled\n", __func__);
	} else {
		u32 scan_lines = aspeed_video_read(v, VE_SRC_SCANLINE_OFFSET);
		dma_addr_t addr = aspeed_video_read(v, VE_SRC0_ADDR);
		u32 offset;

		aspeed_video_update(v, VE_SEQ_CTRL,
				    VE_SEQ_CTRL_AUTO_COMP,
				    v->jpeg_mode);
		aspeed_video_update(v, VE_BCD_CTRL,
				    VE_BCD_CTRL_EN_BCD, 0);
		aspeed_video_write(v, VE_COMP_WINDOW,
				   v->bounding_box.width << 16 |
				   v->bounding_box.height);

		offset = (scan_lines * v->bounding_box.top) +
			 ((256 * v->bounding_box.left) >> (v->yuv420 ? 4:3));
		aspeed_video_write(v, VE_SRC0_ADDR, addr + offset);
		v4l2_dbg(1, debug, &v->v4l2_dev,
			 "%s: BCD disabled\n", __func__);
	}
}

static int aspeed_video_start_frame(struct aspeed_video *video)
{
	dma_addr_t addr;
	unsigned long flags;
	struct aspeed_video_buffer *buf;
	u32 seq_ctrl = aspeed_video_read(video, VE_SEQ_CTRL);
	bool bcd_buf_need = (video->format != VIDEO_FMT_STANDARD);

	if (video->v4l2_input_status) {
		v4l2_dbg(1, debug, &video->v4l2_dev, "No signal; don't start frame\n");
		return 0;
	}

	if (!(seq_ctrl & VE_SEQ_CTRL_COMP_BUSY) ||
	    !(seq_ctrl & VE_SEQ_CTRL_CAP_BUSY)) {
		v4l2_dbg(1, debug, &video->v4l2_dev, "Engine busy; don't start frame\n");
		return -EBUSY;
	}

	if (bcd_buf_need && !video->bcd.size) {
		if (!aspeed_video_alloc_buf(video, &video->bcd,
					    VE_BCD_BUFF_SIZE)) {
			dev_err(video->dev, "Failed to allocate BCD buffer\n");
			dev_err(video->dev, "don't start frame\n");
			return -ENOMEM;
		}
		aspeed_video_write(video, VE_BCD_ADDR, video->bcd.dma);
		v4l2_dbg(1, debug, &video->v4l2_dev, "bcd addr(%pad) size(%d)\n",
			 &video->bcd.dma, video->bcd.size);
	} else if (!bcd_buf_need && video->bcd.size) {
		aspeed_video_free_buf(video, &video->bcd);
	}

	if (video->input == VIDEO_INPUT_GFX) {
		u32 val;

		// update input buffer address as gfx's
		regmap_read(video->gfx, GFX_DISPLAY_ADDR, &val);
		aspeed_video_write(video, VE_TGS_0, val);
	} else if (video->input == VIDEO_INPUT_MEM) {
		aspeed_video_write(video, VE_TGS_0, video->dbg_src.dma);
	}

	spin_lock_irqsave(&video->lock, flags);
	buf = list_first_entry_or_null(&video->buffers,
				       struct aspeed_video_buffer, link);
	if (!buf) {
		spin_unlock_irqrestore(&video->lock, flags);
		v4l2_dbg(1, debug, &video->v4l2_dev, "No buffers; don't start frame\n");
		return -EPROTO;
	}

	set_bit(VIDEO_FRAME_INPRG, &video->flags);
	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	spin_unlock_irqrestore(&video->lock, flags);

	aspeed_video_write(video, VE_COMP_PROC_OFFSET, 0);
	aspeed_video_write(video, VE_COMP_OFFSET, 0);
	aspeed_video_write(video, VE_COMP_ADDR, addr);

	aspeed_video_update(video, VE_INTERRUPT_CTRL, 0,
			    VE_INTERRUPT_COMP_COMPLETE);

	if (video->format == VIDEO_FMT_PARTIAL) {
		aspeed_video_partial_jpeg_update_regs(video);
		if (test_bit(VIDEO_BOUNDING_BOX, &video->flags)) {
			video->perf.last_sample = ktime_get();
			seq_ctrl = VE_SEQ_CTRL_TRIG_CAPTURE | VE_SEQ_CTRL_TRIG_COMP;
		} else {
			seq_ctrl = VE_SEQ_CTRL_TRIG_COMP;
		}
	} else {
		video->perf.last_sample = ktime_get();
		seq_ctrl = VE_SEQ_CTRL_TRIG_CAPTURE | VE_SEQ_CTRL_TRIG_COMP;
	}
	aspeed_video_update(video, VE_SEQ_CTRL, 0, seq_ctrl);

	return 0;
}

static void aspeed_video_enable_mode_detect(struct aspeed_video *video)
{
	/* Enable mode detect interrupts */
	aspeed_video_update(video, VE_INTERRUPT_CTRL, 0,
			    VE_INTERRUPT_MODE_DETECT);

	/* Disable mode detect in order to re-trigger */
	aspeed_video_update(video, VE_SEQ_CTRL,
			    VE_SEQ_CTRL_TRIG_MODE_DET, 0);

	/* Trigger mode detect */
	aspeed_video_update(video, VE_SEQ_CTRL, 0, VE_SEQ_CTRL_TRIG_MODE_DET);
}

static void aspeed_video_off(struct aspeed_video *video)
{
	if (!test_bit(VIDEO_CLOCKS_ON, &video->flags))
		return;

	/* Disable interrupts */
	aspeed_video_write(video, VE_INTERRUPT_CTRL, 0);
	aspeed_video_write(video, VE_INTERRUPT_STATUS, 0xffffffff);

	/* Turn off the relevant clocks */
	clk_disable(video->eclk);
	clk_disable(video->vclk);

	clear_bit(VIDEO_CLOCKS_ON, &video->flags);
}

static void aspeed_video_on(struct aspeed_video *video)
{
	if (test_bit(VIDEO_CLOCKS_ON, &video->flags))
		return;

	/* Turn on the relevant clocks */
	clk_enable(video->vclk);
	clk_enable(video->eclk);

	set_bit(VIDEO_CLOCKS_ON, &video->flags);
}

static void aspeed_video_reset(struct aspeed_video *v)
{
	reset_control_assert(v->reset);
	udelay(100);
	reset_control_deassert(v->reset);
}

static void aspeed_video_bufs_done(struct aspeed_video *video,
				   enum vb2_buffer_state state)
{
	unsigned long flags;
	struct aspeed_video_buffer *buf;
	struct aspeed_video_box *box, *tmp;

	spin_lock_irqsave(&video->lock, flags);
	list_for_each_entry(buf, &video->buffers, link)
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	INIT_LIST_HEAD(&video->buffers);

	list_for_each_entry_safe(box, tmp, &video->boxes, link) {
		list_del(&box->link);
		kfree(box);
	}
	INIT_LIST_HEAD(&video->boxes);
	spin_unlock_irqrestore(&video->lock, flags);
}

static void aspeed_video_irq_res_change(struct aspeed_video *video, ulong delay)
{
	v4l2_dbg(1, debug, &video->v4l2_dev, "Resolution changed; resetting\n");

	set_bit(VIDEO_RES_CHANGE, &video->flags);
	clear_bit(VIDEO_FRAME_INPRG, &video->flags);

	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;

	aspeed_video_write(video, VE_INTERRUPT_CTRL, 0);
	aspeed_video_write(video, VE_INTERRUPT_STATUS, 0xffffffff);
	aspeed_video_reset(video);
	aspeed_video_bufs_done(video, VB2_BUF_STATE_ERROR);

	schedule_delayed_work(&video->res_work, delay);
}

static inline bool _box_data_changed(struct aspeed_video *v, u8 data)
{
	if (v->version >= 6)
		return ((data & 0xf) != 0xf);

	return ((data & 0xf) == 0xf);
}

static void aspeed_video_get_bounding_box(struct aspeed_video *v,
					  struct v4l2_rect *box)
{
	u16 min_x, min_y, max_x, max_y;
	u16 w, h, i, j;
	u32 bytesperline;
	u8 mb_shift = v->yuv420 ? 4 : 3;
	u8 *bcd_buf = v->bcd.virt;

	if (bcd_buf == NULL) {
		box->left = 0;
		box->top = 0;
		box->width = v->pix_fmt.width;
		box->height = v->pix_fmt.width;
		v4l2_dbg(1, debug, &v->v4l2_dev, "%s: bcd buf not ready yet\n", __func__);
		return;
	}

	w = v->pix_fmt.width >> mb_shift;
	h = v->pix_fmt.height >> mb_shift;
	v4l2_dbg(1, debug, &v->v4l2_dev, "%s: macrobox_shift(%d) size (%d * %d)\n",
		 __func__, mb_shift, w, h);

	min_x = 0x3ff;
	min_y = 0x3ff;
	max_x = 0;
	max_y = 0;

	for (j = 0; j < h; j++) {
		bytesperline = w * j;
		for (i = 0; i < w; i++) {
			if (_box_data_changed(v, *(bcd_buf + bytesperline + i))) {
				min_x = min(i, min_x);
				max_x = max(i, max_x);
				min_y = min(j, min_y);
				max_y = max(j, max_y);

				// skip line if max_x can't be bigger
				if (max_x == w)
					i = w;
				// skip the pixels between min_x ~ max_x
				if (max_x > min_x && i > min_x && i < max_x)
					i = max_x;
			}
		}
	}
	v4l2_dbg(1, debug, &v->v4l2_dev,
		 "%s: left %d right %d top %d bottom %d\n", __func__,
		 min_x, max_x, min_y, max_y);

	// clear bcd flag
	if (v->version < 6)
		memset(bcd_buf, 0x01, (w * h));

	// use full size every 8 frames
	if (IS_ALIGNED(v->sequence, 8)) {
		min_x = 0;
		max_x = w - 1;
		min_y = 0;
		max_y = h - 1;
	} else if (min_x > max_x || min_y > max_y || max_x > w || max_y > h) {
		memset(box, 0, sizeof(*box));
		v4l2_dbg(1, debug, &v->v4l2_dev, "box not found\n");
		return;
	}

	box->left = min_x << mb_shift;
	box->top = min_y << mb_shift;
	box->width = (max_x + 1 - min_x) << mb_shift;
	box->height = (max_y + 1 - min_y) << mb_shift;
	v4l2_dbg(1, debug, &v->v4l2_dev,
		 "%s: x: %d, y: %d, w: %d , h: %d\n", __func__,
		 box->left, box->top, box->width, box->height);
}

static void aspeed_video_swap_src_buf(struct aspeed_video *v)
{
	if (v->format == VIDEO_FMT_STANDARD)
		return;

	/* Reset bcd buffer to have a full frame update every 8 frames.  */
	if (IS_ALIGNED(v->sequence, 8))
		memset((u8 *)v->bcd.virt, 0x00, VE_BCD_BUFF_SIZE);

	if (v->sequence & 0x01) {
		aspeed_video_write(v, VE_SRC0_ADDR, v->srcs[1].dma);
		aspeed_video_write(v, VE_SRC1_ADDR, v->srcs[0].dma);
	} else {
		aspeed_video_write(v, VE_SRC0_ADDR, v->srcs[0].dma);
		aspeed_video_write(v, VE_SRC1_ADDR, v->srcs[1].dma);
	}
}

static void aspeed_video_frame_done_handler(struct aspeed_video *video,
					    bool buf_done)
{
	struct aspeed_video_buffer *buf;
	bool empty = true;
	u32 frame_size;

	if (!buf_done)
		return;

	spin_lock(&video->lock);
	clear_bit(VIDEO_FRAME_INPRG, &video->flags);
	buf = list_first_entry_or_null(&video->buffers,
				       struct aspeed_video_buffer,
				       link);
	if (buf) {
		frame_size = aspeed_video_read(video,
					       video->comp_size_read);
		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, frame_size);

		/*
		 * VIDEO_FMT_ASPEED requires continuous update.
		 * On the contrary, standard jpeg can keep last buffer
		 * to always have the latest result.
		 */
		if ((video->format != VIDEO_FMT_ASPEED) &&
		    list_is_last(&buf->link, &video->buffers)) {
			empty = false;
			v4l2_dbg(1, debug, &video->v4l2_dev, "skip to keep last frame updated\n");
		} else {
			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			buf->vb.sequence = video->sequence++;
			buf->vb.field = V4L2_FIELD_NONE;
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_DONE);
			list_del(&buf->link);
			empty = list_empty(&video->buffers);
			if (video->format == VIDEO_FMT_PARTIAL) {
				struct aspeed_video_box *box =
					kmalloc(sizeof(struct aspeed_video_box),
						GFP_KERNEL);

				box->box = video->bounding_box;
				list_add_tail(&box->link, &video->boxes);
			}
		}
	}
	spin_unlock(&video->lock);

	aspeed_video_swap_src_buf(video);

	if (test_bit(VIDEO_STREAMING, &video->flags) && !empty &&
	    (video->input != VIDEO_INPUT_MEM)) {
		set_bit(VIDEO_BOUNDING_BOX, &video->flags);
		aspeed_video_start_frame(video);
	}
}

static irqreturn_t aspeed_video_thread_irq(int irq, void *arg)
{
	struct aspeed_video *v = arg;

	aspeed_video_get_bounding_box(v, &v->bounding_box);

	if (v->bounding_box.width && v->bounding_box.height)
		clear_bit(VIDEO_BOUNDING_BOX, &v->flags);
	else
		set_bit(VIDEO_BOUNDING_BOX, &v->flags);

	aspeed_video_start_frame(v);

	return IRQ_HANDLED;
}

static irqreturn_t aspeed_video_irq(int irq, void *arg)
{
	struct aspeed_video *video = arg;
	u32 sts = aspeed_video_read(video, VE_INTERRUPT_STATUS);
	bool get_box = false;

	aspeed_video_write(video, VE_INTERRUPT_STATUS, sts);
	sts &= aspeed_video_read(video, VE_INTERRUPT_CTRL);

	v4l2_dbg(2, debug, &video->v4l2_dev, "irq sts=%#x %s%s%s%s\n", sts,
		 sts & VE_INTERRUPT_MODE_DETECT_WD ? ", unlock" : "",
		 sts & VE_INTERRUPT_MODE_DETECT ? ", lock" : "",
		 sts & VE_INTERRUPT_CAPTURE_COMPLETE ? ", capture-done" : "",
		 sts & VE_INTERRUPT_COMP_COMPLETE ? ", comp-done" : "");

	/*
	 * Resolution changed or signal was lost; reset the engine and
	 * re-initialize
	 */
	if (sts & VE_INTERRUPT_MODE_DETECT_WD) {
		aspeed_video_irq_res_change(video, 0);
		return IRQ_HANDLED;
	}

	if (sts & VE_INTERRUPT_MODE_DETECT) {
		if (test_bit(VIDEO_RES_DETECT, &video->flags)) {
			aspeed_video_update(video, VE_INTERRUPT_CTRL,
					    VE_INTERRUPT_MODE_DETECT, 0);
			sts &= ~VE_INTERRUPT_MODE_DETECT;
			set_bit(VIDEO_MODE_DETECT_DONE, &video->flags);
			wake_up_interruptible_all(&video->wait);
		} else {
			/*
			 * Signal acquired while NOT doing resolution
			 * detection; reset the engine and re-initialize
			 */
			aspeed_video_irq_res_change(video,
						    RESOLUTION_CHANGE_DELAY);
			return IRQ_HANDLED;
		}
	}

	if (sts & VE_INTERRUPT_COMP_COMPLETE) {
		bool frame_done = false;

		if (video->format != VIDEO_FMT_PARTIAL)
			frame_done = true;
		else if (!test_bit(VIDEO_BOUNDING_BOX, &video->flags) &&
			 video->bounding_box.width && video->bounding_box.height)
			frame_done = true;

		aspeed_video_update(video, VE_SEQ_CTRL,
				    VE_SEQ_CTRL_TRIG_CAPTURE |
				    VE_SEQ_CTRL_FORCE_IDLE |
				    VE_SEQ_CTRL_TRIG_COMP, 0);
		aspeed_video_update(video, VE_INTERRUPT_CTRL,
				    VE_INTERRUPT_COMP_COMPLETE, 0);
		sts &= ~VE_INTERRUPT_COMP_COMPLETE;

		if (frame_done) {
			update_perf(&video->perf);
			aspeed_video_frame_done_handler(video, frame_done);
		} else {
			get_box = true;
		}
	}

	return get_box ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static void aspeed_video_check_and_set_polarity(struct aspeed_video *video)
{
	int i;
	int hsync_counter = 0;
	int vsync_counter = 0;
	u32 sts, ctrl;

	for (i = 0; i < NUM_POLARITY_CHECKS; ++i) {
		sts = aspeed_video_read(video, VE_MODE_DETECT_STATUS);
		if (sts & VE_MODE_DETECT_STATUS_VSYNC)
			vsync_counter--;
		else
			vsync_counter++;

		if (sts & VE_MODE_DETECT_STATUS_HSYNC)
			hsync_counter--;
		else
			hsync_counter++;
	}

	ctrl = aspeed_video_read(video, VE_CTRL);

	if (hsync_counter < 0) {
		ctrl |= VE_CTRL_HSYNC_POL;
		video->detected_timings.polarities &=
			~V4L2_DV_HSYNC_POS_POL;
	} else {
		ctrl &= ~VE_CTRL_HSYNC_POL;
		video->detected_timings.polarities |=
			V4L2_DV_HSYNC_POS_POL;
	}

	if (vsync_counter < 0) {
		ctrl |= VE_CTRL_VSYNC_POL;
		video->detected_timings.polarities &=
			~V4L2_DV_VSYNC_POS_POL;
	} else {
		ctrl &= ~VE_CTRL_VSYNC_POL;
		video->detected_timings.polarities |=
			V4L2_DV_VSYNC_POS_POL;
	}

	aspeed_video_write(video, VE_CTRL, ctrl);
}

static bool aspeed_video_alloc_buf(struct aspeed_video *video,
				   struct aspeed_video_addr *addr,
				   unsigned int size)
{
	addr->virt = dma_alloc_coherent(video->dev, size, &addr->dma,
					GFP_KERNEL);
	if (!addr->virt)
		return false;

	addr->size = size;
	return true;
}

static void aspeed_video_free_buf(struct aspeed_video *video,
				  struct aspeed_video_addr *addr)
{
	dma_free_coherent(video->dev, addr->size, addr->virt, addr->dma);
	addr->size = 0;
	addr->dma = 0ULL;
	addr->virt = NULL;
}

/*
 * Get the minimum HW-supported compression buffer size for the frame size.
 * Assume worst-case JPEG compression size is 1/2 raw size. This should be
 * plenty even for maximum quality; any worse and the engine will simply return
 * incomplete JPEGs.
 */
static void aspeed_video_calc_compressed_size(struct aspeed_video *video,
					      unsigned int frame_size)
{
	int i, j;
	u32 compression_buffer_size_reg = 0;
	unsigned int size;
	const unsigned int num_compression_packets = 4;
	const unsigned int compression_packet_size = 1024;
	const unsigned int max_compressed_size = frame_size * 2; /* 4bpp / 2 */

	video->max_compressed_size = UINT_MAX;

	for (i = 0; i < 8; ++i) {
		for (j = 0; j < 6; ++j) {
			size = (num_compression_packets << j) *
				(compression_packet_size << i);
			if (size < max_compressed_size)
				continue;

			if (size < video->max_compressed_size) {
				compression_buffer_size_reg = (j << 3) | i;
				video->max_compressed_size = size;
			}
		}
	}

	aspeed_video_write(video, VE_STREAM_BUF_SIZE,
			   compression_buffer_size_reg);

	v4l2_dbg(1, debug, &video->v4l2_dev, "Max compressed size: %#x\n",
		 video->max_compressed_size);
}

/*
 * Update v4l2_bt_timings per current status.
 * frame_top/frame_bottom/frame_left/frame_right need to be ready.
 *
 * The following registers start counting from sync's rising edge:
 * 1. VR090: frame edge's left and right
 * 2. VR094: frame edge's top and bottom
 * 3. VR09C: counting from sync's rising edge to falling edge
 *
 * [Vertical timing]
 *             +--+     +-------------------+     +--+
 *             |  |     |     v i d e o     |     |  |
 *          +--+  +-----+                   +-----+  +---+
 *        vsync+--+
 *    frame_top+--------+
 * frame_bottom+----------------------------+
 *
 *                   +-------------------+
 *                   |     v i d e o     |
 *       +--+  +-----+                   +-----+  +---+
 *          |  |                               |  |
 *          +--+                               +--+
 *        vsync+-------------------------------+
 *    frame_top+-----+
 * frame_bottom+-------------------------+
 *
 * [Horizontal timing]
 *             +--+     +-------------------+     +--+
 *             |  |     |     v i d e o     |     |  |
 *          +--+  +-----+                   +-----+  +---+
 *        hsync+--+
 *   frame_left+--------+
 *  frame_right+----------------------------+
 *
 *                   +-------------------+
 *                   |     v i d e o     |
 *       +--+  +-----+                   +-----+  +---+
 *          |  |                               |  |
 *          +--+                               +--+
 *        hsync+-------------------------------+
 *   frame_left+-----+
 *  frame_right+-------------------------+
 *
 * @v: the struct of aspeed_video
 * @det: v4l2_bt_timings to be updated.
 */
static void aspeed_video_get_timings(struct aspeed_video *v,
				     struct v4l2_bt_timings *det)
{
	u32 mds, sync, htotal, vtotal, vsync, hsync;

	mds = aspeed_video_read(v, VE_MODE_DETECT_STATUS);
	sync = aspeed_video_read(v, VE_SYNC_STATUS);
	htotal = aspeed_video_read(v, VE_H_TOTAL_PIXELS);
	vtotal = FIELD_GET(VE_MODE_DETECT_V_LINES, mds);
	vsync = FIELD_GET(VE_SYNC_STATUS_VSYNC, sync);
	hsync = FIELD_GET(VE_SYNC_STATUS_HSYNC, sync);

	/*
	 * This is a workaround for polarity detection.
	 * Because ast-soc counts sync from sync's rising edge, the reg value
	 * of sync would be larger than video's active area if negative.
	 */
	if (vsync > det->height)
		det->polarities &= ~V4L2_DV_VSYNC_POS_POL;
	else
		det->polarities |= V4L2_DV_VSYNC_POS_POL;
	if (hsync > det->width)
		det->polarities &= ~V4L2_DV_HSYNC_POS_POL;
	else
		det->polarities |= V4L2_DV_HSYNC_POS_POL;

	if (det->polarities & V4L2_DV_VSYNC_POS_POL) {
		det->vbackporch = v->frame_top - vsync;
		det->vfrontporch = vtotal - v->frame_bottom;
		det->vsync = vsync;
	} else {
		det->vbackporch = v->frame_top;
		det->vfrontporch = vsync - v->frame_bottom;
		det->vsync = vtotal - vsync;
	}

	if (det->polarities & V4L2_DV_HSYNC_POS_POL) {
		det->hbackporch = v->frame_left - hsync;
		det->hfrontporch = htotal - v->frame_right;
		det->hsync = hsync;
	} else {
		det->hbackporch = v->frame_left;
		det->hfrontporch = hsync - v->frame_right;
		det->hsync = htotal - hsync;
	}

	v4l2_dbg(1, debug, &v->v4l2_dev, "Vertical sync(%s) lines(%d %d %d %d)\n",
		 (det->polarities & V4L2_DV_VSYNC_POS_POL) ? "+" : "-",
		 det->vfrontporch, det->vsync, det->vbackporch, det->height);
	v4l2_dbg(1, debug, &v->v4l2_dev, "Horizontal sync(%s) pixels(%d %d %d %d)\n",
		 (det->polarities & V4L2_DV_HSYNC_POS_POL) ? "+" : "-",
		 det->hfrontporch, det->hsync, det->hbackporch, det->width);
}

static void aspeed_video_get_resolution_gfx(struct aspeed_video *video,
					    struct v4l2_bt_timings *det)
{
	u32 h_val, v_val;

	regmap_read(video->gfx, GFX_H_DISPLAY, &h_val);
	regmap_read(video->gfx, GFX_V_DISPLAY, &v_val);

	det->width = FIELD_GET(GFX_H_DISPLAY_DE, h_val) + 1;
	det->height = FIELD_GET(GFX_V_DISPLAY_DE, v_val) + 1;
	video->v4l2_input_status = 0;
}

#define res_check(v) test_and_clear_bit(VIDEO_MODE_DETECT_DONE, &(v)->flags)

static void aspeed_video_get_resolution_vga(struct aspeed_video *video,
					    struct v4l2_bt_timings *det)
{
	bool invalid_resolution = true;
	int rc;
	int tries = 0;
	u32 mds;
	u32 src_lr_edge;
	u32 src_tb_edge;

	det->width = MIN_WIDTH;
	det->height = MIN_HEIGHT;
	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;
	memset(&video->perf, 0, sizeof(video->perf));

	do {
		if (tries) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (schedule_timeout(INVALID_RESOLUTION_DELAY))
				return;
		}

		set_bit(VIDEO_RES_DETECT, &video->flags);
		aspeed_video_update(video, VE_CTRL,
				    VE_CTRL_VSYNC_POL | VE_CTRL_HSYNC_POL, 0);
		aspeed_video_enable_mode_detect(video);

		rc = wait_event_interruptible_timeout(video->wait,
						      res_check(video),
						      MODE_DETECT_TIMEOUT);
		if (!rc) {
			v4l2_dbg(1, debug, &video->v4l2_dev, "Timed out; first mode detect\n");
			clear_bit(VIDEO_RES_DETECT, &video->flags);
			return;
		}

		mds = aspeed_video_read(video, VE_MODE_DETECT_STATUS);
		if (!(mds & VE_MODE_DETECT_H_STABLE) ||
		    !(mds & VE_MODE_DETECT_V_STABLE) ||
		    (mds & VE_MODE_DETECT_EXTSRC_ADC)) {
			v4l2_dbg(1, debug, &video->v4l2_dev, "detect status(%#x) unstable, try again\n",
				 mds);
			continue;
		}

		aspeed_video_check_and_set_polarity(video);

		aspeed_video_enable_mode_detect(video);

		rc = wait_event_interruptible_timeout(video->wait,
						      res_check(video),
						      MODE_DETECT_TIMEOUT);
		clear_bit(VIDEO_RES_DETECT, &video->flags);
		if (!rc) {
			v4l2_dbg(1, debug, &video->v4l2_dev, "Timed out; second mode detect\n");
			return;
		}

		src_lr_edge = aspeed_video_read(video, VE_SRC_LR_EDGE_DET);
		src_tb_edge = aspeed_video_read(video, VE_SRC_TB_EDGE_DET);

		video->frame_bottom = FIELD_GET(VE_SRC_TB_EDGE_DET_BOT, src_tb_edge);
		video->frame_top = FIELD_GET(VE_SRC_TB_EDGE_DET_TOP, src_tb_edge);

		if (video->frame_top > video->frame_bottom)
			continue;

		video->frame_right = FIELD_GET(VE_SRC_LR_EDGE_DET_RT, src_lr_edge);
		video->frame_left = FIELD_GET(VE_SRC_LR_EDGE_DET_LEFT, src_lr_edge);

		if (video->frame_left > video->frame_right)
			continue;

		invalid_resolution = false;
	} while (invalid_resolution && (tries++ < INVALID_RESOLUTION_RETRIES));

	if (invalid_resolution) {
		v4l2_dbg(1, debug, &video->v4l2_dev, "Invalid resolution detected\n");
		return;
	}

	det->height = (video->frame_bottom - video->frame_top) + 1;
	det->width = (video->frame_right - video->frame_left) + 1;
	video->v4l2_input_status = 0;

	aspeed_video_get_timings(video, det);

	/* Enable mode-detect watchdog, resolution-change watchdog */
	aspeed_video_update(video, VE_INTERRUPT_CTRL, 0,
			    VE_INTERRUPT_MODE_DETECT_WD);
	aspeed_video_update(video, VE_SEQ_CTRL, 0, VE_SEQ_CTRL_EN_WATCHDOG);
}

static void aspeed_video_get_resolution(struct aspeed_video *video)
{
	struct v4l2_bt_timings *det = &video->detected_timings;

	// if input is MEM, leave resolution decided by user through set_dv_timings
	if (video->input == VIDEO_INPUT_MEM) {
		video->v4l2_input_status = 0;
		return;
	}

	if (video->input == VIDEO_INPUT_GFX)
		aspeed_video_get_resolution_gfx(video, det);
	else
		aspeed_video_get_resolution_vga(video, det);

	v4l2_dbg(1, debug, &video->v4l2_dev, "Got resolution: %dx%d\n",
		 det->width, det->height);
}

static void aspeed_video_set_resolution(struct aspeed_video *video)
{
	struct v4l2_bt_timings *act = &video->active_timings;
	unsigned int size = act->width * ALIGN(act->height, 8);

	/* Set capture/compression frame sizes */
	aspeed_video_calc_compressed_size(video, size);

	if (!IS_ALIGNED(act->width, 64)) {
		/*
		 * This is a workaround to fix a silicon bug on A1 and A2
		 * revisions. Since it doesn't break capturing operation of
		 * other revisions, use it for all revisions without checking
		 * the revision ID. It picked new width which is a very next
		 * 64-pixels aligned value to minimize memory bandwidth
		 * and to get better access speed from video engine.
		 */
		u32 width = ALIGN(act->width, 64);

		aspeed_video_write(video, VE_CAP_WINDOW, width << 16 | act->height);
		size = width * ALIGN(act->height, 8);
	} else {
		aspeed_video_write(video, VE_CAP_WINDOW,
				   act->width << 16 | act->height);
	}
	aspeed_video_write(video, VE_COMP_WINDOW,
			   act->width << 16 | act->height);
	aspeed_video_write(video, VE_SRC_SCANLINE_OFFSET, act->width * 4);

	/* Don't use direct mode below 1024 x 768 (irqs don't fire) */
	if (video->input == VIDEO_INPUT_VGA && size < DIRECT_FETCH_THRESHOLD) {
		v4l2_dbg(1, debug, &video->v4l2_dev, "Capture: Sync Mode\n");
		aspeed_video_write(video, VE_TGS_0,
				   FIELD_PREP(VE_TGS_FIRST,
					      video->frame_left - 1) |
				   FIELD_PREP(VE_TGS_LAST,
					      video->frame_right));
		aspeed_video_write(video, VE_TGS_1,
				   FIELD_PREP(VE_TGS_FIRST, video->frame_top) |
				   FIELD_PREP(VE_TGS_LAST,
					      video->frame_bottom + 1));
		aspeed_video_update(video, VE_CTRL,
				    VE_CTRL_INT_DE | VE_CTRL_DIRECT_FETCH,
				    VE_CTRL_INT_DE);
	} else {
		u32 ctrl, val, bpp;

		v4l2_dbg(1, debug, &video->v4l2_dev, "Capture: Direct Mode\n");
		ctrl = VE_CTRL_DIRECT_FETCH;
		if (video->input == VIDEO_INPUT_GFX) {
			regmap_read(video->gfx, GFX_CTRL, &val);
			bpp = FIELD_GET(GFX_CTRL_FMT, val) ? 32 : 16;
			if (bpp == 16)
				ctrl |= VE_CTRL_INT_DE;
			aspeed_video_write(video, VE_TGS_1, act->width * (bpp >> 3));
		} else if (video->input == VIDEO_INPUT_MEM)
			aspeed_video_write(video, VE_TGS_1, act->width * 4);
		aspeed_video_update(video, VE_CTRL,
				    VE_CTRL_INT_DE | VE_CTRL_DIRECT_FETCH,
				    ctrl);
	}

	size *= 4;

	if (size != video->srcs[0].size) {
		if (video->srcs[0].size)
			aspeed_video_free_buf(video, &video->srcs[0]);
		if (video->srcs[1].size)
			aspeed_video_free_buf(video, &video->srcs[1]);

		if (!aspeed_video_alloc_buf(video, &video->srcs[0], size))
			goto err_mem;
		if (!aspeed_video_alloc_buf(video, &video->srcs[1], size))
			goto err_mem;

		v4l2_dbg(1, debug, &video->v4l2_dev, "src buf0 addr(%pad) size(%d)\n",
			 &video->srcs[0].dma, video->srcs[0].size);
		v4l2_dbg(1, debug, &video->v4l2_dev, "src buf1 addr(%pad) size(%d)\n",
			 &video->srcs[1].dma, video->srcs[1].size);
		aspeed_video_write(video, VE_SRC0_ADDR, video->srcs[0].dma);
		aspeed_video_write(video, VE_SRC1_ADDR, video->srcs[1].dma);
	}

	return;

err_mem:
	dev_err(video->dev, "Failed to allocate source buffers\n");

	if (video->srcs[0].size)
		aspeed_video_free_buf(video, &video->srcs[0]);
}

/*
 * Update relative parameters when timing changed.
 *
 * @video: the struct of aspeed_video
 * @timings: the new timings
 */
static void aspeed_video_update_timings(struct aspeed_video *video, struct v4l2_bt_timings *timings)
{
	video->active_timings = *timings;
	aspeed_video_set_resolution(video);

	video->pix_fmt.width = timings->width;
	video->pix_fmt.height = timings->height;
	video->pix_fmt.sizeimage = video->max_compressed_size;
}

static void aspeed_video_update_regs(struct aspeed_video *video)
{
	u8 jpeg_hq_quality = clamp((int)video->jpeg_hq_quality - 1, 0,
				   ASPEED_VIDEO_JPEG_NUM_QUALITIES - 1);
	u32 comp_ctrl =	FIELD_PREP(VE_COMP_CTRL_DCT_LUM, video->jpeg_quality) |
		FIELD_PREP(VE_COMP_CTRL_DCT_CHR, video->jpeg_quality | 0x10) |
		FIELD_PREP(VE_COMP_CTRL_EN_HQ, video->hq_mode) |
		FIELD_PREP(VE_COMP_CTRL_HQ_DCT_LUM, jpeg_hq_quality) |
		FIELD_PREP(VE_COMP_CTRL_HQ_DCT_CHR, jpeg_hq_quality | 0x10);
	u32 ctrl = 0;
	u32 seq_ctrl = 0;

	v4l2_dbg(1, debug, &video->v4l2_dev, "input(%s)\n",
		 input_str[video->input]);
	v4l2_dbg(1, debug, &video->v4l2_dev, "framerate(%d)\n",
		 video->frame_rate);
	v4l2_dbg(1, debug, &video->v4l2_dev, "jpeg format(%s) subsample(%s)\n",
		 format_str[video->format],
		 video->yuv420 ? "420" : "444");
	v4l2_dbg(1, debug, &video->v4l2_dev, "compression quality(%d)\n",
		 video->jpeg_quality);
	v4l2_dbg(1, debug, &video->v4l2_dev, "hq_mode(%s) hq_quality(%d)\n",
		 video->hq_mode ? "on" : "off", video->jpeg_hq_quality);

	if (video->format == VIDEO_FMT_ASPEED)
		aspeed_video_update(video, VE_BCD_CTRL, 0, VE_BCD_CTRL_EN_BCD);
	else
		aspeed_video_update(video, VE_BCD_CTRL, VE_BCD_CTRL_EN_BCD, 0);

	if (video->input == VIDEO_INPUT_VGA)
		ctrl |= VE_CTRL_AUTO_OR_CURSOR;

	if (video->frame_rate)
		ctrl |= FIELD_PREP(VE_CTRL_FRC, video->frame_rate);

	if (video->format == VIDEO_FMT_PARTIAL)
		ctrl |= video->compare_only;

	if (video->format == VIDEO_FMT_STANDARD) {
		comp_ctrl &= ~FIELD_PREP(VE_COMP_CTRL_EN_HQ, video->hq_mode);
		seq_ctrl |= video->jpeg_mode;
	}

	if (video->format != VIDEO_FMT_PARTIAL)
		seq_ctrl |= VE_SEQ_CTRL_AUTO_COMP;

	if (video->yuv420)
		seq_ctrl |= VE_SEQ_CTRL_YUV420;

	if (video->jpeg.virt)
		aspeed_video_update_jpeg_table(video->jpeg.virt, video->yuv420);


	/* Set control registers */
	aspeed_video_update(video, VE_SEQ_CTRL,
			    video->jpeg_mode | VE_SEQ_CTRL_YUV420,
			    seq_ctrl);
	aspeed_video_update(video, VE_CTRL, VE_CTRL_FRC | VE_CTRL_AUTO_OR_CURSOR, ctrl);
	aspeed_video_update(video, VE_COMP_CTRL,
			    VE_COMP_CTRL_DCT_LUM | VE_COMP_CTRL_DCT_CHR |
			    VE_COMP_CTRL_EN_HQ | VE_COMP_CTRL_HQ_DCT_LUM |
			    VE_COMP_CTRL_HQ_DCT_CHR | VE_COMP_CTRL_VQ_4COLOR |
			    VE_COMP_CTRL_VQ_DCT_ONLY,
			    comp_ctrl);
}

static void aspeed_video_init_regs(struct aspeed_video *video)
{
	/* Unlock VE registers */
	aspeed_video_write(video, VE_PROTECTION_KEY, VE_PROTECTION_KEY_UNLOCK);

	/* Disable interrupts */
	aspeed_video_write(video, VE_INTERRUPT_CTRL, 0);
	aspeed_video_write(video, VE_INTERRUPT_STATUS, 0xffffffff);

	/* Clear the offset */
	aspeed_video_write(video, VE_COMP_PROC_OFFSET, 0);
	aspeed_video_write(video, VE_COMP_OFFSET, 0);

	aspeed_video_write(video, VE_JPEG_ADDR, video->jpeg.dma);

	/* Set control registers */
	aspeed_video_write(video, VE_SEQ_CTRL, VE_SEQ_CTRL_AUTO_COMP);
	aspeed_video_write(video, VE_CTRL, VE_CTRL_AUTO_OR_CURSOR |
					   FIELD_PREP(VE_CTRL_CAPTURE_FMT, VIDEO_CAP_FMT_YUV_FULL_SWING));
	aspeed_video_write(video, VE_COMP_CTRL, VE_COMP_CTRL_RSVD);

	/* Don't downscale */
	aspeed_video_write(video, VE_SCALING_FACTOR, 0x10001000);
	aspeed_video_write(video, VE_SCALING_FILTER0, 0x00200000);
	aspeed_video_write(video, VE_SCALING_FILTER1, 0x00200000);
	aspeed_video_write(video, VE_SCALING_FILTER2, 0x00200000);
	aspeed_video_write(video, VE_SCALING_FILTER3, 0x00200000);

	/* Set mode detection defaults */
	aspeed_video_write(video, VE_MODE_DETECT,
			   FIELD_PREP(VE_MODE_DT_HOR_TOLER, 2) |
			   FIELD_PREP(VE_MODE_DT_VER_TOLER, 2) |
			   FIELD_PREP(VE_MODE_DT_HOR_STABLE, 6) |
			   FIELD_PREP(VE_MODE_DT_VER_STABLE, 6) |
			   FIELD_PREP(VE_MODE_DT_EDG_THROD, 0x65));

	aspeed_video_write(video, VE_BCD_CTRL, 0);
}

static void aspeed_video_start(struct aspeed_video *video)
{
	aspeed_video_on(video);

	aspeed_video_init_regs(video);

	/* Resolution set to 640x480 if no signal found */
	aspeed_video_get_resolution(video);

	/* Set timings since the device is being opened for the first time */
	aspeed_video_update_timings(video, &video->detected_timings);
}

static void aspeed_video_stop(struct aspeed_video *video)
{
	set_bit(VIDEO_STOPPED, &video->flags);
	cancel_delayed_work_sync(&video->res_work);

	aspeed_video_off(video);

	if (video->srcs[0].size)
		aspeed_video_free_buf(video, &video->srcs[0]);

	if (video->srcs[1].size)
		aspeed_video_free_buf(video, &video->srcs[1]);

	if (video->bcd.size)
		aspeed_video_free_buf(video, &video->bcd);

	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;
	video->flags = 0;
}

static int aspeed_video_querycap(struct file *file, void *fh,
				 struct v4l2_capability *cap)
{
	strscpy(cap->driver, DEVICE_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Aspeed Video Engine", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 DEVICE_NAME);

	return 0;
}

static int aspeed_video_enum_format(struct file *file, void *fh,
				    struct v4l2_fmtdesc *f)
{
	struct aspeed_video *video = video_drvdata(file);

	if (f->index)
		return -EINVAL;

	f->pixelformat = video->pix_fmt.pixelformat;

	return 0;
}

static int aspeed_video_get_format(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct aspeed_video *video = video_drvdata(file);

	f->fmt.pix = video->pix_fmt;

	return 0;
}

static int aspeed_video_set_format(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct aspeed_video *video = video_drvdata(file);

	if (vb2_is_busy(&video->queue))
		return -EBUSY;

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_JPEG:
		video->format = (f->fmt.pix.flags == V4L2_PIX_FMT_FLAG_PARTIAL_JPG)
			      ? VIDEO_FMT_PARTIAL : VIDEO_FMT_STANDARD;
		break;
	case V4L2_PIX_FMT_AJPG:
		video->format = VIDEO_FMT_ASPEED;
		break;
	default:
		return -EINVAL;
	}
	video->pix_fmt.pixelformat = f->fmt.pix.pixelformat;

	return 0;
}

static int aspeed_video_enum_input(struct file *file, void *fh,
				   struct v4l2_input *inp)
{
	struct aspeed_video *video = video_drvdata(file);

	if (inp->index)
		return -EINVAL;

	strscpy(inp->name, "Host VGA capture", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	inp->status = video->v4l2_input_status;

	return 0;
}

static int aspeed_video_get_input(struct file *file, void *fh, unsigned int *i)
{
	struct aspeed_video *video = video_drvdata(file);

	*i = video->input;

	return 0;
}

static int aspeed_video_set_input(struct file *file, void *fh, unsigned int i)
{
	struct aspeed_video *video = video_drvdata(file);

	if (i >= VIDEO_INPUT_MAX)
		return -EINVAL;

	if (IS_ERR(video->scu)) {
		v4l2_dbg(1, debug, &video->v4l2_dev, "%s: scu isn't ready for input-control\n", __func__);
		return -EINVAL;
	}

	// prepare memory space for user to put test batch
	if ((i == VIDEO_INPUT_MEM) && !video->dbg_src.size) {
		if (!aspeed_video_alloc_buf(video, &video->dbg_src, VE_MAX_SRC_BUFFER_SIZE)) {
			v4l2_err(&video->v4l2_dev, "Failed to allocate buffer for debug input\n");
			return -EINVAL;
		}
		v4l2_dbg(1, debug, &video->v4l2_dev, "dbg src addr(%pad) size(%d)\n",
			 &video->dbg_src.dma, video->dbg_src.size);
	}
	if ((i != VIDEO_INPUT_MEM) && video->dbg_src.size)
		aspeed_video_free_buf(video, &video->dbg_src);

	video->input = i;

	// modify dpll source per current input
	if (video->input == VIDEO_INPUT_VGA)
		regmap_update_bits(video->scu, SCU_MISC_CTRL, SCU_DPLL_SOURCE, 0);
	else
		regmap_update_bits(video->scu, SCU_MISC_CTRL, SCU_DPLL_SOURCE, SCU_DPLL_SOURCE);

	// update signal status
	if (i == VIDEO_INPUT_MEM) {
		video->v4l2_input_status = 0;
	} else {
		aspeed_video_get_resolution(video);
		if (!video->v4l2_input_status) {
			aspeed_video_update_timings(video, &video->detected_timings);
		}
	}

	if (video->input == VIDEO_INPUT_MEM)
		aspeed_video_start_frame(video);

	return 0;
}

static int aspeed_video_get_parm(struct file *file, void *fh,
				 struct v4l2_streamparm *a)
{
	struct aspeed_video *video = video_drvdata(file);

	a->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.capture.readbuffers = ASPEED_VIDEO_V4L2_MIN_BUF_REQ;
	a->parm.capture.timeperframe.numerator = 1;
	if (!video->frame_rate)
		a->parm.capture.timeperframe.denominator = MAX_FRAME_RATE;
	else
		a->parm.capture.timeperframe.denominator = video->frame_rate;

	return 0;
}

static int aspeed_video_set_parm(struct file *file, void *fh,
				 struct v4l2_streamparm *a)
{
	unsigned int frame_rate = 0;
	struct aspeed_video *video = video_drvdata(file);

	a->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.capture.readbuffers = ASPEED_VIDEO_V4L2_MIN_BUF_REQ;

	if (a->parm.capture.timeperframe.numerator)
		frame_rate = a->parm.capture.timeperframe.denominator /
			a->parm.capture.timeperframe.numerator;

	if (!frame_rate || frame_rate > MAX_FRAME_RATE) {
		frame_rate = 0;
		a->parm.capture.timeperframe.denominator = MAX_FRAME_RATE;
		a->parm.capture.timeperframe.numerator = 1;
	}

	if (video->frame_rate != frame_rate) {
		video->frame_rate = frame_rate;
		aspeed_video_update(video, VE_CTRL, VE_CTRL_FRC,
				    FIELD_PREP(VE_CTRL_FRC, frame_rate));
	}

	return 0;
}

static int aspeed_video_enum_framesizes(struct file *file, void *fh,
					struct v4l2_frmsizeenum *fsize)
{
	struct aspeed_video *video = video_drvdata(file);

	if (fsize->index)
		return -EINVAL;

	if (fsize->pixel_format != V4L2_PIX_FMT_JPEG)
		return -EINVAL;

	fsize->discrete.width = video->pix_fmt.width;
	fsize->discrete.height = video->pix_fmt.height;
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;

	return 0;
}

static int aspeed_video_enum_frameintervals(struct file *file, void *fh,
					    struct v4l2_frmivalenum *fival)
{
	struct aspeed_video *video = video_drvdata(file);

	if (fival->index)
		return -EINVAL;

	if (fival->width != video->detected_timings.width ||
	    fival->height != video->detected_timings.height)
		return -EINVAL;

	if (fival->pixel_format != V4L2_PIX_FMT_JPEG)
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;

	fival->stepwise.min.denominator = MAX_FRAME_RATE;
	fival->stepwise.min.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.step = fival->stepwise.max;

	return 0;
}

static int aspeed_video_set_dv_timings(struct file *file, void *fh,
				       struct v4l2_dv_timings *timings)
{
	struct aspeed_video *video = video_drvdata(file);

	// if input is MEM, resolution decided by user
	if (video->input == VIDEO_INPUT_MEM) {
		video->detected_timings.width = timings->bt.width;
		video->detected_timings.height = timings->bt.height;
	}

	if (timings->bt.width == video->active_timings.width &&
	    timings->bt.height == video->active_timings.height)
		return 0;

	if (vb2_is_busy(&video->queue))
		return -EBUSY;

	aspeed_video_update_timings(video, &timings->bt);

	timings->type = V4L2_DV_BT_656_1120;

	v4l2_dbg(1, debug, &video->v4l2_dev, "set new timings(%dx%d)\n",
		 timings->bt.width, timings->bt.height);

	return 0;
}

static int aspeed_video_get_dv_timings(struct file *file, void *fh,
				       struct v4l2_dv_timings *timings)
{
	struct aspeed_video *video = video_drvdata(file);

	timings->type = V4L2_DV_BT_656_1120;
	timings->bt = video->active_timings;

	return 0;
}

static int aspeed_video_query_dv_timings(struct file *file, void *fh,
					 struct v4l2_dv_timings *timings)
{
	int rc;
	struct aspeed_video *video = video_drvdata(file);

	/*
	 * This blocks only if the driver is currently in the process of
	 * detecting a new resolution; in the event of no signal or timeout
	 * this function is woken up.
	 */
	if (file->f_flags & O_NONBLOCK) {
		if (test_bit(VIDEO_RES_CHANGE, &video->flags))
			return -EAGAIN;
	} else {
		rc = wait_event_interruptible(video->wait,
					      !test_bit(VIDEO_RES_CHANGE,
							&video->flags));
		if (rc)
			return -EINTR;
	}

	timings->type = V4L2_DV_BT_656_1120;
	timings->bt = video->detected_timings;

	return video->v4l2_input_status ? -ENOLINK : 0;
}

static int aspeed_video_enum_dv_timings(struct file *file, void *fh,
					struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &aspeed_video_timings_cap,
					NULL, NULL);
}

static int aspeed_video_g_selection(struct file *file, void *fh,
				    struct v4l2_selection *s)
{
	struct aspeed_video *video = video_drvdata(file);
	struct aspeed_video_box *box;
	unsigned long flags;

	if ((s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
	    (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT))
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
		spin_lock_irqsave(&video->lock, flags);
		box = list_first_entry_or_null(&video->boxes,
					       struct aspeed_video_box,
					       link);
		if (box) {
			s->r = box->box;
			list_del(&box->link);
			kfree(box);
		} else {
			memset(&s->r, 0, sizeof(s->r));
		}
		spin_unlock_irqrestore(&video->lock, flags);
		return 0;
	}

	return -EINVAL;
}

static int aspeed_video_dv_timings_cap(struct file *file, void *fh,
				       struct v4l2_dv_timings_cap *cap)
{
	*cap = aspeed_video_timings_cap;

	return 0;
}

static int aspeed_video_sub_event(struct v4l2_fh *fh,
				  const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	}

	return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops aspeed_video_ioctl_ops = {
	.vidioc_querycap = aspeed_video_querycap,

	.vidioc_enum_fmt_vid_cap = aspeed_video_enum_format,
	.vidioc_g_fmt_vid_cap = aspeed_video_get_format,
	.vidioc_s_fmt_vid_cap = aspeed_video_set_format,
	.vidioc_try_fmt_vid_cap = aspeed_video_get_format,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,

	.vidioc_enum_input = aspeed_video_enum_input,
	.vidioc_g_input = aspeed_video_get_input,
	.vidioc_s_input = aspeed_video_set_input,

	.vidioc_g_parm = aspeed_video_get_parm,
	.vidioc_s_parm = aspeed_video_set_parm,
	.vidioc_enum_framesizes = aspeed_video_enum_framesizes,
	.vidioc_enum_frameintervals = aspeed_video_enum_frameintervals,

	.vidioc_s_dv_timings = aspeed_video_set_dv_timings,
	.vidioc_g_dv_timings = aspeed_video_get_dv_timings,
	.vidioc_query_dv_timings = aspeed_video_query_dv_timings,
	.vidioc_enum_dv_timings = aspeed_video_enum_dv_timings,
	.vidioc_dv_timings_cap = aspeed_video_dv_timings_cap,

	.vidioc_g_selection = aspeed_video_g_selection,

	.vidioc_subscribe_event = aspeed_video_sub_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int aspeed_video_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct aspeed_video *video = container_of(ctrl->handler,
						  struct aspeed_video,
						  ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		video->jpeg_quality = ctrl->val;
		if (test_bit(VIDEO_STREAMING, &video->flags))
			aspeed_video_update_regs(video);
		break;
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		video->yuv420 = (ctrl->val == V4L2_JPEG_CHROMA_SUBSAMPLING_420);
		if (test_bit(VIDEO_STREAMING, &video->flags))
			aspeed_video_update_regs(video);
		break;
	case V4L2_CID_ASPEED_HQ_MODE:
		video->hq_mode = ctrl->val;
		if (test_bit(VIDEO_STREAMING, &video->flags))
			aspeed_video_update_regs(video);
		break;
	case V4L2_CID_ASPEED_HQ_JPEG_QUALITY:
		video->jpeg_hq_quality = ctrl->val;
		if (test_bit(VIDEO_STREAMING, &video->flags))
			aspeed_video_update_regs(video);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops aspeed_video_ctrl_ops = {
	.s_ctrl = aspeed_video_set_ctrl,
};

static const struct v4l2_ctrl_config aspeed_ctrl_HQ_mode = {
	.ops = &aspeed_video_ctrl_ops,
	.id = V4L2_CID_ASPEED_HQ_MODE,
	.name = "Aspeed HQ Mode",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = false,
	.max = true,
	.step = 1,
	.def = false,
};

static const struct v4l2_ctrl_config aspeed_ctrl_HQ_jpeg_quality = {
	.ops = &aspeed_video_ctrl_ops,
	.id = V4L2_CID_ASPEED_HQ_JPEG_QUALITY,
	.name = "Aspeed HQ Quality",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = ASPEED_VIDEO_JPEG_NUM_QUALITIES,
	.step = 1,
	.def = 1,
};

static void aspeed_video_resolution_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct aspeed_video *video = container_of(dwork, struct aspeed_video,
						  res_work);
	bool is_res_chg = false;

	aspeed_video_on(video);

	/* Exit early in case no clients remain */
	if (test_bit(VIDEO_STOPPED, &video->flags))
		goto done;

	aspeed_video_init_regs(video);

	aspeed_video_update_regs(video);

	aspeed_video_get_resolution(video);

	if (video->v4l2_input_status)
		goto done;

	is_res_chg = (video->detected_timings.width != video->active_timings.width ||
		      video->detected_timings.height != video->active_timings.height);
	aspeed_video_update_timings(video, &video->detected_timings);

	if (is_res_chg) {
		static const struct v4l2_event ev = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};

		v4l2_dbg(1, debug, &video->v4l2_dev, "fire source change event\n");
		v4l2_event_queue(&video->vdev, &ev);
	} else if (test_bit(VIDEO_STREAMING, &video->flags)) {
		/* No resolution change so just restart streaming */
		aspeed_video_start_frame(video);
	}

done:
	clear_bit(VIDEO_RES_CHANGE, &video->flags);
	wake_up_interruptible_all(&video->wait);
}

/*
 * To mmap source memory for test from memory usage.
 * test from memory input mode requires much bigger size because it is
 * uncompressed BGRA format. Thus, We use VM_READ to tell it is for test
 * or v4l2 now.
 */
static int aspeed_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc;
	struct aspeed_video *v = video_drvdata(file);
	const size_t size = vma->vm_end - vma->vm_start;
	const unsigned long pfn = __phys_to_pfn(v->dbg_src.dma);

	if (v->input != VIDEO_INPUT_MEM || vma->vm_flags & VM_READ)
		return vb2_fop_mmap(file, vma);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	rc = remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
	if (rc) {
		v4l2_err(&v->v4l2_dev, "remap_pfn_range failed(%d)\n", rc);
		return -EAGAIN;
	}
	return 0;
}

static int aspeed_video_open(struct file *file)
{
	int rc;
	struct aspeed_video *video = video_drvdata(file);

	mutex_lock(&video->video_lock);

	rc = v4l2_fh_open(file);
	if (rc) {
		mutex_unlock(&video->video_lock);
		return rc;
	}

	if (v4l2_fh_is_singular_file(file))
		aspeed_video_start(video);

	mutex_unlock(&video->video_lock);

	return 0;
}

static int aspeed_video_release(struct file *file)
{
	int rc;
	struct aspeed_video *video = video_drvdata(file);

	mutex_lock(&video->video_lock);

	if (v4l2_fh_is_singular_file(file))
		aspeed_video_stop(video);

	rc = _vb2_fop_release(file, NULL);

	mutex_unlock(&video->video_lock);

	return rc;
}

static const struct v4l2_file_operations aspeed_video_v4l2_fops = {
	.owner = THIS_MODULE,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = aspeed_video_mmap,
	.open = aspeed_video_open,
	.release = aspeed_video_release,
};

static int aspeed_video_queue_setup(struct vb2_queue *q,
				    unsigned int *num_buffers,
				    unsigned int *num_planes,
				    unsigned int sizes[],
				    struct device *alloc_devs[])
{
	struct aspeed_video *video = vb2_get_drv_priv(q);

	if (*num_planes) {
		if (sizes[0] < video->max_compressed_size)
			return -EINVAL;

		return 0;
	}

	*num_planes = 1;
	sizes[0] = video->max_compressed_size;

	return 0;
}

static int aspeed_video_buf_prepare(struct vb2_buffer *vb)
{
	struct aspeed_video *video = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < video->max_compressed_size)
		return -EINVAL;

	return 0;
}

static int aspeed_video_start_streaming(struct vb2_queue *q,
					unsigned int count)
{
	int rc;
	struct aspeed_video *video = vb2_get_drv_priv(q);

	video->sequence = 0;
	video->perf.duration_max = 0;
	video->perf.duration_min = 0xffffffff;
	set_bit(VIDEO_BOUNDING_BOX, &video->flags);

	aspeed_video_update_regs(video);

	// if input is MEM, don't start capture until user acquire
	if (video->input != VIDEO_INPUT_MEM) {
		rc = aspeed_video_start_frame(video);
		if (rc) {
			aspeed_video_bufs_done(video, VB2_BUF_STATE_QUEUED);
			return rc;
		}
	}

	set_bit(VIDEO_STREAMING, &video->flags);
	return 0;
}

static void aspeed_video_stop_streaming(struct vb2_queue *q)
{
	int rc;
	struct aspeed_video *video = vb2_get_drv_priv(q);

	clear_bit(VIDEO_STREAMING, &video->flags);

	rc = wait_event_timeout(video->wait,
				!test_bit(VIDEO_FRAME_INPRG, &video->flags),
				STOP_TIMEOUT);
	if (!rc) {
		v4l2_dbg(1, debug, &video->v4l2_dev, "Timed out when stopping streaming\n");

		/*
		 * Need to force stop any DMA and try and get HW into a good
		 * state for future calls to start streaming again.
		 */
		aspeed_video_reset(video);

		aspeed_video_init_regs(video);

		aspeed_video_get_resolution(video);
	}

	aspeed_video_bufs_done(video, VB2_BUF_STATE_ERROR);
}

static void aspeed_video_buf_queue(struct vb2_buffer *vb)
{
	bool empty;
	struct aspeed_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct aspeed_video_buffer *avb = to_aspeed_video_buffer(vbuf);
	unsigned long flags;

	spin_lock_irqsave(&video->lock, flags);
	empty = list_empty(&video->buffers);
	list_add_tail(&avb->link, &video->buffers);
	spin_unlock_irqrestore(&video->lock, flags);

	if (test_bit(VIDEO_STREAMING, &video->flags) &&
	    !test_bit(VIDEO_FRAME_INPRG, &video->flags) && empty &&
	    (video->input != VIDEO_INPUT_MEM))
		aspeed_video_start_frame(video);
}

static const struct vb2_ops aspeed_video_vb2_ops = {
	.queue_setup = aspeed_video_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_prepare = aspeed_video_buf_prepare,
	.start_streaming = aspeed_video_start_streaming,
	.stop_streaming = aspeed_video_stop_streaming,
	.buf_queue =  aspeed_video_buf_queue,
};

#ifdef CONFIG_DEBUG_FS
static int aspeed_video_debugfs_show(struct seq_file *s, void *data)
{
	struct aspeed_video *v = s->private;
	u32 val08;

	seq_puts(s, "\n");

	seq_puts(s, "Capture:\n");
	val08 = aspeed_video_read(v, VE_CTRL);
	if (FIELD_GET(VE_CTRL_DIRECT_FETCH, val08)) {
		seq_printf(s, "  %-20s:\tDirect fetch\n", "Mode");
		seq_printf(s, "  %-20s:\t%s\n", "Input", input_str[v->input]);
		seq_printf(s, "  %-20s:\t%s\n", "VGA bpp mode",
			   FIELD_GET(VE_CTRL_INT_DE, val08) ? "16" : "32");
	} else {
		seq_printf(s, "  %-20s:\tSync\n", "Mode");
		seq_printf(s, "  %-20s:\t%s\n", "Video source",
			   FIELD_GET(VE_CTRL_SOURCE, val08) ?
			   "external" : "internal");
		seq_printf(s, "  %-20s:\t%s\n", "DE source",
			   FIELD_GET(VE_CTRL_INT_DE, val08) ?
			   "internal" : "external");
		seq_printf(s, "  %-20s:\t%s\n", "Cursor overlay",
			   FIELD_GET(VE_CTRL_AUTO_OR_CURSOR, val08) ?
			   "Without" : "With");
	}

	seq_printf(s, "  %-20s:\t%s\n", "Signal",
		   v->v4l2_input_status ? "Unlock" : "Lock");
	seq_printf(s, "  %-20s:\t%d\n", "Width", v->pix_fmt.width);
	seq_printf(s, "  %-20s:\t%d\n", "Height", v->pix_fmt.height);
	seq_printf(s, "  %-20s:\t%d\n", "FRC", v->frame_rate);

	seq_puts(s, "\n");

	seq_puts(s, "Compression:\n");
	seq_printf(s, "  %-20s:\t%s\n", "Format", format_str[v->format]);
	seq_printf(s, "  %-20s:\t%s\n", "Subsampling",
		   v->yuv420 ? "420" : "444");
	seq_printf(s, "  %-20s:\t%d\n", "Quality", v->jpeg_quality);
	if (v->format == VIDEO_FMT_ASPEED) {
		seq_printf(s, "  %-20s:\t%s\n", "HQ Mode",
			   v->hq_mode ? "on" : "off");
		seq_printf(s, "  %-20s:\t%d\n", "HQ Quality",
			   v->hq_mode ? v->jpeg_hq_quality : 0);
	}

	seq_puts(s, "\n");

	seq_puts(s, "Performance:\n");
	seq_printf(s, "  %-20s:\t%d\n", "Frame#", v->sequence);
	seq_printf(s, "  %-20s:\n", "Frame Duration(ms)");
	seq_printf(s, "    %-18s:\t%d\n", "Now", v->perf.duration);
	seq_printf(s, "    %-18s:\t%d\n", "Min", v->perf.duration_min);
	seq_printf(s, "    %-18s:\t%d\n", "Max", v->perf.duration_max);
	seq_printf(s, "  %-20s:\t%d\n", "FPS",
		   (v->perf.totaltime && v->sequence) ?
		   1000/(v->perf.totaltime/v->sequence) : 0);


	return 0;
}

DEFINE_SHOW_ATTRIBUTE(aspeed_video_debugfs);

static struct dentry *debugfs_entry;

static void aspeed_video_debugfs_remove(struct aspeed_video *video)
{
	debugfs_remove_recursive(debugfs_entry);
}

static void aspeed_video_debugfs_create(struct aspeed_video *video)
{
	debugfs_entry = debugfs_create_file(DEVICE_NAME, 0444, NULL, video,
					    &aspeed_video_debugfs_fops);
}
#else
static void aspeed_video_debugfs_remove(struct aspeed_video *video) { }
static int aspeed_video_debugfs_create(struct aspeed_video *video)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static int aspeed_video_setup_video(struct aspeed_video *video)
{
	const u64 mask = ~(BIT(V4L2_JPEG_CHROMA_SUBSAMPLING_444) |
			   BIT(V4L2_JPEG_CHROMA_SUBSAMPLING_420));
	struct v4l2_device *v4l2_dev = &video->v4l2_dev;
	struct vb2_queue *vbq = &video->queue;
	struct video_device *vdev = &video->vdev;
	struct v4l2_ctrl_handler *hdl = &video->ctrl_handler;
	int rc;

	video->pix_fmt.pixelformat = V4L2_PIX_FMT_JPEG;
	video->pix_fmt.field = V4L2_FIELD_NONE;
	video->pix_fmt.colorspace = V4L2_COLORSPACE_SRGB;
	video->pix_fmt.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	video->v4l2_input_status = V4L2_IN_ST_NO_SIGNAL;

	rc = v4l2_device_register(video->dev, v4l2_dev);
	if (rc) {
		dev_err(video->dev, "Failed to register v4l2 device\n");
		return rc;
	}

	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &aspeed_video_ctrl_ops,
			  V4L2_CID_JPEG_COMPRESSION_QUALITY, 0,
			  ASPEED_VIDEO_JPEG_NUM_QUALITIES - 1, 1, 0);
	v4l2_ctrl_new_std_menu(hdl, &aspeed_video_ctrl_ops,
			       V4L2_CID_JPEG_CHROMA_SUBSAMPLING,
			       V4L2_JPEG_CHROMA_SUBSAMPLING_420, mask,
			       V4L2_JPEG_CHROMA_SUBSAMPLING_444);
	v4l2_ctrl_new_custom(hdl, &aspeed_ctrl_HQ_mode, NULL);
	v4l2_ctrl_new_custom(hdl, &aspeed_ctrl_HQ_jpeg_quality, NULL);

	rc = hdl->error;
	if (rc) {
		dev_err(video->dev, "Failed to init controls: %d\n", rc);
		goto err_ctrl_init;
	}

	v4l2_dev->ctrl_handler = hdl;

	vbq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vbq->io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF;
	vbq->dev = v4l2_dev->dev;
	vbq->lock = &video->video_lock;
	vbq->ops = &aspeed_video_vb2_ops;
	vbq->mem_ops = &vb2_dma_contig_memops;
	vbq->drv_priv = video;
	vbq->buf_struct_size = sizeof(struct aspeed_video_buffer);
	vbq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vbq->min_buffers_needed = ASPEED_VIDEO_V4L2_MIN_BUF_REQ;

	rc = vb2_queue_init(vbq);
	if (rc) {
		dev_err(video->dev, "Failed to init vb2 queue\n");
		goto err_vb2_init;
	}

	vdev->queue = vbq;
	vdev->fops = &aspeed_video_v4l2_fops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING;
	vdev->v4l2_dev = v4l2_dev;
	strscpy(vdev->name, DEVICE_NAME, sizeof(vdev->name));
	vdev->vfl_type = VFL_TYPE_VIDEO;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->release = video_device_release_empty;
	vdev->ioctl_ops = &aspeed_video_ioctl_ops;
	vdev->lock = &video->video_lock;

	video_set_drvdata(vdev, video);
	rc = video_register_device(vdev, VFL_TYPE_VIDEO, 0);
	if (rc) {
		dev_err(video->dev, "Failed to register video device\n");
		goto err_video_reg;
	}

	return 0;

err_video_reg:
err_vb2_init:
err_ctrl_init:
	v4l2_ctrl_handler_free(&video->ctrl_handler);
	v4l2_device_unregister(v4l2_dev);
	return rc;
}

static int aspeed_video_init(struct aspeed_video *video)
{
	int irq;
	int rc;
	struct device *dev = video->dev;

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!irq) {
		dev_err(dev, "Unable to find IRQ\n");
		return -ENODEV;
	}

	rc = devm_request_threaded_irq(dev, irq, aspeed_video_irq,
				       aspeed_video_thread_irq,
				       IRQF_ONESHOT, DEVICE_NAME, video);
	if (rc < 0) {
		dev_err(dev, "Unable to request IRQ %d\n", irq);
		return rc;
	}

	video->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(video->reset)) {
		dev_err(dev, "Unable to get reset\n");
		return PTR_ERR(video->reset);
	}

	video->eclk = devm_clk_get(dev, "eclk");
	if (IS_ERR(video->eclk)) {
		dev_err(dev, "Unable to get ECLK\n");
		return PTR_ERR(video->eclk);
	}

	rc = clk_prepare(video->eclk);
	if (rc)
		return rc;

	video->vclk = devm_clk_get(dev, "vclk");
	if (IS_ERR(video->vclk)) {
		dev_err(dev, "Unable to get VCLK\n");
		rc = PTR_ERR(video->vclk);
		goto err_unprepare_eclk;
	}

	rc = clk_prepare(video->vclk);
	if (rc)
		goto err_unprepare_eclk;

	of_reserved_mem_device_init(dev);

	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "Failed to set DMA mask\n");
		goto err_release_reserved_mem;
	}

	if (!aspeed_video_alloc_buf(video, &video->jpeg,
				    VE_JPEG_HEADER_SIZE)) {
		dev_err(dev, "Failed to allocate DMA for JPEG header\n");
		rc = -ENOMEM;
		goto err_release_reserved_mem;
	}

	aspeed_video_init_jpeg_table(video->jpeg.virt, video->yuv420);

	return 0;

err_release_reserved_mem:
	of_reserved_mem_device_release(dev);
	clk_unprepare(video->vclk);
err_unprepare_eclk:
	clk_unprepare(video->eclk);

	return rc;
}

static const struct of_device_id aspeed_video_of_match[] = {
	{ .compatible = "aspeed,ast2400-video-engine", .data = &ast2400_config },
	{ .compatible = "aspeed,ast2500-video-engine", .data = &ast2500_config },
	{ .compatible = "aspeed,ast2600-video-engine", .data = &ast2600_config },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_video_of_match);

static int aspeed_video_probe(struct platform_device *pdev)
{
	const struct aspeed_video_config *config;
	struct aspeed_video *video;
	int rc;

	video = devm_kzalloc(&pdev->dev, sizeof(*video), GFP_KERNEL);
	if (!video)
		return -ENOMEM;

	video->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(video->base))
		return PTR_ERR(video->base);

	config = of_device_get_match_data(&pdev->dev);
	video->version = config->version;
	video->jpeg_mode = config->jpeg_mode;
	video->comp_size_read = config->comp_size_read;
	video->compare_only = config->compare_only;

	if (video->version == 6) {
		video->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2600-scu");
		video->gfx = syscon_regmap_lookup_by_compatible("aspeed,ast2600-gfx");
		if (IS_ERR(video->scu))
			dev_err(video->dev, "can't find regmap for scu");
		if (IS_ERR(video->gfx))
			dev_err(video->dev, "can't find regmap for gfx");
	} else {
		video->scu = ERR_PTR(-ENODEV);
		video->gfx = ERR_PTR(-ENODEV);
	}

	video->frame_rate = 30;
	video->jpeg_hq_quality = 1;
	video->dev = &pdev->dev;
	spin_lock_init(&video->lock);
	mutex_init(&video->video_lock);
	init_waitqueue_head(&video->wait);
	INIT_DELAYED_WORK(&video->res_work, aspeed_video_resolution_work);
	INIT_LIST_HEAD(&video->buffers);
	INIT_LIST_HEAD(&video->boxes);

	rc = aspeed_video_init(video);
	if (rc)
		return rc;

	rc = aspeed_video_setup_video(video);
	if (rc) {
		aspeed_video_free_buf(video, &video->jpeg);
		clk_unprepare(video->vclk);
		clk_unprepare(video->eclk);
		return rc;
	}

	aspeed_video_debugfs_create(video);

	return 0;
}

static int aspeed_video_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);
	struct aspeed_video *video = to_aspeed_video(v4l2_dev);

	aspeed_video_off(video);

	aspeed_video_debugfs_remove(video);

	clk_unprepare(video->vclk);
	clk_unprepare(video->eclk);

	vb2_video_unregister_device(&video->vdev);

	v4l2_ctrl_handler_free(&video->ctrl_handler);

	v4l2_device_unregister(v4l2_dev);

	aspeed_video_free_buf(video, &video->jpeg);

	of_reserved_mem_device_release(dev);

	return 0;
}

static struct platform_driver aspeed_video_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = aspeed_video_of_match,
	},
	.probe = aspeed_video_probe,
	.remove = aspeed_video_remove,
};

module_platform_driver(aspeed_video_driver);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0=off,1=info,2=debug,3=reg ops)");

MODULE_DESCRIPTION("ASPEED Video Engine Driver");
MODULE_AUTHOR("Eddie James");
MODULE_LICENSE("GPL v2");
