/*
 *  linux/drivers/acorn/scsi/queue.c: queue handling primitives
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   15-Sep-1997 RMK	Created.
 *   11-Oct-1997 RMK	Corrected problem with queue_remove_exclude
 *			not updating internal linked list properly
 *			(was causing commands to go missing).
 *   30-Aug-2000 RMK	Use Linux list handling and spinlocks
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/init.h>

#include "../scsi.h"

#define DEBUG

typedef struct queue_entry {
	struct list_head   list;
	Scsi_Cmnd	   *SCpnt;
#ifdef DEBUG
	unsigned long	   magic;
#endif
} QE_t;

#ifdef DEBUG
#define QUEUE_MAGIC_FREE	0xf7e1c9a3
#define QUEUE_MAGIC_USED	0xf7e1cc33

#define SET_MAGIC(q,m)	((q)->magic = (m))
#define BAD_MAGIC(q,m)	((q)->magic != (m))
#else
#define SET_MAGIC(q,m)	do { } while (0)
#define BAD_MAGIC(q,m)	(0)
#endif

#include "queue.h"

#define NR_QE	32

/*
 * Function: void queue_initialise (Queue_t *queue)
 * Purpose : initialise a queue
 * Params  : queue - queue to initialise
 */
int queue_initialise (Queue_t *queue)
{
	unsigned int nqueues = NR_QE;
	QE_t *q;

	spin_lock_init(&queue->queue_lock);
	INIT_LIST_HEAD(&queue->head);
	INIT_LIST_HEAD(&queue->free);

	/*
	 * If life was easier, then SCpnt would have a
	 * host-available list head, and we wouldn't
	 * need to keep free lists or allocate this
	 * memory.
	 */
	queue->alloc = q = kmalloc(sizeof(QE_t) * nqueues, GFP_KERNEL);
	if (q) {
		for (; nqueues; q++, nqueues--) {
			SET_MAGIC(q, QUEUE_MAGIC_FREE);
			q->SCpnt = NULL;
			list_add(&q->list, &queue->free);
		}
	}

	return queue->alloc != NULL;
}

/*
 * Function: void queue_free (Queue_t *queue)
 * Purpose : free a queue
 * Params  : queue - queue to free
 */
void queue_free (Queue_t *queue)
{
	if (!list_empty(&queue->head))
		printk(KERN_WARNING "freeing non-empty queue %p\n", queue);
	if (queue->alloc)
		kfree(queue->alloc);
}
     

/*
 * Function: int queue_add_cmd(Queue_t *queue, Scsi_Cmnd *SCpnt, int head)
 * Purpose : Add a new command onto a queue, adding REQUEST_SENSE to head.
 * Params  : queue - destination queue
 *	     SCpnt - command to add
 *	     head  - add command to head of queue
 * Returns : 0 on error, !0 on success
 */
int __queue_add(Queue_t *queue, Scsi_Cmnd *SCpnt, int head)
{
	unsigned long flags;
	struct list_head *l;
	QE_t *q;
	int ret = 0;

	spin_lock_irqsave(&queue->queue_lock, flags);
	if (list_empty(&queue->free))
		goto empty;

	l = queue->free.next;
	list_del(l);

	q = list_entry(l, QE_t, list);
	if (BAD_MAGIC(q, QUEUE_MAGIC_FREE))
		BUG();

	SET_MAGIC(q, QUEUE_MAGIC_USED);
	q->SCpnt = SCpnt;

	if (head)
		list_add(l, &queue->head);
	else
		list_add_tail(l, &queue->head);

	ret = 1;
empty:
	spin_unlock_irqrestore(&queue->queue_lock, flags);
	return ret;
}

static Scsi_Cmnd *__queue_remove(Queue_t *queue, struct list_head *ent)
{
	QE_t *q;

	/*
	 * Move the entry from the "used" list onto the "free" list
	 */
	list_del(ent);
	q = list_entry(ent, QE_t, list);
	if (BAD_MAGIC(q, QUEUE_MAGIC_USED))
		BUG();

	SET_MAGIC(q, QUEUE_MAGIC_FREE);
	list_add(ent, &queue->free);

	return q->SCpnt;
}

/*
 * Function: Scsi_Cmnd *queue_remove_exclude (queue, exclude)
 * Purpose : remove a SCSI command from a queue
 * Params  : queue   - queue to remove command from
 *	     exclude - bit array of target&lun which is busy
 * Returns : Scsi_Cmnd if successful (and a reference), or NULL if no command available
 */
Scsi_Cmnd *queue_remove_exclude(Queue_t *queue, unsigned long *exclude)
{
	unsigned long flags;
	struct list_head *l;
	Scsi_Cmnd *SCpnt = NULL;

	spin_lock_irqsave(&queue->queue_lock, flags);
	list_for_each(l, &queue->head) {
		QE_t *q = list_entry(l, QE_t, list);
		if (!test_bit(q->SCpnt->device->id * 8 + q->SCpnt->device->lun, exclude)) {
			SCpnt = __queue_remove(queue, l);
			break;
		}
	}
	spin_unlock_irqrestore(&queue->queue_lock, flags);

	return SCpnt;
}

