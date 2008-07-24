/*
 * Definitions for TUSB6010 USB 2.0 OTG Dual Role controller
 *
 * Copyright (C) 2006 Nokia Corporation
 * Jarkko Nikula <jarkko.nikula@nokia.com>
 * Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TUSB6010_H__
#define __TUSB6010_H__

extern u8 tusb_get_revision(struct musb *musb);

#ifdef CONFIG_USB_TUSB6010
#define musb_in_tusb()			1
#else
#define musb_in_tusb()			0
#endif

#ifdef CONFIG_USB_TUSB_OMAP_DMA
#define tusb_dma_omap()			1
#else
#define tusb_dma_omap()			0
#endif

/* VLYNQ control register. 32-bit at offset 0x000 */
#define TUSB_VLYNQ_CTRL			0x004

/* Mentor Graphics OTG core registers. 8,- 16- and 32-bit at offset 0x400 */
#define TUSB_BASE_OFFSET		0x400

/* FIFO registers 32-bit at offset 0x600 */
#define TUSB_FIFO_BASE			0x600

/* Device System & Control registers. 32-bit at offset 0x800 */
#define TUSB_SYS_REG_BASE		0x800

#define TUSB_DEV_CONF			(TUSB_SYS_REG_BASE + 0x000)
#define		TUSB_DEV_CONF_USB_HOST_MODE		(1 << 16)
#define		TUSB_DEV_CONF_PROD_TEST_MODE		(1 << 15)
#define		TUSB_DEV_CONF_SOFT_ID			(1 << 1)
#define		TUSB_DEV_CONF_ID_SEL			(1 << 0)

#define TUSB_PHY_OTG_CTRL_ENABLE	(TUSB_SYS_REG_BASE + 0x004)
#define TUSB_PHY_OTG_CTRL		(TUSB_SYS_REG_BASE + 0x008)
#define		TUSB_PHY_OTG_CTRL_WRPROTECT		(0xa5 << 24)
#define		TUSB_PHY_OTG_CTRL_OTG_ID_PULLUP		(1 << 23)
#define		TUSB_PHY_OTG_CTRL_OTG_VBUS_DET_EN	(1 << 19)
#define		TUSB_PHY_OTG_CTRL_OTG_SESS_END_EN	(1 << 18)
#define		TUSB_PHY_OTG_CTRL_TESTM2		(1 << 17)
#define		TUSB_PHY_OTG_CTRL_TESTM1		(1 << 16)
#define		TUSB_PHY_OTG_CTRL_TESTM0		(1 << 15)
#define		TUSB_PHY_OTG_CTRL_TX_DATA2		(1 << 14)
#define		TUSB_PHY_OTG_CTRL_TX_GZ2		(1 << 13)
#define		TUSB_PHY_OTG_CTRL_TX_ENABLE2		(1 << 12)
#define		TUSB_PHY_OTG_CTRL_DM_PULLDOWN		(1 << 11)
#define		TUSB_PHY_OTG_CTRL_DP_PULLDOWN		(1 << 10)
#define		TUSB_PHY_OTG_CTRL_OSC_EN		(1 << 9)
#define		TUSB_PHY_OTG_CTRL_PHYREF_CLKSEL(v)	(((v) & 3) << 7)
#define		TUSB_PHY_OTG_CTRL_PD			(1 << 6)
#define		TUSB_PHY_OTG_CTRL_PLL_ON		(1 << 5)
#define		TUSB_PHY_OTG_CTRL_EXT_RPU		(1 << 4)
#define		TUSB_PHY_OTG_CTRL_PWR_GOOD		(1 << 3)
#define		TUSB_PHY_OTG_CTRL_RESET			(1 << 2)
#define		TUSB_PHY_OTG_CTRL_SUSPENDM		(1 << 1)
#define		TUSB_PHY_OTG_CTRL_CLK_MODE		(1 << 0)

