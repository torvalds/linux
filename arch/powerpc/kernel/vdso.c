// SPDX-License-Identifier: GPL-2.0-or-later

/*
 *    Copyright (C) 2004 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/vdso_datastore.h>
#include <vdso/datapage.h>

#include <asm/syscall.h>
#include <asm/syscalls.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/firmware.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/setup.h>

static_assert(__VDSO_PAGES == VDSO_NR_PAGES);

/* The alignment of the vDSO */
#define VDSO_ALIGNMENT	(1 << 16)

extern char vdso32_start, vdso32_end;
extern char vdso64_start, vdso64_end;

static int vdso_mremap(const struct vm_special_mapping *sm, struct vm_area_struct *new_vma,
		       unsigned long text_size)
{
	unsigned long new_size = new_vma->vm_end - new_vma->vm_start;

	if (new_size != text_size)
		return -EINVAL;

	current->mm->context.vdso = (void __user *)new_vma->vm_start;

	return 0;
}

static int vdso32_mremap(const struct vm_special_mapping *sm, struct vm_area_struct *new_vma)
{
	return vdso_mremap(sm, new_vma, &vdso32_end - &vdso32_start);
}

static int vdso64_mremap(const struct vm_special_mapping *sm, struct vm_area_struct *new_vma)
{
	return vdso_mremap(sm, new_vma, &vdso64_end - &vdso64_start);
}

static void vdso_close(const struct vm_special_mapping *sm, struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;

	/*
	 * close() is called for munmap() but also for mremap(). In the mremap()
	 * case the vdso pointer has already been updated by the mremap() hook
	 * above, so it must not be set to NULL here.
	 */
	if (vma->vm_start != (unsigned long)mm->context.vdso)
		return;

	mm->context.vdso = NULL;
}

static struct vm_special_mapping vdso32_spec __ro_after_init = {
	.name = "[vdso]",
	.mremap = vdso32_mremap,
	.close = vdso_close,
};

static struct vm_special_mapping vdso64_spec __ro_after_init = {
	.name = "[vdso]",
	.mremap = vdso64_mremap,
	.close = vdso_close,
};

/*
 * This is called from binfmt_elf, we create the special vma for the
 * vDSO and insert it into the mm struct tree
 */
static int __arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	unsigned long vdso_size, vdso_base, mappings_size;
	struct vm_special_mapping *vdso_spec;
	unsigned long vvar_size = VDSO_NR_PAGES * PAGE_SIZE;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	if (is_32bit_task()) {
		vdso_spec = &vdso32_spec;
		vdso_size = &vdso32_end - &vdso32_start;
	} else {
		vdso_spec = &vdso64_spec;
		vdso_size = &vdso64_end - &vdso64_start;
	}

	mappings_size = vdso_size + vvar_size;
	mappings_size += (VDSO_ALIGNMENT - 1) & PAGE_MASK;

	/*
	 * Pick a base address for the vDSO in process space.
	 * Add enough to the size so that the result can be aligned.
	 */
	vdso_base = get_unmapped_area(NULL, 0, mappings_size, 0, 0);
	if (IS_ERR_VALUE(vdso_base))
		return vdso_base;

	/* Add required alignment. */
	vdso_base = ALIGN(vdso_base, VDSO_ALIGNMENT);

	vma = vdso_install_vvar_mapping(mm, vdso_base);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	/*
	 * our vma flags don't have VM_WRITE so by default, the process isn't
	 * allowed to write those pages.
	 * gdb can break that with ptrace interface, and thus trigger COW on
	 * those pages but it's then your responsibility to never do that on
	 * the "data" page of the vDSO or you'll stop getting kernel updates
	 * and your nice userland gettimeofday will be totally dead.
	 * It's fine to use that for setting breakpoints in the vDSO code
	 * pages though.
	 */
	vma = _install_special_mapping(mm, vdso_base + vvar_size, vdso_size,
				       VM_READ | VM_EXEC | VM_MAYREAD |
				       VM_MAYWRITE | VM_MAYEXEC, vdso_spec);
	if (IS_ERR(vma)) {
		do_munmap(mm, vdso_base, vvar_size, NULL);
		return PTR_ERR(vma);
	}

	// Now that the mappings are in place, set the mm VDSO pointer
	mm->context.vdso = (void __user *)vdso_base + vvar_size;

	return 0;
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int rc;

	mm->context.vdso = NULL;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	rc = __arch_setup_additional_pages(bprm, uses_interp);

	mmap_write_unlock(mm);
	return rc;
}

