// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Microsoft Corporation.
 *
 * The main part of the mshv_root module, providing APIs to create
 * and manage guest partitions.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/cpuhotplug.h>
#include <linux/random.h>
#include <asm/mshyperv.h>
#include <linux/hyperv.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/page-flags.h>
#include <linux/crash_dump.h>
#include <linux/panic_notifier.h>
#include <linux/vmalloc.h>

#include "mshv_eventfd.h"
#include "mshv.h"
#include "mshv_root.h"

MODULE_AUTHOR("Microsoft");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microsoft Hyper-V root partition VMM interface /dev/mshv");

/* TODO move this to mshyperv.h when needed outside driver */
static inline bool hv_parent_partition(void)
{
	return hv_root_partition();
}

/* TODO move this to another file when debugfs code is added */
enum hv_stats_vp_counters {			/* HV_THREAD_COUNTER */
#if defined(CONFIG_X86)
	VpRootDispatchThreadBlocked			= 201,
#elif defined(CONFIG_ARM64)
	VpRootDispatchThreadBlocked			= 94,
#endif
	VpStatsMaxCounter
};

struct hv_stats_page {
	union {
		u64 vp_cntrs[VpStatsMaxCounter];		/* VP counters */
		u8 data[HV_HYP_PAGE_SIZE];
	};
} __packed;

struct mshv_root mshv_root;

enum hv_scheduler_type hv_scheduler_type;

/* Once we implement the fast extended hypercall ABI they can go away. */
static void * __percpu *root_scheduler_input;
static void * __percpu *root_scheduler_output;

static long mshv_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
static int mshv_dev_open(struct inode *inode, struct file *filp);
static int mshv_dev_release(struct inode *inode, struct file *filp);
static int mshv_vp_release(struct inode *inode, struct file *filp);
static long mshv_vp_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
static int mshv_partition_release(struct inode *inode, struct file *filp);
static long mshv_partition_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
static int mshv_vp_mmap(struct file *file, struct vm_area_struct *vma);
static vm_fault_t mshv_vp_fault(struct vm_fault *vmf);
static int mshv_init_async_handler(struct mshv_partition *partition);
static void mshv_async_hvcall_handler(void *data, u64 *status);

static const union hv_input_vtl input_vtl_zero;
static const union hv_input_vtl input_vtl_normal = {
	.target_vtl = HV_NORMAL_VTL,
	.use_target_vtl = 1,
};

static const struct vm_operations_struct mshv_vp_vm_ops = {
	.fault = mshv_vp_fault,
};

static const struct file_operations mshv_vp_fops = {
	.owner = THIS_MODULE,
	.release = mshv_vp_release,
	.unlocked_ioctl = mshv_vp_ioctl,
	.llseek = noop_llseek,
	.mmap = mshv_vp_mmap,
};

static const struct file_operations mshv_partition_fops = {
	.owner = THIS_MODULE,
	.release = mshv_partition_release,
	.unlocked_ioctl = mshv_partition_ioctl,
	.llseek = noop_llseek,
};

static const struct file_operations mshv_dev_fops = {
	.owner = THIS_MODULE,
	.open = mshv_dev_open,
	.release = mshv_dev_release,
	.unlocked_ioctl = mshv_dev_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice mshv_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mshv",
	.fops = &mshv_dev_fops,
	.mode = 0600,
};

/*
 * Only allow hypercalls that have a u64 partition id as the first member of
 * the input structure.
 * These are sorted by value.
 */
static u16 mshv_passthru_hvcalls[] = {
	HVCALL_GET_PARTITION_PROPERTY,
	HVCALL_SET_PARTITION_PROPERTY,
	HVCALL_INSTALL_INTERCEPT,
	HVCALL_GET_VP_REGISTERS,
	HVCALL_SET_VP_REGISTERS,
	HVCALL_TRANSLATE_VIRTUAL_ADDRESS,
	HVCALL_CLEAR_VIRTUAL_INTERRUPT,
	HVCALL_REGISTER_INTERCEPT_RESULT,
	HVCALL_ASSERT_VIRTUAL_INTERRUPT,
	HVCALL_GET_GPA_PAGES_ACCESS_STATES,
	HVCALL_SIGNAL_EVENT_DIRECT,
	HVCALL_POST_MESSAGE_DIRECT,
	HVCALL_GET_VP_CPUID_VALUES,
};

static bool mshv_hvcall_is_async(u16 code)
{
	switch (code) {
	case HVCALL_SET_PARTITION_PROPERTY:
		return true;
	default:
		break;
	}
	return false;
}

static int mshv_ioctl_passthru_hvcall(struct mshv_partition *partition,
				      bool partition_locked,
				      void __user *user_args)
{
	u64 status;
	int ret = 0, i;
	bool is_async;
	struct mshv_root_hvcall args;
	struct page *page;
	unsigned int pages_order;
	void *input_pg = NULL;
	void *output_pg = NULL;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.status || !args.in_ptr || args.in_sz < sizeof(u64) ||
	    mshv_field_nonzero(args, rsvd) || args.in_sz > HV_HYP_PAGE_SIZE)
		return -EINVAL;

	if (args.out_ptr && (!args.out_sz || args.out_sz > HV_HYP_PAGE_SIZE))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(mshv_passthru_hvcalls); ++i)
		if (args.code == mshv_passthru_hvcalls[i])
			break;

	if (i >= ARRAY_SIZE(mshv_passthru_hvcalls))
		return -EINVAL;

	is_async = mshv_hvcall_is_async(args.code);
	if (is_async) {
		/* async hypercalls can only be called from partition fd */
		if (!partition_locked)
			return -EINVAL;
		ret = mshv_init_async_handler(partition);
		if (ret)
			return ret;
	}

	pages_order = args.out_ptr ? 1 : 0;
	page = alloc_pages(GFP_KERNEL, pages_order);
	if (!page)
		return -ENOMEM;
	input_pg = page_address(page);

	if (args.out_ptr)
		output_pg = (char *)input_pg + PAGE_SIZE;
	else
		output_pg = NULL;

	if (copy_from_user(input_pg, (void __user *)args.in_ptr,
			   args.in_sz)) {
		ret = -EFAULT;
		goto free_pages_out;
	}

	/*
	 * NOTE: This only works because all the allowed hypercalls' input
	 * structs begin with a u64 partition_id field.
	 */
	*(u64 *)input_pg = partition->pt_id;

	if (args.reps)
		status = hv_do_rep_hypercall(args.code, args.reps, 0,
					     input_pg, output_pg);
	else
		status = hv_do_hypercall(args.code, input_pg, output_pg);

	if (hv_result(status) == HV_STATUS_CALL_PENDING) {
		if (is_async) {
			mshv_async_hvcall_handler(partition, &status);
		} else { /* Paranoia check. This shouldn't happen! */
			ret = -EBADFD;
			goto free_pages_out;
		}
	}

	if (hv_result(status) == HV_STATUS_INSUFFICIENT_MEMORY) {
		ret = hv_call_deposit_pages(NUMA_NO_NODE, partition->pt_id, 1);
		if (!ret)
			ret = -EAGAIN;
	} else if (!hv_result_success(status)) {
		ret = hv_result_to_errno(status);
	}

	/*
	 * Always return the status and output data regardless of result.
	 * The VMM may need it to determine how to proceed. E.g. the status may
	 * contain the number of reps completed if a rep hypercall partially
	 * succeeded.
	 */
	args.status = hv_result(status);
	args.reps = args.reps ? hv_repcomp(status) : 0;
	if (copy_to_user(user_args, &args, sizeof(args)))
		ret = -EFAULT;

	if (output_pg &&
	    copy_to_user((void __user *)args.out_ptr, output_pg, args.out_sz))
		ret = -EFAULT;

free_pages_out:
	free_pages((unsigned long)input_pg, pages_order);

	return ret;
}

static inline bool is_ghcb_mapping_available(void)
{
#if IS_ENABLED(CONFIG_X86_64)
	return ms_hyperv.ext_features & HV_VP_GHCB_ROOT_MAPPING_AVAILABLE;
#else
	return 0;
#endif
}

static int mshv_get_vp_registers(u32 vp_index, u64 partition_id, u16 count,
				 struct hv_register_assoc *registers)
{
	return hv_call_get_vp_registers(vp_index, partition_id,
					count, input_vtl_zero, registers);
}

static int mshv_set_vp_registers(u32 vp_index, u64 partition_id, u16 count,
				 struct hv_register_assoc *registers)
{
	return hv_call_set_vp_registers(vp_index, partition_id,
					count, input_vtl_zero, registers);
}

