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

#define PLX_LASRR_IO		BIT(0)		/* Map to: 1=I/O, 0=Mem */
#define PLX_LASRR_ANY32		(BIT(1) * 0)	/* Locate anywhere in 32 bit */
#define PLX_LASRR_LT1MB		(BIT(1) * 1)	/* Locate in 1st meg */
#define PLX_LASRR_ANY64		(BIT(1) * 2)	/* Locate anywhere in 64 bit */
#define PLX_LASRR_MLOC_MASK	GENMASK(2, 1)	/* Memory location bits */
#define PLX_LASRR_PREFETCH	BIT(3)		/* Memory is prefetchable */
/* bits that specify range for memory space decode bits */
#define PLX_LASRR_MEM_MASK	GENMASK(31, 4)
/* bits that specify range for i/o space decode bits */
#define PLX_LASRR_IO_MASK	GENMASK(31, 2)

/* Local Address Space 0 Local Base Address (Remap) Register */
#define PLX_REG_LAS0BA		0x0004
/* Local Address Space 1 Local Base Address (Remap) Register */
#define PLX_REG_LAS1BA		0x00f4

#define PLX_LASBA_EN		BIT(0)		/* Enable slave decode */
/* bits that specify local base address for memory space */
#define PLX_LASBA_MEM_MASK	GENMASK(31, 4)
/* bits that specify local base address for i/o space */
#define PLX_LASBA_IO_MASK	GENMASK(31, 2)

/* Mode/Arbitration Register */
#define PLX_REG_MARBR		0x0008
/* DMA Arbitration Register (alias of MARBR). */
#define PLX_REG_DMAARB		0x00ac

/* Local Bus Latency Timer */
#define PLX_MARBR_LT(x)		(BIT(0) * ((x) & 0xff))
#define PLX_MARBR_LT_MASK	GENMASK(7, 0)
#define PLX_MARBR_LT_SHIFT	0
/* Local Bus Pause Timer */
#define PLX_MARBR_PT(x)		(BIT(8) * ((x) & 0xff))
#define PLX_MARBR_PT_MASK	GENMASK(15, 8)
#define PLX_MARBR_PT_SHIFT	8
/* Local Bus Latency Timer Enable */
#define PLX_MARBR_LTEN		BIT(16)
/* Local Bus Pause Timer Enable */
#define PLX_MARBR_PTEN		BIT(17)
/* Local Bus BREQ Enable */
#define PLX_MARBR_BREQEN	BIT(18)
/* DMA Channel Priority */
#define PLX_MARBR_PRIO_ROT	(BIT(19) * 0)	/* Rotational priority */
#define PLX_MARBR_PRIO_DMA0	(BIT(19) * 1)	/* DMA channel 0 has priority */
#define PLX_MARBR_PRIO_DMA1	(BIT(19) * 2)	/* DMA channel 1 has priority */
#define PLX_MARBR_PRIO_MASK	GENMASK(20, 19)
/* Local Bus Direct Slave Give Up Bus Mode */
#define PLX_MARBR_DSGUBM	BIT(21)
/* Direct Slace LLOCKo# Enable */
#define PLX_MARBR_DSLLOCKOEN	BIT(22)
/* PCI Request Mode */
#define PLX_MARBR_PCIREQM	BIT(23)
/* PCI Specification v2.1 Mode */
#define PLX_MARBR_PCIV21M	BIT(24)
/* PCI Read No Write Mode */
#define PLX_MARBR_PCIRNWM	BIT(25)
/* PCI Read with Write Flush Mode */
#define PLX_MARBR_PCIRWFM	BIT(26)
/* Gate Local Bus Latency Timer with BREQ */
#define PLX_MARBR_GLTBREQ	BIT(27)
/* PCI Read No Flush Mode */
#define PLX_MARBR_PCIRNFM	BIT(28)
/*
 * Make reads from PCI Configuration register 0 return Subsystem ID and
 * Subsystem Vendor ID instead of Device ID and Vendor ID
 */
#define PLX_MARBR_SUBSYSIDS	BIT(29)

/* Big/Little Endian Descriptor Register */
#define PLX_REG_BIGEND		0x000c

/* Configuration Register Big Endian Mode */
#define PLX_BIGEND_CONFIG	BIT(0)
/* Direct Master Big Endian Mode */
#define PLX_BIGEND_DM		BIT(1)
/* Direct Slave Address Space 0 Big Endian Mode */
#define PLX_BIGEND_DSAS0	BIT(2)
/* Direct Slave Expansion ROM Big Endian Mode */
#define PLX_BIGEND_EROM		BIT(3)
/* Big Endian Byte Lane Mode - use most significant byte lanes */
#define PLX_BIGEND_BEBLM	BIT(4)
/* Direct Slave Address Space 1 Big Endian Mode */
#define PLX_BIGEND_DSAS1	BIT(5)
/* DMA Channel 1 Big Endian Mode */
#define PLX_BIGEND_DMA1		BIT(6)
/* DMA Channel 0 Big Endian Mode */
#define PLX_BIGEND_DMA0		BIT(7)
/* DMA Channel N Big Endian Mode (N <= 1) */
#define PLX_BIGEND_DMA(n)	((n) ? PLX_BIGEND_DMA1 : PLX_BIGEND_DMA0)

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

