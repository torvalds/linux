// SPDX-License-Identifier: GPL-2.0-only

/*
 *  HID-BPF support for Linux
 *
 *  Copyright (c) 2022 Benjamin Tissoires
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/bitops.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <linux/hid.h>
#include <linux/hid_bpf.h>
#include <linux/init.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include "hid_bpf_dispatch.h"
#include "entrypoints/entrypoints.lskel.h"

struct hid_bpf_ops *hid_bpf_ops;
EXPORT_SYMBOL(hid_bpf_ops);

/**
 * hid_bpf_device_event - Called whenever an event is coming in from the device
 *
 * @ctx: The HID-BPF context
 *
 * @return %0 on success and keep processing; a negative error code to interrupt
 * the processing of this event
 *
 * Declare an %fmod_ret tracing bpf program to this function and attach this
 * program through hid_bpf_attach_prog() to have this helper called for
 * any incoming event from the device itself.
 *
 * The function is called while on IRQ context, so we can not sleep.
 */
/* never used by the kernel but declared so we can load and attach a tracepoint */
__weak noinline int hid_bpf_device_event(struct hid_bpf_ctx *ctx)
{
	return 0;
}
ALLOW_ERROR_INJECTION(hid_bpf_device_event, ERRNO);

int
dispatch_hid_bpf_device_event(struct hid_device *hdev, enum hid_report_type type, u8 *data,
			      u32 size, int interrupt)
{
	struct hid_bpf_ctx_kern ctx_kern = {
		.ctx = {
			.hid = hdev,
			.report_type = type,
			.size = size,
		},
		.data = data,
	};

	if (type >= HID_REPORT_TYPES)
		return -EINVAL;

	return hid_bpf_prog_run(hdev, HID_BPF_PROG_TYPE_DEVICE_EVENT, &ctx_kern);
}
EXPORT_SYMBOL_GPL(dispatch_hid_bpf_device_event);

/**
 * hid_bpf_get_data - Get the kernel memory pointer associated with the context @ctx
 *
 * @ctx: The HID-BPF context
 * @offset: The offset within the memory
 * @rdwr_buf_size: the const size of the buffer
 *
 * @returns %NULL on error, an %__u8 memory pointer on success
 */
noinline __u8 *
hid_bpf_get_data(struct hid_bpf_ctx *ctx, unsigned int offset, const size_t rdwr_buf_size)
{
	struct hid_bpf_ctx_kern *ctx_kern;

	if (!ctx)
		return NULL;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);

	if (rdwr_buf_size + offset > ctx->size)
		return NULL;

	return ctx_kern->data + offset;
}

/*
 * The following set contains all functions we agree BPF programs
 * can use.
 */
BTF_SET8_START(hid_bpf_kfunc_ids)
BTF_ID_FLAGS(func, call_hid_bpf_prog_release)
BTF_ID_FLAGS(func, hid_bpf_get_data, KF_RET_NULL)
BTF_SET8_END(hid_bpf_kfunc_ids)

static const struct btf_kfunc_id_set hid_bpf_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &hid_bpf_kfunc_ids,
};

static int device_match_id(struct device *dev, const void *id)
{
	struct hid_device *hdev = to_hid_device(dev);

	return hdev->id == *(int *)id;
}

/**
 * hid_bpf_attach_prog - Attach the given @prog_fd to the given HID device
 *
 * @hid_id: the system unique identifier of the HID device
 * @prog_fd: an fd in the user process representing the program to attach
 * @flags: any logical OR combination of &enum hid_bpf_attach_flags
 *
 * @returns %0 on success, an error code otherwise.
 */
/* called from syscall */
noinline int
hid_bpf_attach_prog(unsigned int hid_id, int prog_fd, __u32 flags)
{
	struct hid_device *hdev;
	struct device *dev;
	int prog_type = hid_bpf_get_prog_attach_type(prog_fd);

	if (!hid_bpf_ops)
		return -EINVAL;

	if (prog_type < 0)
		return prog_type;

	if (prog_type >= HID_BPF_PROG_TYPE_MAX)
		return -EINVAL;

	if ((flags & ~HID_BPF_FLAG_MASK))
		return -EINVAL;

	dev = bus_find_device(hid_bpf_ops->bus_type, NULL, &hid_id, device_match_id);
	if (!dev)
		return -EINVAL;

	hdev = to_hid_device(dev);

	return __hid_bpf_attach_prog(hdev, prog_type, prog_fd, flags);
}

/* for syscall HID-BPF */
BTF_SET8_START(hid_bpf_syscall_kfunc_ids)
BTF_ID_FLAGS(func, hid_bpf_attach_prog)
BTF_SET8_END(hid_bpf_syscall_kfunc_ids)

static const struct btf_kfunc_id_set hid_bpf_syscall_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &hid_bpf_syscall_kfunc_ids,
};

void hid_bpf_destroy_device(struct hid_device *hdev)
{
	if (!hdev)
		return;

	/* mark the device as destroyed in bpf so we don't reattach it */
	hdev->bpf.destroyed = true;

	__hid_bpf_destroy_device(hdev);
}
EXPORT_SYMBOL_GPL(hid_bpf_destroy_device);

void hid_bpf_device_init(struct hid_device *hdev)
{
	spin_lock_init(&hdev->bpf.progs_lock);
}
EXPORT_SYMBOL_GPL(hid_bpf_device_init);

static int __init hid_bpf_init(void)
{
	int err;

	/* Note: if we exit with an error any time here, we would entirely break HID, which
	 * is probably not something we want. So we log an error and return success.
	 *
	 * This is not a big deal: the syscall allowing to attach a BPF program to a HID device
	 * will not be available, so nobody will be able to use the functionality.
	 */

	err = register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &hid_bpf_kfunc_set);
	if (err) {
		pr_warn("error while setting HID BPF tracing kfuncs: %d", err);
		return 0;
	}

	err = hid_bpf_preload_skel();
	if (err) {
		pr_warn("error while preloading HID BPF dispatcher: %d", err);
		return 0;
	}

	/* register syscalls after we are sure we can load our preloaded bpf program */
	err = register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &hid_bpf_syscall_kfunc_set);
	if (err) {
		pr_warn("error while setting HID BPF syscall kfuncs: %d", err);
		return 0;
	}

	return 0;
}

static void __exit hid_bpf_exit(void)
{
	/* HID depends on us, so if we hit that code, we are guaranteed that hid
	 * has been removed and thus we do not need to clear the HID devices
	 */
	hid_bpf_free_links_and_skel();
}

late_initcall(hid_bpf_init);
module_exit(hid_bpf_exit);
MODULE_AUTHOR("Benjamin Tissoires");
MODULE_LICENSE("GPL");
