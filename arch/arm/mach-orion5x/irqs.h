/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IRQ definitions for Orion SoC
 *
 *  Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

/*
 * Orion Main Interrupt Controller
 */
#define IRQ_ORION5X_BRIDGE		(1 + 0)
#define IRQ_ORION5X_DOORBELL_H2C	(1 + 1)
#define IRQ_ORION5X_DOORBELL_C2H	(1 + 2)
#define IRQ_ORION5X_UART0		(1 + 3)
#define IRQ_ORION5X_UART1		(1 + 4)
#define IRQ_ORION5X_I2C			(1 + 5)
#define IRQ_ORION5X_GPIO_0_7		(1 + 6)
#define IRQ_ORION5X_GPIO_8_15		(1 + 7)
#define IRQ_ORION5X_GPIO_16_23		(1 + 8)
#define IRQ_ORION5X_GPIO_24_31		(1 + 9)
#define IRQ_ORION5X_PCIE0_ERR		(1 + 10)
#define IRQ_ORION5X_PCIE0_INT		(1 + 11)
#define IRQ_ORION5X_USB1_CTRL		(1 + 12)
#define IRQ_ORION5X_DEV_BUS_ERR		(1 + 14)
#define IRQ_ORION5X_PCI_ERR		(1 + 15)
#define IRQ_ORION5X_USB_BR_ERR		(1 + 16)
#define IRQ_ORION5X_USB0_CTRL		(1 + 17)
#define IRQ_ORION5X_ETH_RX		(1 + 18)
#define IRQ_ORION5X_ETH_TX		(1 + 19)
#define IRQ_ORION5X_ETH_MISC		(1 + 20)
#define IRQ_ORION5X_ETH_SUM		(1 + 21)
#define IRQ_ORION5X_ETH_ERR		(1 + 22)
#define IRQ_ORION5X_IDMA_ERR		(1 + 23)
#define IRQ_ORION5X_IDMA_0		(1 + 24)
#define IRQ_ORION5X_IDMA_1		(1 + 25)
#define IRQ_ORION5X_IDMA_2		(1 + 26)
#define IRQ_ORION5X_IDMA_3		(1 + 27)
#define IRQ_ORION5X_CESA		(1 + 28)
#define IRQ_ORION5X_SATA		(1 + 29)
#define IRQ_ORION5X_XOR0		(1 + 30)
#define IRQ_ORION5X_XOR1		(1 + 31)

/*
 * Orion General Purpose Pins
 */
#define IRQ_ORION5X_GPIO_START	33
#define NR_GPIO_IRQS		32

#define ORION5X_NR_IRQS		(IRQ_ORION5X_GPIO_START + NR_GPIO_IRQS)


#endif
