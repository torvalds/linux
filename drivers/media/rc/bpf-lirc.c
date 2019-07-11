// SPDX-License-Identifier: GPL-2.0
// bpf-lirc.c - handles bpf
//
// Copyright (C) 2018 Sean Young <sean@mess.org>

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/bpf_lirc.h>
#include "rc-core-priv.h"

/*
 * BPF interface for raw IR
 */
const struct bpf_prog_ops lirc_mode2_prog_ops = {
};

BPF_CALL_1(bpf_rc_repeat, u32*, sample)
{
	struct ir_raw_event_ctrl *ctrl;

	ctrl = container_of(sample, struct ir_raw_event_ctrl, bpf_sample);

	rc_repeat(ctrl->dev);

	return 0;
}

static const struct bpf_func_proto rc_repeat_proto = {
	.func	   = bpf_rc_repeat,
	.gpl_only  = true, /* rc_repeat is EXPORT_SYMBOL_GPL */
	.ret_type  = RET_INTEGER,
	.arg1_type = ARG_PTR_TO_CTX,
};

/*
 * Currently rc-core does not support 64-bit scancodes, but there are many
 * known protocols with more than 32 bits. So, define the interface as u64
 * as a future-proof.
 */
BPF_CALL_4(bpf_rc_keydown, u32*, sample, u32, protocol, u64, scancode,
	   u32, toggle)
{
	struct ir_raw_event_ctrl *ctrl;

	ctrl = container_of(sample, struct ir_raw_event_ctrl, bpf_sample);

	rc_keydown(ctrl->dev, protocol, scancode, toggle != 0);

	return 0;
}

static const struct bpf_func_proto rc_keydown_proto = {
	.func	   = bpf_rc_keydown,
	.gpl_only  = true, /* rc_keydown is EXPORT_SYMBOL_GPL */
	.ret_type  = RET_INTEGER,
	.arg1_type = ARG_PTR_TO_CTX,
	.arg2_type = ARG_ANYTHING,
	.arg3_type = ARG_ANYTHING,
	.arg4_type = ARG_ANYTHING,
};

BPF_CALL_3(bpf_rc_pointer_rel, u32*, sample, s32, rel_x, s32, rel_y)
{
	struct ir_raw_event_ctrl *ctrl;

	ctrl = container_of(sample, struct ir_raw_event_ctrl, bpf_sample);

	input_report_rel(ctrl->dev->input_dev, REL_X, rel_x);
	input_report_rel(ctrl->dev->input_dev, REL_Y, rel_y);
	input_sync(ctrl->dev->input_dev);

	return 0;
}

static const struct bpf_func_proto rc_pointer_rel_proto = {
	.func	   = bpf_rc_pointer_rel,
	.gpl_only  = true,
	.ret_type  = RET_INTEGER,
	.arg1_type = ARG_PTR_TO_CTX,
	.arg2_type = ARG_ANYTHING,
	.arg3_type = ARG_ANYTHING,
};

static const struct bpf_func_proto *
lirc_mode2_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_rc_repeat:
		return &rc_repeat_proto;
	case BPF_FUNC_rc_keydown:
		return &rc_keydown_proto;
	case BPF_FUNC_rc_pointer_rel:
		return &rc_pointer_rel_proto;
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_get_prandom_u32:
		return &bpf_get_prandom_u32_proto;
	case BPF_FUNC_trace_printk:
		if (capable(CAP_SYS_ADMIN))
			return bpf_get_trace_printk_proto();
		/* fall through */
	default:
		return NULL;
	}
}

static bool lirc_mode2_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	/* We have one field of u32 */
	return type == BPF_READ && off == 0 && size == sizeof(u32);
}

const struct bpf_verifier_ops lirc_mode2_verifier_ops = {
	.get_func_proto  = lirc_mode2_func_proto,
	.is_valid_access = lirc_mode2_is_valid_access
};

#define BPF_MAX_PROGS 64

static int lirc_bpf_attach(struct rc_dev *rcdev, struct bpf_prog *prog)
{
	struct bpf_prog_array __rcu *old_array;
	struct bpf_prog_array *new_array;
	struct ir_raw_event_ctrl *raw;
	int ret;

	if (rcdev->driver_type != RC_DRIVER_IR_RAW)
		return -EINVAL;

	ret = mutex_lock_interruptible(&ir_raw_handler_lock);
	if (ret)
		return ret;

	raw = rcdev->raw;
	if (!raw) {
		ret = -ENODEV;
		goto unlock;
	}

	if (raw->progs && bpf_prog_array_length(raw->progs) >= BPF_MAX_PROGS) {
		ret = -E2BIG;
		goto unlock;
	}

	old_array = raw->progs;
	ret = bpf_prog_array_copy(old_array, NULL, prog, &new_array);
	if (ret < 0)
		goto unlock;

	rcu_assign_pointer(raw->progs, new_array);
	bpf_prog_array_free(old_array);

unlock:
	mutex_unlock(&ir_raw_handler_lock);
	return ret;
}

