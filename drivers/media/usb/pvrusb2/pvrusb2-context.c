// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */

#include "pvrusb2-context.h"
#include "pvrusb2-io.h"
#include "pvrusb2-ioread.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-debug.h"
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>

static struct pvr2_context *pvr2_context_exist_first;
static struct pvr2_context *pvr2_context_exist_last;
static struct pvr2_context *pvr2_context_notify_first;
static struct pvr2_context *pvr2_context_notify_last;
static DEFINE_MUTEX(pvr2_context_mutex);
static DECLARE_WAIT_QUEUE_HEAD(pvr2_context_sync_data);
static DECLARE_WAIT_QUEUE_HEAD(pvr2_context_cleanup_data);
static int pvr2_context_cleanup_flag;
static int pvr2_context_cleaned_flag;
static struct task_struct *pvr2_context_thread_ptr;


static void pvr2_context_set_notify(struct pvr2_context *mp, int fl)
{
	int signal_flag = 0;
	mutex_lock(&pvr2_context_mutex);
	if (fl) {
		if (!mp->notify_flag) {
			signal_flag = (pvr2_context_notify_first == NULL);
			mp->notify_prev = pvr2_context_notify_last;
			mp->notify_next = NULL;
			pvr2_context_notify_last = mp;
			if (mp->notify_prev) {
				mp->notify_prev->notify_next = mp;
			} else {
				pvr2_context_notify_first = mp;
			}
			mp->notify_flag = !0;
		}
	} else {
		if (mp->notify_flag) {
			mp->notify_flag = 0;
			if (mp->notify_next) {
				mp->notify_next->notify_prev = mp->notify_prev;
			} else {
				pvr2_context_notify_last = mp->notify_prev;
			}
			if (mp->notify_prev) {
				mp->notify_prev->notify_next = mp->notify_next;
			} else {
				pvr2_context_notify_first = mp->notify_next;
			}
		}
	}
	mutex_unlock(&pvr2_context_mutex);
	if (signal_flag) wake_up(&pvr2_context_sync_data);
}


static void pvr2_context_destroy(struct pvr2_context *mp)
{
	pvr2_trace(PVR2_TRACE_CTXT,"pvr2_context %p (destroy)",mp);
	pvr2_hdw_destroy(mp->hdw);
	pvr2_context_set_notify(mp, 0);
	mutex_lock(&pvr2_context_mutex);
	if (mp->exist_next) {
		mp->exist_next->exist_prev = mp->exist_prev;
	} else {
		pvr2_context_exist_last = mp->exist_prev;
	}
	if (mp->exist_prev) {
		mp->exist_prev->exist_next = mp->exist_next;
	} else {
		pvr2_context_exist_first = mp->exist_next;
	}
	if (!pvr2_context_exist_first) {
		/* Trigger wakeup on control thread in case it is waiting
		   for an exit condition. */
		wake_up(&pvr2_context_sync_data);
	}
	mutex_unlock(&pvr2_context_mutex);
	kfree(mp);
}


static void pvr2_context_notify(void *ptr)
{
	struct pvr2_context *mp = ptr;

	pvr2_context_set_notify(mp,!0);
}


static void pvr2_context_check(struct pvr2_context *mp)
{
	struct pvr2_channel *ch1, *ch2;
	pvr2_trace(PVR2_TRACE_CTXT,
		   "pvr2_context %p (notify)", mp);
	if (!mp->initialized_flag && !mp->disconnect_flag) {
		mp->initialized_flag = !0;
		pvr2_trace(PVR2_TRACE_CTXT,
			   "pvr2_context %p (initialize)", mp);
		/* Finish hardware initialization */
		if (pvr2_hdw_initialize(mp->hdw, pvr2_context_notify, mp)) {
			mp->video_stream.stream =
				pvr2_hdw_get_video_stream(mp->hdw);
			/* Trigger interface initialization.  By doing this
			   here initialization runs in our own safe and
			   cozy thread context. */
			if (mp->setup_func) mp->setup_func(mp);
		} else {
			pvr2_trace(PVR2_TRACE_CTXT,
				   "pvr2_context %p (thread skipping setup)",
				   mp);
			/* Even though initialization did not succeed,
			   we're still going to continue anyway.  We need
			   to do this in order to await the expected
			   disconnect (which we will detect in the normal
			   course of operation). */
		}
	}

	for (ch1 = mp->mc_first; ch1; ch1 = ch2) {
		ch2 = ch1->mc_next;
		if (ch1->check_func) ch1->check_func(ch1);
	}

	if (mp->disconnect_flag && !mp->mc_first) {
		/* Go away... */
		pvr2_context_destroy(mp);
		return;
	}
}


