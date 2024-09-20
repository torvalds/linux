/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/smp.h>
#include <asm/bootinfo.h>
#include <asm/bmips.h>
#include <asm/smp-ops.h>
#include <asm/mipsregs.h>
#include <bcm63xx_board.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

void __init prom_init(void)
{
	u32 reg, mask;

	/* Cache CBR addr before CPU/DMA setup */
	bmips_cbr_addr = BMIPS_GET_CBR();

	bcm63xx_cpu_init();

	/* stop any running watchdog */
	bcm_wdt_writel(WDT_STOP_1, WDT_CTL_REG);
	bcm_wdt_writel(WDT_STOP_2, WDT_CTL_REG);

	/* disable all hardware blocks clock for now */
	if (BCMCPU_IS_3368())
		mask = CKCTL_3368_ALL_SAFE_EN;
	else if (BCMCPU_IS_6328())
		mask = CKCTL_6328_ALL_SAFE_EN;
	else if (BCMCPU_IS_6338())
		mask = CKCTL_6338_ALL_SAFE_EN;
	else if (BCMCPU_IS_6345())
		mask = CKCTL_6345_ALL_SAFE_EN;
	else if (BCMCPU_IS_6348())
		mask = CKCTL_6348_ALL_SAFE_EN;
	else if (BCMCPU_IS_6358())
		mask = CKCTL_6358_ALL_SAFE_EN;
	else if (BCMCPU_IS_6362())
		mask = CKCTL_6362_ALL_SAFE_EN;
	else if (BCMCPU_IS_6368())
		mask = CKCTL_6368_ALL_SAFE_EN;
	else
		mask = 0;

	reg = bcm_perf_readl(PERF_CKCTL_REG);
	reg &= ~mask;
	bcm_perf_writel(reg, PERF_CKCTL_REG);

	/* do low level board init */
	board_prom_init();

	/* set up SMP */
	if (!register_bmips_smp_ops()) {
		/*
		 * BCM6328 might not have its second CPU enabled, while BCM3368
		 * and BCM6358 need special handling for their shared TLB, so
		 * disable SMP for now.
		 */
		if (BCMCPU_IS_6328()) {
			reg = bcm_readl(BCM_6328_OTP_BASE +
					OTP_USER_BITS_6328_REG(3));

			if (reg & OTP_6328_REG3_TP1_DISABLED)
				bmips_smp_enabled = 0;
		} else if (BCMCPU_IS_3368() || BCMCPU_IS_6358()) {
			bmips_smp_enabled = 0;
		}

		if (!bmips_smp_enabled)
			return;

		/*
		 * The bootloader has set up the CPU1 reset vector at
		 * 0xa000_0200.
		 * This conflicts with the special interrupt vector (IV).
		 * The bootloader has also set up CPU1 to respond to the wrong
		 * IPI interrupt.
		 * Here we will start up CPU1 in the background and ask it to
		 * reconfigure itself then go back to sleep.
		 */
		memcpy((void *)0xa0000200, bmips_smp_movevec, 0x20);
		__sync();
		set_c0_cause(C_SW0);
		cpumask_set_cpu(1, &bmips_booted_mask);

		/*
		 * FIXME: we really should have some sort of hazard barrier here
		 */
	}
}
