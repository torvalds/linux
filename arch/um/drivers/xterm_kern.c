// SPDX-License-Identifier: GPL-2.0
/* 
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/irqreturn.h>
#include <asm/irq.h>
#include <irq_kern.h>
#include <os.h>

struct xterm_wait {
	struct completion ready;
	int fd;
	int pid;
	int new_fd;
};

static irqreturn_t xterm_interrupt(int irq, void *data)
{
	struct xterm_wait *xterm = data;
	int fd;

	fd = os_rcv_fd(xterm->fd, &xterm->pid);
	if (fd == -EAGAIN)
		return IRQ_NONE;

	xterm->new_fd = fd;
	complete(&xterm->ready);

	return IRQ_HANDLED;
}

int xterm_fd(int socket, int *pid_out)
{
	struct xterm_wait *data;
	int err, ret;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		printk(KERN_ERR "xterm_fd : failed to allocate xterm_wait\n");
		return -ENOMEM;
	}

	/* This is a locked semaphore... */
	*data = ((struct xterm_wait) { .fd 		= socket,
				       .pid 		= -1,
				       .new_fd	 	= -1 });
	init_completion(&data->ready);

	err = um_request_irq(XTERM_IRQ, socket, IRQ_READ, xterm_interrupt,
			     IRQF_SHARED, "xterm", data);
	if (err < 0) {
		printk(KERN_ERR "xterm_fd : failed to get IRQ for xterm, "
		       "err = %d\n",  err);
		ret = err;
		goto out;
	}

	/* ... so here we wait for an xterm interrupt.
	 *
	 * XXX Note, if the xterm doesn't work for some reason (eg. DISPLAY
	 * isn't set) this will hang... */
	wait_for_completion(&data->ready);

	um_free_irq(XTERM_IRQ, data);

	ret = data->new_fd;
	*pid_out = data->pid;
 out:
	kfree(data);

	return ret;
}
