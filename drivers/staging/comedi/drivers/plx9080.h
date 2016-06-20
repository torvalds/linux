/*
 * plx9080.h
 *
 * Copyright (C) 2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
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

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>

/**
 * struct plx_dma_desc - DMA descriptor format for PLX PCI 9080
 * @pci_start_addr:	PCI Bus address for transfer (DMAPADR).
 * @local_start_addr:	Local Bus address for transfer (DMALADR).
 * @transfer_size:	Transfer size in bytes (max 8 MiB) (DMASIZ).
 * @next:		Address of next descriptor + flags (DMADPR).
 *
 * Describes the format of a scatter-gather DMA descriptor for the PLX
 * PCI 9080.  All members are raw, little-endian register values that
 * will be transferred by the DMA engine from local or PCI memory into
 * corresponding registers for the DMA channel.
 *
 * The DMA descriptors must be aligned on a 16-byte boundary.  Bits 3:0
 * of @next contain flags describing the address space of the next
 * descriptor (local or PCI), an "end of chain" marker, an "interrupt on
 * terminal count" bit, and a data transfer direction.
 */
struct plx_dma_desc {
	__le32 pci_start_addr;
	__le32 local_start_addr;
	__le32 transfer_size;
	__le32 next;
};

/*
 * Register Offsets and Bit Definitions
 */

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
#define PLX_MARBR_TO_LT(r)	((r) & PLX_MARBR_LT_MASK)
/* Local Bus Pause Timer */
#define PLX_MARBR_PT(x)		(BIT(8) * ((x) & 0xff))
#define PLX_MARBR_PT_MASK	GENMASK(15, 8)
#define PLX_MARBR_TO_PT(r)	(((r) & PLX_MARBR_PT_MASK) >> 8)
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
 * Note: The Expansion ROM  stuff is only relevant to the PC environment.
 *       This expansion ROM code is executed by the host CPU at boot time.
 *       For this reason no bit definitions are provided here.
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
/* Memory Space Internal Wait States */
#define PLX_LBRD_MSIWS(x)	(BIT(2) * ((x) & 0xf))
#define PLX_LBRD_MSIWS_MASK	GENMASK(5, 2)
#define PLX_LBRD_TO_MSIWS(r)	(((r) & PLS_LBRD_MSIWS_MASK) >> 2)
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
#define PLX_LBRD_TO_PFCOUNT(r)	(((r) & PLX_LBRD_PFCOUNT_MASK) >> 11)
/* Expansion ROM Space Local Bus Width (LBRD0 only) */
#define PLX_LBRD0_EROMWIDTH8	(BIT(16) * 0)	/* 8 bits wide */
#define PLX_LBRD0_EROMWIDTH16	(BIT(16) * 1)	/* 16 bits wide */
#define PLX_LBRD0_EROMWIDTH32	(BIT(16) * 2)	/* 32 bits wide */
#define PLX_LBRD0_EROMWIDTH32A	(BIT(16) * 3)	/* 32 bits wide */
#define PLX_LBRD0_EROMWIDTH_MASK	GENMASK(17, 16)
/* Expansion ROM Space Internal Wait States (LBRD0 only) */
#define PLX_LBRD0_EROMIWS(x)	(BIT(18) * ((x) & 0xf))
#define PLX_LBRD0_EROMIWS_MASK	GENMASK(21, 18)
#define PLX_LBRD0_TO_EROMIWS(r)	(((r) & PLX_LBRD0_EROMIWS_MASK) >> 18)
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
#define PLX_LBRD0_TO_TRDELAY(r)	(((r) & PLX_LBRD0_TRDELAY_MASK) >> 28)

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
#define PLX_DMCFGA_TO_REGNUM(r)	(((r) & PLX_DMCFGA_REGNUM_MASK) >> 2)
/* Function Number */
#define PLX_DMCFGA_FUNCNUM(x)	(BIT(8) * ((x) & 0x7))
#define PLX_DMCFGA_FUNCNUM_MASK	GENMASK(10, 8)
#define PLX_DMCFGA_TO_FUNCNUM(r) (((r) & PLX_DMCFGA_FUNCNUM_MASK) >> 8)
/* Device Number */
#define PLX_DMCFGA_DEVNUM(x)	(BIT(11) * ((x) & 0x1f))
#define PLX_DMCFGA_DEVNUM_MASK	GENMASK(15, 11)
#define PLX_DMCFGA_TO_DEVNUM(r)	(((r) & PLX_DMCFGA_DEVNUM_MASK) >> 11)
/* Bus Number */
#define PLX_DMCFGA_BUSNUM(x)	(BIT(16) * ((x) & 0xff))
#define PLX_DMCFGA_BUSNUM_MASK	GENMASK(23, 16)
#define PLX_DMCFGA_TO_BUSNUM(r)	(((r) & PLX_DMCFGA_BUSNUM_MASK) >> 16)
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

