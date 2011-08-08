#ifndef _ASM_SMTC_MT_H
#define _ASM_SMTC_MT_H

/*
 * Definitions for SMTC multitasking on MIPS MT cores
 */

#include <asm/mips_mt.h>
#include <asm/smtc_ipi.h>

/*
 * System-wide SMTC status information
 */

extern unsigned int smtc_status;

#define SMTC_TLB_SHARED	0x00000001
#define SMTC_MTC_ACTIVE	0x00000002

/*
 * TLB/ASID Management information
 */

#define MAX_SMTC_TLBS 2
#define MAX_SMTC_ASIDS 256
#if NR_CPUS <= 8
typedef char asiduse;
#else
#if NR_CPUS <= 16
typedef short asiduse;
#else
typedef long asiduse;
#endif
#endif

extern asiduse smtc_live_asid[MAX_SMTC_TLBS][MAX_SMTC_ASIDS];

struct mm_struct;
struct task_struct;

void smtc_get_new_mmu_context(struct mm_struct *mm, unsigned long cpu);
void self_ipi(struct smtc_ipi *);
void smtc_flush_tlb_asid(unsigned long asid);
extern int smtc_build_cpu_map(int startslot);
extern void smtc_prepare_cpus(int cpus);
extern void smtc_smp_finish(void);
extern void smtc_boot_secondary(int cpu, struct task_struct *t);
extern void smtc_cpus_done(void);
extern void smtc_init_secondary(void);


/*
 * Sharing the TLB between multiple VPEs means that the
 * "random" index selection function is not allowed to
 * select the current value of the Index register. To
 * avoid additional TLB pressure, the Index registers
 * are "parked" with an non-Valid value.
 */

#define PARKED_INDEX	((unsigned int)0x80000000)

/*
 * Define low-level interrupt mask for IPIs, if necessary.
 * By default, use SW interrupt 1, which requires no external
 * hardware support, but which works only for single-core
 * MIPS MT systems.
 */
#ifndef MIPS_CPU_IPI_IRQ
#define MIPS_CPU_IPI_IRQ 1
#endif

#endif /*  _ASM_SMTC_MT_H */
