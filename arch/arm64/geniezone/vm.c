// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/uaccess.h>

#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch_common.h"

#define PAR_PA47_MASK ((((1UL << 48) - 1) >> 12) << 12)

int gzvm_arch_inform_exit(u16 vm_id)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(MT_HVC_GZVM_INFORM_EXIT, vm_id, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == 0)
		return 0;

	return -ENXIO;
}

int gzvm_arch_probe(void)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0)
		return -ENXIO;

	return 0;
}

int gzvm_arch_set_memregion(u16 vm_id, size_t buf_size,
			    phys_addr_t region)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_MEMREGION, vm_id,
				    buf_size, region, 0, 0, 0, 0, &res);
}

static int gzvm_cap_vm_gpa_size(void __user *argp)
{
	__u64 value = CONFIG_ARM64_PA_BITS;

	if (copy_to_user(argp, &value, sizeof(__u64)))
		return -EFAULT;

	return 0;
}

int gzvm_arch_check_extension(struct gzvm *gzvm, __u64 cap, void __user *argp)
{
	int ret;

	switch (cap) {
	case GZVM_CAP_PROTECTED_VM: {
		__u64 success = 1;

		if (copy_to_user(argp, &success, sizeof(__u64)))
			return -EFAULT;

		return 0;
	}
	case GZVM_CAP_VM_GPA_SIZE: {
		ret = gzvm_cap_vm_gpa_size(argp);
		return ret;
	}
	default:
		break;
	}

	return -EOPNOTSUPP;
}

/**
 * gzvm_arch_create_vm() - create vm
 * @vm_type: VM type. Only supports Linux VM now.
 *
 * Return:
 * * positive value	- VM ID
 * * -ENOMEM		- Memory not enough for storing VM data
 */
int gzvm_arch_create_vm(unsigned long vm_type)
{
	struct arm_smccc_res res;
	int ret;

	ret = gzvm_hypcall_wrapper(MT_HVC_GZVM_CREATE_VM, vm_type, 0, 0, 0, 0,
				   0, 0, &res);
	return ret ? ret : res.a1;
}

int gzvm_arch_destroy_vm(u16 vm_id)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_DESTROY_VM, vm_id, 0, 0, 0, 0,
				    0, 0, &res);
}

int gzvm_arch_memregion_purpose(struct gzvm *gzvm,
				struct gzvm_userspace_memory_region *mem)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_MEMREGION_PURPOSE, gzvm->vm_id,
				    mem->guest_phys_addr, mem->memory_size,
				    mem->flags, 0, 0, 0, &res);
}

int gzvm_arch_set_dtb_config(struct gzvm *gzvm, struct gzvm_dtb_config *cfg)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_SET_DTB_CONFIG, gzvm->vm_id,
				    cfg->dtb_addr, cfg->dtb_size, 0, 0, 0, 0,
				    &res);
}

static int gzvm_vm_arch_enable_cap(struct gzvm *gzvm,
				   struct gzvm_enable_cap *cap,
				   struct arm_smccc_res *res)
{
	return gzvm_hypcall_wrapper(MT_HVC_GZVM_ENABLE_CAP, gzvm->vm_id,
				    cap->cap, cap->args[0], cap->args[1],
				    cap->args[2], cap->args[3], cap->args[4],
				    res);
}

/**
 * gzvm_vm_ioctl_get_pvmfw_size() - Get pvmfw size from hypervisor, return
 *				    in x1, and return to userspace in args
 * @gzvm: Pointer to struct gzvm.
 * @cap: Pointer to struct gzvm_enable_cap.
 * @argp: Pointer to struct gzvm_enable_cap in user space.
 *
 * Return:
 * * 0			- Succeed
 * * -EINVAL		- Hypervisor return invalid results
 * * -EFAULT		- Fail to copy back to userspace buffer
 */
static int gzvm_vm_ioctl_get_pvmfw_size(struct gzvm *gzvm,
					struct gzvm_enable_cap *cap,
					void __user *argp)
{
	struct arm_smccc_res res = {0};

	if (gzvm_vm_arch_enable_cap(gzvm, cap, &res) != 0)
		return -EINVAL;

	cap->args[1] = res.a1;
	if (copy_to_user(argp, cap, sizeof(*cap)))
		return -EFAULT;

	return 0;
}

/**
 * fill_constituents() - Populate pa to buffer until full
 * @consti: Pointer to struct mem_region_addr_range.
 * @consti_cnt: Constituent count.
 * @max_nr_consti: Maximum number of constituent count.
 * @gfn: Guest frame number.
 * @total_pages: Total page numbers.
 * @slot: Pointer to struct gzvm_memslot.
 *
 * Return: how many pages we've fill in, negative if error
 */
static int fill_constituents(struct mem_region_addr_range *consti,
			     int *consti_cnt, int max_nr_consti, u64 gfn,
			     u32 total_pages, struct gzvm_memslot *slot)
{
	u64 pfn, prev_pfn, gfn_end;
	int nr_pages = 1;
	int i = 0;

	if (unlikely(total_pages == 0))
		return -EINVAL;
	gfn_end = gfn + total_pages;

	/* entry 0 */
	if (gzvm_gfn_to_pfn_memslot(slot, gfn, &pfn) != 0)
		return -EFAULT;
	consti[0].address = PFN_PHYS(pfn);
	consti[0].pg_cnt = 1;
	gfn++;
	prev_pfn = pfn;

	while (i < max_nr_consti && gfn < gfn_end) {
		if (gzvm_gfn_to_pfn_memslot(slot, gfn, &pfn) != 0)
			return -EFAULT;
		if (pfn == (prev_pfn + 1)) {
			consti[i].pg_cnt++;
		} else {
			i++;
			if (i >= max_nr_consti)
				break;
			consti[i].address = PFN_PHYS(pfn);
			consti[i].pg_cnt = 1;
		}
		prev_pfn = pfn;
		gfn++;
		nr_pages++;
	}
	if (i != max_nr_consti)
		i++;
	*consti_cnt = i;

	return nr_pages;
}

