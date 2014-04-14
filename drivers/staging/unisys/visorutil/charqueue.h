/* charqueue.h
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

#ifndef __CHARQUEUE_H__
#define __CHARQUEUE_H__

#include "uniklog.h"
#include "timskmod.h"

/* CHARQUEUE is an opaque structure to users.
 * Fields are declared only in the implementation .c files.
 */
typedef struct CHARQUEUE_Tag CHARQUEUE;

CHARQUEUE *visor_charqueue_create(ulong nslots);
void visor_charqueue_enqueue(CHARQUEUE *charqueue, unsigned char c);
int charqueue_dequeue(CHARQUEUE *charqueue);
int visor_charqueue_dequeue_n(CHARQUEUE *charqueue, unsigned char *buf, int n);
BOOL visor_charqueue_is_empty(CHARQUEUE *charqueue);
void visor_charqueue_destroy(CHARQUEUE *charqueue);

#endif

