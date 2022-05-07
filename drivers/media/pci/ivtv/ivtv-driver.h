/*
    ivtv driver internal defines and structures
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef IVTV_DRIVER_H
#define IVTV_DRIVER_H

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* Internal header for ivtv project:
 * Driver for the cx23415/6 chip.
 * Author: Kevin Thayer (nufan_wfk at yahoo.com)
 * License: GPL
 *
 * -----
 * MPG600/MPG160 support by  T.Adachi <tadachi@tadachi-net.com>
 *                      and Takeru KOMORIYA<komoriya@paken.org>
 *
 * AVerMedia M179 GPIO info by Chris Pinkham <cpinkham@bc2va.org>
 *                using information provided by Jiun-Kuei Jung @ AVerMedia.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/list.h>
#include <linux/unistd.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>

#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/tuner.h>
#include <media/drv-intf/cx2341x.h>
#include <media/i2c/ir-kbd-i2c.h>

#include <linux/ivtv.h>

/* Memory layout */
#define IVTV_ENCODER_OFFSET	0x00000000
#define IVTV_ENCODER_SIZE	0x00800000	/* Total size is 0x01000000, but only first half is used */
#define IVTV_DECODER_OFFSET	0x01000000
#define IVTV_DECODER_SIZE	0x00800000	/* Total size is 0x01000000, but only first half is used */
#define IVTV_REG_OFFSET		0x02000000
#define IVTV_REG_SIZE		0x00010000

/* Maximum ivtv driver instances. Some people have a huge number of
   capture cards, so set this to a high value. */
#define IVTV_MAX_CARDS 32

#define IVTV_ENC_STREAM_TYPE_MPG  0
#define IVTV_ENC_STREAM_TYPE_YUV  1
#define IVTV_ENC_STREAM_TYPE_VBI  2
#define IVTV_ENC_STREAM_TYPE_PCM  3
#define IVTV_ENC_STREAM_TYPE_RAD  4
#define IVTV_DEC_STREAM_TYPE_MPG  5
#define IVTV_DEC_STREAM_TYPE_VBI  6
#define IVTV_DEC_STREAM_TYPE_VOUT 7
#define IVTV_DEC_STREAM_TYPE_YUV  8
#define IVTV_MAX_STREAMS	  9

#define IVTV_DMA_SG_OSD_ENT	(2883584/PAGE_SIZE)	/* sg entities */

/* DMA Registers */
#define IVTV_REG_DMAXFER	(0x0000)
#define IVTV_REG_DMASTATUS	(0x0004)
#define IVTV_REG_DECDMAADDR	(0x0008)
#define IVTV_REG_ENCDMAADDR	(0x000c)
#define IVTV_REG_DMACONTROL	(0x0010)
#define IVTV_REG_IRQSTATUS	(0x0040)
#define IVTV_REG_IRQMASK	(0x0048)

/* Setup Registers */
#define IVTV_REG_ENC_SDRAM_REFRESH	(0x07F8)
#define IVTV_REG_ENC_SDRAM_PRECHARGE	(0x07FC)
#define IVTV_REG_DEC_SDRAM_REFRESH	(0x08F8)
#define IVTV_REG_DEC_SDRAM_PRECHARGE	(0x08FC)
#define IVTV_REG_VDM			(0x2800)
#define IVTV_REG_AO			(0x2D00)
#define IVTV_REG_BYTEFLUSH		(0x2D24)
#define IVTV_REG_SPU			(0x9050)
#define IVTV_REG_HW_BLOCKS		(0x9054)
#define IVTV_REG_VPU			(0x9058)
#define IVTV_REG_APU			(0xA064)

/* Other registers */
#define IVTV_REG_DEC_LINE_FIELD		(0x28C0)

/* debugging */
extern int ivtv_debug;
#ifdef CONFIG_VIDEO_ADV_DEBUG
extern int ivtv_fw_debug;
#endif

#define IVTV_DBGFLG_WARN    (1 << 0)
#define IVTV_DBGFLG_INFO    (1 << 1)
#define IVTV_DBGFLG_MB      (1 << 2)
#define IVTV_DBGFLG_IOCTL   (1 << 3)
#define IVTV_DBGFLG_FILE    (1 << 4)
#define IVTV_DBGFLG_DMA     (1 << 5)
#define IVTV_DBGFLG_IRQ     (1 << 6)
#define IVTV_DBGFLG_DEC     (1 << 7)
#define IVTV_DBGFLG_YUV     (1 << 8)
#define IVTV_DBGFLG_I2C     (1 << 9)
/* Flag to turn on high volume debugging */
#define IVTV_DBGFLG_HIGHVOL (1 << 10)

