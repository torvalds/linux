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
#include <linux/yestifier.h>
#include <linux/remoteproc.h>
#include <linux/rpmsg/qcom_glink.h>
#include <linux/rpmsg/qcom_smd.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "remoteproc_internal.h"
#include "qcom_common.h"

#define to_glink_subdev(d) container_of(d, struct qcom_rproc_glink, subdev)
#define to_smd_subdev(d) container_of(d, struct qcom_rproc_subdev, subdev)
#define to_ssr_subdev(d) container_of(d, struct qcom_rproc_ssr, subdev)

static BLOCKING_NOTIFIER_HEAD(ssr_yestifiers);

static int glink_subdev_start(struct rproc_subdev *subdev)
{
	struct qcom_rproc_glink *glink = to_glink_subdev(subdev);

	glink->edge = qcom_glink_smem_register(glink->dev, glink->yesde);

	return PTR_ERR_OR_ZERO(glink->edge);
}

static void glink_subdev_stop(struct rproc_subdev *subdev, bool crashed)
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

	glink->yesde = of_get_child_by_name(dev->parent->of_yesde, "glink-edge");
	if (!glink->yesde)
		return;

	glink->dev = dev;
	glink->subdev.start = glink_subdev_start;
	glink->subdev.stop = glink_subdev_stop;

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
	if (!glink->yesde)
		return;

	rproc_remove_subdev(rproc, &glink->subdev);
	of_yesde_put(glink->yesde);
}
EXPORT_SYMBOL_GPL(qcom_remove_glink_subdev);

/**
 * qcom_register_dump_segments() - register segments for coredump
 * @rproc:	remoteproc handle
 * @fw:		firmware header
 *
 * Register all segments of the ELF in the remoteproc coredump segment list
 *
 * Return: 0 on success, negative erryes on failure.
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

	smd->edge = qcom_smd_register_edge(smd->dev, smd->yesde);

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

	smd->yesde = of_get_child_by_name(dev->parent->of_yesde, "smd-edge");
	if (!smd->yesde)
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
	if (!smd->yesde)
		return;

	rproc_remove_subdev(rproc, &smd->subdev);
	of_yesde_put(smd->yesde);
}
EXPORT_SYMBOL_GPL(qcom_remove_smd_subdev);

/**
 * qcom_register_ssr_yestifier() - register SSR yestification handler
 * @nb:		yestifier_block to yestify for restart yestifications
 *
 * Returns 0 on success, negative erryes on failure.
 *
 * This register the @yestify function as handler for restart yestifications. As
 * remote processors are stopped this function will be called, with the SSR
 * name passed as a parameter.
 */
int qcom_register_ssr_yestifier(struct yestifier_block *nb)
{
	return blocking_yestifier_chain_register(&ssr_yestifiers, nb);
}
EXPORT_SYMBOL_GPL(qcom_register_ssr_yestifier);

/**
 * qcom_unregister_ssr_yestifier() - unregister SSR yestification handler
 * @nb:		yestifier_block to unregister
 */
void qcom_unregister_ssr_yestifier(struct yestifier_block *nb)
{
	blocking_yestifier_chain_unregister(&ssr_yestifiers, nb);
}
EXPORT_SYMBOL_GPL(qcom_unregister_ssr_yestifier);

static void ssr_yestify_unprepare(struct rproc_subdev *subdev)
{
	struct qcom_rproc_ssr *ssr = to_ssr_subdev(subdev);

	blocking_yestifier_call_chain(&ssr_yestifiers, 0, (void *)ssr->name);
}

/**
 * qcom_add_ssr_subdev() - register subdevice as restart yestification source
 * @rproc:	rproc handle
 * @ssr:	SSR subdevice handle
 * @ssr_name:	identifier to use for yestifications originating from @rproc
 *
 * As the @ssr is registered with the @rproc SSR events will be sent to all
 * registered listeners in the system as the remoteproc is shut down.
 */
void qcom_add_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr,
			 const char *ssr_name)
{
	ssr->name = ssr_name;
	ssr->subdev.unprepare = ssr_yestify_unprepare;

	rproc_add_subdev(rproc, &ssr->subdev);
}
EXPORT_SYMBOL_GPL(qcom_add_ssr_subdev);

/**
 * qcom_remove_ssr_subdev() - remove subdevice as restart yestification source
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
