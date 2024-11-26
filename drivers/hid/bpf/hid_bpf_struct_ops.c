// SPDX-License-Identifier: GPL-2.0-only

/*
 *  HID-BPF support for Linux
 *
 *  Copyright (c) 2024 Benjamin Tissoires
 */

#include <linux/bitops.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <linux/hid.h>
#include <linux/hid_bpf.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/workqueue.h>
#include "hid_bpf_dispatch.h"

static struct btf *hid_bpf_ops_btf;

static int hid_bpf_ops_init(struct btf *btf)
{
	hid_bpf_ops_btf = btf;
	return 0;
}

static bool hid_bpf_ops_is_valid_access(int off, int size,
					  enum bpf_access_type type,
					  const struct bpf_prog *prog,
					  struct bpf_insn_access_aux *info)
{
	return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

static int hid_bpf_ops_check_member(const struct btf_type *t,
				      const struct btf_member *member,
				      const struct bpf_prog *prog)
{
	u32 moff = __btf_member_bit_offset(t, member) / 8;

	switch (moff) {
	case offsetof(struct hid_bpf_ops, hid_rdesc_fixup):
	case offsetof(struct hid_bpf_ops, hid_hw_request):
	case offsetof(struct hid_bpf_ops, hid_hw_output_report):
		break;
	default:
		if (prog->sleepable)
			return -EINVAL;
	}

	return 0;
}

struct hid_bpf_offset_write_range {
	const char *struct_name;
	u32 struct_length;
	u32 start;
	u32 end;
};

static int hid_bpf_ops_btf_struct_access(struct bpf_verifier_log *log,
					   const struct bpf_reg_state *reg,
					   int off, int size)
{
#define WRITE_RANGE(_name, _field, _is_string)					\
	{									\
		.struct_name = #_name,						\
		.struct_length = sizeof(struct _name),				\
		.start = offsetof(struct _name, _field),			\
		.end = offsetofend(struct _name, _field) - !!(_is_string),	\
	}

	const struct hid_bpf_offset_write_range write_ranges[] = {
		WRITE_RANGE(hid_bpf_ctx, retval, false),
		WRITE_RANGE(hid_device, name, true),
		WRITE_RANGE(hid_device, uniq, true),
		WRITE_RANGE(hid_device, phys, true),
		WRITE_RANGE(hid_device, quirks, false),
	};
#undef WRITE_RANGE
	const struct btf_type *state = NULL;
	const struct btf_type *t;
	const char *cur = NULL;
	int i;

	t = btf_type_by_id(reg->btf, reg->btf_id);

	for (i = 0; i < ARRAY_SIZE(write_ranges); i++) {
		const struct hid_bpf_offset_write_range *write_range = &write_ranges[i];
		s32 type_id;

		/* we already found a writeable struct, but there is a
		 * new one, let's break the loop.
		 */
		if (t == state && write_range->struct_name != cur)
			break;

		/* new struct to look for */
		if (write_range->struct_name != cur) {
			type_id = btf_find_by_name_kind(reg->btf, write_range->struct_name,
							BTF_KIND_STRUCT);
			if (type_id < 0)
				return -EINVAL;

			state = btf_type_by_id(reg->btf, type_id);
		}

		/* this is not the struct we are looking for */
		if (t != state) {
			cur = write_range->struct_name;
			continue;
		}

		/* first time we see this struct, check for out of bounds */
		if (cur != write_range->struct_name &&
		    off + size > write_range->struct_length) {
			bpf_log(log, "write access for struct %s at off %d with size %d\n",
				write_range->struct_name, off, size);
			return -EACCES;
		}

		/* now check if we are in our boundaries */
		if (off >= write_range->start && off + size <= write_range->end)
			return NOT_INIT;

		cur = write_range->struct_name;
	}


	if (t != state)
		bpf_log(log, "write access to this struct is not supported\n");
	else
		bpf_log(log,
			"write access at off %d with size %d on read-only part of %s\n",
			off, size, cur);

	return -EACCES;
}

static const struct bpf_verifier_ops hid_bpf_verifier_ops = {
	.get_func_proto = bpf_base_func_proto,
	.is_valid_access = hid_bpf_ops_is_valid_access,
	.btf_struct_access = hid_bpf_ops_btf_struct_access,
};

static int hid_bpf_ops_init_member(const struct btf_type *t,
				 const struct btf_member *member,
				 void *kdata, const void *udata)
{
	const struct hid_bpf_ops *uhid_bpf_ops;
	struct hid_bpf_ops *khid_bpf_ops;
	u32 moff;

	uhid_bpf_ops = (const struct hid_bpf_ops *)udata;
	khid_bpf_ops = (struct hid_bpf_ops *)kdata;

	moff = __btf_member_bit_offset(t, member) / 8;

	switch (moff) {
	case offsetof(struct hid_bpf_ops, hid_id):
		/* For hid_id and flags fields, this function has to copy it
		 * and return 1 to indicate that the data has been handled by
		 * the struct_ops type, or the verifier will reject the map if
		 * the value of those fields is not zero.
		 */
		khid_bpf_ops->hid_id = uhid_bpf_ops->hid_id;
		return 1;
	case offsetof(struct hid_bpf_ops, flags):
		if (uhid_bpf_ops->flags & ~BPF_F_BEFORE)
			return -EINVAL;
		khid_bpf_ops->flags = uhid_bpf_ops->flags;
		return 1;
	}
	return 0;
}

static int hid_bpf_reg(void *kdata, struct bpf_link *link)
{
	struct hid_bpf_ops *ops = kdata;
	struct hid_device *hdev;
	int count, err = 0;

	/* prevent multiple attach of the same struct_ops */
	if (ops->hdev)
		return -EINVAL;

	hdev = hid_get_device(ops->hid_id);
	if (IS_ERR(hdev))
		return PTR_ERR(hdev);

	ops->hdev = hdev;

	mutex_lock(&hdev->bpf.prog_list_lock);

	count = list_count_nodes(&hdev->bpf.prog_list);
	if (count >= HID_BPF_MAX_PROGS_PER_DEV) {
		err = -E2BIG;
		goto out_unlock;
	}

	if (ops->hid_rdesc_fixup) {
		if (hdev->bpf.rdesc_ops) {
			err = -EINVAL;
			goto out_unlock;
		}

		hdev->bpf.rdesc_ops = ops;
	}

	if (ops->hid_device_event) {
		err = hid_bpf_allocate_event_data(hdev);
		if (err)
			goto out_unlock;
	}

	if (ops->flags & BPF_F_BEFORE)
		list_add_rcu(&ops->list, &hdev->bpf.prog_list);
	else
		list_add_tail_rcu(&ops->list, &hdev->bpf.prog_list);
	synchronize_srcu(&hdev->bpf.srcu);

out_unlock:
	mutex_unlock(&hdev->bpf.prog_list_lock);

	if (err) {
		if (hdev->bpf.rdesc_ops == ops)
			hdev->bpf.rdesc_ops = NULL;
		hid_put_device(hdev);
	} else if (ops->hid_rdesc_fixup) {
		hid_bpf_reconnect(hdev);
	}

	return err;
}

static void hid_bpf_unreg(void *kdata, struct bpf_link *link)
{
	struct hid_bpf_ops *ops = kdata;
	struct hid_device *hdev;
	bool reconnect = false;

	hdev = ops->hdev;

	/* check if __hid_bpf_ops_destroy_device() has been called */
	if (!hdev)
		return;

	mutex_lock(&hdev->bpf.prog_list_lock);

	list_del_rcu(&ops->list);
	synchronize_srcu(&hdev->bpf.srcu);
	ops->hdev = NULL;

	reconnect = hdev->bpf.rdesc_ops == ops;
	if (reconnect)
		hdev->bpf.rdesc_ops = NULL;

	mutex_unlock(&hdev->bpf.prog_list_lock);

	if (reconnect)
		hid_bpf_reconnect(hdev);

	hid_put_device(hdev);
}

static int __hid_bpf_device_event(struct hid_bpf_ctx *ctx, enum hid_report_type type, u64 source)
{
	return 0;
}

static int __hid_bpf_rdesc_fixup(struct hid_bpf_ctx *ctx)
{
	return 0;
}

static int __hid_bpf_hw_request(struct hid_bpf_ctx *ctx, unsigned char reportnum,
				enum hid_report_type rtype, enum hid_class_request reqtype,
				u64 source)
{
	return 0;
}

static int __hid_bpf_hw_output_report(struct hid_bpf_ctx *ctx, u64 source)
{
	return 0;
}

static struct hid_bpf_ops __bpf_hid_bpf_ops = {
	.hid_device_event = __hid_bpf_device_event,
	.hid_rdesc_fixup = __hid_bpf_rdesc_fixup,
	.hid_hw_request = __hid_bpf_hw_request,
	.hid_hw_output_report = __hid_bpf_hw_output_report,
};

static struct bpf_struct_ops bpf_hid_bpf_ops = {
	.verifier_ops = &hid_bpf_verifier_ops,
	.init = hid_bpf_ops_init,
	.check_member = hid_bpf_ops_check_member,
	.init_member = hid_bpf_ops_init_member,
	.reg = hid_bpf_reg,
	.unreg = hid_bpf_unreg,
	.name = "hid_bpf_ops",
	.cfi_stubs = &__bpf_hid_bpf_ops,
	.owner = THIS_MODULE,
};

void __hid_bpf_ops_destroy_device(struct hid_device *hdev)
{
	struct hid_bpf_ops *e;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &hdev->bpf.prog_list, list) {
		hid_put_device(hdev);
		e->hdev = NULL;
	}
	rcu_read_unlock();
}

static int __init hid_bpf_struct_ops_init(void)
{
	return register_bpf_struct_ops(&bpf_hid_bpf_ops, hid_bpf_ops);
}
late_initcall(hid_bpf_struct_ops_init);
