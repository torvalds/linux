/*
 *  cx18 driver internal defines and structures
 *
 *  Derived from ivtv-driver.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef CX18_DRIVER_H
#define CX18_DRIVER_H

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/list.h>
#include <linux/unistd.h>
#include <linux/pagemap.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/tuner.h>
#include <media/i2c/ir-kbd-i2c.h>
#include "cx18-mailbox.h"
#include "cx18-av-core.h"
#include "cx23418.h"

/* DVB */
#include "demux.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "dvbdev.h"

/* Videobuf / YUV support */
#include <media/videobuf-core.h>
#include <media/videobuf-vmalloc.h>

#ifndef CONFIG_PCI
#  error "This driver requires kernel PCI support."
#endif

#define CX18_MEM_OFFSET	0x00000000
#define CX18_MEM_SIZE	0x04000000
#define CX18_REG_OFFSET	0x02000000

/* Maximum cx18 driver instances. */
#define CX18_MAX_CARDS 32

/* Supported cards */
#define CX18_CARD_HVR_1600_ESMT	      0	/* Hauppauge HVR 1600 (ESMT memory) */
#define CX18_CARD_HVR_1600_SAMSUNG    1	/* Hauppauge HVR 1600 (Samsung memory) */
#define CX18_CARD_COMPRO_H900 	      2	/* Compro VideoMate H900 */
#define CX18_CARD_YUAN_MPC718 	      3	/* Yuan MPC718 */
#define CX18_CARD_CNXT_RAPTOR_PAL     4	/* Conexant Raptor PAL */
#define CX18_CARD_TOSHIBA_QOSMIO_DVBT 5 /* Toshiba Qosmio Interal DVB-T/Analog*/
#define CX18_CARD_LEADTEK_PVR2100     6 /* Leadtek WinFast PVR2100 */
#define CX18_CARD_LEADTEK_DVR3100H    7 /* Leadtek WinFast DVR3100 H */
#define CX18_CARD_GOTVIEW_PCI_DVD3    8 /* GoTView PCI DVD3 Hybrid */
#define CX18_CARD_HVR_1600_S5H1411    9 /* Hauppauge HVR 1600 s5h1411/tda18271*/
#define CX18_CARD_LAST		      9

#define CX18_ENC_STREAM_TYPE_MPG  0
#define CX18_ENC_STREAM_TYPE_TS   1
#define CX18_ENC_STREAM_TYPE_YUV  2
#define CX18_ENC_STREAM_TYPE_VBI  3
#define CX18_ENC_STREAM_TYPE_PCM  4
#define CX18_ENC_STREAM_TYPE_IDX  5
#define CX18_ENC_STREAM_TYPE_RAD  6
#define CX18_MAX_STREAMS	  7

/* system vendor and device IDs */
#define PCI_VENDOR_ID_CX      0x14f1
#define PCI_DEVICE_ID_CX23418 0x5b7a

/* subsystem vendor ID */
#define CX18_PCI_ID_HAUPPAUGE 		0x0070
#define CX18_PCI_ID_COMPRO 		0x185b
#define CX18_PCI_ID_YUAN 		0x12ab
#define CX18_PCI_ID_CONEXANT		0x14f1
#define CX18_PCI_ID_TOSHIBA		0x1179
#define CX18_PCI_ID_LEADTEK		0x107D
#define CX18_PCI_ID_GOTVIEW		0x5854

/* ======================================================================== */
/* ========================== START USER SETTABLE DMA VARIABLES =========== */
/* ======================================================================== */

/* DMA Buffers, Default size in MB allocated */
#define CX18_DEFAULT_ENC_TS_BUFFERS  1
#define CX18_DEFAULT_ENC_MPG_BUFFERS 2
#define CX18_DEFAULT_ENC_IDX_BUFFERS 1
#define CX18_DEFAULT_ENC_YUV_BUFFERS 2
#define CX18_DEFAULT_ENC_VBI_BUFFERS 1
#define CX18_DEFAULT_ENC_PCM_BUFFERS 1

