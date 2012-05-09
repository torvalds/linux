/*
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>
#include <mach/irq.h>
#include <asm/dpmc.h>

.section .l1.text
ENTRY(_sleep_mode)
	[--SP] = (R7:4, P5:3);
	[--SP] = RETS;

	call _set_sic_iwr;

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R1 = W[P0](z);
	BITSET (R1, 3);
	W[P0] = R1.L;

	CLI R2;
	SSYNC;
	IDLE;
	STI R2;

	call _test_pll_locked;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R7 = w[p0](z);
	BITCLR (R7, 3);
	BITCLR (R7, 5);
	w[p0] = R7.L;
	IDLE;
	call _test_pll_locked;

	RETS = [SP++];
	(R7:4, P5:3) = [SP++];
	RTS;
ENDPROC(_sleep_mode)

/*
 * This func never returns as it puts the part into hibernate, and
 * is only called from do_hibernate, so we don't bother saving or
 * restoring any of the normal C runtime state.  When we wake up,
 * the entry point will be in do_hibernate and not here.
 *
 * We accept just one argument -- the value to write to VR_CTL.
 */

ENTRY(_hibernate_mode)
	/* Save/setup the regs we need early for minor pipeline optimization */
	R4 = R0;

	P3.H = hi(VR_CTL);
	P3.L = lo(VR_CTL);
	/* Disable all wakeup sources */
	R0 = IWR_DISABLE_ALL;
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;
	call _set_sic_iwr;
	call _set_dram_srfs;
	SSYNC;

	/* Finally, we climb into our cave to hibernate */
	W[P3] = R4.L;
	CLI R2;
	IDLE;
.Lforever:
	jump .Lforever;
ENDPROC(_hibernate_mode)

ENTRY(_sleep_deeper)
	[--SP] = (R7:4, P5:3);
	[--SP] = RETS;

	CLI R4;

	P3 = R0;
	P4 = R1;
	P5 = R2;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;
	call _set_dram_srfs;	/* Set SDRAM Self Refresh */

	P0.H = hi(PLL_DIV);
	P0.L = lo(PLL_DIV);
	R6 = W[P0](z);
	R0.L = 0xF;
	W[P0] = R0.l;		/* Set Max VCO to SCLK divider */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R5 = W[P0](z);
	R0.L = (CONFIG_MIN_VCO_HZ/CONFIG_CLKIN_HZ) << 9;
	W[P0] = R0.l;		/* Set Min CLKIN to VCO multiplier */

	SSYNC;
	IDLE;

	call _test_pll_locked;

	P0.H = hi(VR_CTL);
	P0.L = lo(VR_CTL);
	R7 = W[P0](z);
	R1 = 0x6;
	R1 <<= 16;
	R2 = 0x0404(Z);
	R1 = R1|R2;

	R2 = DEPOSIT(R7, R1);
	W[P0] = R2;		/* Set Min Core Voltage */

	SSYNC;
	IDLE;

	call _test_pll_locked;

	R0 = P3;
	R1 = P4;
	R3 = P5;
	call _set_sic_iwr;	/* Set Awake from IDLE */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R0 = W[P0](z);
	BITSET (R0, 3);
	W[P0] = R0.L;		/* Turn CCLK OFF */
	SSYNC;
	IDLE;

	call _test_pll_locked;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;	/* Set Awake from IDLE PLL */

	P0.H = hi(VR_CTL);
	P0.L = lo(VR_CTL);
	W[P0]= R7;

	SSYNC;
	IDLE;

	call _test_pll_locked;

	P0.H = hi(PLL_DIV);
	P0.L = lo(PLL_DIV);
	W[P0]= R6;		/* Restore CCLK and SCLK divider */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	w[p0] = R5;		/* Restore VCO multiplier */
	IDLE;
	call _test_pll_locked;

	call _unset_dram_srfs;	/* SDRAM Self Refresh Off */

	STI R4;

	RETS = [SP++];
	(R7:4, P5:3) = [SP++];
	RTS;
ENDPROC(_sleep_deeper)

ENTRY(_set_dram_srfs)
	/*  set the dram to self refresh mode */
	SSYNC;
