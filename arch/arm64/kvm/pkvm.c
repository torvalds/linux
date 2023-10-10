// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <linux/io.h>
#include <linux/kmemleak.h>
#include <linux/kvm_host.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/sort.h>

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>
#include <asm/kvm_pkvm_module.h>
#include <asm/setup.h>

#include "hyp_constants.h"

DEFINE_STATIC_KEY_FALSE(kvm_protected_mode_initialized);

static struct reserved_mem *pkvm_firmware_mem;
static phys_addr_t *pvmfw_base = &kvm_nvhe_sym(pvmfw_base);
static phys_addr_t *pvmfw_size = &kvm_nvhe_sym(pvmfw_size);

static struct pkvm_moveable_reg *moveable_regs = kvm_nvhe_sym(pkvm_moveable_regs);
static struct memblock_region *hyp_memory = kvm_nvhe_sym(hyp_memory);
static unsigned int *hyp_memblock_nr_ptr = &kvm_nvhe_sym(hyp_memblock_nr);

phys_addr_t hyp_mem_base;
phys_addr_t hyp_mem_size;

static int cmp_hyp_memblock(const void *p1, const void *p2)
{
	const struct memblock_region *r1 = p1;
	const struct memblock_region *r2 = p2;

	return r1->base < r2->base ? -1 : (r1->base > r2->base);
}

static void __init sort_memblock_regions(void)
{
	sort(hyp_memory,
	     *hyp_memblock_nr_ptr,
	     sizeof(struct memblock_region),
	     cmp_hyp_memblock,
	     NULL);
}

static int __init register_memblock_regions(void)
{
	struct memblock_region *reg;

	for_each_mem_region(reg) {
		if (*hyp_memblock_nr_ptr >= HYP_MEMBLOCK_REGIONS)
			return -ENOMEM;

		hyp_memory[*hyp_memblock_nr_ptr] = *reg;
		(*hyp_memblock_nr_ptr)++;
	}
	sort_memblock_regions();

	return 0;
}

static int cmp_moveable_reg(const void *p1, const void *p2)
{
	const struct pkvm_moveable_reg *r1 = p1;
	const struct pkvm_moveable_reg *r2 = p2;

	/*
	 * Moveable regions may overlap, so put the largest one first when start
	 * addresses are equal to allow a simpler walk from e.g.
	 * host_stage2_unmap_unmoveable_regs().
	 */
	if (r1->start < r2->start)
		return -1;
	else if (r1->start > r2->start)
		return 1;
	else if (r1->size > r2->size)
		return -1;
	else if (r1->size < r2->size)
		return 1;
	return 0;
}

static void __init sort_moveable_regs(void)
{
	sort(moveable_regs,
	     kvm_nvhe_sym(pkvm_moveable_regs_nr),
	     sizeof(struct pkvm_moveable_reg),
	     cmp_moveable_reg,
	     NULL);
}

static int __init register_moveable_regions(void)
{
	struct memblock_region *reg;
	struct device_node *np;
	int i = 0;

	for_each_mem_region(reg) {
		if (i >= PKVM_NR_MOVEABLE_REGS)
			return -ENOMEM;
		moveable_regs[i].start = reg->base;
		moveable_regs[i].size = reg->size;
		moveable_regs[i].type = PKVM_MREG_MEMORY;
		i++;
	}

	for_each_compatible_node(np, NULL, "pkvm,protected-region") {
		struct resource res;
		u64 start, size;
		int ret;

		if (i >= PKVM_NR_MOVEABLE_REGS)
			return -ENOMEM;

		ret = of_address_to_resource(np, 0, &res);
		if (ret)
			return ret;

		start = res.start;
		size = resource_size(&res);
		if (!PAGE_ALIGNED(start) || !PAGE_ALIGNED(size))
			return -EINVAL;

		moveable_regs[i].start = start;
		moveable_regs[i].size = size;
		moveable_regs[i].type = PKVM_MREG_PROTECTED_RANGE;
		i++;
	}

	kvm_nvhe_sym(pkvm_moveable_regs_nr) = i;
	sort_moveable_regs();

	return 0;
}

