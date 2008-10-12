/*
 * Defines for the address space, registers and register configuration
 * (bit masks, access macros etc) for the PMC-Sierra line of MSP products.
 * This file contains addess maps for all the devices in the line of
 * products but only has register definitions and configuration masks for
 * registers which aren't definitely associated with any device.  Things
 * like clock settings, reset access, the ELB etc.  Individual device
 * drivers will reference the appropriate XXX_BASE value defined here
 * and have individual registers offset from that.
 *
 * Copyright (C) 2005-2007 PMC-Sierra, Inc.  All rights reserved.
 * Author: Andrew Hughes, Andrew_Hughes@pmc-sierra.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */

#include <asm/addrspace.h>
#include <linux/types.h>

#ifndef _ASM_MSP_REGS_H
#define _ASM_MSP_REGS_H

/*
 ########################################################################
 #  Address space and device base definitions                           #
 ########################################################################
 */

/*
 ***************************************************************************
 * System Logic and Peripherals (ELB, UART0, etc) device address space     *
 ***************************************************************************
 */
#define MSP_SLP_BASE		0x1c000000
					/* System Logic and Peripherals */
#define MSP_RST_BASE		(MSP_SLP_BASE + 0x10)
					/* System reset register base	*/
#define MSP_RST_SIZE		0x0C	/* System reset register space	*/

#define MSP_WTIMER_BASE		(MSP_SLP_BASE + 0x04C)
					/* watchdog timer base          */
#define MSP_ITIMER_BASE		(MSP_SLP_BASE + 0x054)
					/* internal timer base          */
#define MSP_UART0_BASE		(MSP_SLP_BASE + 0x100)
					/* UART0 controller base        */
#define MSP_BCPY_CTRL_BASE	(MSP_SLP_BASE + 0x120)
					/* Block Copy controller base   */
#define MSP_BCPY_DESC_BASE	(MSP_SLP_BASE + 0x160)
					/* Block Copy descriptor base   */

/*
 ***************************************************************************
 * PCI address space                                                       *
 ***************************************************************************
 */
#define MSP_PCI_BASE		0x19000000

/*
 ***************************************************************************
 * MSbus device address space                                              *
 ***************************************************************************
 */
#define MSP_MSB_BASE		0x18000000
					/* MSbus address start          */
#define MSP_PER_BASE		(MSP_MSB_BASE + 0x400000)
					/* Peripheral device registers  */
#define MSP_MAC0_BASE		(MSP_MSB_BASE + 0x600000)
					/* MAC A device registers       */
#define MSP_MAC1_BASE		(MSP_MSB_BASE + 0x700000)
					/* MAC B device registers       */
#define MSP_MAC_SIZE		0xE0	/* MAC register space		*/

#define MSP_SEC_BASE		(MSP_MSB_BASE + 0x800000)
					/* Security Engine registers    */
#define MSP_MAC2_BASE		(MSP_MSB_BASE + 0x900000)
					/* MAC C device registers       */
#define MSP_ADSL2_BASE		(MSP_MSB_BASE + 0xA80000)
					/* ADSL2 device registers       */
#define MSP_USB_BASE		(MSP_MSB_BASE + 0xB40000)
					/* USB device registers         */
#define MSP_USB_BASE_START	(MSP_MSB_BASE + 0xB40100)
					/* USB device registers         */
#define MSP_USB_BASE_END	(MSP_MSB_BASE + 0xB401FF)
					/* USB device registers         */
#define MSP_CPUIF_BASE		(MSP_MSB_BASE + 0xC00000)
					/* CPU interface registers      */

/* Devices within the MSbus peripheral block */
#define MSP_UART1_BASE		(MSP_PER_BASE + 0x030)
					/* UART1 controller base        */
#define MSP_SPI_BASE		(MSP_PER_BASE + 0x058)
					/* SPI/MPI control registers    */
#define MSP_TWI_BASE		(MSP_PER_BASE + 0x090)
					/* Two-wire control registers   */
#define MSP_PTIMER_BASE		(MSP_PER_BASE + 0x0F0)
					/* Programmable timer control   */

/*
 ***************************************************************************
 * Physical Memory configuration address space                             *
 ***************************************************************************
 */
#define MSP_MEM_CFG_BASE	0x17f00000

