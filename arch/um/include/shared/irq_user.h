/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __IRQ_USER_H__
#define __IRQ_USER_H__

#include <sysdep/ptrace.h>

struct irq_fd {
	struct irq_fd *next;
	void *id;
	int fd;
	int type;
	int irq;
	int events;
	int current_events;
};

enum { IRQ_READ, IRQ_WRITE };

struct siginfo;
extern void sigio_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs);
extern void free_irq_by_fd(int fd);
extern void reactivate_fd(int fd, int irqnum);
extern void deactivate_fd(int fd, int irqnum);
extern int deactivate_all_fds(void);
extern int activate_ipi(int fd, int pid);

#endif