/* PCI Read Command Code For DMA */
#define PLX_CNTRL_CCRDMA(x)	(BIT(0) * ((x) & 0xf))
#define PLX_CNTRL_CCRDMA_MASK	GENMASK(3, 0)
#define PLX_CNTRL_TO_CCRDMA(r)	((r) & PLX_CNTRL_CCRDMA_MASK)
#define PLX_CNTRL_CCRDMA_NORMAL	PLX_CNTRL_CCRDMA(14)	/* value after reset */
/* PCI Write Command Code For DMA 0 */
#define PLX_CNTRL_CCWDMA(x)	(BIT(4) * ((x) & 0xf))
#define PLX_CNTRL_CCWDMA_MASK	GENMASK(7, 4)
#define PLX_CNTRL_TO_CCWDMA(r)	(((r) & PLX_CNTRL_CCWDMA_MASK) >> 4)
#define PLX_CNTRL_CCWDMA_NORMAL	PLX_CNTRL_CCWDMA(7)	/* value after reset */
/* PCI Memory Read Command Code For Direct Master */
#define PLX_CNTRL_CCRDM(x)	(BIT(8) * ((x) & 0xf))
#define PLX_CNTRL_CCRDM_MASK	GENMASK(11, 8)
#define PLX_CNTRL_TO_CCRDM(r)	(((r) & PLX_CNTRL_CCRDM_MASK) >> 8)
#define PLX_CNTRL_CCRDM_NORMAL	PLX_CNTRL_CCRDM(6)	/* value after reset */
/* PCI Memory Write Command Code For Direct Master */
#define PLX_CNTRL_CCWDM(x)	(BIT(12) * ((x) & 0xf))
#define PLX_CNTRL_CCWDM_MASK	GENMASK(15, 12)
#define PLX_CNTRL_TO_CCWDM(r)	(((r) & PLX_CNTRL_CCWDM_MASK) >> 12)
#define PLX_CNTRL_CCWDM_NORMAL	PLX_CNTRL_CCWDM(7)	/* value after reset */
/* General Purpose Output (USERO) */
#define PLX_CNTRL_USERO		BIT(16)
/* General Purpose Input (USERI) (read-only) */
#define PLX_CNTRL_USERI		BIT(17)
/* Serial EEPROM Clock Output (EESK) */
#define PLX_CNTRL_EESK		BIT(24)
/* Serial EEPROM Chip Select Output (EECS) */
#define PLX_CNTRL_EECS		BIT(25)
/* Serial EEPROM Data Write Bit (EEDI (sic)) */
#define PLX_CNTRL_EEWB		BIT(26)
/* Serial EEPROM Data Read Bit (EEDO (sic)) (read-only) */
#define PLX_CNTRL_EERB		BIT(27)
/* Serial EEPROM Present (read-only) */
#define PLX_CNTRL_EEPRESENT	BIT(28)
/* Reload Configuration Registers from EEPROM */
#define PLX_CNTRL_EERELOAD	BIT(29)
/* PCI Adapter Software Reset (asserts LRESETo#) */
#define PLX_CNTRL_RESET		BIT(30)
/* Local Init Status (read-only) */
#define PLX_CNTRL_INITDONE	BIT(31)
/*
 * Combined command code stuff for convenience.
 */
#define PLX_CNTRL_CC_MASK	\
	(PLX_CNTRL_CCRDMA_MASK | PLX_CNTRL_CCWDMA_MASK | \
	 PLX_CNTRL_CCRDM_MASK | PLX_CNTRL_CCWDM_MASK)
#define PLX_CNTRL_CC_NORMAL	\
	(PLX_CNTRL_CCRDMA_NORMAL | PLX_CNTRL_CCWDMA_NORMAL | \
	 PLX_CNTRL_CCRDM_NORMAL | PLX_CNTRL_CCWDM_NORMAL) /* val after reset */

/* PCI Permanent Configuration ID Register (hard-coded PLX vendor and device) */
#define PLX_REG_PCIHIDR		0x0070

/* Hard-coded ID for PLX PCI 9080 */
#define PLX_PCIHIDR_9080	0x908010b5

/* PCI Permanent Revision ID Register (hard-coded silicon revision) (8-bit). */
#define PLX_REG_PCIHREV		0x0074

/* DMA Channel N Mode Register (N <= 1) */
#define PLX_REG_DMAMODE(n)	((n) ? PLX_REG_DMAMODE1 : PLX_REG_DMAMODE0)
#define PLX_REG_DMAMODE0	0x0080
#define PLX_REG_DMAMODE1	0x0094

