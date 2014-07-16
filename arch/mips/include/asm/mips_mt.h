/*
 * Definitions and decalrations for MIPS MT support that are common between
 * the VSMP, and AP/SP kernel models.
 */
#ifndef __ASM_MIPS_MT_H
#define __ASM_MIPS_MT_H

#include <linux/cpumask.h>

/*
 * How many VPEs and TCs is Linux allowed to use?  0 means no limit.
 */
extern int tclimit;
extern int vpelimit;

extern cpumask_t mt_fpu_cpumask;
extern unsigned long mt_fpemul_threshold;

extern void mips_mt_regdump(unsigned long previous_mvpcontrol_value);

#ifdef CONFIG_MIPS_MT
extern void mips_mt_set_cpuoptions(void);
#else
static inline void mips_mt_set_cpuoptions(void) { }
#endif

struct class;
extern struct class *mt_class;

#endif /* __ASM_MIPS_MT_H */
