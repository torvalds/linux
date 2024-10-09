// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Allegro DVT video encoder driver
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/gcd.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/xlnx-vcu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "allegro-mail.h"
#include "nal-h264.h"
#include "nal-hevc.h"

/*
 * Support up to 4k video streams. The hardware actually supports higher
 * resolutions, which are specified in PG252 June 6, 2018 (H.264/H.265 Video
 * Codec Unit v1.1) Chapter 3.
 */
#define ALLEGRO_WIDTH_MIN 128
#define ALLEGRO_WIDTH_DEFAULT 1920
#define ALLEGRO_WIDTH_MAX 3840
#define ALLEGRO_HEIGHT_MIN 64
#define ALLEGRO_HEIGHT_DEFAULT 1080
#define ALLEGRO_HEIGHT_MAX 2160

#define ALLEGRO_FRAMERATE_DEFAULT ((struct v4l2_fract) { 30, 1 })

#define ALLEGRO_GOP_SIZE_DEFAULT 25
#define ALLEGRO_GOP_SIZE_MAX 1000

/*
 * MCU Control Registers
 *
 * The Zynq UltraScale+ Devices Register Reference documents the registers
 * with an offset of 0x9000, which equals the size of the SRAM and one page
 * gap. The driver handles SRAM and registers separately and, therefore, is
 * oblivious of the offset.
 */
#define AL5_MCU_RESET                   0x0000
#define AL5_MCU_RESET_SOFT              BIT(0)
#define AL5_MCU_RESET_REGS              BIT(1)
#define AL5_MCU_RESET_MODE              0x0004
#define AL5_MCU_RESET_MODE_SLEEP        BIT(0)
#define AL5_MCU_RESET_MODE_HALT         BIT(1)
#define AL5_MCU_STA                     0x0008
#define AL5_MCU_STA_SLEEP               BIT(0)
#define AL5_MCU_WAKEUP                  0x000c

#define AL5_ICACHE_ADDR_OFFSET_MSB      0x0010
#define AL5_ICACHE_ADDR_OFFSET_LSB      0x0014
#define AL5_DCACHE_ADDR_OFFSET_MSB      0x0018
#define AL5_DCACHE_ADDR_OFFSET_LSB      0x001c

#define AL5_MCU_INTERRUPT               0x0100
#define AL5_ITC_CPU_IRQ_MSK             0x0104
#define AL5_ITC_CPU_IRQ_CLR             0x0108
#define AL5_ITC_CPU_IRQ_STA             0x010C
#define AL5_ITC_CPU_IRQ_STA_TRIGGERED   BIT(0)

#define AXI_ADDR_OFFSET_IP              0x0208

/*
 * The MCU accesses the system memory with a 2G offset compared to CPU
 * physical addresses.
 */
#define MCU_CACHE_OFFSET SZ_2G

/*
 * The driver needs to reserve some space at the beginning of capture buffers,
 * because it needs to write SPS/PPS NAL units. The encoder writes the actual
 * frame data after the offset.
 */
#define ENCODER_STREAM_OFFSET SZ_128

#define SIZE_MACROBLOCK 16

/* Encoding options */
#define LOG2_MAX_FRAME_NUM		4
#define LOG2_MAX_PIC_ORDER_CNT		10
#define BETA_OFFSET_DIV_2		-1
#define TC_OFFSET_DIV_2			-1

/*
 * This control allows applications to explicitly disable the encoder buffer.
 * This value is Allegro specific.
 */
#define V4L2_CID_USER_ALLEGRO_ENCODER_BUFFER (V4L2_CID_USER_ALLEGRO_BASE + 0)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

struct allegro_buffer {
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
	struct list_head head;
};

struct allegro_dev;
struct allegro_channel;

struct allegro_mbox {
	struct allegro_dev *dev;
	unsigned int head;
	unsigned int tail;
	unsigned int data;
	size_t size;
	/* protect mailbox from simultaneous accesses */
	struct mutex lock;
};

struct allegro_encoder_buffer {
	unsigned int size;
	unsigned int color_depth;
	unsigned int num_cores;
	unsigned int clk_rate;
};

struct allegro_dev {
	struct v4l2_device v4l2_dev;
	struct video_device video_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct platform_device *plat_dev;

	/* mutex protecting vb2_queue structure */
	struct mutex lock;

	struct regmap *regmap;
	struct regmap *sram;
	struct regmap *settings;

	struct clk *clk_core;
	struct clk *clk_mcu;

	const struct fw_info *fw_info;
	struct allegro_buffer firmware;
	struct allegro_buffer suballocator;
	bool has_encoder_buffer;
	struct allegro_encoder_buffer encoder_buffer;

	struct completion init_complete;
	bool initialized;

	/* The mailbox interface */
	struct allegro_mbox *mbox_command;
	struct allegro_mbox *mbox_status;

	/*
	 * The downstream driver limits the users to 64 users, thus I can use
	 * a bitfield for the user_ids that are in use. See also user_id in
	 * struct allegro_channel.
	 */
	unsigned long channel_user_ids;
	struct list_head channels;
};

static const struct regmap_config allegro_regmap_config = {
	.name = "regmap",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0xfff,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_config allegro_sram_config = {
	.name = "sram",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x7fff,
	.cache_type = REGCACHE_NONE,
};

#define fh_to_channel(__fh) container_of(__fh, struct allegro_channel, fh)

struct allegro_channel {
	struct allegro_dev *dev;
	struct v4l2_fh fh;
	struct v4l2_ctrl_handler ctrl_handler;

	unsigned int width;
	unsigned int height;
	unsigned int stride;
	struct v4l2_fract framerate;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;

	u32 pixelformat;
	unsigned int sizeimage_raw;
	unsigned int osequence;

	u32 codec;
	unsigned int sizeimage_encoded;
	unsigned int csequence;

	bool frame_rc_enable;
	unsigned int bitrate;
	unsigned int bitrate_peak;

	struct allegro_buffer config_blob;

	unsigned int log2_max_frame_num;
	bool temporal_mvp_enable;

	bool enable_loop_filter_across_tiles;
	bool enable_loop_filter_across_slices;
	bool enable_deblocking_filter_override;
	bool enable_reordering;
	bool dbf_ovr_en;

	unsigned int num_ref_idx_l0;
	unsigned int num_ref_idx_l1;

	/* Maximum range for motion estimation */
	int b_hrz_me_range;
	int b_vrt_me_range;
	int p_hrz_me_range;
	int p_vrt_me_range;
	/* Size limits of coding unit */
	int min_cu_size;
	int max_cu_size;
	/* Size limits of transform unit */
	int min_tu_size;
	int max_tu_size;
	int max_transfo_depth_intra;
	int max_transfo_depth_inter;

	struct v4l2_ctrl *mpeg_video_h264_profile;
	struct v4l2_ctrl *mpeg_video_h264_level;
	struct v4l2_ctrl *mpeg_video_h264_i_frame_qp;
	struct v4l2_ctrl *mpeg_video_h264_max_qp;
	struct v4l2_ctrl *mpeg_video_h264_min_qp;
	struct v4l2_ctrl *mpeg_video_h264_p_frame_qp;
	struct v4l2_ctrl *mpeg_video_h264_b_frame_qp;

	struct v4l2_ctrl *mpeg_video_hevc_profile;
	struct v4l2_ctrl *mpeg_video_hevc_level;
	struct v4l2_ctrl *mpeg_video_hevc_tier;
	struct v4l2_ctrl *mpeg_video_hevc_i_frame_qp;
	struct v4l2_ctrl *mpeg_video_hevc_max_qp;
	struct v4l2_ctrl *mpeg_video_hevc_min_qp;
	struct v4l2_ctrl *mpeg_video_hevc_p_frame_qp;
	struct v4l2_ctrl *mpeg_video_hevc_b_frame_qp;

	struct v4l2_ctrl *mpeg_video_frame_rc_enable;
	struct { /* video bitrate mode control cluster */
		struct v4l2_ctrl *mpeg_video_bitrate_mode;
		struct v4l2_ctrl *mpeg_video_bitrate;
		struct v4l2_ctrl *mpeg_video_bitrate_peak;
	};
	struct v4l2_ctrl *mpeg_video_cpb_size;
	struct v4l2_ctrl *mpeg_video_gop_size;

	struct v4l2_ctrl *encoder_buffer;

	/* user_id is used to identify the channel during CREATE_CHANNEL */
	/* not sure, what to set here and if this is actually required */
	int user_id;
	/* channel_id is set by the mcu and used by all later commands */
	int mcu_channel_id;

	struct list_head buffers_reference;
	struct list_head buffers_intermediate;

	struct list_head source_shadow_list;
	struct list_head stream_shadow_list;
	/* protect shadow lists of buffers passed to firmware */
	struct mutex shadow_list_lock;

	struct list_head list;
	struct completion completion;

	unsigned int error;
};

static inline int
allegro_channel_get_i_frame_qp(struct allegro_channel *channel)
{
	if (channel->codec == V4L2_PIX_FMT_HEVC)
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_i_frame_qp);
	else
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_i_frame_qp);
}

static inline int
allegro_channel_get_p_frame_qp(struct allegro_channel *channel)
{
	if (channel->codec == V4L2_PIX_FMT_HEVC)
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_p_frame_qp);
	else
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_p_frame_qp);
}

static inline int
allegro_channel_get_b_frame_qp(struct allegro_channel *channel)
{
	if (channel->codec == V4L2_PIX_FMT_HEVC)
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_b_frame_qp);
	else
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_b_frame_qp);
}

static inline int
allegro_channel_get_min_qp(struct allegro_channel *channel)
{
	if (channel->codec == V4L2_PIX_FMT_HEVC)
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_min_qp);
	else
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_min_qp);
}

static inline int
allegro_channel_get_max_qp(struct allegro_channel *channel)
{
	if (channel->codec == V4L2_PIX_FMT_HEVC)
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_max_qp);
	else
		return v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_max_qp);
}

struct allegro_m2m_buffer {
	struct v4l2_m2m_buffer buf;
	struct list_head head;
};

#define to_allegro_m2m_buffer(__buf) \
	container_of(__buf, struct allegro_m2m_buffer, buf)

struct fw_info {
	unsigned int id;
	unsigned int id_codec;
	char *version;
	unsigned int mailbox_cmd;
	unsigned int mailbox_status;
	size_t mailbox_size;
	enum mcu_msg_version mailbox_version;
	size_t suballocator_size;
};

static const struct fw_info supported_firmware[] = {
	{
		.id = 18296,
		.id_codec = 96272,
		.version = "v2018.2",
		.mailbox_cmd = 0x7800,
		.mailbox_status = 0x7c00,
		.mailbox_size = 0x400 - 0x8,
		.mailbox_version = MCU_MSG_VERSION_2018_2,
		.suballocator_size = SZ_16M,
	}, {
		.id = 14680,
		.id_codec = 126572,
		.version = "v2019.2",
		.mailbox_cmd = 0x7000,
		.mailbox_status = 0x7800,
		.mailbox_size = 0x800 - 0x8,
		.mailbox_version = MCU_MSG_VERSION_2019_2,
		.suballocator_size = SZ_32M,
	},
};

static inline u32 to_mcu_addr(struct allegro_dev *dev, dma_addr_t phys)
{
	if (upper_32_bits(phys) || (lower_32_bits(phys) & MCU_CACHE_OFFSET))
		v4l2_warn(&dev->v4l2_dev,
			  "address %pad is outside mcu window\n", &phys);

	return lower_32_bits(phys) | MCU_CACHE_OFFSET;
}

static inline u32 to_mcu_size(struct allegro_dev *dev, size_t size)
{
	return lower_32_bits(size);
}

static inline u32 to_codec_addr(struct allegro_dev *dev, dma_addr_t phys)
{
	if (upper_32_bits(phys))
		v4l2_warn(&dev->v4l2_dev,
			  "address %pad cannot be used by codec\n", &phys);

	return lower_32_bits(phys);
}

static inline u64 ptr_to_u64(const void *ptr)
{
	return (uintptr_t)ptr;
}

/* Helper functions for channel and user operations */

static unsigned long allegro_next_user_id(struct allegro_dev *dev)
{
	if (dev->channel_user_ids == ~0UL)
		return -EBUSY;

	return ffz(dev->channel_user_ids);
}

static struct allegro_channel *
allegro_find_channel_by_user_id(struct allegro_dev *dev,
				unsigned int user_id)
{
	struct allegro_channel *channel;

	list_for_each_entry(channel, &dev->channels, list) {
		if (channel->user_id == user_id)
			return channel;
	}

	return ERR_PTR(-EINVAL);
}

static struct allegro_channel *
allegro_find_channel_by_channel_id(struct allegro_dev *dev,
				   unsigned int channel_id)
{
	struct allegro_channel *channel;

	list_for_each_entry(channel, &dev->channels, list) {
		if (channel->mcu_channel_id == channel_id)
			return channel;
	}

	return ERR_PTR(-EINVAL);
}

static inline bool channel_exists(struct allegro_channel *channel)
{
	return channel->mcu_channel_id != -1;
}

#define AL_ERROR			0x80
#define AL_ERR_INIT_FAILED		0x81
#define AL_ERR_NO_FRAME_DECODED		0x82
#define AL_ERR_RESOLUTION_CHANGE	0x85
#define AL_ERR_NO_MEMORY		0x87
#define AL_ERR_STREAM_OVERFLOW		0x88
#define AL_ERR_TOO_MANY_SLICES		0x89
#define AL_ERR_BUF_NOT_READY		0x8c
#define AL_ERR_NO_CHANNEL_AVAILABLE	0x8d
#define AL_ERR_RESOURCE_UNAVAILABLE	0x8e
#define AL_ERR_NOT_ENOUGH_CORES		0x8f
#define AL_ERR_REQUEST_MALFORMED	0x90
#define AL_ERR_CMD_NOT_ALLOWED		0x91
#define AL_ERR_INVALID_CMD_VALUE	0x92

static inline const char *allegro_err_to_string(unsigned int err)
{
	switch (err) {
	case AL_ERR_INIT_FAILED:
		return "initialization failed";
	case AL_ERR_NO_FRAME_DECODED:
		return "no frame decoded";
	case AL_ERR_RESOLUTION_CHANGE:
		return "resolution change";
	case AL_ERR_NO_MEMORY:
		return "out of memory";
	case AL_ERR_STREAM_OVERFLOW:
		return "stream buffer overflow";
	case AL_ERR_TOO_MANY_SLICES:
		return "too many slices";
	case AL_ERR_BUF_NOT_READY:
		return "buffer not ready";
	case AL_ERR_NO_CHANNEL_AVAILABLE:
		return "no channel available";
	case AL_ERR_RESOURCE_UNAVAILABLE:
		return "resource unavailable";
	case AL_ERR_NOT_ENOUGH_CORES:
		return "not enough cores";
	case AL_ERR_REQUEST_MALFORMED:
		return "request malformed";
	case AL_ERR_CMD_NOT_ALLOWED:
		return "command not allowed";
	case AL_ERR_INVALID_CMD_VALUE:
		return "invalid command value";
	case AL_ERROR:
	default:
		return "unknown error";
	}
}

static unsigned int estimate_stream_size(unsigned int width,
					 unsigned int height)
{
	unsigned int offset = ENCODER_STREAM_OFFSET;
	unsigned int num_blocks = DIV_ROUND_UP(width, SIZE_MACROBLOCK) *
					DIV_ROUND_UP(height, SIZE_MACROBLOCK);
	unsigned int pcm_size = SZ_256;
	unsigned int partition_table = SZ_256;

	return round_up(offset + num_blocks * pcm_size + partition_table, 32);
}

