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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _BUZ_H_
#define _BUZ_H_

struct zoran_requestbuffers {
	unsigned long count;	/* Number of buffers for MJPEG grabbing */
	unsigned long size;	/* Size PER BUFFER in bytes */
};

struct zoran_sync {
	unsigned long frame;	/* number of buffer that has been free'd */
	unsigned long length;	/* number of code bytes in buffer (capture only) */
	unsigned long seq;	/* frame sequence number */
	struct timeval timestamp;	/* timestamp */
};

struct zoran_status {
	int input;		/* Input channel, has to be set prior to BUZIOC_G_STATUS */
	int signal;		/* Returned: 1 if valid video signal detected */
	int norm;		/* Returned: VIDEO_MODE_PAL or VIDEO_MODE_NTSC */
	int color;		/* Returned: 1 if color signal detected */
};

struct zoran_params {

	/* The following parameters can only be queried */

	int major_version;	/* Major version number of driver */
	int minor_version;	/* Minor version number of driver */

	/* Main control parameters */

	int input;		/* Input channel: 0 = Composite, 1 = S-VHS */
	int norm;		/* Norm: VIDEO_MODE_PAL or VIDEO_MODE_NTSC */
	int decimation;		/* decimation of captured video,
				 * enlargement of video played back.
				 * Valid values are 1, 2, 4 or 0.
				 * 0 is a special value where the user
				 * has full control over video scaling */

	/* The following parameters only have to be set if decimation==0,
	 * for other values of decimation they provide the data how the image is captured */

	int HorDcm;		/* Horizontal decimation: 1, 2 or 4 */
	int VerDcm;		/* Vertical decimation: 1 or 2 */
	int TmpDcm;		/* Temporal decimation: 1 or 2,
				 * if TmpDcm==2 in capture every second frame is dropped,
				 * in playback every frame is played twice */
	int field_per_buff;	/* Number of fields per buffer: 1 or 2 */
	int img_x;		/* start of image in x direction */
	int img_y;		/* start of image in y direction */
	int img_width;		/* image width BEFORE decimation,
				 * must be a multiple of HorDcm*16 */
	int img_height;		/* image height BEFORE decimation,
				 * must be a multiple of VerDcm*8 */

	/* --- End of parameters for decimation==0 only --- */

	/* JPEG control parameters */

	int quality;		/* Measure for quality of compressed images.
				 * Scales linearly with the size of the compressed images.
				 * Must be beetween 0 and 100, 100 is a compression
				 * ratio of 1:4 */

	int odd_even;		/* Which field should come first ??? */

	int APPn;		/* Number of APP segment to be written, must be 0..15 */
	int APP_len;		/* Length of data in JPEG APPn segment */
	char APP_data[60];	/* Data in the JPEG APPn segment. */

	int COM_len;		/* Length of data in JPEG COM segment */
	char COM_data[60];	/* Data in JPEG COM segment */

	unsigned long jpeg_markers;	/* Which markers should go into the JPEG output.
					 * Unless you exactly know what you do, leave them untouched.
					 * Inluding less markers will make the resulting code
					 * smaller, but there will be fewer aplications
					 * which can read it.
					 * The presence of the APP and COM marker is
					 * influenced by APP0_len and COM_len ONLY! */
#define JPEG_MARKER_DHT (1<<3)	/* Define Huffman Tables */
#define JPEG_MARKER_DQT (1<<4)	/* Define Quantization Tables */
#define JPEG_MARKER_DRI (1<<5)	/* Define Restart Interval */
#define JPEG_MARKER_COM (1<<6)	/* Comment segment */
#define JPEG_MARKER_APP (1<<7)	/* App segment, driver will allways use APP0 */

	int VFIFO_FB;		/* Flag for enabling Video Fifo Feedback.
				 * If this flag is turned on and JPEG decompressing
				 * is going to the screen, the decompress process
				 * is stopped every time the Video Fifo is full.
				 * This enables a smooth decompress to the screen
				 * but the video output signal will get scrambled */

	/* Misc */

	char reserved[312];	/* Makes 512 bytes for this structure */
};

/*
Private IOCTL to set up for displaying MJPEG
*/
#define BUZIOC_G_PARAMS       _IOR ('v', BASE_VIDIOCPRIVATE+0,  struct zoran_params)
#define BUZIOC_S_PARAMS       _IOWR('v', BASE_VIDIOCPRIVATE+1,  struct zoran_params)
#define BUZIOC_REQBUFS        _IOWR('v', BASE_VIDIOCPRIVATE+2,  struct zoran_requestbuffers)
#define BUZIOC_QBUF_CAPT      _IOW ('v', BASE_VIDIOCPRIVATE+3,  int)
#define BUZIOC_QBUF_PLAY      _IOW ('v', BASE_VIDIOCPRIVATE+4,  int)
#define BUZIOC_SYNC           _IOR ('v', BASE_VIDIOCPRIVATE+5,  struct zoran_sync)
#define BUZIOC_G_STATUS       _IOWR('v', BASE_VIDIOCPRIVATE+6,  struct zoran_status)


