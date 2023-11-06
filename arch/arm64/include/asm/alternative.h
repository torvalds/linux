/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ALTERNATIVE_H
#define __ASM_ALTERNATIVE_H

#include <asm/alternative-macros.h>

#ifndef __ASSEMBLY__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>

struct alt_instr {
	s32 orig_offset;	/* offset to original instruction */
	s32 alt_offset;		/* offset to replacement instruction */
	u16 cpucap;		/* cpucap bit set for replacement */
	u8  orig_len;		/* size of original instruction(s) */
	u8  alt_len;		/* size of new instruction(s), <= orig_len */
};

typedef void (*alternative_cb_t)(struct alt_instr *alt,
				 __le32 *origptr, __le32 *updptr, int nr_inst);

void __init apply_boot_alternatives(void);
void __init apply_alternatives_all(void);
bool alternative_is_applied(u16 cpucap);

#ifdef CONFIG_MODULES
void apply_alternatives_module(void *start, size_t length);
#else
static inline void apply_alternatives_module(void *start, size_t length) { }
#endif

void alt_cb_patch_nops(struct alt_instr *alt, __le32 *origptr,
		       __le32 *updptr, int nr_inst);

#endif /* __ASSEMBLY__ */
#endif /* __ASM_ALTERNATIVE_H */
