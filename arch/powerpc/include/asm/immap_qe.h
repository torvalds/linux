/*
 * QUICC Engine (QE) Internal Memory Map.
 * The Internal Memory Map for devices with QE on them. This
 * is the superset of all QE devices (8360, etc.).

 * Copyright (C) 2006. Freescale Semicondutor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_POWERPC_IMMAP_QE_H
#define _ASM_POWERPC_IMMAP_QE_H
#ifdef __KERNEL__

#include <linux/kernel.h>
#include <asm/io.h>

#define QE_IMMAP_SIZE	(1024 * 1024)	/* 1MB from 1MB+IMMR */

/* QE I-RAM */
struct qe_iram {
	__be32	iadd;		/* I-RAM Address Register */
	__be32	idata;		/* I-RAM Data Register */
	u8	res0[0x78];
} __attribute__ ((packed));

/* QE Interrupt Controller */
struct qe_ic_regs {
	__be32	qicr;
	__be32	qivec;
	__be32	qripnr;
	__be32	qipnr;
	__be32	qipxcc;
	__be32	qipycc;
	__be32	qipwcc;
	__be32	qipzcc;
	__be32	qimr;
	__be32	qrimr;
	__be32	qicnr;
	u8	res0[0x4];
	__be32	qiprta;
	__be32	qiprtb;
	u8	res1[0x4];
	__be32	qricr;
	u8	res2[0x20];
	__be32	qhivec;
	u8	res3[0x1C];
} __attribute__ ((packed));

/* Communications Processor */
struct cp_qe {
	__be32	cecr;		/* QE command register */
	__be32	ceccr;		/* QE controller configuration register */
	__be32	cecdr;		/* QE command data register */
	u8	res0[0xA];
	__be16	ceter;		/* QE timer event register */
	u8	res1[0x2];
	__be16	cetmr;		/* QE timers mask register */
	__be32	cetscr;		/* QE time-stamp timer control register */
	__be32	cetsr1;		/* QE time-stamp register 1 */
	__be32	cetsr2;		/* QE time-stamp register 2 */
	u8	res2[0x8];
	__be32	cevter;		/* QE virtual tasks event register */
	__be32	cevtmr;		/* QE virtual tasks mask register */
	__be16	cercr;		/* QE RAM control register */
	u8	res3[0x2];
	u8	res4[0x24];
	__be16	ceexe1;		/* QE external request 1 event register */
	u8	res5[0x2];
	__be16	ceexm1;		/* QE external request 1 mask register */
	u8	res6[0x2];
	__be16	ceexe2;		/* QE external request 2 event register */
	u8	res7[0x2];
	__be16	ceexm2;		/* QE external request 2 mask register */
	u8	res8[0x2];
	__be16	ceexe3;		/* QE external request 3 event register */
	u8	res9[0x2];
	__be16	ceexm3;		/* QE external request 3 mask register */
	u8	res10[0x2];
	__be16	ceexe4;		/* QE external request 4 event register */
	u8	res11[0x2];
	__be16	ceexm4;		/* QE external request 4 mask register */
	u8	res12[0x3A];
	__be32	ceurnr;		/* QE microcode revision number register */
	u8	res13[0x244];
} __attribute__ ((packed));

/* QE Multiplexer */
struct qe_mux {
	__be32	cmxgcr;		/* CMX general clock route register */
	__be32	cmxsi1cr_l;	/* CMX SI1 clock route low register */
	__be32	cmxsi1cr_h;	/* CMX SI1 clock route high register */
	__be32	cmxsi1syr;	/* CMX SI1 SYNC route register */
	__be32	cmxucr[4];	/* CMX UCCx clock route registers */
	__be32	cmxupcr;	/* CMX UPC clock route register */
	u8	res0[0x1C];
} __attribute__ ((packed));

