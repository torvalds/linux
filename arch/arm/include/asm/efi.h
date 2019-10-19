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
#include <asm/pgtable.h>
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

#define efi_call_early(f, ...)		sys_table_arg->boottime->f(__VA_ARGS__)
#define __efi_call_early(f, ...)	f(__VA_ARGS__)
#define efi_call_runtime(f, ...)	sys_table_arg->runtime->f(__VA_ARGS__)
#define efi_is_64bit()			(false)

#define efi_table_attr(table, attr, instance)				\
	((table##_t *)instance)->attr

#define efi_call_proto(protocol, f, instance, ...)			\
	((protocol##_t *)instance)->f(instance, ##__VA_ARGS__)

struct screen_info *alloc_screen_info(efi_system_table_t *sys_table_arg);
void free_screen_info(efi_system_table_t *sys_table, struct screen_info *si);

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
 * The kernel zImage should preferably be located between 32 MB and 128 MB
 * from the base of DRAM. The min address leaves space for a maximal size
 * uncompressed image, and the max address is due to how the zImage decompressor
 * picks a destination address.
 */
#define ZIMAGE_OFFSET_LIMIT	SZ_128M
#define MIN_ZIMAGE_OFFSET	MAX_UNCOMP_KERNEL_SIZE

/* on ARM, the FDT should be located in the first 128 MB of RAM */
static inline unsigned long efi_get_max_fdt_addr(unsigned long dram_base)
{
	return dram_base + ZIMAGE_OFFSET_LIMIT;
}

/* on ARM, the initrd should be loaded in a lowmem region */
static inline unsigned long efi_get_max_initrd_addr(unsigned long dram_base,
						    unsigned long image_addr)
{
	return dram_base + SZ_512M;
}

#endif /* _ASM_ARM_EFI_H */
