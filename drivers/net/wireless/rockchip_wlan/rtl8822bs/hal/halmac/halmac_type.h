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

#ifndef _HALMAC_TYPE_H_
#define _HALMAC_TYPE_H_

#include "halmac_2_platform.h"
#include "halmac_hw_cfg.h"
#include "halmac_fw_info.h"
#include "halmac_intf_phy_cmd.h"
#include "halmac_state_machine.h"

#define IN
#define OUT
#define INOUT

#define HALMAC_BCN_IE_BMP_SIZE	24 /* ID0~ID191, 192/8=24 */

#ifndef HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE
#define HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE	80
#endif

#ifndef HALMAC_MSG_LEVEL_TRACE
#define HALMAC_MSG_LEVEL_TRACE		3
#endif

#ifndef HALMAC_MSG_LEVEL_WARNING
#define HALMAC_MSG_LEVEL_WARNING	2
#endif

#ifndef HALMAC_MSG_LEVEL_ERR
#define HALMAC_MSG_LEVEL_ERR		1
#endif

#ifndef HALMAC_MSG_LEVEL_NO_LOG
#define HALMAC_MSG_LEVEL_NO_LOG		0
#endif

#ifndef HALMAC_SDIO_SUPPORT
#define HALMAC_SDIO_SUPPORT		1
#endif

#ifndef HALMAC_USB_SUPPORT
#define HALMAC_USB_SUPPORT		1
#endif

#ifndef HALMAC_PCIE_SUPPORT
#define HALMAC_PCIE_SUPPORT		1
#endif

#ifndef HALMAC_MSG_LEVEL
#define HALMAC_MSG_LEVEL HALMAC_MSG_LEVEL_TRACE
#endif

#ifndef HALMAC_DBG_MONITOR_IO
#define HALMAC_DBG_MONITOR_IO 0
#endif

/* platform api */
#define PLTFM_SDIO_CMD52_R(offset)                                             \
	adapter->pltfm_api->SDIO_CMD52_READ(adapter->drv_adapter, offset)
#define PLTFM_SDIO_CMD53_R8(offset)                                            \
	adapter->pltfm_api->SDIO_CMD53_READ_8(adapter->drv_adapter, offset)
#define PLTFM_SDIO_CMD53_R16(offset)                                           \
	adapter->pltfm_api->SDIO_CMD53_READ_16(adapter->drv_adapter, offset)
#define PLTFM_SDIO_CMD53_R32(offset)                                           \
	adapter->pltfm_api->SDIO_CMD53_READ_32(adapter->drv_adapter, offset)
#define PLTFM_SDIO_CMD53_RN(offset, size, data)                                \
	adapter->pltfm_api->SDIO_CMD53_READ_N(adapter->drv_adapter, offset,    \
					      size, data)
#define PLTFM_SDIO_CMD52_W(offset, val)                                        \
	adapter->pltfm_api->SDIO_CMD52_WRITE(adapter->drv_adapter, offset, val)
#define PLTFM_SDIO_CMD53_W8(offset, val)                                       \
	adapter->pltfm_api->SDIO_CMD53_WRITE_8(adapter->drv_adapter, offset,   \
					       val)
#define PLTFM_SDIO_CMD53_W16(offset, val)                                      \
	adapter->pltfm_api->SDIO_CMD53_WRITE_16(adapter->drv_adapter, offset,  \
						val)
#define PLTFM_SDIO_CMD53_W32(offset, val)                                      \
	adapter->pltfm_api->SDIO_CMD53_WRITE_32(adapter->drv_adapter, offset,  \
						val)
#define PLTFM_SDIO_CMD52_CIA_R(offset)                                         \
	adapter->pltfm_api->SDIO_CMD52_CIA_READ(adapter->drv_adapter, offset)

#define PLTFM_REG_R8(offset)                                                   \
	adapter->pltfm_api->REG_READ_8(adapter->drv_adapter, offset)
#define PLTFM_REG_R16(offset)                                                  \
	adapter->pltfm_api->REG_READ_16(adapter->drv_adapter, offset)
#define PLTFM_REG_R32(offset)                                                  \
	adapter->pltfm_api->REG_READ_32(adapter->drv_adapter, offset)
#define PLTFM_REG_W8(offset, val)                                              \
	adapter->pltfm_api->REG_WRITE_8(adapter->drv_adapter, offset, val)
#define PLTFM_REG_W16(offset, val)                                             \
	adapter->pltfm_api->REG_WRITE_16(adapter->drv_adapter, offset, val)
#define PLTFM_REG_W32(offset, val)                                             \
	adapter->pltfm_api->REG_WRITE_32(adapter->drv_adapter, offset, val)

#define PLTFM_SEND_RSVD_PAGE(buf, size)                                        \
	adapter->pltfm_api->SEND_RSVD_PAGE(adapter->drv_adapter, buf, size)
#define PLTFM_SEND_H2C_PKT(buf, size)                                          \
	adapter->pltfm_api->SEND_H2C_PKT(adapter->drv_adapter, buf, size)

#define PLTFM_FREE(buf, size)                                                  \
	adapter->pltfm_api->RTL_FREE(adapter->drv_adapter, buf, size)
#define PLTFM_MALLOC(size)                                                     \
	adapter->pltfm_api->RTL_MALLOC(adapter->drv_adapter, size)
#define PLTFM_MEMCPY(dest, src, size)                                          \
	adapter->pltfm_api->RTL_MEMCPY(adapter->drv_adapter, dest, src, size)
#define PLTFM_MEMSET(addr, value, size)                                        \
	adapter->pltfm_api->RTL_MEMSET(adapter->drv_adapter, addr, value, size)
#define PLTFM_DELAY_US(us)                                                     \
	adapter->pltfm_api->RTL_DELAY_US(adapter->drv_adapter, us)

#define PLTFM_MUTEX_INIT(mutex)                                                \
	adapter->pltfm_api->MUTEX_INIT(adapter->drv_adapter, mutex)
#define PLTFM_MUTEX_DEINIT(mutex)                                              \
	adapter->pltfm_api->MUTEX_DEINIT(adapter->drv_adapter, mutex)
#define PLTFM_MUTEX_LOCK(mutex)                                                \
	adapter->pltfm_api->MUTEX_LOCK(adapter->drv_adapter, mutex)
#define PLTFM_MUTEX_UNLOCK(mutex)                                              \
	adapter->pltfm_api->MUTEX_UNLOCK(adapter->drv_adapter, mutex)

#define PLTFM_EVENT_SIG(feature_id, proc_status, buf, size)                    \
	adapter->pltfm_api->EVENT_INDICATION(adapter->drv_adapter, feature_id, \
					     proc_status, buf, size)

#if HALMAC_PLATFORM_WINDOWS
#define PLTFM_MSG_PRINT	adapter->pltfm_api->MSG_PRINT
#endif

#define PLTFM_MSG_ALWAYS(...)                                                  \
	adapter->pltfm_api->MSG_PRINT(adapter->drv_adapter, HALMAC_MSG_INIT,   \
				      HALMAC_DBG_ALWAYS, __VA_ARGS__)

#if HALMAC_DBG_MSG_ENABLE

/* Enable debug msg depends on  HALMAC_MSG_LEVEL */
#if (HALMAC_MSG_LEVEL >= HALMAC_MSG_LEVEL_ERR)
#define PLTFM_MSG_ERR(...)                                                     \
	adapter->pltfm_api->MSG_PRINT(adapter->drv_adapter, HALMAC_MSG_INIT,   \
				      HALMAC_DBG_ERR, __VA_ARGS__)
#else
#define PLTFM_MSG_ERR(...)	do {} while (0)
#endif

#if (HALMAC_MSG_LEVEL >= HALMAC_MSG_LEVEL_WARNING)
#define PLTFM_MSG_WARN(...)                                                    \
	adapter->pltfm_api->MSG_PRINT(adapter->drv_adapter, HALMAC_MSG_INIT,   \
				      HALMAC_DBG_WARN, __VA_ARGS__)
#else
#define PLTFM_MSG_WARN(...)	do {} while (0)
#endif

#if (HALMAC_MSG_LEVEL >= HALMAC_MSG_LEVEL_TRACE)
#define PLTFM_MSG_TRACE(...)                                                   \
	adapter->pltfm_api->MSG_PRINT(adapter->drv_adapter, HALMAC_MSG_INIT,   \
				      HALMAC_DBG_TRACE, __VA_ARGS__)
#else
#define PLTFM_MSG_TRACE(...)	do {} while (0)
#endif

#else

/* Disable debug msg  */
#define PLTFM_MSG_ERR(...)	do {} while (0)
#define PLTFM_MSG_WARN(...)	do {} while (0)
#define PLTFM_MSG_TRACE(...)	do {} while (0)

#endif

#if HALMAC_DBG_MONITOR_IO
#define PLTFM_MONITOR_READ(offset, byte, val, __func, __line)                  \
	adapter->pltfm_api->READ_MONITOR(adapter->drv_adapter, offset, byte,   \
					 val, __func, __line)
#define PLTFM_MONITOR_WRITE(offset, byte, val, __func, __line)                 \
	adapter->pltfm_api->WRITE_MONITOR(adapter->drv_adapter, offset, byte,  \
					  val, __func, __line)

#define HALMAC_REG_R8(offset)                                                  \
	api->halmac_mon_reg_read_8(adapter, offset, __func__, __LINE__)
#define HALMAC_REG_R16(offset)                                                 \
	api->halmac_mon_reg_read_16(adapter, offset, __func__, __LINE__)
#define HALMAC_REG_R32(offset)                                                 \
	api->halmac_mon_reg_read_32(adapter, offset, __func__, __LINE__)
#define HALMAC_REG_W8(offset, val)                                             \
	api->halmac_mon_reg_write_8(adapter, offset, val,                      \
				    __func__, __LINE__)
#define HALMAC_REG_W16(offset, val)                                            \
	api->halmac_mon_reg_write_16(adapter, offset, val,                     \
				     __func__, __LINE__)
#define HALMAC_REG_W32(offset, val)                                            \
	api->halmac_mon_reg_write_32(adapter, offset, val,                     \
				     __func__, __LINE__)
#define HALMAC_REG_SDIO_RN(offset, size, data)                                 \
	api->halmac_mon_reg_sdio_cmd53_read_n(adapter, offset, size, data,     \
					      __func__, __LINE__)

#else
#define HALMAC_REG_R8(offset) api->halmac_reg_read_8(adapter, offset)
#define HALMAC_REG_R16(offset) api->halmac_reg_read_16(adapter, offset)
#define HALMAC_REG_R32(offset) api->halmac_reg_read_32(adapter, offset)
#define HALMAC_REG_W8(offset, val) api->halmac_reg_write_8(adapter, offset, val)
#define HALMAC_REG_W16(offset, val)                                            \
	api->halmac_reg_write_16(adapter, offset, val)
#define HALMAC_REG_W32(offset, val)                                            \
	api->halmac_reg_write_32(adapter, offset, val)
#define HALMAC_REG_SDIO_RN(offset, size, data)                                 \
	api->halmac_reg_sdio_cmd53_read_n(adapter, offset, size, data)
#endif

#define HALMAC_REG_W8_CLR(offset, mask)                                        \
	do {                                                                   \
		u32 __offset = (u32)offset;                                    \
		HALMAC_REG_W8(__offset, HALMAC_REG_R8(__offset) & ~(mask));    \
	} while (0)
#define HALMAC_REG_W16_CLR(offset, mask)                                       \
	do {                                                                   \
		u32 __offset = (u32)offset;                                    \
		HALMAC_REG_W16(__offset, HALMAC_REG_R16(__offset) & ~(mask));  \
	} while (0)
#define HALMAC_REG_W32_CLR(offset, mask)                                       \
	do {                                                                   \
		u32 __offset = (u32)offset;                                    \
		HALMAC_REG_W32(__offset, HALMAC_REG_R32(__offset) & ~(mask));  \
	} while (0)

#define HALMAC_REG_W8_SET(offset, mask)                                        \
	do {                                                                   \
		u32 __offset = (u32)offset;                                    \
		HALMAC_REG_W8(__offset, HALMAC_REG_R8(__offset) | mask);       \
	} while (0)
#define HALMAC_REG_W16_SET(offset, mask)                                       \
	do {                                                                   \
		u32 __offset = (u32)offset;                                    \
		HALMAC_REG_W16(__offset, HALMAC_REG_R16(__offset) | mask);     \
	} while (0)
#define HALMAC_REG_W32_SET(offset, mask)                                       \
	do {                                                                   \
		u32 __offset = (u32)offset;                                    \
		HALMAC_REG_W32(__offset, HALMAC_REG_R32(__offset) | mask);     \
	} while (0)

/* Swap Little-endian <-> Big-endia*/
#define SWAP32(x)                                                              \
	((u32)((((u32)(x) & (u32)0x000000ff) << 24) |                          \
	       (((u32)(x) & (u32)0x0000ff00) << 8) |                           \
	       (((u32)(x) & (u32)0x00ff0000) >> 8) |                           \
	       (((u32)(x) & (u32)0xff000000) >> 24)))

#define SWAP16(x)                                                              \
	((u16)((((u16)(x) & (u16)0x00ff) << 8) |                               \
	       (((u16)(x) & (u16)0xff00) >> 8)))

/*1->Little endian 0->Big endian*/
#if HALMAC_SYSTEM_ENDIAN
#ifndef rtk_le16_to_cpu
#define rtk_cpu_to_le32(x)              ((u32)(x))
#define rtk_le32_to_cpu(x)              ((u32)(x))
#define rtk_cpu_to_le16(x)              ((u16)(x))
#define rtk_le16_to_cpu(x)              ((u16)(x))
#define rtk_cpu_to_be32(x)              SWAP32((x))
#define rtk_be32_to_cpu(x)              SWAP32((x))
#define rtk_cpu_to_be16(x)              SWAP16((x))
#define rtk_be16_to_cpu(x)              SWAP16((x))
#endif
#else
#ifndef rtk_le16_to_cpu
#define rtk_cpu_to_le32(x)              SWAP32((x))
#define rtk_le32_to_cpu(x)              SWAP32((x))
#define rtk_cpu_to_le16(x)              SWAP16((x))
#define rtk_le16_to_cpu(x)              SWAP16((x))
#define rtk_cpu_to_be32(x)              ((u32)(x))
#define rtk_be32_to_cpu(x)              ((u32)(x))
#define rtk_cpu_to_be16(x)              ((u16)(x))
#define rtk_be16_to_cpu(x)              ((u16)(x))
#endif
#endif

#define HALMAC_ALIGN(x, a)               HALMAC_ALIGN_MASK(x, (a) - 1)
#define HALMAC_ALIGN_MASK(x, mask)       (((x) + (mask)) & ~(mask))

/* #if !HALMAC_PLATFORM_WINDOWS */
#if !((HALMAC_PLATFORM_WINDOWS == 1) && (HALMAC_PLATFORM_TESTPROGRAM == 0))

/* Byte Swapping routine */
#ifndef EF1BYTE
#define EF1BYTE (u8)
#endif

#ifndef EF2BYTE
#define EF2BYTE rtk_le16_to_cpu
#endif

#ifndef EF4BYTE
#define EF4BYTE rtk_le32_to_cpu
#endif

/* Example:
 * BIT_LEN_MASK_32(0) => 0x00000000
 * BIT_LEN_MASK_32(1) => 0x00000001
 * BIT_LEN_MASK_32(2) => 0x00000003
 * BIT_LEN_MASK_32(32) => 0xFFFFFFFF
 */