static enum v4l2_mpeg_video_h264_level
select_minimum_h264_level(unsigned int width, unsigned int height)
{
	unsigned int pic_width_in_mb = DIV_ROUND_UP(width, SIZE_MACROBLOCK);
	unsigned int frame_height_in_mb = DIV_ROUND_UP(height, SIZE_MACROBLOCK);
	unsigned int frame_size_in_mb = pic_width_in_mb * frame_height_in_mb;
	enum v4l2_mpeg_video_h264_level level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;

	/*
	 * The level limits are specified in Rec. ITU-T H.264 Annex A.3.1 and
	 * also specify limits regarding bit rate and CBP size. Only approximate
	 * the levels using the frame size.
	 *
	 * Level 5.1 allows up to 4k video resolution.
	 */
	if (frame_size_in_mb <= 99)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
	else if (frame_size_in_mb <= 396)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
	else if (frame_size_in_mb <= 792)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
	else if (frame_size_in_mb <= 1620)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
	else if (frame_size_in_mb <= 3600)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
	else if (frame_size_in_mb <= 5120)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
	else if (frame_size_in_mb <= 8192)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
	else if (frame_size_in_mb <= 8704)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
	else if (frame_size_in_mb <= 22080)
		level = V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
	else
		level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;

	return level;
}

static unsigned int h264_maximum_bitrate(enum v4l2_mpeg_video_h264_level level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
		return 64000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
		return 128000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
		return 192000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
		return 384000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
		return 768000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
		return 2000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
		return 4000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
		return 4000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
		return 10000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
		return 14000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
		return 20000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
		return 20000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
		return 50000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
		return 50000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
		return 135000000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
	default:
		return 240000000;
	}
}

static unsigned int h264_maximum_cpb_size(enum v4l2_mpeg_video_h264_level level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
		return 175;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
		return 350;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
		return 500;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
		return 1000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
		return 2000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
		return 2000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
		return 4000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
		return 4000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
		return 10000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
		return 14000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
		return 20000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
		return 25000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
		return 62500;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
		return 62500;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
		return 135000;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
	default:
		return 240000;
	}
}

static enum v4l2_mpeg_video_hevc_level
select_minimum_hevc_level(unsigned int width, unsigned int height)
{
	unsigned int luma_picture_size = width * height;
	enum v4l2_mpeg_video_hevc_level level;

	if (luma_picture_size <= 36864)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_1;
	else if (luma_picture_size <= 122880)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_2;
	else if (luma_picture_size <= 245760)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1;
	else if (luma_picture_size <= 552960)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_3;
	else if (luma_picture_size <= 983040)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1;
	else if (luma_picture_size <= 2228224)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_4;
	else if (luma_picture_size <= 8912896)
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_5;
	else
		level = V4L2_MPEG_VIDEO_HEVC_LEVEL_6;

	return level;
}

static unsigned int hevc_maximum_bitrate(enum v4l2_mpeg_video_hevc_level level)
{
	/*
	 * See Rec. ITU-T H.265 v5 (02/2018), A.4.2 Profile-specific level
	 * limits for the video profiles.
	 */
	switch (level) {
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_1:
		return 128;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2:
		return 1500;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1:
		return 3000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3:
		return 6000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1:
		return 10000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4:
		return 12000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1:
		return 20000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5:
		return 25000;
	default:
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1:
		return 40000;
	}
}

static unsigned int hevc_maximum_cpb_size(enum v4l2_mpeg_video_hevc_level level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_1:
		return 350;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2:
		return 1500;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1:
		return 3000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3:
		return 6000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1:
		return 10000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4:
		return 12000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1:
		return 20000;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5:
		return 25000;
	default:
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1:
		return 40000;
	}
}

static const struct fw_info *
allegro_get_firmware_info(struct allegro_dev *dev,
			  const struct firmware *fw,
			  const struct firmware *fw_codec)
{
	int i;
	unsigned int id = fw->size;
	unsigned int id_codec = fw_codec->size;

	for (i = 0; i < ARRAY_SIZE(supported_firmware); i++)
		if (supported_firmware[i].id == id &&
		    supported_firmware[i].id_codec == id_codec)
			return &supported_firmware[i];

	return NULL;
}

/*
 * Buffers that are used internally by the MCU.
 */

static int allegro_alloc_buffer(struct allegro_dev *dev,
				struct allegro_buffer *buffer, size_t size)
{
	buffer->vaddr = dma_alloc_coherent(&dev->plat_dev->dev, size,
					   &buffer->paddr, GFP_KERNEL);
	if (!buffer->vaddr)
		return -ENOMEM;
	buffer->size = size;

	return 0;
}

static void allegro_free_buffer(struct allegro_dev *dev,
				struct allegro_buffer *buffer)
{
	if (buffer->vaddr) {
		dma_free_coherent(&dev->plat_dev->dev, buffer->size,
				  buffer->vaddr, buffer->paddr);
		buffer->vaddr = NULL;
		buffer->size = 0;
	}
}

/*
 * Mailbox interface to send messages to the MCU.
 */

static void allegro_mcu_interrupt(struct allegro_dev *dev);
static void allegro_handle_message(struct allegro_dev *dev,
				   union mcu_msg_response *msg);

static struct allegro_mbox *allegro_mbox_init(struct allegro_dev *dev,
					      unsigned int base, size_t size)
{
	struct allegro_mbox *mbox;

	mbox = devm_kmalloc(&dev->plat_dev->dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return ERR_PTR(-ENOMEM);

	mbox->dev = dev;

	mbox->head = base;
	mbox->tail = base + 0x4;
	mbox->data = base + 0x8;
	mbox->size = size;
	mutex_init(&mbox->lock);

	regmap_write(dev->sram, mbox->head, 0);
	regmap_write(dev->sram, mbox->tail, 0);

	return mbox;
}

static int allegro_mbox_write(struct allegro_mbox *mbox,
			      const u32 *src, size_t size)
{
	struct regmap *sram = mbox->dev->sram;
	unsigned int tail;
	size_t size_no_wrap;
	int err = 0;
	int stride = regmap_get_reg_stride(sram);

	if (!src)
		return -EINVAL;

	if (size > mbox->size)
		return -EINVAL;

	mutex_lock(&mbox->lock);
	regmap_read(sram, mbox->tail, &tail);
	if (tail > mbox->size) {
		err = -EIO;
		goto out;
	}
	size_no_wrap = min(size, mbox->size - (size_t)tail);
	regmap_bulk_write(sram, mbox->data + tail,
			  src, size_no_wrap / stride);
	regmap_bulk_write(sram, mbox->data,
			  src + (size_no_wrap / sizeof(*src)),
			  (size - size_no_wrap) / stride);
	regmap_write(sram, mbox->tail, (tail + size) % mbox->size);

out:
	mutex_unlock(&mbox->lock);

	return err;
}

static ssize_t allegro_mbox_read(struct allegro_mbox *mbox,
				 u32 *dst, size_t nbyte)
{
	struct {
		u16 length;
		u16 type;
	} __attribute__ ((__packed__)) *header;
	struct regmap *sram = mbox->dev->sram;
	unsigned int head;
	ssize_t size;
	size_t body_no_wrap;
	int stride = regmap_get_reg_stride(sram);

	regmap_read(sram, mbox->head, &head);
	if (head > mbox->size)
		return -EIO;

	/* Assume that the header does not wrap. */
	regmap_bulk_read(sram, mbox->data + head,
			 dst, sizeof(*header) / stride);
	header = (void *)dst;
	size = header->length + sizeof(*header);
	if (size > mbox->size || size & 0x3)
		return -EIO;
	if (size > nbyte)
		return -EINVAL;

	/*
	 * The message might wrap within the mailbox. If the message does not
	 * wrap, the first read will read the entire message, otherwise the
	 * first read will read message until the end of the mailbox and the
	 * second read will read the remaining bytes from the beginning of the
	 * mailbox.
	 *
	 * Skip the header, as was already read to get the size of the body.
	 */
	body_no_wrap = min((size_t)header->length,
			   (size_t)(mbox->size - (head + sizeof(*header))));
	regmap_bulk_read(sram, mbox->data + head + sizeof(*header),
			 dst + (sizeof(*header) / sizeof(*dst)),
			 body_no_wrap / stride);
	regmap_bulk_read(sram, mbox->data,
			 dst + (sizeof(*header) + body_no_wrap) / sizeof(*dst),
			 (header->length - body_no_wrap) / stride);

	regmap_write(sram, mbox->head, (head + size) % mbox->size);

	return size;
}

/**
 * allegro_mbox_send() - Send a message via the mailbox
 * @mbox: the mailbox which is used to send the message
 * @msg: the message to send
 */
static int allegro_mbox_send(struct allegro_mbox *mbox, void *msg)
{
	struct allegro_dev *dev = mbox->dev;
	ssize_t size;
	int err;
	u32 *tmp;

	tmp = kzalloc(mbox->size, GFP_KERNEL);
	if (!tmp) {
		err = -ENOMEM;
		goto out;
	}

	size = allegro_encode_mail(tmp, msg);

	err = allegro_mbox_write(mbox, tmp, size);
	kfree(tmp);
	if (err)
		goto out;

	allegro_mcu_interrupt(dev);

out:
	return err;
}

/**
 * allegro_mbox_notify() - Notify the mailbox about a new message
 * @mbox: The allegro_mbox to notify
 */
static void allegro_mbox_notify(struct allegro_mbox *mbox)
{
	struct allegro_dev *dev = mbox->dev;
	union mcu_msg_response *msg;
	ssize_t size;
	u32 *tmp;
	int err;

	msg = kmalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return;

	msg->header.version = dev->fw_info->mailbox_version;

	tmp = kmalloc(mbox->size, GFP_KERNEL);
	if (!tmp)
		goto out;

	size = allegro_mbox_read(mbox, tmp, mbox->size);
	if (size < 0)
		goto out;

	err = allegro_decode_mail(msg, tmp);
	if (err)
		goto out;

	allegro_handle_message(dev, msg);

out:
	kfree(tmp);
	kfree(msg);
}

static int allegro_encoder_buffer_init(struct allegro_dev *dev,
				       struct allegro_encoder_buffer *buffer)
{
	int err;
	struct regmap *settings = dev->settings;
	unsigned int supports_10_bit;
	unsigned int memory_depth;
	unsigned int num_cores;
	unsigned int color_depth;
	unsigned long clk_rate;

	/* We don't support the encoder buffer pre Firmware version 2019.2 */
	if (dev->fw_info->mailbox_version < MCU_MSG_VERSION_2019_2)
		return -ENODEV;

	if (!settings)
		return -EINVAL;

	err = regmap_read(settings, VCU_ENC_COLOR_DEPTH, &supports_10_bit);
	if (err < 0)
		return err;
	err = regmap_read(settings, VCU_MEMORY_DEPTH, &memory_depth);
	if (err < 0)
		return err;
	err = regmap_read(settings, VCU_NUM_CORE, &num_cores);
	if (err < 0)
		return err;

	clk_rate = clk_get_rate(dev->clk_core);
	if (clk_rate == 0)
		return -EINVAL;

	color_depth = supports_10_bit ? 10 : 8;
	/* The firmware expects the encoder buffer size in bits. */
	buffer->size = color_depth * 32 * memory_depth;
	buffer->color_depth = color_depth;
	buffer->num_cores = num_cores;
	buffer->clk_rate = clk_rate;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "using %d bits encoder buffer with %d-bit color depth\n",
		 buffer->size, color_depth);

	return 0;
}

static void allegro_mcu_send_init(struct allegro_dev *dev,
				  dma_addr_t suballoc_dma, size_t suballoc_size)
{
	struct mcu_msg_init_request msg;

	memset(&msg, 0, sizeof(msg));

	msg.header.type = MCU_MSG_TYPE_INIT;
	msg.header.version = dev->fw_info->mailbox_version;

	msg.suballoc_dma = to_mcu_addr(dev, suballoc_dma);
	msg.suballoc_size = to_mcu_size(dev, suballoc_size);

	if (dev->has_encoder_buffer) {
		msg.encoder_buffer_size = dev->encoder_buffer.size;
		msg.encoder_buffer_color_depth = dev->encoder_buffer.color_depth;
		msg.num_cores = dev->encoder_buffer.num_cores;
		msg.clk_rate = dev->encoder_buffer.clk_rate;
	} else {
		msg.encoder_buffer_size = -1;
		msg.encoder_buffer_color_depth = -1;
		msg.num_cores = -1;
		msg.clk_rate = -1;
	}

	allegro_mbox_send(dev->mbox_command, &msg);
}

static u32 v4l2_pixelformat_to_mcu_format(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_NV12:
		/* AL_420_8BITS: 0x100 -> NV12, 0x88 -> 8 bit */
		return 0x100 | 0x88;
	default:
		return -EINVAL;
	}
}

static u32 v4l2_colorspace_to_mcu_colorspace(enum v4l2_colorspace colorspace)
{
	switch (colorspace) {
	case V4L2_COLORSPACE_REC709:
		return 2;
	case V4L2_COLORSPACE_SMPTE170M:
		return 3;
	case V4L2_COLORSPACE_SMPTE240M:
		return 4;
	case V4L2_COLORSPACE_SRGB:
		return 7;
	default:
		/* UNKNOWN */
		return 0;
	}
}

static u8 v4l2_profile_to_mcu_profile(enum v4l2_mpeg_video_h264_profile profile)
{
	switch (profile) {
	case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
	default:
		return 66;
	}
}

static u16 v4l2_level_to_mcu_level(enum v4l2_mpeg_video_h264_level level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
		return 10;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
		return 11;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
		return 12;
	case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
		return 13;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
		return 20;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
		return 21;
	case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
		return 22;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
		return 30;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
		return 31;
	case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
		return 32;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
		return 40;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
		return 41;
	case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
		return 42;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
		return 50;
	case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
	default:
		return 51;
	}
}

static u8 hevc_profile_to_mcu_profile(enum v4l2_mpeg_video_hevc_profile profile)
{
	switch (profile) {
	default:
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN:
		return 1;
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10:
		return 2;
	case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE:
		return 3;
	}
}

static u16 hevc_level_to_mcu_level(enum v4l2_mpeg_video_hevc_level level)
{
	switch (level) {
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_1:
		return 10;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2:
		return 20;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1:
		return 21;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3:
		return 30;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1:
		return 31;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4:
		return 40;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1:
		return 41;
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5:
		return 50;
	default:
	case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1:
		return 51;
	}
}

static u8 hevc_tier_to_mcu_tier(enum v4l2_mpeg_video_hevc_tier tier)
{
	switch (tier) {
	default:
	case V4L2_MPEG_VIDEO_HEVC_TIER_MAIN:
		return 0;
	case V4L2_MPEG_VIDEO_HEVC_TIER_HIGH:
		return 1;
	}
}

static u32
v4l2_bitrate_mode_to_mcu_mode(enum v4l2_mpeg_video_bitrate_mode mode)
{
	switch (mode) {
	case V4L2_MPEG_VIDEO_BITRATE_MODE_VBR:
		return 2;
	case V4L2_MPEG_VIDEO_BITRATE_MODE_CBR:
	default:
		return 1;
	}
}

static u32 v4l2_cpb_size_to_mcu(unsigned int cpb_size, unsigned int bitrate)
{
	unsigned int cpb_size_kbit;
	unsigned int bitrate_kbps;

	/*
	 * The mcu expects the CPB size in units of a 90 kHz clock, but the
	 * channel follows the V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE and stores
	 * the CPB size in kilobytes.
	 */
	cpb_size_kbit = cpb_size * BITS_PER_BYTE;
	bitrate_kbps = bitrate / 1000;

	return (cpb_size_kbit * 90000) / bitrate_kbps;
}

