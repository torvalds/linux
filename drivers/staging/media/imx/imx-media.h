/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * V4L2 Media Controller Driver for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 */
#ifndef _IMX_MEDIA_H
#define _IMX_MEDIA_H

#include <linux/platform_device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <video/imx-ipu-v3.h>

#define IMX_MEDIA_DEF_PIX_WIDTH		640
#define IMX_MEDIA_DEF_PIX_HEIGHT	480

/*
 * Enumeration of the IPU internal sub-devices
 */
enum {
	IPU_CSI0 = 0,
	IPU_CSI1,
	IPU_VDIC,
	IPU_IC_PRP,
	IPU_IC_PRPENC,
	IPU_IC_PRPVF,
	NUM_IPU_SUBDEVS,
};

/*
 * Pad definitions for the subdevs with multiple source or
 * sink pads
 */

/* ipu_csi */
enum {
	CSI_SINK_PAD = 0,
	CSI_SRC_PAD_DIRECT,
	CSI_SRC_PAD_IDMAC,
	CSI_NUM_PADS,
};

/* ipu_vdic */
enum {
	VDIC_SINK_PAD_DIRECT = 0,
	VDIC_SINK_PAD_IDMAC,
	VDIC_SRC_PAD_DIRECT,
	VDIC_NUM_PADS,
};

/* ipu_ic_prp */
enum {
	PRP_SINK_PAD = 0,
	PRP_SRC_PAD_PRPENC,
	PRP_SRC_PAD_PRPVF,
	PRP_NUM_PADS,
};

/* ipu_ic_prpencvf */
enum {
	PRPENCVF_SINK_PAD = 0,
	PRPENCVF_SRC_PAD,
	PRPENCVF_NUM_PADS,
};

/* How long to wait for EOF interrupts in the buffer-capture subdevs */
#define IMX_MEDIA_EOF_TIMEOUT       2000

struct imx_media_pixfmt {
	/* the in-memory FourCC pixel format */
	u32     fourcc;
	/*
	 * the set of equivalent media bus codes for the fourcc.
	 * NOTE! codes pointer is NULL for in-memory-only formats.
	 */
	const u32 *codes;
	int     bpp;     /* total bpp */
	/* cycles per pixel for generic (bayer) formats for the parallel bus */
	int	cycles;
	enum ipu_color_space cs;
	bool    planar;  /* is a planar format */
	bool    bayer;   /* is a raw bayer format */
	bool    ipufmt;  /* is one of the IPU internal formats */
};

enum imx_pixfmt_sel {
	PIXFMT_SEL_YUV   = BIT(0), /* select YUV formats */
	PIXFMT_SEL_RGB   = BIT(1), /* select RGB formats */
	PIXFMT_SEL_BAYER = BIT(2), /* select BAYER formats */
	PIXFMT_SEL_IPU   = BIT(3), /* select IPU-internal formats */
	PIXFMT_SEL_YUV_RGB = PIXFMT_SEL_YUV | PIXFMT_SEL_RGB,
	PIXFMT_SEL_ANY = PIXFMT_SEL_YUV | PIXFMT_SEL_RGB | PIXFMT_SEL_BAYER,
};

struct imx_media_buffer {
	struct vb2_v4l2_buffer vbuf; /* v4l buffer must be first */
	struct list_head  list;
};

struct imx_media_video_dev {
	struct video_device *vfd;

	/* the user format */
	struct v4l2_pix_format fmt;
	/* the compose rectangle */
	struct v4l2_rect compose;
	const struct imx_media_pixfmt *cc;

	/* links this vdev to master list */
	struct list_head list;
};

static inline struct imx_media_buffer *to_imx_media_vb(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	return container_of(vbuf, struct imx_media_buffer, vbuf);
}

/*
 * to support control inheritance to video devices, this
 * retrieves a pad's list_head of video devices that can
 * be reached from the pad. Note that only the lists in
 * source pads get populated, sink pads have empty lists.
 */
static inline struct list_head *
to_pad_vdev_list(struct v4l2_subdev *sd, int pad_index)
{
	struct list_head *vdev_list = sd->host_priv;

	return vdev_list ? &vdev_list[pad_index] : NULL;
}

