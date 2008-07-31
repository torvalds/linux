/*
 * Pin definitions for AT32AP7000.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_AT32AP700X_H__
#define __ASM_ARCH_AT32AP700X_H__

#define GPIO_PERIPH_A	0
#define GPIO_PERIPH_B	1

/*
 * Pin numbers identifying specific GPIO pins on the chip. They can
 * also be converted to IRQ numbers by passing them through
 * gpio_to_irq().
 */
#define GPIO_PIOA_BASE	(0)
#define GPIO_PIOB_BASE	(GPIO_PIOA_BASE + 32)
#define GPIO_PIOC_BASE	(GPIO_PIOB_BASE + 32)
#define GPIO_PIOD_BASE	(GPIO_PIOC_BASE + 32)
#define GPIO_PIOE_BASE	(GPIO_PIOD_BASE + 32)

#define GPIO_PIN_PA(N)	(GPIO_PIOA_BASE + (N))
#define GPIO_PIN_PB(N)	(GPIO_PIOB_BASE + (N))
#define GPIO_PIN_PC(N)	(GPIO_PIOC_BASE + (N))
#define GPIO_PIN_PD(N)	(GPIO_PIOD_BASE + (N))
#define GPIO_PIN_PE(N)	(GPIO_PIOE_BASE + (N))


/*
 * DMAC peripheral hardware handshaking interfaces, used with dw_dmac
 */
#define DMAC_MCI_RX		0
#define DMAC_MCI_TX		1
#define DMAC_DAC_TX		2
#define DMAC_AC97_A_RX		3
#define DMAC_AC97_A_TX		4
#define DMAC_AC97_B_RX		5
#define DMAC_AC97_B_TX		6
#define DMAC_DMAREQ_0		7
#define DMAC_DMAREQ_1		8
#define DMAC_DMAREQ_2		9
#define DMAC_DMAREQ_3		10

/* HSB master IDs */
#define HMATRIX_MASTER_CPU_DCACHE		0
#define HMATRIX_MASTER_CPU_ICACHE		1
#define HMATRIX_MASTER_PDC			2
#define HMATRIX_MASTER_ISI			3
#define HMATRIX_MASTER_USBA			4
#define HMATRIX_MASTER_LCDC			5
#define HMATRIX_MASTER_MACB0			6
#define HMATRIX_MASTER_MACB1			7
#define HMATRIX_MASTER_DMACA_M0			8
#define HMATRIX_MASTER_DMACA_M1			9

/* HSB slave IDs */
#define HMATRIX_SLAVE_SRAM0			0
#define HMATRIX_SLAVE_SRAM1			1
#define HMATRIX_SLAVE_PBA			2
#define HMATRIX_SLAVE_PBB			3
#define HMATRIX_SLAVE_EBI			4
#define HMATRIX_SLAVE_USBA			5
#define HMATRIX_SLAVE_LCDC			6
#define HMATRIX_SLAVE_DMACA			7

/* Bits in HMATRIX SFR4 (EBI) */
#define HMATRIX_EBI_SDRAM_ENABLE		(1 << 1)
#define HMATRIX_EBI_NAND_ENABLE			(1 << 3)
#define HMATRIX_EBI_CF0_ENABLE			(1 << 4)
#define HMATRIX_EBI_CF1_ENABLE			(1 << 5)
#define HMATRIX_EBI_PULLUP_DISABLE		(1 << 8)

/*
 * Base addresses of controllers that may be accessed early by
 * platform code.
 */
#define PM_BASE		0xfff00000
#define HMATRIX_BASE	0xfff00800
#define SDRAMC_BASE	0xfff03800

#endif /* __ASM_ARCH_AT32AP700X_H__ */
