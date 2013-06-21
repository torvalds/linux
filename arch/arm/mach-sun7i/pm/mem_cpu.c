/*
 * Hibernation support specific for ARM
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2006 Rafael J. Wysocki <rjw <at> sisk.pl>
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU <at> nokia.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#include "pm_types.h"
#include "./pm.h"

struct saved_context saved_context;

static struct saved_context default_copro_value = {
	/* CR0 */
	.cssr = 0x00000002,		 /* Cache Size Selection */
	/* CR1 */
#ifdef CORTEX_A8
	.cr = 0x00C52078,		/* Control */
	.acr = 0x00000002,		/* Auxiliary Control Register*/	
	.cacr = 0x00000000,		/* Coprocessor Access Control */
	.sccfgr = 0x00000000,		/* Secure Config Register*/	
	.scdbgenblr = 0x00000000,	/* Secure Debug Enable Register*/
	.nonscacctrlr= 0x00000000,	/* Nonsecure Access Control Register*/	
#elif defined(CORTEX_A9)
#elif defined(CORTEX_A7)
	.cr = 0x00C50878,		/* Control */
	.acr = 0x00006040,		/* Auxiliary Control Register: needed for smp*/	
	.cacr = 0x00000000,		/* Coprocessor Access Control */
	.sccfgr = 0x00000000,		/* Secure Config Register*/	
	.scdbgenblr = 0x00000000,	/* Secure Debug Enable Register*/
	.nonscacctrlr= 0x00000000,	/* Nonsecure Access Control Register*/	
#endif

	/* CR2 */
	.ttb_0r = 0x00000000,		/* Translation Table Base 0 */
	.ttb_1r = 0x00000000,		/* Translation Table Base 1 */
	.ttbcr = 0x00000000,		/* Translation Talbe Base Control */
	/* CR3 */
	.dacr = 0x00000000,		/* Domain Access Control */
	/* CR5 */
	.d_fsr = 0x00000000,		/* Data Fault Status */
	.i_fsr = 0x00000000,		/* Instruction Fault Status */
	.d_afsr = 0x00000000, 		/* Data Auxilirary Fault Status */
	.i_afsr = 0x00000000,		/* Instruction Auxilirary Fault Status */
	/* CR6 */
	.d_far = 0x00000000,		/* Data Fault Address */
	.i_far = 0x00000000,		/* Instruction Fault Address */
	/* CR7 */
	.par = 0x00000000,		/* Physical Address */
	/* CR9 */				/* FIXME: Are they necessary? */
	.pmcontrolr = 0x00000000,	/* Performance Monitor Control */
	.cesr = 0x00000000,		/* Count Enable Set */
	.cecr = 0x00000000,		/* Count Enable Clear */
	.ofsr = 0x00000000,		/* Overflow Flag Status */
#ifdef CORTEX_A8
	.sir = 0x00000000,		/* Software Increment */
#elif defined(CORTEX_A7)
	.sir = 0x00000000,		/* Software Increment */
#endif
	.pcsr = 0x00000000,		/* Performance Counter Selection */
	.ccr = 0x00000000,		/* Cycle Count */
	.esr = 0x00000000,		/* Event Selection */
	.pmcountr = 0x00000000,		/* Performance Monitor Count */
	.uer = 0x00000000,		/* User Enable */
	.iesr = 0x00000000,		/* Interrupt Enable Set */
	.iecr = 0x00000000,		/* Interrupt Enable Clear */
#ifdef CORTEX_A8
	.l2clr = 0x00000000,		/* L2 Cache Lockdown */
	.l2cauxctrlr = 0x00000042, 	/* L2 Cache Auxiliary Control */
#elif defined(CORTEX_A7)
#endif

	/* CR10 */
#ifdef CORTEX_A8
	.d_tlblr = 0x00000000,		/* Data TLB Lockdown Register */
	.i_tlblr = 0x00000000,		/* Instruction TLB Lockdown Register */
#elif defined(CORTEX_A7)
#endif
	.prrr = 0x000A81A4,		/* Primary Region Remap Register: tex[0], c, b = 010, + A => normal memory 
					*                        tex[0], c, b = 100 || 101, + 1 => strong order or device
					*/
	.nrrr = 0x44E048E0,		/* Normal Memory Remap Register */
	//.prrr = 0xFF0A81A8,		/* Primary Region Remap Register */
	//.nrrr = 0x40E040E0,		/* Normal Memory Remap Register */
	/* CR11 */
