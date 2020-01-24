/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * s3c24xx/s3c64xx SoC series Camera Interface (CAMIF) driver
 *
 * Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
 * Copyright (C) 2012 Tomasz Figa <tomasz.figa@gmail.com>
*/

#ifndef CAMIF_CORE_H_
#define CAMIF_CORE_H_

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/videobuf2-v4l2.h>
#include <media/drv-intf/s3c_camif.h>

#define S3C_CAMIF_DRIVER_NAME	"s3c-camif"
#define CAMIF_REQ_BUFS_MIN	3
#define CAMIF_MAX_OUT_BUFS	4
#define CAMIF_MAX_PIX_WIDTH	4096
#define CAMIF_MAX_PIX_HEIGHT	4096
#define SCALER_MAX_RATIO	64
#define CAMIF_DEF_WIDTH		640
#define CAMIF_DEF_HEIGHT	480
#define CAMIF_STOP_TIMEOUT	1500 /* ms */

#define S3C244X_CAMIF_IP_REV	0x20 /* 2.0 */
#define S3C2450_CAMIF_IP_REV	0x30 /* 3.0 - not implemented, not tested */
#define S3C6400_CAMIF_IP_REV	0x31 /* 3.1 - not implemented, not tested */
#define S3C6410_CAMIF_IP_REV	0x32 /* 3.2 */

/* struct camif_vp::state */

#define ST_VP_PENDING		(1 << 0)
#define ST_VP_RUNNING		(1 << 1)
#define ST_VP_STREAMING		(1 << 2)
#define ST_VP_SENSOR_STREAMING	(1 << 3)

#define ST_VP_ABORTING		(1 << 4)
#define ST_VP_OFF		(1 << 5)
#define ST_VP_LASTIRQ		(1 << 6)

#define ST_VP_CONFIG		(1 << 8)

#define CAMIF_SD_PAD_SINK	0
#define CAMIF_SD_PAD_SOURCE_C	1
#define CAMIF_SD_PAD_SOURCE_P	2
#define CAMIF_SD_PADS_NUM	3

enum img_fmt {
	IMG_FMT_RGB565 = 0x0010,
	IMG_FMT_RGB666,
	IMG_FMT_XRGB8888,
	IMG_FMT_YCBCR420 = 0x0020,
	IMG_FMT_YCRCB420,
	IMG_FMT_YCBCR422P,
	IMG_FMT_YCBYCR422 = 0x0040,
	IMG_FMT_YCRYCB422,
	IMG_FMT_CBYCRY422,
	IMG_FMT_CRYCBY422,
};

#define img_fmt_is_rgb(x) ((x) & 0x10)
#define img_fmt_is_ycbcr(x) ((x) & 0x60)

/* Possible values for struct camif_fmt::flags */
#define FMT_FL_S3C24XX_CODEC	(1 << 0)
#define FMT_FL_S3C24XX_PREVIEW	(1 << 1)
#define FMT_FL_S3C64XX		(1 << 2)

/**
 * struct camif_fmt - pixel format description
 * @fourcc:    fourcc code for this format, 0 if not applicable
 * @color:     a corresponding enum img_fmt
 * @colplanes: number of physically contiguous data planes
 * @flags:     indicate for which SoCs revisions this format is valid
 * @depth:     bits per pixel (total)
 * @ybpp:      number of luminance bytes per pixel
 */
struct camif_fmt {
	u32 fourcc;
	u32 color;
	u16 colplanes;
	u16 flags;
	u8 depth;
	u8 ybpp;
};

/**
 * struct camif_dma_offset - pixel offset information for DMA
 * @initial: offset (in pixels) to first pixel
 * @line: offset (in pixels) from end of line to start of next line
 */
struct camif_dma_offset {
	int	initial;
	int	line;
};

/**
 * struct camif_frame - source/target frame properties
 * @f_width: full pixel width
 * @f_height: full pixel height
 * @rect: crop/composition rectangle
 * @dma_offset: DMA offset configuration
 */
struct camif_frame {
	u16 f_width;
	u16 f_height;
	struct v4l2_rect rect;
	struct camif_dma_offset dma_offset;
};

/* CAMIF clocks enumeration */
enum {
	CLK_GATE,
	CLK_CAM,
	CLK_MAX_NUM,
};

