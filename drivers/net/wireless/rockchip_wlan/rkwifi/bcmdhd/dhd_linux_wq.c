/*
 * Broadcom Dongle Host Driver (DHD), Generic work queue framework
 * Generic interface to handle dhd deferred work events
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/ip.h>
#include <linux/kfifo.h>

#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_linux_wq.h>

/*
 * XXX: always make sure that the size of this structure is aligned to
 * the power of 2 (2^n) i.e, if any new variable has to be added then
 * modify the padding accordingly
 */
typedef struct dhd_deferred_event {
	u8 event;		/* holds the event */
	void *event_data;	/* holds event specific data */
	event_handler_t event_handler;
	unsigned long pad;	/* for memory alignment to power of 2 */
} dhd_deferred_event_t;

#define DEFRD_EVT_SIZE	(sizeof(dhd_deferred_event_t))

/*
 * work events may occur simultaneously.
 * can hold upto 64 low priority events and 16 high priority events
 */
#define DHD_PRIO_WORK_FIFO_SIZE	(16 * DEFRD_EVT_SIZE)
#define DHD_WORK_FIFO_SIZE	(64 * DEFRD_EVT_SIZE)

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32))
#define kfifo_avail(fifo) (fifo->size - kfifo_len(fifo))
#endif /* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)) */

#define DHD_FIFO_HAS_FREE_SPACE(fifo) \
	((fifo) && (kfifo_avail(fifo) >= DEFRD_EVT_SIZE))
#define DHD_FIFO_HAS_ENOUGH_DATA(fifo) \
	((fifo) && (kfifo_len(fifo) >= DEFRD_EVT_SIZE))

struct dhd_deferred_wq {
	struct work_struct deferred_work; /* should be the first member */

	struct kfifo *prio_fifo;
	struct kfifo			*work_fifo;
	u8				*prio_fifo_buf;
	u8				*work_fifo_buf;
	spinlock_t			work_lock;
	void				*dhd_info; /* review: does it require */
	u32				event_skip_mask;
};

static inline struct kfifo*
dhd_kfifo_init(u8 *buf, int size, spinlock_t *lock)
{
	struct kfifo *fifo;
	gfp_t flags = CAN_SLEEP()? GFP_KERNEL : GFP_ATOMIC;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
	fifo = kfifo_init(buf, size, flags, lock);
#else
	fifo = (struct kfifo *)kzalloc(sizeof(struct kfifo), flags);
	if (!fifo) {
		return NULL;
	}
	kfifo_init(fifo, buf, size);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)) */
	return fifo;
}

static inline void
dhd_kfifo_free(struct kfifo *fifo)
{
	kfifo_free(fifo);
	kfree(fifo);
}

/* deferred work functions */
static void dhd_deferred_work_handler(struct work_struct *data);

void*
dhd_deferred_work_init(void *dhd_info)
{
	struct dhd_deferred_wq	*work = NULL;
	u8*	buf;
	unsigned long	fifo_size = 0;
	gfp_t	flags = CAN_SLEEP()? GFP_KERNEL : GFP_ATOMIC;

	if (!dhd_info) {
		DHD_ERROR(("%s: dhd info not initialized\n", __FUNCTION__));
		goto return_null;
	}

	work = (struct dhd_deferred_wq *)kzalloc(sizeof(struct dhd_deferred_wq),
		flags);
	if (!work) {
		DHD_ERROR(("%s: work queue creation failed\n", __FUNCTION__));
		goto return_null;
	}

	INIT_WORK((struct work_struct *)work, dhd_deferred_work_handler);

	/* initialize event fifo */
	spin_lock_init(&work->work_lock);

	/* allocate buffer to hold prio events */
	fifo_size = DHD_PRIO_WORK_FIFO_SIZE;
	fifo_size = is_power_of_2(fifo_size) ? fifo_size :
			roundup_pow_of_two(fifo_size);
	buf = (u8*)kzalloc(fifo_size, flags);
	if (!buf) {
		DHD_ERROR(("%s: prio work fifo allocation failed\n",
			__FUNCTION__));
		goto return_null;
	}

	/* Initialize prio event fifo */
	work->prio_fifo = dhd_kfifo_init(buf, fifo_size, &work->work_lock);
	if (!work->prio_fifo) {
		kfree(buf);
		goto return_null;
	}

	/* allocate buffer to hold work events */
	fifo_size = DHD_WORK_FIFO_SIZE;
	fifo_size = is_power_of_2(fifo_size) ? fifo_size :
			roundup_pow_of_two(fifo_size);
	buf = (u8*)kzalloc(fifo_size, flags);
	if (!buf) {
		DHD_ERROR(("%s: work fifo allocation failed\n", __FUNCTION__));
		goto return_null;
	}

	/* Initialize event fifo */
	work->work_fifo = dhd_kfifo_init(buf, fifo_size, &work->work_lock);
	if (!work->work_fifo) {
		kfree(buf);
		goto return_null;
	}

	work->dhd_info = dhd_info;
	work->event_skip_mask = 0;
	DHD_ERROR(("%s: work queue initialized\n", __FUNCTION__));
	return work;

return_null:
	if (work) {
		dhd_deferred_work_deinit(work);
	}

	return NULL;
}