#define MSP_MEM_INDIRECT_CTL_10	0x10

/*
 * Notes:
 *  1) The SPI registers are split into two blocks, one offset from the
 *     MSP_SPI_BASE by 0x00 and the other offset from the MSP_SPI_BASE by
 *     0x68.  The SPI driver definitions for the register must be aware
 *     of this.
 *  2) The block copy engine register are divided into two regions, one
 *     for the control/configuration of the engine proper and one for the
 *     values of the descriptors used in the copy process.  These have
 *     different base defines (CTRL_BASE vs DESC_BASE)
 *  3) These constants are for physical addresses which means that they
 *     work correctly with "ioremap" and friends.  This means that device
 *     drivers will need to remap these addresses using ioremap and perhaps
 *     the readw/writew macros.  Or they could use the regptr() macro
 *     defined below, but the readw/writew calls are the correct thing.
 *  4) The UARTs have an additional status register offset from the base
 *     address.  This register isn't used in the standard 8250 driver but
 *     may be used in other software.  Consult the hardware datasheet for
 *     offset details.
 *  5) For some unknown reason the security engine (MSP_SEC_BASE) registers
 *     start at an offset of 0x84 from the base address but the block of
 *     registers before this is reserved for the security engine.  The
 *     driver will have to be aware of this but it makes the register
 *     definitions line up better with the documentation.
 */

/*
 ########################################################################
 #  System register definitions.  Not associated with a specific device #
 ########################################################################
 */

/*
 * This macro maps the physical register number into uncached space
 * and (for C code) casts it into a u32 pointer so it can be dereferenced
 * Normally these would be accessed with ioremap and readX/writeX, but
 * these are convenient for a lot of internal kernel code.
 */
#ifdef __ASSEMBLER__
	#define regptr(addr) (KSEG1ADDR(addr))
#else
	#define regptr(addr) ((volatile u32 *const)(KSEG1ADDR(addr)))
#endif

/*
 ***************************************************************************
 * System Logic and Peripherals (RESET, ELB, etc) registers                *
 ***************************************************************************
 */

/* System Control register definitions */
#define	DEV_ID_REG	regptr(MSP_SLP_BASE + 0x00)
					/* Device-ID                 RO */
#define	FWR_ID_REG	regptr(MSP_SLP_BASE + 0x04)
					/* Firmware-ID Register      RW */
#define	SYS_ID_REG0	regptr(MSP_SLP_BASE + 0x08)
					/* System-ID Register-0      RW */
#define	SYS_ID_REG1	regptr(MSP_SLP_BASE + 0x0C)
					/* System-ID Register-1      RW */

/* System Reset register definitions */
#define	RST_STS_REG	regptr(MSP_SLP_BASE + 0x10)
					/* System Reset Status       RO */
#define	RST_SET_REG	regptr(MSP_SLP_BASE + 0x14)
					/* System Set Reset          WO */
#define	RST_CLR_REG	regptr(MSP_SLP_BASE + 0x18)
					/* System Clear Reset        WO */

/* System Clock Registers */
#define PCI_SLP_REG	regptr(MSP_SLP_BASE + 0x1C)
					/* PCI clock generator       RW */
#define URT_SLP_REG	regptr(MSP_SLP_BASE + 0x20)
					/* UART clock generator      RW */
/* reserved		      (MSP_SLP_BASE + 0x24)                     */
/* reserved		      (MSP_SLP_BASE + 0x28)                     */
#define PLL1_SLP_REG	regptr(MSP_SLP_BASE + 0x2C)
					/* PLL1 clock generator      RW */
#define PLL0_SLP_REG	regptr(MSP_SLP_BASE + 0x30)
					/* PLL0 clock generator      RW */
#define MIPS_SLP_REG	regptr(MSP_SLP_BASE + 0x34)
					/* MIPS clock generator      RW */
#define	VE_SLP_REG	regptr(MSP_SLP_BASE + 0x38)
					/* Voice Eng clock generator RW */
/* reserved		      (MSP_SLP_BASE + 0x3C)                     */
#define MSB_SLP_REG	regptr(MSP_SLP_BASE + 0x40)
					/* MS-Bus clock generator    RW */
#define SMAC_SLP_REG	regptr(MSP_SLP_BASE + 0x44)
					/* Sec & MAC clock generator RW */
