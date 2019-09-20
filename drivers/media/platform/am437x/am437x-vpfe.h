/*
 * Copyright (C) 2013 - 2014 Texas Instruments, Inc.
 *
 * Benoit Parrot <bparrot@ti.com>
 * Lad, Prabhakar <prabhakar.csengg@gmail.com>
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef AM437X_VPFE_H
#define AM437X_VPFE_H

#include <linux/am437x-vpfe.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "am437x-vpfe_regs.h"

enum vpfe_pin_pol {
	VPFE_PINPOL_POSITIVE = 0,
	VPFE_PINPOL_NEGATIVE,
};

enum vpfe_hw_if_type {
	/* Raw Bayer */
	VPFE_RAW_BAYER = 0,
	/* BT656 - 8 bit */
	VPFE_BT656,
	/* BT656 - 10 bit */
	VPFE_BT656_10BIT,
	/* YCbCr - 8 bit with external sync */
	VPFE_YCBCR_SYNC_8,
	/* YCbCr - 16 bit with external sync */
	VPFE_YCBCR_SYNC_16,
};

/* interface description */
struct vpfe_hw_if_param {
	enum vpfe_hw_if_type if_type;
	enum vpfe_pin_pol hdpol;
	enum vpfe_pin_pol vdpol;
	unsigned int bus_width;
};

#define VPFE_MAX_SUBDEV		1
#define VPFE_MAX_INPUTS		1

struct vpfe_std_info {
	int active_pixels;
	int active_lines;
	/* current frame format */
	int frame_format;
};

struct vpfe_route {
	u32 input;
	u32 output;
};

struct vpfe_subdev_info {
	/* Sub device group id */
	int grp_id;
	/* inputs available at the sub device */
	struct v4l2_input inputs[VPFE_MAX_INPUTS];
	/* Sub dev routing information for each input */
	struct vpfe_route *routes;
	/* check if sub dev supports routing */
	int can_route;
	/* ccdc bus/interface configuration */
	struct vpfe_hw_if_param vpfe_param;
	struct v4l2_subdev *sd;
};

struct vpfe_config {
	/* information about each subdev */
	struct vpfe_subdev_info sub_devs[VPFE_MAX_SUBDEV];
	/* Flat array, arranged in groups */
	struct v4l2_async_subdev *asd[VPFE_MAX_SUBDEV];
};

struct vpfe_cap_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

enum ccdc_pixfmt {
	CCDC_PIXFMT_RAW = 0,
	CCDC_PIXFMT_YCBCR_16BIT,
	CCDC_PIXFMT_YCBCR_8BIT,
};

enum ccdc_frmfmt {
	CCDC_FRMFMT_PROGRESSIVE = 0,
	CCDC_FRMFMT_INTERLACED,
};

/* PIXEL ORDER IN MEMORY from LSB to MSB */
/* only applicable for 8-bit input mode  */
enum ccdc_pixorder {
	CCDC_PIXORDER_YCBYCR,
	CCDC_PIXORDER_CBYCRY,
};

enum ccdc_buftype {
	CCDC_BUFTYPE_FLD_INTERLEAVED,
	CCDC_BUFTYPE_FLD_SEPARATED
};


/* returns the highest bit used for the gamma */
static inline u8 ccdc_gamma_width_max_bit(enum vpfe_ccdc_gamma_width width)
{
	return 15 - width;
}

/* returns the highest bit used for this data size */
static inline u8 ccdc_data_size_max_bit(enum vpfe_ccdc_data_size sz)
{
	return sz == VPFE_CCDC_DATA_8BITS ? 7 : 15 - sz;
}

/* Structure for CCDC configuration parameters for raw capture mode */
struct ccdc_params_raw {
	/* pixel format */
	enum ccdc_pixfmt pix_fmt;
	/* progressive or interlaced frame */
	enum ccdc_frmfmt frm_fmt;
	struct v4l2_rect win;
	/* Current Format Bytes Per Pixels */
	unsigned int bytesperpixel;
	/* Current Format Bytes per Lines
	 * (Aligned to 32 bytes) used for HORZ_INFO
	 */
	unsigned int bytesperline;
	/* field id polarity */
	enum vpfe_pin_pol fid_pol;
	/* vertical sync polarity */
	enum vpfe_pin_pol vd_pol;
	/* horizontal sync polarity */
	enum vpfe_pin_pol hd_pol;
	/* interleaved or separated fields */
	enum ccdc_buftype buf_type;
	/*
	 * enable to store the image in inverse
	 * order in memory(bottom to top)
	 */
	unsigned char image_invert_enable;
	/* configurable parameters */
	struct vpfe_ccdc_config_params_raw config_params;
};