#define IVTV_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & ivtv_debug) \
			v4l2_info(&itv->v4l2_dev, " " type ": " fmt , ##args);	\
	} while (0)
#define IVTV_DEBUG_WARN(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_WARN,  "warn",  fmt , ## args)
#define IVTV_DEBUG_INFO(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_INFO,  "info",  fmt , ## args)
#define IVTV_DEBUG_MB(fmt, args...)    IVTV_DEBUG(IVTV_DBGFLG_MB,    "mb",    fmt , ## args)
#define IVTV_DEBUG_DMA(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_DMA,   "dma",   fmt , ## args)
#define IVTV_DEBUG_IOCTL(fmt, args...) IVTV_DEBUG(IVTV_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define IVTV_DEBUG_FILE(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_FILE,  "file",  fmt , ## args)
#define IVTV_DEBUG_I2C(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_I2C,   "i2c",   fmt , ## args)
#define IVTV_DEBUG_IRQ(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_IRQ,   "irq",   fmt , ## args)
#define IVTV_DEBUG_DEC(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_DEC,   "dec",   fmt , ## args)
#define IVTV_DEBUG_YUV(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_YUV,   "yuv",   fmt , ## args)

#define IVTV_DEBUG_HIGH_VOL(x, type, fmt, args...) \
	do { \
		if (((x) & ivtv_debug) && (ivtv_debug & IVTV_DBGFLG_HIGHVOL))	\
			v4l2_info(&itv->v4l2_dev, " " type ": " fmt , ##args);	\
	} while (0)
#define IVTV_DEBUG_HI_WARN(fmt, args...)  IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_WARN,  "warn",  fmt , ## args)
#define IVTV_DEBUG_HI_INFO(fmt, args...)  IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_INFO,  "info",  fmt , ## args)
#define IVTV_DEBUG_HI_MB(fmt, args...)    IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_MB,    "mb",    fmt , ## args)
#define IVTV_DEBUG_HI_DMA(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_DMA,   "dma",   fmt , ## args)
#define IVTV_DEBUG_HI_IOCTL(fmt, args...) IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define IVTV_DEBUG_HI_FILE(fmt, args...)  IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_FILE,  "file",  fmt , ## args)
#define IVTV_DEBUG_HI_I2C(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_I2C,   "i2c",   fmt , ## args)
#define IVTV_DEBUG_HI_IRQ(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_IRQ,   "irq",   fmt , ## args)
#define IVTV_DEBUG_HI_DEC(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_DEC,   "dec",   fmt , ## args)
#define IVTV_DEBUG_HI_YUV(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_YUV,   "yuv",   fmt , ## args)

/* Standard kernel messages */
#define IVTV_ERR(fmt, args...)      v4l2_err(&itv->v4l2_dev, fmt , ## args)
#define IVTV_WARN(fmt, args...)     v4l2_warn(&itv->v4l2_dev, fmt , ## args)
#define IVTV_INFO(fmt, args...)     v4l2_info(&itv->v4l2_dev, fmt , ## args)

/* output modes (cx23415 only) */
#define OUT_NONE        0
#define OUT_MPG         1
#define OUT_YUV         2
#define OUT_UDMA_YUV    3
#define OUT_PASSTHROUGH 4

#define IVTV_MAX_PGM_INDEX (400)

/* Default I2C SCL period in microseconds */
#define IVTV_DEFAULT_I2C_CLOCK_PERIOD	20

struct ivtv_options {
	int kilobytes[IVTV_MAX_STREAMS];        /* size in kilobytes of each stream */
	int cardtype;				/* force card type on load */
	int tuner;				/* set tuner on load */
	int radio;				/* enable/disable radio */
	int newi2c;				/* new I2C algorithm */
	int i2c_clock_period;			/* period of SCL for I2C bus */
};

/* ivtv-specific mailbox template */
struct ivtv_mailbox {
	u32 flags;
	u32 cmd;
	u32 retval;
	u32 timeout;
	u32 data[CX2341X_MBOX_MAX_DATA];
};

struct ivtv_api_cache {
	unsigned long last_jiffies;		/* when last command was issued */
	u32 data[CX2341X_MBOX_MAX_DATA];	/* last sent api data */
};

struct ivtv_mailbox_data {
	volatile struct ivtv_mailbox __iomem *mbox;
	/* Bits 0-2 are for the encoder mailboxes, 0-1 are for the decoder mailboxes.
	   If the bit is set, then the corresponding mailbox is in use by the driver. */
	unsigned long busy;
	u8 max_mbox;
};

/* per-buffer bit flags */
#define IVTV_F_B_NEED_BUF_SWAP  (1 << 0)	/* this buffer should be byte swapped */

/* per-stream, s_flags */
#define IVTV_F_S_DMA_PENDING	0	/* this stream has pending DMA */
#define IVTV_F_S_DMA_HAS_VBI	1       /* the current DMA request also requests VBI data */
#define IVTV_F_S_NEEDS_DATA	2	/* this decoding stream needs more data */

#define IVTV_F_S_CLAIMED	3	/* this stream is claimed */
#define IVTV_F_S_STREAMING      4	/* the fw is decoding/encoding this stream */
#define IVTV_F_S_INTERNAL_USE	5	/* this stream is used internally (sliced VBI processing) */
#define IVTV_F_S_PASSTHROUGH	6	/* this stream is in passthrough mode */
#define IVTV_F_S_STREAMOFF	7	/* signal end of stream EOS */
#define IVTV_F_S_APPL_IO        8	/* this stream is used read/written by an application */

#define IVTV_F_S_PIO_PENDING	9	/* this stream has pending PIO */
#define IVTV_F_S_PIO_HAS_VBI	1       /* the current PIO request also requests VBI data */

/* per-ivtv, i_flags */
#define IVTV_F_I_DMA		   0	/* DMA in progress */
#define IVTV_F_I_UDMA		   1	/* UDMA in progress */
#define IVTV_F_I_UDMA_PENDING	   2	/* UDMA pending */
#define IVTV_F_I_SPEED_CHANGE	   3	/* a speed change is in progress */
#define IVTV_F_I_EOS		   4	/* end of encoder stream reached */
#define IVTV_F_I_RADIO_USER	   5	/* the radio tuner is selected */
#define IVTV_F_I_DIG_RST	   6	/* reset digitizer */
#define IVTV_F_I_DEC_YUV	   7	/* YUV instead of MPG is being decoded */
#define IVTV_F_I_UPDATE_CC	   9	/* CC should be updated */
#define IVTV_F_I_UPDATE_WSS	   10	/* WSS should be updated */
#define IVTV_F_I_UPDATE_VPS	   11	/* VPS should be updated */
#define IVTV_F_I_DECODING_YUV	   12	/* this stream is YUV frame decoding */
#define IVTV_F_I_ENC_PAUSED	   13	/* the encoder is paused */
#define IVTV_F_I_VALID_DEC_TIMINGS 14	/* last_dec_timing is valid */
#define IVTV_F_I_HAVE_WORK	   15	/* used in the interrupt handler: there is work to be done */
#define IVTV_F_I_WORK_HANDLER_VBI  16	/* there is work to be done for VBI */
#define IVTV_F_I_WORK_HANDLER_YUV  17	/* there is work to be done for YUV */
#define IVTV_F_I_WORK_HANDLER_PIO  18	/* there is work to be done for PIO */
#define IVTV_F_I_PIO		   19	/* PIO in progress */
#define IVTV_F_I_DEC_PAUSED	   20	/* the decoder is paused */
#define IVTV_F_I_INITED		   21	/* set after first open */
#define IVTV_F_I_FAILED		   22	/* set if first open failed */
#define IVTV_F_I_WORK_HANDLER_PCM  23	/* there is work to be done for PCM */

/* Event notifications */
#define IVTV_F_I_EV_DEC_STOPPED	   28	/* decoder stopped event */
#define IVTV_F_I_EV_VSYNC	   29	/* VSYNC event */
#define IVTV_F_I_EV_VSYNC_FIELD    30	/* VSYNC event field (0 = first, 1 = second field) */
#define IVTV_F_I_EV_VSYNC_ENABLED  31	/* VSYNC event enabled */

/* Scatter-Gather array element, used in DMA transfers */
struct ivtv_sg_element {
	__le32 src;
	__le32 dst;
	__le32 size;
};

struct ivtv_sg_host_element {
	u32 src;
	u32 dst;
	u32 size;
};

struct ivtv_user_dma {
	struct mutex lock;
	int page_count;
	struct page *map[IVTV_DMA_SG_OSD_ENT];
	/* Needed when dealing with highmem userspace buffers */
	struct page *bouncemap[IVTV_DMA_SG_OSD_ENT];

	/* Base Dev SG Array for cx23415/6 */
	struct ivtv_sg_element SGarray[IVTV_DMA_SG_OSD_ENT];
	dma_addr_t SG_handle;
	int SG_length;

	/* SG List of Buffers */
	struct scatterlist SGlist[IVTV_DMA_SG_OSD_ENT];
};

struct ivtv_dma_page_info {
	unsigned long uaddr;
	unsigned long first;
	unsigned long last;
	unsigned int offset;
	unsigned int tail;
	int page_count;
};

struct ivtv_buffer {
	struct list_head list;
	dma_addr_t dma_handle;
	unsigned short b_flags;
	unsigned short dma_xfer_cnt;
	char *buf;
	u32 bytesused;
	u32 readpos;
};

struct ivtv_queue {
	struct list_head list;          /* the list of buffers in this queue */
	u32 buffers;                    /* number of buffers in this queue */
	u32 length;                     /* total number of bytes of available buffer space */
	u32 bytesused;                  /* total number of bytes used in this queue */
};

struct ivtv;				/* forward reference */

struct ivtv_stream {
	/* These first four fields are always set, even if the stream
	   is not actually created. */
	struct video_device vdev;	/* vdev.v4l2_dev is NULL if there is no device */
	struct ivtv *itv;		/* for ease of use */
	const char *name;		/* name of the stream */
	int type;			/* stream type */
	u32 caps;			/* V4L2 capabilities */

	struct v4l2_fh *fh;		/* pointer to the streaming filehandle */
	spinlock_t qlock;		/* locks access to the queues */
	unsigned long s_flags;		/* status flags, see above */
	int dma;			/* can be PCI_DMA_TODEVICE, PCI_DMA_FROMDEVICE or PCI_DMA_NONE */
	u32 pending_offset;
	u32 pending_backup;
	u64 pending_pts;

	u32 dma_offset;
	u32 dma_backup;
	u64 dma_pts;

	int subtype;
	wait_queue_head_t waitq;
	u32 dma_last_offset;

	/* Buffer Stats */
	u32 buffers;
	u32 buf_size;
	u32 buffers_stolen;

	/* Buffer Queues */
	struct ivtv_queue q_free;	/* free buffers */
	struct ivtv_queue q_full;	/* full buffers */
	struct ivtv_queue q_io;		/* waiting for I/O */
	struct ivtv_queue q_dma;	/* waiting for DMA */
	struct ivtv_queue q_predma;	/* waiting for DMA */

	/* DMA xfer counter, buffers belonging to the same DMA
	   xfer will have the same dma_xfer_cnt. */
	u16 dma_xfer_cnt;

	/* Base Dev SG Array for cx23415/6 */
	struct ivtv_sg_host_element *sg_pending;
	struct ivtv_sg_host_element *sg_processing;
	struct ivtv_sg_element *sg_dma;
	dma_addr_t sg_handle;
	int sg_pending_size;
	int sg_processing_size;
	int sg_processed;

	/* SG List of Buffers */
	struct scatterlist *SGlist;
};

struct ivtv_open_id {
	struct v4l2_fh fh;
	int type;                       /* stream type */
	int yuv_frames;                 /* 1: started OUT_UDMA_YUV output mode */
	struct ivtv *itv;
};

static inline struct ivtv_open_id *fh2id(struct v4l2_fh *fh)
{
	return container_of(fh, struct ivtv_open_id, fh);
}

struct yuv_frame_info
{
	u32 update;
	s32 src_x;
	s32 src_y;
	u32 src_w;
	u32 src_h;
	s32 dst_x;
	s32 dst_y;
	u32 dst_w;
	u32 dst_h;
	s32 pan_x;
	s32 pan_y;
	u32 vis_w;
	u32 vis_h;
	u32 interlaced_y;
	u32 interlaced_uv;
	s32 tru_x;
	u32 tru_w;
	u32 tru_h;
	u32 offset_y;
	s32 lace_mode;
	u32 sync_field;
	u32 delay;
	u32 interlaced;
};

#define IVTV_YUV_MODE_INTERLACED	0x00
#define IVTV_YUV_MODE_PROGRESSIVE	0x01
#define IVTV_YUV_MODE_AUTO		0x02
#define IVTV_YUV_MODE_MASK		0x03

#define IVTV_YUV_SYNC_EVEN		0x00
#define IVTV_YUV_SYNC_ODD		0x04
#define IVTV_YUV_SYNC_MASK		0x04

#define IVTV_YUV_BUFFERS 8

struct yuv_playback_info
{
	u32 reg_2834;
	u32 reg_2838;
	u32 reg_283c;
	u32 reg_2840;
	u32 reg_2844;
	u32 reg_2848;
	u32 reg_2854;
	u32 reg_285c;
	u32 reg_2864;

	u32 reg_2870;
	u32 reg_2874;
	u32 reg_2890;
	u32 reg_2898;
	u32 reg_289c;

	u32 reg_2918;
	u32 reg_291c;
	u32 reg_2920;
	u32 reg_2924;
	u32 reg_2928;
	u32 reg_292c;
	u32 reg_2930;

	u32 reg_2934;

	u32 reg_2938;
	u32 reg_293c;
	u32 reg_2940;
	u32 reg_2944;
	u32 reg_2948;
	u32 reg_294c;
	u32 reg_2950;
	u32 reg_2954;
	u32 reg_2958;
	u32 reg_295c;
	u32 reg_2960;
	u32 reg_2964;
	u32 reg_2968;
	u32 reg_296c;

	u32 reg_2970;

	int v_filter_1;
	int v_filter_2;
	int h_filter;

	u8 track_osd; /* Should yuv output track the OSD size & position */

	u32 osd_x_offset;
	u32 osd_y_offset;

	u32 osd_x_pan;
	u32 osd_y_pan;

	u32 osd_vis_w;
	u32 osd_vis_h;

	u32 osd_full_w;
	u32 osd_full_h;

	int decode_height;

	int lace_mode;
	int lace_threshold;
	int lace_sync_field;

	atomic_t next_dma_frame;
	atomic_t next_fill_frame;

	u32 yuv_forced_update;
	int update_frame;

	u8 fields_lapsed;   /* Counter used when delaying a frame */

	struct yuv_frame_info new_frame_info[IVTV_YUV_BUFFERS];
	struct yuv_frame_info old_frame_info;
	struct yuv_frame_info old_frame_info_args;

	void *blanking_ptr;
	dma_addr_t blanking_dmaptr;

	int stream_size;

	u8 draw_frame; /* PVR350 buffer to draw into */
	u8 max_frames_buffered; /* Maximum number of frames to buffer */

	struct v4l2_rect main_rect;
	u32 v4l2_src_w;
	u32 v4l2_src_h;

	u8 running; /* Have any frames been displayed */
};

#define IVTV_VBI_FRAMES 32

/* VBI data */
struct vbi_cc {
	u8 odd[2];	/* two-byte payload of odd field */
	u8 even[2];	/* two-byte payload of even field */;
};

struct vbi_vps {
	u8 data[5];	/* five-byte VPS payload */
};

struct vbi_info {
	/* VBI general data, does not change during streaming */

	u32 raw_decoder_line_size;              /* raw VBI line size from digitizer */
	u8 raw_decoder_sav_odd_field;           /* raw VBI Start Active Video digitizer code of odd field */
	u8 raw_decoder_sav_even_field;          /* raw VBI Start Active Video digitizer code of even field */
	u32 sliced_decoder_line_size;           /* sliced VBI line size from digitizer */
	u8 sliced_decoder_sav_odd_field;        /* sliced VBI Start Active Video digitizer code of odd field */
	u8 sliced_decoder_sav_even_field;       /* sliced VBI Start Active Video digitizer code of even field */

	u32 start[2];				/* start of first VBI line in the odd/even fields */
	u32 count;				/* number of VBI lines per field */
	u32 raw_size;				/* size of raw VBI line from the digitizer */
	u32 sliced_size;			/* size of sliced VBI line from the digitizer */

	u32 dec_start;				/* start in decoder memory of VBI re-insertion buffers */
	u32 enc_start;				/* start in encoder memory of VBI capture buffers */
	u32 enc_size;				/* size of VBI capture area */
	int fpi;				/* number of VBI frames per interrupt */

	struct v4l2_format in;			/* current VBI capture format */
	struct v4l2_sliced_vbi_format *sliced_in; /* convenience pointer to sliced struct in vbi.in union */
	int insert_mpeg;			/* if non-zero, then embed VBI data in MPEG stream */

	/* Raw VBI compatibility hack */

	u32 frame;				/* frame counter hack needed for backwards compatibility
						   of old VBI software */

	/* Sliced VBI output data */

	struct vbi_cc cc_payload[256];		/* sliced VBI CC payload array: it is an array to
						   prevent dropping CC data if they couldn't be
						   processed fast enough */
	int cc_payload_idx;			/* index in cc_payload */
	u8 cc_missing_cnt;			/* counts number of frames without CC for passthrough mode */
	int wss_payload;			/* sliced VBI WSS payload */
	u8 wss_missing_cnt;			/* counts number of frames without WSS for passthrough mode */
	struct vbi_vps vps_payload;		/* sliced VBI VPS payload */

	/* Sliced VBI capture data */

	struct v4l2_sliced_vbi_data sliced_data[36];	/* sliced VBI storage for VBI encoder stream */
	struct v4l2_sliced_vbi_data sliced_dec_data[36];/* sliced VBI storage for VBI decoder stream */

	/* VBI Embedding data */

	/* Buffer for VBI data inserted into MPEG stream.
	   The first byte is a dummy byte that's never used.
	   The next 16 bytes contain the MPEG header for the VBI data,
	   the remainder is the actual VBI data.
	   The max size accepted by the MPEG VBI reinsertion turns out
	   to be 1552 bytes, which happens to be 4 + (1 + 42) * (2 * 18) bytes,
	   where 4 is a four byte header, 42 is the max sliced VBI payload, 1 is
	   a single line header byte and 2 * 18 is the number of VBI lines per frame.

	   However, it seems that the data must be 1K aligned, so we have to
	   pad the data until the 1 or 2 K boundary.

	   This pointer array will allocate 2049 bytes to store each VBI frame. */
	u8 *sliced_mpeg_data[IVTV_VBI_FRAMES];
	u32 sliced_mpeg_size[IVTV_VBI_FRAMES];
	struct ivtv_buffer sliced_mpeg_buf;	/* temporary buffer holding data from sliced_mpeg_data */
	u32 inserted_frame;			/* index in sliced_mpeg_size of next sliced data
						   to be inserted in the MPEG stream */
};

/* forward declaration of struct defined in ivtv-cards.h */
struct ivtv_card;

/* Struct to hold info about ivtv cards */
struct ivtv {
	/* General fixed card data */
	struct pci_dev *pdev;		/* PCI device */
	const struct ivtv_card *card;	/* card information */
	const char *card_name;          /* full name of the card */
	const struct ivtv_card_tuner_i2c *card_i2c; /* i2c addresses to probe for tuner */
	u8 has_cx23415;			/* 1 if it is a cx23415 based card, 0 for cx23416 */
	u8 pvr150_workaround;           /* 1 if the cx25840 needs to workaround a PVR150 bug */
	u8 nof_inputs;			/* number of video inputs */
	u8 nof_audio_inputs;		/* number of audio inputs */
	u32 v4l2_cap;			/* V4L2 capabilities of card */
	u32 hw_flags;			/* hardware description of the board */
	v4l2_std_id tuner_std;		/* the norm of the card's tuner (fixed) */
	struct v4l2_subdev *sd_video;	/* controlling video decoder subdev */
	struct v4l2_subdev *sd_audio;	/* controlling audio subdev */
	struct v4l2_subdev *sd_muxer;	/* controlling audio muxer subdev */
	resource_size_t base_addr;      /* PCI resource base address */
	volatile void __iomem *enc_mem; /* pointer to mapped encoder memory */
	volatile void __iomem *dec_mem; /* pointer to mapped decoder memory */
	volatile void __iomem *reg_mem; /* pointer to mapped registers */
	struct ivtv_options options;	/* user options */

	struct v4l2_device v4l2_dev;
	struct cx2341x_handler cxhdl;
	struct {
		/* PTS/Frame count control cluster */
		struct v4l2_ctrl *ctrl_pts;
		struct v4l2_ctrl *ctrl_frame;
	};
	struct {
		/* Audio Playback control cluster */
		struct v4l2_ctrl *ctrl_audio_playback;
		struct v4l2_ctrl *ctrl_audio_multilingual_playback;
	};
	struct v4l2_ctrl_handler hdl_gpio;
	struct v4l2_subdev sd_gpio;	/* GPIO sub-device */
	u16 instance;

	/* High-level state info */
	unsigned long i_flags;          /* global ivtv flags */
	u8 is_50hz;                     /* 1 if the current capture standard is 50 Hz */
	u8 is_60hz                      /* 1 if the current capture standard is 60 Hz */;
	u8 is_out_50hz                  /* 1 if the current TV output standard is 50 Hz */;
	u8 is_out_60hz                  /* 1 if the current TV output standard is 60 Hz */;
	int output_mode;                /* decoder output mode: NONE, MPG, YUV, UDMA YUV, passthrough */
	u32 audio_input;                /* current audio input */
	u32 active_input;               /* current video input */
	u32 active_output;              /* current video output */
	v4l2_std_id std;                /* current capture TV standard */
	v4l2_std_id std_out;            /* current TV output standard */
	u8 audio_stereo_mode;           /* decoder setting how to handle stereo MPEG audio */
	u8 audio_bilingual_mode;        /* decoder setting how to handle bilingual MPEG audio */

	/* Locking */
	spinlock_t lock;                /* lock access to this struct */
	struct mutex serialize_lock;    /* mutex used to serialize open/close/start/stop/ioctl operations */

	/* Streams */
	int stream_buf_size[IVTV_MAX_STREAMS];          /* stream buffer size */
	struct ivtv_stream streams[IVTV_MAX_STREAMS];	/* stream data */
	atomic_t capturing;		/* count number of active capture streams */
	atomic_t decoding;		/* count number of active decoding streams */

	/* ALSA interface for PCM capture stream */
	struct snd_ivtv_card *alsa;
	void (*pcm_announce_callback)(struct snd_ivtv_card *card, u8 *pcm_data,
				      size_t num_bytes);

	/* Used for ivtv-alsa module loading */
	struct work_struct request_module_wk;

	/* Interrupts & DMA */
	u32 irqmask;                    /* active interrupts */
	u32 irq_rr_idx;                 /* round-robin stream index */
	struct kthread_worker irq_worker;		/* kthread worker for PIO/YUV/VBI actions */
	struct task_struct *irq_worker_task;		/* task for irq_worker */
	struct kthread_work irq_work;	/* kthread work entry */
	spinlock_t dma_reg_lock;        /* lock access to DMA engine registers */
	int cur_dma_stream;		/* index of current stream doing DMA (-1 if none) */
	int cur_pio_stream;		/* index of current stream doing PIO (-1 if none) */
	u32 dma_data_req_offset;        /* store offset in decoder memory of current DMA request */
	u32 dma_data_req_size;          /* store size of current DMA request */
	int dma_retries;                /* current DMA retry attempt */
	struct ivtv_user_dma udma;      /* user based DMA for OSD */
	struct timer_list dma_timer;    /* timer used to catch unfinished DMAs */
	u32 last_vsync_field;           /* last seen vsync field */
	wait_queue_head_t dma_waitq;    /* wake up when the current DMA is finished */
	wait_queue_head_t eos_waitq;    /* wake up when EOS arrives */
	wait_queue_head_t event_waitq;  /* wake up when the next decoder event arrives */
	wait_queue_head_t vsync_waitq;  /* wake up when the next decoder vsync arrives */


	/* Mailbox */
	struct ivtv_mailbox_data enc_mbox;              /* encoder mailboxes */
	struct ivtv_mailbox_data dec_mbox;              /* decoder mailboxes */
	struct ivtv_api_cache api_cache[256];		/* cached API commands */


	/* I2C */
	struct i2c_adapter i2c_adap;
	struct i2c_algo_bit_data i2c_algo;
	struct i2c_client i2c_client;
	int i2c_state;                  /* i2c bit state */
	struct mutex i2c_bus_lock;      /* lock i2c bus */

	struct IR_i2c_init_data ir_i2c_init_data;

	/* Program Index information */
	u32 pgm_info_offset;            /* start of pgm info in encoder memory */
	u32 pgm_info_num;               /* number of elements in the pgm cyclic buffer in encoder memory */
	u32 pgm_info_write_idx;         /* last index written by the card that was transferred to pgm_info[] */
	u32 pgm_info_read_idx;          /* last index in pgm_info read by the application */
	struct v4l2_enc_idx_entry pgm_info[IVTV_MAX_PGM_INDEX]; /* filled from the pgm cyclic buffer on the card */


	/* Miscellaneous */
	u32 open_id;			/* incremented each time an open occurs, is >= 1 */
	int search_pack_header;         /* 1 if ivtv_copy_buf_to_user() is scanning for a pack header (0xba) */
	int speed;                      /* current playback speed setting */
	u8 speed_mute_audio;            /* 1 if audio should be muted when fast forward */
	u64 mpg_data_received;          /* number of bytes received from the MPEG stream */
	u64 vbi_data_inserted;          /* number of VBI bytes inserted into the MPEG stream */
	u32 last_dec_timing[3];         /* cache last retrieved pts/scr/frame values */
	unsigned long dualwatch_jiffies;/* jiffies value of the previous dualwatch check */
	u32 dualwatch_stereo_mode;      /* current detected dualwatch stereo mode */


	/* VBI state info */
	struct vbi_info vbi;            /* VBI-specific data */


	/* YUV playback */
	struct yuv_playback_info yuv_info;              /* YUV playback data */


	/* OSD support */
	unsigned long osd_video_pbase;
	int osd_global_alpha_state;     /* 1 = global alpha is on */
	int osd_local_alpha_state;      /* 1 = local alpha is on */
	int osd_chroma_key_state;       /* 1 = chroma-keying is on */
	u8  osd_global_alpha;           /* current global alpha */
	u32 osd_chroma_key;             /* current chroma key */
	struct v4l2_rect osd_rect;      /* current OSD position and size */
	struct v4l2_rect main_rect;     /* current Main window position and size */
	struct osd_info *osd_info;      /* ivtvfb private OSD info */
	void (*ivtvfb_restore)(struct ivtv *itv); /* Used for a warm start */
};

static inline struct ivtv *to_ivtv(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct ivtv, v4l2_dev);
}

