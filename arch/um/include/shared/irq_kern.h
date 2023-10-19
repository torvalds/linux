/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 */

#ifndef __IRQ_KERN_H__
#define __IRQ_KERN_H__

#include <linux/interrupt.h>
#include <linux/time-internal.h>
#include <asm/ptrace.h>
#include "irq_user.h"

#define UM_IRQ_ALLOC	-1

int um_request_irq(int irq, int fd, enum um_irq_type type,
		   irq_handler_t handler, unsigned long irqflags,
		   const char *devname, void *dev_id);

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
/**
 * um_request_irq_tt - request an IRQ with timetravel handler
 *
 * @irq: the IRQ number, or %UM_IRQ_ALLOC
 * @fd: The file descriptor to request an IRQ for
 * @type: read or write
 * @handler: the (generic style) IRQ handler
 * @irqflags: Linux IRQ flags
 * @devname: name for this to show
 * @dev_id: data pointer to pass to the IRQ handler
 * @timetravel_handler: the timetravel interrupt handler, invoked with the IRQ
 *	number, fd, dev_id and time-travel event pointer.
 *
 * Returns: The interrupt number assigned or a negative error.
 *
 * Note that the timetravel handler is invoked only if the time_travel_mode is
 * %TT_MODE_EXTERNAL, and then it is invoked even while the system is suspended!
 * This function must call time_travel_add_irq_event() for the event passed with
 * an appropriate delay, before sending an ACK on the socket it was invoked for.
 *
 * If this was called while the system is suspended, then adding the event will
 * cause the system to resume.
 *
 * Since this function will almost certainly have to handle the FD's condition,
 * a read will consume the message, and after that it is up to the code using
 * it to pass such a message to the @handler in whichever way it can.
 *
 * If time_travel_mode is not %TT_MODE_EXTERNAL the @timetravel_handler will
 * not be invoked at all and the @handler must handle the FD becoming
 * readable (or writable) instead. Use um_irq_timetravel_handler_used() to
 * distinguish these cases.
 *
 * See virtio_uml.c for an example.
 */
int um_request_irq_tt(int irq, int fd, enum um_irq_type type,
		      irq_handler_t handler, unsigned long irqflags,
		      const char *devname, void *dev_id,
		      void (*timetravel_handler)(int, int, void *,
						 struct time_travel_event *));
#else
static inline
int um_request_irq_tt(int irq, int fd, enum um_irq_type type,
		      irq_handler_t handler, unsigned long irqflags,
		      const char *devname, void *dev_id,
		      void (*timetravel_handler)(int, int, void *,
						 struct time_travel_event *))
{
	return um_request_irq(irq, fd, type, handler, irqflags,
			      devname, dev_id);
}
#endif

static inline bool um_irq_timetravel_handler_used(void)
{
	return time_travel_mode == TT_MODE_EXTERNAL;
}

void um_free_irq(int irq, void *dev_id);
#endif
