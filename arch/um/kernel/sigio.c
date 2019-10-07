// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{linux.intel,addtoit}.com)
 */

#include <linux/interrupt.h>
#include <irq_kern.h>
#include <os.h>
#include <sigio.h>

/* Protected by sigio_lock() called from write_sigio_workaround */
static int sigio_irq_fd = -1;

static irqreturn_t sigio_interrupt(int irq, void *data)
{
	char c;

	os_read_file(sigio_irq_fd, &c, sizeof(c));
	return IRQ_HANDLED;
}

int write_sigio_irq(int fd)
{
	int err;

	err = um_request_irq(SIGIO_WRITE_IRQ, fd, IRQ_READ, sigio_interrupt,
			     0, "write sigio", NULL);
	if (err) {
		printk(KERN_ERR "write_sigio_irq : um_request_irq failed, "
		       "err = %d\n", err);
		return -1;
	}
	sigio_irq_fd = fd;
	return 0;
}

/* These are called from os-Linux/sigio.c to protect its pollfds arrays. */
static DEFINE_SPINLOCK(sigio_spinlock);

void sigio_lock(void)
{
	spin_lock(&sigio_spinlock);
}

void sigio_unlock(void)
{
	spin_unlock(&sigio_spinlock);
}
