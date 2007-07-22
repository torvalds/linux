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

/* Internal header for ivtv project:
 * Driver for the cx23415/6 chip.
 * Author: Kevin Thayer (nufan_wfk at yahoo.com)
 * License: GPL
 * http://www.ivtvdriver.org
 *
 * -----
 * MPG600/MPG160 support by  T.Adachi <tadachi@tadachi-net.com>
 *                      and Takeru KOMORIYA<komoriya@paken.org>
 *
 * AVerMedia M179 GPIO info by Chris Pinkham <cpinkham@bc2va.org>
 *                using information provided by Jiun-Kuei Jung @ AVerMedia.
 */

#include <linux/version.h>
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
#include <linux/byteorder/swab.h>
#include <linux/pagemap.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/cx2341x.h>

/* #define HAVE_XC3028 1 */

#include <media/ivtv.h>

#define IVTV_ENCODER_OFFSET	0x00000000
#define IVTV_ENCODER_SIZE	0x00800000	/* Last half isn't needed 0x01000000 */

#define IVTV_DECODER_OFFSET	0x01000000
#define IVTV_DECODER_SIZE	0x00800000	/* Last half isn't needed 0x01000000 */

#define IVTV_REG_OFFSET 	0x02000000
#define IVTV_REG_SIZE		0x00010000

/* Buffers on hardware offsets */
#define IVTV_YUV_BUFFER_OFFSET    0x001a8600	/* First YUV Buffer */
#define IVTV_YUV_BUFFER_OFFSET_1  0x00240400	/* Second YUV Buffer */
#define IVTV_YUV_BUFFER_OFFSET_2  0x002d8200	/* Third YUV Buffer */
#define IVTV_YUV_BUFFER_OFFSET_3  0x00370000	/* Fourth YUV Buffer */
#define IVTV_YUV_BUFFER_UV_OFFSET 0x65400	/* Offset to UV Buffer */

/* Offset to filter table in firmware */
#define IVTV_YUV_HORIZONTAL_FILTER_OFFSET 0x025d8
#define IVTV_YUV_VERTICAL_FILTER_OFFSET 0x03358

extern const u32 yuv_offset[4];

/* Maximum ivtv driver instances.
   Based on 6 PVR500s each with two PVR15s...
   TODO: make this dynamic. I believe it is only a global in order to support
    ivtv-fb. There must be a better way to do that. */
#define IVTV_MAX_CARDS 12

/* Supported cards */
#define IVTV_CARD_PVR_250 	      0	/* WinTV PVR 250 */
#define IVTV_CARD_PVR_350 	      1	/* encoder, decoder, tv-out */
#define IVTV_CARD_PVR_150 	      2	/* WinTV PVR 150 and PVR 500 (really just two
					   PVR150s on one PCI board) */
#define IVTV_CARD_M179    	      3	/* AVerMedia M179 (encoder only) */
#define IVTV_CARD_MPG600  	      4	/* Kuroutoshikou ITVC16-STVLP/YUAN MPG600, encoder only */
#define IVTV_CARD_MPG160  	      5	/* Kuroutoshikou ITVC15-STVLP/YUAN MPG160
					   cx23415 based, but does not have tv-out */
#define IVTV_CARD_PG600 	      6	/* YUAN PG600/DIAMONDMM PVR-550 based on the CX Falcon 2 */
#define IVTV_CARD_AVC2410 	      7	/* Adaptec AVC-2410 */
#define IVTV_CARD_AVC2010 	      8	/* Adaptec AVD-2010 (No Tuner) */
#define IVTV_CARD_TG5000TV   	      9 /* NAGASE TRANSGEAR 5000TV, encoder only */
#define IVTV_CARD_VA2000MAX_SNT6     10 /* VA2000MAX-STN6 */
#define IVTV_CARD_CX23416GYC 	     11 /* Kuroutoshikou CX23416GYC-STVLP (Yuan MPG600GR OEM) */
#define IVTV_CARD_GV_MVPRX   	     12 /* I/O Data GV-MVP/RX, RX2, RX2W */
#define IVTV_CARD_GV_MVPRX2E 	     13 /* I/O Data GV-MVP/RX2E */
#define IVTV_CARD_GOTVIEW_PCI_DVD    14	/* GotView PCI DVD */
#define IVTV_CARD_GOTVIEW_PCI_DVD2   15	/* GotView PCI DVD2 */
#define IVTV_CARD_YUAN_MPC622        16	/* Yuan MPC622 miniPCI */
#define IVTV_CARD_DCTMTVP1 	     17 /* DIGITAL COWBOY DCT-MTVP1 */
#ifdef HAVE_XC3028
#define IVTV_CARD_PG600V2	     18 /* Yuan PG600V2/GotView PCI DVD Lite/Club3D ZAP-TV1x01 */
#define IVTV_CARD_LAST 		     18
#else
#define IVTV_CARD_LAST 		     17
#endif