#ifndef BIT_LEN_MASK_32
#define BIT_LEN_MASK_32(__bitlen) (0xFFFFFFFF >> (32 - (__bitlen)))
#endif

/* Example:
 * BIT_OFFSET_LEN_MASK_32(0, 2) => 0x00000003
 * BIT_OFFSET_LEN_MASK_32(16, 2) => 0x00030000
 */
#ifndef BIT_OFFSET_LEN_MASK_32
#define BIT_OFFSET_LEN_MASK_32(__bitoffset, __bitlen)                          \
	(BIT_LEN_MASK_32(__bitlen) << (__bitoffset))
#endif

/* Return 4-byte value in host byte ordering from
 * 4-byte pointer in litten-endian system
 */
#ifndef LE_P4BYTE_TO_HOST_4BYTE
#define LE_P4BYTE_TO_HOST_4BYTE(__start) (EF4BYTE(*((u32 *)(__start))))
#endif

/* Translate subfield (continuous bits in little-endian) of
 * 4-byte value in litten byte to 4-byte value in host byte ordering
 */
#ifndef LE_BITS_TO_4BYTE
#define LE_BITS_TO_4BYTE(__start, __bitoffset, __bitlen)                       \
	((LE_P4BYTE_TO_HOST_4BYTE(__start) >> (__bitoffset)) &                 \
	 BIT_LEN_MASK_32(__bitlen))
#endif

/* Mask subfield (continuous bits in little-endian) of 4-byte
 * value in litten byte oredering and return the result in 4-byte
 * value in host byte ordering
 */
#ifndef LE_BITS_CLEARED_TO_4BYTE
#define LE_BITS_CLEARED_TO_4BYTE(__start, __bitoffset, __bitlen)               \
	(LE_P4BYTE_TO_HOST_4BYTE(__start) &                                    \
	 (~BIT_OFFSET_LEN_MASK_32(__bitoffset, __bitlen)))
#endif

/* Set subfield of little-endian 4-byte value to specified value */
#ifndef SET_BITS_TO_LE_4BYTE
#define SET_BITS_TO_LE_4BYTE(__start, __bitoffset, __bitlen, __value)          \
	do {                                                                   \
		*((u32 *)(__start)) = \
		EF4BYTE( \
		LE_BITS_CLEARED_TO_4BYTE(__start, __bitoffset, __bitlen) |     \
		((((u32)__value) & BIT_LEN_MASK_32(__bitlen)) << (__bitoffset))\
		);                                                             \
	} while (0)
#endif

#ifndef HALMAC_BIT_OFFSET_VAL_MASK_32
#define HALMAC_BIT_OFFSET_VAL_MASK_32(__bitval, __bitoffset)                   \
	(__bitval << (__bitoffset))
#endif

#ifndef SET_MEM_OP
#define SET_MEM_OP(dw, value32, mask, shift)                                   \
	(((dw) & ~((mask) << (shift))) | (((value32) & (mask)) << (shift)))
#endif

#ifndef HALMAC_SET_DESC_FIELD_CLR
#define HALMAC_SET_DESC_FIELD_CLR(dw, value32, mask, shift)                    \
	(dw = (rtk_cpu_to_le32(                                                \
		 SET_MEM_OP(rtk_cpu_to_le32(dw), value32, mask, shift))))
#endif

#ifndef HALMAC_SET_DESC_FIELD_NO_CLR
#define HALMAC_SET_DESC_FIELD_NO_CLR(dw, value32, mask, shift)                 \
	(dw |= (rtk_cpu_to_le32(((value32) & (mask)) << (shift))))
#endif

#ifndef HALMAC_GET_DESC_FIELD
#define HALMAC_GET_DESC_FIELD(dw, mask, shift)                                 \
	((rtk_le32_to_cpu(dw) >> (shift)) & (mask))
#endif

#define HALMAC_SET_BD_FIELD_CLR HALMAC_SET_DESC_FIELD_CLR
#define HALMAC_SET_BD_FIELD_NO_CLR HALMAC_SET_DESC_FIELD_NO_CLR
#define HALMAC_GET_BD_FIELD HALMAC_GET_DESC_FIELD

#ifndef GET_H2C_FIELD
#define GET_H2C_FIELD   LE_BITS_TO_4BYTE
#endif

#ifndef SET_H2C_FIELD_CLR
#define SET_H2C_FIELD_CLR       SET_BITS_TO_LE_4BYTE
#endif

#ifndef SET_H2C_FIELD_NO_CLR
#define SET_H2C_FIELD_NO_CLR    SET_BITS_TO_LE_4BYTE
#endif

#ifndef GET_C2H_FIELD
#define GET_C2H_FIELD   LE_BITS_TO_4BYTE
#endif

#ifndef SET_C2H_FIELD_CLR
#define SET_C2H_FIELD_CLR       SET_BITS_TO_LE_4BYTE
#endif

#ifndef SET_C2H_FIELD_NO_CLR
#define SET_C2H_FIELD_NO_CLR    SET_BITS_TO_LE_4BYTE
#endif

#endif /* #if !HALMAC_PLATFORM_WINDOWS */

#ifndef BIT
#define BIT(x)              (1 << (x))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))
#endif

/* HALMAC API return status*/
enum halmac_ret_status {
	HALMAC_RET_SUCCESS = 0x00,
	HALMAC_RET_NOT_SUPPORT = 0x01,
	HALMAC_RET_SUCCESS_ENQUEUE = 0x01, /*Don't use this return code!!*/
	HALMAC_RET_PLATFORM_API_NULL = 0x02,
	HALMAC_RET_EFUSE_SIZE_INCORRECT = 0x03,
	HALMAC_RET_MALLOC_FAIL = 0x04,
	HALMAC_RET_ADAPTER_INVALID = 0x05,
	HALMAC_RET_ITF_INCORRECT = 0x06,
	HALMAC_RET_DLFW_FAIL = 0x07,
	HALMAC_RET_PORT_NOT_SUPPORT = 0x08,
	HALMAC_RET_TXAGG_OVERFLOW = 0x09,
	HALMAC_RET_INIT_LLT_FAIL = 0x0A,
	HALMAC_RET_POWER_STATE_INVALID = 0x0B,
	HALMAC_RET_H2C_ACK_NOT_RECEIVED = 0x0C,
	HALMAC_RET_DL_RSVD_PAGE_FAIL = 0x0D,
	HALMAC_RET_EFUSE_R_FAIL = 0x0E,
	HALMAC_RET_EFUSE_W_FAIL = 0x0F,
	HALMAC_RET_H2C_SW_RES_FAIL = 0x10,
	HALMAC_RET_SEND_H2C_FAIL = 0x11,
	HALMAC_RET_PARA_NOT_SUPPORT = 0x12,
	HALMAC_RET_PLATFORM_API_INCORRECT = 0x13,
	HALMAC_RET_ENDIAN_ERR = 0x14,
	HALMAC_RET_FW_SIZE_ERR = 0x15,
	HALMAC_RET_TRX_MODE_NOT_SUPPORT = 0x16,
	HALMAC_RET_FAIL = 0x17,
	HALMAC_RET_CHANGE_PS_FAIL = 0x18,
	HALMAC_RET_CFG_PARA_FAIL = 0x19,
	HALMAC_RET_UPDATE_PROBE_FAIL = 0x1A,
	HALMAC_RET_SCAN_FAIL = 0x1B,
	HALMAC_RET_STOP_SCAN_FAIL = 0x1C,
	HALMAC_RET_BCN_PARSER_CMD_FAIL = 0x1D,
	HALMAC_RET_POWER_ON_FAIL = 0x1E,
	HALMAC_RET_POWER_OFF_FAIL = 0x1F,
	HALMAC_RET_RX_AGG_MODE_FAIL = 0x20,
	HALMAC_RET_DATA_BUF_NULL = 0x21,
	HALMAC_RET_DATA_SIZE_INCORRECT = 0x22,
	HALMAC_RET_QSEL_INCORRECT = 0x23,
	HALMAC_RET_DMA_MAP_INCORRECT = 0x24,
	HALMAC_RET_SEND_ORIGINAL_H2C_FAIL = 0x25,
	HALMAC_RET_DDMA_FAIL = 0x26,
	HALMAC_RET_FW_CHECKSUM_FAIL = 0x27,
	HALMAC_RET_PWRSEQ_POLLING_FAIL = 0x28,
	HALMAC_RET_PWRSEQ_CMD_INCORRECT = 0x29,
	HALMAC_RET_WRITE_DATA_FAIL = 0x2A,
	HALMAC_RET_DUMP_FIFOSIZE_INCORRECT = 0x2B,
	HALMAC_RET_NULL_POINTER = 0x2C,
	HALMAC_RET_PROBE_NOT_FOUND = 0x2D,
	HALMAC_RET_FW_NO_MEMORY = 0x2E,
	HALMAC_RET_H2C_STATUS_ERR = 0x2F,
	HALMAC_RET_GET_H2C_SPACE_ERR = 0x30,
	HALMAC_RET_H2C_SPACE_FULL = 0x31,
	HALMAC_RET_DATAPACK_NO_FOUND = 0x32,
	HALMAC_RET_CANNOT_FIND_H2C_RESOURCE = 0x33,
	HALMAC_RET_TX_DMA_ERR = 0x34,
	HALMAC_RET_RX_DMA_ERR = 0x35,
	HALMAC_RET_CHIP_NOT_SUPPORT = 0x36,
	HALMAC_RET_FREE_SPACE_NOT_ENOUGH = 0x37,
	HALMAC_RET_CH_SW_SEQ_WRONG = 0x38,
	HALMAC_RET_CH_SW_NO_BUF = 0x39,
	HALMAC_RET_SW_CASE_NOT_SUPPORT = 0x3A,
	HALMAC_RET_CONVERT_SDIO_OFFSET_FAIL = 0x3B,
	HALMAC_RET_INVALID_SOUNDING_SETTING = 0x3C,
	HALMAC_RET_GEN_INFO_NOT_SENT = 0x3D,
	HALMAC_RET_STATE_INCORRECT = 0x3E,
	HALMAC_RET_H2C_BUSY = 0x3F,
	HALMAC_RET_INVALID_FEATURE_ID = 0x40,
	HALMAC_RET_BUFFER_TOO_SMALL = 0x41,
	HALMAC_RET_ZERO_LEN_RSVD_PACKET = 0x42,
	HALMAC_RET_BUSY_STATE = 0x43,
	HALMAC_RET_ERROR_STATE = 0x44,
	HALMAC_RET_API_INVALID = 0x45,
	HALMAC_RET_POLLING_BCN_VALID_FAIL = 0x46,
	HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL = 0x47,
	HALMAC_RET_EEPROM_PARSING_FAIL = 0x48,
	HALMAC_RET_EFUSE_NOT_ENOUGH = 0x49,
	HALMAC_RET_WRONG_ARGUMENT = 0x4A,
	HALMAC_RET_C2H_NOT_HANDLED = 0x4C,
	HALMAC_RET_PARA_SENDING = 0x4D,
	HALMAC_RET_CFG_DLFW_SIZE_FAIL = 0x4E,
	HALMAC_RET_CFG_TXFIFO_PAGE_FAIL = 0x4F,
	HALMAC_RET_SWITCH_CASE_ERROR = 0x50,
	HALMAC_RET_EFUSE_BANK_INCORRECT = 0x51,
	HALMAC_RET_SWITCH_EFUSE_BANK_FAIL = 0x52,
	HALMAC_RET_USB_MODE_UNCHANGE = 0x53,
	HALMAC_RET_NO_DLFW = 0x54,
	HALMAC_RET_USB2_3_SWITCH_UNSUPPORT = 0x55,
	HALMAC_RET_BIP_NO_SUPPORT = 0x56,
	HALMAC_RET_ENTRY_INDEX_ERROR = 0x57,
	HALMAC_RET_ENTRY_KEY_ID_ERROR = 0x58,
	HALMAC_RET_DRV_DL_ERR = 0x59,
	HALMAC_RET_OQT_NOT_ENOUGH = 0x5A,
	HALMAC_RET_PWR_UNCHANGE = 0x5B,
	HALMAC_RET_WRONG_INTF = 0x5C,
	HALMAC_RET_POLLING_HIOE_REQ_FAIL = 0x5E,
	HALMAC_RET_HIOE_CHKSUM_FAIL = 0x5F,
	HALMAC_RET_HIOE_ERR = 0x60,
	HALMAC_RET_FW_NO_SUPPORT = 0x60,
	HALMAC_RET_TXFIFO_NO_EMPTY = 0x61,
	HALMAC_RET_SDIO_CLOCK_ERR = 0x62,
	HALMAC_RET_GET_PINMUX_ERR = 0x63,
	HALMAC_RET_PINMUX_USED = 0x64,
	HALMAC_RET_WRONG_GPIO = 0x65,
	HALMAC_RET_LTECOEX_READY_FAIL = 0x66,
	HALMAC_RET_IDMEM_CHKSUM_FAIL = 0x67,
	HALMAC_RET_ILLEGAL_KEY_FAIL = 0x68,
	HALMAC_RET_FW_READY_CHK_FAIL = 0x69,
	HALMAC_RET_RSVD_PG_OVERFLOW_FAIL = 0x70,
	HALMAC_RET_THRESHOLD_FAIL = 0x71,
	HALMAC_RET_SDIO_MIX_MODE = 0x72,
	HALMAC_RET_TXDESC_SET_FAIL = 0x73,
	HALMAC_RET_WLHDR_FAIL = 0x74,
	HALMAC_RET_WLAN_MODE_FAIL = 0x75,
	HALMAC_RET_SDIO_SEQ_FAIL = 0x72,
	HALMAC_RET_INIT_XTAL_AAC_FAIL = 0x76,
	HALMAC_RET_PINMUX_NOT_SUPPORT = 0x77,
	HALMAC_RET_FWFF_NO_EMPTY = 0x78,
};

enum halmac_chip_id {
	HALMAC_CHIP_ID_8822B = 0,
	HALMAC_CHIP_ID_8821C = 1,
	HALMAC_CHIP_ID_8814B = 2,
	HALMAC_CHIP_ID_8197F = 3,
	HALMAC_CHIP_ID_8822C = 4,
	HALMAC_CHIP_ID_8812F = 5,
	HALMAC_CHIP_ID_UNDEFINE = 0x7F,
};

enum halmac_chip_ver {
	HALMAC_CHIP_VER_A_CUT = 0x00,
	HALMAC_CHIP_VER_B_CUT = 0x01,
	HALMAC_CHIP_VER_C_CUT = 0x02,
	HALMAC_CHIP_VER_D_CUT = 0x03,
	HALMAC_CHIP_VER_E_CUT = 0x04,
	HALMAC_CHIP_VER_F_CUT = 0x05,
	HALMAC_CHIP_VER_TEST = 0xFF,
	HALMAC_CHIP_VER_UNDEFINE = 0x7FFF,
};

enum halmac_network_type_select {
	HALMAC_NETWORK_NO_LINK = 0,
	HALMAC_NETWORK_ADHOC = 1,
	HALMAC_NETWORK_INFRASTRUCTURE = 2,
	HALMAC_NETWORK_AP = 3,
	HALMAC_NETWORK_UNDEFINE = 0x7F,
};

enum halmac_transfer_mode_select {
	HALMAC_TRNSFER_NORMAL = 0x0,
	HALMAC_TRNSFER_LOOPBACK_DIRECT = 0xB,
	HALMAC_TRNSFER_LOOPBACK_DELAY = 0x3,
	HALMAC_TRNSFER_UNDEFINE = 0x7F,
};

