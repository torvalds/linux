/*
 * arch/arm/mach-ixp4xx/include/mach/ixp4xx-regs.h
 *
 * Register definitions for IXP4xx chipset. This file contains 
 * register location and bit definitions only. Platform specific 
 * definitions and helper function declarations are in platform.h 
 * and machine-name.h.
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ASM_ARM_IXP4XX_H_
#define _ASM_ARM_IXP4XX_H_

/*
 * IXP4xx Linux Memory Map:
 *
 * Phy		Size		Virt		Description
 * =========================================================================
 *
 * 0x00000000	0x10000000(max)	PAGE_OFFSET	System RAM
 *
 * 0x48000000	0x04000000	ioremap'd	PCI Memory Space
 *
 * 0x50000000	0x10000000	ioremap'd	EXP BUS
 *
 * 0x6000000	0x00004000	ioremap'd	QMgr
 *
 * 0xC0000000	0x00001000	0xffbff000	PCI CFG
 *
 * 0xC4000000	0x00001000	0xffbfe000	EXP CFG
 *
 * 0xC8000000	0x00013000	0xffbeb000	On-Chip Peripherals
 */

/*
 * Queue Manager
 */
#define IXP4XX_QMGR_BASE_PHYS		(0x60000000)
#define IXP4XX_QMGR_REGION_SIZE		(0x00004000)

/*
 * Expansion BUS Configuration registers
 */
#define IXP4XX_EXP_CFG_BASE_PHYS	(0xC4000000)
#define IXP4XX_EXP_CFG_BASE_VIRT	IOMEM(0xFFBFE000)
#define IXP4XX_EXP_CFG_REGION_SIZE	(0x00001000)

/*
 * PCI Config registers
 */
#define IXP4XX_PCI_CFG_BASE_PHYS	(0xC0000000)
#define	IXP4XX_PCI_CFG_BASE_VIRT	IOMEM(0xFFBFF000)
#define IXP4XX_PCI_CFG_REGION_SIZE	(0x00001000)

/*
 * Peripheral space
 */
#define IXP4XX_PERIPHERAL_BASE_PHYS	(0xC8000000)
#define IXP4XX_PERIPHERAL_BASE_VIRT	IOMEM(0xFFBEB000)
#define IXP4XX_PERIPHERAL_REGION_SIZE	(0x00013000)

/*
 * Debug UART
 *
 * This is basically a remap of UART1 into a region that is section
 * aligned so that it * can be used with the low-level debug code.
 */
#define	IXP4XX_DEBUG_UART_BASE_PHYS	(0xC8000000)
#define	IXP4XX_DEBUG_UART_BASE_VIRT	IOMEM(0xffb00000)
#define	IXP4XX_DEBUG_UART_REGION_SIZE	(0x00001000)

#define IXP4XX_EXP_CS0_OFFSET	0x00
#define IXP4XX_EXP_CS1_OFFSET   0x04
#define IXP4XX_EXP_CS2_OFFSET   0x08
#define IXP4XX_EXP_CS3_OFFSET   0x0C
#define IXP4XX_EXP_CS4_OFFSET   0x10
#define IXP4XX_EXP_CS5_OFFSET   0x14
#define IXP4XX_EXP_CS6_OFFSET   0x18
#define IXP4XX_EXP_CS7_OFFSET   0x1C
#define IXP4XX_EXP_CFG0_OFFSET	0x20
#define IXP4XX_EXP_CFG1_OFFSET	0x24
#define IXP4XX_EXP_CFG2_OFFSET	0x28
#define IXP4XX_EXP_CFG3_OFFSET	0x2C

/*
 * Expansion Bus Controller registers.
 */
#define IXP4XX_EXP_REG(x) ((volatile u32 __iomem *)(IXP4XX_EXP_CFG_BASE_VIRT+(x)))

#define IXP4XX_EXP_CS0      IXP4XX_EXP_REG(IXP4XX_EXP_CS0_OFFSET)
#define IXP4XX_EXP_CS1      IXP4XX_EXP_REG(IXP4XX_EXP_CS1_OFFSET)
#define IXP4XX_EXP_CS2      IXP4XX_EXP_REG(IXP4XX_EXP_CS2_OFFSET) 
#define IXP4XX_EXP_CS3      IXP4XX_EXP_REG(IXP4XX_EXP_CS3_OFFSET)
#define IXP4XX_EXP_CS4      IXP4XX_EXP_REG(IXP4XX_EXP_CS4_OFFSET)
#define IXP4XX_EXP_CS5      IXP4XX_EXP_REG(IXP4XX_EXP_CS5_OFFSET)
#define IXP4XX_EXP_CS6      IXP4XX_EXP_REG(IXP4XX_EXP_CS6_OFFSET)     
#define IXP4XX_EXP_CS7      IXP4XX_EXP_REG(IXP4XX_EXP_CS7_OFFSET)

