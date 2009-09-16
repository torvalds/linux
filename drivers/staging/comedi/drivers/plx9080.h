/* plx9080.h
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
 * Copyright (C) 1999 RG Studio s.c., http://www.rgstudio.com.pl/
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
	volatile uint32_t pci_start_addr;
	volatile uint32_t local_start_addr;
	/* transfer_size is in bytes, only first 23 bits of register are used */
	volatile uint32_t transfer_size;
	/* address of next descriptor (quad word aligned), plus some
	 * additional bits (see PLX_DMA0_DESCRIPTOR_REG) */
	volatile uint32_t next;
};

/**********************************************************************
**            Register Offsets and Bit Definitions
**
** Note: All offsets zero relative.  IE. Some standard base address
** must be added to the Register Number to properly access the register.
**
**********************************************************************/

#define PLX_LAS0RNG_REG         0x0000	/* L, Local Addr Space 0 Range Register */
#define PLX_LAS1RNG_REG         0x00f0	/* L, Local Addr Space 1 Range Register */
#define  LRNG_IO           0x00000001	/* Map to: 1=I/O, 0=Mem */
#define  LRNG_ANY32        0x00000000	/* Locate anywhere in 32 bit */
#define  LRNG_LT1MB        0x00000002	/* Locate in 1st meg */
#define  LRNG_ANY64        0x00000004	/* Locate anywhere in 64 bit */
#define  LRNG_MEM_MASK     0xfffffff0	/*  bits that specify range for memory io */
#define  LRNG_IO_MASK     0xfffffffa	/*  bits that specify range for normal io */

#define PLX_LAS0MAP_REG         0x0004	/* L, Local Addr Space 0 Remap Register */
#define PLX_LAS1MAP_REG         0x00f4	/* L, Local Addr Space 1 Remap Register */
#define  LMAP_EN           0x00000001	/* Enable slave decode */
#define  LMAP_MEM_MASK     0xfffffff0	/*  bits that specify decode for memory io */
#define  LMAP_IO_MASK     0xfffffffa	/*  bits that specify decode bits for normal io */

/* Mode/Arbitration Register.
*/
#define PLX_MARB_REG         0x8	/* L, Local Arbitration Register */
#define PLX_DMAARB_REG      0xac
enum marb_bits {
	MARB_LLT_MASK = 0x000000ff,	/* Local Bus Latency Timer */
	MARB_LPT_MASK = 0x0000ff00,	/* Local Bus Pause Timer */
	MARB_LTEN = 0x00010000,	/* Latency Timer Enable */
	MARB_LPEN = 0x00020000,	/* Pause Timer Enable */
	MARB_BREQ = 0x00040000,	/* Local Bus BREQ Enable */
	MARB_DMA_PRIORITY_MASK = 0x00180000,
	MARB_LBDS_GIVE_UP_BUS_MODE = 0x00200000,	/* local bus direct slave give up bus mode */
	MARB_DS_LLOCK_ENABLE = 0x00400000,	/* direct slave LLOCKo# enable */
	MARB_PCI_REQUEST_MODE = 0x00800000,
	MARB_PCIv21_MODE = 0x01000000,	/* pci specification v2.1 mode */
	MARB_PCI_READ_NO_WRITE_MODE = 0x02000000,
	MARB_PCI_READ_WITH_WRITE_FLUSH_MODE = 0x04000000,
	MARB_GATE_TIMER_WITH_BREQ = 0x08000000,	/* gate local bus latency timer with BREQ */
	MARB_PCI_READ_NO_FLUSH_MODE = 0x10000000,
	MARB_USE_SUBSYSTEM_IDS = 0x20000000,
};

#define PLX_BIGEND_REG 0xc
enum bigend_bits {
	BIGEND_CONFIG = 0x1,	/* use big endian ordering for configuration register accesses */
	BIGEND_DIRECT_MASTER = 0x2,
	BIGEND_DIRECT_SLAVE_LOCAL0 = 0x4,
	BIGEND_ROM = 0x8,
	BIGEND_BYTE_LANE = 0x10,	/* use byte lane consisting of most significant bits instead of least significant */
	BIGEND_DIRECT_SLAVE_LOCAL1 = 0x20,
	BIGEND_DMA1 = 0x40,
	BIGEND_DMA0 = 0x80,
};