struct ccdc_params_ycbcr {
	/* pixel format */
	enum ccdc_pixfmt pix_fmt;
	/* progressive or interlaced frame */
	enum ccdc_frmfmt frm_fmt;
	struct v4l2_rect win;
	/* Current Format Bytes Per Pixels */
	unsigned int bytesperpixel;
	/* Current Format Bytes per Lines
	 * (Aligned to 32 bytes) used for HORZ_INFO
	 */
	unsigned int bytesperline;
	/* field id polarity */
	enum vpfe_pin_pol fid_pol;
	/* vertical sync polarity */
	enum vpfe_pin_pol vd_pol;
	/* horizontal sync polarity */
	enum vpfe_pin_pol hd_pol;
	/* enable BT.656 embedded sync mode */
	int bt656_enable;
	/* cb:y:cr:y or y:cb:y:cr in memory */
	enum ccdc_pixorder pix_order;
	/* interleaved or separated fields  */
	enum ccdc_buftype buf_type;
};

/*
 * CCDC operational configuration
 */
struct ccdc_config {
	/* CCDC interface type */
	enum vpfe_hw_if_type if_type;
	/* Raw Bayer configuration */
	struct ccdc_params_raw bayer;
	/* YCbCr configuration */
	struct ccdc_params_ycbcr ycbcr;
	/* ccdc base address */
	void __iomem *base_addr;
};

struct vpfe_ccdc {
	struct ccdc_config ccdc_cfg;
	u32 ccdc_ctx[VPFE_REG_END / sizeof(u32)];
};

/*
 * struct bus_format - VPFE bus format information
 * width: Bits per pixel (when transferred over a bus)
 * bpp: Bytes per pixel (when stored in memory)
 */
struct bus_format {
	unsigned int width;
	unsigned int bpp;
};

/*
 * struct vpfe_fmt - VPFE media bus format information
 * fourcc: V4L2 pixel format code
 * code: V4L2 media bus format code
 * l: 10 bit bus format info
 * s: 8 bit bus format info
 */
struct vpfe_fmt {
	u32 fourcc;
	u32 code;
	struct bus_format l;
	struct bus_format s;
};

/*
 * When formats[] is modified make sure to adjust this value also.
 * Expect compile time warnings if VPFE_NUM_FORMATS is smaller then
 * the number of elements in formats[].
 */
#define VPFE_NUM_FORMATS	10

struct vpfe_device {
	/* V4l2 specific parameters */
	/* Identifies video device for this channel */
	struct video_device video_dev;
	/* sub devices */
	struct v4l2_subdev **sd;
	/* vpfe cfg */
	struct vpfe_config *cfg;
	/* V4l2 device */
	struct v4l2_device v4l2_dev;
	/* parent device */
	struct device *pdev;
	/* subdevice async Notifier */
	struct v4l2_async_notifier notifier;
	/* Indicates id of the field which is being displayed */
	unsigned field;
	unsigned sequence;
	/* current interface type */
	struct vpfe_hw_if_param vpfe_if_params;
	/* ptr to currently selected sub device */
	struct vpfe_subdev_info *current_subdev;
	/* current input at the sub device */
	int current_input;
	/* Keeps track of the information about the standard */
	struct vpfe_std_info std_info;
	/* std index into std table */
	int std_index;
	/* IRQs used when CCDC output to SDRAM */
	unsigned int irq;
	/* Pointer pointing to current v4l2_buffer */
	struct vpfe_cap_buffer *cur_frm;
	/* Pointer pointing to next v4l2_buffer */
	struct vpfe_cap_buffer *next_frm;
	/* Used to store pixel format */
	struct v4l2_format fmt;
	/* Used to keep a reference to the current vpfe_fmt */
	struct vpfe_fmt *current_vpfe_fmt;
	struct vpfe_fmt	*active_fmt[VPFE_NUM_FORMATS];
	unsigned int num_active_fmt;

	/*
	 * used when IMP is chained to store the crop window which
	 * is different from the image window
	 */
	struct v4l2_rect crop;
	/* Buffer queue used in video-buf */
	struct vb2_queue buffer_queue;
	/* Queue of filled frames */
	struct list_head dma_queue;
	/* IRQ lock for DMA queue */
	spinlock_t dma_queue_lock;
	/* lock used to access this structure */
	struct mutex lock;
	/*
	 * offset where second field starts from the starting of the
	 * buffer for field separated YCbCr formats
	 */
	u32 field_off;
	struct vpfe_ccdc ccdc;
	int stopping;
	struct completion capture_stop;
};

#endif	/* AM437X_VPFE_H */
