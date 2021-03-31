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
#include <linux/memblock.h>
#include <linux/syscalls.h>
#include <vdso/datapage.h>

#include <asm/syscall.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/firmware.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/setup.h>

/* The alignment of the vDSO */
#define VDSO_ALIGNMENT	(1 << 16)

extern char vdso32_start, vdso32_end;
extern char vdso64_start, vdso64_end;

/*
 * The vdso data page (aka. systemcfg for old ppc64 fans) is here.
 * Once the early boot kernel code no longer needs to muck around
 * with it, it will become dynamically allocated
 */
static union {
	struct vdso_arch_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_arch_data *vdso_data = &vdso_data_store.data;

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

static struct vm_special_mapping vvar_spec __ro_after_init = {
	.name = "[vvar]",
};

static struct vm_special_mapping vdso32_spec __ro_after_init = {
	.name = "[vdso]",
	.mremap = vdso32_mremap,
};

static struct vm_special_mapping vdso64_spec __ro_after_init = {
	.name = "[vdso]",
	.mremap = vdso64_mremap,
};

/*
 * This is called from binfmt_elf, we create the special vma for the
 * vDSO and insert it into the mm struct tree
 */
static int __arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	unsigned long vdso_size, vdso_base, mappings_size;
	struct vm_special_mapping *vdso_spec;
	unsigned long vvar_size = PAGE_SIZE;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	if (is_32bit_task()) {
		vdso_spec = &vdso32_spec;
		vdso_size = &vdso32_end - &vdso32_start;
		vdso_base = VDSO32_MBASE;
	} else {
		vdso_spec = &vdso64_spec;
		vdso_size = &vdso64_end - &vdso64_start;
		/*
		 * On 64bit we don't have a preferred map address. This
		 * allows get_unmapped_area to find an area near other mmaps
		 * and most likely share a SLB entry.
		 */
		vdso_base = 0;
	}

	mappings_size = vdso_size + vvar_size;
	mappings_size += (VDSO_ALIGNMENT - 1) & PAGE_MASK;

	/*
	 * pick a base address for the vDSO in process space. We try to put it
	 * at vdso_base which is the "natural" base for it, but we might fail
	 * and end up putting it elsewhere.
	 * Add enough to the size so that the result can be aligned.
	 */
	vdso_base = get_unmapped_area(NULL, vdso_base, mappings_size, 0, 0);
	if (IS_ERR_VALUE(vdso_base))
		return vdso_base;

	/* Add required alignment. */
	vdso_base = ALIGN(vdso_base, VDSO_ALIGNMENT);

	/*
	 * Put vDSO base into mm struct. We need to do this before calling
	 * install_special_mapping or the perf counter mmap tracking code
	 * will fail to recognise it as a vDSO.
	 */
	mm->context.vdso = (void __user *)vdso_base + vvar_size;

	vma = _install_special_mapping(mm, vdso_base, vvar_size,
				       VM_READ | VM_MAYREAD | VM_IO |
				       VM_DONTDUMP | VM_PFNMAP, &vvar_spec);
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
	if (IS_ERR(vma))
		do_munmap(mm, vdso_base, vvar_size, NULL);

	return PTR_ERR_OR_ZERO(vma);
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int rc;

	mm->context.vdso = NULL;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	rc = __arch_setup_additional_pages(bprm, uses_interp);
	if (rc)
		mm->context.vdso = NULL;

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
		if (sys_call_table[i] != (unsigned long)&sys_ni_syscall)
			vdso_data->syscall_map[i >> 5] |= 0x80000000UL >> (i & 0x1f);
		if (IS_ENABLED(CONFIG_COMPAT) &&
		    compat_sys_call_table[i] != (unsigned long)&sys_ni_syscall)
			vdso_data->compat_syscall_map[i >> 5] |= 0x80000000UL >> (i & 0x1f);
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

static struct page ** __init vvar_setup_pages(void)
{
	struct page **pagelist;

	/* .pages is NULL-terminated */
	pagelist = kcalloc(2, sizeof(struct page *), GFP_KERNEL);
	if (!pagelist)
		panic("%s: Cannot allocate page list for VVAR", __func__);

	pagelist[0] = virt_to_page(vdso_data);
	return pagelist;
}

static int __init vdso_init(void)
{
#ifdef CONFIG_PPC64
	/*
	 * Fill up the "systemcfg" stuff for backward compatibility
	 */
	strcpy((char *)vdso_data->eye_catcher, "SYSTEMCFG:PPC64");
	vdso_data->version.major = SYSTEMCFG_MAJOR;
	vdso_data->version.minor = SYSTEMCFG_MINOR;
	vdso_data->processor = mfspr(SPRN_PVR);
	/*
	 * Fake the old platform number for pSeries and add
	 * in LPAR bit if necessary
	 */
	vdso_data->platform = 0x100;
	if (firmware_has_feature(FW_FEATURE_LPAR))
		vdso_data->platform |= 1;
	vdso_data->physicalMemorySize = memblock_phys_mem_size();
	vdso_data->dcache_size = ppc64_caches.l1d.size;
	vdso_data->dcache_line_size = ppc64_caches.l1d.line_size;
	vdso_data->icache_size = ppc64_caches.l1i.size;
	vdso_data->icache_line_size = ppc64_caches.l1i.line_size;
	vdso_data->dcache_block_size = ppc64_caches.l1d.block_size;
	vdso_data->icache_block_size = ppc64_caches.l1i.block_size;
	vdso_data->dcache_log_block_size = ppc64_caches.l1d.log_block_size;
	vdso_data->icache_log_block_size = ppc64_caches.l1i.log_block_size;
#endif /* CONFIG_PPC64 */

	vdso_setup_syscall_map();

	vdso_fixup_features();

	if (IS_ENABLED(CONFIG_VDSO32))
		vdso32_spec.pages = vdso_setup_pages(&vdso32_start, &vdso32_end);

	if (IS_ENABLED(CONFIG_PPC64))
		vdso64_spec.pages = vdso_setup_pages(&vdso64_start, &vdso64_end);

	vvar_spec.pages = vvar_setup_pages();

	smp_wmb();

	return 0;
}
arch_initcall(vdso_init);
