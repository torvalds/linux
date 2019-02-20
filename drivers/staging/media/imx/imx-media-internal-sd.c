/*
 * Media driver for Freescale i.MX5/6 SOC
 *
 * Adds the IPU internal subdevices and the media links between them.
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/platform_device.h>
#include "imx-media.h"

enum isd_enum {
	isd_csi0 = 0,
	isd_csi1,
	isd_vdic,
	isd_ic_prp,
	isd_ic_prpenc,
	isd_ic_prpvf,
	num_isd,
};

static const struct internal_subdev_id {
	enum isd_enum index;
	const char *name;
	u32 grp_id;
} isd_id[num_isd] = {
	[isd_csi0] = {
		.index = isd_csi0,
		.grp_id = IMX_MEDIA_GRP_ID_IPU_CSI0,
		.name = "imx-ipuv3-csi",
	},
	[isd_csi1] = {
		.index = isd_csi1,
		.grp_id = IMX_MEDIA_GRP_ID_IPU_CSI1,
		.name = "imx-ipuv3-csi",
	},
	[isd_vdic] = {
		.index = isd_vdic,
		.grp_id = IMX_MEDIA_GRP_ID_IPU_VDIC,
		.name = "imx-ipuv3-vdic",
	},
	[isd_ic_prp] = {
		.index = isd_ic_prp,
		.grp_id = IMX_MEDIA_GRP_ID_IPU_IC_PRP,
		.name = "imx-ipuv3-ic",
	},
	[isd_ic_prpenc] = {
		.index = isd_ic_prpenc,
		.grp_id = IMX_MEDIA_GRP_ID_IPU_IC_PRPENC,
		.name = "imx-ipuv3-ic",
	},
	[isd_ic_prpvf] = {
		.index = isd_ic_prpvf,
		.grp_id = IMX_MEDIA_GRP_ID_IPU_IC_PRPVF,
		.name = "imx-ipuv3-ic",
	},
};

struct internal_subdev;

struct internal_link {
	const struct internal_subdev *remote;
	int local_pad;
	int remote_pad;
};

/* max pads per internal-sd */
#define MAX_INTERNAL_PADS   8
/* max links per internal-sd pad */
#define MAX_INTERNAL_LINKS  8

struct internal_pad {
	struct internal_link link[MAX_INTERNAL_LINKS];
};

static const struct internal_subdev {
	const struct internal_subdev_id *id;
	struct internal_pad pad[MAX_INTERNAL_PADS];
} int_subdev[num_isd] = {
	[isd_csi0] = {
		.id = &isd_id[isd_csi0],
		.pad[CSI_SRC_PAD_DIRECT] = {
			.link = {
				{
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = &int_subdev[isd_ic_prp],
					.remote_pad = PRP_SINK_PAD,
				}, {
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = &int_subdev[isd_vdic],
					.remote_pad = VDIC_SINK_PAD_DIRECT,
				},
			},
		},
	},

	[isd_csi1] = {
		.id = &isd_id[isd_csi1],
		.pad[CSI_SRC_PAD_DIRECT] = {
			.link = {
				{
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = &int_subdev[isd_ic_prp],
					.remote_pad = PRP_SINK_PAD,
				}, {
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = &int_subdev[isd_vdic],
					.remote_pad = VDIC_SINK_PAD_DIRECT,
				},
			},
		},
	},

	[isd_vdic] = {
		.id = &isd_id[isd_vdic],
		.pad[VDIC_SRC_PAD_DIRECT] = {
			.link = {
				{
					.local_pad = VDIC_SRC_PAD_DIRECT,
					.remote = &int_subdev[isd_ic_prp],
					.remote_pad = PRP_SINK_PAD,
				},
			},
		},
	},

	[isd_ic_prp] = {
		.id = &isd_id[isd_ic_prp],
		.pad[PRP_SRC_PAD_PRPENC] = {
			.link = {
				{
					.local_pad = PRP_SRC_PAD_PRPENC,
					.remote = &int_subdev[isd_ic_prpenc],
					.remote_pad = 0,
				},
			},
		},
		.pad[PRP_SRC_PAD_PRPVF] = {
			.link = {
				{
					.local_pad = PRP_SRC_PAD_PRPVF,
					.remote = &int_subdev[isd_ic_prpvf],
					.remote_pad = 0,
				},
			},
		},
	},

	[isd_ic_prpenc] = {
		.id = &isd_id[isd_ic_prpenc],
	},

	[isd_ic_prpvf] = {
		.id = &isd_id[isd_ic_prpvf],
	},
};

/* form a device name given an internal subdev and ipu id */
static inline void isd_to_devname(char *devname, int sz,
				  const struct internal_subdev *isd,
				  int ipu_id)
{
	int pdev_id = ipu_id * num_isd + isd->id->index;

	snprintf(devname, sz, "%s.%d", isd->id->name, pdev_id);
}