/*OTG status register */
#define TUSB_DEV_OTG_STAT		(TUSB_SYS_REG_BASE + 0x00c)
#define		TUSB_DEV_OTG_STAT_PWR_CLK_GOOD		(1 << 8)
#define		TUSB_DEV_OTG_STAT_SESS_END		(1 << 7)
#define		TUSB_DEV_OTG_STAT_SESS_VALID		(1 << 6)
#define		TUSB_DEV_OTG_STAT_VBUS_VALID		(1 << 5)
#define		TUSB_DEV_OTG_STAT_VBUS_SENSE		(1 << 4)
#define		TUSB_DEV_OTG_STAT_ID_STATUS		(1 << 3)
#define		TUSB_DEV_OTG_STAT_HOST_DISCON		(1 << 2)
#define		TUSB_DEV_OTG_STAT_LINE_STATE		(3 << 0)
#define		TUSB_DEV_OTG_STAT_DP_ENABLE		(1 << 1)
#define		TUSB_DEV_OTG_STAT_DM_ENABLE		(1 << 0)

#define TUSB_DEV_OTG_TIMER		(TUSB_SYS_REG_BASE + 0x010)
#	define TUSB_DEV_OTG_TIMER_ENABLE		(1 << 31)
#	define TUSB_DEV_OTG_TIMER_VAL(v)		((v) & 0x07ffffff)
#define TUSB_PRCM_REV			(TUSB_SYS_REG_BASE + 0x014)

/* PRCM configuration register */
#define TUSB_PRCM_CONF			(TUSB_SYS_REG_BASE + 0x018)
#define		TUSB_PRCM_CONF_SFW_CPEN		(1 << 24)
#define		TUSB_PRCM_CONF_SYS_CLKSEL(v)	(((v) & 3) << 16)

/* PRCM management register */
#define TUSB_PRCM_MNGMT			(TUSB_SYS_REG_BASE + 0x01c)
#define		TUSB_PRCM_MNGMT_SRP_FIX_TIMER(v)	(((v) & 0xf) << 25)
#define		TUSB_PRCM_MNGMT_SRP_FIX_EN		(1 << 24)
#define		TUSB_PRCM_MNGMT_VBUS_VALID_TIMER(v)	(((v) & 0xf) << 20)
#define		TUSB_PRCM_MNGMT_VBUS_VALID_FLT_EN	(1 << 19)
#define		TUSB_PRCM_MNGMT_DFT_CLK_DIS		(1 << 18)
#define		TUSB_PRCM_MNGMT_VLYNQ_CLK_DIS		(1 << 17)
#define		TUSB_PRCM_MNGMT_OTG_SESS_END_EN		(1 << 10)
#define		TUSB_PRCM_MNGMT_OTG_VBUS_DET_EN		(1 << 9)
#define		TUSB_PRCM_MNGMT_OTG_ID_PULLUP		(1 << 8)
#define		TUSB_PRCM_MNGMT_15_SW_EN		(1 << 4)
#define		TUSB_PRCM_MNGMT_33_SW_EN		(1 << 3)
#define		TUSB_PRCM_MNGMT_5V_CPEN			(1 << 2)
#define		TUSB_PRCM_MNGMT_PM_IDLE			(1 << 1)
#define		TUSB_PRCM_MNGMT_DEV_IDLE		(1 << 0)

