/*
 * IA-32 ELF support.
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 06/16/00	A. Mallick	initialize csd/ssd/tssd/cflg for ia32_load_state
 * 04/13/01	D. Mosberger	dropped saving tssd in ar.k1---it's not needed
 * 09/14/01	D. Mosberger	fixed memory management for gdt/tss page
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/security.h>

#include <asm/param.h>
#include <asm/signal.h>

#include "ia32priv.h"
#include "elfcore32.h"

/* Override some function names */
#undef start_thread
#define start_thread			ia32_start_thread
#define elf_format			elf32_format
#define init_elf_binfmt			init_elf32_binfmt
#define exit_elf_binfmt			exit_elf32_binfmt

#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC	IA32_CLOCKS_PER_SEC

extern void ia64_elf32_init (struct pt_regs *regs);

static void elf32_set_personality (void);

static unsigned long __attribute ((unused))
randomize_stack_top(unsigned long stack_top);

#define setup_arg_pages(bprm,tos,exec)		ia32_setup_arg_pages(bprm,exec)
#define elf_map				elf32_map

#undef SET_PERSONALITY
#define SET_PERSONALITY(ex, ibcs2)	elf32_set_personality()

#define elf_read_implies_exec(ex, have_pt_gnu_stack)	(!(have_pt_gnu_stack))

/* Ugly but avoids duplication */
#include "../../../fs/binfmt_elf.c"

extern struct page *ia32_shared_page[];
extern unsigned long *ia32_gdt;
extern struct page *ia32_gate_page;

int
ia32_install_shared_page (struct vm_area_struct *vma, struct vm_fault *vmf)
{
	vmf->page = ia32_shared_page[smp_processor_id()];
	get_page(vmf->page);
	return 0;
}

int
ia32_install_gate_page (struct vm_area_struct *vma, struct vm_fault *vmf)
{
	vmf->page = ia32_gate_page;
	get_page(vmf->page);
	return 0;
}


static struct vm_operations_struct ia32_shared_page_vm_ops = {
	.fault = ia32_install_shared_page
};

static struct vm_operations_struct ia32_gate_page_vm_ops = {
	.fault = ia32_install_gate_page
};

