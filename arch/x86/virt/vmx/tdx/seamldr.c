// SPDX-License-Identifier: GPL-2.0
/*
 * P-SEAMLDR support for TDX module management features like runtime updates
 *
 * Copyright (C) 2025 Intel Corporation
 */
#define pr_fmt(fmt)	"seamldr: " fmt

#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stop_machine.h>

#include <asm/cpufeature.h>
#include <asm/cpufeatures.h>
#include <asm/seamldr.h>

#include "seamcall_internal.h"
#include "tdx.h"

/* P-SEAMLDR SEAMCALL leaf function */
#define P_SEAMLDR_INFO			0x8000000000000000
#define P_SEAMLDR_INSTALL		0x8000000000000001

#define SEAMLDR_MAX_NR_MODULE_PAGES	496
#define SEAMLDR_MAX_NR_SIG_PAGES	1

/*
 * The seamldr_params "scenario" field specifies the operation mode:
 * 0: Install TDX module from scratch (not used by kernel)
 * 1: Update existing TDX module to a compatible version
 */
#define SEAMLDR_SCENARIO_UPDATE		1

/*
 * This is the "SEAMLDR_PARAMS" data structure defined in the
 * "SEAM Loader (SEAMLDR) Interface Specification".
 *
 * It is the in-memory ABI that the kernel passes to the P-SEAMLDR
 * to update the TDX module. It breaks the TDX module image up in
 * page-size pieces.
 */
struct seamldr_params {
	u32	version;
	u32	scenario;
	u64	sigstruct_pages_pa_list[SEAMLDR_MAX_NR_SIG_PAGES];
	u8	reserved[104];
	u64	module_nr_pages;
	u64	module_pages_pa_list[SEAMLDR_MAX_NR_MODULE_PAGES];
} __packed;

static_assert(sizeof(struct seamldr_params) == 4096);

/*
 * Serialize P-SEAMLDR calls since the hardware only allows a single CPU to
 * interact with P-SEAMLDR simultaneously. Use raw version as the calls can
 * be made with interrupts disabled, where plain spinlocks are prohibited in
 * PREEMPT_RT kernels as they become sleeping locks.
 */
static DEFINE_RAW_SPINLOCK(seamldr_lock);

static int seamldr_call(u64 fn, struct tdx_module_args *args)
{
	/*
	 * With this bug, P-SEAMLDR calls corrupt the VMCS
	 * pointer and must be avoided. This path should be
	 * unreachable since sysfs hides the ABIs.
	 */
	if (boot_cpu_has_bug(X86_BUG_SEAMRET_INVD_VMCS)) {
		WARN_ON(1);
		return -EINVAL;
	}

	guard(raw_spinlock)(&seamldr_lock);
	return seamcall_prerr(fn, args);
}

int seamldr_get_info(struct seamldr_info *seamldr_info)
{
	struct tdx_module_args args = {};

	/*
	 * Use slow_virt_to_phys() since @seamldr_info may be allocated on
	 * the stack.
	 */
	args.rcx = slow_virt_to_phys(seamldr_info);
	return seamldr_call(P_SEAMLDR_INFO, &args);
}
EXPORT_SYMBOL_FOR_MODULES(seamldr_get_info, "tdx-host");

/* Call into P-SEAMLDR to install a TDX module update */
static int seamldr_install(const struct seamldr_params *params)
{
	struct tdx_module_args args = {};

	args.rcx = __pa(params);
	return seamldr_call(P_SEAMLDR_INSTALL, &args);
}

#define TDX_IMAGE_VERSION_2		0x200

/* First page of the on-disk module update image: */
struct tdx_image_header {
	u16	version;
	u16	checksum;
	u8	signature[8];
	u32	sigstruct_nr_pages;
	u32	module_nr_pages;
	u8	reserved[4076];
} __packed;

#define TDX_IMAGE_HEADER_SIZE sizeof(struct tdx_image_header)
static_assert(TDX_IMAGE_HEADER_SIZE == 4096);

