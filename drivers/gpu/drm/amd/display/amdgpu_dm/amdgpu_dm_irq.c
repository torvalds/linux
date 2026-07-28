// SPDX-License-Identifier: MIT
/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "dm_services_types.h"
#include "dc.h"
#include "dc/dc_dmub_srv.h"
#include "dc/dc_stat.h"

#include "amdgpu.h"
#include "amdgpu_display.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_kunit_helpers.h"
#include "amdgpu_dm_crtc.h"
#include "amdgpu_dm_hdcp.h"
#include "amdgpu_dm_mst_types.h"
#include "amdgpu_dm_dmub.h"
#include "amdgpu_dm_trace.h"
#include "link/protocols/link_dpcd.h"
#include "link_service_types.h"
#include "ivsrcid/ivsrcid_vislands30.h"
#include "ivsrcid/dcn/irqsrcs_dcn_1_0.h"
#include "modules/inc/mod_freesync.h"
#include <drm/drm_vblank.h>

/**
 * DOC: overview
 *
 * DM provides another layer of IRQ management on top of what the base driver
 * already provides. This is something that could be cleaned up, and is a
 * future TODO item.
 *
 * The base driver provides IRQ source registration with DRM, handler
 * registration into the base driver's IRQ table, and a handler callback
 * amdgpu_irq_handler(), with which DRM calls on interrupts. This generic
 * handler looks up the IRQ table, and calls the respective
 * &amdgpu_irq_src_funcs.process hookups.
 *
 * What DM provides on top are two IRQ tables specifically for top-half and
 * bottom-half IRQ handling, with the bottom-half implementing workqueues:
 *
 * - &amdgpu_display_manager.irq_handler_list_high_tab
 * - &amdgpu_display_manager.irq_handler_list_low_tab
 *
 * They override the base driver's IRQ table, and the effect can be seen
 * in the hooks that DM provides for &amdgpu_irq_src_funcs.process. They
 * are all set to the DM generic handler amdgpu_dm_irq_handler(), which looks up
 * DM's IRQ tables. However, in order for base driver to recognize this hook, DM
 * still needs to register the IRQ with the base driver. See
 * amdgpu_dm_dce110_register_irq_handlers() and
 * amdgpu_dm_dcn10_register_irq_handlers().
 *
 * To expose DC's hardware interrupt toggle to the base driver, DM implements
 * &amdgpu_irq_src_funcs.set hooks. Base driver calls it through
 * amdgpu_irq_update() to enable or disable the interrupt.
 */

/******************************************************************************
 * Private declarations.
 *****************************************************************************/

/**
 * struct amdgpu_dm_irq_handler_data - Data for DM interrupt handlers.
 *
 * @list: Linked list entry referencing the next/previous handler
 * @handler: Handler function
 * @handler_arg: Argument passed to the handler when triggered
 * @dm: DM which this handler belongs to
 * @irq_source: DC interrupt source that this handler is registered for
 * @work: work struct
 */
struct amdgpu_dm_irq_handler_data {
	struct list_head list;
	interrupt_handler handler;
	void *handler_arg;

	struct amdgpu_display_manager *dm;
	/* DAL irq source which registered for this interrupt. */
	enum dc_irq_source irq_source;
	struct work_struct work;
};

#define DM_IRQ_TABLE_LOCK(adev, flags) \
	spin_lock_irqsave(&adev->dm.irq_handler_list_table_lock, flags)

#define DM_IRQ_TABLE_UNLOCK(adev, flags) \
	spin_unlock_irqrestore(&adev->dm.irq_handler_list_table_lock, flags)

/******************************************************************************
 * Private functions.
 *****************************************************************************/

static void init_handler_common_data(struct amdgpu_dm_irq_handler_data *hcd,
				     void (*ih)(void *),
				     void *args,
				     struct amdgpu_display_manager *dm)
{
	hcd->handler = ih;
	hcd->handler_arg = args;
	hcd->dm = dm;
}

/**
 * dm_irq_work_func() - Handle an IRQ outside of the interrupt handler proper.
 *
 * @work: work struct
 */
static void dm_irq_work_func(struct work_struct *work)
{
	struct amdgpu_dm_irq_handler_data *handler_data =
		container_of(work, struct amdgpu_dm_irq_handler_data, work);

	handler_data->handler(handler_data->handler_arg);

	/* Call a DAL subcomponent which registered for interrupt notification
	 * at INTERRUPT_LOW_IRQ_CONTEXT.
	 * (The most common use is HPD interrupt)
	 */
}

/*
 * Remove a handler and return a pointer to handler list from which the
 * handler was removed.
 */
