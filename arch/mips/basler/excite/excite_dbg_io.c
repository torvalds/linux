/*
 *  Copyright (C) 2004 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/gdb-stub.h>
#include <asm/rm9k-ocd.h>
#include <excite.h>

#if defined(CONFIG_SERIAL_8250) && CONFIG_SERIAL_8250_NR_UARTS > 1
#error Debug port used by serial driver
#endif

#define UART_CLK		25000000
#define BASE_BAUD		(UART_CLK / 16)
#define REGISTER_BASE_0		0x0208UL
#define REGISTER_BASE_1		0x0238UL

#define REGISTER_BASE_DBG	REGISTER_BASE_1

#define CPRR	0x0004
#define UACFG	0x0200
#define UAINTS	0x0204
#define UARBR	(REGISTER_BASE_DBG + 0x0000)
#define UATHR	(REGISTER_BASE_DBG + 0x0004)
#define UADLL	(REGISTER_BASE_DBG + 0x0008)
#define UAIER	(REGISTER_BASE_DBG + 0x000c)
#define UADLH	(REGISTER_BASE_DBG + 0x0010)
#define UAIIR	(REGISTER_BASE_DBG + 0x0014)
#define UAFCR	(REGISTER_BASE_DBG + 0x0018)
#define UALCR	(REGISTER_BASE_DBG + 0x001c)
#define UAMCR	(REGISTER_BASE_DBG + 0x0020)
#define UALSR	(REGISTER_BASE_DBG + 0x0024)
#define UAMSR	(REGISTER_BASE_DBG + 0x0028)
#define UASCR	(REGISTER_BASE_DBG + 0x002c)

#define	PARITY_NONE	0
#define	PARITY_ODD	0x08
#define	PARITY_EVEN	0x18
#define	PARITY_MARK	0x28
#define	PARITY_SPACE	0x38

#define	DATA_5BIT	0x0
#define	DATA_6BIT	0x1
#define	DATA_7BIT	0x2
#define	DATA_8BIT	0x3

#define	STOP_1BIT	0x0
#define	STOP_2BIT	0x4

#define BAUD_DBG	57600
#define	PARITY_DBG	PARITY_NONE
#define	DATA_DBG	DATA_8BIT
#define	STOP_DBG	STOP_1BIT

/* Initialize the serial port for KGDB debugging */
void __init excite_kgdb_init(void)
{
	const u32 divisor = BASE_BAUD / BAUD_DBG;

	/* Take the UART out of reset */
	titan_writel(0x00ff1cff, CPRR);
	titan_writel(0x00000000, UACFG);
	titan_writel(0x00000002, UACFG);

	titan_writel(0x0, UALCR);
	titan_writel(0x0, UAIER);

	/* Disable FIFOs */
	titan_writel(0x00, UAFCR);

	titan_writel(0x80, UALCR);
	titan_writel(divisor & 0xff, UADLL);
	titan_writel((divisor & 0xff00) >> 8, UADLH);
	titan_writel(0x0, UALCR);

	titan_writel(DATA_DBG | PARITY_DBG | STOP_DBG, UALCR);

	/* Enable receiver interrupt */
	titan_readl(UARBR);
	titan_writel(0x1, UAIER);
}

int getDebugChar(void)
{
	while (!(titan_readl(UALSR) & 0x1));
	return titan_readl(UARBR);
}

int putDebugChar(int data)
{
	while (!(titan_readl(UALSR) & 0x20));
	titan_writel(data, UATHR);
	return 1;
}

/* KGDB interrupt handler */
asmlinkage void excite_kgdb_inthdl(struct pt_regs *regs)
{
	if (unlikely(
		((titan_readl(UAIIR) & 0x7) == 4)
		&& ((titan_readl(UARBR) & 0xff) == 0x3)))
			set_async_breakpoint(&regs->cp0_epc);
}