/*
 * Intel TDX module update ABI structure. aka. "TDX module blob".
 * This is the on-disk format that fw_upload lands in a kernel
 * buffer.
 *
 * @payload contains sigstruct pages followed by module pages.
 */
struct tdx_image {
	struct tdx_image_header header;
	u8 payload[];
};

/*
 * Given a vmalloc() allocation, write all of the backing physical
 * addresses to pa_list[]. Caller guarantees that the array is big
 * enough.
 */
static void populate_pa_list(u64 *pa_list, const u8 *vmalloc_addr, u32 vmalloc_len_pages)
{
	int i;

	for (i = 0; i < vmalloc_len_pages; i++) {
		unsigned long offset = i * PAGE_SIZE;
		unsigned long pfn = vmalloc_to_pfn(&vmalloc_addr[offset]);

		pa_list[i] = pfn << PAGE_SHIFT;
	}
}

static void populate_seamldr_params(struct seamldr_params *params,
				    const u8 *sig, u32 sig_nr_pages,
				    const u8 *mod, u32 mod_nr_pages)
{
	params->version		= 0;
	params->scenario	= SEAMLDR_SCENARIO_UPDATE;
	params->module_nr_pages	= mod_nr_pages;

	populate_pa_list(params->sigstruct_pages_pa_list, sig, sig_nr_pages);
	populate_pa_list(params->module_pages_pa_list,	  mod, mod_nr_pages);
}

/*
 * @image points to a vmalloc()'d 'struct tdx_image'. Transform
 * it into @params which is the P-SEAMLDR ABI format.
 */
static int init_seamldr_params(struct seamldr_params *params,
			       const struct tdx_image *image,
			       u32 image_len)
{
	const struct tdx_image_header *header	= &image->header;

	u32 sigstruct_len	= header->sigstruct_nr_pages * PAGE_SIZE;
	u32 module_len		= header->module_nr_pages    * PAGE_SIZE;

	u8 *header_start	= (u8 *)header;
	u8 *header_end		= header_start + TDX_IMAGE_HEADER_SIZE;

	u8 *sigstruct_start	= header_end;
	u8 *sigstruct_end	= sigstruct_start + sigstruct_len;

	u8 *module_start	= sigstruct_end;

	/* Check the calculated payload size against the image size. */
	if (TDX_IMAGE_HEADER_SIZE + sigstruct_len + module_len != image_len)
		return -EINVAL;

	/* Reject unsupported tdx_image ABI versions. */
	if (header->version != TDX_IMAGE_VERSION_2)
		return -EINVAL;

	if (header->sigstruct_nr_pages > SEAMLDR_MAX_NR_SIG_PAGES ||
	    header->module_nr_pages    > SEAMLDR_MAX_NR_MODULE_PAGES)
		return -EINVAL;

	if (memcmp(header->signature, "TDX-BLOB", sizeof(header->signature)))
		return -EINVAL;

	if (memchr_inv(header->reserved, 0, sizeof(header->reserved)))
		return -EINVAL;

	populate_seamldr_params(params, sigstruct_start, header->sigstruct_nr_pages,
					module_start,    header->module_nr_pages);
	return 0;
}

/*
 * During a TDX module update, all CPUs start from MODULE_UPDATE_START and
 * progress to MODULE_UPDATE_DONE. Each state is associated with certain
 * work. For some states, just one CPU needs to perform the work, while
 * other CPUs just wait during those states.
 */
enum module_update_state {
	MODULE_UPDATE_START,
	MODULE_UPDATE_SHUTDOWN,
	MODULE_UPDATE_CPU_INSTALL,
	MODULE_UPDATE_CPU_INIT,
	MODULE_UPDATE_RUN_UPDATE,
	MODULE_UPDATE_DONE,
};

static struct update_ctrl {
	enum module_update_state state;
	int num_ack;
	int num_failed;
	/*
	 * Protect update_ctrl. Raw spinlock as it will be acquired from
	 * interrupt-disabled contexts.
	 */
	raw_spinlock_t lock;
} update_ctrl;