static struct list_head *remove_irq_handler(struct amdgpu_device *adev,
					    void *ih,
					    const struct dc_interrupt_params *int_params)
{
	struct list_head *hnd_list;
	struct list_head *entry, *tmp;
	struct amdgpu_dm_irq_handler_data *handler;
	unsigned long irq_table_flags;
	bool handler_removed = false;
	enum dc_irq_source irq_source;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	irq_source = int_params->irq_source;

	switch (int_params->int_context) {
	case INTERRUPT_HIGH_IRQ_CONTEXT:
		hnd_list = &adev->dm.irq_handler_list_high_tab[irq_source];
		break;
	case INTERRUPT_LOW_IRQ_CONTEXT:
	default:
		hnd_list = &adev->dm.irq_handler_list_low_tab[irq_source];
		break;
	}

	list_for_each_safe(entry, tmp, hnd_list) {

		handler = list_entry(entry, struct amdgpu_dm_irq_handler_data,
				     list);

		if (handler == NULL)
			continue;

		if (ih == handler->handler) {
			/* Found our handler. Remove it from the list. */
			list_del(&handler->list);
			handler_removed = true;
			break;
		}
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	if (!handler_removed) {
		/* Not necessarily an error - caller may not
		 * know the context.
		 */
		return NULL;
	}

	if (int_params->int_context == INTERRUPT_LOW_IRQ_CONTEXT)
		cancel_work_sync(&handler->work);

	kfree(handler);

	DRM_DEBUG_KMS(
	"DM_IRQ: removed irq handler: %p for: dal_src=%d, irq context=%d\n",
		ih, int_params->irq_source, int_params->int_context);

	return hnd_list;
}

static bool
validate_irq_registration_params(struct dc_interrupt_params *int_params,
				 void (*ih)(void *))
{
	if (NULL == int_params || NULL == ih) {
		DRM_ERROR("DM_IRQ: invalid input!\n");
		return false;
	}

	if (int_params->int_context >= INTERRUPT_CONTEXT_NUMBER) {
		DRM_ERROR("DM_IRQ: invalid context: %d!\n",
				int_params->int_context);
		return false;
	}

	if (!DAL_VALID_IRQ_SRC_NUM(int_params->irq_source)) {
		DRM_ERROR("DM_IRQ: invalid irq_source: %d!\n",
				int_params->irq_source);
		return false;
	}

	return true;
}

static bool validate_irq_unregistration_params(enum dc_irq_source irq_source,
					       irq_handler_idx handler_idx)
{
	if (handler_idx == DAL_INVALID_IRQ_HANDLER_IDX) {
		DRM_ERROR("DM_IRQ: invalid handler_idx==NULL!\n");
		return false;
	}

	if (!DAL_VALID_IRQ_SRC_NUM(irq_source)) {
		DRM_ERROR("DM_IRQ: invalid irq_source:%d!\n", irq_source);
		return false;
	}

	return true;
}
/******************************************************************************
 * Public functions.
 *
 * Note: caller is responsible for input validation.
 *****************************************************************************/

/**
 * amdgpu_dm_irq_register_interrupt() - Register a handler within DM.
 * @adev: The base driver device containing the DM device.
 * @int_params: Interrupt parameters containing the source, and handler context
 * @ih: Function pointer to the interrupt handler to register
 * @handler_args: Arguments passed to the handler when the interrupt occurs
 *
 * Register an interrupt handler for the given IRQ source, under the given
 * context. The context can either be high or low. High context handlers are
 * executed directly within ISR context, while low context is executed within a
 * workqueue, thereby allowing operations that sleep.
 *
 * Registered handlers are called in a FIFO manner, i.e. the most recently
 * registered handler will be called first.
 *
 * Return: Handler data &struct amdgpu_dm_irq_handler_data containing the IRQ
 *         source, handler function, and args
 */
void *amdgpu_dm_irq_register_interrupt(struct amdgpu_device *adev,
				       struct dc_interrupt_params *int_params,
				       void (*ih)(void *),
				       void *handler_args)
{
	struct list_head *hnd_list;
	struct amdgpu_dm_irq_handler_data *handler_data;
	unsigned long irq_table_flags;
	enum dc_irq_source irq_source;

	if (!validate_irq_registration_params(int_params, ih))
		return DAL_INVALID_IRQ_HANDLER_IDX;

	handler_data = kzalloc_obj(*handler_data);
	if (!handler_data) {
		DRM_ERROR("DM_IRQ: failed to allocate irq handler!\n");
		return DAL_INVALID_IRQ_HANDLER_IDX;
	}

	init_handler_common_data(handler_data, ih, handler_args, &adev->dm);

	irq_source = int_params->irq_source;

	handler_data->irq_source = irq_source;

	/* Lock the list, add the handler. */
	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	switch (int_params->int_context) {
	case INTERRUPT_HIGH_IRQ_CONTEXT:
		hnd_list = &adev->dm.irq_handler_list_high_tab[irq_source];
		break;
	case INTERRUPT_LOW_IRQ_CONTEXT:
	default:
		hnd_list = &adev->dm.irq_handler_list_low_tab[irq_source];
		INIT_WORK(&handler_data->work, dm_irq_work_func);
		break;
	}

	list_add_tail(&handler_data->list, hnd_list);

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	/* This pointer will be stored by code which requested interrupt
	 * registration.
	 * The same pointer will be needed in order to unregister the
	 * interrupt.
	 */

	DRM_DEBUG_KMS(
		"DM_IRQ: added irq handler: %p for: dal_src=%d, irq context=%d\n",
		handler_data,
		irq_source,
		int_params->int_context);

	return handler_data;
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_register_interrupt);

/**
 * amdgpu_dm_irq_unregister_interrupt() - Remove a handler from the DM IRQ table
 * @adev: The base driver device containing the DM device
 * @irq_source: IRQ source to remove the given handler from
 * @ih: Function pointer to the interrupt handler to unregister
 *
 * Go through both low and high context IRQ tables, and find the given handler
 * for the given irq source. If found, remove it. Otherwise, do nothing.
 */
void amdgpu_dm_irq_unregister_interrupt(struct amdgpu_device *adev,
					enum dc_irq_source irq_source,
					void *ih)
{
	struct list_head *handler_list;
	struct dc_interrupt_params int_params;
	int i;

	if (!validate_irq_unregistration_params(irq_source, ih))
		return;

	memset(&int_params, 0, sizeof(int_params));

	int_params.irq_source = irq_source;

	for (i = 0; i < INTERRUPT_CONTEXT_NUMBER; i++) {

		int_params.int_context = i;

		handler_list = remove_irq_handler(adev, ih, &int_params);

		if (handler_list != NULL)
			break;
	}

	if (handler_list == NULL) {
		/* If we got here, it means we searched all irq contexts
		 * for this irq source, but the handler was not found.
		 */
		DRM_ERROR(
		"DM_IRQ: failed to find irq handler:%p for irq_source:%d!\n",
			ih, irq_source);
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_unregister_interrupt);

/**
 * amdgpu_dm_irq_init() - Initialize DM IRQ management
 * @adev:  The base driver device containing the DM device
 *
 * Initialize DM's high and low context IRQ tables.
 *
 * The N by M table contains N IRQ sources, with M
 * &struct amdgpu_dm_irq_handler_data hooked together in a linked list. The
 * list_heads are initialized here. When an interrupt n is triggered, all m
 * handlers are called in sequence, FIFO according to registration order.
 *
 * The low context table requires special steps to initialize, since handlers
 * will be deferred to a workqueue. See &struct irq_list_head.
 */
int amdgpu_dm_irq_init(struct amdgpu_device *adev)
{
	int src;
	struct list_head *lh;

	DRM_DEBUG_KMS("DM_IRQ\n");

	spin_lock_init(&adev->dm.irq_handler_list_table_lock);

	adev->dm.irq_wq = alloc_workqueue("amdgpu_dm_irq",
		WQ_UNBOUND | WQ_HIGHPRI, 0);

	if (!adev->dm.irq_wq)
		return -ENOMEM;

	adev->dm.vmin_vmax_wq = alloc_workqueue("amdgpu_dm_vmin_vmax",
		WQ_UNBOUND, 0);

	if (!adev->dm.vmin_vmax_wq) {
		destroy_workqueue(adev->dm.irq_wq);
		adev->dm.irq_wq = NULL;
		return -ENOMEM;
	}

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++) {
		/* low context handler list init */
		lh = &adev->dm.irq_handler_list_low_tab[src];
		INIT_LIST_HEAD(lh);
		/* high context handler init */
		INIT_LIST_HEAD(&adev->dm.irq_handler_list_high_tab[src]);
	}

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_init);

/**
 * amdgpu_dm_irq_fini() - Tear down DM IRQ management
 * @adev: The base driver device containing the DM device
 *
 * Removes all handlers from the IRQ tables under the spinlock, cancels
 * pending work items, and deallocates all handler data.
 */
void amdgpu_dm_irq_fini(struct amdgpu_device *adev)
{
	int src;
	LIST_HEAD(low_handlers);
	LIST_HEAD(high_handlers);
	struct list_head *entry, *tmp;
	struct amdgpu_dm_irq_handler_data *handler;
	unsigned long irq_table_flags;

	DRM_DEBUG_KMS("DM_IRQ: releasing resources.\n");

	for (src = 0; src < DAL_IRQ_SOURCES_NUMBER; src++) {
		DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

		/*
		 * Move all handlers from the low and high context tables to
		 * temporary lists under the lock. This prevents the ISR from
		 * finding them while we process them outside the lock.
		 */
		list_splice_init(&adev->dm.irq_handler_list_low_tab[src],
				 &low_handlers);
		list_splice_init(&adev->dm.irq_handler_list_high_tab[src],
				 &high_handlers);

		DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

		/*
		 * Cancel all pending work for the low-context handlers
		 * outside the lock. cancel_work_sync() may sleep and waits
		 * until any running work completes, preventing UAF.
		 */
		list_for_each_safe(entry, tmp, &low_handlers) {
			handler = list_entry(entry,
					    struct amdgpu_dm_irq_handler_data,
					    list);
			cancel_work_sync(&handler->work);
		}

		/*
		 * High-context handlers are executed synchronously within ISR
		 * context (see amdgpu_dm_irq_immediate_work()) and have no
		 * work_struct, so there is no pending work to cancel here.
		 * They will be freed along with low_handlers after the loop.
		 */
	}

	/* Deallocate all handlers. */
	list_for_each_safe(entry, tmp, &low_handlers) {
		handler = list_entry(entry,
				     struct amdgpu_dm_irq_handler_data,
				     list);
		list_del(&handler->list);
		kfree(handler);
	}

	list_for_each_safe(entry, tmp, &high_handlers) {
		handler = list_entry(entry,
				     struct amdgpu_dm_irq_handler_data,
				     list);
		list_del(&handler->list);
		kfree(handler);
	}

	if (adev->dm.vmin_vmax_wq) {
		destroy_workqueue(adev->dm.vmin_vmax_wq);
		adev->dm.vmin_vmax_wq = NULL;
	}

	if (adev->dm.irq_wq) {
		destroy_workqueue(adev->dm.irq_wq);
		adev->dm.irq_wq = NULL;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_fini);

void amdgpu_dm_irq_suspend(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	int src;
	struct list_head *hnd_list_l;
	unsigned long irq_table_flags;
	struct list_head *entry, *tmp;
	struct amdgpu_dm_irq_handler_data *handler;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	DRM_DEBUG_KMS("DM_IRQ: suspend\n");

	/**
	 * Disable HW interrupt  for HPD and HPDRX only since FLIP and VBLANK
	 * will be disabled from manage_dm_interrupts on disable CRTC.
	 *
	 * Disable the HW interrupt first, then flush any pending work. Since
	 * the HW interrupt is disabled under the lock, no new IRQ can be
	 * generated after the disable completes. Any work already queued by an
	 * in-flight ISR will be flushed below.
	 */
	for (src = DC_IRQ_SOURCE_HPD1; src <= DC_IRQ_SOURCE_HPD6RX; src++) {
		hnd_list_l = &adev->dm.irq_handler_list_low_tab[src];
		dc_interrupt_set(adev->dm.dc, src, false);

		DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

		if (!list_empty(hnd_list_l)) {
			list_for_each_safe(entry, tmp, hnd_list_l) {
				handler = list_entry(
					entry,
					struct amdgpu_dm_irq_handler_data,
					list);
				flush_work(&handler->work);
			}
		}
		DM_IRQ_TABLE_LOCK(adev, irq_table_flags);
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_disable(dev);
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_suspend);

void amdgpu_dm_irq_resume_early(struct amdgpu_device *adev)
{
	int src;
	struct list_head *hnd_list_h, *hnd_list_l;
	unsigned long irq_table_flags;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	drm_dbg(adev_to_drm(adev), "DM_IRQ: early resume\n");

	/* re-enable short pulse interrupts HW interrupt */
	for (src = DC_IRQ_SOURCE_HPD1RX; src <= DC_IRQ_SOURCE_HPD6RX; src++) {
		hnd_list_l = &adev->dm.irq_handler_list_low_tab[src];
		hnd_list_h = &adev->dm.irq_handler_list_high_tab[src];
		if (!list_empty(hnd_list_l) || !list_empty(hnd_list_h))
			dc_interrupt_set(adev->dm.dc, src, true);
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_resume_early);

void amdgpu_dm_irq_resume_late(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	int src;
	struct list_head *hnd_list_h, *hnd_list_l;
	unsigned long irq_table_flags;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	drm_dbg(adev_to_drm(adev), "DM_IRQ: resume\n");

	/**
	 * Renable HW interrupt  for HPD and only since FLIP and VBLANK
	 * will be enabled from manage_dm_interrupts on enable CRTC.
	 */
	for (src = DC_IRQ_SOURCE_HPD1; src <= DC_IRQ_SOURCE_HPD6; src++) {
		hnd_list_l = &adev->dm.irq_handler_list_low_tab[src];
		hnd_list_h = &adev->dm.irq_handler_list_high_tab[src];
		if (!list_empty(hnd_list_l) || !list_empty(hnd_list_h))
			dc_interrupt_set(adev->dm.dc, src, true);
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);

	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_enable(dev);
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_resume_late);

/*
 * amdgpu_dm_irq_schedule_work - schedule all work items registered for the
 * "irq_source".
 */
STATIC_IFN_KUNIT void amdgpu_dm_irq_schedule_work(struct amdgpu_device *adev,
					enum dc_irq_source irq_source)
{
	struct  list_head *handler_list = &adev->dm.irq_handler_list_low_tab[irq_source];
	struct  amdgpu_dm_irq_handler_data *handler_data;
	bool    work_queued = false;
	unsigned long irq_table_flags;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	if (list_empty(handler_list))
		goto out_unlock;

	list_for_each_entry(handler_data, handler_list, list) {
		if (queue_work(adev->dm.irq_wq, &handler_data->work)) {
			work_queued = true;
			break;
		}
	}

	if (!work_queued) {
		struct  amdgpu_dm_irq_handler_data *handler_data_add;
		/*get the amdgpu_dm_irq_handler_data of first item pointed by handler_list*/
		handler_data = container_of(handler_list->next, struct amdgpu_dm_irq_handler_data, list);

		/*allocate a new amdgpu_dm_irq_handler_data*/
		handler_data_add = kzalloc_obj(*handler_data, GFP_ATOMIC);
		if (!handler_data_add) {
			DRM_ERROR("DM_IRQ: failed to allocate irq handler!\n");
			goto out_unlock;
		}

		/*copy new amdgpu_dm_irq_handler_data members from handler_data*/
		handler_data_add->handler       = handler_data->handler;
		handler_data_add->handler_arg   = handler_data->handler_arg;
		handler_data_add->dm            = handler_data->dm;
		handler_data_add->irq_source    = irq_source;

		list_add_tail(&handler_data_add->list, handler_list);

		INIT_WORK(&handler_data_add->work, dm_irq_work_func);

		if (queue_work(adev->dm.irq_wq, &handler_data_add->work))
			DRM_DEBUG("Queued work for handling interrupt from "
				  "display for IRQ source %d\n",
				  irq_source);
		else
			DRM_ERROR("Failed to queue work for handling interrupt "
				  "from display for IRQ source %d\n",
				  irq_source);
	}

out_unlock:
	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_schedule_work);

/*
 * amdgpu_dm_irq_immediate_work
 * Callback high irq work immediately, don't send to work queue
 */
STATIC_IFN_KUNIT void amdgpu_dm_irq_immediate_work(struct amdgpu_device *adev,
					 enum dc_irq_source irq_source)
{
	struct amdgpu_dm_irq_handler_data *handler_data;
	unsigned long irq_table_flags;

	DM_IRQ_TABLE_LOCK(adev, irq_table_flags);

	list_for_each_entry(handler_data,
			    &adev->dm.irq_handler_list_high_tab[irq_source],
			    list) {
		/* Call a subcomponent which registered for immediate
		 * interrupt notification
		 */
		handler_data->handler(handler_data->handler_arg);
	}

	DM_IRQ_TABLE_UNLOCK(adev, irq_table_flags);
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_immediate_work);

/**
 * amdgpu_dm_irq_handler - Generic DM IRQ handler
 * @adev: amdgpu base driver device containing the DM device
 * @source: Unused
 * @entry: Data about the triggered interrupt
 *
 * Calls all registered high irq work immediately, and schedules work for low
 * irq. The DM IRQ table is used to find the corresponding handlers.
 */
STATIC_IFN_KUNIT int amdgpu_dm_irq_handler(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{

	enum dc_irq_source src =
		dc_interrupt_to_irq_source(
			adev->dm.dc,
			entry->src_id,
			entry->src_data[0]);

	dc_interrupt_ack(adev->dm.dc, src);

	/* Call high irq work immediately */
	amdgpu_dm_irq_immediate_work(adev, src);
	/*Schedule low_irq work */
	amdgpu_dm_irq_schedule_work(adev, src);

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_irq_handler);

STATIC_IFN_KUNIT enum dc_irq_source amdgpu_dm_hpd_to_dal_irq_source(unsigned int type)
{
	switch (type) {
	case AMDGPU_HPD_1:
		return DC_IRQ_SOURCE_HPD1;
	case AMDGPU_HPD_2:
		return DC_IRQ_SOURCE_HPD2;
	case AMDGPU_HPD_3:
		return DC_IRQ_SOURCE_HPD3;
	case AMDGPU_HPD_4:
		return DC_IRQ_SOURCE_HPD4;
	case AMDGPU_HPD_5:
		return DC_IRQ_SOURCE_HPD5;
	case AMDGPU_HPD_6:
		return DC_IRQ_SOURCE_HPD6;
	default:
		return DC_IRQ_SOURCE_INVALID;
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_hpd_to_dal_irq_source);

STATIC_IFN_KUNIT int amdgpu_dm_set_hpd_irq_state(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *source,
				       unsigned int type,
				       enum amdgpu_interrupt_state state)
{
	enum dc_irq_source src = amdgpu_dm_hpd_to_dal_irq_source(type);
	bool st = (state == AMDGPU_IRQ_STATE_ENABLE);

	dc_interrupt_set(adev->dm.dc, src, st);
	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_hpd_irq_state);

static inline int dm_irq_state(struct amdgpu_device *adev,
			       struct amdgpu_irq_src *source,
			       unsigned int crtc_id,
			       enum amdgpu_interrupt_state state,
			       const enum irq_type dal_irq_type,
			       const char *func)
{
	bool st;
	enum dc_irq_source irq_source;
	struct dc *dc = adev->dm.dc;
	struct amdgpu_crtc *acrtc = adev->mode_info.crtcs[crtc_id];

	if (!acrtc) {
		DRM_ERROR(
			"%s: crtc is NULL at id :%d\n",
			func,
			crtc_id);
		return 0;
	}

	if (acrtc->otg_inst == -1)
		return 0;

	irq_source = dal_irq_type + acrtc->otg_inst;

	st = (state == AMDGPU_IRQ_STATE_ENABLE);

	if (dc && dc->caps.ips_support && dc->idle_optimizations_allowed)
		dc_allow_idle_optimizations(dc, false);

	dc_interrupt_set(adev->dm.dc, irq_source, st);
	return 0;
}

STATIC_IFN_KUNIT int amdgpu_dm_set_pflip_irq_state(struct amdgpu_device *adev,
					 struct amdgpu_irq_src *source,
					 unsigned int crtc_id,
					 enum amdgpu_interrupt_state state)
{
	return dm_irq_state(
		adev,
		source,
		crtc_id,
		state,
		IRQ_TYPE_PFLIP,
		__func__);
}
EXPORT_IF_KUNIT(amdgpu_dm_set_pflip_irq_state);

STATIC_IFN_KUNIT int amdgpu_dm_set_crtc_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int crtc_id,
					enum amdgpu_interrupt_state state)
{
	return dm_irq_state(
		adev,
		source,
		crtc_id,
		state,
		IRQ_TYPE_VBLANK,
		__func__);
}
EXPORT_IF_KUNIT(amdgpu_dm_set_crtc_irq_state);

STATIC_IFN_KUNIT int amdgpu_dm_set_vline0_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int crtc_id,
					enum amdgpu_interrupt_state state)
{
	return dm_irq_state(
		adev,
		source,
		crtc_id,
		state,
		IRQ_TYPE_VLINE0,
		__func__);
}
EXPORT_IF_KUNIT(amdgpu_dm_set_vline0_irq_state);

STATIC_IFN_KUNIT int amdgpu_dm_set_dmub_outbox_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int crtc_id,
					enum amdgpu_interrupt_state state)
{
	enum dc_irq_source irq_source = DC_IRQ_SOURCE_DMCUB_OUTBOX;
	bool st = (state == AMDGPU_IRQ_STATE_ENABLE);

	dc_interrupt_set(adev->dm.dc, irq_source, st);
	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_dmub_outbox_irq_state);

STATIC_IFN_KUNIT int amdgpu_dm_set_vupdate_irq_state(struct amdgpu_device *adev,
					   struct amdgpu_irq_src *source,
					   unsigned int crtc_id,
					   enum amdgpu_interrupt_state state)
{
	return dm_irq_state(
		adev,
		source,
		crtc_id,
		state,
		IRQ_TYPE_VUPDATE,
		__func__);
}
EXPORT_IF_KUNIT(amdgpu_dm_set_vupdate_irq_state);

STATIC_IFN_KUNIT int amdgpu_dm_set_dmub_trace_irq_state(struct amdgpu_device *adev,
					   struct amdgpu_irq_src *source,
					   unsigned int type,
					   enum amdgpu_interrupt_state state)
{
	enum dc_irq_source irq_source = DC_IRQ_SOURCE_DMCUB_OUTBOX0;
	bool st = (state == AMDGPU_IRQ_STATE_ENABLE);

	dc_interrupt_set(adev->dm.dc, irq_source, st);
	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_dmub_trace_irq_state);

static const struct amdgpu_irq_src_funcs dm_crtc_irq_funcs = {
	.set = amdgpu_dm_set_crtc_irq_state,
	.process = amdgpu_dm_irq_handler,
};

static const struct amdgpu_irq_src_funcs dm_vline0_irq_funcs = {
	.set = amdgpu_dm_set_vline0_irq_state,
	.process = amdgpu_dm_irq_handler,
};

static const struct amdgpu_irq_src_funcs dm_dmub_outbox_irq_funcs = {
	.set = amdgpu_dm_set_dmub_outbox_irq_state,
	.process = amdgpu_dm_irq_handler,
};

static const struct amdgpu_irq_src_funcs dm_vupdate_irq_funcs = {
	.set = amdgpu_dm_set_vupdate_irq_state,
	.process = amdgpu_dm_irq_handler,
};

static const struct amdgpu_irq_src_funcs dm_dmub_trace_irq_funcs = {
	.set = amdgpu_dm_set_dmub_trace_irq_state,
	.process = amdgpu_dm_irq_handler,
};

static const struct amdgpu_irq_src_funcs dm_pageflip_irq_funcs = {
	.set = amdgpu_dm_set_pflip_irq_state,
	.process = amdgpu_dm_irq_handler,
};

static const struct amdgpu_irq_src_funcs dm_hpd_irq_funcs = {
	.set = amdgpu_dm_set_hpd_irq_state,
	.process = amdgpu_dm_irq_handler,
};

void amdgpu_dm_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->crtc_irq.num_types = adev->mode_info.num_crtc;
	adev->crtc_irq.funcs = &dm_crtc_irq_funcs;

	adev->vline0_irq.num_types = adev->mode_info.num_crtc;
	adev->vline0_irq.funcs = &dm_vline0_irq_funcs;

	adev->dmub_outbox_irq.num_types = 1;
	adev->dmub_outbox_irq.funcs = &dm_dmub_outbox_irq_funcs;

	adev->vupdate_irq.num_types = adev->mode_info.num_crtc;
	adev->vupdate_irq.funcs = &dm_vupdate_irq_funcs;

	adev->dmub_trace_irq.num_types = 1;
	adev->dmub_trace_irq.funcs = &dm_dmub_trace_irq_funcs;

	adev->pageflip_irq.num_types = adev->mode_info.num_crtc;
	adev->pageflip_irq.funcs = &dm_pageflip_irq_funcs;

	adev->hpd_irq.num_types = adev->mode_info.num_hpd;
	adev->hpd_irq.funcs = &dm_hpd_irq_funcs;
}
EXPORT_IF_KUNIT(amdgpu_dm_set_irq_funcs);

void amdgpu_dm_outbox_init(struct amdgpu_device *adev)
{
	dc_interrupt_set(adev->dm.dc,
		DC_IRQ_SOURCE_DMCUB_OUTBOX,
		true);
}
EXPORT_IF_KUNIT(amdgpu_dm_outbox_init);

/**
 * amdgpu_dm_hpd_init - hpd setup callback.
 *
 * @adev: amdgpu_device pointer
 *
 * Setup the hpd pins used by the card (evergreen+).
 * Enable the pin, set the polarity, and enable the hpd interrupts.
 */
void amdgpu_dm_hpd_init(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	int irq_type;
	int i;
	bool use_polling = false;

	/* First, clear all hpd and hpdrx interrupts */
	for (i = DC_IRQ_SOURCE_HPD1; i <= DC_IRQ_SOURCE_HPD6RX; i++) {
		if (!dc_interrupt_set(adev->dm.dc, i, false))
			drm_err(dev, "Failed to clear hpd(rx) source=%d on init\n",
				i);
	}

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		struct amdgpu_dm_connector *amdgpu_dm_connector;
		const struct dc_link *dc_link;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		amdgpu_dm_connector = to_amdgpu_dm_connector(connector);
		dc_link = amdgpu_dm_connector->dc_link;
		if (!dc_link)
			continue;

		/*
		 * Analog connectors may be hot-plugged unlike other connector
		 * types that don't support HPD. Only poll analog connectors.
		 */
		use_polling |= dc_connector_supports_analog(dc_link->link_id.id);

		/*
		 * Get a base driver irq reference for hpd ints for the lifetime
		 * of dm. Note that only hpd interrupt types are registered with
		 * base driver; hpd_rx types aren't. IOW, amdgpu_irq_get/put on
		 * hpd_rx isn't available. DM currently controls hpd_rx
		 * explicitly with dc_interrupt_set()
		 */
		if (dc_link->irq_source_hpd != DC_IRQ_SOURCE_INVALID) {
			irq_type = dc_link->irq_source_hpd - DC_IRQ_SOURCE_HPD1;
			/*
			 * TODO: There's a mismatch between mode_info.num_hpd
			 * and what bios reports as the # of connectors with hpd
			 * sources. Since the # of hpd source types registered
			 * with base driver == mode_info.num_hpd, we have to
			 * fallback to dc_interrupt_set for the remaining types.
			 */
			if (irq_type < adev->mode_info.num_hpd) {
				if (amdgpu_irq_get(adev, &adev->hpd_irq, irq_type))
					drm_err(dev, "DM_IRQ: Failed get HPD for source=%d)!\n",
						dc_link->irq_source_hpd);
			} else {
				dc_interrupt_set(adev->dm.dc,
						 dc_link->irq_source_hpd,
						 true);
			}
		}

		if (dc_link->irq_source_hpd_rx != DC_IRQ_SOURCE_INVALID) {
			dc_interrupt_set(adev->dm.dc,
					dc_link->irq_source_hpd_rx,
					true);
		}
	}
	drm_connector_list_iter_end(&iter);

	if (use_polling)
		drm_kms_helper_poll_init(dev);
}
EXPORT_IF_KUNIT(amdgpu_dm_hpd_init);

/**
 * amdgpu_dm_hpd_fini - hpd tear down callback.
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down the hpd pins used by the card (evergreen+).
 * Disable the hpd interrupts.
 */
void amdgpu_dm_hpd_fini(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	int irq_type;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		struct amdgpu_dm_connector *amdgpu_dm_connector;
		const struct dc_link *dc_link;

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		amdgpu_dm_connector = to_amdgpu_dm_connector(connector);
		dc_link = amdgpu_dm_connector->dc_link;

		if (dc_link->irq_source_hpd != DC_IRQ_SOURCE_INVALID) {
			irq_type = dc_link->irq_source_hpd - DC_IRQ_SOURCE_HPD1;

			/* TODO: See same TODO in amdgpu_dm_hpd_init() */
			if (irq_type < adev->mode_info.num_hpd) {
				if (amdgpu_irq_put(adev, &adev->hpd_irq, irq_type))
					drm_err(dev, "DM_IRQ: Failed put HPD for source=%d!\n",
						dc_link->irq_source_hpd);
			} else {
				dc_interrupt_set(adev->dm.dc,
						 dc_link->irq_source_hpd,
						 false);
			}
		}

		if (dc_link->irq_source_hpd_rx != DC_IRQ_SOURCE_INVALID) {
			dc_interrupt_set(adev->dm.dc,
					dc_link->irq_source_hpd_rx,
					false);
		}
	}
	drm_connector_list_iter_end(&iter);

	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_fini(dev);
}
EXPORT_IF_KUNIT(amdgpu_dm_hpd_fini);