static int pvr2_context_shutok(void)
{
	return pvr2_context_cleanup_flag && (pvr2_context_exist_first == NULL);
}


static int pvr2_context_thread_func(void *foo)
{
	struct pvr2_context *mp;

	pvr2_trace(PVR2_TRACE_CTXT,"pvr2_context thread start");

	do {
		while ((mp = pvr2_context_notify_first) != NULL) {
			pvr2_context_set_notify(mp, 0);
			pvr2_context_check(mp);
		}
		wait_event_interruptible(
			pvr2_context_sync_data,
			((pvr2_context_notify_first != NULL) ||
			 pvr2_context_shutok()));
	} while (!pvr2_context_shutok());

	pvr2_context_cleaned_flag = !0;
	wake_up(&pvr2_context_cleanup_data);

	pvr2_trace(PVR2_TRACE_CTXT,"pvr2_context thread cleaned up");

	wait_event_interruptible(
		pvr2_context_sync_data,
		kthread_should_stop());

	pvr2_trace(PVR2_TRACE_CTXT,"pvr2_context thread end");

	return 0;
}


int pvr2_context_global_init(void)
{
	pvr2_context_thread_ptr = kthread_run(pvr2_context_thread_func,
					      NULL,
					      "pvrusb2-context");
	return IS_ERR(pvr2_context_thread_ptr) ? -ENOMEM : 0;
}


void pvr2_context_global_done(void)
{
	pvr2_context_cleanup_flag = !0;
	wake_up(&pvr2_context_sync_data);
	wait_event_interruptible(
		pvr2_context_cleanup_data,
		pvr2_context_cleaned_flag);
	kthread_stop(pvr2_context_thread_ptr);
}


struct pvr2_context *pvr2_context_create(
	struct usb_interface *intf,
	const struct usb_device_id *devid,
	void (*setup_func)(struct pvr2_context *))
{
	struct pvr2_context *mp = NULL;
	mp = kzalloc(sizeof(*mp),GFP_KERNEL);
	if (!mp) goto done;
	pvr2_trace(PVR2_TRACE_CTXT,"pvr2_context %p (create)",mp);
	mp->setup_func = setup_func;
	mutex_init(&mp->mutex);
	mutex_lock(&pvr2_context_mutex);
	mp->exist_prev = pvr2_context_exist_last;
	mp->exist_next = NULL;
	pvr2_context_exist_last = mp;
	if (mp->exist_prev) {
		mp->exist_prev->exist_next = mp;
	} else {
		pvr2_context_exist_first = mp;
	}
	mutex_unlock(&pvr2_context_mutex);
	mp->hdw = pvr2_hdw_create(intf,devid);
	if (!mp->hdw) {
		pvr2_context_destroy(mp);
		mp = NULL;
		goto done;
	}
	pvr2_context_set_notify(mp, !0);
 done:
	return mp;
}


static void pvr2_context_reset_input_limits(struct pvr2_context *mp)
{
	unsigned int tmsk,mmsk;
	struct pvr2_channel *cp;
	struct pvr2_hdw *hdw = mp->hdw;
	mmsk = pvr2_hdw_get_input_available(hdw);
	tmsk = mmsk;
	for (cp = mp->mc_first; cp; cp = cp->mc_next) {
		if (!cp->input_mask) continue;
		tmsk &= cp->input_mask;
	}
	pvr2_hdw_set_input_allowed(hdw,mmsk,tmsk);
	pvr2_hdw_commit_ctl(hdw);
}


static void pvr2_context_enter(struct pvr2_context *mp)
{
	mutex_lock(&mp->mutex);
}


static void pvr2_context_exit(struct pvr2_context *mp)
{
	int destroy_flag = 0;
	if (!(mp->mc_first || !mp->disconnect_flag)) {
		destroy_flag = !0;
	}
	mutex_unlock(&mp->mutex);
	if (destroy_flag) pvr2_context_notify(mp);
}