void __init kvm_hyp_reserve(void)
{
	u64 hyp_mem_pages = 0;
	int ret;

	if (!is_hyp_mode_available() || is_kernel_in_hyp_mode())
		return;

	if (kvm_get_mode() != KVM_MODE_PROTECTED)
		return;

	ret = register_memblock_regions();
	if (ret) {
		*hyp_memblock_nr_ptr = 0;
		kvm_err("Failed to register hyp memblocks: %d\n", ret);
		return;
	}

	ret = register_moveable_regions();
	if (ret) {
		*hyp_memblock_nr_ptr = 0;
		kvm_err("Failed to register pkvm moveable regions: %d\n", ret);
		return;
	}

	hyp_mem_pages += hyp_s1_pgtable_pages();
	hyp_mem_pages += host_s2_pgtable_pages();
	hyp_mem_pages += hyp_vm_table_pages();
	hyp_mem_pages += hyp_vmemmap_pages(STRUCT_HYP_PAGE_SIZE);
	hyp_mem_pages += hyp_ffa_proxy_pages();

	/*
	 * Try to allocate a PMD-aligned region to reduce TLB pressure once
	 * this is unmapped from the host stage-2, and fallback to PAGE_SIZE.
	 */
	hyp_mem_size = hyp_mem_pages << PAGE_SHIFT;
	hyp_mem_base = memblock_phys_alloc(ALIGN(hyp_mem_size, PMD_SIZE),
					   PMD_SIZE);
	if (!hyp_mem_base)
		hyp_mem_base = memblock_phys_alloc(hyp_mem_size, PAGE_SIZE);
	else
		hyp_mem_size = ALIGN(hyp_mem_size, PMD_SIZE);

	if (!hyp_mem_base) {
		kvm_err("Failed to reserve hyp memory\n");
		return;
	}

	kvm_info("Reserved %lld MiB at 0x%llx\n", hyp_mem_size >> 20,
		 hyp_mem_base);
}

/*
 * Allocates and donates memory for hypervisor VM structs at EL2.
 *
 * Allocates space for the VM state, which includes the hyp vm as well as
 * the hyp vcpus.
 *
 * Stores an opaque handler in the kvm struct for future reference.
 *
 * Return 0 on success, negative error code on failure.
 */
static int __pkvm_create_hyp_vm(struct kvm *host_kvm)
{
	size_t pgd_sz, hyp_vm_sz, hyp_vcpu_sz, last_ran_sz, total_sz;
	struct kvm_vcpu *host_vcpu;
	pkvm_handle_t handle;
	void *pgd, *hyp_vm, *last_ran;
	unsigned long idx;
	int ret;

	if (host_kvm->created_vcpus < 1)
		return -EINVAL;

	pgd_sz = kvm_pgtable_stage2_pgd_size(host_kvm->arch.vtcr);

	/*
	 * The PGD pages will be reclaimed using a hyp_memcache which implies
	 * page granularity. So, use alloc_pages_exact() to get individual
	 * refcounts.
	 */
	pgd = alloc_pages_exact(pgd_sz, GFP_KERNEL_ACCOUNT);
	if (!pgd)
		return -ENOMEM;

	/* Allocate memory to donate to hyp for vm and vcpu pointers. */
	hyp_vm_sz = PAGE_ALIGN(size_add(PKVM_HYP_VM_SIZE,
					size_mul(sizeof(void *),
						 host_kvm->created_vcpus)));
	hyp_vm = alloc_pages_exact(hyp_vm_sz, GFP_KERNEL_ACCOUNT);
	if (!hyp_vm) {
		ret = -ENOMEM;
		goto free_pgd;
	}

	/* Allocate memory to donate to hyp for tracking mmu->last_vcpu_ran. */
	last_ran_sz = PAGE_ALIGN(array_size(num_possible_cpus(), sizeof(int)));
	last_ran = alloc_pages_exact(last_ran_sz, GFP_KERNEL_ACCOUNT);
	if (!last_ran) {
		ret = -ENOMEM;
		goto free_vm;
	}

	/* Donate the VM memory to hyp and let hyp initialize it. */
	ret = kvm_call_hyp_nvhe(__pkvm_init_vm, host_kvm, hyp_vm, pgd, last_ran);
	if (ret < 0)
		goto free_last_ran;

	handle = ret;

	host_kvm->arch.pkvm.handle = handle;

	total_sz = hyp_vm_sz + last_ran_sz + pgd_sz;

	/* Donate memory for the vcpus at hyp and initialize it. */
	hyp_vcpu_sz = PAGE_ALIGN(PKVM_HYP_VCPU_SIZE);
	kvm_for_each_vcpu(idx, host_vcpu, host_kvm) {
		void *hyp_vcpu;

		/* Indexing of the vcpus to be sequential starting at 0. */
		if (WARN_ON(host_vcpu->vcpu_idx != idx)) {
			ret = -EINVAL;
			goto destroy_vm;
		}

		hyp_vcpu = alloc_pages_exact(hyp_vcpu_sz, GFP_KERNEL_ACCOUNT);
		if (!hyp_vcpu) {
			ret = -ENOMEM;
			goto destroy_vm;
		}

		total_sz += hyp_vcpu_sz;

		ret = kvm_call_hyp_nvhe(__pkvm_init_vcpu, handle, host_vcpu,
					hyp_vcpu);
		if (ret) {
			free_pages_exact(hyp_vcpu, hyp_vcpu_sz);
			goto destroy_vm;
		}
	}

	atomic64_set(&host_kvm->stat.protected_hyp_mem, total_sz);
	kvm_account_pgtable_pages(pgd, pgd_sz >> PAGE_SHIFT);

	return 0;

destroy_vm:
	pkvm_destroy_hyp_vm(host_kvm);
	return ret;
free_last_ran:
	free_pages_exact(last_ran, last_ran_sz);
free_vm:
	free_pages_exact(hyp_vm, hyp_vm_sz);
free_pgd:
	free_pages_exact(pgd, pgd_sz);
	return ret;
}

