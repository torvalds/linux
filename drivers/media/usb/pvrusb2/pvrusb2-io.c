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

#include "pvrusb2-io.h"
#include "pvrusb2-debug.h"
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>

static const char *pvr2_buffer_state_decode(enum pvr2_buffer_state);

#define BUFFER_SIG 0x47653271

// #define SANITY_CHECK_BUFFERS


#ifdef SANITY_CHECK_BUFFERS
#define BUFFER_CHECK(bp) do { \
	if ((bp)->signature != BUFFER_SIG) { \
		pvr2_trace(PVR2_TRACE_ERROR_LEGS, \
		"Buffer %p is bad at %s:%d", \
		(bp),__FILE__,__LINE__); \
		pvr2_buffer_describe(bp,"BadSig"); \
		BUG(); \
	} \
} while (0)
#else
#define BUFFER_CHECK(bp) do {} while(0)
#endif

struct pvr2_stream {
	/* Buffers queued for reading */
	struct list_head queued_list;
	unsigned int q_count;
	unsigned int q_bcount;
	/* Buffers with retrieved data */
	struct list_head ready_list;
	unsigned int r_count;
	unsigned int r_bcount;
	/* Buffers available for use */
	struct list_head idle_list;
	unsigned int i_count;
	unsigned int i_bcount;
	/* Pointers to all buffers */
	struct pvr2_buffer **buffers;
	/* Array size of buffers */
	unsigned int buffer_slot_count;
	/* Total buffers actually in circulation */
	unsigned int buffer_total_count;
	/* Designed number of buffers to be in circulation */
	unsigned int buffer_target_count;
	/* Executed when ready list become non-empty */
	pvr2_stream_callback callback_func;
	void *callback_data;
	/* Context for transfer endpoint */
	struct usb_device *dev;
	int endpoint;
	/* Overhead for mutex enforcement */
	spinlock_t list_lock;
	struct mutex mutex;
	/* Tracking state for tolerating errors */
	unsigned int fail_count;
	unsigned int fail_tolerance;

	unsigned int buffers_processed;
	unsigned int buffers_failed;
	unsigned int bytes_processed;
};

struct pvr2_buffer {
	int id;
	int signature;
	enum pvr2_buffer_state state;
	void *ptr;               /* Pointer to storage area */
	unsigned int max_count;  /* Size of storage area */
	unsigned int used_count; /* Amount of valid data in storage area */
	int status;              /* Transfer result status */
	struct pvr2_stream *stream;
	struct list_head list_overhead;
	struct urb *purb;
};

static const char *pvr2_buffer_state_decode(enum pvr2_buffer_state st)
{
	switch (st) {
	case pvr2_buffer_state_none: return "none";
	case pvr2_buffer_state_idle: return "idle";
	case pvr2_buffer_state_queued: return "queued";
	case pvr2_buffer_state_ready: return "ready";
	}
	return "unknown";
}

#ifdef SANITY_CHECK_BUFFERS
static void pvr2_buffer_describe(struct pvr2_buffer *bp,const char *msg)
{
	pvr2_trace(PVR2_TRACE_INFO,
		   "buffer%s%s %p state=%s id=%d status=%d"
		   " stream=%p purb=%p sig=0x%x",
		   (msg ? " " : ""),
		   (msg ? msg : ""),
		   bp,
		   (bp ? pvr2_buffer_state_decode(bp->state) : "(invalid)"),
		   (bp ? bp->id : 0),
		   (bp ? bp->status : 0),
		   (bp ? bp->stream : NULL),
		   (bp ? bp->purb : NULL),
		   (bp ? bp->signature : 0));
}
#endif  /*  SANITY_CHECK_BUFFERS  */

