// SPDX-License-Identifier: GPL-2.0-or-later
/* 
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 *
 *    Copyright (C) 2000-2001 John Marvin <jsm at parisc-linux.org>
 *    Copyright (C) 2000 David Huggins-Daines <dhd with pobox.org>
 *    Copyright (C) 2000 Sam Creasey <sammy@sammy.net>
 *    Copyright (C) 2000 Grant Grundler <grundler with parisc-linux.org>
 *    Copyright (C) 2001 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2001 Richard Hirst <rhirst at parisc-linux.org>
 *    Copyright (C) 2002 Randolph Chung <tausq with parisc-linux.org>
 *    Copyright (C) 2003 James Bottomley <jejb at parisc-linux.org>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <linux/kbuild.h>
#include <linux/pgtable.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/pdc.h>
#include <linux/uaccess.h>

#ifdef CONFIG_64BIT
#define FRAME_SIZE	128
#else
#define FRAME_SIZE	64
#endif
#define FRAME_ALIGN	64

/* Add FRAME_SIZE to the size x and align it to y. All definitions
 * that use align_frame will include space for a frame.
 */
#define align_frame(x,y) (((x)+FRAME_SIZE+(y)-1) - (((x)+(y)-1)%(y)))

int main(void)
{
	DEFINE(TASK_THREAD_INFO, offsetof(struct task_struct, stack));
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_SIGPENDING, offsetof(struct task_struct, pending));
	DEFINE(TASK_PTRACE, offsetof(struct task_struct, ptrace));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_PERSONALITY, offsetof(struct task_struct, personality));
	DEFINE(TASK_PID, offsetof(struct task_struct, pid));
	BLANK();
	DEFINE(TASK_REGS, offsetof(struct task_struct, thread.regs));
	DEFINE(TASK_PT_PSW, offsetof(struct task_struct, thread.regs.gr[ 0]));
	DEFINE(TASK_PT_GR1, offsetof(struct task_struct, thread.regs.gr[ 1]));
	DEFINE(TASK_PT_GR2, offsetof(struct task_struct, thread.regs.gr[ 2]));
	DEFINE(TASK_PT_GR3, offsetof(struct task_struct, thread.regs.gr[ 3]));
	DEFINE(TASK_PT_GR4, offsetof(struct task_struct, thread.regs.gr[ 4]));
	DEFINE(TASK_PT_GR5, offsetof(struct task_struct, thread.regs.gr[ 5]));
	DEFINE(TASK_PT_GR6, offsetof(struct task_struct, thread.regs.gr[ 6]));
	DEFINE(TASK_PT_GR7, offsetof(struct task_struct, thread.regs.gr[ 7]));
	DEFINE(TASK_PT_GR8, offsetof(struct task_struct, thread.regs.gr[ 8]));
	DEFINE(TASK_PT_GR9, offsetof(struct task_struct, thread.regs.gr[ 9]));
	DEFINE(TASK_PT_GR10, offsetof(struct task_struct, thread.regs.gr[10]));
	DEFINE(TASK_PT_GR11, offsetof(struct task_struct, thread.regs.gr[11]));
	DEFINE(TASK_PT_GR12, offsetof(struct task_struct, thread.regs.gr[12]));
	DEFINE(TASK_PT_GR13, offsetof(struct task_struct, thread.regs.gr[13]));
	DEFINE(TASK_PT_GR14, offsetof(struct task_struct, thread.regs.gr[14]));
	DEFINE(TASK_PT_GR15, offsetof(struct task_struct, thread.regs.gr[15]));
	DEFINE(TASK_PT_GR16, offsetof(struct task_struct, thread.regs.gr[16]));
	DEFINE(TASK_PT_GR17, offsetof(struct task_struct, thread.regs.gr[17]));
	DEFINE(TASK_PT_GR18, offsetof(struct task_struct, thread.regs.gr[18]));
	DEFINE(TASK_PT_GR19, offsetof(struct task_struct, thread.regs.gr[19]));
	DEFINE(TASK_PT_GR20, offsetof(struct task_struct, thread.regs.gr[20]));
	DEFINE(TASK_PT_GR21, offsetof(struct task_struct, thread.regs.gr[21]));
	DEFINE(TASK_PT_GR22, offsetof(struct task_struct, thread.regs.gr[22]));
	DEFINE(TASK_PT_GR23, offsetof(struct task_struct, thread.regs.gr[23]));
	DEFINE(TASK_PT_GR24, offsetof(struct task_struct, thread.regs.gr[24]));
	DEFINE(TASK_PT_GR25, offsetof(struct task_struct, thread.regs.gr[25]));
	DEFINE(TASK_PT_GR26, offsetof(struct task_struct, thread.regs.gr[26]));
	DEFINE(TASK_PT_GR27, offsetof(struct task_struct, thread.regs.gr[27]));
	DEFINE(TASK_PT_GR28, offsetof(struct task_struct, thread.regs.gr[28]));
	DEFINE(TASK_PT_GR29, offsetof(struct task_struct, thread.regs.gr[29]));
	DEFINE(TASK_PT_GR30, offsetof(struct task_struct, thread.regs.gr[30]));
	DEFINE(TASK_PT_GR31, offsetof(struct task_struct, thread.regs.gr[31]));
	DEFINE(TASK_PT_FR0, offsetof(struct task_struct, thread.regs.fr[ 0]));
	DEFINE(TASK_PT_FR1, offsetof(struct task_struct, thread.regs.fr[ 1]));
	DEFINE(TASK_PT_FR2, offsetof(struct task_struct, thread.regs.fr[ 2]));
	DEFINE(TASK_PT_FR3, offsetof(struct task_struct, thread.regs.fr[ 3]));
	DEFINE(TASK_PT_FR4, offsetof(struct task_struct, thread.regs.fr[ 4]));
	DEFINE(TASK_PT_FR5, offsetof(struct task_struct, thread.regs.fr[ 5]));
	DEFINE(TASK_PT_FR6, offsetof(struct task_struct, thread.regs.fr[ 6]));
	DEFINE(TASK_PT_FR7, offsetof(struct task_struct, thread.regs.fr[ 7]));
	DEFINE(TASK_PT_FR8, offsetof(struct task_struct, thread.regs.fr[ 8]));
	DEFINE(TASK_PT_FR9, offsetof(struct task_struct, thread.regs.fr[ 9]));
	DEFINE(TASK_PT_FR10, offsetof(struct task_struct, thread.regs.fr[10]));
	DEFINE(TASK_PT_FR11, offsetof(struct task_struct, thread.regs.fr[11]));
	DEFINE(TASK_PT_FR12, offsetof(struct task_struct, thread.regs.fr[12]));
	DEFINE(TASK_PT_FR13, offsetof(struct task_struct, thread.regs.fr[13]));
	DEFINE(TASK_PT_FR14, offsetof(struct task_struct, thread.regs.fr[14]));
	DEFINE(TASK_PT_FR15, offsetof(struct task_struct, thread.regs.fr[15]));
	DEFINE(TASK_PT_FR16, offsetof(struct task_struct, thread.regs.fr[16]));
	DEFINE(TASK_PT_FR17, offsetof(struct task_struct, thread.regs.fr[17]));
	DEFINE(TASK_PT_FR18, offsetof(struct task_struct, thread.regs.fr[18]));
	DEFINE(TASK_PT_FR19, offsetof(struct task_struct, thread.regs.fr[19]));
	DEFINE(TASK_PT_FR20, offsetof(struct task_struct, thread.regs.fr[20]));
	DEFINE(TASK_PT_FR21, offsetof(struct task_struct, thread.regs.fr[21]));
	DEFINE(TASK_PT_FR22, offsetof(struct task_struct, thread.regs.fr[22]));
	DEFINE(TASK_PT_FR23, offsetof(struct task_struct, thread.regs.fr[23]));
	DEFINE(TASK_PT_FR24, offsetof(struct task_struct, thread.regs.fr[24]));
	DEFINE(TASK_PT_FR25, offsetof(struct task_struct, thread.regs.fr[25]));
	DEFINE(TASK_PT_FR26, offsetof(struct task_struct, thread.regs.fr[26]));
	DEFINE(TASK_PT_FR27, offsetof(struct task_struct, thread.regs.fr[27]));
	DEFINE(TASK_PT_FR28, offsetof(struct task_struct, thread.regs.fr[28]));
	DEFINE(TASK_PT_FR29, offsetof(struct task_struct, thread.regs.fr[29]));
	DEFINE(TASK_PT_FR30, offsetof(struct task_struct, thread.regs.fr[30]));
	DEFINE(TASK_PT_FR31, offsetof(struct task_struct, thread.regs.fr[31]));
	DEFINE(TASK_PT_SR0, offsetof(struct task_struct, thread.regs.sr[ 0]));
	DEFINE(TASK_PT_SR1, offsetof(struct task_struct, thread.regs.sr[ 1]));
	DEFINE(TASK_PT_SR2, offsetof(struct task_struct, thread.regs.sr[ 2]));
	DEFINE(TASK_PT_SR3, offsetof(struct task_struct, thread.regs.sr[ 3]));
	DEFINE(TASK_PT_SR4, offsetof(struct task_struct, thread.regs.sr[ 4]));
	DEFINE(TASK_PT_SR5, offsetof(struct task_struct, thread.regs.sr[ 5]));
	DEFINE(TASK_PT_SR6, offsetof(struct task_struct, thread.regs.sr[ 6]));
	DEFINE(TASK_PT_SR7, offsetof(struct task_struct, thread.regs.sr[ 7]));
	DEFINE(TASK_PT_IASQ0, offsetof(struct task_struct, thread.regs.iasq[0]));
	DEFINE(TASK_PT_IASQ1, offsetof(struct task_struct, thread.regs.iasq[1]));
	DEFINE(TASK_PT_IAOQ0, offsetof(struct task_struct, thread.regs.iaoq[0]));
	DEFINE(TASK_PT_IAOQ1, offsetof(struct task_struct, thread.regs.iaoq[1]));
	DEFINE(TASK_PT_CR27, offsetof(struct task_struct, thread.regs.cr27));
	DEFINE(TASK_PT_ORIG_R28, offsetof(struct task_struct, thread.regs.orig_r28));
	DEFINE(TASK_PT_KSP, offsetof(struct task_struct, thread.regs.ksp));
	DEFINE(TASK_PT_KPC, offsetof(struct task_struct, thread.regs.kpc));
	DEFINE(TASK_PT_SAR, offsetof(struct task_struct, thread.regs.sar));
	DEFINE(TASK_PT_IIR, offsetof(struct task_struct, thread.regs.iir));
	DEFINE(TASK_PT_ISR, offsetof(struct task_struct, thread.regs.isr));
	DEFINE(TASK_PT_IOR, offsetof(struct task_struct, thread.regs.ior));
	BLANK();
	DEFINE(TASK_SZ, sizeof(struct task_struct));
	/* TASK_SZ_ALGN includes space for a stack frame. */
	DEFINE(TASK_SZ_ALGN, align_frame(sizeof(struct task_struct), FRAME_ALIGN));
	BLANK();
	DEFINE(PT_PSW, offsetof(struct pt_regs, gr[ 0]));
	DEFINE(PT_GR1, offsetof(struct pt_regs, gr[ 1]));
	DEFINE(PT_GR2, offsetof(struct pt_regs, gr[ 2]));
	DEFINE(PT_GR3, offsetof(struct pt_regs, gr[ 3]));
	DEFINE(PT_GR4, offsetof(struct pt_regs, gr[ 4]));
	DEFINE(PT_GR5, offsetof(struct pt_regs, gr[ 5]));
	DEFINE(PT_GR6, offsetof(struct pt_regs, gr[ 6]));
	DEFINE(PT_GR7, offsetof(struct pt_regs, gr[ 7]));
	DEFINE(PT_GR8, offsetof(struct pt_regs, gr[ 8]));
	DEFINE(PT_GR9, offsetof(struct pt_regs, gr[ 9]));
	DEFINE(PT_GR10, offsetof(struct pt_regs, gr[10]));
	DEFINE(PT_GR11, offsetof(struct pt_regs, gr[11]));
	DEFINE(PT_GR12, offsetof(struct pt_regs, gr[12]));
	DEFINE(PT_GR13, offsetof(struct pt_regs, gr[13]));
	DEFINE(PT_GR14, offsetof(struct pt_regs, gr[14]));
	DEFINE(PT_GR15, offsetof(struct pt_regs, gr[15]));
	DEFINE(PT_GR16, offsetof(struct pt_regs, gr[16]));
	DEFINE(PT_GR17, offsetof(struct pt_regs, gr[17]));
	DEFINE(PT_GR18, offsetof(struct pt_regs, gr[18]));
	DEFINE(PT_GR19, offsetof(struct pt_regs, gr[19]));
	DEFINE(PT_GR20, offsetof(struct pt_regs, gr[20]));
	DEFINE(PT_GR21, offsetof(struct pt_regs, gr[21]));
	DEFINE(PT_GR22, offsetof(struct pt_regs, gr[22]));
	DEFINE(PT_GR23, offsetof(struct pt_regs, gr[23]));
	DEFINE(PT_GR24, offsetof(struct pt_regs, gr[24]));
	DEFINE(PT_GR25, offsetof(struct pt_regs, gr[25]));
	DEFINE(PT_GR26, offsetof(struct pt_regs, gr[26]));
	DEFINE(PT_GR27, offsetof(struct pt_regs, gr[27]));
	DEFINE(PT_GR28, offsetof(struct pt_regs, gr[28]));
	DEFINE(PT_GR29, offsetof(struct pt_regs, gr[29]));
	DEFINE(PT_GR30, offsetof(struct pt_regs, gr[30]));
	DEFINE(PT_GR31, offsetof(struct pt_regs, gr[31]));
	DEFINE(PT_FR0, offsetof(struct pt_regs, fr[ 0]));
	DEFINE(PT_FR1, offsetof(struct pt_regs, fr[ 1]));
	DEFINE(PT_FR2, offsetof(struct pt_regs, fr[ 2]));
	DEFINE(PT_FR3, offsetof(struct pt_regs, fr[ 3]));
	DEFINE(PT_FR4, offsetof(struct pt_regs, fr[ 4]));
	DEFINE(PT_FR5, offsetof(struct pt_regs, fr[ 5]));
	DEFINE(PT_FR6, offsetof(struct pt_regs, fr[ 6]));
	DEFINE(PT_FR7, offsetof(struct pt_regs, fr[ 7]));
	DEFINE(PT_FR8, offsetof(struct pt_regs, fr[ 8]));
	DEFINE(PT_FR9, offsetof(struct pt_regs, fr[ 9]));
	DEFINE(PT_FR10, offsetof(struct pt_regs, fr[10]));
	DEFINE(PT_FR11, offsetof(struct pt_regs, fr[11]));
	DEFINE(PT_FR12, offsetof(struct pt_regs, fr[12]));
	DEFINE(PT_FR13, offsetof(struct pt_regs, fr[13]));
	DEFINE(PT_FR14, offsetof(struct pt_regs, fr[14]));
	DEFINE(PT_FR15, offsetof(struct pt_regs, fr[15]));
	DEFINE(PT_FR16, offsetof(struct pt_regs, fr[16]));
	DEFINE(PT_FR17, offsetof(struct pt_regs, fr[17]));
	DEFINE(PT_FR18, offsetof(struct pt_regs, fr[18]));
	DEFINE(PT_FR19, offsetof(struct pt_regs, fr[19]));
	DEFINE(PT_FR20, offsetof(struct pt_regs, fr[20]));
	DEFINE(PT_FR21, offsetof(struct pt_regs, fr[21]));
	DEFINE(PT_FR22, offsetof(struct pt_regs, fr[22]));
	DEFINE(PT_FR23, offsetof(struct pt_regs, fr[23]));
	DEFINE(PT_FR24, offsetof(struct pt_regs, fr[24]));
	DEFINE(PT_FR25, offsetof(struct pt_regs, fr[25]));
	DEFINE(PT_FR26, offsetof(struct pt_regs, fr[26]));
	DEFINE(PT_FR27, offsetof(struct pt_regs, fr[27]));
	DEFINE(PT_FR28, offsetof(struct pt_regs, fr[28]));
	DEFINE(PT_FR29, offsetof(struct pt_regs, fr[29]));
	DEFINE(PT_FR30, offsetof(struct pt_regs, fr[30]));
	DEFINE(PT_FR31, offsetof(struct pt_regs, fr[31]));
	DEFINE(PT_SR0, offsetof(struct pt_regs, sr[ 0]));
	DEFINE(PT_SR1, offsetof(struct pt_regs, sr[ 1]));
	DEFINE(PT_SR2, offsetof(struct pt_regs, sr[ 2]));
	DEFINE(PT_SR3, offsetof(struct pt_regs, sr[ 3]));
	DEFINE(PT_SR4, offsetof(struct pt_regs, sr[ 4]));
	DEFINE(PT_SR5, offsetof(struct pt_regs, sr[ 5]));
	DEFINE(PT_SR6, offsetof(struct pt_regs, sr[ 6]));
	DEFINE(PT_SR7, offsetof(struct pt_regs, sr[ 7]));
	DEFINE(PT_IASQ0, offsetof(struct pt_regs, iasq[0]));
	DEFINE(PT_IASQ1, offsetof(struct pt_regs, iasq[1]));
	DEFINE(PT_IAOQ0, offsetof(struct pt_regs, iaoq[0]));
	DEFINE(PT_IAOQ1, offsetof(struct pt_regs, iaoq[1]));
	DEFINE(PT_CR27, offsetof(struct pt_regs, cr27));
	DEFINE(PT_ORIG_R28, offsetof(struct pt_regs, orig_r28));
	DEFINE(PT_KSP, offsetof(struct pt_regs, ksp));
	DEFINE(PT_KPC, offsetof(struct pt_regs, kpc));
	DEFINE(PT_SAR, offsetof(struct pt_regs, sar));
	DEFINE(PT_IIR, offsetof(struct pt_regs, iir));
	DEFINE(PT_ISR, offsetof(struct pt_regs, isr));
	DEFINE(PT_IOR, offsetof(struct pt_regs, ior));
	DEFINE(PT_SIZE, sizeof(struct pt_regs));
	/* PT_SZ_ALGN includes space for a stack frame. */
	DEFINE(PT_SZ_ALGN, align_frame(sizeof(struct pt_regs), FRAME_ALIGN));
	BLANK();
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
	DEFINE(TI_PRE_COUNT, offsetof(struct thread_info, preempt_count));
	DEFINE(THREAD_SZ, sizeof(struct thread_info));
	/* THREAD_SZ_ALGN includes space for a stack frame. */
	DEFINE(THREAD_SZ_ALGN, align_frame(sizeof(struct thread_info), FRAME_ALIGN));
	BLANK();
	DEFINE(ICACHE_BASE, offsetof(struct pdc_cache_info, ic_base));
	DEFINE(ICACHE_STRIDE, offsetof(struct pdc_cache_info, ic_stride));
	DEFINE(ICACHE_COUNT, offsetof(struct pdc_cache_info, ic_count));
	DEFINE(ICACHE_LOOP, offsetof(struct pdc_cache_info, ic_loop));
	DEFINE(DCACHE_BASE, offsetof(struct pdc_cache_info, dc_base));
	DEFINE(DCACHE_STRIDE, offsetof(struct pdc_cache_info, dc_stride));
	DEFINE(DCACHE_COUNT, offsetof(struct pdc_cache_info, dc_count));
	DEFINE(DCACHE_LOOP, offsetof(struct pdc_cache_info, dc_loop));
	DEFINE(ITLB_SID_BASE, offsetof(struct pdc_cache_info, it_sp_base));
	DEFINE(ITLB_SID_STRIDE, offsetof(struct pdc_cache_info, it_sp_stride));
	DEFINE(ITLB_SID_COUNT, offsetof(struct pdc_cache_info, it_sp_count));
	DEFINE(ITLB_OFF_BASE, offsetof(struct pdc_cache_info, it_off_base));
	DEFINE(ITLB_OFF_STRIDE, offsetof(struct pdc_cache_info, it_off_stride));
	DEFINE(ITLB_OFF_COUNT, offsetof(struct pdc_cache_info, it_off_count));
	DEFINE(ITLB_LOOP, offsetof(struct pdc_cache_info, it_loop));
	DEFINE(DTLB_SID_BASE, offsetof(struct pdc_cache_info, dt_sp_base));
	DEFINE(DTLB_SID_STRIDE, offsetof(struct pdc_cache_info, dt_sp_stride));
	DEFINE(DTLB_SID_COUNT, offsetof(struct pdc_cache_info, dt_sp_count));
	DEFINE(DTLB_OFF_BASE, offsetof(struct pdc_cache_info, dt_off_base));
	DEFINE(DTLB_OFF_STRIDE, offsetof(struct pdc_cache_info, dt_off_stride));
	DEFINE(DTLB_OFF_COUNT, offsetof(struct pdc_cache_info, dt_off_count));
	DEFINE(DTLB_LOOP, offsetof(struct pdc_cache_info, dt_loop));
	BLANK();
	DEFINE(TIF_BLOCKSTEP_PA_BIT, 31-TIF_BLOCKSTEP);
	DEFINE(TIF_SINGLESTEP_PA_BIT, 31-TIF_SINGLESTEP);
	BLANK();
	DEFINE(ASM_PMD_SHIFT, PMD_SHIFT);
	DEFINE(ASM_PGDIR_SHIFT, PGDIR_SHIFT);
	DEFINE(ASM_BITS_PER_PGD, BITS_PER_PGD);
	DEFINE(ASM_BITS_PER_PMD, BITS_PER_PMD);
	DEFINE(ASM_BITS_PER_PTE, BITS_PER_PTE);
	DEFINE(ASM_PMD_ENTRY, ((PAGE_OFFSET & PMD_MASK) >> PMD_SHIFT));
	DEFINE(ASM_PGD_ENTRY, PAGE_OFFSET >> PGDIR_SHIFT);
	DEFINE(ASM_PGD_ENTRY_SIZE, PGD_ENTRY_SIZE);
	DEFINE(ASM_PMD_ENTRY_SIZE, PMD_ENTRY_SIZE);
	DEFINE(ASM_PTE_ENTRY_SIZE, PTE_ENTRY_SIZE);
	DEFINE(ASM_PFN_PTE_SHIFT, PFN_PTE_SHIFT);
	DEFINE(ASM_PT_INITIAL, PT_INITIAL);
	BLANK();
	/* HUGEPAGE_SIZE is only used in vmlinux.lds.S to align kernel text
	 * and kernel data on physical huge pages */
#ifdef CONFIG_HUGETLB_PAGE
	DEFINE(HUGEPAGE_SIZE, 1UL << REAL_HPAGE_SHIFT);
#else
	DEFINE(HUGEPAGE_SIZE, PAGE_SIZE);
#endif
	BLANK();
	DEFINE(ASM_PDC_RESULT_SIZE, NUM_PDC_RESULT * sizeof(unsigned long));
	BLANK();
	return 0;
}
