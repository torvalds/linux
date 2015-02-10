/*
 * Driver for the VINO (Video In No Out) system found in SGI Indys.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * Copyright (C) 2004,2005 Mikael Nousiainen <tmnousia@cc.hut.fi>
 *
 * Based on the previous version of the driver for 2.4 kernels by:
 * Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 *
 * v4l2_device/v4l2_subdev conversion by:
 * Copyright (C) 2009 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * Note: this conversion is untested! Please contact the linux-media
 * mailinglist if you can test this, together with the test results.
 */

/*
 * TODO:
 * - remove "mark pages reserved-hacks" from memory allocation code
 *   and implement fault()
 * - check decimation, calculating and reporting image size when
 *   using decimation
 * - implement read(), user mode buffers and overlay (?)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/kmod.h>

#include <linux/i2c.h>

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/mutex.h>

#include <asm/paccess.h>
#include <asm/io.h>
#include <asm/sgi/ip22.h>
#include <asm/sgi/mc.h>

#include "vino.h"
#include "saa7191.h"
#include "indycam.h"

/* Uncomment the following line to get lots and lots of (mostly useless)
 * debug info.
 * Note that the debug output also slows down the driver significantly */
// #define VINO_DEBUG
// #define VINO_DEBUG_INT

#define VINO_MODULE_VERSION "0.0.7"

MODULE_DESCRIPTION("SGI VINO Video4Linux2 driver");
MODULE_VERSION(VINO_MODULE_VERSION);
MODULE_AUTHOR("Mikael Nousiainen <tmnousia@cc.hut.fi>");
MODULE_LICENSE("GPL");

#ifdef VINO_DEBUG
#define dprintk(x...) printk("VINO: " x);
#else
#define dprintk(x...)
#endif

#define VINO_NO_CHANNEL			0
#define VINO_CHANNEL_A			1
#define VINO_CHANNEL_B			2

#define VINO_PAL_WIDTH			768
#define VINO_PAL_HEIGHT			576
#define VINO_NTSC_WIDTH			640
#define VINO_NTSC_HEIGHT		480

#define VINO_MIN_WIDTH			32
#define VINO_MIN_HEIGHT			32

#define VINO_CLIPPING_START_ODD_D1	1
#define VINO_CLIPPING_START_ODD_PAL	15
#define VINO_CLIPPING_START_ODD_NTSC	12

#define VINO_CLIPPING_START_EVEN_D1	2
#define VINO_CLIPPING_START_EVEN_PAL	15
#define VINO_CLIPPING_START_EVEN_NTSC	12

#define VINO_INPUT_CHANNEL_COUNT	3

/* the number is the index for vino_inputs */
#define VINO_INPUT_NONE			-1
#define VINO_INPUT_COMPOSITE		0
#define VINO_INPUT_SVIDEO		1
#define VINO_INPUT_D1			2

#define VINO_PAGE_RATIO			(PAGE_SIZE / VINO_PAGE_SIZE)

#define VINO_FIFO_THRESHOLD_DEFAULT	16

#define VINO_FRAMEBUFFER_SIZE		((VINO_PAL_WIDTH \
					  * VINO_PAL_HEIGHT * 4 \
					  + 3 * PAGE_SIZE) & ~(PAGE_SIZE - 1))

#define VINO_FRAMEBUFFER_COUNT_MAX	8

#define VINO_FRAMEBUFFER_UNUSED		0
#define VINO_FRAMEBUFFER_IN_USE		1
#define VINO_FRAMEBUFFER_READY		2

#define VINO_QUEUE_ERROR		-1
#define VINO_QUEUE_MAGIC		0x20050125

#define VINO_MEMORY_NONE		0
#define VINO_MEMORY_MMAP		1
#define VINO_MEMORY_USERPTR		2

#define VINO_DUMMY_DESC_COUNT		4
#define VINO_DESC_FETCH_DELAY		5	/* microseconds */

#define VINO_MAX_FRAME_SKIP_COUNT	128

/* the number is the index for vino_data_formats */
#define VINO_DATA_FMT_NONE		-1
#define VINO_DATA_FMT_GREY		0
#define VINO_DATA_FMT_RGB332		1
#define VINO_DATA_FMT_RGB32		2
#define VINO_DATA_FMT_YUV		3

#define VINO_DATA_FMT_COUNT		4

/* the number is the index for vino_data_norms */
#define VINO_DATA_NORM_NONE		-1
#define VINO_DATA_NORM_NTSC		0
#define VINO_DATA_NORM_PAL		1
#define VINO_DATA_NORM_SECAM		2
#define VINO_DATA_NORM_D1		3

#define VINO_DATA_NORM_COUNT		4

/* I2C controller flags */
#define SGI_I2C_FORCE_IDLE		(0 << 0)
#define SGI_I2C_NOT_IDLE		(1 << 0)
#define SGI_I2C_WRITE			(0 << 1)
#define SGI_I2C_READ			(1 << 1)
#define SGI_I2C_RELEASE_BUS		(0 << 2)
#define SGI_I2C_HOLD_BUS		(1 << 2)
#define SGI_I2C_XFER_DONE		(0 << 4)
#define SGI_I2C_XFER_BUSY		(1 << 4)
#define SGI_I2C_ACK			(0 << 5)
#define SGI_I2C_NACK			(1 << 5)
#define SGI_I2C_BUS_OK			(0 << 7)
#define SGI_I2C_BUS_ERR			(1 << 7)

/* Internal data structure definitions */

struct vino_input {
	char *name;
	v4l2_std_id std;
};

struct vino_clipping {
	unsigned int left, right, top, bottom;
};

struct vino_data_format {
	/* the description */
	char *description;
	/* bytes per pixel */
	unsigned int bpp;
	/* V4L2 fourcc code */
	__u32 pixelformat;
	/* V4L2 colorspace (duh!) */
	enum v4l2_colorspace colorspace;
};

struct vino_data_norm {
	char *description;
	unsigned int width, height;
	struct vino_clipping odd;
	struct vino_clipping even;

	v4l2_std_id std;
	unsigned int fps_min, fps_max;
	__u32 framelines;
};

struct vino_descriptor_table {
	/* the number of PAGE_SIZE sized pages in the buffer */
	unsigned int page_count;
	/* virtual (kmalloc'd) pointers to the actual data
	 * (in PAGE_SIZE chunks, used with mmap streaming) */
	unsigned long *virtual;

	/* cpu address for the VINO descriptor table
	 * (contains DMA addresses, VINO_PAGE_SIZE chunks) */
	unsigned long *dma_cpu;
	/* dma address for the VINO descriptor table
	 * (contains DMA addresses, VINO_PAGE_SIZE chunks) */
	dma_addr_t dma;
};

struct vino_framebuffer {
	/* identifier nubmer */
	unsigned int id;
	/* the length of the whole buffer */
	unsigned int size;
	/* the length of actual data in buffer */
	unsigned int data_size;
	/* the data format */
	unsigned int data_format;
	/* the state of buffer data */
	unsigned int state;
	/* is the buffer mapped in user space? */
	unsigned int map_count;
	/* memory offset for mmap() */
	unsigned int offset;
	/* frame counter */
	unsigned int frame_counter;
	/* timestamp (written when image capture finishes) */
	struct timeval timestamp;

	struct vino_descriptor_table desc_table;

	spinlock_t state_lock;
};

struct vino_framebuffer_fifo {
	unsigned int length;

	unsigned int used;
	unsigned int head;
	unsigned int tail;

	unsigned int data[VINO_FRAMEBUFFER_COUNT_MAX];
};

struct vino_framebuffer_queue {
	unsigned int magic;

	/* VINO_MEMORY_NONE, VINO_MEMORY_MMAP or VINO_MEMORY_USERPTR */
	unsigned int type;
	unsigned int length;

	/* data field of in and out contain index numbers for buffer */
	struct vino_framebuffer_fifo in;
	struct vino_framebuffer_fifo out;

	struct vino_framebuffer *buffer[VINO_FRAMEBUFFER_COUNT_MAX];

	spinlock_t queue_lock;
	struct mutex queue_mutex;
	wait_queue_head_t frame_wait_queue;
};

struct vino_interrupt_data {
	struct timeval timestamp;
	unsigned int frame_counter;
	unsigned int skip_count;
	unsigned int skip;
};

struct vino_channel_settings {
	unsigned int channel;

	int input;
	unsigned int data_format;
	unsigned int data_norm;
	struct vino_clipping clipping;
	unsigned int decimation;
	unsigned int line_size;
	unsigned int alpha;
	unsigned int fps;
	unsigned int framert_reg;

	unsigned int fifo_threshold;

	struct vino_framebuffer_queue fb_queue;

	/* number of the current field */
	unsigned int field;

	/* read in progress */
	int reading;
	/* streaming is active */
	int streaming;
	/* the driver is currently processing the queue */
	int capturing;

	struct mutex mutex;
	spinlock_t capture_lock;

	unsigned int users;

	struct vino_interrupt_data int_data;

	/* V4L support */
	struct video_device *vdev;
};

struct vino_settings {
	struct v4l2_device v4l2_dev;
	struct vino_channel_settings a;
	struct vino_channel_settings b;

	/* the channel which owns this client:
	 * VINO_NO_CHANNEL, VINO_CHANNEL_A or VINO_CHANNEL_B */
	unsigned int decoder_owner;
	struct v4l2_subdev *decoder;
	unsigned int camera_owner;
	struct v4l2_subdev *camera;

	/* a lock for vino register access */
	spinlock_t vino_lock;
	/* a lock for channel input changes */
	spinlock_t input_lock;

	unsigned long dummy_page;
	struct vino_descriptor_table dummy_desc_table;
};

/* Module parameters */

/*
 * Using vino_pixel_conversion the ABGR32-format pixels supplied
 * by the VINO chip can be converted to more common formats
 * like RGBA32 (or probably RGB24 in the future). This way we
 * can give out data that can be specified correctly with
 * the V4L2-definitions.
 *
 * The pixel format is specified as RGBA32 when no conversion
 * is used.
 *
 * Note that this only affects the 32-bit bit depth.
 *
 * Use non-zero value to enable conversion.
 */
static int vino_pixel_conversion;

module_param_named(pixelconv, vino_pixel_conversion, int, 0);

MODULE_PARM_DESC(pixelconv,
		 "enable pixel conversion (non-zero value enables)");

/* Internal data structures */

static struct sgi_vino *vino;

static struct vino_settings *vino_drvdata;