#define PERF_SLP_REG	regptr(MSP_SLP_BASE + 0x48)
					/* Per & TDM clock generator RW */

/* Interrupt Controller Registers */
#define SLP_INT_STS_REG regptr(MSP_SLP_BASE + 0x70)
					/* Interrupt status register RW */
#define SLP_INT_MSK_REG regptr(MSP_SLP_BASE + 0x74)
					/* Interrupt enable/mask     RW */
#define SE_MBOX_REG	regptr(MSP_SLP_BASE + 0x78)
					/* Security Engine mailbox   RW */
#define VE_MBOX_REG	regptr(MSP_SLP_BASE + 0x7C)
					/* Voice Engine mailbox      RW */

/* ELB Controller Registers */
#define CS0_CNFG_REG	regptr(MSP_SLP_BASE + 0x80)
					/* ELB CS0 Configuration Reg    */
#define CS0_ADDR_REG	regptr(MSP_SLP_BASE + 0x84)
					/* ELB CS0 Base Address Reg     */
#define CS0_MASK_REG	regptr(MSP_SLP_BASE + 0x88)
					/* ELB CS0 Mask Register        */
#define CS0_ACCESS_REG	regptr(MSP_SLP_BASE + 0x8C)
					/* ELB CS0 access register      */

#define CS1_CNFG_REG	regptr(MSP_SLP_BASE + 0x90)
					/* ELB CS1 Configuration Reg    */
#define CS1_ADDR_REG	regptr(MSP_SLP_BASE + 0x94)
					/* ELB CS1 Base Address Reg     */
#define CS1_MASK_REG	regptr(MSP_SLP_BASE + 0x98)
					/* ELB CS1 Mask Register        */
#define CS1_ACCESS_REG	regptr(MSP_SLP_BASE + 0x9C)
					/* ELB CS1 access register      */

#define CS2_CNFG_REG	regptr(MSP_SLP_BASE + 0xA0)
					/* ELB CS2 Configuration Reg    */
#define CS2_ADDR_REG	regptr(MSP_SLP_BASE + 0xA4)
					/* ELB CS2 Base Address Reg     */
#define CS2_MASK_REG	regptr(MSP_SLP_BASE + 0xA8)
					/* ELB CS2 Mask Register        */
#define CS2_ACCESS_REG	regptr(MSP_SLP_BASE + 0xAC)
					/* ELB CS2 access register      */

#define CS3_CNFG_REG	regptr(MSP_SLP_BASE + 0xB0)
					/* ELB CS3 Configuration Reg    */
#define CS3_ADDR_REG	regptr(MSP_SLP_BASE + 0xB4)
					/* ELB CS3 Base Address Reg     */
#define CS3_MASK_REG	regptr(MSP_SLP_BASE + 0xB8)
					/* ELB CS3 Mask Register        */
#define CS3_ACCESS_REG	regptr(MSP_SLP_BASE + 0xBC)
					/* ELB CS3 access register      */

#define CS4_CNFG_REG	regptr(MSP_SLP_BASE + 0xC0)
					/* ELB CS4 Configuration Reg    */
#define CS4_ADDR_REG	regptr(MSP_SLP_BASE + 0xC4)
					/* ELB CS4 Base Address Reg     */
#define CS4_MASK_REG	regptr(MSP_SLP_BASE + 0xC8)
					/* ELB CS4 Mask Register        */
#define CS4_ACCESS_REG	regptr(MSP_SLP_BASE + 0xCC)
					/* ELB CS4 access register      */

#define CS5_CNFG_REG	regptr(MSP_SLP_BASE + 0xD0)
					/* ELB CS5 Configuration Reg    */
#define CS5_ADDR_REG	regptr(MSP_SLP_BASE + 0xD4)
					/* ELB CS5 Base Address Reg     */
#define CS5_MASK_REG	regptr(MSP_SLP_BASE + 0xD8)
					/* ELB CS5 Mask Register        */
#define CS5_ACCESS_REG	regptr(MSP_SLP_BASE + 0xDC)
					/* ELB CS5 access register      */

/* reserved			       0xE0 - 0xE8                      */
#define ELB_1PC_EN_REG	regptr(MSP_SLP_BASE + 0xEC)
					/* ELB single PC card detect    */