/* Note: The Expansion ROM  stuff is only relevant to the PC environment.
**       This expansion ROM code is executed by the host CPU at boot time.
**       For this reason no bit definitions are provided here.
*/
#define PLX_ROMRNG_REG         0x0010	/* L, Expn ROM Space Range Register */
#define PLX_ROMMAP_REG         0x0014	/* L, Local Addr Space Range Register */

#define PLX_REGION0_REG         0x0018	/* L, Local Bus Region 0 Descriptor */
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

#define PLX_REGION1_REG         0x00f8	/* L, Local Bus Region 1 Descriptor */

#define PLX_DMRNG_REG          0x001C	/* L, Direct Master Range Register */

#define PLX_LBAPMEM_REG        0x0020	/* L, Lcl Base Addr for PCI mem space */

#define PLX_LBAPIO_REG         0x0024	/* L, Lcl Base Addr for PCI I/O space */

#define PLX_DMMAP_REG          0x0028	/* L, Direct Master Remap Register */
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

#define PLX_CAR_REG            0x002C	/* L, Configuration Address Register */
#define  CAR_CT0           0x00000000	/* Config Type 0 */
#define  CAR_CT1           0x00000001	/* Config Type 1 */
#define  CAR_REG           0x000000FC	/* Register Number Bits */
#define  CAR_FUN           0x00000700	/* Function Number Bits */
#define  CAR_DEV           0x0000F800	/* Device Number Bits */
#define  CAR_BUS           0x00FF0000	/* Bus Number Bits */
#define  CAR_CFG           0x80000000	/* Config Spc Access Enable */

#define PLX_DBR_IN_REG         0x0060	/* L, PCI to Local Doorbell Register */

#define PLX_DBR_OUT_REG        0x0064	/* L, Local to PCI Doorbell Register */

#define PLX_INTRCS_REG         0x0068	/* L, Interrupt Control/Status Reg */
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
#define  ICS_MBIA(x)       (0x10000000 << ((x) & 0x3))	/*  mailbox x is active */

#define PLX_CONTROL_REG        0x006C	/* L, EEPROM Cntl & PCI Cmd Codes */
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

#define PLX_ID_REG	0x70	/*  hard-coded plx vendor and device ids */

#define PLX_REVISION_REG	0x74	/*  silicon revision */

#define PLX_DMA0_MODE_REG	0x80	/*  dma channel 0 mode register */
#define PLX_DMA1_MODE_REG	0x94	/*  dma channel 0 mode register */
#define  PLX_LOCAL_BUS_16_WIDE_BITS	0x1
#define  PLX_LOCAL_BUS_32_WIDE_BITS	0x3
#define  PLX_LOCAL_BUS_WIDTH_MASK	0x3
#define  PLX_DMA_EN_READYIN_BIT	0x40	/*  enable ready in input */
#define  PLX_EN_BTERM_BIT	0x80	/*  enable BTERM# input */
#define  PLX_DMA_LOCAL_BURST_EN_BIT	0x100	/*  enable local burst mode */
#define  PLX_EN_CHAIN_BIT	0x200	/*  enables chaining */
#define  PLX_EN_DMA_DONE_INTR_BIT	0x400	/*  enables interrupt on dma done */
#define  PLX_LOCAL_ADDR_CONST_BIT	0x800	/*  hold local address constant (don't increment) */
#define  PLX_DEMAND_MODE_BIT	0x1000	/*  enables demand-mode for dma transfer */
#define  PLX_EOT_ENABLE_BIT	0x4000
#define  PLX_STOP_MODE_BIT 0x8000
#define  PLX_DMA_INTR_PCI_BIT	0x20000	/*  routes dma interrupt to pci bus (instead of local bus) */

#define PLX_DMA0_PCI_ADDRESS_REG	0x84	/*  pci address that dma transfers start at */
#define PLX_DMA1_PCI_ADDRESS_REG	0x98

#define PLX_DMA0_LOCAL_ADDRESS_REG	0x88	/*  local address that dma transfers start at */
#define PLX_DMA1_LOCAL_ADDRESS_REG	0x9c

#define PLX_DMA0_TRANSFER_SIZE_REG	0x8c	/*  number of bytes to transfer (first 23 bits) */
#define PLX_DMA1_TRANSFER_SIZE_REG	0xa0

