/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TLBFLUSH_H
#define _ASM_POWERPC_TLBFLUSH_H

#ifdef CONFIG_PPC_BOOK3S
#include <asm/book3s/tlbflush.h>
#else
#include <asm/nohash/tlbflush.h>
#endif /* !CONFIG_PPC_BOOK3S */

#endif /* _ASM_POWERPC_TLBFLUSH_H */
