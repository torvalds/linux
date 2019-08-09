// SPDX-License-Identifier: GPL-2.0+
/*
 * V4L2 Image Converter Subdev for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2014-2016 Mentor Graphics Inc.
 */
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include "imx-media.h"
#include "imx-ic.h"

#define IC_TASK_PRP IC_NUM_TASKS
#define IC_NUM_OPS  (IC_NUM_TASKS + 1)

static struct imx_ic_ops *ic_ops[IC_NUM_OPS] = {
	[IC_TASK_PRP]            = &imx_ic_prp_ops,
	[IC_TASK_ENCODER]        = &imx_ic_prpencvf_ops,
	[IC_TASK_VIEWFINDER]     = &imx_ic_prpencvf_ops,
};

struct v4l2_subdev *imx_media_ic_register(struct v4l2_device *v4l2_dev,
					  struct device *ipu_dev,
					  struct ipu_soc *ipu,
					  u32 grp_id)
{
	struct imx_ic_priv *priv;
	int ret;

	priv = devm_kzalloc(ipu_dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->ipu_dev = ipu_dev;
	priv->ipu = ipu;

	/* get our IC task id */
	switch (grp_id) {
	case IMX_MEDIA_GRP_ID_IPU_IC_PRP:
		priv->task_id = IC_TASK_PRP;
		break;
	case IMX_MEDIA_GRP_ID_IPU_IC_PRPENC:
		priv->task_id = IC_TASK_ENCODER;
		break;
	case IMX_MEDIA_GRP_ID_IPU_IC_PRPVF:
		priv->task_id = IC_TASK_VIEWFINDER;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	v4l2_subdev_init(&priv->sd, ic_ops[priv->task_id]->subdev_ops);
	v4l2_set_subdevdata(&priv->sd, priv);
	priv->sd.internal_ops = ic_ops[priv->task_id]->internal_ops;
	priv->sd.entity.ops = ic_ops[priv->task_id]->entity_ops;
	priv->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	priv->sd.owner = ipu_dev->driver->owner;
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	priv->sd.grp_id = grp_id;
	imx_media_grp_id_to_sd_name(priv->sd.name, sizeof(priv->sd.name),
				    priv->sd.grp_id, ipu_get_num(ipu));

	ret = ic_ops[priv->task_id]->init(priv);
	if (ret)
		return ERR_PTR(ret);

	ret = v4l2_device_register_subdev(v4l2_dev, &priv->sd);
	if (ret) {
		ic_ops[priv->task_id]->remove(priv);
		return ERR_PTR(ret);
	}

	return &priv->sd;
}

int imx_media_ic_unregister(struct v4l2_subdev *sd)
{
	struct imx_ic_priv *priv = container_of(sd, struct imx_ic_priv, sd);

	v4l2_info(sd, "Removing\n");

	ic_ops[priv->task_id]->remove(priv);

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	return 0;
}