/* QE Timers */
struct qe_timers {
	u8	gtcfr1;		/* Timer 1 and Timer 2 global config register*/
	u8	res0[0x3];
	u8	gtcfr2;		/* Timer 3 and timer 4 global config register*/
	u8	res1[0xB];
	__be16	gtmdr1;		/* Timer 1 mode register */
	__be16	gtmdr2;		/* Timer 2 mode register */
	__be16	gtrfr1;		/* Timer 1 reference register */
	__be16	gtrfr2;		/* Timer 2 reference register */
	__be16	gtcpr1;		/* Timer 1 capture register */
	__be16	gtcpr2;		/* Timer 2 capture register */
	__be16	gtcnr1;		/* Timer 1 counter */
	__be16	gtcnr2;		/* Timer 2 counter */
	__be16	gtmdr3;		/* Timer 3 mode register */
	__be16	gtmdr4;		/* Timer 4 mode register */
	__be16	gtrfr3;		/* Timer 3 reference register */
	__be16	gtrfr4;		/* Timer 4 reference register */
	__be16	gtcpr3;		/* Timer 3 capture register */
	__be16	gtcpr4;		/* Timer 4 capture register */
	__be16	gtcnr3;		/* Timer 3 counter */
	__be16	gtcnr4;		/* Timer 4 counter */
	__be16	gtevr1;		/* Timer 1 event register */
	__be16	gtevr2;		/* Timer 2 event register */
	__be16	gtevr3;		/* Timer 3 event register */
	__be16	gtevr4;		/* Timer 4 event register */
	__be16	gtps;		/* Timer 1 prescale register */
	u8 res2[0x46];
} __attribute__ ((packed));

/* BRG */
struct qe_brg {
	__be32	brgc[16];	/* BRG configuration registers */
	u8	res0[0x40];
} __attribute__ ((packed));

/* SPI */
struct spi {
	u8	res0[0x20];
	__be32	spmode;		/* SPI mode register */
	u8	res1[0x2];
	u8	spie;		/* SPI event register */
	u8	res2[0x1];
	u8	res3[0x2];
	u8	spim;		/* SPI mask register */
	u8	res4[0x1];
	u8	res5[0x1];
	u8	spcom;		/* SPI command register */
	u8	res6[0x2];
	__be32	spitd;		/* SPI transmit data register (cpu mode) */
	__be32	spird;		/* SPI receive data register (cpu mode) */
	u8	res7[0x8];
} __attribute__ ((packed));

/* SI */
struct si1 {
	__be16	siamr1;		/* SI1 TDMA mode register */
	__be16	sibmr1;		/* SI1 TDMB mode register */
	__be16	sicmr1;		/* SI1 TDMC mode register */
	__be16	sidmr1;		/* SI1 TDMD mode register */
	u8	siglmr1_h;	/* SI1 global mode register high */
	u8	res0[0x1];
	u8	sicmdr1_h;	/* SI1 command register high */
	u8	res2[0x1];
	u8	sistr1_h;	/* SI1 status register high */
	u8	res3[0x1];
	__be16	sirsr1_h;	/* SI1 RAM shadow address register high */
	u8	sitarc1;	/* SI1 RAM counter Tx TDMA */
	u8	sitbrc1;	/* SI1 RAM counter Tx TDMB */
	u8	sitcrc1;	/* SI1 RAM counter Tx TDMC */
	u8	sitdrc1;	/* SI1 RAM counter Tx TDMD */
	u8	sirarc1;	/* SI1 RAM counter Rx TDMA */
	u8	sirbrc1;	/* SI1 RAM counter Rx TDMB */
	u8	sircrc1;	/* SI1 RAM counter Rx TDMC */
	u8	sirdrc1;	/* SI1 RAM counter Rx TDMD */
	u8	res4[0x8];
	__be16	siemr1;		/* SI1 TDME mode register 16 bits */
	__be16	sifmr1;		/* SI1 TDMF mode register 16 bits */
	__be16	sigmr1;		/* SI1 TDMG mode register 16 bits */
	__be16	sihmr1;		/* SI1 TDMH mode register 16 bits */
	u8	siglmg1_l;	/* SI1 global mode register low 8 bits */
	u8	res5[0x1];
	u8	sicmdr1_l;	/* SI1 command register low 8 bits */
	u8	res6[0x1];
	u8	sistr1_l;	/* SI1 status register low 8 bits */
	u8	res7[0x1];
	__be16	sirsr1_l;	/* SI1 RAM shadow address register low 16 bits*/
	u8	siterc1;	/* SI1 RAM counter Tx TDME 8 bits */
	u8	sitfrc1;	/* SI1 RAM counter Tx TDMF 8 bits */
	u8	sitgrc1;	/* SI1 RAM counter Tx TDMG 8 bits */
	u8	sithrc1;	/* SI1 RAM counter Tx TDMH 8 bits */
	u8	sirerc1;	/* SI1 RAM counter Rx TDME 8 bits */
	u8	sirfrc1;	/* SI1 RAM counter Rx TDMF 8 bits */
	u8	sirgrc1;	/* SI1 RAM counter Rx TDMG 8 bits */
	u8	sirhrc1;	/* SI1 RAM counter Rx TDMH 8 bits */
	u8	res8[0x8];
	__be32	siml1;		/* SI1 multiframe limit register */
	u8	siedm1;		/* SI1 extended diagnostic mode register */
	u8	res9[0xBB];
} __attribute__ ((packed));