/* Variants of existing cards but with the same PCI IDs. The driver
   detects these based on other device information.
   These cards must always come last.
   New cards must be inserted above, and the indices of the cards below
   must be adjusted accordingly. */

/* PVR-350 V1 (uses saa7114) */
#define IVTV_CARD_PVR_350_V1 	     (IVTV_CARD_LAST+1)
/* 2 variants of Kuroutoshikou CX23416GYC-STVLP (Yuan MPG600GR OEM) */
#define IVTV_CARD_CX23416GYC_NOGR    (IVTV_CARD_LAST+2)
#define IVTV_CARD_CX23416GYC_NOGRYCS (IVTV_CARD_LAST+3)

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

#define IVTV_V4L2_DEC_MPG_OFFSET  16	/* offset from 0 to register decoder mpg v4l2 minors on */
#define IVTV_V4L2_ENC_PCM_OFFSET  24	/* offset from 0 to register pcm v4l2 minors on */
#define IVTV_V4L2_ENC_YUV_OFFSET  32	/* offset from 0 to register yuv v4l2 minors on */
#define IVTV_V4L2_DEC_YUV_OFFSET  48	/* offset from 0 to register decoder yuv v4l2 minors on */
#define IVTV_V4L2_DEC_VBI_OFFSET   8	/* offset from 0 to register decoder vbi input v4l2 minors on */
#define IVTV_V4L2_DEC_VOUT_OFFSET 16	/* offset from 0 to register vbi output v4l2 minors on */

#define IVTV_ENC_MEM_START 0x00000000
#define IVTV_DEC_MEM_START 0x01000000

/* system vendor and device IDs */
#define PCI_VENDOR_ID_ICOMP  0x4444
#define PCI_DEVICE_ID_IVTV15 0x0803
#define PCI_DEVICE_ID_IVTV16 0x0016

/* subsystem vendor ID */
#define IVTV_PCI_ID_HAUPPAUGE 		0x0070
#define IVTV_PCI_ID_HAUPPAUGE_ALT1 	0x0270
#define IVTV_PCI_ID_HAUPPAUGE_ALT2 	0x4070
#define IVTV_PCI_ID_ADAPTEC 		0x9005
#define IVTV_PCI_ID_AVERMEDIA 		0x1461
#define IVTV_PCI_ID_YUAN1		0x12ab
#define IVTV_PCI_ID_YUAN2 		0xff01
#define IVTV_PCI_ID_YUAN3 		0xffab
#define IVTV_PCI_ID_YUAN4 		0xfbab
#define IVTV_PCI_ID_DIAMONDMM 		0xff92
#define IVTV_PCI_ID_IODATA 		0x10fc
#define IVTV_PCI_ID_MELCO 		0x1154
#define IVTV_PCI_ID_GOTVIEW1		0xffac
#define IVTV_PCI_ID_GOTVIEW2 		0xffad

/* Decoder Buffer hardware size on Chip */
#define IVTV_DEC_MAX_BUF        0x00100000	/* max bytes in decoder buffer */
#define IVTV_DEC_MIN_BUF        0x00010000	/* min bytes in dec buffer */

/* ======================================================================== */
/* ========================== START USER SETTABLE DMA VARIABLES =========== */
/* ======================================================================== */

#define IVTV_DMA_SG_OSD_ENT	(2883584/PAGE_SIZE)	/* sg entities */

/* DMA Buffers, Default size in MB allocated */
#define IVTV_DEFAULT_ENC_MPG_BUFFERS 4
#define IVTV_DEFAULT_ENC_YUV_BUFFERS 2
#define IVTV_DEFAULT_ENC_VBI_BUFFERS 1
#define IVTV_DEFAULT_ENC_PCM_BUFFERS 1
#define IVTV_DEFAULT_DEC_MPG_BUFFERS 1
#define IVTV_DEFAULT_DEC_YUV_BUFFERS 1
#define IVTV_DEFAULT_DEC_VBI_BUFFERS 1