static s16 get_qp_delta(int minuend, int subtrahend)
{
	if (minuend == subtrahend)
		return -1;
	else
		return minuend - subtrahend;
}

static u32 allegro_channel_get_entropy_mode(struct allegro_channel *channel)
{
#define ALLEGRO_ENTROPY_MODE_CAVLC 0
#define ALLEGRO_ENTROPY_MODE_CABAC 1

	/* HEVC always uses CABAC, but this has to be explicitly set */
	if (channel->codec == V4L2_PIX_FMT_HEVC)
		return ALLEGRO_ENTROPY_MODE_CABAC;

	return ALLEGRO_ENTROPY_MODE_CAVLC;
}

static int fill_create_channel_param(struct allegro_channel *channel,
				     struct create_channel_param *param)
{
	int i_frame_qp = allegro_channel_get_i_frame_qp(channel);
	int p_frame_qp = allegro_channel_get_p_frame_qp(channel);
	int b_frame_qp = allegro_channel_get_b_frame_qp(channel);
	int bitrate_mode = v4l2_ctrl_g_ctrl(channel->mpeg_video_bitrate_mode);
	unsigned int cpb_size = v4l2_ctrl_g_ctrl(channel->mpeg_video_cpb_size);

	param->width = channel->width;
	param->height = channel->height;
	param->format = v4l2_pixelformat_to_mcu_format(channel->pixelformat);
	param->colorspace =
		v4l2_colorspace_to_mcu_colorspace(channel->colorspace);
	param->src_mode = 0x0;

	param->codec = channel->codec;
	if (channel->codec == V4L2_PIX_FMT_H264) {
		enum v4l2_mpeg_video_h264_profile profile;
		enum v4l2_mpeg_video_h264_level level;

		profile = v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_profile);
		level = v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_level);

		param->profile = v4l2_profile_to_mcu_profile(profile);
		param->constraint_set_flags = BIT(1);
		param->level = v4l2_level_to_mcu_level(level);
	} else {
		enum v4l2_mpeg_video_hevc_profile profile;
		enum v4l2_mpeg_video_hevc_level level;
		enum v4l2_mpeg_video_hevc_tier tier;

		profile = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_profile);
		level = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_level);
		tier = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_tier);

		param->profile = hevc_profile_to_mcu_profile(profile);
		param->level = hevc_level_to_mcu_level(level);
		param->tier = hevc_tier_to_mcu_tier(tier);
	}

	param->log2_max_poc = LOG2_MAX_PIC_ORDER_CNT;
	param->log2_max_frame_num = channel->log2_max_frame_num;
	param->temporal_mvp_enable = channel->temporal_mvp_enable;

	param->dbf_ovr_en = channel->dbf_ovr_en;
	param->override_lf = channel->enable_deblocking_filter_override;
	param->enable_reordering = channel->enable_reordering;
	param->entropy_mode = allegro_channel_get_entropy_mode(channel);
	param->rdo_cost_mode = 1;
	param->custom_lda = 1;
	param->lf = 1;
	param->lf_x_tile = channel->enable_loop_filter_across_tiles;
	param->lf_x_slice = channel->enable_loop_filter_across_slices;

	param->src_bit_depth = 8;

	param->beta_offset = BETA_OFFSET_DIV_2;
	param->tc_offset = TC_OFFSET_DIV_2;
	param->num_slices = 1;
	param->me_range[0] = channel->b_hrz_me_range;
	param->me_range[1] = channel->b_vrt_me_range;
	param->me_range[2] = channel->p_hrz_me_range;
	param->me_range[3] = channel->p_vrt_me_range;
	param->max_cu_size = channel->max_cu_size;
	param->min_cu_size = channel->min_cu_size;
	param->max_tu_size = channel->max_tu_size;
	param->min_tu_size = channel->min_tu_size;
	param->max_transfo_depth_intra = channel->max_transfo_depth_intra;
	param->max_transfo_depth_inter = channel->max_transfo_depth_inter;

	param->encoder_buffer_enabled = v4l2_ctrl_g_ctrl(channel->encoder_buffer);
	param->encoder_buffer_offset = 0;

	param->rate_control_mode = channel->frame_rc_enable ?
		v4l2_bitrate_mode_to_mcu_mode(bitrate_mode) : 0;

	param->cpb_size = v4l2_cpb_size_to_mcu(cpb_size, channel->bitrate_peak);
	/* Shall be ]0;cpb_size in 90 kHz units]. Use maximum value. */
	param->initial_rem_delay = param->cpb_size;
	param->framerate = DIV_ROUND_UP(channel->framerate.numerator,
					channel->framerate.denominator);
	param->clk_ratio = channel->framerate.denominator == 1001 ? 1001 : 1000;
	param->target_bitrate = channel->bitrate;
	param->max_bitrate = channel->bitrate_peak;
	param->initial_qp = i_frame_qp;
	param->min_qp = allegro_channel_get_min_qp(channel);
	param->max_qp = allegro_channel_get_max_qp(channel);
	param->ip_delta = get_qp_delta(i_frame_qp, p_frame_qp);
	param->pb_delta = get_qp_delta(p_frame_qp, b_frame_qp);
	param->golden_ref = 0;
	param->golden_delta = 2;
	param->golden_ref_frequency = 10;
	param->rate_control_option = 0x00000000;

	param->num_pixel = channel->width + channel->height;
	param->max_psnr = 4200;
	param->max_pixel_value = 255;

	param->gop_ctrl_mode = 0x00000002;
	param->freq_idr = v4l2_ctrl_g_ctrl(channel->mpeg_video_gop_size);
	param->freq_lt = 0;
	param->gdr_mode = 0x00000000;
	param->gop_length = v4l2_ctrl_g_ctrl(channel->mpeg_video_gop_size);
	param->subframe_latency = 0x00000000;

	param->lda_factors[0] = 51;
	param->lda_factors[1] = 90;
	param->lda_factors[2] = 151;
	param->lda_factors[3] = 151;
	param->lda_factors[4] = 151;
	param->lda_factors[5] = 151;

	param->max_num_merge_cand = 5;

	return 0;
}

static int allegro_mcu_send_create_channel(struct allegro_dev *dev,
					   struct allegro_channel *channel)
{
	struct mcu_msg_create_channel msg;
	struct allegro_buffer *blob = &channel->config_blob;
	struct create_channel_param param;
	size_t size;

	memset(&param, 0, sizeof(param));
	fill_create_channel_param(channel, &param);
	allegro_alloc_buffer(dev, blob, sizeof(struct create_channel_param));
	param.version = dev->fw_info->mailbox_version;
	size = allegro_encode_config_blob(blob->vaddr, &param);

	memset(&msg, 0, sizeof(msg));

	msg.header.type = MCU_MSG_TYPE_CREATE_CHANNEL;
	msg.header.version = dev->fw_info->mailbox_version;

	msg.user_id = channel->user_id;

	msg.blob = blob->vaddr;
	msg.blob_size = size;
	msg.blob_mcu_addr = to_mcu_addr(dev, blob->paddr);

	allegro_mbox_send(dev->mbox_command, &msg);

	return 0;
}

static int allegro_mcu_send_destroy_channel(struct allegro_dev *dev,
					    struct allegro_channel *channel)
{
	struct mcu_msg_destroy_channel msg;

	memset(&msg, 0, sizeof(msg));

	msg.header.type = MCU_MSG_TYPE_DESTROY_CHANNEL;
	msg.header.version = dev->fw_info->mailbox_version;

	msg.channel_id = channel->mcu_channel_id;

	allegro_mbox_send(dev->mbox_command, &msg);

	return 0;
}

static int allegro_mcu_send_put_stream_buffer(struct allegro_dev *dev,
					      struct allegro_channel *channel,
					      dma_addr_t paddr,
					      unsigned long size,
					      u64 dst_handle)
{
	struct mcu_msg_put_stream_buffer msg;

	memset(&msg, 0, sizeof(msg));

	msg.header.type = MCU_MSG_TYPE_PUT_STREAM_BUFFER;
	msg.header.version = dev->fw_info->mailbox_version;

	msg.channel_id = channel->mcu_channel_id;
	msg.dma_addr = to_codec_addr(dev, paddr);
	msg.mcu_addr = to_mcu_addr(dev, paddr);
	msg.size = size;
	msg.offset = ENCODER_STREAM_OFFSET;
	/* copied to mcu_msg_encode_frame_response */
	msg.dst_handle = dst_handle;

	allegro_mbox_send(dev->mbox_command, &msg);

	return 0;
}

static int allegro_mcu_send_encode_frame(struct allegro_dev *dev,
					 struct allegro_channel *channel,
					 dma_addr_t src_y, dma_addr_t src_uv,
					 u64 src_handle)
{
	struct mcu_msg_encode_frame msg;
	bool use_encoder_buffer = v4l2_ctrl_g_ctrl(channel->encoder_buffer);

	memset(&msg, 0, sizeof(msg));

	msg.header.type = MCU_MSG_TYPE_ENCODE_FRAME;
	msg.header.version = dev->fw_info->mailbox_version;

	msg.channel_id = channel->mcu_channel_id;
	msg.encoding_options = AL_OPT_FORCE_LOAD;
	if (use_encoder_buffer)
		msg.encoding_options |= AL_OPT_USE_L2;
	msg.pps_qp = 26; /* qp are relative to 26 */
	msg.user_param = 0; /* copied to mcu_msg_encode_frame_response */
	/* src_handle is copied to mcu_msg_encode_frame_response */
	msg.src_handle = src_handle;
	msg.src_y = to_codec_addr(dev, src_y);
	msg.src_uv = to_codec_addr(dev, src_uv);
	msg.stride = channel->stride;

	allegro_mbox_send(dev->mbox_command, &msg);

	return 0;
}

static int allegro_mcu_wait_for_init_timeout(struct allegro_dev *dev,
					     unsigned long timeout_ms)
{
	unsigned long time_left;

	time_left = wait_for_completion_timeout(&dev->init_complete,
						msecs_to_jiffies(timeout_ms));
	if (time_left == 0)
		return -ETIMEDOUT;

	reinit_completion(&dev->init_complete);
	return 0;
}

static int allegro_mcu_push_buffer_internal(struct allegro_channel *channel,
					    enum mcu_msg_type type)
{
	struct allegro_dev *dev = channel->dev;
	struct mcu_msg_push_buffers_internal *msg;
	struct mcu_msg_push_buffers_internal_buffer *buffer;
	unsigned int num_buffers = 0;
	size_t size;
	struct allegro_buffer *al_buffer;
	struct list_head *list;
	int err;

	switch (type) {
	case MCU_MSG_TYPE_PUSH_BUFFER_REFERENCE:
		list = &channel->buffers_reference;
		break;
	case MCU_MSG_TYPE_PUSH_BUFFER_INTERMEDIATE:
		list = &channel->buffers_intermediate;
		break;
	default:
		return -EINVAL;
	}

	list_for_each_entry(al_buffer, list, head)
		num_buffers++;
	size = struct_size(msg, buffer, num_buffers);

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->header.type = type;
	msg->header.version = dev->fw_info->mailbox_version;

	msg->channel_id = channel->mcu_channel_id;
	msg->num_buffers = num_buffers;

	buffer = msg->buffer;
	list_for_each_entry(al_buffer, list, head) {
		buffer->dma_addr = to_codec_addr(dev, al_buffer->paddr);
		buffer->mcu_addr = to_mcu_addr(dev, al_buffer->paddr);
		buffer->size = to_mcu_size(dev, al_buffer->size);
		buffer++;
	}

	err = allegro_mbox_send(dev->mbox_command, msg);

	kfree(msg);
	return err;
}

static int allegro_mcu_push_buffer_intermediate(struct allegro_channel *channel)
{
	enum mcu_msg_type type = MCU_MSG_TYPE_PUSH_BUFFER_INTERMEDIATE;

	return allegro_mcu_push_buffer_internal(channel, type);
}

static int allegro_mcu_push_buffer_reference(struct allegro_channel *channel)
{
	enum mcu_msg_type type = MCU_MSG_TYPE_PUSH_BUFFER_REFERENCE;

	return allegro_mcu_push_buffer_internal(channel, type);
}

static int allocate_buffers_internal(struct allegro_channel *channel,
				     struct list_head *list,
				     size_t n, size_t size)
{
	struct allegro_dev *dev = channel->dev;
	unsigned int i;
	int err;
	struct allegro_buffer *buffer, *tmp;

	for (i = 0; i < n; i++) {
		buffer = kmalloc(sizeof(*buffer), GFP_KERNEL);
		if (!buffer) {
			err = -ENOMEM;
			goto err;
		}
		INIT_LIST_HEAD(&buffer->head);

		err = allegro_alloc_buffer(dev, buffer, size);
		if (err) {
			kfree(buffer);
			goto err;
		}
		list_add(&buffer->head, list);
	}

	return 0;

err:
	list_for_each_entry_safe(buffer, tmp, list, head) {
		list_del(&buffer->head);
		allegro_free_buffer(dev, buffer);
		kfree(buffer);
	}
	return err;
}

static void destroy_buffers_internal(struct allegro_channel *channel,
				     struct list_head *list)
{
	struct allegro_dev *dev = channel->dev;
	struct allegro_buffer *buffer, *tmp;

	list_for_each_entry_safe(buffer, tmp, list, head) {
		list_del(&buffer->head);
		allegro_free_buffer(dev, buffer);
		kfree(buffer);
	}
}

static void destroy_reference_buffers(struct allegro_channel *channel)
{
	return destroy_buffers_internal(channel, &channel->buffers_reference);
}

static void destroy_intermediate_buffers(struct allegro_channel *channel)
{
	return destroy_buffers_internal(channel,
					&channel->buffers_intermediate);
}

static int allocate_intermediate_buffers(struct allegro_channel *channel,
					 size_t n, size_t size)
{
	return allocate_buffers_internal(channel,
					 &channel->buffers_intermediate,
					 n, size);
}

static int allocate_reference_buffers(struct allegro_channel *channel,
				      size_t n, size_t size)
{
	return allocate_buffers_internal(channel,
					 &channel->buffers_reference,
					 n, PAGE_ALIGN(size));
}

