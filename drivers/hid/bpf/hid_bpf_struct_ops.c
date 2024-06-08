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
		break;
	default:
		if (prog->sleepable)
			return -EINVAL;
	}

	return 0;
}

static int hid_bpf_ops_btf_struct_access(struct bpf_verifier_log *log,
					   const struct bpf_reg_state *reg,
					   int off, int size)
{
	const struct btf_type *state;
	const struct btf_type *t;
	s32 type_id;

	type_id = btf_find_by_name_kind(reg->btf, "hid_bpf_ctx",
					BTF_KIND_STRUCT);
	if (type_id < 0)
		return -EINVAL;

	t = btf_type_by_id(reg->btf, reg->btf_id);
	state = btf_type_by_id(reg->btf, type_id);
	if (t != state) {
		bpf_log(log, "only access to hid_bpf_ctx is supported\n");
		return -EACCES;
	}

	/* out-of-bound access in hid_bpf_ctx */
	if (off + size > sizeof(struct hid_bpf_ctx)) {
		bpf_log(log, "write access at off %d with size %d\n", off, size);
		return -EACCES;
	}

	if (off < offsetof(struct hid_bpf_ctx, retval)) {
		bpf_log(log,
			"write access at off %d with size %d on read-only part of hid_bpf_ctx\n",
			off, size);
		return -EACCES;
	}

	return NOT_INIT;
}

static const struct bpf_verifier_ops hid_bpf_verifier_ops = {
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

static int hid_bpf_reg(void *kdata)
{
	struct hid_bpf_ops *ops = kdata;
	struct hid_device *hdev;
	int count, err = 0;

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

static void hid_bpf_unreg(void *kdata)
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

	reconnect = hdev->bpf.rdesc_ops == ops;
	if (reconnect)
		hdev->bpf.rdesc_ops = NULL;

	mutex_unlock(&hdev->bpf.prog_list_lock);

	if (reconnect)
		hid_bpf_reconnect(hdev);

	hid_put_device(hdev);
}

static int __hid_bpf_device_event(struct hid_bpf_ctx *ctx, enum hid_report_type type)
{
	return 0;
}

static int __hid_bpf_rdesc_fixup(struct hid_bpf_ctx *ctx)
{
	return 0;
}

static struct hid_bpf_ops __bpf_hid_bpf_ops = {
	.hid_device_event = __hid_bpf_device_event,
	.hid_rdesc_fixup = __hid_bpf_rdesc_fixup,
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