int pkvm_create_hyp_vm(struct kvm *host_kvm)
{
	int ret = 0;

	mutex_lock(&host_kvm->lock);
	if (!host_kvm->arch.pkvm.handle)
		ret = __pkvm_create_hyp_vm(host_kvm);
	mutex_unlock(&host_kvm->lock);

	return ret;
}

void pkvm_destroy_hyp_vm(struct kvm *host_kvm)
{
	struct kvm_pinned_page *ppage;
	struct mm_struct *mm = current->mm;
	struct rb_node *node;

	if (!host_kvm->arch.pkvm.handle)
		goto out_free;

	WARN_ON(kvm_call_hyp_nvhe(__pkvm_start_teardown_vm, host_kvm->arch.pkvm.handle));

	node = rb_first(&host_kvm->arch.pkvm.pinned_pages);
	while (node) {
		ppage = rb_entry(node, struct kvm_pinned_page, node);
		WARN_ON(kvm_call_hyp_nvhe(__pkvm_reclaim_dying_guest_page,
					  host_kvm->arch.pkvm.handle,
					  page_to_pfn(ppage->page),
					  ppage->ipa));
		cond_resched();

		account_locked_vm(mm, 1, false);
		unpin_user_pages_dirty_lock(&ppage->page, 1, true);
		node = rb_next(node);
		rb_erase(&ppage->node, &host_kvm->arch.pkvm.pinned_pages);
		kfree(ppage);
	}

	WARN_ON(kvm_call_hyp_nvhe(__pkvm_finalize_teardown_vm, host_kvm->arch.pkvm.handle));

out_free:
	host_kvm->arch.pkvm.handle = 0;
	free_hyp_memcache(&host_kvm->arch.pkvm.teardown_mc, host_kvm);
	free_hyp_stage2_memcache(&host_kvm->arch.pkvm.teardown_stage2_mc,
				 host_kvm);
}

int pkvm_init_host_vm(struct kvm *host_kvm, unsigned long type)
{
	mutex_init(&host_kvm->lock);

	if (!(type & KVM_VM_TYPE_ARM_PROTECTED))
		return 0;

	if (!is_protected_kvm_enabled())
		return -EINVAL;

	host_kvm->arch.pkvm.pvmfw_load_addr = PVMFW_INVALID_LOAD_ADDR;
	host_kvm->arch.pkvm.enabled = true;
	return 0;
}