enum halmac_dma_mapping {
	HALMAC_DMA_MAPPING_EXTRA = 0,
	HALMAC_DMA_MAPPING_LOW = 1,
	HALMAC_DMA_MAPPING_NORMAL = 2,
	HALMAC_DMA_MAPPING_HIGH = 3,
	HALMAC_DMA_MAPPING_UNDEFINE = 0x7F,
};

enum halmac_io_size {
	HALMAC_IO_BYTE = 0x0,
	HALMAC_IO_WORD = 0x1,
	HALMAC_IO_DWORD = 0x2,
	HALMAC_IO_UNDEFINE = 0x7F,
};

#define HALMAC_MAP2_HQ		HALMAC_DMA_MAPPING_HIGH
#define HALMAC_MAP2_NQ		HALMAC_DMA_MAPPING_NORMAL
#define HALMAC_MAP2_LQ		HALMAC_DMA_MAPPING_LOW
#define HALMAC_MAP2_EXQ		HALMAC_DMA_MAPPING_EXTRA
#define HALMAC_MAP2_UNDEF	HALMAC_DMA_MAPPING_UNDEFINE

enum halmac_txdesc_queue_tid {
	HALMAC_TXDESC_QSEL_TID0 = 0,
	HALMAC_TXDESC_QSEL_TID1 = 1,
	HALMAC_TXDESC_QSEL_TID2 = 2,
	HALMAC_TXDESC_QSEL_TID3 = 3,
	HALMAC_TXDESC_QSEL_TID4 = 4,
	HALMAC_TXDESC_QSEL_TID5 = 5,
	HALMAC_TXDESC_QSEL_TID6 = 6,
	HALMAC_TXDESC_QSEL_TID7 = 7,
	HALMAC_TXDESC_QSEL_TID8 = 8,
	HALMAC_TXDESC_QSEL_TID9 = 9,
	HALMAC_TXDESC_QSEL_TIDA = 10,
	HALMAC_TXDESC_QSEL_TIDB = 11,
	HALMAC_TXDESC_QSEL_TIDC = 12,
	HALMAC_TXDESC_QSEL_TIDD = 13,
	HALMAC_TXDESC_QSEL_TIDE = 14,
	HALMAC_TXDESC_QSEL_TIDF = 15,

	HALMAC_TXDESC_QSEL_BEACON = 0x10,
	HALMAC_TXDESC_QSEL_HIGH = 0x11,
	HALMAC_TXDESC_QSEL_MGT = 0x12,
	HALMAC_TXDESC_QSEL_H2C_CMD = 0x13,
	HALMAC_TXDESC_QSEL_FWCMD = 0x14,

	HALMAC_TXDESC_QSEL_UNDEFINE = 0x7F,
};

enum halmac_pq_map_id {
	HALMAC_PQ_MAP_VO = 0x0,
	HALMAC_PQ_MAP_VI = 0x1,
	HALMAC_PQ_MAP_BE = 0x2,
	HALMAC_PQ_MAP_BK = 0x3,
	HALMAC_PQ_MAP_MG = 0x4,
	HALMAC_PQ_MAP_HI = 0x5,
	HALMAC_PQ_MAP_NUM = 0x6,
	HALMAC_PQ_MAP_UNDEF = 0x7F,
};

enum halmac_qsel {
	HALMAC_QSEL_VO = HALMAC_TXDESC_QSEL_TID6,
	HALMAC_QSEL_VI = HALMAC_TXDESC_QSEL_TID4,
	HALMAC_QSEL_BE = HALMAC_TXDESC_QSEL_TID0,
	HALMAC_QSEL_BK = HALMAC_TXDESC_QSEL_TID1,
	HALMAC_QSEL_VO_V2 = HALMAC_TXDESC_QSEL_TID7,
	HALMAC_QSEL_VI_V2 = HALMAC_TXDESC_QSEL_TID5,
	HALMAC_QSEL_BE_V2 = HALMAC_TXDESC_QSEL_TID3,
	HALMAC_QSEL_BK_V2 = HALMAC_TXDESC_QSEL_TID2,
	HALMAC_QSEL_TID8 = HALMAC_TXDESC_QSEL_TID8,
	HALMAC_QSEL_TID9 = HALMAC_TXDESC_QSEL_TID9,
	HALMAC_QSEL_TIDA = HALMAC_TXDESC_QSEL_TIDA,
	HALMAC_QSEL_TIDB = HALMAC_TXDESC_QSEL_TIDB,
	HALMAC_QSEL_TIDC = HALMAC_TXDESC_QSEL_TIDC,
	HALMAC_QSEL_TIDD = HALMAC_TXDESC_QSEL_TIDD,
	HALMAC_QSEL_TIDE = HALMAC_TXDESC_QSEL_TIDE,
	HALMAC_QSEL_TIDF = HALMAC_TXDESC_QSEL_TIDF,
	HALMAC_QSEL_BCN = HALMAC_TXDESC_QSEL_BEACON,
	HALMAC_QSEL_HIGH = HALMAC_TXDESC_QSEL_HIGH,
	HALMAC_QSEL_MGNT = HALMAC_TXDESC_QSEL_MGT,
	HALMAC_QSEL_CMD = HALMAC_TXDESC_QSEL_H2C_CMD,
	HALMAC_QSEL_FWCMD = HALMAC_TXDESC_QSEL_FWCMD,
	HALMAC_QSEL_UNDEFINE = 0x7F,
};

enum halmac_acq_id {
	HALMAC_ACQ_ID_VO = 0,
	HALMAC_ACQ_ID_VI = 1,
	HALMAC_ACQ_ID_BE = 2,
	HALMAC_ACQ_ID_BK = 3,
	HALMAC_ACQ_ID_MAX = 0x7F,
};

enum halmac_txdesc_dma_ch {
	HALMAC_TXDESC_DMA_CH0 = 0,
	HALMAC_TXDESC_DMA_CH1 = 1,
	HALMAC_TXDESC_DMA_CH2 = 2,
	HALMAC_TXDESC_DMA_CH3 = 3,
	HALMAC_TXDESC_DMA_CH4 = 4,
	HALMAC_TXDESC_DMA_CH5 = 5,
	HALMAC_TXDESC_DMA_CH6 = 6,
	HALMAC_TXDESC_DMA_CH7 = 7,
	HALMAC_TXDESC_DMA_CH8 = 8,
	HALMAC_TXDESC_DMA_CH9 = 9,
	HALMAC_TXDESC_DMA_CH10 = 10,
	HALMAC_TXDESC_DMA_CH11 = 11,
	HALMAC_TXDESC_DMA_CH12 = 12,
	HALMAC_TXDESC_DMA_CH13 = 13,
	HALMAC_TXDESC_DMA_CH14 = 14,
	HALMAC_TXDESC_DMA_CH15 = 15,
	HALMAC_TXDESC_DMA_CH16 = 16,
	HALMAC_TXDESC_DMA_CH17 = 17,
	HALMAC_TXDESC_DMA_CH18 = 18,
	HALMAC_TXDESC_DMA_CH19 = 19,
	HALMAC_TXDESC_DMA_CH20 = 20,
	HALMAC_TXDESC_DMA_CHMAX,
	HALMAC_TXDESC_DMA_CHUNDEFINE = 0x7F,
};

enum halmac_dma_ch {
	HALMAC_DMA_CH_0 = HALMAC_TXDESC_DMA_CH0,
	HALMAC_DMA_CH_1 = HALMAC_TXDESC_DMA_CH1,
	HALMAC_DMA_CH_2 = HALMAC_TXDESC_DMA_CH2,
	HALMAC_DMA_CH_3 = HALMAC_TXDESC_DMA_CH3,
	HALMAC_DMA_CH_4 = HALMAC_TXDESC_DMA_CH4,
	HALMAC_DMA_CH_5 = HALMAC_TXDESC_DMA_CH5,
	HALMAC_DMA_CH_6 = HALMAC_TXDESC_DMA_CH6,
	HALMAC_DMA_CH_7 = HALMAC_TXDESC_DMA_CH7,
	HALMAC_DMA_CH_8 = HALMAC_TXDESC_DMA_CH8,
	HALMAC_DMA_CH_9 = HALMAC_TXDESC_DMA_CH9,
	HALMAC_DMA_CH_10 = HALMAC_TXDESC_DMA_CH10,
	HALMAC_DMA_CH_11 = HALMAC_TXDESC_DMA_CH11,
	HALMAC_DMA_CH_S0 = HALMAC_TXDESC_DMA_CH12,
	HALMAC_DMA_CH_S1 = HALMAC_TXDESC_DMA_CH13,
	HALMAC_DMA_CH_MGQ = HALMAC_TXDESC_DMA_CH14,
	HALMAC_DMA_CH_HIGH = HALMAC_TXDESC_DMA_CH15,
	HALMAC_DMA_CH_FWCMD = HALMAC_TXDESC_DMA_CH16,
	HALMAC_DMA_CH_MGQ_BAND1 = HALMAC_TXDESC_DMA_CH17,
	HALMAC_DMA_CH_HIGH_BAND1 = HALMAC_TXDESC_DMA_CH18,
	HALMAC_DMA_CH_BCN = HALMAC_TXDESC_DMA_CH19,
	HALMAC_DMA_CH_H2C = HALMAC_TXDESC_DMA_CH20,
	HALMAC_DMA_CH_MAX = HALMAC_TXDESC_DMA_CHMAX,
	HALMAC_DMA_CH_UNDEFINE = 0x7F,
};

enum halmac_interface {
	HALMAC_INTERFACE_PCIE = 0x0,
	HALMAC_INTERFACE_USB = 0x1,
	HALMAC_INTERFACE_SDIO = 0x2,
	HALMAC_INTERFACE_AXI = 0x3,
	HALMAC_INTERFACE_UNDEFINE = 0x7F,
};

enum halmac_rx_agg_mode {
	HALMAC_RX_AGG_MODE_NONE = 0x0,
	HALMAC_RX_AGG_MODE_DMA = 0x1,
	HALMAC_RX_AGG_MODE_USB = 0x2,
	HALMAC_RX_AGG_MODE_UNDEFINE = 0x7F,
};

struct halmac_rxagg_th {
	u8 drv_define;
	u8 timeout;
	u8 size;
	u8 size_limit_en;
};

struct halmac_rxagg_cfg {
	enum halmac_rx_agg_mode mode;
	struct halmac_rxagg_th threshold;
};

struct halmac_api_registry {
	u8 rx_exp_en:1;
	u8 la_mode_en:1;
	u8 cfg_drv_rsvd_pg_en:1;
	u8 sdio_cmd53_4byte_en:1;
	u8 rsvd:4;
};

enum halmac_watcher_sel {
	HALMAC_WATCHER_SDIO_RN_FOOL_PROOFING = 0x0,
	HALMAC_WATCHER_UNDEFINE = 0x7F,
};

enum halmac_trx_mode {
	HALMAC_TRX_MODE_NORMAL = 0x0,
	HALMAC_TRX_MODE_TRXSHARE = 0x1,
	HALMAC_TRX_MODE_WMM = 0x2,
	HALMAC_TRX_MODE_P2P = 0x3,
	HALMAC_TRX_MODE_LOOPBACK = 0x4,
	HALMAC_TRX_MODE_DELAY_LOOPBACK = 0x5,
	HALMAC_TRX_MODE_MAX = 0x6,
	HALMAC_TRX_MODE_WMM_LINUX = 0x7E,
	HALMAC_TRX_MODE_UNDEFINE = 0x7F,
};

enum halmac_wireless_mode {
	HALMAC_WIRELESS_MODE_B = 0x0,
	HALMAC_WIRELESS_MODE_G = 0x1,
	HALMAC_WIRELESS_MODE_N = 0x2,
	HALMAC_WIRELESS_MODE_AC = 0x3,
	HALMAC_WIRELESS_MODE_UNDEFINE = 0x7F,
};

enum halmac_bw {
	HALMAC_BW_20 = 0x00,
	HALMAC_BW_40 = 0x01,
	HALMAC_BW_80 = 0x02,
	HALMAC_BW_160 = 0x03,
	HALMAC_BW_5 = 0x04,
	HALMAC_BW_10 = 0x05,
	HALMAC_BW_MAX = 0x06,
	HALMAC_BW_UNDEFINE = 0x7F,
};

enum halmac_efuse_read_cfg {
	HALMAC_EFUSE_R_AUTO = 0x00,
	HALMAC_EFUSE_R_DRV = 0x01,
	HALMAC_EFUSE_R_FW = 0x02,
	HALMAC_EFUSE_R_UNDEFINE = 0x7F,
};

enum halmac_dlfw_mem {
	HALMAC_DLFW_MEM_EMEM = 0x00,
	HALMAC_DLFW_MEM_EMEM_RSVD_PG = 0x01,
	HALMAC_DLFW_MEM_UNDEFINE = 0x7F,
};

struct halmac_tx_desc {
	u32 dword0;
	u32 dword1;
	u32 dword2;
	u32 dword3;
	u32 dword4;
	u32 dword5;
	u32 dword6;
	u32 dword7;
	u32 dword8;
	u32 dword9;
	u32 dword10;
	u32 dword11;
};

struct halmac_rx_desc {
	u32 dword0;
	u32 dword1;
	u32 dword2;
	u32 dword3;
	u32 dword4;
	u32 dword5;
};

struct halmac_bcn_ie_info {
	u8 func_en;
	u8 size_th;
	u8 timeout;
	u8 ie_bmp[HALMAC_BCN_IE_BMP_SIZE];
};

enum halmac_parameter_cmd {
	/* HALMAC_PARAMETER_CMD_LLT	= 0x1, */
	/* HALMAC_PARAMETER_CMD_R_EFUSE = 0x2, */
	/* HALMAC_PARAMETER_CMD_EFUSE_PATCH = 0x3, */
	HALMAC_PARAMETER_CMD_MAC_W8 = 0x4,
	HALMAC_PARAMETER_CMD_MAC_W16 = 0x5,
	HALMAC_PARAMETER_CMD_MAC_W32 = 0x6,
	HALMAC_PARAMETER_CMD_RF_W = 0x7,
	HALMAC_PARAMETER_CMD_BB_W8 = 0x8,
	HALMAC_PARAMETER_CMD_BB_W16 = 0x9,
	HALMAC_PARAMETER_CMD_BB_W32 = 0XA,
	HALMAC_PARAMETER_CMD_DELAY_US = 0X10,
	HALMAC_PARAMETER_CMD_DELAY_MS = 0X11,
	HALMAC_PARAMETER_CMD_END = 0XFF,
};

union halmac_parameter_content {
	struct _MAC_REG_W {
		u32 value;
		u32 msk;
		u16 offset;
		u8 msk_en;
	} MAC_REG_W;
	struct _BB_REG_W {
		u32 value;
		u32 msk;
		u16 offset;
		u8 msk_en;
	} BB_REG_W;
	struct _RF_REG_W {
		u32 value;
		u32 msk;
		u8 offset;
		u8 msk_en;
		u8 rf_path;
	} RF_REG_W;
	struct _DELAY_TIME {
		u32 rsvd1;
		u32 rsvd2;
		u16 delay_time;
		u8 rsvd3;
	} DELAY_TIME;
};

struct halmac_phy_parameter_info {
	enum halmac_parameter_cmd cmd_id;
	union halmac_parameter_content content;
};

