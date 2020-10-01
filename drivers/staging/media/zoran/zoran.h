/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zoran - Iomega Buz driver
 *
 * Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *
 * based on
 *
 * zoran.0.0.3 Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * and
 *
 * bttv - Bt848 frame grabber driver
 * Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                        & Marcus Metzler (mocm@thp.uni-koeln.de)
 */

#ifndef _BUZ_H_
#define _BUZ_H_

#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#define ZR_NORM_PAL 0
#define ZR_NORM_NTSC 1
#define ZR_NORM_SECAM 2

struct zr_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer          vbuf;
	struct list_head                queue;
};

static inline struct zr_buffer *vb2_to_zr_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	return container_of(vbuf, struct zr_buffer, vbuf);
}

#define ZORAN_NAME    "ZORAN"	/* name of the device */

#define ZR_DEVNAME(zr) ((zr)->name)

#define   BUZ_MAX_WIDTH   (zr->timing->Wa)
#define   BUZ_MAX_HEIGHT  (zr->timing->Ha)
#define   BUZ_MIN_WIDTH    32	/* never display less than 32 pixels */
#define   BUZ_MIN_HEIGHT   24	/* never display less than 24 rows */

#define BUZ_NUM_STAT_COM    4
#define BUZ_MASK_STAT_COM   3

#define BUZ_MAX_FRAME     256	/* Must be a power of 2 */
#define BUZ_MASK_FRAME    255	/* Must be BUZ_MAX_FRAME-1 */

#define BUZ_MAX_INPUT       16

#if VIDEO_MAX_FRAME <= 32
#   define   V4L_MAX_FRAME   32
#elif VIDEO_MAX_FRAME <= 64
#   define   V4L_MAX_FRAME   64
#else
#   error   "Too many video frame buffers to handle"
#endif
#define   V4L_MASK_FRAME   (V4L_MAX_FRAME - 1)

#define MAX_FRAME (BUZ_MAX_FRAME > VIDEO_MAX_FRAME ? BUZ_MAX_FRAME : VIDEO_MAX_FRAME)

#include "zr36057.h"

enum card_type {
	UNKNOWN = -1,

	/* Pinnacle/Miro */
	DC10_OLD,		/* DC30 like */
	DC10_NEW,		/* DC10_PLUS like */
	DC10_PLUS,
	DC30,
	DC30_PLUS,

	/* Linux Media Labs */
	LML33,
	LML33R10,

	/* Iomega */
	BUZ,

	/* AverMedia */
	AVS6EYES,

	/* total number of cards */
	NUM_CARDS
};

enum zoran_codec_mode {
	BUZ_MODE_IDLE,		/* nothing going on */
	BUZ_MODE_MOTION_COMPRESS,	/* grabbing frames */
	BUZ_MODE_MOTION_DECOMPRESS,	/* playing frames */
	BUZ_MODE_STILL_COMPRESS,	/* still frame conversion */
	BUZ_MODE_STILL_DECOMPRESS	/* still frame conversion */
};

enum zoran_map_mode {
	ZORAN_MAP_MODE_NONE,
	ZORAN_MAP_MODE_RAW,
	ZORAN_MAP_MODE_JPG_REC,
	ZORAN_MAP_MODE_JPG_PLAY,
};

enum gpio_type {
	ZR_GPIO_JPEG_SLEEP = 0,
	ZR_GPIO_JPEG_RESET,
	ZR_GPIO_JPEG_FRAME,
	ZR_GPIO_VID_DIR,
	ZR_GPIO_VID_EN,
	ZR_GPIO_VID_RESET,
	ZR_GPIO_CLK_SEL1,
	ZR_GPIO_CLK_SEL2,
	ZR_GPIO_MAX,
};

enum gpcs_type {
	GPCS_JPEG_RESET = 0,
	GPCS_JPEG_START,
	GPCS_MAX,
};

struct zoran_format {
	char *name;
	__u32 fourcc;
	int colorspace;
	int depth;
	__u32 flags;
	__u32 vfespfr;
};

/* flags */
#define ZORAN_FORMAT_COMPRESSED BIT(0)
#define ZORAN_FORMAT_OVERLAY BIT(1)
#define ZORAN_FORMAT_CAPTURE BIT(2)
#define ZORAN_FORMAT_PLAYBACK BIT(3)

/* v4l-capture settings */
struct zoran_v4l_settings {
	int width, height, bytesperline;	/* capture size */
	const struct zoran_format *format;	/* capture format */
};

/* jpg-capture/-playback settings */
struct zoran_jpg_settings {
	int decimation;		/* this bit is used to set everything to default */
	int HorDcm, VerDcm, TmpDcm;	/* capture decimation settings (TmpDcm=1 means both fields) */
	int field_per_buff, odd_even;	/* field-settings (odd_even=1 (+TmpDcm=1) means top-field-first) */
	int img_x, img_y, img_width, img_height;	/* crop settings (subframe capture) */
	struct v4l2_jpegcompression jpg_comp;	/* JPEG-specific capture settings */
};


struct zoran;

/* zoran_fh contains per-open() settings */
struct zoran_fh {
	struct v4l2_fh fh;
	struct zoran *zr;
};