/* Memory Space Local Bus Width */
#define PLX_LBRD_MSWIDTH8	(BIT(0) * 0)	/* 8 bits wide */
#define PLX_LBRD_MSWIDTH16	(BIT(0) * 1)	/* 16 bits wide */
#define PLX_LBRD_MSWIDTH32	(BIT(0) * 2)	/* 32 bits wide */
#define PLX_LBRD_MSWIDTH32A	(BIT(0) * 3)	/* 32 bits wide */
#define PLX_LBRD_MSWIDTH_MASK	GENMASK(1, 0)
#define PLX_LBRD_MSWIDTH_SHIFT	0
/* Memory Space Internal Wait States */
#define PLX_LBRD_MSIWS(x)	(BIT(2) * ((x) & 0xf))
#define PLX_LBRD_MSIWS_MASK	GENMASK(5, 2)
#define PLX_LBRD_MSIWS_SHIFT	2
/* Memory Space Ready Input Enable */
#define PLX_LBRD_MSREADYIEN	BIT(6)
/* Memory Space BTERM# Input Enable */
#define PLX_LBRD_MSBTERMIEN	BIT(7)
/* Memory Space 0 Prefetch Disable (LBRD0 only) */
#define PLX_LBRD0_MSPREDIS	BIT(8)
/* Memory Space 1 Burst Enable (LBRD1 only) */
#define PLX_LBRD1_MSBURSTEN	BIT(8)
/* Expansion ROM Space Prefetch Disable (LBRD0 only) */
#define PLX_LBRD0_EROMPREDIS	BIT(9)
/* Memory Space 1 Prefetch Disable (LBRD1 only) */
#define PLX_LBRD1_MSPREDIS	BIT(9)
/* Read Prefetch Count Enable */
#define PLX_LBRD_RPFCOUNTEN	BIT(10)
/* Prefetch Counter */
#define PLX_LBRD_PFCOUNT(x)	(BIT(11) * ((x) & 0xf))
#define PLX_LBRD_PFCOUNT_MASK	GENMASK(14, 11)
#define PLX_LBRD_PFCOUNT_SHIFT	11
/* Expansion ROM Space Local Bus Width (LBRD0 only) */
#define PLX_LBRD0_EROMWIDTH8	(BIT(16) * 0)	/* 8 bits wide */
#define PLX_LBRD0_EROMWIDTH16	(BIT(16) * 1)	/* 16 bits wide */
#define PLX_LBRD0_EROMWIDTH32	(BIT(16) * 2)	/* 32 bits wide */
#define PLX_LBRD0_EROMWIDTH32A	(BIT(16) * 3)	/* 32 bits wide */
#define PLX_LBRD0_EROMWIDTH_MASK	GENMASK(17, 16)
#define PLX_LBRD0_EROMWIDTH_SHIFT	16
/* Expansion ROM Space Internal Wait States (LBRD0 only) */
#define PLX_LBRD0_EROMIWS(x)	(BIT(18) * ((x) & 0xf))
#define PLX_LBRD0_EROMIWS_MASK	GENMASK(21, 18)
#define PLX_LBRD0_EROMIWS_SHIFT	18
/* Expansion ROM Space Ready Input Enable (LBDR0 only) */
#define PLX_LBRD0_EROMREADYIEN	BIT(22)
/* Expansion ROM Space BTERM# Input Enable (LBRD0 only) */
#define PLX_LBRD0_EROMBTERMIEN	BIT(23)
/* Memory Space 0 Burst Enable (LBRD0 only) */
#define PLX_LBRD0_MSBURSTEN	BIT(24)
/* Extra Long Load From Serial EEPROM  (LBRD0 only) */
#define PLX_LBRD0_EELONGLOAD	BIT(25)
/* Expansion ROM Space Burst Enable (LBRD0 only) */
#define PLX_LBRD0_EROMBURSTEN	BIT(26)
/* Direct Slave PCI Write Mode - assert TRDY# when FIFO full (LBRD0 only) */
#define PLX_LBRD0_DSWMTRDY	BIT(27)
/* PCI Target Retry Delay Clocks / 8 (LBRD0 only) */
#define PLX_LBRD0_TRDELAY(x)	(BIT(28) * ((x) & 0xF))
#define PLX_LBRD0_TRDELAY_MASK	GENMASK(31, 28)
#define PLX_LBRD0_TRDELAY_SHIFT	28