void
dhd_deferred_work_deinit(void *work)
{
	struct dhd_deferred_wq *deferred_work = work;

	if (!deferred_work) {
		DHD_ERROR(("%s: deferred work has been freed already\n",
			__FUNCTION__));
		return;
	}

	/* cancel the deferred work handling */
	cancel_work_sync((struct work_struct *)deferred_work);

	/*
	 * free work event fifo.
	 * kfifo_free frees locally allocated fifo buffer
	 */
	if (deferred_work->prio_fifo) {
		dhd_kfifo_free(deferred_work->prio_fifo);
	}

	if (deferred_work->work_fifo) {
		dhd_kfifo_free(deferred_work->work_fifo);
	}

	kfree(deferred_work);
}

/* select kfifo according to priority */
static inline struct kfifo *
dhd_deferred_work_select_kfifo(struct dhd_deferred_wq *deferred_wq,
	u8 priority)
{
	if (priority == DHD_WQ_WORK_PRIORITY_HIGH) {
		return deferred_wq->prio_fifo;
	} else if (priority == DHD_WQ_WORK_PRIORITY_LOW) {
		return deferred_wq->work_fifo;
	} else {
		return NULL;
	}
}

/*
 *	Prepares event to be queued
 *	Schedules the event
 */
int
dhd_deferred_schedule_work(void *workq, void *event_data, u8 event,
	event_handler_t event_handler, u8 priority)
{
	struct dhd_deferred_wq *deferred_wq = (struct dhd_deferred_wq *)workq;
	struct kfifo *fifo;
	dhd_deferred_event_t deferred_event;
	int bytes_copied = 0;

	if (!deferred_wq) {
		DHD_ERROR(("%s: work queue not initialized\n", __FUNCTION__));
		ASSERT(0);
		return DHD_WQ_STS_UNINITIALIZED;
	}

	if (!event || (event >= DHD_MAX_WQ_EVENTS)) {
		DHD_ERROR(("%s: unknown event, event=%d\n", __FUNCTION__,
			event));
		return DHD_WQ_STS_UNKNOWN_EVENT;
	}

	if (!priority || (priority >= DHD_WQ_MAX_PRIORITY)) {
		DHD_ERROR(("%s: unknown priority, priority=%d\n",
			__FUNCTION__, priority));
		return DHD_WQ_STS_UNKNOWN_PRIORITY;
	}

	if ((deferred_wq->event_skip_mask & (1 << event))) {
		DHD_ERROR(("%s: Skip event requested. Mask = 0x%x\n",
			__FUNCTION__, deferred_wq->event_skip_mask));
		return DHD_WQ_STS_EVENT_SKIPPED;
	}

	/*
	 * default element size is 1, which can be changed
	 * using kfifo_esize(). Older kernel(FC11) doesn't support
	 * changing element size. For compatibility changing
	 * element size is not prefered
	 */
	ASSERT(kfifo_esize(deferred_wq->prio_fifo) == 1);
	ASSERT(kfifo_esize(deferred_wq->work_fifo) == 1);

	deferred_event.event = event;
	deferred_event.event_data = event_data;
	deferred_event.event_handler = event_handler;

	fifo = dhd_deferred_work_select_kfifo(deferred_wq, priority);
	if (DHD_FIFO_HAS_FREE_SPACE(fifo)) {
		bytes_copied = kfifo_in_spinlocked(fifo, &deferred_event,
			DEFRD_EVT_SIZE, &deferred_wq->work_lock);
	}
	if (bytes_copied != DEFRD_EVT_SIZE) {
		DHD_ERROR(("%s: failed to schedule deferred work, "
			"priority=%d, bytes_copied=%d\n", __FUNCTION__,
			priority, bytes_copied));
		return DHD_WQ_STS_SCHED_FAILED;
	}
	schedule_work((struct work_struct *)deferred_wq);
	return DHD_WQ_STS_OK;
}

