/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __IRQ_KERN_H__
#define __IRQ_KERN_H__

#include "linux/interrupt.h"
#include "asm/ptrace.h"

extern int um_request_irq(unsigned int irq, int fd, int type,
			  irqreturn_t (*handler)(int, void *,
						 struct pt_regs *),
			  unsigned long irqflags,  const char * devname,
			  void *dev_id);
extern int init_aio_irq(int irq, char *name,
			irqreturn_t (*handler)(int, void *, struct pt_regs *));

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
