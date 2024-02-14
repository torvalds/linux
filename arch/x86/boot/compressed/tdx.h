/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_TDX_H
#define BOOT_COMPRESSED_TDX_H

#include <linux/types.h>

#ifdef CONFIG_INTEL_TDX_GUEST
void early_tdx_detect(void);
#else
static inline void early_tdx_detect(void) { };
#endif

#endif /* BOOT_COMPRESSED_TDX_H */
