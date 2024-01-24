/*
 * TC956x PMA Header
 *
 * tc956x_pma.h
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:  *  05 Nov 2020 : Initial version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

#ifndef __TC956X_PMA_H__
#define __TC956X_PMA_H__

#include "common.h"

#define PMA_XGMAC_OFFSET	0x4000

/*PMA registers*/
#define XGMAC_PMA_GL_PM_CFG0				0x000001B8
#define XGMAC_PMA_CFG_0_1_R0				0x00001888
#define XGMAC_PMA_CFG_0_1_R1				0x00001890
#define XGMAC_PMA_CFG_0_1_R2				0x00001898
#define XGMAC_PMA_CFG_0_1_R3				0x000018A0
#define XGMAC_PMA_CFG_0_1_R4				0x000018A8

#define	XGMAC_PMA_HWT_REFCK_EN_R0			0x00001080
#define	XGMAC_PMA_HWT_REFCK_TERM_EN_R0		0x00001090
#define XGMAC_PMA_HWT_REFCK_R_EN_R1			0x00001094
#define XGMAC_PMA_HWT_REFCK_TERM_EN_R1		0x000010A4
#define XGMAC_PMA_HWT_REFCK_R_EN_R2			0x000010A8
#define XGMAC_PMA_HWT_REFCK_TERM_EN_R2		0x000010B8
#define XGMAC_PMA_HWT_REFCK_R_EN_R3			0x000010BC
#define XGMAC_PMA_HWT_REFCK_TERM_EN_R3		0x000010CC
#define XGMAC_PMA_HWT_REFCK_R_EN_R4			0x000010D0
#define XGMAC_PMA_HWT_REFCK_TERM_EN_R4		0x000010E0

#define XGMAC_PCS_GL_PC_CNT0				0x0000016C

/*PMA register values*/
#define XGMAC_PMA_OFFSET0					0x00000000
#define XGMAC_PMA_OFFSET1					0x0001EF04
#define XGMAC_PMA_OFFSET2					0x00000001

#endif /* __TC956X_PMA_H__ */
