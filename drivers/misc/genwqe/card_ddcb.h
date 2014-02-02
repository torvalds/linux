#ifndef __CARD_DDCB_H__
#define __CARD_DDCB_H__

/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@de.ibm.com>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <asm/byteorder.h>

#include "genwqe_driver.h"
#include "card_base.h"

/**
 * struct ddcb - Device Driver Control Block DDCB
 * @hsi:        Hardware software interlock
 * @shi:        Software hardware interlock. Hsi and shi are used to interlock
 *              software and hardware activities. We are using a compare and
 *              swap operation to ensure that there are no races when
 *              activating new DDCBs on the queue, or when we need to
 *              purge a DDCB from a running queue.
 * @acfunc:     Accelerator function addresses a unit within the chip
 * @cmd:        Command to work on
 * @cmdopts_16: Options for the command
 * @asiv:       Input data
 * @asv:        Output data
 *
 * The DDCB data format is big endian. Multiple consequtive DDBCs form
 * a DDCB queue.
 */
#define ASIV_LENGTH		104 /* Old specification without ATS field */
#define ASIV_LENGTH_ATS		96  /* New specification with ATS field */
#define ASV_LENGTH		64

struct ddcb {
	union {
		__be32 icrc_hsi_shi_32;	/* iCRC, Hardware/SW interlock */
		struct {
			__be16	icrc_16;
			u8	hsi;
			u8	shi;
		};
	};
	u8  pre;		/* Preamble */
	u8  xdir;		/* Execution Directives */
	__be16 seqnum_16;	/* Sequence Number */

	u8  acfunc;		/* Accelerator Function.. */
	u8  cmd;		/* Command. */
	__be16 cmdopts_16;	/* Command Options */
	u8  sur;		/* Status Update Rate */
	u8  psp;		/* Protection Section Pointer */
	__be16 rsvd_0e_16;	/* Reserved invariant */

	__be64 fwiv_64;		/* Firmware Invariant. */

	union {
		struct {
			__be64 ats_64;  /* Address Translation Spec */
			u8     asiv[ASIV_LENGTH_ATS]; /* New ASIV */
		} n;
		u8  __asiv[ASIV_LENGTH];	/* obsolete */
	};
	u8     asv[ASV_LENGTH];	/* Appl Spec Variant */

	__be16 rsvd_c0_16;	/* Reserved Variant */
	__be16 vcrc_16;		/* Variant CRC */
	__be32 rsvd_32;		/* Reserved unprotected */

	__be64 deque_ts_64;	/* Deque Time Stamp. */

	__be16 retc_16;		/* Return Code */
	__be16 attn_16;		/* Attention/Extended Error Codes */
	__be32 progress_32;	/* Progress indicator. */

	__be64 cmplt_ts_64;	/* Completion Time Stamp. */

	/* The following layout matches the new service layer format */
	__be32 ibdc_32;		/* Inbound Data Count  (* 256) */
	__be32 obdc_32;		/* Outbound Data Count (* 256) */

	__be64 rsvd_SLH_64;	/* Reserved for hardware */
	union {			/* private data for driver */
		u8	priv[8];
		__be64	priv_64;
	};
	__be64 disp_ts_64;	/* Dispatch TimeStamp */
} __attribute__((__packed__));

/* CRC polynomials for DDCB */
#define CRC16_POLYNOMIAL	0x1021

/*
 * SHI: Software to Hardware Interlock
 *   This 1 byte field is written by software to interlock the
 *   movement of one queue entry to another with the hardware in the
 *   chip.
 */
#define DDCB_SHI_INTR		0x04 /* Bit 2 */
#define DDCB_SHI_PURGE		0x02 /* Bit 1 */
#define DDCB_SHI_NEXT		0x01 /* Bit 0 */

/*
 * HSI: Hardware to Software interlock
 * This 1 byte field is written by hardware to interlock the movement
 * of one queue entry to another with the software in the chip.
 */
#define DDCB_HSI_COMPLETED	0x40 /* Bit 6 */
#define DDCB_HSI_FETCHED	0x04 /* Bit 2 */

/*
 * Accessing HSI/SHI is done 32-bit wide
 *   Normally 16-bit access would work too, but on some platforms the
 *   16 compare and swap operation is not supported. Therefore
 *   switching to 32-bit such that those platforms will work too.
 *
 *                                         iCRC HSI/SHI
 */
#define DDCB_INTR_BE32		cpu_to_be32(0x00000004)
#define DDCB_PURGE_BE32		cpu_to_be32(0x00000002)
#define DDCB_NEXT_BE32		cpu_to_be32(0x00000001)
#define DDCB_COMPLETED_BE32	cpu_to_be32(0x00004000)
#define DDCB_FETCHED_BE32	cpu_to_be32(0x00000400)

/* Definitions of DDCB presets */
#define DDCB_PRESET_PRE		0x80
#define ICRC_LENGTH(n)		((n) + 8 + 8 + 8)  /* used ASIV + hdr fields */
#define VCRC_LENGTH(n)		((n))		   /* used ASV */

/*
 * Genwqe Scatter Gather list
 *   Each element has up to 8 entries.
 *   The chaining element is element 0 cause of prefetching needs.
 */

/*
 * 0b0110 Chained descriptor. The descriptor is describing the next
 * descriptor list.
 */
#define SG_CHAINED		(0x6)

/*
 * 0b0010 First entry of a descriptor list. Start from a Buffer-Empty
 * condition.
 */
#define SG_DATA			(0x2)

/*
 * 0b0000 Early terminator. This is the last entry on the list
 * irregardless of the length indicated.
 */
#define SG_END_LIST		(0x0)

/**
 * struct sglist - Scatter gather list
 * @target_addr:       Either a dma addr of memory to work on or a
 *                     dma addr or a subsequent sglist block.
 * @len:               Length of the data block.
 * @flags:             See above.
 *
 * Depending on the command the GenWQE card can use a scatter gather
 * list to describe the memory it works on. Always 8 sg_entry's form
 * a block.
 */
struct sg_entry {
	__be64 target_addr;
	__be32 len;
	__be32 flags;
};

#endif /* __CARD_DDCB_H__ */
