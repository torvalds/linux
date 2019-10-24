// SPDX-License-Identifier: GPL-2.0+
/*
 * Media driver for Freescale i.MX5/6 SOC
 *
 * Adds the IPU internal subdevices and the media links between them.
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 */
#include <linux/platform_device.h>
#include "imx-media.h"

/* max pads per internal-sd */
#define MAX_INTERNAL_PADS   8
/* max links per internal-sd pad */
#define MAX_INTERNAL_LINKS  8

struct internal_subdev;

struct internal_link {
	int remote;
	int local_pad;
	int remote_pad;
};

struct internal_pad {
	int num_links;
	struct internal_link link[MAX_INTERNAL_LINKS];
};

struct internal_subdev {
	u32 grp_id;
	struct internal_pad pad[MAX_INTERNAL_PADS];

	struct v4l2_subdev * (*sync_register)(struct v4l2_device *v4l2_dev,
					      struct device *ipu_dev,
					      struct ipu_soc *ipu,
					      u32 grp_id);
	int (*sync_unregister)(struct v4l2_subdev *sd);
};

static const struct internal_subdev int_subdev[NUM_IPU_SUBDEVS] = {
	[IPU_CSI0] = {
		.grp_id = IMX_MEDIA_GRP_ID_IPU_CSI0,
		.pad[CSI_SRC_PAD_DIRECT] = {
			.num_links = 2,
			.link = {
				{
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = IPU_IC_PRP,
					.remote_pad = PRP_SINK_PAD,
				}, {
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = IPU_VDIC,
					.remote_pad = VDIC_SINK_PAD_DIRECT,
				},
			},
		},
	},

	[IPU_CSI1] = {
		.grp_id = IMX_MEDIA_GRP_ID_IPU_CSI1,
		.pad[CSI_SRC_PAD_DIRECT] = {
			.num_links = 2,
			.link = {
				{
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = IPU_IC_PRP,
					.remote_pad = PRP_SINK_PAD,
				}, {
					.local_pad = CSI_SRC_PAD_DIRECT,
					.remote = IPU_VDIC,
					.remote_pad = VDIC_SINK_PAD_DIRECT,
				},
			},
		},
	},

	[IPU_VDIC] = {
		.grp_id = IMX_MEDIA_GRP_ID_IPU_VDIC,
		.sync_register = imx_media_vdic_register,
		.sync_unregister = imx_media_vdic_unregister,
		.pad[VDIC_SRC_PAD_DIRECT] = {
			.num_links = 1,
			.link = {
				{
					.local_pad = VDIC_SRC_PAD_DIRECT,
					.remote = IPU_IC_PRP,
					.remote_pad = PRP_SINK_PAD,
				},
			},
		},
	},

	[IPU_IC_PRP] = {
		.grp_id = IMX_MEDIA_GRP_ID_IPU_IC_PRP,
		.sync_register = imx_media_ic_register,
		.sync_unregister = imx_media_ic_unregister,
		.pad[PRP_SRC_PAD_PRPENC] = {
			.num_links = 1,
			.link = {
				{
					.local_pad = PRP_SRC_PAD_PRPENC,
					.remote = IPU_IC_PRPENC,
					.remote_pad = PRPENCVF_SINK_PAD,
				},
			},
		},
		.pad[PRP_SRC_PAD_PRPVF] = {
			.num_links = 1,
			.link = {
				{
					.local_pad = PRP_SRC_PAD_PRPVF,
					.remote = IPU_IC_PRPVF,
					.remote_pad = PRPENCVF_SINK_PAD,
				},
			},
		},
	},

	[IPU_IC_PRPENC] = {
		.grp_id = IMX_MEDIA_GRP_ID_IPU_IC_PRPENC,
		.sync_register = imx_media_ic_register,
		.sync_unregister = imx_media_ic_unregister,
	},

	[IPU_IC_PRPVF] = {
		.grp_id = IMX_MEDIA_GRP_ID_IPU_IC_PRPVF,
		.sync_register = imx_media_ic_register,
		.sync_unregister = imx_media_ic_unregister,
	},
};

