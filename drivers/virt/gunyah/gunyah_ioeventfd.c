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
#include <linux/printk.h>

#include <uapi/linux/gunyah.h>

struct gh_ioeventfd {
	struct gh_vm_function_instance *f;
	struct gh_vm_io_handler io_handler;

	struct eventfd_ctx *ctx;
};

static int gh_write_ioeventfd(struct gh_vm_io_handler *io_dev, u64 addr, u32 len, u64 data)
{
	struct gh_ioeventfd *iofd = container_of(io_dev, struct gh_ioeventfd, io_handler);

	eventfd_signal(iofd->ctx, 1);
	return 0;
}

static struct gh_vm_io_handler_ops io_ops = {
	.write = gh_write_ioeventfd,
};

static long gh_ioeventfd_bind(struct gh_vm_function_instance *f)
{
	const struct gh_fn_ioeventfd_arg *args = f->argp;
	struct eventfd_ctx *ctx = NULL;
	struct gh_ioeventfd *iofd;
	int ret;

	if (f->arg_size != sizeof(*args))
		return -EINVAL;

	/* must be natural-word sized, or 0 to ignore length */
	switch (args->len) {
	case 0:
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		return -EINVAL;
	}

	/* check for range overflow */
	if (args->addr + args->len < args->addr)
		return -EINVAL;

	/* ioeventfd with no length can't be combined with DATAMATCH */
	if (!args->len && (args->flags & GH_IOEVENTFD_DATAMATCH))
		return -EINVAL;

	/* All other flag bits are reserved for future use */
	if (args->flags & ~GH_IOEVENTFD_DATAMATCH)
		return -EINVAL;

	ctx = eventfd_ctx_fdget(args->fd);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	iofd = kzalloc(sizeof(*iofd), GFP_KERNEL);
	if (!iofd) {
		ret = -ENOMEM;
		goto err_eventfd;
	}

	f->data = iofd;
	iofd->f = f;

	iofd->ctx = ctx;

	if (args->flags & GH_IOEVENTFD_DATAMATCH) {
		iofd->io_handler.datamatch = true;
		iofd->io_handler.len = args->len;
		iofd->io_handler.data = args->datamatch;
	}
	iofd->io_handler.addr = args->addr;
	iofd->io_handler.ops = &io_ops;

	ret = gh_vm_add_io_handler(f->ghvm, &iofd->io_handler);
	if (ret)
		goto err_io_dev_add;

	return 0;

err_io_dev_add:
	kfree(iofd);
err_eventfd:
	eventfd_ctx_put(ctx);
	return ret;
}

static void gh_ioevent_unbind(struct gh_vm_function_instance *f)
{
	struct gh_ioeventfd *iofd = f->data;

	eventfd_ctx_put(iofd->ctx);
	gh_vm_remove_io_handler(iofd->f->ghvm, &iofd->io_handler);
	kfree(iofd);
}

DECLARE_GH_VM_FUNCTION_INIT(ioeventfd, GH_FN_IOEVENTFD,
				gh_ioeventfd_bind, gh_ioevent_unbind);
MODULE_DESCRIPTION("Gunyah ioeventfds");
MODULE_LICENSE("GPL");