struct halmac_pg_efuse_info {
	u8 *efuse_map;
	u32 efuse_map_size;
	u8 *efuse_mask;
	u32 efuse_mask_size;
};

struct halmac_cfg_param_info {
	u32 buf_size;
	u8 *buf;
	u8 *buf_wptr;
	u32 num;
	u32 avl_buf_size;
	u32 offset_accum;
	u32 value_accum;
	enum halmac_data_type data_type;
	u8 full_fifo_mode;
};

struct halmac_hw_cfg_info {
	u32 efuse_size;
	u32 eeprom_size;
	u32 bt_efuse_size;
	u32 tx_fifo_size;
	u32 rx_fifo_size;
	u32 rx_desc_fifo_size;
	u32 page_size;
	u16 tx_align_size;
	u8 txdesc_size;
	u8 rxdesc_size;
	u8 cam_entry_num;
	u8 chk_security_keyid;
	u8 txdesc_ie_max_num;
	u8 txdesc_body_size;
	u8 ac_oqt_size;
	u8 non_ac_oqt_size;
	u8 acq_num;
	u8 trx_mode;
	u8 usb_txagg_num;
	u32 prtct_efuse_size;
};

struct halmac_sdio_free_space {
	u16 hiq_pg_num;
	u16 miq_pg_num;
	u16 lowq_pg_num;
	u16 pubq_pg_num;
	u16 exq_pg_num;
	u8 ac_oqt_num;
	u8 non_ac_oqt_num;
	u8 ac_empty;
	u8 *macid_map;
	u32 macid_map_size;
};

enum hal_fifo_sel {
	HAL_FIFO_SEL_TX,
	HAL_FIFO_SEL_RX,
	HAL_FIFO_SEL_RSVD_PAGE,
	HAL_FIFO_SEL_REPORT,
	HAL_FIFO_SEL_LLT,
	HAL_FIFO_SEL_RXBUF_FW,
	HAL_FIFO_SEL_RXBUF_PHY,
	HAL_FIFO_SEL_RXDESC,
	HAL_BUF_SECURITY_CAM,
	HAL_BUF_WOW_CAM,
	HAL_BUF_RX_FILTER_CAM,
	HAL_BUF_BA_CAM,
	HAL_BUF_MBSSID_CAM
};

enum halmac_drv_info {
	/* No information is appended in rx_pkt */
	HALMAC_DRV_INFO_NONE,
	/* PHY status is appended after rx_desc */
	HALMAC_DRV_INFO_PHY_STATUS,
	/* PHY status and sniffer info are appended after rx_desc */
	HALMAC_DRV_INFO_PHY_SNIFFER,
	/* PHY status and plcp header are appended after rx_desc */
	HALMAC_DRV_INFO_PHY_PLCP,
	HALMAC_DRV_INFO_UNDEFINE,
};

enum halmac_pri_ch_idx {
	HALMAC_CH_IDX_UNDEFINE = 0,
	HALMAC_CH_IDX_1 = 1,
	HALMAC_CH_IDX_2 = 2,
	HALMAC_CH_IDX_3 = 3,
	HALMAC_CH_IDX_4 = 4,
	HALMAC_CH_IDX_MAX = 5,
};

struct halmac_ch_info {
	enum halmac_cs_action_id action_id;
	enum halmac_bw bw;
	enum halmac_pri_ch_idx pri_ch_idx;
	u8 channel;
	u8 timeout;
	u8 extra_info;
};

struct halmac_ch_extra_info {
	u8 extra_info;
	enum halmac_cs_extra_action_id extra_action_id;
	u8 extra_info_size;
	u8 *extra_info_data;
};

enum halmac_cs_periodic_option {
	HALMAC_CS_PERIODIC_NONE,
	HALMAC_CS_PERIODIC_NORMAL,
	HALMAC_CS_PERIODIC_2_PHASE,
	HALMAC_CS_PERIODIC_SEAMLESS,
};

struct halmac_ch_switch_option {
	enum halmac_bw dest_bw;
	enum halmac_cs_periodic_option periodic_option;
	enum halmac_pri_ch_idx dest_pri_ch_idx;
	/* u32 tsf_high; */
	u32 tsf_low;
	u8 switch_en;
	u8 dest_ch_en;
	u8 absolute_time_en;
	u8 dest_ch;
	u8 scan_mode_en;
	u8 normal_period;
	u8 normal_period_sel;
	u8 normal_cycle;
	u8 phase_2_period;
	u8 phase_2_period_sel;
	u8 nlo_en;
};

struct halmac_drop_pkt_option {
	u8 drop_all:1;
	u8 drop_single:1;
	u8 rsvd:6;
	u8 drop_index;
};

struct halmac_p2pps {
	u8 offload_en:1;
	u8 role:1;
	u8 ctwindow_en:1;
	u8 noa_en:1;
	u8 noa_sel:1;
	u8 all_sta_sleep:1;
	u8 discovery:1;
	u8 disable_close_rf:1;
	u8 p2p_port_id;
	u8 p2p_group;
	u8 p2p_macid;
	u8 ctwindow_length;
	u8 rsvd3;
	u8 rsvd4;
	u8 rsvd5;
	u32 noa_duration_para;
	u32 noa_interval_para;
	u32 noa_start_time_para;
	u32 noa_count_para;
};

struct halmac_fw_build_time {
	u16 year;
	u8 month;
	u8 date;
	u8 hour;
	u8 min;
};

struct halmac_fw_version {
	u16 version;
	u8 sub_version;
	u8 sub_index;
	u16 h2c_version;
	struct halmac_fw_build_time build_time;
};

enum halmac_rf_type {
	HALMAC_RF_1T2R = 0,
	HALMAC_RF_2T4R = 1,
	HALMAC_RF_2T2R = 2,
	HALMAC_RF_2T3R = 3,
	HALMAC_RF_1T1R = 4,
	HALMAC_RF_2T2R_GREEN = 5,
	HALMAC_RF_3T3R = 6,
	HALMAC_RF_3T4R = 7,
	HALMAC_RF_4T4R = 8,
	HALMAC_RF_MAX_TYPE = 0xF,
};

struct halmac_general_info {
	u8 rfe_type;
	enum halmac_rf_type rf_type;
	u8 tx_ant_status;
	u8 rx_ant_status;
	u8 ext_pa;
	u8 package_type;
	u8 mp_mode;
};

struct halmac_pwr_tracking_para {
	u8 enable;
	u8 tx_pwr_index;
	u8 pwr_tracking_offset_value;
	u8 tssi_value;
};

struct halmac_pwr_tracking_option {
	u8 type;
	u8 bbswing_index;
	/* pathA, pathB, pathC, pathD */
	struct halmac_pwr_tracking_para pwr_tracking_para[4];
};

struct halmac_fast_edca_cfg {
	enum halmac_acq_id acq_id;
	u8 queue_to; /* unit : 32us*/
};

struct halmac_txfifo_lifetime_cfg {
	u8 enable;
	u32 lifetime;
};

enum halmac_data_rate {
	HALMAC_CCK1,
	HALMAC_CCK2,
	HALMAC_CCK5_5,
	HALMAC_CCK11,
	HALMAC_OFDM6,
	HALMAC_OFDM9,
	HALMAC_OFDM12,
	HALMAC_OFDM18,
	HALMAC_OFDM24,
	HALMAC_OFDM36,
	HALMAC_OFDM48,
	HALMAC_OFDM54,
	HALMAC_MCS0,
	HALMAC_MCS1,
	HALMAC_MCS2,
	HALMAC_MCS3,
	HALMAC_MCS4,
	HALMAC_MCS5,
	HALMAC_MCS6,
	HALMAC_MCS7,
	HALMAC_MCS8,
	HALMAC_MCS9,
	HALMAC_MCS10,
	HALMAC_MCS11,
	HALMAC_MCS12,
	HALMAC_MCS13,
	HALMAC_MCS14,
	HALMAC_MCS15,
	HALMAC_MCS16,
	HALMAC_MCS17,
	HALMAC_MCS18,
	HALMAC_MCS19,
	HALMAC_MCS20,
	HALMAC_MCS21,
	HALMAC_MCS22,
	HALMAC_MCS23,
	HALMAC_MCS24,
	HALMAC_MCS25,
	HALMAC_MCS26,
	HALMAC_MCS27,
	HALMAC_MCS28,
	HALMAC_MCS29,
	HALMAC_MCS30,
	HALMAC_MCS31,
	HALMAC_VHT_NSS1_MCS0,
	HALMAC_VHT_NSS1_MCS1,
	HALMAC_VHT_NSS1_MCS2,
	HALMAC_VHT_NSS1_MCS3,
	HALMAC_VHT_NSS1_MCS4,
	HALMAC_VHT_NSS1_MCS5,
	HALMAC_VHT_NSS1_MCS6,
	HALMAC_VHT_NSS1_MCS7,
	HALMAC_VHT_NSS1_MCS8,
	HALMAC_VHT_NSS1_MCS9,
	HALMAC_VHT_NSS2_MCS0,
	HALMAC_VHT_NSS2_MCS1,
	HALMAC_VHT_NSS2_MCS2,
	HALMAC_VHT_NSS2_MCS3,
	HALMAC_VHT_NSS2_MCS4,
	HALMAC_VHT_NSS2_MCS5,
	HALMAC_VHT_NSS2_MCS6,
	HALMAC_VHT_NSS2_MCS7,
	HALMAC_VHT_NSS2_MCS8,
	HALMAC_VHT_NSS2_MCS9,
	HALMAC_VHT_NSS3_MCS0,
	HALMAC_VHT_NSS3_MCS1,
	HALMAC_VHT_NSS3_MCS2,
	HALMAC_VHT_NSS3_MCS3,
	HALMAC_VHT_NSS3_MCS4,
	HALMAC_VHT_NSS3_MCS5,
	HALMAC_VHT_NSS3_MCS6,
	HALMAC_VHT_NSS3_MCS7,
	HALMAC_VHT_NSS3_MCS8,
	HALMAC_VHT_NSS3_MCS9,
	HALMAC_VHT_NSS4_MCS0,
	HALMAC_VHT_NSS4_MCS1,
	HALMAC_VHT_NSS4_MCS2,
	HALMAC_VHT_NSS4_MCS3,
	HALMAC_VHT_NSS4_MCS4,
	HALMAC_VHT_NSS4_MCS5,
	HALMAC_VHT_NSS4_MCS6,
	HALMAC_VHT_NSS4_MCS7,
	HALMAC_VHT_NSS4_MCS8,
	HALMAC_VHT_NSS4_MCS9,
	 /*FPGA only*/
	HALMAC_VHT_NSS5_MCS0,
	HALMAC_VHT_NSS6_MCS0,
	HALMAC_VHT_NSS7_MCS0,
	HALMAC_VHT_NSS8_MCS0
};

enum halmac_rf_path {
	HALMAC_RF_PATH_A,
	HALMAC_RF_PATH_B,
	HALMAC_RF_PATH_C,
	HALMAC_RF_PATH_D,
	HALMAC_RF_SYN_0,
	HALMAC_RF_SYN_1
};

enum hal_security_type {
	HAL_SECURITY_TYPE_NONE = 0,
	HAL_SECURITY_TYPE_WEP40 = 1,
	HAL_SECURITY_TYPE_WEP104 = 2,
	HAL_SECURITY_TYPE_TKIP = 3,
	HAL_SECURITY_TYPE_AES128 = 4,
	HAL_SECURITY_TYPE_WAPI = 5,
	HAL_SECURITY_TYPE_AES256 = 6,
	HAL_SECURITY_TYPE_GCMP128 = 7,
	HAL_SECURITY_TYPE_GCMP256 = 8,
	HAL_SECURITY_TYPE_GCMSMS4 = 9,
	HAL_SECURITY_TYPE_BIP = 10,
	HAL_SECURITY_TYPE_UNDEFINE = 0x7F,
};

enum hal_intf_phy {
	HAL_INTF_PHY_USB2 = 0,
	HAL_INTF_PHY_USB3 = 1,
	HAL_INTF_PHY_PCIE_GEN1 = 2,
	HAL_INTF_PHY_PCIE_GEN2 = 3,
	HAL_INTF_PHY_UNDEFINE = 0x7F,
};

struct halmac_cut_amsdu_cfg {
	u8 cut_amsdu_en;
	u8 chk_len_en;
	u8 chk_len_def_val;
	u8 chk_len_l_th;
	u16 chk_len_h_th;
};

enum halmac_dbg_msg_info {
	HALMAC_DBG_ALWAYS,
	HALMAC_DBG_ERR,
	HALMAC_DBG_WARN,
	HALMAC_DBG_TRACE,
};

enum halmac_dbg_msg_type {
	HALMAC_MSG_INIT,
	HALMAC_MSG_EFUSE,
	HALMAC_MSG_FW,
	HALMAC_MSG_H2C,
	HALMAC_MSG_PWR,
	HALMAC_MSG_SND,
	HALMAC_MSG_COMMON,
	HALMAC_MSG_DBI,
	HALMAC_MSG_MDIO,
	HALMAC_MSG_USB,
};

enum halmac_feature_id {
	HALMAC_FEATURE_CFG_PARA,                /* Support */
	HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,     /* Support */
	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,      /* Support */
	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE_MASK, /* Support */
	HALMAC_FEATURE_UPDATE_PACKET,           /* Support */
	HALMAC_FEATURE_SEND_SCAN_PACKET,        /* Support */
	HALMAC_FEATURE_DROP_SCAN_PACKET,        /* Support */
	HALMAC_FEATURE_UPDATE_DATAPACK,
	HALMAC_FEATURE_RUN_DATAPACK,
	HALMAC_FEATURE_CHANNEL_SWITCH,  /* Support */
	HALMAC_FEATURE_IQK,             /* Support */
	HALMAC_FEATURE_POWER_TRACKING,  /* Support */
	HALMAC_FEATURE_PSD,             /* Support */
	HALMAC_FEATURE_FW_SNDING,       /* Support */
	HALMAC_FEATURE_ALL,             /* Support, only for reset */
};

enum halmac_drv_rsvd_pg_num {
	HALMAC_RSVD_PG_NUM8,	/* 1K */
	HALMAC_RSVD_PG_NUM16,   /* 2K */
	HALMAC_RSVD_PG_NUM24,   /* 3K */
	HALMAC_RSVD_PG_NUM32,   /* 4K */
	HALMAC_RSVD_PG_NUM64,   /* 8K */
	HALMAC_RSVD_PG_NUM128,  /* 16K */
	HALMAC_RSVD_PG_NUM256,  /* 32K */
};

enum halmac_pcie_cfg {
	HALMAC_PCIE_GEN1,
	HALMAC_PCIE_GEN2,
	HALMAC_PCIE_CFG_UNDEFINE,
};

enum halmac_portid {
	HALMAC_PORTID0 = 0,
	HALMAC_PORTID1 = 1,
	HALMAC_PORTID2 = 2,
	HALMAC_PORTID3 = 3,
	HALMAC_PORTID4 = 4,
	HALMAC_PORTID_NUM = 5,
};

struct halmac_bcn_ctrl {
	u8 dis_rx_bssid_fit;
	u8 en_txbcn_rpt;
	u8 dis_tsf_udt;
	u8 en_bcn;
	u8 en_rxbcn_rpt;
	u8 en_p2p_ctwin;
	u8 en_p2p_bcn_area;
};

/* User only can use  Address[6]*/
/* Address[0] is lowest, Address[5] is highest */
union halmac_wlan_addr {
	u8 addr[6];
	struct {
		union {
			__le32 low;
			u8 low_byte[4];
		};
		union {
			__le16 high;
			u8 high_byte[2];
		};
	} addr_l_h;
};

