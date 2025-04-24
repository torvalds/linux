/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KEXEC_H
#define _ASM_X86_KEXEC_H

#ifdef CONFIG_X86_32
# define PA_CONTROL_PAGE	0
# define VA_CONTROL_PAGE	1
# define PA_PGD			2
# define PA_SWAP_PAGE		3
# define PAGES_NR		4
#endif

# define KEXEC_CONTROL_PAGE_SIZE	4096
# define KEXEC_CONTROL_CODE_MAX_SIZE	2048

#ifndef __ASSEMBLER__

#include <linux/string.h>
#include <linux/kernel.h>

#include <asm/asm.h>
#include <asm/page.h>
#include <asm/ptrace.h>

struct kimage;

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 *
 * So far x86_64 is limited to 40 physical address bits.
 */
#ifdef CONFIG_X86_32
/* Maximum physical address we can use pages from */
# define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
# define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
# define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE


/* The native architecture */
# define KEXEC_ARCH KEXEC_ARCH_386

/* We can also handle crash dumps from 64 bit kernel. */
# define vmcore_elf_check_arch_cross(x) ((x)->e_machine == EM_X86_64)
#else
/* Maximum physical address we can use pages from */
# define KEXEC_SOURCE_MEMORY_LIMIT      (MAXMEM-1)
/* Maximum address we can reach in physical address mode */
# define KEXEC_DESTINATION_MEMORY_LIMIT (MAXMEM-1)
/* Maximum address we can use for the control pages */
# define KEXEC_CONTROL_MEMORY_LIMIT     (MAXMEM-1)

/* The native architecture */
# define KEXEC_ARCH KEXEC_ARCH_X86_64

extern unsigned long kexec_va_control_page;
extern unsigned long kexec_pa_table_page;
extern unsigned long kexec_pa_swap_page;
#endif

/*
 * This function is responsible for capturing register states if coming
 * via panic otherwise just fix up the ss and sp if coming via kernel
 * mode exception.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs) {
		memcpy(newregs, oldregs, sizeof(*newregs));
	} else {
		asm volatile("mov %%" _ASM_BX ",%0" : "=m"(newregs->bx));
		asm volatile("mov %%" _ASM_CX ",%0" : "=m"(newregs->cx));
		asm volatile("mov %%" _ASM_DX ",%0" : "=m"(newregs->dx));
		asm volatile("mov %%" _ASM_SI ",%0" : "=m"(newregs->si));
		asm volatile("mov %%" _ASM_DI ",%0" : "=m"(newregs->di));
		asm volatile("mov %%" _ASM_BP ",%0" : "=m"(newregs->bp));
		asm volatile("mov %%" _ASM_AX ",%0" : "=m"(newregs->ax));
		asm volatile("mov %%" _ASM_SP ",%0" : "=m"(newregs->sp));
#ifdef CONFIG_X86_64
		asm volatile("mov %%r8,%0" : "=m"(newregs->r8));
		asm volatile("mov %%r9,%0" : "=m"(newregs->r9));
		asm volatile("mov %%r10,%0" : "=m"(newregs->r10));
		asm volatile("mov %%r11,%0" : "=m"(newregs->r11));
		asm volatile("mov %%r12,%0" : "=m"(newregs->r12));
		asm volatile("mov %%r13,%0" : "=m"(newregs->r13));
		asm volatile("mov %%r14,%0" : "=m"(newregs->r14));
		asm volatile("mov %%r15,%0" : "=m"(newregs->r15));
#endif
		asm volatile("mov %%ss,%k0" : "=a"(newregs->ss));
		asm volatile("mov %%cs,%k0" : "=a"(newregs->cs));
#ifdef CONFIG_X86_32
		asm volatile("mov %%ds,%k0" : "=a"(newregs->ds));
		asm volatile("mov %%es,%k0" : "=a"(newregs->es));
#endif
		asm volatile("pushf\n\t"
			     "pop %0" : "=m"(newregs->flags));
		newregs->ip = _THIS_IP_;
	}
}

#ifdef CONFIG_X86_32
typedef asmlinkage unsigned long
relocate_kernel_fn(unsigned long indirection_page,
		   unsigned long control_page,
		   unsigned long start_address,
		   unsigned int has_pae,
		   unsigned int preserve_context);
#else
typedef unsigned long
relocate_kernel_fn(unsigned long indirection_page,
		   unsigned long pa_control_page,
		   unsigned long start_address,
		   unsigned int preserve_context,
		   unsigned int host_mem_enc_active);
#endif
extern relocate_kernel_fn relocate_kernel;
#define ARCH_HAS_KIMAGE_ARCH

#ifdef CONFIG_X86_32
struct kimage_arch {
	pgd_t *pgd;
#ifdef CONFIG_X86_PAE
	pmd_t *pmd0;
	pmd_t *pmd1;
#endif
	pte_t *pte0;
	pte_t *pte1;
};
#else
struct kimage_arch {
	/*
	 * This is a kimage control page, as it must not overlap with either
	 * source or destination address ranges.
	 */
	pgd_t *pgd;
	/*
	 * The virtual mapping of the control code page itself is used only
	 * during the transition, while the current kernel's pages are all
	 * in place. Thus the intermediate page table pages used to map it
	 * are not control pages, but instead just normal pages obtained
	 * with get_zeroed_page(). And have to be tracked (below) so that
	 * they can be freed.
	 */
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
};
#endif /* CONFIG_X86_32 */

#ifdef CONFIG_X86_64
/*
 * Number of elements and order of elements in this structure should match
 * with the ones in arch/x86/purgatory/entry64.S. If you make a change here
 * make an appropriate change in purgatory too.
 */
struct kexec_entry64_regs {
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rsp;
	uint64_t rbp;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rip;
};

extern int arch_kexec_post_alloc_pages(void *vaddr, unsigned int pages,
				       gfp_t gfp);
#define arch_kexec_post_alloc_pages arch_kexec_post_alloc_pages

extern void arch_kexec_pre_free_pages(void *vaddr, unsigned int pages);
#define arch_kexec_pre_free_pages arch_kexec_pre_free_pages

void arch_kexec_protect_crashkres(void);
#define arch_kexec_protect_crashkres arch_kexec_protect_crashkres

void arch_kexec_unprotect_crashkres(void);
#define arch_kexec_unprotect_crashkres arch_kexec_unprotect_crashkres

#ifdef CONFIG_KEXEC_FILE
struct purgatory_info;
int arch_kexec_apply_relocations_add(struct purgatory_info *pi,
				     Elf_Shdr *section,
				     const Elf_Shdr *relsec,
				     const Elf_Shdr *symtab);
#define arch_kexec_apply_relocations_add arch_kexec_apply_relocations_add

int arch_kimage_file_post_load_cleanup(struct kimage *image);
#define arch_kimage_file_post_load_cleanup arch_kimage_file_post_load_cleanup
#endif
#endif

extern void kdump_nmi_shootdown_cpus(void);

#ifdef CONFIG_CRASH_HOTPLUG
void arch_crash_handle_hotplug_event(struct kimage *image, void *arg);
#define arch_crash_handle_hotplug_event arch_crash_handle_hotplug_event

int arch_crash_hotplug_support(struct kimage *image, unsigned long kexec_flags);
#define arch_crash_hotplug_support arch_crash_hotplug_support

unsigned int arch_crash_get_elfcorehdr_size(void);
#define crash_get_elfcorehdr_size arch_crash_get_elfcorehdr_size
#endif

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_KEXEC_H */