/*
 * Explicit guest vCPU suspend is asynchronous by nature (as it is requested by
 * dom0 vCPU for guest vCPU) and thus it can race with "intercept" suspend,
 * done by the hypervisor.
 * "Intercept" suspend leads to asynchronous message delivery to dom0 which
 * should be awaited to keep the VP loop consistent (i.e. no message pending
 * upon VP resume).
 * VP intercept suspend can't be done when the VP is explicitly suspended
 * already, and thus can be only two possible race scenarios:
 *   1. implicit suspend bit set -> explicit suspend bit set -> message sent
 *   2. implicit suspend bit set -> message sent -> explicit suspend bit set
 * Checking for implicit suspend bit set after explicit suspend request has
 * succeeded in either case allows us to reliably identify, if there is a
 * message to receive and deliver to VMM.
 */
static int
mshv_suspend_vp(const struct mshv_vp *vp, bool *message_in_flight)
{
	struct hv_register_assoc explicit_suspend = {
		.name = HV_REGISTER_EXPLICIT_SUSPEND
	};
	struct hv_register_assoc intercept_suspend = {
		.name = HV_REGISTER_INTERCEPT_SUSPEND
	};
	union hv_explicit_suspend_register *es =
		&explicit_suspend.value.explicit_suspend;
	union hv_intercept_suspend_register *is =
		&intercept_suspend.value.intercept_suspend;
	int ret;

	es->suspended = 1;

	ret = mshv_set_vp_registers(vp->vp_index, vp->vp_partition->pt_id,
				    1, &explicit_suspend);
	if (ret) {
		vp_err(vp, "Failed to explicitly suspend vCPU\n");
		return ret;
	}

	ret = mshv_get_vp_registers(vp->vp_index, vp->vp_partition->pt_id,
				    1, &intercept_suspend);
	if (ret) {
		vp_err(vp, "Failed to get intercept suspend state\n");
		return ret;
	}

	*message_in_flight = is->suspended;

	return 0;
}

/*
 * This function is used when VPs are scheduled by the hypervisor's
 * scheduler.
 *
 * Caller has to make sure the registers contain cleared
 * HV_REGISTER_INTERCEPT_SUSPEND and HV_REGISTER_EXPLICIT_SUSPEND registers
 * exactly in this order (the hypervisor clears them sequentially) to avoid
 * potential invalid clearing a newly arrived HV_REGISTER_INTERCEPT_SUSPEND
 * after VP is released from HV_REGISTER_EXPLICIT_SUSPEND in case of the
 * opposite order.
 */
static long mshv_run_vp_with_hyp_scheduler(struct mshv_vp *vp)
{
	long ret;
	struct hv_register_assoc suspend_regs[2] = {
			{ .name = HV_REGISTER_INTERCEPT_SUSPEND },
			{ .name = HV_REGISTER_EXPLICIT_SUSPEND }
	};
	size_t count = ARRAY_SIZE(suspend_regs);

	/* Resume VP execution */
	ret = mshv_set_vp_registers(vp->vp_index, vp->vp_partition->pt_id,
				    count, suspend_regs);
	if (ret) {
		vp_err(vp, "Failed to resume vp execution. %lx\n", ret);
		return ret;
	}

	ret = wait_event_interruptible(vp->run.vp_suspend_queue,
				       vp->run.kicked_by_hv == 1);
	if (ret) {
		bool message_in_flight;

		/*
		 * Otherwise the waiting was interrupted by a signal: suspend
		 * the vCPU explicitly and copy message in flight (if any).
		 */
		ret = mshv_suspend_vp(vp, &message_in_flight);
		if (ret)
			return ret;

		/* Return if no message in flight */
		if (!message_in_flight)
			return -EINTR;

		/* Wait for the message in flight. */
		wait_event(vp->run.vp_suspend_queue, vp->run.kicked_by_hv == 1);
	}

	/*
	 * Reset the flag to make the wait_event call above work
	 * next time.
	 */
	vp->run.kicked_by_hv = 0;

	return 0;
}

static int
mshv_vp_dispatch(struct mshv_vp *vp, u32 flags,
		 struct hv_output_dispatch_vp *res)
{
	struct hv_input_dispatch_vp *input;
	struct hv_output_dispatch_vp *output;
	u64 status;

	preempt_disable();
	input = *this_cpu_ptr(root_scheduler_input);
	output = *this_cpu_ptr(root_scheduler_output);

	memset(input, 0, sizeof(*input));
	memset(output, 0, sizeof(*output));

	input->partition_id = vp->vp_partition->pt_id;
	input->vp_index = vp->vp_index;
	input->time_slice = 0; /* Run forever until something happens */
	input->spec_ctrl = 0; /* TODO: set sensible flags */
	input->flags = flags;

	vp->run.flags.root_sched_dispatched = 1;
	status = hv_do_hypercall(HVCALL_DISPATCH_VP, input, output);
	vp->run.flags.root_sched_dispatched = 0;

	*res = *output;
	preempt_enable();

	if (!hv_result_success(status))
		vp_err(vp, "%s: status %s\n", __func__,
		       hv_result_to_string(status));

	return hv_result_to_errno(status);
}

static int
mshv_vp_clear_explicit_suspend(struct mshv_vp *vp)
{
	struct hv_register_assoc explicit_suspend = {
		.name = HV_REGISTER_EXPLICIT_SUSPEND,
		.value.explicit_suspend.suspended = 0,
	};
	int ret;

	ret = mshv_set_vp_registers(vp->vp_index, vp->vp_partition->pt_id,
				    1, &explicit_suspend);

	if (ret)
		vp_err(vp, "Failed to unsuspend\n");

	return ret;
}

#if IS_ENABLED(CONFIG_X86_64)
static u64 mshv_vp_interrupt_pending(struct mshv_vp *vp)
{
	if (!vp->vp_register_page)
		return 0;
	return vp->vp_register_page->interrupt_vectors.as_uint64;
}
#else
static u64 mshv_vp_interrupt_pending(struct mshv_vp *vp)
{
	return 0;
}
#endif

static bool mshv_vp_dispatch_thread_blocked(struct mshv_vp *vp)
{
	struct hv_stats_page **stats = vp->vp_stats_pages;
	u64 *self_vp_cntrs = stats[HV_STATS_AREA_SELF]->vp_cntrs;
	u64 *parent_vp_cntrs = stats[HV_STATS_AREA_PARENT]->vp_cntrs;

	if (self_vp_cntrs[VpRootDispatchThreadBlocked])
		return self_vp_cntrs[VpRootDispatchThreadBlocked];
	return parent_vp_cntrs[VpRootDispatchThreadBlocked];
}

static int
mshv_vp_wait_for_hv_kick(struct mshv_vp *vp)
{
	int ret;

	ret = wait_event_interruptible(vp->run.vp_suspend_queue,
				       (vp->run.kicked_by_hv == 1 &&
					!mshv_vp_dispatch_thread_blocked(vp)) ||
				       mshv_vp_interrupt_pending(vp));
	if (ret)
		return -EINTR;

	vp->run.flags.root_sched_blocked = 0;
	vp->run.kicked_by_hv = 0;

	return 0;
}

static int mshv_pre_guest_mode_work(struct mshv_vp *vp)
{
	const ulong work_flags = _TIF_NOTIFY_SIGNAL | _TIF_SIGPENDING |
				 _TIF_NEED_RESCHED  | _TIF_NOTIFY_RESUME;
	ulong th_flags;

	th_flags = read_thread_flags();
	while (th_flags & work_flags) {
		int ret;

		/* nb: following will call schedule */
		ret = mshv_do_pre_guest_mode_work(th_flags);

		if (ret)
			return ret;

		th_flags = read_thread_flags();
	}

	return 0;
}

/* Must be called with interrupts enabled */
static long mshv_run_vp_with_root_scheduler(struct mshv_vp *vp)
{
	long ret;

	if (vp->run.flags.root_sched_blocked) {
		/*
		 * Dispatch state of this VP is blocked. Need to wait
		 * for the hypervisor to clear the blocked state before
		 * dispatching it.
		 */
		ret = mshv_vp_wait_for_hv_kick(vp);
		if (ret)
			return ret;
	}

	do {
		u32 flags = 0;
		struct hv_output_dispatch_vp output;

		ret = mshv_pre_guest_mode_work(vp);
		if (ret)
			break;

		if (vp->run.flags.intercept_suspend)
			flags |= HV_DISPATCH_VP_FLAG_CLEAR_INTERCEPT_SUSPEND;

		if (mshv_vp_interrupt_pending(vp))
			flags |= HV_DISPATCH_VP_FLAG_SCAN_INTERRUPT_INJECTION;

		ret = mshv_vp_dispatch(vp, flags, &output);
		if (ret)
			break;

		vp->run.flags.intercept_suspend = 0;

		if (output.dispatch_state == HV_VP_DISPATCH_STATE_BLOCKED) {
			if (output.dispatch_event ==
						HV_VP_DISPATCH_EVENT_SUSPEND) {
				/*
				 * TODO: remove the warning once VP canceling
				 *	 is supported
				 */
				WARN_ONCE(atomic64_read(&vp->run.vp_signaled_count),
					  "%s: vp#%d: unexpected explicit suspend\n",
					  __func__, vp->vp_index);
				/*
				 * Need to clear explicit suspend before
				 * dispatching.
				 * Explicit suspend is either:
				 * - set right after the first VP dispatch or
				 * - set explicitly via hypercall
				 * Since the latter case is not yet supported,
				 * simply clear it here.
				 */
				ret = mshv_vp_clear_explicit_suspend(vp);
				if (ret)
					break;

				ret = mshv_vp_wait_for_hv_kick(vp);
				if (ret)
					break;
			} else {
				vp->run.flags.root_sched_blocked = 1;
				ret = mshv_vp_wait_for_hv_kick(vp);
				if (ret)
					break;
			}
		} else {
			/* HV_VP_DISPATCH_STATE_READY */
			if (output.dispatch_event ==
						HV_VP_DISPATCH_EVENT_INTERCEPT)
				vp->run.flags.intercept_suspend = 1;
		}
	} while (!vp->run.flags.intercept_suspend);

	return ret;
}