#define camera_call(o, f, args...) \
	v4l2_subdev_call(vino_drvdata->camera, o, f, ##args)
#define decoder_call(o, f, args...) \
	v4l2_subdev_call(vino_drvdata->decoder, o, f, ##args)

static const char *vino_driver_name = "vino";
static const char *vino_driver_description = "SGI VINO";
static const char *vino_bus_name = "GIO64 bus";
static const char *vino_vdev_name_a = "SGI VINO Channel A";
static const char *vino_vdev_name_b = "SGI VINO Channel B";

static void vino_capture_tasklet(unsigned long channel);

DECLARE_TASKLET(vino_tasklet_a, vino_capture_tasklet, VINO_CHANNEL_A);
DECLARE_TASKLET(vino_tasklet_b, vino_capture_tasklet, VINO_CHANNEL_B);

static const struct vino_input vino_inputs[] = {
	{
		.name		= "Composite",
		.std		= V4L2_STD_NTSC | V4L2_STD_PAL
		| V4L2_STD_SECAM,
	}, {
		.name		= "S-Video",
		.std		= V4L2_STD_NTSC | V4L2_STD_PAL
		| V4L2_STD_SECAM,
	}, {
		.name		= "D1/IndyCam",
		.std		= V4L2_STD_NTSC,
	}
};

static const struct vino_data_format vino_data_formats[] = {
	{
		.description	= "8-bit greyscale",
		.bpp		= 1,
		.pixelformat	= V4L2_PIX_FMT_GREY,
		.colorspace	= V4L2_COLORSPACE_SMPTE170M,
	}, {
		.description	= "8-bit dithered RGB 3-3-2",
		.bpp		= 1,
		.pixelformat	= V4L2_PIX_FMT_RGB332,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	}, {
		.description	= "32-bit RGB",
		.bpp		= 4,
		.pixelformat	= V4L2_PIX_FMT_RGB32,
		.colorspace	= V4L2_COLORSPACE_SRGB,
	}, {
		.description	= "YUV 4:2:2",
		.bpp		= 2,
		.pixelformat	= V4L2_PIX_FMT_YUYV, // XXX: swapped?
		.colorspace	= V4L2_COLORSPACE_SMPTE170M,
	}
};

static const struct vino_data_norm vino_data_norms[] = {
	{
		.description	= "NTSC",
		.std		= V4L2_STD_NTSC,
		.fps_min	= 6,
		.fps_max	= 30,
		.framelines	= 525,
		.width		= VINO_NTSC_WIDTH,
		.height		= VINO_NTSC_HEIGHT,
		.odd		= {
			.top	= VINO_CLIPPING_START_ODD_NTSC,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_ODD_NTSC
			+ VINO_NTSC_HEIGHT / 2 - 1,
			.right	= VINO_NTSC_WIDTH,
		},
		.even		= {
			.top	= VINO_CLIPPING_START_EVEN_NTSC,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_EVEN_NTSC
			+ VINO_NTSC_HEIGHT / 2 - 1,
			.right	= VINO_NTSC_WIDTH,
		},
	}, {
		.description	= "PAL",
		.std		= V4L2_STD_PAL,
		.fps_min	= 5,
		.fps_max	= 25,
		.framelines	= 625,
		.width		= VINO_PAL_WIDTH,
		.height		= VINO_PAL_HEIGHT,
		.odd		= {
			.top	= VINO_CLIPPING_START_ODD_PAL,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_ODD_PAL
			+ VINO_PAL_HEIGHT / 2 - 1,
			.right	= VINO_PAL_WIDTH,
		},
		.even		= {
			.top	= VINO_CLIPPING_START_EVEN_PAL,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_EVEN_PAL
			+ VINO_PAL_HEIGHT / 2 - 1,
			.right	= VINO_PAL_WIDTH,
		},
	}, {
		.description	= "SECAM",
		.std		= V4L2_STD_SECAM,
		.fps_min	= 5,
		.fps_max	= 25,
		.framelines	= 625,
		.width		= VINO_PAL_WIDTH,
		.height		= VINO_PAL_HEIGHT,
		.odd		= {
			.top	= VINO_CLIPPING_START_ODD_PAL,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_ODD_PAL
			+ VINO_PAL_HEIGHT / 2 - 1,
			.right	= VINO_PAL_WIDTH,
		},
		.even		= {
			.top	= VINO_CLIPPING_START_EVEN_PAL,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_EVEN_PAL
			+ VINO_PAL_HEIGHT / 2 - 1,
			.right	= VINO_PAL_WIDTH,
		},
	}, {
		.description	= "NTSC/D1",
		.std		= V4L2_STD_NTSC,
		.fps_min	= 6,
		.fps_max	= 30,
		.framelines	= 525,
		.width		= VINO_NTSC_WIDTH,
		.height		= VINO_NTSC_HEIGHT,
		.odd		= {
			.top	= VINO_CLIPPING_START_ODD_D1,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_ODD_D1
			+ VINO_NTSC_HEIGHT / 2 - 1,
			.right	= VINO_NTSC_WIDTH,
		},
		.even		= {
			.top	= VINO_CLIPPING_START_EVEN_D1,
			.left	= 0,
			.bottom	= VINO_CLIPPING_START_EVEN_D1
			+ VINO_NTSC_HEIGHT / 2 - 1,
			.right	= VINO_NTSC_WIDTH,
		},
	}
};

#define VINO_INDYCAM_V4L2_CONTROL_COUNT		9

struct v4l2_queryctrl vino_indycam_v4l2_controls[] = {
	{
		.id = V4L2_CID_AUTOGAIN,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Automatic Gain Control",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = INDYCAM_AGC_DEFAULT,
	}, {
		.id = V4L2_CID_AUTO_WHITE_BALANCE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Automatic White Balance",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = INDYCAM_AWB_DEFAULT,
	}, {
		.id = V4L2_CID_GAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Gain",
		.minimum = INDYCAM_GAIN_MIN,
		.maximum = INDYCAM_GAIN_MAX,
		.step = 1,
		.default_value = INDYCAM_GAIN_DEFAULT,
	}, {
		.id = INDYCAM_CONTROL_RED_SATURATION,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Red Saturation",
		.minimum = INDYCAM_RED_SATURATION_MIN,
		.maximum = INDYCAM_RED_SATURATION_MAX,
		.step = 1,
		.default_value = INDYCAM_RED_SATURATION_DEFAULT,
	}, {
		.id = INDYCAM_CONTROL_BLUE_SATURATION,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Blue Saturation",
		.minimum = INDYCAM_BLUE_SATURATION_MIN,
		.maximum = INDYCAM_BLUE_SATURATION_MAX,
		.step = 1,
		.default_value = INDYCAM_BLUE_SATURATION_DEFAULT,
	}, {
		.id = V4L2_CID_RED_BALANCE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Red Balance",
		.minimum = INDYCAM_RED_BALANCE_MIN,
		.maximum = INDYCAM_RED_BALANCE_MAX,
		.step = 1,
		.default_value = INDYCAM_RED_BALANCE_DEFAULT,
	}, {
		.id = V4L2_CID_BLUE_BALANCE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Blue Balance",
		.minimum = INDYCAM_BLUE_BALANCE_MIN,
		.maximum = INDYCAM_BLUE_BALANCE_MAX,
		.step = 1,
		.default_value = INDYCAM_BLUE_BALANCE_DEFAULT,
	}, {
		.id = V4L2_CID_EXPOSURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Shutter Control",
		.minimum = INDYCAM_SHUTTER_MIN,
		.maximum = INDYCAM_SHUTTER_MAX,
		.step = 1,
		.default_value = INDYCAM_SHUTTER_DEFAULT,
	}, {
		.id = V4L2_CID_GAMMA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Gamma",
		.minimum = INDYCAM_GAMMA_MIN,
		.maximum = INDYCAM_GAMMA_MAX,
		.step = 1,
		.default_value = INDYCAM_GAMMA_DEFAULT,
	}
};

#define VINO_SAA7191_V4L2_CONTROL_COUNT		9

struct v4l2_queryctrl vino_saa7191_v4l2_controls[] = {
	{
		.id = V4L2_CID_HUE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Hue",
		.minimum = SAA7191_HUE_MIN,
		.maximum = SAA7191_HUE_MAX,
		.step = 1,
		.default_value = SAA7191_HUE_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_BANDPASS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Luminance Bandpass",
		.minimum = SAA7191_BANDPASS_MIN,
		.maximum = SAA7191_BANDPASS_MAX,
		.step = 1,
		.default_value = SAA7191_BANDPASS_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_BANDPASS_WEIGHT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Luminance Bandpass Weight",
		.minimum = SAA7191_BANDPASS_WEIGHT_MIN,
		.maximum = SAA7191_BANDPASS_WEIGHT_MAX,
		.step = 1,
		.default_value = SAA7191_BANDPASS_WEIGHT_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_CORING,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "HF Luminance Coring",
		.minimum = SAA7191_CORING_MIN,
		.maximum = SAA7191_CORING_MAX,
		.step = 1,
		.default_value = SAA7191_CORING_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_FORCE_COLOUR,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Force Colour",
		.minimum = SAA7191_FORCE_COLOUR_MIN,
		.maximum = SAA7191_FORCE_COLOUR_MAX,
		.step = 1,
		.default_value = SAA7191_FORCE_COLOUR_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_CHROMA_GAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Chrominance Gain Control",
		.minimum = SAA7191_CHROMA_GAIN_MIN,
		.maximum = SAA7191_CHROMA_GAIN_MAX,
		.step = 1,
		.default_value = SAA7191_CHROMA_GAIN_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_VTRC,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "VTR Time Constant",
		.minimum = SAA7191_VTRC_MIN,
		.maximum = SAA7191_VTRC_MAX,
		.step = 1,
		.default_value = SAA7191_VTRC_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_LUMA_DELAY,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Luminance Delay Compensation",
		.minimum = SAA7191_LUMA_DELAY_MIN,
		.maximum = SAA7191_LUMA_DELAY_MAX,
		.step = 1,
		.default_value = SAA7191_LUMA_DELAY_DEFAULT,
	}, {
		.id = SAA7191_CONTROL_VNR,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Vertical Noise Reduction",
		.minimum = SAA7191_VNR_MIN,
		.maximum = SAA7191_VNR_MAX,
		.step = 1,
		.default_value = SAA7191_VNR_DEFAULT,
	}
};

/* VINO framebuffer/DMA descriptor management */

static void vino_free_buffer_with_count(struct vino_framebuffer *fb,
					       unsigned int count)
{
	unsigned int i;

	dprintk("vino_free_buffer_with_count(): count = %d\n", count);

	for (i = 0; i < count; i++) {
		ClearPageReserved(virt_to_page((void *)fb->desc_table.virtual[i]));
		dma_unmap_single(NULL,
				 fb->desc_table.dma_cpu[VINO_PAGE_RATIO * i],
				 PAGE_SIZE, DMA_FROM_DEVICE);
		free_page(fb->desc_table.virtual[i]);
	}

	dma_free_coherent(NULL,
			  VINO_PAGE_RATIO * (fb->desc_table.page_count + 4) *
			  sizeof(dma_addr_t), (void *)fb->desc_table.dma_cpu,
			  fb->desc_table.dma);
	kfree(fb->desc_table.virtual);

	memset(fb, 0, sizeof(struct vino_framebuffer));
}

static void vino_free_buffer(struct vino_framebuffer *fb)
{
	vino_free_buffer_with_count(fb, fb->desc_table.page_count);
}

static int vino_allocate_buffer(struct vino_framebuffer *fb,
				unsigned int size)
{
	unsigned int count, i, j;
	int ret = 0;

	dprintk("vino_allocate_buffer():\n");

	if (size < 1)
		return -EINVAL;

	memset(fb, 0, sizeof(struct vino_framebuffer));

	count = ((size / PAGE_SIZE) + 4) & ~3;

	dprintk("vino_allocate_buffer(): size = %d, count = %d\n",
		size, count);

	/* allocate memory for table with virtual (page) addresses */
	fb->desc_table.virtual =
		kmalloc(count * sizeof(unsigned long), GFP_KERNEL);
	if (!fb->desc_table.virtual)
		return -ENOMEM;

	/* allocate memory for table with dma addresses
	 * (has space for four extra descriptors) */
	fb->desc_table.dma_cpu =
		dma_alloc_coherent(NULL, VINO_PAGE_RATIO * (count + 4) *
				   sizeof(dma_addr_t), &fb->desc_table.dma,
				   GFP_KERNEL | GFP_DMA);
	if (!fb->desc_table.dma_cpu) {
		ret = -ENOMEM;
		goto out_free_virtual;
	}

	/* allocate pages for the buffer and acquire the according
	 * dma addresses */
	for (i = 0; i < count; i++) {
		dma_addr_t dma_data_addr;

		fb->desc_table.virtual[i] =
			get_zeroed_page(GFP_KERNEL | GFP_DMA);
		if (!fb->desc_table.virtual[i]) {
			ret = -ENOBUFS;
			break;
		}

		dma_data_addr =
			dma_map_single(NULL,
				       (void *)fb->desc_table.virtual[i],
				       PAGE_SIZE, DMA_FROM_DEVICE);

		for (j = 0; j < VINO_PAGE_RATIO; j++) {
			fb->desc_table.dma_cpu[VINO_PAGE_RATIO * i + j] =
				dma_data_addr + VINO_PAGE_SIZE * j;
		}

		SetPageReserved(virt_to_page((void *)fb->desc_table.virtual[i]));
	}

	/* page_count needs to be set anyway, because the descriptor table has
	 * been allocated according to this number */
	fb->desc_table.page_count = count;

	if (ret) {
		/* the descriptor with index i doesn't contain
		 * a valid address yet */
		vino_free_buffer_with_count(fb, i);
		return ret;
	}

	//fb->size = size;
	fb->size = count * PAGE_SIZE;
	fb->data_format = VINO_DATA_FMT_NONE;

	/* set the dma stop-bit for the last (count+1)th descriptor */
	fb->desc_table.dma_cpu[VINO_PAGE_RATIO * count] = VINO_DESC_STOP;
	return 0;

 out_free_virtual:
	kfree(fb->desc_table.virtual);
	return ret;
}

#if 0
/* user buffers not fully implemented yet */
static int vino_prepare_user_buffer(struct vino_framebuffer *fb,
				     void *user,
				     unsigned int size)
{
	unsigned int count, i, j;
	int ret = 0;

	dprintk("vino_prepare_user_buffer():\n");

	if (size < 1)
		return -EINVAL;

	memset(fb, 0, sizeof(struct vino_framebuffer));

	count = ((size / PAGE_SIZE)) & ~3;

	dprintk("vino_prepare_user_buffer(): size = %d, count = %d\n",
		size, count);

	/* allocate memory for table with virtual (page) addresses */
	fb->desc_table.virtual = (unsigned long *)
		kmalloc(count * sizeof(unsigned long), GFP_KERNEL);
	if (!fb->desc_table.virtual)
		return -ENOMEM;

	/* allocate memory for table with dma addresses
	 * (has space for four extra descriptors) */
	fb->desc_table.dma_cpu =
		dma_alloc_coherent(NULL, VINO_PAGE_RATIO * (count + 4) *
				   sizeof(dma_addr_t), &fb->desc_table.dma,
				   GFP_KERNEL | GFP_DMA);
	if (!fb->desc_table.dma_cpu) {
		ret = -ENOMEM;
		goto out_free_virtual;
	}

	/* allocate pages for the buffer and acquire the according
	 * dma addresses */
	for (i = 0; i < count; i++) {
		dma_addr_t dma_data_addr;

		fb->desc_table.virtual[i] =
			get_zeroed_page(GFP_KERNEL | GFP_DMA);
		if (!fb->desc_table.virtual[i]) {
			ret = -ENOBUFS;
			break;
		}

		dma_data_addr =
			dma_map_single(NULL,
				       (void *)fb->desc_table.virtual[i],
				       PAGE_SIZE, DMA_FROM_DEVICE);

		for (j = 0; j < VINO_PAGE_RATIO; j++) {
			fb->desc_table.dma_cpu[VINO_PAGE_RATIO * i + j] =
				dma_data_addr + VINO_PAGE_SIZE * j;
		}

		SetPageReserved(virt_to_page((void *)fb->desc_table.virtual[i]));
	}

	/* page_count needs to be set anyway, because the descriptor table has
	 * been allocated according to this number */
	fb->desc_table.page_count = count;

	if (ret) {
		/* the descriptor with index i doesn't contain
		 * a valid address yet */
		vino_free_buffer_with_count(fb, i);
		return ret;
	}

	//fb->size = size;
	fb->size = count * PAGE_SIZE;

	/* set the dma stop-bit for the last (count+1)th descriptor */
	fb->desc_table.dma_cpu[VINO_PAGE_RATIO * count] = VINO_DESC_STOP;
	return 0;

 out_free_virtual:
	kfree(fb->desc_table.virtual);
	return ret;
}
#endif

static void vino_sync_buffer(struct vino_framebuffer *fb)
{
	int i;

	dprintk("vino_sync_buffer():\n");

	for (i = 0; i < fb->desc_table.page_count; i++)
		dma_sync_single_for_cpu(NULL,
					fb->desc_table.dma_cpu[VINO_PAGE_RATIO * i],
					PAGE_SIZE, DMA_FROM_DEVICE);
}

/* Framebuffer fifo functions (need to be locked externally) */

static inline void vino_fifo_init(struct vino_framebuffer_fifo *f,
			   unsigned int length)
{
	f->length = 0;
	f->used = 0;
	f->head = 0;
	f->tail = 0;

	if (length > VINO_FRAMEBUFFER_COUNT_MAX)
		length = VINO_FRAMEBUFFER_COUNT_MAX;

	f->length = length;
}

/* returns true/false */
static inline int vino_fifo_has_id(struct vino_framebuffer_fifo *f,
				   unsigned int id)
{
	unsigned int i;

	for (i = f->head; i == (f->tail - 1); i = (i + 1) % f->length) {
		if (f->data[i] == id)
			return 1;
	}

	return 0;
}

#if 0
/* returns true/false */
static inline int vino_fifo_full(struct vino_framebuffer_fifo *f)
{
	return (f->used == f->length);
}
#endif

static inline unsigned int vino_fifo_get_used(struct vino_framebuffer_fifo *f)
{
	return f->used;
}

static int vino_fifo_enqueue(struct vino_framebuffer_fifo *f, unsigned int id)
{
	if (id >= f->length) {
		return VINO_QUEUE_ERROR;
	}

	if (vino_fifo_has_id(f, id)) {
		return VINO_QUEUE_ERROR;
	}

	if (f->used < f->length) {
		f->data[f->tail] = id;
		f->tail = (f->tail + 1) % f->length;
		f->used++;
	} else {
		return VINO_QUEUE_ERROR;
	}

	return 0;
}

static int vino_fifo_peek(struct vino_framebuffer_fifo *f, unsigned int *id)
{
	if (f->used > 0) {
		*id = f->data[f->head];
	} else {
		return VINO_QUEUE_ERROR;
	}

	return 0;
}

static int vino_fifo_dequeue(struct vino_framebuffer_fifo *f, unsigned int *id)
{
	if (f->used > 0) {
		*id = f->data[f->head];
		f->head = (f->head + 1) % f->length;
		f->used--;
	} else {
		return VINO_QUEUE_ERROR;
	}

	return 0;
}

/* Framebuffer queue functions */

/* execute with queue_lock locked */
static void vino_queue_free_with_count(struct vino_framebuffer_queue *q,
				       unsigned int length)
{
	unsigned int i;

	q->length = 0;
	memset(&q->in, 0, sizeof(struct vino_framebuffer_fifo));
	memset(&q->out, 0, sizeof(struct vino_framebuffer_fifo));
	for (i = 0; i < length; i++) {
		dprintk("vino_queue_free_with_count(): freeing buffer %d\n",
			i);
		vino_free_buffer(q->buffer[i]);
		kfree(q->buffer[i]);
	}

	q->type = VINO_MEMORY_NONE;
	q->magic = 0;
}

static void vino_queue_free(struct vino_framebuffer_queue *q)
{
	dprintk("vino_queue_free():\n");

	if (q->magic != VINO_QUEUE_MAGIC)
		return;
	if (q->type != VINO_MEMORY_MMAP)
		return;

	mutex_lock(&q->queue_mutex);

	vino_queue_free_with_count(q, q->length);

	mutex_unlock(&q->queue_mutex);
}

static int vino_queue_init(struct vino_framebuffer_queue *q,
			   unsigned int *length)
{
	unsigned int i;
	int ret = 0;

	dprintk("vino_queue_init(): length = %d\n", *length);

	if (q->magic == VINO_QUEUE_MAGIC) {
		dprintk("vino_queue_init(): queue already initialized!\n");
		return -EINVAL;
	}

	if (q->type != VINO_MEMORY_NONE) {
		dprintk("vino_queue_init(): queue already initialized!\n");
		return -EINVAL;
	}

	if (*length < 1)
		return -EINVAL;

	mutex_lock(&q->queue_mutex);

	if (*length > VINO_FRAMEBUFFER_COUNT_MAX)
		*length = VINO_FRAMEBUFFER_COUNT_MAX;

	q->length = 0;

	for (i = 0; i < *length; i++) {
		dprintk("vino_queue_init(): allocating buffer %d\n", i);
		q->buffer[i] = kmalloc(sizeof(struct vino_framebuffer),
				       GFP_KERNEL);
		if (!q->buffer[i]) {
			dprintk("vino_queue_init(): kmalloc() failed\n");
			ret = -ENOMEM;
			break;
		}

		ret = vino_allocate_buffer(q->buffer[i],
					   VINO_FRAMEBUFFER_SIZE);
		if (ret) {
			kfree(q->buffer[i]);
			dprintk("vino_queue_init(): "
				"vino_allocate_buffer() failed\n");
			break;
		}

		q->buffer[i]->id = i;
		if (i > 0) {
			q->buffer[i]->offset = q->buffer[i - 1]->offset +
				q->buffer[i - 1]->size;
		} else {
			q->buffer[i]->offset = 0;
		}

		spin_lock_init(&q->buffer[i]->state_lock);

		dprintk("vino_queue_init(): buffer = %d, offset = %d, "
			"size = %d\n", i, q->buffer[i]->offset,
			q->buffer[i]->size);
	}

	if (ret) {
		vino_queue_free_with_count(q, i);
		*length = 0;
	} else {
		q->length = *length;
		vino_fifo_init(&q->in, q->length);
		vino_fifo_init(&q->out, q->length);
		q->type = VINO_MEMORY_MMAP;
		q->magic = VINO_QUEUE_MAGIC;
	}

	mutex_unlock(&q->queue_mutex);

	return ret;
}

static struct vino_framebuffer *vino_queue_add(struct
					       vino_framebuffer_queue *q,
					       unsigned int id)
{
	struct vino_framebuffer *ret = NULL;
	unsigned int total;
	unsigned long flags;

	dprintk("vino_queue_add(): id = %d\n", id);

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	if (id >= q->length)
		goto out;

	/* not needed?: if (vino_fifo_full(&q->out)) {
		goto out;
		}*/
	/* check that outgoing queue isn't already full
	 * (or that it won't become full) */
	total = vino_fifo_get_used(&q->in) +
		vino_fifo_get_used(&q->out);
	if (total >= q->length)
		goto out;

	if (vino_fifo_enqueue(&q->in, id))
		goto out;

	ret = q->buffer[id];

out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

static struct vino_framebuffer *vino_queue_transfer(struct
						    vino_framebuffer_queue *q)
{
	struct vino_framebuffer *ret = NULL;
	struct vino_framebuffer *fb;
	int id;
	unsigned long flags;

	dprintk("vino_queue_transfer():\n");

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	// now this actually removes an entry from the incoming queue
	if (vino_fifo_dequeue(&q->in, &id)) {
		goto out;
	}

	dprintk("vino_queue_transfer(): id = %d\n", id);
	fb = q->buffer[id];

	// we have already checked that the outgoing queue is not full, but...
	if (vino_fifo_enqueue(&q->out, id)) {
		printk(KERN_ERR "vino_queue_transfer(): "
		       "outgoing queue is full, this shouldn't happen!\n");
		goto out;
	}

	ret = fb;
out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

/* returns true/false */
static int vino_queue_incoming_contains(struct vino_framebuffer_queue *q,
					unsigned int id)
{
	int ret = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	ret = vino_fifo_has_id(&q->in, id);

out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

/* returns true/false */
static int vino_queue_outgoing_contains(struct vino_framebuffer_queue *q,
					unsigned int id)
{
	int ret = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	ret = vino_fifo_has_id(&q->out, id);

out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

static int vino_queue_get_incoming(struct vino_framebuffer_queue *q,
				   unsigned int *used)
{
	int ret = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return VINO_QUEUE_ERROR;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0) {
		ret = VINO_QUEUE_ERROR;
		goto out;
	}

	*used = vino_fifo_get_used(&q->in);

out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

static int vino_queue_get_outgoing(struct vino_framebuffer_queue *q,
				   unsigned int *used)
{
	int ret = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return VINO_QUEUE_ERROR;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0) {
		ret = VINO_QUEUE_ERROR;
		goto out;
	}

	*used = vino_fifo_get_used(&q->out);

out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

#if 0
static int vino_queue_get_total(struct vino_framebuffer_queue *q,
				unsigned int *total)
{
	int ret = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return VINO_QUEUE_ERROR;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0) {
		ret = VINO_QUEUE_ERROR;
		goto out;
	}

	*total = vino_fifo_get_used(&q->in) +
		vino_fifo_get_used(&q->out);

out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}
#endif

static struct vino_framebuffer *vino_queue_peek(struct
						vino_framebuffer_queue *q,
						unsigned int *id)
{
	struct vino_framebuffer *ret = NULL;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	if (vino_fifo_peek(&q->in, id)) {
		goto out;
	}

	ret = q->buffer[*id];
out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

static struct vino_framebuffer *vino_queue_remove(struct
						  vino_framebuffer_queue *q,
						  unsigned int *id)
{
	struct vino_framebuffer *ret = NULL;
	unsigned long flags;
	dprintk("vino_queue_remove():\n");

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	if (vino_fifo_dequeue(&q->out, id)) {
		goto out;
	}

	dprintk("vino_queue_remove(): id = %d\n", *id);
	ret = q->buffer[*id];
out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

static struct
vino_framebuffer *vino_queue_get_buffer(struct vino_framebuffer_queue *q,
					unsigned int id)
{
	struct vino_framebuffer *ret = NULL;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);

	if (q->length == 0)
		goto out;

	if (id >= q->length)
		goto out;

	ret = q->buffer[id];
 out:
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

static unsigned int vino_queue_get_length(struct vino_framebuffer_queue *q)
{
	unsigned int length = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return length;
	}

	spin_lock_irqsave(&q->queue_lock, flags);
	length = q->length;
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return length;
}

static int vino_queue_has_mapped_buffers(struct vino_framebuffer_queue *q)
{
	unsigned int i;
	int ret = 0;
	unsigned long flags;

	if (q->magic != VINO_QUEUE_MAGIC) {
		return ret;
	}

	spin_lock_irqsave(&q->queue_lock, flags);
	for (i = 0; i < q->length; i++) {
		if (q->buffer[i]->map_count > 0) {
			ret = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&q->queue_lock, flags);

	return ret;
}

/* VINO functions */

/* execute with input_lock locked */
static void vino_update_line_size(struct vino_channel_settings *vcs)
{
	unsigned int w = vcs->clipping.right - vcs->clipping.left;
	unsigned int d = vcs->decimation;
	unsigned int bpp = vino_data_formats[vcs->data_format].bpp;
	unsigned int lsize;

	dprintk("update_line_size(): before: w = %d, d = %d, "
		"line_size = %d\n", w, d, vcs->line_size);

	/* line size must be multiple of 8 bytes */
	lsize = (bpp * (w / d)) & ~7;
	w = (lsize / bpp) * d;

	vcs->clipping.right = vcs->clipping.left + w;
	vcs->line_size = lsize;

	dprintk("update_line_size(): after: w = %d, d = %d, "
		"line_size = %d\n", w, d, vcs->line_size);
}

/* execute with input_lock locked */
static void vino_set_clipping(struct vino_channel_settings *vcs,
			      unsigned int x, unsigned int y,
			      unsigned int w, unsigned int h)
{
	unsigned int maxwidth, maxheight;
	unsigned int d;

	maxwidth = vino_data_norms[vcs->data_norm].width;
	maxheight = vino_data_norms[vcs->data_norm].height;
	d = vcs->decimation;

	y &= ~1;	/* odd/even fields */

	if (x > maxwidth) {
		x = 0;
	}
	if (y > maxheight) {
		y = 0;
	}

	if (((w / d) < VINO_MIN_WIDTH)
	    || ((h / d) < VINO_MIN_HEIGHT)) {
		w = VINO_MIN_WIDTH * d;
		h = VINO_MIN_HEIGHT * d;
	}

	if ((x + w) > maxwidth) {
		w = maxwidth - x;
		if ((w / d) < VINO_MIN_WIDTH)
			x = maxwidth - VINO_MIN_WIDTH * d;
	}
	if ((y + h) > maxheight) {
		h = maxheight - y;
		if ((h / d) < VINO_MIN_HEIGHT)
			y = maxheight - VINO_MIN_HEIGHT * d;
	}

	vcs->clipping.left = x;
	vcs->clipping.top = y;
	vcs->clipping.right = x + w;
	vcs->clipping.bottom = y + h;

	vino_update_line_size(vcs);

	dprintk("clipping %d, %d, %d, %d / %d - %d\n",
		vcs->clipping.left, vcs->clipping.top, vcs->clipping.right,
		vcs->clipping.bottom, vcs->decimation, vcs->line_size);
}

/* execute with input_lock locked */
static inline void vino_set_default_clipping(struct vino_channel_settings *vcs)
{
	vino_set_clipping(vcs, 0, 0, vino_data_norms[vcs->data_norm].width,
			  vino_data_norms[vcs->data_norm].height);
}

/* execute with input_lock locked */
static void vino_set_scaling(struct vino_channel_settings *vcs,
			     unsigned int w, unsigned int h)
{
	unsigned int x, y, curw, curh, d;

	x = vcs->clipping.left;
	y = vcs->clipping.top;
	curw = vcs->clipping.right - vcs->clipping.left;
	curh = vcs->clipping.bottom - vcs->clipping.top;

	d = max(curw / w, curh / h);

	dprintk("scaling w: %d, h: %d, curw: %d, curh: %d, d: %d\n",
		w, h, curw, curh, d);

	if (d < 1) {
		d = 1;
	} else if (d > 8) {
		d = 8;
	}

	vcs->decimation = d;
	vino_set_clipping(vcs, x, y, w * d, h * d);

	dprintk("scaling %d, %d, %d, %d / %d - %d\n", vcs->clipping.left,
		vcs->clipping.top, vcs->clipping.right, vcs->clipping.bottom,
		vcs->decimation, vcs->line_size);
}

/* execute with input_lock locked */
static inline void vino_set_default_scaling(struct vino_channel_settings *vcs)
{
	vino_set_scaling(vcs, vcs->clipping.right - vcs->clipping.left,
			 vcs->clipping.bottom - vcs->clipping.top);
}

/* execute with input_lock locked */
static void vino_set_framerate(struct vino_channel_settings *vcs,
			       unsigned int fps)
{
	unsigned int mask;

	switch (vcs->data_norm) {
	case VINO_DATA_NORM_NTSC:
	case VINO_DATA_NORM_D1:
		fps = (unsigned int)(fps / 6) * 6; // FIXME: round!

		if (fps < vino_data_norms[vcs->data_norm].fps_min)
			fps = vino_data_norms[vcs->data_norm].fps_min;
		if (fps > vino_data_norms[vcs->data_norm].fps_max)
			fps = vino_data_norms[vcs->data_norm].fps_max;

		switch (fps) {
		case 6:
			mask = 0x003;
			break;
		case 12:
			mask = 0x0c3;
			break;
		case 18:
			mask = 0x333;
			break;
		case 24:
			mask = 0x3ff;
			break;
		case 30:
			mask = 0xfff;
			break;
		default:
			mask = VINO_FRAMERT_FULL;
		}
		vcs->framert_reg = VINO_FRAMERT_RT(mask);
		break;
	case VINO_DATA_NORM_PAL:
	case VINO_DATA_NORM_SECAM:
		fps = (unsigned int)(fps / 5) * 5; // FIXME: round!

		if (fps < vino_data_norms[vcs->data_norm].fps_min)
			fps = vino_data_norms[vcs->data_norm].fps_min;
		if (fps > vino_data_norms[vcs->data_norm].fps_max)
			fps = vino_data_norms[vcs->data_norm].fps_max;

		switch (fps) {
		case 5:
			mask = 0x003;
			break;
		case 10:
			mask = 0x0c3;
			break;
		case 15:
			mask = 0x333;
			break;
		case 20:
			mask = 0x0ff;
			break;
		case 25:
			mask = 0x3ff;
			break;
		default:
			mask = VINO_FRAMERT_FULL;
		}
		vcs->framert_reg = VINO_FRAMERT_RT(mask) | VINO_FRAMERT_PAL;
		break;
	}

	vcs->fps = fps;
}

/* execute with input_lock locked */
static inline void vino_set_default_framerate(struct
					      vino_channel_settings *vcs)
{
	vino_set_framerate(vcs, vino_data_norms[vcs->data_norm].fps_max);
}

/* VINO I2C bus functions */

struct i2c_algo_sgi_data {
	void *data;	/* private data for lowlevel routines */
	unsigned (*getctrl)(void *data);
	void (*setctrl)(void *data, unsigned val);
	unsigned (*rdata)(void *data);
	void (*wdata)(void *data, unsigned val);

	int xfer_timeout;
	int ack_timeout;
};

static int wait_xfer_done(struct i2c_algo_sgi_data *adap)
{
	int i;

	for (i = 0; i < adap->xfer_timeout; i++) {
		if ((adap->getctrl(adap->data) & SGI_I2C_XFER_BUSY) == 0)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int wait_ack(struct i2c_algo_sgi_data *adap)
{
	int i;

	if (wait_xfer_done(adap))
		return -ETIMEDOUT;
	for (i = 0; i < adap->ack_timeout; i++) {
		if ((adap->getctrl(adap->data) & SGI_I2C_NACK) == 0)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int force_idle(struct i2c_algo_sgi_data *adap)
{
	int i;

	adap->setctrl(adap->data, SGI_I2C_FORCE_IDLE);
	for (i = 0; i < adap->xfer_timeout; i++) {
		if ((adap->getctrl(adap->data) & SGI_I2C_NOT_IDLE) == 0)
			goto out;
		udelay(1);
	}
	return -ETIMEDOUT;
out:
	if (adap->getctrl(adap->data) & SGI_I2C_BUS_ERR)
		return -EIO;
	return 0;
}

static int do_address(struct i2c_algo_sgi_data *adap, unsigned int addr,
		      int rd)
{
	if (rd)
		adap->setctrl(adap->data, SGI_I2C_NOT_IDLE);
	/* Check if bus is idle, eventually force it to do so */
	if (adap->getctrl(adap->data) & SGI_I2C_NOT_IDLE)
		if (force_idle(adap))
			return -EIO;
	/* Write out the i2c chip address and specify operation */
	adap->setctrl(adap->data,
		      SGI_I2C_HOLD_BUS | SGI_I2C_WRITE | SGI_I2C_NOT_IDLE);
	if (rd)
		addr |= 1;
	adap->wdata(adap->data, addr);
	if (wait_ack(adap))
		return -EIO;
	return 0;
}

static int i2c_read(struct i2c_algo_sgi_data *adap, unsigned char *buf,
		    unsigned int len)
{
	int i;

	adap->setctrl(adap->data,
		      SGI_I2C_HOLD_BUS | SGI_I2C_READ | SGI_I2C_NOT_IDLE);
	for (i = 0; i < len; i++) {
		if (wait_xfer_done(adap))
			return -EIO;
		buf[i] = adap->rdata(adap->data);
	}
	adap->setctrl(adap->data, SGI_I2C_RELEASE_BUS | SGI_I2C_FORCE_IDLE);

	return 0;

}

static int i2c_write(struct i2c_algo_sgi_data *adap, unsigned char *buf,
		     unsigned int len)
{
	int i;

	/* We are already in write state */
	for (i = 0; i < len; i++) {
		adap->wdata(adap->data, buf[i]);
		if (wait_ack(adap))
			return -EIO;
	}
	return 0;
}

static int sgi_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs,
		    int num)
{
	struct i2c_algo_sgi_data *adap = i2c_adap->algo_data;
	struct i2c_msg *p;
	int i, err = 0;

	for (i = 0; !err && i < num; i++) {
		p = &msgs[i];
		err = do_address(adap, p->addr, p->flags & I2C_M_RD);
		if (err || !p->len)
			continue;
		if (p->flags & I2C_M_RD)
			err = i2c_read(adap, p->buf, p->len);
		else
			err = i2c_write(adap, p->buf, p->len);
	}

	return (err < 0) ? err : i;
}

static u32 sgi_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm sgi_algo = {
	.master_xfer	= sgi_xfer,
	.functionality	= sgi_func,
};

static unsigned i2c_vino_getctrl(void *data)
{
	return vino->i2c_control;
}

static void i2c_vino_setctrl(void *data, unsigned val)
{
	vino->i2c_control = val;
}

static unsigned i2c_vino_rdata(void *data)
{
	return vino->i2c_data;
}

static void i2c_vino_wdata(void *data, unsigned val)
{
	vino->i2c_data = val;
}

static struct i2c_algo_sgi_data i2c_sgi_vino_data = {
	.getctrl = &i2c_vino_getctrl,
	.setctrl = &i2c_vino_setctrl,
	.rdata   = &i2c_vino_rdata,
	.wdata   = &i2c_vino_wdata,
	.xfer_timeout = 200,
	.ack_timeout  = 1000,
};

static struct i2c_adapter vino_i2c_adapter = {
	.name			= "VINO I2C bus",
	.algo			= &sgi_algo,
	.algo_data		= &i2c_sgi_vino_data,
	.owner 			= THIS_MODULE,
};

/*
 * Prepare VINO for DMA transfer...
 * (execute only with vino_lock and input_lock locked)
 */
static int vino_dma_setup(struct vino_channel_settings *vcs,
			  struct vino_framebuffer *fb)
{
	u32 ctrl, intr;
	struct sgi_vino_channel *ch;
	const struct vino_data_norm *norm;

	dprintk("vino_dma_setup():\n");

	vcs->field = 0;
	fb->frame_counter = 0;

	ch = (vcs->channel == VINO_CHANNEL_A) ? &vino->a : &vino->b;
	norm = &vino_data_norms[vcs->data_norm];

	ch->page_index = 0;
	ch->line_count = 0;

	/* VINO line size register is set 8 bytes less than actual */
	ch->line_size = vcs->line_size - 8;

	/* let VINO know where to transfer data */
	ch->start_desc_tbl = fb->desc_table.dma;
	ch->next_4_desc = fb->desc_table.dma;

	/* give vino time to fetch the first four descriptors, 5 usec
	 * should be more than enough time */
	udelay(VINO_DESC_FETCH_DELAY);

	dprintk("vino_dma_setup(): start desc = %08x, next 4 desc = %08x\n",
		ch->start_desc_tbl, ch->next_4_desc);

	/* set the alpha register */
	ch->alpha = vcs->alpha;

	/* set clipping registers */
	ch->clip_start = VINO_CLIP_ODD(norm->odd.top + vcs->clipping.top / 2) |
		VINO_CLIP_EVEN(norm->even.top +
			       vcs->clipping.top / 2) |
		VINO_CLIP_X(vcs->clipping.left);
	ch->clip_end = VINO_CLIP_ODD(norm->odd.top +
				     vcs->clipping.bottom / 2 - 1) |
		VINO_CLIP_EVEN(norm->even.top +
			       vcs->clipping.bottom / 2 - 1) |
		VINO_CLIP_X(vcs->clipping.right);

	/* set the size of actual content in the buffer (DECIMATION !) */
	fb->data_size = ((vcs->clipping.right - vcs->clipping.left) /
			 vcs->decimation) *
		((vcs->clipping.bottom - vcs->clipping.top) /
		 vcs->decimation) *
		vino_data_formats[vcs->data_format].bpp;

	ch->frame_rate = vcs->framert_reg;

	ctrl = vino->control;
	intr = vino->intr_status;

	if (vcs->channel == VINO_CHANNEL_A) {
		/* All interrupt conditions for this channel was cleared
		 * so clear the interrupt status register and enable
		 * interrupts */
		intr &=	~VINO_INTSTAT_A;
		ctrl |= VINO_CTRL_A_INT;

		/* enable synchronization */
		ctrl |= VINO_CTRL_A_SYNC_ENBL;

		/* enable frame assembly */
		ctrl |= VINO_CTRL_A_INTERLEAVE_ENBL;

		/* set decimation used */
		if (vcs->decimation < 2)
			ctrl &= ~VINO_CTRL_A_DEC_ENBL;
		else {
			ctrl |= VINO_CTRL_A_DEC_ENBL;
			ctrl &= ~VINO_CTRL_A_DEC_SCALE_MASK;
			ctrl |= (vcs->decimation - 1) <<
				VINO_CTRL_A_DEC_SCALE_SHIFT;
		}

		/* select input interface */
		if (vcs->input == VINO_INPUT_D1)
			ctrl |= VINO_CTRL_A_SELECT;
		else
			ctrl &= ~VINO_CTRL_A_SELECT;

		/* palette */
		ctrl &= ~(VINO_CTRL_A_LUMA_ONLY | VINO_CTRL_A_RGB |
			  VINO_CTRL_A_DITHER);
	} else {
		intr &= ~VINO_INTSTAT_B;
		ctrl |= VINO_CTRL_B_INT;

		ctrl |= VINO_CTRL_B_SYNC_ENBL;
		ctrl |= VINO_CTRL_B_INTERLEAVE_ENBL;

		if (vcs->decimation < 2)
			ctrl &= ~VINO_CTRL_B_DEC_ENBL;
		else {
			ctrl |= VINO_CTRL_B_DEC_ENBL;
			ctrl &= ~VINO_CTRL_B_DEC_SCALE_MASK;
			ctrl |= (vcs->decimation - 1) <<
				VINO_CTRL_B_DEC_SCALE_SHIFT;

		}
		if (vcs->input == VINO_INPUT_D1)
			ctrl |= VINO_CTRL_B_SELECT;
		else
			ctrl &= ~VINO_CTRL_B_SELECT;

		ctrl &= ~(VINO_CTRL_B_LUMA_ONLY | VINO_CTRL_B_RGB |
			  VINO_CTRL_B_DITHER);
	}

	/* set palette */
	fb->data_format = vcs->data_format;

	switch (vcs->data_format) {
		case VINO_DATA_FMT_GREY:
			ctrl |= (vcs->channel == VINO_CHANNEL_A) ?
				VINO_CTRL_A_LUMA_ONLY : VINO_CTRL_B_LUMA_ONLY;
			break;
		case VINO_DATA_FMT_RGB32:
			ctrl |= (vcs->channel == VINO_CHANNEL_A) ?
				VINO_CTRL_A_RGB : VINO_CTRL_B_RGB;
			break;
		case VINO_DATA_FMT_YUV:
			/* nothing needs to be done */
			break;
		case VINO_DATA_FMT_RGB332:
			ctrl |= (vcs->channel == VINO_CHANNEL_A) ?
				VINO_CTRL_A_RGB | VINO_CTRL_A_DITHER :
				VINO_CTRL_B_RGB | VINO_CTRL_B_DITHER;
			break;
	}

	vino->intr_status = intr;
	vino->control = ctrl;

	return 0;
}

/* (execute only with vino_lock locked) */
static inline void vino_dma_start(struct vino_channel_settings *vcs)
{
	u32 ctrl = vino->control;

	dprintk("vino_dma_start():\n");
	ctrl |= (vcs->channel == VINO_CHANNEL_A) ?
		VINO_CTRL_A_DMA_ENBL : VINO_CTRL_B_DMA_ENBL;
	vino->control = ctrl;
}

/* (execute only with vino_lock locked) */
static inline void vino_dma_stop(struct vino_channel_settings *vcs)
{
	u32 ctrl = vino->control;

	ctrl &= (vcs->channel == VINO_CHANNEL_A) ?
		~VINO_CTRL_A_DMA_ENBL : ~VINO_CTRL_B_DMA_ENBL;
	ctrl &= (vcs->channel == VINO_CHANNEL_A) ?
		~VINO_CTRL_A_INT : ~VINO_CTRL_B_INT;
	vino->control = ctrl;
	dprintk("vino_dma_stop():\n");
}

/*
 * Load dummy page to descriptor registers. This prevents generating of
 * spurious interrupts. (execute only with vino_lock locked)
 */
static void vino_clear_interrupt(struct vino_channel_settings *vcs)
{
	struct sgi_vino_channel *ch;

	ch = (vcs->channel == VINO_CHANNEL_A) ? &vino->a : &vino->b;

	ch->page_index = 0;
	ch->line_count = 0;

	ch->start_desc_tbl = vino_drvdata->dummy_desc_table.dma;
	ch->next_4_desc = vino_drvdata->dummy_desc_table.dma;

	udelay(VINO_DESC_FETCH_DELAY);
	dprintk("channel %c clear interrupt condition\n",
	       (vcs->channel == VINO_CHANNEL_A) ? 'A':'B');
}

static int vino_capture(struct vino_channel_settings *vcs,
			struct vino_framebuffer *fb)
{
	int err = 0;
	unsigned long flags, flags2;

	spin_lock_irqsave(&fb->state_lock, flags);

	if (fb->state == VINO_FRAMEBUFFER_IN_USE)
		err = -EBUSY;
	fb->state = VINO_FRAMEBUFFER_IN_USE;

	spin_unlock_irqrestore(&fb->state_lock, flags);

	if (err)
		return err;

	spin_lock_irqsave(&vino_drvdata->vino_lock, flags);
	spin_lock_irqsave(&vino_drvdata->input_lock, flags2);

	vino_dma_setup(vcs, fb);
	vino_dma_start(vcs);

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags2);
	spin_unlock_irqrestore(&vino_drvdata->vino_lock, flags);

	return err;
}

static
struct vino_framebuffer *vino_capture_enqueue(struct
					      vino_channel_settings *vcs,
					      unsigned int index)
{
	struct vino_framebuffer *fb;
	unsigned long flags;

	dprintk("vino_capture_enqueue():\n");

	spin_lock_irqsave(&vcs->capture_lock, flags);

	fb = vino_queue_add(&vcs->fb_queue, index);
	if (fb == NULL) {
		dprintk("vino_capture_enqueue(): vino_queue_add() failed, "
			"queue full?\n");
		goto out;
	}
out:
	spin_unlock_irqrestore(&vcs->capture_lock, flags);

	return fb;
}

static int vino_capture_next(struct vino_channel_settings *vcs, int start)
{
	struct vino_framebuffer *fb;
	unsigned int incoming, id;
	int err = 0;
	unsigned long flags;

	dprintk("vino_capture_next():\n");

	spin_lock_irqsave(&vcs->capture_lock, flags);

	if (start) {
		/* start capture only if capture isn't in progress already */
		if (vcs->capturing) {
			spin_unlock_irqrestore(&vcs->capture_lock, flags);
			return 0;
		}

	} else {
		/* capture next frame:
		 * stop capture if capturing is not set */
		if (!vcs->capturing) {
			spin_unlock_irqrestore(&vcs->capture_lock, flags);
			return 0;
		}
	}

	err = vino_queue_get_incoming(&vcs->fb_queue, &incoming);
	if (err) {
		dprintk("vino_capture_next(): vino_queue_get_incoming() "
			"failed\n");
		err = -EINVAL;
		goto out;
	}
	if (incoming == 0) {
		dprintk("vino_capture_next(): no buffers available\n");
		goto out;
	}

	fb = vino_queue_peek(&vcs->fb_queue, &id);
	if (fb == NULL) {
		dprintk("vino_capture_next(): vino_queue_peek() failed\n");
		err = -EINVAL;
		goto out;
	}

	if (start) {
		vcs->capturing = 1;
	}

	spin_unlock_irqrestore(&vcs->capture_lock, flags);

	err = vino_capture(vcs, fb);

	return err;

out:
	vcs->capturing = 0;
	spin_unlock_irqrestore(&vcs->capture_lock, flags);

	return err;
}

static inline int vino_is_capturing(struct vino_channel_settings *vcs)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&vcs->capture_lock, flags);

	ret = vcs->capturing;

	spin_unlock_irqrestore(&vcs->capture_lock, flags);

	return ret;
}

/* waits until a frame is captured */
static int vino_wait_for_frame(struct vino_channel_settings *vcs)
{
	wait_queue_t wait;
	int err = 0;

	dprintk("vino_wait_for_frame():\n");

	init_waitqueue_entry(&wait, current);
	/* add ourselves into wait queue */
	add_wait_queue(&vcs->fb_queue.frame_wait_queue, &wait);

	/* to ensure that schedule_timeout will return immediately
	 * if VINO interrupt was triggered meanwhile */
	schedule_timeout_interruptible(msecs_to_jiffies(100));

	if (signal_pending(current))
		err = -EINTR;

	remove_wait_queue(&vcs->fb_queue.frame_wait_queue, &wait);

	dprintk("vino_wait_for_frame(): waiting for frame %s\n",
		err ? "failed" : "ok");

	return err;
}

/* the function assumes that PAGE_SIZE % 4 == 0 */
static void vino_convert_to_rgba(struct vino_framebuffer *fb) {
	unsigned char *pageptr;
	unsigned int page, i;
	unsigned char a;

	for (page = 0; page < fb->desc_table.page_count; page++) {
		pageptr = (unsigned char *)fb->desc_table.virtual[page];

		for (i = 0; i < PAGE_SIZE; i += 4) {
			a = pageptr[0];
			pageptr[0] = pageptr[3];
			pageptr[1] = pageptr[2];
			pageptr[2] = pageptr[1];
			pageptr[3] = a;
			pageptr += 4;
		}
	}
}

/* checks if the buffer is in correct state and syncs data */
static int vino_check_buffer(struct vino_channel_settings *vcs,
			     struct vino_framebuffer *fb)
{
	int err = 0;
	unsigned long flags;

	dprintk("vino_check_buffer():\n");

	spin_lock_irqsave(&fb->state_lock, flags);
	switch (fb->state) {
	case VINO_FRAMEBUFFER_IN_USE:
		err = -EIO;
		break;
	case VINO_FRAMEBUFFER_READY:
		vino_sync_buffer(fb);
		fb->state = VINO_FRAMEBUFFER_UNUSED;
		break;
	default:
		err = -EINVAL;
	}
	spin_unlock_irqrestore(&fb->state_lock, flags);

	if (!err) {
		if (vino_pixel_conversion
		    && (fb->data_format == VINO_DATA_FMT_RGB32)) {
			vino_convert_to_rgba(fb);
		}
	} else if (err && (err != -EINVAL)) {
		dprintk("vino_check_buffer(): buffer not ready\n");

		spin_lock_irqsave(&vino_drvdata->vino_lock, flags);
		vino_dma_stop(vcs);
		vino_clear_interrupt(vcs);
		spin_unlock_irqrestore(&vino_drvdata->vino_lock, flags);
	}

	return err;
}

/* forcefully terminates capture */
static void vino_capture_stop(struct vino_channel_settings *vcs)
{
	unsigned int incoming = 0, outgoing = 0, id;
	unsigned long flags, flags2;

	dprintk("vino_capture_stop():\n");

	spin_lock_irqsave(&vcs->capture_lock, flags);

	/* unset capturing to stop queue processing */
	vcs->capturing = 0;

	spin_lock_irqsave(&vino_drvdata->vino_lock, flags2);

	vino_dma_stop(vcs);
	vino_clear_interrupt(vcs);

	spin_unlock_irqrestore(&vino_drvdata->vino_lock, flags2);

	/* remove all items from the queue */
	if (vino_queue_get_incoming(&vcs->fb_queue, &incoming)) {
		dprintk("vino_capture_stop(): "
			"vino_queue_get_incoming() failed\n");
		goto out;
	}
	while (incoming > 0) {
		vino_queue_transfer(&vcs->fb_queue);

		if (vino_queue_get_incoming(&vcs->fb_queue, &incoming)) {
			dprintk("vino_capture_stop(): "
				"vino_queue_get_incoming() failed\n");
			goto out;
		}
	}

	if (vino_queue_get_outgoing(&vcs->fb_queue, &outgoing)) {
		dprintk("vino_capture_stop(): "
			"vino_queue_get_outgoing() failed\n");
		goto out;
	}
	while (outgoing > 0) {
		vino_queue_remove(&vcs->fb_queue, &id);

		if (vino_queue_get_outgoing(&vcs->fb_queue, &outgoing)) {
			dprintk("vino_capture_stop(): "
				"vino_queue_get_outgoing() failed\n");
			goto out;
		}
	}

out:
	spin_unlock_irqrestore(&vcs->capture_lock, flags);
}

#if 0
static int vino_capture_failed(struct vino_channel_settings *vcs)
{
	struct vino_framebuffer *fb;
	unsigned long flags;
	unsigned int i;
	int ret;

	dprintk("vino_capture_failed():\n");

	spin_lock_irqsave(&vino_drvdata->vino_lock, flags);

	vino_dma_stop(vcs);
	vino_clear_interrupt(vcs);

	spin_unlock_irqrestore(&vino_drvdata->vino_lock, flags);

	ret = vino_queue_get_incoming(&vcs->fb_queue, &i);
	if (ret == VINO_QUEUE_ERROR) {
		dprintk("vino_queue_get_incoming() failed\n");
		return -EINVAL;
	}
	if (i == 0) {
		/* no buffers to process */
		return 0;
	}

	fb = vino_queue_peek(&vcs->fb_queue, &i);
	if (fb == NULL) {
		dprintk("vino_queue_peek() failed\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&fb->state_lock, flags);
	if (fb->state == VINO_FRAMEBUFFER_IN_USE) {
		fb->state = VINO_FRAMEBUFFER_UNUSED;
		vino_queue_transfer(&vcs->fb_queue);
		vino_queue_remove(&vcs->fb_queue, &i);
		/* we should actually discard the newest frame,
		 * but who cares ... */
	}
	spin_unlock_irqrestore(&fb->state_lock, flags);

	return 0;
}
#endif

static void vino_skip_frame(struct vino_channel_settings *vcs)
{
	struct vino_framebuffer *fb;
	unsigned long flags;
	unsigned int id;

	spin_lock_irqsave(&vcs->capture_lock, flags);
	fb = vino_queue_peek(&vcs->fb_queue, &id);
	if (!fb) {
		spin_unlock_irqrestore(&vcs->capture_lock, flags);
		dprintk("vino_skip_frame(): vino_queue_peek() failed!\n");
		return;
	}
	spin_unlock_irqrestore(&vcs->capture_lock, flags);

	spin_lock_irqsave(&fb->state_lock, flags);
	fb->state = VINO_FRAMEBUFFER_UNUSED;
	spin_unlock_irqrestore(&fb->state_lock, flags);

	vino_capture_next(vcs, 0);
}

static void vino_frame_done(struct vino_channel_settings *vcs)
{
	struct vino_framebuffer *fb;
	unsigned long flags;

	spin_lock_irqsave(&vcs->capture_lock, flags);
	fb = vino_queue_transfer(&vcs->fb_queue);
	if (!fb) {
		spin_unlock_irqrestore(&vcs->capture_lock, flags);
		dprintk("vino_frame_done(): vino_queue_transfer() failed!\n");
		return;
	}
	spin_unlock_irqrestore(&vcs->capture_lock, flags);

	fb->frame_counter = vcs->int_data.frame_counter;
	memcpy(&fb->timestamp, &vcs->int_data.timestamp,
	       sizeof(struct timeval));

	spin_lock_irqsave(&fb->state_lock, flags);
	if (fb->state == VINO_FRAMEBUFFER_IN_USE)
		fb->state = VINO_FRAMEBUFFER_READY;
	spin_unlock_irqrestore(&fb->state_lock, flags);

	wake_up(&vcs->fb_queue.frame_wait_queue);

	vino_capture_next(vcs, 0);
}

static void vino_capture_tasklet(unsigned long channel) {
	struct vino_channel_settings *vcs;

	vcs = (channel == VINO_CHANNEL_A)
		? &vino_drvdata->a : &vino_drvdata->b;

	if (vcs->int_data.skip)
		vcs->int_data.skip_count++;

	if (vcs->int_data.skip && (vcs->int_data.skip_count
				   <= VINO_MAX_FRAME_SKIP_COUNT)) {
		vino_skip_frame(vcs);
	} else {
		vcs->int_data.skip_count = 0;
		vino_frame_done(vcs);
	}
}

static irqreturn_t vino_interrupt(int irq, void *dev_id)
{
	u32 ctrl, intr;
	unsigned int fc_a, fc_b;
	int handled_a = 0, skip_a = 0, done_a = 0;
	int handled_b = 0, skip_b = 0, done_b = 0;

#ifdef VINO_DEBUG_INT
	int loop = 0;
	unsigned int line_count = vino->a.line_count,
		page_index = vino->a.page_index,
		field_counter = vino->a.field_counter,
		start_desc_tbl = vino->a.start_desc_tbl,
		next_4_desc = vino->a.next_4_desc;
	unsigned int line_count_2,
		page_index_2,
		field_counter_2,
		start_desc_tbl_2,
		next_4_desc_2;
#endif

	spin_lock(&vino_drvdata->vino_lock);

	while ((intr = vino->intr_status)) {
		fc_a = vino->a.field_counter >> 1;
		fc_b = vino->b.field_counter >> 1;

		/* handle error-interrupts in some special way ?
		 * --> skips frames */
		if (intr & VINO_INTSTAT_A) {
			if (intr & VINO_INTSTAT_A_EOF) {
				vino_drvdata->a.field++;
				if (vino_drvdata->a.field > 1) {
					vino_dma_stop(&vino_drvdata->a);
					vino_clear_interrupt(&vino_drvdata->a);
					vino_drvdata->a.field = 0;
					done_a = 1;
				} else {
					if (vino->a.page_index
					    != vino_drvdata->a.line_size) {
						vino->a.line_count = 0;
						vino->a.page_index =
							vino_drvdata->
							a.line_size;
						vino->a.next_4_desc =
							vino->a.start_desc_tbl;
					}
				}
				dprintk("channel A end-of-field "
					"interrupt: %04x\n", intr);
			} else {
				vino_dma_stop(&vino_drvdata->a);
				vino_clear_interrupt(&vino_drvdata->a);
				vino_drvdata->a.field = 0;
				skip_a = 1;
				dprintk("channel A error interrupt: %04x\n",
					intr);
			}

#ifdef VINO_DEBUG_INT
			line_count_2 = vino->a.line_count;
			page_index_2 = vino->a.page_index;
			field_counter_2 = vino->a.field_counter;
			start_desc_tbl_2 = vino->a.start_desc_tbl;
			next_4_desc_2 = vino->a.next_4_desc;

			printk("intr = %04x, loop = %d, field = %d\n",
			       intr, loop, vino_drvdata->a.field);
			printk("1- line count = %04d, page index = %04d, "
			       "start = %08x, next = %08x\n"
			       "   fieldc = %d, framec = %d\n",
			       line_count, page_index, start_desc_tbl,
			       next_4_desc, field_counter, fc_a);
			printk("12-line count = %04d, page index = %04d, "
			       "   start = %08x, next = %08x\n",
			       line_count_2, page_index_2, start_desc_tbl_2,
			       next_4_desc_2);

			if (done_a)
				printk("\n");
#endif
		}

		if (intr & VINO_INTSTAT_B) {
			if (intr & VINO_INTSTAT_B_EOF) {
				vino_drvdata->b.field++;
				if (vino_drvdata->b.field > 1) {
					vino_dma_stop(&vino_drvdata->b);
					vino_clear_interrupt(&vino_drvdata->b);
					vino_drvdata->b.field = 0;
					done_b = 1;
				}
				dprintk("channel B end-of-field "
					"interrupt: %04x\n", intr);
			} else {
				vino_dma_stop(&vino_drvdata->b);
				vino_clear_interrupt(&vino_drvdata->b);
				vino_drvdata->b.field = 0;
				skip_b = 1;
				dprintk("channel B error interrupt: %04x\n",
					intr);
			}
		}

		/* Always remember to clear interrupt status.
		 * Disable VINO interrupts while we do this. */
		ctrl = vino->control;
		vino->control = ctrl & ~(VINO_CTRL_A_INT | VINO_CTRL_B_INT);
		vino->intr_status = ~intr;
		vino->control = ctrl;

		spin_unlock(&vino_drvdata->vino_lock);

		if ((!handled_a) && (done_a || skip_a)) {
			if (!skip_a) {
				v4l2_get_timestamp(
					&vino_drvdata->a.int_data.timestamp);
				vino_drvdata->a.int_data.frame_counter = fc_a;
			}
			vino_drvdata->a.int_data.skip = skip_a;

			dprintk("channel A %s, interrupt: %d\n",
				skip_a ? "skipping frame" : "frame done",
				intr);
			tasklet_hi_schedule(&vino_tasklet_a);
			handled_a = 1;
		}

		if ((!handled_b) && (done_b || skip_b)) {
			if (!skip_b) {
				v4l2_get_timestamp(
					&vino_drvdata->b.int_data.timestamp);
				vino_drvdata->b.int_data.frame_counter = fc_b;
			}
			vino_drvdata->b.int_data.skip = skip_b;

			dprintk("channel B %s, interrupt: %d\n",
				skip_b ? "skipping frame" : "frame done",
				intr);
			tasklet_hi_schedule(&vino_tasklet_b);
			handled_b = 1;
		}

#ifdef VINO_DEBUG_INT
		loop++;
#endif
		spin_lock(&vino_drvdata->vino_lock);
	}

	spin_unlock(&vino_drvdata->vino_lock);

	return IRQ_HANDLED;
}

/* VINO video input management */

static int vino_get_saa7191_input(int input)
{
	switch (input) {
	case VINO_INPUT_COMPOSITE:
		return SAA7191_INPUT_COMPOSITE;
	case VINO_INPUT_SVIDEO:
		return SAA7191_INPUT_SVIDEO;
	default:
		printk(KERN_ERR "VINO: vino_get_saa7191_input(): "
		       "invalid input!\n");
		return -1;
	}
}

/* execute with input_lock locked */
static int vino_is_input_owner(struct vino_channel_settings *vcs)
{
	switch(vcs->input) {
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO:
		return vino_drvdata->decoder_owner == vcs->channel;
	case VINO_INPUT_D1:
		return vino_drvdata->camera_owner == vcs->channel;
	default:
		return 0;
	}
}

static int vino_acquire_input(struct vino_channel_settings *vcs)
{
	unsigned long flags;
	int ret = 0;

	dprintk("vino_acquire_input():\n");

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	/* First try D1 and then SAA7191 */
	if (vino_drvdata->camera
	    && (vino_drvdata->camera_owner == VINO_NO_CHANNEL)) {
		vino_drvdata->camera_owner = vcs->channel;
		vcs->input = VINO_INPUT_D1;
		vcs->data_norm = VINO_DATA_NORM_D1;
	} else if (vino_drvdata->decoder
		   && (vino_drvdata->decoder_owner == VINO_NO_CHANNEL)) {
		int input;
		int data_norm = 0;
		v4l2_std_id norm;

		input = VINO_INPUT_COMPOSITE;

		ret = decoder_call(video, s_routing,
				vino_get_saa7191_input(input), 0, 0);
		if (ret) {
			ret = -EINVAL;
			goto out;
		}

		spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

		/* Don't hold spinlocks while auto-detecting norm
		 * as it may take a while... */

		ret = decoder_call(video, querystd, &norm);
		if (!ret) {
			for (data_norm = 0; data_norm < 3; data_norm++) {
				if (vino_data_norms[data_norm].std & norm)
					break;
			}
			if (data_norm == 3)
				data_norm = VINO_DATA_NORM_PAL;
			ret = decoder_call(video, s_std, norm);
		}

		spin_lock_irqsave(&vino_drvdata->input_lock, flags);

		if (ret) {
			ret = -EINVAL;
			goto out;
		}

		vino_drvdata->decoder_owner = vcs->channel;

		vcs->input = input;
		vcs->data_norm = data_norm;
	} else {
		vcs->input = (vcs->channel == VINO_CHANNEL_A) ?
			vino_drvdata->b.input : vino_drvdata->a.input;
		vcs->data_norm = (vcs->channel == VINO_CHANNEL_A) ?
			vino_drvdata->b.data_norm : vino_drvdata->a.data_norm;
	}

	if (vcs->input == VINO_INPUT_NONE) {
		ret = -ENODEV;
		goto out;
	}

	vino_set_default_clipping(vcs);
	vino_set_default_scaling(vcs);
	vino_set_default_framerate(vcs);

	dprintk("vino_acquire_input(): %s\n", vino_inputs[vcs->input].name);

out:
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return ret;
}

static int vino_set_input(struct vino_channel_settings *vcs, int input)
{
	struct vino_channel_settings *vcs2 = (vcs->channel == VINO_CHANNEL_A) ?
		&vino_drvdata->b : &vino_drvdata->a;
	unsigned long flags;
	int ret = 0;

	dprintk("vino_set_input():\n");

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	if (vcs->input == input)
		goto out;

	switch (input) {
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO:
		if (!vino_drvdata->decoder) {
			ret = -EINVAL;
			goto out;
		}

		if (vino_drvdata->decoder_owner == VINO_NO_CHANNEL) {
			vino_drvdata->decoder_owner = vcs->channel;
		}

		if (vino_drvdata->decoder_owner == vcs->channel) {
			int data_norm = 0;
			v4l2_std_id norm;

			ret = decoder_call(video, s_routing,
					vino_get_saa7191_input(input), 0, 0);
			if (ret) {
				vino_drvdata->decoder_owner = VINO_NO_CHANNEL;
				ret = -EINVAL;
				goto out;
			}

			spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

			/* Don't hold spinlocks while auto-detecting norm
			 * as it may take a while... */

			ret = decoder_call(video, querystd, &norm);
			if (!ret) {
				for (data_norm = 0; data_norm < 3; data_norm++) {
					if (vino_data_norms[data_norm].std & norm)
						break;
				}
				if (data_norm == 3)
					data_norm = VINO_DATA_NORM_PAL;
				ret = decoder_call(video, s_std, norm);
			}

			spin_lock_irqsave(&vino_drvdata->input_lock, flags);

			if (ret) {
				vino_drvdata->decoder_owner = VINO_NO_CHANNEL;
				ret = -EINVAL;
				goto out;
			}

			vcs->input = input;
			vcs->data_norm = data_norm;
		} else {
			if (input != vcs2->input) {
				ret = -EBUSY;
				goto out;
			}

			vcs->input = input;
			vcs->data_norm = vcs2->data_norm;
		}

		if (vino_drvdata->camera_owner == vcs->channel) {
			/* Transfer the ownership or release the input */
			if (vcs2->input == VINO_INPUT_D1) {
				vino_drvdata->camera_owner = vcs2->channel;
			} else {
				vino_drvdata->camera_owner = VINO_NO_CHANNEL;
			}
		}
		break;
	case VINO_INPUT_D1:
		if (!vino_drvdata->camera) {
			ret = -EINVAL;
			goto out;
		}

		if (vino_drvdata->camera_owner == VINO_NO_CHANNEL)
			vino_drvdata->camera_owner = vcs->channel;

		if (vino_drvdata->decoder_owner == vcs->channel) {
			/* Transfer the ownership or release the input */
			if ((vcs2->input == VINO_INPUT_COMPOSITE) ||
				 (vcs2->input == VINO_INPUT_SVIDEO)) {
				vino_drvdata->decoder_owner = vcs2->channel;
			} else {
				vino_drvdata->decoder_owner = VINO_NO_CHANNEL;
			}
		}

		vcs->input = input;
		vcs->data_norm = VINO_DATA_NORM_D1;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	vino_set_default_clipping(vcs);
	vino_set_default_scaling(vcs);
	vino_set_default_framerate(vcs);

	dprintk("vino_set_input(): %s\n", vino_inputs[vcs->input].name);

out:
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return ret;
}

static void vino_release_input(struct vino_channel_settings *vcs)
{
	struct vino_channel_settings *vcs2 = (vcs->channel == VINO_CHANNEL_A) ?
		&vino_drvdata->b : &vino_drvdata->a;
	unsigned long flags;

	dprintk("vino_release_input():\n");

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	/* Release ownership of the channel
	 * and if the other channel takes input from
	 * the same source, transfer the ownership */
	if (vino_drvdata->camera_owner == vcs->channel) {
		if (vcs2->input == VINO_INPUT_D1) {
			vino_drvdata->camera_owner = vcs2->channel;
		} else {
			vino_drvdata->camera_owner = VINO_NO_CHANNEL;
		}
	} else if (vino_drvdata->decoder_owner == vcs->channel) {
		if ((vcs2->input == VINO_INPUT_COMPOSITE) ||
			 (vcs2->input == VINO_INPUT_SVIDEO)) {
			vino_drvdata->decoder_owner = vcs2->channel;
		} else {
			vino_drvdata->decoder_owner = VINO_NO_CHANNEL;
		}
	}
	vcs->input = VINO_INPUT_NONE;

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);
}

/* execute with input_lock locked */
static int vino_set_data_norm(struct vino_channel_settings *vcs,
			      unsigned int data_norm,
			      unsigned long *flags)
{
	int err = 0;

	if (data_norm == vcs->data_norm)
		return 0;

	switch (vcs->input) {
	case VINO_INPUT_D1:
		/* only one "norm" supported */
		if (data_norm != VINO_DATA_NORM_D1)
			return -EINVAL;
		break;
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO: {
		v4l2_std_id norm;

		if ((data_norm != VINO_DATA_NORM_PAL)
		    && (data_norm != VINO_DATA_NORM_NTSC)
		    && (data_norm != VINO_DATA_NORM_SECAM))
			return -EINVAL;

		spin_unlock_irqrestore(&vino_drvdata->input_lock, *flags);

		/* Don't hold spinlocks while setting norm
		 * as it may take a while... */

		norm = vino_data_norms[data_norm].std;
		err = decoder_call(video, s_std, norm);

		spin_lock_irqsave(&vino_drvdata->input_lock, *flags);

		if (err)
			goto out;

		vcs->data_norm = data_norm;

		vino_set_default_clipping(vcs);
		vino_set_default_scaling(vcs);
		vino_set_default_framerate(vcs);
		break;
	}
	default:
		return -EINVAL;
	}

out:
	return err;
}

/* V4L2 helper functions */

static int vino_find_data_format(__u32 pixelformat)
{
	int i;

	for (i = 0; i < VINO_DATA_FMT_COUNT; i++) {
		if (vino_data_formats[i].pixelformat == pixelformat)
			return i;
	}

	return VINO_DATA_FMT_NONE;
}

static int vino_int_enum_input(struct vino_channel_settings *vcs, __u32 index)
{
	int input = VINO_INPUT_NONE;
	unsigned long flags;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);
	if (vino_drvdata->decoder && vino_drvdata->camera) {
		switch (index) {
		case 0:
			input = VINO_INPUT_COMPOSITE;
			break;
		case 1:
			input = VINO_INPUT_SVIDEO;
			break;
		case 2:
			input = VINO_INPUT_D1;
			break;
		}
	} else if (vino_drvdata->decoder) {
		switch (index) {
		case 0:
			input = VINO_INPUT_COMPOSITE;
			break;
		case 1:
			input = VINO_INPUT_SVIDEO;
			break;
		}
	} else if (vino_drvdata->camera) {
		switch (index) {
		case 0:
			input = VINO_INPUT_D1;
			break;
		}
	}
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return input;
}

/* execute with input_lock locked */
static __u32 vino_find_input_index(struct vino_channel_settings *vcs)
{
	__u32 index = 0;
	// FIXME: detect when no inputs available

	if (vino_drvdata->decoder && vino_drvdata->camera) {
		switch (vcs->input) {
		case VINO_INPUT_COMPOSITE:
			index = 0;
			break;
		case VINO_INPUT_SVIDEO:
			index = 1;
			break;
		case VINO_INPUT_D1:
			index = 2;
			break;
		}
	} else if (vino_drvdata->decoder) {
		switch (vcs->input) {
		case VINO_INPUT_COMPOSITE:
			index = 0;
			break;
		case VINO_INPUT_SVIDEO:
			index = 1;
			break;
		}
	} else if (vino_drvdata->camera) {
		switch (vcs->input) {
		case VINO_INPUT_D1:
			index = 0;
			break;
		}
	}

	return index;
}

/* V4L2 ioctls */

static int vino_querycap(struct file *file, void *__fh,
		struct v4l2_capability *cap)
{
	memset(cap, 0, sizeof(struct v4l2_capability));

	strcpy(cap->driver, vino_driver_name);
	strcpy(cap->card, vino_driver_description);
	strcpy(cap->bus_info, vino_bus_name);
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vino_enum_input(struct file *file, void *__fh,
			       struct v4l2_input *i)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	__u32 index = i->index;
	int input;
	dprintk("requested index = %d\n", index);

	input = vino_int_enum_input(vcs, index);
	if (input == VINO_INPUT_NONE)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = vino_inputs[input].std;
	strcpy(i->name, vino_inputs[input].name);

	if (input == VINO_INPUT_COMPOSITE || input == VINO_INPUT_SVIDEO)
		decoder_call(video, g_input_status, &i->status);
	return 0;
}

static int vino_g_input(struct file *file, void *__fh,
			     unsigned int *i)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	__u32 index;
	int input;
	unsigned long flags;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);
	input = vcs->input;
	index = vino_find_input_index(vcs);
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	dprintk("input = %d\n", input);

	if (input == VINO_INPUT_NONE) {
		return -EINVAL;
	}

	*i = index;

	return 0;
}

static int vino_s_input(struct file *file, void *__fh,
			     unsigned int i)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	int input;
	dprintk("requested input = %d\n", i);

	input = vino_int_enum_input(vcs, i);
	if (input == VINO_INPUT_NONE)
		return -EINVAL;

	return vino_set_input(vcs, input);
}

static int vino_querystd(struct file *file, void *__fh,
			      v4l2_std_id *std)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	switch (vcs->input) {
	case VINO_INPUT_D1:
		*std = vino_inputs[vcs->input].std;
		break;
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO: {
		decoder_call(video, querystd, std);
		break;
	}
	default:
		err = -EINVAL;
	}

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return err;
}

static int vino_g_std(struct file *file, void *__fh,
			   v4l2_std_id *std)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	*std = vino_data_norms[vcs->data_norm].std;
	dprintk("current standard = %d\n", vcs->data_norm);

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return 0;
}

static int vino_s_std(struct file *file, void *__fh,
			   v4l2_std_id std)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	if (!vino_is_input_owner(vcs)) {
		ret = -EBUSY;
		goto out;
	}

	/* check if the standard is valid for the current input */
	if (std & vino_inputs[vcs->input].std) {
		dprintk("standard accepted\n");

		/* change the video norm for SAA7191
		 * and accept NTSC for D1 (do nothing) */

		if (vcs->input == VINO_INPUT_D1)
			goto out;

		if (std & V4L2_STD_PAL) {
			ret = vino_set_data_norm(vcs, VINO_DATA_NORM_PAL,
						 &flags);
		} else if (std & V4L2_STD_NTSC) {
			ret = vino_set_data_norm(vcs, VINO_DATA_NORM_NTSC,
						 &flags);
		} else if (std & V4L2_STD_SECAM) {
			ret = vino_set_data_norm(vcs, VINO_DATA_NORM_SECAM,
						 &flags);
		} else {
			ret = -EINVAL;
		}

		if (ret) {
			ret = -EINVAL;
		}
	} else {
		ret = -EINVAL;
	}

out:
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return ret;
}

static int vino_enum_fmt_vid_cap(struct file *file, void *__fh,
			      struct v4l2_fmtdesc *fd)
{
	dprintk("format index = %d\n", fd->index);

	if (fd->index >= VINO_DATA_FMT_COUNT)
		return -EINVAL;
	dprintk("format name = %s\n", vino_data_formats[fd->index].description);

	fd->pixelformat = vino_data_formats[fd->index].pixelformat;
	strcpy(fd->description, vino_data_formats[fd->index].description);
	return 0;
}

static int vino_try_fmt_vid_cap(struct file *file, void *__fh,
			     struct v4l2_format *f)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	struct vino_channel_settings tempvcs;
	unsigned long flags;
	struct v4l2_pix_format *pf = &f->fmt.pix;

	dprintk("requested: w = %d, h = %d\n",
			pf->width, pf->height);

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);
	memcpy(&tempvcs, vcs, sizeof(struct vino_channel_settings));
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	tempvcs.data_format = vino_find_data_format(pf->pixelformat);
	if (tempvcs.data_format == VINO_DATA_FMT_NONE) {
		tempvcs.data_format = VINO_DATA_FMT_GREY;
		pf->pixelformat =
			vino_data_formats[tempvcs.data_format].
			pixelformat;
	}

	/* data format must be set before clipping/scaling */
	vino_set_scaling(&tempvcs, pf->width, pf->height);

	dprintk("data format = %s\n",
			vino_data_formats[tempvcs.data_format].description);

	pf->width = (tempvcs.clipping.right - tempvcs.clipping.left) /
		tempvcs.decimation;
	pf->height = (tempvcs.clipping.bottom - tempvcs.clipping.top) /
		tempvcs.decimation;

	pf->field = V4L2_FIELD_INTERLACED;
	pf->bytesperline = tempvcs.line_size;
	pf->sizeimage = tempvcs.line_size *
		(tempvcs.clipping.bottom - tempvcs.clipping.top) /
		tempvcs.decimation;
	pf->colorspace =
		vino_data_formats[tempvcs.data_format].colorspace;

	return 0;
}