#ifdef __KERNEL__

#define MAJOR_VERSION 0		/* driver major version */
#define MINOR_VERSION 9		/* driver minor version */
#define RELEASE_VERSION 5	/* release version */

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

#define MAX_KMALLOC_MEM (128*1024)

#include "zr36057.h"

enum card_type {
	UNKNOWN = -1,

	/* Pinnacle/Miro */
	DC10_old,		/* DC30 like */
	DC10_new,		/* DC10plus like */
	DC10plus,
	DC30,
	DC30plus,

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

enum zoran_buffer_state {
	BUZ_STATE_USER,		/* buffer is owned by application */
	BUZ_STATE_PEND,		/* buffer is queued in pend[] ready to feed to I/O */
	BUZ_STATE_DMA,		/* buffer is queued in dma[] for I/O */
	BUZ_STATE_DONE		/* buffer is ready to return to application */
};

enum zoran_map_mode {
	ZORAN_MAP_MODE_RAW,
	ZORAN_MAP_MODE_JPG_REC,
#define ZORAN_MAP_MODE_JPG ZORAN_MAP_MODE_JPG_REC
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
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	int palette;
#endif
	__u32 fourcc;
	int colorspace;
	int depth;
	__u32 flags;
	__u32 vfespfr;
};
/* flags */
#define ZORAN_FORMAT_COMPRESSED 1<<0
#define ZORAN_FORMAT_OVERLAY    1<<1
#define ZORAN_FORMAT_CAPTURE	1<<2
#define ZORAN_FORMAT_PLAYBACK	1<<3

/* overlay-settings */
struct zoran_overlay_settings {
	int is_set;
	int x, y, width, height;	/* position */
	int clipcount;		/* position and number of clips */
	const struct zoran_format *format;	/* overlay format */
};

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

struct zoran_mapping {
	struct file *file;
	int count;
};

struct zoran_jpg_buffer {
	struct zoran_mapping *map;
	__le32 *frag_tab;		/* addresses of frag table */
	u32 frag_tab_bus;	/* same value cached to save time in ISR */
	enum zoran_buffer_state state;	/* non-zero if corresponding buffer is in use in grab queue */
	struct zoran_sync bs;	/* DONE: info to return to application */
};

struct zoran_v4l_buffer {
	struct zoran_mapping *map;
	char *fbuffer;		/* virtual  address of frame buffer */
	unsigned long fbuffer_phys;	/* physical address of frame buffer */
	unsigned long fbuffer_bus;	/* bus      address of frame buffer */
	enum zoran_buffer_state state;	/* state: unused/pending/done */
	struct zoran_sync bs;	/* DONE: info to return to application */
};

enum zoran_lock_activity {
	ZORAN_FREE,		/* free for use */
	ZORAN_ACTIVE,		/* active but unlocked */
	ZORAN_LOCKED,		/* locked */
};

/* buffer collections */
struct zoran_jpg_struct {
	enum zoran_lock_activity active;	/* feature currently in use? */
	struct zoran_jpg_buffer buffer[BUZ_MAX_FRAME];	/* buffers */
	int num_buffers, buffer_size;
	u8 allocated;		/* Flag if buffers are allocated  */
	u8 ready_to_be_freed;	/* hack - see zoran_driver.c */
	u8 need_contiguous;	/* Flag if contiguous buffers are needed */
};

struct zoran_v4l_struct {
	enum zoran_lock_activity active;	/* feature currently in use? */
	struct zoran_v4l_buffer buffer[VIDEO_MAX_FRAME];	/* buffers */
	int num_buffers, buffer_size;
	u8 allocated;		/* Flag if buffers are allocated  */
	u8 ready_to_be_freed;	/* hack - see zoran_driver.c */
};

struct zoran;

/* zoran_fh contains per-open() settings */
struct zoran_fh {
	struct zoran *zr;

	enum zoran_map_mode map_mode;	/* Flag which bufferset will map by next mmap() */

	struct zoran_overlay_settings overlay_settings;
	u32 *overlay_mask;	/* overlay mask */
	enum zoran_lock_activity overlay_active;	/* feature currently in use? */

	struct zoran_v4l_settings v4l_settings;	/* structure with a lot of things to play with */
	struct zoran_v4l_struct v4l_buffers;	/* V4L buffers' info */

	struct zoran_jpg_settings jpg_settings;	/* structure with a lot of things to play with */
	struct zoran_jpg_struct jpg_buffers;	/* MJPEG buffers' info */
};

struct card_info {
	enum card_type type;
	char name[32];
	u16 i2c_decoder, i2c_encoder;			/* I2C types */
	u16 video_vfe, video_codec;			/* videocodec types */
	u16 audio_chip;					/* audio type */

	int inputs;		/* number of video inputs */
	struct input {
		int muxsel;
		char name[32];
	} input[BUZ_MAX_INPUT];

