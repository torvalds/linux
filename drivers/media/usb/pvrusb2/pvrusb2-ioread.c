/*
 *
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

#include "pvrusb2-ioread.h"
#include "pvrusb2-debug.h"
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#define BUFFER_COUNT 32
#define BUFFER_SIZE PAGE_ALIGN(0x4000)

struct pvr2_ioread {
	struct pvr2_stream *stream;
	char *buffer_storage[BUFFER_COUNT];
	char *sync_key_ptr;
	unsigned int sync_key_len;
	unsigned int sync_buf_offs;
	unsigned int sync_state;
	unsigned int sync_trashed_count;
	int enabled;         // Streaming is on
	int spigot_open;     // OK to pass data to client
	int stream_running;  // Passing data to client now

	/* State relevant to current buffer being read */
	struct pvr2_buffer *c_buf;
	char *c_data_ptr;
	unsigned int c_data_len;
	unsigned int c_data_offs;
	struct mutex mutex;
};

static int pvr2_ioread_init(struct pvr2_ioread *cp)
{
	unsigned int idx;

	cp->stream = NULL;
	mutex_init(&cp->mutex);

	for (idx = 0; idx < BUFFER_COUNT; idx++) {
		cp->buffer_storage[idx] = kmalloc(BUFFER_SIZE,GFP_KERNEL);
		if (!(cp->buffer_storage[idx])) break;
	}

	if (idx < BUFFER_COUNT) {
		// An allocation appears to have failed
		for (idx = 0; idx < BUFFER_COUNT; idx++) {
			if (!(cp->buffer_storage[idx])) continue;
			kfree(cp->buffer_storage[idx]);
		}
		return -ENOMEM;
	}
	return 0;
}

static void pvr2_ioread_done(struct pvr2_ioread *cp)
{
	unsigned int idx;

	pvr2_ioread_setup(cp,NULL);
	for (idx = 0; idx < BUFFER_COUNT; idx++) {
		if (!(cp->buffer_storage[idx])) continue;
		kfree(cp->buffer_storage[idx]);
	}
}

struct pvr2_ioread *pvr2_ioread_create(void)
{
	struct pvr2_ioread *cp;
	cp = kzalloc(sizeof(*cp),GFP_KERNEL);
	if (!cp) return NULL;
	pvr2_trace(PVR2_TRACE_STRUCT,"pvr2_ioread_create id=%p",cp);
	if (pvr2_ioread_init(cp) < 0) {
		kfree(cp);
		return NULL;
	}
	return cp;
}

void pvr2_ioread_destroy(struct pvr2_ioread *cp)
{
	if (!cp) return;
	pvr2_ioread_done(cp);
	pvr2_trace(PVR2_TRACE_STRUCT,"pvr2_ioread_destroy id=%p",cp);
	if (cp->sync_key_ptr) {
		kfree(cp->sync_key_ptr);
		cp->sync_key_ptr = NULL;
	}
	kfree(cp);
}

void pvr2_ioread_set_sync_key(struct pvr2_ioread *cp,
			      const char *sync_key_ptr,
			      unsigned int sync_key_len)
{
	if (!cp) return;

	if (!sync_key_ptr) sync_key_len = 0;
	if ((sync_key_len == cp->sync_key_len) &&
	    ((!sync_key_len) ||
	     (!memcmp(sync_key_ptr,cp->sync_key_ptr,sync_key_len)))) return;

	if (sync_key_len != cp->sync_key_len) {
		if (cp->sync_key_ptr) {
			kfree(cp->sync_key_ptr);
			cp->sync_key_ptr = NULL;
		}
		cp->sync_key_len = 0;
		if (sync_key_len) {
			cp->sync_key_ptr = kmalloc(sync_key_len,GFP_KERNEL);
			if (cp->sync_key_ptr) {
				cp->sync_key_len = sync_key_len;
			}
		}
	}
	if (!cp->sync_key_len) return;
	memcpy(cp->sync_key_ptr,sync_key_ptr,cp->sync_key_len);
}