/* Wake-up source clear and mask registers */
#define TUSB_PRCM_WAKEUP_SOURCE		(TUSB_SYS_REG_BASE + 0x020)
#define TUSB_PRCM_WAKEUP_CLEAR		(TUSB_SYS_REG_BASE + 0x028)
#define TUSB_PRCM_WAKEUP_MASK		(TUSB_SYS_REG_BASE + 0x02c)
#define		TUSB_PRCM_WAKEUP_RESERVED_BITS	(0xffffe << 13)
#define		TUSB_PRCM_WGPIO_7	(1 << 12)
#define		TUSB_PRCM_WGPIO_6	(1 << 11)
#define		TUSB_PRCM_WGPIO_5	(1 << 10)
#define		TUSB_PRCM_WGPIO_4	(1 << 9)
#define		TUSB_PRCM_WGPIO_3	(1 << 8)
#define		TUSB_PRCM_WGPIO_2	(1 << 7)
#define		TUSB_PRCM_WGPIO_1	(1 << 6)
#define		TUSB_PRCM_WGPIO_0	(1 << 5)
#define		TUSB_PRCM_WHOSTDISCON	(1 << 4)	/* Host disconnect */
#define		TUSB_PRCM_WBUS		(1 << 3)	/* USB bus resume */
#define		TUSB_PRCM_WNORCS	(1 << 2)	/* NOR chip select */
#define		TUSB_PRCM_WVBUS		(1 << 1)	/* OTG PHY VBUS */
#define		TUSB_PRCM_WID		(1 << 0)	/* OTG PHY ID detect */

#define TUSB_PULLUP_1_CTRL		(TUSB_SYS_REG_BASE + 0x030)
#define TUSB_PULLUP_2_CTRL		(TUSB_SYS_REG_BASE + 0x034)
#define TUSB_INT_CTRL_REV		(TUSB_SYS_REG_BASE + 0x038)
#define TUSB_INT_CTRL_CONF		(TUSB_SYS_REG_BASE + 0x03c)
#define TUSB_USBIP_INT_SRC		(TUSB_SYS_REG_BASE + 0x040)
#define TUSB_USBIP_INT_SET		(TUSB_SYS_REG_BASE + 0x044)
#define TUSB_USBIP_INT_CLEAR		(TUSB_SYS_REG_BASE + 0x048)
#define TUSB_USBIP_INT_MASK		(TUSB_SYS_REG_BASE + 0x04c)
#define TUSB_DMA_INT_SRC		(TUSB_SYS_REG_BASE + 0x050)
#define TUSB_DMA_INT_SET		(TUSB_SYS_REG_BASE + 0x054)
#define TUSB_DMA_INT_CLEAR		(TUSB_SYS_REG_BASE + 0x058)
#define TUSB_DMA_INT_MASK		(TUSB_SYS_REG_BASE + 0x05c)
#define TUSB_GPIO_INT_SRC		(TUSB_SYS_REG_BASE + 0x060)
#define TUSB_GPIO_INT_SET		(TUSB_SYS_REG_BASE + 0x064)
#define TUSB_GPIO_INT_CLEAR		(TUSB_SYS_REG_BASE + 0x068)
#define TUSB_GPIO_INT_MASK		(TUSB_SYS_REG_BASE + 0x06c)

/* NOR flash interrupt source registers */
#define TUSB_INT_SRC			(TUSB_SYS_REG_BASE + 0x070)
#define TUSB_INT_SRC_SET		(TUSB_SYS_REG_BASE + 0x074)
#define TUSB_INT_SRC_CLEAR		(TUSB_SYS_REG_BASE + 0x078)
#define TUSB_INT_MASK			(TUSB_SYS_REG_BASE + 0x07c)
#define		TUSB_INT_SRC_TXRX_DMA_DONE		(1 << 24)
#define		TUSB_INT_SRC_USB_IP_CORE		(1 << 17)
#define		TUSB_INT_SRC_OTG_TIMEOUT		(1 << 16)
#define		TUSB_INT_SRC_VBUS_SENSE_CHNG		(1 << 15)
#define		TUSB_INT_SRC_ID_STATUS_CHNG		(1 << 14)
#define		TUSB_INT_SRC_DEV_WAKEUP			(1 << 13)
#define		TUSB_INT_SRC_DEV_READY			(1 << 12)
#define		TUSB_INT_SRC_USB_IP_TX			(1 << 9)
#define		TUSB_INT_SRC_USB_IP_RX			(1 << 8)
#define		TUSB_INT_SRC_USB_IP_VBUS_ERR		(1 << 7)
#define		TUSB_INT_SRC_USB_IP_VBUS_REQ		(1 << 6)
#define		TUSB_INT_SRC_USB_IP_DISCON		(1 << 5)
#define		TUSB_INT_SRC_USB_IP_CONN		(1 << 4)
#define		TUSB_INT_SRC_USB_IP_SOF			(1 << 3)
#define		TUSB_INT_SRC_USB_IP_RST_BABBLE		(1 << 2)
#define		TUSB_INT_SRC_USB_IP_RESUME		(1 << 1)
#define		TUSB_INT_SRC_USB_IP_SUSPEND		(1 << 0)

