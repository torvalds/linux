/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CSIO_DEFS_H__
#define __CSIO_DEFS_H__

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/pci.h>
#include <linux/jiffies.h>

#define CSIO_INVALID_IDX		0xFFFFFFFF
#define CSIO_INC_STATS(elem, val)	((elem)->stats.val++)
#define CSIO_DEC_STATS(elem, val)	((elem)->stats.val--)
#define CSIO_VALID_WWN(__n)		((*__n >> 4) == 0x5 ? true : false)
#define CSIO_DID_MASK			0xFFFFFF
#define CSIO_WORD_TO_BYTE		4

#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	return readl(addr) + ((u64)readl(addr + 4) << 32);
}

static inline void writeq(u64 val, void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}
#endif

static inline int
csio_list_deleted(struct list_head *list)
{
	return ((list->next == list) && (list->prev == list));
}

#define csio_list_next(elem)	(((struct list_head *)(elem))->next)
#define csio_list_prev(elem)	(((struct list_head *)(elem))->prev)

/* State machine */
typedef void (*csio_sm_state_t)(void *, uint32_t);

struct csio_sm {
	struct list_head	sm_list;
	csio_sm_state_t		sm_state;
};

static inline void
csio_set_state(void *smp, void *state)
{
	((struct csio_sm *)smp)->sm_state = (csio_sm_state_t)state;
}

static inline void
csio_init_state(struct csio_sm *smp, void *state)
{
	csio_set_state(smp, state);
}

static inline void
csio_post_event(void *smp, uint32_t evt)
{
	((struct csio_sm *)smp)->sm_state(smp, evt);
}

static inline csio_sm_state_t
csio_get_state(void *smp)
{
	return ((struct csio_sm *)smp)->sm_state;
}

static inline bool
csio_match_state(void *smp, void *state)
{
	return (csio_get_state(smp) == (csio_sm_state_t)state);
}

#define	CSIO_ASSERT(cond)		BUG_ON(!(cond))

#ifdef __CSIO_DEBUG__
#define CSIO_DB_ASSERT(__c)		CSIO_ASSERT((__c))
#else
#define CSIO_DB_ASSERT(__c)
#endif

#endif /* ifndef __CSIO_DEFS_H__ */
