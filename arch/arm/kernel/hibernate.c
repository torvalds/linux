/*
 * Hibernation support specific for ARM
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <asm/sections.h>

/*
 * Image of the saved processor state
 *
 * coprocessor 15 registers(RW)
 */
struct saved_context_cortex_a8 {
	/* CR0 */
	u32 cssr;	/* Cache Size Selection */
	/* CR1 */
	u32 cr;		/* Control */
	u32 cacr;	/* Coprocessor Access Control */
	/* CR2 */
	u32 ttb_0r;	/* Translation Table Base 0 */
	u32 ttb_1r;	/* Translation Table Base 1 */
	u32 ttbcr;	/* Translation Talbe Base Control */
	/* CR3 */
	u32 dacr;	/* Domain Access Control */
	/* CR5 */
	u32 d_fsr;	/* Data Fault Status */
	u32 i_fsr;	/* Instruction Fault Status */
	u32 d_afsr;	/* Data Auxilirary Fault Status */	 ;
	u32 i_afsr;	/* Instruction Auxilirary Fault Status */;
	/* CR6 */
	u32 d_far;	/* Data Fault Address */
	u32 i_far;	/* Instruction Fault Address */
	/* CR7 */
	u32 par;	/* Physical Address */
	/* CR9 */	/* FIXME: Are they necessary? */
	u32 pmcontrolr;	/* Performance Monitor Control */
	u32 cesr;	/* Count Enable Set */
	u32 cecr;	/* Count Enable Clear */
	u32 ofsr;	/* Overflow Flag Status */
	u32 sir;	/* Software Increment */
	u32 pcsr;	/* Performance Counter Selection */
	u32 ccr;	/* Cycle Count */
	u32 esr;	/* Event Selection */
	u32 pmcountr;	/* Performance Monitor Count */
	u32 uer;	/* User Enable */
	u32 iesr;	/* Interrupt Enable Set */
	u32 iecr;	/* Interrupt Enable Clear */
	u32 l2clr;	/* L2 Cache Lockdown */
	/* CR10 */
	u32 d_tlblr;	/* Data TLB Lockdown Register */
	u32 i_tlblr;	/* Instruction TLB Lockdown Register */
	u32 prrr;	/* Primary Region Remap Register */
	u32 nrrr;	/* Normal Memory Remap Register */
	/* CR11 */
	u32 pleuar;	/* PLE User Accessibility */
	u32 plecnr;	/* PLE Channel Number */
	u32 plecr;	/* PLE Control */
	u32 pleisar;	/* PLE Internal Start Address */
	u32 pleiear;	/* PLE Internal End Address */
	u32 plecidr;	/* PLE Context ID */
	/* CR12 */
	u32 snsvbar;	/* Secure or Nonsecure Vector Base Address */
	/* CR13 */
	u32 fcse;	/* FCSE PID */
	u32 cid;	/* Context ID */
	u32 urwtpid;	/* User read/write Thread and Process ID */
	u32 urotpid;	/* User read-only Thread and Process ID */
	u32 potpid;	/* Privileged only Thread and Process ID */
} __packed;