#define IXP4XX_EXP_CFG0     IXP4XX_EXP_REG(IXP4XX_EXP_CFG0_OFFSET) 
#define IXP4XX_EXP_CFG1     IXP4XX_EXP_REG(IXP4XX_EXP_CFG1_OFFSET) 
#define IXP4XX_EXP_CFG2     IXP4XX_EXP_REG(IXP4XX_EXP_CFG2_OFFSET) 
#define IXP4XX_EXP_CFG3     IXP4XX_EXP_REG(IXP4XX_EXP_CFG3_OFFSET)


/*
 * Peripheral Space Register Region Base Addresses
 */
#define IXP4XX_UART1_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x0000)
#define IXP4XX_UART2_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x1000)
#define IXP4XX_PMU_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x2000)
#define IXP4XX_INTC_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x3000)
#define IXP4XX_GPIO_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x4000)
#define IXP4XX_TIMER_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x5000)
#define IXP4XX_NPEA_BASE_PHYS   	(IXP4XX_PERIPHERAL_BASE_PHYS + 0x6000)
#define IXP4XX_NPEB_BASE_PHYS   	(IXP4XX_PERIPHERAL_BASE_PHYS + 0x7000)
#define IXP4XX_NPEC_BASE_PHYS   	(IXP4XX_PERIPHERAL_BASE_PHYS + 0x8000)
#define IXP4XX_EthB_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x9000)
#define IXP4XX_EthC_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0xA000)
#define IXP4XX_USB_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0xB000)
/* ixp46X only */
#define IXP4XX_EthA_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0xC000)
#define IXP4XX_EthB1_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0xD000)
#define IXP4XX_EthB2_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0xE000)
#define IXP4XX_EthB3_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0xF000)
#define IXP4XX_TIMESYNC_BASE_PHYS	(IXP4XX_PERIPHERAL_BASE_PHYS + 0x10000)
#define IXP4XX_I2C_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x11000)
#define IXP4XX_SSP_BASE_PHYS		(IXP4XX_PERIPHERAL_BASE_PHYS + 0x12000)


#define IXP4XX_UART1_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x0000)
#define IXP4XX_UART2_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x1000)
#define IXP4XX_PMU_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x2000)
#define IXP4XX_INTC_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x3000)
#define IXP4XX_GPIO_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x4000)
#define IXP4XX_TIMER_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x5000)
#define IXP4XX_NPEA_BASE_VIRT   	(IXP4XX_PERIPHERAL_BASE_VIRT + 0x6000)
#define IXP4XX_NPEB_BASE_VIRT   	(IXP4XX_PERIPHERAL_BASE_VIRT + 0x7000)
#define IXP4XX_NPEC_BASE_VIRT   	(IXP4XX_PERIPHERAL_BASE_VIRT + 0x8000)
#define IXP4XX_EthB_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x9000)
#define IXP4XX_EthC_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0xA000)
#define IXP4XX_USB_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0xB000)
/* ixp46X only */
#define IXP4XX_EthA_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0xC000)
#define IXP4XX_EthB1_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0xD000)
#define IXP4XX_EthB2_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0xE000)
#define IXP4XX_EthB3_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0xF000)
#define IXP4XX_TIMESYNC_BASE_VIRT	(IXP4XX_PERIPHERAL_BASE_VIRT + 0x10000)
#define IXP4XX_I2C_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x11000)
#define IXP4XX_SSP_BASE_VIRT		(IXP4XX_PERIPHERAL_BASE_VIRT + 0x12000)

/*
 * Constants to make it easy to access  Interrupt Controller registers
 */
#define IXP4XX_ICPR_OFFSET	0x00 /* Interrupt Status */
#define IXP4XX_ICMR_OFFSET	0x04 /* Interrupt Enable */
#define IXP4XX_ICLR_OFFSET	0x08 /* Interrupt IRQ/FIQ Select */
#define IXP4XX_ICIP_OFFSET      0x0C /* IRQ Status */
#define IXP4XX_ICFP_OFFSET	0x10 /* FIQ Status */
#define IXP4XX_ICHR_OFFSET	0x14 /* Interrupt Priority */
#define IXP4XX_ICIH_OFFSET	0x18 /* IRQ Highest Pri Int */
#define IXP4XX_ICFH_OFFSET	0x1C /* FIQ Highest Pri Int */

/*
 * IXP465-only
 */
