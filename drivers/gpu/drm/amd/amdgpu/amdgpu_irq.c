/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include <linux/irq.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_ih.h"
#include "atom.h"
#include "amdgpu_connectors.h"

#include <linux/pm_runtime.h>

#define AMDGPU_WAIT_IDLE_TIMEOUT 200

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
/**
 * amdgpu_hotplug_work_func - display hotplug work handler
 *
 * @work: work struct
 *
 * This is the hot plug event work handler (all asics).
 * The work gets scheduled from the irq handler if there
 * was a hot plug interrupt.  It walks the connector table
 * and calls the hotplug handler for each one, then sends
 * a drm hotplug event to alert userspace.
 */
static void amdgpu_hotplug_work_func(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  hotplug_work);
	struct drm_device *dev = adev->ddev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	mutex_lock(&mode_config->mutex);
	if (mode_config->num_connector) {
		list_for_each_entry(connector, &mode_config->connector_list, head)
			amdgpu_connector_hotplug(connector);
	}
	mutex_unlock(&mode_config->mutex);
	/* Just fire off a uevent and let userspace tell us what to do */
	drm_helper_hpd_irq_event(dev);
}

/**
 * amdgpu_irq_reset_work_func - execute gpu reset
 *
 * @work: work struct
 *
 * Execute scheduled gpu reset (cayman+).
 * This function is called when the irq handler
 * thinks we need a gpu reset.
 */
static void amdgpu_irq_reset_work_func(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  reset_work);

	amdgpu_gpu_reset(adev);
}

/* Disable *all* interrupts */
static void amdgpu_irq_disable_all(struct amdgpu_device *adev)
{
	unsigned long irqflags;
	unsigned i, j;
	int r;

	spin_lock_irqsave(&adev->irq.lock, irqflags);
	for (i = 0; i < AMDGPU_MAX_IRQ_SRC_ID; ++i) {
		struct amdgpu_irq_src *src = adev->irq.sources[i];

		if (!src || !src->funcs->set || !src->num_types)
			continue;

		for (j = 0; j < src->num_types; ++j) {
			atomic_set(&src->enabled_types[j], 0);
			r = src->funcs->set(adev, src, j,
					    AMDGPU_IRQ_STATE_DISABLE);
			if (r)
				DRM_ERROR("error disabling interrupt (%d)\n",
					  r);
		}
	}
	spin_unlock_irqrestore(&adev->irq.lock, irqflags);
}

/**
 * amdgpu_irq_preinstall - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Gets the hw ready to enable irqs (all asics).
 * This function disables all interrupt sources on the GPU.
 */
void amdgpu_irq_preinstall(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;

	/* Disable *all* interrupts */
	amdgpu_irq_disable_all(adev);
	/* Clear bits */
	amdgpu_ih_process(adev);
}

/**
 * amdgpu_irq_postinstall - drm irq preinstall callback
 *
 * @dev: drm dev pointer
 *
 * Handles stuff to be done after enabling irqs (all asics).
 * Returns 0 on success.
 */
int amdgpu_irq_postinstall(struct drm_device *dev)
{
	dev->max_vblank_count = 0x00ffffff;
	return 0;
}

/**
 * amdgpu_irq_uninstall - drm irq uninstall callback
 *
 * @dev: drm dev pointer
 *
 * This function disables all interrupt sources on the GPU (all asics).
 */
void amdgpu_irq_uninstall(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;

	if (adev == NULL) {
		return;
	}
	amdgpu_irq_disable_all(adev);
}

/**
 * amdgpu_irq_handler - irq handler
 *
 * @int irq, void *arg: args
 *
 * This is the irq handler for the amdgpu driver (all asics).
 */
irqreturn_t amdgpu_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct amdgpu_device *adev = dev->dev_private;
	irqreturn_t ret;

	ret = amdgpu_ih_process(adev);
	if (ret == IRQ_HANDLED)
		pm_runtime_mark_last_busy(dev->dev);
	return ret;
}

/**
 * amdgpu_msi_ok - asic specific msi checks
 *
 * @adev: amdgpu device pointer
 *
 * Handles asic specific MSI checks to determine if
 * MSIs should be enabled on a particular chip (all asics).
 * Returns true if MSIs should be enabled, false if MSIs
 * should not be enabled.
 */
static bool amdgpu_msi_ok(struct amdgpu_device *adev)
{
	/* force MSI on */
	if (amdgpu_msi == 1)
		return true;
	else if (amdgpu_msi == 0)
		return false;

	return true;
}

/**
 * amdgpu_irq_init - init driver interrupt info
 *
 * @adev: amdgpu device pointer
 *
 * Sets up the work irq handlers, vblank init, MSIs, etc. (all asics).
 * Returns 0 for success, error for failure.
 */