static void pvr2_ioread_stop(struct pvr2_ioread *cp)
{
	if (!(cp->enabled)) return;
	pvr2_trace(PVR2_TRACE_START_STOP,
		   "/*---TRACE_READ---*/ pvr2_ioread_stop id=%p",cp);
	pvr2_stream_kill(cp->stream);
	cp->c_buf = NULL;
	cp->c_data_ptr = NULL;
	cp->c_data_len = 0;
	cp->c_data_offs = 0;
	cp->enabled = 0;
	cp->stream_running = 0;
	cp->spigot_open = 0;
	if (cp->sync_state) {
		pvr2_trace(PVR2_TRACE_DATA_FLOW,
			   "/*---TRACE_READ---*/ sync_state <== 0");
		cp->sync_state = 0;
	}
}

static int pvr2_ioread_start(struct pvr2_ioread *cp)
{
	int stat;
	struct pvr2_buffer *bp;
	if (cp->enabled) return 0;
	if (!(cp->stream)) return 0;
	pvr2_trace(PVR2_TRACE_START_STOP,
		   "/*---TRACE_READ---*/ pvr2_ioread_start id=%p",cp);
	while ((bp = pvr2_stream_get_idle_buffer(cp->stream)) != NULL) {
		stat = pvr2_buffer_queue(bp);
		if (stat < 0) {
			pvr2_trace(PVR2_TRACE_DATA_FLOW,
				   "/*---TRACE_READ---*/ pvr2_ioread_start id=%p error=%d",
				   cp,stat);
			pvr2_ioread_stop(cp);
			return stat;
		}
	}
	cp->enabled = !0;
	cp->c_buf = NULL;
	cp->c_data_ptr = NULL;
	cp->c_data_len = 0;
	cp->c_data_offs = 0;
	cp->stream_running = 0;
	if (cp->sync_key_len) {
		pvr2_trace(PVR2_TRACE_DATA_FLOW,
			   "/*---TRACE_READ---*/ sync_state <== 1");
		cp->sync_state = 1;
		cp->sync_trashed_count = 0;
		cp->sync_buf_offs = 0;
	}
	cp->spigot_open = 0;
	return 0;
}

struct pvr2_stream *pvr2_ioread_get_stream(struct pvr2_ioread *cp)
{
	return cp->stream;
}

int pvr2_ioread_setup(struct pvr2_ioread *cp,struct pvr2_stream *sp)
{
	int ret;
	unsigned int idx;
	struct pvr2_buffer *bp;

	mutex_lock(&cp->mutex);
	do {
		if (cp->stream) {
			pvr2_trace(PVR2_TRACE_START_STOP,
				   "/*---TRACE_READ---*/ pvr2_ioread_setup (tear-down) id=%p",
				   cp);
			pvr2_ioread_stop(cp);
			pvr2_stream_kill(cp->stream);
			if (pvr2_stream_get_buffer_count(cp->stream)) {
				pvr2_stream_set_buffer_count(cp->stream,0);
			}
			cp->stream = NULL;
		}
		if (sp) {
			pvr2_trace(PVR2_TRACE_START_STOP,
				   "/*---TRACE_READ---*/ pvr2_ioread_setup (setup) id=%p",
				   cp);
			pvr2_stream_kill(sp);
			ret = pvr2_stream_set_buffer_count(sp,BUFFER_COUNT);
			if (ret < 0) {
				mutex_unlock(&cp->mutex);
				return ret;
			}
			for (idx = 0; idx < BUFFER_COUNT; idx++) {
				bp = pvr2_stream_get_buffer(sp,idx);
				pvr2_buffer_set_buffer(bp,
						       cp->buffer_storage[idx],
						       BUFFER_SIZE);
			}
			cp->stream = sp;
		}
	} while (0);
	mutex_unlock(&cp->mutex);

	return 0;
}