static int rb_ppage_cmp(const void *key, const struct rb_node *node)
{
       struct kvm_pinned_page *p = container_of(node, struct kvm_pinned_page, node);
       phys_addr_t ipa = (phys_addr_t)key;

       return (ipa < p->ipa) ? -1 : (ipa > p->ipa);
}

void pkvm_host_reclaim_page(struct kvm *host_kvm, phys_addr_t ipa)
{
	struct kvm_pinned_page *ppage;
	struct mm_struct *mm = current->mm;
	struct rb_node *node;

	write_lock(&host_kvm->mmu_lock);
	node = rb_find((void *)ipa, &host_kvm->arch.pkvm.pinned_pages,
		       rb_ppage_cmp);
	if (node)
		rb_erase(node, &host_kvm->arch.pkvm.pinned_pages);
	write_unlock(&host_kvm->mmu_lock);

	WARN_ON(!node);
	if (!node)
		return;

	ppage = container_of(node, struct kvm_pinned_page, node);
	account_locked_vm(mm, 1, false);
	unpin_user_pages_dirty_lock(&ppage->page, 1, true);
	kfree(ppage);
}

static int __init pkvm_firmware_rmem_err(struct reserved_mem *rmem,
					 const char *reason)
{
	phys_addr_t end = rmem->base + rmem->size;

	kvm_err("Ignoring pkvm guest firmware memory reservation [%pa - %pa]: %s\n",
		&rmem->base, &end, reason);
	return -EINVAL;
}

static int __init pkvm_firmware_rmem_init(struct reserved_mem *rmem)
{
	unsigned long node = rmem->fdt_node;

	if (pkvm_firmware_mem)
		return pkvm_firmware_rmem_err(rmem, "duplicate reservation");

	if (!of_get_flat_dt_prop(node, "no-map", NULL))
		return pkvm_firmware_rmem_err(rmem, "missing \"no-map\" property");

	if (of_get_flat_dt_prop(node, "reusable", NULL))
		return pkvm_firmware_rmem_err(rmem, "\"reusable\" property unsupported");

	if (!PAGE_ALIGNED(rmem->base))
		return pkvm_firmware_rmem_err(rmem, "base is not page-aligned");

	if (!PAGE_ALIGNED(rmem->size))
		return pkvm_firmware_rmem_err(rmem, "size is not page-aligned");

	*pvmfw_size = rmem->size;
	*pvmfw_base = rmem->base;
	pkvm_firmware_mem = rmem;
	return 0;
}
RESERVEDMEM_OF_DECLARE(pkvm_firmware, "linux,pkvm-guest-firmware-memory",
		       pkvm_firmware_rmem_init);

static int __init pkvm_firmware_rmem_clear(void)
{
	void *addr;
	phys_addr_t size;

	if (likely(!pkvm_firmware_mem))
		return 0;

	kvm_info("Clearing unused pKVM firmware memory\n");
	size = pkvm_firmware_mem->size;
	addr = memremap(pkvm_firmware_mem->base, size, MEMREMAP_WB);
	if (!addr)
		return -EINVAL;

	memset(addr, 0, size);
	/* Clear so user space doesn't get stale info via IOCTL. */
	pkvm_firmware_mem = NULL;

	dcache_clean_poc((unsigned long)addr, (unsigned long)addr + size);
	memunmap(addr);
	return 0;
}

static void _kvm_host_prot_finalize(void *arg)
{
	int *err = arg;

	if (WARN_ON(kvm_call_hyp_nvhe(__pkvm_prot_finalize)))
		WRITE_ONCE(*err, -EINVAL);
}

static int pkvm_drop_host_privileges(void)
{
	int ret = 0;

	/*
	 * Flip the static key upfront as that may no longer be possible
	 * once the host stage 2 is installed.
	 */
	static_branch_enable(&kvm_protected_mode_initialized);

	/*
	 * Fixup the boot mode so that we don't take spurious round
	 * trips via EL2 on cpu_resume. Flush to the PoC for a good
	 * measure, so that it can be observed by a CPU coming out of
	 * suspend with the MMU off.
	 */
	__boot_cpu_mode[0] = __boot_cpu_mode[1] = BOOT_CPU_MODE_EL1;
	dcache_clean_poc((unsigned long)__boot_cpu_mode,
			 (unsigned long)(__boot_cpu_mode + 2));

	on_each_cpu(_kvm_host_prot_finalize, &ret, 1);
	return ret;
}

