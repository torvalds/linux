/*
 * Part of Intel(R) Manageability Engine Interface Linux driver
 *
 * Copyright (c) 2003 - 2008 Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#include <linux/version.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
#define device_create(cls, parent, devt, devpar, fmt...) class_device_create(cls, parent, devt, NULL, ## fmt)
#define HECI_DEVICE_CREATE(cls, parent, devt, devpar, fmt...) class_device_create(cls, parent, devt, NULL, ## fmt)
#define device_destroy class_device_destroy
#else
#define HECI_DEVICE_CREATE(cls, parent, devt, devpar, fmt...) device_create(cls, parent, devt, ## fmt)
#endif
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23))
#define HECI_TASK_NONFREEZABLE current->flags |= PF_NOFREEZE;
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 17))
#include <asm/uaccess.h>
#else
#include <linux/uaccess.h>
#endif

/*****************************************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 28))
#undef INIT_WORK
#define INIT_WORK(_work, _func) \
do { \
	INIT_LIST_HEAD(&(_work)->entry); \
	(_work)->pending = 0; \
	(_work)->func = (void (*)(void *))_func; \
	(_work)->data = _work; \
	init_timer(&(_work)->timer); \
} while (0)
#undef PREPARE_WORK
#define PREPARE_WORK(_work, _func) \
	do { \
		(_work)->func =  (void (*)(void *))_func; \
		(_work)->data = _work; \
	} while (0)

#endif

#ifndef round_jiffies
#define round_jiffies(x) x
#endif

#endif /* < 2.6.20 */


/*****************************************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))

#ifndef IRQF_PROBE_SHARED
#ifdef SA_PROBEIRQ
#define IRQF_PROBE_SHARED SA_PROBEIRQ
#else
#define IRQF_PROBE_SHARED 0
#endif
#endif

#ifndef IRQF_SHARED
#define IRQF_SHARED SA_SHIRQ
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#endif /* < 2.6.18 */


/*****************************************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))

#if (!(defined(RHEL_MAJOR) && (RHEL_MAJOR == 5)))
#if (!(defined(RHEL_VERSION) && (RHEL_VERSION == 4) && (RHEL_UPDATE >= 5)))
typedef irqreturn_t (*irq_handler_t)(int, void*, struct pt_regs *);
#endif
#endif

typedef irqreturn_t (*new_handler_t)(int, void*);
static inline irqreturn_t _kc_request_irq(unsigned int irq,
					  new_handler_t handler,
					  unsigned long flags,
					  const char *devname,
					  void *dev_id)
{
	irq_handler_t new_handler = (irq_handler_t) handler;
	return request_irq(irq, new_handler, flags, devname, dev_id);
}

#undef request_irq
#define request_irq(irq, handler, flags, devname, dev_id) _kc_request_irq((irq), (handler), (flags), (devname), (dev_id))

#endif


#endif