/* Local Bus Width */
#define PLX_DMAMODE_WIDTH8	(BIT(0) * 0)	/* 8 bits wide */
#define PLX_DMAMODE_WIDTH16	(BIT(0) * 1)	/* 16 bits wide */
#define PLX_DMAMODE_WIDTH32	(BIT(0) * 2)	/* 32 bits wide */
#define PLX_DMAMODE_WIDTH32A	(BIT(0) * 3)	/* 32 bits wide */
#define PLX_DMAMODE_WIDTH_MASK	GENMASK(1, 0)
/* Internal Wait States */
#define PLX_DMAMODE_IWS(x)	(BIT(2) * ((x) & 0xf))
#define PLX_DMAMODE_IWS_MASK	GENMASK(5, 2)
#define PLX_DMAMODE_TO_IWS(r)	(((r) & PLX_DMAMODE_IWS_MASK) >> 2)
/* Ready Input Enable */
#define PLX_DMAMODE_READYIEN	BIT(6)
/* BTERM# Input Enable */
#define PLX_DMAMODE_BTERMIEN	BIT(7)
/* Local Burst Enable */
#define PLX_DMAMODE_BURSTEN	BIT(8)
/* Chaining Enable */
#define PLX_DMAMODE_CHAINEN	BIT(9)
/* Done Interrupt Enable */
#define PLX_DMAMODE_DONEIEN	BIT(10)
/* Hold Local Address Constant */
#define PLX_DMAMODE_LACONST	BIT(11)
/* Demand Mode */
#define PLX_DMAMODE_DEMAND	BIT(12)
/* Write And Invalidate Mode */
#define PLX_DMAMODE_WINVALIDATE	BIT(13)
/* DMA EOT Enable - enables EOT0# or EOT1# input pin */
#define PLX_DMAMODE_EOTEN	BIT(14)
/* DMA Stop Data Transfer Mode - 0:BLAST; 1:EOT asserted or DREQ deasserted */
#define PLX_DMAMODE_STOP	BIT(15)
/* DMA Clear Count Mode - count in descriptor cleared on completion */
#define PLX_DMAMODE_CLRCOUNT	BIT(16)
/* DMA Channel Interrupt Select - 0:local bus interrupt; 1:PCI interrupt */
#define PLX_DMAMODE_INTRPCI	BIT(17)

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

/* Descriptor Located In PCI Address Space (not local address space) */
#define PLX_DMADPR_DESCPCI	BIT(0)
/* End Of Chain */
#define PLX_DMADPR_CHAINEND	BIT(1)
/* Interrupt After Terminal Count */
#define PLX_DMADPR_TCINTR	BIT(2)
/* Direction Of Transfer Local Bus To PCI (not PCI to local) */
#define PLX_DMADPR_XFERL2P	BIT(3)
/* Next Descriptor Address Bits 31:4 (16 byte boundary) */
#define PLX_DMADPR_NEXT_MASK	GENMASK(31, 4)

/* DMA Channel N Command/Status Register (N <= 1) (8-bit) */
#define PLX_REG_DMACSR(n)	((n) ? PLX_REG_DMACSR1 : PLX_REG_DMACSR0)
#define PLX_REG_DMACSR0		0x00a8
#define PLX_REG_DMACSR1		0x00a9

/* Channel Enable */
#define PLX_DMACSR_ENABLE	BIT(0)
/* Channel Start - write 1 to start transfer (write-only) */
#define PLX_DMACSR_START	BIT(1)
/* Channel Abort - write 1 to abort transfer (write-only) */
#define PLX_DMACSR_ABORT	BIT(2)
/* Clear Interrupt - write 1 to clear DMA Channel Interrupt (write-only) */
#define PLX_DMACSR_CLEARINTR	BIT(3)
/* Channel Done - transfer complete/inactive (read-only) */
#define PLX_DMACSR_DONE		BIT(4)

/* DMA Threshold Register */
#define PLX_REG_DMATHR		0x00b0

/*
 * DMA Threshold constraints:
 * (C0PLAF + 1) + (C0PLAE + 1) <= 32
 * (C0LPAF + 1) + (C0LPAE + 1) <= 32
 * (C1PLAF + 1) + (C1PLAE + 1) <= 16
 * (C1LPAF + 1) + (C1LPAE + 1) <= 16
 */