#define	IXP4XX_ICPR2_OFFSET	0x20 /* Interrupt Status 2 */
#define	IXP4XX_ICMR2_OFFSET	0x24 /* Interrupt Enable 2 */
#define	IXP4XX_ICLR2_OFFSET	0x28 /* Interrupt IRQ/FIQ Select 2 */
#define IXP4XX_ICIP2_OFFSET     0x2C /* IRQ Status */
#define IXP4XX_ICFP2_OFFSET	0x30 /* FIQ Status */
#define IXP4XX_ICEEN_OFFSET	0x34 /* Error High Pri Enable */


/*
 * Interrupt Controller Register Definitions.
 */

#define IXP4XX_INTC_REG(x) ((volatile u32 *)(IXP4XX_INTC_BASE_VIRT+(x)))

#define IXP4XX_ICPR	IXP4XX_INTC_REG(IXP4XX_ICPR_OFFSET)
#define IXP4XX_ICMR     IXP4XX_INTC_REG(IXP4XX_ICMR_OFFSET)
#define IXP4XX_ICLR     IXP4XX_INTC_REG(IXP4XX_ICLR_OFFSET)
#define IXP4XX_ICIP     IXP4XX_INTC_REG(IXP4XX_ICIP_OFFSET)
#define IXP4XX_ICFP     IXP4XX_INTC_REG(IXP4XX_ICFP_OFFSET)
#define IXP4XX_ICHR     IXP4XX_INTC_REG(IXP4XX_ICHR_OFFSET)
#define IXP4XX_ICIH     IXP4XX_INTC_REG(IXP4XX_ICIH_OFFSET) 
#define IXP4XX_ICFH     IXP4XX_INTC_REG(IXP4XX_ICFH_OFFSET)
#define IXP4XX_ICPR2	IXP4XX_INTC_REG(IXP4XX_ICPR2_OFFSET)
#define IXP4XX_ICMR2    IXP4XX_INTC_REG(IXP4XX_ICMR2_OFFSET)
#define IXP4XX_ICLR2    IXP4XX_INTC_REG(IXP4XX_ICLR2_OFFSET)
#define IXP4XX_ICIP2    IXP4XX_INTC_REG(IXP4XX_ICIP2_OFFSET)
#define IXP4XX_ICFP2    IXP4XX_INTC_REG(IXP4XX_ICFP2_OFFSET)
#define IXP4XX_ICEEN    IXP4XX_INTC_REG(IXP4XX_ICEEN_OFFSET)
                                                                                
/*
 * Constants to make it easy to access GPIO registers
 */
#define IXP4XX_GPIO_GPOUTR_OFFSET       0x00
#define IXP4XX_GPIO_GPOER_OFFSET        0x04
#define IXP4XX_GPIO_GPINR_OFFSET        0x08
#define IXP4XX_GPIO_GPISR_OFFSET        0x0C
#define IXP4XX_GPIO_GPIT1R_OFFSET	0x10
#define IXP4XX_GPIO_GPIT2R_OFFSET	0x14
#define IXP4XX_GPIO_GPCLKR_OFFSET	0x18
#define IXP4XX_GPIO_GPDBSELR_OFFSET	0x1C

/* 
 * GPIO Register Definitions.
 * [Only perform 32bit reads/writes]
 */
#define IXP4XX_GPIO_REG(x) ((volatile u32 *)(IXP4XX_GPIO_BASE_VIRT+(x)))

#define IXP4XX_GPIO_GPOUTR	IXP4XX_GPIO_REG(IXP4XX_GPIO_GPOUTR_OFFSET)
#define IXP4XX_GPIO_GPOER       IXP4XX_GPIO_REG(IXP4XX_GPIO_GPOER_OFFSET)
#define IXP4XX_GPIO_GPINR       IXP4XX_GPIO_REG(IXP4XX_GPIO_GPINR_OFFSET)
#define IXP4XX_GPIO_GPISR       IXP4XX_GPIO_REG(IXP4XX_GPIO_GPISR_OFFSET)
#define IXP4XX_GPIO_GPIT1R      IXP4XX_GPIO_REG(IXP4XX_GPIO_GPIT1R_OFFSET)
#define IXP4XX_GPIO_GPIT2R      IXP4XX_GPIO_REG(IXP4XX_GPIO_GPIT2R_OFFSET)
#define IXP4XX_GPIO_GPCLKR      IXP4XX_GPIO_REG(IXP4XX_GPIO_GPCLKR_OFFSET)
#define IXP4XX_GPIO_GPDBSELR    IXP4XX_GPIO_REG(IXP4XX_GPIO_GPDBSELR_OFFSET)

/*
 * GPIO register bit definitions
 */

/* Interrupt styles
 */
#define IXP4XX_GPIO_STYLE_ACTIVE_HIGH	0x0
#define IXP4XX_GPIO_STYLE_ACTIVE_LOW	0x1
#define IXP4XX_GPIO_STYLE_RISING_EDGE	0x2
#define IXP4XX_GPIO_STYLE_FALLING_EDGE	0x3
#define IXP4XX_GPIO_STYLE_TRANSITIONAL	0x4