/* reserved			       0xF0 - 0xF8                      */
#define ELB_CLK_CFG_REG	regptr(MSP_SLP_BASE + 0xFC)
					/* SDRAM read/ELB timing Reg    */

/* Extended UART status registers */
#define UART0_STATUS_REG	regptr(MSP_UART0_BASE + 0x0c0)
					/* UART Status Register 0       */
#define UART1_STATUS_REG	regptr(MSP_UART1_BASE + 0x170)
					/* UART Status Register 1       */

/* Performance monitoring registers */
#define PERF_MON_CTRL_REG	regptr(MSP_SLP_BASE + 0x140)
					/* Performance monitor control  */
#define PERF_MON_CLR_REG	regptr(MSP_SLP_BASE + 0x144)
					/* Performance monitor clear    */
#define PERF_MON_CNTH_REG	regptr(MSP_SLP_BASE + 0x148)
					/* Perf monitor counter high    */
#define PERF_MON_CNTL_REG	regptr(MSP_SLP_BASE + 0x14C)
					/* Perf monitor counter low     */

/* System control registers */
#define SYS_CTRL_REG		regptr(MSP_SLP_BASE + 0x150)
					/* System control register      */
#define SYS_ERR1_REG		regptr(MSP_SLP_BASE + 0x154)
					/* System Error status 1        */
#define SYS_ERR2_REG		regptr(MSP_SLP_BASE + 0x158)
					/* System Error status 2        */
#define SYS_INT_CFG_REG		regptr(MSP_SLP_BASE + 0x15C)
					/* System Interrupt config      */

/* Voice Engine Memory configuration */
#define VE_MEM_REG		regptr(MSP_SLP_BASE + 0x17C)
					/* Voice engine memory config   */

/* CPU/SLP Error Status registers */
#define CPU_ERR1_REG		regptr(MSP_SLP_BASE + 0x180)
					/* CPU/SLP Error status 1       */
#define CPU_ERR2_REG		regptr(MSP_SLP_BASE + 0x184)
					/* CPU/SLP Error status 1       */

#define EXTENDED_GPIO_REG	regptr(MSP_SLP_BASE + 0x188)
					/* Extended GPIO register       */

/* System Error registers */
#define SLP_ERR_STS_REG		regptr(MSP_SLP_BASE + 0x190)
					/* Int status for SLP errors    */
#define SLP_ERR_MSK_REG		regptr(MSP_SLP_BASE + 0x194)
					/* Int mask for SLP errors      */
#define SLP_ELB_ERST_REG	regptr(MSP_SLP_BASE + 0x198)
					/* External ELB reset           */
#define SLP_BOOT_STS_REG	regptr(MSP_SLP_BASE + 0x19C)
					/* Boot Status                  */

/* Extended ELB addressing */
#define CS0_EXT_ADDR_REG	regptr(MSP_SLP_BASE + 0x1A0)
					/* CS0 Extended address         */
#define CS1_EXT_ADDR_REG	regptr(MSP_SLP_BASE + 0x1A4)
					/* CS1 Extended address         */
#define CS2_EXT_ADDR_REG	regptr(MSP_SLP_BASE + 0x1A8)
					/* CS2 Extended address         */
#define CS3_EXT_ADDR_REG	regptr(MSP_SLP_BASE + 0x1AC)
					/* CS3 Extended address         */
/* reserved					      0x1B0             */
#define CS5_EXT_ADDR_REG	regptr(MSP_SLP_BASE + 0x1B4)
					/* CS5 Extended address         */

/* PLL Adjustment registers */
#define PLL_LOCK_REG		regptr(MSP_SLP_BASE + 0x200)
					/* PLL0 lock status             */
#define PLL_ARST_REG		regptr(MSP_SLP_BASE + 0x204)
					/* PLL Analog reset status      */
#define PLL0_ADJ_REG		regptr(MSP_SLP_BASE + 0x208)
					/* PLL0 Adjustment value        */
#define PLL1_ADJ_REG		regptr(MSP_SLP_BASE + 0x20C)
					/* PLL1 Adjustment value        */

/*
 ***************************************************************************
 * Peripheral Register definitions                                         *
 ***************************************************************************
 */

/* Peripheral status */
#define PER_CTRL_REG		regptr(MSP_PER_BASE + 0x50)
					/* Peripheral control register  */
