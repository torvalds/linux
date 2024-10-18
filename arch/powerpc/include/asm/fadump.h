/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Firmware Assisted dump header file.
 *
 * Copyright 2011 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#ifndef _ASM_POWERPC_FADUMP_H
#define _ASM_POWERPC_FADUMP_H

#ifdef CONFIG_FA_DUMP

extern int crashing_cpu;

extern int is_fadump_memory_area(u64 addr, ulong size);
extern int setup_fadump(void);
extern int is_fadump_active(void);
extern int should_fadump_crash(void);
extern void crash_fadump(struct pt_regs *, const char *);
extern void fadump_cleanup(void);
extern void fadump_append_bootargs(void);

#else	/* CONFIG_FA_DUMP */
static inline int is_fadump_active(void) { return 0; }
static inline int should_fadump_crash(void) { return 0; }
static inline void crash_fadump(struct pt_regs *regs, const char *str) { }
static inline void fadump_cleanup(void) { }
static inline void fadump_append_bootargs(void) { }
#endif /* !CONFIG_FA_DUMP */

#if defined(CONFIG_FA_DUMP) || defined(CONFIG_PRESERVE_FA_DUMP)
extern int early_init_dt_scan_fw_dump(unsigned long node, const char *uname,
				      int depth, void *data);
extern int fadump_reserve_mem(void);
#endif

#if defined(CONFIG_FA_DUMP) && defined(CONFIG_CMA)
void fadump_cma_init(void);
#else
static inline void fadump_cma_init(void) { }
#endif

#endif /* _ASM_POWERPC_FADUMP_H */