static int vino_g_fmt_vid_cap(struct file *file, void *__fh,
			   struct v4l2_format *f)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	struct v4l2_pix_format *pf = &f->fmt.pix;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	pf->width = (vcs->clipping.right - vcs->clipping.left) /
		vcs->decimation;
	pf->height = (vcs->clipping.bottom - vcs->clipping.top) /
		vcs->decimation;
	pf->pixelformat =
		vino_data_formats[vcs->data_format].pixelformat;

	pf->field = V4L2_FIELD_INTERLACED;
	pf->bytesperline = vcs->line_size;
	pf->sizeimage = vcs->line_size *
		(vcs->clipping.bottom - vcs->clipping.top) /
		vcs->decimation;
	pf->colorspace =
		vino_data_formats[vcs->data_format].colorspace;

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);
	return 0;
}

static int vino_s_fmt_vid_cap(struct file *file, void *__fh,
			   struct v4l2_format *f)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	int data_format;
	unsigned long flags;
	struct v4l2_pix_format *pf = &f->fmt.pix;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	data_format = vino_find_data_format(pf->pixelformat);

	if (data_format == VINO_DATA_FMT_NONE) {
		vcs->data_format = VINO_DATA_FMT_GREY;
		pf->pixelformat =
			vino_data_formats[vcs->data_format].
			pixelformat;
	} else {
		vcs->data_format = data_format;
	}

	/* data format must be set before clipping/scaling */
	vino_set_scaling(vcs, pf->width, pf->height);

	dprintk("data format = %s\n",
	       vino_data_formats[vcs->data_format].description);

	pf->width = vcs->clipping.right - vcs->clipping.left;
	pf->height = vcs->clipping.bottom - vcs->clipping.top;

	pf->field = V4L2_FIELD_INTERLACED;
	pf->bytesperline = vcs->line_size;
	pf->sizeimage = vcs->line_size *
		(vcs->clipping.bottom - vcs->clipping.top) /
		vcs->decimation;
	pf->colorspace =
		vino_data_formats[vcs->data_format].colorspace;

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);
	return 0;
}