/* SI Routing Tables */
struct sir {
	u8 	tx[0x400];
	u8	rx[0x400];
	u8	res0[0x800];
} __attribute__ ((packed));

/* USB Controller */
struct usb_ctlr {
	u8	usb_usmod;
	u8	usb_usadr;
	u8	usb_uscom;
	u8	res1[1];
	__be16  usb_usep[4];
	u8	res2[4];
	__be16	usb_usber;
	u8	res3[2];
	__be16	usb_usbmr;
	u8	res4[1];
	u8	usb_usbs;
	__be16	usb_ussft;
	u8	res5[2];
	__be16	usb_usfrn;
	u8	res6[0x22];
} __attribute__ ((packed));

/* MCC */
struct mcc {
	__be32	mcce;		/* MCC event register */
	__be32	mccm;		/* MCC mask register */
	__be32	mccf;		/* MCC configuration register */
	__be32	merl;		/* MCC emergency request level register */
	u8	res0[0xF0];
} __attribute__ ((packed));

/* QE UCC Slow */
struct ucc_slow {
	__be32	gumr_l;		/* UCCx general mode register (low) */
	__be32	gumr_h;		/* UCCx general mode register (high) */
	__be16	upsmr;		/* UCCx protocol-specific mode register */
	u8	res0[0x2];
	__be16	utodr;		/* UCCx transmit on demand register */
	__be16	udsr;		/* UCCx data synchronization register */
	__be16	ucce;		/* UCCx event register */
	u8	res1[0x2];
	__be16	uccm;		/* UCCx mask register */
	u8	res2[0x1];
	u8	uccs;		/* UCCx status register */
	u8	res3[0x24];
	__be16	utpt;
	u8	res4[0x52];
	u8	guemr;		/* UCC general extended mode register */
} __attribute__ ((packed));

/* QE UCC Fast */
struct ucc_fast {
	__be32	gumr;		/* UCCx general mode register */
	__be32	upsmr;		/* UCCx protocol-specific mode register */
	__be16	utodr;		/* UCCx transmit on demand register */
	u8	res0[0x2];
	__be16	udsr;		/* UCCx data synchronization register */
	u8	res1[0x2];
	__be32	ucce;		/* UCCx event register */
	__be32	uccm;		/* UCCx mask register */
	u8	uccs;		/* UCCx status register */
	u8	res2[0x7];
	__be32	urfb;		/* UCC receive FIFO base */
	__be16	urfs;		/* UCC receive FIFO size */
	u8	res3[0x2];
	__be16	urfet;		/* UCC receive FIFO emergency threshold */
	__be16	urfset;		/* UCC receive FIFO special emergency
				   threshold */
	__be32	utfb;		/* UCC transmit FIFO base */
	__be16	utfs;		/* UCC transmit FIFO size */
	u8	res4[0x2];
	__be16	utfet;		/* UCC transmit FIFO emergency threshold */
	u8	res5[0x2];
	__be16	utftt;		/* UCC transmit FIFO transmit threshold */
	u8	res6[0x2];
	__be16	utpt;		/* UCC transmit polling timer */
	u8	res7[0x2];
	__be32	urtry;		/* UCC retry counter register */
	u8	res8[0x4C];
	u8	guemr;		/* UCC general extended mode register */
} __attribute__ ((packed));

struct ucc {
	union {
		struct	ucc_slow slow;
		struct	ucc_fast fast;
		u8	res[0x200];	/* UCC blocks are 512 bytes each */
	};
} __attribute__ ((packed));

