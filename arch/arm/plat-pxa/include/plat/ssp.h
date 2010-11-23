/*
 *  ssp.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver supports the following PXA CPU/SSP ports:-
 *
 *       PXA250     SSP
 *       PXA255     SSP, NSSP
 *       PXA26x     SSP, NSSP, ASSP
 *       PXA27x     SSP1, SSP2, SSP3
 *       PXA3xx     SSP1, SSP2, SSP3, SSP4
 */

#ifndef __ASM_ARCH_SSP_H
#define __ASM_ARCH_SSP_H

#include <linux/list.h>
#include <linux/io.h>

/*
 * SSP Serial Port Registers
 * PXA250, PXA255, PXA26x and PXA27x SSP controllers are all slightly different.
 * PXA255, PXA26x and PXA27x have extra ports, registers and bits.
 */

#define SSCR0		(0x00)  /* SSP Control Register 0 */
#define SSCR1		(0x04)  /* SSP Control Register 1 */
#define SSSR		(0x08)  /* SSP Status Register */
#define SSITR		(0x0C)  /* SSP Interrupt Test Register */
#define SSDR		(0x10)  /* SSP Data Write/Data Read Register */

#define SSTO		(0x28)  /* SSP Time Out Register */
#define SSPSP		(0x2C)  /* SSP Programmable Serial Protocol */
#define SSTSA		(0x30)  /* SSP Tx Timeslot Active */
#define SSRSA		(0x34)  /* SSP Rx Timeslot Active */
#define SSTSS		(0x38)  /* SSP Timeslot Status */
#define SSACD		(0x3C)  /* SSP Audio Clock Divider */
#define SSACDD		(0x40)	/* SSP Audio Clock Dither Divider */

/* Common PXA2xx bits first */
#define SSCR0_DSS	(0x0000000f)	/* Data Size Select (mask) */
#define SSCR0_DataSize(x)  ((x) - 1)	/* Data Size Select [4..16] */
#define SSCR0_FRF	(0x00000030)	/* FRame Format (mask) */
#define SSCR0_Motorola	(0x0 << 4)	/* Motorola's Serial Peripheral Interface (SPI) */
#define SSCR0_TI	(0x1 << 4)	/* Texas Instruments' Synchronous Serial Protocol (SSP) */
#define SSCR0_National	(0x2 << 4)	/* National Microwire */
#define SSCR0_ECS	(1 << 6)	/* External clock select */
#define SSCR0_SSE	(1 << 7)	/* Synchronous Serial Port Enable */
#define SSCR0_SCR(x)	((x) << 8)	/* Serial Clock Rate (mask) */

/* PXA27x, PXA3xx */
#define SSCR0_EDSS	(1 << 20)	/* Extended data size select */
#define SSCR0_NCS	(1 << 21)	/* Network clock select */
#define SSCR0_RIM	(1 << 22)	/* Receive FIFO overrrun interrupt mask */
#define SSCR0_TUM	(1 << 23)	/* Transmit FIFO underrun interrupt mask */
#define SSCR0_FRDC	(0x07000000)	/* Frame rate divider control (mask) */
#define SSCR0_SlotsPerFrm(x) (((x) - 1) << 24)	/* Time slots per frame [1..8] */
#define SSCR0_FPCKE	(1 << 29)	/* FIFO packing enable */
#define SSCR0_ACS	(1 << 30)	/* Audio clock select */
#define SSCR0_MOD	(1 << 31)	/* Mode (normal or network) */


#define SSCR1_RIE	(1 << 0)	/* Receive FIFO Interrupt Enable */
#define SSCR1_TIE	(1 << 1)	/* Transmit FIFO Interrupt Enable */
#define SSCR1_LBM	(1 << 2)	/* Loop-Back Mode */
#define SSCR1_SPO	(1 << 3)	/* Motorola SPI SSPSCLK polarity setting */
#define SSCR1_SPH	(1 << 4)	/* Motorola SPI SSPSCLK phase setting */
#define SSCR1_MWDS	(1 << 5)	/* Microwire Transmit Data Size */
#define SSCR1_TFT	(0x000003c0)	/* Transmit FIFO Threshold (mask) */
#define SSCR1_TxTresh(x) (((x) - 1) << 6) /* level [1..16] */
#define SSCR1_RFT	(0x00003c00)	/* Receive FIFO Threshold (mask) */
#define SSCR1_RxTresh(x) (((x) - 1) << 10) /* level [1..16] */

#define SSSR_TNF	(1 << 2)	/* Transmit FIFO Not Full */
#define SSSR_RNE	(1 << 3)	/* Receive FIFO Not Empty */
#define SSSR_BSY	(1 << 4)	/* SSP Busy */
#define SSSR_TFS	(1 << 5)	/* Transmit FIFO Service Request */
#define SSSR_RFS	(1 << 6)	/* Receive FIFO Service Request */
#define SSSR_ROR	(1 << 7)	/* Receive FIFO Overrun */
#define SSSR_TFL_MASK   (0xf << 8)	/* Transmit FIFO Level mask */
#define SSSR_RFL_MASK   (0xf << 12)	/* Receive FIFO Level mask */

