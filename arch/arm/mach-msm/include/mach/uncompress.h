/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_MSM_UNCOMPRESS_H
#define __ASM_ARCH_MSM_UNCOMPRESS_H

#include <asm/barrier.h>
#include <asm/processor.h>
#include <mach/msm_iomap.h>

#define UART_CSR      (*(volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x08))
#define UART_TF       (*(volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x0c))

#define UART_DM_SR    (*((volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x08)))
#define UART_DM_CR    (*((volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x10)))
#define UART_DM_ISR   (*((volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x14)))
#define UART_DM_NCHAR (*((volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x40)))
#define UART_DM_TF    (*((volatile uint32_t *)(MSM_DEBUG_UART_PHYS + 0x70)))

static void putc(int c)
{
#if defined(MSM_DEBUG_UART_PHYS)
#ifdef CONFIG_MSM_HAS_DEBUG_UART_HS
	/*
	 * Wait for TX_READY to be set; but skip it if we have a
	 * TX underrun.
	 */
	if (!(UART_DM_SR & 0x08))
		while (!(UART_DM_ISR & 0x80))
			cpu_relax();

	UART_DM_CR = 0x300;
	UART_DM_NCHAR = 0x1;
	UART_DM_TF = c;
#else
	while (!(UART_CSR & 0x04))
		cpu_relax();
	UART_TF = c;
#endif
#endif
}

static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
}

#endif