#define PER_STS_REG		regptr(MSP_PER_BASE + 0x54)
					/* Peripheral status register   */

/* SPI/MPI Registers */
#define SMPI_TX_SZ_REG		regptr(MSP_PER_BASE + 0x58)
					/* SPI/MPI Tx Size register     */
#define SMPI_RX_SZ_REG		regptr(MSP_PER_BASE + 0x5C)
					/* SPI/MPI Rx Size register     */
#define SMPI_CTL_REG		regptr(MSP_PER_BASE + 0x60)
					/* SPI/MPI Control register     */
#define SMPI_MS_REG		regptr(MSP_PER_BASE + 0x64)
					/* SPI/MPI Chip Select reg      */
#define SMPI_CORE_DATA_REG	regptr(MSP_PER_BASE + 0xC0)
					/* SPI/MPI Core Data reg        */
#define SMPI_CORE_CTRL_REG	regptr(MSP_PER_BASE + 0xC4)
					/* SPI/MPI Core Control reg     */
#define SMPI_CORE_STAT_REG	regptr(MSP_PER_BASE + 0xC8)
					/* SPI/MPI Core Status reg      */
#define SMPI_CORE_SSEL_REG	regptr(MSP_PER_BASE + 0xCC)
					/* SPI/MPI Core Ssel reg        */
#define SMPI_FIFO_REG		regptr(MSP_PER_BASE + 0xD0)
					/* SPI/MPI Data FIFO reg        */

/* Peripheral Block Error Registers           */
#define PER_ERR_STS_REG		regptr(MSP_PER_BASE + 0x70)
					/* Error Bit Status Register    */
#define PER_ERR_MSK_REG		regptr(MSP_PER_BASE + 0x74)
					/* Error Bit Mask Register      */
#define PER_HDR1_REG		regptr(MSP_PER_BASE + 0x78)
					/* Error Header 1 Register      */
#define PER_HDR2_REG		regptr(MSP_PER_BASE + 0x7C)
					/* Error Header 2 Register      */

/* Peripheral Block Interrupt Registers       */
#define PER_INT_STS_REG		regptr(MSP_PER_BASE + 0x80)
					/* Interrupt status register    */
#define PER_INT_MSK_REG		regptr(MSP_PER_BASE + 0x84)
					/* Interrupt Mask Register      */
#define GPIO_INT_STS_REG	regptr(MSP_PER_BASE + 0x88)
					/* GPIO interrupt status reg    */
#define GPIO_INT_MSK_REG	regptr(MSP_PER_BASE + 0x8C)
					/* GPIO interrupt MASK Reg      */

/* POLO GPIO registers                        */
#define POLO_GPIO_DAT1_REG	regptr(MSP_PER_BASE + 0x0E0)
					/* Polo GPIO[8:0]  data reg     */
#define POLO_GPIO_CFG1_REG	regptr(MSP_PER_BASE + 0x0E4)
					/* Polo GPIO[7:0]  config reg   */
#define POLO_GPIO_CFG2_REG	regptr(MSP_PER_BASE + 0x0E8)
					/* Polo GPIO[15:8] config reg   */
#define POLO_GPIO_OD1_REG	regptr(MSP_PER_BASE + 0x0EC)
					/* Polo GPIO[31:0] output drive */
#define POLO_GPIO_CFG3_REG	regptr(MSP_PER_BASE + 0x170)
					/* Polo GPIO[23:16] config reg  */
#define POLO_GPIO_DAT2_REG	regptr(MSP_PER_BASE + 0x174)
					/* Polo GPIO[15:9]  data reg    */
#define POLO_GPIO_DAT3_REG	regptr(MSP_PER_BASE + 0x178)
					/* Polo GPIO[23:16]  data reg   */
#define POLO_GPIO_DAT4_REG	regptr(MSP_PER_BASE + 0x17C)
					/* Polo GPIO[31:24]  data reg   */
#define POLO_GPIO_DAT5_REG	regptr(MSP_PER_BASE + 0x180)
					/* Polo GPIO[39:32]  data reg   */
#define POLO_GPIO_DAT6_REG	regptr(MSP_PER_BASE + 0x184)
					/* Polo GPIO[47:40]  data reg   */