/**
 * populate_mem_region() - Iterate all mem slot and populate pa to buffer until it's full
 * @gzvm: Pointer to struct gzvm.
 *
 * Return: 0 if it is successful, negative if error
 */
static int populate_mem_region(struct gzvm *gzvm)
{
	int slot_cnt = 0;

	while (slot_cnt < GZVM_MAX_MEM_REGION && gzvm->memslot[slot_cnt].npages != 0) {
		struct gzvm_memslot *memslot = &gzvm->memslot[slot_cnt];
		struct gzvm_memory_region_ranges *region;
		int max_nr_consti, remain_pages;
		u64 gfn, gfn_end;
		u32 buf_size;

		buf_size = PAGE_SIZE * 2;
		region = alloc_pages_exact(buf_size, GFP_KERNEL);
		if (!region)
			return -ENOMEM;

		max_nr_consti = (buf_size - sizeof(*region)) /
				sizeof(struct mem_region_addr_range);

		region->slot = memslot->slot_id;
		remain_pages = memslot->npages;
		gfn = memslot->base_gfn;
		gfn_end = gfn + remain_pages;

		while (gfn < gfn_end) {
			int nr_pages;

			nr_pages = fill_constituents(region->constituents,
						     &region->constituent_cnt,
						     max_nr_consti, gfn,
						     remain_pages, memslot);

			if (nr_pages < 0) {
				pr_err("Failed to fill constituents\n");
				free_pages_exact(region, buf_size);
				return -EFAULT;
			}

			region->gpa = PFN_PHYS(gfn);
			region->total_pages = nr_pages;
			remain_pages -= nr_pages;
			gfn += nr_pages;

			if (gzvm_arch_set_memregion(gzvm->vm_id, buf_size,
						    virt_to_phys(region))) {
				pr_err("Failed to register memregion to hypervisor\n");
				free_pages_exact(region, buf_size);
				return -EFAULT;
			}
		}
		free_pages_exact(region, buf_size);
		++slot_cnt;
	}
	return 0;
}

/**
 * gzvm_vm_ioctl_cap_pvm() - Proceed GZVM_CAP_PROTECTED_VM's subcommands
 * @gzvm: Pointer to struct gzvm.
 * @cap: Pointer to struct gzvm_enable_cap.
 * @argp: Pointer to struct gzvm_enable_cap in user space.
 *
 * Return:
 * * 0			- Succeed
 * * -EINVAL		- Invalid subcommand or arguments
 */
static int gzvm_vm_ioctl_cap_pvm(struct gzvm *gzvm,
				 struct gzvm_enable_cap *cap,
				 void __user *argp)
{
	struct arm_smccc_res res = {0};
	int ret;

	switch (cap->args[0]) {
	case GZVM_CAP_PVM_SET_PVMFW_GPA:
		fallthrough;
	case GZVM_CAP_PVM_SET_PROTECTED_VM:
		/*
		 * If the hypervisor doesn't support block-based demand paging, we
		 * populate memory in advance to improve performance for protected VM.
		 */
		if (gzvm->demand_page_gran == PAGE_SIZE)
			populate_mem_region(gzvm);
		ret = gzvm_vm_arch_enable_cap(gzvm, cap, &res);
		return ret;
	case GZVM_CAP_PVM_GET_PVMFW_SIZE:
		ret = gzvm_vm_ioctl_get_pvmfw_size(gzvm, cap, argp);
		return ret;
	default:
		break;
	}

	return -EINVAL;
}

int gzvm_vm_ioctl_arch_enable_cap(struct gzvm *gzvm,
				  struct gzvm_enable_cap *cap,
				  void __user *argp)
{
	struct arm_smccc_res res = {0};
	int ret;

	switch (cap->cap) {
	case GZVM_CAP_PROTECTED_VM:
		ret = gzvm_vm_ioctl_cap_pvm(gzvm, cap, argp);
		return ret;
	case GZVM_CAP_BLOCK_BASED_DEMAND_PAGING:
		ret = gzvm_vm_arch_enable_cap(gzvm, cap, &res);
		return ret;
	default:
		break;
	}

	return -EINVAL;
}

/**
 * gzvm_hva_to_pa_arch() - converts hva to pa with arch-specific way
 * @hva: Host virtual address.
 *
 * Return: GZVM_PA_ERR_BAD for translation error
 */
u64 gzvm_hva_to_pa_arch(u64 hva)
{
	unsigned long flags;
	u64 par;

	local_irq_save(flags);
	asm volatile("at s1e1r, %0" :: "r" (hva));
	isb();
	par = read_sysreg_par();
	local_irq_restore(flags);

	if (par & SYS_PAR_EL1_F)
		return GZVM_PA_ERR_BAD;
	par = par & PAR_PA47_MASK;
	if (!par)
		return GZVM_PA_ERR_BAD;
	return par;
}

int gzvm_arch_map_guest(u16 vm_id, int memslot_id, u64 pfn, u64 gfn,
			u64 nr_pages)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_MAP_GUEST, vm_id, memslot_id,
				    pfn, gfn, nr_pages, 0, 0, &res);
}

int gzvm_arch_map_guest_block(u16 vm_id, int memslot_id, u64 gfn, u64 nr_pages)
{
	struct arm_smccc_res res;

	return gzvm_hypcall_wrapper(MT_HVC_GZVM_MAP_GUEST_BLOCK, vm_id,
				    memslot_id, gfn, nr_pages, 0, 0, 0, &res);
}