static int __init finalize_pkvm(void)
{
	int ret;

	if (!is_protected_kvm_enabled()) {
		pkvm_firmware_rmem_clear();
		return 0;
	}

	/*
	 * Modules can play an essential part in the pKVM protection. All of
	 * them must properly load to enable protected VMs.
	 */
	if (pkvm_load_early_modules())
		pkvm_firmware_rmem_clear();

	/*
	 * Exclude HYP sections from kmemleak so that they don't get peeked
	 * at, which would end badly once inaccessible.
	 */
	kmemleak_free_part(__hyp_bss_start, __hyp_bss_end - __hyp_bss_start);
	kmemleak_free_part(__hyp_data_start, __hyp_data_end - __hyp_data_start);
	kmemleak_free_part_phys(hyp_mem_base, hyp_mem_size);

	flush_deferred_probe_now();

	/* If no DMA protection. */
	if (!pkvm_iommu_finalized())
		pkvm_firmware_rmem_clear();

	ret = pkvm_drop_host_privileges();
	if (ret) {
		pr_err("Failed to de-privilege the host kernel: %d\n", ret);
		pkvm_firmware_rmem_clear();
	}

#ifdef CONFIG_ANDROID_ARM64_WORKAROUND_DMA_BEYOND_POC
	if (!ret)
		ret = pkvm_register_early_nc_mappings();
#endif

	return ret;
}
device_initcall_sync(finalize_pkvm);

static int pkvm_vm_ioctl_set_fw_ipa(struct kvm *kvm, u64 ipa)
{
	int ret = 0;

	if (!pkvm_firmware_mem)
		return -EINVAL;

	mutex_lock(&kvm->lock);
	if (kvm->arch.pkvm.handle) {
		ret = -EBUSY;
		goto out_unlock;
	}

	kvm->arch.pkvm.pvmfw_load_addr = ipa;
out_unlock:
	mutex_unlock(&kvm->lock);
	return ret;
}

static int pkvm_vm_ioctl_info(struct kvm *kvm,
			      struct kvm_protected_vm_info __user *info)
{
	struct kvm_protected_vm_info kinfo = {
		.firmware_size = pkvm_firmware_mem ?
				 pkvm_firmware_mem->size :
				 0,
	};

	return copy_to_user(info, &kinfo, sizeof(kinfo)) ? -EFAULT : 0;
}