static_assert(sizeof(struct hv_message) <= MSHV_RUN_VP_BUF_SZ,
	      "sizeof(struct hv_message) must not exceed MSHV_RUN_VP_BUF_SZ");

static long mshv_vp_ioctl_run_vp(struct mshv_vp *vp, void __user *ret_msg)
{
	long rc;

	if (hv_scheduler_type == HV_SCHEDULER_TYPE_ROOT)
		rc = mshv_run_vp_with_root_scheduler(vp);
	else
		rc = mshv_run_vp_with_hyp_scheduler(vp);

	if (rc)
		return rc;

	if (copy_to_user(ret_msg, vp->vp_intercept_msg_page,
			 sizeof(struct hv_message)))
		rc = -EFAULT;

	return rc;
}

static int
mshv_vp_ioctl_get_set_state_pfn(struct mshv_vp *vp,
				struct hv_vp_state_data state_data,
				unsigned long user_pfn, size_t page_count,
				bool is_set)
{
	int completed, ret = 0;
	unsigned long check;
	struct page **pages;

	if (page_count > INT_MAX)
		return -EINVAL;
	/*
	 * Check the arithmetic for wraparound/overflow.
	 * The last page address in the buffer is:
	 * (user_pfn + (page_count - 1)) * PAGE_SIZE
	 */
	if (check_add_overflow(user_pfn, (page_count - 1), &check))
		return -EOVERFLOW;
	if (check_mul_overflow(check, PAGE_SIZE, &check))
		return -EOVERFLOW;

	/* Pin user pages so hypervisor can copy directly to them */
	pages = kcalloc(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (completed = 0; completed < page_count; completed += ret) {
		unsigned long user_addr = (user_pfn + completed) * PAGE_SIZE;
		int remaining = page_count - completed;

		ret = pin_user_pages_fast(user_addr, remaining, FOLL_WRITE,
					  &pages[completed]);
		if (ret < 0) {
			vp_err(vp, "%s: Failed to pin user pages error %i\n",
			       __func__, ret);
			goto unpin_pages;
		}
	}

	if (is_set)
		ret = hv_call_set_vp_state(vp->vp_index,
					   vp->vp_partition->pt_id,
					   state_data, page_count, pages,
					   0, NULL);
	else
		ret = hv_call_get_vp_state(vp->vp_index,
					   vp->vp_partition->pt_id,
					   state_data, page_count, pages,
					   NULL);

unpin_pages:
	unpin_user_pages(pages, completed);
	kfree(pages);
	return ret;
}

static long
mshv_vp_ioctl_get_set_state(struct mshv_vp *vp,
			    struct mshv_get_set_vp_state __user *user_args,
			    bool is_set)
{
	struct mshv_get_set_vp_state args;
	long ret = 0;
	union hv_output_get_vp_state vp_state;
	u32 data_sz;
	struct hv_vp_state_data state_data = {};

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.type >= MSHV_VP_STATE_COUNT || mshv_field_nonzero(args, rsvd) ||
	    !args.buf_sz || !PAGE_ALIGNED(args.buf_sz) ||
	    !PAGE_ALIGNED(args.buf_ptr))
		return -EINVAL;

	if (!access_ok((void __user *)args.buf_ptr, args.buf_sz))
		return -EFAULT;

	switch (args.type) {
	case MSHV_VP_STATE_LAPIC:
		state_data.type = HV_GET_SET_VP_STATE_LAPIC_STATE;
		data_sz = HV_HYP_PAGE_SIZE;
		break;
	case MSHV_VP_STATE_XSAVE:
	{
		u64 data_sz_64;

		ret = hv_call_get_partition_property(vp->vp_partition->pt_id,
						     HV_PARTITION_PROPERTY_XSAVE_STATES,
						     &state_data.xsave.states.as_uint64);
		if (ret)
			return ret;

		ret = hv_call_get_partition_property(vp->vp_partition->pt_id,
						     HV_PARTITION_PROPERTY_MAX_XSAVE_DATA_SIZE,
						     &data_sz_64);
		if (ret)
			return ret;

		data_sz = (u32)data_sz_64;
		state_data.xsave.flags = 0;
		/* Always request legacy states */
		state_data.xsave.states.legacy_x87 = 1;
		state_data.xsave.states.legacy_sse = 1;
		state_data.type = HV_GET_SET_VP_STATE_XSAVE;
		break;
	}
	case MSHV_VP_STATE_SIMP:
		state_data.type = HV_GET_SET_VP_STATE_SIM_PAGE;
		data_sz = HV_HYP_PAGE_SIZE;
		break;
	case MSHV_VP_STATE_SIEFP:
		state_data.type = HV_GET_SET_VP_STATE_SIEF_PAGE;
		data_sz = HV_HYP_PAGE_SIZE;
		break;
	case MSHV_VP_STATE_SYNTHETIC_TIMERS:
		state_data.type = HV_GET_SET_VP_STATE_SYNTHETIC_TIMERS;
		data_sz = sizeof(vp_state.synthetic_timers_state);
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user(&user_args->buf_sz, &data_sz, sizeof(user_args->buf_sz)))
		return -EFAULT;

	if (data_sz > args.buf_sz)
		return -EINVAL;

	/* If the data is transmitted via pfns, delegate to helper */
	if (state_data.type & HV_GET_SET_VP_STATE_TYPE_PFN) {
		unsigned long user_pfn = PFN_DOWN(args.buf_ptr);
		size_t page_count = PFN_DOWN(args.buf_sz);

		return mshv_vp_ioctl_get_set_state_pfn(vp, state_data, user_pfn,
						       page_count, is_set);
	}

	/* Paranoia check - this shouldn't happen! */
	if (data_sz > sizeof(vp_state)) {
		vp_err(vp, "Invalid vp state data size!\n");
		return -EINVAL;
	}

	if (is_set) {
		if (copy_from_user(&vp_state, (__user void *)args.buf_ptr, data_sz))
			return -EFAULT;

		return hv_call_set_vp_state(vp->vp_index,
					    vp->vp_partition->pt_id,
					    state_data, 0, NULL,
					    sizeof(vp_state), (u8 *)&vp_state);
	}

	ret = hv_call_get_vp_state(vp->vp_index, vp->vp_partition->pt_id,
				   state_data, 0, NULL, &vp_state);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)args.buf_ptr, &vp_state, data_sz))
		return -EFAULT;

	return 0;
}

static long
mshv_vp_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct mshv_vp *vp = filp->private_data;
	long r = -ENOTTY;

	if (mutex_lock_killable(&vp->vp_mutex))
		return -EINTR;

	switch (ioctl) {
	case MSHV_RUN_VP:
		r = mshv_vp_ioctl_run_vp(vp, (void __user *)arg);
		break;
	case MSHV_GET_VP_STATE:
		r = mshv_vp_ioctl_get_set_state(vp, (void __user *)arg, false);
		break;
	case MSHV_SET_VP_STATE:
		r = mshv_vp_ioctl_get_set_state(vp, (void __user *)arg, true);
		break;
	case MSHV_ROOT_HVCALL:
		r = mshv_ioctl_passthru_hvcall(vp->vp_partition, false,
					       (void __user *)arg);
		break;
	default:
		vp_warn(vp, "Invalid ioctl: %#x\n", ioctl);
		break;
	}
	mutex_unlock(&vp->vp_mutex);

	return r;
}

