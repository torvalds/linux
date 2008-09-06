/*
 * arch/arm/mach-shark/include/mach/system.h
 *
 * by Alexander Schulz
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <linux/io.h>

static void arch_reset(char mode)
{
	short temp;
	local_irq_disable();
	/* Reset the Machine via pc[3] of the sequoia chipset */
	outw(0x09,0x24);
	temp=inw(0x26);
	temp = temp | (1<<3) | (1<<10);
	outw(0x09,0x24);
	outw(temp,0x26);

}

static inline void arch_idle(void)
{
}

#endif