#define POLO_GPIO_DAT7_REG	regptr(MSP_PER_BASE + 0x188)
					/* Polo GPIO[54:48]  data reg   */
#define POLO_GPIO_CFG4_REG	regptr(MSP_PER_BASE + 0x18C)
					/* Polo GPIO[31:24] config reg  */
#define POLO_GPIO_CFG5_REG	regptr(MSP_PER_BASE + 0x190)
					/* Polo GPIO[39:32] config reg  */
#define POLO_GPIO_CFG6_REG	regptr(MSP_PER_BASE + 0x194)
					/* Polo GPIO[47:40] config reg  */
#define POLO_GPIO_CFG7_REG	regptr(MSP_PER_BASE + 0x198)
					/* Polo GPIO[54:48] config reg  */
#define POLO_GPIO_OD2_REG	regptr(MSP_PER_BASE + 0x19C)
					/* Polo GPIO[54:32] output drive */

/* Generic GPIO registers                     */
#define GPIO_DATA1_REG		regptr(MSP_PER_BASE + 0x170)
					/* GPIO[1:0] data register      */
#define GPIO_DATA2_REG		regptr(MSP_PER_BASE + 0x174)
					/* GPIO[5:2] data register      */
#define GPIO_DATA3_REG		regptr(MSP_PER_BASE + 0x178)
					/* GPIO[9:6] data register      */
#define GPIO_DATA4_REG		regptr(MSP_PER_BASE + 0x17C)
					/* GPIO[15:10] data register    */
#define GPIO_CFG1_REG		regptr(MSP_PER_BASE + 0x180)
					/* GPIO[1:0] config register    */
#define GPIO_CFG2_REG		regptr(MSP_PER_BASE + 0x184)
					/* GPIO[5:2] config register    */
#define GPIO_CFG3_REG		regptr(MSP_PER_BASE + 0x188)
					/* GPIO[9:6] config register    */
#define GPIO_CFG4_REG		regptr(MSP_PER_BASE + 0x18C)
					/* GPIO[15:10] config register  */
#define GPIO_OD_REG		regptr(MSP_PER_BASE + 0x190)
					/* GPIO[15:0] output drive      */

/*
 ***************************************************************************
 * CPU Interface register definitions                                      *
 ***************************************************************************
 */
#define PCI_FLUSH_REG		regptr(MSP_CPUIF_BASE + 0x00)
					/* PCI-SDRAM queue flush trigger */
#define OCP_ERR1_REG		regptr(MSP_CPUIF_BASE + 0x04)
					/* OCP Error Attribute 1        */
#define OCP_ERR2_REG		regptr(MSP_CPUIF_BASE + 0x08)
					/* OCP Error Attribute 2        */
#define OCP_STS_REG		regptr(MSP_CPUIF_BASE + 0x0C)
					/* OCP Error Status             */
#define CPUIF_PM_REG		regptr(MSP_CPUIF_BASE + 0x10)
					/* CPU policy configuration     */
#define CPUIF_CFG_REG		regptr(MSP_CPUIF_BASE + 0x10)
					/* Misc configuration options   */

/* Central Interrupt Controller Registers */
#define MSP_CIC_BASE		(MSP_CPUIF_BASE + 0x8000)
					/* Central Interrupt registers  */
#define CIC_EXT_CFG_REG		regptr(MSP_CIC_BASE + 0x00)
					/* External interrupt config    */
#define CIC_STS_REG		regptr(MSP_CIC_BASE + 0x04)
					/* CIC Interrupt Status         */
#define CIC_VPE0_MSK_REG	regptr(MSP_CIC_BASE + 0x08)
					/* VPE0 Interrupt Mask          */
#define CIC_VPE1_MSK_REG	regptr(MSP_CIC_BASE + 0x0C)
					/* VPE1 Interrupt Mask          */
#define CIC_TC0_MSK_REG		regptr(MSP_CIC_BASE + 0x10)
					/* Thread Context 0 Int Mask    */
#define CIC_TC1_MSK_REG		regptr(MSP_CIC_BASE + 0x14)
					/* Thread Context 1 Int Mask    */
#define CIC_TC2_MSK_REG		regptr(MSP_CIC_BASE + 0x18)
					/* Thread Context 2 Int Mask    */