static int vino_cropcap(struct file *file, void *__fh,
			     struct v4l2_cropcap *ccap)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	const struct vino_data_norm *norm;
	unsigned long flags;

	switch (ccap->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		spin_lock_irqsave(&vino_drvdata->input_lock, flags);

		norm = &vino_data_norms[vcs->data_norm];

		spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

		ccap->bounds.left = 0;
		ccap->bounds.top = 0;
		ccap->bounds.width = norm->width;
		ccap->bounds.height = norm->height;
		memcpy(&ccap->defrect, &ccap->bounds,
		       sizeof(struct v4l2_rect));

		ccap->pixelaspect.numerator = 1;
		ccap->pixelaspect.denominator = 1;
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	default:
		return -EINVAL;
	}

	return 0;
}

static int vino_g_crop(struct file *file, void *__fh,
			    struct v4l2_crop *c)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;

	switch (c->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		spin_lock_irqsave(&vino_drvdata->input_lock, flags);

		c->c.left = vcs->clipping.left;
		c->c.top = vcs->clipping.top;
		c->c.width = vcs->clipping.right - vcs->clipping.left;
		c->c.height = vcs->clipping.bottom - vcs->clipping.top;

		spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	default:
		return -EINVAL;
	}

	return 0;
}

static int vino_s_crop(struct file *file, void *__fh,
			    const struct v4l2_crop *c)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;

	switch (c->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		spin_lock_irqsave(&vino_drvdata->input_lock, flags);

		vino_set_clipping(vcs, c->c.left, c->c.top,
				  c->c.width, c->c.height);

		spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	default:
		return -EINVAL;
	}

	return 0;
}

