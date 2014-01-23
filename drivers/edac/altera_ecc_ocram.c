/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "altera_edac.h"

/* OCRAM ECC Management Group Defines */
#define ALTR_MAN_GRP_OCRAM_ECC_OFFSET	0x04
#define ALTR_OCR_ECC_EN_MASK		0x00000001
#define ALTR_OCR_ECC_INJS_MASK		0x00000002
#define ALTR_OCR_ECC_INJD_MASK		0x00000004
#define ALTR_OCR_ECC_SERR_MASK		0x00000008
#define ALTR_OCR_ECC_DERR_MASK		0x00000010

const struct ecc_mgr_prv_data ocramecc_data = {
	.ce_clear_mask = (ALTR_OCR_ECC_EN_MASK | ALTR_OCR_ECC_SERR_MASK),
	.ue_clear_mask = (ALTR_OCR_ECC_EN_MASK | ALTR_OCR_ECC_DERR_MASK),
};

