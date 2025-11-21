/* SPDX-License-Identifier: GPL-2.0 */
#ifndef XMON_BPTS_H
#define XMON_BPTS_H

#define NBPTS	256
#ifndef __ASSEMBLER__
#include <asm/inst.h>
#define BPT_SIZE	(sizeof(ppc_inst_t) * 2)
#define BPT_WORDS	(BPT_SIZE / sizeof(ppc_inst_t))

extern unsigned int bpt_table[NBPTS * BPT_WORDS];
#endif /* __ASSEMBLER__ */

#endif /* XMON_BPTS_H */
