/*
 *  tw68 driver common header file
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
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

#include <linux/version.h>
#define	TW68_VERSION_CODE	KERNEL_VERSION(0, 0, 8)

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev2.h>
#include <linux/kdev_t.h>
#include <linux/input.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <asm/io.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>

#include <media/tuner.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
#  include <media/ir-common.h>
#endif
#include <media/ir-kbd-i2c.h>
#include <media/videobuf-dma-sg.h>

#include "btcx-risc.h"
#include "tw68-reg.h"

#define	UNSET	(-1U)

/*
 * dprintk statement within the code use a 'level' argument.  For
 * our purposes, we use the following levels:
 */
#define	DBG_UNEXPECTED		(1 << 0)
#define	DBG_UNUSUAL		(1 << 1)
#define	DBG_TESTING		(1 << 2)
#define	DBG_BUFF		(1 << 3)
#define	DBG_FLOW		(1 << 15)

/* system vendor and device ID's */
#define	PCI_VENDOR_ID_TECHWELL	0x1797
#define	PCI_DEVICE_ID_6800	0x6800
#define	PCI_DEVICE_ID_6801	0x6801
#define	PCI_DEVICE_ID_AUDIO2	0x6802
#define	PCI_DEVICE_ID_TS3	0x6803
#define	PCI_DEVICE_ID_6804	0x6804
#define	PCI_DEVICE_ID_AUDIO5	0x6805
#define	PCI_DEVICE_ID_TS6	0x6806

/* tw6816 based cards */
#define	PCI_DEVICE_ID_6816_1   0x6810
#define	PCI_DEVICE_ID_6816_2   0x6811
#define	PCI_DEVICE_ID_6816_3   0x6812
#define	PCI_DEVICE_ID_6816_4   0x6813

/* subsystem vendor ID's */
#define	TW68_PCI_ID_TECHWELL	0x1797

#define TW68_NORMS (\
	V4L2_STD_NTSC   | V4L2_STD_PAL       | V4L2_STD_SECAM    | \
	V4L2_STD_PAL_BG | V4L2_STD_PAL_DK    | V4L2_STD_PAL_I    | \
	V4L2_STD_PAL_M  | V4L2_STD_PAL_Nc    | V4L2_STD_PAL_60   | \
	V4L2_STD_525_60 | V4L2_STD_625_50    | \
	V4L2_STD_SECAM_L| V4L2_STD_SECAM_LC  | V4L2_STD_SECAM_DK)

#define	TW68_VID_INTS	(TW68_FFERR | TW68_PABORT | TW68_DMAPERR | \
			 TW68_FFOF   | TW68_DMAPI)
/* TW6800 chips have trouble with these, so we don't set them for that chip */
#define	TW68_VID_INTSX	(TW68_FDMIS | TW68_HLOCK | TW68_VLOCK)

#define	TW68_I2C_INTS	(TW68_SBERR | TW68_SBDONE | TW68_SBERR2  | \
			 TW68_SBDONE2)

typedef enum {
	TW6800,
	TW6801,
	TW6804,
	TWXXXX,
} TW68_DECODER_TYPE;
/* ----------------------------------------------------------- */
/* static data                                                 */

struct tw68_tvnorm {
	char		*name;
	v4l2_std_id	id;

	/* video decoder */
	u32	sync_control;
	u32	luma_control;
	u32	chroma_ctrl1;
	u32	chroma_gain;
	u32	chroma_ctrl2;
	u32	vgate_misc;

	/* video scaler */
	u32	h_delay;
	u32	h_delay0;	/* for TW6800 */
	u32	h_start;
	u32	h_stop;
	u32	v_delay;
	u32	video_v_start;
	u32	video_v_stop;
	u32	vbi_v_start_0;
	u32	vbi_v_stop_0;
	u32	vbi_v_start_1;

	/* Techwell specific */
	u32	format;
};

struct tw68_format {
	char	*name;
	u32	fourcc;
	u32	depth;
	u32	twformat;
};

/* ----------------------------------------------------------- */
/* card configuration					  */

#define TW68_BOARD_NOAUTO		UNSET
#define TW68_BOARD_UNKNOWN		0
#define	TW68_BOARD_GENERIC_6802		1

#define	TW68_MAXBOARDS			16
#define	TW68_INPUT_MAX			8

/* ----------------------------------------------------------- */
/* enums						       */

enum tw68_mpeg_type {
	TW68_MPEG_UNUSED,
	TW68_MPEG_EMPRESS,
	TW68_MPEG_DVB,
};

enum tw68_audio_in {
	TV      = 1,
	LINE1   = 2,
	LINE2   = 3,
	LINE2_LEFT,
};

