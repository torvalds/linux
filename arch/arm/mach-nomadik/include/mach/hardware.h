/*
 * This file contains the hardware definitions of the Nomadik.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * YOU should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/* Nomadik registers live from 0x1000.0000 to 0x1023.0000 -- currently */
#define NOMADIK_IO_VIRTUAL	0xF0000000	/* VA of IO  */
#define NOMADIK_IO_PHYSICAL	0x10000000	/* PA of IO */
#define NOMADIK_IO_SIZE		0x00300000	/* 3MB for all regs */

/* used in C code, so cast to proper type */
#define io_p2v(x) ((void __iomem *)(x) \
			- NOMADIK_IO_PHYSICAL + NOMADIK_IO_VIRTUAL)
#define io_v2p(x) ((unsigned long)(x) \
			- NOMADIK_IO_VIRTUAL + NOMADIK_IO_PHYSICAL)

/* used in asm code, so no casts */
#define IO_ADDRESS(x) ((x) - NOMADIK_IO_PHYSICAL + NOMADIK_IO_VIRTUAL)

/*
 *   Base address defination for Nomadik Onchip Logic Block
 */
#define NOMADIK_FSMC_BASE	0x10100000	/* FSMC registers */
#define NOMADIK_SDRAMC_BASE	0x10110000	/* SDRAM Controller */
#define NOMADIK_CLCDC_BASE	0x10120000	/* CLCD Controller */
#define NOMADIK_MDIF_BASE	0x10120000	/* MDIF */
#define NOMADIK_DMA0_BASE	0x10130000	/* DMA0 Controller */
#define NOMADIK_IC_BASE		0x10140000	/* Vectored Irq Controller */
#define NOMADIK_DMA1_BASE	0x10150000	/* DMA1 Controller */
#define NOMADIK_USB_BASE	0x10170000	/* USB-OTG conf reg base */
#define NOMADIK_CRYP_BASE	0x10180000	/* Crypto processor */
#define NOMADIK_SHA1_BASE	0x10190000	/* SHA-1 Processor */
#define NOMADIK_XTI_BASE	0x101A0000	/* XTI */
#define NOMADIK_RNG_BASE	0x101B0000	/* Random number generator */
#define NOMADIK_SRC_BASE	0x101E0000	/* SRC base */
#define NOMADIK_WDOG_BASE	0x101E1000	/* Watchdog */
#define NOMADIK_MTU0_BASE	0x101E2000	/* Multiple Timer 0 */
#define NOMADIK_MTU1_BASE	0x101E3000	/* Multiple Timer 1 */
#define NOMADIK_GPIO0_BASE	0x101E4000	/* GPIO0 */
#define NOMADIK_GPIO1_BASE	0x101E5000	/* GPIO1 */
#define NOMADIK_GPIO2_BASE	0x101E6000	/* GPIO2 */
#define NOMADIK_GPIO3_BASE	0x101E7000	/* GPIO3 */
#define NOMADIK_RTC_BASE	0x101E8000	/* Real Time Clock base */
#define NOMADIK_PMU_BASE	0x101E9000	/* Power Management Unit */
#define NOMADIK_OWM_BASE	0x101EA000	/* One wire master */
#define NOMADIK_SCR_BASE	0x101EF000	/* Secure Control registers */
#define NOMADIK_MSP2_BASE	0x101F0000	/* MSP 2 interface */
#define NOMADIK_MSP1_BASE	0x101F1000	/* MSP 1 interface */
#define NOMADIK_UART2_BASE	0x101F2000	/* UART 2 interface */
#define NOMADIK_SSIRx_BASE	0x101F3000	/* SSI 8-ch rx interface */
#define NOMADIK_SSITx_BASE	0x101F4000	/* SSI 8-ch tx interface */
#define NOMADIK_MSHC_BASE	0x101F5000	/* Memory Stick(Pro) Host */
#define NOMADIK_SDI_BASE	0x101F6000	/* SD-card/MM-Card */
#define NOMADIK_I2C1_BASE	0x101F7000	/* I2C1 interface */
#define NOMADIK_I2C0_BASE	0x101F8000	/* I2C0 interface */
#define NOMADIK_MSP0_BASE	0x101F9000	/* MSP 0 interface  */
#define NOMADIK_FIRDA_BASE	0x101FA000	/* FIrDA interface  */
#define NOMADIK_UART1_BASE	0x101FB000	/* UART 1 interface */
#define NOMADIK_SSP_BASE	0x101FC000	/* SSP interface  */
#define NOMADIK_UART0_BASE	0x101FD000	/* UART 0 interface */
#define NOMADIK_SGA_BASE	0x101FE000	/* SGA interface */
#define NOMADIK_L2CC_BASE	0x10210000	/* L2 Cache controller */

/* Other ranges, not for p2v/v2p */
#define NOMADIK_BACKUP_RAM	0x80010000
#define NOMADIK_EBROM		0x80000000	/* Embedded boot ROM */
#define NOMADIK_HAMACV_DMEM_BASE 0xA0100000	/* HAMACV Data Memory Start */
#define NOMADIK_HAMACV_DMEM_END	0xA01FFFFF	/* HAMACV Data Memory End */
#define NOMADIK_HAMACA_DMEM	0xA0200000	/* HAMACA Data Memory Space */

#define NOMADIK_FSMC_VA		IO_ADDRESS(NOMADIK_FSMC_BASE)
#define NOMADIK_MTU0_VA		IO_ADDRESS(NOMADIK_MTU0_BASE)
#define NOMADIK_MTU1_VA		IO_ADDRESS(NOMADIK_MTU1_BASE)

#endif /* __ASM_ARCH_HARDWARE_H */
