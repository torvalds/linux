/* SPDX-License-Identifier: GPL-2.0 */
/*
 * otpdefs.h SROM/OTP definitions.
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef _OTPDEFS_H_
#define _OTPDEFS_H_

/* SFLASH */
#define SFLASH_ADDRESS_OFFSET_4368 0x1C000000u
#define SFLASH_SKU_OFFSET_4368 0xEu
#define SFLASH_MACADDR_OFFSET_4368 0x4u
/*
 * In sflash based chips, first word in sflash says the length.
 * So only default value is defined here. Actual length is read
 * from sflash in dhdpcie_srom_sflash_health_chk
 * 0x0521 * 2 .x2 since length says number of words.
 */
#define SFLASH_LEN_4368 0xA42u

#define SROM_ADDRESS_OFFSET_4355 0x0800u
#define SROM_ADDRESS_OFFSET_4364 0xA000u
#define SROM_ADDRESS_OFFSET_4377 0x0800u
#define SROM_ADDRESS(sih, offset) (SI_ENUM_BASE(sih) + (offset))
#define SROM_MACADDR_OFFSET_4355 0x84u
#define SROM_MACADDR_OFFSET_4364 0x82u
#define SROM_MACADDR_OFFSET_4377 0xE2u
#define SROM_SKU_OFFSET_4355 0x8Au
#define SROM_SKU_OFFSET_4364 0x8Cu
#define SROM_SKU_OFFSET_4377 0xECu
#define SROM_CAL_SIG1_OFFSET_4355 0xB8u
#define SROM_CAL_SIG2_OFFSET_4355 0xBAu
#define SROM_CAL_SIG1_OFFSET_4364 0xA0u
#define SROM_CAL_SIG2_OFFSET_4364 0xA2u
#define SROM_CAL_SIG1 0x4c42u
#define SROM_CAL_SIG2 0x424fu
#define SROM_LEN_4355 512u
#define SROM_LEN_4364 2048u
#define SROM_LEN_4377 2048u

#define OTP_USER_AREA_OFFSET_4355 0xC0u
#define OTP_USER_AREA_OFFSET_4364 0xC0u
#define OTP_USER_AREA_OFFSET_4368 0x120u
#define OTP_USER_AREA_OFFSET_4377 0x120u
#define OTP_OFFSET_4368 0x5000u
#define OTP_OFFSET_4377 0x11000u
#define OTP_CTRL1_VAL 0xFA0000
#define OTP_ADDRESS(sih, offset) (SI_ENUM_BASE(sih) + (offset))
#define OTP_VERSION_TUPLE_ID 0x15
#define OTP_VENDOR_TUPLE_ID 0x80
#define OTP_CIS_REGION_END_TUPLE_ID 0XFF

#define PCIE_CTRL_REG_ADDR(sih) (SI_ENUM_BASE(sih) + 0x3000)
#define SPROM_CTRL_REG_ADDR(sih) (SI_ENUM_BASE(sih) + CC_SROM_CTRL)
#define SPROM_CTRL_OPCODE_READ_MASK 0x9FFFFFFF
#define SPROM_CTRL_START_BUSY_MASK 0x80000000
#define SPROM_ADDR(sih) (SI_ENUM_BASE(sih) + CC_SROM_ADDRESS)
#define SPROM_DATA(sih) (SI_ENUM_BASE(sih) + CC_SROM_DATA)
#define OTP_CTRL1_REG_ADDR(sih) (SI_ENUM_BASE(sih) + 0xF4)
#define PMU_MINRESMASK_REG_ADDR(sih) (SI_ENUM_BASE(sih) + MINRESMASKREG)
#define CHIP_COMMON_STATUS_REG_ADDR(sih) (SI_ENUM_BASE(sih) + CC_CHIPST)
#define CHIP_COMMON_CLKDIV2_ADDR(sih) (SI_ENUM_BASE(sih) + CC_CLKDIV2)

#define CC_CLKDIV2_SPROMDIV_MASK 0x7u
#define CC_CLKDIV2_SPROMDIV_VAL 0X4u
#define CC_CHIPSTATUS_STRAP_BTUART_MASK 0x40u
#define PMU_OTP_PWR_ON_MASK 0xC47
#define PMU_PWRUP_DELAY 500 /* in us */
#define DONGLE_TREFUP_PROGRAM_DELAY 5000 /* 5ms in us */
#define SPROM_BUSY_POLL_DELAY 5 /* 5us */

typedef enum {
	BCM4355_IDX = 0,
	BCM4364_IDX,
	BCM4368_IDX,
	BCM4377_IDX,
	BCMMAX_IDX
} chip_idx_t;

typedef enum {
	BCM4368_BTOP_IDX,
	BCM4377_BTOP_IDX,
	BCMMAX_BTOP_IDX
} chip_idx_btop_t;

typedef enum {
	BCM4368_SFLASH_IDX,
	BCMMAX_SFLASH_IDX
} chip_idx_sflash_t;

extern uint32 otp_addr_offsets[];
extern uint32 otp_usrarea_offsets[];
extern uint32 sku_offsets[];
extern uint32 srf_addr_offsets[];
extern uint32 supported_chips[];

char *dhd_get_plat_sku(void);
#endif /* _OTPDEFS_H */
