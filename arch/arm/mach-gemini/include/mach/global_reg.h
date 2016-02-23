/*
 *  This file contains the hardware definitions for Gemini.
 *
 *  Copyright (C) 2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MACH_GLOBAL_REG_H
#define __MACH_GLOBAL_REG_H

/* Global Word ID Register*/
#define GLOBAL_ID			0x00

#define CHIP_ID(reg)			((reg) >> 8)
#define CHIP_REVISION(reg)		((reg) & 0xFF)

/* Global Status Register */
#define GLOBAL_STATUS			0x04

#define CPU_BIG_ENDIAN			(1 << 31)
#define PLL_OSC_30M			(1 << 30)	/* else 60MHz */

#define OPERATION_MODE_MASK		(0xF << 26)
#define OPM_IDDQ			(0xF << 26)
#define OPM_NAND			(0xE << 26)
#define OPM_RING			(0xD << 26)
#define OPM_DIRECT_BOOT			(0xC << 26)
#define OPM_USB1_PHY_TEST		(0xB << 26)
#define OPM_USB0_PHY_TEST		(0xA << 26)
#define OPM_SATA1_PHY_TEST		(0x9 << 26)
#define OPM_SATA0_PHY_TEST		(0x8 << 26)
#define OPM_ICE_ARM			(0x7 << 26)
#define OPM_ICE_FARADAY			(0x6 << 26)
#define OPM_PLL_BYPASS			(0x5 << 26)
#define OPM_DEBUG			(0x4 << 26)
#define OPM_BURN_IN			(0x3 << 26)
#define OPM_MBIST			(0x2 << 26)
#define OPM_SCAN			(0x1 << 26)
#define OPM_REAL			(0x0 << 26)

#define FLASH_TYPE_MASK			(0x3 << 24)
#define FLASH_TYPE_NAND_2K		(0x3 << 24)
#define FLASH_TYPE_NAND_512		(0x2 << 24)
#define FLASH_TYPE_PARALLEL		(0x1 << 24)
#define FLASH_TYPE_SERIAL		(0x0 << 24)
/* if parallel */
#define FLASH_WIDTH_16BIT		(1 << 23)	/* else 8 bit */
/* if serial */
#define FLASH_ATMEL			(1 << 23)	/* else STM */

#define FLASH_SIZE_MASK			(0x3 << 21)
#define NAND_256M			(0x3 << 21)	/* and more */
#define NAND_128M			(0x2 << 21)
#define NAND_64M			(0x1 << 21)
#define NAND_32M			(0x0 << 21)
#define ATMEL_16M			(0x3 << 21)	/* and more */
#define ATMEL_8M			(0x2 << 21)
#define ATMEL_4M_2M			(0x1 << 21)
#define ATMEL_1M			(0x0 << 21)	/* and less */
#define STM_32M				(1 << 22)	/* and more */
#define STM_16M				(0 << 22)	/* and less */

#define FLASH_PARALLEL_HIGH_PIN_CNT	(1 << 20)	/* else low pin cnt */

#define CPU_AHB_RATIO_MASK		(0x3 << 18)
#define CPU_AHB_1_1			(0x0 << 18)
#define CPU_AHB_3_2			(0x1 << 18)
#define CPU_AHB_24_13			(0x2 << 18)
#define CPU_AHB_2_1			(0x3 << 18)

#define REG_TO_AHB_SPEED(reg)		((((reg) >> 15) & 0x7) * 10 + 130)
#define AHB_SPEED_TO_REG(x)		((((x - 130)) / 10) << 15)

/* it is posible to override some settings, use >> OVERRIDE_xxxx_SHIFT */
#define OVERRIDE_FLASH_TYPE_SHIFT	16
#define OVERRIDE_FLASH_WIDTH_SHIFT	16
#define OVERRIDE_FLASH_SIZE_SHIFT	16
#define OVERRIDE_CPU_AHB_RATIO_SHIFT	15
#define OVERRIDE_AHB_SPEED_SHIFT	15

/* Global PLL Control Register */
#define GLOBAL_PLL_CTRL			0x08

#define PLL_BYPASS			(1 << 31)
#define PLL_POWER_DOWN			(1 << 8)
#define PLL_CONTROL_Q			(0x1F << 0)

/* Global Soft Reset Control Register */
#define GLOBAL_RESET			0x0C

