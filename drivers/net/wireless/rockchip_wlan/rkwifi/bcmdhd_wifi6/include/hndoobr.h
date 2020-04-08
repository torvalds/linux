/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HND OOBR interface header
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 * $Id: hndoobr.h 772387 2018-07-17 00:58:05Z $
 */

#ifndef _hndoobr_h_
#define _hndoobr_h_

#include <typedefs.h>
#include <siutils.h>

/* for 'srcpidx' of hnd_oobr_get_intr_config() */
#define HND_CORE_MAIN_INTR	0
#define HND_CORE_ALT_INTR	1

uint32 hnd_oobr_get_intstatus(si_t *sih);
int hnd_oobr_get_intr_config(si_t *sih, uint srccidx, uint srcpidx, uint dstcidx, uint *dstpidx);
int hnd_oobr_set_intr_src(si_t *sih, uint dstcidx, uint dstpidx, uint intrnum);
void hnd_oobr_init(si_t *sih);

#define OOBR_INVALID_PORT       0xFFu

/* per core source/dest sel reg */
#define OOBR_INTR_PER_CONFREG           4u           /* 4 interrupts per configure reg */
#define OOBR_INTR_NUM_MASK              0x7Fu
#define OOBR_INTR_EN                    0x80u
/* per core config reg */
#define OOBR_CORECNF_OUTPUT_MASK        0x0000FF00u
#define OOBR_CORECNF_OUTPUT_SHIFT       8u
#define OOBR_CORECNF_INPUT_MASK         0x00FF0000u
#define OOBR_CORECNF_INPUT_SHIFT        16u

typedef volatile struct hndoobr_percore_reg {
	uint32 sourcesel[OOBR_INTR_PER_CONFREG];        /* 0x00 - 0x0c */
	uint32 destsel[OOBR_INTR_PER_CONFREG];          /* 0x10 - 0x1c */
	uint32 reserved[6];
	uint32 config;                                  /* 0x38 */
	uint32 reserved1[17];                           /* 0x3c to 0x7c */
} hndoobr_percore_reg_t;

/* capability reg */
#define OOBR_CAP_CORECNT_MASK   0x1fu
typedef volatile struct hndoobr_reg {
	uint32 capability;                      /* 0x00 */
	uint32 reserved[3];
	uint32 intstatus[4];                    /* 0x10 - 0x1c */
	uint32 reserved1[56];                   /* 0x20 - 0xfc */
	hndoobr_percore_reg_t percore_reg[1];   /* 0x100 */
} hndoobr_reg_t;

#endif /* _hndoobr_h_ */
