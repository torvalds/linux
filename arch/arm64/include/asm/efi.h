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
#else
#define efi_init()
#endif

int efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md);
int efi_set_mapping_permissions(struct mm_struct *mm, efi_memory_desc_t *md);

#define arch_efi_call_virt_setup()					\
({									\
	efi_virtmap_load();						\
	__efi_fpsimd_begin();						\
})

#define arch_efi_call_virt(p, f, args...)				\
({									\
	efi_##f##_t *__f;						\
	__f = p->f;							\
	__efi_rt_asm_wrapper(__f, #f, args);				\
})

#define arch_efi_call_virt_teardown()					\
({									\
	__efi_fpsimd_end();						\
	efi_virtmap_unload();						\
})

efi_status_t __efi_rt_asm_wrapper(void *, const char *, ...);

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

/* on arm64, the FDT may be located anywhere in system RAM */
static inline unsigned long efi_get_max_fdt_addr(unsigned long dram_base)
{
	return ULONG_MAX;
}

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
static inline unsigned long efi_get_max_initrd_addr(unsigned long dram_base,
						    unsigned long image_addr)
{
	return (image_addr & ~(SZ_1G - 1UL)) + (1UL << (VA_BITS_MIN - 1));
}

#define efi_bs_call(func, ...)	efi_system_table()->boottime->func(__VA_ARGS__)
#define efi_rt_call(func, ...)	efi_system_table()->runtime->func(__VA_ARGS__)
#define efi_is_native()		(true)

#define efi_table_attr(inst, attr)	(inst->attr)

#define efi_call_proto(inst, func, ...) inst->func(inst, ##__VA_ARGS__)

#define alloc_screen_info(x...)		&screen_info

static inline void free_screen_info(struct screen_info *si)
{
}

static inline void efifb_setup_from_dmi(struct screen_info *si, const char *opt)
{
}

#define EFI_ALLOC_ALIGN		SZ_64K

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

#endif /* _ASM_EFI_H */
