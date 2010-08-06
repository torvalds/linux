/*
 * arch/arm/mach-kirkwood/include/mach/irqs.h
 *
 * IRQ definitions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

/*
 * Low Interrupt Controller
 */
#define IRQ_KIRKWOOD_HIGH_SUM	0
#define IRQ_KIRKWOOD_BRIDGE	1
#define IRQ_KIRKWOOD_HOST2CPU	2
#define IRQ_KIRKWOOD_CPU2HOST	3
#define IRQ_KIRKWOOD_XOR_00	5
#define IRQ_KIRKWOOD_XOR_01	6
#define IRQ_KIRKWOOD_XOR_10	7
#define IRQ_KIRKWOOD_XOR_11	8
#define IRQ_KIRKWOOD_PCIE	9
#define IRQ_KIRKWOOD_PCIE1	10
#define IRQ_KIRKWOOD_GE00_SUM	11
#define IRQ_KIRKWOOD_GE01_SUM	15
#define IRQ_KIRKWOOD_USB	19
#define IRQ_KIRKWOOD_SATA	21
#define IRQ_KIRKWOOD_CRYPTO	22
#define IRQ_KIRKWOOD_SPI	23
#define IRQ_KIRKWOOD_I2S	24
#define IRQ_KIRKWOOD_TS_0	26
#define IRQ_KIRKWOOD_SDIO	28
#define IRQ_KIRKWOOD_TWSI	29
#define IRQ_KIRKWOOD_AVB	30
#define IRQ_KIRKWOOD_TDMI	31

/*
 * High Interrupt Controller
 */
#define IRQ_KIRKWOOD_UART_0	33
#define IRQ_KIRKWOOD_UART_1	34
#define IRQ_KIRKWOOD_GPIO_LOW_0_7	35
#define IRQ_KIRKWOOD_GPIO_LOW_8_15	36
#define IRQ_KIRKWOOD_GPIO_LOW_16_23	37
#define IRQ_KIRKWOOD_GPIO_LOW_24_31	38
#define IRQ_KIRKWOOD_GPIO_HIGH_0_7	39
#define IRQ_KIRKWOOD_GPIO_HIGH_8_15	40
#define IRQ_KIRKWOOD_GPIO_HIGH_16_23	41
#define IRQ_KIRKWOOD_GE00_ERR	46
#define IRQ_KIRKWOOD_GE01_ERR	47

/*
 * KIRKWOOD General Purpose Pins
 */
#define IRQ_KIRKWOOD_GPIO_START	64
#define NR_GPIO_IRQS		50

#define NR_IRQS			(IRQ_KIRKWOOD_GPIO_START + NR_GPIO_IRQS)


#endif
