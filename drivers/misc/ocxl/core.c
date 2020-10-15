// SPDX-License-Identifier: GPL-2.0+
// Copyright 2019 IBM Corp.
#include <linux/idr.h>
#include "ocxl_internal.h"

static struct ocxl_fn *ocxl_fn_get(struct ocxl_fn *fn)
{
	return (get_device(&fn->dev) == NULL) ? NULL : fn;
}

static void ocxl_fn_put(struct ocxl_fn *fn)
{
	put_device(&fn->dev);
}

static struct ocxl_afu *alloc_afu(struct ocxl_fn *fn)
{
	struct ocxl_afu *afu;

	afu = kzalloc(sizeof(struct ocxl_afu), GFP_KERNEL);
	if (!afu)
		return NULL;

	kref_init(&afu->kref);
	mutex_init(&afu->contexts_lock);
	mutex_init(&afu->afu_control_lock);
	idr_init(&afu->contexts_idr);
	afu->fn = fn;
	ocxl_fn_get(fn);
	return afu;
}

static void free_afu(struct kref *kref)
{
	struct ocxl_afu *afu = container_of(kref, struct ocxl_afu, kref);

	idr_destroy(&afu->contexts_idr);
	ocxl_fn_put(afu->fn);
	kfree(afu);
}

void ocxl_afu_get(struct ocxl_afu *afu)
{
	kref_get(&afu->kref);
}
EXPORT_SYMBOL_GPL(ocxl_afu_get);

void ocxl_afu_put(struct ocxl_afu *afu)
{
	kref_put(&afu->kref, free_afu);
}
EXPORT_SYMBOL_GPL(ocxl_afu_put);

static int assign_afu_actag(struct ocxl_afu *afu)
{
	struct ocxl_fn *fn = afu->fn;
	int actag_count, actag_offset;
	struct pci_dev *pci_dev = to_pci_dev(fn->dev.parent);

	/*
	 * if there were not enough actags for the function, each afu
	 * reduces its count as well
	 */
	actag_count = afu->config.actag_supported *
		fn->actag_enabled / fn->actag_supported;
	actag_offset = ocxl_actag_afu_alloc(fn, actag_count);
	if (actag_offset < 0) {
		dev_err(&pci_dev->dev, "Can't allocate %d actags for AFU: %d\n",
			actag_count, actag_offset);
		return actag_offset;
	}
	afu->actag_base = fn->actag_base + actag_offset;
	afu->actag_enabled = actag_count;

	ocxl_config_set_afu_actag(pci_dev, afu->config.dvsec_afu_control_pos,
				afu->actag_base, afu->actag_enabled);
	dev_dbg(&pci_dev->dev, "actag base=%d enabled=%d\n",
		afu->actag_base, afu->actag_enabled);
	return 0;
}

static void reclaim_afu_actag(struct ocxl_afu *afu)
{
	struct ocxl_fn *fn = afu->fn;
	int start_offset, size;

	start_offset = afu->actag_base - fn->actag_base;
	size = afu->actag_enabled;
	ocxl_actag_afu_free(afu->fn, start_offset, size);
}

static int assign_afu_pasid(struct ocxl_afu *afu)
{
	struct ocxl_fn *fn = afu->fn;
	int pasid_count, pasid_offset;
	struct pci_dev *pci_dev = to_pci_dev(fn->dev.parent);

	/*
	 * We only support the case where the function configuration
	 * requested enough PASIDs to cover all AFUs.
	 */
	pasid_count = 1 << afu->config.pasid_supported_log;
	pasid_offset = ocxl_pasid_afu_alloc(fn, pasid_count);
	if (pasid_offset < 0) {
		dev_err(&pci_dev->dev, "Can't allocate %d PASIDs for AFU: %d\n",
			pasid_count, pasid_offset);
		return pasid_offset;
	}
	afu->pasid_base = fn->pasid_base + pasid_offset;
	afu->pasid_count = 0;
	afu->pasid_max = pasid_count;

	ocxl_config_set_afu_pasid(pci_dev, afu->config.dvsec_afu_control_pos,
				afu->pasid_base,
				afu->config.pasid_supported_log);
	dev_dbg(&pci_dev->dev, "PASID base=%d, enabled=%d\n",
		afu->pasid_base, pasid_count);
	return 0;
}