/* DMA Channel 0 PCI-to-Local Almost Full (divided by 2, minus 1) */
#define PLX_DMATHR_C0PLAF(x)	(BIT(0) * ((x) & 0xf))
#define PLX_DMATHR_C0PLAF_MASK	GENMASK(3, 0)
#define PLX_DMATHR_TO_C0PLAF(r)	((r) & PLX_DMATHR_C0PLAF_MASK)
/* DMA Channel 0 Local-to-PCI Almost Empty (divided by 2, minus 1) */
#define PLX_DMATHR_C0LPAE(x)	(BIT(4) * ((x) & 0xf))
#define PLX_DMATHR_C0LPAE_MASK	GENMASK(7, 4)
#define PLX_DMATHR_TO_C0LPAE(r)	(((r) & PLX_DMATHR_C0LPAE_MASK) >> 4)
/* DMA Channel 0 Local-to-PCI Almost Full (divided by 2, minus 1) */
#define PLX_DMATHR_C0LPAF(x)	(BIT(8) * ((x) & 0xf))
#define PLX_DMATHR_C0LPAF_MASK	GENMASK(11, 8)
#define PLX_DMATHR_TO_C0LPAF(r)	(((r) & PLX_DMATHR_C0LPAF_MASK) >> 8)
/* DMA Channel 0 PCI-to-Local Almost Empty (divided by 2, minus 1) */
#define PLX_DMATHR_C0PLAE(x)	(BIT(12) * ((x) & 0xf))
#define PLX_DMATHR_C0PLAE_MASK	GENMASK(15, 12)
#define PLX_DMATHR_TO_C0PLAE(r)	(((r) & PLX_DMATHR_C0PLAE_MASK) >> 12)
/* DMA Channel 1 PCI-to-Local Almost Full (divided by 2, minus 1) */
#define PLX_DMATHR_C1PLAF(x)	(BIT(16) * ((x) & 0xf))
#define PLX_DMATHR_C1PLAF_MASK	GENMASK(19, 16)
#define PLX_DMATHR_TO_C1PLAF(r)	(((r) & PLX_DMATHR_C1PLAF_MASK) >> 16)
/* DMA Channel 1 Local-to-PCI Almost Empty (divided by 2, minus 1) */
#define PLX_DMATHR_C1LPAE(x)	(BIT(20) * ((x) & 0xf))
#define PLX_DMATHR_C1LPAE_MASK	GENMASK(23, 20)
#define PLX_DMATHR_TO_C1LPAE(r)	(((r) & PLX_DMATHR_C1LPAE_MASK) >> 20)
/* DMA Channel 1 Local-to-PCI Almost Full (divided by 2, minus 1) */
#define PLX_DMATHR_C1LPAF(x)	(BIT(24) * ((x) & 0xf))
#define PLX_DMATHR_C1LPAF_MASK	GENMASK(27, 24)
#define PLX_DMATHR_TO_C1LPAF(r)	(((r) & PLX_DMATHR_C1LPAF_MASK) >> 24)
/* DMA Channel 1 PCI-to-Local Almost Empty (divided by 2, minus 1) */
#define PLX_DMATHR_C1PLAE(x)	(BIT(28) * ((x) & 0xf))
#define PLX_DMATHR_C1PLAE_MASK	GENMASK(31, 28)
#define PLX_DMATHR_TO_C1PLAE(r)	(((r) & PLX_DMATHR_C1PLAE_MASK) >> 28)

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

/**
 * plx9080_abort_dma - Abort a PLX PCI 9080 DMA transfer
 * @iobase:	Remapped base address of configuration registers.
 * @channel:	DMA channel number (0 or 1).
 *
 * Aborts the DMA transfer on the channel, which must have been enabled
 * and started beforehand.
 *
 * Return:
 *	%0 on success.
 *	-%ETIMEDOUT if timed out waiting for abort to complete.
 */
static inline int plx9080_abort_dma(void __iomem *iobase, unsigned int channel)
{
	void __iomem *dma_cs_addr;
	u8 dma_status;
	const int timeout = 10000;
	unsigned int i;

	dma_cs_addr = iobase + PLX_REG_DMACSR(channel);

	/* abort dma transfer if necessary */
	dma_status = readb(dma_cs_addr);
	if ((dma_status & PLX_DMACSR_ENABLE) == 0)
		return 0;

	/* wait to make sure done bit is zero */
	for (i = 0; (dma_status & PLX_DMACSR_DONE) && i < timeout; i++) {
		udelay(1);
		dma_status = readb(dma_cs_addr);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	/* disable and abort channel */
	writeb(PLX_DMACSR_ABORT, dma_cs_addr);
	/* wait for dma done bit */
	dma_status = readb(dma_cs_addr);
	for (i = 0; (dma_status & PLX_DMACSR_DONE) == 0 && i < timeout; i++) {
		udelay(1);
		dma_status = readb(dma_cs_addr);
	}
	if (i == timeout)
		return -ETIMEDOUT;

	return 0;
}

#endif /* __COMEDI_PLX9080_H */