static ssize_t allegro_h264_write_sps(struct allegro_channel *channel,
				      void *dest, size_t n)
{
	struct allegro_dev *dev = channel->dev;
	struct nal_h264_sps *sps;
	ssize_t size;
	unsigned int size_mb = SIZE_MACROBLOCK;
	/* Calculation of crop units in Rec. ITU-T H.264 (04/2017) p. 76 */
	unsigned int crop_unit_x = 2;
	unsigned int crop_unit_y = 2;
	enum v4l2_mpeg_video_h264_profile profile;
	enum v4l2_mpeg_video_h264_level level;
	unsigned int cpb_size;
	unsigned int cpb_size_scale;

	sps = kzalloc(sizeof(*sps), GFP_KERNEL);
	if (!sps)
		return -ENOMEM;

	profile = v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_profile);
	level = v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_level);

	sps->profile_idc = nal_h264_profile(profile);
	sps->constraint_set0_flag = 0;
	sps->constraint_set1_flag = 1;
	sps->constraint_set2_flag = 0;
	sps->constraint_set3_flag = 0;
	sps->constraint_set4_flag = 0;
	sps->constraint_set5_flag = 0;
	sps->level_idc = nal_h264_level(level);
	sps->seq_parameter_set_id = 0;
	sps->log2_max_frame_num_minus4 = LOG2_MAX_FRAME_NUM - 4;
	sps->pic_order_cnt_type = 0;
	sps->log2_max_pic_order_cnt_lsb_minus4 = LOG2_MAX_PIC_ORDER_CNT - 4;
	sps->max_num_ref_frames = 3;
	sps->gaps_in_frame_num_value_allowed_flag = 0;
	sps->pic_width_in_mbs_minus1 =
		DIV_ROUND_UP(channel->width, size_mb) - 1;
	sps->pic_height_in_map_units_minus1 =
		DIV_ROUND_UP(channel->height, size_mb) - 1;
	sps->frame_mbs_only_flag = 1;
	sps->mb_adaptive_frame_field_flag = 0;
	sps->direct_8x8_inference_flag = 1;
	sps->frame_cropping_flag =
		(channel->width % size_mb) || (channel->height % size_mb);
	if (sps->frame_cropping_flag) {
		sps->crop_left = 0;
		sps->crop_right = (round_up(channel->width, size_mb) - channel->width) / crop_unit_x;
		sps->crop_top = 0;
		sps->crop_bottom = (round_up(channel->height, size_mb) - channel->height) / crop_unit_y;
	}
	sps->vui_parameters_present_flag = 1;
	sps->vui.aspect_ratio_info_present_flag = 0;
	sps->vui.overscan_info_present_flag = 0;

	sps->vui.video_signal_type_present_flag = 1;
	sps->vui.video_format = 5; /* unspecified */
	sps->vui.video_full_range_flag = nal_h264_full_range(channel->quantization);
	sps->vui.colour_description_present_flag = 1;
	sps->vui.colour_primaries = nal_h264_color_primaries(channel->colorspace);
	sps->vui.transfer_characteristics =
		nal_h264_transfer_characteristics(channel->colorspace, channel->xfer_func);
	sps->vui.matrix_coefficients =
		nal_h264_matrix_coeffs(channel->colorspace, channel->ycbcr_enc);

	sps->vui.chroma_loc_info_present_flag = 1;
	sps->vui.chroma_sample_loc_type_top_field = 0;
	sps->vui.chroma_sample_loc_type_bottom_field = 0;

	sps->vui.timing_info_present_flag = 1;
	sps->vui.num_units_in_tick = channel->framerate.denominator;
	sps->vui.time_scale = 2 * channel->framerate.numerator;

	sps->vui.fixed_frame_rate_flag = 1;
	sps->vui.nal_hrd_parameters_present_flag = 0;
	sps->vui.vcl_hrd_parameters_present_flag = 1;
	sps->vui.vcl_hrd_parameters.cpb_cnt_minus1 = 0;
	/* See Rec. ITU-T H.264 (04/2017) p. 410 E-53 */
	sps->vui.vcl_hrd_parameters.bit_rate_scale =
		ffs(channel->bitrate_peak) - 6;
	sps->vui.vcl_hrd_parameters.bit_rate_value_minus1[0] =
		channel->bitrate_peak / (1 << (6 + sps->vui.vcl_hrd_parameters.bit_rate_scale)) - 1;
	/* See Rec. ITU-T H.264 (04/2017) p. 410 E-54 */
	cpb_size = v4l2_ctrl_g_ctrl(channel->mpeg_video_cpb_size);
	cpb_size_scale = ffs(cpb_size) - 4;
	sps->vui.vcl_hrd_parameters.cpb_size_scale = cpb_size_scale;
	sps->vui.vcl_hrd_parameters.cpb_size_value_minus1[0] =
		(cpb_size * 1000) / (1 << (4 + cpb_size_scale)) - 1;
	sps->vui.vcl_hrd_parameters.cbr_flag[0] =
		!v4l2_ctrl_g_ctrl(channel->mpeg_video_frame_rc_enable);
	sps->vui.vcl_hrd_parameters.initial_cpb_removal_delay_length_minus1 = 31;
	sps->vui.vcl_hrd_parameters.cpb_removal_delay_length_minus1 = 31;
	sps->vui.vcl_hrd_parameters.dpb_output_delay_length_minus1 = 31;
	sps->vui.vcl_hrd_parameters.time_offset_length = 0;
	sps->vui.low_delay_hrd_flag = 0;
	sps->vui.pic_struct_present_flag = 1;
	sps->vui.bitstream_restriction_flag = 0;

	size = nal_h264_write_sps(&dev->plat_dev->dev, dest, n, sps);

	kfree(sps);

	return size;
}

static ssize_t allegro_h264_write_pps(struct allegro_channel *channel,
				      void *dest, size_t n)
{
	struct allegro_dev *dev = channel->dev;
	struct nal_h264_pps *pps;
	ssize_t size;

	pps = kzalloc(sizeof(*pps), GFP_KERNEL);
	if (!pps)
		return -ENOMEM;

	pps->pic_parameter_set_id = 0;
	pps->seq_parameter_set_id = 0;
	pps->entropy_coding_mode_flag = 0;
	pps->bottom_field_pic_order_in_frame_present_flag = 0;
	pps->num_slice_groups_minus1 = 0;
	pps->num_ref_idx_l0_default_active_minus1 = channel->num_ref_idx_l0 - 1;
	pps->num_ref_idx_l1_default_active_minus1 = channel->num_ref_idx_l1 - 1;
	pps->weighted_pred_flag = 0;
	pps->weighted_bipred_idc = 0;
	pps->pic_init_qp_minus26 = 0;
	pps->pic_init_qs_minus26 = 0;
	pps->chroma_qp_index_offset = 0;
	pps->deblocking_filter_control_present_flag = 1;
	pps->constrained_intra_pred_flag = 0;
	pps->redundant_pic_cnt_present_flag = 0;
	pps->transform_8x8_mode_flag = 0;
	pps->pic_scaling_matrix_present_flag = 0;
	pps->second_chroma_qp_index_offset = 0;

	size = nal_h264_write_pps(&dev->plat_dev->dev, dest, n, pps);

	kfree(pps);

	return size;
}

static void allegro_channel_eos_event(struct allegro_channel *channel)
{
	const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};

	v4l2_event_queue_fh(&channel->fh, &eos_event);
}

static ssize_t allegro_hevc_write_vps(struct allegro_channel *channel,
				      void *dest, size_t n)
{
	struct allegro_dev *dev = channel->dev;
	struct nal_hevc_vps *vps;
	struct nal_hevc_profile_tier_level *ptl;
	ssize_t size;
	unsigned int num_ref_frames = channel->num_ref_idx_l0;
	s32 profile = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_profile);
	s32 level = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_level);
	s32 tier = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_tier);

	vps = kzalloc(sizeof(*vps), GFP_KERNEL);
	if (!vps)
		return -ENOMEM;

	vps->base_layer_internal_flag = 1;
	vps->base_layer_available_flag = 1;
	vps->temporal_id_nesting_flag = 1;

	ptl = &vps->profile_tier_level;
	ptl->general_profile_idc = nal_hevc_profile(profile);
	ptl->general_profile_compatibility_flag[ptl->general_profile_idc] = 1;
	ptl->general_tier_flag = nal_hevc_tier(tier);
	ptl->general_progressive_source_flag = 1;
	ptl->general_frame_only_constraint_flag = 1;
	ptl->general_level_idc = nal_hevc_level(level);

	vps->sub_layer_ordering_info_present_flag = 0;
	vps->max_dec_pic_buffering_minus1[0] = num_ref_frames;
	vps->max_num_reorder_pics[0] = num_ref_frames;

	size = nal_hevc_write_vps(&dev->plat_dev->dev, dest, n, vps);

	kfree(vps);

	return size;
}

static ssize_t allegro_hevc_write_sps(struct allegro_channel *channel,
				      void *dest, size_t n)
{
	struct allegro_dev *dev = channel->dev;
	struct nal_hevc_sps *sps;
	struct nal_hevc_profile_tier_level *ptl;
	struct nal_hevc_vui_parameters *vui;
	struct nal_hevc_hrd_parameters *hrd;
	ssize_t size;
	unsigned int cpb_size;
	unsigned int num_ref_frames = channel->num_ref_idx_l0;
	s32 profile = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_profile);
	s32 level = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_level);
	s32 tier = v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_tier);

	sps = kzalloc(sizeof(*sps), GFP_KERNEL);
	if (!sps)
		return -ENOMEM;

	sps->temporal_id_nesting_flag = 1;

	ptl = &sps->profile_tier_level;
	ptl->general_profile_idc = nal_hevc_profile(profile);
	ptl->general_profile_compatibility_flag[ptl->general_profile_idc] = 1;
	ptl->general_tier_flag = nal_hevc_tier(tier);
	ptl->general_progressive_source_flag = 1;
	ptl->general_frame_only_constraint_flag = 1;
	ptl->general_level_idc = nal_hevc_level(level);

	sps->seq_parameter_set_id = 0;
	sps->chroma_format_idc = 1; /* Only 4:2:0 sampling supported */
	sps->pic_width_in_luma_samples = round_up(channel->width, 8);
	sps->pic_height_in_luma_samples = round_up(channel->height, 8);
	sps->conf_win_right_offset =
		sps->pic_width_in_luma_samples - channel->width;
	sps->conf_win_bottom_offset =
		sps->pic_height_in_luma_samples - channel->height;
	sps->conformance_window_flag =
		sps->conf_win_right_offset || sps->conf_win_bottom_offset;

	sps->log2_max_pic_order_cnt_lsb_minus4 = LOG2_MAX_PIC_ORDER_CNT - 4;

	sps->sub_layer_ordering_info_present_flag = 1;
	sps->max_dec_pic_buffering_minus1[0] = num_ref_frames;
	sps->max_num_reorder_pics[0] = num_ref_frames;

	sps->log2_min_luma_coding_block_size_minus3 =
		channel->min_cu_size - 3;
	sps->log2_diff_max_min_luma_coding_block_size =
		channel->max_cu_size - channel->min_cu_size;
	sps->log2_min_luma_transform_block_size_minus2 =
		channel->min_tu_size - 2;
	sps->log2_diff_max_min_luma_transform_block_size =
		channel->max_tu_size - channel->min_tu_size;
	sps->max_transform_hierarchy_depth_intra =
		channel->max_transfo_depth_intra;
	sps->max_transform_hierarchy_depth_inter =
		channel->max_transfo_depth_inter;

	sps->sps_temporal_mvp_enabled_flag = channel->temporal_mvp_enable;
	sps->strong_intra_smoothing_enabled_flag = channel->max_cu_size > 4;

	sps->vui_parameters_present_flag = 1;
	vui = &sps->vui;

	vui->video_signal_type_present_flag = 1;
	vui->video_format = 5; /* unspecified */
	vui->video_full_range_flag = nal_hevc_full_range(channel->quantization);
	vui->colour_description_present_flag = 1;
	vui->colour_primaries = nal_hevc_color_primaries(channel->colorspace);
	vui->transfer_characteristics = nal_hevc_transfer_characteristics(channel->colorspace,
									  channel->xfer_func);
	vui->matrix_coeffs = nal_hevc_matrix_coeffs(channel->colorspace, channel->ycbcr_enc);

	vui->chroma_loc_info_present_flag = 1;
	vui->chroma_sample_loc_type_top_field = 0;
	vui->chroma_sample_loc_type_bottom_field = 0;

	vui->vui_timing_info_present_flag = 1;
	vui->vui_num_units_in_tick = channel->framerate.denominator;
	vui->vui_time_scale = channel->framerate.numerator;

	vui->bitstream_restriction_flag = 1;
	vui->motion_vectors_over_pic_boundaries_flag = 1;
	vui->restricted_ref_pic_lists_flag = 1;
	vui->log2_max_mv_length_horizontal = 15;
	vui->log2_max_mv_length_vertical = 15;

	vui->vui_hrd_parameters_present_flag = 1;
	hrd = &vui->nal_hrd_parameters;
	hrd->vcl_hrd_parameters_present_flag = 1;

	hrd->initial_cpb_removal_delay_length_minus1 = 31;
	hrd->au_cpb_removal_delay_length_minus1 = 30;
	hrd->dpb_output_delay_length_minus1 = 30;

	hrd->bit_rate_scale = ffs(channel->bitrate_peak) - 6;
	hrd->vcl_hrd[0].bit_rate_value_minus1[0] =
		(channel->bitrate_peak >> (6 + hrd->bit_rate_scale)) - 1;

	cpb_size = v4l2_ctrl_g_ctrl(channel->mpeg_video_cpb_size) * 1000;
	hrd->cpb_size_scale = ffs(cpb_size) - 4;
	hrd->vcl_hrd[0].cpb_size_value_minus1[0] = (cpb_size >> (4 + hrd->cpb_size_scale)) - 1;

	hrd->vcl_hrd[0].cbr_flag[0] = !v4l2_ctrl_g_ctrl(channel->mpeg_video_frame_rc_enable);

	size = nal_hevc_write_sps(&dev->plat_dev->dev, dest, n, sps);

	kfree(sps);

	return size;
}

static ssize_t allegro_hevc_write_pps(struct allegro_channel *channel,
				      struct mcu_msg_encode_frame_response *msg,
				      void *dest, size_t n)
{
	struct allegro_dev *dev = channel->dev;
	struct nal_hevc_pps *pps;
	ssize_t size;
	int i;

	pps = kzalloc(sizeof(*pps), GFP_KERNEL);
	if (!pps)
		return -ENOMEM;

	pps->pps_pic_parameter_set_id = 0;
	pps->pps_seq_parameter_set_id = 0;

	if (msg->num_column > 1 || msg->num_row > 1) {
		pps->tiles_enabled_flag = 1;
		pps->num_tile_columns_minus1 = msg->num_column - 1;
		pps->num_tile_rows_minus1 = msg->num_row - 1;

		for (i = 0; i < msg->num_column; i++)
			pps->column_width_minus1[i] = msg->tile_width[i] - 1;

		for (i = 0; i < msg->num_row; i++)
			pps->row_height_minus1[i] = msg->tile_height[i] - 1;
	}

	pps->loop_filter_across_tiles_enabled_flag =
		channel->enable_loop_filter_across_tiles;
	pps->pps_loop_filter_across_slices_enabled_flag =
		channel->enable_loop_filter_across_slices;
	pps->deblocking_filter_control_present_flag = 1;
	pps->deblocking_filter_override_enabled_flag =
		channel->enable_deblocking_filter_override;
	pps->pps_beta_offset_div2 = BETA_OFFSET_DIV_2;
	pps->pps_tc_offset_div2 = TC_OFFSET_DIV_2;

	pps->lists_modification_present_flag = channel->enable_reordering;

	size = nal_hevc_write_pps(&dev->plat_dev->dev, dest, n, pps);

	kfree(pps);

	return size;
}

static u64 allegro_put_buffer(struct allegro_channel *channel,
			      struct list_head *list,
			      struct vb2_v4l2_buffer *buffer)
{
	struct v4l2_m2m_buffer *b = container_of(buffer,
						 struct v4l2_m2m_buffer, vb);
	struct allegro_m2m_buffer *shadow = to_allegro_m2m_buffer(b);

	mutex_lock(&channel->shadow_list_lock);
	list_add_tail(&shadow->head, list);
	mutex_unlock(&channel->shadow_list_lock);

	return ptr_to_u64(buffer);
}

static struct vb2_v4l2_buffer *
allegro_get_buffer(struct allegro_channel *channel,
		   struct list_head *list, u64 handle)
{
	struct allegro_m2m_buffer *shadow, *tmp;
	struct vb2_v4l2_buffer *buffer = NULL;

	mutex_lock(&channel->shadow_list_lock);
	list_for_each_entry_safe(shadow, tmp, list, head) {
		if (handle == ptr_to_u64(&shadow->buf.vb)) {
			buffer = &shadow->buf.vb;
			list_del_init(&shadow->head);
			break;
		}
	}
	mutex_unlock(&channel->shadow_list_lock);

	return buffer;
}

