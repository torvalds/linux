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

#define RT_TAG	('1178')

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

u64 rtw_modular6423a(u64 x, u64 y)
{
	return do_div(x, y);
}

u64 rtw_division6423a(u64 x, u64 y)
{
	do_div(x, y);
	return x;
}

/* rtw_cbuf_full23a - test if cbuf is full
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is full
 */
inline bool rtw_cbuf_full23a(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read-1) ? true : false;
}

/* rtw_cbuf_empty23a - test if cbuf is empty
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is empty
 */
inline bool rtw_cbuf_empty23a(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read) ? true : false;
}

/**
 * rtw_cbuf_push23a - push a pointer into cbuf
 * @cbuf: pointer of struct rtw_cbuf
 * @buf: pointer to push in
 *
 * Lock free operation, be careful of the use scheme
 * Returns: true push success
 */
bool rtw_cbuf_push23a(struct rtw_cbuf *cbuf, void *buf)
{
	if (rtw_cbuf_full23a(cbuf))
		return _FAIL;

	if (0)
		DBG_8723A("%s on %u\n", __func__, cbuf->write);
	cbuf->bufs[cbuf->write] = buf;
	cbuf->write = (cbuf->write+1)%cbuf->size;

	return _SUCCESS;
}

/**
 * rtw_cbuf_pop23a - pop a pointer from cbuf
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Lock free operation, be careful of the use scheme
 * Returns: pointer popped out
 */
void *rtw_cbuf_pop23a(struct rtw_cbuf *cbuf)
{
	void *buf;
	if (rtw_cbuf_empty23a(cbuf))
		return NULL;

	if (0)
		DBG_8723A("%s on %u\n", __func__, cbuf->read);
	buf = cbuf->bufs[cbuf->read];
	cbuf->read = (cbuf->read+1)%cbuf->size;

	return buf;
}

/**
 * rtw_cbuf_alloc23a - allocte a rtw_cbuf with given size and do initialization
 * @size: size of pointer
 *
 * Returns: pointer of srtuct rtw_cbuf, NULL for allocation failure
 */
struct rtw_cbuf *rtw_cbuf_alloc23a(u32 size)
{
	struct rtw_cbuf *cbuf;

	cbuf = kmalloc(sizeof(*cbuf) + sizeof(void *)*size, GFP_KERNEL);

	if (cbuf) {
		cbuf->write = 0;
		cbuf->read = 0;
		cbuf->size = size;
	}

	return cbuf;
}

/**
 * rtw_cbuf_free - free the given rtw_cbuf
 * @cbuf: pointer of struct rtw_cbuf to free
 */
void rtw_cbuf_free(struct rtw_cbuf *cbuf)
{
	kfree(cbuf);
}