enum tw68_video_out {
	CCIR656 = 1,
};

/* Structs for card definition */
struct tw68_input {
	char			*name;		/* text description */
	unsigned int		vmux;		/* mux value */
	enum tw68_audio_in	mux;
	unsigned int		gpio;
	unsigned int		tv:1;
};

struct tw68_board {
	char			*name;
	unsigned int		audio_clock;

	/* input switching */
	unsigned int		gpiomask;
	struct tw68_input	inputs[TW68_INPUT_MAX];
	struct tw68_input	radio;
	struct tw68_input	mute;

	/* i2c chip info */
	unsigned int		tuner_type;
	unsigned int		radio_type;
	unsigned char		tuner_addr;
	unsigned char		radio_addr;

	unsigned int		tda9887_conf;
	unsigned int		tuner_config;

	enum tw68_video_out	video_out;
	enum tw68_mpeg_type	mpeg;
	unsigned int		vid_port_opts;
};

#define card_has_radio(dev)	(NULL != tw68_boards[dev->board].radio.name)
#define card_has_mpeg(dev)	(TW68_MPEG_UNUSED != \
					tw68_boards[dev->board].mpeg)
#define card_in(dev, n)		(tw68_boards[dev->board].inputs[n])
#define card(dev)		(tw68_boards[dev->board])

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define	RESOURCE_VIDEO			1
#define	RESOURCE_VBI			2

#define	INTERLACE_AUTO			0
#define	INTERLACE_ON			1
#define	INTERLACE_OFF			2

#define	BUFFER_TIMEOUT	msecs_to_jiffies(500)	/* 0.5 seconds */

struct tw68_dev;	/* forward delclaration */

/* tvaudio thread status */
struct tw68_thread {
	struct task_struct	*thread;
	unsigned int		scan1;
	unsigned int		scan2;
	unsigned int		mode;
	unsigned int		stopped;
};

/* buffer for one video/vbi/ts frame */
struct tw68_buf {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* tw68 specific */
	struct tw68_format	*fmt;
	struct tw68_input	*input;
	unsigned int		top_seen;
	int (*activate)(struct tw68_dev *dev,
			struct tw68_buf *buf,
			struct tw68_buf *next);
	struct btcx_riscmem	risc;
	unsigned int		bpl;
};

struct tw68_dmaqueue {
	struct tw68_dev		*dev;
	struct list_head	active;
	struct list_head	queued;
	struct timer_list	timeout;
	struct btcx_riscmem	stopper;
	int (*buf_compat)(struct tw68_buf *prev,
			  struct tw68_buf *buf);
	int (*start_dma)(struct tw68_dev *dev,
			 struct tw68_dmaqueue *q,
			 struct tw68_buf *buf);
};

/* video filehandle status */
struct tw68_fh {
	struct tw68_dev		*dev;
	unsigned int		radio;
	enum v4l2_buf_type	type;
	unsigned int		resources;
	enum v4l2_priority	prio;

	/* video capture */
	struct tw68_format	*fmt;
	unsigned int		width, height;
	struct videobuf_queue	cap;	/* also used for overlay */

	/* vbi capture */
	struct videobuf_queue	vbi;
};

/* dmasound dsp status */
struct tw68_dmasound {
	struct mutex		lock;
	int			minor_mixer;
	int			minor_dsp;
	unsigned int		users_dsp;

	/* mixer */
	enum tw68_audio_in	input;
	unsigned int		count;
	unsigned int		line1;
	unsigned int		line2;

	/* dsp */
	unsigned int		afmt;
	unsigned int		rate;
	unsigned int		channels;
	unsigned int		recording_on;
	unsigned int		dma_running;
	unsigned int		blocks;
	unsigned int		blksize;
	unsigned int		bufsize;
	struct videobuf_dmabuf	dma;
	unsigned int		dma_blk;
	unsigned int		read_offset;
	unsigned int		read_count;
	void 			*priv_data;
	struct snd_pcm_substream	*substream;
};

struct tw68_fmt {
	char			*name;
	u32			fourcc;	/* v4l2 format id */
	int			depth;
	int			flags;
	u32			twformat;
};

/* ts/mpeg status */
struct tw68_ts {
	/* TS capture */
	int			nr_packets;
	int			nr_bufs;
};

/* ts/mpeg ops */
struct tw68_mpeg_ops {
	enum tw68_mpeg_type	type;
	struct list_head	next;
	int			(*init)(struct tw68_dev *dev);
	int			(*fini)(struct tw68_dev *dev);
	void			(*signal_change)(struct tw68_dev *dev);
};

enum tw68_ts_status {
	TW68_TS_STOPPED,
	TW68_TS_BUFF_DONE,
	TW68_TS_STARTED,
};