int amdgpu_irq_init(struct amdgpu_device *adev)
{
	int r = 0;

	spin_lock_init(&adev->irq.lock);
	r = drm_vblank_init(adev->ddev, adev->mode_info.num_crtc);
	if (r) {
		return r;
	}
	/* enable msi */
	adev->irq.msi_enabled = false;

	if (amdgpu_msi_ok(adev)) {
		int ret = pci_enable_msi(adev->pdev);
		if (!ret) {
			adev->irq.msi_enabled = true;
			dev_info(adev->dev, "amdgpu: using MSI.\n");
		}
	}

	INIT_WORK(&adev->hotplug_work, amdgpu_hotplug_work_func);
	INIT_WORK(&adev->reset_work, amdgpu_irq_reset_work_func);

	adev->irq.installed = true;
	r = drm_irq_install(adev->ddev, adev->ddev->pdev->irq);
	if (r) {
		adev->irq.installed = false;
		flush_work(&adev->hotplug_work);
		return r;
	}

	DRM_INFO("amdgpu: irq initialized.\n");
	return 0;
}

/**
 * amdgpu_irq_fini - tear down driver interrupt info
 *
 * @adev: amdgpu device pointer
 *
 * Tears down the work irq handlers, vblank handlers, MSIs, etc. (all asics).
 */
void amdgpu_irq_fini(struct amdgpu_device *adev)
{
	unsigned i;

	drm_vblank_cleanup(adev->ddev);
	if (adev->irq.installed) {
		drm_irq_uninstall(adev->ddev);
		adev->irq.installed = false;
		if (adev->irq.msi_enabled)
			pci_disable_msi(adev->pdev);
		flush_work(&adev->hotplug_work);
	}

	for (i = 0; i < AMDGPU_MAX_IRQ_SRC_ID; ++i) {
		struct amdgpu_irq_src *src = adev->irq.sources[i];

		if (!src)
			continue;

		kfree(src->enabled_types);
		src->enabled_types = NULL;
		if (src->data) {
			kfree(src->data);
			kfree(src);
			adev->irq.sources[i] = NULL;
		}
	}
}

/**
 * amdgpu_irq_add_id - register irq source
 *
 * @adev: amdgpu device pointer
 * @src_id: source id for this source
 * @source: irq source
 *
 */
int amdgpu_irq_add_id(struct amdgpu_device *adev, unsigned src_id,
		      struct amdgpu_irq_src *source)
{
	if (src_id >= AMDGPU_MAX_IRQ_SRC_ID)
		return -EINVAL;

	if (adev->irq.sources[src_id] != NULL)
		return -EINVAL;

	if (!source->funcs)
		return -EINVAL;

	if (source->num_types && !source->enabled_types) {
		atomic_t *types;

		types = kcalloc(source->num_types, sizeof(atomic_t),
				GFP_KERNEL);
		if (!types)
			return -ENOMEM;

		source->enabled_types = types;
	}

	adev->irq.sources[src_id] = source;

	return 0;
}

/**
 * amdgpu_irq_dispatch - dispatch irq to IP blocks
 *
 * @adev: amdgpu device pointer
 * @entry: interrupt vector
 *
 * Dispatches the irq to the different IP blocks
 */
void amdgpu_irq_dispatch(struct amdgpu_device *adev,
			 struct amdgpu_iv_entry *entry)
{
	unsigned src_id = entry->src_id;
	struct amdgpu_irq_src *src;
	int r;

	if (src_id >= AMDGPU_MAX_IRQ_SRC_ID) {
		DRM_DEBUG("Invalid src_id in IV: %d\n", src_id);
		return;
	}

	if (adev->irq.virq[src_id]) {
		generic_handle_irq(irq_find_mapping(adev->irq.domain, src_id));
	} else {
		src = adev->irq.sources[src_id];
		if (!src) {
			DRM_DEBUG("Unhandled interrupt src_id: %d\n", src_id);
			return;
		}

		r = src->funcs->process(adev, src, entry);
		if (r)
			DRM_ERROR("error processing interrupt (%d)\n", r);
	}
}

/**
 * amdgpu_irq_update - update hw interrupt state
 *
 * @adev: amdgpu device pointer
 * @src: interrupt src you want to enable
 * @type: type of interrupt you want to update
 *
 * Updates the interrupt state for a specific src (all asics).
 */
int amdgpu_irq_update(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *src, unsigned type)
{
	unsigned long irqflags;
	enum amdgpu_interrupt_state state;
	int r;

	spin_lock_irqsave(&adev->irq.lock, irqflags);

	/* we need to determine after taking the lock, otherwise
	   we might disable just enabled interrupts again */
	if (amdgpu_irq_enabled(adev, src, type))
		state = AMDGPU_IRQ_STATE_ENABLE;
	else
		state = AMDGPU_IRQ_STATE_DISABLE;

	r = src->funcs->set(adev, src, type, state);
	spin_unlock_irqrestore(&adev->irq.lock, irqflags);
	return r;
}