#define PLX_DMA0_DESCRIPTOR_REG	0x90	/*  descriptor pointer register */
#define PLX_DMA1_DESCRIPTOR_REG	0xa4
#define  PLX_DESC_IN_PCI_BIT	0x1	/*  descriptor is located in pci space (not local space) */
#define  PLX_END_OF_CHAIN_BIT	0x2	/*  end of chain bit */
#define  PLX_INTR_TERM_COUNT	0x4	/*  interrupt when this descriptor's transfer is finished */
#define  PLX_XFER_LOCAL_TO_PCI 0x8	/*  transfer from local to pci bus (not pci to local) */

#define PLX_DMA0_CS_REG	0xa8	/*  command status register */
#define PLX_DMA1_CS_REG	0xa9
#define  PLX_DMA_EN_BIT	0x1	/*  enable dma channel */
#define  PLX_DMA_START_BIT	0x2	/*  start dma transfer */
#define  PLX_DMA_ABORT_BIT	0x4	/*  abort dma transfer */
#define  PLX_CLEAR_DMA_INTR_BIT	0x8	/*  clear dma interrupt */
#define  PLX_DMA_DONE_BIT	0x10	/*  transfer done status bit */

#define PLX_DMA0_THRESHOLD_REG	0xb0	/*  command status register */

/*
 * Accesses near the end of memory can cause the PLX chip
 * to pre-fetch data off of end-of-ram.  Limit the size of
 * memory so host-side accesses cannot occur.
 */

#define PLX_PREFETCH   32

/*
 * The PCI Interface, via the PCI-9060 Chip, has up to eight (8) Mailbox
 * Registers.  The PUTS (Power-Up Test Suite) handles the board-side
 * interface/interaction using the first 4 registers.  Specifications for
 * the use of the full PUTS' command and status interface is contained
 * within a separate SBE PUTS Manual.  The Host-Side Device Driver only
 * uses a subset of the full PUTS interface.
 */

/*****************************************/
/***    MAILBOX #(-1) - MEM ACCESS STS ***/
/*****************************************/

#define MBX_STS_VALID      0x57584744	/* 'WXGD' */
#define MBX_STS_DILAV      0x44475857	/* swapped = 'DGXW' */

/*****************************************/
/***    MAILBOX #0  -  PUTS STATUS     ***/
/*****************************************/

#define MBX_STS_MASK       0x000000ff	/* PUTS Status Register bits */
#define MBX_STS_TMASK      0x0000000f	/* register bits for TEST number */

#define MBX_STS_PCIRESET   0x00000100	/* Host issued PCI reset request */
#define MBX_STS_BUSY       0x00000080	/* PUTS is in progress */
#define MBX_STS_ERROR      0x00000040	/* PUTS has failed */
#define MBX_STS_RESERVED   0x000000c0	/* Undefined -> status in transition.
					   We are in process of changing
					   bits; we SET Error bit before
					   RESET of Busy bit */

#define MBX_RESERVED_5     0x00000020	/* FYI: reserved/unused bit */
#define MBX_RESERVED_4     0x00000010	/* FYI: reserved/unused bit */

/******************************************/
/***    MAILBOX #1  -  PUTS COMMANDS    ***/
/******************************************/

/*
 * Any attempt to execute an unimplement command results in the PUTS
 * interface executing a NOOP and continuing as if the offending command
 * completed normally.  Note: this supplies a simple method to interrogate
 * mailbox command processing functionality.
 */

#define MBX_CMD_MASK       0xffff0000	/* PUTS Command Register bits */

#define MBX_CMD_ABORTJ     0x85000000	/* abort and jump */
#define MBX_CMD_RESETP     0x86000000	/* reset and pause at start */
#define MBX_CMD_PAUSE      0x87000000	/* pause immediately */
#define MBX_CMD_PAUSEC     0x88000000	/* pause on completion */
#define MBX_CMD_RESUME     0x89000000	/* resume operation */
#define MBX_CMD_STEP       0x8a000000	/* single step tests */

#define MBX_CMD_BSWAP      0x8c000000	/* identify byte swap scheme */
#define MBX_CMD_BSWAP_0    0x8c000000	/* use scheme 0 */
#define MBX_CMD_BSWAP_1    0x8c000001	/* use scheme 1 */

#define MBX_CMD_SETHMS     0x8d000000	/* setup host memory access window
					   size */
#define MBX_CMD_SETHBA     0x8e000000	/* setup host memory access base
					   address */
#define MBX_CMD_MGO        0x8f000000	/* perform memory setup and continue
					   (IE. Done) */
