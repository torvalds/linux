// SPDX-License-Identifier: GPL-2.0

#ifndef __ARM64_ASM_SETUP_H
#define __ARM64_ASM_SETUP_H

#include <uapi/asm/setup.h>

void *get_early_fdt_ptr(void);
void early_fdt_map(u64 dt_phys);

#endif
