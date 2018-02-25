/*
 *
 * (C) COPYRIGHT 2010-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */



#include <mali_kbase.h>
#include <mali_kbase_debug.h>
#include <mali_kbase_tlstream.h>

static struct base_jd_udata kbase_event_process(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct base_jd_udata data;

	lockdep_assert_held(&kctx->jctx.lock);

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	KBASE_DEBUG_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

	data = katom->udata;

	KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx, atomic_sub_return(1, &kctx->timeline.jd_atoms_in_flight));

	KBASE_TLSTREAM_TL_NRET_ATOM_CTX(katom, kctx);
	KBASE_TLSTREAM_TL_DEL_ATOM(katom);

	katom->status = KBASE_JD_ATOM_STATE_UNUSED;

	wake_up(&katom->completed);

	return data;
}

int kbase_event_pending(struct kbase_context *ctx)
{
	KBASE_DEBUG_ASSERT(ctx);

	return (atomic_read(&ctx->event_count) != 0) ||
			(atomic_read(&ctx->event_closed) != 0);
}

KBASE_EXPORT_TEST_API(kbase_event_pending);

int kbase_event_dequeue(struct kbase_context *ctx, struct base_jd_event_v2 *uevent)
{
	struct kbase_jd_atom *atom;

	KBASE_DEBUG_ASSERT(ctx);

	mutex_lock(&ctx->event_mutex);

	if (list_empty(&ctx->event_list)) {
		if (!atomic_read(&ctx->event_closed)) {
			mutex_unlock(&ctx->event_mutex);
			return -1;
		}

		/* generate the BASE_JD_EVENT_DRV_TERMINATED message on the fly */
		mutex_unlock(&ctx->event_mutex);
		uevent->event_code = BASE_JD_EVENT_DRV_TERMINATED;
		memset(&uevent->udata, 0, sizeof(uevent->udata));
		dev_dbg(ctx->kbdev->dev,
				"event system closed, returning BASE_JD_EVENT_DRV_TERMINATED(0x%X)\n",
				BASE_JD_EVENT_DRV_TERMINATED);
		return 0;
	}

	/* normal event processing */
	atomic_dec(&ctx->event_count);
	atom = list_entry(ctx->event_list.next, struct kbase_jd_atom, dep_item[0]);
	list_del(ctx->event_list.next);

	mutex_unlock(&ctx->event_mutex);

	dev_dbg(ctx->kbdev->dev, "event dequeuing %p\n", (void *)atom);
	uevent->event_code = atom->event_code;
	uevent->atom_number = (atom - ctx->jctx.atoms);

	if (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
		kbase_jd_free_external_resources(atom);

	mutex_lock(&ctx->jctx.lock);
	uevent->udata = kbase_event_process(ctx, atom);
	mutex_unlock(&ctx->jctx.lock);

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_event_dequeue);

/**
 * kbase_event_process_noreport_worker - Worker for processing atoms that do not
 *                                       return an event but do have external
 *                                       resources
 * @data:  Work structure
 */
static void kbase_event_process_noreport_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom,
			work);
	struct kbase_context *kctx = katom->kctx;

	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
		kbase_jd_free_external_resources(katom);

	mutex_lock(&kctx->jctx.lock);
	kbase_event_process(kctx, katom);
	mutex_unlock(&kctx->jctx.lock);
}

/**
 * kbase_event_process_noreport - Process atoms that do not return an event
 * @kctx:  Context pointer
 * @katom: Atom to be processed
 *
 * Atoms that do not have external resources will be processed immediately.
 * Atoms that do have external resources will be processed on a workqueue, in
 * order to avoid locking issues.
 */
static void kbase_event_process_noreport(struct kbase_context *kctx,
		struct kbase_jd_atom *katom)
{
	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {
		INIT_WORK(&katom->work, kbase_event_process_noreport_worker);
		queue_work(kctx->event_workq, &katom->work);
	} else {
		kbase_event_process(kctx, katom);
	}
}