int pvr2_ioread_set_enabled(struct pvr2_ioread *cp,int fl)
{
	int ret = 0;
	if ((!fl) == (!(cp->enabled))) return ret;

	mutex_lock(&cp->mutex);
	do {
		if (fl) {
			ret = pvr2_ioread_start(cp);
		} else {
			pvr2_ioread_stop(cp);
		}
	} while (0);
	mutex_unlock(&cp->mutex);
	return ret;
}

static int pvr2_ioread_get_buffer(struct pvr2_ioread *cp)
{
	int stat;

	while (cp->c_data_len <= cp->c_data_offs) {
		if (cp->c_buf) {
			// Flush out current buffer first.
			stat = pvr2_buffer_queue(cp->c_buf);
			if (stat < 0) {
				// Streaming error...
				pvr2_trace(PVR2_TRACE_DATA_FLOW,
					   "/*---TRACE_READ---*/ pvr2_ioread_read id=%p queue_error=%d",
					   cp,stat);
				pvr2_ioread_stop(cp);
				return 0;
			}
			cp->c_buf = NULL;
			cp->c_data_ptr = NULL;
			cp->c_data_len = 0;
			cp->c_data_offs = 0;
		}
		// Now get a freshly filled buffer.
		cp->c_buf = pvr2_stream_get_ready_buffer(cp->stream);
		if (!cp->c_buf) break; // Nothing ready; done.
		cp->c_data_len = pvr2_buffer_get_count(cp->c_buf);
		if (!cp->c_data_len) {
			// Nothing transferred.  Was there an error?
			stat = pvr2_buffer_get_status(cp->c_buf);
			if (stat < 0) {
				// Streaming error...
				pvr2_trace(PVR2_TRACE_DATA_FLOW,
					   "/*---TRACE_READ---*/ pvr2_ioread_read id=%p buffer_error=%d",
					   cp,stat);
				pvr2_ioread_stop(cp);
				// Give up.
				return 0;
			}
			// Start over...
			continue;
		}
		cp->c_data_offs = 0;
		cp->c_data_ptr = cp->buffer_storage[
			pvr2_buffer_get_id(cp->c_buf)];
	}
	return !0;
}

static void pvr2_ioread_filter(struct pvr2_ioread *cp)
{
	unsigned int idx;
	if (!cp->enabled) return;
	if (cp->sync_state != 1) return;

	// Search the stream for our synchronization key.  This is made
	// complicated by the fact that in order to be honest with
	// ourselves here we must search across buffer boundaries...
	mutex_lock(&cp->mutex);
	while (1) {
		// Ensure we have a buffer
		if (!pvr2_ioread_get_buffer(cp)) break;
		if (!cp->c_data_len) break;

		// Now walk the buffer contents until we match the key or
		// run out of buffer data.
		for (idx = cp->c_data_offs; idx < cp->c_data_len; idx++) {
			if (cp->sync_buf_offs >= cp->sync_key_len) break;
			if (cp->c_data_ptr[idx] ==
			    cp->sync_key_ptr[cp->sync_buf_offs]) {
				// Found the next key byte
				(cp->sync_buf_offs)++;
			} else {
				// Whoops, mismatched.  Start key over...
				cp->sync_buf_offs = 0;
			}
		}

		// Consume what we've walked through
		cp->c_data_offs += idx;
		cp->sync_trashed_count += idx;

		// If we've found the key, then update state and get out.
		if (cp->sync_buf_offs >= cp->sync_key_len) {
			cp->sync_trashed_count -= cp->sync_key_len;
			pvr2_trace(PVR2_TRACE_DATA_FLOW,
				   "/*---TRACE_READ---*/ sync_state <== 2 (skipped %u bytes)",
				   cp->sync_trashed_count);
			cp->sync_state = 2;
			cp->sync_buf_offs = 0;
			break;
		}

		if (cp->c_data_offs < cp->c_data_len) {
			// Sanity check - should NEVER get here
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "ERROR: pvr2_ioread filter sync problem len=%u offs=%u",
				   cp->c_data_len,cp->c_data_offs);
			// Get out so we don't get stuck in an infinite
			// loop.
			break;
		}

		continue; // (for clarity)
	}
	mutex_unlock(&cp->mutex);
}