/* ivtv extensions to be loaded */
extern int (*ivtv_ext_init)(struct ivtv *);

/* Globals */
extern int ivtv_first_minor;

/*==============Prototypes==================*/

/* Hardware/IRQ */
void ivtv_set_irq_mask(struct ivtv *itv, u32 mask);
void ivtv_clear_irq_mask(struct ivtv *itv, u32 mask);

/* try to set output mode, return current mode. */
int ivtv_set_output_mode(struct ivtv *itv, int mode);

/* return current output stream based on current mode */
struct ivtv_stream *ivtv_get_output_stream(struct ivtv *itv);

/* Return non-zero if a signal is pending */
int ivtv_msleep_timeout(unsigned int msecs, int intr);

/* Wait on queue, returns -EINTR if interrupted */
int ivtv_waitq(wait_queue_head_t *waitq);

/* Read Hauppauge eeprom */
struct tveeprom; /* forward reference */
void ivtv_read_eeprom(struct ivtv *itv, struct tveeprom *tv);

/* First-open initialization: load firmware, init cx25840, etc. */
int ivtv_init_on_first_open(struct ivtv *itv);

/* Test if the current VBI mode is raw (1) or sliced (0) */
static inline int ivtv_raw_vbi(const struct ivtv *itv)
{
	return itv->vbi.in.type == V4L2_BUF_TYPE_VBI_CAPTURE;
}

