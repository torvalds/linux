/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RISCV_PI_H_
#define _RISCV_PI_H_

#include <linux/types.h>

/*
 * The following functions are exported (but prefixed). Declare them here so
 * that LLVM does not complain it lacks the 'static' keyword (which, if
 * added, makes LLVM complain because the function is unused).
 */

u64 get_kaslr_seed(uintptr_t dtb_pa);
u64 get_kaslr_seed_zkr(const uintptr_t dtb_pa);
bool set_nokaslr_from_cmdline(uintptr_t dtb_pa);
u64 set_satp_mode_from_cmdline(uintptr_t dtb_pa);
u64 set_satp_mode_from_fdt(uintptr_t dtb_pa);

bool fdt_early_match_extension_isa(const void *fdt, const char *ext_name);

#endif /* _RISCV_PI_H_ */
