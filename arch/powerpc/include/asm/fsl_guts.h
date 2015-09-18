/**
 * Freecale 85xx and 86xx Global Utilties register set
 *
 * Authors: Jeff Brown
 *          Timur Tabi <timur@freescale.com>
 *
 * Copyright 2004,2007,2012 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_POWERPC_FSL_GUTS_H__
#define __ASM_POWERPC_FSL_GUTS_H__
#ifdef __KERNEL__

/**
 * Global Utility Registers.
 *
 * Not all registers defined in this structure are available on all chips, so
 * you are expected to know whether a given register actually exists on your
 * chip before you access it.
 *
 * Also, some registers are similar on different chips but have slightly
 * different names.  In these cases, one name is chosen to avoid extraneous
 * #ifdefs.
 */
struct ccsr_guts {
	__be32	porpllsr;	/* 0x.0000 - POR PLL Ratio Status Register */
	__be32	porbmsr;	/* 0x.0004 - POR Boot Mode Status Register */
	__be32	porimpscr;	/* 0x.0008 - POR I/O Impedance Status and Control Register */
	__be32	pordevsr;	/* 0x.000c - POR I/O Device Status Register */
	__be32	pordbgmsr;	/* 0x.0010 - POR Debug Mode Status Register */
	__be32	pordevsr2;	/* 0x.0014 - POR device status register 2 */
	u8	res018[0x20 - 0x18];
	__be32	porcir;		/* 0x.0020 - POR Configuration Information Register */
	u8	res024[0x30 - 0x24];
	__be32	gpiocr;		/* 0x.0030 - GPIO Control Register */
	u8	res034[0x40 - 0x34];
	__be32	gpoutdr;	/* 0x.0040 - General-Purpose Output Data Register */
	u8	res044[0x50 - 0x44];
	__be32	gpindr;		/* 0x.0050 - General-Purpose Input Data Register */
	u8	res054[0x60 - 0x54];
	__be32	pmuxcr;		/* 0x.0060 - Alternate Function Signal Multiplex Control */
        __be32  pmuxcr2;	/* 0x.0064 - Alternate function signal multiplex control 2 */
        __be32  dmuxcr;		/* 0x.0068 - DMA Mux Control Register */
        u8	res06c[0x70 - 0x6c];
	__be32	devdisr;	/* 0x.0070 - Device Disable Control */
#define CCSR_GUTS_DEVDISR_TB1	0x00001000
#define CCSR_GUTS_DEVDISR_TB0	0x00004000
	__be32	devdisr2;	/* 0x.0074 - Device Disable Control 2 */
	u8	res078[0x7c - 0x78];
	__be32  pmjcr;		/* 0x.007c - 4 Power Management Jog Control Register */
	__be32	powmgtcsr;	/* 0x.0080 - Power Management Status and Control Register */
	__be32  pmrccr;		/* 0x.0084 - Power Management Reset Counter Configuration Register */
	__be32  pmpdccr;	/* 0x.0088 - Power Management Power Down Counter Configuration Register */
	__be32  pmcdr;		/* 0x.008c - 4Power management clock disable register */
	__be32	mcpsumr;	/* 0x.0090 - Machine Check Summary Register */
	__be32	rstrscr;	/* 0x.0094 - Reset Request Status and Control Register */
	__be32  ectrstcr;	/* 0x.0098 - Exception reset control register */
	__be32  autorstsr;	/* 0x.009c - Automatic reset status register */
	__be32	pvr;		/* 0x.00a0 - Processor Version Register */
	__be32	svr;		/* 0x.00a4 - System Version Register */
	u8	res0a8[0xb0 - 0xa8];
	__be32	rstcr;		/* 0x.00b0 - Reset Control Register */
	u8	res0b4[0xc0 - 0xb4];
	__be32  iovselsr;	/* 0x.00c0 - I/O voltage select status register
					     Called 'elbcvselcr' on 86xx SOCs */
	u8	res0c4[0x100 - 0xc4];
	__be32	rcwsr[16];	/* 0x.0100 - Reset Control Word Status registers
					     There are 16 registers */
	u8	res140[0x224 - 0x140];
	__be32  iodelay1;	/* 0x.0224 - IO delay control register 1 */
	__be32  iodelay2;	/* 0x.0228 - IO delay control register 2 */
	u8	res22c[0x604 - 0x22c];
	__be32	pamubypenr; 	/* 0x.604 - PAMU bypass enable register */
	u8	res608[0x800 - 0x608];
	__be32	clkdvdr;	/* 0x.0800 - Clock Divide Register */
	u8	res804[0x900 - 0x804];
	__be32	ircr;		/* 0x.0900 - Infrared Control Register */
	u8	res904[0x908 - 0x904];
	__be32	dmacr;		/* 0x.0908 - DMA Control Register */
	u8	res90c[0x914 - 0x90c];
	__be32	elbccr;		/* 0x.0914 - eLBC Control Register */
	u8	res918[0xb20 - 0x918];
	__be32	ddr1clkdr;	/* 0x.0b20 - DDR1 Clock Disable Register */
	__be32	ddr2clkdr;	/* 0x.0b24 - DDR2 Clock Disable Register */
	__be32	ddrclkdr;	/* 0x.0b28 - DDR Clock Disable Register */
	u8	resb2c[0xe00 - 0xb2c];
	__be32	clkocr;		/* 0x.0e00 - Clock Out Select Register */
	u8	rese04[0xe10 - 0xe04];
	__be32	ddrdllcr;	/* 0x.0e10 - DDR DLL Control Register */
	u8	rese14[0xe20 - 0xe14];
	__be32	lbcdllcr;	/* 0x.0e20 - LBC DLL Control Register */
	__be32  cpfor;		/* 0x.0e24 - L2 charge pump fuse override register */
	u8	rese28[0xf04 - 0xe28];
	__be32	srds1cr0;	/* 0x.0f04 - SerDes1 Control Register 0 */
	__be32	srds1cr1;	/* 0x.0f08 - SerDes1 Control Register 0 */
	u8	resf0c[0xf2c - 0xf0c];
	__be32  itcr;		/* 0x.0f2c - Internal transaction control register */
	u8	resf30[0xf40 - 0xf30];
	__be32	srds2cr0;	/* 0x.0f40 - SerDes2 Control Register 0 */
	__be32	srds2cr1;	/* 0x.0f44 - SerDes2 Control Register 0 */
} __attribute__ ((packed));