struct halmac_platform_api {
	/* R/W register */
	u8 (*SDIO_CMD52_READ)(void *drv_adapter, u32 offset);
	u8 (*SDIO_CMD53_READ_8)(void *drv_adapter, u32 offset);
	u16 (*SDIO_CMD53_READ_16)(void *drv_adapter, u32 offset);
	u32 (*SDIO_CMD53_READ_32)(void *drv_adapter, u32 offset);
	u8 (*SDIO_CMD53_READ_N)(void *drv_adapter, u32 offset, u32 size,
				u8 *data);
	void (*SDIO_CMD52_WRITE)(void *drv_adapter, u32 offset, u8 value);
	void (*SDIO_CMD53_WRITE_8)(void *drv_adapter, u32 offset, u8 value);
	void (*SDIO_CMD53_WRITE_16)(void *drv_adapter, u32 offset, u16 value);
	void (*SDIO_CMD53_WRITE_32)(void *drv_adapter, u32 offset, u32 value);
	u8 (*REG_READ_8)(void *drv_adapter, u32 offset);
	u16 (*REG_READ_16)(void *drv_adapter, u32 offset);
	u32 (*REG_READ_32)(void *drv_adapter, u32 offset);
	void (*REG_WRITE_8)(void *drv_adapter, u32 offset, u8 value);
	void (*REG_WRITE_16)(void *drv_adapter, u32 offset, u16 value);
	void (*REG_WRITE_32)(void *drv_adapter, u32 offset, u32 value);
	u8 (*SDIO_CMD52_CIA_READ)(void *drv_adapter, u32 offset);

	/* send pBuf to reserved page, the tx_desc is not included in pBuf */
	/* driver need to fill tx_desc with qsel = bcn */
	u8 (*SEND_RSVD_PAGE)(void *drv_adapter, u8 *buf, u32 size);
	/* send pBuf to h2c queue, the tx_desc is not included in pBuf */
	/* driver need to fill tx_desc with qsel = h2c */
	u8 (*SEND_H2C_PKT)(void *drv_adapter, u8 *buf, u32 size);

	u8 (*RTL_FREE)(void *drv_adapter, void *buf, u32 size);
	void* (*RTL_MALLOC)(void *drv_adapter, u32 size);
	u8 (*RTL_MEMCPY)(void *drv_adapter, void *dest, void *src, u32 size);
	u8 (*RTL_MEMSET)(void *drv_adapter, void *addr, u8 value, u32 size);
	void (*RTL_DELAY_US)(void *drv_adapter, u32 us);

	u8 (*MUTEX_INIT)(void *drv_adapter, HALMAC_MUTEX *mutex);
	u8 (*MUTEX_DEINIT)(void *drv_adapter, HALMAC_MUTEX *mutex);
	u8 (*MUTEX_LOCK)(void *drv_adapter, HALMAC_MUTEX *mutex);
	u8 (*MUTEX_UNLOCK)(void *drv_adapter, HALMAC_MUTEX *mutex);

	u8 (*MSG_PRINT)(void *drv_adapter, u32 msg_type, u8 msg_level,
			s8 *fmt, ...);
	u8 (*BUFF_PRINT)(void *drv_adapter, u32 msg_type, u8 msg_level, s8 *buf,
			 u32 size);

	u8 (*EVENT_INDICATION)(void *drv_adapter,
			       enum halmac_feature_id feature_id,
			       enum halmac_cmd_process_status process_status,
			       u8 *buf, u32 size);
#if HALMAC_DBG_MONITOR_IO
	void (*READ_MONITOR)(void *drv_adapter, u32 offset, u32 byte, u32 val,
			     const char *caller, const u32 line);
	void (*WRITE_MONITOR)(void *drv_adapter, u32 offset, u32 byte, u32 val,
			      const char *caller, const u32 line);
#endif
#if HALMAC_PLATFORM_TESTPROGRAM
	struct halmisc_platform_api *halmisc_pltfm_api;
#endif
};

enum halmac_snd_role {
	HAL_BFER = 0,
	HAL_BFEE = 1,
};

enum halmac_csi_seg_len {
	HAL_CSI_SEG_4K = 0,
	HAL_CSI_SEG_8K = 1,
	HAL_CSI_SEG_11K = 2,
};

struct halmac_cfg_mumimo_para {
	enum halmac_snd_role role;
	u8 sounding_sts[6];
	u16 grouping_bitmap;
	u8 mu_tx_en;
	u32 given_gid_tab[2];
	u32 given_user_pos[4];
};

struct halmac_su_bfer_init_para {
	u8 userid;
	u16 paid;
	u16 csi_para;
	union halmac_wlan_addr bfer_address;
};

struct halmac_mu_bfee_init_para {
	u8 userid;
	u16 paid;
	u32 user_position_l;	/*for gid 0~15*/
	u32 user_position_h;	/*for gid 16~31*/
	u32 user_position_l_1;	/*for gid 32~47*/
	u32 user_position_h_1;	/*for gid 48~63*/
};

struct halmac_mu_bfer_init_para {
	u16 paid;
	u16 csi_para;
	u16 my_aid;
	enum halmac_csi_seg_len csi_length_sel;
	union halmac_wlan_addr bfer_address;
};

struct halmac_ch_sw_info {
	u8 *buf;
	u8 *buf_wptr;
	u8 scan_mode;
	u8 extra_info_en;
	u32 buf_size;
	u32 avl_buf_size;
	u32 total_size;
	u32 ch_num;
};

struct halmac_ch_sw_extra_scan {
	u8 dwell_ext_val:7;
	u8 dwell_ext_en:1;
	u8 dwell_ext_c2h:1;
	u8 post_tx_c2h:1;
	u8 pre_tx_c2h:1;
	u8 post_switch_c2h:1;
	u8 pre_switch_c2h:1;
	u8 rsvd0:2;
	u8 wait_probrsp:1;
	u8 txid:7;
	u8 rsvd1:1;
};

struct halmac_scan_rpt_info {
	u8 *buf;
	u8 *buf_wptr;
	u32 buf_size;
	u32 avl_buf_size;
	u32 total_size;
	u32 ack_tsf_low;
	u32 ack_tsf_high;
	u32 rpt_tsf_low;
	u32 rpt_tsf_high;
};

struct halmac_event_trigger {
	u32 phy_efuse_map : 1;
	u32 log_efuse_map : 1;
	u32 log_efuse_mask : 1;
	u32 rsvd1 : 27;
};

struct halmac_h2c_header_info {
	u16 sub_cmd_id;
	u16 content_size;
	u8 ack;
};

struct halmac_ver {
	u8 major_ver;
	u8 prototype_ver;
	u8 minor_ver;
};

enum halmac_api_id {
	/*stuff, need to be the 1st*/
	HALMAC_API_STUFF = 0x0,
	/*stuff, need to be the 1st*/
	HALMAC_API_MAC_POWER_SWITCH = 0x1,
	HALMAC_API_DOWNLOAD_FIRMWARE = 0x2,
	HALMAC_API_CFG_MAC_ADDR = 0x3,
	HALMAC_API_CFG_BSSID = 0x4,
	HALMAC_API_CFG_MULTICAST_ADDR = 0x5,
	HALMAC_API_PRE_INIT_SYSTEM_CFG = 0x6,
	HALMAC_API_INIT_SYSTEM_CFG = 0x7,
	HALMAC_API_INIT_TRX_CFG = 0x8,
	HALMAC_API_CFG_RX_AGGREGATION = 0x9,
	HALMAC_API_INIT_PROTOCOL_CFG = 0xA,
	HALMAC_API_INIT_EDCA_CFG = 0xB,
	HALMAC_API_CFG_OPERATION_MODE = 0xC,
	HALMAC_API_CFG_CH_BW = 0xD,
	HALMAC_API_CFG_BW = 0xE,
	HALMAC_API_INIT_WMAC_CFG = 0xF,
	HALMAC_API_INIT_MAC_CFG = 0x10,
	HALMAC_API_INIT_SDIO_CFG = 0x11,
	HALMAC_API_INIT_USB_CFG = 0x12,
	HALMAC_API_INIT_PCIE_CFG = 0x13,
	HALMAC_API_INIT_INTERFACE_CFG = 0x14,
	HALMAC_API_DEINIT_SDIO_CFG = 0x15,
	HALMAC_API_DEINIT_USB_CFG = 0x16,
	HALMAC_API_DEINIT_PCIE_CFG = 0x17,
	HALMAC_API_DEINIT_INTERFACE_CFG = 0x18,
	HALMAC_API_GET_EFUSE_SIZE = 0x19,
	HALMAC_API_DUMP_EFUSE_MAP = 0x1A,
	HALMAC_API_GET_LOGICAL_EFUSE_SIZE = 0x1D,
	HALMAC_API_DUMP_LOGICAL_EFUSE_MAP = 0x1E,
	HALMAC_API_WRITE_LOGICAL_EFUSE = 0x1F,
	HALMAC_API_READ_LOGICAL_EFUSE = 0x20,
	HALMAC_API_PG_EFUSE_BY_MAP = 0x21,
	HALMAC_API_GET_C2H_INFO = 0x22,
	HALMAC_API_CFG_FWLPS_OPTION = 0x23,
	HALMAC_API_CFG_FWIPS_OPTION = 0x24,
	HALMAC_API_ENTER_WOWLAN = 0x25,
	HALMAC_API_LEAVE_WOWLAN = 0x26,
	HALMAC_API_ENTER_PS = 0x27,
	HALMAC_API_LEAVE_PS = 0x28,
	HALMAC_API_H2C_LB = 0x29,
	HALMAC_API_DEBUG = 0x2A,
	HALMAC_API_CFG_PARAMETER = 0x2B,
	HALMAC_API_UPDATE_PACKET = 0x2C,
	HALMAC_API_BCN_IE_FILTER = 0x2D,
	HALMAC_API_REG_READ_8 = 0x2E,
	HALMAC_API_REG_WRITE_8 = 0x2F,
	HALMAC_API_REG_READ_16 = 0x30,
	HALMAC_API_REG_WRITE_16 = 0x31,
	HALMAC_API_REG_READ_32 = 0x32,
	HALMAC_API_REG_WRITE_32 = 0x33,
	HALMAC_API_TX_ALLOWED_SDIO = 0x34,
	HALMAC_API_SET_BULKOUT_NUM = 0x35,
	HALMAC_API_GET_SDIO_TX_ADDR = 0x36,
	HALMAC_API_GET_USB_BULKOUT_ID = 0x37,
	HALMAC_API_TIMER_2S = 0x38,
	HALMAC_API_FILL_TXDESC_CHECKSUM = 0x39,
	HALMAC_API_SEND_ORIGINAL_H2C = 0x3A,
	HALMAC_API_UPDATE_DATAPACK = 0x3B,
	HALMAC_API_RUN_DATAPACK = 0x3C,
	HALMAC_API_CFG_DRV_INFO = 0x3D,
	HALMAC_API_SEND_BT_COEX = 0x3E,
	HALMAC_API_VERIFY_PLATFORM_API = 0x3F,
	HALMAC_API_GET_FIFO_SIZE = 0x40,
	HALMAC_API_DUMP_FIFO = 0x41,
	HALMAC_API_CFG_TXBF = 0x42,
	HALMAC_API_CFG_MUMIMO = 0x43,
	HALMAC_API_CFG_SOUNDING = 0x44,
	HALMAC_API_DEL_SOUNDING = 0x45,
	HALMAC_API_SU_BFER_ENTRY_INIT = 0x46,
	HALMAC_API_SU_BFEE_ENTRY_INIT = 0x47,
	HALMAC_API_MU_BFER_ENTRY_INIT = 0x48,
	HALMAC_API_MU_BFEE_ENTRY_INIT = 0x49,
	HALMAC_API_SU_BFER_ENTRY_DEL = 0x4A,
	HALMAC_API_SU_BFEE_ENTRY_DEL = 0x4B,
	HALMAC_API_MU_BFER_ENTRY_DEL = 0x4C,
	HALMAC_API_MU_BFEE_ENTRY_DEL = 0x4D,
	HALMAC_API_ADD_CH_INFO = 0x4E,
	HALMAC_API_ADD_EXTRA_CH_INFO = 0x4F,
	HALMAC_API_CTRL_CH_SWITCH = 0x50,
	HALMAC_API_CLEAR_CH_INFO = 0x51,
	HALMAC_API_SEND_GENERAL_INFO = 0x52,
	HALMAC_API_START_IQK = 0x53,
	HALMAC_API_CTRL_PWR_TRACKING = 0x54,
	HALMAC_API_PSD = 0x55,
	HALMAC_API_CFG_TX_AGG_ALIGN = 0x56,
	HALMAC_API_QUERY_STATE = 0x57,
	HALMAC_API_RESET_FEATURE = 0x58,
	HALMAC_API_CHECK_FW_STATUS = 0x59,
	HALMAC_API_DUMP_FW_DMEM = 0x5A,
	HALMAC_API_CFG_MAX_DL_SIZE = 0x5B,
	HALMAC_API_INIT_OBJ = 0x5C,
	HALMAC_API_DEINIT_OBJ = 0x5D,
	HALMAC_API_CFG_LA_MODE = 0x5E,
	HALMAC_API_GET_HW_VALUE = 0x5F,
	HALMAC_API_SET_HW_VALUE = 0x60,
	HALMAC_API_CFG_DRV_RSVD_PG_NUM = 0x61,
	HALMAC_API_WRITE_EFUSE_BT = 0x63,
	HALMAC_API_DUMP_EFUSE_MAP_BT = 0x64,
	HALMAC_API_DL_DRV_RSVD_PG = 0x65,
	HALMAC_API_PCIE_SWITCH = 0x66,
	HALMAC_API_PHY_CFG = 0x67,
	HALMAC_API_CFG_RX_FIFO_EXPANDING_MODE = 0x68,
	HALMAC_API_CFG_CSI_RATE = 0x69,
	HALMAC_API_P2PPS = 0x6A,
	HALMAC_API_CFG_TX_ADDR = 0x6B,
	HALMAC_API_CFG_NET_TYPE = 0x6C,
	HALMAC_API_CFG_TSF_RESET = 0x6D,
	HALMAC_API_CFG_BCN_SPACE = 0x6E,
	HALMAC_API_CFG_BCN_CTRL = 0x6F,
	HALMAC_API_CFG_SIDEBAND_INT = 0x70,
	HALMAC_API_REGISTER_API = 0x71,
	HALMAC_API_FREE_DOWNLOAD_FIRMWARE = 0x72,
	HALMAC_API_GET_FW_VERSION = 0x73,
	HALMAC_API_GET_EFUSE_AVAL_SIZE = 0x74,
	HALMAC_API_CHK_TXDESC = 0x75,
	HALMAC_API_SDIO_CMD53_4BYTE = 0x76,
	HALMAC_API_CFG_TRANS_ADDR = 0x77,
	HALMAC_API_INTF_INTEGRA_TUNING	= 0x78,
	HALMAC_API_TXFIFO_IS_EMPTY = 0x79,
	HALMAC_API_DOWNLOAD_FLASH = 0x7A,
	HALMAC_API_READ_FLASH = 0x7B,
	HALMAC_API_ERASE_FLASH = 0x7C,
	HALMAC_API_CHECK_FLASH = 0x7D,
	HALMAC_API_SDIO_HW_INFO = 0x80,
	HALMAC_API_READ_EFUSE_BT = 0x81,
	HALMAC_API_CFG_EFUSE_AUTO_CHECK = 0x82,
	HALMAC_API_CFG_PINMUX_GET_FUNC = 0x83,
	HALMAC_API_CFG_PINMUX_SET_FUNC = 0x84,
	HALMAC_API_CFG_PINMUX_FREE_FUNC = 0x85,
	HALMAC_API_CFG_PINMUX_WL_LED_MODE = 0x86,
	HALMAC_API_CFG_PINMUX_WL_LED_SW_CTRL = 0x87,
	HALMAC_API_CFG_PINMUX_SDIO_INT_POLARITY = 0x88,
	HALMAC_API_CFG_PINMUX_GPIO_MODE = 0x89,
	HALMAC_API_CFG_PINMUX_GPIO_OUTPUT = 0x90,
	HALMAC_API_REG_READ_INDIRECT_32 = 0x91,
	HALMAC_API_REG_SDIO_CMD53_READ_N = 0x92,
	HALMAC_API_PINMUX_PIN_STATUS = 0x94,
	HALMAC_API_OFLD_FUNC_CFG = 0x95,
	HALMAC_API_MASK_LOGICAL_EFUSE = 0x96,
	HALMAC_API_RX_CUT_AMSDU_CFG = 0x97,
	HALMAC_API_FW_SNDING = 0x98,
	HALMAC_API_ENTER_CPU_SLEEP_MODE = 0x99,
	HALMAC_API_GET_CPU_MODE = 0x9A,
	HALMAC_API_DRV_FWCTRL = 0x9B,
	HALMAC_API_EN_REF_AUTOK = 0x9C,
	HALMAC_API_RESET_WIFI_FW = 0x9D,
	HALMAC_API_CFGSPC_SET_PCIE = 0x9E,
	HALMAC_API_GET_WATCHER = 0x9F,
	HALMAC_API_DUMP_LOGICAL_EFUSE_MASK = 0xA0,
	HALMAC_API_READ_WIFI_PHY_EFUSE = 0xA1,
	HALMAC_API_WRITE_WIFI_PHY_EFUSE = 0xA2,
	HALMAC_API_MAX
};