static int vino_g_parm(struct file *file, void *__fh,
			    struct v4l2_streamparm *sp)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	struct v4l2_captureparm *cp = &sp->parm.capture;

	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = 1;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	cp->timeperframe.denominator = vcs->fps;

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	/* TODO: cp->readbuffers = xxx; */

	return 0;
}

static int vino_s_parm(struct file *file, void *__fh,
			    struct v4l2_streamparm *sp)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	struct v4l2_captureparm *cp = &sp->parm.capture;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	if ((cp->timeperframe.numerator == 0) ||
	    (cp->timeperframe.denominator == 0)) {
		/* reset framerate */
		vino_set_default_framerate(vcs);
	} else {
		vino_set_framerate(vcs, cp->timeperframe.denominator /
				   cp->timeperframe.numerator);
	}

	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return 0;
}

static int vino_reqbufs(struct file *file, void *__fh,
			     struct v4l2_requestbuffers *rb)
{
	struct vino_channel_settings *vcs = video_drvdata(file);

	if (vcs->reading)
		return -EBUSY;

	/* TODO: check queue type */
	if (rb->memory != V4L2_MEMORY_MMAP) {
		dprintk("type not mmap\n");
		return -EINVAL;
	}

	dprintk("count = %d\n", rb->count);
	if (rb->count > 0) {
		if (vino_is_capturing(vcs)) {
			dprintk("busy, capturing\n");
			return -EBUSY;
		}

		if (vino_queue_has_mapped_buffers(&vcs->fb_queue)) {
			dprintk("busy, buffers still mapped\n");
			return -EBUSY;
		} else {
			vcs->streaming = 0;
			vino_queue_free(&vcs->fb_queue);
			vino_queue_init(&vcs->fb_queue, &rb->count);
		}
	} else {
		vcs->streaming = 0;
		vino_capture_stop(vcs);
		vino_queue_free(&vcs->fb_queue);
	}

