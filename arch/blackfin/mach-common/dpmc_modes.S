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
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	call _set_sic_iwr;

	R0 = 0xFFFF (Z);
	call _set_rtc_istat;

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
	( R7:0, P5:0 ) = [SP++];
	RTS;
ENDPROC(_sleep_mode)

ENTRY(_hibernate_mode)
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	R3 = R0;
	R0 = IWR_DISABLE_ALL;
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;
	call _set_sic_iwr;
	call _set_dram_srfs;
	SSYNC;

	R0 = 0xFFFF (Z);
	call _set_rtc_istat;

	P0.H = hi(VR_CTL);
	P0.L = lo(VR_CTL);

	W[P0] = R3.L;
	CLI R2;
	IDLE;
.Lforever:
	jump .Lforever;
ENDPROC(_hibernate_mode)

ENTRY(_sleep_deeper)
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	CLI R4;

	P3 = R0;
	P4 = R1;
	P5 = R2;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;
	call _set_dram_srfs;	/* Set SDRAM Self Refresh */

	/* Clear all the interrupts,bits sticky */
	R0 = 0xFFFF (Z);
	call _set_rtc_istat;
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
	( R7:0, P5:0 ) = [SP++];
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
	R2 = [P0];
	BITSET(R2, 24); /* SRFS enter self-refresh mode */
	[P0] = R2;
	SSYNC;

	P0.L = lo(EBIU_SDSTAT);
	P0.H = hi(EBIU_SDSTAT);
1:
	R2 = w[P0];
	SSYNC;
	cc = BITTST(R2, 1); /* SDSRA poll self-refresh status */
	if !cc jump 1b;

	P0.L = lo(EBIU_SDGCTL);
	P0.H = hi(EBIU_SDGCTL);
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

	P0.L = lo(EBIU_SDGCTL); /* release CLKOUT from self-refresh */
	P0.H = hi(EBIU_SDGCTL);
	R2 = [P0];
	BITSET(R2, 0); /* SCTLE enable CLKOUT */
	[P0] = R2
	SSYNC;

	P0.L = lo(EBIU_SDGCTL); /* release SDRAM from self-refresh */
	P0.H = hi(EBIU_SDGCTL);
	R2 = [P0];
	BITCLR(R2, 24); /* clear SRFS bit */
	[P0] = R2
#endif
	SSYNC;
	RTS;
ENDPROC(_unset_dram_srfs)

ENTRY(_set_sic_iwr)
#if defined(CONFIG_BF54x) || defined(CONFIG_BF52x) || defined(CONFIG_BF561) || \
	defined(CONFIG_BF538) || defined(CONFIG_BF539) || defined(CONFIG_BF51x)
	P0.H = hi(SIC_IWR0);
	P0.L = lo(SIC_IWR0);
	P1.H = hi(SIC_IWR1);
	P1.L = lo(SIC_IWR1);
	[P1] = R1;
#if defined(CONFIG_BF54x)
	P1.H = hi(SIC_IWR2);
	P1.L = lo(SIC_IWR2);
	[P1] = R2;
#endif
#else
	P0.H = hi(SIC_IWR);
	P0.L = lo(SIC_IWR);
#endif
	[P0] = R0;

	SSYNC;
	RTS;
ENDPROC(_set_sic_iwr)

ENTRY(_set_rtc_istat)
#ifndef CONFIG_BF561
	P0.H = hi(RTC_ISTAT);
	P0.L = lo(RTC_ISTAT);
	w[P0] = R0.L;
	SSYNC;
#elif (ANOMALY_05000371)
	nop;
	nop;
	nop;
	nop;
#endif
	RTS;
ENDPROC(_set_rtc_istat)

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
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;
	/* Save System MMRs */
	R2 = R0;
	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);

#ifdef SIC_IMASK0
	PM_SYS_PUSH(SIC_IMASK0)
#endif
#ifdef SIC_IMASK1
	PM_SYS_PUSH(SIC_IMASK1)
#endif
#ifdef SIC_IMASK2
	PM_SYS_PUSH(SIC_IMASK2)
#endif
#ifdef SIC_IMASK
	PM_SYS_PUSH(SIC_IMASK)
#endif
#ifdef SICA_IMASK0
	PM_SYS_PUSH(SICA_IMASK0)
#endif
#ifdef SICA_IMASK1
	PM_SYS_PUSH(SICA_IMASK1)
#endif
#ifdef SIC_IAR2
	PM_SYS_PUSH(SIC_IAR0)
	PM_SYS_PUSH(SIC_IAR1)
	PM_SYS_PUSH(SIC_IAR2)
