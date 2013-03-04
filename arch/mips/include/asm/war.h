/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle
 * Copyright (C) 2007  Maciej W. Rozycki
 */
#ifndef _ASM_WAR_H
#define _ASM_WAR_H

#include <war.h>

/*
 * Work around certain R4000 CPU errata (as implemented by GCC):
 *
 * - A double-word or a variable shift may give an incorrect result
 *   if executed immediately after starting an integer division:
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #28
 *   "MIPS R4000MC Errata, Processor Revision 2.2 and 3.0", erratum
 *   #19
 *
 * - A double-word or a variable shift may give an incorrect result
 *   if executed while an integer multiplication is in progress:
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   errata #16 & #28
 *
 * - An integer division may give an incorrect result if started in
 *   a delay slot of a taken branch or a jump:
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #52
 */
#ifdef CONFIG_CPU_R4000_WORKAROUNDS
#define R4000_WAR 1
#else
#define R4000_WAR 0
#endif

/*
 * Work around certain R4400 CPU errata (as implemented by GCC):
 *
 * - A double-word or a variable shift may give an incorrect result
 *   if executed immediately after starting an integer division:
 *   "MIPS R4400MC Errata, Processor Revision 1.0", erratum #10
 *   "MIPS R4400MC Errata, Processor Revision 2.0 & 3.0", erratum #4
 */
#ifdef CONFIG_CPU_R4400_WORKAROUNDS
#define R4400_WAR 1
#else
#define R4400_WAR 0
#endif

/*
 * Work around the "daddi" and "daddiu" CPU errata:
 *
 * - The `daddi' instruction fails to trap on overflow.
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #23
 *
 * - The `daddiu' instruction can produce an incorrect result.
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #41
 *   "MIPS R4000MC Errata, Processor Revision 2.2 and 3.0", erratum
 *   #15
 *   "MIPS R4400PC/SC Errata, Processor Revision 1.0", erratum #7
 *   "MIPS R4400MC Errata, Processor Revision 1.0", erratum #5
 */
#ifdef CONFIG_CPU_DADDI_WORKAROUNDS
#define DADDI_WAR 1
#else
#define DADDI_WAR 0
#endif

/*
 * Another R4600 erratum.  Due to the lack of errata information the exact
 * technical details aren't known.  I've experimentally found that disabling
 * interrupts during indexed I-cache flushes seems to be sufficient to deal
 * with the issue.
 */
#ifndef R4600_V1_INDEX_ICACHEOP_WAR
#error Check setting of R4600_V1_INDEX_ICACHEOP_WAR for your platform
#endif

/*
 * Pleasures of the R4600 V1.x.	 Cite from the IDT R4600 V1.7 errata:
 *
 *  18. The CACHE instructions Hit_Writeback_Invalidate_D, Hit_Writeback_D,
 *	Hit_Invalidate_D and Create_Dirty_Excl_D should only be
 *	executed if there is no other dcache activity. If the dcache is
 *	accessed for another instruction immeidately preceding when these
 *	cache instructions are executing, it is possible that the dcache
 *	tag match outputs used by these cache instructions will be
 *	incorrect. These cache instructions should be preceded by at least
 *	four instructions that are not any kind of load or store
 *	instruction.
 *
 *	This is not allowed:	lw
 *				nop
 *				nop
 *				nop
 *				cache	    Hit_Writeback_Invalidate_D
 *
 *	This is allowed:	lw
 *				nop
 *				nop
 *				nop
 *				nop
 *				cache	    Hit_Writeback_Invalidate_D
 */
#ifndef R4600_V1_HIT_CACHEOP_WAR
#error Check setting of R4600_V1_HIT_CACHEOP_WAR for your platform
#endif


/*
 * Writeback and invalidate the primary cache dcache before DMA.
 *
 * R4600 v2.0 bug: "The CACHE instructions Hit_Writeback_Inv_D,
 * Hit_Writeback_D, Hit_Invalidate_D and Create_Dirty_Exclusive_D will only
 * operate correctly if the internal data cache refill buffer is empty.	 These
 * CACHE instructions should be separated from any potential data cache miss
 * by a load instruction to an uncached address to empty the response buffer."
 * (Revision 2.0 device errata from IDT available on http://www.idt.com/
 * in .pdf format.)
 */