static vm_fault_t mshv_vp_fault(struct vm_fault *vmf)
{
	struct mshv_vp *vp = vmf->vma->vm_file->private_data;

	switch (vmf->vma->vm_pgoff) {
	case MSHV_VP_MMAP_OFFSET_REGISTERS:
		vmf->page = virt_to_page(vp->vp_register_page);
		break;
	case MSHV_VP_MMAP_OFFSET_INTERCEPT_MESSAGE:
		vmf->page = virt_to_page(vp->vp_intercept_msg_page);
		break;
	case MSHV_VP_MMAP_OFFSET_GHCB:
		vmf->page = virt_to_page(vp->vp_ghcb_page);
		break;
	default:
		return VM_FAULT_SIGBUS;
	}

	get_page(vmf->page);

	return 0;
}

static int mshv_vp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mshv_vp *vp = file->private_data;

	switch (vma->vm_pgoff) {
	case MSHV_VP_MMAP_OFFSET_REGISTERS:
		if (!vp->vp_register_page)
			return -ENODEV;
		break;
	case MSHV_VP_MMAP_OFFSET_INTERCEPT_MESSAGE:
		if (!vp->vp_intercept_msg_page)
			return -ENODEV;
		break;
	case MSHV_VP_MMAP_OFFSET_GHCB:
		if (!vp->vp_ghcb_page)
			return -ENODEV;
		break;
	default:
		return -EINVAL;
	}

	vma->vm_ops = &mshv_vp_vm_ops;
	return 0;
}

static int
mshv_vp_release(struct inode *inode, struct file *filp)
{
	struct mshv_vp *vp = filp->private_data;

	/* Rest of VP cleanup happens in destroy_partition() */
	mshv_partition_put(vp->vp_partition);
	return 0;
}

static void mshv_vp_stats_unmap(u64 partition_id, u32 vp_index)
{
	union hv_stats_object_identity identity = {
		.vp.partition_id = partition_id,
		.vp.vp_index = vp_index,
	};

	identity.vp.stats_area_type = HV_STATS_AREA_SELF;
	hv_call_unmap_stat_page(HV_STATS_OBJECT_VP, &identity);

	identity.vp.stats_area_type = HV_STATS_AREA_PARENT;
	hv_call_unmap_stat_page(HV_STATS_OBJECT_VP, &identity);
}

static int mshv_vp_stats_map(u64 partition_id, u32 vp_index,
			     void *stats_pages[])
{
	union hv_stats_object_identity identity = {
		.vp.partition_id = partition_id,
		.vp.vp_index = vp_index,
	};
	int err;

	identity.vp.stats_area_type = HV_STATS_AREA_SELF;
	err = hv_call_map_stat_page(HV_STATS_OBJECT_VP, &identity,
				    &stats_pages[HV_STATS_AREA_SELF]);
	if (err)
		return err;

	identity.vp.stats_area_type = HV_STATS_AREA_PARENT;
	err = hv_call_map_stat_page(HV_STATS_OBJECT_VP, &identity,
				    &stats_pages[HV_STATS_AREA_PARENT]);
	if (err)
		goto unmap_self;

	return 0;

unmap_self:
	identity.vp.stats_area_type = HV_STATS_AREA_SELF;
	hv_call_unmap_stat_page(HV_STATS_OBJECT_VP, &identity);
	return err;
}

static long
mshv_partition_ioctl_create_vp(struct mshv_partition *partition,
			       void __user *arg)
{
	struct mshv_create_vp args;
	struct mshv_vp *vp;
	struct page *intercept_message_page, *register_page, *ghcb_page;
	void *stats_pages[2];
	long ret;

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	if (args.vp_index >= MSHV_MAX_VPS)
		return -EINVAL;

	if (partition->pt_vp_array[args.vp_index])
		return -EEXIST;

	ret = hv_call_create_vp(NUMA_NO_NODE, partition->pt_id, args.vp_index,
				0 /* Only valid for root partition VPs */);
	if (ret)
		return ret;

	ret = hv_call_map_vp_state_page(partition->pt_id, args.vp_index,
					HV_VP_STATE_PAGE_INTERCEPT_MESSAGE,
					input_vtl_zero,
					&intercept_message_page);
	if (ret)
		goto destroy_vp;

	if (!mshv_partition_encrypted(partition)) {
		ret = hv_call_map_vp_state_page(partition->pt_id, args.vp_index,
						HV_VP_STATE_PAGE_REGISTERS,
						input_vtl_zero,
						&register_page);
		if (ret)
			goto unmap_intercept_message_page;
	}

	if (mshv_partition_encrypted(partition) &&
	    is_ghcb_mapping_available()) {
		ret = hv_call_map_vp_state_page(partition->pt_id, args.vp_index,
						HV_VP_STATE_PAGE_GHCB,
						input_vtl_normal,
						&ghcb_page);
		if (ret)
			goto unmap_register_page;
	}

	if (hv_parent_partition()) {
		ret = mshv_vp_stats_map(partition->pt_id, args.vp_index,
					stats_pages);
		if (ret)
			goto unmap_ghcb_page;
	}

	vp = kzalloc(sizeof(*vp), GFP_KERNEL);
	if (!vp)
		goto unmap_stats_pages;

	vp->vp_partition = mshv_partition_get(partition);
	if (!vp->vp_partition) {
		ret = -EBADF;
		goto free_vp;
	}

	mutex_init(&vp->vp_mutex);
	init_waitqueue_head(&vp->run.vp_suspend_queue);
	atomic64_set(&vp->run.vp_signaled_count, 0);

	vp->vp_index = args.vp_index;
	vp->vp_intercept_msg_page = page_to_virt(intercept_message_page);
	if (!mshv_partition_encrypted(partition))
		vp->vp_register_page = page_to_virt(register_page);

	if (mshv_partition_encrypted(partition) && is_ghcb_mapping_available())
		vp->vp_ghcb_page = page_to_virt(ghcb_page);

	if (hv_parent_partition())
		memcpy(vp->vp_stats_pages, stats_pages, sizeof(stats_pages));

	/*
	 * Keep anon_inode_getfd last: it installs fd in the file struct and
	 * thus makes the state accessible in user space.
	 */
	ret = anon_inode_getfd("mshv_vp", &mshv_vp_fops, vp,
			       O_RDWR | O_CLOEXEC);
	if (ret < 0)
		goto put_partition;

	/* already exclusive with the partition mutex for all ioctls */
	partition->pt_vp_count++;
	partition->pt_vp_array[args.vp_index] = vp;

	return ret;

put_partition:
	mshv_partition_put(partition);
free_vp:
	kfree(vp);
unmap_stats_pages:
	if (hv_parent_partition())
		mshv_vp_stats_unmap(partition->pt_id, args.vp_index);
unmap_ghcb_page:
	if (mshv_partition_encrypted(partition) && is_ghcb_mapping_available()) {
		hv_call_unmap_vp_state_page(partition->pt_id, args.vp_index,
					    HV_VP_STATE_PAGE_GHCB,
					    input_vtl_normal);
	}
unmap_register_page:
	if (!mshv_partition_encrypted(partition)) {
		hv_call_unmap_vp_state_page(partition->pt_id, args.vp_index,
					    HV_VP_STATE_PAGE_REGISTERS,
					    input_vtl_zero);
	}
unmap_intercept_message_page:
	hv_call_unmap_vp_state_page(partition->pt_id, args.vp_index,
				    HV_VP_STATE_PAGE_INTERCEPT_MESSAGE,
				    input_vtl_zero);
destroy_vp:
	hv_call_delete_vp(partition->pt_id, args.vp_index);
	return ret;
}

static int mshv_init_async_handler(struct mshv_partition *partition)
{
	if (completion_done(&partition->async_hypercall)) {
		pt_err(partition,
		       "Cannot issue async hypercall while another one in progress!\n");
		return -EPERM;
	}

	reinit_completion(&partition->async_hypercall);
	return 0;
}

static void mshv_async_hvcall_handler(void *data, u64 *status)
{
	struct mshv_partition *partition = data;

	wait_for_completion(&partition->async_hypercall);
	pt_dbg(partition, "Async hypercall completed!\n");

	*status = partition->async_hypercall_status;
}

static int
mshv_partition_region_share(struct mshv_mem_region *region)
{
	u32 flags = HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_SHARED;

	if (region->flags.large_pages)
		flags |= HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE;

	return hv_call_modify_spa_host_access(region->partition->pt_id,
			region->pages, region->nr_pages,
			HV_MAP_GPA_READABLE | HV_MAP_GPA_WRITABLE,
			flags, true);
}

static int
mshv_partition_region_unshare(struct mshv_mem_region *region)
{
	u32 flags = HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_EXCLUSIVE;

	if (region->flags.large_pages)
		flags |= HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE;

	return hv_call_modify_spa_host_access(region->partition->pt_id,
			region->pages, region->nr_pages,
			0,
			flags, false);
}