#endif
#ifdef SIC_IAR3
	PM_SYS_PUSH(SIC_IAR3)
#endif
#ifdef SIC_IAR4
	PM_SYS_PUSH(SIC_IAR4)
	PM_SYS_PUSH(SIC_IAR5)
	PM_SYS_PUSH(SIC_IAR6)
#endif
#ifdef SIC_IAR7
	PM_SYS_PUSH(SIC_IAR7)
#endif
#ifdef SIC_IAR8
	PM_SYS_PUSH(SIC_IAR8)
	PM_SYS_PUSH(SIC_IAR9)
	PM_SYS_PUSH(SIC_IAR10)
	PM_SYS_PUSH(SIC_IAR11)
#endif

#ifdef SICA_IAR0
	PM_SYS_PUSH(SICA_IAR0)
	PM_SYS_PUSH(SICA_IAR1)
	PM_SYS_PUSH(SICA_IAR2)
	PM_SYS_PUSH(SICA_IAR3)
	PM_SYS_PUSH(SICA_IAR4)
	PM_SYS_PUSH(SICA_IAR5)
	PM_SYS_PUSH(SICA_IAR6)
	PM_SYS_PUSH(SICA_IAR7)
#endif

#ifdef SIC_IWR
	PM_SYS_PUSH(SIC_IWR)
#endif
#ifdef SIC_IWR0
	PM_SYS_PUSH(SIC_IWR0)
#endif
#ifdef SIC_IWR1
	PM_SYS_PUSH(SIC_IWR1)
#endif
#ifdef SIC_IWR2
	PM_SYS_PUSH(SIC_IWR2)
#endif
#ifdef SICA_IWR0
	PM_SYS_PUSH(SICA_IWR0)
#endif
#ifdef SICA_IWR1
	PM_SYS_PUSH(SICA_IWR1)
#endif

#ifdef PINT0_ASSIGN
	PM_SYS_PUSH(PINT0_ASSIGN)
	PM_SYS_PUSH(PINT1_ASSIGN)
	PM_SYS_PUSH(PINT2_ASSIGN)
	PM_SYS_PUSH(PINT3_ASSIGN)
#endif

	PM_SYS_PUSH(EBIU_AMBCTL0)
	PM_SYS_PUSH(EBIU_AMBCTL1)
	PM_SYS_PUSH16(EBIU_AMGCTL)

#ifdef EBIU_FCTL
	PM_SYS_PUSH(EBIU_MBSCTL)
	PM_SYS_PUSH(EBIU_MODE)
	PM_SYS_PUSH(EBIU_FCTL)
