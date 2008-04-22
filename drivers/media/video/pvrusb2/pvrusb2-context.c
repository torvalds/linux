/*
 *  $Id$
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "pvrusb2-context.h"
#include "pvrusb2-io.h"
#include "pvrusb2-ioread.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-debug.h"
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>


static void pvr2_context_destroy(struct pvr2_context *mp)
{
	pvr2_trace(PVR2_TRACE_STRUCT,"Destroying pvr_main id=%p",mp);
	if (mp->hdw) pvr2_hdw_destroy(mp->hdw);
	kfree(mp);
}


static void pvr2_context_state_check(struct pvr2_context *mp)
{
	if (mp->init_flag) return;

	switch (pvr2_hdw_get_state(mp->hdw)) {
	case PVR2_STATE_WARM: break;
	case PVR2_STATE_ERROR: break;
	case PVR2_STATE_READY: break;
	case PVR2_STATE_RUN: break;
	default: return;
	}

	pvr2_context_enter(mp); do {
		mp->init_flag = !0;
		mp->video_stream.stream = pvr2_hdw_get_video_stream(mp->hdw);
		if (mp->setup_func) {
			mp->setup_func(mp);
		}
	} while (0); pvr2_context_exit(mp);
 }


struct pvr2_context *pvr2_context_create(
	struct usb_interface *intf,
	const struct usb_device_id *devid,
	void (*setup_func)(struct pvr2_context *))
{
	struct pvr2_context *mp = NULL;
	mp = kzalloc(sizeof(*mp),GFP_KERNEL);
	if (!mp) goto done;
	pvr2_trace(PVR2_TRACE_STRUCT,"Creating pvr_main id=%p",mp);
	mp->setup_func = setup_func;
	mutex_init(&mp->mutex);
	mp->hdw = pvr2_hdw_create(intf,devid);
	if (!mp->hdw) {
		pvr2_context_destroy(mp);
		mp = NULL;
		goto done;
	}
	pvr2_hdw_set_state_callback(mp->hdw,
				    (void (*)(void *))pvr2_context_state_check,
				    mp);
	pvr2_context_state_check(mp);
 done:
	return mp;
}


void pvr2_context_enter(struct pvr2_context *mp)
{
	mutex_lock(&mp->mutex);
	pvr2_trace(PVR2_TRACE_CREG,"pvr2_context_enter(id=%p)",mp);
}


void pvr2_context_exit(struct pvr2_context *mp)
{
	int destroy_flag = 0;
	if (!(mp->mc_first || !mp->disconnect_flag)) {
		destroy_flag = !0;
	}
	pvr2_trace(PVR2_TRACE_CREG,"pvr2_context_exit(id=%p) outside",mp);
	mutex_unlock(&mp->mutex);
	if (destroy_flag) pvr2_context_destroy(mp);
}


static void pvr2_context_run_checks(struct pvr2_context *mp)
{
	struct pvr2_channel *ch1,*ch2;
	for (ch1 = mp->mc_first; ch1; ch1 = ch2) {
		ch2 = ch1->mc_next;
		if (ch1->check_func) {
			ch1->check_func(ch1);
		}
	}
}


void pvr2_context_disconnect(struct pvr2_context *mp)
{
	pvr2_context_enter(mp); do {
		pvr2_hdw_disconnect(mp->hdw);
		mp->disconnect_flag = !0;
		pvr2_context_run_checks(mp);
	} while (0); pvr2_context_exit(mp);
}


void pvr2_channel_init(struct pvr2_channel *cp,struct pvr2_context *mp)
{
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
	pvr2_channel_disclaim_stream(cp);
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
}


int pvr2_channel_claim_stream(struct pvr2_channel *cp,
			      struct pvr2_context_stream *sp)
{
	int code = 0;
	pvr2_context_enter(cp->mc_head); do {
		if (sp == cp->stream) break;
		if (sp->user) {
			code = -EBUSY;
			break;
		}
		pvr2_channel_disclaim_stream(cp);
		if (!sp) break;
		sp->user = cp;
		cp->stream = sp;
	} while (0); pvr2_context_exit(cp->mc_head);
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


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