/* an entry in a pad's video device list */
struct imx_media_pad_vdev {
	struct imx_media_video_dev *vdev;
	struct list_head list;
};

struct imx_media_dev {
	struct media_device md;
	struct v4l2_device  v4l2_dev;

	/* the pipeline object */
	struct media_pipeline pipe;

	struct mutex mutex; /* protect elements below */

	/* master video device list */
	struct list_head vdev_list;

	/* IPUs this media driver control, valid after subdevs bound */
	struct ipu_soc *ipu[2];

	/* for async subdev registration */
	struct v4l2_async_notifier notifier;

	/* IC scaler/CSC mem2mem video device */
	struct imx_media_video_dev *m2m_vdev;

	/* the IPU internal subdev's registered synchronously */
	struct v4l2_subdev *sync_sd[2][NUM_IPU_SUBDEVS];
};

/* imx-media-utils.c */
const struct imx_media_pixfmt *
imx_media_find_pixel_format(u32 fourcc, enum imx_pixfmt_sel sel);
int imx_media_enum_pixel_formats(u32 *fourcc, u32 index,
				 enum imx_pixfmt_sel sel, u32 code);
const struct imx_media_pixfmt *
imx_media_find_mbus_format(u32 code, enum imx_pixfmt_sel sel);
int imx_media_enum_mbus_formats(u32 *code, u32 index,
				enum imx_pixfmt_sel sel);

static inline const struct imx_media_pixfmt *
imx_media_find_ipu_format(u32 code, enum imx_pixfmt_sel fmt_sel)
{
	return imx_media_find_mbus_format(code, fmt_sel | PIXFMT_SEL_IPU);
}

static inline int imx_media_enum_ipu_formats(u32 *code, u32 index,
					     enum imx_pixfmt_sel fmt_sel)
{
	return imx_media_enum_mbus_formats(code, index,
					  fmt_sel | PIXFMT_SEL_IPU);
}

int imx_media_init_mbus_fmt(struct v4l2_mbus_framefmt *mbus,
			    u32 width, u32 height, u32 code, u32 field,
			    const struct imx_media_pixfmt **cc);
int imx_media_init_cfg(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state);
void imx_media_try_colorimetry(struct v4l2_mbus_framefmt *tryfmt,
			       bool ic_route);
int imx_media_mbus_fmt_to_pix_fmt(struct v4l2_pix_format *pix,
				  const struct v4l2_mbus_framefmt *mbus,
				  const struct imx_media_pixfmt *cc);
void imx_media_grp_id_to_sd_name(char *sd_name, int sz,
				 u32 grp_id, int ipu_id);
struct v4l2_subdev *
imx_media_find_subdev_by_fwnode(struct imx_media_dev *imxmd,
				struct fwnode_handle *fwnode);
struct v4l2_subdev *
imx_media_find_subdev_by_devname(struct imx_media_dev *imxmd,
				 const char *devname);
void imx_media_add_video_device(struct imx_media_dev *imxmd,
				struct imx_media_video_dev *vdev);
int imx_media_pipeline_csi2_channel(struct media_entity *start_entity);
struct media_pad *
imx_media_pipeline_pad(struct media_entity *start_entity, u32 grp_id,
		       enum v4l2_buf_type buftype, bool upstream);
struct v4l2_subdev *
imx_media_pipeline_subdev(struct media_entity *start_entity, u32 grp_id,
			  bool upstream);
struct video_device *
imx_media_pipeline_video_device(struct media_entity *start_entity,
				enum v4l2_buf_type buftype, bool upstream);

struct imx_media_dma_buf {
	void          *virt;
	dma_addr_t     phys;
	unsigned long  len;
};

void imx_media_free_dma_buf(struct device *dev,
			    struct imx_media_dma_buf *buf);
int imx_media_alloc_dma_buf(struct device *dev,
			    struct imx_media_dma_buf *buf,
			    int size);

int imx_media_pipeline_set_stream(struct imx_media_dev *imxmd,
				  struct media_entity *entity,
				  bool on);