#define CIC_TC3_MSK_REG		regptr(MSP_CIC_BASE + 0x18)
					/* Thread Context 3 Int Mask    */
#define CIC_TC4_MSK_REG		regptr(MSP_CIC_BASE + 0x18)
					/* Thread Context 4 Int Mask    */
#define CIC_PCIMSI_STS_REG	regptr(MSP_CIC_BASE + 0x18)
#define CIC_PCIMSI_MSK_REG	regptr(MSP_CIC_BASE + 0x18)
#define CIC_PCIFLSH_REG		regptr(MSP_CIC_BASE + 0x18)
#define CIC_VPE0_SWINT_REG	regptr(MSP_CIC_BASE + 0x08)


/*
 ***************************************************************************
 * Memory controller registers                                             *
 ***************************************************************************
 */
#define MEM_CFG1_REG		regptr(MSP_MEM_CFG_BASE + 0x00)
#define MEM_SS_ADDR		regptr(MSP_MEM_CFG_BASE + 0x00)
#define MEM_SS_DATA		regptr(MSP_MEM_CFG_BASE + 0x04)
#define MEM_SS_WRITE		regptr(MSP_MEM_CFG_BASE + 0x08)

/*
 ***************************************************************************
 * PCI controller registers                                                *
 ***************************************************************************
 */
#define PCI_BASE_REG		regptr(MSP_PCI_BASE + 0x00)
#define PCI_CONFIG_SPACE_REG	regptr(MSP_PCI_BASE + 0x800)
#define PCI_JTAG_DEVID_REG	regptr(MSP_SLP_BASE + 0x13c)

/*
 ########################################################################
 #  Register content & macro definitions                                #
 ########################################################################
 */

/*
 ***************************************************************************
 * DEV_ID defines                                                          *
 ***************************************************************************
 */
#define DEV_ID_PCI_DIS		(1 << 26)       /* Set if PCI disabled */
#define DEV_ID_PCI_HOST		(1 << 20)       /* Set if PCI host */
#define DEV_ID_SINGLE_PC	(1 << 19)       /* Set if single PC Card */
#define DEV_ID_FAMILY		(0xff << 8)     /* family ID code */
#define POLO_ZEUS_SUB_FAMILY	(0x7  << 16)    /* sub family for Polo/Zeus */

#define MSPFPGA_ID		(0x00  << 8)    /* you are on your own here */
#define MSP5000_ID		(0x50  << 8)
#define MSP4F00_ID		(0x4f  << 8)    /* FPGA version of MSP4200 */
#define MSP4E00_ID		(0x4f  << 8)    /* FPGA version of MSP7120 */
#define MSP4200_ID		(0x42  << 8)
#define MSP4000_ID		(0x40  << 8)
#define MSP2XXX_ID		(0x20  << 8)
#define MSPZEUS_ID		(0x10  << 8)

#define MSP2004_SUB_ID		(0x0   << 16)
#define MSP2005_SUB_ID		(0x1   << 16)
#define MSP2006_SUB_ID		(0x1   << 16)
#define MSP2007_SUB_ID		(0x2   << 16)
#define MSP2010_SUB_ID		(0x3   << 16)
#define MSP2015_SUB_ID		(0x4   << 16)
#define MSP2020_SUB_ID		(0x5   << 16)
#define MSP2100_SUB_ID		(0x6   << 16)

/*
 ***************************************************************************
 * RESET defines                                                           *
 ***************************************************************************
 */
#define MSP_GR_RST		(0x01 << 0)     /* Global reset bit     */
#define MSP_MR_RST		(0x01 << 1)     /* MIPS reset bit       */
#define MSP_PD_RST		(0x01 << 2)     /* PVC DMA reset bit    */
#define MSP_PP_RST		(0x01 << 3)     /* PVC reset bit        */
/* reserved                                                             */
#define MSP_EA_RST		(0x01 << 6)     /* Mac A reset bit      */
#define MSP_EB_RST		(0x01 << 7)     /* Mac B reset bit      */
#define MSP_SE_RST		(0x01 << 8)     /* Security Eng reset bit */
#define MSP_PB_RST		(0x01 << 9)     /* Per block reset bit  */
#define MSP_EC_RST		(0x01 << 10)    /* Mac C reset bit      */
#define MSP_TW_RST		(0x01 << 11)    /* TWI reset bit        */
#define MSP_SPI_RST		(0x01 << 12)    /* SPI/MPI reset bit    */
#define MSP_U1_RST		(0x01 << 13)    /* UART1 reset bit      */
#define MSP_U0_RST		(0x01 << 14)    /* UART0 reset bit      */

