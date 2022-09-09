/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef	__ASM_PATCHING_H
#define	__ASM_PATCHING_H

#include <linux/types.h>

int aarch64_insn_read(void *addr, u32 *insnp);
int aarch64_insn_write(void *addr, u32 insn);

int aarch64_insn_patch_text_nosync(void *addr, u32 insn);
int aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt);

#endif	/* __ASM_PATCHING_H */
