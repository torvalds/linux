/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_EFI_H
#define _ASM_EFI_H

#include <asm/boot.h>
#include <asm/cpufeature.h>
#include <asm/fpsimd.h>
#include <asm/io.h>
#include <asm/memory.h>
#include <asm/mmu_context.h>
#include <asm/neon.h>
#include <asm/ptrace.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_EFI
extern void efi_init(void);

bool efi_runtime_fixup_exception(struct pt_regs *regs, const char *msg);
#else
#define efi_init()

static inline
bool efi_runtime_fixup_exception(struct pt_regs *regs, const char *msg)
{
	return false;
}
#endif

int efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md);
int efi_set_mapping_permissions(struct mm_struct *mm, efi_memory_desc_t *md);

#define arch_efi_call_virt_setup()					\
({									\
	efi_virtmap_load();						\
	__efi_fpsimd_begin();						\
	spin_lock(&efi_rt_lock);					\
})

#undef arch_efi_call_virt
#define arch_efi_call_virt(p, f, args...)				\
	__efi_rt_asm_wrapper((p)->f, #f, args)

#define arch_efi_call_virt_teardown()					\
({									\
	spin_unlock(&efi_rt_lock);					\
	__efi_fpsimd_end();						\
	efi_virtmap_unload();						\
})

extern spinlock_t efi_rt_lock;
extern u64 *efi_rt_stack_top;
efi_status_t __efi_rt_asm_wrapper(void *, const char *, ...);

/*
 * efi_rt_stack_top[-1] contains the value the stack pointer had before
 * switching to the EFI runtime stack.
 */
#define current_in_efi()						\
	(!preemptible() && efi_rt_stack_top != NULL &&			\
	 on_task_stack(current, READ_ONCE(efi_rt_stack_top[-1]), 1))

#define ARCH_EFI_IRQ_FLAGS_MASK (PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT)

/*
 * Even when Linux uses IRQ priorities for IRQ disabling, EFI does not.
 * And EFI shouldn't really play around with priority masking as it is not aware
 * which priorities the OS has assigned to its interrupts.
 */
#define arch_efi_save_flags(state_flags)		\
	((void)((state_flags) = read_sysreg(daif)))

#define arch_efi_restore_flags(state_flags)	write_sysreg(state_flags, daif)


/* arch specific definitions used by the stub code */

/*
 * In some configurations (e.g. VMAP_STACK && 64K pages), stacks built into the
 * kernel need greater alignment than we require the segments to be padded to.
 */
#define EFI_KIMG_ALIGN	\
	(SEGMENT_ALIGN > THREAD_ALIGN ? SEGMENT_ALIGN : THREAD_ALIGN)

/*
 * On arm64, we have to ensure that the initrd ends up in the linear region,
 * which is a 1 GB aligned region of size '1UL << (VA_BITS_MIN - 1)' that is
 * guaranteed to cover the kernel Image.
 *
 * Since the EFI stub is part of the kernel Image, we can relax the
 * usual requirements in Documentation/arm64/booting.rst, which still
 * apply to other bootloaders, and are required for some kernel
 * configurations.
 */
static inline unsigned long efi_get_max_initrd_addr(unsigned long image_addr)
{
	return (image_addr & ~(SZ_1G - 1UL)) + (1UL << (VA_BITS_MIN - 1));
}

static inline unsigned long efi_get_kimg_min_align(void)
{
	extern bool efi_nokaslr;

	/*
	 * Although relocatable kernels can fix up the misalignment with
	 * respect to MIN_KIMG_ALIGN, the resulting virtual text addresses are
	 * subtly out of sync with those recorded in the vmlinux when kaslr is
	 * disabled but the image required relocation anyway. Therefore retain
	 * 2M alignment if KASLR was explicitly disabled, even if it was not
	 * going to be activated to begin with.
	 */
	return efi_nokaslr ? MIN_KIMG_ALIGN : EFI_KIMG_ALIGN;
}

#define EFI_ALLOC_ALIGN		SZ_64K
#define EFI_ALLOC_LIMIT		((1UL << 48) - 1)

/*
 * On ARM systems, virtually remapped UEFI runtime services are set up in two
 * distinct stages:
 * - The stub retrieves the final version of the memory map from UEFI, populates
 *   the virt_addr fields and calls the SetVirtualAddressMap() [SVAM] runtime
 *   service to communicate the new mapping to the firmware (Note that the new
 *   mapping is not live at this time)
 * - During an early initcall(), the EFI system table is permanently remapped
 *   and the virtual remapping of the UEFI Runtime Services regions is loaded
 *   into a private set of page tables. If this all succeeds, the Runtime
 *   Services are enabled and the EFI_RUNTIME_SERVICES bit set.
 */

static inline void efi_set_pgd(struct mm_struct *mm)
{
	__switch_mm(mm);

	if (system_uses_ttbr0_pan()) {
		if (mm != current->active_mm) {
			/*
			 * Update the current thread's saved ttbr0 since it is
			 * restored as part of a return from exception. Enable
			 * access to the valid TTBR0_EL1 and invoke the errata
			 * workaround directly since there is no return from
			 * exception when invoking the EFI run-time services.
			 */
			update_saved_ttbr0(current, mm);
			uaccess_ttbr0_enable();
			post_ttbr_update_workaround();
		} else {
			/*
			 * Defer the switch to the current thread's TTBR0_EL1
			 * until uaccess_enable(). Restore the current
			 * thread's saved ttbr0 corresponding to its active_mm
			 */
			uaccess_ttbr0_disable();
			update_saved_ttbr0(current, current->active_mm);
		}
	}
}

void efi_virtmap_load(void);
void efi_virtmap_unload(void);

static inline void efi_capsule_flush_cache_range(void *addr, int size)
{
	dcache_clean_inval_poc((unsigned long)addr, (unsigned long)addr + size);
}

#endif /* _ASM_EFI_H */
