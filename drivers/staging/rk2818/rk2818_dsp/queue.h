/******************************************************************************
 * queue.h
 *
 * Copyright (c) 2009 Fuzhou Rockchip Co.,Ltd.
 *
 * DESCRIPTION: -
 *     Functions used to manage a FIFO queue.
 *
 * modification history
 * --------------------
 * Keith Lin, Feb 27, 2009,  Initial version
 * --------------------
 ******************************************************************************/

#ifndef QUEUE_H
#define QUEUE_H


typedef struct Item
{
	void *elem;

	/* data size of the element */
	int32_t size;
}Item;

typedef struct Queue
{
	Item *items;

	/**
	 * first - the first item of the queue,
	 * last - the last item of the queue.
	 */
	int32_t first, last;

	/* the number of items in the queue */
	int32_t nb_item;

	/* max item number */
	int32_t max_nb_item;

	/* total data size in the queue */
	int32_t size;

    /* parameter for destruct function */
    void *param;

    /* call this call back to destruct elements before flush */
    void  (*destruct)(void *, void *);
} Queue;

Queue *queue_init(int32_t max_nb_item, void (*destruct)(void *, void *), void *param);
int32_t queue_put(Queue *q, void *elem, int32_t size);
void *queue_get(Queue *q);
void queue_flush(Queue *q);
void queue_destroy(Queue *q);
int32_t queue_is_full(Queue *q);
int32_t queue_is_empty(Queue *q);
int32_t queue_size(Queue *q);

#endif /* QUEUE_H */