/* ======================================================================== */
/* ========================== END USER SETTABLE DMA VARIABLES ============= */
/* ======================================================================== */

/* Decoder Status Register */
#define IVTV_DMA_ERR_LIST 	0x00000010
#define IVTV_DMA_ERR_WRITE 	0x00000008
#define IVTV_DMA_ERR_READ 	0x00000004
#define IVTV_DMA_SUCCESS_WRITE 	0x00000002
#define IVTV_DMA_SUCCESS_READ 	0x00000001
#define IVTV_DMA_READ_ERR 	(IVTV_DMA_ERR_LIST | IVTV_DMA_ERR_READ)
#define IVTV_DMA_WRITE_ERR 	(IVTV_DMA_ERR_LIST | IVTV_DMA_ERR_WRITE)
#define IVTV_DMA_ERR 		(IVTV_DMA_ERR_LIST | IVTV_DMA_ERR_WRITE | IVTV_DMA_ERR_READ)

/* DMA Registers */
#define IVTV_REG_DMAXFER 	(0x0000)
#define IVTV_REG_DMASTATUS 	(0x0004)
#define IVTV_REG_DECDMAADDR 	(0x0008)
#define IVTV_REG_ENCDMAADDR 	(0x000c)
#define IVTV_REG_DMACONTROL 	(0x0010)
#define IVTV_REG_IRQSTATUS 	(0x0040)
#define IVTV_REG_IRQMASK 	(0x0048)

/* Setup Registers */
#define IVTV_REG_ENC_SDRAM_REFRESH 	(0x07F8)
#define IVTV_REG_ENC_SDRAM_PRECHARGE 	(0x07FC)
#define IVTV_REG_DEC_SDRAM_REFRESH 	(0x08F8)
#define IVTV_REG_DEC_SDRAM_PRECHARGE 	(0x08FC)
#define IVTV_REG_VDM 			(0x2800)
#define IVTV_REG_AO 			(0x2D00)
#define IVTV_REG_BYTEFLUSH 		(0x2D24)
#define IVTV_REG_SPU 			(0x9050)
#define IVTV_REG_HW_BLOCKS 		(0x9054)
#define IVTV_REG_VPU 			(0x9058)
#define IVTV_REG_APU 			(0xA064)

#define IVTV_IRQ_ENC_START_CAP		(0x1 << 31)
#define IVTV_IRQ_ENC_EOS		(0x1 << 30)
#define IVTV_IRQ_ENC_VBI_CAP		(0x1 << 29)
#define IVTV_IRQ_ENC_VIM_RST		(0x1 << 28)
#define IVTV_IRQ_ENC_DMA_COMPLETE	(0x1 << 27)
#define IVTV_IRQ_ENC_PIO_COMPLETE	(0x1 << 25)
#define IVTV_IRQ_DEC_AUD_MODE_CHG	(0x1 << 24)
#define IVTV_IRQ_DEC_DATA_REQ		(0x1 << 22)
#define IVTV_IRQ_DEC_DMA_COMPLETE	(0x1 << 20)
#define IVTV_IRQ_DEC_VBI_RE_INSERT	(0x1 << 19)
#define IVTV_IRQ_DMA_ERR		(0x1 << 18)
#define IVTV_IRQ_DMA_WRITE		(0x1 << 17)
#define IVTV_IRQ_DMA_READ		(0x1 << 16)
#define IVTV_IRQ_DEC_VSYNC		(0x1 << 10)

/* IRQ Masks */
#define IVTV_IRQ_MASK_INIT (IVTV_IRQ_DMA_ERR|IVTV_IRQ_ENC_DMA_COMPLETE|\
		IVTV_IRQ_DMA_READ|IVTV_IRQ_ENC_PIO_COMPLETE)

#define IVTV_IRQ_MASK_CAPTURE (IVTV_IRQ_ENC_START_CAP | IVTV_IRQ_ENC_EOS)
#define IVTV_IRQ_MASK_DECODE  (IVTV_IRQ_DEC_DATA_REQ|IVTV_IRQ_DEC_AUD_MODE_CHG)

