/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX27ADS_H__
#define __ASM_ARCH_MXC_BOARD_MX27ADS_H__

/* external interrupt multiplexer */
#define MXC_EXP_IO_BASE		(MXC_BOARD_IRQ_START)

#define MXC_VIRTUAL_INTS_BASE	(MXC_EXP_IO_BASE + MXC_MAX_EXP_IO_LINES)
#define MXC_SDIO1_CARD_IRQ	MXC_VIRTUAL_INTS_BASE
#define MXC_SDIO2_CARD_IRQ	(MXC_VIRTUAL_INTS_BASE + 1)
#define MXC_SDIO3_CARD_IRQ	(MXC_VIRTUAL_INTS_BASE + 2)

#define MXC_MAX_BOARD_INTS      (MXC_MAX_EXP_IO_LINES + \
				MXC_MAX_VIRTUAL_INTS)

/*
 * MXC UART EVB board level configurations
 */
#define MXC_LL_UART_PADDR       UART1_BASE_ADDR
#define MXC_LL_UART_VADDR       AIPI_IO_ADDRESS(UART1_BASE_ADDR)

/*
 * @name Memory Size parameters
 */

/*
 * Size of SDRAM memory
 */
#define SDRAM_MEM_SIZE          SZ_128M

/*
 * PBC Controller parameters
 */

/*
 * Base address of PBC controller, CS4
 */
#define PBC_BASE_ADDRESS        0xEB000000
#define PBC_REG_ADDR(offset)    (PBC_BASE_ADDRESS + (offset))

/*
 * PBC Interupt name definitions
 */
#define PBC_GPIO1_0  0
#define PBC_GPIO1_1  1
#define PBC_GPIO1_2  2
#define PBC_GPIO1_3  3
#define PBC_GPIO1_4  4
#define PBC_GPIO1_5  5

#define PBC_INTR_MAX_NUM 6
#define PBC_INTR_SHARED_MAX_NUM 8

/* When the PBC address connection is fixed in h/w, defined as 1 */
#define PBC_ADDR_SH             0

/* Offsets for the PBC Controller register */
/*
 * PBC Board version register offset
 */
#define PBC_VERSION_REG         PBC_REG_ADDR(0x00000 >> PBC_ADDR_SH)
/*
 * PBC Board control register 1 set address.
 */
#define PBC_BCTRL1_SET_REG      PBC_REG_ADDR(0x00008 >> PBC_ADDR_SH)
/*
 * PBC Board control register 1 clear address.
 */
#define PBC_BCTRL1_CLEAR_REG    PBC_REG_ADDR(0x0000C >> PBC_ADDR_SH)
/*
 * PBC Board control register 2 set address.
 */
#define PBC_BCTRL2_SET_REG      PBC_REG_ADDR(0x00010 >> PBC_ADDR_SH)
/*
 * PBC Board control register 2 clear address.
 */
#define PBC_BCTRL2_CLEAR_REG    PBC_REG_ADDR(0x00014 >> PBC_ADDR_SH)
/*
 * PBC Board control register 3 set address.
 */
#define PBC_BCTRL3_SET_REG      PBC_REG_ADDR(0x00018 >> PBC_ADDR_SH)
/*
 * PBC Board control register 3 clear address.
 */
#define PBC_BCTRL3_CLEAR_REG    PBC_REG_ADDR(0x0001C >> PBC_ADDR_SH)
/*
 * PBC Board control register 3 set address.
 */
#define PBC_BCTRL4_SET_REG      PBC_REG_ADDR(0x00020 >> PBC_ADDR_SH)
/*
 * PBC Board control register 4 clear address.
 */
#define PBC_BCTRL4_CLEAR_REG    PBC_REG_ADDR(0x00024 >> PBC_ADDR_SH)
/*PBC_ADDR_SH
 * PBC Board status register 1.
 */
#define PBC_BSTAT1_REG          PBC_REG_ADDR(0x00028 >> PBC_ADDR_SH)
/*
 * PBC Board interrupt status register.
 */
#define PBC_INTSTATUS_REG       PBC_REG_ADDR(0x0002C >> PBC_ADDR_SH)
/*
 * PBC Board interrupt current status register.
 */