#ifdef CORTEX_A8
	.pleuar = 0x00000000,		/* PLE User Accessibility */
	.plecnr = 0x00000000,		/* PLE Channel Number */
	.plecr = 0x00000000,		/* PLE Control */
	.pleisar = 0x00000000,		/* PLE Internal Start Address */
	.pleiear = 0x00000000,		/* PLE Internal End Address */
	.plecidr = 0x00000000,		/* PLE Context ID */
#elif defined(CORTEX_A7)
#endif	

	/* CR12 */
#ifdef CORTEX_A8
	.snsvbar = 0x00000000,		/* Secure or Nonsecure Vector Base Address */
	.monvecbar = 0x00000000,	/*Monitor Vector Base*/
#elif defined(CORTEX_A9)
#elif defined(CORTEX_A7)

#endif	

	/* CR13 */
	.fcse = 0x00000000,		/* FCSE PID */
	.cid = 0x00000000,		/* Context ID */
	.urwtpid = 0x00000000,		/* User read/write Thread and Process ID */
	.urotpid = 0x00000000,		/* User read-only Thread and Process ID */
	.potpid = 0x00000000,		/* Privileged only Thread and Process ID */
	
};

/*__save_processor_state: store the co-processor state into to location point by ctxt
*@ctxt: indicate where to store co-processor state.
*/
void __save_processor_state(struct saved_context *ctxt)
{
	/* CR0 */
	//save_mem_status(0x101);
	//busy_waiting();
	asm volatile ("mrc p15, 2, %0, c0, c0, 0" : "=r"(ctxt->cssr));
	
	/* CR1 */
#ifdef CORTEX_A8
	//save_mem_status(0x102);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(ctxt->cr));
	asm volatile ("mrc p15, 0, %0, c1, c0, 2" : "=r"(ctxt->cacr));
#elif defined(CORTEX_A9)
	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(ctxt->cr));
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r"(ctxt->actlr));
	asm volatile ("mrc p15, 0, %0, c1, c0, 2" : "=r"(ctxt->cacr));
	asm volatile ("mrc p15, 0, %0, c1, c1, 1" : "=r"(ctxt->sder));
	asm volatile ("mrc p15, 0, %0, c1, c1, 3" : "=r"(ctxt->vcr));
#elif defined(CORTEX_A7)
	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(ctxt->cr));
	asm volatile ("mrc p15, 0, %0, c1, c0, 2" : "=r"(ctxt->cacr));
#endif

	/* CR2 */
	//save_mem_status(0x103);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ctxt->ttb_0r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(ctxt->ttb_1r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(ctxt->ttbcr));
	/* CR3 */
	//save_mem_status(0x104);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(ctxt->dacr));
	/* CR5 */
	//save_mem_status(0x105);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(ctxt->d_fsr));
	asm volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(ctxt->i_fsr));
	asm volatile ("mrc p15, 0, %0, c5, c1, 0" : "=r"(ctxt->d_afsr));
	asm volatile ("mrc p15, 0, %0, c5, c1, 1" : "=r"(ctxt->i_afsr));
	//save_mem_status(0x106);
	//busy_waiting();
	/* CR6 */
	asm volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(ctxt->d_far));
	asm volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(ctxt->i_far));
	/* CR7 */
	//save_mem_status(0x107);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c7, c4, 0" : "=r"(ctxt->par));
	/* CR9 */
	//save_mem_status(0x108);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c9, c12, 0" : "=r"(ctxt->pmcontrolr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 1" : "=r"(ctxt->cesr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 2" : "=r"(ctxt->cecr));
	asm volatile ("mrc p15, 0, %0, c9, c12, 3" : "=r"(ctxt->ofsr));
#ifdef CORTEX_A8
	//save_mem_status(0x109);
	//busy_waiting();
	//asm volatile ("mrc p15, 0, %0, c9, c12, 4" : "=r"(ctxt->sir));
#elif defined(CORTEX_A7)
	//busy_waiting();
	//asm volatile ("mrc p15, 0, %0, c9, c12, 4" : "=r"(ctxt->sir));
#endif
	//save_mem_status(0x10a);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c9, c12, 5" : "=r"(ctxt->pcsr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 0" : "=r"(ctxt->ccr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 1" : "=r"(ctxt->esr));
	asm volatile ("mrc p15, 0, %0, c9, c13, 2" : "=r"(ctxt->pmcountr));
	asm volatile ("mrc p15, 0, %0, c9, c14, 0" : "=r"(ctxt->uer));
	asm volatile ("mrc p15, 0, %0, c9, c14, 1" : "=r"(ctxt->iesr));
	asm volatile ("mrc p15, 0, %0, c9, c14, 2" : "=r"(ctxt->iecr));
