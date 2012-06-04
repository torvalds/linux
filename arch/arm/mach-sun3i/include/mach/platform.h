/*
 * arch/arm/mach-sun3i/include/mach/platform.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __address_h
#define __address_h                     1

/*
 * Memory definitions
 */

/* Physical Address */
#define SW_PA_BROM_START             0xffff0000
#define SW_PA_BROM_END               0xffff7fff   /* 32KB */

#define SW_PA_SRAM_BASE              0x00000000	/*16KB*/

#define SW_PA_SDRAM_START            0x80000000
#define SW_PA_SDRAM_END              0xbfffffff   /* 1GB */

#define SW_PA_IO_BASE                0x01c00000
#define SW_PA_SRAM_IO_BASE           0x01c00000   /* 4KB */
#define SW_PA_DRAM_IO_BASE           0x01c01000
#define SW_PA_DMAC_IO_BASE           0x01c02000
#define SW_PA_NANDFLASHC_IO_BASE     0x01c03000
#define SW_PA_TSI_IO_BASE            0x01c04000
#define SW_PA_SPI0_IO_BASE           0x01c05000
#define SW_PA_SPI1_IO_BASE           0x01c06000
#define SW_PA_MSCC_IO_BASE           0x01c07000
#define SW_PA_CSI0_IO_BASE           0x01c09000
#define SW_PA_TVE_IO_BASE            0x01c0a000
#define SW_PA_EMAC_IO_BASE           0x01c0b000
#define SW_PA_TCON0_IO_BASE          0x01c0c000
#define SW_PA_VE_IO_BASE             0x01c0e000
#define SW_PA_SDC0_IO_BASE           0x01c0f000
#define SW_PA_SDC1_IO_BASE           0x01c10000
#define SW_PA_SDC2_IO_BASE           0x01c11000
#define SW_PA_SDC3_IO_BASE           0x01c12000
#define SW_PA_USB0_IO_BASE           0x01c13000
#define SW_PA_USB1_IO_BASE           0x01c14000
#define SW_PA_SSE_IO_BASE            0x01c15000
#define SW_PA_ATA_IO_BASE            0x01c16000
#define SW_PA_CCM_IO_BASE		0x01c20000
#define SW_PA_INT_IO_BASE            0x01c20400
#define SW_PA_PORTC_IO_BASE          0x01c20800
#define SW_PA_TIMERC_IO_BASE         0x01c20c00
#define SW_PA_UART0_IO_BASE          0x01c21000
#define SW_PA_UART1_IO_BASE          0x01c21400
#define SW_PA_UART2_IO_BASE          0x01c21800
#define SW_PA_UART3_IO_BASE          0x01c21c00
#define SW_PA_TWI0_IO_BASE		0x01c24000
#define SW_PA_TWI1_IO_BASE		0x01c24400
#define SW_PA_TW2_IO_BASE		0x01c26c00
#define SW_PA_UART4_IO_BASE          0x01c25000
#define SW_PA_UART5_IO_BASE          0x01c25400
#define SW_PA_UART6_IO_BASE          0x01c25800
#define SW_PA_UART7_IO_BASE          0x01c25c00


/* Virtual Address */
#define SW_VA_SRAM_BASE              0xf0000000	/*16KB*/

