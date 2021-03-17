/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
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
#include <asm/ptrace.h>

#ifdef CONFIG_EFI
void efi_init(void);

int efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md);
int efi_set_mapping_permissions(struct mm_struct *mm, efi_memory_desc_t *md);

#define arch_efi_call_virt_setup()	efi_virtmap_load()
#define arch_efi_call_virt_teardown()	efi_virtmap_unload()

#define arch_efi_call_virt(p, f, args...)				\
({									\
	efi_##f##_t *__f;						\
	__f = p->f;							\
	__f(args);							\
})

#define ARCH_EFI_IRQ_FLAGS_MASK \
	(PSR_J_BIT | PSR_E_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT | \
	 PSR_T_BIT | MODE_MASK)

static inline void efi_set_pgd(struct mm_struct *mm)
{
	check_and_switch_context(mm, NULL);
}

void efi_virtmap_load(void);
void efi_virtmap_unload(void);

#else
#define efi_init()
#endif /* CONFIG_EFI */

/* arch specific definitions used by the stub code */

struct screen_info *alloc_screen_info(void);
void free_screen_info(struct screen_info *si);

static inline void efifb_setup_from_dmi(struct screen_info *si, const char *opt)
{
}

/*
 * A reasonable upper bound for the uncompressed kernel size is 32 MBytes,
 * so we will reserve that amount of memory. We have no easy way to tell what
 * the actuall size of code + data the uncompressed kernel will use.
 * If this is insufficient, the decompressor will relocate itself out of the
 * way before performing the decompression.
 */
#define MAX_UNCOMP_KERNEL_SIZE	SZ_32M

/*
 * phys-to-virt patching requires that the physical to virtual offset fits
 * into the immediate field of an add/sub instruction, which comes down to the
 * 24 least significant bits being zero, and so the offset should be a multiple
 * of 16 MB. Since PAGE_OFFSET itself is a multiple of 16 MB, the physical
 * base should be aligned to 16 MB as well.
 */
#define EFI_PHYS_ALIGN		SZ_16M

/* on ARM, the FDT should be located in a lowmem region */
static inline unsigned long efi_get_max_fdt_addr(unsigned long image_addr)
{
	return round_down(image_addr, EFI_PHYS_ALIGN) + SZ_512M;
}

/* on ARM, the initrd should be loaded in a lowmem region */
static inline unsigned long efi_get_max_initrd_addr(unsigned long image_addr)
{
	return round_down(image_addr, EFI_PHYS_ALIGN) + SZ_512M;
}

struct efi_arm_entry_state {
	u32	cpsr_before_ebs;
	u32	sctlr_before_ebs;
	u32	cpsr_after_ebs;
	u32	sctlr_after_ebs;
};

#endif /* _ASM_ARM_EFI_H */
