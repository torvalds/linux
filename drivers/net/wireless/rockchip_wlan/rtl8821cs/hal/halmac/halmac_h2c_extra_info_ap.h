/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HAL_H2CEXTRAINFO_H2C_C2H_AP_H_
#define _HAL_H2CEXTRAINFO_H2C_C2H_AP_H_

/* H2C extra info (rsvd page) usage, unit : page (128byte)*/
/* dlfw : not include txdesc size*/
/* update pkt : not include txdesc size*/
/* cfg param : not include txdesc size*/
/* scan info : not include txdesc size*/
/* dl flash : not include txdesc size*/
#define DLFW_RSVDPG_SIZE 2048
#define UPDATE_PKT_RSVDPG_SIZE 2048
#define CFG_PARAM_RSVDPG_SIZE 2048
#define SCAN_INFO_RSVDPG_SIZE 256
#define DL_FLASH_RSVDPG_SIZE 2048
/* su0 snding pkt : include txdesc size */
#define SU0_SNDING_PKT_OFFSET 0
#define SU0_SNDING_PKT_RSVDPG_SIZE 128

#define PARAM_INFO_GET_LEN(extra_info) GET_C2H_FIELD(extra_info + 0X00, 0, 8)
#define PARAM_INFO_SET_LEN(extra_info, value)                                  \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 0, 8, value)
#define PARAM_INFO_SET_LEN_NO_CLR(extra_info, value)                           \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 0, 8, value)
#define PARAM_INFO_GET_IO_CMD(extra_info) GET_C2H_FIELD(extra_info + 0X00, 8, 7)
#define PARAM_INFO_SET_IO_CMD(extra_info, value)                               \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 8, 7, value)
#define PARAM_INFO_SET_IO_CMD_NO_CLR(extra_info, value)                        \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 8, 7, value)
#define PARAM_INFO_GET_MSK_EN(extra_info)                                      \
	GET_C2H_FIELD(extra_info + 0X00, 15, 1)
#define PARAM_INFO_SET_MSK_EN(extra_info, value)                               \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 15, 1, value)
#define PARAM_INFO_SET_MSK_EN_NO_CLR(extra_info, value)                        \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 15, 1, value)
#define PARAM_INFO_GET_LLT_PG_BNDY(extra_info)                                 \
	GET_C2H_FIELD(extra_info + 0X00, 16, 8)
#define PARAM_INFO_SET_LLT_PG_BNDY(extra_info, value)                          \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_SET_LLT_PG_BNDY_NO_CLR(extra_info, value)                   \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_GET_EFUSE_RSVDPAGE_LOC(extra_info)                          \
	GET_C2H_FIELD(extra_info + 0X00, 16, 8)
#define PARAM_INFO_SET_EFUSE_RSVDPAGE_LOC(extra_info, value)                   \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_SET_EFUSE_RSVDPAGE_LOC_NO_CLR(extra_info, value)            \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_GET_EFUSE_PATCH_EN(extra_info)                              \
	GET_C2H_FIELD(extra_info + 0X00, 16, 8)
#define PARAM_INFO_SET_EFUSE_PATCH_EN(extra_info, value)                       \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_SET_EFUSE_PATCH_EN_NO_CLR(extra_info, value)                \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_GET_RF_ADDR(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 16, 8)
#define PARAM_INFO_SET_RF_ADDR(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_SET_RF_ADDR_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 8, value)
#define PARAM_INFO_GET_IO_ADDR(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 16, 16)
#define PARAM_INFO_SET_IO_ADDR(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 16, value)
#define PARAM_INFO_SET_IO_ADDR_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 16, value)
#define PARAM_INFO_GET_DELAY_VAL(extra_info)                                   \
	GET_C2H_FIELD(extra_info + 0X00, 16, 16)
#define PARAM_INFO_SET_DELAY_VAL(extra_info, value)                            \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 16, value)
#define PARAM_INFO_SET_DELAY_VAL_NO_CLR(extra_info, value)                     \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 16, value)
#define PARAM_INFO_GET_RF_PATH(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 24, 8)
#define PARAM_INFO_SET_RF_PATH(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 24, 8, value)
#define PARAM_INFO_SET_RF_PATH_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 24, 8, value)
#define PARAM_INFO_GET_DATA(extra_info) GET_C2H_FIELD(extra_info + 0X04, 0, 32)
#define PARAM_INFO_SET_DATA(extra_info, value)                                 \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 0, 32, value)
#define PARAM_INFO_SET_DATA_NO_CLR(extra_info, value)                          \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 0, 32, value)
#define PARAM_INFO_GET_MASK(extra_info) GET_C2H_FIELD(extra_info + 0X08, 0, 32)
#define PARAM_INFO_SET_MASK(extra_info, value)                                 \
	SET_C2H_FIELD_CLR(extra_info + 0X08, 0, 32, value)
