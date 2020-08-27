// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Image Loader helpers
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2015 Sony Mobile Communications Inc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/remoteproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/rpmsg/qcom_smd.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "remoteproc_internal.h"
#include "qcom_common.h"

#define to_glink_subdev(d) container_of(d, struct qcom_rproc_glink, subdev)
#define to_smd_subdev(d) container_of(d, struct qcom_rproc_subdev, subdev)
#define to_ssr_subdev(d) container_of(d, struct qcom_rproc_ssr, subdev)

static BLOCKING_NOTIFIER_HEAD(ssr_notifiers);

static int glink_subdev_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	glink->edge = qcom_glink_smem_register(glink->dev, glink->node);

	return PTR_ERR_OR_ZERO(glink->edge);
}

static void glink_subdev_stop(struct rproc_subdev *subdev, bool crashed)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	qcom_glink_smem_unregister(glink->edge);
	glink->edge = NULL;
}

static void glink_subdev_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	qcom_glink_ssr_notify(glink->ssr_name);
}

/**
 * qcom_add_glink_subdev() - try to add a GLINK subdevice to rproc
 * @rproc:	rproc handle to parent the subdevice
 * @glink:	reference to a GLINK subdev context
 * @ssr_name:	identifier of the associated remoteproc for ssr notifications
 */
void qcom_add_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink,
			   const char *ssr_name)
{
	struct device *dev = &rproc->dev;

	glink->node = of_get_child_by_name(dev->parent->of_node, "glink-edge");
	if (!glink->node)
		return;

	glink->ssr_name = kstrdup_const(ssr_name, GFP_KERNEL);
	if (!glink->ssr_name)
		return;

	glink->dev = dev;
	glink->subdev.start = glink_subdev_start;
	glink->subdev.stop = glink_subdev_stop;
	glink->subdev.unprepare = glink_subdev_unprepare;

	rproc_add_subdev(rproc, &glink->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_glink_subdev);

/**
 * qcom_remove_glink_subdev() - remove a GLINK subdevice from rproc
 * @rproc:	rproc handle
 * @glink:	reference to a GLINK subdev context
 */
void qcom_remove_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink)
{
	if (!glink->node)
		return;

	rproc_remove_subdev(rproc, &glink->subdev);
	kfree_const(glink->ssr_name);
	of_node_put(glink->node);
}
EXPORT_SYMBOL_GPL(qcom_remove_glink_subdev);

/**
 * qcom_register_dump_segments() - register segments for coredump
 * @rproc:	remoteproc handle
 * @fw:		firmware header
 *
 * Register all segments of the ELF in the remoteproc coredump segment list
 *
 * Return: 0 on success, negative errno on failure.
 */
int qcom_register_dump_segments(struct rproc *rproc,
				const struct firmware *fw)
{
	const struct elf32_phdr *phdrs;
	const struct elf32_phdr *phdr;
	const struct elf32_hdr *ehdr;
	int ret;
	int i;

	ehdr = (struct elf32_hdr *)fw->data;
	phdrs = (struct elf32_phdr *)(ehdr + 1);

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &phdrs[i];

		if (phdr->p_type != PT_LOAD)
			continue;

		if ((phdr->p_flags & QCOM_MDT_TYPE_MASK) == QCOM_MDT_TYPE_HASH)
			continue;

		if (!phdr->p_memsz)
			continue;

		ret = rproc_coredump_add_segment(rproc, phdr->p_paddr,
						 phdr->p_memsz);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_register_dump_segments);

static int smd_subdev_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_subdev *smd = to_smd_subdev(subdev);

	smd->edge = qcom_smd_register_edge(smd->dev, smd->node);

	return PTR_ERR_OR_ZERO(smd->edge);
}

static void smd_subdev_stop(struct rproc_subdev *subdev, bool crashed)
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
	smd->subdev.start = smd_subdev_start;
	smd->subdev.stop = smd_subdev_stop;

	rproc_add_subdev(rproc, &smd->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_smd_subdev);

/**
 * qcom_remove_smd_subdev() - remove the smd subdevice from rproc
 * @rproc:	rproc handle
 * @smd:	the SMD subdevice to remove
 */
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd)
{
	if (!smd->node)
		return;

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

static void ssr_notify_unprepare(struct rproc_subdev *subdev)
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
	ssr->subdev.unprepare = ssr_notify_unprepare;

	rproc_add_subdev(rproc, &ssr->subdev);
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