/* i2c stuff */
#define I2C_CLIENTS_MAX 16

/* debugging */

#define IVTV_DBGFLG_WARN  (1 << 0)
#define IVTV_DBGFLG_INFO  (1 << 1)
#define IVTV_DBGFLG_API   (1 << 2)
#define IVTV_DBGFLG_DMA   (1 << 3)
#define IVTV_DBGFLG_IOCTL (1 << 4)
#define IVTV_DBGFLG_I2C   (1 << 5)
#define IVTV_DBGFLG_IRQ   (1 << 6)
#define IVTV_DBGFLG_DEC   (1 << 7)
#define IVTV_DBGFLG_YUV   (1 << 8)
/* Flag to turn on high volume debugging */
#define IVTV_DBGFLG_HIGHVOL (1 << 9)

/* NOTE: extra space before comma in 'itv->num , ## args' is required for
   gcc-2.95, otherwise it won't compile. */
#define IVTV_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & ivtv_debug) \
			printk(KERN_INFO "ivtv%d " type ": " fmt, itv->num , ## args); \
	} while (0)
#define IVTV_DEBUG_WARN(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_WARN, "warning", fmt , ## args)
#define IVTV_DEBUG_INFO(fmt, args...)  IVTV_DEBUG(IVTV_DBGFLG_INFO, "info",fmt , ## args)
#define IVTV_DEBUG_API(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_API, "api", fmt , ## args)
#define IVTV_DEBUG_DMA(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_DMA, "dma", fmt , ## args)
#define IVTV_DEBUG_IOCTL(fmt, args...) IVTV_DEBUG(IVTV_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define IVTV_DEBUG_I2C(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_I2C, "i2c", fmt , ## args)
#define IVTV_DEBUG_IRQ(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_IRQ, "irq", fmt , ## args)
#define IVTV_DEBUG_DEC(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_DEC, "dec", fmt , ## args)
#define IVTV_DEBUG_YUV(fmt, args...)   IVTV_DEBUG(IVTV_DBGFLG_YUV, "yuv", fmt , ## args)