/* ========== HPD handling ========== */
static void force_connector_state(
	struct amdgpu_dm_connector *aconnector,
	enum drm_connector_force force_state)
{
	struct drm_connector *connector = &aconnector->base;

	mutex_lock(&connector->dev->mode_config.mutex);
	aconnector->base.force = force_state;
	mutex_unlock(&connector->dev->mode_config.mutex);

	mutex_lock(&aconnector->hpd_lock);
	drm_kms_helper_connector_hotplug_event(connector);
	mutex_unlock(&aconnector->hpd_lock);
}

STATIC_IFN_KUNIT void dm_handle_hpd_rx_offload_work(struct work_struct *work)
{
	struct hpd_rx_irq_offload_work *offload_work;
	struct amdgpu_dm_connector *aconnector;
	struct dc_link *dc_link;
	struct amdgpu_device *adev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	unsigned long flags;
	union test_response test_response;

	memset(&test_response, 0, sizeof(test_response));

	offload_work = container_of(work, struct hpd_rx_irq_offload_work, work);
	aconnector = offload_work->offload_wq->aconnector;
	adev = offload_work->adev;

	if (!aconnector) {
		drm_err(adev_to_drm(adev), "Can't retrieve aconnector in hpd_rx_irq_offload_work");
		goto skip;
	}

	dc_link = aconnector->dc_link;

	mutex_lock(&aconnector->hpd_lock);
	if (!dc_link_detect_connection_type(dc_link, &new_connection_type))
		drm_err(adev_to_drm(adev), "KMS: Failed to detect connector\n");
	mutex_unlock(&aconnector->hpd_lock);

	if (new_connection_type == dc_connection_none)
		goto skip;

	if (amdgpu_in_reset(adev))
		goto skip;

	if (offload_work->data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY ||
		offload_work->data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
		dm_handle_mst_sideband_msg_ready_event(&aconnector->mst_mgr, DOWN_OR_UP_MSG_RDY_EVENT);
		spin_lock_irqsave(&offload_work->offload_wq->offload_lock, flags);
		offload_work->offload_wq->is_handling_mst_msg_rdy_event = false;
		spin_unlock_irqrestore(&offload_work->offload_wq->offload_lock, flags);
		goto skip;
	}

	mutex_lock(&adev->dm.dc_lock);
	if (offload_work->data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		dc_link_dp_handle_automated_test(dc_link);

		if (aconnector->timing_changed) {
			/* force connector disconnect and reconnect */
			force_connector_state(aconnector, DRM_FORCE_OFF);
			msleep(100);
			force_connector_state(aconnector, DRM_FORCE_UNSPECIFIED);
		}

		test_response.bits.ACK = 1;

		core_link_write_dpcd(
		dc_link,
		DP_TEST_RESPONSE,
		&test_response.raw,
		sizeof(test_response));
	} else if ((dc_link->connector_signal != SIGNAL_TYPE_EDP) &&
			dc_link_check_link_loss_status(dc_link, &offload_work->data) &&
			dc_link_dp_allow_hpd_rx_irq(dc_link)) {
		/* offload_work->data is from handle_hpd_rx_irq->
		 * schedule_hpd_rx_offload_work.this is defer handle
		 * for hpd short pulse. upon here, link status may be
		 * changed, need get latest link status from dpcd
		 * registers. if link status is good, skip run link
		 * training again.
		 */
		union hpd_irq_data irq_data;

		memset(&irq_data, 0, sizeof(irq_data));

		/* before dc_link_dp_handle_link_loss, allow new link lost handle
		 * request be added to work queue if link lost at end of dc_link_
		 * dp_handle_link_loss
		 */
		spin_lock_irqsave(&offload_work->offload_wq->offload_lock, flags);
		offload_work->offload_wq->is_handling_link_loss = false;
		spin_unlock_irqrestore(&offload_work->offload_wq->offload_lock, flags);

		if ((dc_link_dp_read_hpd_rx_irq_data(dc_link, &irq_data) == DC_OK) &&
			dc_link_check_link_loss_status(dc_link, &irq_data))
			dc_link_dp_handle_link_loss(dc_link);
	}
	mutex_unlock(&adev->dm.dc_lock);

skip:
	kfree(offload_work);

}
EXPORT_IF_KUNIT(dm_handle_hpd_rx_offload_work);

