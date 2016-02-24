/*
 * Broadcom Dongle Host Driver (DHD), Generic work queue framework
 * Generic interface to handle dhd deferred work events
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_linux_wq.c 589976 2015-10-01 07:01:27Z $
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

struct dhd_deferred_event_t {
	u8	event; /* holds the event */
	void	*event_data; /* Holds event specific data */
	event_handler_t event_handler;
};
#define DEFRD_EVT_SIZE	sizeof(struct dhd_deferred_event_t)

struct dhd_deferred_wq {
	struct work_struct	deferred_work; /* should be the first member */

	/*
	 * work events may occur simultaneously.
	 * Can hold upto 64 low priority events and 4 high priority events
	 */
#define DHD_PRIO_WORK_FIFO_SIZE	(4 * sizeof(struct dhd_deferred_event_t))
#define DHD_WORK_FIFO_SIZE	(64 * sizeof(struct dhd_deferred_event_t))
	struct kfifo			*prio_fifo;
	struct kfifo			*work_fifo;
	u8				*prio_fifo_buf;
	u8				*work_fifo_buf;
	spinlock_t			work_lock;
	void				*dhd_info; /* review: does it require */
};
struct dhd_deferred_wq	*deferred_wq = NULL;

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
#endif 
	return fifo;
}

static inline void
dhd_kfifo_free(struct kfifo *fifo)
{
	kfifo_free(fifo);
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
		DHD_ERROR(("%s: work queue creation failed \n", __FUNCTION__));
		goto return_null;
	}

	INIT_WORK((struct work_struct *)work, dhd_deferred_work_handler);

	/* initialize event fifo */
	spin_lock_init(&work->work_lock);

	/* allocate buffer to hold prio events */
	fifo_size = DHD_PRIO_WORK_FIFO_SIZE;
	fifo_size = is_power_of_2(fifo_size)? fifo_size : roundup_pow_of_two(fifo_size);
	buf = (u8*)kzalloc(fifo_size, flags);
	if (!buf) {
		DHD_ERROR(("%s: prio work fifo allocation failed \n", __FUNCTION__));
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
	fifo_size = is_power_of_2(fifo_size)? fifo_size : roundup_pow_of_two(fifo_size);
	buf = (u8*)kzalloc(fifo_size, flags);
	if (!buf) {
		DHD_ERROR(("%s: work fifo allocation failed \n", __FUNCTION__));
		goto return_null;
	}

	/* Initialize event fifo */
	work->work_fifo = dhd_kfifo_init(buf, fifo_size, &work->work_lock);
	if (!work->work_fifo) {
		kfree(buf);
		goto return_null;
	}

	work->dhd_info = dhd_info;
	deferred_wq = work;
	DHD_ERROR(("%s: work queue initialized \n", __FUNCTION__));
	return work;

return_null:

	if (work)
		dhd_deferred_work_deinit(work);

	return NULL;
}

void
dhd_deferred_work_deinit(void *work)
{
	struct dhd_deferred_wq *deferred_work = work;


	if (!deferred_work) {
		DHD_ERROR(("%s: deferred work has been freed alread \n", __FUNCTION__));
		return;
	}

	/* cancel the deferred work handling */
	cancel_work_sync((struct work_struct *)deferred_work);

	/*
	 * free work event fifo.
	 * kfifo_free frees locally allocated fifo buffer
	 */
	if (deferred_work->prio_fifo)
		dhd_kfifo_free(deferred_work->prio_fifo);

	if (deferred_work->work_fifo)
		dhd_kfifo_free(deferred_work->work_fifo);

	kfree(deferred_work);

	/* deinit internal reference pointer */
	deferred_wq = NULL;
}

/*
 *	Prepares event to be queued
 *	Schedules the event
 */
int
dhd_deferred_schedule_work(void *event_data, u8 event, event_handler_t event_handler, u8 priority)
{
	struct	dhd_deferred_event_t	deferred_event;
	int	status;

	if (!deferred_wq) {
		DHD_ERROR(("%s: work queue not initialized \n", __FUNCTION__));
		ASSERT(0);
		return DHD_WQ_STS_UNINITIALIZED;
	}

	if (!event || (event >= DHD_MAX_WQ_EVENTS)) {
		DHD_ERROR(("%s: Unknown event \n", __FUNCTION__));
		return DHD_WQ_STS_UNKNOWN_EVENT;
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

	if (priority == DHD_WORK_PRIORITY_HIGH) {
		status = kfifo_in_spinlocked(deferred_wq->prio_fifo, &deferred_event,
			DEFRD_EVT_SIZE, &deferred_wq->work_lock);
	} else {
		status = kfifo_in_spinlocked(deferred_wq->work_fifo, &deferred_event,
			DEFRD_EVT_SIZE, &deferred_wq->work_lock);
	}

	if (!status) {
		return DHD_WQ_STS_SCHED_FAILED;
	}
	schedule_work((struct work_struct *)deferred_wq);
	return DHD_WQ_STS_OK;
}

static int
dhd_get_scheduled_work(struct dhd_deferred_event_t *event)
{
	int	status = 0;

	if (!deferred_wq) {
		DHD_ERROR(("%s: work queue not initialized \n", __FUNCTION__));
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

	/* first read  priorit event fifo */
	status = kfifo_out_spinlocked(deferred_wq->prio_fifo, event,
		DEFRD_EVT_SIZE, &deferred_wq->work_lock);

	if (!status) {
		/* priority fifo is empty. Now read low prio work fifo */
		status = kfifo_out_spinlocked(deferred_wq->work_fifo, event,
			DEFRD_EVT_SIZE, &deferred_wq->work_lock);
	}

	return status;
}

/*
 *	Called when work is scheduled
 */
static void
dhd_deferred_work_handler(struct work_struct *work)
{
	struct dhd_deferred_wq		*deferred_work = (struct dhd_deferred_wq *)work;
	struct dhd_deferred_event_t	work_event;
	int				status;

	if (!deferred_work) {
		DHD_ERROR(("%s: work queue not initialized\n", __FUNCTION__));
		return;
	}

	do {
		status = dhd_get_scheduled_work(&work_event);
		DHD_TRACE(("%s: event to handle %d \n", __FUNCTION__, status));
		if (!status) {
			DHD_TRACE(("%s: No event to handle %d \n", __FUNCTION__, status));
			break;
		}

		if (work_event.event > DHD_MAX_WQ_EVENTS) {
			DHD_TRACE(("%s: Unknown event %d \n", __FUNCTION__, work_event.event));
			break;
		}

		if (work_event.event_handler) {
			work_event.event_handler(deferred_work->dhd_info,
				work_event.event_data, work_event.event);
		} else {
			DHD_ERROR(("%s: event not defined %d\n", __FUNCTION__, work_event.event));
		}
	} while (1);
	return;
}
