/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_CHECKSUM_H
#define ___ASM_SPARC_CHECKSUM_H
#define _HAVE_ARCH_COPY_AND_CSUM_FROM_USER
#if defined(__sparc__) && defined(__arch64__)
#include <asm/checksum_64.h>
#else
#include <asm/checksum_32.h>
#endif
#endif
