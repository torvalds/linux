// SPDX-License-Identifier: GPL-2.0

#ifndef __ARM64_ASM_SETUP_H
#define __ARM64_ASM_SETUP_H

#include <uapi/asm/setup.h>

void *get_early_fdt_ptr(void);
void early_fdt_map(u64 dt_phys);

/*
 * These two variables are used in the head.S file.
 */
extern phys_addr_t __fdt_pointer __initdata;
extern u64 __cacheline_aligned boot_args[4];

#endif
