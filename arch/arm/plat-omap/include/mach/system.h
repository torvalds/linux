/*
 * Copied from arch/arm/mach-sa1100/include/mach/system.h
 * Copyright (c) 1999 Nicolas Pitre <nico@cam.org>
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#ifndef CONFIG_MACH_VOICEBLUE
#define voiceblue_reset()		do {} while (0)
#endif

extern void omap_prcm_arch_reset(char mode);

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void omap1_arch_reset(char mode)
{
	/*
	 * Workaround for 5912/1611b bug mentioned in sprz209d.pdf p. 28
	 * "Global Software Reset Affects Traffic Controller Frequency".
	 */
	if (cpu_is_omap5912()) {
		omap_writew(omap_readw(DPLL_CTL) & ~(1 << 4),
				 DPLL_CTL);
		omap_writew(0x8, ARM_RSTCT1);
	}

	if (machine_is_voiceblue())
		voiceblue_reset();
	else
		omap_writew(1, ARM_RSTCT1);
}

static inline void arch_reset(char mode)
{
	if (!cpu_is_omap24xx())
		omap1_arch_reset(mode);
	else
		omap_prcm_arch_reset(mode);
}

#endif