#define RESET_GLOBAL			(1 << 31)
#define RESET_CPU1			(1 << 30)
#define RESET_TVE			(1 << 28)
#define RESET_SATA1			(1 << 27)
#define RESET_SATA0			(1 << 26)
#define RESET_CIR			(1 << 25)
#define RESET_EXT_DEV			(1 << 24)
#define RESET_WD			(1 << 23)
#define RESET_GPIO2			(1 << 22)
#define RESET_GPIO1			(1 << 21)
#define RESET_GPIO0			(1 << 20)
#define RESET_SSP			(1 << 19)
#define RESET_UART			(1 << 18)
#define RESET_TIMER			(1 << 17)
#define RESET_RTC			(1 << 16)
#define RESET_INT1			(1 << 15)
#define RESET_INT0			(1 << 14)
#define RESET_LCD			(1 << 13)
#define RESET_LPC			(1 << 12)
#define RESET_APB			(1 << 11)
#define RESET_DMA			(1 << 10)
#define RESET_USB1			(1 << 9)
#define RESET_USB0			(1 << 8)
#define RESET_PCI			(1 << 7)
#define RESET_GMAC1			(1 << 6)
#define RESET_GMAC0			(1 << 5)
#define RESET_SECURITY			(1 << 4)
#define RESET_RAID			(1 << 3)
#define RESET_IDE			(1 << 2)
#define RESET_FLASH			(1 << 1)
#define RESET_DRAM			(1 << 0)

/* Global IO Pad Driving Capability Control Register */
#define GLOBAL_IO_DRIVING_CTRL		0x10

#define DRIVING_CURRENT_MASK		0x3

/* here 00-4mA, 01-8mA, 10-12mA, 11-16mA */
#define GPIO1_PADS_31_28_SHIFT		28
#define GPIO0_PADS_31_16_SHIFT		26
#define GPIO0_PADS_15_0_SHIFT		24
#define PCI_AND_EXT_RESET_PADS_SHIFT	22
#define IDE_PADS_SHIFT			20
#define GMAC1_PADS_SHIFT		18
#define GMAC0_PADS_SHIFT		16
/* DRAM is not in mA and poorly documented */
#define DRAM_CLOCK_PADS_SHIFT		8
#define DRAM_DATA_PADS_SHIFT		4
#define DRAM_CONTROL_PADS_SHIFT		0

/* Global IO Pad Slew Rate Control Register */
#define GLOBAL_IO_SLEW_RATE_CTRL	0x14

#define GPIO1_PADS_31_28_SLOW		(1 << 10)
#define GPIO0_PADS_31_16_SLOW		(1 << 9)
#define GPIO0_PADS_15_0_SLOW		(1 << 8)
#define PCI_PADS_SLOW			(1 << 7)
#define IDE_PADS_SLOW			(1 << 6)
#define GMAC1_PADS_SLOW			(1 << 5)
#define GMAC0_PADS_SLOW			(1 << 4)
#define DRAM_CLOCK_PADS_SLOW		(1 << 1)
#define DRAM_IO_PADS_SLOW		(1 << 0)

/*
 * General skew control defines
 * 16 steps, each step is around 0.2ns
 */
#define SKEW_MASK			0xF

/* Global IDE PAD Skew Control Register */
#define GLOBAL_IDE_SKEW_CTRL		0x18

#define IDE1_HOST_STROBE_DELAY_SHIFT	28
#define IDE1_DEVICE_STROBE_DELAY_SHIFT	24
#define IDE1_OUTPUT_IO_SKEW_SHIFT	20
#define IDE1_INPUT_IO_SKEW_SHIFT	16
#define IDE0_HOST_STROBE_DELAY_SHIFT	12
#define IDE0_DEVICE_STROBE_DELAY_SHIFT	8
#define IDE0_OUTPUT_IO_SKEW_SHIFT	4
#define IDE0_INPUT_IO_SKEW_SHIFT	0

/* Global GMAC Control Pad Skew Control Register */
#define GLOBAL_GMAC_CTRL_SKEW_CTRL	0x1C

#define GMAC1_TXC_SKEW_SHIFT		28
#define GMAC1_TXEN_SKEW_SHIFT		24
#define GMAC1_RXC_SKEW_SHIFT		20
#define GMAC1_RXDV_SKEW_SHIFT		16
#define GMAC0_TXC_SKEW_SHIFT		12
#define GMAC0_TXEN_SKEW_SHIFT		8
#define GMAC0_RXC_SKEW_SHIFT		4
#define GMAC0_RXDV_SKEW_SHIFT		0

/* Global GMAC0 Data PAD Skew Control Register */
#define GLOBAL_GMAC0_DATA_SKEW_CTRL	0x20
/* Global GMAC1 Data PAD Skew Control Register */
#define GLOBAL_GMAC1_DATA_SKEW_CTRL	0x24

