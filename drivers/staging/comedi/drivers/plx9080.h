/*
 * plx9080.h
 *
 * Copyright (C) 2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * I modified this file from the plx9060.h header for the
 * wanXL device driver in the linux kernel,
 * for the register offsets and bit definitions.  Made minor modifications,
 * added plx9080 registers and
 * stripped out stuff that was specifically for the wanXL driver.
 * Note: I've only made sure the definitions are correct as far
 * as I make use of them.  There are still various plx9060-isms
 * left in this header file.
 *
 ********************************************************************
 *
 * Copyright (C) 1999 RG Studio s.c.
 * Written by Krzysztof Halasa <khc@rgstudio.com.pl>
 *
 * Portions (C) SBE Inc., used by permission.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __COMEDI_PLX9080_H
#define __COMEDI_PLX9080_H

/*  descriptor block used for chained dma transfers */
struct plx_dma_desc {
	__le32 pci_start_addr;
	__le32 local_start_addr;
	/* transfer_size is in bytes, only first 23 bits of register are used */
	__le32 transfer_size;
	/*
	 * address of next descriptor (quad word aligned), plus some
	 * additional bits (see PLX_REG_DMADPR)
	 */
	__le32 next;
};

/**********************************************************************
**            Register Offsets and Bit Definitions
**
** Note: All offsets zero relative.  IE. Some standard base address
** must be added to the Register Number to properly access the register.
**
**********************************************************************/

/* Local Address Space 0 Range Register */
#define PLX_REG_LAS0RR		0x0000
/* Local Address Space 1 Range Register */
#define PLX_REG_LAS1RR		0x00f0

#define  LRNG_IO           0x00000001	/* Map to: 1=I/O, 0=Mem */
#define  LRNG_ANY32        0x00000000	/* Locate anywhere in 32 bit */
#define  LRNG_LT1MB        0x00000002	/* Locate in 1st meg */
#define  LRNG_ANY64        0x00000004	/* Locate anywhere in 64 bit */
/*  bits that specify range for memory io */
#define  LRNG_MEM_MASK     0xfffffff0
/*  bits that specify range for normal io */
#define  LRNG_IO_MASK     0xfffffffc

/* Local Address Space 0 Local Base Address (Remap) Register */
#define PLX_REG_LAS0BA		0x0004
/* Local Address Space 1 Local Base Address (Remap) Register */
#define PLX_REG_LAS1BA		0x00f4

#define  LMAP_EN           0x00000001	/* Enable slave decode */
/*  bits that specify decode for memory io */
#define  LMAP_MEM_MASK     0xfffffff0
/*  bits that specify decode bits for normal io */
#define  LMAP_IO_MASK     0xfffffffc

/* Mode/Arbitration Register */
#define PLX_REG_MARBR		0x0008
/* DMA Arbitration Register (alias of MARBR). */
#define PLX_REG_DMAARB		0x00ac

enum marb_bits {
	MARB_LLT_MASK = 0x000000ff,	/* Local Bus Latency Timer */
	MARB_LPT_MASK = 0x0000ff00,	/* Local Bus Pause Timer */
	MARB_LTEN = 0x00010000,	/* Latency Timer Enable */
	MARB_LPEN = 0x00020000,	/* Pause Timer Enable */
	MARB_BREQ = 0x00040000,	/* Local Bus BREQ Enable */
	MARB_DMA_PRIORITY_MASK = 0x00180000,
	/* local bus direct slave give up bus mode */
	MARB_LBDS_GIVE_UP_BUS_MODE = 0x00200000,
	/* direct slave LLOCKo# enable */
	MARB_DS_LLOCK_ENABLE = 0x00400000,
	MARB_PCI_REQUEST_MODE = 0x00800000,
	MARB_PCIV21_MODE = 0x01000000,	/* pci specification v2.1 mode */
	MARB_PCI_READ_NO_WRITE_MODE = 0x02000000,
	MARB_PCI_READ_WITH_WRITE_FLUSH_MODE = 0x04000000,
	/* gate local bus latency timer with BREQ */
	MARB_GATE_TIMER_WITH_BREQ = 0x08000000,
	MARB_PCI_READ_NO_FLUSH_MODE = 0x10000000,
	MARB_USE_SUBSYSTEM_IDS = 0x20000000,
};

