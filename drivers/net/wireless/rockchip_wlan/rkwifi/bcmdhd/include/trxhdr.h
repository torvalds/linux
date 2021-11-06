/*
 * TRX image file header format.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _TRX_HDR_H
#define _TRX_HDR_H

#include <typedefs.h>

#define TRX_MAGIC	0x30524448	/* "HDR0" */
#define TRX_MAX_LEN	0x3B0000	/* Max length */
#define TRX_NO_HEADER	1		/* Do not write TRX header */
#define TRX_GZ_FILES	0x2     /* Contains up to TRX_MAX_OFFSET individual gzip files */
#define TRX_EMBED_UCODE	0x8	/* Trx contains embedded ucode image */
#define TRX_ROMSIM_IMAGE	0x10	/* Trx contains ROM simulation image */
#define TRX_UNCOMP_IMAGE	0x20	/* Trx contains uncompressed rtecdc.bin image */
#define TRX_BOOTLOADER		0x40	/* the image is a bootloader */

#define TRX_V1		1
#define TRX_V1_MAX_OFFSETS	3		/* V1: Max number of individual files */

#ifndef BCMTRXV2
#define TRX_VERSION	TRX_V1		/* Version 1 */
#define TRX_MAX_OFFSET TRX_V1_MAX_OFFSETS
#endif

/* BMAC Host driver/application like bcmdl need to support both Ver 1 as well as
 * Ver 2 of trx header. To make it generic, trx_header is structure is modified
 * as below where size of "offsets" field will vary as per the TRX version.
 * Currently, BMAC host driver and bcmdl are modified to support TRXV2 as well.
 * To make sure, other applications like "dhdl" which are yet to be enhanced to support
 * TRXV2 are not broken, new macro and structure defintion take effect only when BCMTRXV2
 * is defined.
 */
struct trx_header {
	uint32 magic;		/* "HDR0" */
	uint32 len;		/* Length of file including header */
	uint32 crc32;		/* 32-bit CRC from flag_version to end of file */
	uint32 flag_version;	/* 0:15 flags, 16:31 version */
#ifndef BCMTRXV2
	uint32 offsets[TRX_MAX_OFFSET];	/* Offsets of partitions from start of header */
#else
	uint32 offsets[1];	/* Offsets of partitions from start of header */
#endif
};

#ifdef BCMTRXV2
#define TRX_VERSION		TRX_V2		/* Version 2 */
#define TRX_MAX_OFFSET  TRX_V2_MAX_OFFSETS

#define TRX_V2		2
/* V2: Max number of individual files
 * To support SDR signature + Config data region
 */
#define TRX_V2_MAX_OFFSETS	5
#define SIZEOF_TRXHDR_V1	(sizeof(struct trx_header)+(TRX_V1_MAX_OFFSETS-1)*sizeof(uint32))
#define SIZEOF_TRXHDR_V2	(sizeof(struct trx_header)+(TRX_V2_MAX_OFFSETS-1)*sizeof(uint32))
#ifdef IL_BIGENDIAN
#define TRX_VER(trx)		(ltoh32((trx)->flag_version>>16))
#else
#define TRX_VER(trx)		((trx)->flag_version>>16)
#endif
#define ISTRX_V1(trx)		(TRX_VER(trx) == TRX_V1)
#define ISTRX_V2(trx)		(TRX_VER(trx) == TRX_V2)
/* For V2, return size of V2 size: others, return V1 size */
#define SIZEOF_TRX(trx)	    (ISTRX_V2(trx) ? SIZEOF_TRXHDR_V2: SIZEOF_TRXHDR_V1)
#else
#define SIZEOF_TRX(trx)	    (sizeof(struct trx_header))
#endif /* BCMTRXV2 */

/* Compatibility */
typedef struct trx_header TRXHDR, *PTRXHDR;

#endif /* _TRX_HDR_H */