#define IVTV_DEBUG_HIGH_VOL(x, type, fmt, args...) \
	do { \
		if (((x) & ivtv_debug) && (ivtv_debug & IVTV_DBGFLG_HIGHVOL)) \
			printk(KERN_INFO "ivtv%d " type ": " fmt, itv->num , ## args); \
	} while (0)
#define IVTV_DEBUG_HI_WARN(fmt, args...)  IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_WARN, "warning", fmt , ## args)
#define IVTV_DEBUG_HI_INFO(fmt, args...)  IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_INFO, "info",fmt , ## args)
#define IVTV_DEBUG_HI_API(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_API, "api", fmt , ## args)
#define IVTV_DEBUG_HI_DMA(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_DMA, "dma", fmt , ## args)
#define IVTV_DEBUG_HI_IOCTL(fmt, args...) IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define IVTV_DEBUG_HI_I2C(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_I2C, "i2c", fmt , ## args)
#define IVTV_DEBUG_HI_IRQ(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_IRQ, "irq", fmt , ## args)
#define IVTV_DEBUG_HI_DEC(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_DEC, "dec", fmt , ## args)
#define IVTV_DEBUG_HI_YUV(fmt, args...)   IVTV_DEBUG_HIGH_VOL(IVTV_DBGFLG_YUV, "yuv", fmt , ## args)

#define IVTV_FB_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & ivtv_debug) \
			printk(KERN_INFO "ivtv%d-fb " type ": " fmt, itv->num , ## args); \
	} while (0)
#define IVTV_FB_DEBUG_WARN(fmt, args...)  IVTV_FB_DEBUG(IVTV_DBGFLG_WARN, "warning", fmt , ## args)
#define IVTV_FB_DEBUG_INFO(fmt, args...)  IVTV_FB_DEBUG(IVTV_DBGFLG_INFO, "info", fmt , ## args)
#define IVTV_FB_DEBUG_API(fmt, args...)   IVTV_FB_DEBUG(IVTV_DBGFLG_API, "api", fmt , ## args)
#define IVTV_FB_DEBUG_DMA(fmt, args...)   IVTV_FB_DEBUG(IVTV_DBGFLG_DMA, "dma", fmt , ## args)
#define IVTV_FB_DEBUG_IOCTL(fmt, args...) IVTV_FB_DEBUG(IVTV_DBGFLG_IOCTL, "ioctl", fmt , ## args)
#define IVTV_FB_DEBUG_I2C(fmt, args...)   IVTV_FB_DEBUG(IVTV_DBGFLG_I2C, "i2c", fmt , ## args)
#define IVTV_FB_DEBUG_IRQ(fmt, args...)   IVTV_FB_DEBUG(IVTV_DBGFLG_IRQ, "irq", fmt , ## args)
#define IVTV_FB_DEBUG_DEC(fmt, args...)   IVTV_FB_DEBUG(IVTV_DBGFLG_DEC, "dec", fmt , ## args)
#define IVTV_FB_DEBUG_YUV(fmt, args...)   IVTV_FB_DEBUG(IVTV_DBGFLG_YUV, "yuv", fmt , ## args)

/* Standard kernel messages */
#define IVTV_ERR(fmt, args...)      printk(KERN_ERR  "ivtv%d: " fmt, itv->num , ## args)
#define IVTV_WARN(fmt, args...)     printk(KERN_WARNING "ivtv%d: " fmt, itv->num , ## args)
#define IVTV_INFO(fmt, args...)     printk(KERN_INFO "ivtv%d: " fmt, itv->num , ## args)
#define IVTV_FB_ERR(fmt, args...)   printk(KERN_ERR  "ivtv%d-fb: " fmt, itv->num , ## args)
#define IVTV_FB_WARN(fmt, args...)  printk(KERN_WARNING  "ivtv%d-fb: " fmt, itv->num , ## args)
#define IVTV_FB_INFO(fmt, args...)  printk(KERN_INFO "ivtv%d-fb: " fmt, itv->num , ## args)

/* Values for IVTV_API_DEC_PLAYBACK_SPEED mpeg_frame_type_mask parameter: */
#define MPEG_FRAME_TYPE_IFRAME 1
#define MPEG_FRAME_TYPE_IFRAME_PFRAME 3
#define MPEG_FRAME_TYPE_ALL 7

/* output modes (cx23415 only) */
#define OUT_NONE        0
#define OUT_MPG         1
#define OUT_YUV         2
#define OUT_UDMA_YUV    3
#define OUT_PASSTHROUGH 4

#define IVTV_MAX_PGM_INDEX (400)

extern int ivtv_debug;


struct ivtv_options {
	int megabytes[IVTV_MAX_STREAMS]; /* Size in megabytes of each stream */
	int cardtype;		/* force card type on load */
	int tuner;		/* set tuner on load */
	int radio;		/* enable/disable radio */
	int newi2c;		/* New I2C algorithm */
};

#define IVTV_MBOX_DMA_START 6
#define IVTV_MBOX_DMA_END 8
#define IVTV_MBOX_DMA 9
#define IVTV_MBOX_FIELD_DISPLAYED 8

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
#define IVTV_F_B_NEED_BUF_SWAP  0	/* this buffer should be byte swapped */

/* per-stream, s_flags */
#define IVTV_F_S_DMA_PENDING	0	/* this stream has pending DMA */
#define IVTV_F_S_DMA_HAS_VBI	1       /* the current DMA request also requests VBI data */
#define IVTV_F_S_NEEDS_DATA	2 	/* this decoding stream needs more data */

#define IVTV_F_S_CLAIMED 	3	/* this stream is claimed */
#define IVTV_F_S_STREAMING      4	/* the fw is decoding/encoding this stream */
#define IVTV_F_S_INTERNAL_USE	5	/* this stream is used internally (sliced VBI processing) */
#define IVTV_F_S_PASSTHROUGH	6	/* this stream is in passthrough mode */
#define IVTV_F_S_STREAMOFF	7	/* signal end of stream EOS */
#define IVTV_F_S_APPL_IO        8	/* this stream is used read/written by an application */

#define IVTV_F_S_PIO_PENDING	9	/* this stream has pending PIO */
#define IVTV_F_S_PIO_HAS_VBI	1       /* the current PIO request also requests VBI data */

/* per-ivtv, i_flags */
#define IVTV_F_I_DMA		   0 	/* DMA in progress */
#define IVTV_F_I_UDMA		   1 	/* UDMA in progress */
#define IVTV_F_I_UDMA_PENDING	   2 	/* UDMA pending */
#define IVTV_F_I_SPEED_CHANGE	   3 	/* A speed change is in progress */
#define IVTV_F_I_EOS		   4 	/* End of encoder stream reached */
#define IVTV_F_I_RADIO_USER	   5 	/* The radio tuner is selected */
#define IVTV_F_I_DIG_RST	   6 	/* Reset digitizer */
#define IVTV_F_I_DEC_YUV	   7 	/* YUV instead of MPG is being decoded */
#define IVTV_F_I_ENC_VBI	   8 	/* VBI DMA */
#define IVTV_F_I_UPDATE_CC	   9  	/* CC should be updated */
#define IVTV_F_I_UPDATE_WSS	   10 	/* WSS should be updated */
#define IVTV_F_I_UPDATE_VPS	   11 	/* VPS should be updated */
#define IVTV_F_I_DECODING_YUV	   12 	/* this stream is YUV frame decoding */
#define IVTV_F_I_ENC_PAUSED	   13 	/* the encoder is paused */
#define IVTV_F_I_VALID_DEC_TIMINGS 14 	/* last_dec_timing is valid */
#define IVTV_F_I_HAVE_WORK  	   15	/* Used in the interrupt handler: there is work to be done */
#define IVTV_F_I_WORK_HANDLER_VBI  16	/* there is work to be done for VBI */
#define IVTV_F_I_WORK_HANDLER_YUV  17	/* there is work to be done for YUV */
#define IVTV_F_I_WORK_HANDLER_PIO  18	/* there is work to be done for PIO */
#define IVTV_F_I_PIO		   19	/* PIO in progress */
#define IVTV_F_I_DEC_PAUSED	   20 	/* the decoder is paused */

/* Event notifications */
#define IVTV_F_I_EV_DEC_STOPPED	   28	/* decoder stopped event */
#define IVTV_F_I_EV_VSYNC	   29 	/* VSYNC event */
#define IVTV_F_I_EV_VSYNC_FIELD    30 	/* VSYNC event field (0 = first, 1 = second field) */
#define IVTV_F_I_EV_VSYNC_ENABLED  31 	/* VSYNC event enabled */

/* Scatter-Gather array element, used in DMA transfers */
struct ivtv_SG_element {
	u32 src;
	u32 dst;
	u32 size;
};

struct ivtv_user_dma {
	struct mutex lock;
	int page_count;
	struct page *map[IVTV_DMA_SG_OSD_ENT];

	/* Base Dev SG Array for cx23415/6 */
	struct ivtv_SG_element SGarray[IVTV_DMA_SG_OSD_ENT];
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
	unsigned long b_flags;
	char *buf;

	u32 bytesused;
	u32 readpos;
};

struct ivtv_queue {
	struct list_head list;
	u32 buffers;
	u32 length;
	u32 bytesused;
};

struct ivtv;	/* forward reference */

struct ivtv_stream {
	/* These first four fields are always set, even if the stream
	   is not actually created. */
	struct video_device *v4l2dev;	/* NULL when stream not created */
	struct ivtv *itv; 		/* for ease of use */
	const char *name;		/* name of the stream */
	int type;			/* stream type */

	u32 id;
	spinlock_t qlock; 	/* locks access to the queues */
	unsigned long s_flags;	/* status flags, see above */
	int dma;		/* can be PCI_DMA_TODEVICE,
				   PCI_DMA_FROMDEVICE or
				   PCI_DMA_NONE */
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

	/* Base Dev SG Array for cx23415/6 */
	struct ivtv_SG_element *SGarray;
	struct ivtv_SG_element *PIOarray;
	dma_addr_t SG_handle;
	int SG_length;

	/* SG List of Buffers */
	struct scatterlist *SGlist;
};

struct ivtv_open_id {
	u32 open_id;
	int type;
	enum v4l2_priority prio;
	struct ivtv *itv;
};

#define IVTV_YUV_UPDATE_HORIZONTAL  0x01
#define IVTV_YUV_UPDATE_VERTICAL    0x02

struct yuv_frame_info
{
	u32 update;
	int src_x;
	int src_y;
	unsigned int src_w;
	unsigned int src_h;
	int dst_x;
	int dst_y;
	unsigned int dst_w;
	unsigned int dst_h;
	int pan_x;
	int pan_y;
	u32 vis_w;
	u32 vis_h;
	u32 interlaced_y;
	u32 interlaced_uv;
	int tru_x;
	u32 tru_w;
	u32 tru_h;
	u32 offset_y;
};

#define IVTV_YUV_MODE_INTERLACED	0x00
#define IVTV_YUV_MODE_PROGRESSIVE	0x01
#define IVTV_YUV_MODE_AUTO		0x02
#define IVTV_YUV_MODE_MASK		0x03

#define IVTV_YUV_SYNC_EVEN		0x00
#define IVTV_YUV_SYNC_ODD		0x04
#define IVTV_YUV_SYNC_MASK		0x04

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

	u32 osd_x_offset;
	u32 osd_y_offset;

	u32 osd_x_pan;
	u32 osd_y_pan;

	u32 osd_vis_w;
	u32 osd_vis_h;

	int decode_height;

	int frame_interlaced;
	int frame_interlaced_last;

	int lace_mode;
	int lace_threshold;
	int lace_sync_field;

	atomic_t next_dma_frame;
	atomic_t next_fill_frame;

	u32 yuv_forced_update;
	int update_frame;
	struct yuv_frame_info new_frame_info[4];
	struct yuv_frame_info old_frame_info;
	struct yuv_frame_info old_frame_info_args;

	void *blanking_ptr;
	dma_addr_t blanking_dmaptr;
};

