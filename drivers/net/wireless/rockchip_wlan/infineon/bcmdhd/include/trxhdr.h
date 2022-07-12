/*
 * TRX image file header format.
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: trxhdr.h 520026 2014-12-10 01:29:40Z $
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

#define TRX_VERSION_BIT_OFFSET  16
#define R_COMP_SIZE             32	/* R component - 32 bytes */
#define S_COMP_SIZE             32	/* S component - 32 bytes */
#define ECDSA_SIGNATURE_SIZE (R_COMP_SIZE + S_COMP_SIZE) /* r[32bytes] and s[32bytes] components */

enum {
	TRX_V4_OFFS_SIGN_INFO_IDX                   = 0,
	TRX_V4_OFFS_DATA_FOR_SIGN1_IDX              = 1,
	TRX_V4_OFFS_DATA_FOR_SIGN2_IDX              = 2,
	TRX_V4_OFFS_ROOT_MODULUS_IDX                = 3,
	TRX_V4_OFFS_ROOT_EXPONENT_IDX               = 67,
	TRX_V4_OFFS_CONT_MODULUS_IDX                = 68,
	TRX_V4_OFFS_CONT_EXPONENT_IDX               = 132,
	TRX_V4_OFFS_HASH_FW_IDX                     = 133,
	TRX_V4_OFFS_FW_LEN_IDX                      = 149,
	TRX_V4_OFFS_TR_RST_IDX                      = 150,
	TRX_V4_OFFS_FW_VER_FOR_ANTIROOLBACK_IDX     = 151,
	TRX_V4_OFFS_IV_IDX                          = 152,
	TRX_V4_OFFS_NONCE_IDX                       = 160,
	TRX_V4_OFFS_SIGN_INFO2_IDX                  = 168,
	TRX_V4_OFFS_MAX_IDX
};

#if defined BCMTRXV4
#define TRX_VERSION     4                       /* Version 4 */
#define TRX_MAX_OFFSET  (TRX_V4_OFFS_MAX_IDX)

#elif defined BCMTRXV3
#define TRX_VERSION     3		/* Version 3 */
#define TRX_MAX_OFFSET  4		/* FW size + Jump_addr + NVRAM size[if exist]
					 * + signature size
					 */
#elif defined BCMTRXV2
#define TRX_VERSION     2		/* Version 2 */
#define TRX_MAX_OFFSET  5		/* Max number of individual files
					 * to support SDR signature + Config data region
					 */
#else
#define TRX_VERSION     1		/* Version 1 */
#define TRX_MAX_OFFSET  3		/* Max number of individual files */
#endif /* BCMTRXV3 BCMTRXV2 BCMTRXV4 */

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
	uint32 offsets[TRX_MAX_OFFSET];	/* Offsets of partitions from start of header */
};

/* bootloader makes special use of trx header "offsets" array */
enum {
	TRX_OFFS_FW_LEN_IDX     = 0,	/* Size of the fw; used in uncompressed case */
	TRX_OFFS_TR_RST_IDX     = 1,	/* RAM address[tr_rst] for jump to after download */
	TRX_OFFS_DSG_LEN_IDX    = 2,	/* Len of digital signature */
	TRX_OFFS_CFG_LEN_IDX    = 3	/* Len of config region */
};

/* Compatibility */
typedef struct trx_header TRXHDR, *PTRXHDR;

#endif /* _TRX_HDR_H */
