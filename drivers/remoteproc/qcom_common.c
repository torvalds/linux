/*
 * Qualcomm Peripheral Image Loader helpers
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/remoteproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/rpmsg/qcom_smd.h>

#include "remoteproc_internal.h"
#include "qcom_common.h"

#define to_glink_subdev(d) container_of(d, struct qcom_rproc_glink, subdev)
#define to_smd_subdev(d) container_of(d, struct qcom_rproc_subdev, subdev)
#define to_ssr_subdev(d) container_of(d, struct qcom_rproc_ssr, subdev)

static BLOCKING_NOTIFIER_HEAD(ssr_notifiers);

/**
 * qcom_mdt_find_rsc_table() - provide dummy resource table for remoteproc
 * @rproc:	remoteproc handle
 * @fw:		firmware header
 * @tablesz:	outgoing size of the table
 *
 * Returns a dummy table.
 */
struct resource_table *qcom_mdt_find_rsc_table(struct rproc *rproc,
					       const struct firmware *fw,
					       int *tablesz)
{
	static struct resource_table table = { .ver = 1, };

	*tablesz = sizeof(table);
	return &table;
}
EXPORT_SYMBOL_GPL(qcom_mdt_find_rsc_table);

static int glink_subdev_probe(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	glink->edge = qcom_glink_smem_register(glink->dev, glink->node);

	return IS_ERR(glink->edge) ? PTR_ERR(glink->edge) : 0;
}

static void glink_subdev_remove(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	qcom_glink_smem_unregister(glink->edge);
	glink->edge = NULL;
}

/**
 * qcom_add_glink_subdev() - try to add a GLINK subdevice to rproc
 * @rproc:	rproc handle to parent the subdevice
 * @glink:	reference to a GLINK subdev context
 */
void qcom_add_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink)
{
	struct device *dev = &rproc->dev;

	glink->node = of_get_child_by_name(dev->parent->of_node, "glink-edge");
	if (!glink->node)
		return;

	glink->dev = dev;
	rproc_add_subdev(rproc, &glink->subdev, glink_subdev_probe, glink_subdev_remove);
}
EXPORT_SYMBOL_GPL(qcom_add_glink_subdev);

/**
 * qcom_remove_glink_subdev() - remove a GLINK subdevice from rproc
 * @rproc:	rproc handle
 * @glink:	reference to a GLINK subdev context
 */
void qcom_remove_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink)
{
	rproc_remove_subdev(rproc, &glink->subdev);
	of_node_put(glink->node);
}
EXPORT_SYMBOL_GPL(qcom_remove_glink_subdev);

static int smd_subdev_probe(struct rproc_subdev *subdev)
{
	struct qcom_rproc_subdev *smd = to_smd_subdev(subdev);

	smd->edge = qcom_smd_register_edge(smd->dev, smd->node);

	return PTR_ERR_OR_ZERO(smd->edge);
}

static void smd_subdev_remove(struct rproc_subdev *subdev)
{
	struct qcom_rproc_subdev *smd = to_smd_subdev(subdev);

	qcom_smd_unregister_edge(smd->edge);
	smd->edge = NULL;
}

/**
 * qcom_add_smd_subdev() - try to add a SMD subdevice to rproc
 * @rproc:	rproc handle to parent the subdevice
 * @smd:	reference to a Qualcomm subdev context
 */
void qcom_add_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd)
{
	struct device *dev = &rproc->dev;

	smd->node = of_get_child_by_name(dev->parent->of_node, "smd-edge");
	if (!smd->node)
		return;

	smd->dev = dev;
	rproc_add_subdev(rproc, &smd->subdev, smd_subdev_probe, smd_subdev_remove);
}
EXPORT_SYMBOL_GPL(qcom_add_smd_subdev);

/**
 * qcom_remove_smd_subdev() - remove the smd subdevice from rproc
 * @rproc:	rproc handle
 * @smd:	the SMD subdevice to remove
 */
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd)
{
	rproc_remove_subdev(rproc, &smd->subdev);
	of_node_put(smd->node);
}
EXPORT_SYMBOL_GPL(qcom_remove_smd_subdev);

/**
 * qcom_register_ssr_notifier() - register SSR notification handler
 * @nb:		notifier_block to notify for restart notifications
 *
 * Returns 0 on success, negative errno on failure.
 *
 * This register the @notify function as handler for restart notifications. As
 * remote processors are stopped this function will be called, with the SSR
 * name passed as a parameter.
 */
int qcom_register_ssr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ssr_notifiers, nb);
}
EXPORT_SYMBOL_GPL(qcom_register_ssr_notifier);

/**
 * qcom_unregister_ssr_notifier() - unregister SSR notification handler
 * @nb:		notifier_block to unregister
 */
void qcom_unregister_ssr_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&ssr_notifiers, nb);
}
EXPORT_SYMBOL_GPL(qcom_unregister_ssr_notifier);

static int ssr_notify_start(struct rproc_subdev *subdev)
{
	return  0;
}

static void ssr_notify_stop(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);

	blocking_notifier_call_chain(&ssr_notifiers, 0, (void *)ssr->name);
}

/**
 * qcom_add_ssr_subdev() - register subdevice as restart notification source
 * @rproc:	rproc handle
 * @ssr:	SSR subdevice handle
 * @ssr_name:	identifier to use for notifications originating from @rproc
 *
 * As the @ssr is registered with the @rproc SSR events will be sent to all
 * registered listeners in the system as the remoteproc is shut down.
 */
void qcom_add_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr,
			 const char *ssr_name)
{
	ssr->name = ssr_name;

	rproc_add_subdev(rproc, &ssr->subdev, ssr_notify_start, ssr_notify_stop);
}
EXPORT_SYMBOL_GPL(qcom_add_ssr_subdev);

/**
 * qcom_remove_ssr_subdev() - remove subdevice as restart notification source
 * @rproc:	rproc handle
 * @ssr:	SSR subdevice handle
 */
void qcom_remove_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr)
{
	rproc_remove_subdev(rproc, &ssr->subdev);
}
EXPORT_SYMBOL_GPL(qcom_remove_ssr_subdev);

MODULE_DESCRIPTION("Qualcomm Remoteproc helper driver");
MODULE_LICENSE("GPL v2");
