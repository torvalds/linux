/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */
#ifndef _ASM_EFI_H
#define _ASM_EFI_H

#include <asm/csr.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/ptrace.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>

#ifdef CONFIG_EFI
extern void efi_init(void);
#else
#define efi_init()
#endif

int efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md);
int efi_set_mapping_permissions(struct mm_struct *mm, efi_memory_desc_t *md, bool);

#define arch_efi_call_virt_setup()      ({		\
		sync_kernel_mappings(efi_mm.pgd);	\
		efi_virtmap_load();			\
	})
#define arch_efi_call_virt_teardown()   efi_virtmap_unload()

#define ARCH_EFI_IRQ_FLAGS_MASK (SR_IE | SR_SPIE)

/* Load initrd anywhere in system RAM */
static inline unsigned long efi_get_max_initrd_addr(unsigned long image_addr)
{
	return ULONG_MAX;
}

static inline unsigned long efi_get_kimg_min_align(void)
{
	/*
	 * RISC-V requires the kernel image to placed 2 MB aligned base for 64
	 * bit and 4MB for 32 bit.
	 */
	return IS_ENABLED(CONFIG_64BIT) ? SZ_2M : SZ_4M;
}

#define EFI_KIMG_PREFERRED_ADDRESS	efi_get_kimg_min_align()

void efi_virtmap_load(void);
void efi_virtmap_unload(void);

unsigned long stext_offset(void);

#endif /* _ASM_EFI_H */