/* 
 * Mask used to clear interrupt styles 
 */
#define IXP4XX_GPIO_STYLE_CLEAR		0x7
#define IXP4XX_GPIO_STYLE_SIZE		3

/*
 * Constants to make it easy to access Timer Control/Status registers
 */
#define IXP4XX_OSTS_OFFSET	0x00  /* Continious TimeStamp */
#define IXP4XX_OST1_OFFSET	0x04  /* Timer 1 Timestamp */
#define IXP4XX_OSRT1_OFFSET	0x08  /* Timer 1 Reload */
#define IXP4XX_OST2_OFFSET	0x0C  /* Timer 2 Timestamp */
#define IXP4XX_OSRT2_OFFSET	0x10  /* Timer 2 Reload */
#define IXP4XX_OSWT_OFFSET	0x14  /* Watchdog Timer */
#define IXP4XX_OSWE_OFFSET	0x18  /* Watchdog Enable */
#define IXP4XX_OSWK_OFFSET	0x1C  /* Watchdog Key */
#define IXP4XX_OSST_OFFSET	0x20  /* Timer Status */

/*
 * Operating System Timer Register Definitions.
 */

#define IXP4XX_TIMER_REG(x) ((volatile u32 *)(IXP4XX_TIMER_BASE_VIRT+(x)))

#define IXP4XX_OSTS	IXP4XX_TIMER_REG(IXP4XX_OSTS_OFFSET)
#define IXP4XX_OST1	IXP4XX_TIMER_REG(IXP4XX_OST1_OFFSET)
#define IXP4XX_OSRT1	IXP4XX_TIMER_REG(IXP4XX_OSRT1_OFFSET)
#define IXP4XX_OST2	IXP4XX_TIMER_REG(IXP4XX_OST2_OFFSET)
#define IXP4XX_OSRT2	IXP4XX_TIMER_REG(IXP4XX_OSRT2_OFFSET)
#define IXP4XX_OSWT	IXP4XX_TIMER_REG(IXP4XX_OSWT_OFFSET)
#define IXP4XX_OSWE	IXP4XX_TIMER_REG(IXP4XX_OSWE_OFFSET)
#define IXP4XX_OSWK	IXP4XX_TIMER_REG(IXP4XX_OSWK_OFFSET)
#define IXP4XX_OSST	IXP4XX_TIMER_REG(IXP4XX_OSST_OFFSET)

/*
 * Timer register values and bit definitions 
 */
#define IXP4XX_OST_ENABLE		0x00000001
#define IXP4XX_OST_ONE_SHOT		0x00000002
/* Low order bits of reload value ignored */
#define IXP4XX_OST_RELOAD_MASK		0x00000003
#define IXP4XX_OST_DISABLED		0x00000000
#define IXP4XX_OSST_TIMER_1_PEND	0x00000001
#define IXP4XX_OSST_TIMER_2_PEND	0x00000002
#define IXP4XX_OSST_TIMER_TS_PEND	0x00000004
#define IXP4XX_OSST_TIMER_WDOG_PEND	0x00000008
#define IXP4XX_OSST_TIMER_WARM_RESET	0x00000010

#define	IXP4XX_WDT_KEY			0x0000482E

#define	IXP4XX_WDT_RESET_ENABLE		0x00000001
#define	IXP4XX_WDT_IRQ_ENABLE		0x00000002
#define	IXP4XX_WDT_COUNT_ENABLE		0x00000004


/*
 * Constants to make it easy to access PCI Control/Status registers
 */
#define PCI_NP_AD_OFFSET            0x00
#define PCI_NP_CBE_OFFSET           0x04
#define PCI_NP_WDATA_OFFSET         0x08
#define PCI_NP_RDATA_OFFSET         0x0c
#define PCI_CRP_AD_CBE_OFFSET       0x10
#define PCI_CRP_WDATA_OFFSET        0x14
#define PCI_CRP_RDATA_OFFSET        0x18
#define PCI_CSR_OFFSET              0x1c
#define PCI_ISR_OFFSET              0x20
#define PCI_INTEN_OFFSET            0x24
#define PCI_DMACTRL_OFFSET          0x28
#define PCI_AHBMEMBASE_OFFSET       0x2c
#define PCI_AHBIOBASE_OFFSET        0x30
#define PCI_PCIMEMBASE_OFFSET       0x34
#define PCI_AHBDOORBELL_OFFSET      0x38
#define PCI_PCIDOORBELL_OFFSET      0x3C
#define PCI_ATPDMA0_AHBADDR_OFFSET  0x40
#define PCI_ATPDMA0_PCIADDR_OFFSET  0x44
#define PCI_ATPDMA0_LENADDR_OFFSET  0x48
#define PCI_ATPDMA1_AHBADDR_OFFSET  0x4C
#define PCI_ATPDMA1_PCIADDR_OFFSET  0x50
#define PCI_ATPDMA1_LENADDR_OFFSET	0x54