#endif

	PM_SYS_PUSH16(SYSCR)

	/* Save Core MMRs */
	P0.H = hi(SRAM_BASE_ADDRESS);
	P0.L = lo(SRAM_BASE_ADDRESS);

	PM_PUSH(DMEM_CONTROL)
	PM_PUSH(DCPLB_ADDR0)
	PM_PUSH(DCPLB_ADDR1)
	PM_PUSH(DCPLB_ADDR2)
	PM_PUSH(DCPLB_ADDR3)
	PM_PUSH(DCPLB_ADDR4)
	PM_PUSH(DCPLB_ADDR5)
	PM_PUSH(DCPLB_ADDR6)
	PM_PUSH(DCPLB_ADDR7)
	PM_PUSH(DCPLB_ADDR8)
	PM_PUSH(DCPLB_ADDR9)
	PM_PUSH(DCPLB_ADDR10)
	PM_PUSH(DCPLB_ADDR11)
	PM_PUSH(DCPLB_ADDR12)
	PM_PUSH(DCPLB_ADDR13)
	PM_PUSH(DCPLB_ADDR14)
	PM_PUSH(DCPLB_ADDR15)
	PM_PUSH(DCPLB_DATA0)
	PM_PUSH(DCPLB_DATA1)
	PM_PUSH(DCPLB_DATA2)
	PM_PUSH(DCPLB_DATA3)
	PM_PUSH(DCPLB_DATA4)
	PM_PUSH(DCPLB_DATA5)
	PM_PUSH(DCPLB_DATA6)
	PM_PUSH(DCPLB_DATA7)
	PM_PUSH(DCPLB_DATA8)
	PM_PUSH(DCPLB_DATA9)
	PM_PUSH(DCPLB_DATA10)
	PM_PUSH(DCPLB_DATA11)
	PM_PUSH(DCPLB_DATA12)
	PM_PUSH(DCPLB_DATA13)
	PM_PUSH(DCPLB_DATA14)
	PM_PUSH(DCPLB_DATA15)
	PM_PUSH(IMEM_CONTROL)
	PM_PUSH(ICPLB_ADDR0)
	PM_PUSH(ICPLB_ADDR1)
	PM_PUSH(ICPLB_ADDR2)
	PM_PUSH(ICPLB_ADDR3)
	PM_PUSH(ICPLB_ADDR4)
	PM_PUSH(ICPLB_ADDR5)
	PM_PUSH(ICPLB_ADDR6)
	PM_PUSH(ICPLB_ADDR7)
	PM_PUSH(ICPLB_ADDR8)
	PM_PUSH(ICPLB_ADDR9)
	PM_PUSH(ICPLB_ADDR10)
	PM_PUSH(ICPLB_ADDR11)
	PM_PUSH(ICPLB_ADDR12)
	PM_PUSH(ICPLB_ADDR13)
	PM_PUSH(ICPLB_ADDR14)
	PM_PUSH(ICPLB_ADDR15)
	PM_PUSH(ICPLB_DATA0)
	PM_PUSH(ICPLB_DATA1)
	PM_PUSH(ICPLB_DATA2)
	PM_PUSH(ICPLB_DATA3)
	PM_PUSH(ICPLB_DATA4)
	PM_PUSH(ICPLB_DATA5)
	PM_PUSH(ICPLB_DATA6)
	PM_PUSH(ICPLB_DATA7)
	PM_PUSH(ICPLB_DATA8)
	PM_PUSH(ICPLB_DATA9)
	PM_PUSH(ICPLB_DATA10)
	PM_PUSH(ICPLB_DATA11)
	PM_PUSH(ICPLB_DATA12)
	PM_PUSH(ICPLB_DATA13)
	PM_PUSH(ICPLB_DATA14)
	PM_PUSH(ICPLB_DATA15)
	PM_PUSH(EVT0)
	PM_PUSH(EVT1)
	PM_PUSH(EVT2)
	PM_PUSH(EVT3)
	PM_PUSH(EVT4)
	PM_PUSH(EVT5)
	PM_PUSH(EVT6)
	PM_PUSH(EVT7)
	PM_PUSH(EVT8)
	PM_PUSH(EVT9)
	PM_PUSH(EVT10)
	PM_PUSH(EVT11)
	PM_PUSH(EVT12)
	PM_PUSH(EVT13)
	PM_PUSH(EVT14)
	PM_PUSH(EVT15)
	PM_PUSH(IMASK)
	PM_PUSH(ILAT)
	PM_PUSH(IPRIO)
	PM_PUSH(TCNTL)
	PM_PUSH(TPERIOD)
	PM_PUSH(TSCALE)
	PM_PUSH(TCOUNT)
	PM_PUSH(TBUFCTL)

	/* Save Core Registers */
	[--sp] = SYSCFG;
	[--sp] = ( R7:0, P5:0 );
	[--sp] = fp;
	[--sp] = usp;

	[--sp] = i0;
	[--sp] = i1;
	[--sp] = i2;
	[--sp] = i3;

	[--sp] = m0;
	[--sp] = m1;
	[--sp] = m2;
	[--sp] = m3;

	[--sp] = l0;
	[--sp] = l1;
	[--sp] = l2;
	[--sp] = l3;

	[--sp] = b0;
	[--sp] = b1;
	[--sp] = b2;
	[--sp] = b3;
	[--sp] = a0.x;
	[--sp] = a0.w;
	[--sp] = a1.x;
	[--sp] = a1.w;

	[--sp] = LC0;
	[--sp] = LC1;
	[--sp] = LT0;
	[--sp] = LT1;
	[--sp] = LB0;
	[--sp] = LB1;

	[--sp] = ASTAT;
	[--sp] = CYCLES;
	[--sp] = CYCLES2;

	[--sp] = RETS;
	r0 = RETI;
	[--sp] = r0;
	[--sp] = RETX;
	[--sp] = RETN;
	[--sp] = RETE;
	[--sp] = SEQSTAT;

	/* Save Magic, return address and Stack Pointer */
	P0.H = 0;
	P0.L = 0;
	R0.H = 0xDEAD;	/* Hibernate Magic */
	R0.L = 0xBEEF;
	[P0++] = R0;	/* Store Hibernate Magic */
	R0.H = .Lpm_resume_here;
	R0.L = .Lpm_resume_here;
	[P0++] = R0;	/* Save Return Address */
	[P0++] = SP;	/* Save Stack Pointer */
	P0.H = _hibernate_mode;
	P0.L = _hibernate_mode;
	R0 = R2;
	call (P0); /* Goodbye */

