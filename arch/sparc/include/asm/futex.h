/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_FUTEX_H
#define ___ASM_SPARC_FUTEX_H
#if defined(__sparc__) && defined(__arch64__)
#include <asm/futex_64.h>
#else
#include <asm/futex_32.h>
#endif
#endif
