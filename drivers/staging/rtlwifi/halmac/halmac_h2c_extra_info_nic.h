/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HAL_H2CEXTRAINFO_H2C_C2H_NIC_H_
#define _HAL_H2CEXTRAINFO_H2C_C2H_NIC_H_
#define PHY_PARAMETER_INFO_GET_LENGTH(__extra_info)                            \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 0, 8)
#define PHY_PARAMETER_INFO_SET_LENGTH(__extra_info, __value)                   \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 0, 8, __value)
#define PHY_PARAMETER_INFO_GET_IO_CMD(__extra_info)                            \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 8, 7)
#define PHY_PARAMETER_INFO_SET_IO_CMD(__extra_info, __value)                   \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 8, 7, __value)
#define PHY_PARAMETER_INFO_GET_MSK_EN(__extra_info)                            \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 15, 1)
#define PHY_PARAMETER_INFO_SET_MSK_EN(__extra_info, __value)                   \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 15, 1, __value)
#define PHY_PARAMETER_INFO_GET_LLT_PG_BNDY(__extra_info)                       \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 8)
#define PHY_PARAMETER_INFO_SET_LLT_PG_BNDY(__extra_info, __value)              \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 8, __value)
#define PHY_PARAMETER_INFO_GET_EFUSE_RSVDPAGE_LOC(__extra_info)                \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 8)
#define PHY_PARAMETER_INFO_SET_EFUSE_RSVDPAGE_LOC(__extra_info, __value)       \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 8, __value)
#define PHY_PARAMETER_INFO_GET_EFUSE_PATCH_EN(__extra_info)                    \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 8)
#define PHY_PARAMETER_INFO_SET_EFUSE_PATCH_EN(__extra_info, __value)           \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 8, __value)
#define PHY_PARAMETER_INFO_GET_RF_ADDR(__extra_info)                           \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 8)
#define PHY_PARAMETER_INFO_SET_RF_ADDR(__extra_info, __value)                  \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 8, __value)
#define PHY_PARAMETER_INFO_GET_IO_ADDR(__extra_info)                           \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 16)
#define PHY_PARAMETER_INFO_SET_IO_ADDR(__extra_info, __value)                  \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 16, __value)
#define PHY_PARAMETER_INFO_GET_DELAY_VALUE(__extra_info)                       \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 16)
#define PHY_PARAMETER_INFO_SET_DELAY_VALUE(__extra_info, __value)              \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 16, __value)
#define PHY_PARAMETER_INFO_GET_RF_PATH(__extra_info)                           \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 24, 8)
#define PHY_PARAMETER_INFO_SET_RF_PATH(__extra_info, __value)                  \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 24, 8, __value)
#define PHY_PARAMETER_INFO_GET_DATA(__extra_info)                              \
	LE_BITS_TO_4BYTE(__extra_info + 0X04, 0, 32)
#define PHY_PARAMETER_INFO_SET_DATA(__extra_info, __value)                     \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X04, 0, 32, __value)
#define PHY_PARAMETER_INFO_GET_MASK(__extra_info)                              \
	LE_BITS_TO_4BYTE(__extra_info + 0X08, 0, 32)
#define PHY_PARAMETER_INFO_SET_MASK(__extra_info, __value)                     \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X08, 0, 32, __value)
#define CHANNEL_INFO_GET_CHANNEL(__extra_info)                                 \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 0, 8)
#define CHANNEL_INFO_SET_CHANNEL(__extra_info, __value)                        \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 0, 8, __value)
#define CHANNEL_INFO_GET_PRI_CH_IDX(__extra_info)                              \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 8, 4)
#define CHANNEL_INFO_SET_PRI_CH_IDX(__extra_info, __value)                     \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 8, 4, __value)
#define CHANNEL_INFO_GET_BANDWIDTH(__extra_info)                               \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 12, 4)
#define CHANNEL_INFO_SET_BANDWIDTH(__extra_info, __value)                      \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 12, 4, __value)
#define CHANNEL_INFO_GET_TIMEOUT(__extra_info)                                 \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 8)
#define CHANNEL_INFO_SET_TIMEOUT(__extra_info, __value)                        \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 8, __value)
#define CHANNEL_INFO_GET_ACTION_ID(__extra_info)                               \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 24, 7)
#define CHANNEL_INFO_SET_ACTION_ID(__extra_info, __value)                      \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 24, 7, __value)
#define CHANNEL_INFO_GET_CH_EXTRA_INFO(__extra_info)                           \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 31, 1)
#define CHANNEL_INFO_SET_CH_EXTRA_INFO(__extra_info, __value)                  \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 31, 1, __value)
#define CH_EXTRA_INFO_GET_CH_EXTRA_INFO_ID(__extra_info)                       \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 0, 7)
#define CH_EXTRA_INFO_SET_CH_EXTRA_INFO_ID(__extra_info, __value)              \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 0, 7, __value)
#define CH_EXTRA_INFO_GET_CH_EXTRA_INFO(__extra_info)                          \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 7, 1)
#define CH_EXTRA_INFO_SET_CH_EXTRA_INFO(__extra_info, __value)                 \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 7, 1, __value)
#define CH_EXTRA_INFO_GET_CH_EXTRA_INFO_SIZE(__extra_info)                     \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 8, 8)
#define CH_EXTRA_INFO_SET_CH_EXTRA_INFO_SIZE(__extra_info, __value)            \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 8, 8, __value)
#define CH_EXTRA_INFO_GET_CH_EXTRA_INFO_DATA(__extra_info)                     \
	LE_BITS_TO_4BYTE(__extra_info + 0X00, 16, 1)
#define CH_EXTRA_INFO_SET_CH_EXTRA_INFO_DATA(__extra_info, __value)            \
	SET_BITS_TO_LE_4BYTE(__extra_info + 0X00, 16, 1, __value)
#endif
