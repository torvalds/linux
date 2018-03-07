/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LIBGCC_H
#define __ASM_LIBGCC_H

#include <asm/byteorder.h>

typedef int word_type __attribute__ ((mode (__word__)));

#ifdef __BIG_ENDIAN
struct DWstruct {
	int high, low;
};

struct TWstruct {
	long long high, low;
};
#elif defined(__LITTLE_ENDIAN)
struct DWstruct {
	int low, high;
};

struct TWstruct {
	long long low, high;
};
#else
#error I feel sick.
#endif

typedef union {
	struct DWstruct s;
	long long ll;
} DWunion;

#if defined(CONFIG_64BIT) && defined(CONFIG_CPU_MIPSR6)
typedef int ti_type __attribute__((mode(TI)));

typedef union {
	struct TWstruct s;
	ti_type ti;
} TWunion;
#endif

#endif /* __ASM_LIBGCC_H */