/* Big/Little Endian Descriptor Register */
#define PLX_REG_BIGEND		0x000c

enum bigend_bits {
	/* use big endian ordering for configuration register accesses */
	BIGEND_CONFIG = 0x1,
	BIGEND_DIRECT_MASTER = 0x2,
	BIGEND_DIRECT_SLAVE_LOCAL0 = 0x4,
	BIGEND_ROM = 0x8,
	/*
	 * use byte lane consisting of most significant bits instead of
	 * least significant
	 */
	BIGEND_BYTE_LANE = 0x10,
	BIGEND_DIRECT_SLAVE_LOCAL1 = 0x20,
	BIGEND_DMA1 = 0x40,
	BIGEND_DMA0 = 0x80,
};

/*
** Note: The Expansion ROM  stuff is only relevant to the PC environment.
**       This expansion ROM code is executed by the host CPU at boot time.
**       For this reason no bit definitions are provided here.
 */
/* Expansion ROM Range Register */
#define PLX_REG_EROMRR		0x0010
/* Expansion ROM Local Base Address (Remap) Register */
#define PLX_REG_EROMBA		0x0014

/* Local Address Space 0/Expansion ROM Bus Region Descriptor Register */
#define PLX_REG_LBRD0		0x0018
/* Local Address Space 1 Bus Region Descriptor Register */
#define PLX_REG_LBRD1		0x00f8

#define  RGN_WIDTH         0x00000002	/* Local bus width bits */
#define  RGN_8BITS         0x00000000	/* 08 bit Local Bus */
#define  RGN_16BITS        0x00000001	/* 16 bit Local Bus */
#define  RGN_32BITS        0x00000002	/* 32 bit Local Bus */
#define  RGN_MWS           0x0000003C	/* Memory Access Wait States */
#define  RGN_0MWS          0x00000000
#define  RGN_1MWS          0x00000004
#define  RGN_2MWS          0x00000008
#define  RGN_3MWS          0x0000000C
#define  RGN_4MWS          0x00000010
#define  RGN_6MWS          0x00000018
#define  RGN_8MWS          0x00000020
#define  RGN_MRE           0x00000040	/* Memory Space Ready Input Enable */
#define  RGN_MBE           0x00000080	/* Memory Space Bterm Input Enable */
#define  RGN_READ_PREFETCH_DISABLE 0x00000100
#define  RGN_ROM_PREFETCH_DISABLE 0x00000200
#define  RGN_READ_PREFETCH_COUNT_ENABLE 0x00000400
#define  RGN_RWS           0x003C0000	/* Expn ROM Wait States */
#define  RGN_RRE           0x00400000	/* ROM Space Ready Input Enable */
#define  RGN_RBE           0x00800000	/* ROM Space Bterm Input Enable */
#define  RGN_MBEN          0x01000000	/* Memory Space Burst Enable */
#define  RGN_RBEN          0x04000000	/* ROM Space Burst Enable */
#define  RGN_THROT         0x08000000	/* De-assert TRDY when FIFO full */
#define  RGN_TRD           0xF0000000	/* Target Ready Delay /8 */

/* Local Range Register for Direct Master to PCI */
#define PLX_REG_DMRR		0x001c

/* Local Bus Base Address Register for Direct Master to PCI Memory */
#define PLX_REG_DMLBAM		0x0020

/* Local Base Address Register for Direct Master to PCI IO/CFG */
#define PLX_REG_DMLBAI		0x0024

/* PCI Base Address (Remap) Register for Direct Master to PCI Memory */
#define PLX_REG_DMPBAM		0x0028

#define  DMM_MAE           0x00000001	/* Direct Mstr Memory Acc Enable */
#define  DMM_IAE           0x00000002	/* Direct Mstr I/O Acc Enable */
#define  DMM_LCK           0x00000004	/* LOCK Input Enable */
#define  DMM_PF4           0x00000008	/* Prefetch 4 Mode Enable */
#define  DMM_THROT         0x00000010	/* Assert IRDY when read FIFO full */
#define  DMM_PAF0          0x00000000	/* Programmable Almost fill level */
#define  DMM_PAF1          0x00000020	/* Programmable Almost fill level */
#define  DMM_PAF2          0x00000040	/* Programmable Almost fill level */
#define  DMM_PAF3          0x00000060	/* Programmable Almost fill level */
#define  DMM_PAF4          0x00000080	/* Programmable Almost fill level */
#define  DMM_PAF5          0x000000A0	/* Programmable Almost fill level */
#define  DMM_PAF6          0x000000C0	/* Programmable Almost fill level */
#define  DMM_PAF7          0x000000D0	/* Programmable Almost fill level */
#define  DMM_MAP           0xFFFF0000	/* Remap Address Bits */