static bool
dhd_get_scheduled_work(struct dhd_deferred_wq *deferred_wq,
	dhd_deferred_event_t *event)
{
	int bytes_copied = 0;

	if (!deferred_wq) {
		DHD_ERROR(("%s: work queue not initialized\n", __FUNCTION__));
		return DHD_WQ_STS_UNINITIALIZED;
	}

	/*
	 * default element size is 1 byte, which can be changed
	 * using kfifo_esize(). Older kernel(FC11) doesn't support
	 * changing element size. For compatibility changing
	 * element size is not prefered
	 */
	ASSERT(kfifo_esize(deferred_wq->prio_fifo) == 1);
	ASSERT(kfifo_esize(deferred_wq->work_fifo) == 1);

	/* handle priority work */
	if (DHD_FIFO_HAS_ENOUGH_DATA(deferred_wq->prio_fifo)) {
		bytes_copied = kfifo_out_spinlocked(deferred_wq->prio_fifo,
			event, DEFRD_EVT_SIZE, &deferred_wq->work_lock);
	}

	/* handle normal work if priority work doesn't have enough data */
	if ((bytes_copied != DEFRD_EVT_SIZE) &&
		DHD_FIFO_HAS_ENOUGH_DATA(deferred_wq->work_fifo)) {
		bytes_copied = kfifo_out_spinlocked(deferred_wq->work_fifo,
			event, DEFRD_EVT_SIZE, &deferred_wq->work_lock);
	}

	return (bytes_copied == DEFRD_EVT_SIZE);
}

static inline void
dhd_deferred_dump_work_event(dhd_deferred_event_t *work_event)
{
	if (!work_event) {
		DHD_ERROR(("%s: work_event is null\n", __FUNCTION__));
		return;
	}

	DHD_ERROR(("%s: work_event->event = %d\n", __FUNCTION__,
		work_event->event));
	DHD_ERROR(("%s: work_event->event_data = %p\n", __FUNCTION__,
		work_event->event_data));
	DHD_ERROR(("%s: work_event->event_handler = %p\n", __FUNCTION__,
		work_event->event_handler));
}

/*
 *	Called when work is scheduled
 */
static void
dhd_deferred_work_handler(struct work_struct *work)
{
	struct dhd_deferred_wq *deferred_work = (struct dhd_deferred_wq *)work;
	dhd_deferred_event_t work_event;

	if (!deferred_work) {
		DHD_ERROR(("%s: work queue not initialized\n", __FUNCTION__));
		return;
	}

	do {
		if (!dhd_get_scheduled_work(deferred_work, &work_event)) {
			DHD_TRACE(("%s: no event to handle\n", __FUNCTION__));
			break;
		}

		if (work_event.event >= DHD_MAX_WQ_EVENTS) {
			DHD_ERROR(("%s: unknown event\n", __FUNCTION__));
			dhd_deferred_dump_work_event(&work_event);
			ASSERT(work_event.event < DHD_MAX_WQ_EVENTS);
			continue;
		}

		/*
		 * XXX: don't do NULL check for 'work_event.event_data'
		 * as for some events like DHD_WQ_WORK_DHD_LOG_DUMP the
		 * event data is always NULL even though rest of the
		 * event parameters are valid
		 */

		if (work_event.event_handler) {
			work_event.event_handler(deferred_work->dhd_info,
				work_event.event_data, work_event.event);
		} else {
			DHD_ERROR(("%s: event handler is null\n",
				__FUNCTION__));
			dhd_deferred_dump_work_event(&work_event);
			ASSERT(work_event.event_handler != NULL);
		}
	} while (1);

	return;
}

void
dhd_deferred_work_set_skip(void *work, u8 event, bool set)
{
	struct dhd_deferred_wq *deferred_wq = (struct dhd_deferred_wq *)work;

	if (!deferred_wq || !event || (event >= DHD_MAX_WQ_EVENTS)) {
		DHD_ERROR(("%s: Invalid!!\n", __FUNCTION__));
		return;
	}

	if (set) {
		/* Set */
		deferred_wq->event_skip_mask |= (1 << event);
	} else {
		/* Clear */
		deferred_wq->event_skip_mask &= ~(1 << event);
	}
}