struct hpd_rx_irq_offload_work_queue *amdgpu_dm_hpd_rx_irq_create_workqueue(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	int max_caps = dc->caps.max_links;
	int i = 0;
	struct hpd_rx_irq_offload_work_queue *hpd_rx_offload_wq = NULL;

	hpd_rx_offload_wq = kzalloc_objs(*hpd_rx_offload_wq, max_caps);

	if (!hpd_rx_offload_wq)
		return NULL;


	for (i = 0; i < max_caps; i++) {
		hpd_rx_offload_wq[i].wq =
				    create_singlethread_workqueue("amdgpu_dm_hpd_rx_offload_wq");

		if (hpd_rx_offload_wq[i].wq == NULL) {
			drm_err(adev_to_drm(adev), "create amdgpu_dm_hpd_rx_offload_wq fail!");
			goto out_err;
		}

		spin_lock_init(&hpd_rx_offload_wq[i].offload_lock);
	}

	return hpd_rx_offload_wq;

out_err:
	for (i = 0; i < max_caps; i++) {
		if (hpd_rx_offload_wq[i].wq)
			destroy_workqueue(hpd_rx_offload_wq[i].wq);
	}
	kfree(hpd_rx_offload_wq);
	return NULL;
}
EXPORT_IF_KUNIT(amdgpu_dm_hpd_rx_irq_create_workqueue);

void amdgpu_dm_hpd_rx_irq_work_suspend(struct amdgpu_display_manager *dm)
{
	int i;

	if (dm->hpd_rx_offload_wq) {
		for (i = 0; i < dm->dc->caps.max_links; i++)
			flush_workqueue(dm->hpd_rx_offload_wq[i].wq);
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_hpd_rx_irq_work_suspend);

STATIC_IFN_KUNIT bool are_sinks_equal(const struct dc_sink *sink1, const struct dc_sink *sink2)
{
	if (!sink1 || !sink2)
		return false;
	if (sink1->sink_signal != sink2->sink_signal)
		return false;

	if (sink1->dc_edid.length != sink2->dc_edid.length)
		return false;

	if (memcmp(sink1->dc_edid.raw_edid, sink2->dc_edid.raw_edid,
		   sink1->dc_edid.length) != 0)
		return false;
	return true;
}
EXPORT_IF_KUNIT(are_sinks_equal);


/**
 * DOC: amdgpu_dm_hdmi_hpd_debounce_work
 *
 * HDMI HPD debounce delay in milliseconds. When an HDMI display toggles HPD
 * (such as during power save transitions), this delay determines how long to
 * wait before processing the HPD event. This allows distinguishing between a
 * physical unplug (>hdmi_hpd_debounce_delay)
 * and a spontaneous RX HPD toggle (<hdmi_hpd_debounce_delay).
 *
 * If the toggle is less than this delay, the driver compares sink capabilities
 * and permits a hotplug event if they changed.
 *
 * The default value of 1500ms was chosen based on experimental testing with
 * various monitors that exhibit spontaneous HPD toggling behavior.
 */
void amdgpu_dm_hdmi_hpd_debounce_work(struct work_struct *work)
{
	struct amdgpu_dm_connector *aconnector =
		container_of(to_delayed_work(work), struct amdgpu_dm_connector,
			     hdmi_hpd_debounce_work);
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dc *dc = aconnector->dc_link->ctx->dc;
	bool fake_reconnect = false;
	bool reallow_idle = false;
	bool ret = false;

	guard(mutex)(&aconnector->hpd_lock);

	/* Re-detect the display */
	scoped_guard(mutex, &adev->dm.dc_lock) {
		if (dc->caps.ips_support && dc->ctx->dmub_srv->idle_allowed) {
			dc_allow_idle_optimizations(dc, false);
			reallow_idle = true;
		}
		ret = dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);
	}

	if (ret) {
		/* Apply workaround delay for certain panels */
		amdgpu_dm_apply_delay_after_dpcd_poweroff(adev, aconnector->dc_sink);
		/* Compare sinks to determine if this was a spontaneous HPD toggle */
		if (are_sinks_equal(aconnector->dc_link->local_sink, aconnector->hdmi_prev_sink)) {
			/*
			 * Sinks match - this was a spontaneous HDMI HPD toggle.
			 */
			drm_dbg_kms(dev, "HDMI HPD: Sink unchanged after debounce, internal re-enable\n");
			fake_reconnect = true;
		}

		/* Update connector state */
		amdgpu_dm_update_connector_after_detect(aconnector);

		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		/* Only notify OS if sink actually changed */
		if (!fake_reconnect && aconnector->base.force == DRM_FORCE_UNSPECIFIED)
			drm_kms_helper_hotplug_event(dev);
	}

	/* Release the cached sink reference */
	if (aconnector->hdmi_prev_sink) {
		dc_sink_release(aconnector->hdmi_prev_sink);
		aconnector->hdmi_prev_sink = NULL;
	}

	scoped_guard(mutex, &adev->dm.dc_lock) {
		if (reallow_idle && dc->caps.ips_support)
			dc_allow_idle_optimizations(dc, true);
	}
}
EXPORT_IF_KUNIT(amdgpu_dm_hdmi_hpd_debounce_work);

STATIC_IFN_KUNIT void handle_hpd_irq_helper(struct amdgpu_dm_connector *aconnector,
				  enum dc_detect_reason reason)
{
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct dm_connector_state *dm_con_state = to_dm_connector_state(connector->state);
	struct dc *dc = aconnector->dc_link->ctx->dc;
	bool ret = false;
	bool debounce_required = false;

	if (adev->dm.disable_hpd_irq)
		return;

	/*
	 * In case of failure or MST no need to update connector status or notify the OS
	 * since (for MST case) MST does this in its own context.
	 */
	guard(mutex)(&aconnector->hpd_lock);