#define PBC_INTCURR_STATUS_REG  PBC_REG_ADDR(0x00034 >> PBC_ADDR_SH)
/*
 * PBC Interrupt mask register set address.
 */
#define PBC_INTMASK_SET_REG     PBC_REG_ADDR(0x00038 >> PBC_ADDR_SH)
/*
 * PBC Interrupt mask register clear address.
 */
#define PBC_INTMASK_CLEAR_REG   PBC_REG_ADDR(0x0003C >> PBC_ADDR_SH)
/*
 * External UART A.
 */
#define PBC_SC16C652_UARTA_REG  PBC_REG_ADDR(0x20000 >> PBC_ADDR_SH)
/*
 * UART 4 Expanding Signal Status.
 */
#define PBC_UART_STATUS_REG     PBC_REG_ADDR(0x22000 >> PBC_ADDR_SH)
/*
 * UART 4 Expanding Signal Control Set.
 */
#define PBC_UCTRL_SET_REG       PBC_REG_ADDR(0x24000 >> PBC_ADDR_SH)
/*
 * UART 4 Expanding Signal Control Clear.
 */
#define PBC_UCTRL_CLR_REG       PBC_REG_ADDR(0x26000 >> PBC_ADDR_SH)
/*
 * Ethernet Controller IO base address.
 */
#define PBC_CS8900A_IOBASE_REG  PBC_REG_ADDR(0x40000 >> PBC_ADDR_SH)
/*
 * Ethernet Controller Memory base address.
 */
#define PBC_CS8900A_MEMBASE_REG PBC_REG_ADDR(0x42000 >> PBC_ADDR_SH)
/*
 * Ethernet Controller DMA base address.
 */
#define PBC_CS8900A_DMABASE_REG PBC_REG_ADDR(0x44000 >> PBC_ADDR_SH)

/* PBC Board Version Register bit definition */
#define PBC_VERSION_ADS         0x8000	/* Bit15=1 means version for ads */
#define PBC_VERSION_EVB_REVB    0x4000	/* BIT14=1 means version for evb revb */

/* PBC Board Control Register 1 bit definitions */
#define PBC_BCTRL1_ERST         0x0001	/* Ethernet Reset */
#define PBC_BCTRL1_URST         0x0002	/* Reset External UART controller */
#define PBC_BCTRL1_FRST         0x0004	/* FEC Reset */
#define PBC_BCTRL1_ESLEEP       0x0010	/* Enable ethernet Sleep */
#define PBC_BCTRL1_LCDON        0x0800	/* Enable the LCD */

/* PBC Board Control Register 2 bit definitions */
#define PBC_BCTRL2_VCC_EN       0x0004	/*   Enable VCC */
#define PBC_BCTRL2_VPP_EN       0x0008	/*   Enable Vpp */
#define PBC_BCTRL2_ATAFEC_EN    0X0010
#define PBC_BCTRL2_ATAFEC_SEL   0X0020
#define PBC_BCTRL2_ATA_EN       0X0040
#define PBC_BCTRL2_IRDA_SD      0X0080
#define PBC_BCTRL2_IRDA_EN      0X0100
#define PBC_BCTRL2_CCTL10       0X0200
#define PBC_BCTRL2_CCTL11       0X0400

/* PBC Board Control Register 3 bit definitions */
#define PBC_BCTRL3_HSH_EN       0X0020
#define PBC_BCTRL3_FSH_MOD      0X0040
#define PBC_BCTRL3_OTG_HS_EN    0X0080
#define PBC_BCTRL3_OTG_VBUS_EN  0X0100
#define PBC_BCTRL3_FSH_VBUS_EN  0X0200
#define PBC_BCTRL3_USB_OTG_ON   0X0800
#define PBC_BCTRL3_USB_FSH_ON   0X1000

/* PBC Board Control Register 4 bit definitions */
#define PBC_BCTRL4_REGEN_SEL    0X0001
#define PBC_BCTRL4_USER_OFF     0X0002
#define PBC_BCTRL4_VIB_EN       0X0004
#define PBC_BCTRL4_PWRGT1_EN    0X0008
#define PBC_BCTRL4_PWRGT2_EN    0X0010
#define PBC_BCTRL4_STDBY_PRI    0X0020

