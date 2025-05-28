/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __IRQ_USER_H__
#define __IRQ_USER_H__

#include <sysdep/ptrace.h>

enum um_irq_type {
	IRQ_READ,
	IRQ_WRITE,
	NUM_IRQ_TYPES,
};

struct siginfo;
extern void sigio_handler(int sig, struct siginfo *unused_si,
			  struct uml_pt_regs *regs, void *mc);
void sigio_run_timetravel_handlers(void);
extern void free_irq_by_fd(int fd);
extern void deactivate_fd(int fd, int irqnum);
extern int deactivate_all_fds(void);

#endif
