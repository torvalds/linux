/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 */

#ifndef __IRQ_KERN_H__
#define __IRQ_KERN_H__

#include <linux/interrupt.h>
#include <asm/ptrace.h>
#include "irq_user.h"

#define UM_IRQ_ALLOC	-1

int um_request_irq(int irq, int fd, enum um_irq_type type,
		   irq_handler_t handler, unsigned long irqflags,
		   const char *devname, void *dev_id);
void um_free_irq(int irq, void *dev_id);
#endif
