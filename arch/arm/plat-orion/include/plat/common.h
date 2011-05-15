/*
 * arch/arm/plat-orion/include/plat/common.h
 *
 * Marvell Orion SoC common setup code used by different mach-/common.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_COMMON_H


void __init orion_uart0_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk);

void __init orion_uart1_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk);

void __init orion_uart2_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk);

void __init orion_uart3_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk);
#endif