/* PCI Configuration Address Register for Direct Master to PCI IO/CFG */
#define PLX_REG_DMCFGA		0x002c

#define  CAR_CT0           0x00000000	/* Config Type 0 */
#define  CAR_CT1           0x00000001	/* Config Type 1 */
#define  CAR_REG           0x000000FC	/* Register Number Bits */
#define  CAR_FUN           0x00000700	/* Function Number Bits */
#define  CAR_DEV           0x0000F800	/* Device Number Bits */
#define  CAR_BUS           0x00FF0000	/* Bus Number Bits */
#define  CAR_CFG           0x80000000	/* Config Spc Access Enable */

/*
 * Mailbox Register N (N <= 7)
 *
 * Note that if the I2O feature is enabled (QSR[0] is set), Mailbox Register 0
 * is replaced by the Inbound Queue Port, and Mailbox Register 1 is replaced
 * by the Outbound Queue Port.  However, Mailbox Register 0 and 1 are always
 * accessible at alternative offsets if the I2O feature is enabled.
 */
#define PLX_REG_MBOX(n)		(0x0040 + (n) * 4)
#define PLX_REG_MBOX0		PLX_REG_MBOX(0)
#define PLX_REG_MBOX1		PLX_REG_MBOX(1)
#define PLX_REG_MBOX2		PLX_REG_MBOX(2)
#define PLX_REG_MBOX3		PLX_REG_MBOX(3)
#define PLX_REG_MBOX4		PLX_REG_MBOX(4)
#define PLX_REG_MBOX5		PLX_REG_MBOX(5)
#define PLX_REG_MBOX6		PLX_REG_MBOX(6)
#define PLX_REG_MBOX7		PLX_REG_MBOX(7)

/* Alternative offsets for Mailbox Registers 0 and 1 (in case I2O is enabled) */
#define PLX_REG_ALT_MBOX(n)	((n) < 2 ? 0x0078 + (n) * 4 : PLX_REG_MBOX(n))
#define PLX_REG_ALT_MBOX0	PLX_REG_ALT_MBOX(0)
#define PLX_REG_ALT_MBOX1	PLX_REG_ALT_MBOX(1)

/* PCI-to-Local Doorbell Register */
#define PLX_REG_P2LDBELL	0x0060

/* Local-to-PCI Doorbell Register */
#define PLX_REG_L2PDBELL	0x0064

/* Interrupt Control/Status Register */
#define PLX_REG_INTCSR		0x0068

#define  ICS_AERR          0x00000001	/* Assert LSERR on ABORT */
#define  ICS_PERR          0x00000002	/* Assert LSERR on Parity Error */
#define  ICS_SERR          0x00000004	/* Generate PCI SERR# */
#define  ICS_MBIE          0x00000008	/*  mailbox interrupt enable */
#define  ICS_PIE           0x00000100	/* PCI Interrupt Enable */
#define  ICS_PDIE          0x00000200	/* PCI Doorbell Interrupt Enable */
#define  ICS_PAIE          0x00000400	/* PCI Abort Interrupt Enable */
#define  ICS_PLIE          0x00000800	/* PCI Local Int Enable */
#define  ICS_RAE           0x00001000	/* Retry Abort Enable */
#define  ICS_PDIA          0x00002000	/* PCI Doorbell Interrupt Active */
#define  ICS_PAIA          0x00004000	/* PCI Abort Interrupt Active */
#define  ICS_LIA           0x00008000	/* Local Interrupt Active */
#define  ICS_LIE           0x00010000	/* Local Interrupt Enable */
#define  ICS_LDIE          0x00020000	/* Local Doorbell Int Enable */
#define  ICS_DMA0_E        0x00040000	/* DMA #0 Interrupt Enable */
#define  ICS_DMA1_E        0x00080000	/* DMA #1 Interrupt Enable */
#define  ICS_LDIA          0x00100000	/* Local Doorbell Int Active */
#define  ICS_DMA0_A        0x00200000	/* DMA #0 Interrupt Active */
#define  ICS_DMA1_A        0x00400000	/* DMA #1 Interrupt Active */
#define  ICS_BIA           0x00800000	/* BIST Interrupt Active */
#define  ICS_TA_DM         0x01000000	/* Target Abort - Direct Master */
#define  ICS_TA_DMA0       0x02000000	/* Target Abort - DMA #0 */
#define  ICS_TA_DMA1       0x04000000	/* Target Abort - DMA #1 */
#define  ICS_TA_RA         0x08000000	/* Target Abort - Retry Timeout */
/*  mailbox x is active */
#define  ICS_MBIA(x)       (0x10000000 << ((x) & 0x3))

