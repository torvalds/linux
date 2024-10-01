// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2023
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#include <linux/init.h>
#include <linux/module.h>

#include "dcmipp-common.h"

/* Helper function to allocate and initialize pads */
struct media_pad *dcmipp_pads_init(u16 num_pads, const unsigned long *pads_flags)
{
	struct media_pad *pads;
	unsigned int i;

	/* Allocate memory for the pads */
	pads = kcalloc(num_pads, sizeof(*pads), GFP_KERNEL);
	if (!pads)
		return ERR_PTR(-ENOMEM);

	/* Initialize the pads */
	for (i = 0; i < num_pads; i++) {
		pads[i].index = i;
		pads[i].flags = pads_flags[i];
	}

	return pads;
}

static const struct media_entity_operations dcmipp_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

int dcmipp_ent_sd_register(struct dcmipp_ent_device *ved,
			   struct v4l2_subdev *sd,
			   struct v4l2_device *v4l2_dev,
			   const char *const name,
			   u32 function,
			   u16 num_pads,
			   const unsigned long *pads_flag,
			   const struct v4l2_subdev_internal_ops *sd_int_ops,
			   const struct v4l2_subdev_ops *sd_ops,
			   irq_handler_t handler,
			   irq_handler_t thread_fn)
{
	int ret;

	/* Allocate the pads. Should be released from the sd_int_op release */
	ved->pads = dcmipp_pads_init(num_pads, pads_flag);
	if (IS_ERR(ved->pads))
		return PTR_ERR(ved->pads);

	/* Fill the dcmipp_ent_device struct */
	ved->ent = &sd->entity;

	/* Initialize the subdev */
	v4l2_subdev_init(sd, sd_ops);
	sd->internal_ops = sd_int_ops;
	sd->entity.function = function;
	sd->entity.ops = &dcmipp_entity_ops;
	sd->owner = THIS_MODULE;
	strscpy(sd->name, name, sizeof(sd->name));
	v4l2_set_subdevdata(sd, ved);

	/* Expose this subdev to user space */
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	if (sd->ctrl_handler)
		sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS;

	/* Initialize the media entity */
	ret = media_entity_pads_init(&sd->entity, num_pads, ved->pads);
	if (ret)
		goto err_clean_pads;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0)
		goto err_clean_m_ent;

	/* Register the subdev with the v4l2 and the media framework */
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret) {
		dev_err(v4l2_dev->dev,
			"%s: subdev register failed (err=%d)\n",
			name, ret);
		goto err_clean_m_ent;
	}

	ved->handler = handler;
	ved->thread_fn = thread_fn;

	return 0;

err_clean_m_ent:
	media_entity_cleanup(&sd->entity);
err_clean_pads:
	dcmipp_pads_cleanup(ved->pads);
	return ret;
}

void
dcmipp_ent_sd_unregister(struct dcmipp_ent_device *ved, struct v4l2_subdev *sd)
{
	media_entity_cleanup(ved->ent);
	v4l2_device_unregister_subdev(sd);
}
