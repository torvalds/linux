// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/gunyah.h>
#include <linux/gunyah_vm_mgr.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/printk.h>

#include <uapi/linux/gunyah.h>

struct gh_irqfd {
	struct gh_resource *ghrsc;
	struct gh_vm_resource_ticket ticket;
	struct gh_vm_function_instance *f;

	bool level;

	struct eventfd_ctx *ctx;
	wait_queue_entry_t wait;
	poll_table pt;
};

static int irqfd_wakeup(wait_queue_entry_t *wait, unsigned int mode, int sync, void *key)
{
	struct gh_irqfd *irqfd = container_of(wait, struct gh_irqfd, wait);
	__poll_t flags = key_to_poll(key);
	u64 enable_mask = GH_BELL_NONBLOCK;
	u64 old_flags;
	int ret = 0;

	if (flags & EPOLLIN) {
		if (irqfd->ghrsc) {
			ret = gh_hypercall_bell_send(irqfd->ghrsc->capid, enable_mask, &old_flags);
			if (ret)
				pr_err_ratelimited("Failed to inject interrupt %d: %d\n",
						irqfd->ticket.label, ret);
		} else
			pr_err_ratelimited("Premature injection of interrupt\n");
	}

	return 0;
}

static void irqfd_ptable_queue_proc(struct file *file, wait_queue_head_t *wqh, poll_table *pt)
{
	struct gh_irqfd *irq_ctx = container_of(pt, struct gh_irqfd, pt);

	add_wait_queue(wqh, &irq_ctx->wait);
}

static int gh_irqfd_populate(struct gh_vm_resource_ticket *ticket, struct gh_resource *ghrsc)
{
	struct gh_irqfd *irqfd = container_of(ticket, struct gh_irqfd, ticket);
	u64 enable_mask = GH_BELL_NONBLOCK;
	u64 ack_mask = ~0;
	int ret = 0;

	if (irqfd->ghrsc) {
		pr_warn("irqfd%d already got a Gunyah resource. Check if multiple resources with same label were configured.\n",
			irqfd->ticket.label);
		return -1;
	}

	irqfd->ghrsc = ghrsc;
	if (irqfd->level) {
		ret = gh_hypercall_bell_set_mask(irqfd->ghrsc->capid, enable_mask, ack_mask);
		if (ret)
			pr_warn("irq %d couldn't be set as level triggered. Might cause IRQ storm if asserted\n",
				irqfd->ticket.label);
	}

	return 0;
}

static void gh_irqfd_unpopulate(struct gh_vm_resource_ticket *ticket, struct gh_resource *ghrsc)
{
	struct gh_irqfd *irqfd = container_of(ticket, struct gh_irqfd, ticket);
	u64 cnt;

	eventfd_ctx_remove_wait_queue(irqfd->ctx, &irqfd->wait, &cnt);
}

static long gh_irqfd_bind(struct gh_vm_function_instance *f)
{
	struct gh_fn_irqfd_arg *args = f->argp;
	struct gh_irqfd *irqfd;
	__poll_t events;
	struct fd fd;
	long r;

	if (f->arg_size != sizeof(*args))
		return -EINVAL;

	/* All other flag bits are reserved for future use */
	if (args->flags & ~GH_IRQFD_LEVEL)
		return -EINVAL;

	irqfd = kzalloc(sizeof(*irqfd), GFP_KERNEL);
	if (!irqfd)
		return -ENOMEM;

	irqfd->f = f;
	f->data = irqfd;

	fd = fdget(args->fd);
	if (!fd.file) {
		kfree(irqfd);
		return -EBADF;
	}

	irqfd->ctx = eventfd_ctx_fileget(fd.file);
	if (IS_ERR(irqfd->ctx)) {
		r = PTR_ERR(irqfd->ctx);
		goto err_fdput;
	}

	if (args->flags & GH_IRQFD_LEVEL)
		irqfd->level = true;

	init_waitqueue_func_entry(&irqfd->wait, irqfd_wakeup);
	init_poll_funcptr(&irqfd->pt, irqfd_ptable_queue_proc);

	irqfd->ticket.resource_type = GH_RESOURCE_TYPE_BELL_TX;
	irqfd->ticket.label = args->label;
	irqfd->ticket.owner = THIS_MODULE;
	irqfd->ticket.populate = gh_irqfd_populate;
	irqfd->ticket.unpopulate = gh_irqfd_unpopulate;

	r = gh_vm_add_resource_ticket(f->ghvm, &irqfd->ticket);
	if (r)
		goto err_ctx;

	events = vfs_poll(fd.file, &irqfd->pt);
	if (events & EPOLLIN)
		pr_warn("Premature injection of interrupt\n");
	fdput(fd);

	return 0;
err_ctx:
	eventfd_ctx_put(irqfd->ctx);
err_fdput:
	fdput(fd);
	kfree(irqfd);
	return r;
}

static void gh_irqfd_unbind(struct gh_vm_function_instance *f)
{
	struct gh_irqfd *irqfd = f->data;

	gh_vm_remove_resource_ticket(irqfd->f->ghvm, &irqfd->ticket);
	eventfd_ctx_put(irqfd->ctx);
	kfree(irqfd);
}

DECLARE_GH_VM_FUNCTION_INIT(irqfd, GH_FN_IRQFD, gh_irqfd_bind, gh_irqfd_unbind);
MODULE_DESCRIPTION("Gunyah irqfds");
MODULE_LICENSE("GPL");
