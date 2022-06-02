/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Low level suspend code for AM43XX SoCs
 *
 * Copyright (C) 2013-2018 Texas Instruments Incorporated - https://www.ti.com/
 *	Dave Gerlach, Vaibhav Bedia
 */

#include <linux/linkage.h>
#include <linux/ti-emif-sram.h>
#include <linux/platform_data/pm33xx.h>
#include <asm/assembler.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/page.h>

#include "cm33xx.h"
#include "common.h"
#include "iomap.h"
#include "omap-secure.h"
#include "omap44xx.h"
#include "pm-asm-offsets.h"
#include "prm33xx.h"
#include "prcm43xx.h"

/* replicated define because linux/bitops.h cannot be included in assembly */
#define BIT(nr)			(1 << (nr))

#define AM33XX_CM_CLKCTRL_MODULESTATE_DISABLED		0x00030000
#define AM33XX_CM_CLKCTRL_MODULEMODE_DISABLE		0x0003
#define AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE		0x0002

#define AM43XX_EMIF_POWEROFF_ENABLE			0x1
#define AM43XX_EMIF_POWEROFF_DISABLE			0x0

#define AM43XX_CM_CLKSTCTRL_CLKTRCTRL_SW_SLEEP		0x1
#define AM43XX_CM_CLKSTCTRL_CLKTRCTRL_HW_AUTO		0x3

#define AM43XX_CM_BASE					0x44DF0000

#define AM43XX_CM_REGADDR(inst, reg)                           \
       AM33XX_L4_WK_IO_ADDRESS(AM43XX_CM_BASE + (inst) + (reg))

#define AM43XX_CM_MPU_CLKSTCTRL AM43XX_CM_REGADDR(AM43XX_CM_MPU_INST, \
					AM43XX_CM_MPU_MPU_CDOFFS)
#define AM43XX_CM_MPU_MPU_CLKCTRL AM43XX_CM_REGADDR(AM43XX_CM_MPU_INST, \
					AM43XX_CM_MPU_MPU_CLKCTRL_OFFSET)
#define AM43XX_CM_PER_EMIF_CLKCTRL  AM43XX_CM_REGADDR(AM43XX_CM_PER_INST, \
					AM43XX_CM_PER_EMIF_CLKCTRL_OFFSET)
#define AM43XX_PRM_EMIF_CTRL_OFFSET			0x0030

#define RTC_SECONDS_REG					0x0
#define RTC_PMIC_REG					0x98
#define RTC_PMIC_POWER_EN				BIT(16)
#define RTC_PMIC_EXT_WAKEUP_STS				BIT(12)
#define RTC_PMIC_EXT_WAKEUP_POL				BIT(4)
#define RTC_PMIC_EXT_WAKEUP_EN				BIT(0)

	.arm
	.arch armv7-a
	.arch_extension sec
	.align 3