#ifndef R4600_V2_HIT_CACHEOP_WAR
#error Check setting of R4600_V2_HIT_CACHEOP_WAR for your platform
#endif

/*
 * When an interrupt happens on a CP0 register read instruction, CPU may
 * lock up or read corrupted values of CP0 registers after it enters
 * the exception handler.
 *
 * This workaround makes sure that we read a "safe" CP0 register as the
 * first thing in the exception handler, which breaks one of the
 * pre-conditions for this problem.
 */
#ifndef R5432_CP0_INTERRUPT_WAR
#error Check setting of R5432_CP0_INTERRUPT_WAR for your platform
#endif

/*
 * Workaround for the Sibyte M3 errata the text of which can be found at
 *
 *   http://sibyte.broadcom.com/hw/bcm1250/docs/pass2errata.txt
 *
 * This will enable the use of a special TLB refill handler which does a
 * consistency check on the information in c0_badvaddr and c0_entryhi and
 * will just return and take the exception again if the information was
 * found to be inconsistent.
 */
#ifndef BCM1250_M3_WAR
#error Check setting of BCM1250_M3_WAR for your platform
#endif

/*
 * This is a DUART workaround related to glitches around register accesses
 */
#ifndef SIBYTE_1956_WAR
#error Check setting of SIBYTE_1956_WAR for your platform
#endif

/*
 * Fill buffers not flushed on CACHE instructions
 *
 * Hit_Invalidate_I cacheops invalidate an icache line but the refill
 * for that line can get stale data from the fill buffer instead of
 * accessing memory if the previous icache miss was also to that line.
 *
 * Workaround: generate an icache refill from a different line
 *
 * Affects:
 *  MIPS 4K		RTL revision <3.0, PRID revision <4
 */
#ifndef MIPS4K_ICACHE_REFILL_WAR
#error Check setting of MIPS4K_ICACHE_REFILL_WAR for your platform
#endif

/*
 * Missing implicit forced flush of evictions caused by CACHE
 * instruction
 *
 * Evictions caused by a CACHE instructions are not forced on to the
 * bus. The BIU gives higher priority to fetches than to the data from
 * the eviction buffer and no collision detection is performed between
 * fetches and pending data from the eviction buffer.
 *
 * Workaround: Execute a SYNC instruction after the cache instruction
 *
 * Affects:
 *   MIPS 5Kc,5Kf	RTL revision <2.3, PRID revision <8
 *   MIPS 20Kc		RTL revision <4.0, PRID revision <?
 */
#ifndef MIPS_CACHE_SYNC_WAR
#error Check setting of MIPS_CACHE_SYNC_WAR for your platform
#endif

/*
 * From TX49/H2 manual: "If the instruction (i.e. CACHE) is issued for
 * the line which this instruction itself exists, the following
 * operation is not guaranteed."
 *
 * Workaround: do two phase flushing for Index_Invalidate_I
 */
#ifndef TX49XX_ICACHE_INDEX_INV_WAR
#error Check setting of TX49XX_ICACHE_INDEX_INV_WAR for your platform
#endif

/*
 * The RM7000 processors and the E9000 cores have a bug (though PMC-Sierra
 * opposes it being called that) where invalid instructions in the same
 * I-cache line worth of instructions being fetched may case spurious
 * exceptions.
 */
#ifndef ICACHE_REFILLS_WORKAROUND_WAR
#error Check setting of ICACHE_REFILLS_WORKAROUND_WAR for your platform
#endif

/*
 * On the R10000 up to version 2.6 (not sure about 2.7) there is a bug that
 * may cause ll / sc and lld / scd sequences to execute non-atomically.
 */
#ifndef R10000_LLSC_WAR
#error Check setting of R10000_LLSC_WAR for your platform
#endif

/*
 * 34K core erratum: "Problems Executing the TLBR Instruction"
 */
#ifndef MIPS34K_MISSED_ITLB_WAR
#error Check setting of MIPS34K_MISSED_ITLB_WAR for your platform
#endif

#endif /* _ASM_WAR_H */
