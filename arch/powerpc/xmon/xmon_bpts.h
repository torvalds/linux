/* SPDX-License-Identifier: GPL-2.0 */
#ifndef XMON_BPTS_H
#define XMON_BPTS_H

#define NBPTS	256
#ifndef __ASSEMBLY__
#define BPT_SIZE	(sizeof(unsigned int) * 2)
#define BPT_WORDS	(BPT_SIZE / sizeof(unsigned int))

extern unsigned int bpt_table[NBPTS * BPT_WORDS];

#endif /* __ASSEMBLY__ */

#endif /* XMON_BPTS_H */