static int create_internal_link(struct imx_media_dev *imxmd,
				struct v4l2_subdev *src,
				struct v4l2_subdev *sink,
				const struct internal_link *link)
{
	int ret;

	/* skip if this link already created */
	if (media_entity_find_link(&src->entity.pads[link->local_pad],
				   &sink->entity.pads[link->remote_pad]))
		return 0;

	v4l2_info(&imxmd->v4l2_dev, "%s:%d -> %s:%d\n",
		  src->name, link->local_pad,
		  sink->name, link->remote_pad);

	ret = media_create_pad_link(&src->entity, link->local_pad,
				    &sink->entity, link->remote_pad, 0);
	if (ret)
		v4l2_err(&imxmd->v4l2_dev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static int create_ipu_internal_links(struct imx_media_dev *imxmd,
				     const struct internal_subdev *intsd,
				     struct v4l2_subdev *sd,
				     int ipu_id)
{
	const struct internal_pad *intpad;
	const struct internal_link *link;
	struct media_pad *pad;
	int i, j, ret;

	/* create the source->sink links */
	for (i = 0; i < sd->entity.num_pads; i++) {
		intpad = &intsd->pad[i];
		pad = &sd->entity.pads[i];

		if (!(pad->flags & MEDIA_PAD_FL_SOURCE))
			continue;

		for (j = 0; j < intpad->num_links; j++) {
			struct v4l2_subdev *sink;

			link = &intpad->link[j];
			sink = imxmd->sync_sd[ipu_id][link->remote];

			ret = create_internal_link(imxmd, sd, sink, link);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int imx_media_register_ipu_internal_subdevs(struct imx_media_dev *imxmd,
					    struct v4l2_subdev *csi)
{
	struct device *ipu_dev = csi->dev->parent;
	const struct internal_subdev *intsd;
	struct v4l2_subdev *sd;
	struct ipu_soc *ipu;
	int i, ipu_id, ret;

	ipu = dev_get_drvdata(ipu_dev);
	if (!ipu) {
		v4l2_err(&imxmd->v4l2_dev, "invalid IPU device!\n");
		return -ENODEV;
	}

	ipu_id = ipu_get_num(ipu);
	if (ipu_id > 1) {
		v4l2_err(&imxmd->v4l2_dev, "invalid IPU id %d!\n", ipu_id);
		return -ENODEV;
	}

	mutex_lock(&imxmd->mutex);

	/* register the synchronous subdevs */
	for (i = 0; i < NUM_IPU_SUBDEVS; i++) {
		intsd = &int_subdev[i];

		sd = imxmd->sync_sd[ipu_id][i];

		/*
		 * skip if this sync subdev already registered or its
		 * not a sync subdev (one of the CSIs)
		 */
		if (sd || !intsd->sync_register)
			continue;

		mutex_unlock(&imxmd->mutex);
		sd = intsd->sync_register(&imxmd->v4l2_dev, ipu_dev, ipu,
					  intsd->grp_id);
		mutex_lock(&imxmd->mutex);
		if (IS_ERR(sd)) {
			ret = PTR_ERR(sd);
			goto err_unwind;
		}

		imxmd->sync_sd[ipu_id][i] = sd;
	}

	/*
	 * all the sync subdevs are registered, create the media links
	 * between them.
	 */
	for (i = 0; i < NUM_IPU_SUBDEVS; i++) {
		intsd = &int_subdev[i];

		if (intsd->grp_id == csi->grp_id) {
			sd = csi;
		} else {
			sd = imxmd->sync_sd[ipu_id][i];
			if (!sd)
				continue;
		}

		ret = create_ipu_internal_links(imxmd, intsd, sd, ipu_id);
		if (ret) {
			mutex_unlock(&imxmd->mutex);
			imx_media_unregister_ipu_internal_subdevs(imxmd);
			return ret;
		}
	}

	mutex_unlock(&imxmd->mutex);
	return 0;

err_unwind:
	while (--i >= 0) {
		intsd = &int_subdev[i];
		sd = imxmd->sync_sd[ipu_id][i];
		if (!sd || !intsd->sync_unregister)
			continue;
		mutex_unlock(&imxmd->mutex);
		intsd->sync_unregister(sd);
		mutex_lock(&imxmd->mutex);
	}

	mutex_unlock(&imxmd->mutex);
	return ret;
}

void imx_media_unregister_ipu_internal_subdevs(struct imx_media_dev *imxmd)
{
	const struct internal_subdev *intsd;
	struct v4l2_subdev *sd;
	int i, j;

	mutex_lock(&imxmd->mutex);

	for (i = 0; i < 2; i++) {
		for (j = 0; j < NUM_IPU_SUBDEVS; j++) {
			intsd = &int_subdev[j];
			sd = imxmd->sync_sd[i][j];

			if (!sd || !intsd->sync_unregister)
				continue;

			mutex_unlock(&imxmd->mutex);
			intsd->sync_unregister(sd);
			mutex_lock(&imxmd->mutex);
		}
	}

	mutex_unlock(&imxmd->mutex);
}
