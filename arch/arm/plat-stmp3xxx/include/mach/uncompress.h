/*
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_PLAT_UNCOMPRESS_H
#define __ASM_PLAT_UNCOMPRESS_H

/*
 * Register includes are for when the MMU enabled; we need to define our
 * own stuff here for pre-MMU use
 */
#define UARTDBG_BASE		0x80070000
#define UART(c)			(((volatile unsigned *)UARTDBG_BASE)[c])

/*
 * This does not append a newline
 */
static void putc(char c)
{
	/* Wait for TX fifo empty */
	while ((UART(6) & (1<<7)) == 0)
		continue;

	/* Write byte */
	UART(0) = c;

	/* Wait for last bit to exit the UART */
	while (UART(6) & (1<<3))
		continue;
}

static void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()

#endif /* __ASM_PLAT_UNCOMPRESS_H */
