// SPDX-License-Identifier: GPL-2.0-only

/*
 *  HID-BPF support for Linux
 *
 *  Copyright (c) 2022-2024 Benjamin Tissoires
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
#include <linux/minmax.h>
#include <linux/module.h>
#include "hid_bpf_dispatch.h"

const struct hid_ops *hid_ops;
EXPORT_SYMBOL(hid_ops);

u8 *
dispatch_hid_bpf_device_event(struct hid_device *hdev, enum hid_report_type type, u8 *data,
			      u32 *size, int interrupt, u64 source, bool from_bpf)
{
	struct hid_bpf_ctx_kern ctx_kern = {
		.ctx = {
			.hid = hdev,
			.allocated_size = hdev->bpf.allocated_data,
			.size = *size,
		},
		.data = hdev->bpf.device_data,
		.from_bpf = from_bpf,
	};
	struct hid_bpf_ops *e;
	int ret;

	if (type >= HID_REPORT_TYPES)
		return ERR_PTR(-EINVAL);

	/* no program has been attached yet */
	if (!hdev->bpf.device_data)
		return data;

	memset(ctx_kern.data, 0, hdev->bpf.allocated_data);
	memcpy(ctx_kern.data, data, *size);

	rcu_read_lock();
	list_for_each_entry_rcu(e, &hdev->bpf.prog_list, list) {
		if (e->hid_device_event) {
			ret = e->hid_device_event(&ctx_kern.ctx, type, source);
			if (ret < 0) {
				rcu_read_unlock();
				return ERR_PTR(ret);
			}

			if (ret)
				ctx_kern.ctx.size = ret;
		}
	}
	rcu_read_unlock();

	ret = ctx_kern.ctx.size;
	if (ret) {
		if (ret > ctx_kern.ctx.allocated_size)
			return ERR_PTR(-EINVAL);

		*size = ret;
	}

	return ctx_kern.data;
}
EXPORT_SYMBOL_GPL(dispatch_hid_bpf_device_event);

int dispatch_hid_bpf_raw_requests(struct hid_device *hdev,
				  unsigned char reportnum, u8 *buf,
				  u32 size, enum hid_report_type rtype,
				  enum hid_class_request reqtype,
				  u64 source, bool from_bpf)
{
	struct hid_bpf_ctx_kern ctx_kern = {
		.ctx = {
			.hid = hdev,
			.allocated_size = size,
			.size = size,
		},
		.data = buf,
		.from_bpf = from_bpf,
	};
	struct hid_bpf_ops *e;
	int ret, idx;

	if (rtype >= HID_REPORT_TYPES)
		return -EINVAL;

	idx = srcu_read_lock(&hdev->bpf.srcu);
	list_for_each_entry_srcu(e, &hdev->bpf.prog_list, list,
				 srcu_read_lock_held(&hdev->bpf.srcu)) {
		if (!e->hid_hw_request)
			continue;

		ret = e->hid_hw_request(&ctx_kern.ctx, reportnum, rtype, reqtype, source);
		if (ret)
			goto out;
	}
	ret = 0;

out:
	srcu_read_unlock(&hdev->bpf.srcu, idx);
	return ret;
}
EXPORT_SYMBOL_GPL(dispatch_hid_bpf_raw_requests);

int dispatch_hid_bpf_output_report(struct hid_device *hdev,
				   __u8 *buf, u32 size, u64 source,
				   bool from_bpf)
{
	struct hid_bpf_ctx_kern ctx_kern = {
		.ctx = {
			.hid = hdev,
			.allocated_size = size,
			.size = size,
		},
		.data = buf,
		.from_bpf = from_bpf,
	};
	struct hid_bpf_ops *e;
	int ret, idx;

	idx = srcu_read_lock(&hdev->bpf.srcu);
	list_for_each_entry_srcu(e, &hdev->bpf.prog_list, list,
				 srcu_read_lock_held(&hdev->bpf.srcu)) {
		if (!e->hid_hw_output_report)
			continue;

		ret = e->hid_hw_output_report(&ctx_kern.ctx, source);
		if (ret)
			goto out;
	}
	ret = 0;

out:
	srcu_read_unlock(&hdev->bpf.srcu, idx);
	return ret;
}
EXPORT_SYMBOL_GPL(dispatch_hid_bpf_output_report);

