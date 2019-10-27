/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 */

#ifndef __IRQ_KERN_H__
#define __IRQ_KERN_H__

#include <linux/interrupt.h>
#include <asm/ptrace.h>

extern int um_request_irq(unsigned int irq, int fd, int type,
			  irq_handler_t handler,
			  unsigned long irqflags,  const char * devname,
			  void *dev_id);
void um_free_irq(unsigned int irq, void *dev);
#endif

