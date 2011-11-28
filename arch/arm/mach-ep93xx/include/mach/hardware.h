/*
 * arch/arm/mach-ep93xx/include/mach/hardware.h
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <mach/ep93xx-regs.h>
#include <mach/platform.h>

/*
 * The EP93xx has two external crystal oscillators.  To generate the
 * required high-frequency clocks, the processor uses two phase-locked-
 * loops (PLLs) to multiply the incoming external clock signal to much
 * higher frequencies that are then divided down by programmable dividers
 * to produce the needed clocks.  The PLLs operate independently of one
 * another.
 */
#define EP93XX_EXT_CLK_RATE	14745600
#define EP93XX_EXT_RTC_RATE	32768

#define EP93XX_KEYTCHCLK_DIV4	(EP93XX_EXT_CLK_RATE / 4)
#define EP93XX_KEYTCHCLK_DIV16	(EP93XX_EXT_CLK_RATE / 16)

#endif