/* MultiPHY UTOPIA POS Controllers (UPC) */
struct upc {
	__be32	upgcr;		/* UTOPIA/POS general configuration register */
	__be32	uplpa;		/* UTOPIA/POS last PHY address */
	__be32	uphec;		/* ATM HEC register */
	__be32	upuc;		/* UTOPIA/POS UCC configuration */
	__be32	updc1;		/* UTOPIA/POS device 1 configuration */
	__be32	updc2;		/* UTOPIA/POS device 2 configuration */
	__be32	updc3;		/* UTOPIA/POS device 3 configuration */
	__be32	updc4;		/* UTOPIA/POS device 4 configuration */
	__be32	upstpa;		/* UTOPIA/POS STPA threshold */
	u8	res0[0xC];
	__be32	updrs1_h;	/* UTOPIA/POS device 1 rate select */
	__be32	updrs1_l;	/* UTOPIA/POS device 1 rate select */
	__be32	updrs2_h;	/* UTOPIA/POS device 2 rate select */
	__be32	updrs2_l;	/* UTOPIA/POS device 2 rate select */
	__be32	updrs3_h;	/* UTOPIA/POS device 3 rate select */
	__be32	updrs3_l;	/* UTOPIA/POS device 3 rate select */
	__be32	updrs4_h;	/* UTOPIA/POS device 4 rate select */
	__be32	updrs4_l;	/* UTOPIA/POS device 4 rate select */
	__be32	updrp1;		/* UTOPIA/POS device 1 receive priority low */
	__be32	updrp2;		/* UTOPIA/POS device 2 receive priority low */
	__be32	updrp3;		/* UTOPIA/POS device 3 receive priority low */
	__be32	updrp4;		/* UTOPIA/POS device 4 receive priority low */
	__be32	upde1;		/* UTOPIA/POS device 1 event */
	__be32	upde2;		/* UTOPIA/POS device 2 event */
	__be32	upde3;		/* UTOPIA/POS device 3 event */
	__be32	upde4;		/* UTOPIA/POS device 4 event */
	__be16	uprp1;
	__be16	uprp2;
	__be16	uprp3;
	__be16	uprp4;
	u8	res1[0x8];
	__be16	uptirr1_0;	/* Device 1 transmit internal rate 0 */
	__be16	uptirr1_1;	/* Device 1 transmit internal rate 1 */
	__be16	uptirr1_2;	/* Device 1 transmit internal rate 2 */
	__be16	uptirr1_3;	/* Device 1 transmit internal rate 3 */
	__be16	uptirr2_0;	/* Device 2 transmit internal rate 0 */
	__be16	uptirr2_1;	/* Device 2 transmit internal rate 1 */
	__be16	uptirr2_2;	/* Device 2 transmit internal rate 2 */
	__be16	uptirr2_3;	/* Device 2 transmit internal rate 3 */
	__be16	uptirr3_0;	/* Device 3 transmit internal rate 0 */
	__be16	uptirr3_1;	/* Device 3 transmit internal rate 1 */
	__be16	uptirr3_2;	/* Device 3 transmit internal rate 2 */
	__be16	uptirr3_3;	/* Device 3 transmit internal rate 3 */
	__be16	uptirr4_0;	/* Device 4 transmit internal rate 0 */
	__be16	uptirr4_1;	/* Device 4 transmit internal rate 1 */
	__be16	uptirr4_2;	/* Device 4 transmit internal rate 2 */
	__be16	uptirr4_3;	/* Device 4 transmit internal rate 3 */
	__be32	uper1;		/* Device 1 port enable register */
	__be32	uper2;		/* Device 2 port enable register */
	__be32	uper3;		/* Device 3 port enable register */
	__be32	uper4;		/* Device 4 port enable register */
	u8	res2[0x150];
} __attribute__ ((packed));

/* SDMA */
struct sdma {
	__be32	sdsr;		/* Serial DMA status register */
	__be32	sdmr;		/* Serial DMA mode register */
	__be32	sdtr1;		/* SDMA system bus threshold register */
	__be32	sdtr2;		/* SDMA secondary bus threshold register */
	__be32	sdhy1;		/* SDMA system bus hysteresis register */
	__be32	sdhy2;		/* SDMA secondary bus hysteresis register */
	__be32	sdta1;		/* SDMA system bus address register */
	__be32	sdta2;		/* SDMA secondary bus address register */
	__be32	sdtm1;		/* SDMA system bus MSNUM register */
	__be32	sdtm2;		/* SDMA secondary bus MSNUM register */
	u8	res0[0x10];
	__be32	sdaqr;		/* SDMA address bus qualify register */
	__be32	sdaqmr;		/* SDMA address bus qualify mask register */
	u8	res1[0x4];
	__be32	sdebcr;		/* SDMA CAM entries base register */
	u8	res2[0x38];
} __attribute__ ((packed));

