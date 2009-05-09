/*
 * Largely written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 *
 * $FreeBSD: src/sys/cam/scsi/scsi_all.h,v 1.21 2002/10/08 17:12:44 ken Exp $
 *
 * Copyright (c) 2003 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id$
 */

#ifndef	_AICLIB_H
#define _AICLIB_H

struct scsi_sense
{
	uint8_t opcode;
	uint8_t byte2;
	uint8_t unused[2];
	uint8_t length;
	uint8_t control;
};

#define		SCSI_REV_0		0
#define		SCSI_REV_CCS		1
#define		SCSI_REV_2		2
#define		SCSI_REV_SPC		3
#define		SCSI_REV_SPC2		4

struct scsi_sense_data
{
	uint8_t error_code;
#define	SSD_ERRCODE			0x7F
#define		SSD_CURRENT_ERROR	0x70
#define		SSD_DEFERRED_ERROR	0x71
#define	SSD_ERRCODE_VALID	0x80	
	uint8_t segment;
	uint8_t flags;
#define	SSD_KEY				0x0F
#define		SSD_KEY_NO_SENSE	0x00
#define		SSD_KEY_RECOVERED_ERROR	0x01
#define		SSD_KEY_NOT_READY	0x02
#define		SSD_KEY_MEDIUM_ERROR	0x03
#define		SSD_KEY_HARDWARE_ERROR	0x04
#define		SSD_KEY_ILLEGAL_REQUEST	0x05
#define		SSD_KEY_UNIT_ATTENTION	0x06
#define		SSD_KEY_DATA_PROTECT	0x07
#define		SSD_KEY_BLANK_CHECK	0x08
#define		SSD_KEY_Vendor_Specific	0x09
#define		SSD_KEY_COPY_ABORTED	0x0a
#define		SSD_KEY_ABORTED_COMMAND	0x0b		
#define		SSD_KEY_EQUAL		0x0c
#define		SSD_KEY_VOLUME_OVERFLOW	0x0d
#define		SSD_KEY_MISCOMPARE	0x0e
#define		SSD_KEY_RESERVED	0x0f			
#define	SSD_ILI		0x20
#define	SSD_EOM		0x40
#define	SSD_FILEMARK	0x80
	uint8_t info[4];
	uint8_t extra_len;
	uint8_t cmd_spec_info[4];
	uint8_t add_sense_code;
	uint8_t add_sense_code_qual;
	uint8_t fru;
	uint8_t sense_key_spec[3];
#define	SSD_SCS_VALID		0x80
#define SSD_FIELDPTR_CMD	0x40
#define SSD_BITPTR_VALID	0x08
#define SSD_BITPTR_VALUE	0x07
#define SSD_MIN_SIZE 18
	uint8_t extra_bytes[14];
#define SSD_FULL_SIZE sizeof(struct scsi_sense_data)
};

/*
 * Status Byte
 */
#define	SCSI_STATUS_OK			0x00
#define	SCSI_STATUS_CHECK_COND		0x02
#define	SCSI_STATUS_COND_MET		0x04
#define	SCSI_STATUS_BUSY		0x08
#define SCSI_STATUS_INTERMED		0x10
#define SCSI_STATUS_INTERMED_COND_MET	0x14
#define SCSI_STATUS_RESERV_CONFLICT	0x18
#define SCSI_STATUS_CMD_TERMINATED	0x22	/* Obsolete in SAM-2 */
#define SCSI_STATUS_QUEUE_FULL		0x28
#define SCSI_STATUS_ACA_ACTIVE		0x30
#define SCSI_STATUS_TASK_ABORTED	0x40

/************************* Large Disk Handling ********************************/
static inline int
aic_sector_div(sector_t capacity, int heads, int sectors)
{
	/* ugly, ugly sector_div calling convention.. */
	sector_div(capacity, (heads * sectors));
	return (int)capacity;
}

static inline uint32_t
scsi_4btoul(uint8_t *bytes)
{
	uint32_t rv;

	rv = (bytes[0] << 24) |
	     (bytes[1] << 16) |
	     (bytes[2] << 8) |
	     bytes[3];
	return (rv);
}

/* Macros for generating the elements of the PCI ID tables. */

#define GETID(v, s) (unsigned)(((v) >> (s)) & 0xFFFF ?: PCI_ANY_ID)

#define ID_C(x, c)						\
{								\
	GETID(x,32), GETID(x,48), GETID(x,0), GETID(x,16),	\
	(c) << 8, 0xFFFF00, 0					\
}

#define ID2C(x)                          \
	ID_C(x, PCI_CLASS_STORAGE_SCSI), \
	ID_C(x, PCI_CLASS_STORAGE_RAID)

#define IDIROC(x)  ((x) | ~ID_ALL_IROC_MASK)

/* Generate IDs for all 16 possibilites.
 * The argument has already masked out
 * the 4 least significant bits of the device id.
 * (e.g., mask: ID_9005_GENERIC_MASK).
 */
#define ID16(x)                          \
	ID(x),                           \
	ID((x) | 0x0001000000000000ull), \
	ID((x) | 0x0002000000000000ull), \
	ID((x) | 0x0003000000000000ull), \
	ID((x) | 0x0004000000000000ull), \
	ID((x) | 0x0005000000000000ull), \
	ID((x) | 0x0006000000000000ull), \
	ID((x) | 0x0007000000000000ull), \
	ID((x) | 0x0008000000000000ull), \
	ID((x) | 0x0009000000000000ull), \
	ID((x) | 0x000A000000000000ull), \
	ID((x) | 0x000B000000000000ull), \
	ID((x) | 0x000C000000000000ull), \
	ID((x) | 0x000D000000000000ull), \
	ID((x) | 0x000E000000000000ull), \
	ID((x) | 0x000F000000000000ull)

#endif /*_AICLIB_H */