static int
mshv_region_remap_pages(struct mshv_mem_region *region, u32 map_flags,
			u64 page_offset, u64 page_count)
{
	if (page_offset + page_count > region->nr_pages)
		return -EINVAL;

	if (region->flags.large_pages)
		map_flags |= HV_MAP_GPA_LARGE_PAGE;

	/* ask the hypervisor to map guest ram */
	return hv_call_map_gpa_pages(region->partition->pt_id,
				     region->start_gfn + page_offset,
				     page_count, map_flags,
				     region->pages + page_offset);
}

static int
mshv_region_map(struct mshv_mem_region *region)
{
	u32 map_flags = region->hv_map_flags;

	return mshv_region_remap_pages(region, map_flags,
				       0, region->nr_pages);
}

static void
mshv_region_evict_pages(struct mshv_mem_region *region,
			u64 page_offset, u64 page_count)
{
	if (region->flags.range_pinned)
		unpin_user_pages(region->pages + page_offset, page_count);

	memset(region->pages + page_offset, 0,
	       page_count * sizeof(struct page *));
}

static void
mshv_region_evict(struct mshv_mem_region *region)
{
	mshv_region_evict_pages(region, 0, region->nr_pages);
}

static int
mshv_region_populate_pages(struct mshv_mem_region *region,
			   u64 page_offset, u64 page_count)
{
	u64 done_count, nr_pages;
	struct page **pages;
	__u64 userspace_addr;
	int ret;

	if (page_offset + page_count > region->nr_pages)
		return -EINVAL;

	for (done_count = 0; done_count < page_count; done_count += ret) {
		pages = region->pages + page_offset + done_count;
		userspace_addr = region->start_uaddr +
				(page_offset + done_count) *
				HV_HYP_PAGE_SIZE;
		nr_pages = min(page_count - done_count,
			       MSHV_PIN_PAGES_BATCH_SIZE);

		/*
		 * Pinning assuming 4k pages works for large pages too.
		 * All page structs within the large page are returned.
		 *
		 * Pin requests are batched because pin_user_pages_fast
		 * with the FOLL_LONGTERM flag does a large temporary
		 * allocation of contiguous memory.
		 */
		if (region->flags.range_pinned)
			ret = pin_user_pages_fast(userspace_addr,
						  nr_pages,
						  FOLL_WRITE | FOLL_LONGTERM,
						  pages);
		else
			ret = -EOPNOTSUPP;

		if (ret < 0)
			goto release_pages;
	}

	if (PageHuge(region->pages[page_offset]))
		region->flags.large_pages = true;

	return 0;

release_pages:
	mshv_region_evict_pages(region, page_offset, done_count);
	return ret;
}

static int
mshv_region_populate(struct mshv_mem_region *region)
{
	return mshv_region_populate_pages(region, 0, region->nr_pages);
}

static struct mshv_mem_region *
mshv_partition_region_by_gfn(struct mshv_partition *partition, u64 gfn)
{
	struct mshv_mem_region *region;

	hlist_for_each_entry(region, &partition->pt_mem_regions, hnode) {
		if (gfn >= region->start_gfn &&
		    gfn < region->start_gfn + region->nr_pages)
			return region;
	}

	return NULL;
}

static struct mshv_mem_region *
mshv_partition_region_by_uaddr(struct mshv_partition *partition, u64 uaddr)
{
	struct mshv_mem_region *region;

	hlist_for_each_entry(region, &partition->pt_mem_regions, hnode) {
		if (uaddr >= region->start_uaddr &&
		    uaddr < region->start_uaddr +
			    (region->nr_pages << HV_HYP_PAGE_SHIFT))
			return region;
	}

	return NULL;
}

/*
 * NB: caller checks and makes sure mem->size is page aligned
 * Returns: 0 with regionpp updated on success, or -errno
 */
static int mshv_partition_create_region(struct mshv_partition *partition,
					struct mshv_user_mem_region *mem,
					struct mshv_mem_region **regionpp,
					bool is_mmio)
{
	struct mshv_mem_region *region;
	u64 nr_pages = HVPFN_DOWN(mem->size);

	/* Reject overlapping regions */
	if (mshv_partition_region_by_gfn(partition, mem->guest_pfn) ||
	    mshv_partition_region_by_gfn(partition, mem->guest_pfn + nr_pages - 1) ||
	    mshv_partition_region_by_uaddr(partition, mem->userspace_addr) ||
	    mshv_partition_region_by_uaddr(partition, mem->userspace_addr + mem->size - 1))
		return -EEXIST;

	region = vzalloc(sizeof(*region) + sizeof(struct page *) * nr_pages);
	if (!region)
		return -ENOMEM;

	region->nr_pages = nr_pages;
	region->start_gfn = mem->guest_pfn;
	region->start_uaddr = mem->userspace_addr;
	region->hv_map_flags = HV_MAP_GPA_READABLE | HV_MAP_GPA_ADJUSTABLE;
	if (mem->flags & BIT(MSHV_SET_MEM_BIT_WRITABLE))
		region->hv_map_flags |= HV_MAP_GPA_WRITABLE;
	if (mem->flags & BIT(MSHV_SET_MEM_BIT_EXECUTABLE))
		region->hv_map_flags |= HV_MAP_GPA_EXECUTABLE;

	/* Note: large_pages flag populated when we pin the pages */
	if (!is_mmio)
		region->flags.range_pinned = true;

	region->partition = partition;

	*regionpp = region;

	return 0;
}

/*
 * Map guest ram. if snp, make sure to release that from the host first
 * Side Effects: In case of failure, pages are unpinned when feasible.
 */
static int
mshv_partition_mem_region_map(struct mshv_mem_region *region)
{
	struct mshv_partition *partition = region->partition;
	int ret;

	ret = mshv_region_populate(region);
	if (ret) {
		pt_err(partition, "Failed to populate memory region: %d\n",
		       ret);
		goto err_out;
	}

	/*
	 * For an SNP partition it is a requirement that for every memory region
	 * that we are going to map for this partition we should make sure that
	 * host access to that region is released. This is ensured by doing an
	 * additional hypercall which will update the SLAT to release host
	 * access to guest memory regions.
	 */
	if (mshv_partition_encrypted(partition)) {
		ret = mshv_partition_region_unshare(region);
		if (ret) {
			pt_err(partition,
			       "Failed to unshare memory region (guest_pfn: %llu): %d\n",
			       region->start_gfn, ret);
			goto evict_region;
		}
	}

	ret = mshv_region_map(region);
	if (ret && mshv_partition_encrypted(partition)) {
		int shrc;

		shrc = mshv_partition_region_share(region);
		if (!shrc)
			goto evict_region;

		pt_err(partition,
		       "Failed to share memory region (guest_pfn: %llu): %d\n",
		       region->start_gfn, shrc);
		/*
		 * Don't unpin if marking shared failed because pages are no
		 * longer mapped in the host, ie root, anymore.
		 */
		goto err_out;
	}

	return 0;

evict_region:
	mshv_region_evict(region);
err_out:
	return ret;
}

/*
 * This maps two things: guest RAM and for pci passthru mmio space.
 *
 * mmio:
 *  - vfio overloads vm_pgoff to store the mmio start pfn/spa.
 *  - Two things need to happen for mapping mmio range:
 *	1. mapped in the uaddr so VMM can access it.
 *	2. mapped in the hwpt (gfn <-> mmio phys addr) so guest can access it.
 *
 *   This function takes care of the second. The first one is managed by vfio,
 *   and hence is taken care of via vfio_pci_mmap_fault().
 */
static long
mshv_map_user_memory(struct mshv_partition *partition,
		     struct mshv_user_mem_region mem)
{
	struct mshv_mem_region *region;
	struct vm_area_struct *vma;
	bool is_mmio;
	ulong mmio_pfn;
	long ret;

	if (mem.flags & BIT(MSHV_SET_MEM_BIT_UNMAP) ||
	    !access_ok((const void *)mem.userspace_addr, mem.size))
		return -EINVAL;

	mmap_read_lock(current->mm);
	vma = vma_lookup(current->mm, mem.userspace_addr);
	is_mmio = vma ? !!(vma->vm_flags & (VM_IO | VM_PFNMAP)) : 0;
	mmio_pfn = is_mmio ? vma->vm_pgoff : 0;
	mmap_read_unlock(current->mm);

	if (!vma)
		return -EINVAL;

	ret = mshv_partition_create_region(partition, &mem, &region,
					   is_mmio);
	if (ret)
		return ret;

	if (is_mmio)
		ret = hv_call_map_mmio_pages(partition->pt_id, mem.guest_pfn,
					     mmio_pfn, HVPFN_DOWN(mem.size));
	else
		ret = mshv_partition_mem_region_map(region);

	if (ret)
		goto errout;

	/* Install the new region */
	hlist_add_head(&region->hnode, &partition->pt_mem_regions);

	return 0;

errout:
	vfree(region);
	return ret;
}