/*
 * PCI Control/Status Registers
 */
#define IXP4XX_PCI_CSR(x) ((volatile u32 *)(IXP4XX_PCI_CFG_BASE_VIRT+(x)))

#define PCI_NP_AD               IXP4XX_PCI_CSR(PCI_NP_AD_OFFSET)
#define PCI_NP_CBE              IXP4XX_PCI_CSR(PCI_NP_CBE_OFFSET)
#define PCI_NP_WDATA            IXP4XX_PCI_CSR(PCI_NP_WDATA_OFFSET)
#define PCI_NP_RDATA            IXP4XX_PCI_CSR(PCI_NP_RDATA_OFFSET)
#define PCI_CRP_AD_CBE          IXP4XX_PCI_CSR(PCI_CRP_AD_CBE_OFFSET)
#define PCI_CRP_WDATA           IXP4XX_PCI_CSR(PCI_CRP_WDATA_OFFSET)
#define PCI_CRP_RDATA           IXP4XX_PCI_CSR(PCI_CRP_RDATA_OFFSET)
#define PCI_CSR                 IXP4XX_PCI_CSR(PCI_CSR_OFFSET) 
#define PCI_ISR                 IXP4XX_PCI_CSR(PCI_ISR_OFFSET)
#define PCI_INTEN               IXP4XX_PCI_CSR(PCI_INTEN_OFFSET)
#define PCI_DMACTRL             IXP4XX_PCI_CSR(PCI_DMACTRL_OFFSET)
#define PCI_AHBMEMBASE          IXP4XX_PCI_CSR(PCI_AHBMEMBASE_OFFSET)
#define PCI_AHBIOBASE           IXP4XX_PCI_CSR(PCI_AHBIOBASE_OFFSET)
#define PCI_PCIMEMBASE          IXP4XX_PCI_CSR(PCI_PCIMEMBASE_OFFSET)
#define PCI_AHBDOORBELL         IXP4XX_PCI_CSR(PCI_AHBDOORBELL_OFFSET)
#define PCI_PCIDOORBELL         IXP4XX_PCI_CSR(PCI_PCIDOORBELL_OFFSET)
#define PCI_ATPDMA0_AHBADDR     IXP4XX_PCI_CSR(PCI_ATPDMA0_AHBADDR_OFFSET)
#define PCI_ATPDMA0_PCIADDR     IXP4XX_PCI_CSR(PCI_ATPDMA0_PCIADDR_OFFSET)
#define PCI_ATPDMA0_LENADDR     IXP4XX_PCI_CSR(PCI_ATPDMA0_LENADDR_OFFSET)
#define PCI_ATPDMA1_AHBADDR     IXP4XX_PCI_CSR(PCI_ATPDMA1_AHBADDR_OFFSET)
#define PCI_ATPDMA1_PCIADDR     IXP4XX_PCI_CSR(PCI_ATPDMA1_PCIADDR_OFFSET)
#define PCI_ATPDMA1_LENADDR     IXP4XX_PCI_CSR(PCI_ATPDMA1_LENADDR_OFFSET)

/*
 * PCI register values and bit definitions 
 */

/* CSR bit definitions */
#define PCI_CSR_HOST    	0x00000001
#define PCI_CSR_ARBEN   	0x00000002
#define PCI_CSR_ADS     	0x00000004
#define PCI_CSR_PDS     	0x00000008
#define PCI_CSR_ABE     	0x00000010
#define PCI_CSR_DBT     	0x00000020
#define PCI_CSR_ASE     	0x00000100
#define PCI_CSR_IC      	0x00008000

/* ISR (Interrupt status) Register bit definitions */
#define PCI_ISR_PSE     	0x00000001
#define PCI_ISR_PFE     	0x00000002
#define PCI_ISR_PPE     	0x00000004
#define PCI_ISR_AHBE    	0x00000008
#define PCI_ISR_APDC    	0x00000010
#define PCI_ISR_PADC    	0x00000020
#define PCI_ISR_ADB     	0x00000040
#define PCI_ISR_PDB     	0x00000080

/* INTEN (Interrupt Enable) Register bit definitions */
#define PCI_INTEN_PSE   	0x00000001
#define PCI_INTEN_PFE   	0x00000002
#define PCI_INTEN_PPE   	0x00000004
#define PCI_INTEN_AHBE  	0x00000008
#define PCI_INTEN_APDC  	0x00000010
#define PCI_INTEN_PADC  	0x00000020
#define PCI_INTEN_ADB   	0x00000040
#define PCI_INTEN_PDB   	0x00000080