static void reclaim_afu_pasid(struct ocxl_afu *afu)
{
	struct ocxl_fn *fn = afu->fn;
	int start_offset, size;

	start_offset = afu->pasid_base - fn->pasid_base;
	size = 1 << afu->config.pasid_supported_log;
	ocxl_pasid_afu_free(afu->fn, start_offset, size);
}

static int reserve_fn_bar(struct ocxl_fn *fn, int bar)
{
	struct pci_dev *dev = to_pci_dev(fn->dev.parent);
	int rc, idx;

	if (bar != 0 && bar != 2 && bar != 4)
		return -EINVAL;

	idx = bar >> 1;
	if (fn->bar_used[idx]++ == 0) {
		rc = pci_request_region(dev, bar, "ocxl");
		if (rc)
			return rc;
	}
	return 0;
}

static void release_fn_bar(struct ocxl_fn *fn, int bar)
{
	struct pci_dev *dev = to_pci_dev(fn->dev.parent);
	int idx;

	if (bar != 0 && bar != 2 && bar != 4)
		return;

	idx = bar >> 1;
	if (--fn->bar_used[idx] == 0)
		pci_release_region(dev, bar);
	WARN_ON(fn->bar_used[idx] < 0);
}

static int map_mmio_areas(struct ocxl_afu *afu)
{
	int rc;
	struct pci_dev *pci_dev = to_pci_dev(afu->fn->dev.parent);

	rc = reserve_fn_bar(afu->fn, afu->config.global_mmio_bar);
	if (rc)
		return rc;

	rc = reserve_fn_bar(afu->fn, afu->config.pp_mmio_bar);
	if (rc) {
		release_fn_bar(afu->fn, afu->config.global_mmio_bar);
		return rc;
	}

	afu->global_mmio_start =
		pci_resource_start(pci_dev, afu->config.global_mmio_bar) +
		afu->config.global_mmio_offset;
	afu->pp_mmio_start =
		pci_resource_start(pci_dev, afu->config.pp_mmio_bar) +
		afu->config.pp_mmio_offset;

	afu->global_mmio_ptr = ioremap(afu->global_mmio_start,
				afu->config.global_mmio_size);
	if (!afu->global_mmio_ptr) {
		release_fn_bar(afu->fn, afu->config.pp_mmio_bar);
		release_fn_bar(afu->fn, afu->config.global_mmio_bar);
		dev_err(&pci_dev->dev, "Error mapping global mmio area\n");
		return -ENOMEM;
	}

	/*
	 * Leave an empty page between the per-process mmio area and
	 * the AFU interrupt mappings
	 */
	afu->irq_base_offset = afu->config.pp_mmio_stride + PAGE_SIZE;
	return 0;
}

static void unmap_mmio_areas(struct ocxl_afu *afu)
{
	if (afu->global_mmio_ptr) {
		iounmap(afu->global_mmio_ptr);
		afu->global_mmio_ptr = NULL;
	}
	afu->global_mmio_start = 0;
	afu->pp_mmio_start = 0;
	release_fn_bar(afu->fn, afu->config.pp_mmio_bar);
	release_fn_bar(afu->fn, afu->config.global_mmio_bar);
}

static int configure_afu(struct ocxl_afu *afu, u8 afu_idx, struct pci_dev *dev)
{
	int rc;

	rc = ocxl_config_read_afu(dev, &afu->fn->config, &afu->config, afu_idx);
	if (rc)
		return rc;

	rc = assign_afu_actag(afu);
	if (rc)
		return rc;

	rc = assign_afu_pasid(afu);
	if (rc)
		goto err_free_actag;

	rc = map_mmio_areas(afu);
	if (rc)
		goto err_free_pasid;

	return 0;

err_free_pasid:
	reclaim_afu_pasid(afu);
err_free_actag:
	reclaim_afu_actag(afu);
	return rc;
}

static void deconfigure_afu(struct ocxl_afu *afu)
{
	unmap_mmio_areas(afu);
	reclaim_afu_pasid(afu);
	reclaim_afu_actag(afu);
}

static int activate_afu(struct pci_dev *dev, struct ocxl_afu *afu)
{
	ocxl_config_set_afu_state(dev, afu->config.dvsec_afu_control_pos, 1);

	return 0;
}

