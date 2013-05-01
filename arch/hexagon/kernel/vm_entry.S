/*
 * Event entry/exit for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <asm/asm-offsets.h>  /*  assembly-safer versions of C defines */
#include <asm/mem-layout.h>   /*  sigh, except for page_offset  */
#include <asm/hexagon_vm.h>
#include <asm/thread_info.h>

/*
 * Entry into guest-mode Linux under Hexagon Virtual Machine.
 * Stack pointer points to event record - build pt_regs on top of it,
 * set up a plausible C stack frame, and dispatch to the C handler.
 * On return, do vmrte virtual instruction with SP where we started.
 *
 * VM Spec 0.5 uses a trap to fetch HVM record now.
 */

/*
 * Save full register state, while setting up thread_info struct
 * pointer derived from kernel stack pointer in THREADINFO_REG
 * register, putting prior thread_info.regs pointer in a callee-save
 * register (R24, which had better not ever be assigned to THREADINFO_REG),
 * and updating thread_info.regs to point to current stack frame,
 * so as to support nested events in kernel mode.
 *
 * As this is common code, we set the pt_regs system call number
 * to -1 for all events.  It will be replaced with the system call
 * number in the case where we decode a system call (trap0(#1)).
 */

#if CONFIG_HEXAGON_ARCH_VERSION < 4
#define save_pt_regs()\
 memd(R0 + #_PT_R3130) = R31:30; \
 { memw(R0 + #_PT_R2928) = R28; \
   R31 = memw(R0 + #_PT_ER_VMPSP); }\
 { memw(R0 + #(_PT_R2928 + 4)) = R31; \
   R31 = ugp; } \
 { memd(R0 + #_PT_R2726) = R27:26; \
   R30 = gp ; } \
 memd(R0 + #_PT_R2524) = R25:24; \
 memd(R0 + #_PT_R2322) = R23:22; \
 memd(R0 + #_PT_R2120) = R21:20; \
 memd(R0 + #_PT_R1918) = R19:18; \
 memd(R0 + #_PT_R1716) = R17:16; \
 memd(R0 + #_PT_R1514) = R15:14; \
 memd(R0 + #_PT_R1312) = R13:12; \
 { memd(R0 + #_PT_R1110) = R11:10; \
   R15 = lc0; } \
 { memd(R0 + #_PT_R0908) = R9:8; \
   R14 = sa0; } \
 { memd(R0 + #_PT_R0706) = R7:6; \
   R13 = lc1; } \
 { memd(R0 + #_PT_R0504) = R5:4; \
   R12 = sa1; } \
 { memd(R0 + #_PT_GPUGP) = R31:30; \
   R11 = m1; \
   R2.H = #HI(_THREAD_SIZE); } \
 { memd(R0 + #_PT_LC0SA0) = R15:14; \
   R10 = m0; \
   R2.L = #LO(_THREAD_SIZE); } \
 { memd(R0 + #_PT_LC1SA1) = R13:12; \
   R15 = p3:0; \
   R2 = neg(R2); } \
 { memd(R0 + #_PT_M1M0) = R11:10; \
   R14  = usr; \
   R2 = and(R0,R2); } \
 { memd(R0 + #_PT_PREDSUSR) =  R15:14; \
   THREADINFO_REG = R2; } \
 { r24 = memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS); \
   memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS) = R0; \
   R2 = #-1; } \
 { memw(R0 + #_PT_SYSCALL_NR) = R2; \
   R30 = #0; }
#else
/* V4+ */
/* the # ## # syntax inserts a literal ## */
#define save_pt_regs()\
	{ memd(R0 + #_PT_R3130) = R31:30; \
		R30 = memw(R0 + #_PT_ER_VMPSP); }\
	{ memw(R0 + #_PT_R2928) = R28; \
		memw(R0 + #(_PT_R2928 + 4)) = R30; }\
	{ R31:30 = C11:10; \
		memd(R0 + #_PT_R2726) = R27:26; \
		memd(R0 + #_PT_R2524) = R25:24; }\
	{ memd(R0 + #_PT_R2322) = R23:22; \
		memd(R0 + #_PT_R2120) = R21:20; }\
	{ memd(R0 + #_PT_R1918) = R19:18; \
		memd(R0 + #_PT_R1716) = R17:16; }\
	{ memd(R0 + #_PT_R1514) = R15:14; \
		memd(R0 + #_PT_R1312) = R13:12; \
		R17:16 = C13:12; }\
	{ memd(R0 + #_PT_R1110) = R11:10; \
		memd(R0 + #_PT_R0908) = R9:8; \
	  R15:14 = C1:0; } \
	{ memd(R0 + #_PT_R0706) = R7:6; \
		memd(R0 + #_PT_R0504) = R5:4; \
    R13:12 = C3:2; } \
	{ memd(R0 + #_PT_GPUGP) = R31:30; \
		memd(R0 + #_PT_LC0SA0) = R15:14; \
	  R11:10 = C7:6; }\
	{	THREADINFO_REG = and(R0, # ## #-_THREAD_SIZE); \
		memd(R0 + #_PT_LC1SA1) = R13:12; \
	  R15 = p3:0; }\
	{ memd(R0 + #_PT_M1M0) = R11:10; \
		memw(R0 + #_PT_PREDSUSR + 4) =  R15; }\
	{ r24 = memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS); \
	  memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS) = R0; \
	  R2 = #-1; } \
	{ memw(R0 + #_PT_SYSCALL_NR) = R2; \
		memd(R0 + #_PT_CS1CS0) = R17:16; \
	  R30 = #0; }
#endif

/*
 * Restore registers and thread_info.regs state. THREADINFO_REG
 * is assumed to still be sane, and R24 to have been correctly
 * preserved. Don't restore R29 (SP) until later.
 */

#if CONFIG_HEXAGON_ARCH_VERSION < 4
#define restore_pt_regs() \
	{ memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS) = R24; \
	  R15:14 = memd(R0 + #_PT_PREDSUSR); } \
	{ R11:10 = memd(R0 + #_PT_M1M0); \
	  p3:0 = R15; } \
	{ R13:12 = memd(R0 + #_PT_LC1SA1); \
	  usr = R14; } \
	{ R15:14 = memd(R0 + #_PT_LC0SA0); \
	  m1 = R11; } \
	{ R3:2 = memd(R0 + #_PT_R0302); \
	  m0 = R10; } \
	{ R5:4 = memd(R0 + #_PT_R0504); \
	  lc1 = R13; } \
	{ R7:6 = memd(R0 + #_PT_R0706); \
	  sa1 = R12; } \
	{ R9:8 = memd(R0 + #_PT_R0908); \
	  lc0 = R15; } \
	{ R11:10 = memd(R0 + #_PT_R1110); \
	  sa0 = R14; } \
	{ R13:12 = memd(R0 + #_PT_R1312); \
	  R15:14 = memd(R0 + #_PT_R1514); } \
	{ R17:16 = memd(R0 + #_PT_R1716); \
	  R19:18 = memd(R0 + #_PT_R1918); } \
	{ R21:20 = memd(R0 + #_PT_R2120); \
	  R23:22 = memd(R0 + #_PT_R2322); } \
	{ R25:24 = memd(R0 + #_PT_R2524); \
	  R27:26 = memd(R0 + #_PT_R2726); } \
	R31:30 = memd(R0 + #_PT_GPUGP); \
	{ R28 = memw(R0 + #_PT_R2928); \
	  ugp = R31; } \
	{ R31:30 = memd(R0 + #_PT_R3130); \
	  gp = R30; }
#else
/* V4+ */
#define restore_pt_regs() \
	{ memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS) = R24; \
	  R15:14 = memd(R0 + #_PT_PREDSUSR); } \
	{ R11:10 = memd(R0 + #_PT_M1M0); \
		R13:12 = memd(R0 + #_PT_LC1SA1); \
		p3:0 = R15; } \
	{ R15:14 = memd(R0 + #_PT_LC0SA0); \
		R3:2 = memd(R0 + #_PT_R0302); \
		usr = R14; } \
	{ R5:4 = memd(R0 + #_PT_R0504); \
		R7:6 = memd(R0 + #_PT_R0706); \
		C7:6 = R11:10; }\
	{ R9:8 = memd(R0 + #_PT_R0908); \
		R11:10 = memd(R0 + #_PT_R1110); \
    C3:2 = R13:12; }\
	{ R13:12 = memd(R0 + #_PT_R1312); \
	  R15:14 = memd(R0 + #_PT_R1514); \
		C1:0 = R15:14; }\
	{ R17:16 = memd(R0 + #_PT_R1716); \
	  R19:18 = memd(R0 + #_PT_R1918); } \
	{ R21:20 = memd(R0 + #_PT_R2120); \
	  R23:22 = memd(R0 + #_PT_R2322); } \
	{ R25:24 = memd(R0 + #_PT_R2524); \
	  R27:26 = memd(R0 + #_PT_R2726); } \
	R31:30 = memd(R0 + #_PT_CS1CS0); \
	{ C13:12 = R31:30; \
		R31:30 = memd(R0 + #_PT_GPUGP) ; \
		R28 = memw(R0 + #_PT_R2928); }\
	{ C11:10 = R31:30; \
		R31:30 = memd(R0 + #_PT_R3130); }
#endif

	/*
	 * Clears off enough space for the rest of pt_regs; evrec is a part
	 * of pt_regs in HVM mode.  Save R0/R1, set handler's address in R1.
	 * R0 is the address of pt_regs and is the parameter to save_pt_regs.
	 */

/*
 * Since the HVM isn't automagically pushing the EVREC onto the stack anymore,
 * we'll subract the entire size out and then fill it in ourselves.
 * Need to save off R0, R1, R2, R3 immediately.
 */

#if CONFIG_HEXAGON_ARCH_VERSION < 4
#define	vm_event_entry(CHandler) \
	{ \
		R29 = add(R29, #-(_PT_REGS_SIZE)); \
		memd(R29 + #(_PT_R0100 + -_PT_REGS_SIZE)) = R1:0; \
	} \
	{ \
		memd(R29 +#_PT_R0302) = R3:2; \
	} \
	trap1(#HVM_TRAP1_VMGETREGS); \
	{ \
		memd(R29 + #_PT_ER_VMEL) = R1:0; \
		R0 = R29; \
		R1.L = #LO(CHandler); \
	} \
	{ \
		memd(R29 + #_PT_ER_VMPSP) = R3:2; \
		R1.H = #HI(CHandler); \
		jump event_dispatch; \
	}
#else
/* V4+ */
/* turn on I$ prefetch early */
/* the # ## # syntax inserts a literal ## */
#define	vm_event_entry(CHandler) \
	{ \
		R29 = add(R29, #-(_PT_REGS_SIZE)); \
		memd(R29 + #(_PT_R0100 + -_PT_REGS_SIZE)) = R1:0; \
		memd(R29 + #(_PT_R0302 + -_PT_REGS_SIZE)) = R3:2; \
		R0 = usr; \
	} \
	{ \
		memw(R29 + #_PT_PREDSUSR) = R0; \
		R0 = setbit(R0, #16); \
	} \
	usr = R0; \
	R1:0 = G1:0; \
	{ \
		memd(R29 + #_PT_ER_VMEL) = R1:0; \
		R1 = # ## #(CHandler); \
		R3:2 = G3:2; \
	} \
	{ \
		R0 = R29; \
		memd(R29 + #_PT_ER_VMPSP) = R3:2; \
		jump event_dispatch; \
	}
#endif

.text
	/*
	 * Do bulk save/restore in one place.
	 * Adds a jump to dispatch latency, but
	 * saves hundreds of bytes.
	 */

event_dispatch:
	save_pt_regs()
	callr	r1

	/*
	 * Coming back from the C-world, our thread info pointer
	 * should be in the designated register (usually R19)
	 *
	 * If we were in kernel mode, we don't need to check scheduler
	 * or signals if CONFIG_PREEMPT is not set.  If set, then it has
	 * to jump to a need_resched kind of block.
	 * BTW, CONFIG_PREEMPT is not supported yet.
	 */

#ifdef CONFIG_PREEMPT
	R0 = #VM_INT_DISABLE
	trap1(#HVM_TRAP1_VMSETIE)
#endif

	/*  "Nested control path" -- if the previous mode was kernel  */
	{
		R0 = memw(R29 + #_PT_ER_VMEST);
		R16.L = #LO(do_work_pending);
	}
	{
		P0 = tstbit(R0, #HVM_VMEST_UM_SFT);
		if (!P0.new) jump:nt restore_all;
		R16.H = #HI(do_work_pending);
		R0 = #VM_INT_DISABLE;
	}

	/*
	 * Check also the return from fork/system call, normally coming back from
	 * user mode
	 *
	 * R16 needs to have do_work_pending, and R0 should have VM_INT_DISABLE
	 */

check_work_pending:
	/*  Disable interrupts while checking TIF  */
	trap1(#HVM_TRAP1_VMSETIE)
	{
		R0 = R29;  /*  regs should still be at top of stack  */
		R1 = memw(THREADINFO_REG + #_THREAD_INFO_FLAGS);
		callr R16;
	}

	{
		P0 = cmp.eq(R0, #0); if (!P0.new) jump:nt check_work_pending;
		R0 = #VM_INT_DISABLE;
	}

restore_all:
	/*
	 * Disable interrupts, if they weren't already, before reg restore.
	 * R0 gets preloaded with #VM_INT_DISABLE before we get here.
	 */
	trap1(#HVM_TRAP1_VMSETIE)

	/*  do the setregs here for VM 0.5  */
	/*  R29 here should already be pointing at pt_regs  */
	{
		R1:0 = memd(R29 + #_PT_ER_VMEL);
		R3:2 = memd(R29 + #_PT_ER_VMPSP);
	}
#if CONFIG_HEXAGON_ARCH_VERSION < 4
	trap1(#HVM_TRAP1_VMSETREGS);
#else
	G1:0 = R1:0;
	G3:2 = R3:2;
#endif

	R0 = R29
	restore_pt_regs()
	{
		R1:0 = memd(R29 + #_PT_R0100);
		R29 = add(R29, #_PT_REGS_SIZE);
	}
	trap1(#HVM_TRAP1_VMRTE)
	/* Notreached */


	.globl _K_enter_genex
_K_enter_genex:
	vm_event_entry(do_genex)

	.globl _K_enter_interrupt
_K_enter_interrupt:
	vm_event_entry(arch_do_IRQ)

	.globl _K_enter_trap0
_K_enter_trap0:
	vm_event_entry(do_trap0)

	.globl _K_enter_machcheck
_K_enter_machcheck:
	vm_event_entry(do_machcheck)

	.globl _K_enter_debug
_K_enter_debug:
	vm_event_entry(do_debug_exception)

	.globl ret_from_fork
ret_from_fork:
	{
		call schedule_tail
		R16.H = #HI(do_work_pending);
	}
	{
		P0 = cmp.eq(R24, #0);
		R16.L = #LO(do_work_pending);
		R0 = #VM_INT_DISABLE;
	}
	if P0 jump check_work_pending
	{
		R0 = R25;
		callr R24
	}
	{
		jump check_work_pending
		R0 = #VM_INT_DISABLE;
	}
