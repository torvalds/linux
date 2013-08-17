/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



#include <kbase/src/common/mali_kbase.h>

#define beenthere(f, a...)	pr_debug("%s:" f, __func__, ##a)

STATIC base_jd_udata kbase_event_process(kbase_context *kctx, kbase_jd_atom *katom)
{
	base_jd_udata data;

	OSK_ASSERT(kctx != NULL);
	OSK_ASSERT(katom != NULL);
	OSK_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

	data = katom->udata;

	katom->status = KBASE_JD_ATOM_STATE_UNUSED;

	wake_up(&katom->completed);

	return data;
}

int kbase_event_pending(kbase_context *ctx)
{
	int ret;

	OSK_ASSERT(ctx);

	mutex_lock(&ctx->event_mutex);
	ret  = (MALI_FALSE == OSK_DLIST_IS_EMPTY(&ctx->event_list)) || (MALI_TRUE == ctx->event_closed);
	mutex_unlock(&ctx->event_mutex);

	return ret;
}
KBASE_EXPORT_TEST_API(kbase_event_pending)

int kbase_event_dequeue(kbase_context *ctx, base_jd_event_v2 *uevent)
{
	kbase_jd_atom *atom;
	kbase_jd_atom *trace_atom;
	int err;

	OSK_ASSERT(ctx);

	mutex_lock(&ctx->event_mutex);

	if (OSK_DLIST_IS_EMPTY(&ctx->event_list))
	{
		if (ctx->event_closed)
		{
			/* generate the BASE_JD_EVENT_DRV_TERMINATED message on the fly */
			mutex_unlock(&ctx->event_mutex);
			uevent->event_code = BASE_JD_EVENT_DRV_TERMINATED;
			memset(&uevent->udata, 0, sizeof(uevent->udata));
			beenthere("event system closed, returning BASE_JD_EVENT_DRV_TERMINATED(0x%X)\n", BASE_JD_EVENT_DRV_TERMINATED);
			return 0;
		}
		else
		{
			mutex_unlock(&ctx->event_mutex);
			return -1;
		}
	}

	/* normal event processing */
	trace_atom = OSK_DLIST_FRONT(&ctx->event_list, kbase_jd_atom, dep_item[0]);
	kbasep_list_trace_add(0, ctx->kbdev, trace_atom, &ctx->event_list, KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_EVENT_LIST);
	atom = OSK_DLIST_POP_FRONT(&ctx->event_list, kbase_jd_atom, dep_item[0], err);
	if (err) {
		kbasep_list_trace_dump(ctx->kbdev);
		BUG();
	}

	mutex_unlock(&ctx->event_mutex);

	beenthere("event dequeuing %p\n", (void*)atom);
	uevent->event_code = atom->event_code;
	uevent->atom_number = (atom - ctx->jctx.atoms);
	uevent->udata = kbase_event_process(ctx, atom);

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_event_dequeue)

static void kbase_event_post_worker(osk_workq_work *data)
{
	kbase_jd_atom *atom = CONTAINER_OF(data, kbase_jd_atom, work);
	kbase_context *ctx = atom->kctx;

	if (atom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
	{
		kbase_jd_free_external_resources(atom);
	}

	if (atom->core_req & BASE_JD_REQ_EVENT_ONLY_ON_FAILURE)
	{
		if (atom->event_code == BASE_JD_EVENT_DONE)
		{
			/* Don't report the event */
			kbase_event_process(ctx, atom);
			return;
		}
	}

	kbasep_list_trace_add(1, ctx->kbdev, atom, &ctx->event_list, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_EVENT_LIST);
	mutex_lock(&ctx->event_mutex);
	OSK_DLIST_PUSH_BACK(&ctx->event_list, atom, kbase_jd_atom, dep_item[0]);
	mutex_unlock(&ctx->event_mutex);

	kbase_event_wakeup(ctx);
}

void kbase_event_post(kbase_context *ctx, kbase_jd_atom *atom)
{
	OSK_ASSERT(ctx);
	OSK_ASSERT(atom);

	osk_workq_work_init(&atom->work, kbase_event_post_worker);
	osk_workq_submit(&ctx->event_workq, &atom->work);
}
KBASE_EXPORT_TEST_API(kbase_event_post)

void kbase_event_close(kbase_context * kctx)
{
	mutex_lock(&kctx->event_mutex);
	kctx->event_closed = MALI_TRUE;
	mutex_unlock(&kctx->event_mutex);
	kbase_event_wakeup(kctx);
}

mali_error kbase_event_init(kbase_context *kctx)
{
	osk_error osk_err;

	OSK_ASSERT(kctx);
	OSK_DLIST_INIT(&kctx->event_list);
	mutex_init(&kctx->event_mutex);
	kctx->event_closed = MALI_FALSE;

	osk_err = osk_workq_init(&kctx->event_workq, "kbase_event", OSK_WORKQ_RESCUER);
	if (OSK_ERR_NONE != osk_err)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_event_init)

void kbase_event_cleanup(kbase_context *kctx)
{
	OSK_ASSERT(kctx);

	osk_workq_flush(&kctx->event_workq);
	osk_workq_term(&kctx->event_workq);

	/* We use kbase_event_dequeue to remove the remaining events as that
	 * deals with all the cleanup needed for the atoms.
	 *
	 * Note: use of kctx->event_list without a lock is safe because this must be the last
	 * thread using it (because we're about to terminate the lock)
	 */
	while (!OSK_DLIST_IS_EMPTY(&kctx->event_list))
	{
		base_jd_event_v2 event;
		kbase_event_dequeue(kctx, &event);
	}
}
KBASE_EXPORT_TEST_API(kbase_event_cleanup)