const u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, const u8 *rdesc, unsigned int *size)
{
	int ret;
	struct hid_bpf_ctx_kern ctx_kern = {
		.ctx = {
			.hid = hdev,
			.size = *size,
			.allocated_size = HID_MAX_DESCRIPTOR_SIZE,
		},
	};

	if (!hdev->bpf.rdesc_ops)
		goto ignore_bpf;

	ctx_kern.data = kzalloc(ctx_kern.ctx.allocated_size, GFP_KERNEL);
	if (!ctx_kern.data)
		goto ignore_bpf;

	memcpy(ctx_kern.data, rdesc, min_t(unsigned int, *size, HID_MAX_DESCRIPTOR_SIZE));

	ret = hdev->bpf.rdesc_ops->hid_rdesc_fixup(&ctx_kern.ctx);
	if (ret < 0)
		goto ignore_bpf;

	if (ret) {
		if (ret > ctx_kern.ctx.allocated_size)
			goto ignore_bpf;

		*size = ret;
	}

	return krealloc(ctx_kern.data, *size, GFP_KERNEL);

 ignore_bpf:
	kfree(ctx_kern.data);
	return rdesc;
}
EXPORT_SYMBOL_GPL(call_hid_bpf_rdesc_fixup);

static int device_match_id(struct device *dev, const void *id)
{
	struct hid_device *hdev = to_hid_device(dev);

	return hdev->id == *(int *)id;
}

struct hid_device *hid_get_device(unsigned int hid_id)
{
	struct device *dev;

	if (!hid_ops)
		return ERR_PTR(-EINVAL);

	dev = bus_find_device(hid_ops->bus_type, NULL, &hid_id, device_match_id);
	if (!dev)
		return ERR_PTR(-EINVAL);

	return to_hid_device(dev);
}

void hid_put_device(struct hid_device *hid)
{
	put_device(&hid->dev);
}

static int __hid_bpf_allocate_data(struct hid_device *hdev, u8 **data, u32 *size)
{
	u8 *alloc_data;
	unsigned int i, j, max_report_len = 0;
	size_t alloc_size = 0;

	/* compute the maximum report length for this device */
	for (i = 0; i < HID_REPORT_TYPES; i++) {
		struct hid_report_enum *report_enum = hdev->report_enum + i;

		for (j = 0; j < HID_MAX_IDS; j++) {
			struct hid_report *report = report_enum->report_id_hash[j];

			if (report)
				max_report_len = max(max_report_len, hid_report_len(report));
		}
	}

	/*
	 * Give us a little bit of extra space and some predictability in the
	 * buffer length we create. This way, we can tell users that they can
	 * work on chunks of 64 bytes of memory without having the bpf verifier
	 * scream at them.
	 */
	alloc_size = DIV_ROUND_UP(max_report_len, 64) * 64;

	alloc_data = kzalloc(alloc_size, GFP_KERNEL);
	if (!alloc_data)
		return -ENOMEM;

	*data = alloc_data;
	*size = alloc_size;

	return 0;
}

int hid_bpf_allocate_event_data(struct hid_device *hdev)
{
	/* hdev->bpf.device_data is already allocated, abort */
	if (hdev->bpf.device_data)
		return 0;

	return __hid_bpf_allocate_data(hdev, &hdev->bpf.device_data, &hdev->bpf.allocated_data);
}

int hid_bpf_reconnect(struct hid_device *hdev)
{
	if (!test_and_set_bit(ffs(HID_STAT_REPROBED), &hdev->status)) {
		/* trigger call to call_hid_bpf_rdesc_fixup() during the next probe */
		hdev->bpf_rsize = 0;
		return device_reprobe(&hdev->dev);
	}

	return 0;
}

/* Disables missing prototype warnings */
__bpf_kfunc_start_defs();

/**
 * hid_bpf_get_data - Get the kernel memory pointer associated with the context @ctx
 *
 * @ctx: The HID-BPF context
 * @offset: The offset within the memory
 * @rdwr_buf_size: the const size of the buffer
 *
 * @returns %NULL on error, an %__u8 memory pointer on success
 */
