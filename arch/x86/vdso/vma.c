/*
 * Copyright 2007 Andi Kleen, SUSE Labs.
 * Subject to the GPL, v.2
 *
 * This contains most of the x86 vDSO kernel-side code.
 */
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/cpu.h>
#include <asm/vgtod.h>
#include <asm/proto.h>
#include <asm/vdso.h>
#include <asm/vvar.h>
#include <asm/page.h>
#include <asm/hpet.h>
#include <asm/desc.h>

#if defined(CONFIG_X86_64)
unsigned int __read_mostly vdso64_enabled = 1;
#endif

void __init init_vdso_image(const struct vdso_image *image)
{
	int i;
	int npages = (image->size) / PAGE_SIZE;

	BUG_ON(image->size % PAGE_SIZE != 0);
	for (i = 0; i < npages; i++)
		image->text_mapping.pages[i] =
			virt_to_page(image->data + i*PAGE_SIZE);

	apply_alternatives((struct alt_instr *)(image->data + image->alt),
			   (struct alt_instr *)(image->data + image->alt +
						image->alt_len));
}

struct linux_binprm;

/* Put the vdso above the (randomized) stack with another randomized offset.
   This way there is no hole in the middle of address space.
   To save memory make sure it is still in the same PTE as the stack top.
   This doesn't give that many random bits.

   Only used for the 64-bit and x32 vdsos. */
static unsigned long vdso_addr(unsigned long start, unsigned len)
{
#ifdef CONFIG_X86_32
	return 0;
#else
	unsigned long addr, end;
	unsigned offset;
	end = (start + PMD_SIZE - 1) & PMD_MASK;
	if (end >= TASK_SIZE_MAX)
		end = TASK_SIZE_MAX;
	end -= len;
	/* This loses some more bits than a modulo, but is cheaper */
	offset = get_random_int() & (PTRS_PER_PTE - 1);
	addr = start + (offset << PAGE_SHIFT);
	if (addr >= end)
		addr = end;

	/*
	 * page-align it here so that get_unmapped_area doesn't
	 * align it wrongfully again to the next page. addr can come in 4K
	 * unaligned here as a result of stack start randomization.
	 */
	addr = PAGE_ALIGN(addr);
	addr = align_vdso_addr(addr);

	return addr;
#endif
}

static int map_vdso(const struct vdso_image *image, bool calculate_addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long addr, text_start;
	int ret = 0;
	static struct page *no_pages[] = {NULL};
	static struct vm_special_mapping vvar_mapping = {
		.name = "[vvar]",
		.pages = no_pages,
	};

	if (calculate_addr) {
		addr = vdso_addr(current->mm->start_stack,
				 image->size - image->sym_vvar_start);
	} else {
		addr = 0;
	}

	down_write(&mm->mmap_sem);

	addr = get_unmapped_area(NULL, addr,
				 image->size - image->sym_vvar_start, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	text_start = addr - image->sym_vvar_start;
	current->mm->context.vdso = (void __user *)text_start;

	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 */
	vma = _install_special_mapping(mm,
				       text_start,
				       image->size,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       &image->text_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	vma = _install_special_mapping(mm,
				       addr,
				       -image->sym_vvar_start,
				       VM_READ|VM_MAYREAD,
				       &vvar_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	if (image->sym_vvar_page)
		ret = remap_pfn_range(vma,
				      text_start + image->sym_vvar_page,
				      __pa_symbol(&__vvar_page) >> PAGE_SHIFT,
				      PAGE_SIZE,
				      PAGE_READONLY);

	if (ret)
		goto up_fail;

#ifdef CONFIG_HPET_TIMER
	if (hpet_address && image->sym_hpet_page) {
		ret = io_remap_pfn_range(vma,
			text_start + image->sym_hpet_page,
			hpet_address >> PAGE_SHIFT,
			PAGE_SIZE,
			pgprot_noncached(PAGE_READONLY));

		if (ret)
			goto up_fail;
	}
#endif

up_fail:
	if (ret)
		current->mm->context.vdso = NULL;

	up_write(&mm->mmap_sem);
	return ret;
}

#if defined(CONFIG_X86_32) || defined(CONFIG_COMPAT)
static int load_vdso32(void)
{
	int ret;

	if (vdso32_enabled != 1)  /* Other values all mean "disabled" */
		return 0;

	ret = map_vdso(selected_vdso32, false);
	if (ret)
		return ret;

	if (selected_vdso32->sym_VDSO32_SYSENTER_RETURN)
		current_thread_info()->sysenter_return =
			current->mm->context.vdso +
			selected_vdso32->sym_VDSO32_SYSENTER_RETURN;

	return 0;
}
#endif

#ifdef CONFIG_X86_64
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	if (!vdso64_enabled)
		return 0;

	return map_vdso(&vdso_image_64, true);
}

#ifdef CONFIG_COMPAT
int compat_arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp)
{
#ifdef CONFIG_X86_X32_ABI
	if (test_thread_flag(TIF_X32)) {
		if (!vdso64_enabled)
			return 0;

		return map_vdso(&vdso_image_x32, true);
	}
#endif

	return load_vdso32();
}
#endif
#else
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	return load_vdso32();
}
#endif

#ifdef CONFIG_X86_64
static __init int vdso_setup(char *s)
{
	vdso64_enabled = simple_strtoul(s, NULL, 0);
	return 0;
}
__setup("vdso=", vdso_setup);
#endif

#ifdef CONFIG_X86_64
static void vgetcpu_cpu_init(void *arg)
{
	int cpu = smp_processor_id();
	struct desc_struct d = { };
	unsigned long node = 0;
#ifdef CONFIG_NUMA
	node = cpu_to_node(cpu);
#endif
	if (cpu_has(&cpu_data(cpu), X86_FEATURE_RDTSCP))
		write_rdtscp_aux((node << 12) | cpu);

	/*
	 * Store cpu number in limit so that it can be loaded
	 * quickly in user space in vgetcpu. (12 bits for the CPU
	 * and 8 bits for the node)
	 */
	d.limit0 = cpu | ((node & 0xf) << 12);
	d.limit = node >> 4;
	d.type = 5;		/* RO data, expand down, accessed */
	d.dpl = 3;		/* Visible to user code */
	d.s = 1;		/* Not a system segment */
	d.p = 1;		/* Present */
	d.d = 1;		/* 32-bit */

	write_gdt_entry(get_cpu_gdt_table(cpu), GDT_ENTRY_PER_CPU, &d, DESCTYPE_S);
}

static int
vgetcpu_cpu_notifier(struct notifier_block *n, unsigned long action, void *arg)
{
	long cpu = (long)arg;

	if (action == CPU_ONLINE || action == CPU_ONLINE_FROZEN)
		smp_call_function_single(cpu, vgetcpu_cpu_init, NULL, 1);

	return NOTIFY_DONE;
}

static int __init init_vdso(void)
{
	init_vdso_image(&vdso_image_64);

#ifdef CONFIG_X86_X32_ABI
	init_vdso_image(&vdso_image_x32);
#endif

	cpu_notifier_register_begin();

	on_each_cpu(vgetcpu_cpu_init, NULL, 1);
	/* notifier priority > KVM */
	__hotcpu_notifier(vgetcpu_cpu_notifier, 30);

	cpu_notifier_register_done();

	return 0;
}
subsys_initcall(init_vdso);
#endif /* CONFIG_X86_64 */
