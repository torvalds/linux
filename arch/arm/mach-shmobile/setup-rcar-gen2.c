/*
 * R-Car Generation 2 support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/clk/shmobile.h>
#include <linux/clocksource.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/rcar-gen2.h>
#include <asm/mach/arch.h>
#include "common.h"

#define MODEMR 0xe6160060

u32 rcar_gen2_read_mode_pins(void)
{
	static u32 mode;
	static bool mode_valid;

	if (!mode_valid) {
		void __iomem *modemr = ioremap_nocache(MODEMR, 4);
		BUG_ON(!modemr);
		mode = ioread32(modemr);
		iounmap(modemr);
		mode_valid = true;
	}

	return mode;
}

#define CNTCR 0
#define CNTFID0 0x20

void __init rcar_gen2_timer_init(void)
{
#if defined(CONFIG_ARM_ARCH_TIMER) || defined(CONFIG_COMMON_CLK)
	u32 mode = rcar_gen2_read_mode_pins();
#endif
#ifdef CONFIG_ARM_ARCH_TIMER
	void __iomem *base;
	int extal_mhz = 0;
	u32 freq;

	/* At Linux boot time the r8a7790 arch timer comes up
	 * with the counter disabled. Moreover, it may also report
	 * a potentially incorrect fixed 13 MHz frequency. To be
	 * correct these registers need to be updated to use the
	 * frequency EXTAL / 2 which can be determined by the MD pins.
	 */

	switch (mode & (MD(14) | MD(13))) {
	case 0:
		extal_mhz = 15;
		break;
	case MD(13):
		extal_mhz = 20;
		break;
	case MD(14):
		extal_mhz = 26;
		break;
	case MD(13) | MD(14):
		extal_mhz = 30;
		break;
	}

	/* The arch timer frequency equals EXTAL / 2 */
	freq = extal_mhz * (1000000 / 2);

	/* Remap "armgcnt address map" space */
	base = ioremap(0xe6080000, PAGE_SIZE);

	/*
	 * Update the timer if it is either not running, or is not at the
	 * right frequency. The timer is only configurable in secure mode
	 * so this avoids an abort if the loader started the timer and
	 * entered the kernel in non-secure mode.
	 */

	if ((ioread32(base + CNTCR) & 1) == 0 ||
	    ioread32(base + CNTFID0) != freq) {
		/* Update registers with correct frequency */
		iowrite32(freq, base + CNTFID0);
		asm volatile("mcr p15, 0, %0, c14, c0, 0" : : "r" (freq));

		/* make sure arch timer is started by setting bit 0 of CNTCR */
		iowrite32(1, base + CNTCR);
	}

	iounmap(base);
#endif /* CONFIG_ARM_ARCH_TIMER */

#ifdef CONFIG_COMMON_CLK
	rcar_gen2_clocks_init(mode);
#endif
	clocksource_of_init();
}