__bpf_kfunc __u8 *
hid_bpf_get_data(struct hid_bpf_ctx *ctx, unsigned int offset, const size_t rdwr_buf_size)
{
	struct hid_bpf_ctx_kern *ctx_kern;

	if (!ctx)
		return NULL;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);

	if (rdwr_buf_size + offset > ctx->allocated_size)
		return NULL;

	return ctx_kern->data + offset;
}

/**
 * hid_bpf_allocate_context - Allocate a context to the given HID device
 *
 * @hid_id: the system unique identifier of the HID device
 *
 * @returns A pointer to &struct hid_bpf_ctx on success, %NULL on error.
 */
__bpf_kfunc struct hid_bpf_ctx *
hid_bpf_allocate_context(unsigned int hid_id)
{
	struct hid_device *hdev;
	struct hid_bpf_ctx_kern *ctx_kern = NULL;

	hdev = hid_get_device(hid_id);
	if (IS_ERR(hdev))
		return NULL;

	ctx_kern = kzalloc(sizeof(*ctx_kern), GFP_KERNEL);
	if (!ctx_kern) {
		hid_put_device(hdev);
		return NULL;
	}

	ctx_kern->ctx.hid = hdev;

	return &ctx_kern->ctx;
}

/**
 * hid_bpf_release_context - Release the previously allocated context @ctx
 *
 * @ctx: the HID-BPF context to release
 *
 */
__bpf_kfunc void
hid_bpf_release_context(struct hid_bpf_ctx *ctx)
{
	struct hid_bpf_ctx_kern *ctx_kern;
	struct hid_device *hid;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);
	hid = (struct hid_device *)ctx_kern->ctx.hid; /* ignore const */

	kfree(ctx_kern);

	/* get_device() is called by bus_find_device() */
	hid_put_device(hid);
}

static int
__hid_bpf_hw_check_params(struct hid_bpf_ctx *ctx, __u8 *buf, size_t *buf__sz,
			  enum hid_report_type rtype)
{
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	u32 report_len;

	/* check arguments */
	if (!ctx || !hid_ops || !buf)
		return -EINVAL;

	switch (rtype) {
	case HID_INPUT_REPORT:
	case HID_OUTPUT_REPORT:
	case HID_FEATURE_REPORT:
		break;
	default:
		return -EINVAL;
	}

	if (*buf__sz < 1)
		return -EINVAL;

	report_enum = ctx->hid->report_enum + rtype;
	report = hid_ops->hid_get_report(report_enum, buf);
	if (!report)
		return -EINVAL;

	report_len = hid_report_len(report);

	if (*buf__sz > report_len)
		*buf__sz = report_len;

	return 0;
}

/**
 * hid_bpf_hw_request - Communicate with a HID device
 *
 * @ctx: the HID-BPF context previously allocated in hid_bpf_allocate_context()
 * @buf: a %PTR_TO_MEM buffer
 * @buf__sz: the size of the data to transfer
 * @rtype: the type of the report (%HID_INPUT_REPORT, %HID_FEATURE_REPORT, %HID_OUTPUT_REPORT)
 * @reqtype: the type of the request (%HID_REQ_GET_REPORT, %HID_REQ_SET_REPORT, ...)
 *
 * @returns %0 on success, a negative error code otherwise.
 */
__bpf_kfunc int
hid_bpf_hw_request(struct hid_bpf_ctx *ctx, __u8 *buf, size_t buf__sz,
		   enum hid_report_type rtype, enum hid_class_request reqtype)
{
	struct hid_bpf_ctx_kern *ctx_kern;
	size_t size = buf__sz;
	u8 *dma_data;
	int ret;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);

	if (ctx_kern->from_bpf)
		return -EDEADLOCK;

	/* check arguments */
	ret = __hid_bpf_hw_check_params(ctx, buf, &size, rtype);
	if (ret)
		return ret;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
	case HID_REQ_GET_IDLE:
	case HID_REQ_GET_PROTOCOL:
	case HID_REQ_SET_REPORT:
	case HID_REQ_SET_IDLE:
	case HID_REQ_SET_PROTOCOL:
		break;
	default:
		return -EINVAL;
	}

	dma_data = kmemdup(buf, size, GFP_KERNEL);
	if (!dma_data)
		return -ENOMEM;

	ret = hid_ops->hid_hw_raw_request(ctx->hid,
					      dma_data[0],
					      dma_data,
					      size,
					      rtype,
					      reqtype,
					      (u64)(long)ctx,
					      true); /* prevent infinite recursions */

	if (ret > 0)
		memcpy(buf, dma_data, ret);

	kfree(dma_data);
	return ret;
}