struct vp_pix_limits {
	u16 max_out_width;
	u16 max_sc_out_width;
	u16 out_width_align;
	u16 max_height;
	u8 min_out_width;
	u16 out_hor_offset_align;
};

struct camif_pix_limits {
	u16 win_hor_offset_align;
};

/**
 * struct s3c_camif_variant - CAMIF variant structure
 * @vp_pix_limits:    pixel limits for the codec and preview paths
 * @camif_pix_limits: pixel limits for the camera input interface
 * @ip_revision:      the CAMIF IP revision: 0x20 for s3c244x, 0x32 for s3c6410
 */
struct s3c_camif_variant {
	struct vp_pix_limits vp_pix_limits[2];
	struct camif_pix_limits pix_limits;
	u8 ip_revision;
	u8 has_img_effect;
	unsigned int vp_offset;
};

struct s3c_camif_drvdata {
	const struct s3c_camif_variant *variant;
	unsigned long bus_clk_freq;
};

struct camif_scaler {
	u8 scaleup_h;
	u8 scaleup_v;
	u8 copy;
	u8 enable;
	u32 h_shift;
	u32 v_shift;
	u32 pre_h_ratio;
	u32 pre_v_ratio;
	u32 pre_dst_width;
	u32 pre_dst_height;
	u32 main_h_ratio;
	u32 main_v_ratio;
};

struct camif_dev;

/**
 * struct camif_vp - CAMIF data processing path structure (codec/preview)
 * @irq_queue:	    interrupt handling waitqueue
 * @irq:	    interrupt number for this data path
 * @camif:	    pointer to the camif structure
 * @pad:	    media pad for the video node
 * @vdev            video device
 * @ctrl_handler:   video node controls handler
 * @owner:	    file handle that own the streaming
 * @pending_buf_q:  pending (empty) buffers queue head
 * @active_buf_q:   active (being written) buffers queue head
 * @active_buffers: counter of buffer set up at the DMA engine
 * @buf_index:	    identifier of a last empty buffer set up in H/W
 * @frame_sequence: image frame sequence counter
 * @reqbufs_count:  the number of buffers requested
 * @scaler:	    the scaler structure
 * @out_fmt:	    pixel format at this video path output
 * @payload:	    the output data frame payload size
 * @out_frame:	    the output pixel resolution
 * @state:	    the video path's state
 * @fmt_flags:	    flags determining supported pixel formats
 * @id:		    CAMIF id, 0 - codec, 1 - preview
 * @rotation:	    current image rotation value
 * @hflip:	    apply horizontal flip if set
 * @vflip:	    apply vertical flip if set
 */
struct camif_vp {
	wait_queue_head_t	irq_queue;
	int			irq;
	struct camif_dev	*camif;
	struct media_pad	pad;
	struct video_device	vdev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_fh		*owner;
	struct vb2_queue	vb_queue;
	struct list_head	pending_buf_q;
	struct list_head	active_buf_q;
	unsigned int		active_buffers;
	unsigned int		buf_index;
	unsigned int		frame_sequence;
	unsigned int		reqbufs_count;
	struct camif_scaler	scaler;
	const struct camif_fmt	*out_fmt;
	unsigned int		payload;
	struct camif_frame	out_frame;
	unsigned int		state;
	u16			fmt_flags;
	u8			id;
	u16			rotation;
	u8			hflip;
	u8			vflip;
	unsigned int		offset;
};

/* Video processing path enumeration */
#define VP_CODEC	0
#define VP_PREVIEW	1
#define CAMIF_VP_NUM	2

/**
 * struct camif_dev - the CAMIF driver private data structure
 * @media_dev:    top-level media device structure
 * @v4l2_dev:	  root v4l2_device
 * @subdev:       camera interface ("catchcam") subdev
 * @mbus_fmt:	  camera input media bus format
 * @camif_crop:   camera input interface crop rectangle
 * @pads:	  the camif subdev's media pads
 * @stream_count: the camera interface streaming reference counter
 * @sensor:       image sensor data structure
 * @m_pipeline:	  video entity pipeline description
 * @ctrl_handler: v4l2 control handler (owned by @subdev)
 * @test_pattern: test pattern controls
 * @vp:           video path (DMA) description (codec/preview)
 * @variant:      variant information for this device
 * @dev:	  pointer to the CAMIF device struct
 * @pdata:	  a copy of the driver's platform data
 * @clock:	  clocks required for the CAMIF operation
 * @lock:	  mutex protecting this data structure
 * @slock:	  spinlock protecting CAMIF registers
 * @io_base:	  start address of the mmapped CAMIF registers
 */