#define MBX_CMD_NOOP       0xFF000000	/* dummy, illegal command */

/*****************************************/
/***    MAILBOX #2  -  MEMORY SIZE     ***/
/*****************************************/

#define MBX_MEMSZ_MASK     0xffff0000	/* PUTS Memory Size Register bits */

#define MBX_MEMSZ_128KB    0x00020000	/* 128 kilobyte board */
#define MBX_MEMSZ_256KB    0x00040000	/* 256 kilobyte board */
#define MBX_MEMSZ_512KB    0x00080000	/* 512 kilobyte board */
#define MBX_MEMSZ_1MB      0x00100000	/* 1 megabyte board */
#define MBX_MEMSZ_2MB      0x00200000	/* 2 megabyte board */
#define MBX_MEMSZ_4MB      0x00400000	/* 4 megabyte board */
#define MBX_MEMSZ_8MB      0x00800000	/* 8 megabyte board */
#define MBX_MEMSZ_16MB     0x01000000	/* 16 megabyte board */

/***************************************/
/***    MAILBOX #2  -  BOARD TYPE    ***/
/***************************************/

#define MBX_BTYPE_MASK          0x0000ffff	/* PUTS Board Type Register */
#define MBX_BTYPE_FAMILY_MASK   0x0000ff00	/* PUTS Board Family Register */
#define MBX_BTYPE_SUBTYPE_MASK  0x000000ff	/* PUTS Board Subtype */

#define MBX_BTYPE_PLX9060       0x00000100	/* PLX family type */
#define MBX_BTYPE_PLX9080       0x00000300	/* PLX wanXL100s family type */

#define MBX_BTYPE_WANXL_4       0x00000104	/* wanXL400, 4-port */
#define MBX_BTYPE_WANXL_2       0x00000102	/* wanXL200, 2-port */
#define MBX_BTYPE_WANXL_1s      0x00000301	/* wanXL100s, 1-port */
#define MBX_BTYPE_WANXL_1t      0x00000401	/* wanXL100T1, 1-port */

/*****************************************/
/***    MAILBOX #3  -  SHMQ MAILBOX    ***/
/*****************************************/

#define MBX_SMBX_MASK           0x000000ff	/* PUTS SHMQ Mailbox bits */

/***************************************/
/***    GENERIC HOST-SIDE DRIVER     ***/
/***************************************/

#define MBX_ERR    0
#define MBX_OK     1

/* mailbox check routine - type of testing */
#define MBXCHK_STS      0x00	/* check for PUTS status */
#define MBXCHK_NOWAIT   0x01	/* dont care about PUTS status */

/* system allocates this many bytes for address mapping mailbox space */
#define MBX_ADDR_SPACE_360 0x80	/* wanXL100s/200/400 */
#define MBX_ADDR_MASK_360 (MBX_ADDR_SPACE_360-1)

static inline int plx9080_abort_dma(void *iobase, unsigned int channel)
{
	void *dma_cs_addr;
	uint8_t dma_status;
	const int timeout = 10000;
	unsigned int i;

	if (channel)
		dma_cs_addr = iobase + PLX_DMA1_CS_REG;
	else
		dma_cs_addr = iobase + PLX_DMA0_CS_REG;

	/*  abort dma transfer if necessary */
	dma_status = readb(dma_cs_addr);
	if ((dma_status & PLX_DMA_EN_BIT) == 0)
		return 0;

	/*  wait to make sure done bit is zero */
	for (i = 0; (dma_status & PLX_DMA_DONE_BIT) && i < timeout; i++) {
		udelay(1);
		dma_status = readb(dma_cs_addr);
	}
	if (i == timeout) {
		printk
		    ("plx9080: cancel() timed out waiting for dma %i done clear\n",
		     channel);
		return -ETIMEDOUT;
	}
	/*  disable and abort channel */
	writeb(PLX_DMA_ABORT_BIT, dma_cs_addr);
	/*  wait for dma done bit */
	dma_status = readb(dma_cs_addr);
	for (i = 0; (dma_status & PLX_DMA_DONE_BIT) == 0 && i < timeout; i++) {
		udelay(1);
		dma_status = readb(dma_cs_addr);
	}
	if (i == timeout) {
		printk
		    ("plx9080: cancel() timed out waiting for dma %i done set\n",
		     channel);
		return -ETIMEDOUT;
	}

	return 0;
}

#endif /* __COMEDI_PLX9080_H */