/* Debug Space */
struct dbg {
	__be32	bpdcr;		/* Breakpoint debug command register */
	__be32	bpdsr;		/* Breakpoint debug status register */
	__be32	bpdmr;		/* Breakpoint debug mask register */
	__be32	bprmrr0;	/* Breakpoint request mode risc register 0 */
	__be32	bprmrr1;	/* Breakpoint request mode risc register 1 */
	u8	res0[0x8];
	__be32	bprmtr0;	/* Breakpoint request mode trb register 0 */
	__be32	bprmtr1;	/* Breakpoint request mode trb register 1 */
	u8	res1[0x8];
	__be32	bprmir;		/* Breakpoint request mode immediate register */
	__be32	bprmsr;		/* Breakpoint request mode serial register */
	__be32	bpemr;		/* Breakpoint exit mode register */
	u8	res2[0x48];
} __attribute__ ((packed));

/*
 * RISC Special Registers (Trap and Breakpoint).  These are described in
 * the QE Developer's Handbook.
 */
struct rsp {
	__be32 tibcr[16];	/* Trap/instruction breakpoint control regs */
	u8 res0[64];
	__be32 ibcr0;
	__be32 ibs0;
	__be32 ibcnr0;
	u8 res1[4];
	__be32 ibcr1;
	__be32 ibs1;
	__be32 ibcnr1;
	__be32 npcr;
	__be32 dbcr;
	__be32 dbar;
	__be32 dbamr;
	__be32 dbsr;
	__be32 dbcnr;
	u8 res2[12];
	__be32 dbdr_h;
	__be32 dbdr_l;
	__be32 dbdmr_h;
	__be32 dbdmr_l;
	__be32 bsr;
	__be32 bor;
	__be32 bior;
	u8 res3[4];
	__be32 iatr[4];
	__be32 eccr;		/* Exception control configuration register */
	__be32 eicr;
	u8 res4[0x100-0xf8];
} __attribute__ ((packed));

struct qe_immap {
	struct qe_iram		iram;		/* I-RAM */
	struct qe_ic_regs	ic;		/* Interrupt Controller */
	struct cp_qe		cp;		/* Communications Processor */
	struct qe_mux		qmx;		/* QE Multiplexer */
	struct qe_timers	qet;		/* QE Timers */
	struct spi		spi[0x2];	/* spi */
	struct mcc		mcc;		/* mcc */
	struct qe_brg		brg;		/* brg */
	struct usb_ctlr		usb;		/* USB */
	struct si1		si1;		/* SI */
	u8			res11[0x800];
	struct sir		sir;		/* SI Routing Tables */
	struct ucc		ucc1;		/* ucc1 */
	struct ucc		ucc3;		/* ucc3 */
	struct ucc		ucc5;		/* ucc5 */
	struct ucc		ucc7;		/* ucc7 */
	u8			res12[0x600];
	struct upc		upc1;		/* MultiPHY UTOPIA POS Ctrlr 1*/
	struct ucc		ucc2;		/* ucc2 */
	struct ucc		ucc4;		/* ucc4 */
	struct ucc		ucc6;		/* ucc6 */
	struct ucc		ucc8;		/* ucc8 */
	u8			res13[0x600];
	struct upc		upc2;		/* MultiPHY UTOPIA POS Ctrlr 2*/
	struct sdma		sdma;		/* SDMA */
	struct dbg		dbg;		/* 0x104080 - 0x1040FF
						   Debug Space */
	struct rsp		rsp[0x2];	/* 0x104100 - 0x1042FF
						   RISC Special Registers
						   (Trap and Breakpoint) */
	u8			res14[0x300];	/* 0x104300 - 0x1045FF */
	u8			res15[0x3A00];	/* 0x104600 - 0x107FFF */
	u8			res16[0x8000];	/* 0x108000 - 0x110000 */
	u8			muram[0xC000];	/* 0x110000 - 0x11C000
						   Multi-user RAM */
	u8			res17[0x24000];	/* 0x11C000 - 0x140000 */
	u8			res18[0xC0000];	/* 0x140000 - 0x200000 */
} __attribute__ ((packed));

extern struct qe_immap __iomem *qe_immr;
extern phys_addr_t get_qe_base(void);

static inline unsigned long immrbar_virt_to_phys(void *address)
{
	if ( ((u32)address >= (u32)qe_immr) &&
			((u32)address < ((u32)qe_immr + QE_IMMAP_SIZE)) )
		return (unsigned long)(address - (u32)qe_immr +
				(u32)get_qe_base());
	return (unsigned long)virt_to_phys(address);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_IMMAP_QE_H */