static void allegro_channel_finish_frame(struct allegro_channel *channel,
		struct mcu_msg_encode_frame_response *msg)
{
	struct allegro_dev *dev = channel->dev;
	struct vb2_v4l2_buffer *src_buf;
	struct vb2_v4l2_buffer *dst_buf;
	struct {
		u32 offset;
		u32 size;
	} *partition;
	enum vb2_buffer_state state = VB2_BUF_STATE_ERROR;
	char *curr;
	ssize_t len;
	ssize_t free;

	src_buf = allegro_get_buffer(channel, &channel->source_shadow_list,
				     msg->src_handle);
	if (!src_buf)
		v4l2_warn(&dev->v4l2_dev,
			  "channel %d: invalid source buffer\n",
			  channel->mcu_channel_id);

	dst_buf = allegro_get_buffer(channel, &channel->stream_shadow_list,
				     msg->dst_handle);
	if (!dst_buf)
		v4l2_warn(&dev->v4l2_dev,
			  "channel %d: invalid stream buffer\n",
			  channel->mcu_channel_id);

	if (!src_buf || !dst_buf)
		goto err;

	if (v4l2_m2m_is_last_draining_src_buf(channel->fh.m2m_ctx, src_buf)) {
		dst_buf->flags |= V4L2_BUF_FLAG_LAST;
		allegro_channel_eos_event(channel);
		v4l2_m2m_mark_stopped(channel->fh.m2m_ctx);
	}

	dst_buf->sequence = channel->csequence++;

	if (msg->error_code & AL_ERROR) {
		v4l2_err(&dev->v4l2_dev,
			 "channel %d: failed to encode frame: %s (%x)\n",
			 channel->mcu_channel_id,
			 allegro_err_to_string(msg->error_code),
			 msg->error_code);
		goto err;
	}

	if (msg->partition_table_size != 1) {
		v4l2_warn(&dev->v4l2_dev,
			  "channel %d: only handling first partition table entry (%d entries)\n",
			  channel->mcu_channel_id, msg->partition_table_size);
	}

	if (msg->partition_table_offset +
	    msg->partition_table_size * sizeof(*partition) >
	    vb2_plane_size(&dst_buf->vb2_buf, 0)) {
		v4l2_err(&dev->v4l2_dev,
			 "channel %d: partition table outside of dst_buf\n",
			 channel->mcu_channel_id);
		goto err;
	}

	partition =
	    vb2_plane_vaddr(&dst_buf->vb2_buf, 0) + msg->partition_table_offset;
	if (partition->offset + partition->size >
	    vb2_plane_size(&dst_buf->vb2_buf, 0)) {
		v4l2_err(&dev->v4l2_dev,
			 "channel %d: encoded frame is outside of dst_buf (offset 0x%x, size 0x%x)\n",
			 channel->mcu_channel_id, partition->offset,
			 partition->size);
		goto err;
	}

	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "channel %d: encoded frame of size %d is at offset 0x%x\n",
		 channel->mcu_channel_id, partition->size, partition->offset);

	/*
	 * The payload must include the data before the partition offset,
	 * because we will put the sps and pps data there.
	 */
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
			      partition->offset + partition->size);

	curr = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	free = partition->offset;

	if (channel->codec == V4L2_PIX_FMT_HEVC && msg->is_idr) {
		len = allegro_hevc_write_vps(channel, curr, free);
		if (len < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "not enough space for video parameter set: %zd left\n",
				 free);
			goto err;
		}
		curr += len;
		free -= len;
		v4l2_dbg(1, debug, &dev->v4l2_dev,
			 "channel %d: wrote %zd byte VPS nal unit\n",
			 channel->mcu_channel_id, len);
	}

	if (msg->is_idr) {
		if (channel->codec == V4L2_PIX_FMT_H264)
			len = allegro_h264_write_sps(channel, curr, free);
		else
			len = allegro_hevc_write_sps(channel, curr, free);
		if (len < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "not enough space for sequence parameter set: %zd left\n",
				 free);
			goto err;
		}
		curr += len;
		free -= len;
		v4l2_dbg(1, debug, &dev->v4l2_dev,
			 "channel %d: wrote %zd byte SPS nal unit\n",
			 channel->mcu_channel_id, len);
	}

	if (msg->slice_type == AL_ENC_SLICE_TYPE_I) {
		if (channel->codec == V4L2_PIX_FMT_H264)
			len = allegro_h264_write_pps(channel, curr, free);
		else
			len = allegro_hevc_write_pps(channel, msg, curr, free);
		if (len < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "not enough space for picture parameter set: %zd left\n",
				 free);
			goto err;
		}
		curr += len;
		free -= len;
		v4l2_dbg(1, debug, &dev->v4l2_dev,
			 "channel %d: wrote %zd byte PPS nal unit\n",
			 channel->mcu_channel_id, len);
	}

	if (msg->slice_type != AL_ENC_SLICE_TYPE_I && !msg->is_idr) {
		dst_buf->vb2_buf.planes[0].data_offset = free;
		free = 0;
	} else {
		if (channel->codec == V4L2_PIX_FMT_H264)
			len = nal_h264_write_filler(&dev->plat_dev->dev, curr, free);
		else
			len = nal_hevc_write_filler(&dev->plat_dev->dev, curr, free);
		if (len < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "failed to write %zd filler data\n", free);
			goto err;
		}
		curr += len;
		free -= len;
		v4l2_dbg(2, debug, &dev->v4l2_dev,
			 "channel %d: wrote %zd bytes filler nal unit\n",
			 channel->mcu_channel_id, len);
	}

	if (free != 0) {
		v4l2_err(&dev->v4l2_dev,
			 "non-VCL NAL units do not fill space until VCL NAL unit: %zd bytes left\n",
			 free);
		goto err;
	}

	state = VB2_BUF_STATE_DONE;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, false);
	if (msg->is_idr)
		dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	else
		dst_buf->flags |= V4L2_BUF_FLAG_PFRAME;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "channel %d: encoded frame #%03d (%s%s, QP %d, %d bytes)\n",
		 channel->mcu_channel_id,
		 dst_buf->sequence,
		 msg->is_idr ? "IDR, " : "",
		 msg->slice_type == AL_ENC_SLICE_TYPE_I ? "I slice" :
		 msg->slice_type == AL_ENC_SLICE_TYPE_P ? "P slice" : "unknown",
		 msg->qp, partition->size);

err:
	if (src_buf)
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);

	if (dst_buf)
		v4l2_m2m_buf_done(dst_buf, state);
}

static int allegro_handle_init(struct allegro_dev *dev,
			       struct mcu_msg_init_response *msg)
{
	complete(&dev->init_complete);

	return 0;
}

static int
allegro_handle_create_channel(struct allegro_dev *dev,
			      struct mcu_msg_create_channel_response *msg)
{
	struct allegro_channel *channel;
	int err = 0;
	struct create_channel_param param;

	channel = allegro_find_channel_by_user_id(dev, msg->user_id);
	if (IS_ERR(channel)) {
		v4l2_warn(&dev->v4l2_dev,
			  "received %s for unknown user %d\n",
			  msg_type_name(msg->header.type),
			  msg->user_id);
		return -EINVAL;
	}

	if (msg->error_code) {
		v4l2_err(&dev->v4l2_dev,
			 "user %d: mcu failed to create channel: %s (%x)\n",
			 channel->user_id,
			 allegro_err_to_string(msg->error_code),
			 msg->error_code);
		err = -EIO;
		goto out;
	}

	channel->mcu_channel_id = msg->channel_id;
	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "user %d: channel has channel id %d\n",
		 channel->user_id, channel->mcu_channel_id);

	err = allegro_decode_config_blob(&param, msg, channel->config_blob.vaddr);
	allegro_free_buffer(channel->dev, &channel->config_blob);
	if (err)
		goto out;

	channel->num_ref_idx_l0 = param.num_ref_idx_l0;
	channel->num_ref_idx_l1 = param.num_ref_idx_l1;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "channel %d: intermediate buffers: %d x %d bytes\n",
		 channel->mcu_channel_id,
		 msg->int_buffers_count, msg->int_buffers_size);
	err = allocate_intermediate_buffers(channel, msg->int_buffers_count,
					    msg->int_buffers_size);
	if (err) {
		v4l2_err(&dev->v4l2_dev,
			 "channel %d: failed to allocate intermediate buffers\n",
			 channel->mcu_channel_id);
		goto out;
	}
	err = allegro_mcu_push_buffer_intermediate(channel);
	if (err)
		goto out;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "channel %d: reference buffers: %d x %d bytes\n",
		 channel->mcu_channel_id,
		 msg->rec_buffers_count, msg->rec_buffers_size);
	err = allocate_reference_buffers(channel, msg->rec_buffers_count,
					 msg->rec_buffers_size);
	if (err) {
		v4l2_err(&dev->v4l2_dev,
			 "channel %d: failed to allocate reference buffers\n",
			 channel->mcu_channel_id);
		goto out;
	}
	err = allegro_mcu_push_buffer_reference(channel);
	if (err)
		goto out;

out:
	channel->error = err;
	complete(&channel->completion);

	/* Handled successfully, error is passed via channel->error */
	return 0;
}

static int
allegro_handle_destroy_channel(struct allegro_dev *dev,
			       struct mcu_msg_destroy_channel_response *msg)
{
	struct allegro_channel *channel;

	channel = allegro_find_channel_by_channel_id(dev, msg->channel_id);
	if (IS_ERR(channel)) {
		v4l2_err(&dev->v4l2_dev,
			 "received %s for unknown channel %d\n",
			 msg_type_name(msg->header.type),
			 msg->channel_id);
		return -EINVAL;
	}

	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "user %d: vcu destroyed channel %d\n",
		 channel->user_id, channel->mcu_channel_id);
	complete(&channel->completion);

	return 0;
}

static int
allegro_handle_encode_frame(struct allegro_dev *dev,
			    struct mcu_msg_encode_frame_response *msg)
{
	struct allegro_channel *channel;

	channel = allegro_find_channel_by_channel_id(dev, msg->channel_id);
	if (IS_ERR(channel)) {
		v4l2_err(&dev->v4l2_dev,
			 "received %s for unknown channel %d\n",
			 msg_type_name(msg->header.type),
			 msg->channel_id);
		return -EINVAL;
	}

	allegro_channel_finish_frame(channel, msg);

	return 0;
}

static void allegro_handle_message(struct allegro_dev *dev,
				   union mcu_msg_response *msg)
{
	switch (msg->header.type) {
	case MCU_MSG_TYPE_INIT:
		allegro_handle_init(dev, &msg->init);
		break;
	case MCU_MSG_TYPE_CREATE_CHANNEL:
		allegro_handle_create_channel(dev, &msg->create_channel);
		break;
	case MCU_MSG_TYPE_DESTROY_CHANNEL:
		allegro_handle_destroy_channel(dev, &msg->destroy_channel);
		break;
	case MCU_MSG_TYPE_ENCODE_FRAME:
		allegro_handle_encode_frame(dev, &msg->encode_frame);
		break;
	default:
		v4l2_warn(&dev->v4l2_dev,
			  "%s: unknown message %s\n",
			  __func__, msg_type_name(msg->header.type));
		break;
	}
}

static irqreturn_t allegro_hardirq(int irq, void *data)
{
	struct allegro_dev *dev = data;
	unsigned int status;

	regmap_read(dev->regmap, AL5_ITC_CPU_IRQ_STA, &status);
	if (!(status & AL5_ITC_CPU_IRQ_STA_TRIGGERED))
		return IRQ_NONE;

	regmap_write(dev->regmap, AL5_ITC_CPU_IRQ_CLR, status);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t allegro_irq_thread(int irq, void *data)
{
	struct allegro_dev *dev = data;

	/*
	 * The firmware is initialized after the mailbox is setup. We further
	 * check the AL5_ITC_CPU_IRQ_STA register, if the firmware actually
	 * triggered the interrupt. Although this should not happen, make sure
	 * that we ignore interrupts, if the mailbox is not initialized.
	 */
	if (!dev->mbox_status)
		return IRQ_NONE;

	allegro_mbox_notify(dev->mbox_status);

	return IRQ_HANDLED;
}

static void allegro_copy_firmware(struct allegro_dev *dev,
				  const u8 * const buf, size_t size)
{
	int err = 0;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "copy mcu firmware (%zu B) to SRAM\n", size);
	err = regmap_bulk_write(dev->sram, 0x0, buf, size / 4);
	if (err)
		v4l2_err(&dev->v4l2_dev,
			 "failed to copy firmware: %d\n", err);
}

static void allegro_copy_fw_codec(struct allegro_dev *dev,
				  const u8 * const buf, size_t size)
{
	int err;
	dma_addr_t icache_offset, dcache_offset;

	/*
	 * The downstream allocates 600 KB for the codec firmware to have some
	 * extra space for "possible extensions." My tests were fine with
	 * allocating just enough memory for the actual firmware, but I am not
	 * sure that the firmware really does not use the remaining space.
	 */
	err = allegro_alloc_buffer(dev, &dev->firmware, size);
	if (err) {
		v4l2_err(&dev->v4l2_dev,
			 "failed to allocate %zu bytes for firmware\n", size);
		return;
	}

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "copy codec firmware (%zd B) to phys %pad\n",
		 size, &dev->firmware.paddr);
	memcpy(dev->firmware.vaddr, buf, size);

	regmap_write(dev->regmap, AXI_ADDR_OFFSET_IP,
		     upper_32_bits(dev->firmware.paddr));

	icache_offset = dev->firmware.paddr - MCU_CACHE_OFFSET;
	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "icache_offset: msb = 0x%x, lsb = 0x%x\n",
		 upper_32_bits(icache_offset), lower_32_bits(icache_offset));
	regmap_write(dev->regmap, AL5_ICACHE_ADDR_OFFSET_MSB,
		     upper_32_bits(icache_offset));
	regmap_write(dev->regmap, AL5_ICACHE_ADDR_OFFSET_LSB,
		     lower_32_bits(icache_offset));

	dcache_offset =
	    (dev->firmware.paddr & 0xffffffff00000000ULL) - MCU_CACHE_OFFSET;
	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "dcache_offset: msb = 0x%x, lsb = 0x%x\n",
		 upper_32_bits(dcache_offset), lower_32_bits(dcache_offset));
	regmap_write(dev->regmap, AL5_DCACHE_ADDR_OFFSET_MSB,
		     upper_32_bits(dcache_offset));
	regmap_write(dev->regmap, AL5_DCACHE_ADDR_OFFSET_LSB,
		     lower_32_bits(dcache_offset));
}

static void allegro_free_fw_codec(struct allegro_dev *dev)
{
	allegro_free_buffer(dev, &dev->firmware);
}

/*
 * Control functions for the MCU
 */

static int allegro_mcu_enable_interrupts(struct allegro_dev *dev)
{
	return regmap_write(dev->regmap, AL5_ITC_CPU_IRQ_MSK, BIT(0));
}

static int allegro_mcu_disable_interrupts(struct allegro_dev *dev)
{
	return regmap_write(dev->regmap, AL5_ITC_CPU_IRQ_MSK, 0);
}

static int allegro_mcu_wait_for_sleep(struct allegro_dev *dev)
{
	unsigned long timeout;
	unsigned int status;

	timeout = jiffies + msecs_to_jiffies(100);
	while (regmap_read(dev->regmap, AL5_MCU_STA, &status) == 0 &&
	       status != AL5_MCU_STA_SLEEP) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	return 0;
}

