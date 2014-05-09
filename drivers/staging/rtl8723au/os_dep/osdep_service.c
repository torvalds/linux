/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/


#define _OSDEP_SERVICE_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>

void _rtw_init_queue23a(struct rtw_queue *pqueue)
{
	INIT_LIST_HEAD(&pqueue->queue);
	spin_lock_init(&pqueue->lock);
}
