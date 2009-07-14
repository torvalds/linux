/*
 * SH-2A UBC definitions
 *
 * Copyright (C) 2008 Kieran Bingham
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __ASM_CPU_SH2A_UBC_H
#define __ASM_CPU_SH2A_UBC_H

#define UBC_BARA                0xfffc0400
#define UBC_BAMRA               0xfffc0404
#define UBC_BBRA                0xfffc04a0	/* 16 bit access */
#define UBC_BDRA                0xfffc0408
#define UBC_BDMRA               0xfffc040c

#define UBC_BARB                0xfffc0410
#define UBC_BAMRB               0xfffc0414
#define UBC_BBRB                0xfffc04b0	/* 16 bit access */
#define UBC_BDRB                0xfffc0418
#define UBC_BDMRB               0xfffc041c

#define UBC_BRCR                0xfffc04c0

#endif /* __ASM_CPU_SH2A_UBC_H */