static void pvr2_buffer_remove(struct pvr2_buffer *bp)
{
	unsigned int *cnt;
	unsigned int *bcnt;
	unsigned int ccnt;
	struct pvr2_stream *sp = bp->stream;
	switch (bp->state) {
	case pvr2_buffer_state_idle:
		cnt = &sp->i_count;
		bcnt = &sp->i_bcount;
		ccnt = bp->max_count;
		break;
	case pvr2_buffer_state_queued:
		cnt = &sp->q_count;
		bcnt = &sp->q_bcount;
		ccnt = bp->max_count;
		break;
	case pvr2_buffer_state_ready:
		cnt = &sp->r_count;
		bcnt = &sp->r_bcount;
		ccnt = bp->used_count;
		break;
	default:
		return;
	}
	list_del_init(&bp->list_overhead);
	(*cnt)--;
	(*bcnt) -= ccnt;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/"
		   " bufferPool     %8s dec cap=%07d cnt=%02d",
		   pvr2_buffer_state_decode(bp->state),*bcnt,*cnt);
	bp->state = pvr2_buffer_state_none;
}

static void pvr2_buffer_set_none(struct pvr2_buffer *bp)
{
	unsigned long irq_flags;
	struct pvr2_stream *sp;
	BUFFER_CHECK(bp);
	sp = bp->stream;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/ bufferState    %p %6s --> %6s",
		   bp,
		   pvr2_buffer_state_decode(bp->state),
		   pvr2_buffer_state_decode(pvr2_buffer_state_none));
	spin_lock_irqsave(&sp->list_lock,irq_flags);
	pvr2_buffer_remove(bp);
	spin_unlock_irqrestore(&sp->list_lock,irq_flags);
}

static int pvr2_buffer_set_ready(struct pvr2_buffer *bp)
{
	int fl;
	unsigned long irq_flags;
	struct pvr2_stream *sp;
	BUFFER_CHECK(bp);
	sp = bp->stream;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/ bufferState    %p %6s --> %6s",
		   bp,
		   pvr2_buffer_state_decode(bp->state),
		   pvr2_buffer_state_decode(pvr2_buffer_state_ready));
	spin_lock_irqsave(&sp->list_lock,irq_flags);
	fl = (sp->r_count == 0);
	pvr2_buffer_remove(bp);
	list_add_tail(&bp->list_overhead,&sp->ready_list);
	bp->state = pvr2_buffer_state_ready;
	(sp->r_count)++;
	sp->r_bcount += bp->used_count;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/"
		   " bufferPool     %8s inc cap=%07d cnt=%02d",
		   pvr2_buffer_state_decode(bp->state),
		   sp->r_bcount,sp->r_count);
	spin_unlock_irqrestore(&sp->list_lock,irq_flags);
	return fl;
}

static void pvr2_buffer_set_idle(struct pvr2_buffer *bp)
{
	unsigned long irq_flags;
	struct pvr2_stream *sp;
	BUFFER_CHECK(bp);
	sp = bp->stream;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/ bufferState    %p %6s --> %6s",
		   bp,
		   pvr2_buffer_state_decode(bp->state),
		   pvr2_buffer_state_decode(pvr2_buffer_state_idle));
	spin_lock_irqsave(&sp->list_lock,irq_flags);
	pvr2_buffer_remove(bp);
	list_add_tail(&bp->list_overhead,&sp->idle_list);
	bp->state = pvr2_buffer_state_idle;
	(sp->i_count)++;
	sp->i_bcount += bp->max_count;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/"
		   " bufferPool     %8s inc cap=%07d cnt=%02d",
		   pvr2_buffer_state_decode(bp->state),
		   sp->i_bcount,sp->i_count);
	spin_unlock_irqrestore(&sp->list_lock,irq_flags);
}

static void pvr2_buffer_set_queued(struct pvr2_buffer *bp)
{
	unsigned long irq_flags;
	struct pvr2_stream *sp;
	BUFFER_CHECK(bp);
	sp = bp->stream;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/ bufferState    %p %6s --> %6s",
		   bp,
		   pvr2_buffer_state_decode(bp->state),
		   pvr2_buffer_state_decode(pvr2_buffer_state_queued));
	spin_lock_irqsave(&sp->list_lock,irq_flags);
	pvr2_buffer_remove(bp);
	list_add_tail(&bp->list_overhead,&sp->queued_list);
	bp->state = pvr2_buffer_state_queued;
	(sp->q_count)++;
	sp->q_bcount += bp->max_count;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/"
		   " bufferPool     %8s inc cap=%07d cnt=%02d",
		   pvr2_buffer_state_decode(bp->state),
		   sp->q_bcount,sp->q_count);
	spin_unlock_irqrestore(&sp->list_lock,irq_flags);
}