/* NOR flash interrupt registers reserved bits. Must be written as 0 */
#define		TUSB_INT_MASK_RESERVED_17		(0x3fff << 17)
#define		TUSB_INT_MASK_RESERVED_13		(1 << 13)
#define		TUSB_INT_MASK_RESERVED_8		(0xf << 8)
#define		TUSB_INT_SRC_RESERVED_26		(0x1f << 26)
#define		TUSB_INT_SRC_RESERVED_18		(0x3f << 18)
#define		TUSB_INT_SRC_RESERVED_10		(0x03 << 10)

/* Reserved bits for NOR flash interrupt mask and clear register */
#define		TUSB_INT_MASK_RESERVED_BITS	(TUSB_INT_MASK_RESERVED_17 | \
						TUSB_INT_MASK_RESERVED_13 | \
						TUSB_INT_MASK_RESERVED_8)

/* Reserved bits for NOR flash interrupt status register */
#define		TUSB_INT_SRC_RESERVED_BITS	(TUSB_INT_SRC_RESERVED_26 | \
						TUSB_INT_SRC_RESERVED_18 | \
						TUSB_INT_SRC_RESERVED_10)

#define TUSB_GPIO_REV			(TUSB_SYS_REG_BASE + 0x080)
#define TUSB_GPIO_CONF			(TUSB_SYS_REG_BASE + 0x084)
#define TUSB_DMA_CTRL_REV		(TUSB_SYS_REG_BASE + 0x100)
#define TUSB_DMA_REQ_CONF		(TUSB_SYS_REG_BASE + 0x104)
#define TUSB_EP0_CONF			(TUSB_SYS_REG_BASE + 0x108)
#define TUSB_DMA_EP_MAP			(TUSB_SYS_REG_BASE + 0x148)

/* Offsets from each ep base register */
#define TUSB_EP_TX_OFFSET		0x10c	/* EP_IN in docs */
#define TUSB_EP_RX_OFFSET		0x14c	/* EP_OUT in docs */
#define TUSB_EP_MAX_PACKET_SIZE_OFFSET	0x188

#define TUSB_WAIT_COUNT			(TUSB_SYS_REG_BASE + 0x1c8)
#define TUSB_SCRATCH_PAD		(TUSB_SYS_REG_BASE + 0x1c4)
#define TUSB_PROD_TEST_RESET		(TUSB_SYS_REG_BASE + 0x1d8)

/* Device System & Control register bitfields */
#define TUSB_INT_CTRL_CONF_INT_RELCYC(v)	(((v) & 0x7) << 18)
#define TUSB_INT_CTRL_CONF_INT_POLARITY		(1 << 17)
#define TUSB_INT_CTRL_CONF_INT_MODE		(1 << 16)
#define TUSB_GPIO_CONF_DMAREQ(v)		(((v) & 0x3f) << 24)
#define TUSB_DMA_REQ_CONF_BURST_SIZE(v)		(((v) & 3) << 26)
#define TUSB_DMA_REQ_CONF_DMA_REQ_EN(v)		(((v) & 0x3f) << 20)
#define TUSB_DMA_REQ_CONF_DMA_REQ_ASSER(v)	(((v) & 0xf) << 16)
#define TUSB_EP0_CONFIG_SW_EN			(1 << 8)
#define TUSB_EP0_CONFIG_DIR_TX			(1 << 7)
#define TUSB_EP0_CONFIG_XFR_SIZE(v)		((v) & 0x7f)
#define TUSB_EP_CONFIG_SW_EN			(1 << 31)
#define TUSB_EP_CONFIG_XFR_SIZE(v)		((v) & 0x7fffffff)
#define TUSB_PROD_TEST_RESET_VAL		0xa596
#define TUSB_EP_FIFO(ep)			(TUSB_FIFO_BASE + (ep) * 0x20)