/* Alternate function signal multiplex control */
#define MPC85xx_PMUXCR_QE(x) (0x8000 >> (x))

#ifdef CONFIG_PPC_86xx

#define CCSR_GUTS_DMACR_DEV_SSI	0	/* DMA controller/channel set to SSI */
#define CCSR_GUTS_DMACR_DEV_IR	1	/* DMA controller/channel set to IR */

/*
 * Set the DMACR register in the GUTS
 *
 * The DMACR register determines the source of initiated transfers for each
 * channel on each DMA controller.  Rather than have a bunch of repetitive
 * macros for the bit patterns, we just have a function that calculates
 * them.
 *
 * guts: Pointer to GUTS structure
 * co: The DMA controller (0 or 1)
 * ch: The channel on the DMA controller (0, 1, 2, or 3)
 * device: The device to set as the source (CCSR_GUTS_DMACR_DEV_xx)
 */
static inline void guts_set_dmacr(struct ccsr_guts __iomem *guts,
	unsigned int co, unsigned int ch, unsigned int device)
{
	unsigned int shift = 16 + (8 * (1 - co) + 2 * (3 - ch));

	clrsetbits_be32(&guts->dmacr, 3 << shift, device << shift);
}

#define CCSR_GUTS_PMUXCR_LDPSEL		0x00010000
#define CCSR_GUTS_PMUXCR_SSI1_MASK	0x0000C000	/* Bitmask for SSI1 */
#define CCSR_GUTS_PMUXCR_SSI1_LA	0x00000000	/* Latched address */
#define CCSR_GUTS_PMUXCR_SSI1_HI	0x00004000	/* High impedance */
#define CCSR_GUTS_PMUXCR_SSI1_SSI	0x00008000	/* Used for SSI1 */
#define CCSR_GUTS_PMUXCR_SSI2_MASK	0x00003000	/* Bitmask for SSI2 */
#define CCSR_GUTS_PMUXCR_SSI2_LA	0x00000000	/* Latched address */
#define CCSR_GUTS_PMUXCR_SSI2_HI	0x00001000	/* High impedance */
#define CCSR_GUTS_PMUXCR_SSI2_SSI	0x00002000	/* Used for SSI2 */
#define CCSR_GUTS_PMUXCR_LA_22_25_LA	0x00000000	/* Latched Address */
#define CCSR_GUTS_PMUXCR_LA_22_25_HI	0x00000400	/* High impedance */
#define CCSR_GUTS_PMUXCR_DBGDRV		0x00000200	/* Signals not driven */
#define CCSR_GUTS_PMUXCR_DMA2_0		0x00000008
#define CCSR_GUTS_PMUXCR_DMA2_3		0x00000004
#define CCSR_GUTS_PMUXCR_DMA1_0		0x00000002
#define CCSR_GUTS_PMUXCR_DMA1_3		0x00000001

/*
 * Set the DMA external control bits in the GUTS
 *
 * The DMA external control bits in the PMUXCR are only meaningful for
 * channels 0 and 3.  Any other channels are ignored.
 *
 * guts: Pointer to GUTS structure
 * co: The DMA controller (0 or 1)
 * ch: The channel on the DMA controller (0, 1, 2, or 3)
 * value: the new value for the bit (0 or 1)
 */
static inline void guts_set_pmuxcr_dma(struct ccsr_guts __iomem *guts,
	unsigned int co, unsigned int ch, unsigned int value)
{
	if ((ch == 0) || (ch == 3)) {
		unsigned int shift = 2 * (co + 1) - (ch & 1) - 1;

		clrsetbits_be32(&guts->pmuxcr, 1 << shift, value << shift);
	}
}

#define CCSR_GUTS_CLKDVDR_PXCKEN	0x80000000
#define CCSR_GUTS_CLKDVDR_SSICKEN	0x20000000
#define CCSR_GUTS_CLKDVDR_PXCKINV	0x10000000
#define CCSR_GUTS_CLKDVDR_PXCKDLY_SHIFT 25
#define CCSR_GUTS_CLKDVDR_PXCKDLY_MASK	0x06000000
#define CCSR_GUTS_CLKDVDR_PXCKDLY(x) \
	(((x) & 3) << CCSR_GUTS_CLKDVDR_PXCKDLY_SHIFT)
#define CCSR_GUTS_CLKDVDR_PXCLK_SHIFT	16
#define CCSR_GUTS_CLKDVDR_PXCLK_MASK	0x001F0000
#define CCSR_GUTS_CLKDVDR_PXCLK(x) (((x) & 31) << CCSR_GUTS_CLKDVDR_PXCLK_SHIFT)
#define CCSR_GUTS_CLKDVDR_SSICLK_MASK	0x000000FF
#define CCSR_GUTS_CLKDVDR_SSICLK(x) ((x) & CCSR_GUTS_CLKDVDR_SSICLK_MASK)

#endif

#endif
#endif