struct camif_dev {
	struct media_device		media_dev;
	struct v4l2_device		v4l2_dev;
	struct v4l2_subdev		subdev;
	struct v4l2_mbus_framefmt	mbus_fmt;
	struct v4l2_rect		camif_crop;
	struct media_pad		pads[CAMIF_SD_PADS_NUM];
	int				stream_count;

	struct cam_sensor {
		struct v4l2_subdev	*sd;
		short			power_count;
		short			stream_count;
	} sensor;
	struct media_pipeline		*m_pipeline;

	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_ctrl		*ctrl_test_pattern;
	struct {
		struct v4l2_ctrl	*ctrl_colorfx;
		struct v4l2_ctrl	*ctrl_colorfx_cbcr;
	};
	u8				test_pattern;
	u8				colorfx;
	u8				colorfx_cb;
	u8				colorfx_cr;

	struct camif_vp			vp[CAMIF_VP_NUM];

	const struct s3c_camif_variant	*variant;
	struct device			*dev;
	struct s3c_camif_plat_data	pdata;
	struct clk			*clock[CLK_MAX_NUM];
	struct mutex			lock;
	spinlock_t			slock;
	void __iomem			*io_base;
};

/**
 * struct camif_addr - Y/Cb/Cr DMA start address structure
 * @y:	 luminance plane dma address
 * @cb:	 Cb plane dma address
 * @cr:	 Cr plane dma address
 */
struct camif_addr {
	dma_addr_t y;
	dma_addr_t cb;
	dma_addr_t cr;
};

/**
 * struct camif_buffer - the camif video buffer structure
 * @vb:    vb2 buffer
 * @list:  list head for the buffers queue
 * @paddr: DMA start addresses
 * @index: an identifier of this buffer at the DMA engine
 */
struct camif_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
	struct camif_addr paddr;
	unsigned int index;
};

const struct camif_fmt *s3c_camif_find_format(struct camif_vp *vp,
	      const u32 *pixelformat, int index);
int s3c_camif_register_video_node(struct camif_dev *camif, int idx);
void s3c_camif_unregister_video_node(struct camif_dev *camif, int idx);
irqreturn_t s3c_camif_irq_handler(int irq, void *priv);
int s3c_camif_create_subdev(struct camif_dev *camif);
void s3c_camif_unregister_subdev(struct camif_dev *camif);
int s3c_camif_set_defaults(struct camif_dev *camif);
int s3c_camif_get_scaler_config(struct camif_vp *vp,
				struct camif_scaler *scaler);

static inline void camif_active_queue_add(struct camif_vp *vp,
					  struct camif_buffer *buf)
{
	list_add_tail(&buf->list, &vp->active_buf_q);
	vp->active_buffers++;
}

static inline struct camif_buffer *camif_active_queue_pop(
					struct camif_vp *vp)
{
	struct camif_buffer *buf = list_first_entry(&vp->active_buf_q,
					      struct camif_buffer, list);
	list_del(&buf->list);
	vp->active_buffers--;
	return buf;
}

static inline struct camif_buffer *camif_active_queue_peek(
			   struct camif_vp *vp, int index)
{
	struct camif_buffer *tmp, *buf;

	if (WARN_ON(list_empty(&vp->active_buf_q)))
		return NULL;

	list_for_each_entry_safe(buf, tmp, &vp->active_buf_q, list) {
		if (buf->index == index) {
			list_del(&buf->list);
			vp->active_buffers--;
			return buf;
		}
	}

	return NULL;
}

static inline void camif_pending_queue_add(struct camif_vp *vp,
					   struct camif_buffer *buf)
{
	list_add_tail(&buf->list, &vp->pending_buf_q);
}

static inline struct camif_buffer *camif_pending_queue_pop(
					struct camif_vp *vp)
{
	struct camif_buffer *buf = list_first_entry(&vp->pending_buf_q,
					      struct camif_buffer, list);
	list_del(&buf->list);
	return buf;
}

#endif /* CAMIF_CORE_H_ */
