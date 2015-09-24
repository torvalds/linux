/*
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_EFI_H
#define __ASM_ARM_EFI_H

#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/early_ioremap.h>
#include <asm/fixmap.h>
#include <asm/highmem.h>
#include <asm/mach/map.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>

#ifdef CONFIG_EFI
void efi_init(void);

int efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md);

#define efi_call_virt(f, ...)						\
({									\
	efi_##f##_t *__f;						\
	efi_status_t __s;						\
									\
	efi_virtmap_load();						\
	__f = efi.systab->runtime->f;					\
	__s = __f(__VA_ARGS__);						\
	efi_virtmap_unload();						\
	__s;								\
})

#define __efi_call_virt(f, ...)						\
({									\
	efi_##f##_t *__f;						\
									\
	efi_virtmap_load();						\
	__f = efi.systab->runtime->f;					\
	__f(__VA_ARGS__);						\
	efi_virtmap_unload();						\
})

static inline void efi_set_pgd(struct mm_struct *mm)
{
	check_and_switch_context(mm, NULL);
}

void efi_virtmap_load(void);
void efi_virtmap_unload(void);

#else
#define efi_init()
#endif /* CONFIG_EFI */

#endif /* _ASM_ARM_EFI_H */
