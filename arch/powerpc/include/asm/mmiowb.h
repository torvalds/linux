/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_MMIOWB_H
#define _ASM_POWERPC_MMIOWB_H

#ifdef CONFIG_MMIOWB

#include <linux/compiler.h>
#include <asm/barrier.h>
#include <asm/paca.h>

#define arch_mmiowb_state()	(&local_paca->mmiowb_state)
#define mmiowb()		mb()

#endif /* CONFIG_MMIOWB */

#include <asm-generic/mmiowb.h>

#endif	/* _ASM_POWERPC_MMIOWB_H */