/*
 * Serial EEPROM Control, PCI Command Codes, User I/O Control,
 * Init Control Register
 */
#define PLX_REG_CNTRL		0x006c

#define  CTL_RDMA          0x0000000E	/* DMA Read Command */
#define  CTL_WDMA          0x00000070	/* DMA Write Command */
#define  CTL_RMEM          0x00000600	/* Memory Read Command */
#define  CTL_WMEM          0x00007000	/* Memory Write Command */
#define  CTL_USERO         0x00010000	/* USERO output pin control bit */
#define  CTL_USERI         0x00020000	/* USERI input pin bit */
#define  CTL_EE_CLK        0x01000000	/* EEPROM Clock line */
#define  CTL_EE_CS         0x02000000	/* EEPROM Chip Select */
#define  CTL_EE_W          0x04000000	/* EEPROM Write bit */
#define  CTL_EE_R          0x08000000	/* EEPROM Read bit */
#define  CTL_EECHK         0x10000000	/* EEPROM Present bit */
#define  CTL_EERLD         0x20000000	/* EEPROM Reload Register */
#define  CTL_RESET         0x40000000	/* !! Adapter Reset !! */
#define  CTL_READY         0x80000000	/* Local Init Done */

/* PCI Permanent Configuration ID Register (hard-coded PLX vendor and device) */
#define PLX_REG_PCIHIDR		0x0070

/* PCI Permanent Revision ID Register (hard-coded silicon revision) (8-bit). */
#define PLX_REG_PCIHREV		0x0074

/* DMA Channel N Mode Register (N <= 1) */
#define PLX_REG_DMAMODE(n)	((n) ? PLX_REG_DMAMODE1 : PLX_REG_DMAMODE0)
#define PLX_REG_DMAMODE0	0x0080
#define PLX_REG_DMAMODE1	0x0094

#define  PLX_LOCAL_BUS_16_WIDE_BITS	0x1
#define  PLX_LOCAL_BUS_32_WIDE_BITS	0x3
#define  PLX_LOCAL_BUS_WIDTH_MASK	0x3
#define  PLX_DMA_EN_READYIN_BIT	0x40	/*  enable ready in input */
#define  PLX_EN_BTERM_BIT	0x80	/*  enable BTERM# input */
#define  PLX_DMA_LOCAL_BURST_EN_BIT	0x100	/*  enable local burst mode */
#define  PLX_EN_CHAIN_BIT	0x200	/*  enables chaining */
/*  enables interrupt on dma done */
#define  PLX_EN_DMA_DONE_INTR_BIT	0x400
/*  hold local address constant (don't increment) */
#define  PLX_LOCAL_ADDR_CONST_BIT	0x800
/*  enables demand-mode for dma transfer */
#define  PLX_DEMAND_MODE_BIT	0x1000
#define  PLX_EOT_ENABLE_BIT	0x4000
#define  PLX_STOP_MODE_BIT 0x8000
/*  routes dma interrupt to pci bus (instead of local bus) */
#define  PLX_DMA_INTR_PCI_BIT	0x20000

/* DMA Channel N PCI Address Register (N <= 1) */
#define PLX_REG_DMAPADR(n)	((n) ? PLX_REG_DMAPADR1 : PLX_REG_DMAPADR0)
#define PLX_REG_DMAPADR0	0x0084
#define PLX_REG_DMAPADR1	0x0098