#define IVTV_VBI_FRAMES 32

/* VBI data */
struct vbi_info {
	u32 dec_start;
	u32 enc_start, enc_size;
	int fpi;
	u32 frame;
	u32 dma_offset;
	u8 cc_data_odd[256];
	u8 cc_data_even[256];
	int cc_pos;
	u8 cc_no_update;
	u8 vps[5];
	u8 vps_found;
	int wss;
	u8 wss_found;
	u8 wss_no_update;
	u32 raw_decoder_line_size;
	u8 raw_decoder_sav_odd_field;
	u8 raw_decoder_sav_even_field;
	u32 sliced_decoder_line_size;
	u8 sliced_decoder_sav_odd_field;
	u8 sliced_decoder_sav_even_field;
	struct v4l2_format in;
	/* convenience pointer to sliced struct in vbi_in union */
	struct v4l2_sliced_vbi_format *sliced_in;
	u32 service_set_in;
	int insert_mpeg;

	/* Buffer for the maximum of 2 * 18 * packet_size sliced VBI lines.
	   One for /dev/vbi0 and one for /dev/vbi8 */
	struct v4l2_sliced_vbi_data sliced_data[36];
	struct v4l2_sliced_vbi_data sliced_dec_data[36];

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
	struct ivtv_buffer sliced_mpeg_buf;
	u32 inserted_frame;

