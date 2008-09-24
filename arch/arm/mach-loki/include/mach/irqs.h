/*
 * arch/arm/mach-loki/include/mach/irqs.h
 *
 * IRQ definitions for Marvell Loki (88RC8480) SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#include "loki.h"	/* need GPIO_MAX */

/*
 * Interrupt Controller
 */
#define IRQ_LOKI_PCIE_A_CPU_DRBL	0
#define IRQ_LOKI_CPU_PCIE_A_DRBL	1
#define IRQ_LOKI_PCIE_B_CPU_DRBL	2
#define IRQ_LOKI_CPU_PCIE_B_DRBL	3
#define IRQ_LOKI_COM_A_ERR		6
#define IRQ_LOKI_COM_A_IN		7
#define IRQ_LOKI_COM_A_OUT		8
#define IRQ_LOKI_COM_B_ERR		9
#define IRQ_LOKI_COM_B_IN		10
#define IRQ_LOKI_COM_B_OUT		11
#define IRQ_LOKI_DMA_A			12
#define IRQ_LOKI_DMA_B			13
#define IRQ_LOKI_SAS_A			14
#define IRQ_LOKI_SAS_B			15
#define IRQ_LOKI_DDR			16
#define IRQ_LOKI_XOR			17
#define IRQ_LOKI_BRIDGE			18
#define IRQ_LOKI_PCIE_A_ERR		20
#define IRQ_LOKI_PCIE_A_INT		21
#define IRQ_LOKI_PCIE_B_ERR		22
#define IRQ_LOKI_PCIE_B_INT		23
#define IRQ_LOKI_GBE_A_INT		24
#define IRQ_LOKI_GBE_B_INT		25
#define IRQ_LOKI_DEV_ERR		26
#define IRQ_LOKI_UART0			27
#define IRQ_LOKI_UART1			28
#define IRQ_LOKI_TWSI			29
#define IRQ_LOKI_GPIO_23_0		30
#define IRQ_LOKI_GPIO_25_24		31

/*
 * Loki General Purpose Pins
 */
#define IRQ_LOKI_GPIO_START	32
#define NR_GPIO_IRQS		GPIO_MAX

#define NR_IRQS			(IRQ_LOKI_GPIO_START + NR_GPIO_IRQS)


#endif