/* DMA Channel N Local Address Register (N <= 1) */
#define PLX_REG_DMALADR(n)	((n) ? PLX_REG_DMALADR1 : PLX_REG_DMALADR0)
#define PLX_REG_DMALADR0	0x0088
#define PLX_REG_DMALADR1	0x009c

/* DMA Channel N Transfer Size (Bytes) Register (N <= 1) (first 23 bits) */
#define PLX_REG_DMASIZ(n)	((n) ? PLX_REG_DMASIZ1 : PLX_REG_DMASIZ0)
#define PLX_REG_DMASIZ0		0x008c
#define PLX_REG_DMASIZ1		0x00a0

/* DMA Channel N Descriptor Pointer Register (N <= 1) */
#define PLX_REG_DMADPR(n)	((n) ? PLX_REG_DMADPR1 : PLX_REG_DMADPR0)
#define PLX_REG_DMADPR0		0x0090
#define PLX_REG_DMADPR1		0x00a4

/*  descriptor is located in pci space (not local space) */
#define  PLX_DESC_IN_PCI_BIT	0x1
#define  PLX_END_OF_CHAIN_BIT	0x2	/*  end of chain bit */
/*  interrupt when this descriptor's transfer is finished */
#define  PLX_INTR_TERM_COUNT	0x4
/*  transfer from local to pci bus (not pci to local) */
#define  PLX_XFER_LOCAL_TO_PCI 0x8

/* DMA Channel N Command/Status Register (N <= 1) (8-bit) */
#define PLX_REG_DMACSR(n)	((n) ? PLX_REG_DMACSR1 : PLX_REG_DMACSR0)
#define PLX_REG_DMACSR0		0x00a8
#define PLX_REG_DMACSR1		0x00a9

#define  PLX_DMA_EN_BIT	0x1	/*  enable dma channel */
#define  PLX_DMA_START_BIT	0x2	/*  start dma transfer */
#define  PLX_DMA_ABORT_BIT	0x4	/*  abort dma transfer */
#define  PLX_CLEAR_DMA_INTR_BIT	0x8	/*  clear dma interrupt */
#define  PLX_DMA_DONE_BIT	0x10	/*  transfer done status bit */

/* DMA Threshold Register */
#define PLX_REG_DMATHR		0x00b0

/*
 * Messaging Queue Registers OPLFIS, OPLFIM, IQP, OQP, MQCR, QBAR, IFHPR,
 * IFTPR, IPHPR, IPTPR, OFHPR, OFTPR, OPHPR, OPTPR, and QSR have been omitted.
 * They are used by the I2O feature.  (IQP and OQP occupy the usual offsets of
 * the MBOX0 and MBOX1 registers if the I2O feature is enabled, but MBOX0 and
 * MBOX1 are accessible via alternative offsets.
 */

/* Queue Status/Control Register */
#define PLX_REG_QSR		0x00e8

/* Value of QSR after reset - disables I2O feature completely. */
#define PLX_QSR_VALUE_AFTER_RESET	0x00000050

/*
 * Accesses near the end of memory can cause the PLX chip
 * to pre-fetch data off of end-of-ram.  Limit the size of
 * memory so host-side accesses cannot occur.
 */

#define PLX_PREFETCH   32

static inline int plx9080_abort_dma(void __iomem *iobase, unsigned int channel)
{
	void __iomem *dma_cs_addr;
	u8 dma_status;
	const int timeout = 10000;
	unsigned int i;

	dma_cs_addr = iobase + PLX_REG_DMACSR(channel);

	/*  abort dma transfer if necessary */
	dma_status = readb(dma_cs_addr);
	if ((dma_status & PLX_DMA_EN_BIT) == 0)
		return 0;

	/*  wait to make sure done bit is zero */
	for (i = 0; (dma_status & PLX_DMA_DONE_BIT) && i < timeout; i++) {
		udelay(1);
		dma_status = readb(dma_cs_addr);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	/*  disable and abort channel */
	writeb(PLX_DMA_ABORT_BIT, dma_cs_addr);
	/*  wait for dma done bit */
	dma_status = readb(dma_cs_addr);
	for (i = 0; (dma_status & PLX_DMA_DONE_BIT) == 0 && i < timeout; i++) {
		udelay(1);
		dma_status = readb(dma_cs_addr);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	return 0;
}

#endif /* __COMEDI_PLX9080_H */