#define VDSO_DO_FIXUPS(type, value, bits, sec) do {					\
	void *__start = (void *)VDSO##bits##_SYMBOL(&vdso##bits##_start, sec##_start);	\
	void *__end = (void *)VDSO##bits##_SYMBOL(&vdso##bits##_start, sec##_end);	\
											\
	do_##type##_fixups((value), __start, __end);					\
} while (0)

static void __init vdso_fixup_features(void)
{
#ifdef CONFIG_PPC64
	VDSO_DO_FIXUPS(feature, cur_cpu_spec->cpu_features, 64, ftr_fixup);
	VDSO_DO_FIXUPS(feature, cur_cpu_spec->mmu_features, 64, mmu_ftr_fixup);
	VDSO_DO_FIXUPS(feature, powerpc_firmware_features, 64, fw_ftr_fixup);
	VDSO_DO_FIXUPS(lwsync, cur_cpu_spec->cpu_features, 64, lwsync_fixup);
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_VDSO32
	VDSO_DO_FIXUPS(feature, cur_cpu_spec->cpu_features, 32, ftr_fixup);
	VDSO_DO_FIXUPS(feature, cur_cpu_spec->mmu_features, 32, mmu_ftr_fixup);
#ifdef CONFIG_PPC64
	VDSO_DO_FIXUPS(feature, powerpc_firmware_features, 32, fw_ftr_fixup);
#endif /* CONFIG_PPC64 */
	VDSO_DO_FIXUPS(lwsync, cur_cpu_spec->cpu_features, 32, lwsync_fixup);
#endif
}

/*
 * Called from setup_arch to initialize the bitmap of available
 * syscalls in the systemcfg page
 */
static void __init vdso_setup_syscall_map(void)
{
	unsigned int i;

	for (i = 0; i < NR_syscalls; i++) {
		if (sys_call_table[i] != (void *)&sys_ni_syscall)
			vdso_k_arch_data->syscall_map[i >> 5] |= 0x80000000UL >> (i & 0x1f);
		if (IS_ENABLED(CONFIG_COMPAT) &&
		    compat_sys_call_table[i] != (void *)&sys_ni_syscall)
			vdso_k_arch_data->compat_syscall_map[i >> 5] |= 0x80000000UL >> (i & 0x1f);
	}
}

#ifdef CONFIG_PPC64
int vdso_getcpu_init(void)
{
	unsigned long cpu, node, val;

	/*
	 * SPRG_VDSO contains the CPU in the bottom 16 bits and the NUMA node
	 * in the next 16 bits.  The VDSO uses this to implement getcpu().
	 */
	cpu = get_cpu();
	WARN_ON_ONCE(cpu > 0xffff);

	node = cpu_to_node(cpu);
	WARN_ON_ONCE(node > 0xffff);

	val = (cpu & 0xffff) | ((node & 0xffff) << 16);
	mtspr(SPRN_SPRG_VDSO_WRITE, val);
	get_paca()->sprg_vdso = val;

	put_cpu();

	return 0;
}
/* We need to call this before SMP init */
early_initcall(vdso_getcpu_init);
#endif

static struct page ** __init vdso_setup_pages(void *start, void *end)
{
	int i;
	struct page **pagelist;
	int pages = (end - start) >> PAGE_SHIFT;

	pagelist = kcalloc(pages + 1, sizeof(struct page *), GFP_KERNEL);
	if (!pagelist)
		panic("%s: Cannot allocate page list for VDSO", __func__);

	for (i = 0; i < pages; i++)
		pagelist[i] = virt_to_page(start + i * PAGE_SIZE);

	return pagelist;
}

static int __init vdso_init(void)
{
#ifdef CONFIG_PPC64
	vdso_k_arch_data->dcache_block_size = ppc64_caches.l1d.block_size;
	vdso_k_arch_data->icache_block_size = ppc64_caches.l1i.block_size;
	vdso_k_arch_data->dcache_log_block_size = ppc64_caches.l1d.log_block_size;
	vdso_k_arch_data->icache_log_block_size = ppc64_caches.l1i.log_block_size;
#endif /* CONFIG_PPC64 */

	vdso_setup_syscall_map();

	vdso_fixup_features();

	if (IS_ENABLED(CONFIG_VDSO32))
		vdso32_spec.pages = vdso_setup_pages(&vdso32_start, &vdso32_end);

	if (IS_ENABLED(CONFIG_PPC64))
		vdso64_spec.pages = vdso_setup_pages(&vdso64_start, &vdso64_end);

	smp_wmb();

	return 0;
}
arch_initcall(vdso_init);