/* imx-media-dev-common.c */
int imx_media_probe_complete(struct v4l2_async_notifier *notifier);
struct imx_media_dev *imx_media_dev_init(struct device *dev,
					 const struct media_device_ops *ops);
int imx_media_dev_notifier_register(struct imx_media_dev *imxmd,
			    const struct v4l2_async_notifier_operations *ops);

/* imx-media-fim.c */
struct imx_media_fim;
void imx_media_fim_eof_monitor(struct imx_media_fim *fim, ktime_t timestamp);
int imx_media_fim_set_stream(struct imx_media_fim *fim,
			     const struct v4l2_fract *frame_interval,
			     bool on);
int imx_media_fim_add_controls(struct imx_media_fim *fim);
struct imx_media_fim *imx_media_fim_init(struct v4l2_subdev *sd);
void imx_media_fim_free(struct imx_media_fim *fim);

/* imx-media-internal-sd.c */
int imx_media_register_ipu_internal_subdevs(struct imx_media_dev *imxmd,
					    struct v4l2_subdev *csi);
void imx_media_unregister_ipu_internal_subdevs(struct imx_media_dev *imxmd);

/* imx-media-of.c */
int imx_media_add_of_subdevs(struct imx_media_dev *dev,
			     struct device_node *np);
int imx_media_of_add_csi(struct imx_media_dev *imxmd,
			 struct device_node *csi_np);

/* imx-media-vdic.c */
struct v4l2_subdev *imx_media_vdic_register(struct v4l2_device *v4l2_dev,
					    struct device *ipu_dev,
					    struct ipu_soc *ipu,
					    u32 grp_id);
int imx_media_vdic_unregister(struct v4l2_subdev *sd);

/* imx-ic-common.c */
struct v4l2_subdev *imx_media_ic_register(struct v4l2_device *v4l2_dev,
					  struct device *ipu_dev,
					  struct ipu_soc *ipu,
					  u32 grp_id);
int imx_media_ic_unregister(struct v4l2_subdev *sd);

/* imx-media-capture.c */
struct imx_media_video_dev *
imx_media_capture_device_init(struct device *dev, struct v4l2_subdev *src_sd,
			      int pad, bool legacy_api);
void imx_media_capture_device_remove(struct imx_media_video_dev *vdev);
int imx_media_capture_device_register(struct imx_media_video_dev *vdev,
				      u32 link_flags);
void imx_media_capture_device_unregister(struct imx_media_video_dev *vdev);
struct imx_media_buffer *
imx_media_capture_device_next_buf(struct imx_media_video_dev *vdev);
void imx_media_capture_device_error(struct imx_media_video_dev *vdev);

/* imx-media-csc-scaler.c */
struct imx_media_video_dev *
imx_media_csc_scaler_device_init(struct imx_media_dev *dev);
int imx_media_csc_scaler_device_register(struct imx_media_video_dev *vdev);
void imx_media_csc_scaler_device_unregister(struct imx_media_video_dev *vdev);

/* subdev group ids */
#define IMX_MEDIA_GRP_ID_CSI2          BIT(8)
#define IMX_MEDIA_GRP_ID_CSI           BIT(9)
#define IMX_MEDIA_GRP_ID_IPU_CSI_BIT   10
#define IMX_MEDIA_GRP_ID_IPU_CSI       (0x3 << IMX_MEDIA_GRP_ID_IPU_CSI_BIT)
#define IMX_MEDIA_GRP_ID_IPU_CSI0      BIT(IMX_MEDIA_GRP_ID_IPU_CSI_BIT)
#define IMX_MEDIA_GRP_ID_IPU_CSI1      (2 << IMX_MEDIA_GRP_ID_IPU_CSI_BIT)
#define IMX_MEDIA_GRP_ID_IPU_VDIC      BIT(12)
#define IMX_MEDIA_GRP_ID_IPU_IC_PRP    BIT(13)
#define IMX_MEDIA_GRP_ID_IPU_IC_PRPENC BIT(14)
#define IMX_MEDIA_GRP_ID_IPU_IC_PRPVF  BIT(15)
#define IMX_MEDIA_GRP_ID_CSI_MUX       BIT(16)

#endif