#define SW_VA_IO_BASE                0xf1c00000
#define SW_VA_SRAM_IO_BASE           0xf1c00000   /* 4KB */
#define SW_VA_DRAM_IO_BASE           0xf1c01000
#define SW_VA_DMAC_IO_BASE           0xf1c02000
#define SW_VA_NANDFLASHC_IO_BASE     0xf1c03000
#define SW_VA_TSI_IO_BASE            0xf1c04000
#define SW_VA_SPI0_IO_BASE           0xf1c05000
#define SW_VA_SPI1_IO_BASE           0xf1c06000
#define SW_VA_MSCC_IO_BASE           0xf1c07000
#define SW_VA_CSI0_IO_BASE           0xf1c09000
#define SW_VA_TVE_IO_BASE            0xf1c0a000
#define SW_VA_EMAC_IO_BASE           0xf1c0b000
#define SW_VA_TCON0_IO_BASE          0xf1c0c000
#define SW_VA_VE_IO_BASE             0xf1c0e000
#define SW_VA_SDC0_IO_BASE           0xf1c0f000
#define SW_VA_SDC1_IO_BASE           0xf1c10000
#define SW_VA_SDC2_IO_BASE           0xf1c11000
#define SW_VA_SDC3_IO_BASE           0xf1c12000
#define SW_VA_USB0_IO_BASE           0xf1c13000
#define SW_VA_USB1_IO_BASE           0xf1c14000
#define SW_VA_SSE_IO_BASE            0xf1c15000
#define SW_VA_ATA_IO_BASE            0xf1c16000
#define SW_VA_CCM_IO_BASE		0xf1c20000
#define SW_VA_INT_IO_BASE            0xf1c20400
#define SW_VA_PORTC_IO_BASE          0xf1c20800
#define SW_VA_TIMERC_IO_BASE         0xf1c20c00
#define SW_VA_UART0_IO_BASE          0xf1c21000
#define SW_VA_UART1_IO_BASE          0xf1c21400
#define SW_VA_UART2_IO_BASE          0xf1c21800
#define SW_VA_UART3_IO_BASE          0xf1c21c00
#define SW_VA_TWI0_IO_BASE		0xf1c24000
#define SW_VA_TWI1_IO_BASE		0xf1c24400
#define SW_VA_TWI2_IO_BASE		0xf1c26c00
#define SW_VA_UART4_IO_BASE          0xf1c25000
#define SW_VA_UART5_IO_BASE          0xf1c25400
#define SW_VA_UART6_IO_BASE          0xf1c25800
#define SW_VA_UART7_IO_BASE          0xf1c25c00


/**
 * Timer registers addr
 *
 */

#define SW_TIMER_INT_CTL_REG         (SW_VA_TIMERC_IO_BASE + 0x00)
#define SW_TIMER_INT_STA_REG         (SW_VA_TIMERC_IO_BASE + 0x04)
#define SW_TIMER0_CTL_REG            (SW_VA_TIMERC_IO_BASE + 0x10)
#define SW_TIMER0_INTVAL_REG         (SW_VA_TIMERC_IO_BASE + 0x14)
#define SW_TIMER0_CNTVAL_REG         (SW_VA_TIMERC_IO_BASE + 0x18)


/**
 * Interrupt controller registers
 *
 */
#define SW_INT_VECTOR_REG             (SW_VA_INT_IO_BASE + 0x00)
#define SW_INT_BASE_ADR_REG           (SW_VA_INT_IO_BASE + 0x04)
#define SW_INT_PENDING_REG0           (SW_VA_INT_IO_BASE + 0x08)
#define SW_INT_PENDING_REG1           (SW_VA_INT_IO_BASE + 0x0c)
#define SW_INT_CFG_REG                (SW_VA_INT_IO_BASE + 0x10)
#define SW_INT_ENABLE_REG0            (SW_VA_INT_IO_BASE + 0x14)
#define SW_INT_ENABLE_REG1            (SW_VA_INT_IO_BASE + 0x18)
#define SW_INT_MASK_REG0              (SW_VA_INT_IO_BASE + 0x1c)
#define SW_INT_MASK_REG1              (SW_VA_INT_IO_BASE + 0x20)
#define SW_INT_RESP_REG0              (SW_VA_INT_IO_BASE + 0x24)
#define SW_INT_RESP_REG1              (SW_VA_INT_IO_BASE + 0x28)
#define SW_INT_FORCE_REG0             (SW_VA_INT_IO_BASE + 0x2c)
#define SW_INT_FORCE_REG1             (SW_VA_INT_IO_BASE + 0x30)
#define SW_INT_PTY_REG0               (SW_VA_INT_IO_BASE + 0x34)
#define SW_INT_PTY_REG1               (SW_VA_INT_IO_BASE + 0x38)
#define SW_INT_PTY_REG2               (SW_VA_INT_IO_BASE + 0x3c)
#define SW_INT_PTY_REG3               (SW_VA_INT_IO_BASE + 0x40)