/**
 * amdgpu_irq_get - enable interrupt
 *
 * @adev: amdgpu device pointer
 * @src: interrupt src you want to enable
 * @type: type of interrupt you want to enable
 *
 * Enables the interrupt type for a specific src (all asics).
 */
int amdgpu_irq_get(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		   unsigned type)
{
	if (!adev->ddev->irq_enabled)
		return -ENOENT;

	if (type >= src->num_types)
		return -EINVAL;

	if (!src->enabled_types || !src->funcs->set)
		return -EINVAL;

	if (atomic_inc_return(&src->enabled_types[type]) == 1)
		return amdgpu_irq_update(adev, src, type);

	return 0;
}

bool amdgpu_irq_get_delayed(struct amdgpu_device *adev,
			struct amdgpu_irq_src *src,
			unsigned type)
{
	if ((type >= src->num_types) || !src->enabled_types)
		return false;
	return atomic_inc_return(&src->enabled_types[type]) == 1;
}

/**
 * amdgpu_irq_put - disable interrupt
 *
 * @adev: amdgpu device pointer
 * @src: interrupt src you want to disable
 * @type: type of interrupt you want to disable
 *
 * Disables the interrupt type for a specific src (all asics).
 */
int amdgpu_irq_put(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		   unsigned type)
{
	if (!adev->ddev->irq_enabled)
		return -ENOENT;

	if (type >= src->num_types)
		return -EINVAL;

	if (!src->enabled_types || !src->funcs->set)
		return -EINVAL;

	if (atomic_dec_and_test(&src->enabled_types[type]))
		return amdgpu_irq_update(adev, src, type);

	return 0;
}

/**
 * amdgpu_irq_enabled - test if irq is enabled or not
 *
 * @adev: amdgpu device pointer
 * @idx: interrupt src you want to test
 *
 * Tests if the given interrupt source is enabled or not
 */
bool amdgpu_irq_enabled(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
			unsigned type)
{
	if (!adev->ddev->irq_enabled)
		return false;

	if (type >= src->num_types)
		return false;

	if (!src->enabled_types || !src->funcs->set)
		return false;

	return !!atomic_read(&src->enabled_types[type]);
}

/* gen irq */
static void amdgpu_irq_mask(struct irq_data *irqd)
{
	/* XXX */
}

static void amdgpu_irq_unmask(struct irq_data *irqd)
{
	/* XXX */
}

static struct irq_chip amdgpu_irq_chip = {
	.name = "amdgpu-ih",
	.irq_mask = amdgpu_irq_mask,
	.irq_unmask = amdgpu_irq_unmask,
};

static int amdgpu_irqdomain_map(struct irq_domain *d,
				unsigned int irq, irq_hw_number_t hwirq)
{
	if (hwirq >= AMDGPU_MAX_IRQ_SRC_ID)
		return -EPERM;

	irq_set_chip_and_handler(irq,
				 &amdgpu_irq_chip, handle_simple_irq);
	return 0;
}

static struct irq_domain_ops amdgpu_hw_irqdomain_ops = {
	.map = amdgpu_irqdomain_map,
};

/**
 * amdgpu_irq_add_domain - create a linear irq domain
 *
 * @adev: amdgpu device pointer
 *
 * Create an irq domain for GPU interrupt sources
 * that may be driven by another driver (e.g., ACP).
 */
int amdgpu_irq_add_domain(struct amdgpu_device *adev)
{
	adev->irq.domain = irq_domain_add_linear(NULL, AMDGPU_MAX_IRQ_SRC_ID,
						 &amdgpu_hw_irqdomain_ops, adev);
	if (!adev->irq.domain) {
		DRM_ERROR("GPU irq add domain failed\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * amdgpu_irq_remove_domain - remove the irq domain
 *
 * @adev: amdgpu device pointer
 *
 * Remove the irq domain for GPU interrupt sources
 * that may be driven by another driver (e.g., ACP).
 */
void amdgpu_irq_remove_domain(struct amdgpu_device *adev)
{
	if (adev->irq.domain) {
		irq_domain_remove(adev->irq.domain);
		adev->irq.domain = NULL;
	}
}

/**
 * amdgpu_irq_create_mapping - create a mapping between a domain irq and a
 *                             Linux irq
 *
 * @adev: amdgpu device pointer
 * @src_id: IH source id
 *
 * Create a mapping between a domain irq (GPU IH src id) and a Linux irq
 * Use this for components that generate a GPU interrupt, but are driven
 * by a different driver (e.g., ACP).
 * Returns the Linux irq.
 */
unsigned amdgpu_irq_create_mapping(struct amdgpu_device *adev, unsigned src_id)
{
	adev->irq.virq[src_id] = irq_create_mapping(adev->irq.domain, src_id);

	return adev->irq.virq[src_id];
}
