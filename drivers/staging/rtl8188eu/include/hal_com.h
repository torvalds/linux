/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_COMMON_H__
#define __HAL_COMMON_H__

/*  */
/*        Rate Definition */
/*  */
/* CCK */
#define	RATR_1M					0x00000001
#define	RATR_2M					0x00000002
#define	RATR_55M				0x00000004
#define	RATR_11M				0x00000008
/* OFDM */
#define	RATR_6M					0x00000010
#define	RATR_9M					0x00000020
#define	RATR_12M				0x00000040
#define	RATR_18M				0x00000080
#define	RATR_24M				0x00000100
#define	RATR_36M				0x00000200
#define	RATR_48M				0x00000400
#define	RATR_54M				0x00000800
/* MCS 1 Spatial Stream */
#define	RATR_MCS0				0x00001000
#define	RATR_MCS1				0x00002000
#define	RATR_MCS2				0x00004000
#define	RATR_MCS3				0x00008000
#define	RATR_MCS4				0x00010000
#define	RATR_MCS5				0x00020000
#define	RATR_MCS6				0x00040000
#define	RATR_MCS7				0x00080000
/* MCS 2 Spatial Stream */
#define	RATR_MCS8				0x00100000
#define	RATR_MCS9				0x00200000
#define	RATR_MCS10				0x00400000
#define	RATR_MCS11				0x00800000
#define	RATR_MCS12				0x01000000
#define	RATR_MCS13				0x02000000
#define	RATR_MCS14				0x04000000
#define	RATR_MCS15				0x08000000

/* CCK */
#define RATE_1M					BIT(0)
#define RATE_2M					BIT(1)
#define RATE_5_5M				BIT(2)
#define RATE_11M				BIT(3)
/* OFDM */
#define RATE_6M					BIT(4)
#define RATE_9M					BIT(5)
#define RATE_12M				BIT(6)
#define RATE_18M				BIT(7)
#define RATE_24M				BIT(8)
#define RATE_36M				BIT(9)
#define RATE_48M				BIT(10)
#define RATE_54M				BIT(11)
/* MCS 1 Spatial Stream */
#define RATE_MCS0				BIT(12)
#define RATE_MCS1				BIT(13)
#define RATE_MCS2				BIT(14)
#define RATE_MCS3				BIT(15)
#define RATE_MCS4				BIT(16)
#define RATE_MCS5				BIT(17)
#define RATE_MCS6				BIT(18)
#define RATE_MCS7				BIT(19)
/* MCS 2 Spatial Stream */
#define RATE_MCS8				BIT(20)
#define RATE_MCS9				BIT(21)
#define RATE_MCS10				BIT(22)
#define RATE_MCS11				BIT(23)
#define RATE_MCS12				BIT(24)
#define RATE_MCS13				BIT(25)
#define RATE_MCS14				BIT(26)
#define RATE_MCS15				BIT(27)

/*  ALL CCK Rate */
#define	RATE_ALL_CCK		(RATR_1M | RATR_2M | RATR_55M | RATR_11M)
#define	RATE_ALL_OFDM_AG	(RATR_6M | RATR_9M | RATR_12M | RATR_18M | \
				 RATR_24M | RATR_36M | RATR_48M | RATR_54M)
#define	RATE_ALL_OFDM_1SS	(RATR_MCS0 | RATR_MCS1 | RATR_MCS2 |	\
				 RATR_MCS3 | RATR_MCS4 | RATR_MCS5|RATR_MCS6 | \
				 RATR_MCS7)
#define	RATE_ALL_OFDM_2SS	(RATR_MCS8 | RATR_MCS9 | RATR_MCS10 | \
				 RATR_MCS11 | RATR_MCS12 | RATR_MCS13 | \
				 RATR_MCS14 | RATR_MCS15)

/*------------------------------ Tx Desc definition Macro --------------------*/
/* pragma mark -- Tx Desc related definition. -- */
/*	Rate */
/*  CCK Rates, TxHT = 0 */
#define DESC_RATE1M				0x00
#define DESC_RATE2M				0x01
#define DESC_RATE5_5M				0x02
#define DESC_RATE11M				0x03

/*  OFDM Rates, TxHT = 0 */
#define DESC_RATE6M				0x04
#define DESC_RATE9M				0x05
#define DESC_RATE12M				0x06
#define DESC_RATE18M				0x07
#define DESC_RATE24M				0x08
#define DESC_RATE36M				0x09
#define DESC_RATE48M				0x0a
#define DESC_RATE54M				0x0b

/*  MCS Rates, TxHT = 1 */
#define DESC_RATEMCS0				0x0c
#define DESC_RATEMCS1				0x0d
#define DESC_RATEMCS2				0x0e
#define DESC_RATEMCS3				0x0f
#define DESC_RATEMCS4				0x10
#define DESC_RATEMCS5				0x11
#define DESC_RATEMCS6				0x12
#define DESC_RATEMCS7				0x13
#define DESC_RATEMCS8				0x14
#define DESC_RATEMCS9				0x15
#define DESC_RATEMCS10				0x16
#define DESC_RATEMCS11				0x17
#define DESC_RATEMCS12				0x18
#define DESC_RATEMCS13				0x19
#define DESC_RATEMCS14				0x1a
#define DESC_RATEMCS15				0x1b
#define DESC_RATEMCS15_SG			0x1c
#define DESC_RATEMCS32				0x20

/*  1 Byte long (in unit of TU) */
#define REG_P2P_CTWIN				0x0572
#define REG_NOA_DESC_SEL			0x05CF
#define REG_NOA_DESC_DURATION			0x05E0
#define REG_NOA_DESC_INTERVAL			0x05E4
#define REG_NOA_DESC_START			0x05E8
#define REG_NOA_DESC_COUNT			0x05EC

#include "HalVerDef.h"
void dump_chip_info(struct HAL_VERSION	ChipVersion);


/* return the final channel plan decision */
u8 hal_com_get_channel_plan(u8 hw_channel_plan, u8 sw_channel_plan,
			    u8 def_channel_plan, bool load_fail);

u8 MRateToHwRate(u8 rate);

void hal_set_brate_cfg(u8 *brates, u16 *rate_cfg);

bool hal_mapping_out_pipe(struct adapter *adapter, u8 numoutpipe);

void hal_init_macaddr(struct adapter *adapter);
#endif /* __HAL_COMMON_H__ */