/* Maximum firmware DMA buffers per stream */
#define CX18_MAX_FW_MDLS_PER_STREAM 63

/* YUV buffer sizes in bytes to ensure integer # of frames per buffer */
#define CX18_UNIT_ENC_YUV_BUFSIZE	(720 *  32 * 3 / 2) /* bytes */
#define CX18_625_LINE_ENC_YUV_BUFSIZE	(CX18_UNIT_ENC_YUV_BUFSIZE * 576/32)
#define CX18_525_LINE_ENC_YUV_BUFSIZE	(CX18_UNIT_ENC_YUV_BUFSIZE * 480/32)

/* IDX buffer size should be a multiple of the index entry size from the chip */
struct cx18_enc_idx_entry {
	__le32 length;
	__le32 offset_low;
	__le32 offset_high;
	__le32 flags;
	__le32 pts_low;
	__le32 pts_high;
} __attribute__ ((packed));
#define CX18_UNIT_ENC_IDX_BUFSIZE \
	(sizeof(struct cx18_enc_idx_entry) * V4L2_ENC_IDX_ENTRIES)

/* DMA buffer, default size in kB allocated */
#define CX18_DEFAULT_ENC_TS_BUFSIZE   32
#define CX18_DEFAULT_ENC_MPG_BUFSIZE  32
#define CX18_DEFAULT_ENC_IDX_BUFSIZE  (CX18_UNIT_ENC_IDX_BUFSIZE * 1 / 1024 + 1)
#define CX18_DEFAULT_ENC_YUV_BUFSIZE  (CX18_UNIT_ENC_YUV_BUFSIZE * 3 / 1024 + 1)
#define CX18_DEFAULT_ENC_PCM_BUFSIZE   4

/* i2c stuff */
#define I2C_CLIENTS_MAX 16

/* debugging */

/* Flag to turn on high volume debugging */
#define CX18_DBGFLG_WARN  (1 << 0)
#define CX18_DBGFLG_INFO  (1 << 1)
#define CX18_DBGFLG_API   (1 << 2)
#define CX18_DBGFLG_DMA   (1 << 3)
#define CX18_DBGFLG_IOCTL (1 << 4)
#define CX18_DBGFLG_FILE  (1 << 5)
#define CX18_DBGFLG_I2C   (1 << 6)
#define CX18_DBGFLG_IRQ   (1 << 7)
/* Flag to turn on high volume debugging */
#define CX18_DBGFLG_HIGHVOL (1 << 8)

/* NOTE: extra space before comma in 'fmt , ## args' is required for
   gcc-2.95, otherwise it won't compile. */
#define CX18_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & cx18_debug) \
			v4l2_info(&cx->v4l2_dev, " " type ": " fmt , ## args); \
	} while (0)
#define CX18_DEBUG_WARN(fmt, args...)  CX18_DEBUG(CX18_DBGFLG_WARN, "warning", fmt , ## args)
#define CX18_DEBUG_INFO(fmt, args...)  CX18_DEBUG(CX18_DBGFLG_INFO, "info", fmt , ## args)
#define CX18_DEBUG_API(fmt, args...)   CX18_DEBUG(CX18_DBGFLG_API, "api", fmt , ## args)
#define CX18_DEBUG_DMA(fmt, args...)   CX18_DEBUG(CX18_DBGFLG_DMA, "dma", fmt , ## args)
#define CX18_DEBUG_IOCTL(fmt, args...) CX18_DEBUG(CX18_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define CX18_DEBUG_FILE(fmt, args...)  CX18_DEBUG(CX18_DBGFLG_FILE, "file", fmt , ## args)
#define CX18_DEBUG_I2C(fmt, args...)   CX18_DEBUG(CX18_DBGFLG_I2C, "i2c", fmt , ## args)
#define CX18_DEBUG_IRQ(fmt, args...)   CX18_DEBUG(CX18_DBGFLG_IRQ, "irq", fmt , ## args)

