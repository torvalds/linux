/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_LOONGARCH_EFI_H
#define _ASM_LOONGARCH_EFI_H

#include <linux/efi.h>

void __init efi_init(void);
void __init efi_runtime_init(void);
void __init *efi_fdt_pointer(void);
void efifb_setup_from_dmi(struct screen_info *si, const char *opt);

#define ARCH_EFI_IRQ_FLAGS_MASK  0x00000004  /* Bit 2: CSR.CRMD.IE */

#define arch_efi_call_virt_setup()
#define arch_efi_call_virt_teardown()

#define EFI_ALLOC_ALIGN		SZ_64K
#define EFI_RT_VIRTUAL_OFFSET	CSR_DMW0_BASE

static inline unsigned long efi_get_max_initrd_addr(unsigned long image_addr)
{
	return ULONG_MAX;
}

static inline unsigned long efi_get_kimg_min_align(void)
{
	return SZ_2M;
}

#define EFI_KIMG_PREFERRED_ADDRESS	PHYSADDR(VMLINUX_LOAD_ADDRESS)

#endif /* _ASM_LOONGARCH_EFI_H */
