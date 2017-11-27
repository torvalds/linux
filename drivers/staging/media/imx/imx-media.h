/*
 * V4L2 Media Controller Driver for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _IMX_MEDIA_H
#define _IMX_MEDIA_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <video/imx-ipu-v3.h>

/*
 * This is somewhat arbitrary, but we need at least:
 * - 4 video devices per IPU
 * - 3 IC subdevs per IPU
 * - 1 VDIC subdev per IPU
 * - 2 CSI subdevs per IPU
 * - 1 mipi-csi2 receiver subdev
 * - 2 video-mux subdevs
 * - 2 camera sensor subdevs per IPU (1 parallel, 1 mipi-csi2)
 *
 */
/* max video devices */
#define IMX_MEDIA_MAX_VDEVS          8
/* max subdevices */
#define IMX_MEDIA_MAX_SUBDEVS       32
/* max pads per subdev */
#define IMX_MEDIA_MAX_PADS          16
/* max links per pad */
#define IMX_MEDIA_MAX_LINKS          8

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

#define CSI_NUM_SINK_PADS 1
#define CSI_NUM_SRC_PADS  2

/* ipu_vdic */
enum {
	VDIC_SINK_PAD_DIRECT = 0,
	VDIC_SINK_PAD_IDMAC,
	VDIC_SRC_PAD_DIRECT,
	VDIC_NUM_PADS,
};

#define VDIC_NUM_SINK_PADS 2
#define VDIC_NUM_SRC_PADS  1

/* ipu_ic_prp */
enum {
	PRP_SINK_PAD = 0,
	PRP_SRC_PAD_PRPENC,
	PRP_SRC_PAD_PRPVF,
	PRP_NUM_PADS,
};

#define PRP_NUM_SINK_PADS 1
#define PRP_NUM_SRC_PADS  2

/* ipu_ic_prpencvf */
enum {
	PRPENCVF_SINK_PAD = 0,
	PRPENCVF_SRC_PAD,
	PRPENCVF_NUM_PADS,
};

#define PRPENCVF_NUM_SINK_PADS 1
#define PRPENCVF_NUM_SRC_PADS  1

/* How long to wait for EOF interrupts in the buffer-capture subdevs */
#define IMX_MEDIA_EOF_TIMEOUT       1000

struct imx_media_pixfmt {
	u32     fourcc;
	u32     codes[4];
	int     bpp;     /* total bpp */
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
	const struct imx_media_pixfmt *cc;
};

static inline struct imx_media_buffer *to_imx_media_vb(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	return container_of(vbuf, struct imx_media_buffer, vbuf);
}

struct imx_media_link {
	struct device_node *remote_sd_node;
	char               remote_devname[32];
	int                local_pad;
	int                remote_pad;
};

struct imx_media_pad {
	struct media_pad  pad;
	struct imx_media_link link[IMX_MEDIA_MAX_LINKS];
	bool devnode; /* does this pad link to a device node */
	int num_links;

	/*
	 * list of video devices that can be reached from this pad,
	 * list is only valid for source pads.
	 */
	struct imx_media_video_dev *vdev[IMX_MEDIA_MAX_VDEVS];
	int num_vdevs;
};

struct imx_media_internal_sd_platformdata {
	char sd_name[V4L2_SUBDEV_NAME_SIZE];
	u32 grp_id;
	int ipu_id;
};

struct imx_media_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev       *sd; /* set when bound */

	struct imx_media_pad     pad[IMX_MEDIA_MAX_PADS];
	int num_sink_pads;
	int num_src_pads;

	/* the platform device if this is an internal subdev */
	struct platform_device *pdev;
	/* the devname is needed for async devname match */
	char devname[32];

	/* if this is a sensor */
	struct v4l2_fwnode_endpoint sensor_ep;
};

struct imx_media_dev {
	struct media_device md;
	struct v4l2_device  v4l2_dev;

	/* the pipeline object */
	struct media_pipeline pipe;

	struct mutex mutex; /* protect elements below */

	/* master subdevice list */
	struct imx_media_subdev subdev[IMX_MEDIA_MAX_SUBDEVS];
	int num_subdevs;

	/* master video device list */
	struct imx_media_video_dev *vdev[IMX_MEDIA_MAX_VDEVS];
	int num_vdevs;

	/* IPUs this media driver control, valid after subdevs bound */
	struct ipu_soc *ipu[2];

	/* for async subdev registration */
	struct v4l2_async_subdev *async_ptrs[IMX_MEDIA_MAX_SUBDEVS];
	struct v4l2_async_notifier subdev_notifier;
};

enum codespace_sel {
	CS_SEL_YUV = 0,
	CS_SEL_RGB,
	CS_SEL_ANY,
};

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
void imx_media_fill_default_mbus_fields(struct v4l2_mbus_framefmt *tryfmt,
					struct v4l2_mbus_framefmt *fmt,
					bool ic_route);