/* Called for unmapping both the guest ram and the mmio space */
static long
mshv_unmap_user_memory(struct mshv_partition *partition,
		       struct mshv_user_mem_region mem)
{
	struct mshv_mem_region *region;
	u32 unmap_flags = 0;

	if (!(mem.flags & BIT(MSHV_SET_MEM_BIT_UNMAP)))
		return -EINVAL;

	region = mshv_partition_region_by_gfn(partition, mem.guest_pfn);
	if (!region)
		return -EINVAL;

	/* Paranoia check */
	if (region->start_uaddr != mem.userspace_addr ||
	    region->start_gfn != mem.guest_pfn ||
	    region->nr_pages != HVPFN_DOWN(mem.size))
		return -EINVAL;

	hlist_del(&region->hnode);

	if (region->flags.large_pages)
		unmap_flags |= HV_UNMAP_GPA_LARGE_PAGE;

	/* ignore unmap failures and continue as process may be exiting */
	hv_call_unmap_gpa_pages(partition->pt_id, region->start_gfn,
				region->nr_pages, unmap_flags);

	mshv_region_evict(region);

	vfree(region);
	return 0;
}

static long
mshv_partition_ioctl_set_memory(struct mshv_partition *partition,
				struct mshv_user_mem_region __user *user_mem)
{
	struct mshv_user_mem_region mem;

	if (copy_from_user(&mem, user_mem, sizeof(mem)))
		return -EFAULT;

	if (!mem.size ||
	    !PAGE_ALIGNED(mem.size) ||
	    !PAGE_ALIGNED(mem.userspace_addr) ||
	    (mem.flags & ~MSHV_SET_MEM_FLAGS_MASK) ||
	    mshv_field_nonzero(mem, rsvd))
		return -EINVAL;

	if (mem.flags & BIT(MSHV_SET_MEM_BIT_UNMAP))
		return mshv_unmap_user_memory(partition, mem);

	return mshv_map_user_memory(partition, mem);
}

static long
mshv_partition_ioctl_ioeventfd(struct mshv_partition *partition,
			       void __user *user_args)
{
	struct mshv_user_ioeventfd args;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	return mshv_set_unset_ioeventfd(partition, &args);
}

static long
mshv_partition_ioctl_irqfd(struct mshv_partition *partition,
			   void __user *user_args)
{
	struct mshv_user_irqfd args;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	return mshv_set_unset_irqfd(partition, &args);
}

static long
mshv_partition_ioctl_get_gpap_access_bitmap(struct mshv_partition *partition,
					    void __user *user_args)
{
	struct mshv_gpap_access_bitmap args;
	union hv_gpa_page_access_state *states;
	long ret, i;
	union hv_gpa_page_access_state_flags hv_flags = {};
	u8 hv_type_mask;
	ulong bitmap_buf_sz, states_buf_sz;
	int written = 0;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.access_type >= MSHV_GPAP_ACCESS_TYPE_COUNT ||
	    args.access_op >= MSHV_GPAP_ACCESS_OP_COUNT ||
	    mshv_field_nonzero(args, rsvd) || !args.page_count ||
	    !args.bitmap_ptr)
		return -EINVAL;

	if (check_mul_overflow(args.page_count, sizeof(*states), &states_buf_sz))
		return -E2BIG;

	/* Num bytes needed to store bitmap; one bit per page rounded up */
	bitmap_buf_sz = DIV_ROUND_UP(args.page_count, 8);

	/* Sanity check */
	if (bitmap_buf_sz > states_buf_sz)
		return -EBADFD;

	switch (args.access_type) {
	case MSHV_GPAP_ACCESS_TYPE_ACCESSED:
		hv_type_mask = 1;
		if (args.access_op == MSHV_GPAP_ACCESS_OP_CLEAR) {
			hv_flags.clear_accessed = 1;
			/* not accessed implies not dirty */
			hv_flags.clear_dirty = 1;
		} else { /* MSHV_GPAP_ACCESS_OP_SET */
			hv_flags.set_accessed = 1;
		}
		break;
	case MSHV_GPAP_ACCESS_TYPE_DIRTY:
		hv_type_mask = 2;
		if (args.access_op == MSHV_GPAP_ACCESS_OP_CLEAR) {
			hv_flags.clear_dirty = 1;
		} else { /* MSHV_GPAP_ACCESS_OP_SET */
			hv_flags.set_dirty = 1;
			/* dirty implies accessed */
			hv_flags.set_accessed = 1;
		}
		break;
	}

	states = vzalloc(states_buf_sz);
	if (!states)
		return -ENOMEM;

	ret = hv_call_get_gpa_access_states(partition->pt_id, args.page_count,
					    args.gpap_base, hv_flags, &written,
					    states);
	if (ret)
		goto free_return;

	/*
	 * Overwrite states buffer with bitmap - the bits in hv_type_mask
	 * correspond to bitfields in hv_gpa_page_access_state
	 */
	for (i = 0; i < written; ++i)
		__assign_bit(i, (ulong *)states,
			     states[i].as_uint8 & hv_type_mask);

	/* zero the unused bits in the last byte(s) of the returned bitmap */
	for (i = written; i < bitmap_buf_sz * 8; ++i)
		__clear_bit(i, (ulong *)states);

	if (copy_to_user((void __user *)args.bitmap_ptr, states, bitmap_buf_sz))
		ret = -EFAULT;

free_return:
	vfree(states);
	return ret;
}

static long
mshv_partition_ioctl_set_msi_routing(struct mshv_partition *partition,
				     void __user *user_args)
{
	struct mshv_user_irq_entry *entries = NULL;
	struct mshv_user_irq_table args;
	long ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.nr > MSHV_MAX_GUEST_IRQS ||
	    mshv_field_nonzero(args, rsvd))
		return -EINVAL;

	if (args.nr) {
		struct mshv_user_irq_table __user *urouting = user_args;

		entries = vmemdup_user(urouting->entries,
				       array_size(sizeof(*entries),
						  args.nr));
		if (IS_ERR(entries))
			return PTR_ERR(entries);
	}
	ret = mshv_update_routing_table(partition, entries, args.nr);
	kvfree(entries);

	return ret;
}

static long
mshv_partition_ioctl_initialize(struct mshv_partition *partition)
{
	long ret;

	if (partition->pt_initialized)
		return 0;

	ret = hv_call_initialize_partition(partition->pt_id);
	if (ret)
		goto withdraw_mem;

	partition->pt_initialized = true;

	return 0;

withdraw_mem:
	hv_call_withdraw_memory(U64_MAX, NUMA_NO_NODE, partition->pt_id);

	return ret;
}

static long
mshv_partition_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct mshv_partition *partition = filp->private_data;
	long ret;
	void __user *uarg = (void __user *)arg;

	if (mutex_lock_killable(&partition->pt_mutex))
		return -EINTR;

	switch (ioctl) {
	case MSHV_INITIALIZE_PARTITION:
		ret = mshv_partition_ioctl_initialize(partition);
		break;
	case MSHV_SET_GUEST_MEMORY:
		ret = mshv_partition_ioctl_set_memory(partition, uarg);
		break;
	case MSHV_CREATE_VP:
		ret = mshv_partition_ioctl_create_vp(partition, uarg);
		break;
	case MSHV_IRQFD:
		ret = mshv_partition_ioctl_irqfd(partition, uarg);
		break;
	case MSHV_IOEVENTFD:
		ret = mshv_partition_ioctl_ioeventfd(partition, uarg);
		break;
	case MSHV_SET_MSI_ROUTING:
		ret = mshv_partition_ioctl_set_msi_routing(partition, uarg);
		break;
	case MSHV_GET_GPAP_ACCESS_BITMAP:
		ret = mshv_partition_ioctl_get_gpap_access_bitmap(partition,
								  uarg);
		break;
	case MSHV_ROOT_HVCALL:
		ret = mshv_ioctl_passthru_hvcall(partition, true, uarg);
		break;
	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&partition->pt_mutex);
	return ret;
}

static int
disable_vp_dispatch(struct mshv_vp *vp)
{
	int ret;
	struct hv_register_assoc dispatch_suspend = {
		.name = HV_REGISTER_DISPATCH_SUSPEND,
		.value.dispatch_suspend.suspended = 1,
	};

	ret = mshv_set_vp_registers(vp->vp_index, vp->vp_partition->pt_id,
				    1, &dispatch_suspend);
	if (ret)
		vp_err(vp, "failed to suspend\n");

	return ret;
}

static int
get_vp_signaled_count(struct mshv_vp *vp, u64 *count)
{
	int ret;
	struct hv_register_assoc root_signal_count = {
		.name = HV_REGISTER_VP_ROOT_SIGNAL_COUNT,
	};

	ret = mshv_get_vp_registers(vp->vp_index, vp->vp_partition->pt_id,
				    1, &root_signal_count);

	if (ret) {
		vp_err(vp, "Failed to get root signal count");
		*count = 0;
		return ret;
	}

	*count = root_signal_count.value.reg64;

	return ret;
}

