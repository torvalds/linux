/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_TEXT_PATCHING_H
#define _ASM_S390_TEXT_PATCHING_H

#include <asm/barrier.h>

static __always_inline void sync_core(void)
{
	bcr_serialize();
}

void text_poke_sync(void);
void text_poke_sync_lock(void);

#endif /* _ASM_S390_TEXT_PATCHING_H */
