/*
 * Media driver for Freescale i.MX5/6 SOC
 *
 * Adds the internal subdevices and the media links between them.
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
		.grp_id = IMX_MEDIA_GRP_ID_CSI0,
		.name = "imx-ipuv3-csi",
	},
	[isd_csi1] = {
		.index = isd_csi1,
		.grp_id = IMX_MEDIA_GRP_ID_CSI1,
		.name = "imx-ipuv3-csi",
	},
	[isd_vdic] = {
		.index = isd_vdic,
		.grp_id = IMX_MEDIA_GRP_ID_VDIC,
		.name = "imx-ipuv3-vdic",
	},
	[isd_ic_prp] = {
		.index = isd_ic_prp,
		.grp_id = IMX_MEDIA_GRP_ID_IC_PRP,
		.name = "imx-ipuv3-ic",
	},
	[isd_ic_prpenc] = {
		.index = isd_ic_prpenc,
		.grp_id = IMX_MEDIA_GRP_ID_IC_PRPENC,
		.name = "imx-ipuv3-ic",
	},
	[isd_ic_prpvf] = {
		.index = isd_ic_prpvf,
		.grp_id = IMX_MEDIA_GRP_ID_IC_PRPVF,
		.name = "imx-ipuv3-ic",
	},
};

struct internal_link {
	const struct internal_subdev_id *remote_id;
	int remote_pad;
};

struct internal_pad {
	bool devnode; /* does this pad link to a device node */
	struct internal_link link[IMX_MEDIA_MAX_LINKS];
};

static const struct internal_subdev {
	const struct internal_subdev_id *id;
	struct internal_pad pad[IMX_MEDIA_MAX_PADS];
	int num_sink_pads;
	int num_src_pads;
} internal_subdev[num_isd] = {
	[isd_csi0] = {
		.id = &isd_id[isd_csi0],
		.num_sink_pads = CSI_NUM_SINK_PADS,
		.num_src_pads = CSI_NUM_SRC_PADS,
		.pad[CSI_SRC_PAD_DIRECT] = {
			.link = {
				{
					.remote_id = &isd_id[isd_ic_prp],
					.remote_pad = PRP_SINK_PAD,
				}, {
					.remote_id =  &isd_id[isd_vdic],
					.remote_pad = VDIC_SINK_PAD_DIRECT,
				},
			},
		},
		.pad[CSI_SRC_PAD_IDMAC] = {
			.devnode = true,
		},
	},

	[isd_csi1] = {
		.id = &isd_id[isd_csi1],
		.num_sink_pads = CSI_NUM_SINK_PADS,
		.num_src_pads = CSI_NUM_SRC_PADS,
		.pad[CSI_SRC_PAD_DIRECT] = {
			.link = {
				{
					.remote_id = &isd_id[isd_ic_prp],
					.remote_pad = PRP_SINK_PAD,
				}, {
					.remote_id =  &isd_id[isd_vdic],
					.remote_pad = VDIC_SINK_PAD_DIRECT,
				},
			},
		},
		.pad[CSI_SRC_PAD_IDMAC] = {
			.devnode = true,
		},
	},

	[isd_vdic] = {
		.id = &isd_id[isd_vdic],
		.num_sink_pads = VDIC_NUM_SINK_PADS,
		.num_src_pads = VDIC_NUM_SRC_PADS,
		.pad[VDIC_SINK_PAD_IDMAC] = {
			.devnode = true,
		},
		.pad[VDIC_SRC_PAD_DIRECT] = {
			.link = {
				{
					.remote_id =  &isd_id[isd_ic_prp],
					.remote_pad = PRP_SINK_PAD,
				},
			},
		},
	},

	[isd_ic_prp] = {
		.id = &isd_id[isd_ic_prp],
		.num_sink_pads = PRP_NUM_SINK_PADS,
		.num_src_pads = PRP_NUM_SRC_PADS,
		.pad[PRP_SRC_PAD_PRPENC] = {
			.link = {
				{
					.remote_id = &isd_id[isd_ic_prpenc],
					.remote_pad = 0,
				},
			},
		},
		.pad[PRP_SRC_PAD_PRPVF] = {
			.link = {
				{
					.remote_id = &isd_id[isd_ic_prpvf],
					.remote_pad = 0,
				},
			},
		},
	},

	[isd_ic_prpenc] = {
		.id = &isd_id[isd_ic_prpenc],
		.num_sink_pads = PRPENCVF_NUM_SINK_PADS,
		.num_src_pads = PRPENCVF_NUM_SRC_PADS,
		.pad[PRPENCVF_SRC_PAD] = {
			.devnode = true,
		},
	},

	[isd_ic_prpvf] = {
		.id = &isd_id[isd_ic_prpvf],
		.num_sink_pads = PRPENCVF_NUM_SINK_PADS,
		.num_src_pads = PRPENCVF_NUM_SRC_PADS,
		.pad[PRPENCVF_SRC_PAD] = {
			.devnode = true,
		},
	},
};