/*
 * Function: Scsi_Cmnd *queue_remove (queue)
 * Purpose : removes first SCSI command from a queue
 * Params  : queue   - queue to remove command from
 * Returns : Scsi_Cmnd if successful (and a reference), or NULL if no command available
 */
Scsi_Cmnd *queue_remove(Queue_t *queue)
{
	unsigned long flags;
	Scsi_Cmnd *SCpnt = NULL;

	spin_lock_irqsave(&queue->queue_lock, flags);
	if (!list_empty(&queue->head))
		SCpnt = __queue_remove(queue, queue->head.next);
	spin_unlock_irqrestore(&queue->queue_lock, flags);

	return SCpnt;
}

/*
 * Function: Scsi_Cmnd *queue_remove_tgtluntag (queue, target, lun, tag)
 * Purpose : remove a SCSI command from the queue for a specified target/lun/tag
 * Params  : queue  - queue to remove command from
 *	     target - target that we want
 *	     lun    - lun on device
 *	     tag    - tag on device
 * Returns : Scsi_Cmnd if successful, or NULL if no command satisfies requirements
 */
Scsi_Cmnd *queue_remove_tgtluntag (Queue_t *queue, int target, int lun, int tag)
{
	unsigned long flags;
	struct list_head *l;
	Scsi_Cmnd *SCpnt = NULL;

	spin_lock_irqsave(&queue->queue_lock, flags);
	list_for_each(l, &queue->head) {
		QE_t *q = list_entry(l, QE_t, list);
		if (q->SCpnt->device->id == target && q->SCpnt->device->lun == lun &&
		    q->SCpnt->tag == tag) {
			SCpnt = __queue_remove(queue, l);
			break;
		}
	}
	spin_unlock_irqrestore(&queue->queue_lock, flags);

	return SCpnt;
}

/*
 * Function: queue_remove_all_target(queue, target)
 * Purpose : remove all SCSI commands from the queue for a specified target
 * Params  : queue  - queue to remove command from
 *           target - target device id
 * Returns : nothing
 */
void queue_remove_all_target(Queue_t *queue, int target)
{
	unsigned long flags;
	struct list_head *l;

	spin_lock_irqsave(&queue->queue_lock, flags);
	list_for_each(l, &queue->head) {
		QE_t *q = list_entry(l, QE_t, list);
		if (q->SCpnt->device->id == target)
			__queue_remove(queue, l);
	}
	spin_unlock_irqrestore(&queue->queue_lock, flags);
}

/*
 * Function: int queue_probetgtlun (queue, target, lun)
 * Purpose : check to see if we have a command in the queue for the specified
 *	     target/lun.
 * Params  : queue  - queue to look in
 *	     target - target we want to probe
 *	     lun    - lun on target
 * Returns : 0 if not found, != 0 if found
 */
int queue_probetgtlun (Queue_t *queue, int target, int lun)
{
	unsigned long flags;
	struct list_head *l;
	int found = 0;

	spin_lock_irqsave(&queue->queue_lock, flags);
	list_for_each(l, &queue->head) {
		QE_t *q = list_entry(l, QE_t, list);
		if (q->SCpnt->device->id == target && q->SCpnt->device->lun == lun) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&queue->queue_lock, flags);

	return found;
}

/*
 * Function: int queue_remove_cmd(Queue_t *queue, Scsi_Cmnd *SCpnt)
 * Purpose : remove a specific command from the queues
 * Params  : queue - queue to look in
 *	     SCpnt - command to find
 * Returns : 0 if not found
 */
int queue_remove_cmd(Queue_t *queue, Scsi_Cmnd *SCpnt)
{
	unsigned long flags;
	struct list_head *l;
	int found = 0;

	spin_lock_irqsave(&queue->queue_lock, flags);
	list_for_each(l, &queue->head) {
		QE_t *q = list_entry(l, QE_t, list);
		if (q->SCpnt == SCpnt) {
			__queue_remove(queue, l);
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&queue->queue_lock, flags);

	return found;
}

EXPORT_SYMBOL(queue_initialise);
EXPORT_SYMBOL(queue_free);
EXPORT_SYMBOL(__queue_add);
EXPORT_SYMBOL(queue_remove);
EXPORT_SYMBOL(queue_remove_exclude);
EXPORT_SYMBOL(queue_remove_tgtluntag);
EXPORT_SYMBOL(queue_remove_cmd);
EXPORT_SYMBOL(queue_remove_all_target);
EXPORT_SYMBOL(queue_probetgtlun);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("SCSI command queueing");
MODULE_LICENSE("GPL");