/*
 ***************************************************************************
 * UART defines                                                            *
 ***************************************************************************
 */
#define MSP_BASE_BAUD		25000000
#define MSP_UART_REG_LEN	0x20

/*
 ***************************************************************************
 * ELB defines                                                             *
 ***************************************************************************
 */
#define PCCARD_32		0x02    /* Set if is PCCARD 32 (Cardbus) */
#define SINGLE_PCCARD		0x01    /* Set to enable single PC card */

/*
 ***************************************************************************
 * CIC defines                                                             *
 ***************************************************************************
 */

/* CIC_EXT_CFG_REG */
#define EXT_INT_POL(eirq)			(1 << (eirq + 8))
#define EXT_INT_EDGE(eirq)			(1 << eirq)

#define CIC_EXT_SET_TRIGGER_LEVEL(reg, eirq)	(reg &= ~EXT_INT_EDGE(eirq))
#define CIC_EXT_SET_TRIGGER_EDGE(reg, eirq)	(reg |= EXT_INT_EDGE(eirq))
#define CIC_EXT_SET_ACTIVE_HI(reg, eirq)	(reg |= EXT_INT_POL(eirq))
#define CIC_EXT_SET_ACTIVE_LO(reg, eirq)	(reg &= ~EXT_INT_POL(eirq))
#define CIC_EXT_SET_ACTIVE_RISING		CIC_EXT_SET_ACTIVE_HI
#define CIC_EXT_SET_ACTIVE_FALLING		CIC_EXT_SET_ACTIVE_LO

#define CIC_EXT_IS_TRIGGER_LEVEL(reg, eirq) \
				((reg & EXT_INT_EDGE(eirq)) == 0)
#define CIC_EXT_IS_TRIGGER_EDGE(reg, eirq)	(reg & EXT_INT_EDGE(eirq))
#define CIC_EXT_IS_ACTIVE_HI(reg, eirq)		(reg & EXT_INT_POL(eirq))
#define CIC_EXT_IS_ACTIVE_LO(reg, eirq) \
				((reg & EXT_INT_POL(eirq)) == 0)
#define CIC_EXT_IS_ACTIVE_RISING		CIC_EXT_IS_ACTIVE_HI
#define CIC_EXT_IS_ACTIVE_FALLING		CIC_EXT_IS_ACTIVE_LO

/*
 ***************************************************************************
 * Memory Controller defines                                               *
 ***************************************************************************
 */

/* Indirect memory controller registers */
#define DDRC_CFG(n)		(n)
#define DDRC_DEBUG(n)		(0x04 + n)
#define DDRC_CTL(n)		(0x40 + n)

/* Macro to perform DDRC indirect write */
#define DDRC_INDIRECT_WRITE(reg, mask, value) \
({ \
	*MEM_SS_ADDR = (((mask) & 0xf) << 8) | ((reg) & 0xff); \
	*MEM_SS_DATA = (value); \
	*MEM_SS_WRITE = 1; \
})

/*
 ***************************************************************************
 * SPI/MPI Mode                                                            *
 ***************************************************************************
 */
#define SPI_MPI_RX_BUSY		0x00008000	/* SPI/MPI Receive Busy */
#define SPI_MPI_FIFO_EMPTY	0x00004000	/* SPI/MPI Fifo Empty   */
#define SPI_MPI_TX_BUSY		0x00002000	/* SPI/MPI Transmit Busy */
#define SPI_MPI_FIFO_FULL	0x00001000	/* SPI/MPU FIFO full    */

/*
 ***************************************************************************
 * SPI/MPI Control Register                                                *
 ***************************************************************************
 */
#define SPI_MPI_RX_START	0x00000004	/* Start receive command */
#define SPI_MPI_FLUSH_Q		0x00000002	/* Flush SPI/MPI Queue */
#define SPI_MPI_TX_START	0x00000001	/* Start Transmit Command */

#endif /* !_ASM_MSP_REGS_H */