/*
 * Shift value for byte enable on NP cmd/byte enable register
 */
#define IXP4XX_PCI_NP_CBE_BESL		4

/*
 * PCI commands supported by NP access unit
 */
#define NP_CMD_IOREAD			0x2
#define NP_CMD_IOWRITE			0x3
#define NP_CMD_CONFIGREAD		0xa
#define NP_CMD_CONFIGWRITE		0xb
#define NP_CMD_MEMREAD			0x6
#define	NP_CMD_MEMWRITE			0x7

/*
 * Constants for CRP access into local config space
 */
#define CRP_AD_CBE_BESL         20
#define CRP_AD_CBE_WRITE	0x00010000


/*
 * USB Device Controller
 *
 * These are used by the USB gadget driver, so they don't follow the
 * IXP4XX_ naming convetions.
 *
 */
# define IXP4XX_USB_REG(x)       (*((volatile u32 *)(x)))

/* UDC Undocumented - Reserved1 */
#define UDC_RES1	IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0004)  
/* UDC Undocumented - Reserved2 */
#define UDC_RES2	IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0008)  
/* UDC Undocumented - Reserved3 */
#define UDC_RES3	IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x000C)  
/* UDC Control Register */
#define UDCCR		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0000)  
/* UDC Endpoint 0 Control/Status Register */
#define UDCCS0		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0010)  
/* UDC Endpoint 1 (IN) Control/Status Register */
#define UDCCS1		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0014)  
/* UDC Endpoint 2 (OUT) Control/Status Register */
#define UDCCS2		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0018)  
/* UDC Endpoint 3 (IN) Control/Status Register */
#define UDCCS3		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x001C)  
/* UDC Endpoint 4 (OUT) Control/Status Register */
#define UDCCS4		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0020)  
/* UDC Endpoint 5 (Interrupt) Control/Status Register */
#define UDCCS5		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0024)  
/* UDC Endpoint 6 (IN) Control/Status Register */
#define UDCCS6		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0028)  
/* UDC Endpoint 7 (OUT) Control/Status Register */
#define UDCCS7		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x002C)  
/* UDC Endpoint 8 (IN) Control/Status Register */
#define UDCCS8		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0030)  
/* UDC Endpoint 9 (OUT) Control/Status Register */
#define UDCCS9		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0034)  
/* UDC Endpoint 10 (Interrupt) Control/Status Register */
#define UDCCS10		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0038)  
/* UDC Endpoint 11 (IN) Control/Status Register */
#define UDCCS11		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x003C)  
/* UDC Endpoint 12 (OUT) Control/Status Register */
#define UDCCS12		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0040)  
/* UDC Endpoint 13 (IN) Control/Status Register */
#define UDCCS13		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0044)  
/* UDC Endpoint 14 (OUT) Control/Status Register */
#define UDCCS14		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0048)  
/* UDC Endpoint 15 (Interrupt) Control/Status Register */
#define UDCCS15		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x004C)  
/* UDC Frame Number Register High */
#define UFNRH		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0060)  
/* UDC Frame Number Register Low */
#define UFNRL		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0064)  
/* UDC Byte Count Reg 2 */
#define UBCR2		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0068)  
/* UDC Byte Count Reg 4 */
#define UBCR4		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x006c)  
/* UDC Byte Count Reg 7 */
#define UBCR7		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0070)  
/* UDC Byte Count Reg 9 */
#define UBCR9		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0074)  
/* UDC Byte Count Reg 12 */
#define UBCR12		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0078)  
/* UDC Byte Count Reg 14 */
#define UBCR14		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x007c)  
/* UDC Endpoint 0 Data Register */
#define UDDR0		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0080)  
/* UDC Endpoint 1 Data Register */
#define UDDR1		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0100)  
/* UDC Endpoint 2 Data Register */
#define UDDR2		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0180)  
/* UDC Endpoint 3 Data Register */
#define UDDR3		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0200)  
/* UDC Endpoint 4 Data Register */
#define UDDR4		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0400)  
/* UDC Endpoint 5 Data Register */
#define UDDR5		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x00A0)  
/* UDC Endpoint 6 Data Register */
#define UDDR6		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0600)  
/* UDC Endpoint 7 Data Register */
#define UDDR7		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0680)  
/* UDC Endpoint 8 Data Register */
#define UDDR8		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0700)  
/* UDC Endpoint 9 Data Register */
#define UDDR9		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0900)  
/* UDC Endpoint 10 Data Register */
#define UDDR10		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x00C0)  
/* UDC Endpoint 11 Data Register */
#define UDDR11		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0B00)  
/* UDC Endpoint 12 Data Register */
#define UDDR12		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0B80)  
/* UDC Endpoint 13 Data Register */
#define UDDR13		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0C00)  
/* UDC Endpoint 14 Data Register */
#define UDDR14		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0E00)  
/* UDC Endpoint 15 Data Register */
#define UDDR15		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x00E0)  
/* UDC Interrupt Control Register 0 */
#define UICR0		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0050)  
/* UDC Interrupt Control Register 1 */
#define UICR1		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0054)  
/* UDC Status Interrupt Register 0 */
#define USIR0		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x0058)  
/* UDC Status Interrupt Register 1 */
#define USIR1		IXP4XX_USB_REG(IXP4XX_USB_BASE_VIRT+0x005C)  