#ifdef CORTEX_A8
	//save_mem_status(0x10b);
	//busy_waiting();
	asm volatile ("mrc p15, 1, %0, c9, c0, 0" : "=r"(ctxt->l2clr));
	asm volatile ("mrc p15, 1, %0, c9, c0, 2" : "=r"(ctxt->l2cauxctrlr));
#elif defined(CORTEX_A7)
#endif

	/* CR10 */
#ifdef CORTEX_A8
	//save_mem_status(0x10c);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c10, c0, 0" : "=r"(ctxt->d_tlblr));
	//save_mem_status(0x10d);
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c10, c0, 1" : "=r"(ctxt->i_tlblr));
#elif defined(CORTEX_A7)

#endif
	asm volatile ("mrc p15, 0, %0, c10, c2, 0" : "=r"(ctxt->prrr));
	asm volatile ("mrc p15, 0, %0, c10, c2, 1" : "=r"(ctxt->nrrr));
	
	/* CR11 */
#ifdef CORTEX_A8
	asm volatile ("mrc p15, 0, %0, c11, c1, 0" : "=r"(ctxt->pleuar));
	asm volatile ("mrc p15, 0, %0, c11, c2, 0" : "=r"(ctxt->plecnr));
	asm volatile ("mrc p15, 0, %0, c11, c4, 0" : "=r"(ctxt->plecr));
	asm volatile ("mrc p15, 0, %0, c11, c5, 0" : "=r"(ctxt->pleisar));
	asm volatile ("mrc p15, 0, %0, c11, c7, 0" : "=r"(ctxt->pleiear));
	asm volatile ("mrc p15, 0, %0, c11, c15, 0" : "=r"(ctxt->plecidr));
#elif defined(CORTEX_A7)
#endif

	/* CR12 */
#ifdef CORTEX_A8
	asm volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(ctxt->snsvbar));
	asm volatile ("mrc p15, 0, %0, c12, c0, 1" : "=r"(ctxt->monvecbar));	
#elif defined(CORTEX_A9)
	asm volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(ctxt->vbar));
	asm volatile ("mrc p15, 0, %0, c12, c0, 1" : "=r"(ctxt->mvbar));
	asm volatile ("mrc p15, 0, %0, c12, c1, 1" : "=r"(ctxt->vir));
#elif defined(CORTEX_A7)
	asm volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(ctxt->vbar));
	asm volatile ("mrc p15, 0, %0, c12, c0, 1" : "=r"(ctxt->mvbar));
	//asm volatile ("mrc p15, 0, %0, c12, c1, 0" : "=r"(ctxt->isr));
#endif

	/* CR13 */
	//save_mem_status(0x10e);
	//busy_waiting();
	//asm volatile ("mrc p15, 0, %0, c13, c0, 0" : "=r"(ctxt->fcse));
	asm volatile ("mrc p15, 0, %0, c13, c0, 1" : "=r"(ctxt->cid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 2" : "=r"(ctxt->urwtpid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 3" : "=r"(ctxt->urotpid));
	asm volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r"(ctxt->potpid));
	/* CR15*/
#ifdef CORTEX_A9
	//save_mem_status(0x10f);
	//busy_waiting();
	asm volatile ("mrc p15, 5, %0, c15, c7, 2" : "=r"(ctxt->mtlbar));
#endif
}

