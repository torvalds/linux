/* arch/arm/mach-zynq/include/mach/uncompress.h
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_UNCOMPRESS_H__
#define __MACH_UNCOMPRESS_H__

#include <linux/io.h>
#include <asm/processor.h>
#include <mach/zynq_soc.h>
#include <mach/uart.h>

void arch_decomp_setup(void)
{
}

static inline void flush(void)
{
	/*
	 * Wait while the FIFO is not empty
	 */
	while (!(__raw_readl(IOMEM(LL_UART_PADDR + UART_SR_OFFSET)) &
		UART_SR_TXEMPTY))
		cpu_relax();
}

#define arch_decomp_wdog()

static void putc(char ch)
{
	/*
	 * Wait for room in the FIFO, then write the char into the FIFO
	 */
	while (__raw_readl(IOMEM(LL_UART_PADDR + UART_SR_OFFSET)) &
		UART_SR_TXFULL)
		cpu_relax();

	__raw_writel(ch, IOMEM(LL_UART_PADDR + UART_FIFO_OFFSET));
}

#endif