#define UDCCR_UDE	(1 << 0)	/* UDC enable */
#define UDCCR_UDA	(1 << 1)	/* UDC active */
#define UDCCR_RSM	(1 << 2)	/* Device resume */
#define UDCCR_RESIR	(1 << 3)	/* Resume interrupt request */
#define UDCCR_SUSIR	(1 << 4)	/* Suspend interrupt request */
#define UDCCR_SRM	(1 << 5)	/* Suspend/resume interrupt mask */
#define UDCCR_RSTIR	(1 << 6)	/* Reset interrupt request */
#define UDCCR_REM	(1 << 7)	/* Reset interrupt mask */

#define UDCCS0_OPR	(1 << 0)	/* OUT packet ready */
#define UDCCS0_IPR	(1 << 1)	/* IN packet ready */
#define UDCCS0_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS0_DRWF	(1 << 3)	/* Device remote wakeup feature */
#define UDCCS0_SST	(1 << 4)	/* Sent stall */
#define UDCCS0_FST	(1 << 5)	/* Force stall */
#define UDCCS0_RNE	(1 << 6)	/* Receive FIFO no empty */
#define UDCCS0_SA	(1 << 7)	/* Setup active */

#define UDCCS_BI_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_BI_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_BI_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_BI_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_BI_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BI_FST	(1 << 5)	/* Force stall */
#define UDCCS_BI_TSP	(1 << 7)	/* Transmit short packet */

#define UDCCS_BO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_BO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_BO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_BO_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BO_FST	(1 << 5)	/* Force stall */
#define UDCCS_BO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_BO_RSP	(1 << 7)	/* Receive short packet */

#define UDCCS_II_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_II_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_II_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_II_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_II_TSP	(1 << 7)	/* Transmit short packet */

#define UDCCS_IO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_IO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_IO_ROF	(1 << 3)	/* Receive overflow */
#define UDCCS_IO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_IO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_IO_RSP	(1 << 7)	/* Receive short packet */

#define UDCCS_INT_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_INT_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_INT_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_INT_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_INT_SST	(1 << 4)	/* Sent stall */
#define UDCCS_INT_FST	(1 << 5)	/* Force stall */
#define UDCCS_INT_TSP	(1 << 7)	/* Transmit short packet */

#define UICR0_IM0	(1 << 0)	/* Interrupt mask ep 0 */
#define UICR0_IM1	(1 << 1)	/* Interrupt mask ep 1 */
#define UICR0_IM2	(1 << 2)	/* Interrupt mask ep 2 */
#define UICR0_IM3	(1 << 3)	/* Interrupt mask ep 3 */
#define UICR0_IM4	(1 << 4)	/* Interrupt mask ep 4 */
#define UICR0_IM5	(1 << 5)	/* Interrupt mask ep 5 */
#define UICR0_IM6	(1 << 6)	/* Interrupt mask ep 6 */
#define UICR0_IM7	(1 << 7)	/* Interrupt mask ep 7 */

#define UICR1_IM8	(1 << 0)	/* Interrupt mask ep 8 */
#define UICR1_IM9	(1 << 1)	/* Interrupt mask ep 9 */
#define UICR1_IM10	(1 << 2)	/* Interrupt mask ep 10 */
#define UICR1_IM11	(1 << 3)	/* Interrupt mask ep 11 */
#define UICR1_IM12	(1 << 4)	/* Interrupt mask ep 12 */
#define UICR1_IM13	(1 << 5)	/* Interrupt mask ep 13 */
#define UICR1_IM14	(1 << 6)	/* Interrupt mask ep 14 */
#define UICR1_IM15	(1 << 7)	/* Interrupt mask ep 15 */