#define PARAM_INFO_SET_MASK_NO_CLR(extra_info, value)                          \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X08, 0, 32, value)
#define CH_INFO_GET_CH(extra_info) GET_C2H_FIELD(extra_info + 0X00, 0, 8)
#define CH_INFO_SET_CH(extra_info, value)                                      \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 0, 8, value)
#define CH_INFO_SET_CH_NO_CLR(extra_info, value)                               \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 0, 8, value)
#define CH_INFO_GET_PRI_CH_IDX(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 8, 4)
#define CH_INFO_SET_PRI_CH_IDX(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 8, 4, value)
#define CH_INFO_SET_PRI_CH_IDX_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 8, 4, value)
#define CH_INFO_GET_BW(extra_info) GET_C2H_FIELD(extra_info + 0X00, 12, 4)
#define CH_INFO_SET_BW(extra_info, value)                                      \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 12, 4, value)
#define CH_INFO_SET_BW_NO_CLR(extra_info, value)                               \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 12, 4, value)
#define CH_INFO_GET_TIMEOUT(extra_info) GET_C2H_FIELD(extra_info + 0X00, 16, 8)
#define CH_INFO_SET_TIMEOUT(extra_info, value)                                 \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 8, value)
#define CH_INFO_SET_TIMEOUT_NO_CLR(extra_info, value)                          \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 8, value)
#define CH_INFO_GET_ACTION_ID(extra_info)                                      \
	GET_C2H_FIELD(extra_info + 0X00, 24, 7)
#define CH_INFO_SET_ACTION_ID(extra_info, value)                               \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 24, 7, value)
#define CH_INFO_SET_ACTION_ID_NO_CLR(extra_info, value)                        \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 24, 7, value)
#define CH_INFO_GET_EXTRA_INFO(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 31, 1)
#define CH_INFO_SET_EXTRA_INFO(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 31, 1, value)
#define CH_INFO_SET_EXTRA_INFO_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 31, 1, value)
#define CH_EXTRA_INFO_GET_ID(extra_info) GET_C2H_FIELD(extra_info + 0X00, 0, 7)
#define CH_EXTRA_INFO_SET_ID(extra_info, value)                                \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 0, 7, value)
#define CH_EXTRA_INFO_SET_ID_NO_CLR(extra_info, value)                         \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 0, 7, value)
#define CH_EXTRA_INFO_GET_INFO(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 7, 1)
#define CH_EXTRA_INFO_SET_INFO(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 7, 1, value)
#define CH_EXTRA_INFO_SET_INFO_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 7, 1, value)
#define CH_EXTRA_INFO_GET_SIZE(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 8, 8)
#define CH_EXTRA_INFO_SET_SIZE(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 8, 8, value)
#define CH_EXTRA_INFO_SET_SIZE_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 8, 8, value)
#define CH_EXTRA_INFO_GET_DATA(extra_info)                                     \
	GET_C2H_FIELD(extra_info + 0X00, 16, 1)
#define CH_EXTRA_INFO_SET_DATA(extra_info, value)                              \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 1, value)
#define CH_EXTRA_INFO_SET_DATA_NO_CLR(extra_info, value)                       \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 1, value)
#define HIOE_INSTRUCTION_INFO_GET_BYTEDATA_L(extra_info)                       \
	GET_C2H_FIELD(extra_info + 0X00, 0, 16)
#define HIOE_INSTRUCTION_INFO_SET_BYTEDATA_L(extra_info, value)                \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 0, 16, value)
#define HIOE_INSTRUCTION_INFO_SET_BYTEDATA_L_NO_CLR(extra_info, value)         \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 0, 16, value)
#define HIOE_INSTRUCTION_INFO_GET_BITDATA(extra_info)                          \
	GET_C2H_FIELD(extra_info + 0X00, 0, 16)
#define HIOE_INSTRUCTION_INFO_SET_BITDATA(extra_info, value)                   \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 0, 16, value)
#define HIOE_INSTRUCTION_INFO_SET_BITDATA_NO_CLR(extra_info, value)            \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 0, 16, value)
#define HIOE_INSTRUCTION_INFO_GET_BYTEDATA_H(extra_info)                       \
	GET_C2H_FIELD(extra_info + 0X00, 16, 16)
#define HIOE_INSTRUCTION_INFO_SET_BYTEDATA_H(extra_info, value)                \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 16, value)
#define HIOE_INSTRUCTION_INFO_SET_BYTEDATA_H_NO_CLR(extra_info, value)         \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 16, value)
#define HIOE_INSTRUCTION_INFO_GET_BITMASK(extra_info)                          \
	GET_C2H_FIELD(extra_info + 0X00, 16, 16)
