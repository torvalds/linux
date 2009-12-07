/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _OSD_H_
#define _OSD_H_

#include <linux/workqueue.h>

/* Defines */
#define ALIGN_UP(value, align)	(((value) & (align-1)) ?		\
				 (((value) + (align-1)) & ~(align-1)) :	\
				 (value))
#define ALIGN_DOWN(value, align)	((value) & ~(align-1))
#define NUM_PAGES_SPANNED(addr, len)	((ALIGN_UP(addr+len, PAGE_SIZE) - \
					 ALIGN_DOWN(addr, PAGE_SIZE)) >>  \
					 PAGE_SHIFT)

#define LOWORD(dw)	((unsigned short)(dw))
#define HIWORD(dw)	((unsigned short)(((unsigned int) (dw) >> 16) & 0xFFFF))

struct hv_guid {
	unsigned char data[16];
};

struct osd_waitevent {
	int condition;
	wait_queue_head_t event;
};

/* Osd routines */

extern void *osd_VirtualAllocExec(unsigned int size);

extern void *osd_PageAlloc(unsigned int count);
extern void osd_PageFree(void *page, unsigned int count);

extern struct osd_waitevent *osd_WaitEventCreate(void);
extern void osd_WaitEventSet(struct osd_waitevent *waitEvent);
extern int osd_WaitEventWait(struct osd_waitevent *waitEvent);

/* If >0, waitEvent got signaled. If ==0, timeout. If < 0, error */
extern int osd_WaitEventWaitEx(struct osd_waitevent *waitEvent,
			       u32 TimeoutInMs);

int osd_schedule_callback(struct workqueue_struct *wq,
			  void (*func)(void *),
			  void *data);

#endif /* _OSD_H_ */