	if (adev->dm.hdcp_workqueue) {
		hdcp_reset_display(adev->dm.hdcp_workqueue, aconnector->dc_link->link_index);
		dm_con_state->update_hdcp = true;
	}
	if (aconnector->fake_enable)
		aconnector->fake_enable = false;

	aconnector->timing_changed = false;

	if (!dc_link_detect_connection_type(aconnector->dc_link, &new_connection_type))
		drm_err(adev_to_drm(adev), "KMS: Failed to detect connector\n");

	/*
	 * Check for HDMI disconnect with debounce enabled.
	 */
	debounce_required = (aconnector->hdmi_hpd_debounce_delay_ms > 0 &&
			      dc_is_hdmi_signal(aconnector->dc_link->connector_signal) &&
			      new_connection_type == dc_connection_none &&
			      aconnector->dc_link->local_sink != NULL);

	if (aconnector->base.force && new_connection_type == dc_connection_none) {
		amdgpu_dm_emulated_link_detect(aconnector->dc_link);

		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		if (aconnector->base.force == DRM_FORCE_UNSPECIFIED ||
		    reason == DETECT_REASON_HPDRX)
			drm_kms_helper_connector_hotplug_event(connector);
	} else if (debounce_required) {
		/*
		 * HDMI disconnect detected - schedule delayed work instead of
		 * processing immediately. This allows us to coalesce spurious
		 * HDMI signals from physical unplugs.
		 */
		drm_dbg_kms(dev, "HDMI HPD: Disconnect detected, scheduling debounce work (%u ms)\n",
			    aconnector->hdmi_hpd_debounce_delay_ms);

		/* Cache the current sink for later comparison */
		if (aconnector->hdmi_prev_sink)
			dc_sink_release(aconnector->hdmi_prev_sink);
		aconnector->hdmi_prev_sink = aconnector->dc_link->local_sink;
		if (aconnector->hdmi_prev_sink)
			dc_sink_retain(aconnector->hdmi_prev_sink);

		/* Schedule delayed detection. */
		if (mod_delayed_work(system_percpu_wq,
				 &aconnector->hdmi_hpd_debounce_work,
				 msecs_to_jiffies(aconnector->hdmi_hpd_debounce_delay_ms)))
			drm_dbg_kms(dev, "HDMI HPD: Re-scheduled debounce work\n");

	} else {

		/* If the aconnector->hdmi_hpd_debounce_work is scheduled, exit early */
		if (delayed_work_pending(&aconnector->hdmi_hpd_debounce_work))
			return;

		scoped_guard(mutex, &adev->dm.dc_lock) {
			dc_exit_ips_for_hw_access(dc);
			ret = dc_link_detect(aconnector->dc_link, reason);
		}
		if (ret) {
			/* w/a delay for certain panels */
			amdgpu_dm_apply_delay_after_dpcd_poweroff(adev, aconnector->dc_sink);
			amdgpu_dm_update_connector_after_detect(aconnector);

			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			if (aconnector->base.force == DRM_FORCE_UNSPECIFIED ||
			    reason == DETECT_REASON_HPDRX)
				drm_kms_helper_connector_hotplug_event(connector);
		}
	}
}
EXPORT_IF_KUNIT(handle_hpd_irq_helper);

STATIC_IFN_KUNIT void handle_hpd_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;

	handle_hpd_irq_helper(aconnector, DETECT_REASON_HPD);

}
EXPORT_IF_KUNIT(handle_hpd_irq);

STATIC_IFN_KUNIT void schedule_hpd_rx_offload_work(struct amdgpu_device *adev, struct hpd_rx_irq_offload_work_queue *offload_wq,
							union hpd_irq_data hpd_irq_data)
{
	struct hpd_rx_irq_offload_work *offload_work = kzalloc_obj(*offload_work);

	if (!offload_work) {
		drm_err(adev_to_drm(adev), "Failed to allocate hpd_rx_irq_offload_work.\n");
		return;
	}

	INIT_WORK(&offload_work->work, dm_handle_hpd_rx_offload_work);
	offload_work->data = hpd_irq_data;
	offload_work->offload_wq = offload_wq;
	offload_work->adev = adev;

	queue_work(offload_wq->wq, &offload_work->work);
	drm_dbg_kms(adev_to_drm(adev), "queue work to handle hpd_rx offload work");
}
EXPORT_IF_KUNIT(schedule_hpd_rx_offload_work);

STATIC_IFN_KUNIT void handle_hpd_rx_irq(void *param)
{
	struct amdgpu_dm_connector *aconnector = (struct amdgpu_dm_connector *)param;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dc_link *dc_link = aconnector->dc_link;
	bool is_mst_root_connector = aconnector->mst_mgr.mst_state;
	bool result = false;
	enum dc_connection_type new_connection_type = dc_connection_none;
	struct amdgpu_device *adev = drm_to_adev(dev);
	union hpd_irq_data hpd_irq_data;
	bool link_loss = false;
	bool has_left_work = false;
	int idx = dc_link->link_index;
	struct hpd_rx_irq_offload_work_queue *offload_wq = &adev->dm.hpd_rx_offload_wq[idx];
	struct dc *dc = aconnector->dc_link->ctx->dc;

	memset(&hpd_irq_data, 0, sizeof(hpd_irq_data));

	if (adev->dm.disable_hpd_irq)
		return;

	/*
	 * TODO:Temporary add mutex to protect hpd interrupt not have a gpio
	 * conflict, after implement i2c helper, this mutex should be
	 * retired.
	 */
	mutex_lock(&aconnector->hpd_lock);

	result = dc_link_handle_hpd_rx_irq(dc_link, &hpd_irq_data,
						&link_loss, true, &has_left_work);

	if (!has_left_work)
		goto out;

	if (hpd_irq_data.bytes.device_service_irq.bits.AUTOMATED_TEST) {
		schedule_hpd_rx_offload_work(adev, offload_wq, hpd_irq_data);
		goto out;
	}

	if (dc_link_dp_allow_hpd_rx_irq(dc_link)) {
		if (hpd_irq_data.bytes.device_service_irq.bits.UP_REQ_MSG_RDY ||
			hpd_irq_data.bytes.device_service_irq.bits.DOWN_REP_MSG_RDY) {
			bool skip = false;

			/*
			 * DOWN_REP_MSG_RDY is also handled by polling method
			 * mgr->cbs->poll_hpd_irq()
			 */
			spin_lock(&offload_wq->offload_lock);
			skip = offload_wq->is_handling_mst_msg_rdy_event;

			if (!skip)
				offload_wq->is_handling_mst_msg_rdy_event = true;

			spin_unlock(&offload_wq->offload_lock);

			if (!skip)
				schedule_hpd_rx_offload_work(adev, offload_wq, hpd_irq_data);

			goto out;
		}

		if (link_loss) {
			bool skip = false;

			spin_lock(&offload_wq->offload_lock);
			skip = offload_wq->is_handling_link_loss;

			if (!skip)
				offload_wq->is_handling_link_loss = true;

			spin_unlock(&offload_wq->offload_lock);

			if (!skip)
				schedule_hpd_rx_offload_work(adev, offload_wq, hpd_irq_data);

			goto out;
		}
	}

out:
	if (result && !is_mst_root_connector) {
		/* Downstream Port status changed. */
		if (!dc_link_detect_connection_type(dc_link, &new_connection_type))
			drm_err(adev_to_drm(adev), "KMS: Failed to detect connector\n");

		if (aconnector->base.force && new_connection_type == dc_connection_none) {
			amdgpu_dm_emulated_link_detect(dc_link);

			if (aconnector->fake_enable)
				aconnector->fake_enable = false;

			amdgpu_dm_update_connector_after_detect(aconnector);


			drm_modeset_lock_all(dev);
			dm_restore_drm_connector_state(dev, connector);
			drm_modeset_unlock_all(dev);

			drm_kms_helper_connector_hotplug_event(connector);
		} else {
			bool ret = false;

			mutex_lock(&adev->dm.dc_lock);
			dc_exit_ips_for_hw_access(dc);
			ret = dc_link_detect(dc_link, DETECT_REASON_HPDRX);
			mutex_unlock(&adev->dm.dc_lock);

			if (ret) {
				if (aconnector->fake_enable)
					aconnector->fake_enable = false;

				amdgpu_dm_update_connector_after_detect(aconnector);

				drm_modeset_lock_all(dev);
				dm_restore_drm_connector_state(dev, connector);
				drm_modeset_unlock_all(dev);

				drm_kms_helper_connector_hotplug_event(connector);
			}
		}
	}
	if (hpd_irq_data.bytes.device_service_irq.bits.CP_IRQ) {
		if (adev->dm.hdcp_workqueue)
			hdcp_handle_cpirq(adev->dm.hdcp_workqueue,  aconnector->base.index);
	}

	if (dc_link->type != dc_connection_mst_branch)
		drm_dp_cec_irq(&aconnector->dm_dp_aux.aux);

	mutex_unlock(&aconnector->hpd_lock);
}
EXPORT_IF_KUNIT(handle_hpd_rx_irq);

/**
 * dmub_hpd_callback - DMUB HPD interrupt processing callback.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * Dmub Hpd interrupt processing callback. Gets displayindex through the
 * ink index and calls helper to do the processing.
 */
STATIC_IFN_KUNIT void dmub_hpd_callback(struct amdgpu_device *adev,
			      struct dmub_notification *notify)
{
	struct amdgpu_dm_connector *aconnector;
	struct amdgpu_dm_connector *hpd_aconnector = NULL;
	struct drm_connector *connector;
	struct drm_connector_list_iter iter;
	struct dc_link *link;
	u8 link_index = 0;
	struct drm_device *dev;

	if (adev == NULL)
		return;

	if (notify == NULL) {
		drm_err(adev_to_drm(adev), "DMUB HPD callback notification was NULL");
		return;
	}

	if (notify->link_index > adev->dm.dc->link_count) {
		drm_err(adev_to_drm(adev), "DMUB HPD index (%u)is abnormal", notify->link_index);
		return;
	}

	/* Skip DMUB HPD IRQ in suspend/resume. We will probe them later. */
	if (notify->type == DMUB_NOTIFICATION_HPD && adev->in_suspend) {
		drm_info(adev_to_drm(adev), "Skip DMUB HPD IRQ callback in suspend/resume\n");
		return;
	}

	link_index = notify->link_index;
	link = adev->dm.dc->links[link_index];
	dev = adev->dm.ddev;

	drm_connector_list_iter_begin(dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		if (link && aconnector->dc_link == link) {
			if (notify->type == DMUB_NOTIFICATION_HPD)
				drm_info(adev_to_drm(adev), "DMUB HPD IRQ callback: link_index=%u\n", link_index);
			else if (notify->type == DMUB_NOTIFICATION_HPD_IRQ)
				drm_info(adev_to_drm(adev), "DMUB HPD RX IRQ callback: link_index=%u\n", link_index);
			else
				drm_warn(adev_to_drm(adev), "DMUB Unknown HPD callback type %d, link_index=%u\n",
						notify->type, link_index);

			hpd_aconnector = aconnector;
			break;
		}
	}
	drm_connector_list_iter_end(&iter);

