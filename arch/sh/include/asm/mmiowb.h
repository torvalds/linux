/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_MMIOWB_H
#define __ASM_SH_MMIOWB_H

#include <asm/barrier.h>

/* synco on SH-4A, otherwise a nop */
#define mmiowb()			wmb()

#include <asm-generic/mmiowb.h>

#endif	/* __ASM_SH_MMIOWB_H */