/**
 * hid_bpf_hw_output_report - Send an output report to a HID device
 *
 * @ctx: the HID-BPF context previously allocated in hid_bpf_allocate_context()
 * @buf: a %PTR_TO_MEM buffer
 * @buf__sz: the size of the data to transfer
 *
 * Returns the number of bytes transferred on success, a negative error code otherwise.
 */
__bpf_kfunc int
hid_bpf_hw_output_report(struct hid_bpf_ctx *ctx, __u8 *buf, size_t buf__sz)
{
	struct hid_bpf_ctx_kern *ctx_kern;
	size_t size = buf__sz;
	u8 *dma_data;
	int ret;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);
	if (ctx_kern->from_bpf)
		return -EDEADLOCK;

	/* check arguments */
	ret = __hid_bpf_hw_check_params(ctx, buf, &size, HID_OUTPUT_REPORT);
	if (ret)
		return ret;

	dma_data = kmemdup(buf, size, GFP_KERNEL);
	if (!dma_data)
		return -ENOMEM;

	ret = hid_ops->hid_hw_output_report(ctx->hid, dma_data, size, (u64)(long)ctx, true);

	kfree(dma_data);
	return ret;
}

static int
__hid_bpf_input_report(struct hid_bpf_ctx *ctx, enum hid_report_type type, u8 *buf,
		       size_t size, bool lock_already_taken)
{
	struct hid_bpf_ctx_kern *ctx_kern;
	int ret;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);
	if (ctx_kern->from_bpf)
		return -EDEADLOCK;

	/* check arguments */
	ret = __hid_bpf_hw_check_params(ctx, buf, &size, type);
	if (ret)
		return ret;

	return hid_ops->hid_input_report(ctx->hid, type, buf, size, 0, (u64)(long)ctx, true,
					 lock_already_taken);
}

/**
 * hid_bpf_try_input_report - Inject a HID report in the kernel from a HID device
 *
 * @ctx: the HID-BPF context previously allocated in hid_bpf_allocate_context()
 * @type: the type of the report (%HID_INPUT_REPORT, %HID_FEATURE_REPORT, %HID_OUTPUT_REPORT)
 * @buf: a %PTR_TO_MEM buffer
 * @buf__sz: the size of the data to transfer
 *
 * Returns %0 on success, a negative error code otherwise. This function will immediately
 * fail if the device is not available, thus can be safely used in IRQ context.
 */
__bpf_kfunc int
hid_bpf_try_input_report(struct hid_bpf_ctx *ctx, enum hid_report_type type, u8 *buf,
			 const size_t buf__sz)
{
	struct hid_bpf_ctx_kern *ctx_kern;
	bool from_hid_event_hook;

	ctx_kern = container_of(ctx, struct hid_bpf_ctx_kern, ctx);
	from_hid_event_hook = ctx_kern->data && ctx_kern->data == ctx->hid->bpf.device_data;

	return __hid_bpf_input_report(ctx, type, buf, buf__sz, from_hid_event_hook);
}

/**
 * hid_bpf_input_report - Inject a HID report in the kernel from a HID device
 *
 * @ctx: the HID-BPF context previously allocated in hid_bpf_allocate_context()
 * @type: the type of the report (%HID_INPUT_REPORT, %HID_FEATURE_REPORT, %HID_OUTPUT_REPORT)
 * @buf: a %PTR_TO_MEM buffer
 * @buf__sz: the size of the data to transfer
 *
 * Returns %0 on success, a negative error code otherwise. This function will wait for the
 * device to be available before injecting the event, thus needs to be called in sleepable
 * context.
 */
__bpf_kfunc int
hid_bpf_input_report(struct hid_bpf_ctx *ctx, enum hid_report_type type, u8 *buf,
		     const size_t buf__sz)
{
	int ret;

	ret = down_interruptible(&ctx->hid->driver_input_lock);
	if (ret)
		return ret;

	/* check arguments */
	ret = __hid_bpf_input_report(ctx, type, buf, buf__sz, true /* lock_already_taken */);

	up(&ctx->hid->driver_input_lock);

	return ret;
}
__bpf_kfunc_end_defs();