	return 0;
}

static void vino_v4l2_get_buffer_status(struct vino_channel_settings *vcs,
					struct vino_framebuffer *fb,
					struct v4l2_buffer *b)
{
	if (vino_queue_outgoing_contains(&vcs->fb_queue,
					 fb->id)) {
		b->flags &= ~V4L2_BUF_FLAG_QUEUED;
		b->flags |= V4L2_BUF_FLAG_DONE;
	} else if (vino_queue_incoming_contains(&vcs->fb_queue,
				       fb->id)) {
		b->flags &= ~V4L2_BUF_FLAG_DONE;
		b->flags |= V4L2_BUF_FLAG_QUEUED;
	} else {
		b->flags &= ~(V4L2_BUF_FLAG_DONE |
			      V4L2_BUF_FLAG_QUEUED);
	}

	b->flags &= ~(V4L2_BUF_FLAG_TIMECODE);

	if (fb->map_count > 0)
		b->flags |= V4L2_BUF_FLAG_MAPPED;

	b->flags &= ~V4L2_BUF_FLAG_TIMESTAMP_MASK;
	b->flags |= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	b->index = fb->id;
	b->memory = (vcs->fb_queue.type == VINO_MEMORY_MMAP) ?
		V4L2_MEMORY_MMAP : V4L2_MEMORY_USERPTR;
	b->m.offset = fb->offset;
	b->bytesused = fb->data_size;
	b->length = fb->size;
	b->field = V4L2_FIELD_INTERLACED;
	b->sequence = fb->frame_counter;
	memcpy(&b->timestamp, &fb->timestamp,
	       sizeof(struct timeval));
	// b->input ?

