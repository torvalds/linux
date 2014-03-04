/* charqueue.c
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 *  Simple character queue implementation for Linux kernel mode.
 */

#include "charqueue.h"

#define MYDRVNAME "charqueue"

#define IS_EMPTY(charqueue) (charqueue->head == charqueue->tail)



struct CHARQUEUE_Tag {
	int alloc_size;
	int nslots;
	spinlock_t lock;
	int head, tail;
	unsigned char buf[0];
};



CHARQUEUE *charqueue_create(ulong nslots)
{
	int alloc_size = sizeof(CHARQUEUE) + nslots + 1;
	CHARQUEUE *cq = kmalloc(alloc_size, GFP_KERNEL|__GFP_NORETRY);
	if (cq == NULL) {
		ERRDRV("charqueue_create allocation failed (alloc_size=%d)",
		       alloc_size);
		return NULL;
	}
	cq->alloc_size = alloc_size;
	cq->nslots = nslots;
	cq->head = cq->tail = 0;
	spin_lock_init(&cq->lock);
	return cq;
}
EXPORT_SYMBOL_GPL(charqueue_create);



void charqueue_enqueue(CHARQUEUE *charqueue, unsigned char c)
{
	int alloc_slots = charqueue->nslots+1;  /* 1 slot is always empty */

	spin_lock(&charqueue->lock);
	charqueue->head = (charqueue->head+1) % alloc_slots;
	if (charqueue->head == charqueue->tail)
		/* overflow; overwrite the oldest entry */
		charqueue->tail = (charqueue->tail+1) % alloc_slots;
	charqueue->buf[charqueue->head] = c;
	spin_unlock(&charqueue->lock);
}
EXPORT_SYMBOL_GPL(charqueue_enqueue);



BOOL charqueue_is_empty(CHARQUEUE *charqueue)
{
	BOOL b;
	spin_lock(&charqueue->lock);
	b = IS_EMPTY(charqueue);
	spin_unlock(&charqueue->lock);
	return b;
}
EXPORT_SYMBOL_GPL(charqueue_is_empty);



static int charqueue_dequeue_1(CHARQUEUE *charqueue)
{
	int alloc_slots = charqueue->nslots + 1;  /* 1 slot is always empty */

	if (IS_EMPTY(charqueue))
		return -1;
	charqueue->tail = (charqueue->tail+1) % alloc_slots;
	return charqueue->buf[charqueue->tail];
}



int charqueue_dequeue(CHARQUEUE *charqueue)
{
	int rc = -1;

	spin_lock(&charqueue->lock);
	RETINT(charqueue_dequeue_1(charqueue));
Away:
	spin_unlock(&charqueue->lock);
	return rc;
}



int charqueue_dequeue_n(CHARQUEUE *charqueue, unsigned char *buf, int n)
{
	int rc = -1, counter = 0, c;

	spin_lock(&charqueue->lock);
	for (;;) {
		if (n <= 0)
			break;  /* no more buffer space */
		c = charqueue_dequeue_1(charqueue);
		if (c < 0)
			break;  /* no more input */
		*buf = (unsigned char)(c);
		buf++;
		n--;
		counter++;
	}
	RETINT(counter);

Away:
	spin_unlock(&charqueue->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(charqueue_dequeue_n);



void charqueue_destroy(CHARQUEUE *charqueue)
{
	if (charqueue == NULL)
		return;
	kfree(charqueue);
}
EXPORT_SYMBOL_GPL(charqueue_destroy);