#define HIOE_INSTRUCTION_INFO_SET_BITMASK(extra_info, value)                   \
	SET_C2H_FIELD_CLR(extra_info + 0X00, 16, 16, value)
#define HIOE_INSTRUCTION_INFO_SET_BITMASK_NO_CLR(extra_info, value)            \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X00, 16, 16, value)
#define HIOE_INSTRUCTION_INFO_GET_REG_ADDR(extra_info)                         \
	GET_C2H_FIELD(extra_info + 0X04, 0, 22)
#define HIOE_INSTRUCTION_INFO_SET_REG_ADDR(extra_info, value)                  \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 0, 22, value)
#define HIOE_INSTRUCTION_INFO_SET_REG_ADDR_NO_CLR(extra_info, value)           \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 0, 22, value)
#define HIOE_INSTRUCTION_INFO_GET_DELAY_VALUE(extra_info)                      \
	GET_C2H_FIELD(extra_info + 0X04, 0, 22)
#define HIOE_INSTRUCTION_INFO_SET_DELAY_VALUE(extra_info, value)               \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 0, 22, value)
#define HIOE_INSTRUCTION_INFO_SET_DELAY_VALUE_NO_CLR(extra_info, value)        \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 0, 22, value)
#define HIOE_INSTRUCTION_INFO_GET_MODE_SELECT(extra_info)                      \
	GET_C2H_FIELD(extra_info + 0X04, 22, 1)
#define HIOE_INSTRUCTION_INFO_SET_MODE_SELECT(extra_info, value)               \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 22, 1, value)
#define HIOE_INSTRUCTION_INFO_SET_MODE_SELECT_NO_CLR(extra_info, value)        \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 22, 1, value)
#define HIOE_INSTRUCTION_INFO_GET_IO_DELAY(extra_info)                         \
	GET_C2H_FIELD(extra_info + 0X04, 23, 1)
#define HIOE_INSTRUCTION_INFO_SET_IO_DELAY(extra_info, value)                  \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 23, 1, value)
#define HIOE_INSTRUCTION_INFO_SET_IO_DELAY_NO_CLR(extra_info, value)           \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 23, 1, value)
#define HIOE_INSTRUCTION_INFO_GET_BYTEMASK(extra_info)                         \
	GET_C2H_FIELD(extra_info + 0X04, 24, 4)
#define HIOE_INSTRUCTION_INFO_SET_BYTEMASK(extra_info, value)                  \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 24, 4, value)
#define HIOE_INSTRUCTION_INFO_SET_BYTEMASK_NO_CLR(extra_info, value)           \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 24, 4, value)
#define HIOE_INSTRUCTION_INFO_GET_RD_EN(extra_info)                            \
	GET_C2H_FIELD(extra_info + 0X04, 28, 1)
#define HIOE_INSTRUCTION_INFO_SET_RD_EN(extra_info, value)                     \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 28, 1, value)
#define HIOE_INSTRUCTION_INFO_SET_RD_EN_NO_CLR(extra_info, value)              \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 28, 1, value)
#define HIOE_INSTRUCTION_INFO_GET_WR_EN(extra_info)                            \
	GET_C2H_FIELD(extra_info + 0X04, 29, 1)
#define HIOE_INSTRUCTION_INFO_SET_WR_EN(extra_info, value)                     \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 29, 1, value)
#define HIOE_INSTRUCTION_INFO_SET_WR_EN_NO_CLR(extra_info, value)              \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 29, 1, value)
#define HIOE_INSTRUCTION_INFO_GET_RAW_R(extra_info)                            \
	GET_C2H_FIELD(extra_info + 0X04, 30, 1)
#define HIOE_INSTRUCTION_INFO_SET_RAW_R(extra_info, value)                     \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 30, 1, value)
#define HIOE_INSTRUCTION_INFO_SET_RAW_R_NO_CLR(extra_info, value)              \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 30, 1, value)
#define HIOE_INSTRUCTION_INFO_GET_RAW(extra_info)                              \
	GET_C2H_FIELD(extra_info + 0X04, 31, 1)
#define HIOE_INSTRUCTION_INFO_SET_RAW(extra_info, value)                       \
	SET_C2H_FIELD_CLR(extra_info + 0X04, 31, 1, value)
#define HIOE_INSTRUCTION_INFO_SET_RAW_NO_CLR(extra_info, value)                \
	SET_C2H_FIELD_NO_CLR(extra_info + 0X04, 31, 1, value)
#endif