/* extra bits in PXA255, PXA26x and PXA27x SSP ports */
#define SSCR0_TISSP		(1 << 4)	/* TI Sync Serial Protocol */
#define SSCR0_PSP		(3 << 4)	/* PSP - Programmable Serial Protocol */
#define SSCR1_TTELP		(1 << 31)	/* TXD Tristate Enable Last Phase */
#define SSCR1_TTE		(1 << 30)	/* TXD Tristate Enable */
#define SSCR1_EBCEI		(1 << 29)	/* Enable Bit Count Error interrupt */
#define SSCR1_SCFR		(1 << 28)	/* Slave Clock free Running */
#define SSCR1_ECRA		(1 << 27)	/* Enable Clock Request A */
#define SSCR1_ECRB		(1 << 26)	/* Enable Clock request B */
#define SSCR1_SCLKDIR		(1 << 25)	/* Serial Bit Rate Clock Direction */
#define SSCR1_SFRMDIR		(1 << 24)	/* Frame Direction */
#define SSCR1_RWOT		(1 << 23)	/* Receive Without Transmit */
#define SSCR1_TRAIL		(1 << 22)	/* Trailing Byte */
#define SSCR1_TSRE		(1 << 21)	/* Transmit Service Request Enable */
#define SSCR1_RSRE		(1 << 20)	/* Receive Service Request Enable */
#define SSCR1_TINTE		(1 << 19)	/* Receiver Time-out Interrupt enable */
#define SSCR1_PINTE		(1 << 18)	/* Peripheral Trailing Byte Interupt Enable */
#define SSCR1_IFS		(1 << 16)	/* Invert Frame Signal */
#define SSCR1_STRF		(1 << 15)	/* Select FIFO or EFWR */
#define SSCR1_EFWR		(1 << 14)	/* Enable FIFO Write/Read */

#define SSSR_BCE		(1 << 23)	/* Bit Count Error */
#define SSSR_CSS		(1 << 22)	/* Clock Synchronisation Status */
#define SSSR_TUR		(1 << 21)	/* Transmit FIFO Under Run */
#define SSSR_EOC		(1 << 20)	/* End Of Chain */
#define SSSR_TINT		(1 << 19)	/* Receiver Time-out Interrupt */
#define SSSR_PINT		(1 << 18)	/* Peripheral Trailing Byte Interrupt */


#define SSPSP_SCMODE(x)		((x) << 0)	/* Serial Bit Rate Clock Mode */
#define SSPSP_SFRMP		(1 << 2)	/* Serial Frame Polarity */
#define SSPSP_ETDS		(1 << 3)	/* End of Transfer data State */
#define SSPSP_STRTDLY(x)	((x) << 4)	/* Start Delay */
#define SSPSP_DMYSTRT(x)	((x) << 7)	/* Dummy Start */
#define SSPSP_SFRMDLY(x)	((x) << 9)	/* Serial Frame Delay */
#define SSPSP_SFRMWDTH(x)	((x) << 16)	/* Serial Frame Width */
#define SSPSP_DMYSTOP(x)	((x) << 23)	/* Dummy Stop */
#define SSPSP_FSRT		(1 << 25)	/* Frame Sync Relative Timing */

/* PXA3xx */
#define SSPSP_EDMYSTRT(x)	((x) << 26)     /* Extended Dummy Start */
#define SSPSP_EDMYSTOP(x)	((x) << 28)     /* Extended Dummy Stop */
#define SSPSP_TIMING_MASK	(0x7f8001f0)

#define SSACD_SCDB		(1 << 3)	/* SSPSYSCLK Divider Bypass */
#define SSACD_ACPS(x)		((x) << 4)	/* Audio clock PLL select */
#define SSACD_ACDS(x)		((x) << 0)	/* Audio clock divider select */
#define SSACD_SCDX8		(1 << 7)	/* SYSCLK division ratio select */

enum pxa_ssp_type {
	SSP_UNDEFINED = 0,
	PXA25x_SSP,  /* pxa 210, 250, 255, 26x */
	PXA25x_NSSP, /* pxa 255, 26x (including ASSP) */
	PXA27x_SSP,
	PXA168_SSP,
};

struct ssp_device {
	struct platform_device *pdev;
	struct list_head	node;

	struct clk	*clk;
	void __iomem	*mmio_base;
	unsigned long	phys_base;

	const char	*label;
	int		port_id;
	int		type;
	int		use_count;
	int		irq;
	int		drcmr_rx;
	int		drcmr_tx;
};

/**
 * pxa_ssp_write_reg - Write to a SSP register
 *
 * @dev: SSP device to access
 * @reg: Register to write to
 * @val: Value to be written.
 */
static inline void pxa_ssp_write_reg(struct ssp_device *dev, u32 reg, u32 val)
{
	__raw_writel(val, dev->mmio_base + reg);
}

/**
 * pxa_ssp_read_reg - Read from a SSP register
 *
 * @dev: SSP device to access
 * @reg: Register to read from
 */
static inline u32 pxa_ssp_read_reg(struct ssp_device *dev, u32 reg)
{
	return __raw_readl(dev->mmio_base + reg);
}

struct ssp_device *pxa_ssp_request(int port, const char *label);
void pxa_ssp_free(struct ssp_device *);
#endif /* __ASM_ARCH_SSP_H */
