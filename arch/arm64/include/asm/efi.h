#ifndef _ASM_EFI_H
#define _ASM_EFI_H

#include <asm/io.h>
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
	kernel_neon_begin();						\
	efi_virtmap_load();						\
})

#define arch_efi_call_virt(p, f, args...)				\
({									\
	efi_##f##_t *__f;						\
	__f = p->f;							\
	__f(args);							\
})

#define arch_efi_call_virt_teardown()					\
({									\
	efi_virtmap_unload();						\
	kernel_neon_end();						\
})

#define ARCH_EFI_IRQ_FLAGS_MASK (PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT)

/* arch specific definitions used by the stub code */

/*
 * AArch64 requires the DTB to be 8-byte aligned in the first 512MiB from
 * start of kernel and may not cross a 2MiB boundary. We set alignment to
 * 2MiB so we know it won't cross a 2MiB boundary.
 */
#define EFI_FDT_ALIGN	SZ_2M   /* used by allocate_new_fdt_and_exit_boot() */
#define MAX_FDT_OFFSET	SZ_512M

#define efi_call_early(f, ...)		sys_table_arg->boottime->f(__VA_ARGS__)
#define __efi_call_early(f, ...)	f(__VA_ARGS__)
#define efi_is_64bit()			(true)

#define efi_call_proto(protocol, f, instance, ...)			\
	((protocol##_t *)instance)->f(instance, ##__VA_ARGS__)

#define alloc_screen_info(x...)		&screen_info
#define free_screen_info(x...)

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
	switch_mm(NULL, mm, NULL);
}

void efi_virtmap_load(void);
void efi_virtmap_unload(void);

#endif /* _ASM_EFI_H */