static void pvr2_buffer_wipe(struct pvr2_buffer *bp)
{
	if (bp->state == pvr2_buffer_state_queued) {
		usb_kill_urb(bp->purb);
	}
}

static int pvr2_buffer_init(struct pvr2_buffer *bp,
			    struct pvr2_stream *sp,
			    unsigned int id)
{
	memset(bp,0,sizeof(*bp));
	bp->signature = BUFFER_SIG;
	bp->id = id;
	pvr2_trace(PVR2_TRACE_BUF_POOL,
		   "/*---TRACE_FLOW---*/ bufferInit     %p stream=%p",bp,sp);
	bp->stream = sp;
	bp->state = pvr2_buffer_state_none;
	INIT_LIST_HEAD(&bp->list_overhead);
	bp->purb = usb_alloc_urb(0,GFP_KERNEL);
	if (! bp->purb) return -ENOMEM;
#ifdef SANITY_CHECK_BUFFERS
	pvr2_buffer_describe(bp,"create");
#endif
	return 0;
}

static void pvr2_buffer_done(struct pvr2_buffer *bp)
{
#ifdef SANITY_CHECK_BUFFERS
	pvr2_buffer_describe(bp,"delete");
#endif
	pvr2_buffer_wipe(bp);
	pvr2_buffer_set_none(bp);
	bp->signature = 0;
	bp->stream = NULL;
	usb_free_urb(bp->purb);
	pvr2_trace(PVR2_TRACE_BUF_POOL,"/*---TRACE_FLOW---*/"
		   " bufferDone     %p",bp);
}

static int pvr2_stream_buffer_count(struct pvr2_stream *sp,unsigned int cnt)
{
	int ret;
	unsigned int scnt;

	/* Allocate buffers pointer array in multiples of 32 entries */
	if (cnt == sp->buffer_total_count) return 0;

	pvr2_trace(PVR2_TRACE_BUF_POOL,
		   "/*---TRACE_FLOW---*/ poolResize    "
		   " stream=%p cur=%d adj=%+d",
		   sp,
		   sp->buffer_total_count,
		   cnt-sp->buffer_total_count);

	scnt = cnt & ~0x1f;
	if (cnt > scnt) scnt += 0x20;

	if (cnt > sp->buffer_total_count) {
		if (scnt > sp->buffer_slot_count) {
			struct pvr2_buffer **nb;
			nb = kmalloc(scnt * sizeof(*nb),GFP_KERNEL);
			if (!nb) return -ENOMEM;
			if (sp->buffer_slot_count) {
				memcpy(nb,sp->buffers,
				       sp->buffer_slot_count * sizeof(*nb));
				kfree(sp->buffers);
			}
			sp->buffers = nb;
			sp->buffer_slot_count = scnt;
		}
		while (sp->buffer_total_count < cnt) {
			struct pvr2_buffer *bp;
			bp = kmalloc(sizeof(*bp),GFP_KERNEL);
			if (!bp) return -ENOMEM;
			ret = pvr2_buffer_init(bp,sp,sp->buffer_total_count);
			if (ret) {
				kfree(bp);
				return -ENOMEM;
			}
			sp->buffers[sp->buffer_total_count] = bp;
			(sp->buffer_total_count)++;
			pvr2_buffer_set_idle(bp);
		}
	} else {
		while (sp->buffer_total_count > cnt) {
			struct pvr2_buffer *bp;
			bp = sp->buffers[sp->buffer_total_count - 1];
			/* Paranoia */
			sp->buffers[sp->buffer_total_count - 1] = NULL;
			(sp->buffer_total_count)--;
			pvr2_buffer_done(bp);
			kfree(bp);
		}
		if (scnt < sp->buffer_slot_count) {
			struct pvr2_buffer **nb = NULL;
			if (scnt) {
				nb = kmemdup(sp->buffers, scnt * sizeof(*nb),
					     GFP_KERNEL);
				if (!nb) return -ENOMEM;
			}
			kfree(sp->buffers);
			sp->buffers = nb;
			sp->buffer_slot_count = scnt;
		}
	}
	return 0;
}