	dprintk("buffer %d: length = %d, bytesused = %d, offset = %d\n",
		fb->id, fb->size, fb->data_size, fb->offset);
}

static int vino_querybuf(struct file *file, void *__fh,
			      struct v4l2_buffer *b)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	struct vino_framebuffer *fb;

	if (vcs->reading)
		return -EBUSY;

	/* TODO: check queue type */
	if (b->index >= vino_queue_get_length(&vcs->fb_queue)) {
		dprintk("invalid index = %d\n",
		       b->index);
		return -EINVAL;
	}

	fb = vino_queue_get_buffer(&vcs->fb_queue,
				   b->index);
	if (fb == NULL) {
		dprintk("vino_queue_get_buffer() failed");
		return -EINVAL;
	}

	vino_v4l2_get_buffer_status(vcs, fb, b);

	return 0;
}

static int vino_qbuf(struct file *file, void *__fh,
			  struct v4l2_buffer *b)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	struct vino_framebuffer *fb;
	int ret;

	if (vcs->reading)
		return -EBUSY;

	/* TODO: check queue type */
	if (b->memory != V4L2_MEMORY_MMAP) {
		dprintk("type not mmap\n");
		return -EINVAL;
	}

	fb = vino_capture_enqueue(vcs, b->index);
	if (fb == NULL)
		return -EINVAL;

	vino_v4l2_get_buffer_status(vcs, fb, b);

	if (vcs->streaming) {
		ret = vino_capture_next(vcs, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int vino_dqbuf(struct file *file, void *__fh,
			   struct v4l2_buffer *b)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned int nonblocking = file->f_flags & O_NONBLOCK;
	struct vino_framebuffer *fb;
	unsigned int incoming, outgoing;
	int err;

	if (vcs->reading)
		return -EBUSY;

	/* TODO: check queue type */

	err = vino_queue_get_incoming(&vcs->fb_queue, &incoming);
	if (err) {
		dprintk("vino_queue_get_incoming() failed\n");
		return -EINVAL;
	}
	err = vino_queue_get_outgoing(&vcs->fb_queue, &outgoing);
	if (err) {
		dprintk("vino_queue_get_outgoing() failed\n");
		return -EINVAL;
	}

	dprintk("incoming = %d, outgoing = %d\n", incoming, outgoing);

	if (outgoing == 0) {
		if (incoming == 0) {
			dprintk("no incoming or outgoing buffers\n");
			return -EINVAL;
		}
		if (nonblocking) {
			dprintk("non-blocking I/O was selected and "
				"there are no buffers to dequeue\n");
			return -EAGAIN;
		}

		err = vino_wait_for_frame(vcs);
		if (err) {
			err = vino_wait_for_frame(vcs);
			if (err) {
				/* interrupted or no frames captured because of
				 * frame skipping */
				/* vino_capture_failed(vcs); */
				return -EIO;
			}
		}
	}

	fb = vino_queue_remove(&vcs->fb_queue, &b->index);
	if (fb == NULL) {
		dprintk("vino_queue_remove() failed\n");
		return -EINVAL;
	}

	err = vino_check_buffer(vcs, fb);

	vino_v4l2_get_buffer_status(vcs, fb, b);

	if (err)
		return -EIO;

	return 0;
}

static int vino_streamon(struct file *file, void *__fh,
		enum v4l2_buf_type i)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned int incoming;
	int ret;
	if (vcs->reading)
		return -EBUSY;

	if (vcs->streaming)
		return 0;

	// TODO: check queue type

	if (vino_queue_get_length(&vcs->fb_queue) < 1) {
		dprintk("no buffers allocated\n");
		return -EINVAL;
	}

	ret = vino_queue_get_incoming(&vcs->fb_queue, &incoming);
	if (ret) {
		dprintk("vino_queue_get_incoming() failed\n");
		return -EINVAL;
	}

	vcs->streaming = 1;

	if (incoming > 0) {
		ret = vino_capture_next(vcs, 1);
		if (ret) {
			vcs->streaming = 0;

			dprintk("couldn't start capture\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int vino_streamoff(struct file *file, void *__fh,
		enum v4l2_buf_type i)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	if (vcs->reading)
		return -EBUSY;

	if (!vcs->streaming)
		return 0;

	vcs->streaming = 0;
	vino_capture_stop(vcs);

	return 0;
}

static int vino_queryctrl(struct file *file, void *__fh,
			       struct v4l2_queryctrl *queryctrl)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	int i;
	int err = 0;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	switch (vcs->input) {
	case VINO_INPUT_D1:
		for (i = 0; i < VINO_INDYCAM_V4L2_CONTROL_COUNT; i++) {
			if (vino_indycam_v4l2_controls[i].id ==
			    queryctrl->id) {
				memcpy(queryctrl,
				       &vino_indycam_v4l2_controls[i],
				       sizeof(struct v4l2_queryctrl));
				queryctrl->reserved[0] = 0;
				goto found;
			}
		}

		err =  -EINVAL;
		break;
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO:
		for (i = 0; i < VINO_SAA7191_V4L2_CONTROL_COUNT; i++) {
			if (vino_saa7191_v4l2_controls[i].id ==
			    queryctrl->id) {
				memcpy(queryctrl,
				       &vino_saa7191_v4l2_controls[i],
				       sizeof(struct v4l2_queryctrl));
				queryctrl->reserved[0] = 0;
				goto found;
			}
		}

		err =  -EINVAL;
		break;
	default:
		err =  -EINVAL;
	}

 found:
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return err;
}

static int vino_g_ctrl(struct file *file, void *__fh,
			    struct v4l2_control *control)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	int i;
	int err = 0;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	switch (vcs->input) {
	case VINO_INPUT_D1: {
		err = -EINVAL;
		for (i = 0; i < VINO_INDYCAM_V4L2_CONTROL_COUNT; i++) {
			if (vino_indycam_v4l2_controls[i].id == control->id) {
				err = 0;
				break;
			}
		}

		if (err)
			goto out;

		err = camera_call(core, g_ctrl, control);
		if (err)
			err = -EINVAL;
		break;
	}
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO: {
		err = -EINVAL;
		for (i = 0; i < VINO_SAA7191_V4L2_CONTROL_COUNT; i++) {
			if (vino_saa7191_v4l2_controls[i].id == control->id) {
				err = 0;
				break;
			}
		}

		if (err)
			goto out;

		err = decoder_call(core, g_ctrl, control);
		if (err)
			err = -EINVAL;
		break;
	}
	default:
		err =  -EINVAL;
	}

out:
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return err;
}

static int vino_s_ctrl(struct file *file, void *__fh,
			    struct v4l2_control *control)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned long flags;
	int i;
	int err = 0;

	spin_lock_irqsave(&vino_drvdata->input_lock, flags);

	if (!vino_is_input_owner(vcs)) {
		err = -EBUSY;
		goto out;
	}

	switch (vcs->input) {
	case VINO_INPUT_D1: {
		err = -EINVAL;
		for (i = 0; i < VINO_INDYCAM_V4L2_CONTROL_COUNT; i++) {
			if (vino_indycam_v4l2_controls[i].id == control->id) {
				err = 0;
				break;
			}
		}
		if (err)
			goto out;
		if (control->value < vino_indycam_v4l2_controls[i].minimum ||
		    control->value > vino_indycam_v4l2_controls[i].maximum) {
			err = -ERANGE;
			goto out;
		}
		err = camera_call(core, s_ctrl, control);
		if (err)
			err = -EINVAL;
		break;
	}
	case VINO_INPUT_COMPOSITE:
	case VINO_INPUT_SVIDEO: {
		err = -EINVAL;
		for (i = 0; i < VINO_SAA7191_V4L2_CONTROL_COUNT; i++) {
			if (vino_saa7191_v4l2_controls[i].id == control->id) {
				err = 0;
				break;
			}
		}
		if (err)
			goto out;
		if (control->value < vino_saa7191_v4l2_controls[i].minimum ||
		    control->value > vino_saa7191_v4l2_controls[i].maximum) {
			err = -ERANGE;
			goto out;
		}

		err = decoder_call(core, s_ctrl, control);
		if (err)
			err = -EINVAL;
		break;
	}
	default:
		err =  -EINVAL;
	}

out:
	spin_unlock_irqrestore(&vino_drvdata->input_lock, flags);

	return err;
}

/* File operations */

static int vino_open(struct file *file)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	int ret = 0;
	dprintk("open(): channel = %c\n",
	       (vcs->channel == VINO_CHANNEL_A) ? 'A' : 'B');

	mutex_lock(&vcs->mutex);

	if (vcs->users) {
		dprintk("open(): driver busy\n");
		ret = -EBUSY;
		goto out;
	}

	ret = vino_acquire_input(vcs);
	if (ret) {
		dprintk("open(): vino_acquire_input() failed\n");
		goto out;
	}

	vcs->users++;

 out:
	mutex_unlock(&vcs->mutex);

	dprintk("open(): %s!\n", ret ? "failed" : "complete");

	return ret;
}

static int vino_close(struct file *file)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	dprintk("close():\n");

	mutex_lock(&vcs->mutex);

	vcs->users--;

	if (!vcs->users) {
		vino_release_input(vcs);

		/* stop DMA and free buffers */
		vino_capture_stop(vcs);
		vino_queue_free(&vcs->fb_queue);
	}

	mutex_unlock(&vcs->mutex);

	return 0;
}

static void vino_vm_open(struct vm_area_struct *vma)
{
	struct vino_framebuffer *fb = vma->vm_private_data;

	fb->map_count++;
	dprintk("vino_vm_open(): count = %d\n", fb->map_count);
}

static void vino_vm_close(struct vm_area_struct *vma)
{
	struct vino_framebuffer *fb = vma->vm_private_data;

	fb->map_count--;
	dprintk("vino_vm_close(): count = %d\n", fb->map_count);
}

