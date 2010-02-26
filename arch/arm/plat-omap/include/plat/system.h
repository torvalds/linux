/*
 * Copied from arch/arm/mach-sa1100/include/mach/system.h
 * Copyright (c) 1999 Nicolas Pitre <nico@fluxnic.net>
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

#include <plat/prcm.h>

#ifndef CONFIG_MACH_VOICEBLUE
#define voiceblue_reset()		do {} while (0)
#else
extern void voiceblue_reset(void);
#endif

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void omap1_arch_reset(char mode, const char *cmd)
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

static inline void arch_reset(char mode, const char *cmd)
{
	if (!cpu_class_is_omap2())
		omap1_arch_reset(mode, cmd);
	else
		omap_prcm_arch_reset(mode, cmd);
}

#endif