/* This is a PCI post thing, where if the pci register is not read, then
   the write doesn't always take effect right away. By reading back the
   register any pending PCI writes will be performed (in order), and so
   you can be sure that the writes are guaranteed to be done.

   Rarely needed, only in some timing sensitive cases.
   Apparently if this is not done some motherboards seem
   to kill the firmware and get into the broken state until computer is
   rebooted. */
#define write_sync(val, reg) \
	do { writel(val, reg); readl(reg); } while (0)

#define read_reg(reg) readl(itv->reg_mem + (reg))
#define write_reg(val, reg) writel(val, itv->reg_mem + (reg))
#define write_reg_sync(val, reg) \
	do { write_reg(val, reg); read_reg(reg); } while (0)

#define read_enc(addr) readl(itv->enc_mem + (u32)(addr))
#define write_enc(val, addr) writel(val, itv->enc_mem + (u32)(addr))
#define write_enc_sync(val, addr) \
	do { write_enc(val, addr); read_enc(addr); } while (0)

#define read_dec(addr) readl(itv->dec_mem + (u32)(addr))
#define write_dec(val, addr) writel(val, itv->dec_mem + (u32)(addr))
#define write_dec_sync(val, addr) \
	do { write_dec(val, addr); read_dec(addr); } while (0)

/* Call the specified callback for all subdevs matching hw (if 0, then
   match them all). Ignore any errors. */
#define ivtv_call_hw(itv, hw, o, f, args...)				\
	v4l2_device_mask_call_all(&(itv)->v4l2_dev, hw, o, f, ##args)

#define ivtv_call_all(itv, o, f, args...) ivtv_call_hw(itv, 0, o, f , ##args)

/* Call the specified callback for all subdevs matching hw (if 0, then
   match them all). If the callback returns an error other than 0 or
   -ENOIOCTLCMD, then return with that error code. */
#define ivtv_call_hw_err(itv, hw, o, f, args...)			\
	v4l2_device_mask_call_until_err(&(itv)->v4l2_dev, hw, o, f, ##args)

#define ivtv_call_all_err(itv, o, f, args...) ivtv_call_hw_err(itv, 0, o, f , ##args)

#endif
