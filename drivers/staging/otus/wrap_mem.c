/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*  Module Name : wrap_mem.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for memory management     */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>
#include <net/iw_handler.h>

/* Memory management */
/* Called to allocate uncached memory, allocated memory must	*/
/* in 4-byte boundary						*/
void *zfwMemAllocate(zdev_t *dev, u32_t size)
{
	void *mem = NULL;
	mem = kmalloc(size, GFP_ATOMIC);
	return mem;
}


/* Called to free allocated memory */
void zfwMemFree(zdev_t *dev, void *mem, u32_t size)
{
	kfree(mem);
	return;
}

void zfwMemoryCopy(u8_t *dst, u8_t *src, u16_t length)
{
	/* u16_t i; */

	memcpy(dst, src, length);
	/*
	 * for(i=0; i<length; i++)
	 * {
	 *	dst[i] = src[i];
	 * }
	 */
	return;
}

void zfwZeroMemory(u8_t *va, u16_t length)
{
	/* u16_t i; */
	memset(va, 0, length);
	/*
	 * for(i=0; i<length; i++)
	 * {
	 *	va[i] = 0;
	 * }
	 */
	return;
}

void zfwMemoryMove(u8_t *dst, u8_t *src, u16_t length)
{
	memcpy(dst, src, length);
	return;
}

u8_t zfwMemoryIsEqual(u8_t *m1, u8_t *m2, u16_t length)
{
	/* u16_t i; */
	int ret;

	ret = memcmp(m1, m2, length);

	return ((ret == 0) ? TRUE : FALSE);
	/*
	 * for(i=0; i<length; i++)
	 *{
	 *	 if ( m1[i] != m2[i] )
	 *	{
	 *		return FALSE;
	 *	}
	 *}
	 *
	 * return TRUE;
	 */
}

/* Leave an empty line below to remove warning message on some compiler */
