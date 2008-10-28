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
/*                                                                      */
/*  Module Name : zdcompat.h                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains function defintion for compatibility.       */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#ifndef _ZDCOMPAT_H
#define _ZDCOMPAT_H

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
#ifndef INIT_TQUEUE
#define INIT_TQUEUE(_tq, _routine, _data)                       \
        do {                                                    \
                (_tq)->next = NULL;                             \
                (_tq)->sync = 0;                                \
                PREPARE_TQUEUE((_tq), (_routine), (_data));     \
        } while (0)
#define PREPARE_TQUEUE(_tq, _routine, _data)                    \
        do {                                                    \
                (_tq)->routine = _routine;                      \
                (_tq)->data = _data;                            \
        } while (0)
#endif

#ifndef INIT_WORK
#define work_struct tq_struct

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
#define schedule_work(a)   queue_task(a, &tq_scheduler)
#else
#define schedule_work(a)  schedule_task(a)
#endif

#define flush_scheduled_work  flush_scheduled_tasks
#define INIT_WORK(_wq, _routine, _data)  INIT_TQUEUE(_wq, _routine, _data)
#define PREPARE_WORK(_wq, _routine, _data)  PREPARE_TQUEUE(_wq, _routine, _data)
#endif
#endif // < 2.5 kernel


#ifndef DECLARE_TASKLET
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
#define tasklet_schedule(a)   queue_task(a, &tq_scheduler)
#else
#define tasklet_schedule(a)   schedule_task(a)
#endif
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,38))
typedef struct device netdevice_t;
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,4))
typedef struct net_device netdevice_t;
#else
#undef netdevice_t
typedef struct net_device netdevice_t;
#endif

#ifdef WIRELESS_EXT
#if (WIRELESS_EXT < 13)
struct iw_request_info
{
        __u16           cmd;            /* Wireless Extension command */
        __u16           flags;          /* More to come ;-) */
};
#endif
#endif

/* linux < 2.5.69 */
#ifndef IRQ_NONE
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif

#ifndef in_atomic
#define in_atomic()  0
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))  // fixme
#define URB_ASYNC_UNLINK  USB_ASYNC_UNLINK
#else
#define USB_QUEUE_BULK 0
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
#define free_netdev(x)       kfree(x)
#endif


#endif