	if (hpd_aconnector) {
		if (notify->type == DMUB_NOTIFICATION_HPD) {
			if (hpd_aconnector->dc_link->hpd_status == (notify->hpd_status == DP_HPD_PLUG))
				drm_warn(adev_to_drm(adev), "DMUB reported hpd status unchanged. link_index=%u\n", link_index);
			handle_hpd_irq_helper(hpd_aconnector, DETECT_REASON_HPD);
		} else if (notify->type == DMUB_NOTIFICATION_HPD_IRQ) {
			handle_hpd_rx_irq(hpd_aconnector);
		}
	}
}
EXPORT_IF_KUNIT(dmub_hpd_callback);

/**
 * dmub_hpd_sense_callback - DMUB HPD sense processing callback.
 * @adev: amdgpu_device pointer
 * @notify: dmub notification structure
 *
 * HPD sense changes can occur during low power states and need to be
 * notified from firmware to driver.
 */
STATIC_IFN_KUNIT void dmub_hpd_sense_callback(struct amdgpu_device *adev,
			      struct dmub_notification *notify)
{
	drm_dbg_driver(adev_to_drm(adev), "DMUB HPD SENSE callback.\n");
}
EXPORT_IF_KUNIT(dmub_hpd_sense_callback);

int amdgpu_dm_register_hpd_handlers(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct amdgpu_dm_connector *aconnector;
	const struct dc_link *dc_link;
	struct dc_interrupt_params int_params = {0};

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	if (dc_is_dmub_outbox_supported(adev->dm.dc)) {
		if (!dm_register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD,
			dmub_hpd_callback, true)) {
			drm_err(adev_to_drm(adev), "fail to register dmub hpd callback");
			return -EINVAL;
		}

		if (!dm_register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD_IRQ,
			dmub_hpd_callback, true)) {
			drm_err(adev_to_drm(adev), "fail to register dmub hpd callback");
			return -EINVAL;
		}

		if (!dm_register_dmub_notify_callback(adev, DMUB_NOTIFICATION_HPD_SENSE_NOTIFY,
			dmub_hpd_sense_callback, true)) {
			drm_err(adev_to_drm(adev), "fail to register dmub hpd sense callback");
			return -EINVAL;
		}
	}

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);
		dc_link = aconnector->dc_link;

		if (dc_link->irq_source_hpd != DC_IRQ_SOURCE_INVALID) {
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source = dc_link->irq_source_hpd;

			if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
				int_params.irq_source  < DC_IRQ_SOURCE_HPD1 ||
				int_params.irq_source  > DC_IRQ_SOURCE_HPD6) {
				drm_err(adev_to_drm(adev), "Failed to register hpd irq!\n");
				return -EINVAL;
			}

			if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
				handle_hpd_irq, (void *) aconnector))
				return -ENOMEM;
		}

		if (dc_link->irq_source_hpd_rx != DC_IRQ_SOURCE_INVALID) {

			/* Also register for DP short pulse (hpd_rx). */
			int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
			int_params.irq_source =	dc_link->irq_source_hpd_rx;

			if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
				int_params.irq_source  < DC_IRQ_SOURCE_HPD1RX ||
				int_params.irq_source  > DC_IRQ_SOURCE_HPD6RX) {
				drm_err(adev_to_drm(adev), "Failed to register hpd rx irq!\n");
				return -EINVAL;
			}

			if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
				handle_hpd_rx_irq, (void *) aconnector))
				return -ENOMEM;
		}
	}
	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_register_hpd_handlers);

/* ========== IRQ handlers ========== */
struct amdgpu_crtc *
amdgpu_dm_get_crtc_by_otg_inst(struct amdgpu_device *adev,
		     int otg_inst)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;

	if (WARN_ON(otg_inst == -1))
		return adev->mode_info.crtcs[0];

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		amdgpu_crtc = to_amdgpu_crtc(crtc);

		if (amdgpu_crtc->otg_inst == otg_inst)
			return amdgpu_crtc;
	}

	return NULL;
}
EXPORT_IF_KUNIT(amdgpu_dm_get_crtc_by_otg_inst);

/**
 * dm_pflip_high_irq() - Handle pageflip interrupt
 * @interrupt_params: ignored
 *
 * Handles the pageflip interrupt by notifying all interested parties
 * that the pageflip has been completed.
 */
STATIC_IFN_KUNIT void dm_pflip_high_irq(void *interrupt_params)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct drm_device *dev = adev_to_drm(adev);
	unsigned long flags;
	struct drm_pending_vblank_event *e;
	u32 vpos, hpos, v_blank_start, v_blank_end;
	bool vrr_active;

	amdgpu_crtc = amdgpu_dm_get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_PFLIP);

	/* IRQ could occur when in initial stage */
	/* TODO work and BO cleanup */
	if (amdgpu_crtc == NULL) {
		drm_dbg_state(dev, "CRTC is null, returning.\n");
		return;
	}

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);

	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED) {
		drm_dbg_state(dev,
			      "amdgpu_crtc->pflip_status = %d != AMDGPU_FLIP_SUBMITTED(%d) on crtc:%d[%p]\n",
			      amdgpu_crtc->pflip_status, AMDGPU_FLIP_SUBMITTED,
			      amdgpu_crtc->crtc_id, amdgpu_crtc);
		spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
		return;
	}

	/* page flip completed. */
	e = amdgpu_crtc->event;
	amdgpu_crtc->event = NULL;

	WARN_ON(!e);

	vrr_active = amdgpu_dm_crtc_vrr_active_irq(amdgpu_crtc);

	/* Fixed refresh rate, or VRR scanout position outside front-porch? */
	if (!vrr_active ||
	    !dc_stream_get_scanoutpos(amdgpu_crtc->dm_irq_params.stream, &v_blank_start,
				      &v_blank_end, &hpos, &vpos) ||
	    (vpos < v_blank_start)) {
		/* Update to correct count and vblank timestamp if racing with
		 * vblank irq. This also updates to the correct vblank timestamp
		 * even in VRR mode, as scanout is past the front-porch atm.
		 */
		drm_crtc_accurate_vblank_count(&amdgpu_crtc->base);

		/* Wake up userspace by sending the pageflip event with proper
		 * count and timestamp of vblank of flip completion.
		 */
		if (e) {
			drm_crtc_send_vblank_event(&amdgpu_crtc->base, e);

			/* Event sent, so done with vblank for this flip */
			drm_crtc_vblank_put(&amdgpu_crtc->base);
		}
	} else if (e) {
		/* VRR active and inside front-porch: vblank count and
		 * timestamp for pageflip event will only be up to date after
		 * drm_crtc_handle_vblank() has been executed from late vblank
		 * irq handler after start of back-porch (vline 0). We queue the
		 * pageflip event for send-out by drm_crtc_handle_vblank() with
		 * updated timestamp and count, once it runs after us.
		 *
		 * We need to open-code this instead of using the helper
		 * drm_crtc_arm_vblank_event(), as that helper would
		 * call drm_crtc_accurate_vblank_count(), which we must
		 * not call in VRR mode while we are in front-porch!
		 */

		/* sequence will be replaced by real count during send-out. */
		e->sequence = drm_crtc_vblank_count(&amdgpu_crtc->base);
		e->pipe = amdgpu_crtc->crtc_id;

		list_add_tail(&e->base.link, &adev_to_drm(adev)->vblank_event_list);
		e = NULL;
	}

	/* Keep track of vblank of this flip for flip throttling. We use the
	 * cooked hw counter, as that one incremented at start of this vblank
	 * of pageflip completion, so last_flip_vblank is the forbidden count
	 * for queueing new pageflips if vsync + VRR is enabled.
	 */
	amdgpu_crtc->dm_irq_params.last_flip_vblank =
		amdgpu_get_vblank_counter_kms(&amdgpu_crtc->base);

	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);

	drm_dbg_state(dev,
		      "crtc:%d[%p], pflip_stat:AMDGPU_FLIP_NONE, vrr[%d]-fp %d\n",
		      amdgpu_crtc->crtc_id, amdgpu_crtc, vrr_active, (int)!e);
}
EXPORT_IF_KUNIT(dm_pflip_high_irq);

STATIC_IFN_KUNIT void dm_handle_vmin_vmax_update(struct work_struct *offload_work)
{
	struct vupdate_offload_work *work = container_of(offload_work, struct vupdate_offload_work, work);
	struct amdgpu_device *adev = work->adev;
	struct dc_stream_state *stream = work->stream;
	struct dc_crtc_timing_adjust *adjust = work->adjust;

	mutex_lock(&adev->dm.dc_lock);
	dc_stream_adjust_vmin_vmax(adev->dm.dc, stream, adjust);
	mutex_unlock(&adev->dm.dc_lock);

	dc_stream_release(stream);
	kfree(work->adjust);
	kfree(work);
}
EXPORT_IF_KUNIT(dm_handle_vmin_vmax_update);

static void schedule_dc_vmin_vmax(struct amdgpu_device *adev,
	struct dc_stream_state *stream,
	struct dc_crtc_timing_adjust *adjust)
{
	struct vupdate_offload_work *offload_work = kzalloc_obj(*offload_work,
								GFP_NOWAIT);
	if (!offload_work) {
		drm_dbg_driver(adev_to_drm(adev), "Failed to allocate vupdate_offload_work\n");
		return;
	}

	struct dc_crtc_timing_adjust *adjust_copy = kzalloc_obj(*adjust_copy,
								GFP_NOWAIT);
	if (!adjust_copy) {
		drm_dbg_driver(adev_to_drm(adev), "Failed to allocate adjust_copy\n");
		kfree(offload_work);
		return;
	}

	dc_stream_retain(stream);
	memcpy(adjust_copy, adjust, sizeof(*adjust_copy));

	INIT_WORK(&offload_work->work, dm_handle_vmin_vmax_update);
	offload_work->adev = adev;
	offload_work->stream = stream;
	offload_work->adjust = adjust_copy;

	queue_work(adev->dm.vmin_vmax_wq, &offload_work->work);
}

/**
 * dm_crtc_high_irq_handler() - Common OTG vblank/flip event handling
 * @adev: amdgpu device
 * @acrtc: the CRTC to service
 * Performs writeback completion, vblank event handling, CRC processing, VRR BTR
 * updates and pageflip completion delivery.
 *
 * On DCN this is driven by VUPDATE_NO_LOCK (the register latch point) from
 * dm_vupdate_high_irq(); on DCE it is driven by VLINE0 at the start of vblank
 * from dm_crtc_high_irq().
 */
static void dm_crtc_high_irq_handler(struct amdgpu_device *adev,
				     struct amdgpu_crtc *acrtc)
{
	unsigned long flags;
	int vrr_active;
	bool is_dcn = amdgpu_ip_version(adev, DCE_HWIP, 0) != 0;

	if (acrtc->wb_conn && acrtc->wb_pending) {
		struct dc_stream_state *stream = acrtc->dm_irq_params.stream;
		unsigned int v_total, refresh_hz;

		v_total = stream->adjust.v_total_max ?
			  stream->adjust.v_total_max : stream->timing.v_total;
		refresh_hz = div_u64((uint64_t) stream->timing.pix_clk_100hz *
			     100LL, (v_total * stream->timing.h_total));
		mdelay(1000 / refresh_hz);

		/*
		 * Completion (signalling the out fence and releasing the vblank
		 * reference taken in dm_set_writeback()) is handled by the shared
		 * helper, which is also used by the teardown path.
		 */
		if (amdgpu_dm_crtc_complete_writeback(acrtc))
			dc_stream_fc_disable_writeback(adev->dm.dc,
						       acrtc->dm_irq_params.stream, 0);
	}