/* global device status */
struct tw68_dev {
	struct list_head	devlist;
	struct mutex		lock;
	spinlock_t		slock;
	struct v4l2_prio_state	prio;
	struct v4l2_device	v4l2_dev;
	/* workstruct for loading modules */
	struct work_struct request_module_wk;

	/* insmod option/autodetected */
	int			autodetected;

	/* various device info */
	TW68_DECODER_TYPE	vdecoder;
	unsigned int		resources;
	struct video_device	*video_dev;
	struct video_device	*radio_dev;
	struct video_device	*vbi_dev;
	struct tw68_dmasound	dmasound;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
	/* infrared remote */
	int			has_remote;
	struct card_ir		*remote;
#endif

	/* pci i/o */
	char			name[32];
	int			nr;
	struct pci_dev		*pci;
	unsigned char		pci_rev, pci_lat;
	u32			__iomem *lmmio;
	u8			__iomem *bmmio;
	u32			pci_irqmask;
	/* The irq mask to be used will depend upon the chip type */
	u32			board_virqmask;

	/* config info */
	unsigned int		board;
	unsigned int		tuner_type;
	unsigned int 		radio_type;
	unsigned char		tuner_addr;
	unsigned char		radio_addr;

	unsigned int		tda9887_conf;
	unsigned int		gpio_value;

	/* i2c i/o */
	struct i2c_algo_bit_data i2c_algo;
	struct i2c_adapter	i2c_adap;
	struct i2c_client	i2c_client;
	u32			i2c_state;
	u32			i2c_done;
	wait_queue_head_t	i2c_queue;
	int			i2c_rc;
	unsigned char		eedata[256];

	/* video+ts+vbi capture */
	struct tw68_dmaqueue	video_q;
	struct tw68_dmaqueue	vbi_q;
	unsigned int		video_fieldcount;
	unsigned int		vbi_fieldcount;

	/* various v4l controls */
	struct tw68_tvnorm	*tvnorm;	/* video */
	struct tw68_tvaudio	*tvaudio;
#if 0
	unsigned int		ctl_input;
	int			ctl_bright;
	int			ctl_contrast;
	int			ctl_hue;
	int			ctl_saturation;
	int			ctl_freq;
	int			ctl_mute;	/* audio */
	int			ctl_volume;
	int			ctl_invert;	/* private */
	int			ctl_mirror;
	int			ctl_y_odd;
	int			ctl_y_even;
	int			ctl_automute;
#endif

	/* crop */
	struct v4l2_rect	crop_bounds;
	struct v4l2_rect	crop_defrect;
	struct v4l2_rect	crop_current;

	/* other global state info */
	unsigned int		automute;
	struct tw68_thread	thread;
	/* input is latest requested by app, hw_input is current hw setting */
	struct tw68_input	*input;
	struct tw68_input	*hw_input;
	unsigned int		hw_mute;
	int			last_carrier;
	int			nosignal;
	unsigned int		insuspend;

	/* TW68_MPEG_* */
	struct tw68_ts		ts;
	struct tw68_dmaqueue	ts_q;
	enum tw68_ts_status 	ts_state;
	unsigned int 		buff_cnt;
	struct tw68_mpeg_ops	*mops;

	void (*gate_ctrl)(struct tw68_dev *dev, int open);
};

/* ----------------------------------------------------------- */

#define tw_readl(reg)		readl(dev->lmmio + ((reg) >> 2))
#define	tw_readb(reg)		readb(dev->bmmio + (reg))
#define tw_writel(reg, value)	writel((value), dev->lmmio + ((reg) >> 2))
#define	tw_writeb(reg, value)	writeb((value), dev->bmmio + (reg))

#define tw_andorl(reg, mask, value) \
		writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
		((value) & (mask)), dev->lmmio+((reg)>>2))
#define	tw_andorb(reg, mask, value) \
		writeb((readb(dev->bmmio + (reg)) & ~(mask)) |\
		((value) & (mask)), dev->bmmio+(reg))
#define tw_setl(reg, bit)	tw_andorl((reg), (bit), (bit))
#define	tw_setb(reg, bit)	tw_andorb((reg), (bit), (bit))
#define	tw_clearl(reg, bit)	\
		writel((readl(dev->lmmio + ((reg) >> 2)) & ~(bit)), \
		dev->lmmio + ((reg) >> 2))
#define	tw_clearb(reg, bit)	\
		writeb((readb(dev->bmmio+(reg)) & ~(bit)), \
		dev->bmmio + (reg))
