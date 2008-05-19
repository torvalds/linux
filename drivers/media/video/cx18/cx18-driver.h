/*
 *  cx18 driver internal defines and structures
 *
 *  Derived from ivtv-driver.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#ifndef CX18_DRIVER_H
#define CX18_DRIVER_H

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

#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
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
#define CX18_CARD_LAST 		      3

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

/* NOTE: extra space before comma in 'cx->num , ## args' is required for
   gcc-2.95, otherwise it won't compile. */
#define CX18_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & cx18_debug) \
			printk(KERN_INFO "cx18-%d " type ": " fmt, cx->num , ## args); \
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
			printk(KERN_INFO "cx18%d " type ": " fmt, cx->num , ## args); \
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
#define CX18_ERR(fmt, args...)      printk(KERN_ERR  "cx18-%d: " fmt, cx->num , ## args)
#define CX18_WARN(fmt, args...)     printk(KERN_WARNING "cx18-%d: " fmt, cx->num , ## args)
#define CX18_INFO(fmt, args...)     printk(KERN_INFO "cx18-%d: " fmt, cx->num , ## args)

/* Values for CX18_API_DEC_PLAYBACK_SPEED mpeg_frame_type_mask parameter: */
#define MPEG_FRAME_TYPE_IFRAME 1
#define MPEG_FRAME_TYPE_IFRAME_PFRAME 3
#define MPEG_FRAME_TYPE_ALL 7

#define CX18_MAX_PGM_INDEX (400)

extern int cx18_debug;


struct cx18_options {
	int megabytes[CX18_MAX_STREAMS]; /* Size in megabytes of each stream */
	int cardtype;		/* force card type on load */
	int tuner;		/* set tuner on load */
	int radio;		/* enable/disable radio */
};

/* per-buffer bit flags */
#define CX18_F_B_NEED_BUF_SWAP  0	/* this buffer should be byte swapped */

/* per-stream, s_flags */
#define CX18_F_S_CLAIMED 	3	/* this stream is claimed */
#define CX18_F_S_STREAMING      4	/* the fw is decoding/encoding this stream */
#define CX18_F_S_INTERNAL_USE	5	/* this stream is used internally (sliced VBI processing) */
#define CX18_F_S_STREAMOFF	7	/* signal end of stream EOS */
#define CX18_F_S_APPL_IO        8	/* this stream is used read/written by an application */

/* per-cx18, i_flags */
#define CX18_F_I_LOADED_FW	0 	/* Loaded the firmware the first time */
#define CX18_F_I_EOS		4 	/* End of encoder stream reached */
#define CX18_F_I_RADIO_USER	5 	/* The radio tuner is selected */
#define CX18_F_I_ENC_PAUSED	13 	/* the encoder is paused */
#define CX18_F_I_INITED		21 	/* set after first open */
#define CX18_F_I_FAILED		22 	/* set if first open failed */

/* These are the VBI types as they appear in the embedded VBI private packets. */
#define CX18_SLICED_TYPE_TELETEXT_B     (1)
#define CX18_SLICED_TYPE_CAPTION_525    (4)
#define CX18_SLICED_TYPE_WSS_625        (5)
#define CX18_SLICED_TYPE_VPS            (7)

struct cx18_buffer {
	struct list_head list;
	dma_addr_t dma_handle;
	u32 id;
	unsigned long b_flags;
	char *buf;

	u32 bytesused;
	u32 readpos;
};

struct cx18_queue {
	struct list_head list;
	u32 buffers;
	u32 length;
	u32 bytesused;
};

struct cx18_dvb {
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

struct cx18_stream {
	/* These first four fields are always set, even if the stream
	   is not actually created. */
	struct video_device *v4l2dev;	/* NULL when stream not created */
	struct cx18 *cx; 		/* for ease of use */
	const char *name;		/* name of the stream */
	int type;			/* stream type */
	u32 handle;			/* task handle */
	unsigned mdl_offset;

	u32 id;
	spinlock_t qlock; 	/* locks access to the queues */
	unsigned long s_flags;	/* status flags, see above */
	int dma;		/* can be PCI_DMA_TODEVICE,
				   PCI_DMA_FROMDEVICE or
				   PCI_DMA_NONE */
	u64 dma_pts;
	wait_queue_head_t waitq;

	/* Buffer Stats */
	u32 buffers;
	u32 buf_size;
	u32 buffers_stolen;

	/* Buffer Queues */
	struct cx18_queue q_free;	/* free buffers */
	struct cx18_queue q_full;	/* full buffers */
	struct cx18_queue q_io;		/* waiting for I/O */

	/* DVB / Digital Transport */
	struct cx18_dvb dvb;
};

struct cx18_open_id {
	u32 open_id;
	int type;
	enum v4l2_priority prio;
	struct cx18 *cx;
};

/* forward declaration of struct defined in cx18-cards.h */
struct cx18_card;


#define CX18_VBI_FRAMES 32

/* VBI data */
struct vbi_info {
	u32 enc_size;
	u32 frame;
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
	u8 *sliced_mpeg_data[CX18_VBI_FRAMES];
	u32 sliced_mpeg_size[CX18_VBI_FRAMES];
	struct cx18_buffer sliced_mpeg_buf;
	u32 inserted_frame;

	u32 start[2], count;
	u32 raw_size;
	u32 sliced_size;
};

/* Per cx23418, per I2C bus private algo callback data */
struct cx18_i2c_algo_callback_data {
	struct cx18 *cx;
	int bus_index;   /* 0 or 1 for the cx23418's 1st or 2nd I2C bus */
};

/* Struct to hold info about cx18 cards */
struct cx18 {
	int num;		/* board number, -1 during init! */
	char name[8];		/* board name for printk and interrupts (e.g. 'cx180') */
	struct pci_dev *dev;	/* PCI device */
	const struct cx18_card *card;	/* card information */
	const char *card_name;  /* full name of the card */
	const struct cx18_card_tuner_i2c *card_i2c; /* i2c addresses to probe for tuner */
	u8 is_50hz;
	u8 is_60hz;
	u8 is_out_50hz;
	u8 is_out_60hz;
	u8 nof_inputs;		/* number of video inputs */
	u8 nof_audio_inputs;	/* number of audio inputs */
	u16 buffer_id;		/* buffer ID counter */
	u32 v4l2_cap;		/* V4L2 capabilities of card */
	u32 hw_flags; 		/* Hardware description of the board */
	unsigned mdl_offset;
	struct cx18_scb *scb;   /* pointer to SCB */

	struct cx18_av_state av_state;

	/* codec settings */
	struct cx2341x_mpeg_params params;
	u32 filter_mode;
	u32 temporal_strength;
	u32 spatial_strength;

	/* dualwatch */
	unsigned long dualwatch_jiffies;
	u16 dualwatch_stereo_mode;

	/* Digitizer type */
	int digitizer;		/* 0x00EF = saa7114 0x00FO = saa7115 0x0106 = mic */

	struct mutex serialize_lock;    /* mutex used to serialize open/close/start/stop/ioctl operations */
	struct cx18_options options; 	/* User options */
	int stream_buf_size[CX18_MAX_STREAMS]; /* Stream buffer size */
	struct cx18_stream streams[CX18_MAX_STREAMS]; 	/* Stream data */
	unsigned long i_flags;  /* global cx18 flags */
	atomic_t capturing;	/* count number of active capture streams */
	spinlock_t lock;        /* lock access to this struct */
	int search_pack_header;

	spinlock_t dma_reg_lock; /* lock access to DMA engine registers */

	int open_id;		/* incremented each time an open occurs, used as
				   unique ID. Starts at 1, so 0 can be used as
				   uninitialized value in the stream->id. */

	u32 base_addr;
	struct v4l2_prio_state prio;

	u8 card_rev;
	void __iomem *enc_mem, *reg_mem;

	struct vbi_info vbi;

	u32 pgm_info_offset;
	u32 pgm_info_num;
	u32 pgm_info_write_idx;
	u32 pgm_info_read_idx;
	struct v4l2_enc_idx_entry pgm_info[CX18_MAX_PGM_INDEX];

	u64 mpg_data_received;
	u64 vbi_data_inserted;

	wait_queue_head_t mb_apu_waitq;
	wait_queue_head_t mb_cpu_waitq;
	wait_queue_head_t mb_epu_waitq;
	wait_queue_head_t mb_hpu_waitq;
	wait_queue_head_t cap_w;
	/* when the current DMA is finished this queue is woken up */
	wait_queue_head_t dma_waitq;

	/* i2c */
	struct i2c_adapter i2c_adap[2];
	struct i2c_algo_bit_data i2c_algo[2];
	struct cx18_i2c_algo_callback_data i2c_algo_cb_data[2];
	struct i2c_client i2c_client[2];
	struct mutex i2c_bus_lock[2];
	struct i2c_client *i2c_clients[I2C_CLIENTS_MAX];

	/* v4l2 and User settings */

	/* codec settings */
	u32 audio_input;
	u32 active_input;
	u32 active_output;
	v4l2_std_id std;
	v4l2_std_id tuner_std;	/* The norm of the tuner (fixed) */
};

/* Globals */
extern struct cx18 *cx18_cards[];
extern int cx18_cards_active;
extern int cx18_first_minor;
extern spinlock_t cx18_cards_lock;

/*==============Prototypes==================*/

/* Return non-zero if a signal is pending */
int cx18_msleep_timeout(unsigned int msecs, int intr);

/* Read Hauppauge eeprom */
struct tveeprom; /* forward reference */
void cx18_read_eeprom(struct cx18 *cx, struct tveeprom *tv);

/* First-open initialization: load firmware, etc. */
int cx18_init_on_first_open(struct cx18 *cx);

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

#define read_reg(reg) readl(cx->reg_mem + (reg))
#define write_reg(val, reg) writel(val, cx->reg_mem + (reg))
#define write_reg_sync(val, reg) \
	do { write_reg(val, reg); read_reg(reg); } while (0)

#define read_enc(addr) readl(cx->enc_mem + (u32)(addr))
#define write_enc(val, addr) writel(val, cx->enc_mem + (u32)(addr))
#define write_enc_sync(val, addr) \
	do { write_enc(val, addr); read_enc(addr); } while (0)

#define sw1_irq_enable(val) do { \
	write_reg(val, SW1_INT_STATUS); \
	write_reg(read_reg(SW1_INT_ENABLE_PCI) | (val), SW1_INT_ENABLE_PCI); \
} while (0)

#define sw1_irq_disable(val) \
	write_reg(read_reg(SW1_INT_ENABLE_PCI) & ~(val), SW1_INT_ENABLE_PCI);

#define sw2_irq_enable(val) do { \
	write_reg(val, SW2_INT_STATUS); \
	write_reg(read_reg(SW2_INT_ENABLE_PCI) | (val), SW2_INT_ENABLE_PCI); \
} while (0)

#define sw2_irq_disable(val) \
	write_reg(read_reg(SW2_INT_ENABLE_PCI) & ~(val), SW2_INT_ENABLE_PCI);

#define setup_page(addr) do { \
    u32 val = read_reg(0xD000F8) & ~0x1f00; \
    write_reg(val | (((addr) >> 17) & 0x1f00), 0xD000F8); \
} while (0)

#endif /* CX18_DRIVER_H */