#ifndef __ASSEMBLY__
/*
 * Enumerations for SD cards and memory stick card. This corresponds to
 * the card EN bits in the IMR: SD1_EN | MS_EN | SD3_EN | SD2_EN.
 */
enum mxc_card_no {
	MXC_CARD_SD2 = 0,
	MXC_CARD_SD3,
	MXC_CARD_MS,
	MXC_CARD_SD1,
	MXC_CARD_MIN = MXC_CARD_SD2,
	MXC_CARD_MAX = MXC_CARD_SD1,
};
#endif

#define MXC_CPLD_VER_1_50       0x01

/*
 * PBC BSTAT Register bit definitions
 */
#define PBC_BSTAT_PRI_INT       0X0001
#define PBC_BSTAT_USB_BYP       0X0002
#define PBC_BSTAT_ATA_IOCS16    0X0004
#define PBC_BSTAT_ATA_CBLID     0X0008
#define PBC_BSTAT_ATA_DASP      0X0010
#define PBC_BSTAT_PWR_RDY       0X0020
#define PBC_BSTAT_SD3_WP        0X0100
#define PBC_BSTAT_SD2_WP        0X0200
#define PBC_BSTAT_SD1_WP        0X0400
#define PBC_BSTAT_SD3_DET       0X0800
#define PBC_BSTAT_SD2_DET       0X1000
#define PBC_BSTAT_SD1_DET       0X2000
#define PBC_BSTAT_MS_DET        0X4000
#define PBC_BSTAT_SD3_DET_BIT   11
#define PBC_BSTAT_SD2_DET_BIT   12
#define PBC_BSTAT_SD1_DET_BIT   13
#define PBC_BSTAT_MS_DET_BIT    14
#define MXC_BSTAT_BIT(n)        ((n == MXC_CARD_SD2) ? PBC_BSTAT_SD2_DET : \
				 ((n == MXC_CARD_SD3) ? PBC_BSTAT_SD3_DET : \
				 ((n == MXC_CARD_SD1) ? PBC_BSTAT_SD1_DET : \
				 ((n == MXC_CARD_MS) ? PBC_BSTAT_MS_DET : \
					0x0))))

/*
 * PBC UART Control Register bit definitions
 */
#define PBC_UCTRL_DCE_DCD       0X0001
#define PBC_UCTRL_DCE_DSR       0X0002
#define PBC_UCTRL_DCE_RI        0X0004
#define PBC_UCTRL_DTE_DTR       0X0100

/*
 * PBC UART Status Register bit definitions
 */
#define PBC_USTAT_DTE_DCD       0X0001
#define PBC_USTAT_DTE_DSR       0X0002
#define PBC_USTAT_DTE_RI        0X0004
#define PBC_USTAT_DCE_DTR       0X0100

/*
 * PBC Interupt mask register bit definitions
 */
#define PBC_INTR_SD3_R_EN_BIT   4
#define PBC_INTR_SD2_R_EN_BIT   0
#define PBC_INTR_SD1_R_EN_BIT   6
#define PBC_INTR_MS_R_EN_BIT    5
#define PBC_INTR_SD3_EN_BIT     13
#define PBC_INTR_SD2_EN_BIT     12
#define PBC_INTR_MS_EN_BIT      14
#define PBC_INTR_SD1_EN_BIT     15

#define PBC_INTR_SD2_R_EN       0x0001
#define PBC_INTR_LOW_BAT        0X0002
#define PBC_INTR_OTG_FSOVER     0X0004
#define PBC_INTR_FSH_OVER       0X0008
#define PBC_INTR_SD3_R_EN       0x0010
#define PBC_INTR_MS_R_EN        0x0020
#define PBC_INTR_SD1_R_EN       0x0040
#define PBC_INTR_FEC_INT        0X0080
#define PBC_INTR_ENET_INT       0X0100
#define PBC_INTR_OTGFS_INT      0X0200
#define PBC_INTR_XUART_INT      0X0400
#define PBC_INTR_CCTL12         0X0800
#define PBC_INTR_SD2_EN         0x1000
#define PBC_INTR_SD3_EN         0x2000
#define PBC_INTR_MS_EN          0x4000
#define PBC_INTR_SD1_EN         0x8000



