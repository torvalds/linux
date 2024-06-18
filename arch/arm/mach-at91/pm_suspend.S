/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-at91/pm_slow_clock.S
 *
 *  Copyright (C) 2006 Savin Zlobec
 *
 * AT91SAM9 support:
 *  Copyright (C) 2007 Anti Sullin <anti.sullin@artecdesign.ee>
 */
#include <linux/linkage.h>
#include <linux/clk/at91_pmc.h>
#include "pm.h"
#include "pm_data-offsets.h"

#ifdef CONFIG_CPU_V7
.arch armv7-a
#endif

#define	SRAMC_SELF_FRESH_ACTIVE		0x01
#define	SRAMC_SELF_FRESH_EXIT		0x00

pmc	.req	r0
tmp1	.req	r4
tmp2	.req	r5
tmp3	.req	r6

/*
 * Wait until master clock is ready (after switching master clock source)
 *
 * @r_mckid:	register holding master clock identifier
 *
 * Side effects: overwrites r7, r8
 */
	.macro wait_mckrdy r_mckid
#ifdef CONFIG_SOC_SAMA7
	cmp	\r_mckid, #0
	beq	1f
	mov	r7, #AT91_PMC_MCKXRDY
	b	2f
#endif
1:	mov	r7, #AT91_PMC_MCKRDY
2:	ldr	r8, [pmc, #AT91_PMC_SR]
	and	r8, r7
	cmp	r8, r7
	bne	2b
	.endm

/*
 * Wait until master oscillator has stabilized.
 *
 * Side effects: overwrites r7
 */
	.macro wait_moscrdy
1:	ldr	r7, [pmc, #AT91_PMC_SR]
	tst	r7, #AT91_PMC_MOSCS
	beq	1b
	.endm

/*
 * Wait for main oscillator selection is done
 *
 * Side effects: overwrites r7
 */
	.macro wait_moscsels
1:	ldr	r7, [pmc, #AT91_PMC_SR]
	tst	r7, #AT91_PMC_MOSCSELS
	beq	1b
	.endm

/*
 * Put the processor to enter the idle state
 *
 * Side effects: overwrites r7
 */
	.macro at91_cpu_idle

#if defined(CONFIG_CPU_V7)
	mov	r7, #AT91_PMC_PCK
	str	r7, [pmc, #AT91_PMC_SCDR]

	dsb

	wfi		@ Wait For Interrupt
#else
	mcr	p15, 0, tmp1, c7, c0, 4
#endif

	.endm

/**
 * Set state for 2.5V low power regulator
 * @ena: 0 - disable regulator
 *	 1 - enable regulator
 *
 * Side effects: overwrites r7, r8, r9, r10
 */
	.macro at91_2_5V_reg_set_low_power ena
#ifdef CONFIG_SOC_SAMA7
	ldr	r7, .sfrbu
	mov	r8, #\ena
	ldr	r9, [r7, #AT91_SFRBU_25LDOCR]
	orr	r9, r9, #AT91_SFRBU_25LDOCR_LP
	cmp	r8, #1
	beq	lp_done_\ena
	bic	r9, r9, #AT91_SFRBU_25LDOCR_LP
lp_done_\ena:
	ldr	r10, =AT91_SFRBU_25LDOCR_LDOANAKEY
	orr	r9, r9, r10
	str	r9, [r7, #AT91_SFRBU_25LDOCR]
#endif
	.endm

	.macro at91_backup_set_lpm reg
#ifdef CONFIG_SOC_SAMA7
	orr	\reg, \reg, #0x200000
#endif
	.endm

	.text

	.arm

#ifdef CONFIG_SOC_SAMA7
/**
 * Enable self-refresh
 *
 * Side effects: overwrites r2, r3, tmp1, tmp2, tmp3, r7
 */
.macro at91_sramc_self_refresh_ena
	ldr	r2, .sramc_base
	ldr	r3, .sramc_phy_base
	ldr	r7, .pm_mode

	dsb

	/* Disable all AXI ports. */
	ldr	tmp1, [r2, #UDDRC_PCTRL_0]
	bic	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_0]

	ldr	tmp1, [r2, #UDDRC_PCTRL_1]
	bic	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_1]

	ldr	tmp1, [r2, #UDDRC_PCTRL_2]
	bic	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_2]

	ldr	tmp1, [r2, #UDDRC_PCTRL_3]
	bic	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_3]

	ldr	tmp1, [r2, #UDDRC_PCTRL_4]
	bic	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_4]

sr_ena_1:
	/* Wait for all ports to disable. */
	ldr	tmp1, [r2, #UDDRC_PSTAT]
	ldr	tmp2, =UDDRC_PSTAT_ALL_PORTS
	tst	tmp1, tmp2
	bne	sr_ena_1

	/* Switch to self-refresh. */
	ldr	tmp1, [r2, #UDDRC_PWRCTL]
	orr	tmp1, tmp1, #UDDRC_PWRCTL_SELFREF_SW
	str	tmp1, [r2, #UDDRC_PWRCTL]

sr_ena_2:
	/* Wait for self-refresh enter. */
	ldr	tmp1, [r2, #UDDRC_STAT]
	bic	tmp1, tmp1, #~UDDRC_STAT_SELFREF_TYPE_MSK
	cmp	tmp1, #UDDRC_STAT_SELFREF_TYPE_SW
	bne	sr_ena_2

	/* Disable DX DLLs for non-backup modes. */
	cmp	r7, #AT91_PM_BACKUP
	beq	sr_ena_3

	/* Do not soft reset the AC DLL. */
	ldr	tmp1, [r3, DDR3PHY_ACDLLCR]
	bic	tmp1, tmp1, DDR3PHY_ACDLLCR_DLLSRST
	str	tmp1, [r3, DDR3PHY_ACDLLCR]

	/* Disable DX DLLs. */
	ldr	tmp1, [r3, #DDR3PHY_DX0DLLCR]
	orr	tmp1, tmp1, #DDR3PHY_DXDLLCR_DLLDIS
	str	tmp1, [r3, #DDR3PHY_DX0DLLCR]

	ldr	tmp1, [r3, #DDR3PHY_DX1DLLCR]
	orr	tmp1, tmp1, #DDR3PHY_DXDLLCR_DLLDIS
	str	tmp1, [r3, #DDR3PHY_DX1DLLCR]

sr_ena_3:
	/* Power down DDR PHY data receivers. */
	ldr	tmp1, [r3, #DDR3PHY_DXCCR]
	orr	tmp1, tmp1, #DDR3PHY_DXCCR_DXPDR
	str	tmp1, [r3, #DDR3PHY_DXCCR]

	/* Power down ADDR/CMD IO. */
	ldr	tmp1, [r3, #DDR3PHY_ACIOCR]
	orr	tmp1, tmp1, #DDR3PHY_ACIORC_ACPDD
	orr	tmp1, tmp1, #DDR3PHY_ACIOCR_CKPDD_CK0
	orr	tmp1, tmp1, #DDR3PHY_ACIOCR_CSPDD_CS0
	str	tmp1, [r3, #DDR3PHY_ACIOCR]

	/* Power down ODT. */
	ldr	tmp1, [r3, #DDR3PHY_DSGCR]
	orr	tmp1, tmp1, #DDR3PHY_DSGCR_ODTPDD_ODT0
	str	tmp1, [r3, #DDR3PHY_DSGCR]
.endm

/**
 * Disable self-refresh
 *
 * Side effects: overwrites r2, r3, tmp1, tmp2, tmp3
 */
.macro at91_sramc_self_refresh_dis
	ldr	r2, .sramc_base
	ldr	r3, .sramc_phy_base

	/* Power up DDR PHY data receivers. */
	ldr	tmp1, [r3, #DDR3PHY_DXCCR]
	bic	tmp1, tmp1, #DDR3PHY_DXCCR_DXPDR
	str	tmp1, [r3, #DDR3PHY_DXCCR]

	/* Power up the output of CK and CS pins. */
	ldr	tmp1, [r3, #DDR3PHY_ACIOCR]
	bic	tmp1, tmp1, #DDR3PHY_ACIORC_ACPDD
	bic	tmp1, tmp1, #DDR3PHY_ACIOCR_CKPDD_CK0
	bic	tmp1, tmp1, #DDR3PHY_ACIOCR_CSPDD_CS0
	str	tmp1, [r3, #DDR3PHY_ACIOCR]

	/* Power up ODT. */
	ldr	tmp1, [r3, #DDR3PHY_DSGCR]
	bic	tmp1, tmp1, #DDR3PHY_DSGCR_ODTPDD_ODT0
	str	tmp1, [r3, #DDR3PHY_DSGCR]

	/* Enable DX DLLs. */
	ldr	tmp1, [r3, #DDR3PHY_DX0DLLCR]
	bic	tmp1, tmp1, #DDR3PHY_DXDLLCR_DLLDIS
	str	tmp1, [r3, #DDR3PHY_DX0DLLCR]

	ldr	tmp1, [r3, #DDR3PHY_DX1DLLCR]
	bic	tmp1, tmp1, #DDR3PHY_DXDLLCR_DLLDIS
	str	tmp1, [r3, #DDR3PHY_DX1DLLCR]

	/* Enable quasi-dynamic programming. */
	mov	tmp1, #0
	str	tmp1, [r2, #UDDRC_SWCTRL]

	/* De-assert SDRAM initialization. */
	ldr	tmp1, [r2, #UDDRC_DFIMISC]
	bic	tmp1, tmp1, #UDDRC_DFIMISC_DFI_INIT_COMPLETE_EN
	str	tmp1, [r2, #UDDRC_DFIMISC]

	/* Quasi-dynamic programming done. */
	mov	tmp1, #UDDRC_SWCTRL_SW_DONE
	str	tmp1, [r2, #UDDRC_SWCTRL]

sr_dis_1:
	ldr	tmp1, [r2, #UDDRC_SWSTAT]
	tst	tmp1, #UDDRC_SWSTAT_SW_DONE_ACK
	beq	sr_dis_1

	/* DLL soft-reset + DLL lock wait + ITM reset */
	mov	tmp1, #(DDR3PHY_PIR_INIT | DDR3PHY_PIR_DLLSRST | \
			DDR3PHY_PIR_DLLLOCK | DDR3PHY_PIR_ITMSRST)
	str	tmp1, [r3, #DDR3PHY_PIR]

sr_dis_4:
	/* Wait for it. */
	ldr	tmp1, [r3, #DDR3PHY_PGSR]
	tst	tmp1, #DDR3PHY_PGSR_IDONE
	beq	sr_dis_4

	/* Enable quasi-dynamic programming. */
	mov	tmp1, #0
	str	tmp1, [r2, #UDDRC_SWCTRL]

	/* Assert PHY init complete enable signal. */
	ldr	tmp1, [r2, #UDDRC_DFIMISC]
	orr	tmp1, tmp1, #UDDRC_DFIMISC_DFI_INIT_COMPLETE_EN
	str	tmp1, [r2, #UDDRC_DFIMISC]

	/* Programming is done. Set sw_done. */
	mov	tmp1, #UDDRC_SWCTRL_SW_DONE
	str	tmp1, [r2, #UDDRC_SWCTRL]

sr_dis_5:
	/* Wait for it. */
	ldr	tmp1, [r2, #UDDRC_SWSTAT]
	tst	tmp1, #UDDRC_SWSTAT_SW_DONE_ACK
	beq	sr_dis_5

	/* Trigger self-refresh exit. */
	ldr	tmp1, [r2, #UDDRC_PWRCTL]
	bic	tmp1, tmp1, #UDDRC_PWRCTL_SELFREF_SW
	str	tmp1, [r2, #UDDRC_PWRCTL]

sr_dis_6:
	/* Wait for self-refresh exit done. */
	ldr	tmp1, [r2, #UDDRC_STAT]
	bic	tmp1, tmp1, #~UDDRC_STAT_OPMODE_MSK
	cmp	tmp1, #UDDRC_STAT_OPMODE_NORMAL
	bne	sr_dis_6

	/* Enable all AXI ports. */
	ldr	tmp1, [r2, #UDDRC_PCTRL_0]
	orr	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_0]

	ldr	tmp1, [r2, #UDDRC_PCTRL_1]
	orr	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_1]

	ldr	tmp1, [r2, #UDDRC_PCTRL_2]
	orr	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_2]

	ldr	tmp1, [r2, #UDDRC_PCTRL_3]
	orr	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_3]

	ldr	tmp1, [r2, #UDDRC_PCTRL_4]
	orr	tmp1, tmp1, #0x1
	str	tmp1, [r2, #UDDRC_PCTRL_4]

	dsb
.endm
#else
/**
 * Enable self-refresh
 *
 * register usage:
 * 	@r1: memory type
 *	@r2: base address of the sram controller
 *	@r3: temporary
 */
.macro at91_sramc_self_refresh_ena
	ldr	r1, .memtype
	ldr	r2, .sramc_base

	cmp	r1, #AT91_MEMCTRL_MC
	bne	sr_ena_ddrc_sf

	/* Active SDRAM self-refresh mode */
	mov	r3, #1
	str	r3, [r2, #AT91_MC_SDRAMC_SRR]
	b	sr_ena_exit

sr_ena_ddrc_sf:
	cmp	r1, #AT91_MEMCTRL_DDRSDR
	bne	sr_ena_sdramc_sf

	/*
	 * DDR Memory controller
	 */

	/* LPDDR1 --> force DDR2 mode during self-refresh */
	ldr	r3, [r2, #AT91_DDRSDRC_MDR]
	str	r3, .saved_sam9_mdr
	bic	r3, r3, #~AT91_DDRSDRC_MD
	cmp	r3, #AT91_DDRSDRC_MD_LOW_POWER_DDR
	ldreq	r3, [r2, #AT91_DDRSDRC_MDR]
	biceq	r3, r3, #AT91_DDRSDRC_MD
	orreq	r3, r3, #AT91_DDRSDRC_MD_DDR2
	streq	r3, [r2, #AT91_DDRSDRC_MDR]

	/* Active DDRC self-refresh mode */
	ldr	r3, [r2, #AT91_DDRSDRC_LPR]
	str	r3, .saved_sam9_lpr
	bic	r3, r3, #AT91_DDRSDRC_LPCB
	orr	r3, r3, #AT91_DDRSDRC_LPCB_SELF_REFRESH
	str	r3, [r2, #AT91_DDRSDRC_LPR]

	/* If using the 2nd ddr controller */
	ldr	r2, .sramc1_base
	cmp	r2, #0
	beq	sr_ena_no_2nd_ddrc

	ldr	r3, [r2, #AT91_DDRSDRC_MDR]
	str	r3, .saved_sam9_mdr1
	bic	r3, r3, #~AT91_DDRSDRC_MD
	cmp	r3, #AT91_DDRSDRC_MD_LOW_POWER_DDR
	ldreq	r3, [r2, #AT91_DDRSDRC_MDR]
	biceq	r3, r3, #AT91_DDRSDRC_MD
	orreq	r3, r3, #AT91_DDRSDRC_MD_DDR2
	streq	r3, [r2, #AT91_DDRSDRC_MDR]

	/* Active DDRC self-refresh mode */
	ldr	r3, [r2, #AT91_DDRSDRC_LPR]
	str	r3, .saved_sam9_lpr1
	bic	r3, r3, #AT91_DDRSDRC_LPCB
	orr	r3, r3, #AT91_DDRSDRC_LPCB_SELF_REFRESH
	str	r3, [r2, #AT91_DDRSDRC_LPR]

sr_ena_no_2nd_ddrc:
	b	sr_ena_exit

	/*
	 * SDRAMC Memory controller
	 */
sr_ena_sdramc_sf:
	/* Active SDRAMC self-refresh mode */
	ldr	r3, [r2, #AT91_SDRAMC_LPR]
	str	r3, .saved_sam9_lpr
	bic	r3, r3, #AT91_SDRAMC_LPCB
	orr	r3, r3, #AT91_SDRAMC_LPCB_SELF_REFRESH
	str	r3, [r2, #AT91_SDRAMC_LPR]

	ldr	r3, .saved_sam9_lpr
	str	r3, [r2, #AT91_SDRAMC_LPR]

sr_ena_exit:
.endm

/**
 * Disable self-refresh
 *
 * register usage:
 * 	@r1: memory type
 *	@r2: base address of the sram controller
 *	@r3: temporary
 */
.macro at91_sramc_self_refresh_dis
	ldr	r1, .memtype
	ldr	r2, .sramc_base

	cmp	r1, #AT91_MEMCTRL_MC
	bne	sr_dis_ddrc_exit_sf

	/*
	 * at91rm9200 Memory controller
	 */

	 /*
	  * For exiting the self-refresh mode, do nothing,
	  * automatically exit the self-refresh mode.
	  */
	b	sr_dis_exit

sr_dis_ddrc_exit_sf:
	cmp	r1, #AT91_MEMCTRL_DDRSDR
	bne	sdramc_exit_sf

	/* DDR Memory controller */

	/* Restore MDR in case of LPDDR1 */
	ldr	r3, .saved_sam9_mdr
	str	r3, [r2, #AT91_DDRSDRC_MDR]
	/* Restore LPR on AT91 with DDRAM */
	ldr	r3, .saved_sam9_lpr
	str	r3, [r2, #AT91_DDRSDRC_LPR]

	/* If using the 2nd ddr controller */
	ldr	r2, .sramc1_base
	cmp	r2, #0
	ldrne	r3, .saved_sam9_mdr1
	strne	r3, [r2, #AT91_DDRSDRC_MDR]
	ldrne	r3, .saved_sam9_lpr1
	strne	r3, [r2, #AT91_DDRSDRC_LPR]

	b	sr_dis_exit

sdramc_exit_sf:
	/* SDRAMC Memory controller */
	ldr	r3, .saved_sam9_lpr
	str	r3, [r2, #AT91_SDRAMC_LPR]

sr_dis_exit:
.endm
#endif

.macro at91_pm_ulp0_mode
	ldr	pmc, .pmc_base
	ldr	tmp2, .pm_mode
	ldr	tmp3, .mckr_offset

	/* Check if ULP0 fast variant has been requested. */
	cmp	tmp2, #AT91_PM_ULP0_FAST
	bne	0f

	/* Set highest prescaler for power saving */
	ldr	tmp1, [pmc, tmp3]
	bic	tmp1, tmp1, #AT91_PMC_PRES
	orr	tmp1, tmp1, #AT91_PMC_PRES_64
	str	tmp1, [pmc, tmp3]

	mov	tmp3, #0
	wait_mckrdy tmp3
	b	1f

0:
	/* Turn off the crystal oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	bic	tmp1, tmp1, #AT91_PMC_MOSCEN
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Save RC oscillator state */
	ldr	tmp1, [pmc, #AT91_PMC_SR]
	str	tmp1, .saved_osc_status
	tst	tmp1, #AT91_PMC_MOSCRCS
	bne	1f

	/* Turn off RC oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	bic	tmp1, tmp1, #AT91_PMC_MOSCRCEN
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Wait main RC disabled done */
2:	ldr	tmp1, [pmc, #AT91_PMC_SR]
	tst	tmp1, #AT91_PMC_MOSCRCS
	bne	2b

	/* Wait for interrupt */
1:	at91_cpu_idle

	/* Check if ULP0 fast variant has been requested. */
	cmp	tmp2, #AT91_PM_ULP0_FAST
	bne	5f

	/* Set lowest prescaler for fast resume. */
	ldr	tmp3, .mckr_offset
	ldr	tmp1, [pmc, tmp3]
	bic	tmp1, tmp1, #AT91_PMC_PRES
	str	tmp1, [pmc, tmp3]

	mov	tmp3, #0
	wait_mckrdy tmp3
	b	6f

5:	/* Restore RC oscillator state */
	ldr	tmp1, .saved_osc_status
	tst	tmp1, #AT91_PMC_MOSCRCS
	beq	4f

	/* Turn on RC oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	orr	tmp1, tmp1, #AT91_PMC_MOSCRCEN
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Wait main RC stabilization */
3:	ldr	tmp1, [pmc, #AT91_PMC_SR]
	tst	tmp1, #AT91_PMC_MOSCRCS
	beq	3b

	/* Turn on the crystal oscillator */
4:	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	orr	tmp1, tmp1, #AT91_PMC_MOSCEN
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	wait_moscrdy
6:
.endm

/**
 * Note: This procedure only applies on the platform which uses
 * the external crystal oscillator as a main clock source.
 */
.macro at91_pm_ulp1_mode
	ldr	pmc, .pmc_base
	ldr	tmp2, .mckr_offset
	mov	tmp3, #0

	/* Save RC oscillator state and check if it is enabled. */
	ldr	tmp1, [pmc, #AT91_PMC_SR]
	str	tmp1, .saved_osc_status
	tst	tmp1, #AT91_PMC_MOSCRCS
	bne	2f

	/* Enable RC oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	orr	tmp1, tmp1, #AT91_PMC_MOSCRCEN
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Wait main RC stabilization */
1:	ldr	tmp1, [pmc, #AT91_PMC_SR]
	tst	tmp1, #AT91_PMC_MOSCRCS
	beq	1b

	/* Switch the main clock source to 12-MHz RC oscillator */
2:	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	bic	tmp1, tmp1, #AT91_PMC_MOSCSEL
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	wait_moscsels

	/* Disable the crystal oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	bic	tmp1, tmp1, #AT91_PMC_MOSCEN
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Switch the master clock source to main clock */
	ldr	tmp1, [pmc, tmp2]
	bic	tmp1, tmp1, #AT91_PMC_CSS
	orr	tmp1, tmp1, #AT91_PMC_CSS_MAIN
	str	tmp1, [pmc, tmp2]

	wait_mckrdy tmp3

	/* Enter the ULP1 mode by set WAITMODE bit in CKGR_MOR */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	orr	tmp1, tmp1, #AT91_PMC_WAITMODE
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Quirk for SAM9X60's PMC */
	nop
	nop

	wait_mckrdy tmp3

	/* Enable the crystal oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	orr	tmp1, tmp1, #AT91_PMC_MOSCEN
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	wait_moscrdy

	/* Switch the master clock source to slow clock */
	ldr	tmp1, [pmc, tmp2]
	bic	tmp1, tmp1, #AT91_PMC_CSS
	str	tmp1, [pmc, tmp2]

	wait_mckrdy tmp3

	/* Switch main clock source to crystal oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	orr	tmp1, tmp1, #AT91_PMC_MOSCSEL
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	wait_moscsels

	/* Switch the master clock source to main clock */
	ldr	tmp1, [pmc, tmp2]
	bic	tmp1, tmp1, #AT91_PMC_CSS
	orr	tmp1, tmp1, #AT91_PMC_CSS_MAIN
	str	tmp1, [pmc, tmp2]

	wait_mckrdy tmp3

	/* Restore RC oscillator state */
	ldr	tmp1, .saved_osc_status
	tst	tmp1, #AT91_PMC_MOSCRCS
	bne	3f

	/* Disable RC oscillator */
	ldr	tmp1, [pmc, #AT91_CKGR_MOR]
	bic	tmp1, tmp1, #AT91_PMC_MOSCRCEN
	bic	tmp1, tmp1, #AT91_PMC_KEY_MASK
	orr	tmp1, tmp1, #AT91_PMC_KEY
	str	tmp1, [pmc, #AT91_CKGR_MOR]

	/* Wait RC oscillator disable done */
4:	ldr	tmp1, [pmc, #AT91_PMC_SR]
	tst	tmp1, #AT91_PMC_MOSCRCS
	bne	4b

3:
.endm

.macro at91_plla_disable
	/* Save PLLA setting and disable it */
	ldr	tmp1, .pmc_version
	cmp	tmp1, #AT91_PMC_V1
	beq	1f

#ifdef CONFIG_HAVE_AT91_SAM9X60_PLL
	/* Save PLLA settings. */
	ldr	tmp2, [pmc, #AT91_PMC_PLL_UPDT]
	bic	tmp2, tmp2, #AT91_PMC_PLL_UPDT_ID
	str	tmp2, [pmc, #AT91_PMC_PLL_UPDT]

	/* save div. */
	mov	tmp1, #0
	ldr	tmp2, [pmc, #AT91_PMC_PLL_CTRL0]
	bic	tmp2, tmp2, #0xffffff00
	orr	tmp1, tmp1, tmp2

	/* save mul. */
	ldr	tmp2, [pmc, #AT91_PMC_PLL_CTRL1]
	bic	tmp2, tmp2, #0xffffff
	orr	tmp1, tmp1, tmp2
	str	tmp1, .saved_pllar

	/* step 2. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_UPDT]
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_UPDATE
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_ID
	str	tmp1, [pmc, #AT91_PMC_PLL_UPDT]

	/* step 3. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_CTRL0]
	bic	tmp1, tmp1, #AT91_PMC_PLL_CTRL0_ENPLLCK
	orr	tmp1, tmp1, #AT91_PMC_PLL_CTRL0_ENPLL
	str	tmp1, [pmc, #AT91_PMC_PLL_CTRL0]

	/* step 4. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_UPDT]
	orr	tmp1, tmp1, #AT91_PMC_PLL_UPDT_UPDATE
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_ID
	str	tmp1, [pmc, #AT91_PMC_PLL_UPDT]

	/* step 5. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_CTRL0]
	bic	tmp1, tmp1, #AT91_PMC_PLL_CTRL0_ENPLL
	str	tmp1, [pmc, #AT91_PMC_PLL_CTRL0]

	/* step 7. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_UPDT]
	orr	tmp1, tmp1, #AT91_PMC_PLL_UPDT_UPDATE
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_ID
	str	tmp1, [pmc, #AT91_PMC_PLL_UPDT]

	b	2f
#endif

1:	/* Save PLLA setting and disable it */
	ldr	tmp1, [pmc, #AT91_CKGR_PLLAR]
	str	tmp1, .saved_pllar

	/* Disable PLLA. */
	mov	tmp1, #AT91_PMC_PLLCOUNT
	orr	tmp1, tmp1, #(1 << 29)		/* bit 29 always set */
	str	tmp1, [pmc, #AT91_CKGR_PLLAR]
2:
.endm

.macro at91_plla_enable
	ldr	tmp2, .saved_pllar
	ldr	tmp3, .pmc_version
	cmp	tmp3, #AT91_PMC_V1
	beq	4f

#ifdef CONFIG_HAVE_AT91_SAM9X60_PLL
	/* step 1. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_UPDT]
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_ID
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_UPDATE
	str	tmp1, [pmc, #AT91_PMC_PLL_UPDT]

	/* step 2. */
	ldr	tmp1, =AT91_PMC_PLL_ACR_DEFAULT_PLLA
	str	tmp1, [pmc, #AT91_PMC_PLL_ACR]

	/* step 3. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_CTRL1]
	mov	tmp3, tmp2
	bic	tmp3, tmp3, #0xffffff
	orr	tmp1, tmp1, tmp3
	str	tmp1, [pmc, #AT91_PMC_PLL_CTRL1]

	/* step 8. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_UPDT]
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_ID
	orr	tmp1, tmp1, #AT91_PMC_PLL_UPDT_UPDATE
	str	tmp1, [pmc, #AT91_PMC_PLL_UPDT]

	/* step 9. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_CTRL0]
	orr	tmp1, tmp1, #AT91_PMC_PLL_CTRL0_ENLOCK
	orr	tmp1, tmp1, #AT91_PMC_PLL_CTRL0_ENPLL
	orr	tmp1, tmp1, #AT91_PMC_PLL_CTRL0_ENPLLCK
	bic	tmp1, tmp1, #0xff
	mov	tmp3, tmp2
	bic	tmp3, tmp3, #0xffffff00
	orr	tmp1, tmp1, tmp3
	str	tmp1, [pmc, #AT91_PMC_PLL_CTRL0]

	/* step 10. */
	ldr	tmp1, [pmc, #AT91_PMC_PLL_UPDT]
	orr	tmp1, tmp1, #AT91_PMC_PLL_UPDT_UPDATE
	bic	tmp1, tmp1, #AT91_PMC_PLL_UPDT_ID
	str	tmp1, [pmc, #AT91_PMC_PLL_UPDT]

	/* step 11. */
3:	ldr	tmp1, [pmc, #AT91_PMC_PLL_ISR0]
	tst	tmp1, #0x1
	beq	3b
	b	2f
#endif

	/* Restore PLLA setting */
4:	str	tmp2, [pmc, #AT91_CKGR_PLLAR]

	/* Enable PLLA. */
	tst	tmp2, #(AT91_PMC_MUL &  0xff0000)
	bne	1f
	tst	tmp2, #(AT91_PMC_MUL & ~0xff0000)
	beq	2f

1:	ldr	tmp1, [pmc, #AT91_PMC_SR]
	tst	tmp1, #AT91_PMC_LOCKA
	beq	1b
2:
.endm

/**
 * at91_mckx_ps_enable:	save MCK1..4 settings and switch it to main clock
 *
 * Side effects: overwrites tmp1, tmp2
 */
.macro at91_mckx_ps_enable
#ifdef CONFIG_SOC_SAMA7
	ldr	pmc, .pmc_base

	/* There are 4 MCKs we need to handle: MCK1..4 */
	mov	tmp1, #1
e_loop:	cmp	tmp1, #5
	beq	e_done

	/* Write MCK ID to retrieve the settings. */
	str	tmp1, [pmc, #AT91_PMC_MCR_V2]
	ldr	tmp2, [pmc, #AT91_PMC_MCR_V2]

e_save_mck1:
	cmp	tmp1, #1
	bne	e_save_mck2
	str	tmp2, .saved_mck1
	b	e_ps

e_save_mck2:
	cmp	tmp1, #2
	bne	e_save_mck3
	str	tmp2, .saved_mck2
	b	e_ps

e_save_mck3:
	cmp	tmp1, #3
	bne	e_save_mck4
	str	tmp2, .saved_mck3
	b	e_ps

e_save_mck4:
	str	tmp2, .saved_mck4

e_ps:
	/* Use CSS=MAINCK and DIV=1. */
	bic	tmp2, tmp2, #AT91_PMC_MCR_V2_CSS
	bic	tmp2, tmp2, #AT91_PMC_MCR_V2_DIV
	orr	tmp2, tmp2, #AT91_PMC_MCR_V2_CSS_MAINCK
	orr	tmp2, tmp2, #AT91_PMC_MCR_V2_DIV1
	str	tmp2, [pmc, #AT91_PMC_MCR_V2]

	wait_mckrdy tmp1

	add	tmp1, tmp1, #1
	b	e_loop

e_done:
#endif
.endm

/**
 * at91_mckx_ps_restore: restore MCK1..4 settings
 *
 * Side effects: overwrites tmp1, tmp2
 */
.macro at91_mckx_ps_restore
#ifdef CONFIG_SOC_SAMA7
	ldr	pmc, .pmc_base

	/* There are 4 MCKs we need to handle: MCK1..4 */
	mov	tmp1, #1
r_loop:	cmp	tmp1, #5
	beq	r_done

r_save_mck1:
	cmp	tmp1, #1
	bne	r_save_mck2
	ldr	tmp2, .saved_mck1
	b	r_ps

r_save_mck2:
	cmp	tmp1, #2
	bne	r_save_mck3
	ldr	tmp2, .saved_mck2
	b	r_ps

r_save_mck3:
	cmp	tmp1, #3
	bne	r_save_mck4
	ldr	tmp2, .saved_mck3
	b	r_ps

r_save_mck4:
	ldr	tmp2, .saved_mck4

r_ps:
	/* Write MCK ID to retrieve the settings. */
	str	tmp1, [pmc, #AT91_PMC_MCR_V2]
	ldr	tmp3, [pmc, #AT91_PMC_MCR_V2]

	/* We need to restore CSS and DIV. */
	bic	tmp3, tmp3, #AT91_PMC_MCR_V2_CSS
	bic	tmp3, tmp3, #AT91_PMC_MCR_V2_DIV
	orr	tmp3, tmp3, tmp2
	bic	tmp3, tmp3, #AT91_PMC_MCR_V2_ID_MSK
	orr	tmp3, tmp3, tmp1
	orr	tmp3, tmp3, #AT91_PMC_MCR_V2_CMD
	str	tmp2, [pmc, #AT91_PMC_MCR_V2]

	wait_mckrdy tmp1

	add	tmp1, tmp1, #1
	b	r_loop
r_done:
#endif
.endm

.macro at91_ulp_mode
	at91_mckx_ps_enable

	ldr	pmc, .pmc_base
	ldr	tmp2, .mckr_offset
	ldr	tmp3, .pm_mode

	/* Save Master clock setting */
	ldr	tmp1, [pmc, tmp2]
	str	tmp1, .saved_mckr

	/*
	 * Set master clock source to:
	 * - MAINCK if using ULP0 fast variant
	 * - slow clock, otherwise
	 */
	bic	tmp1, tmp1, #AT91_PMC_CSS
	cmp	tmp3, #AT91_PM_ULP0_FAST
	bne	save_mck
	orr	tmp1, tmp1, #AT91_PMC_CSS_MAIN
save_mck:
	str	tmp1, [pmc, tmp2]

	mov	tmp3, #0
	wait_mckrdy tmp3

	at91_plla_disable

	/* Enable low power mode for 2.5V regulator. */
	at91_2_5V_reg_set_low_power 1

	ldr	tmp3, .pm_mode
	cmp	tmp3, #AT91_PM_ULP1
	beq	ulp1_mode

	at91_pm_ulp0_mode
	b	ulp_exit

ulp1_mode:
	at91_pm_ulp1_mode
	b	ulp_exit

ulp_exit:
	/* Disable low power mode for 2.5V regulator. */
	at91_2_5V_reg_set_low_power 0

	ldr	pmc, .pmc_base

	at91_plla_enable

	/*
	 * Restore master clock setting
	 */
	ldr	tmp1, .mckr_offset
	ldr	tmp2, .saved_mckr
	str	tmp2, [pmc, tmp1]

	mov	tmp3, #0
	wait_mckrdy tmp3

	at91_mckx_ps_restore
.endm

.macro at91_backup_mode
	/* Switch the master clock source to slow clock. */
	ldr	pmc, .pmc_base
	ldr	tmp2, .mckr_offset
	ldr	tmp1, [pmc, tmp2]
	bic	tmp1, tmp1, #AT91_PMC_CSS
	str	tmp1, [pmc, tmp2]

	mov	tmp3, #0
	wait_mckrdy tmp3

	/*BUMEN*/
	ldr	r0, .sfrbu
	mov	tmp1, #0x1
	str	tmp1, [r0, #0x10]

	/* Wait for it. */
1:	ldr	tmp1, [r0, #0x10]
	tst	tmp1, #0x1
	beq	1b

	/* Shutdown */
	ldr	r0, .shdwc
	mov	tmp1, #0xA5000000
	add	tmp1, tmp1, #0x1
	at91_backup_set_lpm tmp1
	str	tmp1, [r0, #0]
.endm

/*
 * void at91_suspend_sram_fn(struct at91_pm_data*)
 * @input param:
 * 	@r0: base address of struct at91_pm_data
 */
/* at91_pm_suspend_in_sram must be 8-byte aligned per the requirements of fncpy() */
	.align 3
ENTRY(at91_pm_suspend_in_sram)
	/* Save registers on stack */
	stmfd	sp!, {r4 - r12, lr}

	/* Drain write buffer */
	mov	tmp1, #0
	mcr	p15, 0, tmp1, c7, c10, 4

	/* Flush tlb. */
	mov	r4, #0
	mcr	p15, 0, r4, c8, c7, 0

	ldr	tmp1, [r0, #PM_DATA_PMC_MCKR_OFFSET]
	str	tmp1, .mckr_offset
	ldr	tmp1, [r0, #PM_DATA_PMC_VERSION]
	str	tmp1, .pmc_version
	ldr	tmp1, [r0, #PM_DATA_MEMCTRL]
	str	tmp1, .memtype
	ldr	tmp1, [r0, #PM_DATA_MODE]
	str	tmp1, .pm_mode

	/*
	 * ldrne below are here to preload their address in the TLB as access
	 * to RAM may be limited while in self-refresh.
	 */
	ldr	tmp1, [r0, #PM_DATA_PMC]
	str	tmp1, .pmc_base
	cmp	tmp1, #0
	ldrne	tmp2, [tmp1, #0]

	ldr	tmp1, [r0, #PM_DATA_RAMC0]
	str	tmp1, .sramc_base
	cmp	tmp1, #0
	ldrne	tmp2, [tmp1, #0]

	ldr	tmp1, [r0, #PM_DATA_RAMC1]
	str	tmp1, .sramc1_base
	cmp	tmp1, #0
	ldrne	tmp2, [tmp1, #0]

#ifndef CONFIG_SOC_SAM_V4_V5
	/* ldrne below are here to preload their address in the TLB */
	ldr	tmp1, [r0, #PM_DATA_RAMC_PHY]
	str	tmp1, .sramc_phy_base
	cmp	tmp1, #0
	ldrne	tmp2, [tmp1, #0]

	ldr	tmp1, [r0, #PM_DATA_SHDWC]
	str	tmp1, .shdwc
	cmp	tmp1, #0
	ldrne	tmp2, [tmp1, #0]

	ldr	tmp1, [r0, #PM_DATA_SFRBU]
	str	tmp1, .sfrbu
	cmp	tmp1, #0
	ldrne	tmp2, [tmp1, #0x10]
#endif

	/* Active the self-refresh mode */
	at91_sramc_self_refresh_ena

	ldr	r0, .pm_mode
	cmp	r0, #AT91_PM_STANDBY
	beq	standby
	cmp	r0, #AT91_PM_BACKUP
	beq	backup_mode

	at91_ulp_mode
	b	exit_suspend

standby:
	/* Wait for interrupt */
	ldr	pmc, .pmc_base
	at91_cpu_idle
	b	exit_suspend

backup_mode:
	at91_backup_mode

exit_suspend:
	/* Exit the self-refresh mode */
	at91_sramc_self_refresh_dis

	/* Restore registers, and return */
	ldmfd	sp!, {r4 - r12, pc}
ENDPROC(at91_pm_suspend_in_sram)

.pmc_base:
	.word 0
.sramc_base:
	.word 0
.sramc1_base:
	.word 0
.sramc_phy_base:
	.word 0
.shdwc:
	.word 0
.sfrbu:
	.word 0
.memtype:
	.word 0
.pm_mode:
	.word 0
.mckr_offset:
	.word 0
.pmc_version:
	.word 0
.saved_mckr:
	.word 0
.saved_pllar:
	.word 0
.saved_sam9_lpr:
	.word 0
.saved_sam9_lpr1:
	.word 0
.saved_sam9_mdr:
	.word 0
.saved_sam9_mdr1:
	.word 0
.saved_osc_status:
	.word 0
#ifdef CONFIG_SOC_SAMA7
.saved_mck1:
	.word 0
.saved_mck2:
	.word 0
.saved_mck3:
	.word 0
.saved_mck4:
	.word 0
#endif

ENTRY(at91_pm_suspend_in_sram_sz)
	.word .-at91_pm_suspend_in_sram
