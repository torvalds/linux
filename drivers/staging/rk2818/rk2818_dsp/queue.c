/******************************************************************************
 * queue.c
 *
 * Copyright (c) 2009 Fuzhou Rockchip Co.,Ltd.
 *
 * DESCRIPTION: -
 *     Functions used to manage a FIFO queue .
 *
 * modification history
 * --------------------
 * Keith Lin, Feb 27, 2009,  Initial version
 * --------------------
 ******************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "queue.h"

DEFINE_SPINLOCK(lock);
unsigned long flag;

/**
 * queue initialize,
 * return Queue pointer on success and NULL on error.
 */
Queue *queue_init(int32_t max_nb_item, void (*destruct)(void *, void *), void *param)
{
	Queue *q = NULL;
	spin_lock_irqsave(&lock, flag);

	q = kmalloc(sizeof(*q), GFP_DMA);
	if(NULL == q) {
	    spin_unlock_irqrestore(&lock, flag);
		return NULL;
    }

	q->items = kmalloc(max_nb_item * sizeof(*q->items), GFP_DMA);

	if(NULL == q->items)
	{
		kfree(q);
		spin_unlock_irqrestore(&lock, flag);
		return NULL;
	}

	q->first = q->last = 0;
	q->nb_item = 0;
	q->max_nb_item = max_nb_item;
	q->size = 0;
    q->destruct = (void (*)(void *, void *))destruct;
    q->param = param;
    spin_unlock_irqrestore(&lock, flag);
	return q;
}

/**
 * put an element to the queue,
 * return 0 on success and -1 on error.
 */
int32_t queue_put(Queue *q, void *elem, int32_t size)
{
    spin_lock_irqsave(&lock, flag);
	if(q->nb_item >= q->max_nb_item) {
        spin_unlock_irqrestore(&lock, flag);
		return -1;
    }

	q->items[q->last].elem = elem;
	q->items[q->last].size = size;
	q->last = (q->last + 1) % q->max_nb_item;
	q->size += size;
	q->nb_item++;

    spin_unlock_irqrestore(&lock, flag);
	return 0;
}

/**
 * get an element from the queue.
 * return element pointer on success and NULL on error.
 */
void *queue_get(Queue *q)
{
	Item *item;

    spin_lock_irqsave(&lock, flag);
	if(q->nb_item <= 0) {
	    spin_unlock_irqrestore(&lock, flag);
		return NULL;
    }

	item = &(q->items[q->first]);
	q->first = (q->first + 1) % q->max_nb_item;
	q->size -= item->size;
	q->nb_item--;

    spin_unlock_irqrestore(&lock, flag);
	return item->elem;
}

/**
 * Flush the queue.
 */
void queue_flush(Queue *q)
{
    spin_lock_irqsave(&lock, flag);
    /* destruct all elements in the queue */
    if (NULL != q->destruct)
    {
        for(; q->nb_item > 0; q->nb_item --)
        {
            q->destruct(q->param, q->items[q->first].elem);
            q->first = (q->first + 1) % q->max_nb_item;
        }
    }

	q->first = q->last = 0;
	q->nb_item = 0;
	q->size = 0;
	spin_unlock_irqrestore(&lock, flag);
}

/**
 * Destroy the queue.
 */
void queue_destroy(Queue *q)
{
    spin_lock_irqsave(&lock, flag);
    /* destruct all elements first */
    if (NULL != q->destruct)
    {
        for(; q->nb_item > 0; q->nb_item--)
        {
            q->destruct(q->param, q->items[q->first].elem);
            q->first = (q->first + 1) % q->max_nb_item;
        }
    }

	if(NULL != q)
	{
		if(NULL != q->items)
        {
			kfree(q->items);
            q->items = NULL;
        }
		kfree(q);
		q = NULL;
	}
	spin_unlock_irqrestore(&lock, flag);
}

int32_t queue_is_full(Queue *q)
{
    int32_t ret = 0;
    spin_lock_irqsave(&lock, flag);
    ret = (q->nb_item >= q->max_nb_item) ? 1 : 0;
    spin_unlock_irqrestore(&lock, flag);
	return ret;
}

int32_t queue_is_empty(Queue *q)
{
    int32_t ret = 0;
    spin_lock_irqsave(&lock, flag);
    ret = (q->nb_item == 0);
    spin_unlock_irqrestore(&lock, flag);
	return ret;
}

int32_t queue_size(Queue *q)
{
    int32_t ret = 0;
    spin_lock_irqsave(&lock, flag);
    ret = q->size;
    spin_unlock_irqrestore(&lock, flag);
    return ret;
}