static int lirc_bpf_detach(struct rc_dev *rcdev, struct bpf_prog *prog)
{
	struct bpf_prog_array __rcu *old_array;
	struct bpf_prog_array *new_array;
	struct ir_raw_event_ctrl *raw;
	int ret;

	if (rcdev->driver_type != RC_DRIVER_IR_RAW)
		return -EINVAL;

	ret = mutex_lock_interruptible(&ir_raw_handler_lock);
	if (ret)
		return ret;

	raw = rcdev->raw;
	if (!raw) {
		ret = -ENODEV;
		goto unlock;
	}

	old_array = raw->progs;
	ret = bpf_prog_array_copy(old_array, prog, NULL, &new_array);
	/*
	 * Do not use bpf_prog_array_delete_safe() as we would end up
	 * with a dummy entry in the array, and the we would free the
	 * dummy in lirc_bpf_free()
	 */
	if (ret)
		goto unlock;

	rcu_assign_pointer(raw->progs, new_array);
	bpf_prog_array_free(old_array);
	bpf_prog_put(prog);
unlock:
	mutex_unlock(&ir_raw_handler_lock);
	return ret;
}

void lirc_bpf_run(struct rc_dev *rcdev, u32 sample)
{
	struct ir_raw_event_ctrl *raw = rcdev->raw;

	raw->bpf_sample = sample;

	if (raw->progs)
		BPF_PROG_RUN_ARRAY(raw->progs, &raw->bpf_sample, BPF_PROG_RUN);
}

/*
 * This should be called once the rc thread has been stopped, so there can be
 * no concurrent bpf execution.
 */
void lirc_bpf_free(struct rc_dev *rcdev)
{
	struct bpf_prog_array_item *item;

	if (!rcdev->raw->progs)
		return;

	item = rcu_dereference(rcdev->raw->progs)->items;
	while (item->prog) {
		bpf_prog_put(item->prog);
		item++;
	}

	bpf_prog_array_free(rcdev->raw->progs);
}

int lirc_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct rc_dev *rcdev;
	int ret;

	if (attr->attach_flags)
		return -EINVAL;

	rcdev = rc_dev_get_from_fd(attr->target_fd);
	if (IS_ERR(rcdev))
		return PTR_ERR(rcdev);

	ret = lirc_bpf_attach(rcdev, prog);

	put_device(&rcdev->dev);

	return ret;
}

int lirc_prog_detach(const union bpf_attr *attr)
{
	struct bpf_prog *prog;
	struct rc_dev *rcdev;
	int ret;

	if (attr->attach_flags)
		return -EINVAL;

	prog = bpf_prog_get_type(attr->attach_bpf_fd,
				 BPF_PROG_TYPE_LIRC_MODE2);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	rcdev = rc_dev_get_from_fd(attr->target_fd);
	if (IS_ERR(rcdev)) {
		bpf_prog_put(prog);
		return PTR_ERR(rcdev);
	}

	ret = lirc_bpf_detach(rcdev, prog);

	bpf_prog_put(prog);
	put_device(&rcdev->dev);

	return ret;
}

int lirc_prog_query(const union bpf_attr *attr, union bpf_attr __user *uattr)
{
	__u32 __user *prog_ids = u64_to_user_ptr(attr->query.prog_ids);
	struct bpf_prog_array __rcu *progs;
	struct rc_dev *rcdev;
	u32 cnt, flags = 0;
	int ret;

	if (attr->query.query_flags)
		return -EINVAL;

	rcdev = rc_dev_get_from_fd(attr->query.target_fd);
	if (IS_ERR(rcdev))
		return PTR_ERR(rcdev);

	if (rcdev->driver_type != RC_DRIVER_IR_RAW) {
		ret = -EINVAL;
		goto put;
	}

	ret = mutex_lock_interruptible(&ir_raw_handler_lock);
	if (ret)
		goto put;

	progs = rcdev->raw->progs;
	cnt = progs ? bpf_prog_array_length(progs) : 0;

	if (copy_to_user(&uattr->query.prog_cnt, &cnt, sizeof(cnt))) {
		ret = -EFAULT;
		goto unlock;
	}

	if (copy_to_user(&uattr->query.attach_flags, &flags, sizeof(flags))) {
		ret = -EFAULT;
		goto unlock;
	}

	if (attr->query.prog_cnt != 0 && prog_ids && cnt)
		ret = bpf_prog_array_copy_to_user(progs, prog_ids, cnt);

unlock:
	mutex_unlock(&ir_raw_handler_lock);
put:
	put_device(&rcdev->dev);

	return ret;
}