/* Local Range Register for Direct Master to PCI */
#define PLX_REG_DMRR		0x001c

/* Local Bus Base Address Register for Direct Master to PCI Memory */
#define PLX_REG_DMLBAM		0x0020

/* Local Base Address Register for Direct Master to PCI IO/CFG */
#define PLX_REG_DMLBAI		0x0024

/* PCI Base Address (Remap) Register for Direct Master to PCI Memory */
#define PLX_REG_DMPBAM		0x0028

/* Direct Master Memory Access Enable */
#define PLX_DMPBAM_MEMACCEN	BIT(0)
/* Direct Master I/O Access Enable */
#define PLX_DMPBAM_IOACCEN	BIT(1)
/* LLOCK# Input Enable */
#define PLX_DMPBAM_LLOCKIEN	BIT(2)
/* Direct Master Read Prefetch Size Control (bits 12, 3) */
#define PLX_DMPBAM_RPSIZECONT	((BIT(12) * 0) | (BIT(3) * 0))
#define PLX_DMPBAM_RPSIZE4	((BIT(12) * 0) | (BIT(3) * 1))
#define PLX_DMPBAM_RPSIZE8	((BIT(12) * 1) | (BIT(3) * 0))
#define PLX_DMPBAM_RPSIZE16	((BIT(12) * 1) | (BIT(3) * 1))
#define PLX_DMPBAM_RPSIZE_MASK	(BIT(12) | BIT(3))
/* Direct Master PCI Read Mode - deassert IRDY when FIFO full */
#define PLX_DMPBAM_RMIRDY	BIT(4)
/* Programmable Almost Full Level (bits 10, 8:5) */
#define PLX_DMPBAM_PAFL(x)	((BIT(10) * !!((x) & 0x10)) | \
				 (BIT(5) * ((x) & 0xf)))
#define PLX_DMPBAM_TO_PAFL(v)	((((BIT(10) & (v)) >> 1) | \
				  (GENMASK(8, 5) & (v))) >> 5)
#define PLX_DMPBAM_PAFL_MASK	(BIT(10) | GENMASK(8, 5))
/* Write And Invalidate Mode */
#define PLX_DMPBAM_WIM		BIT(9)
/* Direct Master Prefetch Limit */
#define PLX_DBPBAM_PFLIMIT	BIT(11)
/* I/O Remap Select */
#define PLX_DMPBAM_IOREMAPSEL	BIT(13)
/* Direct Master Write Delay */
#define PLX_DMPBAM_WDELAYNONE	(BIT(14) * 0)
#define PLX_DMPBAM_WDELAY4	(BIT(14) * 1)
#define PLX_DMPBAM_WDELAY8	(BIT(14) * 2)
#define PLX_DMPBAM_WDELAY16	(BIT(14) * 3)
#define PLX_DMPBAM_WDELAY_MASK	GENMASK(15, 14)
/* Remap of Local-to-PCI Space Into PCI Address Space */
#define PLX_DMPBAM_REMAP_MASK	GENMASK(31, 16)

/* PCI Configuration Address Register for Direct Master to PCI IO/CFG */
#define PLX_REG_DMCFGA		0x002c

/* Congiguration Type */
#define PLX_DMCFGA_TYPE0	(BIT(0) * 0)
#define PLX_DMCFGA_TYPE1	(BIT(0) * 1)
#define PLX_DMCFGA_TYPE_MASK	GENMASK(1, 0)
/* Register Number */
#define PLX_DMCFGA_REGNUM(x)	(BIT(2) * ((x) & 0x3f))
#define PLX_DMCFGA_REGNUM_MASK	GENMASK(7, 2)
#define PLX_DMCFGA_REGNUM_SHIFT	2
/* Function Number */
#define PLX_DMCFGA_FUNCNUM(x)	(BIT(8) * ((x) & 0x7))
#define PLX_DMCFGA_FUNCNUM_MASK	GENMASK(10, 8)
#define PLX_DMCFGA_FUNCNUM_SHIFT 8
/* Device Number */
#define PLX_DMCFGA_DEVNUM(x)	(BIT(11) * ((x) & 0x1f))
#define PLX_DMCFGA_DEVNUM_MASK	GENMASK(15, 11)
#define PLX_DMCFGA_DEVNUM_SHIFT	11
/* Bus Number */
#define PLX_DMCFGA_BUSNUM(x)	(BIT(16) * ((x) & 0xff))
#define PLX_DMCFGA_BUSNUM_MASK	GENMASK(23, 16)
#define PLX_DMCFGA_BUSNUM_SHIFT	16
/* Configuration Enable */
#define PLX_DMCFGA_CONFIGEN	BIT(31)

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