	u32 start[2], count;
	u32 raw_size;
	u32 sliced_size;
};

/* forward declaration of struct defined in ivtv-cards.h */
struct ivtv_card;

/* Struct to hold info about ivtv cards */
struct ivtv {
	int num;		/* board number, -1 during init! */
	char name[8];		/* board name for printk and interrupts (e.g. 'ivtv0') */
	struct pci_dev *dev;	/* PCI device */
	const struct ivtv_card *card;	/* card information */
	const char *card_name;  /* full name of the card */
	u8 has_cx23415;		/* 1 if it is a cx23415 based card, 0 for cx23416 */
	u8 is_50hz;
	u8 is_60hz;
	u8 is_out_50hz;
	u8 is_out_60hz;
	u8 pvr150_workaround;   /* 1 if the cx25840 needs to workaround a PVR150 bug */
	u8 nof_inputs;		/* number of video inputs */
	u8 nof_audio_inputs;	/* number of audio inputs */
	u32 v4l2_cap;		/* V4L2 capabilities of card */
	u32 hw_flags; 		/* Hardware description of the board */

	/* controlling Video decoder function */
	int (*video_dec_func)(struct ivtv *, unsigned int, void *);

	struct ivtv_options options; 	/* User options */
	int stream_buf_size[IVTV_MAX_STREAMS]; /* Stream buffer size */
	struct ivtv_stream streams[IVTV_MAX_STREAMS]; 	/* Stream data */
	int speed;
	u8 speed_mute_audio;
	unsigned long i_flags;  /* global ivtv flags */
	atomic_t capturing;	/* count number of active capture streams */
	atomic_t decoding;	/* count number of active decoding streams */
	u32 irq_rr_idx; /* Round-robin stream index */
	int cur_dma_stream;	/* index of stream doing DMA */
	int cur_pio_stream;	/* index of stream doing PIO */
	u32 dma_data_req_offset;
	u32 dma_data_req_size;
	int output_mode;        /* NONE, MPG, YUV, UDMA YUV, passthrough */
	spinlock_t lock;        /* lock access to this struct */
	int search_pack_header;