ENTRY(am43xx_do_wfi)
	stmfd	sp!, {r4 - r11, lr}	@ save registers on stack

	/* Save wfi_flags arg to data space */
	mov	r4, r0
	adr	r3, am43xx_pm_ro_sram_data
	ldr	r2, [r3, #AMX3_PM_RO_SRAM_DATA_VIRT_OFFSET]
	str	r4, [r2, #AMX3_PM_WFI_FLAGS_OFFSET]

#ifdef CONFIG_CACHE_L2X0
	/* Retrieve l2 cache virt address BEFORE we shut off EMIF */
	ldr	r1, get_l2cache_base
	blx	r1
	mov	r8, r0
#endif

	/* Only flush cache is we know we are losing MPU context */
	tst	r4, #WFI_FLAG_FLUSH_CACHE
	beq	cache_skip_flush

	/*
	 * Flush all data from the L1 and L2 data cache before disabling
	 * SCTLR.C bit.
	 */
	ldr	r1, kernel_flush
	blx	r1

	/*
	 * Clear the SCTLR.C bit to prevent further data cache
	 * allocation. Clearing SCTLR.C would make all the data accesses
	 * strongly ordered and would not hit the cache.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 2)	@ Disable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb
	dsb

	/*
	 * Invalidate L1 and L2 data cache.
	 */
	ldr	r1, kernel_flush
	blx	r1

#ifdef CONFIG_CACHE_L2X0
	/*
	 * Clean and invalidate the L2 cache.
	 */
#ifdef CONFIG_PL310_ERRATA_727915
	mov	r0, #0x03
	mov	r12, #OMAP4_MON_L2X0_DBG_CTRL_INDEX
	dsb
	smc	#0
	dsb
#endif
	mov	r0, r8
	adr	r4, am43xx_pm_ro_sram_data
	ldr	r3, [r4, #AMX3_PM_RO_SRAM_DATA_VIRT_OFFSET]

	mov	r2, r0
	ldr	r0, [r2, #L2X0_AUX_CTRL]
	str	r0, [r3, #AMX3_PM_L2_AUX_CTRL_VAL_OFFSET]
	ldr	r0, [r2, #L310_PREFETCH_CTRL]
	str	r0, [r3, #AMX3_PM_L2_PREFETCH_CTRL_VAL_OFFSET]

	ldr	r0, l2_val
	str	r0, [r2, #L2X0_CLEAN_INV_WAY]
wait:
	ldr	r0, [r2, #L2X0_CLEAN_INV_WAY]
	ldr	r1, l2_val
	ands	r0, r0, r1
	bne	wait
#ifdef CONFIG_PL310_ERRATA_727915
	mov	r0, #0x00
	mov	r12, #OMAP4_MON_L2X0_DBG_CTRL_INDEX
	dsb
	smc	#0
	dsb
#endif
l2x_sync:
	mov	r0, r8
	mov	r2, r0
	mov	r0, #0x0
	str	r0, [r2, #L2X0_CACHE_SYNC]
sync:
	ldr	r0, [r2, #L2X0_CACHE_SYNC]
	ands	r0, r0, #0x1
	bne	sync
#endif

	/* Restore wfi_flags */
	adr	r3, am43xx_pm_ro_sram_data
	ldr	r2, [r3, #AMX3_PM_RO_SRAM_DATA_VIRT_OFFSET]
	ldr	r4, [r2, #AMX3_PM_WFI_FLAGS_OFFSET]

cache_skip_flush:
	/*
	 * If we are trying to enter RTC+DDR mode we must perform
	 * a read from the rtc address space to ensure translation
	 * presence in the TLB to avoid page table walk after DDR
	 * is unavailable.
	 */
	tst	r4, #WFI_FLAG_RTC_ONLY
	beq	skip_rtc_va_refresh

	adr	r3, am43xx_pm_ro_sram_data
	ldr	r1, [r3, #AMX3_PM_RTC_BASE_VIRT_OFFSET]
	ldr	r0, [r1]

skip_rtc_va_refresh:
	/* Check if we want self refresh */
	tst	r4, #WFI_FLAG_SELF_REFRESH
	beq	emif_skip_enter_sr

	adr     r9, am43xx_emif_sram_table

	ldr     r3, [r9, #EMIF_PM_ENTER_SR_OFFSET]
	blx     r3

emif_skip_enter_sr:
	/* Only necessary if PER is losing context */
	tst	r4, #WFI_FLAG_SAVE_EMIF
	beq	emif_skip_save

	ldr     r3, [r9, #EMIF_PM_SAVE_CONTEXT_OFFSET]
	blx	r3

emif_skip_save:
	/* Only can disable EMIF if we have entered self refresh */
	tst	r4, #WFI_FLAG_SELF_REFRESH
	beq	emif_skip_disable

	/* Disable EMIF */
	ldr	r1, am43xx_virt_emif_clkctrl
	ldr	r2, [r1]
	bic	r2, r2, #AM33XX_CM_CLKCTRL_MODULEMODE_DISABLE
	str	r2, [r1]

wait_emif_disable:
	ldr	r2, [r1]
	mov	r3, #AM33XX_CM_CLKCTRL_MODULESTATE_DISABLED
	cmp	r2, r3
	bne	wait_emif_disable

emif_skip_disable:
	tst	r4, #WFI_FLAG_RTC_ONLY
	beq	skip_rtc_only

	adr	r3, am43xx_pm_ro_sram_data
	ldr	r1, [r3, #AMX3_PM_RTC_BASE_VIRT_OFFSET]

	ldr	r0, [r1, #RTC_PMIC_REG]
	orr	r0, r0, #RTC_PMIC_POWER_EN
	orr	r0, r0, #RTC_PMIC_EXT_WAKEUP_STS
	orr	r0, r0, #RTC_PMIC_EXT_WAKEUP_EN
	orr	r0, r0, #RTC_PMIC_EXT_WAKEUP_POL
	str	r0, [r1, #RTC_PMIC_REG]
	ldr	r0, [r1, #RTC_PMIC_REG]
	/* Wait for 2 seconds to lose power */
	mov	r3, #2
	ldr	r2, [r1, #RTC_SECONDS_REG]
rtc_loop:
	ldr	r0, [r1, #RTC_SECONDS_REG]
	cmp	r0, r2
	beq	rtc_loop
	mov	r2, r0
	subs	r3, r3, #1
	bne	rtc_loop

	b	re_enable_emif

skip_rtc_only:

	tst	r4, #WFI_FLAG_WAKE_M3
	beq	wkup_m3_skip

	/*
	 * For the MPU WFI to be registered as an interrupt
	 * to WKUP_M3, MPU_CLKCTRL.MODULEMODE needs to be set
	 * to DISABLED
	 */
	ldr	r1, am43xx_virt_mpu_clkctrl
	ldr	r2, [r1]
	bic	r2, r2, #AM33XX_CM_CLKCTRL_MODULEMODE_DISABLE
	str	r2, [r1]

	/*
	 * Put MPU CLKDM to SW_SLEEP
	 */
	ldr	r1, am43xx_virt_mpu_clkstctrl
	mov	r2, #AM43XX_CM_CLKSTCTRL_CLKTRCTRL_SW_SLEEP
	str	r2, [r1]

wkup_m3_skip:
	/*
	 * Execute a barrier instruction to ensure that all cache,
	 * TLB and branch predictor maintenance operations issued
	 * have completed.
	 */
	dsb
	dmb

	/*
	 * Execute a WFI instruction and wait until the
	 * STANDBYWFI output is asserted to indicate that the
	 * CPU is in idle and low power state. CPU can specualatively
	 * prefetch the instructions so add NOPs after WFI. Sixteen
	 * NOPs as per Cortex-A9 pipeline.
	 */
	wfi

	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

	/* We come here in case of an abort due to a late interrupt */
	ldr	r1, am43xx_virt_mpu_clkstctrl
	mov	r2, #AM43XX_CM_CLKSTCTRL_CLKTRCTRL_HW_AUTO
	str	r2, [r1]

	/* Set MPU_CLKCTRL.MODULEMODE back to ENABLE */
	ldr	r1, am43xx_virt_mpu_clkctrl
	mov	r2, #AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE
	str	r2, [r1]

re_enable_emif:
	/* Re-enable EMIF */
	ldr	r1, am43xx_virt_emif_clkctrl
	mov	r2, #AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE
	str	r2, [r1]
wait_emif_enable:
	ldr	r3, [r1]
	cmp	r2, r3
	bne	wait_emif_enable

	tst	r4, #WFI_FLAG_FLUSH_CACHE
	beq	cache_skip_restore

	/*
	 * Set SCTLR.C bit to allow data cache allocation
	 */
	mrc	p15, 0, r0, c1, c0, 0
	orr	r0, r0, #(1 << 2)	@ Enable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

cache_skip_restore:
	/* Only necessary if PER is losing context */
	tst	r4, #WFI_FLAG_SELF_REFRESH
	beq	emif_skip_exit_sr_abt

	adr	r9, am43xx_emif_sram_table
	ldr	r1, [r9, #EMIF_PM_ABORT_SR_OFFSET]
	blx	r1

emif_skip_exit_sr_abt:
	/* Let the suspend code know about the abort */
	mov	r0, #1
	ldmfd	sp!, {r4 - r11, pc}	@ restore regs and return
ENDPROC(am43xx_do_wfi)

	.align
ENTRY(am43xx_resume_offset)
	.word . - am43xx_do_wfi

ENTRY(am43xx_resume_from_deep_sleep)
	/* Set MPU CLKSTCTRL to HW AUTO so that CPUidle works properly */
	ldr	r1, am43xx_virt_mpu_clkstctrl
	mov	r2, #AM43XX_CM_CLKSTCTRL_CLKTRCTRL_HW_AUTO
	str	r2, [r1]

	/* For AM43xx, use EMIF power down until context is restored */
	ldr	r2, am43xx_phys_emif_poweroff
	mov	r1, #AM43XX_EMIF_POWEROFF_ENABLE
	str	r1, [r2, #0x0]

	/* Re-enable EMIF */
	ldr	r1, am43xx_phys_emif_clkctrl
	mov	r2, #AM33XX_CM_CLKCTRL_MODULEMODE_ENABLE
	str	r2, [r1]
wait_emif_enable1:
	ldr	r3, [r1]
	cmp	r2, r3
	bne	wait_emif_enable1

	adr     r9, am43xx_emif_sram_table

	ldr     r1, [r9, #EMIF_PM_RESTORE_CONTEXT_OFFSET]
	blx     r1

	ldr     r1, [r9, #EMIF_PM_EXIT_SR_OFFSET]
	blx     r1

	ldr     r2, am43xx_phys_emif_poweroff
	mov     r1, #AM43XX_EMIF_POWEROFF_DISABLE
	str     r1, [r2, #0x0]

	ldr     r1, [r9, #EMIF_PM_RUN_HW_LEVELING]
	blx     r1

#ifdef CONFIG_CACHE_L2X0
	ldr	r2, l2_cache_base
	ldr	r0, [r2, #L2X0_CTRL]
	and	r0, #0x0f
	cmp	r0, #1
	beq	skip_l2en			@ Skip if already enabled

	adr	r4, am43xx_pm_ro_sram_data
	ldr	r3, [r4, #AMX3_PM_RO_SRAM_DATA_PHYS_OFFSET]
	ldr     r0, [r3, #AMX3_PM_L2_PREFETCH_CTRL_VAL_OFFSET]

	ldr	r12, l2_smc1
	dsb
	smc	#0
	dsb
set_aux_ctrl:
	ldr     r0, [r3, #AMX3_PM_L2_AUX_CTRL_VAL_OFFSET]
	ldr	r12, l2_smc2
	dsb
	smc	#0
	dsb

	/* L2 invalidate on resume */
	ldr	r0, l2_val
	ldr	r2, l2_cache_base
	str	r0, [r2, #L2X0_INV_WAY]
wait2:
	ldr	r0, [r2, #L2X0_INV_WAY]
	ldr	r1, l2_val
	ands	r0, r0, r1
	bne	wait2
#ifdef CONFIG_PL310_ERRATA_727915
	mov	r0, #0x00
	mov	r12, #OMAP4_MON_L2X0_DBG_CTRL_INDEX
	dsb
	smc	#0
	dsb
#endif
l2x_sync2:
	ldr	r2, l2_cache_base
	mov	r0, #0x0
	str	r0, [r2, #L2X0_CACHE_SYNC]
sync2:
	ldr	r0, [r2, #L2X0_CACHE_SYNC]
	ands	r0, r0, #0x1
	bne	sync2

	mov	r0, #0x1
	ldr	r12, l2_smc3
	dsb
	smc	#0
	dsb
#endif
skip_l2en:
	/* We are back. Branch to the common CPU resume routine */
	mov	r0, #0
	ldr	pc, resume_addr
ENDPROC(am43xx_resume_from_deep_sleep)

/*
 * Local variables
 */
	.align
kernel_flush:
	.word   v7_flush_dcache_all
ddr_start:
	.word	PAGE_OFFSET

am43xx_phys_emif_poweroff:
	.word   (AM43XX_CM_BASE + AM43XX_PRM_DEVICE_INST + \
		 AM43XX_PRM_EMIF_CTRL_OFFSET)
am43xx_virt_mpu_clkstctrl:
	.word	(AM43XX_CM_MPU_CLKSTCTRL)
am43xx_virt_mpu_clkctrl:
	.word	(AM43XX_CM_MPU_MPU_CLKCTRL)
am43xx_virt_emif_clkctrl:
	.word	(AM43XX_CM_PER_EMIF_CLKCTRL)
am43xx_phys_emif_clkctrl:
	.word	(AM43XX_CM_BASE + AM43XX_CM_PER_INST + \
		 AM43XX_CM_PER_EMIF_CLKCTRL_OFFSET)

#ifdef CONFIG_CACHE_L2X0
/* L2 cache related defines for AM437x */
get_l2cache_base:
	.word	omap4_get_l2cache_base
l2_cache_base:
	.word	OMAP44XX_L2CACHE_BASE
l2_smc1:
	.word	OMAP4_MON_L2X0_PREFETCH_INDEX
l2_smc2:
	.word	OMAP4_MON_L2X0_AUXCTRL_INDEX
l2_smc3:
	.word	OMAP4_MON_L2X0_CTRL_INDEX
l2_val:
	.word	0xffff
#endif

.align 3
/* DDR related defines */
ENTRY(am43xx_emif_sram_table)
	.space EMIF_PM_FUNCTIONS_SIZE

ENTRY(am43xx_pm_sram)
	.word am43xx_do_wfi
	.word am43xx_do_wfi_sz
	.word am43xx_resume_offset
	.word am43xx_emif_sram_table
	.word am43xx_pm_ro_sram_data

resume_addr:
	.word   cpu_resume - PAGE_OFFSET + 0x80000000
.align 3

ENTRY(am43xx_pm_ro_sram_data)
	.space AMX3_PM_RO_SRAM_DATA_SIZE

ENTRY(am43xx_do_wfi_sz)
	.word	. - am43xx_do_wfi