int imx_media_mbus_fmt_to_pix_fmt(struct v4l2_pix_format *pix,
				  struct v4l2_mbus_framefmt *mbus,
				  const struct imx_media_pixfmt *cc);
int imx_media_mbus_fmt_to_ipu_image(struct ipu_image *image,
				    struct v4l2_mbus_framefmt *mbus);
int imx_media_ipu_image_to_mbus_fmt(struct v4l2_mbus_framefmt *mbus,
				    struct ipu_image *image);

struct imx_media_subdev *
imx_media_find_async_subdev(struct imx_media_dev *imxmd,
			    struct device_node *np,
			    const char *devname);
struct imx_media_subdev *
imx_media_add_async_subdev(struct imx_media_dev *imxmd,
			   struct device_node *np,
			   struct platform_device *pdev);
int imx_media_add_pad_link(struct imx_media_dev *imxmd,
			   struct imx_media_pad *pad,
			   struct device_node *remote_node,
			   const char *remote_devname,
			   int local_pad, int remote_pad);

void imx_media_grp_id_to_sd_name(char *sd_name, int sz,
				 u32 grp_id, int ipu_id);

int imx_media_add_internal_subdevs(struct imx_media_dev *imxmd,
				   struct imx_media_subdev *csi[4]);
void imx_media_remove_internal_subdevs(struct imx_media_dev *imxmd);

struct imx_media_subdev *
imx_media_find_subdev_by_sd(struct imx_media_dev *imxmd,
			    struct v4l2_subdev *sd);
struct imx_media_subdev *
imx_media_find_subdev_by_id(struct imx_media_dev *imxmd,
			    u32 grp_id);
int imx_media_add_video_device(struct imx_media_dev *imxmd,
			       struct imx_media_video_dev *vdev);
int imx_media_find_mipi_csi2_channel(struct imx_media_dev *imxmd,
				     struct media_entity *start_entity);
struct imx_media_subdev *
imx_media_find_upstream_subdev(struct imx_media_dev *imxmd,
			       struct media_entity *start_entity,
			       u32 grp_id);
struct imx_media_subdev *
__imx_media_find_sensor(struct imx_media_dev *imxmd,
			struct media_entity *start_entity);
struct imx_media_subdev *
imx_media_find_sensor(struct imx_media_dev *imxmd,
		      struct media_entity *start_entity);

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

/* imx-media-fim.c */
struct imx_media_fim;
void imx_media_fim_eof_monitor(struct imx_media_fim *fim, ktime_t timestamp);
int imx_media_fim_set_stream(struct imx_media_fim *fim,
			     const struct v4l2_fract *frame_interval,
			     bool on);
int imx_media_fim_add_controls(struct imx_media_fim *fim);
struct imx_media_fim *imx_media_fim_init(struct v4l2_subdev *sd);
void imx_media_fim_free(struct imx_media_fim *fim);

/* imx-media-of.c */
struct imx_media_subdev *
imx_media_of_find_subdev(struct imx_media_dev *imxmd,
			 struct device_node *np,
			 const char *name);
int imx_media_of_parse(struct imx_media_dev *dev,
		       struct imx_media_subdev *(*csi)[4],
		       struct device_node *np);

/* imx-media-capture.c */
struct imx_media_video_dev *
imx_media_capture_device_init(struct v4l2_subdev *src_sd, int pad);
void imx_media_capture_device_remove(struct imx_media_video_dev *vdev);
int imx_media_capture_device_register(struct imx_media_video_dev *vdev);
void imx_media_capture_device_unregister(struct imx_media_video_dev *vdev);
struct imx_media_buffer *
imx_media_capture_device_next_buf(struct imx_media_video_dev *vdev);
void imx_media_capture_device_set_format(struct imx_media_video_dev *vdev,
					 struct v4l2_pix_format *pix);
void imx_media_capture_device_error(struct imx_media_video_dev *vdev);

/* subdev group ids */
#define IMX_MEDIA_GRP_ID_SENSOR    (1 << 8)
#define IMX_MEDIA_GRP_ID_VIDMUX    (1 << 9)
#define IMX_MEDIA_GRP_ID_CSI2      (1 << 10)
#define IMX_MEDIA_GRP_ID_CSI_BIT   11
#define IMX_MEDIA_GRP_ID_CSI       (0x3 << IMX_MEDIA_GRP_ID_CSI_BIT)
#define IMX_MEDIA_GRP_ID_CSI0      (1 << IMX_MEDIA_GRP_ID_CSI_BIT)
#define IMX_MEDIA_GRP_ID_CSI1      (2 << IMX_MEDIA_GRP_ID_CSI_BIT)
#define IMX_MEDIA_GRP_ID_VDIC      (1 << 13)
#define IMX_MEDIA_GRP_ID_IC_PRP    (1 << 14)
#define IMX_MEDIA_GRP_ID_IC_PRPENC (1 << 15)
#define IMX_MEDIA_GRP_ID_IC_PRPVF  (1 << 16)

#endif
