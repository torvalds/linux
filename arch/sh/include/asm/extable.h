/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_EXTABLE_H
#define __ASM_SH_EXTABLE_H

#include <asm-generic/extable.h>

#if defined(CONFIG_SUPERH64) && defined(CONFIG_MMU)
#define ARCH_HAS_SEARCH_EXTABLE
#endif

#endif
