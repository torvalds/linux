/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/efi.h>

extern struct boot_params *boot_params_pointer asm("boot_params");

extern void trampoline_32bit_src(void *, bool);
extern const u16 trampoline_ljmp_imm_offset;

void efi_adjust_memory_range_protection(unsigned long start,
					unsigned long size);

#ifdef CONFIG_X86_64
efi_status_t efi_setup_5level_paging(void);
void efi_5level_switch(void);
#else
static inline efi_status_t efi_setup_5level_paging(void) { return EFI_SUCCESS; }
static inline void efi_5level_switch(void) {}
#endif
