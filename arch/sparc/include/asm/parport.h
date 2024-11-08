/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ___ASM_SPARC_PARPORT_H
#define ___ASM_SPARC_PARPORT_H

#if defined(__sparc__) && defined(__arch64__)
#include <asm/parport_64.h>
#else
#include <asm-generic/parport.h>
#endif
#endif