	vrr_active = amdgpu_dm_crtc_vrr_active_irq(acrtc);

	drm_dbg_vbl(adev_to_drm(adev),
		    "crtc:%d, vupdate-vrr:%d, planes:%d\n", acrtc->crtc_id,
		    vrr_active, acrtc->dm_irq_params.active_planes);

	/**
	 * Core vblank handling.
	 *
	 * On DCN this handler runs at VUPDATE_NO_LOCK, the register latch
	 * point, which is the correct place to timestamp both VRR and non-VRR
	 * vblanks.
	 *
	 * On DCE this handler runs at the start of front-porch, where only
	 * non-VRR timestamping is valid; VRR vblank is deferred to
	 * dm_vupdate_high_irq() after end of front-porch.
	 */
	if (is_dcn || !vrr_active)
		amdgpu_dm_crtc_handle_vblank(acrtc);

	/**
	 * Following stuff must happen at start of vblank, for crc
	 * computation and below-the-range btr support in vrr mode.
	 */
	amdgpu_dm_crtc_handle_crc_irq(&acrtc->base);

	/* BTR updates need to happen before VUPDATE on Vega and above. */
	if (adev->family < AMDGPU_FAMILY_AI)
		return;

	spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);

	if (acrtc->dm_irq_params.stream &&
		acrtc->dm_irq_params.vrr_params.supported) {
		bool replay_en = acrtc->dm_irq_params.stream->link->replay_settings.replay_feature_enabled;
		bool psr_en = acrtc->dm_irq_params.stream->link->psr_settings.psr_feature_enabled;
		bool fs_active_var_en = acrtc->dm_irq_params.freesync_config.state == VRR_STATE_ACTIVE_VARIABLE;

		mod_freesync_handle_v_update(adev->dm.freesync_module,
					     acrtc->dm_irq_params.stream,
					     &acrtc->dm_irq_params.vrr_params);

		/* update vmin_vmax only if freesync is enabled, or only if PSR and REPLAY are disabled */
		if (fs_active_var_en || (!fs_active_var_en && !replay_en && !psr_en)) {
			schedule_dc_vmin_vmax(adev, acrtc->dm_irq_params.stream,
					&acrtc->dm_irq_params.vrr_params.adjust);
		}
	}

	/*
	 * Deliver pageflip completion events (DCN only).
	 *
	 * Since GRPH_PFLIP is not used, VUPDATE_NO_LOCK is the flip latch
	 * point. Deliver any pending pageflip completion event from here,
	 * once HW has consumed the new address (the OTG no longer reports a
	 * pending flip).
	 *
	 * Also handle the case here where there aren't any active planes and
	 * DCN HUBP may be clock-gated, so the flip-pending status may be
	 * undefined.
	 */
	if (is_dcn && acrtc->pflip_status == AMDGPU_FLIP_SUBMITTED &&
	    acrtc->event) {

		if (!dc_get_flip_pending_on_otg(adev->dm.dc, acrtc->otg_inst)) {
			drm_crtc_send_vblank_event(&acrtc->base, acrtc->event);
			acrtc->event = NULL;
			drm_crtc_vblank_put(&acrtc->base);
			acrtc->pflip_status = AMDGPU_FLIP_NONE;
		}
		/*
		 * If the flip is still pending, leave it armed and
		 * retry on the next vupdate.
		 */
	} else if (is_dcn && acrtc->pflip_status == AMDGPU_FLIP_SUBMITTED &&
		   acrtc->dm_irq_params.active_planes == 0) {

		if (acrtc->event) {
			drm_crtc_send_vblank_event(&acrtc->base, acrtc->event);
			acrtc->event = NULL;
			drm_crtc_vblank_put(&acrtc->base);
		}
		acrtc->pflip_status = AMDGPU_FLIP_NONE;
	}

	spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
}

STATIC_IFN_KUNIT void dm_vupdate_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;
	struct drm_device *drm_dev;
	struct drm_vblank_crtc *vblank;
	ktime_t frame_duration_ns, previous_timestamp;
	unsigned long flags;
	int vrr_active;

	acrtc = amdgpu_dm_get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VUPDATE);
	if (!acrtc)
		return;

	vrr_active = amdgpu_dm_crtc_vrr_active_irq(acrtc);
	drm_dev = acrtc->base.dev;
	vblank = drm_crtc_vblank_crtc(&acrtc->base);
	previous_timestamp = atomic64_read(&irq_params->previous_timestamp);
	frame_duration_ns = vblank->time - previous_timestamp;

	if (frame_duration_ns > 0) {
		trace_amdgpu_refresh_rate_track(acrtc->base.index,
					frame_duration_ns,
					ktime_divns(NSEC_PER_SEC, frame_duration_ns));
		atomic64_set(&irq_params->previous_timestamp, vblank->time);
	}

	drm_dbg_vbl(drm_dev,
		    "crtc:%d, vupdate-vrr:%d\n", acrtc->crtc_id,
		    vrr_active);

	/*
	 * On DCN, VUPDATE_NO_LOCK is the single OTG interrupt used to deliver
	 * vblank and pageflip completion events; VSTARTUP and GRPH_PFLIP are
	 * not used. Run the full handler here.
	 */
	if (amdgpu_ip_version(adev, DCE_HWIP, 0) != 0) {
		dm_crtc_high_irq_handler(adev, acrtc);
		return;
	}

	/* DCE only below. */

	/* Core vblank handling is done here after end of front-porch in
	 * vrr mode, as vblank timestamping will give valid results
	 * while now done after front-porch. This will also deliver
	 * page-flip completion events that have been queued to us
	 * if a pageflip happened inside front-porch.
	 */
	if (vrr_active && acrtc->dm_irq_params.stream) {
		bool replay_en = acrtc->dm_irq_params.stream->link->replay_settings.replay_feature_enabled;
		bool psr_en = acrtc->dm_irq_params.stream->link->psr_settings.psr_feature_enabled;
		bool fs_active_var_en = acrtc->dm_irq_params.freesync_config.state
			== VRR_STATE_ACTIVE_VARIABLE;

		amdgpu_dm_crtc_handle_vblank(acrtc);

		/* BTR processing for pre-DCE12 ASICs */
		if (adev->family < AMDGPU_FAMILY_AI) {
			spin_lock_irqsave(&adev_to_drm(adev)->event_lock, flags);
			mod_freesync_handle_v_update(
				adev->dm.freesync_module,
				acrtc->dm_irq_params.stream,
				&acrtc->dm_irq_params.vrr_params);

			if (fs_active_var_en || (!fs_active_var_en && !replay_en && !psr_en)) {
				schedule_dc_vmin_vmax(adev,
					acrtc->dm_irq_params.stream,
					&acrtc->dm_irq_params.vrr_params.adjust);
			}
			spin_unlock_irqrestore(&adev_to_drm(adev)->event_lock, flags);
		}
	}
}
EXPORT_IF_KUNIT(dm_vupdate_high_irq);

/**
 * dm_crtc_high_irq() - Handles CRTC interrupt
 * @interrupt_params: used for determining the CRTC instance
 *
 * Handles the CRTC/VSYNC interrupt by notifying DRM's VBLANK event handler.
 *
 * Used on DCE (VLINE0, set to vblank start). On DCN the equivalent handling is
 * driven by VUPDATE_NO_LOCK in dm_vupdate_high_irq().
 */
STATIC_IFN_KUNIT void dm_crtc_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;

	acrtc = amdgpu_dm_get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VBLANK);
	if (!acrtc)
		return;

	dm_crtc_high_irq_handler(adev, acrtc);
}
EXPORT_IF_KUNIT(dm_crtc_high_irq);

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
/**
 * dm_dcn_vertical_interrupt0_high_irq() - Handles OTG Vertical interrupt0 for
 * DCN generation ASICs
 * @interrupt_params: interrupt parameters
 *
 * Used to set crc window/read out crc value at vertical line 0 position
 */
static void dm_dcn_vertical_interrupt0_high_irq(void *interrupt_params)
{
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_crtc *acrtc;

	acrtc = amdgpu_dm_get_crtc_by_otg_inst(adev, irq_params->irq_src - IRQ_TYPE_VLINE0);

	if (!acrtc)
		return;

	amdgpu_dm_crtc_handle_crc_window_irq(&acrtc->base);
}
#endif /* CONFIG_DRM_AMD_SECURE_DISPLAY */

STATIC_IFN_KUNIT void dm_handle_hpd_work(struct work_struct *work)
{
	struct dmub_hpd_work *dmub_hpd_wrk;

	dmub_hpd_wrk = container_of(work, struct dmub_hpd_work, handle_hpd_work);

	if (!dmub_hpd_wrk->dmub_notify) {
		drm_err(adev_to_drm(dmub_hpd_wrk->adev), "dmub_hpd_wrk dmub_notify is NULL");
		return;
	}

	if (dmub_hpd_wrk->dmub_notify->type < ARRAY_SIZE(dmub_hpd_wrk->adev->dm.dmub_callback)) {
		dmub_hpd_wrk->adev->dm.dmub_callback[dmub_hpd_wrk->dmub_notify->type](dmub_hpd_wrk->adev,
		dmub_hpd_wrk->dmub_notify);
	}

	kfree(dmub_hpd_wrk->dmub_notify);
	kfree(dmub_hpd_wrk);

}
EXPORT_IF_KUNIT(dm_handle_hpd_work);

STATIC_IFN_KUNIT const char *dmub_notification_type_str(enum dmub_notification_type e)
{
	switch (e) {
	case DMUB_NOTIFICATION_NO_DATA:
		return "NO_DATA";
	case DMUB_NOTIFICATION_AUX_REPLY:
		return "AUX_REPLY";
	case DMUB_NOTIFICATION_HPD:
		return "HPD";
	case DMUB_NOTIFICATION_HPD_IRQ:
		return "HPD_IRQ";
	case DMUB_NOTIFICATION_SET_CONFIG_REPLY:
		return "SET_CONFIG_REPLY";
	case DMUB_NOTIFICATION_DPIA_NOTIFICATION:
		return "DPIA_NOTIFICATION";
	case DMUB_NOTIFICATION_HPD_SENSE_NOTIFY:
		return "HPD_SENSE_NOTIFY";
	case DMUB_NOTIFICATION_FUSED_IO:
		return "FUSED_IO";
	default:
		return "<unknown>";
	}
}
EXPORT_IF_KUNIT(dmub_notification_type_str);

#define DMUB_TRACE_MAX_READ 64
/**
 * dm_dmub_outbox1_low_irq() - Handles Outbox interrupt
 * @interrupt_params: used for determining the Outbox instance
 *
 * Handles the Outbox Interrupt
 * event handler.
 */