.Lpm_resume_here:

	/* Restore Core Registers */
	SEQSTAT = [sp++];
	RETE = [sp++];
	RETN = [sp++];
	RETX = [sp++];
	r0 = [sp++];
	RETI = r0;
	RETS = [sp++];

	CYCLES2 = [sp++];
	CYCLES = [sp++];
	ASTAT = [sp++];

	LB1 = [sp++];
	LB0 = [sp++];
	LT1 = [sp++];
	LT0 = [sp++];
	LC1 = [sp++];
	LC0 = [sp++];

	a1.w = [sp++];
	a1.x = [sp++];
	a0.w = [sp++];
	a0.x = [sp++];
	b3 = [sp++];
	b2 = [sp++];
	b1 = [sp++];
	b0 = [sp++];

	l3 = [sp++];
	l2 = [sp++];
	l1 = [sp++];
	l0 = [sp++];

	m3 = [sp++];
	m2 = [sp++];
	m1 = [sp++];
	m0 = [sp++];

	i3 = [sp++];
	i2 = [sp++];
	i1 = [sp++];
	i0 = [sp++];

	usp = [sp++];
	fp = [sp++];

	( R7 : 0, P5 : 0) = [ SP ++ ];
	SYSCFG = [sp++];

	/* Restore Core MMRs */

	PM_POP(TBUFCTL)
	PM_POP(TCOUNT)
	PM_POP(TSCALE)
	PM_POP(TPERIOD)
	PM_POP(TCNTL)
	PM_POP(IPRIO)
	PM_POP(ILAT)
	PM_POP(IMASK)
	PM_POP(EVT15)
	PM_POP(EVT14)
	PM_POP(EVT13)
	PM_POP(EVT12)
	PM_POP(EVT11)
	PM_POP(EVT10)
	PM_POP(EVT9)
	PM_POP(EVT8)
	PM_POP(EVT7)
	PM_POP(EVT6)
	PM_POP(EVT5)
	PM_POP(EVT4)
	PM_POP(EVT3)
	PM_POP(EVT2)
	PM_POP(EVT1)
	PM_POP(EVT0)
	PM_POP(ICPLB_DATA15)
	PM_POP(ICPLB_DATA14)
	PM_POP(ICPLB_DATA13)
	PM_POP(ICPLB_DATA12)
	PM_POP(ICPLB_DATA11)
	PM_POP(ICPLB_DATA10)
	PM_POP(ICPLB_DATA9)
	PM_POP(ICPLB_DATA8)
	PM_POP(ICPLB_DATA7)
	PM_POP(ICPLB_DATA6)
	PM_POP(ICPLB_DATA5)
	PM_POP(ICPLB_DATA4)
	PM_POP(ICPLB_DATA3)
	PM_POP(ICPLB_DATA2)
	PM_POP(ICPLB_DATA1)
	PM_POP(ICPLB_DATA0)
	PM_POP(ICPLB_ADDR15)
	PM_POP(ICPLB_ADDR14)
	PM_POP(ICPLB_ADDR13)
	PM_POP(ICPLB_ADDR12)
	PM_POP(ICPLB_ADDR11)
	PM_POP(ICPLB_ADDR10)
	PM_POP(ICPLB_ADDR9)
	PM_POP(ICPLB_ADDR8)
	PM_POP(ICPLB_ADDR7)
	PM_POP(ICPLB_ADDR6)
	PM_POP(ICPLB_ADDR5)
	PM_POP(ICPLB_ADDR4)
	PM_POP(ICPLB_ADDR3)
	PM_POP(ICPLB_ADDR2)
	PM_POP(ICPLB_ADDR1)
	PM_POP(ICPLB_ADDR0)
	PM_POP(IMEM_CONTROL)
	PM_POP(DCPLB_DATA15)
	PM_POP(DCPLB_DATA14)
	PM_POP(DCPLB_DATA13)
	PM_POP(DCPLB_DATA12)
	PM_POP(DCPLB_DATA11)
	PM_POP(DCPLB_DATA10)
	PM_POP(DCPLB_DATA9)
	PM_POP(DCPLB_DATA8)
	PM_POP(DCPLB_DATA7)
	PM_POP(DCPLB_DATA6)
	PM_POP(DCPLB_DATA5)
	PM_POP(DCPLB_DATA4)
	PM_POP(DCPLB_DATA3)
	PM_POP(DCPLB_DATA2)
	PM_POP(DCPLB_DATA1)
	PM_POP(DCPLB_DATA0)
	PM_POP(DCPLB_ADDR15)
	PM_POP(DCPLB_ADDR14)
	PM_POP(DCPLB_ADDR13)
	PM_POP(DCPLB_ADDR12)
	PM_POP(DCPLB_ADDR11)
	PM_POP(DCPLB_ADDR10)
	PM_POP(DCPLB_ADDR9)
	PM_POP(DCPLB_ADDR8)
	PM_POP(DCPLB_ADDR7)
	PM_POP(DCPLB_ADDR6)
	PM_POP(DCPLB_ADDR5)
	PM_POP(DCPLB_ADDR4)
	PM_POP(DCPLB_ADDR3)
	PM_POP(DCPLB_ADDR2)
	PM_POP(DCPLB_ADDR1)
	PM_POP(DCPLB_ADDR0)
	PM_POP(DMEM_CONTROL)

	/* Restore System MMRs */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	PM_SYS_POP16(SYSCR)