	spinlock_t dma_reg_lock; /* lock access to DMA engine registers */
	struct mutex serialize_lock;  /* lock used to serialize starting streams */

	/* User based DMA for OSD */
	struct ivtv_user_dma udma;

	int open_id;		/* incremented each time an open occurs, used as unique ID.
				   starts at 1, so 0 can be used as uninitialized value
				   in the stream->id. */

	u32 base_addr;
	u32 irqmask;

	struct v4l2_prio_state prio;
	struct workqueue_struct *irq_work_queues;
	struct work_struct irq_work_queue;
	struct timer_list dma_timer; /* Timer used to catch unfinished DMAs */

	struct vbi_info vbi;

	struct ivtv_mailbox_data enc_mbox;
	struct ivtv_mailbox_data dec_mbox;
	struct ivtv_api_cache api_cache[256]; 	/* Cached API Commands */

	u8 card_rev;
	volatile void __iomem *enc_mem, *dec_mem, *reg_mem;

	u32 pgm_info_offset;
	u32 pgm_info_num;
	u32 pgm_info_write_idx;
	u32 pgm_info_read_idx;
	struct v4l2_enc_idx_entry pgm_info[IVTV_MAX_PGM_INDEX];

	u64 mpg_data_received;
	u64 vbi_data_inserted;

	wait_queue_head_t cap_w;
	/* when the next decoder event arrives this queue is woken up */
	wait_queue_head_t event_waitq;
	/* when the next decoder vsync arrives this queue is woken up */
	wait_queue_head_t vsync_waitq;
	/* when the current DMA is finished this queue is woken up */
	wait_queue_head_t dma_waitq;

	/* OSD support */
	unsigned long osd_video_pbase;
	int osd_global_alpha_state; /* 0=off : 1=on */
	int osd_local_alpha_state;  /* 0=off : 1=on */
	int osd_color_key_state;    /* 0=off : 1=on */
	u8  osd_global_alpha;       /* Current global alpha */
	u32 osd_color_key;          /* Current color key */
	u32 osd_pixelformat; 	    /* Current pixel format */
	struct v4l2_rect osd_rect;  /* Current OSD position and size */
	struct v4l2_rect main_rect; /* Current Main window position and size */

	u32 last_dec_timing[3];     /* Store last retrieved pts/scr/frame values */

	/* i2c */
	struct i2c_adapter i2c_adap;
	struct i2c_algo_bit_data i2c_algo;
	struct i2c_client i2c_client;
	struct mutex i2c_bus_lock;
	int i2c_state;
	struct i2c_client *i2c_clients[I2C_CLIENTS_MAX];

	/* v4l2 and User settings */

	/* codec settings */
	struct cx2341x_mpeg_params params;
	u32 audio_input;
	u32 active_input;
	u32 active_output;
	v4l2_std_id std;
	v4l2_std_id std_out;
	v4l2_std_id tuner_std;	/* The norm of the tuner (fixed) */
	u8 audio_stereo_mode;
	u8 audio_bilingual_mode;

	/* dualwatch */
	unsigned long dualwatch_jiffies;
	u16 dualwatch_stereo_mode;

	/* Digitizer type */
	int digitizer;		/* 0x00EF = saa7114 0x00FO = saa7115 0x0106 = mic */

	u32 lastVsyncFrame;

	struct yuv_playback_info yuv_info;
	struct osd_info *osd_info;
};

/* Globals */
extern struct ivtv *ivtv_cards[];
extern int ivtv_cards_active;
extern int ivtv_first_minor;
extern spinlock_t ivtv_cards_lock;

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

#endif /* IVTV_DRIVER_H */