/**
 * kbase_event_coalesce - Move pending events to the main event list
 * @kctx:  Context pointer
 *
 * kctx->event_list and kctx->event_coalesce_count must be protected
 * by a lock unless this is the last thread using them
 * (and we're about to terminate the lock).
 *
 * Return: The number of pending events moved to the main event list
 */
static int kbase_event_coalesce(struct kbase_context *kctx)
{
	const int event_count = kctx->event_coalesce_count;

	/* Join the list of pending events onto the tail of the main list
	   and reset it */
	list_splice_tail_init(&kctx->event_coalesce_list, &kctx->event_list);
	kctx->event_coalesce_count = 0;

	/* Return the number of events moved */
	return event_count;
}

void kbase_event_post(struct kbase_context *ctx, struct kbase_jd_atom *atom)
{
	if (atom->core_req & BASE_JD_REQ_EVENT_ONLY_ON_FAILURE) {
		if (atom->event_code == BASE_JD_EVENT_DONE) {
			/* Don't report the event */
			kbase_event_process_noreport(ctx, atom);
			return;
		}
	}

	if (atom->core_req & BASEP_JD_REQ_EVENT_NEVER) {
		/* Don't report the event */
		kbase_event_process_noreport(ctx, atom);
		return;
	}
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(atom, TL_ATOM_STATE_POSTED);
	if (atom->core_req & BASE_JD_REQ_EVENT_COALESCE) {
		/* Don't report the event until other event(s) have completed */
		mutex_lock(&ctx->event_mutex);
		list_add_tail(&atom->dep_item[0], &ctx->event_coalesce_list);
		++ctx->event_coalesce_count;
		mutex_unlock(&ctx->event_mutex);
	} else {
		/* Report the event and any pending events now */
		int event_count = 1;

		mutex_lock(&ctx->event_mutex);
		event_count += kbase_event_coalesce(ctx);
		list_add_tail(&atom->dep_item[0], &ctx->event_list);
		atomic_add(event_count, &ctx->event_count);
		mutex_unlock(&ctx->event_mutex);

		kbase_event_wakeup(ctx);
	}
}
KBASE_EXPORT_TEST_API(kbase_event_post);

void kbase_event_close(struct kbase_context *kctx)
{
	mutex_lock(&kctx->event_mutex);
	atomic_set(&kctx->event_closed, true);
	mutex_unlock(&kctx->event_mutex);
	kbase_event_wakeup(kctx);
}

int kbase_event_init(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	INIT_LIST_HEAD(&kctx->event_list);
	INIT_LIST_HEAD(&kctx->event_coalesce_list);
	mutex_init(&kctx->event_mutex);
	atomic_set(&kctx->event_count, 0);
	kctx->event_coalesce_count = 0;
	atomic_set(&kctx->event_closed, false);
	kctx->event_workq = alloc_workqueue("kbase_event", WQ_MEM_RECLAIM, 1);

	if (NULL == kctx->event_workq)
		return -EINVAL;

	return 0;
}

KBASE_EXPORT_TEST_API(kbase_event_init);

void kbase_event_cleanup(struct kbase_context *kctx)
{
	int event_count;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(kctx->event_workq);

	flush_workqueue(kctx->event_workq);
	destroy_workqueue(kctx->event_workq);

	/* We use kbase_event_dequeue to remove the remaining events as that
	 * deals with all the cleanup needed for the atoms.
	 *
	 * Note: use of kctx->event_list without a lock is safe because this must be the last
	 * thread using it (because we're about to terminate the lock)
	 */
	event_count = kbase_event_coalesce(kctx);
	atomic_add(event_count, &kctx->event_count);

	while (!list_empty(&kctx->event_list)) {
		struct base_jd_event_v2 event;

		kbase_event_dequeue(kctx, &event);
	}
}

KBASE_EXPORT_TEST_API(kbase_event_cleanup);