#define CX18_DEBUG_HIGH_VOL(x, type, fmt, args...) \
	do { \
		if (((x) & cx18_debug) && (cx18_debug & CX18_DBGFLG_HIGHVOL)) \
			v4l2_info(&cx->v4l2_dev, " " type ": " fmt , ## args); \
	} while (0)
#define CX18_DEBUG_HI_WARN(fmt, args...)  CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_WARN, "warning", fmt , ## args)
#define CX18_DEBUG_HI_INFO(fmt, args...)  CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_INFO, "info", fmt , ## args)
#define CX18_DEBUG_HI_API(fmt, args...)   CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_API, "api", fmt , ## args)
#define CX18_DEBUG_HI_DMA(fmt, args...)   CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_DMA, "dma", fmt , ## args)
#define CX18_DEBUG_HI_IOCTL(fmt, args...) CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define CX18_DEBUG_HI_FILE(fmt, args...)  CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_FILE, "file", fmt , ## args)
#define CX18_DEBUG_HI_I2C(fmt, args...)   CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_I2C, "i2c", fmt , ## args)
#define CX18_DEBUG_HI_IRQ(fmt, args...)   CX18_DEBUG_HIGH_VOL(CX18_DBGFLG_IRQ, "irq", fmt , ## args)

/* Standard kernel messages */
#define CX18_ERR(fmt, args...)      v4l2_err(&cx->v4l2_dev, fmt , ## args)
#define CX18_WARN(fmt, args...)     v4l2_warn(&cx->v4l2_dev, fmt , ## args)
#define CX18_INFO(fmt, args...)     v4l2_info(&cx->v4l2_dev, fmt , ## args)

/* Messages for internal subdevs to use */
#define CX18_DEBUG_DEV(x, dev, type, fmt, args...) \
	do { \
		if ((x) & cx18_debug) \
			v4l2_info(dev, " " type ": " fmt , ## args); \
	} while (0)
#define CX18_DEBUG_WARN_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_WARN, dev, "warning", fmt , ## args)
#define CX18_DEBUG_INFO_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_INFO, dev, "info", fmt , ## args)
#define CX18_DEBUG_API_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_API, dev, "api", fmt , ## args)
#define CX18_DEBUG_DMA_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_DMA, dev, "dma", fmt , ## args)
#define CX18_DEBUG_IOCTL_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_IOCTL, dev, "ioctl", fmt , ## args)
#define CX18_DEBUG_FILE_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_FILE, dev, "file", fmt , ## args)
#define CX18_DEBUG_I2C_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_I2C, dev, "i2c", fmt , ## args)
#define CX18_DEBUG_IRQ_DEV(dev, fmt, args...) \
		CX18_DEBUG_DEV(CX18_DBGFLG_IRQ, dev, "irq", fmt , ## args)

#define CX18_DEBUG_HIGH_VOL_DEV(x, dev, type, fmt, args...) \
	do { \
		if (((x) & cx18_debug) && (cx18_debug & CX18_DBGFLG_HIGHVOL)) \
			v4l2_info(dev, " " type ": " fmt , ## args); \
	} while (0)
#define CX18_DEBUG_HI_WARN_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_WARN, dev, "warning", fmt , ## args)
#define CX18_DEBUG_HI_INFO_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_INFO, dev, "info", fmt , ## args)
#define CX18_DEBUG_HI_API_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_API, dev, "api", fmt , ## args)
#define CX18_DEBUG_HI_DMA_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_DMA, dev, "dma", fmt , ## args)
#define CX18_DEBUG_HI_IOCTL_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_IOCTL, dev, "ioctl", fmt , ## args)
#define CX18_DEBUG_HI_FILE_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_FILE, dev, "file", fmt , ## args)
#define CX18_DEBUG_HI_I2C_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_I2C, dev, "i2c", fmt , ## args)
#define CX18_DEBUG_HI_IRQ_DEV(dev, fmt, args...) \
	CX18_DEBUG_HIGH_VOL_DEV(CX18_DBGFLG_IRQ, dev, "irq", fmt , ## args)

#define CX18_ERR_DEV(dev, fmt, args...)      v4l2_err(dev, fmt , ## args)
#define CX18_WARN_DEV(dev, fmt, args...)     v4l2_warn(dev, fmt , ## args)
#define CX18_INFO_DEV(dev, fmt, args...)     v4l2_info(dev, fmt , ## args)

extern int cx18_debug;

struct cx18_options {
	int megabytes[CX18_MAX_STREAMS]; /* Size in megabytes of each stream */
	int cardtype;		/* force card type on load */
	int tuner;		/* set tuner on load */
	int radio;		/* enable/disable radio */
};

/* per-mdl bit flags */
#define CX18_F_M_NEED_SWAP  0	/* mdl buffer data must be endianness swapped */

/* per-stream, s_flags */
#define CX18_F_S_CLAIMED 	3	/* this stream is claimed */
#define CX18_F_S_STREAMING      4	/* the fw is decoding/encoding this stream */
#define CX18_F_S_INTERNAL_USE	5	/* this stream is used internally (sliced VBI processing) */
#define CX18_F_S_STREAMOFF	7	/* signal end of stream EOS */
#define CX18_F_S_APPL_IO        8	/* this stream is used read/written by an application */
#define CX18_F_S_STOPPING	9	/* telling the fw to stop capturing */

/* per-cx18, i_flags */
#define CX18_F_I_LOADED_FW		0 	/* Loaded firmware 1st time */
#define CX18_F_I_EOS			4 	/* End of encoder stream */
#define CX18_F_I_RADIO_USER		5 	/* radio tuner is selected */
#define CX18_F_I_ENC_PAUSED		13 	/* the encoder is paused */
#define CX18_F_I_INITED			21 	/* set after first open */
#define CX18_F_I_FAILED			22 	/* set if first open failed */

/* These are the VBI types as they appear in the embedded VBI private packets. */
#define CX18_SLICED_TYPE_TELETEXT_B     (1)
#define CX18_SLICED_TYPE_CAPTION_525    (4)
#define CX18_SLICED_TYPE_WSS_625        (5)
#define CX18_SLICED_TYPE_VPS            (7)

/**
 * list_entry_is_past_end - check if a previous loop cursor is off list end
 * @pos:	the type * previously used as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Check if the entry's list_head is the head of the list, thus it's not a
 * real entry but was the loop cursor that walked past the end
 */
#define list_entry_is_past_end(pos, head, member) \
	(&pos->member == (head))

struct cx18_buffer {
	struct list_head list;
	dma_addr_t dma_handle;
	char *buf;

	u32 bytesused;
	u32 readpos;
};

struct cx18_mdl {
	struct list_head list;
	u32 id;		/* index into cx->scb->cpu_mdl[] of 1st cx18_mdl_ent */

	unsigned int skipped;
	unsigned long m_flags;

	struct list_head buf_list;
	struct cx18_buffer *curr_buf; /* current buffer in list for reading */

	u32 bytesused;
	u32 readpos;
};

struct cx18_queue {
	struct list_head list;
	atomic_t depth;
	u32 bytesused;
	spinlock_t lock;
};

struct cx18_stream; /* forward reference */

struct cx18_dvb {
	struct cx18_stream *stream;
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	struct dmxdev dmxdev;
	struct dvb_adapter dvb_adapter;
	struct dvb_demux demux;
	struct dvb_frontend *fe;
	struct dvb_net dvbnet;
	int enabled;
	int feeding;
	struct mutex feedlock;
};

struct cx18;	 /* forward reference */
struct cx18_scb; /* forward reference */


#define CX18_MAX_MDL_ACKS 2
#define CX18_MAX_IN_WORK_ORDERS (CX18_MAX_FW_MDLS_PER_STREAM + 7)
/* CPU_DE_RELEASE_MDL can burst CX18_MAX_FW_MDLS_PER_STREAM orders in a group */

#define CX18_F_EWO_MB_STALE_UPON_RECEIPT 0x1
#define CX18_F_EWO_MB_STALE_WHILE_PROC   0x2
#define CX18_F_EWO_MB_STALE \
	     (CX18_F_EWO_MB_STALE_UPON_RECEIPT | CX18_F_EWO_MB_STALE_WHILE_PROC)

struct cx18_in_work_order {
	struct work_struct work;
	atomic_t pending;
	struct cx18 *cx;
	unsigned long flags;
	int rpu;
	struct cx18_mailbox mb;
	struct cx18_mdl_ack mdl_ack[CX18_MAX_MDL_ACKS];
	char *str;
};

#define CX18_INVALID_TASK_HANDLE 0xffffffff

struct cx18_stream {
	/* These first five fields are always set, even if the stream
	   is not actually created. */
	struct video_device video_dev;	/* v4l2_dev is NULL when stream not created */
	struct cx18_dvb *dvb;		/* DVB / Digital Transport */
	struct cx18 *cx; 		/* for ease of use */
	const char *name;		/* name of the stream */
	int type;			/* stream type */
	u32 handle;			/* task handle */
	u32 v4l2_dev_caps;		/* device capabilities */
	unsigned int mdl_base_idx;

	u32 id;
	unsigned long s_flags;	/* status flags, see above */
	int dma;		/* can be PCI_DMA_TODEVICE,
				   PCI_DMA_FROMDEVICE or
				   PCI_DMA_NONE */
	wait_queue_head_t waitq;

	/* Buffers */
	struct list_head buf_pool;	/* buffers not attached to an MDL */
	u32 buffers;			/* total buffers owned by this stream */
	u32 buf_size;			/* size in bytes of a single buffer */

	/* MDL sizes - all stream MDLs are the same size */
	u32 bufs_per_mdl;
	u32 mdl_size;		/* total bytes in all buffers in a mdl */

	/* MDL Queues */
	struct cx18_queue q_free;	/* free - in rotation, not committed */
	struct cx18_queue q_busy;	/* busy - in use by firmware */
	struct cx18_queue q_full;	/* full - data for user apps */
	struct cx18_queue q_idle;	/* idle - not in rotation */

	struct work_struct out_work_order;

	/* Videobuf for YUV video */
	u32 pixelformat;
	u32 vb_bytes_per_frame;
	u32 vb_bytes_per_line;
	struct list_head vb_capture;    /* video capture queue */
	spinlock_t vb_lock;
	struct timer_list vb_timeout;

	struct videobuf_queue vbuf_q;
	spinlock_t vbuf_q_lock; /* Protect vbuf_q */
	enum v4l2_buf_type vb_type;
};

struct cx18_videobuf_buffer {
	/* Common video buffer sub-system struct */
	struct videobuf_buffer vb;
	v4l2_std_id tvnorm; /* selected tv norm */
	u32 bytes_used;
};

struct cx18_open_id {
	struct v4l2_fh fh;
	u32 open_id;
	int type;
	struct cx18 *cx;
};

static inline struct cx18_open_id *fh2id(struct v4l2_fh *fh)
{
	return container_of(fh, struct cx18_open_id, fh);
}

static inline struct cx18_open_id *file2id(struct file *file)
{
	return fh2id(file->private_data);
}

/* forward declaration of struct defined in cx18-cards.h */
struct cx18_card;

/*
 * A note about "sliced" VBI data as implemented in this driver:
 *
 * Currently we collect the sliced VBI in the form of Ancillary Data
 * packets, inserted by the AV core decoder/digitizer/slicer in the
 * horizontal blanking region of the VBI lines, in "raw" mode as far as
 * the Encoder is concerned.  We don't ever tell the Encoder itself
 * to provide sliced VBI. (AV Core: sliced mode - Encoder: raw mode)
 *
 * We then process the ancillary data ourselves to send the sliced data
 * to the user application directly or build up MPEG-2 private stream 1
 * packets to splice into (only!) MPEG-2 PS streams for the user app.
 *
 * (That's how ivtv essentially does it.)
 *
 * The Encoder should be able to extract certain sliced VBI data for
 * us and provide it in a separate stream or splice it into any type of
 * MPEG PS or TS stream, but this isn't implemented yet.
 */

/*
 * Number of "raw" VBI samples per horizontal line we tell the Encoder to
 * grab from the decoder/digitizer/slicer output for raw or sliced VBI.
 * It depends on the pixel clock and the horiz rate:
 *
 * (1/Fh)*(2*Fp) = Samples/line
 *     = 4 bytes EAV + Anc data in hblank + 4 bytes SAV + active samples
 *
 *  Sliced VBI data is sent as ancillary data during horizontal blanking
 *  Raw VBI is sent as active video samples during vertcal blanking
 *
 *  We use a  BT.656 pxiel clock of 13.5 MHz and a BT.656 active line
 *  length of 720 pixels @ 4:2:2 sampling.  Thus...
 *
 *  For systems that use a 15.734 kHz horizontal rate, such as
 *  NTSC-M, PAL-M, PAL-60, and other 60 Hz/525 line systems, we have:
 *
 *  (1/15.734 kHz) * 2 * 13.5 MHz = 1716 samples/line =
 *  4 bytes SAV + 268 bytes anc data + 4 bytes SAV + 1440 active samples
 *
 *  For systems that use a 15.625 kHz horizontal rate, such as
 *  PAL-B/G/H, PAL-I, SECAM-L and other 50 Hz/625 line systems, we have:
 *
 *  (1/15.625 kHz) * 2 * 13.5 MHz = 1728 samples/line =
 *  4 bytes SAV + 280 bytes anc data + 4 bytes SAV + 1440 active samples
 */
#define VBI_ACTIVE_SAMPLES	1444 /* 4 byte SAV + 720 Y + 720 U/V */
#define VBI_HBLANK_SAMPLES_60HZ	272 /* 4 byte EAV + 268 anc/fill */
#define VBI_HBLANK_SAMPLES_50HZ	284 /* 4 byte EAV + 280 anc/fill */

#define CX18_VBI_FRAMES 32

struct vbi_info {
	/* Current state of v4l2 VBI settings for this device */
	struct v4l2_format in;
	struct v4l2_sliced_vbi_format *sliced_in; /* pointer to in.fmt.sliced */
	u32 count;    /* Count of VBI data lines: 60 Hz: 12 or 50 Hz: 18 */
	u32 start[2]; /* First VBI data line per field: 10 & 273 or 6 & 318 */

	u32 frame; /* Count of VBI buffers/frames received from Encoder */

	/*
	 * Vars for creation and insertion of MPEG Private Stream 1 packets
	 * of sliced VBI data into an MPEG PS
	 */

	/* Boolean: create and insert Private Stream 1 packets into the PS */
	int insert_mpeg;

	/*
	 * Buffer for the maximum of 2 * 18 * packet_size sliced VBI lines.
	 * Used in cx18-vbi.c only for collecting sliced data, and as a source
	 * during conversion of sliced VBI data into MPEG Priv Stream 1 packets.
	 * We don't need to save state here, but the array may have been a bit
	 * too big (2304 bytes) to alloc from the stack.
	 */
	struct v4l2_sliced_vbi_data sliced_data[36];

	/*
	 * A ring buffer of driver-generated MPEG-2 PS
	 * Program Pack/Private Stream 1 packets for sliced VBI data insertion
	 * into the MPEG PS stream.
	 *
	 * In each sliced_mpeg_data[] buffer is:
	 * 	16 byte MPEG-2 PS Program Pack Header
	 * 	16 byte MPEG-2 Private Stream 1 PES Header
	 * 	 4 byte magic number: "itv0" or "ITV0"
	 * 	 4 byte first  field line mask, if "itv0"
	 * 	 4 byte second field line mask, if "itv0"
	 * 	36 lines, if "ITV0"; or <36 lines, if "itv0"; of sliced VBI data
	 *
	 * 	Each line in the payload is
	 *	 1 byte line header derived from the SDID (WSS, CC, VPS, etc.)
	 *	42 bytes of line data
	 *
	 * That's a maximum 1552 bytes of payload in the Private Stream 1 packet
	 * which is the payload size a PVR-350 (CX23415) MPEG decoder will
	 * accept for VBI data. So, including the headers, it's a maximum 1584
	 * bytes total.
	 */
#define CX18_SLICED_MPEG_DATA_MAXSZ	1584
	/* copy_vbi_buf() needs 8 temp bytes on the end for the worst case */
#define CX18_SLICED_MPEG_DATA_BUFSZ	(CX18_SLICED_MPEG_DATA_MAXSZ+8)
	u8 *sliced_mpeg_data[CX18_VBI_FRAMES];
	u32 sliced_mpeg_size[CX18_VBI_FRAMES];

	/* Count of Program Pack/Program Stream 1 packets inserted into PS */
	u32 inserted_frame;

	/*
	 * A dummy driver stream transfer mdl & buffer with a copy of the next
	 * sliced_mpeg_data[] buffer for output to userland apps.
	 * Only used in cx18-fileops.c, but its state needs to persist at times.
	 */
	struct cx18_mdl sliced_mpeg_mdl;
	struct cx18_buffer sliced_mpeg_buf;
};

/* Per cx23418, per I2C bus private algo callback data */
struct cx18_i2c_algo_callback_data {
	struct cx18 *cx;
	int bus_index;   /* 0 or 1 for the cx23418's 1st or 2nd I2C bus */
};

#define CX18_MAX_MMIO_WR_RETRIES 10

/* Struct to hold info about cx18 cards */
struct cx18 {
	int instance;
	struct pci_dev *pci_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_subdev *sd_av;     /* A/V decoder/digitizer sub-device */
	struct v4l2_subdev *sd_extmux; /* External multiplexer sub-dev */

	const struct cx18_card *card;	/* card information */
	const char *card_name;  /* full name of the card */
	const struct cx18_card_tuner_i2c *card_i2c; /* i2c addresses to probe for tuner */
	u8 is_50hz;
	u8 is_60hz;
	u8 nof_inputs;		/* number of video inputs */
	u8 nof_audio_inputs;	/* number of audio inputs */
	u32 v4l2_cap;		/* V4L2 capabilities of card */
	u32 hw_flags; 		/* Hardware description of the board */
	unsigned int free_mdl_idx;
	struct cx18_scb __iomem *scb; /* pointer to SCB */
	struct mutex epu2apu_mb_lock; /* protect driver to chip mailbox in SCB*/
	struct mutex epu2cpu_mb_lock; /* protect driver to chip mailbox in SCB*/

	struct cx18_av_state av_state;

	/* codec settings */
	struct cx2341x_handler cxhdl;
	u32 filter_mode;
	u32 temporal_strength;
	u32 spatial_strength;

	/* dualwatch */
	unsigned long dualwatch_jiffies;
	u32 dualwatch_stereo_mode;

	struct mutex serialize_lock;    /* mutex used to serialize open/close/start/stop/ioctl operations */
	struct cx18_options options; 	/* User options */
	int stream_buffers[CX18_MAX_STREAMS]; /* # of buffers for each stream */
	int stream_buf_size[CX18_MAX_STREAMS]; /* Stream buffer size */
	struct cx18_stream streams[CX18_MAX_STREAMS]; 	/* Stream data */
	struct snd_cx18_card *alsa; /* ALSA interface for PCM capture stream */
	void (*pcm_announce_callback)(struct snd_cx18_card *card, u8 *pcm_data,
				      size_t num_bytes);

	unsigned long i_flags;  /* global cx18 flags */
	atomic_t ana_capturing;	/* count number of active analog capture streams */
	atomic_t tot_capturing;	/* total count number of active capture streams */
	int search_pack_header;

	int open_id;		/* incremented each time an open occurs, used as
				   unique ID. Starts at 1, so 0 can be used as
				   uninitialized value in the stream->id. */

	resource_size_t base_addr;

	u8 card_rev;
	void __iomem *enc_mem, *reg_mem;

	struct vbi_info vbi;

	u64 mpg_data_received;
	u64 vbi_data_inserted;

	wait_queue_head_t mb_apu_waitq;
	wait_queue_head_t mb_cpu_waitq;
	wait_queue_head_t cap_w;
	/* when the current DMA is finished this queue is woken up */
	wait_queue_head_t dma_waitq;

	u32 sw1_irq_mask;
	u32 sw2_irq_mask;
	u32 hw2_irq_mask;

	struct workqueue_struct *in_work_queue;
	char in_workq_name[11]; /* "cx18-NN-in" */
	struct cx18_in_work_order in_work_order[CX18_MAX_IN_WORK_ORDERS];
	char epu_debug_str[256]; /* CX18_EPU_DEBUG is rare: use shared space */

	/* i2c */
	struct i2c_adapter i2c_adap[2];
	struct i2c_algo_bit_data i2c_algo[2];
	struct cx18_i2c_algo_callback_data i2c_algo_cb_data[2];

	struct IR_i2c_init_data ir_i2c_init_data;

	/* gpio */
	u32 gpio_dir;
	u32 gpio_val;
	struct mutex gpio_lock;
	struct v4l2_subdev sd_gpiomux;
	struct v4l2_subdev sd_resetctrl;

	/* v4l2 and User settings */

	/* codec settings */
	u32 audio_input;
	u32 active_input;
	v4l2_std_id std;
	v4l2_std_id tuner_std;	/* The norm of the tuner (fixed) */

	/* Used for cx18-alsa module loading */
	struct work_struct request_module_wk;
};

static inline struct cx18 *to_cx18(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct cx18, v4l2_dev);
}

/* cx18 extensions to be loaded */
extern int (*cx18_ext_init)(struct cx18 *);

/* Globals */
extern int cx18_first_minor;

/*==============Prototypes==================*/

/* Return non-zero if a signal is pending */
int cx18_msleep_timeout(unsigned int msecs, int intr);

/* Read Hauppauge eeprom */
struct tveeprom; /* forward reference */
void cx18_read_eeprom(struct cx18 *cx, struct tveeprom *tv);

/* First-open initialization: load firmware, etc. */
int cx18_init_on_first_open(struct cx18 *cx);

/* Test if the current VBI mode is raw (1) or sliced (0) */
static inline int cx18_raw_vbi(const struct cx18 *cx)
{
	return cx->vbi.in.type == V4L2_BUF_TYPE_VBI_CAPTURE;
}

/* Call the specified callback for all subdevs with a grp_id bit matching the
 * mask in hw (if 0, then match them all). Ignore any errors. */
#define cx18_call_hw(cx, hw, o, f, args...)				\
	v4l2_device_mask_call_all(&(cx)->v4l2_dev, hw, o, f, ##args)

#define cx18_call_all(cx, o, f, args...) cx18_call_hw(cx, 0, o, f , ##args)

/* Call the specified callback for all subdevs with a grp_id bit matching the
 * mask in hw (if 0, then match them all). If the callback returns an error
 * other than 0 or -ENOIOCTLCMD, then return with that error code. */
#define cx18_call_hw_err(cx, hw, o, f, args...)				\
	v4l2_device_mask_call_until_err(&(cx)->v4l2_dev, hw, o, f, ##args)

#define cx18_call_all_err(cx, o, f, args...) \
	cx18_call_hw_err(cx, 0, o, f , ##args)

#endif /* CX18_DRIVER_H */