/*__restore_processor_state: restore the co-processor state according the info point by ctxt
*@ctxt: indicate where to get the backuped co-processor state.
*/
void __restore_processor_state(struct saved_context *ctxt)
{
	/* CR0 */
	asm volatile ("mcr p15, 2, %0, c0, c0, 0" : : "r"(ctxt->cssr));
	/* CR1 */
#if defined(CORTEX_A8)
	//busy_waiting();
	//asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(ctxt->cr)); //will effect visible addr space, cache, instruction prediction
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
#elif defined(CORTEX_A9)
	//asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(ctxt->cr));
	//busy_waiting();
	asm volatile ("mcr p15, 0, %0, c1, c0, 1" : : "r"(ctxt->actlr));
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
	asm volatile ("mcr p15, 0, %0, c1, c1, 1" : : "r"(ctxt->sder));
	asm volatile ("mcr p15, 0, %0, c1, c1, 3" : : "r"(ctxt->vcr));
#elif defined(CORTEX_A7)
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
#endif

	/* CR2 */
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(ctxt->ttb_0r));
	asm volatile ("dsb");
	asm volatile ("isb");
	asm volatile ("mcr p15, 0, %0, c2, c0, 1" : : "r"(ctxt->ttb_1r));
	asm volatile ("dsb");
	asm volatile ("isb");
	asm volatile ("mcr p15, 0, %0, c2, c0, 2" : : "r"(ctxt->ttbcr));
	asm volatile ("dsb");
	asm volatile ("isb");
	
	//flush_tlb_all();
	/* CR3 */
	asm volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(ctxt->dacr));
	/* CR7 */
	asm volatile ("mcr p15, 0, %0, c7, c4, 0" : : "r"(ctxt->par));
		
	/* CR9 */
	asm volatile ("mcr p15, 0, %0, c9, c12, 5" : : "r"(ctxt->pcsr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 0" : : "r"(ctxt->ccr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 1" : : "r"(ctxt->esr));
	asm volatile ("mcr p15, 0, %0, c9, c13, 2" : : "r"(ctxt->pmcountr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 0" : : "r"(ctxt->uer));
	asm volatile ("mcr p15, 0, %0, c9, c14, 1" : : "r"(ctxt->iesr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 2" : : "r"(ctxt->iecr));
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 1, %0, c9, c0, 0" : : "r"(ctxt->l2clr));
	asm volatile ("mcr p15, 1, %0, c9, c0, 2" : : "r"(ctxt->l2cauxctrlr)); 
#elif defined(CORTEX_A7)
#endif

	/* CR10 */
	asm volatile ("mcr p15, 0, %0, c10, c2, 0" : : "r"(ctxt->prrr));
	asm volatile ("mcr p15, 0, %0, c10, c2, 1" : : "r"(ctxt->nrrr));
	
	/* CR11 */
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 0, %0, c11, c1, 0" : : "r"(ctxt->pleuar));
	asm volatile ("mcr p15, 0, %0, c11, c2, 0" : : "r"(ctxt->plecnr));
	asm volatile ("mcr p15, 0, %0, c11, c4, 0" : : "r"(ctxt->plecr));
	asm volatile ("mcr p15, 0, %0, c11, c5, 0" : : "r"(ctxt->pleisar));
	asm volatile ("mcr p15, 0, %0, c11, c7, 0" : : "r"(ctxt->pleiear));
	asm volatile ("mcr p15, 0, %0, c11, c15, 0" : : "r"(ctxt->plecidr));
#elif defined(CORTEX_A7)
#endif

	/* CR12 */
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->snsvbar));
	asm volatile ("mcr p15, 0, %0, c12, c0, 1" : : "r"(ctxt->monvecbar));	
#elif defined(CORTEX_A9)
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->vbar));
	asm volatile ("mcr p15, 0, %0, c12, c0, 1" : : "r"(ctxt->mvbar));
	asm volatile ("mcr p15, 0, %0, c12, c1, 1" : : "r"(ctxt->vir));
#elif defined(CORTEX_A7)
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->vbar));
	asm volatile ("mcr p15, 0, %0, c12, c0, 1" : : "r"(ctxt->mvbar));
	//asm volatile ("mcr p15, 0, %0, c12, c1, 0" : : "r"(ctxt->isr));
#endif

	/* CR13 */
	//asm volatile ("mcr p15, 0, %0, c13, c0, 0" : : "r"(ctxt->fcse));
	asm volatile ("mcr p15, 0, %0, c13, c0, 1" : : "r"(ctxt->cid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 2" : : "r"(ctxt->urwtpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 3" : : "r"(ctxt->urotpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 4" : : "r"(ctxt->potpid));
	
	/* CR15 */
#ifdef CORTEX_A9
	asm volatile ("mcr p15, 5, %0, c15, c7, 2" : : "r"(ctxt->mtlbar));
#endif

	asm volatile ("dsb");
	asm volatile ("isb");
}

/*disable_cache_invalidate: disable invalidate cache when cpu reset.
*/
void disable_cache_invalidate(void)
{
	#define CPU_CONFIG_REG (0XF1C20D3C)
	__u32 val = *(volatile __u32 *)(CPU_CONFIG_REG);
	val &=  (~0x3);
	val |= 0x03;	//disable invalidate
	*(volatile __u32 *)(CPU_CONFIG_REG) = val; 
	
	return;
}

/*set_copro_default: set co-processor to default state.
*note: the api will effect visible addr space, be careful to call it.
*/
void set_copro_default(void)
{
	struct saved_context *ctxt = &default_copro_value;
	/* CR0 */
	asm volatile ("mcr p15, 2, %0, c0, c0, 0" : : "r"(ctxt->cssr));
	/* CR1 */
#ifdef CORTEX_A8
	//busy_waiting();
	asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(ctxt->cr)); //will effect visible addr space
	asm volatile ("mcr p15, 0, %0, c1, c0, 1" : : "r"(ctxt->acr)); //?
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
	asm volatile ("mcr p15, 0, %0, c1, c1, 0" : : "r"(ctxt->sccfgr)); //?
	asm volatile ("mcr p15, 0, %0, c1, c1, 1" : : "r"(ctxt->scdbgenblr)); //?
	asm volatile ("mcr p15, 0, %0, c1, c1, 2" : : "r"(ctxt->nonscacctrlr)); //?
#elif defined(CORTEX_A7)
	asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(ctxt->cr)); //will effect visible addr space
	asm volatile ("mcr p15, 0, %0, c1, c0, 1" : : "r"(ctxt->acr)); //?
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(ctxt->cacr));
	asm volatile ("mcr p15, 0, %0, c1, c1, 0" : : "r"(ctxt->sccfgr)); //?
	asm volatile ("mcr p15, 0, %0, c1, c1, 1" : : "r"(ctxt->scdbgenblr)); //?
	asm volatile ("mcr p15, 0, %0, c1, c1, 2" : : "r"(ctxt->nonscacctrlr)); //?