#define USIR0_IR0	(1 << 0)	/* Interrupt request ep 0 */
#define USIR0_IR1	(1 << 1)	/* Interrupt request ep 1 */
#define USIR0_IR2	(1 << 2)	/* Interrupt request ep 2 */
#define USIR0_IR3	(1 << 3)	/* Interrupt request ep 3 */
#define USIR0_IR4	(1 << 4)	/* Interrupt request ep 4 */
#define USIR0_IR5	(1 << 5)	/* Interrupt request ep 5 */
#define USIR0_IR6	(1 << 6)	/* Interrupt request ep 6 */
#define USIR0_IR7	(1 << 7)	/* Interrupt request ep 7 */

#define USIR1_IR8	(1 << 0)	/* Interrupt request ep 8 */
#define USIR1_IR9	(1 << 1)	/* Interrupt request ep 9 */
#define USIR1_IR10	(1 << 2)	/* Interrupt request ep 10 */
#define USIR1_IR11	(1 << 3)	/* Interrupt request ep 11 */
#define USIR1_IR12	(1 << 4)	/* Interrupt request ep 12 */
#define USIR1_IR13	(1 << 5)	/* Interrupt request ep 13 */
#define USIR1_IR14	(1 << 6)	/* Interrupt request ep 14 */
#define USIR1_IR15	(1 << 7)	/* Interrupt request ep 15 */

#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */

/* "fuse" bits of IXP_EXP_CFG2 */
/* All IXP4xx CPUs */
#define IXP4XX_FEATURE_RCOMP		(1 << 0)
#define IXP4XX_FEATURE_USB_DEVICE	(1 << 1)
#define IXP4XX_FEATURE_HASH		(1 << 2)
#define IXP4XX_FEATURE_AES		(1 << 3)
#define IXP4XX_FEATURE_DES		(1 << 4)
#define IXP4XX_FEATURE_HDLC		(1 << 5)
#define IXP4XX_FEATURE_AAL		(1 << 6)
#define IXP4XX_FEATURE_HSS		(1 << 7)
#define IXP4XX_FEATURE_UTOPIA		(1 << 8)
#define IXP4XX_FEATURE_NPEB_ETH0	(1 << 9)
#define IXP4XX_FEATURE_NPEC_ETH		(1 << 10)
#define IXP4XX_FEATURE_RESET_NPEA	(1 << 11)
#define IXP4XX_FEATURE_RESET_NPEB	(1 << 12)
#define IXP4XX_FEATURE_RESET_NPEC	(1 << 13)
#define IXP4XX_FEATURE_PCI		(1 << 14)
#define IXP4XX_FEATURE_UTOPIA_PHY_LIMIT	(3 << 16)
#define IXP4XX_FEATURE_XSCALE_MAX_FREQ	(3 << 22)
#define IXP42X_FEATURE_MASK		(IXP4XX_FEATURE_RCOMP            | \
					 IXP4XX_FEATURE_USB_DEVICE       | \
					 IXP4XX_FEATURE_HASH             | \
					 IXP4XX_FEATURE_AES              | \
					 IXP4XX_FEATURE_DES              | \
					 IXP4XX_FEATURE_HDLC             | \
					 IXP4XX_FEATURE_AAL              | \
					 IXP4XX_FEATURE_HSS              | \
					 IXP4XX_FEATURE_UTOPIA           | \
					 IXP4XX_FEATURE_NPEB_ETH0        | \
					 IXP4XX_FEATURE_NPEC_ETH         | \
					 IXP4XX_FEATURE_RESET_NPEA       | \
					 IXP4XX_FEATURE_RESET_NPEB       | \
					 IXP4XX_FEATURE_RESET_NPEC       | \
					 IXP4XX_FEATURE_PCI              | \
					 IXP4XX_FEATURE_UTOPIA_PHY_LIMIT | \
					 IXP4XX_FEATURE_XSCALE_MAX_FREQ)


/* IXP43x/46x CPUs */
#define IXP4XX_FEATURE_ECC_TIMESYNC	(1 << 15)
#define IXP4XX_FEATURE_USB_HOST		(1 << 18)
#define IXP4XX_FEATURE_NPEA_ETH		(1 << 19)
#define IXP43X_FEATURE_MASK		(IXP42X_FEATURE_MASK             | \
					 IXP4XX_FEATURE_ECC_TIMESYNC     | \
					 IXP4XX_FEATURE_USB_HOST         | \
					 IXP4XX_FEATURE_NPEA_ETH)

/* IXP46x CPU (including IXP455) only */
#define IXP4XX_FEATURE_NPEB_ETH_1_TO_3	(1 << 20)
#define IXP4XX_FEATURE_RSA		(1 << 21)
#define IXP46X_FEATURE_MASK		(IXP43X_FEATURE_MASK             | \
					 IXP4XX_FEATURE_NPEB_ETH_1_TO_3  | \
					 IXP4XX_FEATURE_RSA)

#endif