/* Enable Local Bus LSERR# when PCI Bus Target Abort or Master Abort occurs */
#define PLX_INTCSR_LSEABORTEN	BIT(0)
/* Enable Local Bus LSERR# when PCI parity error occurs */
#define PLX_INTCSR_LSEPARITYEN	BIT(1)
/* Generate PCI Bus SERR# when set to 1 */
#define PLX_INTCSR_GENSERR	BIT(2)
/* Mailbox Interrupt Enable (local bus interrupts on PCI write to MBOX0-3) */
#define PLX_INTCSR_MBIEN	BIT(3)
/* PCI Interrupt Enable */
#define PLX_INTCSR_PIEN		BIT(8)
/* PCI Doorbell Interrupt Enable */
#define PLX_INTCSR_PDBIEN	BIT(9)
/* PCI Abort Interrupt Enable */
#define PLX_INTCSR_PABORTIEN	BIT(10)
/* PCI Local Interrupt Enable */
#define PLX_INTCSR_PLIEN	BIT(11)
/* Retry Abort Enable (for diagnostic purposes only) */
#define PLX_INTCSR_RAEN		BIT(12)
/* PCI Doorbell Interrupt Active (read-only) */
#define PLX_INTCSR_PDBIA	BIT(13)
/* PCI Abort Interrupt Active (read-only) */
#define PLX_INTCSR_PABORTIA	BIT(14)
/* Local Interrupt (LINTi#) Active (read-only) */
#define PLX_INTCSR_PLIA		BIT(15)
/* Local Interrupt Output (LINTo#) Enable */
#define PLX_INTCSR_LIOEN	BIT(16)
/* Local Doorbell Interrupt Enable */
#define PLX_INTCSR_LDBIEN	BIT(17)
/* DMA Channel 0 Interrupt Enable */
#define PLX_INTCSR_DMA0IEN	BIT(18)
/* DMA Channel 1 Interrupt Enable */
#define PLX_INTCSR_DMA1IEN	BIT(19)
/* DMA Channel N Interrupt Enable (N <= 1) */
#define PLX_INTCSR_DMAIEN(n)	((n) ? PLX_INTCSR_DMA1IEN : PLX_INTCSR_DMA0IEN)
/* Local Doorbell Interrupt Active (read-only) */
#define PLX_INTCSR_LDBIA	BIT(20)
/* DMA Channel 0 Interrupt Active (read-only) */
#define PLX_INTCSR_DMA0IA	BIT(21)
/* DMA Channel 1 Interrupt Active (read-only) */
#define PLX_INTCSR_DMA1IA	BIT(22)
/* DMA Channel N Interrupt Active (N <= 1) (read-only) */
#define PLX_INTCSR_DMAIA(n)	((n) ? PLX_INTCSR_DMA1IA : PLX_INTCSR_DMA0IA)
/* BIST Interrupt Active (read-only) */
#define PLX_INTCSR_BISTIA	BIT(23)
/* Direct Master Not Bus Master During Master Or Target Abort (read-only) */
#define PLX_INTCSR_ABNOTDM	BIT(24)
/* DMA Channel 0 Not Bus Master During Master Or Target Abort (read-only) */
#define PLX_INTCSR_ABNOTDMA0	BIT(25)
/* DMA Channel 1 Not Bus Master During Master Or Target Abort (read-only) */
#define PLX_INTCSR_ABNOTDMA1	BIT(26)
/* DMA Channel N Not Bus Master During Master Or Target Abort (read-only) */
#define PLX_INTCSR_ABNOTDMA(n)	((n) ? PLX_INTCSR_ABNOTDMA1 \
				     : PLX_INTCSR_ABNOTDMA0)
/* Target Abort Not Generated After 256 Master Retries (read-only) */
#define PLX_INTCSR_ABNOTRETRY	BIT(27)
/* PCI Wrote Mailbox 0 (enabled if bit 3 set) (read-only) */
#define PLX_INTCSR_MB0IA	BIT(28)
/* PCI Wrote Mailbox 1 (enabled if bit 3 set) (read-only) */
#define PLX_INTCSR_MB1IA	BIT(29)
/* PCI Wrote Mailbox 2 (enabled if bit 3 set) (read-only) */
#define PLX_INTCSR_MB2IA	BIT(30)
/* PCI Wrote Mailbox 3 (enabled if bit 3 set) (read-only) */
#define PLX_INTCSR_MB3IA	BIT(31)
/* PCI Wrote Mailbox N (N <= 3) (enabled if bit 3 set) (read-only) */
#define PLX_INTCSR_MBIA(n)	BIT(28 + (n))

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