#endif	
	
	/* CR2 */
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(ctxt->ttb_0r));
	//flush_tlb_all();
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
	asm volatile ("mcr p15, 0, %0, c9, c14, 0" : : "r"(ctxt->uer));
	asm volatile ("mcr p15, 0, %0, c9, c14, 1" : : "r"(ctxt->iesr));
	asm volatile ("mcr p15, 0, %0, c9, c14, 2" : : "r"(ctxt->iecr));
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 1, %0, c9, c0, 0" : : "r"(ctxt->l2clr));
	asm volatile ("mcr p15, 1, %0, c9, c0, 2" : : "r"(ctxt->l2cauxctrlr)); //?
#elif defined(CORTEX_A7)
#endif

	/* CR10 */
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 0, %0, c10, c0, 0" : : "r"(ctxt->d_tlblr));
	asm volatile ("mcr p15, 0, %0, c10, c0, 1" : : "r"(ctxt->i_tlblr));
#elif defined(CORTEX_A7)
#endif
	asm volatile ("mcr p15, 0, %0, c10, c2, 0" : : "r"(ctxt->prrr));
	asm volatile ("mcr p15, 0, %0, c10, c2, 1" : : "r"(ctxt->nrrr));
		
	/* CR11 */
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 0, %0, c11, c1, 0" : : "r"(ctxt->pleuar));
	asm volatile ("mcr p15, 0, %0, c11, c2, 0" : : "r"(ctxt->plecnr));
	asm volatile ("mcr p15, 0, %0, c11, c4, 0" : : "r"(ctxt->plecr));
	asm volatile ("mcr p15, 0, %0, c11, c5, 0" : : "r"(ctxt->pleisar));
	asm volatile ("mcr p15, 0, %0, c11, c7, 0" : : "r"(ctxt->pleiear));
	asm volatile ("mcr p15, 0, %0, c11, c15, 0" : : "r"(ctxt->plecidr));
#elif defined(CORTEX_A7)
#endif

	/* CR12 */
#ifdef CORTEX_A8
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->snsvbar));
	asm volatile ("mcr p15, 0, %0, c12, c0, 1" : : "r"(ctxt->monvecbar)); //?
#elif defined(CORTEX_A7)
	asm volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r"(ctxt->vbar));
	asm volatile ("mcr p15, 0, %0, c12, c0, 1" : : "r"(ctxt->mvbar));
	//asm volatile ("mcr p15, 0, %0, c12, c1, 0" : : "r"(ctxt->isr));
#endif

	/* CR13 */
	//asm volatile ("mcr p15, 0, %0, c13, c0, 0" : : "r"(ctxt->fcse));
	asm volatile ("mcr p15, 0, %0, c13, c0, 1" : : "r"(ctxt->cid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 2" : : "r"(ctxt->urwtpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 3" : : "r"(ctxt->urotpid));
	asm volatile ("mcr p15, 0, %0, c13, c0, 4" : : "r"(ctxt->potpid));
	asm volatile ("dsb");
	asm volatile ("isb");
	return;
}

/*save_processor_state: save current co-proccessor state.*/
void save_processor_state(void)
{
	__save_processor_state(&saved_context);
}

/*restore_processor_state: restore */
void restore_processor_state(void)
{
	__restore_processor_state(&saved_context);
}

void restore_processor_ttbr0(void)
{
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(saved_context.ttb_0r));
	return;
}

void set_ttbr0(void)
{
	__u32 ttb = 0;
	//read ttbr1
	asm volatile ("mrc p15, 0, %0, c2, c0, 1"  : "=r"(ttb));
	//write ttbr0
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(ttb));
	asm volatile ("isb");
	return;
}

