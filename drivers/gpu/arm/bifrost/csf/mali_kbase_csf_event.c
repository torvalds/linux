// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */
#include <mali_kbase.h>
#include "mali_kbase_csf_event.h"

/**
 * struct kbase_csf_event_cb - CSF event callback.
 *
 * @link:      Link to the rest of the list.
 * @kctx:      Pointer to the Kbase context this event belongs to.
 * @callback:  Callback function to call when a CSF event is signalled.
 * @param:     Parameter to pass to the callback function.
 *
 * This structure belongs to the list of events which is part of a Kbase
 * context, and describes a callback function with a custom parameter to pass
 * to it when a CSF event is signalled.
 */
struct kbase_csf_event_cb {
	struct list_head link;
	struct kbase_context *kctx;
	kbase_csf_event_callback *callback;
	void *param;
};

int kbase_csf_event_wait_add(struct kbase_context *kctx,
			     kbase_csf_event_callback *callback, void *param)
{
	int err = -ENOMEM;
	struct kbase_csf_event_cb *event_cb =
		kzalloc(sizeof(struct kbase_csf_event_cb), GFP_KERNEL);

	if (event_cb) {
		unsigned long flags;

		event_cb->kctx = kctx;
		event_cb->callback = callback;
		event_cb->param = param;

		spin_lock_irqsave(&kctx->csf.event.lock, flags);
		list_add_tail(&event_cb->link, &kctx->csf.event.callback_list);
		dev_dbg(kctx->kbdev->dev,
			"Added event handler %pK with param %pK\n", event_cb,
			event_cb->param);
		spin_unlock_irqrestore(&kctx->csf.event.lock, flags);

		err = 0;
	}

	return err;
}

void kbase_csf_event_wait_remove(struct kbase_context *kctx,
		kbase_csf_event_callback *callback, void *param)
{
	struct kbase_csf_event_cb *event_cb;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event.lock, flags);

	list_for_each_entry(event_cb, &kctx->csf.event.callback_list, link) {
		if ((event_cb->callback == callback) && (event_cb->param == param)) {
			list_del(&event_cb->link);
			dev_dbg(kctx->kbdev->dev,
				"Removed event handler %pK with param %pK\n",
				event_cb, event_cb->param);
			kfree(event_cb);
			break;
		}
	}
	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);
}

static void sync_update_notify_gpu(struct kbase_context *kctx)
{
	bool can_notify_gpu;
	unsigned long flags;

	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	can_notify_gpu = kctx->kbdev->pm.backend.gpu_powered;
#ifdef KBASE_PM_RUNTIME
	if (kctx->kbdev->pm.backend.gpu_sleep_mode_active)
		can_notify_gpu = false;
#endif

	if (can_notify_gpu) {
		kbase_csf_ring_doorbell(kctx->kbdev, CSF_KERNEL_DOORBELL_NR);
		KBASE_KTRACE_ADD(kctx->kbdev, CSF_SYNC_UPDATE_NOTIFY_GPU_EVENT, kctx, 0u);
	}

	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
}

void kbase_csf_event_signal(struct kbase_context *kctx, bool notify_gpu)
{
	struct kbase_csf_event_cb *event_cb, *next_event_cb;
	unsigned long flags;

	dev_dbg(kctx->kbdev->dev,
		"Signal event (%s GPU notify) for context %pK\n",
		notify_gpu ? "with" : "without", (void *)kctx);

	/* First increment the signal count and wake up event thread.
	 */
	atomic_set(&kctx->event_count, 1);
	kbase_event_wakeup(kctx);

	/* Signal the CSF firmware. This is to ensure that pending command
	 * stream synch object wait operations are re-evaluated.
	 * Write to GLB_DOORBELL would suffice as spec says that all pending
	 * synch object wait operations are re-evaluated on a write to any
	 * CS_DOORBELL/GLB_DOORBELL register.
	 */
	if (notify_gpu)
		sync_update_notify_gpu(kctx);

	/* Now invoke the callbacks registered on backend side.
	 * Allow item removal inside the loop, if requested by the callback.
	 */
	spin_lock_irqsave(&kctx->csf.event.lock, flags);

	list_for_each_entry_safe(
		event_cb, next_event_cb, &kctx->csf.event.callback_list, link) {
		enum kbase_csf_event_callback_action action;

		dev_dbg(kctx->kbdev->dev,
			"Calling event handler %pK with param %pK\n",
			(void *)event_cb, event_cb->param);
		action = event_cb->callback(event_cb->param);
		if (action == KBASE_CSF_EVENT_CALLBACK_REMOVE) {
			list_del(&event_cb->link);
			kfree(event_cb);
		}
	}

	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);
}