int pkvm_vm_ioctl_enable_cap(struct kvm *kvm, struct kvm_enable_cap *cap)
{
	if (!kvm_vm_is_protected(kvm))
		return -EINVAL;

	if (cap->args[1] || cap->args[2] || cap->args[3])
		return -EINVAL;

	switch (cap->flags) {
	case KVM_CAP_ARM_PROTECTED_VM_FLAGS_SET_FW_IPA:
		return pkvm_vm_ioctl_set_fw_ipa(kvm, cap->args[0]);
	case KVM_CAP_ARM_PROTECTED_VM_FLAGS_INFO:
		return pkvm_vm_ioctl_info(kvm, (void __force __user *)cap->args[0]);
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_MODULES
static char early_pkvm_modules[COMMAND_LINE_SIZE] __initdata;

static int __init early_pkvm_modules_cfg(char *arg)
{
	/*
	 * Loading pKVM modules with kvm-arm.protected_modules is deprecated
	 * Use kvm-arm.protected_modules=<module1>,<module2>
	 */
	if (!arg)
		return -EINVAL;

	strscpy(early_pkvm_modules, arg, COMMAND_LINE_SIZE);

	return 0;
}
early_param("kvm-arm.protected_modules", early_pkvm_modules_cfg);

static void free_modprobe_argv(struct subprocess_info *info)
{
	kfree(info->argv);
}

/*
 * Heavily inspired by request_module(). The latest couldn't be reused though as
 * the feature can be disabled depending on umh configuration. Here some
 * security is enforced by making sure this can be called only when pKVM is
 * enabled, not yet completely initialized.
 */
static int __init __pkvm_request_early_module(char *module_name,
					      char *module_path)
{
	char *modprobe_path = CONFIG_MODPROBE_PATH;
	struct subprocess_info *info;
	static char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
		NULL
	};
	char **argv;
	int idx = 0;

	if (!is_protected_kvm_enabled())
		return -EACCES;

	if (static_branch_likely(&kvm_protected_mode_initialized))
		return -EACCES;

	argv = kmalloc(sizeof(char *) * 7, GFP_KERNEL);
	if (!argv)
		return -ENOMEM;

	argv[idx++] = modprobe_path;
	argv[idx++] = "-q";
	if (*module_path != '\0') {
		argv[idx++] = "-d";
		argv[idx++] = module_path;
	}
	argv[idx++] = "--";
	argv[idx++] = module_name;
	argv[idx++] = NULL;

	info = call_usermodehelper_setup(modprobe_path, argv, envp, GFP_KERNEL,
					 NULL, free_modprobe_argv, NULL);
	if (!info)
		goto err;

	/* Even with CONFIG_STATIC_USERMODEHELPER we really want this path */
	info->path = modprobe_path;

	return call_usermodehelper_exec(info, UMH_WAIT_PROC | UMH_KILLABLE);
err:
	kfree(argv);

	return -ENOMEM;
}

static int __init pkvm_request_early_module(char *module_name, char *module_path)
{
	int err = __pkvm_request_early_module(module_name, module_path);

	if (!err)
		return 0;

	/* Already tried the default path */
	if (*module_path == '\0')
		return err;

	pr_info("loading %s from %s failed, fallback to the default path\n",
		module_name, module_path);

	return __pkvm_request_early_module(module_name, "");
}

int __init pkvm_load_early_modules(void)
{
	char *token, *buf = early_pkvm_modules;
	char *module_path = CONFIG_PKVM_MODULE_PATH;
	int err;

	while (true) {
		token = strsep(&buf, ",");

		if (!token)
			break;

		if (*token) {
			err = pkvm_request_early_module(token, module_path);
			if (err) {
				pr_err("Failed to load pkvm module %s: %d\n",
				       token, err);
				return err;
			}
		}

		if (buf)
			*(buf - 1) = ',';
	}

	return 0;
}

struct pkvm_mod_sec_mapping {
	struct pkvm_module_section *sec;
	enum kvm_pgtable_prot prot;
};

static void pkvm_unmap_module_pages(void *kern_va, void *hyp_va, size_t size)
{
	size_t offset;
	u64 pfn;

	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(kern_va + offset);
		kvm_call_hyp_nvhe(__pkvm_unmap_module_page, pfn,
				  hyp_va + offset);
	}
}

static void pkvm_unmap_module_sections(struct pkvm_mod_sec_mapping *secs_map, void *hyp_va_base, int nr_secs)
{
	size_t offset, size;
	void *start;
	int i;

	for (i = 0; i < nr_secs; i++) {
		start = secs_map[i].sec->start;
		size = secs_map[i].sec->end - start;
		offset = start - secs_map[0].sec->start;
		pkvm_unmap_module_pages(start, hyp_va_base + offset, size);
	}
}

static int pkvm_map_module_section(struct pkvm_mod_sec_mapping *sec_map, void *hyp_va)
{
	size_t offset, size = sec_map->sec->end - sec_map->sec->start;
	int ret;
	u64 pfn;

	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		pfn = vmalloc_to_pfn(sec_map->sec->start + offset);
		ret = kvm_call_hyp_nvhe(__pkvm_map_module_page, pfn,
					hyp_va + offset, sec_map->prot);
		if (ret) {
			pkvm_unmap_module_pages(sec_map->sec->start, hyp_va, offset);
			return ret;
		}
	}

	return 0;
}

static int pkvm_map_module_sections(struct pkvm_mod_sec_mapping *secs_map, void *hyp_va_base, int nr_secs)
{
	size_t offset;
	int i, ret;

	for (i = 0; i < nr_secs; i++) {
		offset = secs_map[i].sec->start - secs_map[0].sec->start;
		ret = pkvm_map_module_section(&secs_map[i], hyp_va_base + offset);
		if (ret) {
			pkvm_unmap_module_sections(secs_map, hyp_va_base, i);
			return ret;
		}
	}

	return 0;
}