void pvr2_context_disconnect(struct pvr2_context *mp)
{
	pvr2_hdw_disconnect(mp->hdw);
	if (!pvr2_context_shutok())
		pvr2_context_notify(mp);
	mp->disconnect_flag = !0;
}


void pvr2_channel_init(struct pvr2_channel *cp,struct pvr2_context *mp)
{
	pvr2_context_enter(mp);
	cp->hdw = mp->hdw;
	cp->mc_head = mp;
	cp->mc_next = NULL;
	cp->mc_prev = mp->mc_last;
	if (mp->mc_last) {
		mp->mc_last->mc_next = cp;
	} else {
		mp->mc_first = cp;
	}
	mp->mc_last = cp;
	pvr2_context_exit(mp);
}


static void pvr2_channel_disclaim_stream(struct pvr2_channel *cp)
{
	if (!cp->stream) return;
	pvr2_stream_kill(cp->stream->stream);
	cp->stream->user = NULL;
	cp->stream = NULL;
}


void pvr2_channel_done(struct pvr2_channel *cp)
{
	struct pvr2_context *mp = cp->mc_head;
	pvr2_context_enter(mp);
	cp->input_mask = 0;
	pvr2_channel_disclaim_stream(cp);
	pvr2_context_reset_input_limits(mp);
	if (cp->mc_next) {
		cp->mc_next->mc_prev = cp->mc_prev;
	} else {
		mp->mc_last = cp->mc_prev;
	}
	if (cp->mc_prev) {
		cp->mc_prev->mc_next = cp->mc_next;
	} else {
		mp->mc_first = cp->mc_next;
	}
	cp->hdw = NULL;
	pvr2_context_exit(mp);
}


int pvr2_channel_limit_inputs(struct pvr2_channel *cp,unsigned int cmsk)
{
	unsigned int tmsk,mmsk;
	int ret = 0;
	struct pvr2_channel *p2;
	struct pvr2_hdw *hdw = cp->hdw;

	mmsk = pvr2_hdw_get_input_available(hdw);
	cmsk &= mmsk;
	if (cmsk == cp->input_mask) {
		/* No change; nothing to do */
		return 0;
	}

	pvr2_context_enter(cp->mc_head);
	do {
		if (!cmsk) {
			cp->input_mask = 0;
			pvr2_context_reset_input_limits(cp->mc_head);
			break;
		}
		tmsk = mmsk;
		for (p2 = cp->mc_head->mc_first; p2; p2 = p2->mc_next) {
			if (p2 == cp) continue;
			if (!p2->input_mask) continue;
			tmsk &= p2->input_mask;
		}
		if (!(tmsk & cmsk)) {
			ret = -EPERM;
			break;
		}
		tmsk &= cmsk;
		if ((ret = pvr2_hdw_set_input_allowed(hdw,mmsk,tmsk)) != 0) {
			/* Internal failure changing allowed list; probably
			   should not happen, but react if it does. */
			break;
		}
		cp->input_mask = cmsk;
		pvr2_hdw_commit_ctl(hdw);
	} while (0);
	pvr2_context_exit(cp->mc_head);
	return ret;
}


unsigned int pvr2_channel_get_limited_inputs(struct pvr2_channel *cp)
{
	return cp->input_mask;
}


int pvr2_channel_claim_stream(struct pvr2_channel *cp,
			      struct pvr2_context_stream *sp)
{
	int code = 0;
	pvr2_context_enter(cp->mc_head); do {
		if (sp == cp->stream) break;
		if (sp && sp->user) {
			code = -EBUSY;
			break;
		}
		pvr2_channel_disclaim_stream(cp);
		if (!sp) break;
		sp->user = cp;
		cp->stream = sp;
	} while (0);
	pvr2_context_exit(cp->mc_head);
	return code;
}


// This is the marker for the real beginning of a legitimate mpeg2 stream.
static char stream_sync_key[] = {
	0x00, 0x00, 0x01, 0xba,
};

struct pvr2_ioread *pvr2_channel_create_mpeg_stream(
	struct pvr2_context_stream *sp)
{
	struct pvr2_ioread *cp;
	cp = pvr2_ioread_create();
	if (!cp) return NULL;
	pvr2_ioread_setup(cp,sp->stream);
	pvr2_ioread_set_sync_key(cp,stream_sync_key,sizeof(stream_sync_key));
	return cp;
}
