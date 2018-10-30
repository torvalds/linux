/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SH_UNALIGNED_H
#define _ASM_SH_UNALIGNED_H

#ifdef CONFIG_CPU_SH4A
/* SH-4A can handle unaligned loads in a relatively neutered fashion. */
#include <asm/unaligned-sh4a.h>
#else
/* Otherwise, SH can't handle unaligned accesses. */
#include <asm-generic/unaligned.h>
#endif

#endif /* _ASM_SH_UNALIGNED_H */
