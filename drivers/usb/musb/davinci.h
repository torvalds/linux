/*
 * Copyright (C) 2005-2006 by Texas Instruments
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 */

#ifndef __MUSB_HDRDF_H__
#define __MUSB_HDRDF_H__

/*
 * DaVinci-specific definitions
 */

/* Integrated highspeed/otg PHY */
#define	USBPHY_CTL_PADDR	(DAVINCI_SYSTEM_MODULE_BASE + 0x34)
#define	USBPHY_PHYCLKGD		(1 << 8)
#define	USBPHY_SESNDEN		(1 << 7)	/* v(sess_end) comparator */
#define	USBPHY_VBDTCTEN		(1 << 6)	/* v(bus) comparator */
#define	USBPHY_PHYPLLON		(1 << 4)	/* override pll suspend */
#define	USBPHY_CLKO1SEL		(1 << 3)
#define	USBPHY_OSCPDWN		(1 << 2)
#define	USBPHY_PHYPDWN		(1 << 0)

/* For now include usb OTG module registers here */
#define DAVINCI_USB_VERSION_REG		0x00
#define DAVINCI_USB_CTRL_REG		0x04
#define DAVINCI_USB_STAT_REG		0x08
#define DAVINCI_RNDIS_REG		0x10
#define DAVINCI_AUTOREQ_REG		0x14
#define DAVINCI_USB_INT_SOURCE_REG	0x20
#define DAVINCI_USB_INT_SET_REG		0x24
#define DAVINCI_USB_INT_SRC_CLR_REG	0x28
#define DAVINCI_USB_INT_MASK_REG	0x2c
#define DAVINCI_USB_INT_MASK_SET_REG	0x30
#define DAVINCI_USB_INT_MASK_CLR_REG	0x34
#define DAVINCI_USB_INT_SRC_MASKED_REG	0x38
#define DAVINCI_USB_EOI_REG		0x3c
#define DAVINCI_USB_EOI_INTVEC		0x40

/* BEGIN CPPI-generic (?) */

/* CPPI related registers */
#define DAVINCI_TXCPPI_CTRL_REG		0x80
#define DAVINCI_TXCPPI_TEAR_REG		0x84
#define DAVINCI_CPPI_EOI_REG		0x88
#define DAVINCI_CPPI_INTVEC_REG		0x8c
#define DAVINCI_TXCPPI_MASKED_REG	0x90
#define DAVINCI_TXCPPI_RAW_REG		0x94
#define DAVINCI_TXCPPI_INTENAB_REG	0x98
#define DAVINCI_TXCPPI_INTCLR_REG	0x9c

#define DAVINCI_RXCPPI_CTRL_REG		0xC0
#define DAVINCI_RXCPPI_MASKED_REG	0xD0
#define DAVINCI_RXCPPI_RAW_REG		0xD4
#define DAVINCI_RXCPPI_INTENAB_REG	0xD8
#define DAVINCI_RXCPPI_INTCLR_REG	0xDC

#define DAVINCI_RXCPPI_BUFCNT0_REG	0xE0
#define DAVINCI_RXCPPI_BUFCNT1_REG	0xE4
#define DAVINCI_RXCPPI_BUFCNT2_REG	0xE8
#define DAVINCI_RXCPPI_BUFCNT3_REG	0xEC

/* CPPI state RAM entries */
#define DAVINCI_CPPI_STATERAM_BASE_OFFSET   0x100

#define DAVINCI_TXCPPI_STATERAM_OFFSET(chnum) \
	(DAVINCI_CPPI_STATERAM_BASE_OFFSET +       ((chnum) * 0x40))
#define DAVINCI_RXCPPI_STATERAM_OFFSET(chnum) \
	(DAVINCI_CPPI_STATERAM_BASE_OFFSET + 0x20 + ((chnum) * 0x40))

/* CPPI masks */
#define DAVINCI_DMA_CTRL_ENABLE		1
#define DAVINCI_DMA_CTRL_DISABLE	0

#define DAVINCI_DMA_ALL_CHANNELS_ENABLE	0xF
#define DAVINCI_DMA_ALL_CHANNELS_DISABLE 0xF

/* END CPPI-generic (?) */

#define DAVINCI_USB_TX_ENDPTS_MASK	0x1f		/* ep0 + 4 tx */
#define DAVINCI_USB_RX_ENDPTS_MASK	0x1e		/* 4 rx */

#define DAVINCI_USB_USBINT_SHIFT	16
#define DAVINCI_USB_TXINT_SHIFT		0
#define DAVINCI_USB_RXINT_SHIFT		8

#define DAVINCI_INTR_DRVVBUS		0x0100

#define DAVINCI_USB_USBINT_MASK		0x01ff0000	/* 8 Mentor, DRVVBUS */
#define DAVINCI_USB_TXINT_MASK \
	(DAVINCI_USB_TX_ENDPTS_MASK << DAVINCI_USB_TXINT_SHIFT)
#define DAVINCI_USB_RXINT_MASK \
	(DAVINCI_USB_RX_ENDPTS_MASK << DAVINCI_USB_RXINT_SHIFT)

#define DAVINCI_BASE_OFFSET		0x400

#endif	/* __MUSB_HDRDF_H__ */