static void
drain_vp_signals(struct mshv_vp *vp)
{
	u64 hv_signal_count;
	u64 vp_signal_count;

	get_vp_signaled_count(vp, &hv_signal_count);

	vp_signal_count = atomic64_read(&vp->run.vp_signaled_count);

	/*
	 * There should be at most 1 outstanding notification, but be extra
	 * careful anyway.
	 */
	while (hv_signal_count != vp_signal_count) {
		WARN_ON(hv_signal_count - vp_signal_count != 1);

		if (wait_event_interruptible(vp->run.vp_suspend_queue,
					     vp->run.kicked_by_hv == 1))
			break;
		vp->run.kicked_by_hv = 0;
		vp_signal_count = atomic64_read(&vp->run.vp_signaled_count);
	}
}

static void drain_all_vps(const struct mshv_partition *partition)
{
	int i;
	struct mshv_vp *vp;

	/*
	 * VPs are reachable from ISR. It is safe to not take the partition
	 * lock because nobody else can enter this function and drop the
	 * partition from the list.
	 */
	for (i = 0; i < MSHV_MAX_VPS; i++) {
		vp = partition->pt_vp_array[i];
		if (!vp)
			continue;
		/*
		 * Disable dispatching of the VP in the hypervisor. After this
		 * the hypervisor guarantees it won't generate any signals for
		 * the VP and the hypervisor's VP signal count won't change.
		 */
		disable_vp_dispatch(vp);
		drain_vp_signals(vp);
	}
}

static void
remove_partition(struct mshv_partition *partition)
{
	spin_lock(&mshv_root.pt_ht_lock);
	hlist_del_rcu(&partition->pt_hnode);
	spin_unlock(&mshv_root.pt_ht_lock);

	synchronize_rcu();
}

/*
 * Tear down a partition and remove it from the list.
 * Partition's refcount must be 0
 */
static void destroy_partition(struct mshv_partition *partition)
{
	struct mshv_vp *vp;
	struct mshv_mem_region *region;
	int i, ret;
	struct hlist_node *n;

	if (refcount_read(&partition->pt_ref_count)) {
		pt_err(partition,
		       "Attempt to destroy partition but refcount > 0\n");
		return;
	}

	if (partition->pt_initialized) {
		/*
		 * We only need to drain signals for root scheduler. This should be
		 * done before removing the partition from the partition list.
		 */
		if (hv_scheduler_type == HV_SCHEDULER_TYPE_ROOT)
			drain_all_vps(partition);

		/* Remove vps */
		for (i = 0; i < MSHV_MAX_VPS; ++i) {
			vp = partition->pt_vp_array[i];
			if (!vp)
				continue;

			if (hv_parent_partition())
				mshv_vp_stats_unmap(partition->pt_id, vp->vp_index);

			if (vp->vp_register_page) {
				(void)hv_call_unmap_vp_state_page(partition->pt_id,
								  vp->vp_index,
								  HV_VP_STATE_PAGE_REGISTERS,
								  input_vtl_zero);
				vp->vp_register_page = NULL;
			}

			(void)hv_call_unmap_vp_state_page(partition->pt_id,
							  vp->vp_index,
							  HV_VP_STATE_PAGE_INTERCEPT_MESSAGE,
							  input_vtl_zero);
			vp->vp_intercept_msg_page = NULL;

			if (vp->vp_ghcb_page) {
				(void)hv_call_unmap_vp_state_page(partition->pt_id,
								  vp->vp_index,
								  HV_VP_STATE_PAGE_GHCB,
								  input_vtl_normal);
				vp->vp_ghcb_page = NULL;
			}

			kfree(vp);

			partition->pt_vp_array[i] = NULL;
		}

		/* Deallocates and unmaps everything including vcpus, GPA mappings etc */
		hv_call_finalize_partition(partition->pt_id);

		partition->pt_initialized = false;
	}

	remove_partition(partition);

	/* Remove regions, regain access to the memory and unpin the pages */
	hlist_for_each_entry_safe(region, n, &partition->pt_mem_regions,
				  hnode) {
		hlist_del(&region->hnode);

		if (mshv_partition_encrypted(partition)) {
			ret = mshv_partition_region_share(region);
			if (ret) {
				pt_err(partition,
				       "Failed to regain access to memory, unpinning user pages will fail and crash the host error: %d\n",
				      ret);
				return;
			}
		}

		mshv_region_evict(region);

		vfree(region);
	}

	/* Withdraw and free all pages we deposited */
	hv_call_withdraw_memory(U64_MAX, NUMA_NO_NODE, partition->pt_id);
	hv_call_delete_partition(partition->pt_id);

	mshv_free_routing_table(partition);
	kfree(partition);
}

struct
mshv_partition *mshv_partition_get(struct mshv_partition *partition)
{
	if (refcount_inc_not_zero(&partition->pt_ref_count))
		return partition;
	return NULL;
}

struct
mshv_partition *mshv_partition_find(u64 partition_id)
	__must_hold(RCU)
{
	struct mshv_partition *p;

	hash_for_each_possible_rcu(mshv_root.pt_htable, p, pt_hnode,
				   partition_id)
		if (p->pt_id == partition_id)
			return p;

	return NULL;
}

void
mshv_partition_put(struct mshv_partition *partition)
{
	if (refcount_dec_and_test(&partition->pt_ref_count))
		destroy_partition(partition);
}

static int
mshv_partition_release(struct inode *inode, struct file *filp)
{
	struct mshv_partition *partition = filp->private_data;

	mshv_eventfd_release(partition);

	cleanup_srcu_struct(&partition->pt_irq_srcu);

	mshv_partition_put(partition);

	return 0;
}

static int
add_partition(struct mshv_partition *partition)
{
	spin_lock(&mshv_root.pt_ht_lock);

	hash_add_rcu(mshv_root.pt_htable, &partition->pt_hnode,
		     partition->pt_id);

	spin_unlock(&mshv_root.pt_ht_lock);

	return 0;
}

static long
mshv_ioctl_create_partition(void __user *user_arg, struct device *module_dev)
{
	struct mshv_create_partition args;
	u64 creation_flags;
	struct hv_partition_creation_properties creation_properties = {};
	union hv_partition_isolation_properties isolation_properties = {};
	struct mshv_partition *partition;
	struct file *file;
	int fd;
	long ret;

	if (copy_from_user(&args, user_arg, sizeof(args)))
		return -EFAULT;

	if ((args.pt_flags & ~MSHV_PT_FLAGS_MASK) ||
	    args.pt_isolation >= MSHV_PT_ISOLATION_COUNT)
		return -EINVAL;

	/* Only support EXO partitions */
	creation_flags = HV_PARTITION_CREATION_FLAG_EXO_PARTITION |
			 HV_PARTITION_CREATION_FLAG_INTERCEPT_MESSAGE_PAGE_ENABLED;

	if (args.pt_flags & BIT(MSHV_PT_BIT_LAPIC))
		creation_flags |= HV_PARTITION_CREATION_FLAG_LAPIC_ENABLED;
	if (args.pt_flags & BIT(MSHV_PT_BIT_X2APIC))
		creation_flags |= HV_PARTITION_CREATION_FLAG_X2APIC_CAPABLE;
	if (args.pt_flags & BIT(MSHV_PT_BIT_GPA_SUPER_PAGES))
		creation_flags |= HV_PARTITION_CREATION_FLAG_GPA_SUPER_PAGES_ENABLED;

	switch (args.pt_isolation) {
	case MSHV_PT_ISOLATION_NONE:
		isolation_properties.isolation_type =
			HV_PARTITION_ISOLATION_TYPE_NONE;
		break;
	}

	partition = kzalloc(sizeof(*partition), GFP_KERNEL);
	if (!partition)
		return -ENOMEM;

	partition->pt_module_dev = module_dev;
	partition->isolation_type = isolation_properties.isolation_type;

	refcount_set(&partition->pt_ref_count, 1);

	mutex_init(&partition->pt_mutex);

	mutex_init(&partition->pt_irq_lock);

	init_completion(&partition->async_hypercall);

	INIT_HLIST_HEAD(&partition->irq_ack_notifier_list);

	INIT_HLIST_HEAD(&partition->pt_devices);

	INIT_HLIST_HEAD(&partition->pt_mem_regions);

	mshv_eventfd_init(partition);

	ret = init_srcu_struct(&partition->pt_irq_srcu);
	if (ret)
		goto free_partition;

	ret = hv_call_create_partition(creation_flags,
				       creation_properties,
				       isolation_properties,
				       &partition->pt_id);
	if (ret)
		goto cleanup_irq_srcu;

	ret = add_partition(partition);
	if (ret)
		goto delete_partition;

	ret = mshv_init_async_handler(partition);
	if (ret)
		goto remove_partition;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto remove_partition;
	}

	file = anon_inode_getfile("mshv_partition", &mshv_partition_fops,
				  partition, O_RDWR);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto put_fd;
	}

	fd_install(fd, file);

	return fd;