#define TUSB_DIDR1_LO				(TUSB_SYS_REG_BASE + 0x1f8)
#define TUSB_DIDR1_HI				(TUSB_SYS_REG_BASE + 0x1fc)
#define		TUSB_DIDR1_HI_CHIP_REV(v)		(((v) >> 17) & 0xf)
#define			TUSB_DIDR1_HI_REV_20		0
#define			TUSB_DIDR1_HI_REV_30		1
#define			TUSB_DIDR1_HI_REV_31		2

#define TUSB_REV_10	0x10
#define TUSB_REV_20	0x20
#define TUSB_REV_30	0x30
#define TUSB_REV_31	0x31

/*----------------------------------------------------------------------------*/

#ifdef CONFIG_USB_TUSB6010

/* configuration parameters specific to this silicon */

/* Number of Tx endpoints. Legal values are 1 - 16 (this value includes EP0) */
#define MUSB_C_NUM_EPT 16

/* Number of Rx endpoints. Legal values are 1 - 16 (this value includes EP0) */
#define MUSB_C_NUM_EPR 16

/* Endpoint 1 to 15 direction types. C_EP1_DEF is defined if either Tx endpoint
 * 1 or Rx endpoint 1 are used.
 */
#define MUSB_C_EP1_DEF

/* C_EP1_TX_DEF is defined if Tx endpoint 1 is used */
#define MUSB_C_EP1_TX_DEF

/* C_EP1_RX_DEF is defined if Rx endpoint 1 is used */
#define MUSB_C_EP1_RX_DEF

/* C_EP1_TOR_DEF is defined if Tx endpoint 1 and Rx endpoint 1 share a FIFO */
/* #define C_EP1_TOR_DEF */

/* C_EP1_TAR_DEF is defined if both Tx endpoint 1 and Rx endpoint 1 are used
 * and do not share a FIFO.
 */
#define MUSB_C_EP1_TAR_DEF

/* Similarly for all other used endpoints */
#define MUSB_C_EP2_DEF
#define MUSB_C_EP2_TX_DEF
#define MUSB_C_EP2_RX_DEF
#define MUSB_C_EP2_TAR_DEF
#define MUSB_C_EP3_DEF
#define MUSB_C_EP3_TX_DEF
#define MUSB_C_EP3_RX_DEF
#define MUSB_C_EP3_TAR_DEF
#define MUSB_C_EP4_DEF
#define MUSB_C_EP4_TX_DEF
#define MUSB_C_EP4_RX_DEF
#define MUSB_C_EP4_TAR_DEF

/* Endpoint 1 to 15 FIFO address bits. Legal values are 3 to 13 - corresponding
 * to FIFO sizes of 8 to 8192 bytes. If an Tx endpoint shares a FIFO with an Rx
 * endpoint then the Rx FIFO size must be the same as the Tx FIFO size. All
 * endpoints 1 to 15 must be defined, unused endpoints should be set to 2.
 */
#define MUSB_C_EP1T_BITS 5
#define MUSB_C_EP1R_BITS 5
#define MUSB_C_EP2T_BITS 5
#define MUSB_C_EP2R_BITS 5
#define MUSB_C_EP3T_BITS 3
#define MUSB_C_EP3R_BITS 3
#define MUSB_C_EP4T_BITS 3
#define MUSB_C_EP4R_BITS 3