STATIC_IFN_KUNIT void dm_dmub_outbox1_low_irq(void *interrupt_params)
{
	struct dmub_notification notify = {0};
	struct common_irq_params *irq_params = interrupt_params;
	struct amdgpu_device *adev = irq_params->adev;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct dmcub_trace_buf_entry entry = { 0 };
	u32 count = 0;
	struct dmub_hpd_work *dmub_hpd_wrk;

	do {
		if (dc_dmub_srv_get_dmub_outbox0_msg(dm->dc, &entry)) {
			trace_amdgpu_dmub_trace_high_irq(entry.trace_code, entry.tick_count,
							entry.param0, entry.param1);

			drm_dbg_driver(adev_to_drm(adev), "trace_code:%u, tick_count:%u, param0:%u, param1:%u\n",
				 entry.trace_code, entry.tick_count, entry.param0, entry.param1);
		} else
			break;

		count++;

	} while (count <= DMUB_TRACE_MAX_READ);

	if (count > DMUB_TRACE_MAX_READ)
		drm_dbg_driver(adev_to_drm(adev), "Warning : count > DMUB_TRACE_MAX_READ");

	if (dc_enable_dmub_notifications(adev->dm.dc) &&
		irq_params->irq_src == DC_IRQ_SOURCE_DMCUB_OUTBOX) {

		do {
			dc_stat_get_dmub_notification(adev->dm.dc, &notify);
			if (notify.type >= ARRAY_SIZE(dm->dmub_thread_offload)) {
				drm_err(adev_to_drm(adev), "DM: notify type %d invalid!", notify.type);
				continue;
			}
			if (!dm->dmub_callback[notify.type]) {
				drm_warn(adev_to_drm(adev), "DMUB notification skipped due to no handler: type=%s\n",
					dmub_notification_type_str(notify.type));
				continue;
			}
			if (dm->dmub_thread_offload[notify.type]) {
				dmub_hpd_wrk = kzalloc_obj(*dmub_hpd_wrk,
							   GFP_ATOMIC);
				if (!dmub_hpd_wrk) {
					drm_err(adev_to_drm(adev), "Failed to allocate dmub_hpd_wrk");
					return;
				}
				dmub_hpd_wrk->dmub_notify = kmemdup(&notify, sizeof(struct dmub_notification),
								    GFP_ATOMIC);
				if (!dmub_hpd_wrk->dmub_notify) {
					kfree(dmub_hpd_wrk);
					drm_err(adev_to_drm(adev), "Failed to allocate dmub_hpd_wrk->dmub_notify");
					return;
				}
				INIT_WORK(&dmub_hpd_wrk->handle_hpd_work, dm_handle_hpd_work);
				dmub_hpd_wrk->adev = adev;
				queue_work(adev->dm.delayed_hpd_wq, &dmub_hpd_wrk->handle_hpd_work);
			} else {
				dm->dmub_callback[notify.type](adev, &notify);
			}
		} while (notify.pending_notification);
	}
}
EXPORT_IF_KUNIT(dm_dmub_outbox1_low_irq);

/* Register IRQ sources and initialize IRQ callbacks */
int amdgpu_dm_dce110_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
	unsigned int src_id;
	unsigned int client_id = AMDGPU_IRQ_CLIENTID_LEGACY;
	/* Use different interrupts for VBLANK on DCE 6 vs. newer. */
	const unsigned int vblank_d1 =
		adev->dm.dc->ctx->dce_version >= DCE_VERSION_8_0
		? VISLANDS30_IV_SRCID_D1_VERTICAL_INTERRUPT0 : 1;

	if (adev->family >= AMDGPU_FAMILY_AI)
		client_id = SOC15_IH_CLIENTID_DCE;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/*
	 * Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 */

	/* Use VBLANK interrupt */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		src_id = vblank_d1 + i;
		r = amdgpu_irq_add_id(adev, client_id, src_id, &adev->crtc_irq);
		if (r) {
			drm_err(adev_to_drm(adev), "Failed to add crtc irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, src_id, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VBLANK1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VBLANK6) {
			drm_err(adev_to_drm(adev), "Failed to register vblank irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vblank_params[int_params.irq_source - DC_IRQ_SOURCE_VBLANK1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_crtc_high_irq, c_irq_params))
			return -ENOMEM;
	}

	if (dc_supports_vrr(adev->dm.dc->ctx->dce_version)) {
		/* Use VUPDATE interrupt */
		for (i = 0; i < adev->mode_info.num_crtc; i++) {
			src_id = VISLANDS30_IV_SRCID_D1_V_UPDATE_INT + i * 2;
			r = amdgpu_irq_add_id(adev, client_id, src_id, &adev->vupdate_irq);
			if (r) {
				drm_err(adev_to_drm(adev), "Failed to add vupdate irq id!\n");
				return r;
			}

			int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
			int_params.irq_source =
				dc_interrupt_to_irq_source(dc, src_id, 0);

			if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
				int_params.irq_source  < DC_IRQ_SOURCE_VUPDATE1 ||
				int_params.irq_source  > DC_IRQ_SOURCE_VUPDATE6) {
				drm_err(adev_to_drm(adev), "Failed to register vupdate irq!\n");
				return -EINVAL;
			}

			c_irq_params = &adev->dm.vupdate_params[
				int_params.irq_source - DC_IRQ_SOURCE_VUPDATE1];
			c_irq_params->adev = adev;
			c_irq_params->irq_src = int_params.irq_source;

			if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
				dm_vupdate_high_irq, c_irq_params))
				return -ENOMEM;
		}
	}

	/* Use GRPH_PFLIP interrupt */
	for (i = VISLANDS30_IV_SRCID_D1_GRPH_PFLIP;
			i <= VISLANDS30_IV_SRCID_D6_GRPH_PFLIP; i += 2) {
		r = amdgpu_irq_add_id(adev, client_id, i, &adev->pageflip_irq);
		if (r) {
			drm_err(adev_to_drm(adev), "Failed to add page flip irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_PFLIP_FIRST ||
			int_params.irq_source  > DC_IRQ_SOURCE_PFLIP_LAST) {
			drm_err(adev_to_drm(adev), "Failed to register pflip irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.pflip_params[int_params.irq_source - DC_IRQ_SOURCE_PFLIP_FIRST];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_pflip_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, client_id,
			VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A, &adev->hpd_irq);
	if (r) {
		drm_err(adev_to_drm(adev), "Failed to add hpd irq id!\n");
		return r;
	}

	r = amdgpu_dm_register_hpd_handlers(adev);

	return r;
}
EXPORT_IF_KUNIT(amdgpu_dm_dce110_register_irq_handlers);

/* Register IRQ sources and initialize IRQ callbacks */
int amdgpu_dm_dcn10_register_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r;
	int i;
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	static const unsigned int vrtl_int_srcid[] = {
		DCN_1_0__SRCID__OTG1_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG2_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG3_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG4_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG5_VERTICAL_INTERRUPT0_CONTROL,
		DCN_1_0__SRCID__OTG6_VERTICAL_INTERRUPT0_CONTROL
	};
#endif

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	/*
	 * Actions of amdgpu_irq_add_id():
	 * 1. Register a set() function with base driver.
	 *    Base driver will call set() function to enable/disable an
	 *    interrupt in DC hardware.
	 * 2. Register amdgpu_dm_irq_handler().
	 *    Base driver will call amdgpu_dm_irq_handler() for ALL interrupts
	 *    coming from DC hardware.
	 *    amdgpu_dm_irq_handler() will re-direct the interrupt to DC
	 *    for acknowledging and handling.
	 */

	/* Use otg vertical line interrupt */
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	for (i = 0; i <= adev->mode_info.num_crtc - 1; i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE,
				vrtl_int_srcid[i], &adev->vline0_irq);

		if (r) {
			drm_err(adev_to_drm(adev), "Failed to add vline0 irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, vrtl_int_srcid[i], 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source < DC_IRQ_SOURCE_DC1_VLINE0 ||
			int_params.irq_source > DC_IRQ_SOURCE_DC6_VLINE0) {
			drm_err(adev_to_drm(adev), "Failed to register vline0 irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vline0_params[int_params.irq_source
					- DC_IRQ_SOURCE_DC1_VLINE0];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_dcn_vertical_interrupt0_high_irq,
			c_irq_params))
			return -ENOMEM;
	}
#endif

	/* Use VUPDATE_NO_LOCK interrupt on DCN, which seems to correspond to
	 * the regular VUPDATE interrupt on DCE. We want DC_IRQ_SOURCE_VUPDATEx
	 * to trigger at end of each vblank, regardless of state of the lock,
	 * matching DCE behaviour.
	 */
	for (i = DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT;
	     i <= DCN_1_0__SRCID__OTG0_IHC_V_UPDATE_NO_LOCK_INTERRUPT + adev->mode_info.num_crtc - 1;
	     i++) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, i, &adev->vupdate_irq);

		if (r) {
			drm_err(adev_to_drm(adev), "Failed to add vupdate irq id!\n");
			return r;
		}

		int_params.int_context = INTERRUPT_HIGH_IRQ_CONTEXT;
		int_params.irq_source =
			dc_interrupt_to_irq_source(dc, i, 0);

		if (int_params.irq_source == DC_IRQ_SOURCE_INVALID ||
			int_params.irq_source  < DC_IRQ_SOURCE_VUPDATE1 ||
			int_params.irq_source  > DC_IRQ_SOURCE_VUPDATE6) {
			drm_err(adev_to_drm(adev), "Failed to register vupdate irq!\n");
			return -EINVAL;
		}

		c_irq_params = &adev->dm.vupdate_params[int_params.irq_source - DC_IRQ_SOURCE_VUPDATE1];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_vupdate_high_irq, c_irq_params))
			return -ENOMEM;
	}

	/* HPD */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, DCN_1_0__SRCID__DC_HPD1_INT,
			&adev->hpd_irq);
	if (r) {
		drm_err(adev_to_drm(adev), "Failed to add hpd irq id!\n");
		return r;
	}

	r = amdgpu_dm_register_hpd_handlers(adev);

	return r;
}
EXPORT_IF_KUNIT(amdgpu_dm_dcn10_register_irq_handlers);

/* Register Outbox IRQ sources and initialize IRQ callbacks */
int amdgpu_dm_register_outbox_irq_handlers(struct amdgpu_device *adev)
{
	struct dc *dc = adev->dm.dc;
	struct common_irq_params *c_irq_params;
	struct dc_interrupt_params int_params = {0};
	int r, i;

	int_params.requested_polarity = INTERRUPT_POLARITY_DEFAULT;
	int_params.current_polarity = INTERRUPT_POLARITY_DEFAULT;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DCE, DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT,
			&adev->dmub_outbox_irq);
	if (r) {
		drm_err(adev_to_drm(adev), "Failed to add outbox irq id!\n");
		return r;
	}

	if (dc->ctx->dmub_srv) {
		i = DCN_1_0__SRCID__DMCUB_OUTBOX_LOW_PRIORITY_READY_INT;
		int_params.int_context = INTERRUPT_LOW_IRQ_CONTEXT;
		int_params.irq_source =
		dc_interrupt_to_irq_source(dc, i, 0);

		c_irq_params = &adev->dm.dmub_outbox_params[0];

		c_irq_params->adev = adev;
		c_irq_params->irq_src = int_params.irq_source;

		if (!amdgpu_dm_irq_register_interrupt(adev, &int_params,
			dm_dmub_outbox1_low_irq, c_irq_params))
			return -ENOMEM;
	}

	return 0;
}
EXPORT_IF_KUNIT(amdgpu_dm_register_outbox_irq_handlers);