/*
 * The following set contains all functions we agree BPF programs
 * can use.
 */
BTF_KFUNCS_START(hid_bpf_kfunc_ids)
BTF_ID_FLAGS(func, hid_bpf_get_data, KF_RET_NULL)
BTF_ID_FLAGS(func, hid_bpf_allocate_context, KF_ACQUIRE | KF_RET_NULL | KF_SLEEPABLE)
BTF_ID_FLAGS(func, hid_bpf_release_context, KF_RELEASE | KF_SLEEPABLE)
BTF_ID_FLAGS(func, hid_bpf_hw_request, KF_SLEEPABLE)
BTF_ID_FLAGS(func, hid_bpf_hw_output_report, KF_SLEEPABLE)
BTF_ID_FLAGS(func, hid_bpf_input_report, KF_SLEEPABLE)
BTF_ID_FLAGS(func, hid_bpf_try_input_report)
BTF_KFUNCS_END(hid_bpf_kfunc_ids)

static const struct btf_kfunc_id_set hid_bpf_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &hid_bpf_kfunc_ids,
};

/* for syscall HID-BPF */
BTF_KFUNCS_START(hid_bpf_syscall_kfunc_ids)
BTF_ID_FLAGS(func, hid_bpf_allocate_context, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, hid_bpf_release_context, KF_RELEASE)
BTF_ID_FLAGS(func, hid_bpf_hw_request)
BTF_ID_FLAGS(func, hid_bpf_hw_output_report)
BTF_ID_FLAGS(func, hid_bpf_input_report)
BTF_KFUNCS_END(hid_bpf_syscall_kfunc_ids)

static const struct btf_kfunc_id_set hid_bpf_syscall_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &hid_bpf_syscall_kfunc_ids,
};

int hid_bpf_connect_device(struct hid_device *hdev)
{
	bool need_to_allocate = false;
	struct hid_bpf_ops *e;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &hdev->bpf.prog_list, list) {
		if (e->hid_device_event) {
			need_to_allocate = true;
			break;
		}
	}
	rcu_read_unlock();

	/* only allocate BPF data if there are programs attached */
	if (!need_to_allocate)
		return 0;

	return hid_bpf_allocate_event_data(hdev);
}
EXPORT_SYMBOL_GPL(hid_bpf_connect_device);

void hid_bpf_disconnect_device(struct hid_device *hdev)
{
	kfree(hdev->bpf.device_data);
	hdev->bpf.device_data = NULL;
	hdev->bpf.allocated_data = 0;
}
EXPORT_SYMBOL_GPL(hid_bpf_disconnect_device);

void hid_bpf_destroy_device(struct hid_device *hdev)
{
	if (!hdev)
		return;

	/* mark the device as destroyed in bpf so we don't reattach it */
	hdev->bpf.destroyed = true;

	__hid_bpf_ops_destroy_device(hdev);

	synchronize_srcu(&hdev->bpf.srcu);
	cleanup_srcu_struct(&hdev->bpf.srcu);
}
EXPORT_SYMBOL_GPL(hid_bpf_destroy_device);

int hid_bpf_device_init(struct hid_device *hdev)
{
	INIT_LIST_HEAD(&hdev->bpf.prog_list);
	mutex_init(&hdev->bpf.prog_list_lock);
	return init_srcu_struct(&hdev->bpf.srcu);
}
EXPORT_SYMBOL_GPL(hid_bpf_device_init);

static int __init hid_bpf_init(void)
{
	int err;

	/* Note: if we exit with an error any time here, we would entirely break HID, which
	 * is probably not something we want. So we log an error and return success.
	 *
	 * This is not a big deal: nobody will be able to use the functionality.
	 */

	err = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &hid_bpf_kfunc_set);
	if (err) {
		pr_warn("error while setting HID BPF tracing kfuncs: %d", err);
		return 0;
	}

	err = register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &hid_bpf_syscall_kfunc_set);
	if (err) {
		pr_warn("error while setting HID BPF syscall kfuncs: %d", err);
		return 0;
	}

	return 0;
}

late_initcall(hid_bpf_init);
MODULE_AUTHOR("Benjamin Tissoires");
MODULE_LICENSE("GPL");