static int pvr2_stream_achieve_buffer_count(struct pvr2_stream *sp)
{
	struct pvr2_buffer *bp;
	unsigned int cnt;

	if (sp->buffer_total_count == sp->buffer_target_count) return 0;

	pvr2_trace(PVR2_TRACE_BUF_POOL,
		   "/*---TRACE_FLOW---*/"
		   " poolCheck      stream=%p cur=%d tgt=%d",
		   sp,sp->buffer_total_count,sp->buffer_target_count);

	if (sp->buffer_total_count < sp->buffer_target_count) {
		return pvr2_stream_buffer_count(sp,sp->buffer_target_count);
	}

	cnt = 0;
	while ((sp->buffer_total_count - cnt) > sp->buffer_target_count) {
		bp = sp->buffers[sp->buffer_total_count - (cnt + 1)];
		if (bp->state != pvr2_buffer_state_idle) break;
		cnt++;
	}
	if (cnt) {
		pvr2_stream_buffer_count(sp,sp->buffer_total_count - cnt);
	}

	return 0;
}

static void pvr2_stream_internal_flush(struct pvr2_stream *sp)
{
	struct list_head *lp;
	struct pvr2_buffer *bp1;
	while ((lp = sp->queued_list.next) != &sp->queued_list) {
		bp1 = list_entry(lp,struct pvr2_buffer,list_overhead);
		pvr2_buffer_wipe(bp1);
		/* At this point, we should be guaranteed that no
		   completion callback may happen on this buffer.  But it's
		   possible that it might have completed after we noticed
		   it but before we wiped it.  So double check its status
		   here first. */
		if (bp1->state != pvr2_buffer_state_queued) continue;
		pvr2_buffer_set_idle(bp1);
	}
	if (sp->buffer_total_count != sp->buffer_target_count) {
		pvr2_stream_achieve_buffer_count(sp);
	}
}

static void pvr2_stream_init(struct pvr2_stream *sp)
{
	spin_lock_init(&sp->list_lock);
	mutex_init(&sp->mutex);
	INIT_LIST_HEAD(&sp->queued_list);
	INIT_LIST_HEAD(&sp->ready_list);
	INIT_LIST_HEAD(&sp->idle_list);
}

static void pvr2_stream_done(struct pvr2_stream *sp)
{
	mutex_lock(&sp->mutex); do {
		pvr2_stream_internal_flush(sp);
		pvr2_stream_buffer_count(sp,0);
	} while (0); mutex_unlock(&sp->mutex);
}

static void buffer_complete(struct urb *urb)
{
	struct pvr2_buffer *bp = urb->context;
	struct pvr2_stream *sp;
	unsigned long irq_flags;
	BUFFER_CHECK(bp);
	sp = bp->stream;
	bp->used_count = 0;
	bp->status = 0;
	pvr2_trace(PVR2_TRACE_BUF_FLOW,
		   "/*---TRACE_FLOW---*/ bufferComplete %p stat=%d cnt=%d",
		   bp,urb->status,urb->actual_length);
	spin_lock_irqsave(&sp->list_lock,irq_flags);
	if ((!(urb->status)) ||
	    (urb->status == -ENOENT) ||
	    (urb->status == -ECONNRESET) ||
	    (urb->status == -ESHUTDOWN)) {
		(sp->buffers_processed)++;
		sp->bytes_processed += urb->actual_length;
		bp->used_count = urb->actual_length;
		if (sp->fail_count) {
			pvr2_trace(PVR2_TRACE_TOLERANCE,
				   "stream %p transfer ok"
				   " - fail count reset",sp);
			sp->fail_count = 0;
		}
	} else if (sp->fail_count < sp->fail_tolerance) {
		// We can tolerate this error, because we're below the
		// threshold...
		(sp->fail_count)++;
		(sp->buffers_failed)++;
		pvr2_trace(PVR2_TRACE_TOLERANCE,
			   "stream %p ignoring error %d"
			   " - fail count increased to %u",
			   sp,urb->status,sp->fail_count);
	} else {
		(sp->buffers_failed)++;
		bp->status = urb->status;
	}
	spin_unlock_irqrestore(&sp->list_lock,irq_flags);
	pvr2_buffer_set_ready(bp);
	if (sp && sp->callback_func) {
		sp->callback_func(sp->callback_data);
	}
}

