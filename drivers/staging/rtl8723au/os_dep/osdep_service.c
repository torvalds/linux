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
#include <linux/vmalloc.h>

/*
* Translate the OS dependent @param error_code to OS independent RTW_STATUS_CODE23a
* @return: one of RTW_STATUS_CODE23a
*/
inline int RTW_STATUS_CODE23a(int error_code)
{
	if (error_code >= 0)
		return _SUCCESS;
	return _FAIL;
}

inline u8 *_rtw_vmalloc(u32 sz)
{
	u8	*pbuf;
	pbuf = vmalloc(sz);

	return pbuf;
}

inline u8 *_rtw_zvmalloc(u32 sz)
{
	u8	*pbuf;
	pbuf = _rtw_vmalloc(sz);
	if (pbuf != NULL)
		memset(pbuf, 0, sz);

	return pbuf;
}

inline void _rtw_vmfree(u8 *pbuf, u32 sz)
{
	vfree(pbuf);
}

void _rtw_init_queue23a(struct rtw_queue *pqueue)
{
	INIT_LIST_HEAD(&pqueue->queue);
	spin_lock_init(&pqueue->lock);
}

u32 _rtw_queue_empty23a(struct rtw_queue *pqueue)
{
	if (list_empty(&pqueue->queue))
		return true;
	else
		return false;
}