#define MUSB_C_EP5T_BITS 2
#define MUSB_C_EP5R_BITS 2
#define MUSB_C_EP6T_BITS 2
#define MUSB_C_EP6R_BITS 2
#define MUSB_C_EP7T_BITS 2
#define MUSB_C_EP7R_BITS 2
#define MUSB_C_EP8T_BITS 2
#define MUSB_C_EP8R_BITS 2
#define MUSB_C_EP9T_BITS 2
#define MUSB_C_EP9R_BITS 2
#define MUSB_C_EP10T_BITS 2
#define MUSB_C_EP10R_BITS 2
#define MUSB_C_EP11T_BITS 2
#define MUSB_C_EP11R_BITS 2
#define MUSB_C_EP12T_BITS 2
#define MUSB_C_EP12R_BITS 2
#define MUSB_C_EP13T_BITS 2
#define MUSB_C_EP13R_BITS 2
#define MUSB_C_EP14T_BITS 2
#define MUSB_C_EP14R_BITS 2
#define MUSB_C_EP15T_BITS 2
#define MUSB_C_EP15R_BITS 2

/* Define the following constant if the USB2.0 Transceiver Macrocell data width
 * is 16-bits.
 */
/* #define C_UTM_16 */

/* Define this constant if the CPU uses big-endian byte ordering. */
/* #define C_BIGEND */

/* Define the following constant if any Tx endpoint is required to support
 * multiple bulk packets.
 */
/* #define C_MP_TX */

/* Define the following constant if any Rx endpoint is required to support
 * multiple bulk packets.
 */
/* #define C_MP_RX */

/* Define the following constant if any Tx endpoint is required to support high
 * bandwidth ISO.
 */
/* #define C_HB_TX */

/* Define the following constant if any Rx endpoint is required to support high
 * bandwidth ISO.
 */
/* #define C_HB_RX */

/* Define the following constant if software connect/disconnect control is
 * required.
 */
#define MUSB_C_SOFT_CON

/* Define the following constant if Vendor Control Registers are required. */
/* #define C_VEND_REG */

/* Vendor control register widths. */
#define MUSB_C_VCTL_BITS 4
#define MUSB_C_VSTAT_BITS 8

/* Define the following constant to include a DMA controller. */
/* #define C_DMA */

/* Define the following constant if 2 or more DMA channels are required. */
/* #define C_DMA2 */

/* Define the following constant if 3 or more DMA channels are required. */
/* #define C_DMA3 */

/* Define the following constant if 4 or more DMA channels are required. */
/* #define C_DMA4 */

/* Define the following constant if 5 or more DMA channels are required. */
/* #define C_DMA5 */

/* Define the following constant if 6 or more DMA channels are required. */
/* #define C_DMA6 */

/* Define the following constant if 7 or more DMA channels are required. */
/* #define C_DMA7 */

/* Define the following constant if 8 or more DMA channels are required. */
/* #define C_DMA8 */

/* Enable Dynamic FIFO Sizing */
#define MUSB_C_DYNFIFO_DEF

/* Derived constants. The following constants are derived from the previous
 * configuration constants
 */

/* Total number of endpoints. Legal values are 2 - 16. This must be equal to
 * the larger of C_NUM_EPT, C_NUM_EPR
 */
/* #define MUSB_C_NUM_EPS 5 */

/* C_EPMAX_BITS is equal to the largest endpoint FIFO word address bits */
#define MUSB_C_EPMAX_BITS 11

/* C_RAM_BITS is the number of address bits required to address the RAM (32-bit
 * addresses).  It is defined as log2 of the sum of 2** of all the endpoint FIFO
 * dword address bits (rounded up).
 */
#define MUSB_C_RAM_BITS 12

#endif /* CONFIG_USB_TUSB6010 */

#endif /* __TUSB6010_H__ */
