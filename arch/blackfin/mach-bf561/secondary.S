/*
 * File:         arch/blackfin/mach-bf561/secondary.S
 * Based on:     arch/blackfin/mach-bf561/head.S
 * Author:       Philippe Gerum <rpm@xenomai.org>
 *
 *               Copyright 2007 Analog Devices Inc.
 *
 * Description:  BF561 coreB bootstrap file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/blackfin.h>
#include <asm/asm-offsets.h>

__INIT

/* Lay the initial stack into the L1 scratch area of Core B */
#define INITIAL_STACK	(COREB_L1_SCRATCH_START + L1_SCRATCH_LENGTH - 12)

ENTRY(_coreb_trampoline_start)
	/* Set the SYSCFG register */
	R0 = 0x36;
	SYSCFG = R0; /*Enable Cycle Counter and Nesting Of Interrupts(3rd Bit)*/
	R0 = 0;

	/*Clear Out All the data and pointer  Registers*/
	R1 = R0;
	R2 = R0;
	R3 = R0;
	R4 = R0;
	R5 = R0;
	R6 = R0;
	R7 = R0;

	P0 = R0;
	P1 = R0;
	P2 = R0;
	P3 = R0;
	P4 = R0;
	P5 = R0;

	LC0 = r0;
	LC1 = r0;
	L0 = r0;
	L1 = r0;
	L2 = r0;
	L3 = r0;

	/* Clear Out All the DAG Registers*/
	B0 = r0;
	B1 = r0;
	B2 = r0;
	B3 = r0;

	I0 = r0;
	I1 = r0;
	I2 = r0;
	I3 = r0;

	M0 = r0;
	M1 = r0;
	M2 = r0;
	M3 = r0;

	/* Turn off the icache */
	p0.l = LO(IMEM_CONTROL);
	p0.h = HI(IMEM_CONTROL);
	R1 = [p0];
	R0 = ~ENICPLB;
	R0 = R0 & R1;

	/* Anomaly 05000125 */
#ifdef ANOMALY_05000125
	CLI R2;
	SSYNC;
#endif
	[p0] = R0;
	SSYNC;
#ifdef ANOMALY_05000125
	STI R2;
#endif

	/* Turn off the dcache */
	p0.l = LO(DMEM_CONTROL);
	p0.h = HI(DMEM_CONTROL);
	R1 = [p0];
	R0 = ~ENDCPLB;
	R0 = R0 & R1;

	/* Anomaly 05000125 */
#ifdef ANOMALY_05000125
	CLI R2;
	SSYNC;
#endif
	[p0] = R0;
	SSYNC;
#ifdef ANOMALY_05000125
	STI R2;
#endif

	/* in case of double faults, save a few things */
	p0.l = _init_retx_coreb;
	p0.h = _init_retx_coreb;
	R0 = RETX;
	[P0] = R0;

#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/* Only save these if we are storing them,
	 * This happens here, since L1 gets clobbered
	 * below
	 */
	GET_PDA(p0, r0);
	r7 = [p0 + PDA_RETX];
	p1.l = _init_saved_retx_coreb;
	p1.h = _init_saved_retx_coreb;
	[p1] = r7;

	r7 = [p0 + PDA_DCPLB];
	p1.l = _init_saved_dcplb_fault_addr_coreb;
	p1.h = _init_saved_dcplb_fault_addr_coreb;
	[p1] = r7;

	r7 = [p0 + PDA_ICPLB];
	p1.l = _init_saved_icplb_fault_addr_coreb;
	p1.h = _init_saved_icplb_fault_addr_coreb;
	[p1] = r7;

	r7 = [p0 + PDA_SEQSTAT];
	p1.l = _init_saved_seqstat_coreb;
	p1.h = _init_saved_seqstat_coreb;
	[p1] = r7;
#endif

	/* Initialize stack pointer */
	sp.l = lo(INITIAL_STACK);
	sp.h = hi(INITIAL_STACK);
	fp = sp;
	usp = sp;

	/* This section keeps the processor in supervisor mode
	 * during core B startup.  Branches to the idle task.
	 */

	/* EVT15 = _real_start */

	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _coreb_start;
	p1.h = _coreb_start;
	[p0] = p1;
	csync;

	p0.l = lo(IMASK);
	p0.h = hi(IMASK);
	p1.l = IMASK_IVG15;
	p1.h = 0x0;
	[p0] = p1;
	csync;

	raise 15;
	p0.l = .LWAIT_HERE;
	p0.h = .LWAIT_HERE;
	reti = p0;
#if defined(ANOMALY_05000281)
	nop; nop; nop;
#endif
	rti;

.LWAIT_HERE:
	jump .LWAIT_HERE;
ENDPROC(_coreb_trampoline_start)
ENTRY(_coreb_trampoline_end)

ENTRY(_coreb_start)
	[--sp] = reti;

	p0.l = lo(WDOGB_CTL);
	p0.h = hi(WDOGB_CTL);
	r0 = 0xAD6(z);
	w[p0] = r0;	/* Clear the watchdog. */
	ssync;

	/*
	 * switch to IDLE stack.
	 */
	p0.l = _secondary_stack;
	p0.h = _secondary_stack;
	sp = [p0];
	usp = sp;
	fp = sp;
	sp += -12;
	call _init_pda
	sp += 12;
	call _secondary_start_kernel;
.L_exit:
	jump.s	.L_exit;
ENDPROC(_coreb_start)

__FINIT