struct saved_context_cortex_a9 {
	/* CR0 */
	u32 cssr;	/* Cache Size Selection */
	/* CR1 */
	u32 cr;
	u32 actlr;
	u32 cacr;
	u32 sder;
	u32 vcr;
	/* CR2 */
	u32 ttb_0r;	/* Translation Table Base 0 */
	u32 ttb_1r;	/* Translation Table Base 1 */
	u32 ttbcr;	/* Translation Talbe Base Control */
	/* CR3 */
	u32 dacr;	/* Domain Access Control */
	/* CR5 */
	u32 d_fsr;	/* Data Fault Status */
	u32 i_fsr;	/* Instruction Fault Status */
	u32 d_afsr;	/* Data Auxilirary Fault Status */	 ;
	u32 i_afsr;	/* Instruction Auxilirary Fault Status */;
	/* CR6 */
	u32 d_far;	/* Data Fault Address */
	u32 i_far;	/* Instruction Fault Address */
	/* CR7 */
	u32 par;	/* Physical Address */
	/* CR9 */	/* FIXME: Are they necessary? */
	u32 pmcontrolr;	/* Performance Monitor Control */
	u32 cesr;	/* Count Enable Set */
	u32 cecr;	/* Count Enable Clear */
	u32 ofsr;	/* Overflow Flag Status */
	u32 pcsr;	/* Performance Counter Selection */
	u32 ccr;	/* Cycle Count */
	u32 esr;	/* Event Selection */
	u32 pmcountr;	/* Performance Monitor Count */
	u32 uer;	/* User Enable */
	u32 iesr;	/* Interrupt Enable Set */
	u32 iecr;	/* Interrupt Enable Clear */
	/* CR10 */
	u32 d_tlblr;	/* Data TLB Lockdown Register */
	u32 prrr;	/* Primary Region Remap Register */
	u32 nrrr;	/* Normal Memory Remap Register */
	/* CR11 */
	/* CR12 */
	u32 vbar;
	u32 mvbar;
	u32 vir;
	/* CR13 */
	u32 fcse;	/* FCSE PID */
	u32 cid;	/* Context ID */
	u32 urwtpid;	/* User read/write Thread and Process ID */
	u32 urotpid;	/* User read-only Thread and Process ID */
	u32 potpid;	/* Privileged only Thread and Process ID */
	/* CR15 */
	u32 mtlbar;
} __packed;

union saved_context {
	struct saved_context_cortex_a8 cortex_a8;
	struct saved_context_cortex_a9 cortex_a9;
};

/* Used in hibernate_asm.S */
#define USER_CONTEXT_SIZE (15 * sizeof(u32))
unsigned long saved_context_r0[USER_CONTEXT_SIZE];
unsigned long saved_cpsr;
unsigned long saved_context_r13_svc;
unsigned long saved_context_r14_svc;
unsigned long saved_spsr_svc;

static union saved_context saved_context;

/*
 * pfn_is_nosave - check if given pfn is in the 'nosave' section
 */
int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa_symbol(&__nosave_begin)
		>> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa_symbol(&__nosave_end))
		>> PAGE_SHIFT;

	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

#define PART_NUM_CORTEX_A8	(0xC08)
#define PART_NUM_CORTEX_A9	(0xC09)

static inline u32 arm_primary_part_number(void)
{
	u32 id;

	asm volatile ("mrc     p15, 0, %0, c0, c0, 0" : "=r"(id));

	/* Is this ARM? */
	if ((id & 0xff000000) != 0x41000000)
		return UINT_MAX;

	id >>= 4;
	id &= 0xfff;
	return id;
}

