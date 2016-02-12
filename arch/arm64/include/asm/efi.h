#ifndef _ASM_EFI_H
#define _ASM_EFI_H

#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/neon.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_EFI
extern void efi_init(void);
#else
#define efi_init()
#endif

int efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md);

#define efi_call_virt(f, ...)						\
({									\
	efi_##f##_t *__f;						\
	efi_status_t __s;						\
									\
	kernel_neon_begin();						\
	efi_virtmap_load();						\
	__f = efi.systab->runtime->f;					\
	__s = __f(__VA_ARGS__);						\
	efi_virtmap_unload();						\
	kernel_neon_end();						\
	__s;								\
})

#define __efi_call_virt(f, ...)						\
({									\
	efi_##f##_t *__f;						\
									\
	kernel_neon_begin();						\
	efi_virtmap_load();						\
	__f = efi.systab->runtime->f;					\
	__f(__VA_ARGS__);						\
	efi_virtmap_unload();						\
	kernel_neon_end();						\
})

/* arch specific definitions used by the stub code */

/*
 * AArch64 requires the DTB to be 8-byte aligned in the first 512MiB from
 * start of kernel and may not cross a 2MiB boundary. We set alignment to
 * 2MiB so we know it won't cross a 2MiB boundary.
 */
#define EFI_FDT_ALIGN	SZ_2M   /* used by allocate_new_fdt_and_exit_boot() */
#define MAX_FDT_OFFSET	SZ_512M

#define efi_call_early(f, ...) sys_table_arg->boottime->f(__VA_ARGS__)

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