#if defined(EBIU_RSTCTL)	/* DDR */
	P0.H = hi(EBIU_RSTCTL);
	P0.L = lo(EBIU_RSTCTL);
	R2 = [P0];
	BITSET(R2, 3); /* SRREQ enter self-refresh mode */
	[P0] = R2;
	SSYNC;
1:
	R2 = [P0];
	CC = BITTST(R2, 4);
	if !CC JUMP 1b;
#else 				/* SDRAM */
	P0.L = lo(EBIU_SDGCTL);
	P0.H = hi(EBIU_SDGCTL);
	P1.L = lo(EBIU_SDSTAT);
	P1.H = hi(EBIU_SDSTAT);

	R2 = [P0];
	BITSET(R2, 24); /* SRFS enter self-refresh mode */
	[P0] = R2;
	SSYNC;

1:
	R2 = w[P1];
	SSYNC;
	cc = BITTST(R2, 1); /* SDSRA poll self-refresh status */
	if !cc jump 1b;

	R2 = [P0];
	BITCLR(R2, 0); /* SCTLE disable CLKOUT */
	[P0] = R2;
#endif
	RTS;
ENDPROC(_set_dram_srfs)

ENTRY(_unset_dram_srfs)
	/*  set the dram out of self refresh mode */

#if defined(EBIU_RSTCTL)	/* DDR */
	P0.H = hi(EBIU_RSTCTL);
	P0.L = lo(EBIU_RSTCTL);
	R2 = [P0];
	BITCLR(R2, 3); /* clear SRREQ bit */
	[P0] = R2;
#elif defined(EBIU_SDGCTL)	/* SDRAM */
	/* release CLKOUT from self-refresh */
	P0.L = lo(EBIU_SDGCTL);
	P0.H = hi(EBIU_SDGCTL);

	R2 = [P0];
	BITSET(R2, 0); /* SCTLE enable CLKOUT */
	[P0] = R2
	SSYNC;

	/* release SDRAM from self-refresh */
	R2 = [P0];
	BITCLR(R2, 24); /* clear SRFS bit */
	[P0] = R2
#endif

	SSYNC;
	RTS;
ENDPROC(_unset_dram_srfs)

ENTRY(_set_sic_iwr)
#ifdef SIC_IWR0
	P0.H = hi(SYSMMR_BASE);
	P0.L = lo(SYSMMR_BASE);
	[P0 + (SIC_IWR0 - SYSMMR_BASE)] = R0;
	[P0 + (SIC_IWR1 - SYSMMR_BASE)] = R1;
# ifdef SIC_IWR2
	[P0 + (SIC_IWR2 - SYSMMR_BASE)] = R2;
# endif
#else
	P0.H = hi(SIC_IWR);
	P0.L = lo(SIC_IWR);
	[P0] = R0;
#endif

	SSYNC;
	RTS;
ENDPROC(_set_sic_iwr)

ENTRY(_test_pll_locked)
	P0.H = hi(PLL_STAT);
	P0.L = lo(PLL_STAT);
1:
	R0 = W[P0] (Z);
	CC = BITTST(R0,5);
	IF !CC JUMP 1b;
	RTS;
ENDPROC(_test_pll_locked)

.section .text
ENTRY(_do_hibernate)
	bfin_cpu_reg_save;
	bfin_sys_mmr_save;
	bfin_core_mmr_save;

	/* Setup args to hibernate mode early for pipeline optimization */
	R0 = M3;
	P1.H = _hibernate_mode;
	P1.L = _hibernate_mode;

	/* Save Magic, return address and Stack Pointer */
	P0 = 0;
	R1.H = 0xDEAD;	/* Hibernate Magic */
	R1.L = 0xBEEF;
	R2.H = .Lpm_resume_here;
	R2.L = .Lpm_resume_here;
	[P0++] = R1;	/* Store Hibernate Magic */
	[P0++] = R2;	/* Save Return Address */
	[P0++] = SP;	/* Save Stack Pointer */

	/* Must use an indirect call as we need to jump to L1 */
	call (P1); /* Goodbye */

.Lpm_resume_here:

	bfin_core_mmr_restore;
	bfin_sys_mmr_restore;
	bfin_cpu_reg_restore;

	[--sp] = RETI;	/* Clear Global Interrupt Disable */
	SP += 4;

	RTS;
ENDPROC(_do_hibernate)