static int allegro_mcu_start(struct allegro_dev *dev)
{
	unsigned long timeout;
	unsigned int status;
	int err;

	err = regmap_write(dev->regmap, AL5_MCU_WAKEUP, BIT(0));
	if (err)
		return err;

	timeout = jiffies + msecs_to_jiffies(100);
	while (regmap_read(dev->regmap, AL5_MCU_STA, &status) == 0 &&
	       status == AL5_MCU_STA_SLEEP) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	err = regmap_write(dev->regmap, AL5_MCU_WAKEUP, 0);
	if (err)
		return err;

	return 0;
}

static int allegro_mcu_reset(struct allegro_dev *dev)
{
	int err;

	/*
	 * Ensure that the AL5_MCU_WAKEUP bit is set to 0 otherwise the mcu
	 * does not go to sleep after the reset.
	 */
	err = regmap_write(dev->regmap, AL5_MCU_WAKEUP, 0);
	if (err)
		return err;

	err = regmap_write(dev->regmap,
			   AL5_MCU_RESET_MODE, AL5_MCU_RESET_MODE_SLEEP);
	if (err < 0)
		return err;

	err = regmap_write(dev->regmap, AL5_MCU_RESET, AL5_MCU_RESET_SOFT);
	if (err < 0)
		return err;

	return allegro_mcu_wait_for_sleep(dev);
}

static void allegro_mcu_interrupt(struct allegro_dev *dev)
{
	regmap_write(dev->regmap, AL5_MCU_INTERRUPT, BIT(0));
}

static void allegro_destroy_channel(struct allegro_channel *channel)
{
	struct allegro_dev *dev = channel->dev;
	unsigned long time_left;

	if (channel_exists(channel)) {
		reinit_completion(&channel->completion);
		allegro_mcu_send_destroy_channel(dev, channel);
		time_left = wait_for_completion_timeout(&channel->completion,
							msecs_to_jiffies(5000));
		if (time_left == 0)
			v4l2_warn(&dev->v4l2_dev,
				  "channel %d: timeout while destroying\n",
				  channel->mcu_channel_id);

		channel->mcu_channel_id = -1;
	}

	destroy_intermediate_buffers(channel);
	destroy_reference_buffers(channel);

	v4l2_ctrl_grab(channel->mpeg_video_h264_profile, false);
	v4l2_ctrl_grab(channel->mpeg_video_h264_level, false);
	v4l2_ctrl_grab(channel->mpeg_video_h264_i_frame_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_h264_max_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_h264_min_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_h264_p_frame_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_h264_b_frame_qp, false);

	v4l2_ctrl_grab(channel->mpeg_video_hevc_profile, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_level, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_tier, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_i_frame_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_max_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_min_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_p_frame_qp, false);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_b_frame_qp, false);

	v4l2_ctrl_grab(channel->mpeg_video_frame_rc_enable, false);
	v4l2_ctrl_grab(channel->mpeg_video_bitrate_mode, false);
	v4l2_ctrl_grab(channel->mpeg_video_bitrate, false);
	v4l2_ctrl_grab(channel->mpeg_video_bitrate_peak, false);
	v4l2_ctrl_grab(channel->mpeg_video_cpb_size, false);
	v4l2_ctrl_grab(channel->mpeg_video_gop_size, false);

	v4l2_ctrl_grab(channel->encoder_buffer, false);

	if (channel->user_id != -1) {
		clear_bit(channel->user_id, &dev->channel_user_ids);
		channel->user_id = -1;
	}
}

/*
 * Create the MCU channel
 *
 * After the channel has been created, the picture size, format, colorspace
 * and framerate are fixed. Also the codec, profile, bitrate, etc. cannot be
 * changed anymore.
 *
 * The channel can be created only once. The MCU will accept source buffers
 * and stream buffers only after a channel has been created.
 */
static int allegro_create_channel(struct allegro_channel *channel)
{
	struct allegro_dev *dev = channel->dev;
	unsigned long time_left;

	if (channel_exists(channel)) {
		v4l2_warn(&dev->v4l2_dev,
			  "channel already exists\n");
		return 0;
	}

	channel->user_id = allegro_next_user_id(dev);
	if (channel->user_id < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "no free channels available\n");
		return -EBUSY;
	}
	set_bit(channel->user_id, &dev->channel_user_ids);

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "user %d: creating channel (%4.4s, %dx%d@%d)\n",
		 channel->user_id,
		 (char *)&channel->codec, channel->width, channel->height,
		 DIV_ROUND_UP(channel->framerate.numerator,
			      channel->framerate.denominator));

	v4l2_ctrl_grab(channel->mpeg_video_h264_profile, true);
	v4l2_ctrl_grab(channel->mpeg_video_h264_level, true);
	v4l2_ctrl_grab(channel->mpeg_video_h264_i_frame_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_h264_max_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_h264_min_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_h264_p_frame_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_h264_b_frame_qp, true);

	v4l2_ctrl_grab(channel->mpeg_video_hevc_profile, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_level, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_tier, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_i_frame_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_max_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_min_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_p_frame_qp, true);
	v4l2_ctrl_grab(channel->mpeg_video_hevc_b_frame_qp, true);

	v4l2_ctrl_grab(channel->mpeg_video_frame_rc_enable, true);
	v4l2_ctrl_grab(channel->mpeg_video_bitrate_mode, true);
	v4l2_ctrl_grab(channel->mpeg_video_bitrate, true);
	v4l2_ctrl_grab(channel->mpeg_video_bitrate_peak, true);
	v4l2_ctrl_grab(channel->mpeg_video_cpb_size, true);
	v4l2_ctrl_grab(channel->mpeg_video_gop_size, true);

	v4l2_ctrl_grab(channel->encoder_buffer, true);

	reinit_completion(&channel->completion);
	allegro_mcu_send_create_channel(dev, channel);
	time_left = wait_for_completion_timeout(&channel->completion,
						msecs_to_jiffies(5000));
	if (time_left == 0)
		channel->error = -ETIMEDOUT;
	if (channel->error)
		goto err;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "channel %d: accepting buffers\n",
		 channel->mcu_channel_id);

	return 0;

err:
	allegro_destroy_channel(channel);

	return channel->error;
}

/**
 * allegro_channel_adjust() - Adjust channel parameters to current format
 * @channel: the channel to adjust
 *
 * Various parameters of a channel and their limits depend on the currently
 * set format. Adjust the parameters after a format change in one go.
 */
static void allegro_channel_adjust(struct allegro_channel *channel)
{
	struct allegro_dev *dev = channel->dev;
	u32 codec = channel->codec;
	struct v4l2_ctrl *ctrl;
	s64 min;
	s64 max;

	channel->sizeimage_encoded =
		estimate_stream_size(channel->width, channel->height);

	if (codec == V4L2_PIX_FMT_H264) {
		ctrl = channel->mpeg_video_h264_level;
		min = select_minimum_h264_level(channel->width, channel->height);
	} else {
		ctrl = channel->mpeg_video_hevc_level;
		min = select_minimum_hevc_level(channel->width, channel->height);
	}
	if (ctrl->minimum > min)
		v4l2_dbg(1, debug, &dev->v4l2_dev,
			 "%s.minimum: %lld -> %lld\n",
			 v4l2_ctrl_get_name(ctrl->id), ctrl->minimum, min);
	v4l2_ctrl_lock(ctrl);
	__v4l2_ctrl_modify_range(ctrl, min, ctrl->maximum,
				 ctrl->step, ctrl->default_value);
	v4l2_ctrl_unlock(ctrl);

	ctrl = channel->mpeg_video_bitrate;
	if (codec == V4L2_PIX_FMT_H264)
		max = h264_maximum_bitrate(v4l2_ctrl_g_ctrl(channel->mpeg_video_h264_level));
	else
		max = hevc_maximum_bitrate(v4l2_ctrl_g_ctrl(channel->mpeg_video_hevc_level));
	if (ctrl->maximum < max)
		v4l2_dbg(1, debug, &dev->v4l2_dev,
			 "%s: maximum: %lld -> %lld\n",
			 v4l2_ctrl_get_name(ctrl->id), ctrl->maximum, max);
	v4l2_ctrl_lock(ctrl);
	__v4l2_ctrl_modify_range(ctrl, ctrl->minimum, max,
				 ctrl->step, ctrl->default_value);
	v4l2_ctrl_unlock(ctrl);

	ctrl = channel->mpeg_video_bitrate_peak;
	v4l2_ctrl_lock(ctrl);
	__v4l2_ctrl_modify_range(ctrl, ctrl->minimum, max,
				 ctrl->step, ctrl->default_value);
	v4l2_ctrl_unlock(ctrl);

	v4l2_ctrl_activate(channel->mpeg_video_h264_profile,
			   codec == V4L2_PIX_FMT_H264);
	v4l2_ctrl_activate(channel->mpeg_video_h264_level,
			   codec == V4L2_PIX_FMT_H264);
	v4l2_ctrl_activate(channel->mpeg_video_h264_i_frame_qp,
			   codec == V4L2_PIX_FMT_H264);
	v4l2_ctrl_activate(channel->mpeg_video_h264_max_qp,
			   codec == V4L2_PIX_FMT_H264);
	v4l2_ctrl_activate(channel->mpeg_video_h264_min_qp,
			   codec == V4L2_PIX_FMT_H264);
	v4l2_ctrl_activate(channel->mpeg_video_h264_p_frame_qp,
			   codec == V4L2_PIX_FMT_H264);
	v4l2_ctrl_activate(channel->mpeg_video_h264_b_frame_qp,
			   codec == V4L2_PIX_FMT_H264);

	v4l2_ctrl_activate(channel->mpeg_video_hevc_profile,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_level,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_tier,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_i_frame_qp,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_max_qp,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_min_qp,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_p_frame_qp,
			   codec == V4L2_PIX_FMT_HEVC);
	v4l2_ctrl_activate(channel->mpeg_video_hevc_b_frame_qp,
			   codec == V4L2_PIX_FMT_HEVC);

	if (codec == V4L2_PIX_FMT_H264)
		channel->log2_max_frame_num = LOG2_MAX_FRAME_NUM;
	channel->temporal_mvp_enable = true;
	channel->dbf_ovr_en = (codec == V4L2_PIX_FMT_H264);
	channel->enable_deblocking_filter_override = (codec == V4L2_PIX_FMT_HEVC);
	channel->enable_reordering = (codec == V4L2_PIX_FMT_HEVC);
	channel->enable_loop_filter_across_tiles = true;
	channel->enable_loop_filter_across_slices = true;

	if (codec == V4L2_PIX_FMT_H264) {
		channel->b_hrz_me_range = 8;
		channel->b_vrt_me_range = 8;
		channel->p_hrz_me_range = 16;
		channel->p_vrt_me_range = 16;
		channel->max_cu_size = ilog2(16);
		channel->min_cu_size = ilog2(8);
		channel->max_tu_size = ilog2(4);
		channel->min_tu_size = ilog2(4);
	} else {
		channel->b_hrz_me_range = 16;
		channel->b_vrt_me_range = 16;
		channel->p_hrz_me_range = 32;
		channel->p_vrt_me_range = 32;
		channel->max_cu_size = ilog2(32);
		channel->min_cu_size = ilog2(8);
		channel->max_tu_size = ilog2(32);
		channel->min_tu_size = ilog2(4);
	}
	channel->max_transfo_depth_intra = 1;
	channel->max_transfo_depth_inter = 1;
}

static void allegro_set_default_params(struct allegro_channel *channel)
{
	channel->width = ALLEGRO_WIDTH_DEFAULT;
	channel->height = ALLEGRO_HEIGHT_DEFAULT;
	channel->stride = round_up(channel->width, 32);
	channel->framerate = ALLEGRO_FRAMERATE_DEFAULT;

	channel->colorspace = V4L2_COLORSPACE_REC709;
	channel->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	channel->quantization = V4L2_QUANTIZATION_DEFAULT;
	channel->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	channel->pixelformat = V4L2_PIX_FMT_NV12;
	channel->sizeimage_raw = channel->stride * channel->height * 3 / 2;

	channel->codec = V4L2_PIX_FMT_H264;
}

static int allegro_queue_setup(struct vb2_queue *vq,
			       unsigned int *nbuffers, unsigned int *nplanes,
			       unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct allegro_channel *channel = vb2_get_drv_priv(vq);
	struct allegro_dev *dev = channel->dev;

	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "%s: queue setup[%s]: nplanes = %d\n",
		 V4L2_TYPE_IS_OUTPUT(vq->type) ? "output" : "capture",
		 *nplanes == 0 ? "REQBUFS" : "CREATE_BUFS", *nplanes);

	if (*nplanes != 0) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
			if (sizes[0] < channel->sizeimage_raw)
				return -EINVAL;
		} else {
			if (sizes[0] < channel->sizeimage_encoded)
				return -EINVAL;
		}
	} else {
		*nplanes = 1;
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			sizes[0] = channel->sizeimage_raw;
		else
			sizes[0] = channel->sizeimage_encoded;
	}

	return 0;
}

static int allegro_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct allegro_channel *channel = vb2_get_drv_priv(vb->vb2_queue);
	struct allegro_dev *dev = channel->dev;

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			v4l2_err(&dev->v4l2_dev,
				 "channel %d: unsupported field\n",
				 channel->mcu_channel_id);
			return -EINVAL;
		}
	}

	return 0;
}

static void allegro_buf_queue(struct vb2_buffer *vb)
{
	struct allegro_channel *channel = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *q = vb->vb2_queue;

	if (V4L2_TYPE_IS_CAPTURE(q->type) &&
	    vb2_is_streaming(q) &&
	    v4l2_m2m_dst_buf_is_last(channel->fh.m2m_ctx)) {
		unsigned int i;

		for (i = 0; i < vb->num_planes; i++)
			vb2_set_plane_payload(vb, i, 0);

		vbuf->field = V4L2_FIELD_NONE;
		vbuf->sequence = channel->csequence++;

		v4l2_m2m_last_buffer_done(channel->fh.m2m_ctx, vbuf);
		allegro_channel_eos_event(channel);
		return;
	}

	v4l2_m2m_buf_queue(channel->fh.m2m_ctx, vbuf);
}

static int allegro_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct allegro_channel *channel = vb2_get_drv_priv(q);
	struct allegro_dev *dev = channel->dev;

	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "%s: start streaming\n",
		 V4L2_TYPE_IS_OUTPUT(q->type) ? "output" : "capture");

	v4l2_m2m_update_start_streaming_state(channel->fh.m2m_ctx, q);

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		channel->osequence = 0;
	else
		channel->csequence = 0;

	return 0;
}