#define tw_call_all(dev, o, f, args...) do {				\
	if (dev->gate_ctrl)						\
		dev->gate_ctrl(dev, 1);					\
	v4l2_device_call_all(&(dev)->v4l2_dev, 0, o, f , ##args);	\
	if (dev->gate_ctrl)						\
		dev->gate_ctrl(dev, 0);					\
} while (0)

#define tw_wait(us) { udelay(us); }

static inline struct tw68_dev *to_tw68_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct tw68_dev, v4l2_dev);
}

/* ----------------------------------------------------------- */
/* tw68-core.c                                                */

extern struct list_head  tw68_devlist;
extern struct mutex tw68_devlist_lock;
extern unsigned int irq_debug;

int tw68_buffer_count(unsigned int size, unsigned int count);
void tw68_buffer_queue(struct tw68_dev *dev, struct tw68_dmaqueue *q,
		      struct tw68_buf *buf);
void tw68_buffer_timeout(unsigned long data);
int tw68_set_dmabits(struct tw68_dev *dev);
void tw68_dma_free(struct videobuf_queue *q, struct tw68_buf *buf);
void tw68_wakeup(struct tw68_dmaqueue *q, unsigned int *field_count);
int tw68_buffer_requeue(struct tw68_dev *dev, struct tw68_dmaqueue *q);

/* ----------------------------------------------------------- */
/* tw68-cards.c                                                */

extern struct tw68_board tw68_boards[];
extern const unsigned int tw68_bcount;
extern struct pci_device_id __devinitdata tw68_pci_tbl[];

int tw68_board_init1(struct tw68_dev *dev);
int tw68_board_init2(struct tw68_dev *dev);
int tw68_tuner_callback(void *priv, int component, int command, int arg);

/* ----------------------------------------------------------- */
/* tw68-i2c.c                                                  */

int tw68_i2c_register(struct tw68_dev *dev);
int tw68_i2c_unregister(struct tw68_dev *dev);
void tw68_irq_i2c(struct tw68_dev *dev, int status);

/* ----------------------------------------------------------- */
/* tw68-video.c                                                */

extern unsigned int video_debug;
extern struct video_device tw68_video_template;
extern struct video_device tw68_radio_template;

int tw68_videoport_init(struct tw68_dev *dev);
void tw68_set_tvnorm_hw(struct tw68_dev *dev);

int tw68_video_init1(struct tw68_dev *dev);
int tw68_video_init2(struct tw68_dev *dev);
void tw68_irq_video_signalchange(struct tw68_dev *dev);
void tw68_irq_video_done(struct tw68_dev *dev, unsigned long status);

/* ----------------------------------------------------------- */
/* tw68-ts.c                                                   */

int tw68_ts_init1(struct tw68_dev *dev);
int tw68_ts_fini(struct tw68_dev *dev);
void tw68_irq_ts_done(struct tw68_dev *dev, unsigned long status);

int tw68_ts_register(struct tw68_mpeg_ops *ops);
void tw68_ts_unregister(struct tw68_mpeg_ops *ops);

int tw68_ts_init_hw(struct tw68_dev *dev);

/* ----------------------------------------------------------- */
/* tw68-vbi.c                                                  */

extern struct videobuf_queue_ops tw68_vbi_qops;
extern struct video_device tw68_vbi_template;

int tw68_vbi_init1(struct tw68_dev *dev);
int tw68_vbi_fini(struct tw68_dev *dev);
void tw68_irq_vbi_done(struct tw68_dev *dev, unsigned long status);

/* ----------------------------------------------------------- */
/* tw68-tvaudio.c                                              */

int tw68_tvaudio_rx2mode(u32 rx);

void tw68_tvaudio_setmute(struct tw68_dev *dev);
void tw68_tvaudio_setinput(struct tw68_dev *dev,
			      struct tw68_input *in);
void tw68_tvaudio_setvolume(struct tw68_dev *dev, int level);
int tw68_tvaudio_getstereo(struct tw68_dev *dev);
void tw68_tvaudio_init(struct tw68_dev *dev);
int tw68_tvaudio_init2(struct tw68_dev *dev);
int tw68_tvaudio_fini(struct tw68_dev *dev);
int tw68_tvaudio_do_scan(struct tw68_dev *dev);
int tw_dsp_writel(struct tw68_dev *dev, int reg, u32 value);
void tw68_enable_i2s(struct tw68_dev *dev);

/* ----------------------------------------------------------- */
/* tw68-risc.c                                                 */

int tw68_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
	struct scatterlist *sglist, unsigned int top_offset,
	unsigned int bottom_offset, unsigned int bpl,
	unsigned int padding, unsigned int lines);
int tw68_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc);
int tw68_risc_overlay(struct tw68_fh *fh, struct btcx_riscmem *risc,
		      int field_type);