static const struct vm_operations_struct vino_vm_ops = {
	.open	= vino_vm_open,
	.close	= vino_vm_close,
};

static int vino_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vino_channel_settings *vcs = video_drvdata(file);

	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	struct vino_framebuffer *fb = NULL;
	unsigned int i, length;
	int ret = 0;

	dprintk("mmap():\n");

	// TODO: reject mmap if already mapped

	if (mutex_lock_interruptible(&vcs->mutex))
		return -EINTR;

	if (vcs->reading) {
		ret = -EBUSY;
		goto out;
	}

	// TODO: check queue type

	if (!(vma->vm_flags & VM_WRITE)) {
		dprintk("mmap(): app bug: PROT_WRITE please\n");
		ret = -EINVAL;
		goto out;
	}
	if (!(vma->vm_flags & VM_SHARED)) {
		dprintk("mmap(): app bug: MAP_SHARED please\n");
		ret = -EINVAL;
		goto out;
	}

	/* find the correct buffer using offset */
	length = vino_queue_get_length(&vcs->fb_queue);
	if (length == 0) {
		dprintk("mmap(): queue not initialized\n");
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < length; i++) {
		fb = vino_queue_get_buffer(&vcs->fb_queue, i);
		if (fb == NULL) {
			dprintk("mmap(): vino_queue_get_buffer() failed\n");
			ret = -EINVAL;
			goto out;
		}

		if (fb->offset == offset)
			goto found;
	}

	dprintk("mmap(): invalid offset = %lu\n", offset);
	ret = -EINVAL;
	goto out;

found:
	dprintk("mmap(): buffer = %d\n", i);

	if (size > (fb->desc_table.page_count * PAGE_SIZE)) {
		dprintk("mmap(): failed: size = %lu > %lu\n",
			size, fb->desc_table.page_count * PAGE_SIZE);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < fb->desc_table.page_count; i++) {
		unsigned long pfn =
			virt_to_phys((void *)fb->desc_table.virtual[i]) >>
			PAGE_SHIFT;

		if (size < PAGE_SIZE)
			break;

		// protection was: PAGE_READONLY
		if (remap_pfn_range(vma, start, pfn, PAGE_SIZE,
				    vma->vm_page_prot)) {
			dprintk("mmap(): remap_pfn_range() failed\n");
			ret = -EAGAIN;
			goto out;
		}

		start += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	fb->map_count = 1;

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_flags &= ~VM_IO;
	vma->vm_private_data = fb;
	vma->vm_file = file;
	vma->vm_ops = &vino_vm_ops;

out:
	mutex_unlock(&vcs->mutex);

	return ret;
}

static unsigned int vino_poll(struct file *file, poll_table *pt)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	unsigned int outgoing;
	unsigned int ret = 0;

	// lock mutex (?)
	// TODO: this has to be corrected for different read modes

	dprintk("poll():\n");

	if (vino_queue_get_outgoing(&vcs->fb_queue, &outgoing)) {
		dprintk("poll(): vino_queue_get_outgoing() failed\n");
		ret = POLLERR;
		goto error;
	}
	if (outgoing > 0)
		goto over;

	poll_wait(file, &vcs->fb_queue.frame_wait_queue, pt);

	if (vino_queue_get_outgoing(&vcs->fb_queue, &outgoing)) {
		dprintk("poll(): vino_queue_get_outgoing() failed\n");
		ret = POLLERR;
		goto error;
	}

over:
	dprintk("poll(): data %savailable\n",
		(outgoing > 0) ? "" : "not ");

	if (outgoing > 0)
		ret = POLLIN | POLLRDNORM;

error:
	return ret;
}

static long vino_ioctl(struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct vino_channel_settings *vcs = video_drvdata(file);
	long ret;

	if (mutex_lock_interruptible(&vcs->mutex))
		return -EINTR;

	ret = video_ioctl2(file, cmd, arg);

	mutex_unlock(&vcs->mutex);

	return ret;
}

/* Initialization and cleanup */

/* __initdata */
static int vino_init_stage;

const struct v4l2_ioctl_ops vino_ioctl_ops = {
	.vidioc_enum_fmt_vid_cap     = vino_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap 	     = vino_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap  	     = vino_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	     = vino_try_fmt_vid_cap,
	.vidioc_querycap    	     = vino_querycap,
	.vidioc_enum_input   	     = vino_enum_input,
	.vidioc_g_input      	     = vino_g_input,
	.vidioc_s_input      	     = vino_s_input,
	.vidioc_g_std 		     = vino_g_std,
	.vidioc_s_std 		     = vino_s_std,
	.vidioc_querystd             = vino_querystd,
	.vidioc_cropcap      	     = vino_cropcap,
	.vidioc_s_crop       	     = vino_s_crop,
	.vidioc_g_crop       	     = vino_g_crop,
	.vidioc_s_parm 		     = vino_s_parm,
	.vidioc_g_parm 		     = vino_g_parm,
	.vidioc_reqbufs              = vino_reqbufs,
	.vidioc_querybuf             = vino_querybuf,
	.vidioc_qbuf                 = vino_qbuf,
	.vidioc_dqbuf                = vino_dqbuf,
	.vidioc_streamon             = vino_streamon,
	.vidioc_streamoff            = vino_streamoff,
	.vidioc_queryctrl            = vino_queryctrl,
	.vidioc_g_ctrl               = vino_g_ctrl,
	.vidioc_s_ctrl               = vino_s_ctrl,
};

static const struct v4l2_file_operations vino_fops = {
	.owner		= THIS_MODULE,
	.open		= vino_open,
	.release	= vino_close,
	.unlocked_ioctl	= vino_ioctl,
	.mmap		= vino_mmap,
	.poll		= vino_poll,
};

static struct video_device vdev_template = {
	.name		= "NOT SET",
	.fops		= &vino_fops,
	.ioctl_ops 	= &vino_ioctl_ops,
	.tvnorms 	= V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
};

static void vino_module_cleanup(int stage)
{
	switch(stage) {
	case 11:
		video_unregister_device(vino_drvdata->b.vdev);
		vino_drvdata->b.vdev = NULL;
	case 10:
		video_unregister_device(vino_drvdata->a.vdev);
		vino_drvdata->a.vdev = NULL;
	case 9:
		i2c_del_adapter(&vino_i2c_adapter);
	case 8:
		free_irq(SGI_VINO_IRQ, NULL);
	case 7:
		if (vino_drvdata->b.vdev) {
			video_device_release(vino_drvdata->b.vdev);
			vino_drvdata->b.vdev = NULL;
		}
	case 6:
		if (vino_drvdata->a.vdev) {
			video_device_release(vino_drvdata->a.vdev);
			vino_drvdata->a.vdev = NULL;
		}
	case 5:
		/* all entries in dma_cpu dummy table have the same address */
		dma_unmap_single(NULL,
				 vino_drvdata->dummy_desc_table.dma_cpu[0],
				 PAGE_SIZE, DMA_FROM_DEVICE);
		dma_free_coherent(NULL, VINO_DUMMY_DESC_COUNT
				  * sizeof(dma_addr_t),
				  (void *)vino_drvdata->
				  dummy_desc_table.dma_cpu,
				  vino_drvdata->dummy_desc_table.dma);
	case 4:
		free_page(vino_drvdata->dummy_page);
	case 3:
		v4l2_device_unregister(&vino_drvdata->v4l2_dev);
	case 2:
		kfree(vino_drvdata);
	case 1:
		iounmap(vino);
	case 0:
		break;
	default:
		dprintk("vino_module_cleanup(): invalid cleanup stage = %d\n",
			stage);
	}
}

static int vino_probe(void)
{
	unsigned long rev_id;

	if (ip22_is_fullhouse()) {
		printk(KERN_ERR "VINO doesn't exist in IP22 Fullhouse\n");
		return -ENODEV;
	}

	if (!(sgimc->systemid & SGIMC_SYSID_EPRESENT)) {
		printk(KERN_ERR "VINO is not found (EISA BUS not present)\n");
		return -ENODEV;
	}

	vino = (struct sgi_vino *)ioremap(VINO_BASE, sizeof(struct sgi_vino));
	if (!vino) {
		printk(KERN_ERR "VINO: ioremap() failed\n");
		return -EIO;
	}
	vino_init_stage++;

	if (get_dbe(rev_id, &(vino->rev_id))) {
		printk(KERN_ERR "Failed to read VINO revision register\n");
		vino_module_cleanup(vino_init_stage);
		return -ENODEV;
	}

	if (VINO_ID_VALUE(rev_id) != VINO_CHIP_ID) {
		printk(KERN_ERR "Unknown VINO chip ID (Rev/ID: 0x%02lx)\n",
		       rev_id);
		vino_module_cleanup(vino_init_stage);
		return -ENODEV;
	}

	printk(KERN_INFO "VINO revision %ld found\n", VINO_REV_NUM(rev_id));

	return 0;
}

static int vino_init(void)
{
	dma_addr_t dma_dummy_address;
	int err;
	int i;

	vino_drvdata = kzalloc(sizeof(struct vino_settings), GFP_KERNEL);
	if (!vino_drvdata) {
		vino_module_cleanup(vino_init_stage);
		return -ENOMEM;
	}
	vino_init_stage++;
	strlcpy(vino_drvdata->v4l2_dev.name, "vino",
			sizeof(vino_drvdata->v4l2_dev.name));
	err = v4l2_device_register(NULL, &vino_drvdata->v4l2_dev);
	if (err)
		return err;
	vino_init_stage++;

	/* create a dummy dma descriptor */
	vino_drvdata->dummy_page = get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!vino_drvdata->dummy_page) {
		vino_module_cleanup(vino_init_stage);
		return -ENOMEM;
	}
	vino_init_stage++;

	// TODO: use page_count in dummy_desc_table

	vino_drvdata->dummy_desc_table.dma_cpu =
		dma_alloc_coherent(NULL,
		VINO_DUMMY_DESC_COUNT * sizeof(dma_addr_t),
		&vino_drvdata->dummy_desc_table.dma,
		GFP_KERNEL | GFP_DMA);
	if (!vino_drvdata->dummy_desc_table.dma_cpu) {
		vino_module_cleanup(vino_init_stage);
		return -ENOMEM;
	}
	vino_init_stage++;

	dma_dummy_address = dma_map_single(NULL,
					   (void *)vino_drvdata->dummy_page,
					PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < VINO_DUMMY_DESC_COUNT; i++) {
		vino_drvdata->dummy_desc_table.dma_cpu[i] = dma_dummy_address;
	}

	/* initialize VINO */

	vino->control = 0;
	vino->a.next_4_desc = vino_drvdata->dummy_desc_table.dma;
	vino->b.next_4_desc = vino_drvdata->dummy_desc_table.dma;
	udelay(VINO_DESC_FETCH_DELAY);

	vino->intr_status = 0;

	vino->a.fifo_thres = VINO_FIFO_THRESHOLD_DEFAULT;
	vino->b.fifo_thres = VINO_FIFO_THRESHOLD_DEFAULT;

	return 0;
}

static int vino_init_channel_settings(struct vino_channel_settings *vcs,
				 unsigned int channel, const char *name)
{
	vcs->channel = channel;
	vcs->input = VINO_INPUT_NONE;
	vcs->alpha = 0;
	vcs->users = 0;
	vcs->data_format = VINO_DATA_FMT_GREY;
	vcs->data_norm = VINO_DATA_NORM_NTSC;
	vcs->decimation = 1;
	vino_set_default_clipping(vcs);
	vino_set_default_framerate(vcs);

	vcs->capturing = 0;

	mutex_init(&vcs->mutex);
	spin_lock_init(&vcs->capture_lock);

	mutex_init(&vcs->fb_queue.queue_mutex);
	spin_lock_init(&vcs->fb_queue.queue_lock);
	init_waitqueue_head(&vcs->fb_queue.frame_wait_queue);

	vcs->vdev = video_device_alloc();
	if (!vcs->vdev) {
		vino_module_cleanup(vino_init_stage);
		return -ENOMEM;
	}
	vino_init_stage++;

	memcpy(vcs->vdev, &vdev_template,
	       sizeof(struct video_device));
	strcpy(vcs->vdev->name, name);
	vcs->vdev->release = video_device_release;
	vcs->vdev->v4l2_dev = &vino_drvdata->v4l2_dev;

	video_set_drvdata(vcs->vdev, vcs);

	return 0;
}

static int __init vino_module_init(void)
{
	int ret;

	printk(KERN_INFO "SGI VINO driver version %s\n",
	       VINO_MODULE_VERSION);

	ret = vino_probe();
	if (ret)
		return ret;

	ret = vino_init();
	if (ret)
		return ret;

	/* initialize data structures */

	spin_lock_init(&vino_drvdata->vino_lock);
	spin_lock_init(&vino_drvdata->input_lock);

	ret = vino_init_channel_settings(&vino_drvdata->a, VINO_CHANNEL_A,
				    vino_vdev_name_a);
	if (ret)
		return ret;

	ret = vino_init_channel_settings(&vino_drvdata->b, VINO_CHANNEL_B,
				    vino_vdev_name_b);
	if (ret)
		return ret;

	/* initialize hardware and register V4L devices */

	ret = request_irq(SGI_VINO_IRQ, vino_interrupt, 0,
		vino_driver_description, NULL);
	if (ret) {
		printk(KERN_ERR "VINO: requesting IRQ %02d failed\n",
		       SGI_VINO_IRQ);
		vino_module_cleanup(vino_init_stage);
		return -EAGAIN;
	}
	vino_init_stage++;

	ret = i2c_add_adapter(&vino_i2c_adapter);
	if (ret) {
		printk(KERN_ERR "VINO I2C bus registration failed\n");
		vino_module_cleanup(vino_init_stage);
		return ret;
	}
	i2c_set_adapdata(&vino_i2c_adapter, &vino_drvdata->v4l2_dev);
	vino_init_stage++;

	ret = video_register_device(vino_drvdata->a.vdev,
				    VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		printk(KERN_ERR "VINO channel A Video4Linux-device "
		       "registration failed\n");
		vino_module_cleanup(vino_init_stage);
		return -EINVAL;
	}
	vino_init_stage++;

	ret = video_register_device(vino_drvdata->b.vdev,
				    VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		printk(KERN_ERR "VINO channel B Video4Linux-device "
		       "registration failed\n");
		vino_module_cleanup(vino_init_stage);
		return -EINVAL;
	}
	vino_init_stage++;

	vino_drvdata->decoder =
		v4l2_i2c_new_subdev(&vino_drvdata->v4l2_dev, &vino_i2c_adapter,
			       "saa7191", 0, I2C_ADDRS(0x45));
	vino_drvdata->camera =
		v4l2_i2c_new_subdev(&vino_drvdata->v4l2_dev, &vino_i2c_adapter,
			       "indycam", 0, I2C_ADDRS(0x2b));

	dprintk("init complete!\n");

	return 0;
}

static void __exit vino_module_exit(void)
{
	dprintk("exiting, stage = %d ...\n", vino_init_stage);
	vino_module_cleanup(vino_init_stage);
	dprintk("cleanup complete, exit!\n");
}

module_init(vino_module_init);
module_exit(vino_module_exit);