enum halmac_la_mode {
	HALMAC_LA_MODE_DISABLE = 0,
	HALMAC_LA_MODE_PARTIAL = 1,
	HALMAC_LA_MODE_FULL = 2,
	HALMAC_LA_MODE_UNDEFINE = 0x7F,
};

enum halmac_rx_fifo_expanding_mode {
	HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE = 0,
	HALMAC_RX_FIFO_EXPANDING_MODE_1_BLOCK = 1,
	HALMAC_RX_FIFO_EXPANDING_MODE_2_BLOCK = 2,
	HALMAC_RX_FIFO_EXPANDING_MODE_3_BLOCK = 3,
	HALMAC_RX_FIFO_EXPANDING_MODE_4_BLOCK = 4,
	HALMAC_RX_FIFO_EXPANDING_MODE_UNDEFINE = 0x7F,
};

enum halmac_sdio_cmd53_4byte_mode {
	HALMAC_SDIO_CMD53_4BYTE_MODE_DISABLE = 0,
	HALMAC_SDIO_CMD53_4BYTE_MODE_RW = 1,
	HALMAC_SDIO_CMD53_4BYTE_MODE_R = 2,
	HALMAC_SDIO_CMD53_4BYTE_MODE_W = 3,
	HALMAC_SDIO_CMD53_4BYTE_MODE_UNDEFINE = 0x7F,
};

enum halmac_usb_mode {
	HALMAC_USB_MODE_U2 = 1,
	HALMAC_USB_MODE_U3 = 2,
};

enum halmac_sdio_tx_format {
	HALMAC_SDIO_AGG_MODE = 1,
	HALMAC_SDIO_DUMMY_BLOCK_MODE = 2,
	HALMAC_SDIO_DUMMY_AUTO_MODE = 3,
};

enum halmac_sdio_clk_monitor {
	HALMAC_MONITOR_5US = 1,
	HALMAC_MONITOR_50US = 2,
	HALMAC_MONITOR_9MS = 3,
};

enum halmac_hw_id {
	/* Get HW value */
	HALMAC_HW_RQPN_MAPPING = 0x00,
	HALMAC_HW_EFUSE_SIZE = 0x01,
	HALMAC_HW_EEPROM_SIZE = 0x02,
	HALMAC_HW_BT_BANK_EFUSE_SIZE = 0x03,
	HALMAC_HW_BT_BANK1_EFUSE_SIZE = 0x04,
	HALMAC_HW_BT_BANK2_EFUSE_SIZE = 0x05,
	HALMAC_HW_TXFIFO_SIZE = 0x06,
	HALMAC_HW_RXFIFO_SIZE = 0x07,
	HALMAC_HW_RSVD_PG_BNDY = 0x08,
	HALMAC_HW_CAM_ENTRY_NUM = 0x09,
	HALMAC_HW_IC_VERSION = 0x0A,
	HALMAC_HW_PAGE_SIZE = 0x0B,
	HALMAC_HW_TX_AGG_ALIGN_SIZE = 0x0C,
	HALMAC_HW_RX_AGG_ALIGN_SIZE = 0x0D,
	HALMAC_HW_DRV_INFO_SIZE = 0x0E,
	HALMAC_HW_TXFF_ALLOCATION = 0x0F,
	HALMAC_HW_RSVD_EFUSE_SIZE = 0x10,
	HALMAC_HW_FW_HDR_SIZE = 0x11,
	HALMAC_HW_TX_DESC_SIZE = 0x12,
	HALMAC_HW_RX_DESC_SIZE = 0x13,
	HALMAC_HW_FW_MAX_SIZE = 0x14,
	HALMAC_HW_ORI_H2C_SIZE = 0x15,
	HALMAC_HW_RSVD_DRV_PGNUM = 0x16,
	HALMAC_HW_TX_PAGE_SIZE = 0x17,
	HALMAC_HW_USB_TXAGG_DESC_NUM = 0x18,
	HALMAC_HW_WLAN_EFUSE_AVAILABLE_SIZE = 0x19,
	HALMAC_HW_AC_OQT_SIZE = 0x1C,
	HALMAC_HW_NON_AC_OQT_SIZE = 0x1D,
	HALMAC_HW_AC_QUEUE_NUM = 0x1E,
	HALMAC_HW_RQPN_CH_MAPPING = 0x1F,
	HALMAC_HW_PWR_STATE = 0x20,
	HALMAC_HW_SDIO_INT_LAT = 0x21,
	HALMAC_HW_SDIO_CLK_CNT = 0x22,
	/* Set HW value */
	HALMAC_HW_USB_MODE = 0x60,
	HALMAC_HW_SEQ_EN = 0x61,
	HALMAC_HW_BANDWIDTH = 0x62,
	HALMAC_HW_CHANNEL = 0x63,
	HALMAC_HW_PRI_CHANNEL_IDX = 0x64,
	HALMAC_HW_EN_BB_RF = 0x65,
	HALMAC_HW_SDIO_TX_PAGE_THRESHOLD = 0x66,
	HALMAC_HW_AMPDU_CONFIG = 0x67,
	HALMAC_HW_RX_SHIFT = 0x68,
	HALMAC_HW_TXDESC_CHECKSUM = 0x69,
	HALMAC_HW_RX_CLK_GATE = 0x6A,
	HALMAC_HW_RXGCK_FIFO = 0x6B,
	HALMAC_HW_RX_IGNORE = 0x6C,
	HALMAC_HW_SDIO_TX_FORMAT = 0x6D,
	HALMAC_HW_FAST_EDCA = 0x6E,
	HALMAC_HW_LDO25_EN = 0x6F,
	HALMAC_HW_PCIE_REF_AUTOK = 0x70,
	HALMAC_HW_RTS_FULL_BW = 0x71,
	HALMAC_HW_FREE_CNT_EN = 0x72,
	HALMAC_HW_SDIO_WT_EN = 0x73,
	HALMAC_HW_SDIO_CLK_MONITOR = 0x74,
	HALMAC_HW_TXFIFO_LIFETIME = 0x75,
	HALMAC_HW_ID_UNDEFINE = 0x7F,
};

enum halmac_efuse_bank {
	HALMAC_EFUSE_BANK_WIFI = 0,
	HALMAC_EFUSE_BANK_BT = 1,
	HALMAC_EFUSE_BANK_BT_1 = 2,
	HALMAC_EFUSE_BANK_BT_2 = 3,
	HALMAC_EFUSE_BANK_MAX,
	HALMAC_EFUSE_BANK_UNDEFINE = 0X7F,
};

enum halmac_sdio_spec_ver {
	HALMAC_SDIO_SPEC_VER_2_00 = 0,
	HALMAC_SDIO_SPEC_VER_3_00 = 1,
	HALMAC_SDIO_SPEC_VER_UNDEFINE = 0X7F,
};

enum halmac_gpio_func {
	HALMAC_GPIO_FUNC_WL_LED = 0,
	HALMAC_GPIO_FUNC_SDIO_INT = 1,
	HALMAC_GPIO_FUNC_SW_IO_0 = 2,
	HALMAC_GPIO_FUNC_SW_IO_1 = 3,
	HALMAC_GPIO_FUNC_SW_IO_2 = 4,
	HALMAC_GPIO_FUNC_SW_IO_3 = 5,
	HALMAC_GPIO_FUNC_SW_IO_4 = 6,
	HALMAC_GPIO_FUNC_SW_IO_5 = 7,
	HALMAC_GPIO_FUNC_SW_IO_6 = 8,
	HALMAC_GPIO_FUNC_SW_IO_7 = 9,
	HALMAC_GPIO_FUNC_SW_IO_8 = 10,
	HALMAC_GPIO_FUNC_SW_IO_9 = 11,
	HALMAC_GPIO_FUNC_SW_IO_10 = 12,
	HALMAC_GPIO_FUNC_SW_IO_11 = 13,
	HALMAC_GPIO_FUNC_SW_IO_12 = 14,
	HALMAC_GPIO_FUNC_SW_IO_13 = 15,
	HALMAC_GPIO_FUNC_SW_IO_14 = 16,
	HALMAC_GPIO_FUNC_SW_IO_15 = 17,
	HALMAC_GPIO_FUNC_BT_HOST_WAKE1 = 18,
	HALMAC_GPIO_FUNC_BT_DEV_WAKE1 = 19,
	HALMAC_GPIO_FUNC_S0_PAPE = 20,
	HALMAC_GPIO_FUNC_S1_PAPE = 21,
	HALMAC_GPIO_FUNC_S0_TRSW = 22,
	HALMAC_GPIO_FUNC_S1_TRSW = 23,
	HALMAC_GPIO_FUNC_S0_TRSWB = 24,
	HALMAC_GPIO_FUNC_S1_TRSWB = 25,
	HALMAC_GPIO_FUNC_UNDEFINE = 0X7F,
};

enum halmac_wlled_mode {
	HALMAC_WLLED_MODE_TRX = 0,
	HALMAC_WLLED_MODE_TX = 1,
	HALMAC_WLLED_MODE_RX = 2,
	HALMAC_WLLED_MODE_SW_CTRL = 3,
	HALMAC_WLLED_MODE_UNDEFINE = 0X7F,
};

enum halmac_psf_fcs_chk_thr {
	HALMAC_PSF_FCS_CHK_THR_4 = 1,
	HALMAC_PSF_FCS_CHK_THR_8 = 2,
	HALMAC_PSF_FCS_CHK_THR_12 = 3,
	HALMAC_PSF_FCS_CHK_THR_16 = 4,
	HALMAC_PSF_FCS_CHK_THR_20 = 5,
	HALMAC_PSF_FCS_CHK_THR_24 = 6,
	HALMAC_PSF_FCS_CHK_THR_28 = 7,
};

enum halmac_func_ctrl {
	HALMAC_DISABLE = 0,
	HALMAC_ENABLE = 1,
	HALMAC_DEFAULT = 0xFE,
	HALMAC_IGNORE = 0xFF
};

enum halmac_pcie_clkdly {
	HALMAC_CLKDLY_0 = 0,
	HALMAC_CLKDLY_5US = 1,
	HALMAC_CLKDLY_6US = 2,
	HALMAC_CLKDLY_11US = 3,
	HALMAC_CLKDLY_15US = 4,
	HALMAC_CLKDLY_19US = 5,
	HALMAC_CLKDLY_25US = 6,
	HALMAC_CLKDLY_30US = 7,
	HALMAC_CLKDLY_38US = 8,
	HALMAC_CLKDLY_50US = 9,
	HALMAC_CLKDLY_64US = 10,
	HALMAC_CLKDLY_100US = 11,
	HALMAC_CLKDLY_128US = 12,
	HALMAC_CLKDLY_150US = 13,
	HALMAC_CLKDLY_192US = 14,
	HALMAC_CLKDLY_200US = 15,
	HALMAC_CLKDLY_R_ERR = 0xFD,
	HALMAC_CLKDLY_DEF = 0xFE,
	HALMAC_CLKDLY_IGNORE = 0xFF
};

enum halmac_pcie_l1dly {
	HALMAC_L1DLY_16US = 0,
	HALMAC_L1DLY_32US = 1,
	HALMAC_L1DLY_64US = 2,
	HALMAC_L1DLY_INFI = 3,
	HALMAC_L1DLY_R_ERR = 0xFD,
	HALMAC_L1DLY_DEF = 0xFE,
	HALMAC_L1DLY_IGNORE = 0xFF
};

enum halmac_pcie_l0sdly {
	HALMAC_L0SDLY_1US = 0,
	HALMAC_L0SDLY_3US = 1,
	HALMAC_L0SDLY_5US = 2,
	HALMAC_L0SDLY_7US = 3,
	HALMAC_L0SDLY_R_ERR = 0xFD,
	HALMAC_L0SDLY_DEF = 0xFE,
	HALMAC_L0SDLY_IGNORE = 0xFF
};

struct halmac_pcie_cfgspc_param {
	u8 write;
	u8 read;
	enum halmac_func_ctrl l0s_ctrl;
	enum halmac_func_ctrl l1_ctrl;
	enum halmac_func_ctrl l1ss_ctrl;
	enum halmac_func_ctrl wake_ctrl;
	enum halmac_func_ctrl crq_ctrl;
	enum halmac_pcie_clkdly clkdly_ctrl;
	enum halmac_pcie_l0sdly l0sdly_ctrl;
	enum halmac_pcie_l1dly l1dly_ctrl;
};

struct halmac_txff_allocation {
	u16 tx_fifo_pg_num;
	u16 rsvd_pg_num;
	u16 rsvd_drv_pg_num;
	u16 acq_pg_num;
	u16 high_queue_pg_num;
	u16 low_queue_pg_num;
	u16 normal_queue_pg_num;
	u16 extra_queue_pg_num;
	u16 pub_queue_pg_num;
	u16 rsvd_boundary;
	u16 rsvd_drv_addr;
	u16 rsvd_h2c_info_addr;
	u16 rsvd_h2c_sta_info_addr;
	u16 rsvd_h2cq_addr;
	u16 rsvd_cpu_instr_addr;
	u16 rsvd_fw_txbuf_addr;
	u16 rsvd_csibuf_addr;
	enum halmac_la_mode la_mode;
	enum halmac_rx_fifo_expanding_mode rx_fifo_exp_mode;
};

