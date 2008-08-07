/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997,2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 */

#ifndef _ASM_IA64_SN_L1_H
#define _ASM_IA64_SN_L1_H

/* brick type response codes */
#define L1_BRICKTYPE_PX         0x23            /* # */
#define L1_BRICKTYPE_PE         0x25            /* % */
#define L1_BRICKTYPE_N_p0       0x26            /* & */
#define L1_BRICKTYPE_IP45       0x34            /* 4 */
#define L1_BRICKTYPE_IP41       0x35            /* 5 */
#define L1_BRICKTYPE_TWISTER    0x36            /* 6 */ /* IP53 & ROUTER */
#define L1_BRICKTYPE_IX         0x3d            /* = */
#define L1_BRICKTYPE_IP34       0x61            /* a */
#define L1_BRICKTYPE_GA		0x62            /* b */
#define L1_BRICKTYPE_C          0x63            /* c */
#define L1_BRICKTYPE_OPUS_TIO	0x66		/* f */
#define L1_BRICKTYPE_I          0x69            /* i */
#define L1_BRICKTYPE_N          0x6e            /* n */
#define L1_BRICKTYPE_OPUS       0x6f		/* o */
#define L1_BRICKTYPE_P          0x70            /* p */
#define L1_BRICKTYPE_R          0x72            /* r */
#define L1_BRICKTYPE_CHI_CG     0x76            /* v */
#define L1_BRICKTYPE_X          0x78            /* x */
#define L1_BRICKTYPE_X2         0x79            /* y */
#define L1_BRICKTYPE_SA		0x5e            /* ^ */
#define L1_BRICKTYPE_PA		0x6a            /* j */
#define L1_BRICKTYPE_IA		0x6b            /* k */
#define L1_BRICKTYPE_ATHENA	0x2b            /* + */
#define L1_BRICKTYPE_DAYTONA	0x7a            /* z */
#define L1_BRICKTYPE_1932	0x2c		/* . */
#define L1_BRICKTYPE_191010	0x2e		/* , */

/* board type response codes */
#define L1_BOARDTYPE_IP69       0x0100          /* CA */
#define L1_BOARDTYPE_IP63       0x0200          /* CB */
#define L1_BOARDTYPE_BASEIO     0x0300          /* IB */
#define L1_BOARDTYPE_PCIE2SLOT  0x0400          /* IC */
#define L1_BOARDTYPE_PCIX3SLOT  0x0500          /* ID */
#define L1_BOARDTYPE_PCIXPCIE4SLOT 0x0600       /* IE */
#define L1_BOARDTYPE_ABACUS     0x0700          /* AB */
#define L1_BOARDTYPE_DAYTONA    0x0800          /* AD */
#define L1_BOARDTYPE_INVAL      (-1)            /* invalid brick type */

#endif /* _ASM_IA64_SN_L1_H */