#ifdef EBIU_FCTL
	PM_SYS_POP(EBIU_FCTL)
	PM_SYS_POP(EBIU_MODE)
	PM_SYS_POP(EBIU_MBSCTL)
#endif
	PM_SYS_POP16(EBIU_AMGCTL)
	PM_SYS_POP(EBIU_AMBCTL1)
	PM_SYS_POP(EBIU_AMBCTL0)

#ifdef PINT0_ASSIGN
	PM_SYS_POP(PINT3_ASSIGN)
	PM_SYS_POP(PINT2_ASSIGN)
	PM_SYS_POP(PINT1_ASSIGN)
	PM_SYS_POP(PINT0_ASSIGN)
#endif

#ifdef SICA_IWR1
	PM_SYS_POP(SICA_IWR1)
#endif
#ifdef SICA_IWR0
	PM_SYS_POP(SICA_IWR0)
#endif
#ifdef SIC_IWR2
	PM_SYS_POP(SIC_IWR2)
#endif
#ifdef SIC_IWR1
	PM_SYS_POP(SIC_IWR1)
#endif
#ifdef SIC_IWR0
	PM_SYS_POP(SIC_IWR0)
#endif
#ifdef SIC_IWR
	PM_SYS_POP(SIC_IWR)
#endif

#ifdef SICA_IAR0
	PM_SYS_POP(SICA_IAR7)
	PM_SYS_POP(SICA_IAR6)
	PM_SYS_POP(SICA_IAR5)
	PM_SYS_POP(SICA_IAR4)
	PM_SYS_POP(SICA_IAR3)
	PM_SYS_POP(SICA_IAR2)
	PM_SYS_POP(SICA_IAR1)
	PM_SYS_POP(SICA_IAR0)
#endif

#ifdef SIC_IAR8
	PM_SYS_POP(SIC_IAR11)
	PM_SYS_POP(SIC_IAR10)
	PM_SYS_POP(SIC_IAR9)
	PM_SYS_POP(SIC_IAR8)
#endif
#ifdef SIC_IAR7
	PM_SYS_POP(SIC_IAR7)
#endif
#ifdef SIC_IAR6
	PM_SYS_POP(SIC_IAR6)
	PM_SYS_POP(SIC_IAR5)
	PM_SYS_POP(SIC_IAR4)
#endif
#ifdef SIC_IAR3
	PM_SYS_POP(SIC_IAR3)
#endif
#ifdef SIC_IAR2
	PM_SYS_POP(SIC_IAR2)
	PM_SYS_POP(SIC_IAR1)
	PM_SYS_POP(SIC_IAR0)
#endif
#ifdef SICA_IMASK1
	PM_SYS_POP(SICA_IMASK1)
#endif
#ifdef SICA_IMASK0
	PM_SYS_POP(SICA_IMASK0)
#endif
#ifdef SIC_IMASK
	PM_SYS_POP(SIC_IMASK)
#endif
#ifdef SIC_IMASK2
	PM_SYS_POP(SIC_IMASK2)
#endif
#ifdef SIC_IMASK1
	PM_SYS_POP(SIC_IMASK1)
#endif
#ifdef SIC_IMASK0
	PM_SYS_POP(SIC_IMASK0)
#endif

	[--sp] = RETI;	/* Clear Global Interrupt Disable */
	SP += 4;

	RETS = [SP++];
	( R7:0, P5:0 ) = [SP++];
	RTS;
ENDPROC(_do_hibernate)