void kbase_csf_event_term(struct kbase_context *kctx)
{
	struct kbase_csf_event_cb *event_cb, *next_event_cb;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event.lock, flags);

	list_for_each_entry_safe(
		event_cb, next_event_cb, &kctx->csf.event.callback_list, link) {
		list_del(&event_cb->link);
		dev_warn(kctx->kbdev->dev,
			"Removed event handler %pK with param %pK\n",
			(void *)event_cb, event_cb->param);
		kfree(event_cb);
	}

	WARN_ON(!list_empty(&kctx->csf.event.error_list));

	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);
}

void kbase_csf_event_init(struct kbase_context *const kctx)
{
	INIT_LIST_HEAD(&kctx->csf.event.callback_list);
	INIT_LIST_HEAD(&kctx->csf.event.error_list);
	spin_lock_init(&kctx->csf.event.lock);
}

void kbase_csf_event_remove_error(struct kbase_context *kctx,
				  struct kbase_csf_notification *error)
{
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event.lock, flags);
	list_del_init(&error->link);
	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);
}

bool kbase_csf_event_read_error(struct kbase_context *kctx,
				struct base_csf_notification *event_data)
{
	struct kbase_csf_notification *error_data = NULL;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event.lock, flags);
	if (likely(!list_empty(&kctx->csf.event.error_list))) {
		error_data = list_first_entry(&kctx->csf.event.error_list,
			struct kbase_csf_notification, link);
		list_del_init(&error_data->link);
		*event_data = error_data->data;
		dev_dbg(kctx->kbdev->dev, "Dequeued error %pK in context %pK\n",
			(void *)error_data, (void *)kctx);
	}
	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);
	return !!error_data;
}

void kbase_csf_event_add_error(struct kbase_context *const kctx,
			struct kbase_csf_notification *const error,
			struct base_csf_notification const *const data)
{
	unsigned long flags;

	if (WARN_ON(!kctx))
		return;

	if (WARN_ON(!error))
		return;

	if (WARN_ON(!data))
		return;

	spin_lock_irqsave(&kctx->csf.event.lock, flags);
	if (list_empty(&error->link)) {
		error->data = *data;
		list_add_tail(&error->link, &kctx->csf.event.error_list);
		dev_dbg(kctx->kbdev->dev,
			"Added error %pK of type %d in context %pK\n",
			(void *)error, data->type, (void *)kctx);
	} else {
		dev_dbg(kctx->kbdev->dev, "Error %pK of type %d already pending in context %pK",
			(void *)error, error->data.type, (void *)kctx);
	}
	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);
}

bool kbase_csf_event_error_pending(struct kbase_context *kctx)
{
	bool error_pending = false;
	unsigned long flags;

	spin_lock_irqsave(&kctx->csf.event.lock, flags);
	error_pending = !list_empty(&kctx->csf.event.error_list);

	dev_dbg(kctx->kbdev->dev, "%s error is pending in context %pK\n",
		error_pending ? "An" : "No", (void *)kctx);

	spin_unlock_irqrestore(&kctx->csf.event.lock, flags);

	return error_pending;
}