/* For interrupts like xuart, enet etc */
#define EXPIO_PARENT_INT        IOMUX_TO_IRQ(MX27_PIN_TIN)
#define MXC_MAX_EXP_IO_LINES    16

/*
 * This corresponds to PBC_INTMASK_SET_REG at offset 0x38.
 *
 */
#define EXPIO_INT_LOW_BAT       (MXC_EXP_IO_BASE + 1)
#define EXPIO_INT_OTG_FS_OVR    (MXC_EXP_IO_BASE + 2)
#define EXPIO_INT_FSH_OVR       (MXC_EXP_IO_BASE + 3)
#define EXPIO_INT_RES4          (MXC_EXP_IO_BASE + 4)
#define EXPIO_INT_RES5          (MXC_EXP_IO_BASE + 5)
#define EXPIO_INT_RES6          (MXC_EXP_IO_BASE + 6)
#define EXPIO_INT_FEC           (MXC_EXP_IO_BASE + 7)
#define EXPIO_INT_ENET_INT      (MXC_EXP_IO_BASE + 8)
#define EXPIO_INT_OTG_FS_INT    (MXC_EXP_IO_BASE + 9)
#define EXPIO_INT_XUART_INTA    (MXC_EXP_IO_BASE + 10)
#define EXPIO_INT_CCTL12_INT    (MXC_EXP_IO_BASE + 11)
#define EXPIO_INT_SD2_EN        (MXC_EXP_IO_BASE + 12)
#define EXPIO_INT_SD3_EN        (MXC_EXP_IO_BASE + 13)
#define EXPIO_INT_MS_EN         (MXC_EXP_IO_BASE + 14)
#define EXPIO_INT_SD1_EN        (MXC_EXP_IO_BASE + 15)

/*
 * This is System IRQ used by CS8900A for interrupt generation
 * taken from platform.h
 */
#define CS8900AIRQ              EXPIO_INT_ENET_INT
/* This is I/O Base address used to access registers of CS8900A on MXC ADS */
#define CS8900A_BASE_ADDRESS    (PBC_CS8900A_IOBASE_REG + 0x300)

#define MXC_PMIC_INT_LINE       IOMUX_TO_IRQ(MX27_PIN_TOUT)

/*
* This is used to detect if the CPLD version is for mx27 evb board rev-a
*/
#define PBC_CPLD_VERSION_IS_REVA() \
	((__raw_readw(PBC_VERSION_REG) & \
	(PBC_VERSION_ADS | PBC_VERSION_EVB_REVB))\
	== 0)

/* This is used to active or inactive ata signal in CPLD .
 *  It is dependent with hardware
 */
#define PBC_ATA_SIGNAL_ACTIVE() \
	__raw_writew(           \
		PBC_BCTRL2_ATAFEC_EN|PBC_BCTRL2_ATAFEC_SEL|PBC_BCTRL2_ATA_EN, \
		PBC_BCTRL2_CLEAR_REG)

#define PBC_ATA_SIGNAL_INACTIVE() \
	__raw_writew(  \
		PBC_BCTRL2_ATAFEC_EN|PBC_BCTRL2_ATAFEC_SEL|PBC_BCTRL2_ATA_EN, \
		PBC_BCTRL2_SET_REG)

#define MXC_BD_LED1             (1 << 5)
#define MXC_BD_LED2             (1 << 6)
#define MXC_BD_LED_ON(led) \
	__raw_writew(led, PBC_BCTRL1_SET_REG)
#define MXC_BD_LED_OFF(led) \
	__raw_writew(led, PBC_BCTRL1_CLEAR_REG)

/* to determine the correct external crystal reference */
#define CKIH_27MHZ_BIT_SET      (1 << 3)

#endif				/* __ASM_ARCH_MXC_BOARD_MX27ADS_H__ */