static inline void __save_processor_state_a8(
		struct saved_context_cortex_a8 *ctxt)
{
	/* CR0 */
	asm volatile ("mrc p15, 2, %0, c0, c0, 0" : "=r"(ctxt->cssr));
	/* CR1 */
	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(ctxt->cr));
	asm volatile ("mrc p15, 0, %0, c1, c0, 2" : "=r"(ctxt->cacr));
	/* CR2 */
	asm volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ctxt->ttb_0r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ctxt->ttb_1r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ctxt->ttbcr));
	/* CR3 */
	asm volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(ctxt->dacr));
	/* CR5 */
	asm volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(ctxt->d_fsr));
	asm volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(ctxt->i_fsr));
	asm volatile ("mrc p15, 0, %0, c5, c1, 0" : "=r"(ctxt->d_afsr));
	asm volatile ("mrc p15, 0, %0, c5, c1, 1" : "=r"(ctxt->i_afsr));
	/* CR6 */
	asm volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(ctxt->d_far));
	asm volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(ctxt->i_far));
	/* CR7 */
	asm volatile ("mrc p15, 0, %0, c7, c4, 0" : "=r"(ctxt->par));
	/* CR9 */
	asm volatile ("mrc p15, 0, %0, c9, c12, 0" : "=r"(ctxt->pmcontrolr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 1" : "=r"(ctxt->cesr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 2" : "=r"(ctxt->cecr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 3" : "=r"(ctxt->ofsr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 4" : "=r"(ctxt->sir));
	asm volatile ("mrc p15, 0, %0, c9, c12, 5" : "=r"(ctxt->pcsr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 0" : "=r"(ctxt->ccr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 1" : "=r"(ctxt->esr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 2" : "=r"(ctxt->pmcountr));
	asm volatile ("mrc p15, 0, %0, c9, c14, 0" : "=r"(ctxt->uer));
	asm volatile ("mrc p15, 0, %0, c9, c14, 1" : "=r"(ctxt->iesr));
	asm volatile ("mrc p15, 0, %0, c9, c14, 2" : "=r"(ctxt->iecr));
	asm volatile ("mrc p15, 1, %0, c9, c0, 0" : "=r"(ctxt->l2clr));
	/* CR10 */
	asm volatile ("mrc p15, 0, %0, c10, c0, 0" : "=r"(ctxt->d_tlblr));
	asm volatile ("mrc p15, 0, %0, c10, c0, 1" : "=r"(ctxt->i_tlblr));
	asm volatile ("mrc p15, 0, %0, c10, c2, 0" : "=r"(ctxt->prrr));
	asm volatile ("mrc p15, 0, %0, c10, c2, 1" : "=r"(ctxt->nrrr));
	/* CR11 */
	asm volatile ("mrc p15, 0, %0, c11, c1, 0" : "=r"(ctxt->pleuar));
	asm volatile ("mrc p15, 0, %0, c11, c2, 0" : "=r"(ctxt->plecnr));
	asm volatile ("mrc p15, 0, %0, c11, c4, 0" : "=r"(ctxt->plecr));
	asm volatile ("mrc p15, 0, %0, c11, c5, 0" : "=r"(ctxt->pleisar));
	asm volatile ("mrc p15, 0, %0, c11, c7, 0" : "=r"(ctxt->pleiear));
	asm volatile ("mrc p15, 0, %0, c11, c15, 0" : "=r"(ctxt->plecidr));
	/* CR12 */
	asm volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(ctxt->snsvbar));
	/* CR13 */
	asm volatile ("mrc p15, 0, %0, c13, c0, 0" : "=r"(ctxt->fcse));
	asm volatile ("mrc p15, 0, %0, c13, c0, 1" : "=r"(ctxt->cid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 2" : "=r"(ctxt->urwtpid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 3" : "=r"(ctxt->urotpid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r"(ctxt->potpid));
}

static inline void __save_processor_state_a9(
		struct saved_context_cortex_a9 *ctxt)
{
	/* CR0 */
	asm volatile ("mrc p15, 2, %0, c0, c0, 0" : "=r"(ctxt->cssr));
	/* CR1 */
	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(ctxt->cr));
	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r"(ctxt->actlr));
	asm volatile ("mrc p15, 0, %0, c1, c0, 2" : "=r"(ctxt->cacr));
#ifndef CONFIG_ARM_TRUSTZONE
	asm volatile ("mrc p15, 0, %0, c1, c1, 1" : "=r"(ctxt->sder));
	asm volatile ("mrc p15, 0, %0, c1, c1, 3" : "=r"(ctxt->vcr));
#endif
	/* CR2 */
	asm volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ctxt->ttb_0r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ctxt->ttb_1r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ctxt->ttbcr));
	/* CR3 */
	asm volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(ctxt->dacr));
	/* CR5 */
	asm volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(ctxt->d_fsr));
	asm volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(ctxt->i_fsr));
	asm volatile ("mrc p15, 0, %0, c5, c1, 0" : "=r"(ctxt->d_afsr));
	asm volatile ("mrc p15, 0, %0, c5, c1, 1" : "=r"(ctxt->i_afsr));
	/* CR6 */
	asm volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(ctxt->d_far));
	asm volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(ctxt->i_far));
	/* CR7 */
	asm volatile ("mrc p15, 0, %0, c7, c4, 0" : "=r"(ctxt->par));
	/* CR9 */
	asm volatile ("mrc p15, 0, %0, c9, c12, 0" : "=r"(ctxt->pmcontrolr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 1" : "=r"(ctxt->cesr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 2" : "=r"(ctxt->cecr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 3" : "=r"(ctxt->ofsr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 5" : "=r"(ctxt->pcsr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 0" : "=r"(ctxt->ccr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 1" : "=r"(ctxt->esr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 2" : "=r"(ctxt->pmcountr));
	asm volatile ("mrc p15, 0, %0, c9, c14, 0" : "=r"(ctxt->uer));
	asm volatile ("mrc p15, 0, %0, c9, c14, 1" : "=r"(ctxt->iesr));
	asm volatile ("mrc p15, 0, %0, c9, c14, 2" : "=r"(ctxt->iecr));
	/* CR10 */
	asm volatile ("mrc p15, 0, %0, c10, c0, 0" : "=r"(ctxt->d_tlblr));
	asm volatile ("mrc p15, 0, %0, c10, c2, 0" : "=r"(ctxt->prrr));
	asm volatile ("mrc p15, 0, %0, c10, c2, 1" : "=r"(ctxt->nrrr));
	/* CR11 */
	/* CR12 */
	asm volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(ctxt->vbar));
#ifndef CONFIG_ARM_TRUSTZONE
	asm volatile ("mrc p15, 0, %0, c12, c0, 1" : "=r"(ctxt->mvbar));
	asm volatile ("mrc p15, 0, %0, c12, c1, 1" : "=r"(ctxt->vir));
#endif
	/* CR13 */
	asm volatile ("mrc p15, 0, %0, c13, c0, 0" : "=r"(ctxt->fcse));
	asm volatile ("mrc p15, 0, %0, c13, c0, 1" : "=r"(ctxt->cid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 2" : "=r"(ctxt->urwtpid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 3" : "=r"(ctxt->urotpid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r"(ctxt->potpid));
	/* CR15*/
#ifndef CONFIG_ARM_TRUSTZONE
	asm volatile ("mrc p15, 5, %0, c15, c7, 2" : "=r"(ctxt->mtlbar));
#endif
}

static inline void __save_processor_state(union saved_context *ctxt)
{
	switch (arm_primary_part_number()) {
	case PART_NUM_CORTEX_A8:
		__save_processor_state_a8(&ctxt->cortex_a8);
		break;
	case PART_NUM_CORTEX_A9:
		__save_processor_state_a9(&ctxt->cortex_a9);
		break;
	default:
		WARN(true, "Hibernation is not supported for this processor.(%d)",
				arm_primary_part_number());
	}
}

static inline void __restore_processor_state_a8(
		struct saved_context_cortex_a8 *ctxt)
{
	/* CR0 */
	asm volatile ("mcr p15, 2, %0, c0, c0, 0" : : "r"(ctxt->cssr));
	/* CR1 */
	asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(ctxt->cr));
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
	/* CR2 */
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(ctxt->ttb_0r));
	asm volatile ("mcr p15, 0, %0, c2, c0, 1" : : "r"(ctxt->ttb_1r));
	asm volatile ("mcr p15, 0, %0, c2, c0, 2" : : "r"(ctxt->ttbcr));
	/* CR3 */
	asm volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(ctxt->dacr));
	/* CR5 */
	asm volatile ("mcr p15, 0, %0, c5, c0, 0" : : "r"(ctxt->d_fsr));
	asm volatile ("mcr p15, 0, %0, c5, c0, 1" : : "r"(ctxt->i_fsr));
	asm volatile ("mcr p15, 0, %0, c5, c1, 0" : : "r"(ctxt->d_afsr));
	asm volatile ("mcr p15, 0, %0, c5, c1, 1" : : "r"(ctxt->i_afsr));
	/* CR6 */
	asm volatile ("mcr p15, 0, %0, c6, c0, 0" : : "r"(ctxt->d_far));
	asm volatile ("mcr p15, 0, %0, c6, c0, 2" : : "r"(ctxt->i_far));
	/* CR7 */
	asm volatile ("mcr p15, 0, %0, c7, c4, 0" : : "r"(ctxt->par));
	/* CR9 */
	asm volatile ("mcr p15, 0, %0, c9, c12, 0" : : "r"(ctxt->pmcontrolr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 1" : : "r"(ctxt->cesr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 2" : : "r"(ctxt->cecr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 3" : : "r"(ctxt->ofsr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 4" : : "r"(ctxt->sir));
	asm volatile ("mcr p15, 0, %0, c9, c12, 5" : : "r"(ctxt->pcsr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 0" : : "r"(ctxt->ccr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 1" : : "r"(ctxt->esr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 2" : : "r"(ctxt->pmcountr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 0" : : "r"(ctxt->uer));
	asm volatile ("mcr p15, 0, %0, c9, c14, 1" : : "r"(ctxt->iesr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 2" : : "r"(ctxt->iecr));
	asm volatile ("mcr p15, 1, %0, c9, c0, 0" : : "r"(ctxt->l2clr));
	/* CR10 */
	asm volatile ("mcr p15, 0, %0, c10, c0, 0" : : "r"(ctxt->d_tlblr));
	asm volatile ("mcr p15, 0, %0, c10, c0, 1" : : "r"(ctxt->i_tlblr));
	asm volatile ("mcr p15, 0, %0, c10, c2, 0" : : "r"(ctxt->prrr));
	asm volatile ("mcr p15, 0, %0, c10, c2, 1" : : "r"(ctxt->nrrr));
	/* CR11 */
	asm volatile ("mcr p15, 0, %0, c11, c1, 0" : : "r"(ctxt->pleuar));
	asm volatile ("mcr p15, 0, %0, c11, c2, 0" : : "r"(ctxt->plecnr));
	asm volatile ("mcr p15, 0, %0, c11, c4, 0" : : "r"(ctxt->plecr));
	asm volatile ("mcr p15, 0, %0, c11, c5, 0" : : "r"(ctxt->pleisar));
	asm volatile ("mcr p15, 0, %0, c11, c7, 0" : : "r"(ctxt->pleiear));
	asm volatile ("mcr p15, 0, %0, c11, c15, 0" : : "r"(ctxt->plecidr));
	/* CR12 */
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->snsvbar));
	/* CR13 */
	asm volatile ("mcr p15, 0, %0, c13, c0, 0" : : "r"(ctxt->fcse));
	asm volatile ("mcr p15, 0, %0, c13, c0, 1" : : "r"(ctxt->cid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 2" : : "r"(ctxt->urwtpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 3" : : "r"(ctxt->urotpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 4" : : "r"(ctxt->potpid));
}

static inline void __restore_processor_state_a9(
		struct saved_context_cortex_a9 *ctxt)
{
	/* CR0 */
	asm volatile ("mcr p15, 2, %0, c0, c0, 0" : : "r"(ctxt->cssr));
	/* CR1 */
	asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(ctxt->cr));
	asm volatile ("mcr p15, 0, %0, c1, c0, 1" : : "r"(ctxt->actlr));
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
#ifndef CONFIG_ARM_TRUSTZONE
	asm volatile ("mcr p15, 0, %0, c1, c1, 1" : : "r"(ctxt->sder));
	asm volatile ("mcr p15, 0, %0, c1, c1, 3" : : "r"(ctxt->vcr));
#endif
	/* CR2 */
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(ctxt->ttb_0r));
	asm volatile ("mcr p15, 0, %0, c2, c0, 1" : : "r"(ctxt->ttb_1r));
	asm volatile ("mcr p15, 0, %0, c2, c0, 2" : : "r"(ctxt->ttbcr));
	/* CR3 */
	asm volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(ctxt->dacr));
	/* CR5 */
	asm volatile ("mcr p15, 0, %0, c5, c0, 0" : : "r"(ctxt->d_fsr));
	asm volatile ("mcr p15, 0, %0, c5, c0, 1" : : "r"(ctxt->i_fsr));
	asm volatile ("mcr p15, 0, %0, c5, c1, 0" : : "r"(ctxt->d_afsr));
	asm volatile ("mcr p15, 0, %0, c5, c1, 1" : : "r"(ctxt->i_afsr));
	/* CR6 */
	asm volatile ("mcr p15, 0, %0, c6, c0, 0" : : "r"(ctxt->d_far));
	asm volatile ("mcr p15, 0, %0, c6, c0, 2" : : "r"(ctxt->i_far));
	/* CR7 */
	asm volatile ("mcr p15, 0, %0, c7, c4, 0" : : "r"(ctxt->par));
	/* CR9 */
	asm volatile ("mcr p15, 0, %0, c9, c12, 0" : : "r"(ctxt->pmcontrolr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 1" : : "r"(ctxt->cesr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 2" : : "r"(ctxt->cecr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 3" : : "r"(ctxt->ofsr));
	asm volatile ("mcr p15, 0, %0, c9, c12, 5" : : "r"(ctxt->pcsr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 0" : : "r"(ctxt->ccr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 1" : : "r"(ctxt->esr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 2" : : "r"(ctxt->pmcountr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 0" : : "r"(ctxt->uer));
	asm volatile ("mcr p15, 0, %0, c9, c14, 1" : : "r"(ctxt->iesr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 2" : : "r"(ctxt->iecr));
	/* CR10 */
	asm volatile ("mcr p15, 0, %0, c10, c0, 0" : : "r"(ctxt->d_tlblr));
	asm volatile ("mcr p15, 0, %0, c10, c2, 0" : : "r"(ctxt->prrr));
	asm volatile ("mcr p15, 0, %0, c10, c2, 1" : : "r"(ctxt->nrrr));
	/* CR11 */
	/* CR12 */
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->vbar));
#ifndef CONFIG_ARM_TRUSTZONE
	asm volatile ("mcr p15, 0, %0, c12, c0, 1" : : "r"(ctxt->mvbar));
	asm volatile ("mcr p15, 0, %0, c12, c1, 1" : : "r"(ctxt->vir));
#endif
	/* CR13 */
	asm volatile ("mcr p15, 0, %0, c13, c0, 0" : : "r"(ctxt->fcse));
	asm volatile ("mcr p15, 0, %0, c13, c0, 1" : : "r"(ctxt->cid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 2" : : "r"(ctxt->urwtpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 3" : : "r"(ctxt->urotpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 4" : : "r"(ctxt->potpid));
	/* CR15 */
#ifndef CONFIG_ARM_TRUSTZONE
	asm volatile ("mcr p15, 5, %0, c15, c7, 2" : : "r"(ctxt->mtlbar));
#endif
}

static inline void __restore_processor_state(union saved_context *ctxt)
{
	switch (arm_primary_part_number()) {
	case PART_NUM_CORTEX_A8:
		__restore_processor_state_a8(&ctxt->cortex_a8);
		break;
	case PART_NUM_CORTEX_A9:
		__restore_processor_state_a9(&ctxt->cortex_a9);
		break;
	default:
		WARN(true, "Hibernation is not supported for this processor.(%d)",
				arm_primary_part_number());
	}
}

void save_processor_state(void)
{
	preempt_disable();
	__save_processor_state(&saved_context);
}

void restore_processor_state(void)
{
	__restore_processor_state(&saved_context);
	preempt_enable();
}