	int norms;
	struct tvnorm *tvn[3];	/* supported TV norms */

	u32 jpeg_int;		/* JPEG interrupt */
	u32 vsync_int;		/* VSYNC interrupt */
	s8 gpio[ZR_GPIO_MAX];
	u8 gpcs[GPCS_MAX];

	struct vfe_polarity vfe_pol;
	u8 gpio_pol[ZR_GPIO_MAX];

	/* is the /GWS line conected? */
	u8 gws_not_connected;

	/* avs6eyes mux setting */
	u8 input_mux;

	void (*init) (struct zoran * zr);
};

struct zoran {
	struct video_device *video_dev;

	struct i2c_adapter i2c_adapter;	/* */
	struct i2c_algo_bit_data i2c_algo;	/* */
	u32 i2cbr;

	struct i2c_client *decoder;	/* video decoder i2c client */
	struct i2c_client *encoder;	/* video encoder i2c client */

	struct videocodec *codec;	/* video codec */
	struct videocodec *vfe;	/* video front end */

	struct mutex resource_lock;	/* prevent evil stuff */

	u8 initialized;		/* flag if zoran has been correctly initalized */
	int user;		/* number of current users */
	struct card_info card;
	struct tvnorm *timing;

	unsigned short id;	/* number of this device */
	char name[32];		/* name of this device */
	struct pci_dev *pci_dev;	/* PCI device */
	unsigned char revision;	/* revision of zr36057 */
	unsigned char __iomem *zr36057_mem;/* pointer to mapped IO memory */

	spinlock_t spinlock;	/* Spinlock */

	/* Video for Linux parameters */
	int input, norm;	/* card's norm and input - norm=VIDEO_MODE_* */
	int hue, saturation, contrast, brightness;	/* Current picture params */
	struct video_buffer buffer;	/* Current buffer params */
	struct zoran_overlay_settings overlay_settings;
	u32 *overlay_mask;	/* overlay mask */
	enum zoran_lock_activity overlay_active;	/* feature currently in use? */

	wait_queue_head_t v4l_capq;

	int v4l_overlay_active;	/* Overlay grab is activated */
	int v4l_memgrab_active;	/* Memory grab is activated */

	int v4l_grab_frame;	/* Frame number being currently grabbed */
#define NO_GRAB_ACTIVE (-1)
	unsigned long v4l_grab_seq;	/* Number of frames grabbed */
	struct zoran_v4l_settings v4l_settings;	/* structure with a lot of things to play with */

	/* V4L grab queue of frames pending */
	unsigned long v4l_pend_head;
	unsigned long v4l_pend_tail;
	unsigned long v4l_sync_tail;
	int v4l_pend[V4L_MAX_FRAME];
	struct zoran_v4l_struct v4l_buffers;	/* V4L buffers' info */

	/* Buz MJPEG parameters */
	enum zoran_codec_mode codec_mode;	/* status of codec */
	struct zoran_jpg_settings jpg_settings;	/* structure with a lot of things to play with */

	wait_queue_head_t jpg_capq;	/* wait here for grab to finish */

	/* grab queue counts/indices, mask with BUZ_MASK_STAT_COM before using as index */
	/* (dma_head - dma_tail) is number active in DMA, must be <= BUZ_NUM_STAT_COM */
	/* (value & BUZ_MASK_STAT_COM) corresponds to index in stat_com table */
	unsigned long jpg_que_head;	/* Index where to put next buffer which is queued */
	unsigned long jpg_dma_head;	/* Index of next buffer which goes into stat_com  */
	unsigned long jpg_dma_tail;	/* Index of last buffer in stat_com               */
	unsigned long jpg_que_tail;	/* Index of last buffer in queue                  */
	unsigned long jpg_seq_num;	/* count of frames since grab/play started        */
	unsigned long jpg_err_seq;	/* last seq_num before error                      */
	unsigned long jpg_err_shift;
	unsigned long jpg_queued_num;	/* count of frames queued since grab/play started */

	/* zr36057's code buffer table */
	__le32 *stat_com;		/* stat_com[i] is indexed by dma_head/tail & BUZ_MASK_STAT_COM */

	/* (value & BUZ_MASK_FRAME) corresponds to index in pend[] queue */
	int jpg_pend[BUZ_MAX_FRAME];

	/* array indexed by frame number */
	struct zoran_jpg_struct jpg_buffers;	/* MJPEG buffers' info */

	/* Additional stuff for testing */
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *zoran_proc;
#else
	void *zoran_proc;
#endif
	int testing;
	int jpeg_error;
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

	u32 last_isr;
	unsigned long frame_num;

	wait_queue_head_t test_q;
};

/* There was something called _ALPHA_BUZ that used the PCI address instead of
 * the kernel iomapped address for btread/btwrite.  */
#define btwrite(dat,adr)    writel((dat), zr->zr36057_mem+(adr))
#define btread(adr)         readl(zr->zr36057_mem+(adr))

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

#endif				/* __kernel__ */

#endif
