/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_XOR_H
#define ___ASM_SPARC_XOR_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/xor_64.h>
#else
#include <asm/xor_32.h>
#endif
#endif