/**
*@name CCM controller register address
*@{
*/
#define SW_CCM_CORE_VE_PLL_REG			(SW_VA_CCM_IO_BASE + 0x00)
#define SW_CCM_AUDIO_HOSC_PLL_REG		(SW_VA_CCM_IO_BASE + 0x04)
#define SW_CCM_AHB_APB_CFG_REG			(SW_VA_CCM_IO_BASE + 0x08)
#define SW_CCM_AHB_GATE_REG				(SW_VA_CCM_IO_BASE + 0x0C)
#define SW_CCM_APB_GATE_REG				(SW_VA_CCM_IO_BASE + 0x10)
#define SW_CCM_SDRAM_PLL_REG			(SW_VA_CCM_IO_BASE + 0x20)
#define SW_CCM_MISC_CLK_REG				(SW_VA_CCM_IO_BASE + 0x4C)
/**
*@}
*/

/**
*@name TWI controller register address
*@{
*/
#define SW_TWI0_ADDR_REG					(SW_VA_TWI0_IO_BASE + 0x00)
#define SW_TWI0_XADDR_REG					(SW_VA_TWI0_IO_BASE + 0x04)
#define SW_TWI0_DATA_REG					(SW_VA_TWI0_IO_BASE + 0x08)
#define SW_TWI0_CNTR_REG					(SW_VA_TWI0_IO_BASE + 0x0C)
#define SW_TWI0_STAT_REG					(SW_VA_TWI0_IO_BASE + 0x10)
#define SW_TWI0_CCR_REG					(SW_VA_TWI0_IO_BASE + 0x14)
#define SW_TWI0_SRST_REG					(SW_VA_TWI0_IO_BASE + 0x18)
#define SW_TWI0_EFR_REG					(SW_VA_TWI0_IO_BASE + 0x1C)
#define SW_TWI0_LCR_REG					(SW_VA_TWI0_IO_BASE + 0x20)

#define SW_TWI1_ADDR_REG					(SW_VA_TWI1_IO_BASE + 0x00)
#define SW_TWI1_XADDR_REG					(SW_VA_TWI1_IO_BASE + 0x04)
#define SW_TWI1_DATA_REG					(SW_VA_TWI1_IO_BASE + 0x08)
#define SW_TWI1_CNTR_REG					(SW_VA_TWI1_IO_BASE + 0x0C)
#define SW_TWI1_STAT_REG					(SW_VA_TWI1_IO_BASE + 0x10)
#define SW_TWI1_CCR_REG					(SW_VA_TWI1_IO_BASE + 0x14)
#define SW_TWI1_SRST_REG					(SW_VA_TWI1_IO_BASE + 0x18)
#define SW_TWI1_EFR_REG					(SW_VA_TWI1_IO_BASE + 0x1C)
#define SW_TWI1_LCR_REG					(SW_VA_TWI1_IO_BASE + 0x20)

#define SW_TWI2_ADDR_REG					(SW_VA_TWI2_IO_BASE + 0x00)
#define SW_TWI2_XADDR_REG					(SW_VA_TWI2_IO_BASE + 0x04)
#define SW_TWI2_DATA_REG					(SW_VA_TWI2_IO_BASE + 0x08)
#define SW_TWI2_CNTR_REG					(SW_VA_TWI2_IO_BASE + 0x0C)
#define SW_TWI2_STAT_REG					(SW_VA_TWI2_IO_BASE + 0x10)
#define SW_TWI2_CCR_REG					(SW_VA_TWI2_IO_BASE + 0x14)
#define SW_TWI2_SRST_REG					(SW_VA_TWI2_IO_BASE + 0x18)
#define SW_TWI2_EFR_REG					(SW_VA_TWI2_IO_BASE + 0x1C)
#define SW_TWI2_LCR_REG					(SW_VA_TWI2_IO_BASE + 0x20)
/**
*@}
*/

/**
*@name DRAM controller register address
*@{
*/
#define SW_DRAM_SDR_CTL_REG				(SW_VA_DRAM_IO_BASE + 0x0C)
/**
*@}
*/

/**
 * UART registers
 *
 */
#define SW_UART0_THR                  (*(volatile unsigned int *)(SW_VA_UART0_IO_BASE + 0x00))
#define SW_UART0_LSR                  (*(volatile unsigned int *)(SW_VA_UART0_IO_BASE + 0x14))
#define SW_UART0_USR                  (*(volatile unsigned int *)(SW_VA_UART0_IO_BASE + 0x7c))

#define __REG(x)    (*(volatile unsigned int   *)(x))
#define PA_VIC_BASE             0x01c20400
#define VA_VIC_BASE             IO_ADDRESS(PA_VIC_BASE)

#define PIO_BASE                       SW_PA_PORTC_IO_BASE



#endif