static void deactivate_afu(struct ocxl_afu *afu)
{
	struct pci_dev *dev = to_pci_dev(afu->fn->dev.parent);

	ocxl_config_set_afu_state(dev, afu->config.dvsec_afu_control_pos, 0);
}

static int init_afu(struct pci_dev *dev, struct ocxl_fn *fn, u8 afu_idx)
{
	int rc;
	struct ocxl_afu *afu;

	afu = alloc_afu(fn);
	if (!afu)
		return -ENOMEM;

	rc = configure_afu(afu, afu_idx, dev);
	if (rc) {
		ocxl_afu_put(afu);
		return rc;
	}

	rc = activate_afu(dev, afu);
	if (rc) {
		deconfigure_afu(afu);
		ocxl_afu_put(afu);
		return rc;
	}

	list_add_tail(&afu->list, &fn->afu_list);

	return 0;
}

static void remove_afu(struct ocxl_afu *afu)
{
	list_del(&afu->list);
	ocxl_context_detach_all(afu);
	deactivate_afu(afu);
	deconfigure_afu(afu);
	ocxl_afu_put(afu); // matches the implicit get in alloc_afu
}

static struct ocxl_fn *alloc_function(void)
{
	struct ocxl_fn *fn;

	fn = kzalloc(sizeof(struct ocxl_fn), GFP_KERNEL);
	if (!fn)
		return NULL;

	INIT_LIST_HEAD(&fn->afu_list);
	INIT_LIST_HEAD(&fn->pasid_list);
	INIT_LIST_HEAD(&fn->actag_list);

	return fn;
}

static void free_function(struct ocxl_fn *fn)
{
	WARN_ON(!list_empty(&fn->afu_list));
	WARN_ON(!list_empty(&fn->pasid_list));
	kfree(fn);
}

static void free_function_dev(struct device *dev)
{
	struct ocxl_fn *fn = container_of(dev, struct ocxl_fn, dev);

	free_function(fn);
}

static int set_function_device(struct ocxl_fn *fn, struct pci_dev *dev)
{
	fn->dev.parent = &dev->dev;
	fn->dev.release = free_function_dev;
	return dev_set_name(&fn->dev, "ocxlfn.%s", dev_name(&dev->dev));
}

static int assign_function_actag(struct ocxl_fn *fn)
{
	struct pci_dev *dev = to_pci_dev(fn->dev.parent);
	u16 base, enabled, supported;
	int rc;

	rc = ocxl_config_get_actag_info(dev, &base, &enabled, &supported);
	if (rc)
		return rc;

	fn->actag_base = base;
	fn->actag_enabled = enabled;
	fn->actag_supported = supported;

	ocxl_config_set_actag(dev, fn->config.dvsec_function_pos,
			fn->actag_base,	fn->actag_enabled);
	dev_dbg(&fn->dev, "actag range starting at %d, enabled %d\n",
		fn->actag_base, fn->actag_enabled);
	return 0;
}

static int set_function_pasid(struct ocxl_fn *fn)
{
	struct pci_dev *dev = to_pci_dev(fn->dev.parent);
	int rc, desired_count, max_count;

	/* A function may not require any PASID */
	if (fn->config.max_pasid_log < 0)
		return 0;

	rc = ocxl_config_get_pasid_info(dev, &max_count);
	if (rc)
		return rc;

	desired_count = 1 << fn->config.max_pasid_log;

	if (desired_count > max_count) {
		dev_err(&fn->dev,
			"Function requires more PASIDs than is available (%d vs. %d)\n",
			desired_count, max_count);
		return -ENOSPC;
	}

	fn->pasid_base = 0;
	return 0;
}

static int configure_function(struct ocxl_fn *fn, struct pci_dev *dev)
{
	int rc;

	rc = pci_enable_device(dev);
	if (rc) {
		dev_err(&dev->dev, "pci_enable_device failed: %d\n", rc);
		return rc;
	}

	/*
	 * Once it has been confirmed to work on our hardware, we
	 * should reset the function, to force the adapter to restart
	 * from scratch.
	 * A function reset would also reset all its AFUs.
	 *
	 * Some hints for implementation:
	 *
	 * - there's not status bit to know when the reset is done. We
	 *   should try reading the config space to know when it's
	 *   done.
	 * - probably something like:
	 *	Reset
	 *	wait 100ms
	 *	issue config read
	 *	allow device up to 1 sec to return success on config
	 *	read before declaring it broken
	 *
	 * Some shared logic on the card (CFG, TLX) won't be reset, so
	 * there's no guarantee that it will be enough.
	 */
	rc = ocxl_config_read_function(dev, &fn->config);
	if (rc)
		return rc;

	rc = set_function_device(fn, dev);
	if (rc)
		return rc;

	rc = assign_function_actag(fn);
	if (rc)
		return rc;

	rc = set_function_pasid(fn);
	if (rc)
		return rc;

	rc = ocxl_link_setup(dev, 0, &fn->link);
	if (rc)
		return rc;

	rc = ocxl_config_set_TL(dev, fn->config.dvsec_tl_pos);
	if (rc) {
		ocxl_link_release(dev, fn->link);
		return rc;
	}
	return 0;
}