/* Called with ctrl->lock held or during initialization. */
static void __set_target_state(struct update_ctrl *ctrl,
			       enum module_update_state newstate)
{
	/* Reset ack counter. */
	ctrl->num_ack = 0;
	ctrl->state = newstate;
}

/* Last one to ack a state moves to the next state. */
static void ack_state(struct update_ctrl *ctrl, int result)
{
	raw_spin_lock(&ctrl->lock);

	ctrl->num_failed += !!result;
	ctrl->num_ack++;
	if (ctrl->num_ack == num_online_cpus() && !ctrl->num_failed)
		__set_target_state(ctrl, ctrl->state + 1);

	raw_spin_unlock(&ctrl->lock);
}

static void init_state(struct update_ctrl *ctrl)
{
	raw_spin_lock_init(&ctrl->lock);
	__set_target_state(ctrl, MODULE_UPDATE_START + 1);
	ctrl->num_failed = 0;
}

/*
 * See multi_cpu_stop() from where this multi-cpu state-machine was
 * adopted.
 */
static int do_seamldr_install_module(void *seamldr_params)
{
	enum module_update_state curstate = MODULE_UPDATE_START;
	enum module_update_state newstate;
	bool is_lead_cpu = false;
	int ret = 0;

	/*
	 * Some steps must be run on exactly one CPU. Pick a "lead" CPU to
	 * execute those steps. Use CPU 0 because it is always online.
	 */
	if (smp_processor_id() == 0)
		is_lead_cpu = true;

	do {
		newstate = READ_ONCE(update_ctrl.state);

		if (curstate == newstate) {
			cpu_relax();
			continue;
		}

		curstate = newstate;
		switch (curstate) {
		case MODULE_UPDATE_SHUTDOWN:
			if (is_lead_cpu)
				ret = tdx_module_shutdown();
			break;
		case MODULE_UPDATE_CPU_INSTALL:
			ret = seamldr_install(seamldr_params);
			break;
		case MODULE_UPDATE_CPU_INIT:
			ret = tdx_cpu_enable();
			break;
		case MODULE_UPDATE_RUN_UPDATE:
			if (is_lead_cpu)
				ret = tdx_module_run_update();
			break;
		default:
			break;
		}

		ack_state(&update_ctrl, ret);
	} while (curstate != MODULE_UPDATE_DONE &&
		 !READ_ONCE(update_ctrl.num_failed));

	return ret;
}

/**
 * seamldr_install_module - Install a new TDX module.
 * @data: Pointer to the TDX module image.
 * @data_len: Size of the TDX module image.
 *
 * Returns 0 on success, negative error code on failure.
 */
int seamldr_install_module(const u8 *data, u32 data_len)
{
	struct seamldr_params *params;
	const struct tdx_image *image;
	int ret;

	/*
	 * init_seamldr_params() reads the header early.
	 * Ensure there is enough data to do at least that.
	 */
	if (data_len < TDX_IMAGE_HEADER_SIZE)
		return -EINVAL;

	image = (const struct tdx_image *)data;

	params = kzalloc_obj(*params);
	if (!params)
		return -ENOMEM;

	/* Populate 'params' from 'image'. */
	ret = init_seamldr_params(params, image, data_len);
	if (ret)
		goto out;

	/* Ensure a stable set of online CPUs for the update process. */
	cpus_read_lock();
	init_state(&update_ctrl);
	ret = stop_machine_cpuslocked(do_seamldr_install_module, params,
				      cpu_online_mask);
	cpus_read_unlock();

out:
	kfree(params);
	return ret;
}
EXPORT_SYMBOL_FOR_MODULES(seamldr_install_module, "tdx-host");

/*
 * stop_machine() does not interrupt preemption-disabled regions.
 * Simply disabling preempt prevents updates.
 */
void seamldr_lock_module_update(void)
{
	preempt_disable();
}
EXPORT_SYMBOL_FOR_MODULES(seamldr_lock_module_update, "tdx-host");

void seamldr_unlock_module_update(void)
{
	preempt_enable();
}
EXPORT_SYMBOL_FOR_MODULES(seamldr_unlock_module_update, "tdx-host");
