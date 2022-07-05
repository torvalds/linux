/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARCHRANDOM_H
#define _ASM_ARCHRANDOM_H

static inline bool __init smccc_probe_trng(void)
{
	return false;
}

#include <asm-generic/archrandom.h>

#endif /* _ASM_ARCHRANDOM_H */