static const struct internal_subdev *find_intsd_by_grp_id(u32 grp_id)
{
	enum isd_enum i;

	for (i = 0; i < num_isd; i++) {
		const struct internal_subdev *isd = &int_subdev[i];

		if (isd->id->grp_id == grp_id)
			return isd;
	}

	return NULL;
}

static struct v4l2_subdev *find_sink(struct imx_media_dev *imxmd,
				     struct v4l2_subdev *src,
				     const struct internal_link *link)
{
	char sink_devname[32];
	int ipu_id;

	/*
	 * retrieve IPU id from subdev name, note: can't get this from
	 * struct imx_media_ipu_internal_sd_pdata because if src is
	 * a CSI, it has different struct ipu_client_platformdata which
	 * does not contain IPU id.
	 */
	if (sscanf(src->name, "ipu%d", &ipu_id) != 1)
		return NULL;

	isd_to_devname(sink_devname, sizeof(sink_devname),
		       link->remote, ipu_id - 1);

	return imx_media_find_subdev_by_devname(imxmd, sink_devname);
}

static int create_ipu_internal_link(struct imx_media_dev *imxmd,
				    struct v4l2_subdev *src,
				    const struct internal_link *link)
{
	struct v4l2_subdev *sink;
	int ret;

	sink = find_sink(imxmd, src, link);
	if (!sink)
		return -ENODEV;

	v4l2_info(&imxmd->v4l2_dev, "%s:%d -> %s:%d\n",
		  src->name, link->local_pad,
		  sink->name, link->remote_pad);

	ret = media_create_pad_link(&src->entity, link->local_pad,
				    &sink->entity, link->remote_pad, 0);
	if (ret)
		v4l2_err(&imxmd->v4l2_dev,
			 "create_pad_link failed: %d\n", ret);

	return ret;
}

int imx_media_create_ipu_internal_links(struct imx_media_dev *imxmd,
					struct v4l2_subdev *sd)
{
	const struct internal_subdev *intsd;
	const struct internal_pad *intpad;
	const struct internal_link *link;
	struct media_pad *pad;
	int i, j, ret;

	intsd = find_intsd_by_grp_id(sd->grp_id);
	if (!intsd)
		return -ENODEV;

	/* create the source->sink links */
	for (i = 0; i < sd->entity.num_pads; i++) {
		intpad = &intsd->pad[i];
		pad = &sd->entity.pads[i];

		if (!(pad->flags & MEDIA_PAD_FL_SOURCE))
			continue;

		for (j = 0; ; j++) {
			link = &intpad->link[j];

			if (!link->remote)
				break;

			ret = create_ipu_internal_link(imxmd, sd, link);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/* register an internal subdev as a platform device */
static int add_internal_subdev(struct imx_media_dev *imxmd,
			       const struct internal_subdev *isd,
			       int ipu_id)
{
	struct imx_media_ipu_internal_sd_pdata pdata;
	struct platform_device_info pdevinfo = {};
	struct platform_device *pdev;

	pdata.grp_id = isd->id->grp_id;

	/* the id of IPU this subdev will control */
	pdata.ipu_id = ipu_id;

	/* create subdev name */
	imx_media_grp_id_to_sd_name(pdata.sd_name, sizeof(pdata.sd_name),
				    pdata.grp_id, ipu_id);

	pdevinfo.name = isd->id->name;
	pdevinfo.id = ipu_id * num_isd + isd->id->index;
	pdevinfo.parent = imxmd->md.dev;
	pdevinfo.data = &pdata;
	pdevinfo.size_data = sizeof(pdata);
	pdevinfo.dma_mask = DMA_BIT_MASK(32);

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return imx_media_add_async_subdev(imxmd, NULL, pdev);
}

/* adds the internal subdevs in one ipu */
int imx_media_add_ipu_internal_subdevs(struct imx_media_dev *imxmd,
				       int ipu_id)
{
	enum isd_enum i;
	int ret;

	for (i = 0; i < num_isd; i++) {
		const struct internal_subdev *isd = &int_subdev[i];

		/*
		 * the CSIs are represented in the device-tree, so those
		 * devices are already added to the async subdev list by
		 * of_parse_subdev().
		 */
		switch (isd->id->grp_id) {
		case IMX_MEDIA_GRP_ID_IPU_CSI0:
		case IMX_MEDIA_GRP_ID_IPU_CSI1:
			ret = 0;
			break;
		default:
			ret = add_internal_subdev(imxmd, isd, ipu_id);
			break;
		}

		if (ret)
			goto remove;
	}

	return 0;

remove:
	imx_media_remove_ipu_internal_subdevs(imxmd);
	return ret;
}

void imx_media_remove_ipu_internal_subdevs(struct imx_media_dev *imxmd)
{
	struct imx_media_async_subdev *imxasd;
	struct v4l2_async_subdev *asd;

	list_for_each_entry(asd, &imxmd->notifier.asd_list, asd_list) {
		imxasd = to_imx_media_asd(asd);

		if (!imxasd->pdev)
			continue;

		platform_device_unregister(imxasd->pdev);
	}
}