static int __pkvm_cmp_mod_sec(const void *p1, const void *p2)
{
	struct pkvm_mod_sec_mapping const *s1 = p1;
	struct pkvm_mod_sec_mapping const *s2 = p2;

	return s1->sec->start < s2->sec->start ? -1 : s1->sec->start > s2->sec->start;
}

int __pkvm_load_el2_module(struct module *this, unsigned long *token)
{
	struct pkvm_el2_module *mod = &this->arch.hyp;
	struct pkvm_mod_sec_mapping secs_map[] = {
		{ &mod->text, KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_X },
		{ &mod->bss, KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_W },
		{ &mod->rodata, KVM_PGTABLE_PROT_R },
		{ &mod->data, KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_W },
	};
	void *start, *end, *hyp_va;
	struct arm_smccc_res res;
	kvm_nvhe_reloc_t *endrel;
	int ret, i, secs_first;
	size_t offset, size;

	/* The pKVM hyp only allows loading before it is fully initialized */
	if (!is_protected_kvm_enabled() || is_pkvm_initialized())
		return -EOPNOTSUPP;

	for (i = 0; i < ARRAY_SIZE(secs_map); i++) {
		if (!PAGE_ALIGNED(secs_map[i].sec->start)) {
			kvm_err("EL2 sections are not page-aligned\n");
			return -EINVAL;
		}
	}

	if (!try_module_get(this)) {
		kvm_err("Kernel module has been unloaded\n");
		return -ENODEV;
	}

	/* Missing or empty module sections are placed first */
	sort(secs_map, ARRAY_SIZE(secs_map), sizeof(secs_map[0]), __pkvm_cmp_mod_sec, NULL);
	for (secs_first = 0; secs_first < ARRAY_SIZE(secs_map); secs_first++) {
		start = secs_map[secs_first].sec->start;
		if (start)
			break;
	}
	end = secs_map[ARRAY_SIZE(secs_map) - 1].sec->end;
	size = end - start;

	arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(__pkvm_alloc_module_va),
			  size >> PAGE_SHIFT, &res);
	if (res.a0 != SMCCC_RET_SUCCESS || !res.a1) {
		kvm_err("Failed to allocate hypervisor VA space for EL2 module\n");
		module_put(this);
		return res.a0 == SMCCC_RET_SUCCESS ? -ENOMEM : -EPERM;
	}
	hyp_va = (void *)res.a1;

	/*
	 * The token can be used for other calls related to this module.
	 * Conveniently the only information needed is this addr so let's use it
	 * as an identifier.
	 */
	if (token)
		*token = (unsigned long)hyp_va;

	endrel = (void *)mod->relocs + mod->nr_relocs * sizeof(*endrel);
	kvm_apply_hyp_module_relocations(start, hyp_va, mod->relocs, endrel);

	/*
	 * Exclude EL2 module sections from kmemleak before making them
	 * inaccessible.
	 */
	kmemleak_free_part(start, size);

	ret = pkvm_map_module_sections(secs_map + secs_first, hyp_va,
				       ARRAY_SIZE(secs_map) - secs_first);
	if (ret) {
		kvm_err("Failed to map EL2 module page: %d\n", ret);
		module_put(this);
		return ret;
	}

	offset = (size_t)((void *)mod->init - start);
	ret = kvm_call_hyp_nvhe(__pkvm_init_module, hyp_va + offset);
	if (ret) {
		kvm_err("Failed to init EL2 module: %d\n", ret);
		pkvm_unmap_module_sections(secs_map, hyp_va, ARRAY_SIZE(secs_map));
		module_put(this);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(__pkvm_load_el2_module);

int __pkvm_register_el2_call(unsigned long hfn_hyp_va)
{
	return kvm_call_hyp_nvhe(__pkvm_register_hcall, hfn_hyp_va);
}
EXPORT_SYMBOL(__pkvm_register_el2_call);
#endif /* CONFIG_MODULES */