struct pvr2_stream *pvr2_stream_create(void)
{
	struct pvr2_stream *sp;
	sp = kzalloc(sizeof(*sp),GFP_KERNEL);
	if (!sp) return sp;
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_stream_create: sp=%p",sp);
	pvr2_stream_init(sp);
	return sp;
}

void pvr2_stream_destroy(struct pvr2_stream *sp)
{
	if (!sp) return;
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_stream_destroy: sp=%p",sp);
	pvr2_stream_done(sp);
	kfree(sp);
}

void pvr2_stream_setup(struct pvr2_stream *sp,
		       struct usb_device *dev,
		       int endpoint,
		       unsigned int tolerance)
{
	mutex_lock(&sp->mutex); do {
		pvr2_stream_internal_flush(sp);
		sp->dev = dev;
		sp->endpoint = endpoint;
		sp->fail_tolerance = tolerance;
	} while(0); mutex_unlock(&sp->mutex);
}

void pvr2_stream_set_callback(struct pvr2_stream *sp,
			      pvr2_stream_callback func,
			      void *data)
{
	unsigned long irq_flags;
	mutex_lock(&sp->mutex); do {
		spin_lock_irqsave(&sp->list_lock,irq_flags);
		sp->callback_data = data;
		sp->callback_func = func;
		spin_unlock_irqrestore(&sp->list_lock,irq_flags);
	} while(0); mutex_unlock(&sp->mutex);
}

void pvr2_stream_get_stats(struct pvr2_stream *sp,
			   struct pvr2_stream_stats *stats,
			   int zero_counts)
{
	unsigned long irq_flags;
	spin_lock_irqsave(&sp->list_lock,irq_flags);
	if (stats) {
		stats->buffers_in_queue = sp->q_count;
		stats->buffers_in_idle = sp->i_count;
		stats->buffers_in_ready = sp->r_count;
		stats->buffers_processed = sp->buffers_processed;
		stats->buffers_failed = sp->buffers_failed;
		stats->bytes_processed = sp->bytes_processed;
	}
	if (zero_counts) {
		sp->buffers_processed = 0;
		sp->buffers_failed = 0;
		sp->bytes_processed = 0;
	}
	spin_unlock_irqrestore(&sp->list_lock,irq_flags);
}

/* Query / set the nominal buffer count */
int pvr2_stream_get_buffer_count(struct pvr2_stream *sp)
{
	return sp->buffer_target_count;
}

int pvr2_stream_set_buffer_count(struct pvr2_stream *sp,unsigned int cnt)
{
	int ret;
	if (sp->buffer_target_count == cnt) return 0;
	mutex_lock(&sp->mutex); do {
		sp->buffer_target_count = cnt;
		ret = pvr2_stream_achieve_buffer_count(sp);
	} while(0); mutex_unlock(&sp->mutex);
	return ret;
}

struct pvr2_buffer *pvr2_stream_get_idle_buffer(struct pvr2_stream *sp)
{
	struct list_head *lp = sp->idle_list.next;
	if (lp == &sp->idle_list) return NULL;
	return list_entry(lp,struct pvr2_buffer,list_overhead);
}

