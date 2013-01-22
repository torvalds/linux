#ifndef __ASM_RC32434_IRQ_H
#define __ASM_RC32434_IRQ_H

#define NR_IRQS 256

#include <asm/mach-generic/irq.h>
#include <asm/mach-rc32434/rb.h>

/* Interrupt Controller */
#define IC_GROUP0_PEND		(REGBASE + 0x38000)
#define IC_GROUP0_MASK		(REGBASE + 0x38008)
#define IC_GROUP_OFFSET		0x0C

#define NUM_INTR_GROUPS		5

/* 16550 UARTs */
#define GROUP0_IRQ_BASE		8	/* GRP2 IRQ numbers start here */
					/* GRP3 IRQ numbers start here */
#define GROUP1_IRQ_BASE		(GROUP0_IRQ_BASE + 32)
					/* GRP4 IRQ numbers start here */
#define GROUP2_IRQ_BASE		(GROUP1_IRQ_BASE + 32)
					/* GRP5 IRQ numbers start here */
#define GROUP3_IRQ_BASE		(GROUP2_IRQ_BASE + 32)
#define GROUP4_IRQ_BASE		(GROUP3_IRQ_BASE + 32)

#define UART0_IRQ		(GROUP3_IRQ_BASE + 0)

#define ETH0_DMA_RX_IRQ		(GROUP1_IRQ_BASE + 0)
#define ETH0_DMA_TX_IRQ		(GROUP1_IRQ_BASE + 1)
#define ETH0_RX_OVR_IRQ		(GROUP3_IRQ_BASE + 9)
#define ETH0_TX_UND_IRQ		(GROUP3_IRQ_BASE + 10)

#define GPIO_MAPPED_IRQ_BASE	GROUP4_IRQ_BASE
#define GPIO_MAPPED_IRQ_GROUP	4

#endif	/* __ASM_RC32434_IRQ_H */