struct card_info {
	enum card_type type;
	char name[32];
	const char *i2c_decoder;	/* i2c decoder device */
	const unsigned short *addrs_decoder;
	const char *i2c_encoder;	/* i2c encoder device */
	const unsigned short *addrs_encoder;
	u16 video_vfe, video_codec;			/* videocodec types */
	u16 audio_chip;					/* audio type */

	int inputs;		/* number of video inputs */
	struct input {
		int muxsel;
		char name[32];
	} input[BUZ_MAX_INPUT];

	v4l2_std_id norms;
	const struct tvnorm *tvn[3];	/* supported TV norms */

	u32 jpeg_int;		/* JPEG interrupt */
	u32 vsync_int;		/* VSYNC interrupt */
	s8 gpio[ZR_GPIO_MAX];
	u8 gpcs[GPCS_MAX];

	struct vfe_polarity vfe_pol;
	u8 gpio_pol[ZR_GPIO_MAX];

	/* is the /GWS line connected? */
	u8 gws_not_connected;

	/* avs6eyes mux setting */
	u8 input_mux;

	void (*init)(struct zoran *zr);
};

struct zoran {
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;
	struct video_device *video_dev;
	struct vb2_queue vq;

	struct i2c_adapter i2c_adapter;	/* */
	struct i2c_algo_bit_data i2c_algo;	/* */
	u32 i2cbr;

	struct v4l2_subdev *decoder;	/* video decoder sub-device */
	struct v4l2_subdev *encoder;	/* video encoder sub-device */

	struct videocodec *codec;	/* video codec */
	struct videocodec *vfe;	/* video front end */

	struct mutex lock;	/* file ops serialize lock */

	u8 initialized;		/* flag if zoran has been correctly initialized */
	struct card_info card;
	const struct tvnorm *timing;

	unsigned short id;	/* number of this device */
	char name[32];		/* name of this device */
	struct pci_dev *pci_dev;	/* PCI device */
	unsigned char revision;	/* revision of zr36057 */
	unsigned char __iomem *zr36057_mem;/* pointer to mapped IO memory */

	spinlock_t spinlock;	/* Spinlock */

	/* Video for Linux parameters */
	int input;	/* card's norm and input */
	v4l2_std_id norm;

	/* Current buffer params */
	unsigned int buffer_size;

	struct zoran_v4l_settings v4l_settings;	/* structure with a lot of things to play with */

	/* Buz MJPEG parameters */
	enum zoran_codec_mode codec_mode;	/* status of codec */
	struct zoran_jpg_settings jpg_settings;	/* structure with a lot of things to play with */

	/* grab queue counts/indices, mask with BUZ_MASK_STAT_COM before using as index */
	/* (dma_head - dma_tail) is number active in DMA, must be <= BUZ_NUM_STAT_COM */
	/* (value & BUZ_MASK_STAT_COM) corresponds to index in stat_com table */
	unsigned long jpg_que_head;	/* Index where to put next buffer which is queued */
	unsigned long jpg_dma_head;	/* Index of next buffer which goes into stat_com */
	unsigned long jpg_dma_tail;	/* Index of last buffer in stat_com */
	unsigned long jpg_que_tail;	/* Index of last buffer in queue */
	unsigned long jpg_seq_num;	/* count of frames since grab/play started */
	unsigned long jpg_err_seq;	/* last seq_num before error */
	unsigned long jpg_err_shift;
	unsigned long jpg_queued_num;	/* count of frames queued since grab/play started */
	unsigned long vbseq;

	/* zr36057's code buffer table */
	__le32 *stat_com;		/* stat_com[i] is indexed by dma_head/tail & BUZ_MASK_STAT_COM */

	/* Additional stuff for testing */
	unsigned int ghost_int;
	int intr_counter_GIRQ1;
	int intr_counter_GIRQ0;
	int intr_counter_CodRepIRQ;
	int intr_counter_JPEGRepIRQ;
	int field_counter;
	int IRQ1_in;
	int IRQ1_out;
	int JPEG_in;
	int JPEG_out;
	int JPEG_0;
	int JPEG_1;
	int END_event_missed;
	int JPEG_missed;
	int JPEG_error;
	int num_errors;
	int JPEG_max_missed;
	int JPEG_min_missed;
	unsigned int prepared;
	unsigned int queued;

	u32 last_isr;
	unsigned long frame_num;
	int running;
	int buf_in_reserve;

	dma_addr_t p_sc;
	__le32 *stat_comb;
	dma_addr_t p_scb;
	enum zoran_map_mode map_mode;
	struct list_head queued_bufs;
	spinlock_t queued_bufs_lock; /* Protects queued_bufs */
	struct zr_buffer *inuse[BUZ_NUM_STAT_COM * 2];
};

static inline struct zoran *to_zoran(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct zoran, v4l2_dev);
}

/* There was something called _ALPHA_BUZ that used the PCI address instead of
 * the kernel iomapped address for btread/btwrite.  */
#define btwrite(dat, adr)    writel((dat), zr->zr36057_mem + (adr))
#define btread(adr)         readl(zr->zr36057_mem + (adr))

#define btand(dat, adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat, adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat, mask, adr) btwrite((dat) | ((mask) & btread(adr)), adr)

#endif

int zoran_queue_init(struct zoran *zr, struct vb2_queue *vq);
void zoran_queue_exit(struct zoran *zr);
int zr_set_buf(struct zoran *zr);
