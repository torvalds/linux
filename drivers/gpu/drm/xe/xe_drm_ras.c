// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/bitmap.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/drm_ras.h>

#include "xe_device_types.h"
#include "xe_drm_ras.h"
#include "xe_ras.h"

static const char * const error_components[] = DRM_XE_RAS_ERROR_COMPONENT_NAMES;
static const char * const error_severity[] = DRM_XE_RAS_ERROR_SEVERITY_NAMES;

static int query_error_counter(struct xe_device *xe,
			       enum drm_xe_ras_error_severity severity,
			       u32 error_id, const char **name, u32 *val)
{
	struct xe_drm_ras *ras = &xe->ras;
	struct xe_drm_ras_counter *info = ras->info[severity];

	if (!info || !info[error_id].name)
		return -ENOENT;

	*name = info[error_id].name;

	/* Fetch counter from system controller if supported */
	if (xe->info.has_sysctrl)
		return xe_ras_get_counter(xe, severity, error_id, val);

	*val = atomic_read(&info[error_id].counter);

	return 0;
}

static int clear_error_counter(struct xe_device *xe,
			       enum drm_xe_ras_error_severity severity,
			       u32 error_id)
{
	struct xe_drm_ras *ras = &xe->ras;
	struct xe_drm_ras_counter *info = ras->info[severity];

	if (!info || !info[error_id].name)
		return -ENOENT;

	/* Clear counter from system controller if supported */
	if (xe->info.has_sysctrl)
		return xe_ras_clear_counter(xe, severity, error_id);

	atomic_set(&info[error_id].counter, 0);

	return 0;
}

static int query_uncorrectable_error_counter(struct drm_ras_node *ep, u32 error_id,
					     const char **name, u32 *val)
{
	struct xe_device *xe = ep->priv;

	return query_error_counter(xe, DRM_XE_RAS_ERR_SEV_UNCORRECTABLE, error_id, name, val);
}

static int clear_uncorrectable_error_counter(struct drm_ras_node *node, u32 error_id)
{
	struct xe_device *xe = node->priv;

	return clear_error_counter(xe, DRM_XE_RAS_ERR_SEV_UNCORRECTABLE, error_id);
}

static int query_correctable_error_counter(struct drm_ras_node *ep, u32 error_id,
					   const char **name, u32 *val)
{
	struct xe_device *xe = ep->priv;

	return query_error_counter(xe, DRM_XE_RAS_ERR_SEV_CORRECTABLE, error_id, name, val);
}

static int clear_correctable_error_counter(struct drm_ras_node *node, u32 error_id)
{
	struct xe_device *xe = node->priv;

	return clear_error_counter(xe, DRM_XE_RAS_ERR_SEV_CORRECTABLE, error_id);
}

static struct xe_drm_ras_counter *allocate_and_copy_counters(struct xe_device *xe)
{
	struct xe_drm_ras_counter *counter;
	int i;

	counter = drmm_kcalloc(&xe->drm, DRM_XE_RAS_ERR_COMP_MAX, sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return ERR_PTR(-ENOMEM);

	for (i = DRM_XE_RAS_ERR_COMP_CORE_COMPUTE; i < DRM_XE_RAS_ERR_COMP_MAX; i++) {
		if (!error_components[i])
			continue;

		counter[i].name = error_components[i];
		atomic_set(&counter[i].counter, 0);
	}

	return counter;
}

static int assign_node_params(struct xe_device *xe, struct drm_ras_node *node,
			      const enum drm_xe_ras_error_severity severity)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct xe_drm_ras *ras = &xe->ras;
	const char *device_name;

	device_name = kasprintf(GFP_KERNEL, "%04x:%02x:%02x.%d",
				pci_domain_nr(pdev->bus), pdev->bus->number,
				PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	if (!device_name)
		return -ENOMEM;

	node->device_name = device_name;
	node->node_name = error_severity[severity];
	node->type = DRM_RAS_NODE_TYPE_ERROR_COUNTER;
	node->error_counter_range.first = DRM_XE_RAS_ERR_COMP_CORE_COMPUTE;
	node->error_counter_range.last = DRM_XE_RAS_ERR_COMP_MAX - 1;
	node->priv = xe;

	ras->info[severity] = allocate_and_copy_counters(xe);
	if (IS_ERR(ras->info[severity]))
		return PTR_ERR(ras->info[severity]);

	if (severity == DRM_XE_RAS_ERR_SEV_CORRECTABLE) {
		node->query_error_counter = query_correctable_error_counter;
		node->clear_error_counter = clear_correctable_error_counter;
	} else {
		node->query_error_counter = query_uncorrectable_error_counter;
		node->clear_error_counter = clear_uncorrectable_error_counter;
	}

	return 0;
}

static void cleanup_node_param(struct drm_ras_node *node)
{
	kfree(node->device_name);
	node->device_name = NULL;
}

static void cleanup_node(struct drm_device *drm, void *node)
{
	drm_ras_node_unregister(node);
	cleanup_node_param(node);
}

static int register_nodes(struct xe_device *xe)
{
	struct xe_drm_ras *ras = &xe->ras;
	struct drm_ras_node *node;
	int i, ret;

	for_each_error_severity(i) {
		node = &ras->node[i];

		ret = assign_node_params(xe, node, i);
		if (ret)
			goto free_param;

		ret = drm_ras_node_register(node);
		if (ret)
			goto free_param;

		ret = drmm_add_action_or_reset(&xe->drm, cleanup_node, node);
		if (ret)
			goto null_info;
	}

	return 0;

free_param:
	cleanup_node_param(node);
null_info:
	ras->info[i] = NULL;
	return ret;
}

/**
 * xe_drm_ras_init() - Initialize DRM RAS
 * @xe: xe device instance
 *
 * Allocate and register DRM RAS nodes per device
 *
 * Return: 0 on success, negative error code otherwise.
 */
int xe_drm_ras_init(struct xe_device *xe)
{
	struct xe_drm_ras *ras = &xe->ras;
	struct drm_ras_node *node;
	int err;

	node = drmm_kcalloc(&xe->drm, DRM_XE_RAS_ERR_SEV_MAX, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	ras->node = node;

	err = register_nodes(xe);
	if (err) {
		drm_err(&xe->drm, "Failed to register DRM RAS nodes (%pe)\n", ERR_PTR(err));
		return err;
	}

	return 0;
}