static void allegro_stop_streaming(struct vb2_queue *q)
{
	struct allegro_channel *channel = vb2_get_drv_priv(q);
	struct allegro_dev *dev = channel->dev;
	struct vb2_v4l2_buffer *buffer;
	struct allegro_m2m_buffer *shadow, *tmp;

	v4l2_dbg(2, debug, &dev->v4l2_dev,
		 "%s: stop streaming\n",
		 V4L2_TYPE_IS_OUTPUT(q->type) ? "output" : "capture");

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		mutex_lock(&channel->shadow_list_lock);
		list_for_each_entry_safe(shadow, tmp,
					 &channel->source_shadow_list, head) {
			list_del(&shadow->head);
			v4l2_m2m_buf_done(&shadow->buf.vb, VB2_BUF_STATE_ERROR);
		}
		mutex_unlock(&channel->shadow_list_lock);

		while ((buffer = v4l2_m2m_src_buf_remove(channel->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_ERROR);
	} else {
		mutex_lock(&channel->shadow_list_lock);
		list_for_each_entry_safe(shadow, tmp,
					 &channel->stream_shadow_list, head) {
			list_del(&shadow->head);
			v4l2_m2m_buf_done(&shadow->buf.vb, VB2_BUF_STATE_ERROR);
		}
		mutex_unlock(&channel->shadow_list_lock);

		allegro_destroy_channel(channel);
		while ((buffer = v4l2_m2m_dst_buf_remove(channel->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_ERROR);
	}

	v4l2_m2m_update_stop_streaming_state(channel->fh.m2m_ctx, q);

	if (V4L2_TYPE_IS_OUTPUT(q->type) &&
	    v4l2_m2m_has_stopped(channel->fh.m2m_ctx))
		allegro_channel_eos_event(channel);
}

static const struct vb2_ops allegro_queue_ops = {
	.queue_setup = allegro_queue_setup,
	.buf_prepare = allegro_buf_prepare,
	.buf_queue = allegro_buf_queue,
	.start_streaming = allegro_start_streaming,
	.stop_streaming = allegro_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int allegro_queue_init(void *priv,
			      struct vb2_queue *src_vq,
			      struct vb2_queue *dst_vq)
{
	int err;
	struct allegro_channel *channel = priv;

	src_vq->dev = &channel->dev->plat_dev->dev;
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = channel;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = &allegro_queue_ops;
	src_vq->buf_struct_size = sizeof(struct allegro_m2m_buffer);
	src_vq->lock = &channel->dev->lock;
	err = vb2_queue_init(src_vq);
	if (err)
		return err;

	dst_vq->dev = &channel->dev->plat_dev->dev;
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->drv_priv = channel;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &allegro_queue_ops;
	dst_vq->buf_struct_size = sizeof(struct allegro_m2m_buffer);
	dst_vq->lock = &channel->dev->lock;
	err = vb2_queue_init(dst_vq);
	if (err)
		return err;

	return 0;
}

static int allegro_clamp_qp(struct allegro_channel *channel,
			    struct v4l2_ctrl *ctrl)
{
	struct v4l2_ctrl *next_ctrl;

	if (ctrl->id == V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP)
		next_ctrl = channel->mpeg_video_h264_p_frame_qp;
	else if (ctrl->id == V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP)
		next_ctrl = channel->mpeg_video_h264_b_frame_qp;
	else
		return 0;

	/* Modify range automatically updates the value */
	__v4l2_ctrl_modify_range(next_ctrl, ctrl->val, 51, 1, ctrl->val);

	return allegro_clamp_qp(channel, next_ctrl);
}

static int allegro_clamp_bitrate(struct allegro_channel *channel,
				 struct v4l2_ctrl *ctrl)
{
	struct v4l2_ctrl *ctrl_bitrate = channel->mpeg_video_bitrate;
	struct v4l2_ctrl *ctrl_bitrate_peak = channel->mpeg_video_bitrate_peak;

	if (ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR &&
	    ctrl_bitrate_peak->val < ctrl_bitrate->val)
		ctrl_bitrate_peak->val = ctrl_bitrate->val;

	return 0;
}

static int allegro_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct allegro_channel *channel = container_of(ctrl->handler,
						       struct allegro_channel,
						       ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		allegro_clamp_bitrate(channel, ctrl);
		break;
	case V4L2_CID_USER_ALLEGRO_ENCODER_BUFFER:
		if (!channel->dev->has_encoder_buffer)
			ctrl->val = 0;
		break;
	}

	return 0;
}

static int allegro_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct allegro_channel *channel = container_of(ctrl->handler,
						       struct allegro_channel,
						       ctrl_handler);
	struct allegro_dev *dev = channel->dev;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "s_ctrl: %s = %d\n", v4l2_ctrl_get_name(ctrl->id), ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		channel->frame_rc_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		channel->bitrate = channel->mpeg_video_bitrate->val;
		channel->bitrate_peak = channel->mpeg_video_bitrate_peak->val;
		v4l2_ctrl_activate(channel->mpeg_video_bitrate_peak,
				   ctrl->val == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:
		allegro_clamp_qp(channel, ctrl);
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops allegro_ctrl_ops = {
	.try_ctrl = allegro_try_ctrl,
	.s_ctrl = allegro_s_ctrl,
};

static const struct v4l2_ctrl_config allegro_encoder_buffer_ctrl_config = {
	.id = V4L2_CID_USER_ALLEGRO_ENCODER_BUFFER,
	.name = "Encoder Buffer Enable",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static int allegro_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct allegro_dev *dev = video_get_drvdata(vdev);
	struct allegro_channel *channel = NULL;
	struct v4l2_ctrl_handler *handler;
	u64 mask;
	int ret;
	unsigned int bitrate_max;
	unsigned int bitrate_def;
	unsigned int cpb_size_max;
	unsigned int cpb_size_def;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	v4l2_fh_init(&channel->fh, vdev);

	init_completion(&channel->completion);
	INIT_LIST_HEAD(&channel->source_shadow_list);
	INIT_LIST_HEAD(&channel->stream_shadow_list);
	mutex_init(&channel->shadow_list_lock);

	channel->dev = dev;

	allegro_set_default_params(channel);

	handler = &channel->ctrl_handler;
	v4l2_ctrl_handler_init(handler, 0);
	channel->mpeg_video_h264_profile = v4l2_ctrl_new_std_menu(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE, 0x0,
			V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
	mask = 1 << V4L2_MPEG_VIDEO_H264_LEVEL_1B;
	channel->mpeg_video_h264_level = v4l2_ctrl_new_std_menu(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			V4L2_MPEG_VIDEO_H264_LEVEL_5_1, mask,
			V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
	channel->mpeg_video_h264_i_frame_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP,
				  0, 51, 1, 30);
	channel->mpeg_video_h264_max_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
				  0, 51, 1, 51);
	channel->mpeg_video_h264_min_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
				  0, 51, 1, 0);
	channel->mpeg_video_h264_p_frame_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP,
				  0, 51, 1, 30);
	channel->mpeg_video_h264_b_frame_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP,
				  0, 51, 1, 30);

	channel->mpeg_video_hevc_profile =
		v4l2_ctrl_new_std_menu(handler,
				       &allegro_ctrl_ops,
				       V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
				       V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN, 0x0,
				       V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
	channel->mpeg_video_hevc_level =
		v4l2_ctrl_new_std_menu(handler,
				       &allegro_ctrl_ops,
				       V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
				       V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1, 0x0,
				       V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
	channel->mpeg_video_hevc_tier =
		v4l2_ctrl_new_std_menu(handler,
				       &allegro_ctrl_ops,
				       V4L2_CID_MPEG_VIDEO_HEVC_TIER,
				       V4L2_MPEG_VIDEO_HEVC_TIER_HIGH, 0x0,
				       V4L2_MPEG_VIDEO_HEVC_TIER_MAIN);
	channel->mpeg_video_hevc_i_frame_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP,
				  0, 51, 1, 30);
	channel->mpeg_video_hevc_max_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP,
				  0, 51, 1, 51);
	channel->mpeg_video_hevc_min_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
				  0, 51, 1, 0);
	channel->mpeg_video_hevc_p_frame_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP,
				  0, 51, 1, 30);
	channel->mpeg_video_hevc_b_frame_qp =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP,
				  0, 51, 1, 30);

	channel->mpeg_video_frame_rc_enable =
		v4l2_ctrl_new_std(handler,
				  &allegro_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
				  false, 0x1,
				  true, false);
	channel->mpeg_video_bitrate_mode = v4l2_ctrl_new_std_menu(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
			V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);

	if (channel->codec == V4L2_PIX_FMT_H264) {
		bitrate_max = h264_maximum_bitrate(V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
		bitrate_def = h264_maximum_bitrate(V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
		cpb_size_max = h264_maximum_cpb_size(V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
		cpb_size_def = h264_maximum_cpb_size(V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
	} else {
		bitrate_max = hevc_maximum_bitrate(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
		bitrate_def = hevc_maximum_bitrate(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
		cpb_size_max = hevc_maximum_cpb_size(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
		cpb_size_def = hevc_maximum_cpb_size(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
	}
	channel->mpeg_video_bitrate = v4l2_ctrl_new_std(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_BITRATE,
			0, bitrate_max, 1, bitrate_def);
	channel->mpeg_video_bitrate_peak = v4l2_ctrl_new_std(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
			0, bitrate_max, 1, bitrate_def);
	channel->mpeg_video_cpb_size = v4l2_ctrl_new_std(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE,
			0, cpb_size_max, 1, cpb_size_def);
	channel->mpeg_video_gop_size = v4l2_ctrl_new_std(handler,
			&allegro_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			0, ALLEGRO_GOP_SIZE_MAX,
			1, ALLEGRO_GOP_SIZE_DEFAULT);
	channel->encoder_buffer = v4l2_ctrl_new_custom(handler,
			&allegro_encoder_buffer_ctrl_config, NULL);
	v4l2_ctrl_new_std(handler,
			  &allegro_ctrl_ops,
			  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
			  1, 32,
			  1, 1);
	if (handler->error != 0) {
		ret = handler->error;
		goto error;
	}

	channel->fh.ctrl_handler = handler;

	v4l2_ctrl_cluster(3, &channel->mpeg_video_bitrate_mode);

	v4l2_ctrl_handler_setup(handler);

	channel->mcu_channel_id = -1;
	channel->user_id = -1;

	INIT_LIST_HEAD(&channel->buffers_reference);
	INIT_LIST_HEAD(&channel->buffers_intermediate);

	channel->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, channel,
						allegro_queue_init);

	if (IS_ERR(channel->fh.m2m_ctx)) {
		ret = PTR_ERR(channel->fh.m2m_ctx);
		goto error;
	}

	list_add(&channel->list, &dev->channels);
	file->private_data = &channel->fh;
	v4l2_fh_add(&channel->fh);

	allegro_channel_adjust(channel);

	return 0;

error:
	v4l2_ctrl_handler_free(handler);
	kfree(channel);
	return ret;
}

static int allegro_release(struct file *file)
{
	struct allegro_channel *channel = fh_to_channel(file->private_data);

	v4l2_m2m_ctx_release(channel->fh.m2m_ctx);

	list_del(&channel->list);

	v4l2_ctrl_handler_free(&channel->ctrl_handler);

	v4l2_fh_del(&channel->fh);
	v4l2_fh_exit(&channel->fh);

	kfree(channel);

	return 0;
}

static int allegro_querycap(struct file *file, void *fh,
			    struct v4l2_capability *cap)
{
	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, "Allegro DVT Video Encoder", sizeof(cap->card));

	return 0;
}

static int allegro_enum_fmt_vid(struct file *file, void *fh,
				struct v4l2_fmtdesc *f)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (f->index >= 1)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_NV12;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (f->index >= 2)
			return -EINVAL;
		if (f->index == 0)
			f->pixelformat = V4L2_PIX_FMT_H264;
		if (f->index == 1)
			f->pixelformat = V4L2_PIX_FMT_HEVC;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int allegro_g_fmt_vid_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct allegro_channel *channel = fh_to_channel(fh);

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = channel->width;
	f->fmt.pix.height = channel->height;

	f->fmt.pix.colorspace = channel->colorspace;
	f->fmt.pix.ycbcr_enc = channel->ycbcr_enc;
	f->fmt.pix.quantization = channel->quantization;
	f->fmt.pix.xfer_func = channel->xfer_func;

	f->fmt.pix.pixelformat = channel->codec;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = channel->sizeimage_encoded;

	return 0;
}

static int allegro_try_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	f->fmt.pix.field = V4L2_FIELD_NONE;

	f->fmt.pix.width = clamp_t(__u32, f->fmt.pix.width,
				   ALLEGRO_WIDTH_MIN, ALLEGRO_WIDTH_MAX);
	f->fmt.pix.height = clamp_t(__u32, f->fmt.pix.height,
				    ALLEGRO_HEIGHT_MIN, ALLEGRO_HEIGHT_MAX);

	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_HEVC &&
	    f->fmt.pix.pixelformat != V4L2_PIX_FMT_H264)
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_H264;

	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage =
		estimate_stream_size(f->fmt.pix.width, f->fmt.pix.height);

	return 0;
}

static int allegro_s_fmt_vid_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct allegro_channel *channel = fh_to_channel(fh);
	struct vb2_queue *vq;
	int err;

	err = allegro_try_fmt_vid_cap(file, fh, f);
	if (err)
		return err;

	vq = v4l2_m2m_get_vq(channel->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;
	if (vb2_is_busy(vq))
		return -EBUSY;

	channel->codec = f->fmt.pix.pixelformat;

	allegro_channel_adjust(channel);

	return 0;
}

static int allegro_g_fmt_vid_out(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct allegro_channel *channel = fh_to_channel(fh);

	f->fmt.pix.field = V4L2_FIELD_NONE;

	f->fmt.pix.width = channel->width;
	f->fmt.pix.height = channel->height;

	f->fmt.pix.colorspace = channel->colorspace;
	f->fmt.pix.ycbcr_enc = channel->ycbcr_enc;
	f->fmt.pix.quantization = channel->quantization;
	f->fmt.pix.xfer_func = channel->xfer_func;

	f->fmt.pix.pixelformat = channel->pixelformat;
	f->fmt.pix.bytesperline = channel->stride;
	f->fmt.pix.sizeimage = channel->sizeimage_raw;

	return 0;
}

static int allegro_try_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	f->fmt.pix.field = V4L2_FIELD_NONE;

	/*
	 * The firmware of the Allegro codec handles the padding internally
	 * and expects the visual frame size when configuring a channel.
	 * Therefore, unlike other encoder drivers, this driver does not round
	 * up the width and height to macroblock alignment and does not
	 * implement the selection api.
	 */
	f->fmt.pix.width = clamp_t(__u32, f->fmt.pix.width,
				   ALLEGRO_WIDTH_MIN, ALLEGRO_WIDTH_MAX);
	f->fmt.pix.height = clamp_t(__u32, f->fmt.pix.height,
				    ALLEGRO_HEIGHT_MIN, ALLEGRO_HEIGHT_MAX);

	f->fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 32);
	f->fmt.pix.sizeimage =
		f->fmt.pix.bytesperline * f->fmt.pix.height * 3 / 2;

	return 0;
}

static int allegro_s_fmt_vid_out(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct allegro_channel *channel = fh_to_channel(fh);
	int err;

	err = allegro_try_fmt_vid_out(file, fh, f);
	if (err)
		return err;

	channel->width = f->fmt.pix.width;
	channel->height = f->fmt.pix.height;
	channel->stride = f->fmt.pix.bytesperline;
	channel->sizeimage_raw = f->fmt.pix.sizeimage;

	channel->colorspace = f->fmt.pix.colorspace;
	channel->ycbcr_enc = f->fmt.pix.ycbcr_enc;
	channel->quantization = f->fmt.pix.quantization;
	channel->xfer_func = f->fmt.pix.xfer_func;

	allegro_channel_adjust(channel);

	return 0;
}

static int allegro_channel_cmd_stop(struct allegro_channel *channel)
{
	if (v4l2_m2m_has_stopped(channel->fh.m2m_ctx))
		allegro_channel_eos_event(channel);

	return 0;
}

static int allegro_channel_cmd_start(struct allegro_channel *channel)
{
	if (v4l2_m2m_has_stopped(channel->fh.m2m_ctx))
		vb2_clear_last_buffer_dequeued(&channel->fh.m2m_ctx->cap_q_ctx.q);

	return 0;
}

static int allegro_encoder_cmd(struct file *file, void *fh,
			       struct v4l2_encoder_cmd *cmd)
{
	struct allegro_channel *channel = fh_to_channel(fh);
	int err;

	err = v4l2_m2m_ioctl_try_encoder_cmd(file, fh, cmd);
	if (err)
		return err;

	err = v4l2_m2m_ioctl_encoder_cmd(file, fh, cmd);
	if (err)
		return err;

	if (cmd->cmd == V4L2_ENC_CMD_STOP)
		err = allegro_channel_cmd_stop(channel);