static void deconfigure_function(struct ocxl_fn *fn)
{
	struct pci_dev *dev = to_pci_dev(fn->dev.parent);

	ocxl_link_release(dev, fn->link);
	pci_disable_device(dev);
}

static struct ocxl_fn *init_function(struct pci_dev *dev)
{
	struct ocxl_fn *fn;
	int rc;

	fn = alloc_function();
	if (!fn)
		return ERR_PTR(-ENOMEM);

	rc = configure_function(fn, dev);
	if (rc) {
		free_function(fn);
		return ERR_PTR(rc);
	}

	rc = device_register(&fn->dev);
	if (rc) {
		deconfigure_function(fn);
		put_device(&fn->dev);
		return ERR_PTR(rc);
	}
	return fn;
}

// Device detection & initialisation

struct ocxl_fn *ocxl_function_open(struct pci_dev *dev)
{
	int rc, afu_count = 0;
	u8 afu;
	struct ocxl_fn *fn;

	if (!radix_enabled()) {
		dev_err(&dev->dev, "Unsupported memory model (hash)\n");
		return ERR_PTR(-ENODEV);
	}

	fn = init_function(dev);
	if (IS_ERR(fn)) {
		dev_err(&dev->dev, "function init failed: %li\n",
			PTR_ERR(fn));
		return fn;
	}

	for (afu = 0; afu <= fn->config.max_afu_index; afu++) {
		rc = ocxl_config_check_afu_index(dev, &fn->config, afu);
		if (rc > 0) {
			rc = init_afu(dev, fn, afu);
			if (rc) {
				dev_err(&dev->dev,
					"Can't initialize AFU index %d\n", afu);
				continue;
			}
			afu_count++;
		}
	}
	dev_info(&dev->dev, "%d AFU(s) configured\n", afu_count);
	return fn;
}
EXPORT_SYMBOL_GPL(ocxl_function_open);

struct list_head *ocxl_function_afu_list(struct ocxl_fn *fn)
{
	return &fn->afu_list;
}
EXPORT_SYMBOL_GPL(ocxl_function_afu_list);

struct ocxl_afu *ocxl_function_fetch_afu(struct ocxl_fn *fn, u8 afu_idx)
{
	struct ocxl_afu *afu;

	list_for_each_entry(afu, &fn->afu_list, list) {
		if (afu->config.idx == afu_idx)
			return afu;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(ocxl_function_fetch_afu);

const struct ocxl_fn_config *ocxl_function_config(struct ocxl_fn *fn)
{
	return &fn->config;
}
EXPORT_SYMBOL_GPL(ocxl_function_config);

void ocxl_function_close(struct ocxl_fn *fn)
{
	struct ocxl_afu *afu, *tmp;

	list_for_each_entry_safe(afu, tmp, &fn->afu_list, list) {
		remove_afu(afu);
	}

	deconfigure_function(fn);
	device_unregister(&fn->dev);
}
EXPORT_SYMBOL_GPL(ocxl_function_close);

// AFU Metadata

struct ocxl_afu_config *ocxl_afu_config(struct ocxl_afu *afu)
{
	return &afu->config;
}
EXPORT_SYMBOL_GPL(ocxl_afu_config);

void ocxl_afu_set_private(struct ocxl_afu *afu, void *private)
{
	afu->private = private;
}
EXPORT_SYMBOL_GPL(ocxl_afu_set_private);

void *ocxl_afu_get_private(struct ocxl_afu *afu)
{
	if (afu)
		return afu->private;

	return NULL;
}
EXPORT_SYMBOL_GPL(ocxl_afu_get_private);
