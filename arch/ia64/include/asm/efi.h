/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_EFI_H
#define _ASM_EFI_H

typedef int (*efi_freemem_callback_t) (u64 start, u64 end, void *arg);

void *efi_get_pal_addr(void);
void efi_map_pal_code(void);
void efi_memmap_walk(efi_freemem_callback_t, void *);
void efi_memmap_walk_uc(efi_freemem_callback_t, void *);
void efi_gettimeofday(struct timespec64 *ts);

#endif