	if (cmd->cmd == V4L2_ENC_CMD_START)
		err = allegro_channel_cmd_start(channel);

	return err;
}

static int allegro_enum_framesizes(struct file *file, void *fh,
				   struct v4l2_frmsizeenum *fsize)
{
	switch (fsize->pixel_format) {
	case V4L2_PIX_FMT_HEVC:
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_NV12:
		break;
	default:
		return -EINVAL;
	}

	if (fsize->index)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = ALLEGRO_WIDTH_MIN;
	fsize->stepwise.max_width = ALLEGRO_WIDTH_MAX;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.min_height = ALLEGRO_HEIGHT_MIN;
	fsize->stepwise.max_height = ALLEGRO_HEIGHT_MAX;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int allegro_ioctl_streamon(struct file *file, void *priv,
				  enum v4l2_buf_type type)
{
	struct v4l2_fh *fh = file->private_data;
	struct allegro_channel *channel = fh_to_channel(fh);
	int err;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		err = allegro_create_channel(channel);
		if (err)
			return err;
	}

	return v4l2_m2m_streamon(file, fh->m2m_ctx, type);
}

static int allegro_g_parm(struct file *file, void *fh,
			  struct v4l2_streamparm *a)
{
	struct allegro_channel *channel = fh_to_channel(fh);
	struct v4l2_fract *timeperframe;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	timeperframe = &a->parm.output.timeperframe;
	timeperframe->numerator = channel->framerate.denominator;
	timeperframe->denominator = channel->framerate.numerator;

	return 0;
}

static int allegro_s_parm(struct file *file, void *fh,
			  struct v4l2_streamparm *a)
{
	struct allegro_channel *channel = fh_to_channel(fh);
	struct v4l2_fract *timeperframe;
	int div;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	timeperframe = &a->parm.output.timeperframe;

	if (timeperframe->numerator == 0 || timeperframe->denominator == 0)
		return allegro_g_parm(file, fh, a);

	div = gcd(timeperframe->denominator, timeperframe->numerator);
	channel->framerate.numerator = timeperframe->denominator / div;
	channel->framerate.denominator = timeperframe->numerator / div;

	return 0;
}

static int allegro_subscribe_event(struct v4l2_fh *fh,
				   const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static const struct v4l2_ioctl_ops allegro_ioctl_ops = {
	.vidioc_querycap = allegro_querycap,
	.vidioc_enum_fmt_vid_cap = allegro_enum_fmt_vid,
	.vidioc_enum_fmt_vid_out = allegro_enum_fmt_vid,
	.vidioc_g_fmt_vid_cap = allegro_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = allegro_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = allegro_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_out = allegro_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out = allegro_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out = allegro_s_fmt_vid_out,

	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,

	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,

	.vidioc_streamon = allegro_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd = allegro_encoder_cmd,
	.vidioc_enum_framesizes = allegro_enum_framesizes,

	.vidioc_g_parm		= allegro_g_parm,
	.vidioc_s_parm		= allegro_s_parm,

	.vidioc_subscribe_event = allegro_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations allegro_fops = {
	.owner = THIS_MODULE,
	.open = allegro_open,
	.release = allegro_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static int allegro_register_device(struct allegro_dev *dev)
{
	struct video_device *video_dev = &dev->video_dev;

	strscpy(video_dev->name, "allegro", sizeof(video_dev->name));
	video_dev->fops = &allegro_fops;
	video_dev->ioctl_ops = &allegro_ioctl_ops;
	video_dev->release = video_device_release_empty;
	video_dev->lock = &dev->lock;
	video_dev->v4l2_dev = &dev->v4l2_dev;
	video_dev->vfl_dir = VFL_DIR_M2M;
	video_dev->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	video_set_drvdata(video_dev, dev);

	return video_register_device(video_dev, VFL_TYPE_VIDEO, 0);
}

static void allegro_device_run(void *priv)
{
	struct allegro_channel *channel = priv;
	struct allegro_dev *dev = channel->dev;
	struct vb2_v4l2_buffer *src_buf;
	struct vb2_v4l2_buffer *dst_buf;
	dma_addr_t src_y;
	dma_addr_t src_uv;
	dma_addr_t dst_addr;
	unsigned long dst_size;
	u64 src_handle;
	u64 dst_handle;

	dst_buf = v4l2_m2m_dst_buf_remove(channel->fh.m2m_ctx);
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	dst_size = vb2_plane_size(&dst_buf->vb2_buf, 0);
	dst_handle = allegro_put_buffer(channel, &channel->stream_shadow_list,
					dst_buf);
	allegro_mcu_send_put_stream_buffer(dev, channel, dst_addr, dst_size,
					   dst_handle);

	src_buf = v4l2_m2m_src_buf_remove(channel->fh.m2m_ctx);
	src_buf->sequence = channel->osequence++;
	src_y = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	src_uv = src_y + (channel->stride * channel->height);
	src_handle = allegro_put_buffer(channel, &channel->source_shadow_list,
					src_buf);
	allegro_mcu_send_encode_frame(dev, channel, src_y, src_uv, src_handle);

	v4l2_m2m_job_finish(dev->m2m_dev, channel->fh.m2m_ctx);
}

static const struct v4l2_m2m_ops allegro_m2m_ops = {
	.device_run = allegro_device_run,
};

static int allegro_mcu_hw_init(struct allegro_dev *dev,
			       const struct fw_info *info)
{
	int err;

	dev->mbox_command = allegro_mbox_init(dev, info->mailbox_cmd,
					      info->mailbox_size);
	dev->mbox_status = allegro_mbox_init(dev, info->mailbox_status,
					     info->mailbox_size);
	if (IS_ERR(dev->mbox_command) || IS_ERR(dev->mbox_status)) {
		v4l2_err(&dev->v4l2_dev,
			 "failed to initialize mailboxes\n");
		return -EIO;
	}

	err = allegro_encoder_buffer_init(dev, &dev->encoder_buffer);
	dev->has_encoder_buffer = (err == 0);
	if (!dev->has_encoder_buffer)
		v4l2_info(&dev->v4l2_dev, "encoder buffer not available\n");

	allegro_mcu_enable_interrupts(dev);

	/* The mcu sends INIT after reset. */
	allegro_mcu_start(dev);
	err = allegro_mcu_wait_for_init_timeout(dev, 5000);
	if (err < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "mcu did not send INIT after reset\n");
		err = -EIO;
		goto err_disable_interrupts;
	}

	err = allegro_alloc_buffer(dev, &dev->suballocator,
				   info->suballocator_size);
	if (err) {
		v4l2_err(&dev->v4l2_dev,
			 "failed to allocate %zu bytes for suballocator\n",
			 info->suballocator_size);
		goto err_reset_mcu;
	}

	allegro_mcu_send_init(dev, dev->suballocator.paddr,
			      dev->suballocator.size);
	err = allegro_mcu_wait_for_init_timeout(dev, 5000);
	if (err < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "mcu failed to configure sub-allocator\n");
		err = -EIO;
		goto err_free_suballocator;
	}

	return 0;

err_free_suballocator:
	allegro_free_buffer(dev, &dev->suballocator);
err_reset_mcu:
	allegro_mcu_reset(dev);
err_disable_interrupts:
	allegro_mcu_disable_interrupts(dev);

	return err;
}

static int allegro_mcu_hw_deinit(struct allegro_dev *dev)
{
	int err;

	err = allegro_mcu_reset(dev);
	if (err)
		v4l2_warn(&dev->v4l2_dev,
			  "mcu failed to enter sleep state\n");

	err = allegro_mcu_disable_interrupts(dev);
	if (err)
		v4l2_warn(&dev->v4l2_dev,
			  "failed to disable interrupts\n");

	allegro_free_buffer(dev, &dev->suballocator);

	return 0;
}

static void allegro_fw_callback(const struct firmware *fw, void *context)
{
	struct allegro_dev *dev = context;
	const char *fw_codec_name = "al5e.fw";
	const struct firmware *fw_codec;
	int err;

	if (!fw)
		return;

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "requesting codec firmware '%s'\n", fw_codec_name);
	err = request_firmware(&fw_codec, fw_codec_name, &dev->plat_dev->dev);
	if (err)
		goto err_release_firmware;

	dev->fw_info = allegro_get_firmware_info(dev, fw, fw_codec);
	if (!dev->fw_info) {
		v4l2_err(&dev->v4l2_dev, "firmware is not supported\n");
		goto err_release_firmware_codec;
	}

	v4l2_info(&dev->v4l2_dev,
		  "using mcu firmware version '%s'\n", dev->fw_info->version);

	pm_runtime_enable(&dev->plat_dev->dev);
	err = pm_runtime_resume_and_get(&dev->plat_dev->dev);
	if (err)
		goto err_release_firmware_codec;

	/* Ensure that the mcu is sleeping at the reset vector */
	err = allegro_mcu_reset(dev);
	if (err) {
		v4l2_err(&dev->v4l2_dev, "failed to reset mcu\n");
		goto err_suspend;
	}

	allegro_copy_firmware(dev, fw->data, fw->size);
	allegro_copy_fw_codec(dev, fw_codec->data, fw_codec->size);

	err = allegro_mcu_hw_init(dev, dev->fw_info);
	if (err) {
		v4l2_err(&dev->v4l2_dev, "failed to initialize mcu\n");
		goto err_free_fw_codec;
	}

	dev->m2m_dev = v4l2_m2m_init(&allegro_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "failed to init mem2mem device\n");
		goto err_mcu_hw_deinit;
	}

	err = allegro_register_device(dev);
	if (err) {
		v4l2_err(&dev->v4l2_dev, "failed to register video device\n");
		goto err_m2m_release;
	}

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "allegro codec registered as /dev/video%d\n",
		 dev->video_dev.num);

	dev->initialized = true;

	release_firmware(fw_codec);
	release_firmware(fw);

	return;

err_m2m_release:
	v4l2_m2m_release(dev->m2m_dev);
	dev->m2m_dev = NULL;
err_mcu_hw_deinit:
	allegro_mcu_hw_deinit(dev);
err_free_fw_codec:
	allegro_free_fw_codec(dev);
err_suspend:
	pm_runtime_put(&dev->plat_dev->dev);
	pm_runtime_disable(&dev->plat_dev->dev);
err_release_firmware_codec:
	release_firmware(fw_codec);
err_release_firmware:
	release_firmware(fw);
}

static int allegro_firmware_request_nowait(struct allegro_dev *dev)
{
	const char *fw = "al5e_b.fw";

	v4l2_dbg(1, debug, &dev->v4l2_dev,
		 "requesting firmware '%s'\n", fw);
	return request_firmware_nowait(THIS_MODULE, true, fw,
				       &dev->plat_dev->dev, GFP_KERNEL, dev,
				       allegro_fw_callback);
}

static int allegro_probe(struct platform_device *pdev)
{
	struct allegro_dev *dev;
	struct resource *res, *sram_res;
	int ret;
	int irq;
	void __iomem *regs, *sram_regs;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->plat_dev = pdev;
	init_completion(&dev->init_complete);
	INIT_LIST_HEAD(&dev->channels);

	mutex_init(&dev->lock);

	dev->initialized = false;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		dev_err(&pdev->dev,
			"regs resource missing from device tree\n");
		return -EINVAL;
	}
	regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!regs) {
		dev_err(&pdev->dev, "failed to map registers\n");
		return -ENOMEM;
	}
	dev->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &allegro_regmap_config);
	if (IS_ERR(dev->regmap)) {
		dev_err(&pdev->dev, "failed to init regmap\n");
		return PTR_ERR(dev->regmap);
	}

	sram_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	if (!sram_res) {
		dev_err(&pdev->dev,
			"sram resource missing from device tree\n");
		return -EINVAL;
	}
	sram_regs = devm_ioremap(&pdev->dev,
				 sram_res->start,
				 resource_size(sram_res));
	if (!sram_regs) {
		dev_err(&pdev->dev, "failed to map sram\n");
		return -ENOMEM;
	}
	dev->sram = devm_regmap_init_mmio(&pdev->dev, sram_regs,
					  &allegro_sram_config);
	if (IS_ERR(dev->sram)) {
		dev_err(&pdev->dev, "failed to init sram\n");
		return PTR_ERR(dev->sram);
	}

	dev->settings = syscon_regmap_lookup_by_compatible("xlnx,vcu-settings");
	if (IS_ERR(dev->settings))
		dev_warn(&pdev->dev, "failed to open settings\n");

	dev->clk_core = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(dev->clk_core))
		return PTR_ERR(dev->clk_core);

	dev->clk_mcu = devm_clk_get(&pdev->dev, "mcu_clk");
	if (IS_ERR(dev->clk_mcu))
		return PTR_ERR(dev->clk_mcu);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_threaded_irq(&pdev->dev, irq,
					allegro_hardirq,
					allegro_irq_thread,
					IRQF_SHARED, dev_name(&pdev->dev), dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dev);

	ret = allegro_firmware_request_nowait(dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "failed to request firmware: %d\n", ret);
		return ret;
	}

	return 0;
}

static void allegro_remove(struct platform_device *pdev)
{
	struct allegro_dev *dev = platform_get_drvdata(pdev);

	if (dev->initialized) {
		video_unregister_device(&dev->video_dev);
		if (dev->m2m_dev)
			v4l2_m2m_release(dev->m2m_dev);
		allegro_mcu_hw_deinit(dev);
		allegro_free_fw_codec(dev);
	}

	pm_runtime_put(&dev->plat_dev->dev);
	pm_runtime_disable(&dev->plat_dev->dev);

	v4l2_device_unregister(&dev->v4l2_dev);
}

static int allegro_runtime_resume(struct device *device)
{
	struct allegro_dev *dev = dev_get_drvdata(device);
	struct regmap *settings = dev->settings;
	unsigned int clk_mcu;
	unsigned int clk_core;
	int err;

	if (!settings)
		return -EINVAL;

#define MHZ_TO_HZ(freq) ((freq) * 1000 * 1000)

	err = regmap_read(settings, VCU_CORE_CLK, &clk_core);
	if (err < 0)
		return err;
	err = clk_set_rate(dev->clk_core, MHZ_TO_HZ(clk_core));
	if (err < 0)
		return err;
	err = clk_prepare_enable(dev->clk_core);
	if (err)
		return err;

	err = regmap_read(settings, VCU_MCU_CLK, &clk_mcu);
	if (err < 0)
		goto disable_clk_core;
	err = clk_set_rate(dev->clk_mcu, MHZ_TO_HZ(clk_mcu));
	if (err < 0)
		goto disable_clk_core;
	err = clk_prepare_enable(dev->clk_mcu);
	if (err)
		goto disable_clk_core;

#undef MHZ_TO_HZ

	return 0;

disable_clk_core:
	clk_disable_unprepare(dev->clk_core);

	return err;
}

static int allegro_runtime_suspend(struct device *device)
{
	struct allegro_dev *dev = dev_get_drvdata(device);

	clk_disable_unprepare(dev->clk_mcu);
	clk_disable_unprepare(dev->clk_core);

	return 0;
}

static const struct of_device_id allegro_dt_ids[] = {
	{ .compatible = "allegro,al5e-1.1" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, allegro_dt_ids);

static const struct dev_pm_ops allegro_pm_ops = {
	.runtime_resume = allegro_runtime_resume,
	.runtime_suspend = allegro_runtime_suspend,
};

static struct platform_driver allegro_driver = {
	.probe = allegro_probe,
	.remove_new = allegro_remove,
	.driver = {
		.name = "allegro",
		.of_match_table = allegro_dt_ids,
		.pm = &allegro_pm_ops,
	},
};

module_platform_driver(allegro_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Tretter <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Allegro DVT encoder driver");
