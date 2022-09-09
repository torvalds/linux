/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux performance counter support for ARC
 *
 * Copyright (C) 2014-2015 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2011-2013 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASM_PERF_EVENT_H
#define __ASM_PERF_EVENT_H

/* Max number of counters that PCT block may ever have */
#define ARC_PERF_MAX_COUNTERS	32

#define ARC_REG_CC_BUILD	0xF6
#define ARC_REG_CC_INDEX	0x240
#define ARC_REG_CC_NAME0	0x241
#define ARC_REG_CC_NAME1	0x242

#define ARC_REG_PCT_BUILD	0xF5
#define ARC_REG_PCT_COUNTL	0x250
#define ARC_REG_PCT_COUNTH	0x251
#define ARC_REG_PCT_SNAPL	0x252
#define ARC_REG_PCT_SNAPH	0x253
#define ARC_REG_PCT_CONFIG	0x254
#define ARC_REG_PCT_CONTROL	0x255
#define ARC_REG_PCT_INDEX	0x256
#define ARC_REG_PCT_INT_CNTL	0x25C
#define ARC_REG_PCT_INT_CNTH	0x25D
#define ARC_REG_PCT_INT_CTRL	0x25E
#define ARC_REG_PCT_INT_ACT	0x25F

#define ARC_REG_PCT_CONFIG_USER	(1 << 18)	/* count in user mode */
#define ARC_REG_PCT_CONFIG_KERN	(1 << 19)	/* count in kernel mode */

#define ARC_REG_PCT_CONTROL_CC	(1 << 16)	/* clear counts */
#define ARC_REG_PCT_CONTROL_SN	(1 << 17)	/* snapshot */

struct arc_reg_pct_build {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int m:8, c:8, r:5, i:1, s:2, v:8;
#else
	unsigned int v:8, s:2, i:1, r:5, c:8, m:8;
#endif
};

struct arc_reg_cc_build {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int c:16, r:8, v:8;
#else
	unsigned int v:8, r:8, c:16;
#endif
};

#define PERF_COUNT_ARC_DCLM	(PERF_COUNT_HW_MAX + 0)
#define PERF_COUNT_ARC_DCSM	(PERF_COUNT_HW_MAX + 1)
#define PERF_COUNT_ARC_ICM	(PERF_COUNT_HW_MAX + 2)
#define PERF_COUNT_ARC_BPOK	(PERF_COUNT_HW_MAX + 3)
#define PERF_COUNT_ARC_EDTLB	(PERF_COUNT_HW_MAX + 4)
#define PERF_COUNT_ARC_EITLB	(PERF_COUNT_HW_MAX + 5)
#define PERF_COUNT_ARC_LDC	(PERF_COUNT_HW_MAX + 6)
#define PERF_COUNT_ARC_STC	(PERF_COUNT_HW_MAX + 7)

#define PERF_COUNT_ARC_HW_MAX	(PERF_COUNT_HW_MAX + 8)

#ifdef CONFIG_PERF_EVENTS
#define perf_arch_bpf_user_pt_regs(regs) (struct user_regs_struct *)regs
#endif

#endif /* __ASM_PERF_EVENT_H */