struct halmac_rqpn_map {
	enum halmac_dma_mapping dma_map_vo;
	enum halmac_dma_mapping dma_map_vi;
	enum halmac_dma_mapping dma_map_be;
	enum halmac_dma_mapping dma_map_bk;
	enum halmac_dma_mapping dma_map_mg;
	enum halmac_dma_mapping dma_map_hi;
};

struct halmac_rqpn_ch_map {
	enum halmac_dma_ch dma_map_vo;
	enum halmac_dma_ch dma_map_vi;
	enum halmac_dma_ch dma_map_be;
	enum halmac_dma_ch dma_map_bk;
	enum halmac_dma_ch dma_map_mg;
	enum halmac_dma_ch dma_map_hi;
};

struct halmac_security_setting {
	u8 tx_encryption;
	u8 rx_decryption;
	u8 bip_enable;
	u8 compare_keyid;
};

struct halmac_cam_entry_info {
	enum hal_security_type security_type;
	u32 key[4];
	u32 key_ext[4];
	u8 mac_address[6];
	u8 unicast;
	u8 key_id;
	u8 valid;
};

struct halmac_cam_entry_format {
	u16 key_id : 2;
	u16 type : 3;
	u16 mic : 1;
	u16 grp : 1;
	u16 spp_mode : 1;
	u16 rpt_md : 1;
	u16 ext_sectype : 1;
	u16 mgnt : 1;
	u16 rsvd1 : 4;
	u16 valid : 1;
	u8 mac_address[6];
	u32 key[4];
	u32 rsvd[2];
};

struct halmac_tx_page_threshold_info {
	u32	threshold;
	enum halmac_dma_mapping dma_queue_sel;
	u8 enable;
};

struct halmac_ampdu_config {
	u8 max_agg_num;
	u8 max_len_en;
	u32 ht_max_len;
	u32 vht_max_len;
};

struct halmac_rqpn {
	enum halmac_trx_mode mode;
	enum halmac_dma_mapping dma_map_vo;
	enum halmac_dma_mapping dma_map_vi;
	enum halmac_dma_mapping dma_map_be;
	enum halmac_dma_mapping dma_map_bk;
	enum halmac_dma_mapping dma_map_mg;
	enum halmac_dma_mapping dma_map_hi;
};

struct halmac_ch_mapping {
	enum halmac_trx_mode mode;
	enum halmac_dma_ch dma_map_vo;
	enum halmac_dma_ch dma_map_vi;
	enum halmac_dma_ch dma_map_be;
	enum halmac_dma_ch dma_map_bk;
	enum halmac_dma_ch dma_map_mg;
	enum halmac_dma_ch dma_map_hi;
};

struct halmac_pg_num {
	enum halmac_trx_mode mode;
	u16 hq_num;
	u16 nq_num;
	u16 lq_num;
	u16 exq_num;
	u16 gap_num;/*used for loopback mode*/
};

struct halmac_ch_pg_num {
	enum halmac_trx_mode mode;
	u16 ch_num[HALMAC_TXDESC_DMA_CH16 + 1];
	u16 gap_num;
};

struct halmac_intf_phy_para {
	u16 offset;
	u16 value;
	u16 ip_sel;
	u16 cut;
	u16 plaform;
};

struct halmac_iqk_para {
	u8 clear;
	u8 segment_iqk;
};

struct halmac_txdesc_ie_param {
	u8 *start_offset;
	u8 *end_offset;
	u8 *ie_offset;
	u8 *ie_exist;
};

struct halmac_sdio_hw_info {
	enum halmac_sdio_spec_ver spec_ver;
	u32 clock_speed;
	u8 io_hi_speed_flag; /* Halmac internal use */
	enum halmac_sdio_tx_format tx_addr_format;
	u16 block_size;
	u8 tx_seq;
	u8 io_indir_flag; /* Halmac internal use */
	u8 io_warn_flag; /* SW */
};

struct halmac_edca_para {
	u8 aifs;
	u8 cw;
	u16 txop_limit;
};

struct halmac_mac_rx_ignore_cfg {
	u8 hdr_chk_en;
	u8 fcs_chk_en;
	u8 cck_rst_en;
	enum halmac_psf_fcs_chk_thr fcs_chk_thr;
};

struct halmac_rx_ignore_info {
	u8 hdr_chk_mask;
	u8 fcs_chk_mask;
	u8 hdr_chk_en;
	u8 fcs_chk_en;
	u8 cck_rst_en;
	enum halmac_psf_fcs_chk_thr fcs_chk_thr;
};

struct halmac_get_watcher {
	u32 sdio_rn_not_align;
};

struct halmac_watcher {
	struct halmac_get_watcher get_watcher;
};

struct halmac_pinmux_info {
	/* byte0 */
	u8 wl_led:1;
	u8 sdio_int:1;
	u8 bt_host_wake:1;
	u8 bt_dev_wake:1;
	u8 rsvd1:4;
	/* byte1 */
	u8 sw_io_0:1;
	u8 sw_io_1:1;
	u8 sw_io_2:1;
	u8 sw_io_3:1;
	u8 sw_io_4:1;
	u8 sw_io_5:1;
	u8 sw_io_6:1;
	u8 sw_io_7:1;
	/* byte2 */
	u8 sw_io_8:1;
	u8 sw_io_9:1;
	u8 sw_io_10:1;
	u8 sw_io_11:1;
	u8 sw_io_12:1;
	u8 sw_io_13:1;
	u8 sw_io_14:1;
	u8 sw_io_15:1;
	/* byte3 */
	u8 s0_trsw:1;
	u8 s1_trsw:1;
	u8 s0_pape:1;
	u8 s1_pape:1;
	u8 s0_trswb:1;
	u8 s1_trswb:1;
};

struct halmac_ofld_func_info {
	u32 halmac_malloc_max_sz;
	u32 rsvd_pg_drv_buf_max_sz;
};

struct halmac_pltfm_cfg_info {
	u32 malloc_size;
	u32 rsvd_pg_size;
};

struct halmac_su_snding_info {
	u8 su0_en;
	u8 *su0_ndpa_pkt;
	u32 su0_pkt_sz;
};

struct halmac_mu_snding_info {
	u8 tmp;
};

struct halmac_h2c_info {
	u32 buf_fs;
	u32 buf_size;
	u8 seq_num;
};

struct halmac_adapter {
	enum halmac_dma_mapping pq_map[HALMAC_PQ_MAP_NUM];
	enum halmac_dma_ch ch_map[HALMAC_PQ_MAP_NUM];
	HALMAC_MUTEX h2c_seq_mutex; /* protect h2c seq num */
	HALMAC_MUTEX efuse_mutex; /*protect adapter efuse map */
	HALMAC_MUTEX sdio_indir_mutex; /*protect sdio indirect access */
	struct halmac_cfg_param_info cfg_param_info;
	struct halmac_ch_sw_info ch_sw_info;
	struct halmac_scan_rpt_info scan_rpt_info;
	struct halmac_event_trigger evnt;
	struct halmac_hw_cfg_info hw_cfg_info;
	struct halmac_sdio_free_space sdio_fs;
	struct halmac_api_registry api_registry;
	struct halmac_pinmux_info pinmux_info;
	struct halmac_pltfm_cfg_info pltfm_info;
	struct halmac_h2c_info h2c_info;
	void *drv_adapter;
	u8 *efuse_map;
	void *halmac_api;
	struct halmac_platform_api *pltfm_api;
	u32 efuse_end;
	u32 dlfw_pkt_size;
	enum halmac_chip_id chip_id;
	enum halmac_chip_ver chip_ver;
	struct halmac_fw_version fw_ver;
	struct halmac_state halmac_state;
	enum halmac_interface intf;
	enum halmac_trx_mode trx_mode;
	struct halmac_txff_allocation txff_alloc;
	u8 efuse_map_valid;
	u8 efuse_seg_size;
	u8 rpwm;
	u8 bulkout_num;
	u8 drv_info_size;
	enum halmac_sdio_cmd53_4byte_mode sdio_cmd53_4byte;
	struct halmac_sdio_hw_info sdio_hw_info;
	u8 tx_desc_transfer;
	u8 tx_desc_checksum;
	u8 efuse_auto_check_en;
	u8 pcie_refautok_en;
	u8 pwr_off_flow_flag;
	u8 nlo_flag;
	struct halmac_rx_ignore_info rx_ignore_info;
	struct halmac_watcher watcher;
#if HALMAC_PLATFORM_TESTPROGRAM
	struct halmisc_adapter *halmisc_adapter;
#endif
};