struct pvr2_buffer *pvr2_stream_get_ready_buffer(struct pvr2_stream *sp)
{
	struct list_head *lp = sp->ready_list.next;
	if (lp == &sp->ready_list) return NULL;
	return list_entry(lp,struct pvr2_buffer,list_overhead);
}

struct pvr2_buffer *pvr2_stream_get_buffer(struct pvr2_stream *sp,int id)
{
	if (id < 0) return NULL;
	if (id >= sp->buffer_total_count) return NULL;
	return sp->buffers[id];
}

int pvr2_stream_get_ready_count(struct pvr2_stream *sp)
{
	return sp->r_count;
}

void pvr2_stream_kill(struct pvr2_stream *sp)
{
	struct pvr2_buffer *bp;
	mutex_lock(&sp->mutex); do {
		pvr2_stream_internal_flush(sp);
		while ((bp = pvr2_stream_get_ready_buffer(sp)) != NULL) {
			pvr2_buffer_set_idle(bp);
		}
		if (sp->buffer_total_count != sp->buffer_target_count) {
			pvr2_stream_achieve_buffer_count(sp);
		}
	} while(0); mutex_unlock(&sp->mutex);
}

int pvr2_buffer_queue(struct pvr2_buffer *bp)
{
#undef SEED_BUFFER
#ifdef SEED_BUFFER
	unsigned int idx;
	unsigned int val;
#endif
	int ret = 0;
	struct pvr2_stream *sp;
	if (!bp) return -EINVAL;
	sp = bp->stream;
	mutex_lock(&sp->mutex); do {
		pvr2_buffer_wipe(bp);
		if (!sp->dev) {
			ret = -EIO;
			break;
		}
		pvr2_buffer_set_queued(bp);
#ifdef SEED_BUFFER
		for (idx = 0; idx < (bp->max_count) / 4; idx++) {
			val = bp->id << 24;
			val |= idx;
			((unsigned int *)(bp->ptr))[idx] = val;
		}
#endif
		bp->status = -EINPROGRESS;
		usb_fill_bulk_urb(bp->purb,      // struct urb *urb
				  sp->dev,       // struct usb_device *dev
				  // endpoint (below)
				  usb_rcvbulkpipe(sp->dev,sp->endpoint),
				  bp->ptr,       // void *transfer_buffer
				  bp->max_count, // int buffer_length
				  buffer_complete,
				  bp);
		usb_submit_urb(bp->purb,GFP_KERNEL);
	} while(0); mutex_unlock(&sp->mutex);
	return ret;
}

int pvr2_buffer_set_buffer(struct pvr2_buffer *bp,void *ptr,unsigned int cnt)
{
	int ret = 0;
	unsigned long irq_flags;
	struct pvr2_stream *sp;
	if (!bp) return -EINVAL;
	sp = bp->stream;
	mutex_lock(&sp->mutex); do {
		spin_lock_irqsave(&sp->list_lock,irq_flags);
		if (bp->state != pvr2_buffer_state_idle) {
			ret = -EPERM;
		} else {
			bp->ptr = ptr;
			bp->stream->i_bcount -= bp->max_count;
			bp->max_count = cnt;
			bp->stream->i_bcount += bp->max_count;
			pvr2_trace(PVR2_TRACE_BUF_FLOW,
				   "/*---TRACE_FLOW---*/ bufferPool    "
				   " %8s cap cap=%07d cnt=%02d",
				   pvr2_buffer_state_decode(
					   pvr2_buffer_state_idle),
				   bp->stream->i_bcount,bp->stream->i_count);
		}
		spin_unlock_irqrestore(&sp->list_lock,irq_flags);
	} while(0); mutex_unlock(&sp->mutex);
	return ret;
}

unsigned int pvr2_buffer_get_count(struct pvr2_buffer *bp)
{
	return bp->used_count;
}

int pvr2_buffer_get_status(struct pvr2_buffer *bp)
{
	return bp->status;
}

int pvr2_buffer_get_id(struct pvr2_buffer *bp)
{
	return bp->id;
}
