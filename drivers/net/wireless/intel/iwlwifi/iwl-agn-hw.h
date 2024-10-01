/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014 Intel Corporation
 */
/*
 * Please use this file (iwl-agn-hw.h) only for hardware-related definitions.
 */

#ifndef __iwl_agn_hw_h__
#define __iwl_agn_hw_h__

#define IWLAGN_RTC_INST_LOWER_BOUND		(0x000000)
#define IWLAGN_RTC_INST_UPPER_BOUND		(0x020000)

#define IWLAGN_RTC_DATA_LOWER_BOUND		(0x800000)
#define IWLAGN_RTC_DATA_UPPER_BOUND		(0x80C000)

#define IWLAGN_RTC_INST_SIZE (IWLAGN_RTC_INST_UPPER_BOUND - \
				IWLAGN_RTC_INST_LOWER_BOUND)
#define IWLAGN_RTC_DATA_SIZE (IWLAGN_RTC_DATA_UPPER_BOUND - \
				IWLAGN_RTC_DATA_LOWER_BOUND)

#define IWL60_RTC_INST_LOWER_BOUND		(0x000000)
#define IWL60_RTC_INST_UPPER_BOUND		(0x040000)
#define IWL60_RTC_DATA_LOWER_BOUND		(0x800000)
#define IWL60_RTC_DATA_UPPER_BOUND		(0x814000)
#define IWL60_RTC_INST_SIZE \
	(IWL60_RTC_INST_UPPER_BOUND - IWL60_RTC_INST_LOWER_BOUND)
#define IWL60_RTC_DATA_SIZE \
	(IWL60_RTC_DATA_UPPER_BOUND - IWL60_RTC_DATA_LOWER_BOUND)

/* RSSI to dBm */
#define IWLAGN_RSSI_OFFSET	44

#define IWLAGN_DEFAULT_TX_RETRY			15
#define IWLAGN_MGMT_DFAULT_RETRY_LIMIT		3
#define IWLAGN_RTS_DFAULT_RETRY_LIMIT		60
#define IWLAGN_BAR_DFAULT_RETRY_LIMIT		60
#define IWLAGN_LOW_RETRY_LIMIT			7

/* Limit range of txpower output target to be between these values */
#define IWLAGN_TX_POWER_TARGET_POWER_MIN	(0)	/* 0 dBm: 1 milliwatt */
#define IWLAGN_TX_POWER_TARGET_POWER_MAX	(16)	/* 16 dBm */

/* EEPROM */
#define IWLAGN_EEPROM_IMG_SIZE		2048

/* high blocks contain PAPD data */
#define OTP_HIGH_IMAGE_SIZE_6x00        (6 * 512 * sizeof(u16)) /* 6 KB */
#define OTP_HIGH_IMAGE_SIZE_1000        (0x200 * sizeof(u16)) /* 1024 bytes */
#define OTP_MAX_LL_ITEMS_1000		(3)	/* OTP blocks for 1000 */
#define OTP_MAX_LL_ITEMS_6x00		(4)	/* OTP blocks for 6x00 */
#define OTP_MAX_LL_ITEMS_6x50		(7)	/* OTP blocks for 6x50 */
#define OTP_MAX_LL_ITEMS_2x00		(4)	/* OTP blocks for 2x00 */


#define IWLAGN_NUM_QUEUES		20

#endif /* __iwl_agn_hw_h__ */