int pvr2_ioread_avail(struct pvr2_ioread *cp)
{
	int ret;
	if (!(cp->enabled)) {
		// Stream is not enabled; so this is an I/O error
		return -EIO;
	}

	if (cp->sync_state == 1) {
		pvr2_ioread_filter(cp);
		if (cp->sync_state == 1) return -EAGAIN;
	}

	ret = 0;
	if (cp->stream_running) {
		if (!pvr2_stream_get_ready_count(cp->stream)) {
			// No data available at all right now.
			ret = -EAGAIN;
		}
	} else {
		if (pvr2_stream_get_ready_count(cp->stream) < BUFFER_COUNT/2) {
			// Haven't buffered up enough yet; try again later
			ret = -EAGAIN;
		}
	}

	if ((!(cp->spigot_open)) != (!(ret == 0))) {
		cp->spigot_open = (ret == 0);
		pvr2_trace(PVR2_TRACE_DATA_FLOW,
			   "/*---TRACE_READ---*/ data is %s",
			   cp->spigot_open ? "available" : "pending");
	}

	return ret;
}

int pvr2_ioread_read(struct pvr2_ioread *cp,void __user *buf,unsigned int cnt)
{
	unsigned int copied_cnt;
	unsigned int bcnt;
	const char *src;
	int stat;
	int ret = 0;
	unsigned int req_cnt = cnt;

	if (!cnt) {
		pvr2_trace(PVR2_TRACE_TRAP,
			   "/*---TRACE_READ---*/ pvr2_ioread_read id=%p ZERO Request? Returning zero.",
cp);
		return 0;
	}

	stat = pvr2_ioread_avail(cp);
	if (stat < 0) return stat;

	cp->stream_running = !0;

	mutex_lock(&cp->mutex);
	do {

		// Suck data out of the buffers and copy to the user
		copied_cnt = 0;
		if (!buf) cnt = 0;
		while (1) {
			if (!pvr2_ioread_get_buffer(cp)) {
				ret = -EIO;
				break;
			}

			if (!cnt) break;

			if (cp->sync_state == 2) {
				// We're repeating the sync key data into
				// the stream.
				src = cp->sync_key_ptr + cp->sync_buf_offs;
				bcnt = cp->sync_key_len - cp->sync_buf_offs;
			} else {
				// Normal buffer copy
				src = cp->c_data_ptr + cp->c_data_offs;
				bcnt = cp->c_data_len - cp->c_data_offs;
			}

			if (!bcnt) break;

			// Don't run past user's buffer
			if (bcnt > cnt) bcnt = cnt;

			if (copy_to_user(buf,src,bcnt)) {
				// User supplied a bad pointer?
				// Give up - this *will* cause data
				// to be lost.
				ret = -EFAULT;
				break;
			}
			cnt -= bcnt;
			buf += bcnt;
			copied_cnt += bcnt;

			if (cp->sync_state == 2) {
				// Update offset inside sync key that we're
				// repeating back out.
				cp->sync_buf_offs += bcnt;
				if (cp->sync_buf_offs >= cp->sync_key_len) {
					// Consumed entire key; switch mode
					// to normal.
					pvr2_trace(PVR2_TRACE_DATA_FLOW,
						   "/*---TRACE_READ---*/ sync_state <== 0");
					cp->sync_state = 0;
				}
			} else {
				// Update buffer offset.
				cp->c_data_offs += bcnt;
			}
		}

	} while (0);
	mutex_unlock(&cp->mutex);

	if (!ret) {
		if (copied_cnt) {
			// If anything was copied, return that count
			ret = copied_cnt;
		} else {
			// Nothing copied; suggest to caller that another
			// attempt should be tried again later
			ret = -EAGAIN;
		}
	}

	pvr2_trace(PVR2_TRACE_DATA_FLOW,
		   "/*---TRACE_READ---*/ pvr2_ioread_read id=%p request=%d result=%d",
		   cp,req_cnt,ret);
	return ret;
}