/* form a device name given a group id and ipu id */
static inline void isd_id_to_devname(char *devname, int sz,
				     const struct internal_subdev_id *id,
				     int ipu_id)
{
	int pdev_id = ipu_id * num_isd + id->index;

	snprintf(devname, sz, "%s.%d", id->name, pdev_id);
}

/* adds the links from given internal subdev */
static int add_internal_links(struct imx_media_dev *imxmd,
			      const struct internal_subdev *isd,
			      struct imx_media_subdev *imxsd,
			      int ipu_id)
{
	int i, num_pads, ret;

	num_pads = isd->num_sink_pads + isd->num_src_pads;

	for (i = 0; i < num_pads; i++) {
		const struct internal_pad *intpad = &isd->pad[i];
		struct imx_media_pad *pad = &imxsd->pad[i];
		int j;

		/* init the pad flags for this internal subdev */
		pad->pad.flags = (i < isd->num_sink_pads) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
		/* export devnode pad flag to the subdevs */
		pad->devnode = intpad->devnode;

		for (j = 0; ; j++) {
			const struct internal_link *link;
			char remote_devname[32];

			link = &intpad->link[j];

			if (!link->remote_id)
				break;

			isd_id_to_devname(remote_devname,
					  sizeof(remote_devname),
					  link->remote_id, ipu_id);

			ret = imx_media_add_pad_link(imxmd, pad,
						     NULL, remote_devname,
						     i, link->remote_pad);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/* register an internal subdev as a platform device */
static struct imx_media_subdev *
add_internal_subdev(struct imx_media_dev *imxmd,
		    const struct internal_subdev *isd,
		    int ipu_id)
{
	struct imx_media_internal_sd_platformdata pdata;
	struct platform_device_info pdevinfo = {0};
	struct imx_media_subdev *imxsd;
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
		return ERR_CAST(pdev);

	imxsd = imx_media_add_async_subdev(imxmd, NULL, pdev);
	if (IS_ERR(imxsd))
		return imxsd;

	imxsd->num_sink_pads = isd->num_sink_pads;
	imxsd->num_src_pads = isd->num_src_pads;

	return imxsd;
}

/* adds the internal subdevs in one ipu */
static int add_ipu_internal_subdevs(struct imx_media_dev *imxmd,
				    struct imx_media_subdev *csi0,
				    struct imx_media_subdev *csi1,
				    int ipu_id)
{
	enum isd_enum i;
	int ret;

	for (i = 0; i < num_isd; i++) {
		const struct internal_subdev *isd = &internal_subdev[i];
		struct imx_media_subdev *imxsd;

		/*
		 * the CSIs are represented in the device-tree, so those
		 * devices are added already, and are added to the async
		 * subdev list by of_parse_subdev(), so we are given those
		 * subdevs as csi0 and csi1.
		 */
		switch (isd->id->grp_id) {
		case IMX_MEDIA_GRP_ID_CSI0:
			imxsd = csi0;
			break;
		case IMX_MEDIA_GRP_ID_CSI1:
			imxsd = csi1;
			break;
		default:
			imxsd = add_internal_subdev(imxmd, isd, ipu_id);
			break;
		}

		if (IS_ERR(imxsd))
			return PTR_ERR(imxsd);

		/* add the links from this subdev */
		if (imxsd) {
			ret = add_internal_links(imxmd, isd, imxsd, ipu_id);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int imx_media_add_internal_subdevs(struct imx_media_dev *imxmd,
				   struct imx_media_subdev *csi[4])
{
	int ret;

	ret = add_ipu_internal_subdevs(imxmd, csi[0], csi[1], 0);
	if (ret)
		goto remove;

	ret = add_ipu_internal_subdevs(imxmd, csi[2], csi[3], 1);
	if (ret)
		goto remove;

	return 0;

remove:
	imx_media_remove_internal_subdevs(imxmd);
	return ret;
}

void imx_media_remove_internal_subdevs(struct imx_media_dev *imxmd)
{
	struct imx_media_subdev *imxsd;
	int i;

	for (i = 0; i < imxmd->subdev_notifier.num_subdevs; i++) {
		imxsd = &imxmd->subdev[i];
		if (!imxsd->pdev)
			continue;
		platform_device_unregister(imxsd->pdev);
	}
}