struct halmac_api {
	enum halmac_ret_status
	(*halmac_register_api)(struct halmac_adapter *adapter,
			       struct halmac_api_registry *registry);
	enum halmac_ret_status
	(*halmac_mac_power_switch)(struct halmac_adapter *adapter,
				   enum halmac_mac_power pwr);
	enum halmac_ret_status
	(*halmac_download_firmware)(struct halmac_adapter *adapter, u8 *fw_bin,
				    u32 size);
	enum halmac_ret_status
	(*halmac_free_download_firmware)(struct halmac_adapter *adapter,
					 enum halmac_dlfw_mem mem_sel,
					 u8 *fw_bin, u32 size);
	enum halmac_ret_status
	(*halmac_reset_wifi_fw)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_get_fw_version)(struct halmac_adapter *adapter,
				 struct halmac_fw_version *ver);
	enum halmac_ret_status
	(*halmac_cfg_mac_addr)(struct halmac_adapter *adapter,
			       u8 port, union halmac_wlan_addr *addr);
	enum halmac_ret_status
	(*halmac_cfg_bssid)(struct halmac_adapter *adapter, u8 port,
			    union halmac_wlan_addr *addr);
	enum halmac_ret_status
	(*halmac_cfg_multicast_addr)(struct halmac_adapter *adapter,
				     union halmac_wlan_addr *addr);
	enum halmac_ret_status
	(*halmac_pre_init_system_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_system_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_trx_cfg)(struct halmac_adapter *adapter,
			       enum halmac_trx_mode mode);
	enum halmac_ret_status
	(*halmac_init_h2c)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_cfg_rx_aggregation)(struct halmac_adapter *adapter,
				     struct halmac_rxagg_cfg *cfg);
	enum halmac_ret_status
	(*halmac_init_protocol_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_edca_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_cfg_operation_mode)(struct halmac_adapter *adapter,
				     enum halmac_wireless_mode mode);
	enum halmac_ret_status
	(*halmac_cfg_ch_bw)(struct halmac_adapter *adapter, u8 ch,
			    enum halmac_pri_ch_idx idx, enum halmac_bw bw);
	enum halmac_ret_status
	(*halmac_cfg_bw)(struct halmac_adapter *adapter, enum halmac_bw bw);
	enum halmac_ret_status
	(*halmac_init_wmac_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_mac_cfg)(struct halmac_adapter *adapter,
			       enum halmac_trx_mode mode);
	enum halmac_ret_status
	(*halmac_init_interface_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_deinit_interface_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_sdio_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_usb_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_init_pcie_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_deinit_sdio_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_deinit_usb_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_deinit_pcie_cfg)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_get_efuse_size)(struct halmac_adapter *adapter, u32 *size);
	enum halmac_ret_status
	(*halmac_get_efuse_available_size)(struct halmac_adapter *adapter,
					   u32 *size);
	enum halmac_ret_status
	(*halmac_dump_efuse_map)(struct halmac_adapter *adapter,
				 enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status
	(*halmac_dump_efuse_map_bt)(struct halmac_adapter *adapter,
				    enum halmac_efuse_bank bank, u32 size,
				    u8 *map);
	enum halmac_ret_status
	(*halmac_write_efuse_bt)(struct halmac_adapter *adapter, u32 offset,
				 u8 value, enum halmac_efuse_bank bank);
	enum halmac_ret_status
	(*halmac_read_efuse_bt)(struct halmac_adapter *adapter, u32 offset,
				u8 *value, enum halmac_efuse_bank bank);
	enum halmac_ret_status
	(*halmac_cfg_efuse_auto_check)(struct halmac_adapter *adapter,
				       u8 enable);
	enum halmac_ret_status
	(*halmac_get_logical_efuse_size)(struct halmac_adapter *adapter,
					 u32 *size);
	enum halmac_ret_status
	(*halmac_dump_logical_efuse_map)(struct halmac_adapter *adapter,
					 enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status
	(*halmac_dump_logical_efuse_mask)(struct halmac_adapter *adapter,
					  enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status
	(*halmac_write_logical_efuse)(struct halmac_adapter *adapter,
				      u32 offset, u8 value);
	enum halmac_ret_status
	(*halmac_read_logical_efuse)(struct halmac_adapter *adapter, u32 offset,
				     u8 *value);
	enum halmac_ret_status
	(*halmac_pg_efuse_by_map)(struct halmac_adapter *adapter,
				  struct halmac_pg_efuse_info *info,
				  enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status
	(*halmac_mask_logical_efuse)(struct halmac_adapter *adapter,
				     struct halmac_pg_efuse_info *info);
	enum halmac_ret_status
	(*halmac_get_c2h_info)(struct halmac_adapter *adapter, u8 *buf,
			       u32 size);
	enum halmac_ret_status
	(*halmac_h2c_lb)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_debug)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_cfg_parameter)(struct halmac_adapter *adapter,
				struct halmac_phy_parameter_info *info,
				u8 full_fifo);
	enum halmac_ret_status
	(*halmac_update_packet)(struct halmac_adapter *adapter,
				enum halmac_packet_id pkt_id, u8 *pkt,
				u32 size);
	enum halmac_ret_status
	(*halmac_bcn_ie_filter)(struct halmac_adapter *adapter,
				struct halmac_bcn_ie_info *info);
	u8
	(*halmac_reg_read_8)(struct halmac_adapter *adapter, u32 offset);
	enum halmac_ret_status
	(*halmac_reg_write_8)(struct halmac_adapter *adapter, u32 offset,
			      u8 value);
	u16
	(*halmac_reg_read_16)(struct halmac_adapter *adapter, u32 offset);
	enum halmac_ret_status
	(*halmac_reg_write_16)(struct halmac_adapter *adapter, u32 offset,
			       u16 value);
	u32
	(*halmac_reg_read_32)(struct halmac_adapter *adapter, u32 offset);
	enum halmac_ret_status
	(*halmac_reg_write_32)(struct halmac_adapter *adapter, u32 offset,
			       u32 value);
	u32
	(*halmac_reg_read_indirect_32)(struct halmac_adapter *adapter,
				       u32 offset);
	enum halmac_ret_status
	(*halmac_reg_sdio_cmd53_read_n)(struct halmac_adapter *adapter,
					u32 offset, u32 size, u8 *value);
	enum halmac_ret_status
	(*halmac_tx_allowed_sdio)(struct halmac_adapter *adapter, u8 *buf,
				  u32 size);
	enum halmac_ret_status
	(*halmac_set_bulkout_num)(struct halmac_adapter *adapter, u8 num);
	enum halmac_ret_status
	(*halmac_get_sdio_tx_addr)(struct halmac_adapter *adapter, u8 *buf,
				   u32 size, u32 *cmd53_addr);
	enum halmac_ret_status
	(*halmac_get_usb_bulkout_id)(struct halmac_adapter *adapter, u8 *buf,
				     u32 size, u8 *id);
	enum halmac_ret_status
	(*halmac_fill_txdesc_checksum)(struct halmac_adapter *adapter,
				       u8 *txdesc);
	enum halmac_ret_status
	(*halmac_update_datapack)(struct halmac_adapter *adapter,
				  enum halmac_data_type data_type,
				  struct halmac_phy_parameter_info *info);
	enum halmac_ret_status
	(*halmac_run_datapack)(struct halmac_adapter *adapter,
			       enum halmac_data_type data_type);
	enum halmac_ret_status
	(*halmac_cfg_drv_info)(struct halmac_adapter *adapter,
			       enum halmac_drv_info drv_info);
	enum halmac_ret_status
	(*halmac_send_bt_coex)(struct halmac_adapter *adapter, u8 *buf,
			       u32 size, u8 ack);
	enum halmac_ret_status
	(*halmac_verify_platform_api)(struct halmac_adapter *adapter);
	u32
	(*halmac_get_fifo_size)(struct halmac_adapter *adapter,
				enum hal_fifo_sel sel);
	enum halmac_ret_status
	(*halmac_dump_fifo)(struct halmac_adapter *adapter,
			    enum hal_fifo_sel sel, u32 start_addr, u32 size,
			    u8 *data);
	enum halmac_ret_status
	(*halmac_cfg_txbf)(struct halmac_adapter *adapter, u8 userid,
			   enum halmac_bw bw, u8 txbf_en);
	enum halmac_ret_status
	(*halmac_cfg_mumimo)(struct halmac_adapter *adapter,
			     struct halmac_cfg_mumimo_para *param);
	enum halmac_ret_status
	(*halmac_cfg_sounding)(struct halmac_adapter *adapter,
			       enum halmac_snd_role role,
			       enum halmac_data_rate rate);
	enum halmac_ret_status
	(*halmac_del_sounding)(struct halmac_adapter *adapter,
			       enum halmac_snd_role role);
	enum halmac_ret_status
	(*halmac_su_bfer_entry_init)(struct halmac_adapter *adapter,
				     struct halmac_su_bfer_init_para *param);
	enum halmac_ret_status
	(*halmac_su_bfee_entry_init)(struct halmac_adapter *adapter, u8 userid,
				     u16 paid);
	enum halmac_ret_status
	(*halmac_mu_bfer_entry_init)(struct halmac_adapter *adapter,
				     struct halmac_mu_bfer_init_para *param);
	enum halmac_ret_status
	(*halmac_mu_bfee_entry_init)(struct halmac_adapter *adapter,
				     struct halmac_mu_bfee_init_para *param);
	enum halmac_ret_status
	(*halmac_su_bfer_entry_del)(struct halmac_adapter *adapter, u8 userid);
	enum halmac_ret_status
	(*halmac_su_bfee_entry_del)(struct halmac_adapter *adapter, u8 userid);
	enum halmac_ret_status
	(*halmac_mu_bfer_entry_del)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_mu_bfee_entry_del)(struct halmac_adapter *adapter, u8 userid);
	enum halmac_ret_status
	(*halmac_add_ch_info)(struct halmac_adapter *adapter,
			      struct halmac_ch_info *info);
	enum halmac_ret_status
	(*halmac_add_extra_ch_info)(struct halmac_adapter *adapter,
				    struct halmac_ch_extra_info *info);
	enum halmac_ret_status
	(*halmac_ctrl_ch_switch)(struct halmac_adapter *adapter,
				 struct halmac_ch_switch_option *opt);
	enum halmac_ret_status
	(*halmac_send_scan_packet)(struct halmac_adapter *adapter, u8 index,
				   u8 *pkt, u32 size);
	enum halmac_ret_status
	(*halmac_drop_scan_packet)(struct halmac_adapter *adapter,
				   struct halmac_drop_pkt_option *opt);
	enum halmac_ret_status
	(*halmac_p2pps)(struct halmac_adapter *adapter,
			struct halmac_p2pps *info);
	enum halmac_ret_status
	(*halmac_clear_ch_info)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_send_general_info)(struct halmac_adapter *adapter,
				    struct halmac_general_info *info);
	enum halmac_ret_status
	(*halmac_start_iqk)(struct halmac_adapter *adapter,
			    struct halmac_iqk_para *param);
	enum halmac_ret_status
	(*halmac_ctrl_pwr_tracking)(struct halmac_adapter *adapter,
				    struct halmac_pwr_tracking_option *opt);
	enum halmac_ret_status
	(*halmac_psd)(struct halmac_adapter *adapter, u16 start_psd,
		      u16 end_psd);
	enum halmac_ret_status
	(*halmac_cfg_tx_agg_align)(struct halmac_adapter *adapter, u8 enable,
				   u16 align_size);
	enum halmac_ret_status
	(*halmac_query_status)(struct halmac_adapter *adapter,
			       enum halmac_feature_id feature_id,
			       enum halmac_cmd_process_status *proc_status,
			       u8 *data, u32 *size);
	enum halmac_ret_status
	(*halmac_reset_feature)(struct halmac_adapter *adapter,
				enum halmac_feature_id feature_id);
	enum halmac_ret_status
	(*halmac_check_fw_status)(struct halmac_adapter *adapter,
				  u8 *fw_status);
	enum halmac_ret_status
	(*halmac_dump_fw_dmem)(struct halmac_adapter *adapter, u8 *dmem,
			       u32 *size);
	enum halmac_ret_status
	(*halmac_cfg_max_dl_size)(struct halmac_adapter *adapter, u32 size);
	enum halmac_ret_status
	(*halmac_cfg_la_mode)(struct halmac_adapter *adapter,
			      enum halmac_la_mode mode);
	enum halmac_ret_status
	(*halmac_cfg_rxff_expand_mode)(struct halmac_adapter *adapter,
				       enum halmac_rx_fifo_expanding_mode mode);
	enum halmac_ret_status
	(*halmac_config_security)(struct halmac_adapter *adapter,
				  struct halmac_security_setting *setting);
	u8
	(*halmac_get_used_cam_entry_num)(struct halmac_adapter *adapter,
					 enum hal_security_type sec_type);
	enum halmac_ret_status
	(*halmac_write_cam)(struct halmac_adapter *adapter, u32 idx,
			    struct halmac_cam_entry_info *info);
	enum halmac_ret_status
	(*halmac_read_cam_entry)(struct halmac_adapter *adapter, u32 idx,
				 struct halmac_cam_entry_format *content);
	enum halmac_ret_status
	(*halmac_clear_cam_entry)(struct halmac_adapter *adapter, u32 idx);
	enum halmac_ret_status
	(*halmac_get_hw_value)(struct halmac_adapter *adapter,
			       enum halmac_hw_id hw_id, void *value);
	enum halmac_ret_status
	(*halmac_set_hw_value)(struct halmac_adapter *adapter,
			       enum halmac_hw_id hw_id, void *value);
	enum halmac_ret_status
	(*halmac_cfg_drv_rsvd_pg_num)(struct halmac_adapter *adapter,
				      enum halmac_drv_rsvd_pg_num pg_num);
	enum halmac_ret_status
	(*halmac_get_chip_version)(struct halmac_adapter *adapter,
				   struct halmac_ver *ver);
	enum halmac_ret_status
	(*halmac_chk_txdesc)(struct halmac_adapter *adapter, u8 *buf, u32 size);
	enum halmac_ret_status
	(*halmac_dl_drv_rsvd_page)(struct halmac_adapter *adapter, u8 pg_offset,
				   u8 *buf, u32 size);
	enum halmac_ret_status
	(*halmac_pcie_switch)(struct halmac_adapter *adapter,
			      enum halmac_pcie_cfg cfg);
	enum halmac_ret_status
	(*halmac_phy_cfg)(struct halmac_adapter *adapter,
			  enum halmac_intf_phy_platform pltfm);
	enum halmac_ret_status
	(*halmac_cfg_csi_rate)(struct halmac_adapter *adapter, u8 rssi,
			       u8 cur_rate, u8 fixrate_en, u8 *new_rate,
			       u8 *bmp_ofdm54);
#if HALMAC_SDIO_SUPPORT
	enum halmac_ret_status
	(*halmac_sdio_cmd53_4byte)(struct halmac_adapter *adapter,
				   enum halmac_sdio_cmd53_4byte_mode mode);
	enum halmac_ret_status
	(*halmac_sdio_hw_info)(struct halmac_adapter *adapter,
			       struct halmac_sdio_hw_info *info);
#endif
	enum halmac_ret_status
	(*halmac_cfg_transmitter_addr)(struct halmac_adapter *adapter, u8 port,
				       union halmac_wlan_addr *addr);
	enum halmac_ret_status
	(*halmac_cfg_net_type)(struct halmac_adapter *adapter, u8 port,
			       enum halmac_network_type_select net_type);
	enum halmac_ret_status
	(*halmac_cfg_tsf_rst)(struct halmac_adapter *adapter, u8 port);
	enum halmac_ret_status
	(*halmac_cfg_bcn_space)(struct halmac_adapter *adapter, u8 port,
				u32 bcn_space);
	enum halmac_ret_status
	(*halmac_rw_bcn_ctrl)(struct halmac_adapter *adapter, u8 port,
			      u8 write_en, struct halmac_bcn_ctrl *ctrl);
	enum halmac_ret_status
	(*halmac_interface_integration_tuning)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_txfifo_is_empty)(struct halmac_adapter *adapter, u32 chk_num);
	enum halmac_ret_status
	(*halmac_download_flash)(struct halmac_adapter *adapter, u8 *fw_bin,
				 u32 size, u32 rom_addr);
	enum halmac_ret_status
	(*halmac_read_flash)(struct halmac_adapter *adapter, u32 addr,
			     u32 length, u8 *data);
	enum halmac_ret_status
	(*halmac_erase_flash)(struct halmac_adapter *adapter, u8 erase_cmd,
			      u32 addr);
	enum halmac_ret_status
	(*halmac_check_flash)(struct halmac_adapter *adapter, u8 *fw_bin,
			      u32 size, u32 addr);
	enum halmac_ret_status
	(*halmac_cfg_edca_para)(struct halmac_adapter *adapter,
				enum halmac_acq_id acq_id,
				struct halmac_edca_para *param);
	enum halmac_ret_status
	(*halmac_pinmux_get_func)(struct halmac_adapter *adapter,
				  enum halmac_gpio_func gpio_func, u8 *enable);
	enum halmac_ret_status
	(*halmac_pinmux_set_func)(struct halmac_adapter *adapter,
				  enum halmac_gpio_func gpio_func);
	enum halmac_ret_status
	(*halmac_pinmux_free_func)(struct halmac_adapter *adapter,
				   enum halmac_gpio_func gpio_func);
	enum halmac_ret_status
	(*halmac_pinmux_wl_led_mode)(struct halmac_adapter *adapter,
				     enum halmac_wlled_mode mode);
	void
	(*halmac_pinmux_wl_led_sw_ctrl)(struct halmac_adapter *adapter, u8 on);
	void
	(*halmac_pinmux_sdio_int_polarity)(struct halmac_adapter *adapter,
					   u8 low_active);
	enum halmac_ret_status
	(*halmac_pinmux_gpio_mode)(struct halmac_adapter *adapter, u8 gpio_id,
				   u8 output);
	enum halmac_ret_status
	(*halmac_pinmux_gpio_output)(struct halmac_adapter *adapter, u8 gpio_id,
				     u8 high);
	enum halmac_ret_status
	(*halmac_pinmux_pin_status)(struct halmac_adapter *adapter, u8 pin_id,
				    u8 *high);
	enum halmac_ret_status
	(*halmac_ofld_func_cfg)(struct halmac_adapter *adapter,
				struct halmac_ofld_func_info *info);
	enum halmac_ret_status
	(*halmac_rx_cut_amsdu_cfg)(struct halmac_adapter *adapter,
				   struct halmac_cut_amsdu_cfg *cfg);
	enum halmac_ret_status
	(*halmac_fw_snding)(struct halmac_adapter *adapter,
			    struct halmac_su_snding_info *su_info,
			    struct halmac_mu_snding_info *mu_info, u8 period);
	enum halmac_ret_status
	(*halmac_get_mac_addr)(struct halmac_adapter *adapter, u8 port,
			       union halmac_wlan_addr *addr);
	enum halmac_ret_status
	(*halmac_init_low_pwr)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_enter_cpu_sleep_mode)(struct halmac_adapter *adapter);
	enum halmac_ret_status
	(*halmac_get_cpu_mode)(struct halmac_adapter *adapter,
			       enum halmac_wlcpu_mode *mode);
	enum halmac_ret_status
	(*halmac_drv_fwctrl)(struct halmac_adapter *adapter, u8 *payload,
			     u32 size, u8 ack);
	enum halmac_ret_status
	(*halmac_read_efuse)(struct halmac_adapter *adapter, u32 offset,
			     u8 *value);
	enum halmac_ret_status
	(*halmac_write_efuse)(struct halmac_adapter *adapter, u32 offset,
			      u8 value);
	enum halmac_ret_status
	(*halmac_write_wifi_phy_efuse)(struct halmac_adapter *adapter,
				       u32 offset, u8 value);
	enum halmac_ret_status
	(*halmac_read_wifi_phy_efuse)(struct halmac_adapter *adapter,
				      u32 offset, u32 size, u8 *value);
#if HALMAC_PCIE_SUPPORT
	enum halmac_ret_status
	(*halmac_cfgspc_set_pcie)(struct halmac_adapter *adapter,
				  struct halmac_pcie_cfgspc_param *param);
#endif
	enum halmac_ret_status
	(*halmac_en_ref_autok_pcie)(struct halmac_adapter *adapter, u8 en);
	enum halmac_ret_status
	(*halmac_get_watcher)(struct halmac_adapter *adapter,
			      enum halmac_watcher_sel sel, void *vlue);
	enum halmac_ret_status
	(*halmac_cfg_rf_pinmux)(struct halmac_adapter *adapter,
				u8 value);
#if HALMAC_DBG_MONITOR_IO
	u8
	(*halmac_mon_reg_read_8)(struct halmac_adapter *adapter, u32 offset,
				 const char *func, const u32 line);
	u16
	(*halmac_mon_reg_read_16)(struct halmac_adapter *adapter, u32 offset,
				  const char *func, const u32 line);
	u32
	(*halmac_mon_reg_read_32)(struct halmac_adapter *adapter, u32 offset,
				  const char *func, const u32 line);
	enum halmac_ret_status
	(*halmac_mon_reg_sdio_cmd53_read_n)(struct halmac_adapter *adapter,
					    u32 offset, u32 size, u8 *value,
					    const char *func, const u32 line);
	enum halmac_ret_status
	(*halmac_mon_reg_write_8)(struct halmac_adapter *adapter, u32 offset,
				  u8 value, const char *func, const u32 line);
	enum halmac_ret_status
	(*halmac_mon_reg_write_16)(struct halmac_adapter *adapter, u32 offset,
				   u16 value, const char *func, const u32 line);
	enum halmac_ret_status
	(*halmac_mon_reg_write_32)(struct halmac_adapter *adapter, u32 offset,
				   u32 value, const char *func, const u32 line);
#endif
#if HALMAC_PLATFORM_TESTPROGRAM
	struct halmisc_api *halmisc_api;
#endif
};

#define HALMAC_GET_API(halmac_adapter)                                         \
	((struct halmac_api *)halmac_adapter->halmac_api)

static HALMAC_INLINE enum halmac_ret_status
halmac_fw_validate(struct halmac_adapter *adapter)
{
	if (adapter->halmac_state.dlfw_state != HALMAC_DLFW_DONE &&
	    adapter->halmac_state.dlfw_state != HALMAC_GEN_INFO_SENT)
		return HALMAC_RET_NO_DLFW;

	return HALMAC_RET_SUCCESS;
}

#endif
