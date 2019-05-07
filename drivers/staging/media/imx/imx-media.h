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
#define IMX_MEDIA_EOF_TIMEOUT       1000

struct imx_media_pixfmt {
	u32     fourcc;
	u32     codes[4];
	int     bpp;     /* total bpp */
	/* cycles per pixel for generic (bayer) formats for the parallel bus */
	int	cycles;
	enum ipu_color_space cs;
	bool    planar;  /* is a planar format */
	bool    bayer;   /* is a raw bayer format */
	bool    ipufmt;  /* is one of the IPU internal formats */
};

struct imx_media_buffer {
	struct vb2_v4l2_buffer vbuf; /* v4l buffer must be first */
	struct list_head  list;
};

struct imx_media_video_dev {
	struct video_device *vfd;

	/* the user format */
	struct v4l2_format fmt;
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

struct imx_media_internal_sd_platformdata {
	char sd_name[V4L2_SUBDEV_NAME_SIZE];
	u32 grp_id;
	int ipu_id;
};

struct imx_media_async_subdev {
	/* the base asd - must be first in this struct */
	struct v4l2_async_subdev asd;
	/* the platform device of IPU-internal subdevs */
	struct platform_device *pdev;
};

static inline struct imx_media_async_subdev *
to_imx_media_asd(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct imx_media_async_subdev, asd);
}

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
};

enum codespace_sel {
	CS_SEL_YUV = 0,
	CS_SEL_RGB,
	CS_SEL_ANY,
};

/* imx-media-utils.c */
const struct imx_media_pixfmt *
imx_media_find_format(u32 fourcc, enum codespace_sel cs_sel, bool allow_bayer);
int imx_media_enum_format(u32 *fourcc, u32 index, enum codespace_sel cs_sel);
const struct imx_media_pixfmt *
imx_media_find_mbus_format(u32 code, enum codespace_sel cs_sel,
			   bool allow_bayer);
int imx_media_enum_mbus_format(u32 *code, u32 index, enum codespace_sel cs_sel,
			       bool allow_bayer);
const struct imx_media_pixfmt *
imx_media_find_ipu_format(u32 code, enum codespace_sel cs_sel);
int imx_media_enum_ipu_format(u32 *code, u32 index, enum codespace_sel cs_sel);
int imx_media_init_mbus_fmt(struct v4l2_mbus_framefmt *mbus,
			    u32 width, u32 height, u32 code, u32 field,
			    const struct imx_media_pixfmt **cc);
int imx_media_init_cfg(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg);
void imx_media_fill_default_mbus_fields(struct v4l2_mbus_framefmt *tryfmt,
					struct v4l2_mbus_framefmt *fmt,
					bool ic_route);
int imx_media_mbus_fmt_to_pix_fmt(struct v4l2_pix_format *pix,
				  struct v4l2_rect *compose,
				  const struct v4l2_mbus_framefmt *mbus,
				  const struct imx_media_pixfmt *cc);
int imx_media_mbus_fmt_to_ipu_image(struct ipu_image *image,
				    struct v4l2_mbus_framefmt *mbus);
int imx_media_ipu_image_to_mbus_fmt(struct v4l2_mbus_framefmt *mbus,
				    struct ipu_image *image);
void imx_media_grp_id_to_sd_name(char *sd_name, int sz,
				 u32 grp_id, int ipu_id);
struct v4l2_subdev *
imx_media_find_subdev_by_fwnode(struct imx_media_dev *imxmd,
				struct fwnode_handle *fwnode);
struct v4l2_subdev *
imx_media_find_subdev_by_devname(struct imx_media_dev *imxmd,
				 const char *devname);
int imx_media_add_video_device(struct imx_media_dev *imxmd,
			       struct imx_media_video_dev *vdev);
int imx_media_find_mipi_csi2_channel(struct imx_media_dev *imxmd,
				     struct media_entity *start_entity);
struct media_pad *
imx_media_find_upstream_pad(struct imx_media_dev *imxmd,
			    struct media_entity *start_entity,
			    u32 grp_id);
struct v4l2_subdev *
imx_media_find_upstream_subdev(struct imx_media_dev *imxmd,
			       struct media_entity *start_entity,
			       u32 grp_id);

struct imx_media_dma_buf {
	void          *virt;
	dma_addr_t     phys;
	unsigned long  len;
};

void imx_media_free_dma_buf(struct imx_media_dev *imxmd,
			    struct imx_media_dma_buf *buf);
int imx_media_alloc_dma_buf(struct imx_media_dev *imxmd,
			    struct imx_media_dma_buf *buf,
			    int size);

int imx_media_pipeline_set_stream(struct imx_media_dev *imxmd,
				  struct media_entity *entity,
				  bool on);

/* imx-media-dev.c */
int imx_media_add_async_subdev(struct imx_media_dev *imxmd,
			       struct fwnode_handle *fwnode,
			       struct platform_device *pdev);

int imx_media_subdev_bound(struct v4l2_async_notifier *notifier,
			   struct v4l2_subdev *sd,
			   struct v4l2_async_subdev *asd);
int imx_media_link_notify(struct media_link *link, u32 flags,
			  unsigned int notification);
void imx_media_notify(struct v4l2_subdev *sd, unsigned int notification,
		      void *arg);
int imx_media_probe_complete(struct v4l2_async_notifier *notifier);

struct imx_media_dev *imx_media_dev_init(struct device *dev);
int imx_media_dev_notifier_register(struct imx_media_dev *imxmd);

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
int imx_media_add_internal_subdevs(struct imx_media_dev *imxmd);
int imx_media_create_ipu_internal_links(struct imx_media_dev *imxmd,
					struct v4l2_subdev *sd);
void imx_media_remove_internal_subdevs(struct imx_media_dev *imxmd);

/* imx-media-of.c */
int imx_media_add_of_subdevs(struct imx_media_dev *dev,
			     struct device_node *np);
int imx_media_create_of_links(struct imx_media_dev *imxmd,
			      struct v4l2_subdev *sd);
int imx_media_create_csi_of_links(struct imx_media_dev *imxmd,
				  struct v4l2_subdev *csi);
int imx_media_of_add_csi(struct imx_media_dev *imxmd,
			 struct device_node *csi_np);

/* imx-media-capture.c */
struct imx_media_video_dev *
imx_media_capture_device_init(struct v4l2_subdev *src_sd, int pad);
void imx_media_capture_device_remove(struct imx_media_video_dev *vdev);
int imx_media_capture_device_register(struct imx_media_video_dev *vdev);
void imx_media_capture_device_unregister(struct imx_media_video_dev *vdev);
struct imx_media_buffer *
imx_media_capture_device_next_buf(struct imx_media_video_dev *vdev);
void imx_media_capture_device_set_format(struct imx_media_video_dev *vdev,
					 const struct v4l2_pix_format *pix,
					 const struct v4l2_rect *compose);
void imx_media_capture_device_error(struct imx_media_video_dev *vdev);

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

#endif