void
ia64_elf32_init (struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	/*
	 * Map GDT below 4GB, where the processor can find it.  We need to map
	 * it with privilege level 3 because the IVE uses non-privileged accesses to these
	 * tables.  IA-32 segmentation is used to protect against IA-32 accesses to them.
	 */
	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = IA32_GDT_OFFSET;
		vma->vm_end = vma->vm_start + PAGE_SIZE;
		vma->vm_page_prot = PAGE_SHARED;
		vma->vm_flags = VM_READ|VM_MAYREAD|VM_RESERVED;
		vma->vm_ops = &ia32_shared_page_vm_ops;
		down_write(&current->mm->mmap_sem);
		{
			if (insert_vm_struct(current->mm, vma)) {
				kmem_cache_free(vm_area_cachep, vma);
				up_write(&current->mm->mmap_sem);
				BUG();
			}
		}
		up_write(&current->mm->mmap_sem);
	}

	/*
	 * When user stack is not executable, push sigreturn code to stack makes
	 * segmentation fault raised when returning to kernel. So now sigreturn
	 * code is locked in specific gate page, which is pointed by pretcode
	 * when setup_frame_ia32
	 */
	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = IA32_GATE_OFFSET;
		vma->vm_end = vma->vm_start + PAGE_SIZE;
		vma->vm_page_prot = PAGE_COPY_EXEC;
		vma->vm_flags = VM_READ | VM_MAYREAD | VM_EXEC
				| VM_MAYEXEC | VM_RESERVED;
		vma->vm_ops = &ia32_gate_page_vm_ops;
		down_write(&current->mm->mmap_sem);
		{
			if (insert_vm_struct(current->mm, vma)) {
				kmem_cache_free(vm_area_cachep, vma);
				up_write(&current->mm->mmap_sem);
				BUG();
			}
		}
		up_write(&current->mm->mmap_sem);
	}

	/*
	 * Install LDT as anonymous memory.  This gives us all-zero segment descriptors
	 * until a task modifies them via modify_ldt().
	 */
	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = IA32_LDT_OFFSET;
		vma->vm_end = vma->vm_start + PAGE_ALIGN(IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE);
		vma->vm_page_prot = PAGE_SHARED;
		vma->vm_flags = VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE;
		down_write(&current->mm->mmap_sem);
		{
			if (insert_vm_struct(current->mm, vma)) {
				kmem_cache_free(vm_area_cachep, vma);
				up_write(&current->mm->mmap_sem);
				BUG();
			}
		}
		up_write(&current->mm->mmap_sem);
	}

	ia64_psr(regs)->ac = 0;		/* turn off alignment checking */
	regs->loadrs = 0;
	/*
	 *  According to the ABI %edx points to an `atexit' handler.  Since we don't have
	 *  one we'll set it to 0 and initialize all the other registers just to make
	 *  things more deterministic, ala the i386 implementation.
	 */
	regs->r8 = 0;	/* %eax */
	regs->r11 = 0;	/* %ebx */
	regs->r9 = 0;	/* %ecx */
	regs->r10 = 0;	/* %edx */
	regs->r13 = 0;	/* %ebp */
	regs->r14 = 0;	/* %esi */
	regs->r15 = 0;	/* %edi */

	current->thread.eflag = IA32_EFLAG;
	current->thread.fsr = IA32_FSR_DEFAULT;
	current->thread.fcr = IA32_FCR_DEFAULT;
	current->thread.fir = 0;
	current->thread.fdr = 0;

	/*
	 * Setup GDTD.  Note: GDTD is the descrambled version of the pseudo-descriptor
	 * format defined by Figure 3-11 "Pseudo-Descriptor Format" in the IA-32
	 * architecture manual. Also note that the only fields that are not ignored are
	 * `base', `limit', 'G', `P' (must be 1) and `S' (must be 0).
	 */
	regs->r31 = IA32_SEG_UNSCRAMBLE(IA32_SEG_DESCRIPTOR(IA32_GDT_OFFSET, IA32_PAGE_SIZE - 1,
							    0, 0, 0, 1, 0, 0, 0));
	/* Setup the segment selectors */
	regs->r16 = (__USER_DS << 16) | __USER_DS; /* ES == DS, GS, FS are zero */
	regs->r17 = (__USER_DS << 16) | __USER_CS; /* SS, CS; ia32_load_state() sets TSS and LDT */

	ia32_load_segment_descriptors(current);
	ia32_load_state(current);
}

/*
 * Undo the override of setup_arg_pages() without this ia32_setup_arg_pages()
 * will suffer infinite self recursion.
 */
#undef setup_arg_pages

int
ia32_setup_arg_pages (struct linux_binprm *bprm, int executable_stack)
{
	int ret;

	ret = setup_arg_pages(bprm, IA32_STACK_TOP, executable_stack);
	if (!ret) {
		/*
		 * Can't do it in ia64_elf32_init(). Needs to be done before
		 * calls to elf32_map()
		 */
		current->thread.ppl = ia32_init_pp_list();
	}

	return ret;
}

static void
elf32_set_personality (void)
{
	set_personality(PER_LINUX32);
	current->thread.map_base  = IA32_PAGE_OFFSET/3;
}

static unsigned long
elf32_map(struct file *filep, unsigned long addr, struct elf_phdr *eppnt,
		int prot, int type, unsigned long unused)
{
	unsigned long pgoff = (eppnt->p_vaddr) & ~IA32_PAGE_MASK;

	return ia32_do_mmap(filep, (addr & IA32_PAGE_MASK), eppnt->p_filesz + pgoff, prot, type,
			    eppnt->p_offset - pgoff);
}

#define cpu_uses_ia32el()	(local_cpu_data->family > 0x1f)

static int __init check_elf32_binfmt(void)
{
	if (cpu_uses_ia32el()) {
		printk("Please use IA-32 EL for executing IA-32 binaries\n");
		unregister_binfmt(&elf_format);
	}
	return 0;
}

module_init(check_elf32_binfmt)