put_fd:
	put_unused_fd(fd);
remove_partition:
	remove_partition(partition);
delete_partition:
	hv_call_delete_partition(partition->pt_id);
cleanup_irq_srcu:
	cleanup_srcu_struct(&partition->pt_irq_srcu);
free_partition:
	kfree(partition);

	return ret;
}

static long mshv_dev_ioctl(struct file *filp, unsigned int ioctl,
			   unsigned long arg)
{
	struct miscdevice *misc = filp->private_data;

	switch (ioctl) {
	case MSHV_CREATE_PARTITION:
		return mshv_ioctl_create_partition((void __user *)arg,
						misc->this_device);
	}

	return -ENOTTY;
}

static int
mshv_dev_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int
mshv_dev_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int mshv_cpuhp_online;
static int mshv_root_sched_online;

static const char *scheduler_type_to_string(enum hv_scheduler_type type)
{
	switch (type) {
	case HV_SCHEDULER_TYPE_LP:
		return "classic scheduler without SMT";
	case HV_SCHEDULER_TYPE_LP_SMT:
		return "classic scheduler with SMT";
	case HV_SCHEDULER_TYPE_CORE_SMT:
		return "core scheduler";
	case HV_SCHEDULER_TYPE_ROOT:
		return "root scheduler";
	default:
		return "unknown scheduler";
	};
}

/* TODO move this to hv_common.c when needed outside */
static int __init hv_retrieve_scheduler_type(enum hv_scheduler_type *out)
{
	struct hv_input_get_system_property *input;
	struct hv_output_get_system_property *output;
	unsigned long flags;
	u64 status;

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output = *this_cpu_ptr(hyperv_pcpu_output_arg);

	memset(input, 0, sizeof(*input));
	memset(output, 0, sizeof(*output));
	input->property_id = HV_SYSTEM_PROPERTY_SCHEDULER_TYPE;

	status = hv_do_hypercall(HVCALL_GET_SYSTEM_PROPERTY, input, output);
	if (!hv_result_success(status)) {
		local_irq_restore(flags);
		pr_err("%s: %s\n", __func__, hv_result_to_string(status));
		return hv_result_to_errno(status);
	}

	*out = output->scheduler_type;
	local_irq_restore(flags);

	return 0;
}

/* Retrieve and stash the supported scheduler type */
static int __init mshv_retrieve_scheduler_type(struct device *dev)
{
	int ret;

	ret = hv_retrieve_scheduler_type(&hv_scheduler_type);
	if (ret)
		return ret;

	dev_info(dev, "Hypervisor using %s\n",
		 scheduler_type_to_string(hv_scheduler_type));

	switch (hv_scheduler_type) {
	case HV_SCHEDULER_TYPE_CORE_SMT:
	case HV_SCHEDULER_TYPE_LP_SMT:
	case HV_SCHEDULER_TYPE_ROOT:
	case HV_SCHEDULER_TYPE_LP:
		/* Supported scheduler, nothing to do */
		break;
	default:
		dev_err(dev, "unsupported scheduler 0x%x, bailing.\n",
			hv_scheduler_type);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int mshv_root_scheduler_init(unsigned int cpu)
{
	void **inputarg, **outputarg, *p;

	inputarg = (void **)this_cpu_ptr(root_scheduler_input);
	outputarg = (void **)this_cpu_ptr(root_scheduler_output);

	/* Allocate two consecutive pages. One for input, one for output. */
	p = kmalloc(2 * HV_HYP_PAGE_SIZE, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	*inputarg = p;
	*outputarg = (char *)p + HV_HYP_PAGE_SIZE;

	return 0;
}

static int mshv_root_scheduler_cleanup(unsigned int cpu)
{
	void *p, **inputarg, **outputarg;

	inputarg = (void **)this_cpu_ptr(root_scheduler_input);
	outputarg = (void **)this_cpu_ptr(root_scheduler_output);

	p = *inputarg;

	*inputarg = NULL;
	*outputarg = NULL;

	kfree(p);

	return 0;
}

/* Must be called after retrieving the scheduler type */
static int
root_scheduler_init(struct device *dev)
{
	int ret;

	if (hv_scheduler_type != HV_SCHEDULER_TYPE_ROOT)
		return 0;

	root_scheduler_input = alloc_percpu(void *);
	root_scheduler_output = alloc_percpu(void *);

	if (!root_scheduler_input || !root_scheduler_output) {
		dev_err(dev, "Failed to allocate root scheduler buffers\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "mshv_root_sched",
				mshv_root_scheduler_init,
				mshv_root_scheduler_cleanup);

	if (ret < 0) {
		dev_err(dev, "Failed to setup root scheduler state: %i\n", ret);
		goto out;
	}

	mshv_root_sched_online = ret;

	return 0;

out:
	free_percpu(root_scheduler_input);
	free_percpu(root_scheduler_output);
	return ret;
}

static void
root_scheduler_deinit(void)
{
	if (hv_scheduler_type != HV_SCHEDULER_TYPE_ROOT)
		return;

	cpuhp_remove_state(mshv_root_sched_online);
	free_percpu(root_scheduler_input);
	free_percpu(root_scheduler_output);
}

static int mshv_reboot_notify(struct notifier_block *nb,
			      unsigned long code, void *unused)
{
	cpuhp_remove_state(mshv_cpuhp_online);
	return 0;
}

struct notifier_block mshv_reboot_nb = {
	.notifier_call = mshv_reboot_notify,
};

static void mshv_root_partition_exit(void)
{
	unregister_reboot_notifier(&mshv_reboot_nb);
	root_scheduler_deinit();
}

static int __init mshv_root_partition_init(struct device *dev)
{
	int err;

	if (mshv_retrieve_scheduler_type(dev))
		return -ENODEV;

	err = root_scheduler_init(dev);
	if (err)
		return err;

	err = register_reboot_notifier(&mshv_reboot_nb);
	if (err)
		goto root_sched_deinit;

	return 0;

root_sched_deinit:
	root_scheduler_deinit();
	return err;
}

static int __init mshv_parent_partition_init(void)
{
	int ret;
	struct device *dev;
	union hv_hypervisor_version_info version_info;

	if (!hv_root_partition() || is_kdump_kernel())
		return -ENODEV;

	if (hv_get_hypervisor_version(&version_info))
		return -ENODEV;

	ret = misc_register(&mshv_dev);
	if (ret)
		return ret;

	dev = mshv_dev.this_device;

	if (version_info.build_number < MSHV_HV_MIN_VERSION ||
	    version_info.build_number > MSHV_HV_MAX_VERSION) {
		dev_err(dev, "Running on unvalidated Hyper-V version\n");
		dev_err(dev, "Versions: current: %u  min: %u  max: %u\n",
			version_info.build_number, MSHV_HV_MIN_VERSION,
			MSHV_HV_MAX_VERSION);
	}

	mshv_root.synic_pages = alloc_percpu(struct hv_synic_pages);
	if (!mshv_root.synic_pages) {
		dev_err(dev, "Failed to allocate percpu synic page\n");
		ret = -ENOMEM;
		goto device_deregister;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "mshv_synic",
				mshv_synic_init,
				mshv_synic_cleanup);
	if (ret < 0) {
		dev_err(dev, "Failed to setup cpu hotplug state: %i\n", ret);
		goto free_synic_pages;
	}

	mshv_cpuhp_online = ret;

	ret = mshv_root_partition_init(dev);
	if (ret)
		goto remove_cpu_state;

	ret = mshv_irqfd_wq_init();
	if (ret)
		goto exit_partition;

	spin_lock_init(&mshv_root.pt_ht_lock);
	hash_init(mshv_root.pt_htable);

	hv_setup_mshv_handler(mshv_isr);

	return 0;

exit_partition:
	if (hv_root_partition())
		mshv_root_partition_exit();
remove_cpu_state:
	cpuhp_remove_state(mshv_cpuhp_online);
free_synic_pages:
	free_percpu(mshv_root.synic_pages);
device_deregister:
	misc_deregister(&mshv_dev);
	return ret;
}

static void __exit mshv_parent_partition_exit(void)
{
	hv_setup_mshv_handler(NULL);
	mshv_port_table_fini();
	misc_deregister(&mshv_dev);
	mshv_irqfd_wq_cleanup();
	if (hv_root_partition())
		mshv_root_partition_exit();
	cpuhp_remove_state(mshv_cpuhp_online);
	free_percpu(mshv_root.synic_pages);
}

module_init(mshv_parent_partition_init);
module_exit(mshv_parent_partition_exit);