#define GMAC_TXD_SKEW_SHIFT(x)		(((x) * 4) + 16)
#define GMAC_RXD_SKEW_SHIFT(x)		((x) * 4)

/* CPU has two AHB busses. */

/* Global Arbitration0 Control Register */
#define GLOBAL_ARBITRATION0_CTRL	0x28

#define BOOT_CONTROLLER_HIGH_PRIO	(1 << 3)
#define DMA_BUS1_HIGH_PRIO		(1 << 2)
#define CPU0_HIGH_PRIO			(1 << 0)

/* Global Arbitration1 Control Register */
#define GLOBAL_ARBITRATION1_CTRL	0x2C

#define TVE_HIGH_PRIO			(1 << 9)
#define PCI_HIGH_PRIO			(1 << 8)
#define USB1_HIGH_PRIO			(1 << 7)
#define USB0_HIGH_PRIO			(1 << 6)
#define GMAC1_HIGH_PRIO			(1 << 5)
#define GMAC0_HIGH_PRIO			(1 << 4)
#define SECURITY_HIGH_PRIO		(1 << 3)
#define RAID_HIGH_PRIO			(1 << 2)
#define IDE_HIGH_PRIO			(1 << 1)
#define DMA_BUS2_HIGH_PRIO		(1 << 0)

/* Common bits for both arbitration registers */
#define BURST_LENGTH_SHIFT		16
#define BURST_LENGTH_MASK		(0x3F << 16)

/* Miscellaneous Control Register */
#define GLOBAL_MISC_CTRL		0x30

#define MEMORY_SPACE_SWAP		(1 << 31)
#define USB1_PLUG_MINIB			(1 << 30) /* else plug is mini-A */
#define USB0_PLUG_MINIB			(1 << 29)
#define GMAC_GMII			(1 << 28)
#define GMAC_1_ENABLE			(1 << 27)
/* TODO: define ATA/SATA bits */
#define USB1_VBUS_ON			(1 << 23)
#define USB0_VBUS_ON			(1 << 22)
#define APB_CLKOUT_ENABLE		(1 << 21)
#define TVC_CLKOUT_ENABLE		(1 << 20)
#define EXT_CLKIN_ENABLE		(1 << 19)
#define PCI_66MHZ			(1 << 18) /* else 33 MHz */
#define PCI_CLKOUT_ENABLE		(1 << 17)
#define LPC_CLKOUT_ENABLE		(1 << 16)
#define USB1_WAKEUP_ON			(1 << 15)
#define USB0_WAKEUP_ON			(1 << 14)
/* TODO: define PCI idle detect bits */
#define TVC_PADS_ENABLE			(1 << 9)
#define SSP_PADS_ENABLE			(1 << 8)
#define LCD_PADS_ENABLE			(1 << 7)
#define LPC_PADS_ENABLE			(1 << 6)
#define PCI_PADS_ENABLE			(1 << 5)
#define IDE_PADS_ENABLE			(1 << 4)
#define DRAM_PADS_POWER_DOWN		(1 << 3)
#define NAND_PADS_DISABLE		(1 << 2)
#define PFLASH_PADS_DISABLE		(1 << 1)
#define SFLASH_PADS_DISABLE		(1 << 0)

/* Global Clock Control Register */
#define GLOBAL_CLOCK_CTRL		0x34

#define POWER_STATE_G0			(1 << 31)
#define POWER_STATE_S1			(1 << 30) /* else it is S3/S4 state */
#define SECURITY_APB_AHB		(1 << 29)
/* else Security APB clk will be 0.75xAHB */
/* TODO: TVC clock divider */
#define PCI_CLKRUN_ENABLE		(1 << 16)
#define BOOT_CLK_DISABLE		(1 << 13)
#define TVC_CLK_DISABLE			(1 << 12)
#define FLASH_CLK_DISABLE		(1 << 11)
#define DDR_CLK_DISABLE			(1 << 10)
#define PCI_CLK_DISABLE			(1 << 9)
#define IDE_CLK_DISABLE			(1 << 8)
#define USB1_CLK_DISABLE		(1 << 7)
#define USB0_CLK_DISABLE		(1 << 6)
#define SATA1_CLK_DISABLE		(1 << 5)
#define SATA0_CLK_DISABLE		(1 << 4)
#define GMAC1_CLK_DISABLE		(1 << 3)
#define GMAC0_CLK_DISABLE		(1 << 2)
#define SECURITY_CLK_DISABLE		(1 << 1)

/* TODO: other registers definitions if needed */

#endif /* __MACH_GLOBAL_REG_H */
