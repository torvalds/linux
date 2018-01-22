/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __M32R_KMAP_TYPES_H
#define __M32R_KMAP_TYPES_H

#ifdef CONFIG_DEBUG_HIGHMEM
#define  __WITH_KM_FENCE
#endif

#include <asm-generic/kmap_types.h>

#undef __WITH_KM_FENCE

#endif /* __M32R_KMAP_TYPES_H */
