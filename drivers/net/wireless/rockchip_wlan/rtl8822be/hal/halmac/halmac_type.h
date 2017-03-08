#ifndef _HALMAC_TYPE_H_
#define _HALMAC_TYPE_H_

#include "halmac_2_platform.h"
#include "halmac_hw_cfg.h"
#include "halmac_fw_info.h"


#define IN
#define OUT
#define INOUT
#define VOID void

#define HALMAC_SCAN_CH_NUM_MAX                  28
#define HALMAC_BCN_IE_BMP_SIZE                  24 /* ID0~ID191, 192/8=24 */
#define HALMAC_PHY_PARAMETER_SIZE               12
#define HALMAC_PHY_PARAMETER_MAX_NUM            128
#define HALMAC_MAX_SSID_LEN                     32
#define HALMAC_SUPPORT_NLO_NUM                  16
#define HALMAC_SUPPORT_PROBE_REQ_NUM			8
#define HALMC_DDMA_POLLING_COUNT                1000
#define API_ARRAY_SIZE							32

/* platform api */
#define PLATFORM_SDIO_CMD52_READ				pHalmac_adapter->pHalmac_platform_api->SDIO_CMD52_READ
#define PLATFORM_SDIO_CMD53_READ_8              pHalmac_adapter->pHalmac_platform_api->SDIO_CMD53_READ_8
#define PLATFORM_SDIO_CMD53_READ_16             pHalmac_adapter->pHalmac_platform_api->SDIO_CMD53_READ_16
#define PLATFORM_SDIO_CMD53_READ_32             pHalmac_adapter->pHalmac_platform_api->SDIO_CMD53_READ_32
#define PLATFORM_SDIO_CMD52_WRITE               pHalmac_adapter->pHalmac_platform_api->SDIO_CMD52_WRITE
#define PLATFORM_SDIO_CMD53_WRITE_8             pHalmac_adapter->pHalmac_platform_api->SDIO_CMD53_WRITE_8
#define PLATFORM_SDIO_CMD53_WRITE_16			pHalmac_adapter->pHalmac_platform_api->SDIO_CMD53_WRITE_16
#define PLATFORM_SDIO_CMD53_WRITE_32			pHalmac_adapter->pHalmac_platform_api->SDIO_CMD53_WRITE_32

#define PLATFORM_REG_READ_8                     pHalmac_adapter->pHalmac_platform_api->REG_READ_8
#define PLATFORM_REG_READ_16                    pHalmac_adapter->pHalmac_platform_api->REG_READ_16
#define PLATFORM_REG_READ_32                    pHalmac_adapter->pHalmac_platform_api->REG_READ_32
#define PLATFORM_REG_WRITE_8                    pHalmac_adapter->pHalmac_platform_api->REG_WRITE_8
#define PLATFORM_REG_WRITE_16                   pHalmac_adapter->pHalmac_platform_api->REG_WRITE_16
#define PLATFORM_REG_WRITE_32                   pHalmac_adapter->pHalmac_platform_api->REG_WRITE_32

#define PLATFORM_SEND_RSVD_PAGE                 pHalmac_adapter->pHalmac_platform_api->SEND_RSVD_PAGE
#define PLATFORM_SEND_H2C_PKT                   pHalmac_adapter->pHalmac_platform_api->SEND_H2C_PKT

#define PLATFORM_RTL_FREE                       pHalmac_adapter->pHalmac_platform_api->RTL_FREE
#define PLATFORM_RTL_MALLOC                     pHalmac_adapter->pHalmac_platform_api->RTL_MALLOC
#define PLATFORM_RTL_MEMCPY                     pHalmac_adapter->pHalmac_platform_api->RTL_MEMCPY
#define PLATFORM_RTL_MEMSET                     pHalmac_adapter->pHalmac_platform_api->RTL_MEMSET
#define PLATFORM_RTL_DELAY_US                   pHalmac_adapter->pHalmac_platform_api->RTL_DELAY_US

#define PLATFORM_MUTEX_INIT                     pHalmac_adapter->pHalmac_platform_api->MUTEX_INIT
#define PLATFORM_MUTEX_DEINIT                   pHalmac_adapter->pHalmac_platform_api->MUTEX_DEINIT
#define PLATFORM_MUTEX_LOCK                     pHalmac_adapter->pHalmac_platform_api->MUTEX_LOCK
#define PLATFORM_MUTEX_UNLOCK                   pHalmac_adapter->pHalmac_platform_api->MUTEX_UNLOCK

#define PLATFORM_EVENT_INDICATION               pHalmac_adapter->pHalmac_platform_api->EVENT_INDICATION


#if HALMAC_DBG_MSG_ENABLE
#define PLATFORM_MSG_PRINT                      pHalmac_adapter->pHalmac_platform_api->MSG_PRINT
#else
#define PLATFORM_MSG_PRINT(pDriver_adapter, msg_type, msg_level, fmt, ...)
#endif

#if HALMAC_PLATFORM_TESTPROGRAM
#define PLATFORM_WRITE_DATA_SDIO_ADDR            pHalmac_adapter->pHalmac_platform_api->WRITE_DATA_SDIO_ADDR
#define PLATFORM_WRITE_DATA_USB_BULKOUT_ID       pHalmac_adapter->pHalmac_platform_api->WRITE_DATA_USB_BULKOUT_ID
#define PLATFORM_WRITE_DATA_PCIE_QUEUE           pHalmac_adapter->pHalmac_platform_api->WRITE_DATA_PCIE_QUEUE
#define PLATFORM_READ_DATA                       pHalmac_adapter->pHalmac_platform_api->READ_DATA
#endif

#define HALMAC_REG_READ_8                        pHalmac_api->halmac_reg_read_8
#define HALMAC_REG_READ_16                       pHalmac_api->halmac_reg_read_16
#define HALMAC_REG_READ_32                       pHalmac_api->halmac_reg_read_32
#define HALMAC_REG_WRITE_8                       pHalmac_api->halmac_reg_write_8
#define HALMAC_REG_WRITE_16                      pHalmac_api->halmac_reg_write_16
#define HALMAC_REG_WRITE_32                      pHalmac_api->halmac_reg_write_32

/* Swap Little-endian <-> Big-endia*/
#define SWAP32(x) ((u32)( \
			   (((u32)(x) & (u32)0x000000ff) << 24) | \
			   (((u32)(x) & (u32)0x0000ff00) << 8) | \
			   (((u32)(x) & (u32)0x00ff0000) >> 8) | \
			   (((u32)(x) & (u32)0xff000000) >> 24)))

#define SWAP16(x) ((u16)( \
			   (((u16)(x) & (u16)0x00ff) << 8) | \
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
#ifndef EF1Byte
#define EF1Byte (u8)
#endif

#ifndef EF2Byte
#define EF2Byte rtk_le16_to_cpu
#endif

#ifndef EF4Byte
#define EF4Byte rtk_le32_to_cpu
#endif

/* Example:
 * BIT_LEN_MASK_32(0) => 0x00000000
 * BIT_LEN_MASK_32(1) => 0x00000001
 * BIT_LEN_MASK_32(2) => 0x00000003
 * BIT_LEN_MASK_32(32) => 0xFFFFFFFF
 */
#ifndef BIT_LEN_MASK_32
#define BIT_LEN_MASK_32(__BitLen) \
	(0xFFFFFFFF >> (32 - (__BitLen)))
#endif

/* Example:
 * BIT_OFFSET_LEN_MASK_32(0, 2) => 0x00000003
 * BIT_OFFSET_LEN_MASK_32(16, 2) => 0x00030000
 */
#ifndef BIT_OFFSET_LEN_MASK_32
#define BIT_OFFSET_LEN_MASK_32(__BitOffset, __BitLen) \
	(BIT_LEN_MASK_32(__BitLen) << (__BitOffset))
#endif

/* Return 4-byte value in host byte ordering from
 * 4-byte pointer in litten-endian system
 */
#ifndef LE_P4BYTE_TO_HOST_4BYTE
#define LE_P4BYTE_TO_HOST_4BYTE(__pStart) \
	(EF4Byte(*((u32 *)(__pStart))))
#endif


/* Translate subfield (continuous bits in little-endian) of
 * 4-byte value in litten byte to 4-byte value in host byte ordering
 */
#ifndef LE_BITS_TO_4BYTE
#define LE_BITS_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
	( \
		(LE_P4BYTE_TO_HOST_4BYTE(__pStart) >> (__BitOffset)) \
		& \
		BIT_LEN_MASK_32(__BitLen) \
	)
#endif

/* Mask subfield (continuous bits in little-endian) of 4-byte
 * value in litten byte oredering and return the result in 4-byte
 * value in host byte ordering
 */
#ifndef LE_BITS_CLEARED_TO_4BYTE
#define LE_BITS_CLEARED_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
	( \
		LE_P4BYTE_TO_HOST_4BYTE(__pStart) \
		& \
		(~BIT_OFFSET_LEN_MASK_32(__BitOffset, __BitLen)) \
	)
#endif

/* Set subfield of little-endian 4-byte value to specified value */
#ifndef SET_BITS_TO_LE_4BYTE
#define SET_BITS_TO_LE_4BYTE(__pStart, __BitOffset, __BitLen, __Value) \
	do { \
		*((u32 *)(__pStart)) = \
			EF4Byte( \
				LE_BITS_CLEARED_TO_4BYTE(__pStart, __BitOffset, __BitLen) \
				| \
				((((u32)__Value) & BIT_LEN_MASK_32(__BitLen)) << (__BitOffset)) \
			); \
	} while (0)
#endif

#ifndef HALMAC_BIT_OFFSET_VAL_MASK_32
#define HALMAC_BIT_OFFSET_VAL_MASK_32(__BitVal, __BitOffset) \
	(__BitVal << (__BitOffset))
#endif

#ifndef SET_MEM_OP
#define SET_MEM_OP(Dw, Value32, Mask, Shift) \
	(((Dw) & ~((Mask) << (Shift))) | (((Value32) & (Mask)) << (Shift)))
#endif

#ifndef HALMAC_SET_DESC_FIELD_CLR
#define HALMAC_SET_DESC_FIELD_CLR(Dw, Value32, Mask, Shift) \
	(Dw = (rtk_cpu_to_le32(SET_MEM_OP(rtk_cpu_to_le32(Dw), Value32, Mask, Shift))))
#endif

#ifndef HALMAC_SET_DESC_FIELD_NO_CLR
#define HALMAC_SET_DESC_FIELD_NO_CLR(Dw, Value32, Mask, Shift) \
	(Dw |= (rtk_cpu_to_le32(((Value32) & (Mask)) << (Shift))))
#endif

#ifndef HALMAC_GET_DESC_FIELD
#define HALMAC_GET_DESC_FIELD(Dw, Mask, Shift) \
	((rtk_le32_to_cpu(Dw) >> (Shift)) & (Mask))
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

/* HALMAC API return status*/
typedef enum _HALMAC_RET_STATUS {
	HALMAC_RET_SUCCESS = 0x00,
	HALMAC_RET_SUCCESS_ENQUEUE = 0x01,
	HALMAC_RET_PLATFORM_API_NULL = 0x02,
	HALMAC_RET_EFUSE_SIZE_INCORRECT = 0x03,
	HALMAC_RET_MALLOC_FAIL = 0x04,
	HALMAC_RET_ADAPTER_INVALID = 0x05,
	HALMAC_RET_ITF_INCORRECT = 0x06,
	HALMAC_RET_DLFW_FAIL = 0x07,
	HALMAC_RET_PORT_NOT_SUPPORT = 0x08,
	HALMAC_RET_TRXMODE_NOT_SUPPORT = 0x09,
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
	HALMAC_RET_NOT_SUPPORT = 0x4B,
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
} HALMAC_RET_STATUS;

typedef enum _HALMAC_MAC_CLOCK_HW_DEF {
	HALMAC_MAC_CLOCK_HW_DEF_80M = 0,
	HALMAC_MAC_CLOCK_HW_DEF_40M = 1,
	HALMAC_MAC_CLOCK_HW_DEF_20M = 2,
} HALMAC_MAC_CLOCK_HW_DEF;

/* Rx aggregation parameters */
typedef enum _HALMAC_NORMAL_RXAGG_TH_TO {
	HALMAC_NORMAL_RXAGG_THRESHOLD = 0xFF,
	HALMAC_NORMAL_RXAGG_TIMEOUT = 0x01,
} HALMAC_NORMAL_RXAGG_TH_TO;

typedef enum _HALMAC_LOOPBACK_RXAGG_TH_TO {
	HALMAC_LOOPBACK_RXAGG_THRESHOLD = 0xFF,
	HALMAC_LOOPBACK_RXAGG_TIMEOUT = 0x01,
} HALMAC_LOOPBACK_RXAGG_TH_TO;

/* Chip ID*/
typedef enum _HALMAC_CHIP_ID {
	HALMAC_CHIP_ID_8822B = 0,
	HALMAC_CHIP_ID_8821C = 1,
	HALMAC_CHIP_ID_8824B = 2,
	HALMAC_CHIP_ID_8197F = 3,
	HALMAC_CHIP_ID_UNDEFINE = 0x7F,
} HALMAC_CHIP_ID;

typedef enum _HALMAC_CHIP_ID_HW_DEF {
	HALMAC_CHIP_ID_HW_DEF_8723A = 0x01,
	HALMAC_CHIP_ID_HW_DEF_8188E = 0x02,
	HALMAC_CHIP_ID_HW_DEF_8881A = 0x03,
	HALMAC_CHIP_ID_HW_DEF_8812A = 0x04,
	HALMAC_CHIP_ID_HW_DEF_8821A = 0x05,
	HALMAC_CHIP_ID_HW_DEF_8723B = 0x06,
	HALMAC_CHIP_ID_HW_DEF_8192E = 0x07,
	HALMAC_CHIP_ID_HW_DEF_8814A = 0x08,
	HALMAC_CHIP_ID_HW_DEF_8821C = 0x09,
	HALMAC_CHIP_ID_HW_DEF_8822B = 0x0A,
	HALMAC_CHIP_ID_HW_DEF_8703B = 0x0B,
	HALMAC_CHIP_ID_HW_DEF_8188F = 0x0C,
	HALMAC_CHIP_ID_HW_DEF_8192F = 0x0D,
	HALMAC_CHIP_ID_HW_DEF_8197F = 0x0E,
	HALMAC_CHIP_ID_HW_DEF_8723D = 0x0F,
	HALMAC_CHIP_ID_HW_DEF_UNDEFINE = 0x7F,
	HALMAC_CHIP_ID_HW_DEF_PS = 0xEA,
} HALMAC_CHIP_ID_HW_DEF;

/* Chip Version*/
typedef enum _HALMAC_CHIP_VER {
	HALMAC_CHIP_VER_A_CUT = 0x00,
	HALMAC_CHIP_VER_B_CUT = 0x01,
	HALMAC_CHIP_VER_C_CUT = 0x02,
	HALMAC_CHIP_VER_D_CUT = 0x03,
	HALMAC_CHIP_VER_E_CUT = 0x04,
	HALMAC_CHIP_VER_F_CUT = 0x05,
	HALMAC_CHIP_VER_TEST = 0xFF,
	HALMAC_CHIP_VER_UNDEFINE = 0x7FFF,
} HALMAC_CHIP_VER;

/* Network type select */
typedef enum _HALMAC_NETWORK_TYPE_SELECT {
	HALMAC_NETWORK_NO_LINK = 0,
	HALMAC_NETWORK_ADHOC = 1,
	HALMAC_NETWORK_INFRASTRUCTURE = 2,
	HALMAC_NETWORK_AP = 3,
	HALMAC_NETWORK_UNDEFINE = 0x7F,
} HALMAC_NETWORK_TYPE_SELECT;

/* Transfer mode select */
typedef enum _HALMAC_TRNSFER_MODE_SELECT {
	HALMAC_TRNSFER_NORMAL = 0x0,
	HALMAC_TRNSFER_LOOPBACK_DIRECT = 0xB,
	HALMAC_TRNSFER_LOOPBACK_DELAY = 0x3,
	HALMAC_TRNSFER_UNDEFINE = 0x7F,
} HALMAC_TRNSFER_MODE_SELECT;

/* Queue select */
typedef enum _HALMAC_DMA_MAPPING {
	HALMAC_DMA_MAPPING_EXTRA = 0,
	HALMAC_DMA_MAPPING_LOW = 1,
	HALMAC_DMA_MAPPING_NORMAL = 2,
	HALMAC_DMA_MAPPING_HIGH = 3,
	HALMAC_DMA_MAPPING_UNDEFINE = 0x7F,
} HALMAC_DMA_MAPPING;

/* TXDESC queue select TID */
typedef enum _HALMAC_TXDESC_QUEUE_TID {
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

	HALMAC_TXDESC_QSEL_UNDEFINE = 0x7F,
} HALMAC_TXDESC_QUEUE_TID;

typedef enum _HALMAC_PTCL_QUEUE {
	HALMAC_PTCL_QUEUE_VO = 0x0,
	HALMAC_PTCL_QUEUE_VI = 0x1,
	HALMAC_PTCL_QUEUE_BE = 0x2,
	HALMAC_PTCL_QUEUE_BK = 0x3,
	HALMAC_PTCL_QUEUE_MG = 0x4,
	HALMAC_PTCL_QUEUE_HI = 0x5,
	HALMAC_PTCL_QUEUE_NUM = 0x6,
	HALMAC_PTCL_QUEUE_UNDEFINE = 0x7F,
} HALMAC_PTCL_QUEUE;

typedef enum {
	HALMAC_QUEUE_SELECT_VO = HALMAC_TXDESC_QSEL_TID6,
	HALMAC_QUEUE_SELECT_VI = HALMAC_TXDESC_QSEL_TID4,
	HALMAC_QUEUE_SELECT_BE = HALMAC_TXDESC_QSEL_TID0,
	HALMAC_QUEUE_SELECT_BK = HALMAC_TXDESC_QSEL_TID1,
	HALMAC_QUEUE_SELECT_VO_V2 = HALMAC_TXDESC_QSEL_TID7,
	HALMAC_QUEUE_SELECT_VI_V2 = HALMAC_TXDESC_QSEL_TID5,
	HALMAC_QUEUE_SELECT_BE_V2 = HALMAC_TXDESC_QSEL_TID3,
	HALMAC_QUEUE_SELECT_BK_V2 = HALMAC_TXDESC_QSEL_TID2,
	HALMAC_QUEUE_SELECT_BCN = HALMAC_TXDESC_QSEL_BEACON,
	HALMAC_QUEUE_SELECT_HIGH = HALMAC_TXDESC_QSEL_HIGH,
	HALMAC_QUEUE_SELECT_MGNT = HALMAC_TXDESC_QSEL_MGT,
	HALMAC_QUEUE_SELECT_CMD = HALMAC_TXDESC_QSEL_H2C_CMD,
	HALMAC_QUEUE_SELECT_UNDEFINE = 0x7F,
} HALMAC_QUEUE_SELECT;


/* USB burst size */
typedef enum _HALMAC_USB_BURST_SIZE {
	HALMAC_USB_BURST_SIZE_3_0 = 0x0,
	HALMAC_USB_BURST_SIZE_2_0_HSPEED = 0x1,
	HALMAC_USB_BURST_SIZE_2_0_FSPEED = 0x2,
	HALMAC_USB_BURST_SIZE_2_0_OTHERS = 0x3,
	HALMAC_USB_BURST_SIZE_UNDEFINE = 0x7F,
} HALMAC_USB_BURST_SIZE;

/* HAL API  function parameters*/
typedef enum _HALMAC_INTERFACE {
	HALMAC_INTERFACE_PCIE = 0x0,
	HALMAC_INTERFACE_USB = 0x1,
	HALMAC_INTERFACE_SDIO = 0x2,
	HALMAC_INTERFACE_UNDEFINE = 0x7F,
} HALMAC_INTERFACE;

typedef enum _HALMAC_RX_AGG_MODE {
	HALMAC_RX_AGG_MODE_NONE = 0x0,
	HALMAC_RX_AGG_MODE_DMA = 0x1,
	HALMAC_RX_AGG_MODE_USB = 0x2,
	HALMAC_RX_AGG_MODE_UNDEFINE = 0x7F,
} HALMAC_RX_AGG_MODE;
typedef struct _HALMAC_RXAGG_TH {
	u8 drv_define;
	u8 timeout;
	u8 size;
} HALMAC_RXAGG_TH, *PHALMAC_RXAGG_TH;

typedef struct _HALMAC_RXAGG_CFG {
	HALMAC_RX_AGG_MODE mode;
	HALMAC_RXAGG_TH threshold;
} HALMAC_RXAGG_CFG, *PHALMAC_RXAGG_CFG;


typedef enum _HALMAC_MAC_POWER {
	HALMAC_MAC_POWER_OFF = 0x0,
	HALMAC_MAC_POWER_ON = 0x1,
	HALMAC_MAC_POWER_UNDEFINE = 0x7F,
} HALMAC_MAC_POWER;

typedef enum _HALMAC_PS_STATE {
	HALMAC_PS_STATE_ACT = 0x0,
	HALMAC_PS_STATE_LPS = 0x1,
	HALMAC_PS_STATE_IPS = 0x2,
	HALMAC_PS_STATE_UNDEFINE = 0x7F,
} HALMAC_PS_STATE;

typedef enum _HALMAC_TRX_MODE {
	HALMAC_TRX_MODE_NORMAL = 0x0,
	HALMAC_TRX_MODE_TRXSHARE = 0x1,
	HALMAC_TRX_MODE_WMM = 0x2,
	HALMAC_TRX_MODE_P2P = 0x3,
	HALMAC_TRX_MODE_LOOPBACK = 0x4,
	HALMAC_TRX_MODE_DELAY_LOOPBACK = 0x5,
	HALMAC_TRX_MODE_UNDEFINE = 0x7F,
} HALMAC_TRX_MODE;

typedef enum _HALMAC_WIRELESS_MODE {
	HALMAC_WIRELESS_MODE_B = 0x0,
	HALMAC_WIRELESS_MODE_G = 0x1,
	HALMAC_WIRELESS_MODE_N = 0x2,
	HALMAC_WIRELESS_MODE_AC = 0x3,
	HALMAC_WIRELESS_MODE_UNDEFINE = 0x7F,
} HALMAC_WIRELESS_MODE;

typedef enum _HALMAC_BW {
	HALMAC_BW_20 = 0x00,
	HALMAC_BW_40 = 0x01,
	HALMAC_BW_80 = 0x02,
	HALMAC_BW_160 = 0x03,
	HALMAC_BW_5 = 0x04,
	HALMAC_BW_10 = 0x05,
	HALMAC_BW_MAX = 0x06,
	HALMAC_BW_UNDEFINE = 0x7F,
} HALMAC_BW;


typedef enum _HALMAC_EFUSE_READ_CFG {
	HALMAC_EFUSE_R_AUTO = 0x00,
	HALMAC_EFUSE_R_DRV = 0x01,
	HALMAC_EFUSE_R_FW = 0x02,
	HALMAC_EFUSE_R_UNDEFINE = 0x7F,
} HALMAC_EFUSE_READ_CFG;


typedef struct _HALMAC_TX_DESC {
	u32	Dword0;
	u32	Dword1;
	u32	Dword2;
	u32	Dword3;
	u32	Dword4;
	u32	Dword5;
	u32	Dword6;
	u32	Dword7;
	u32	Dword8;
	u32	Dword9;
	u32	Dword10;
	u32	Dword11;
} HALMAC_TX_DESC, *PHALMAC_TX_DESC;

typedef struct _HALMAC_RX_DESC {
	u32	Dword0;
	u32	Dword1;
	u32	Dword2;
	u32	Dword3;
	u32	Dword4;
	u32	Dword5;
} HALMAC_RX_DESC, *PHALMAC_RX_DESC;

typedef struct _HALMAC_FWLPS_OPTION {
	u8	mode;
	u8	clk_request;
	u8	rlbm;
	u8	smart_ps;
	u8	awake_interval;
	u8	all_queue_uapsd;
	u8	pwr_state;
	u8	low_pwr_rx_beacon;
	u8	ant_auto_switch;
	u8	ps_allow_bt_high_Priority;
	u8	protect_bcn;
	u8	silence_period;
	u8	fast_bt_connect;
	u8	two_antenna_en;
	u8	adopt_user_Setting;
	u8	drv_bcn_early_shift;
	u8	enter_32K;
} HALMAC_FWLPS_OPTION, *PHALMAC_FWLPS_OPTION;

typedef struct _HALMAC_FWIPS_OPTION {
	u8 adopt_user_Setting;
} HALMAC_FWIPS_OPTION, *PHALMAC_FWIPS_OPTION;

typedef struct _HALMAC_WOWLAN_OPTION {
	u8 adopt_user_Setting;
} HALMAC_WOWLAN_OPTION, *PHALMAC_WOWLAN_OPTION;

typedef struct _HALMAC_BCN_IE_INFO {
	u8	func_en;
	u8	size_th;
	u8	timeout;
	u8	ie_bmp[HALMAC_BCN_IE_BMP_SIZE];
} HALMAC_BCN_IE_INFO, *PHALMAC_BCN_IE_INFO;

typedef enum _HALMAC_REG_TYPE {
	HALMAC_REG_TYPE_MAC = 0x0,
	HALMAC_REG_TYPE_BB = 0x1,
	HALMAC_REG_TYPE_RF = 0x2,
	HALMAC_REG_TYPE_UNDEFINE = 0x7F,
} HALMAC_REG_TYPE;

typedef enum _HALMAC_PARAMETER_CMD {
	/* HALMAC_PARAMETER_CMD_LLT				= 0x1, */
	/* HALMAC_PARAMETER_CMD_R_EFUSE			= 0x2, */
	/* HALMAC_PARAMETER_CMD_EFUSE_PATCH	= 0x3, */
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
} HALMAC_PARAMETER_CMD;

typedef union _HALMAC_PARAMETER_CONTENT {
	struct _MAC_REG_W {
		u32	value;
		u32	msk;
		u16	offset;
		u8	msk_en;
	} MAC_REG_W;
	struct _BB_REG_W {
		u32	value;
		u32	msk;
		u16	offset;
		u8	msk_en;
	} BB_REG_W;
	struct _RF_REG_W {
		u32	value;
		u32	msk;
		u8	offset;
		u8	msk_en;
		u8	rf_path;
	} RF_REG_W;
	struct _DELAY_TIME {
		u32	rsvd1;
		u32	rsvd2;
		u16	delay_time;
		u8	rsvd3;
	} DELAY_TIME;
} HALMAC_PARAMETER_CONTENT, *PHALMAC_PARAMETER_CONTENT;

typedef struct _HALMAC_PHY_PARAMETER_INFO {
	HALMAC_PARAMETER_CMD cmd_id;
	HALMAC_PARAMETER_CONTENT content;
} HALMAC_PHY_PARAMETER_INFO, *PHALMAC_PHY_PARAMETER_INFO;

typedef struct _HALMAC_H2C_INFO {
	u16 h2c_seq_num; /* H2C sequence number */
	u8 in_use; /* 0 : empty 1 : used */
	HALMAC_H2C_RETURN_CODE	status;
} HALMAC_H2C_INFO, *PHALMAC_H2C_INFO;

typedef struct _HALMAC_PG_EFUSE_INFO {
	u8 *pEfuse_map;
	u32	efuse_map_size;
	u8 *pEfuse_mask;
	u32 efuse_mask_size;
} HALMAC_PG_EFUSE_INFO, *PHALMAC_PG_EFUSE_INFO;

typedef struct _HALMAC_TXAGG_BUFF_INFO {
	u8 *pTx_agg_buf;
	u8 *pCurr_pkt_buf;
	u32	avai_buf_size;
	u32	total_pkt_size;
	u8 agg_num;
} HALMAC_TXAGG_BUFF_INFO, *PHALMAC_TXAGG_BUFF_INFO;

typedef struct _HALMAC_CONFIG_PARA_INFO {
	u32 para_buf_size; /* Parameter buffer size */
	u8 *pCfg_para_buf; /* Buffer for config parameter */
	u8 *pPara_buf_w; /* Write pointer of the parameter buffer */
	u32 para_num; /* Parameter numbers in parameter buffer */
	u32 avai_para_buf_size; /* Free size of parameter buffer */
	u32 offset_accumulation;
	u32 value_accumulation;
	HALMAC_DATA_TYPE data_type; /*DataType which is passed to FW*/
	u8 datapack_segment; /*DataPack Segment, from segment0...*/
	u8 full_fifo_mode; /* Used full tx fifo to save cfg parameter */
} HALMAC_CONFIG_PARA_INFO, *PHALMAC_CONFIG_PARA_INFO;

typedef struct _HALMAC_HW_CONFIG_INFO {
	u32	efuse_size; /* Record efuse size */
	u32	eeprom_size; /* Record eeprom size */
	u32 bt_efuse_size; /* Record BT efuse size */
	u32	tx_fifo_size; /* Record tx fifo size */
	u32 rx_fifo_size; /* Record rx fifo size */
	u8 txdesc_size; /* Record tx desc size */
	u8 rxdesc_size; /* Record rx desc size */
	u8 cam_entry_num; /* Record CAM entry number */
} HALMAC_HW_CONFIG_INFO, *PHALMAC_HW_CONFIG_INFO;

typedef struct _HALMAC_SDIO_FREE_SPACE {
	u16	high_queue_number; /* Free space of HIQ */
	u16	normal_queue_number; /* Free space of MIDQ */
	u16	low_queue_number; /* Free space of LOWQ */
	u16	public_queue_number; /* Free space of PUBQ */
	u16	extra_queue_number; /* Free space of EXBQ */
	u8 ac_oqt_number;
	u8 non_ac_oqt_number;
} HALMAC_SDIO_FREE_SPACE, *PHALMAC_SDIO_FREE_SPACE;

typedef enum _HAL_FIFO_SEL {
	HAL_FIFO_SEL_TX,
	HAL_FIFO_SEL_RX,
	HAL_FIFO_SEL_RSVD_PAGE,
	HAL_FIFO_SEL_REPORT,
	HAL_FIFO_SEL_LLT,
} HAL_FIFO_SEL;

typedef enum _HALMAC_DRV_INFO {
	HALMAC_DRV_INFO_NONE, /* No information is appended in rx_pkt */
	HALMAC_DRV_INFO_PHY_STATUS, /* PHY status is appended after rx_desc */
	HALMAC_DRV_INFO_PHY_SNIFFER, /* PHY status and sniffer info are appended after rx_desc */
	HALMAC_DRV_INFO_PHY_PLCP, /* PHY status and plcp header are appended after rx_desc */
	HALMAC_DRV_INFO_UNDEFINE,
} HALMAC_DRV_INFO;

typedef struct _HALMAC_BT_COEX_CMD {
	u8 element_id;
	u8 op_code;
	u8 op_code_ver;
	u8 req_num;
	u8 data0;
	u8 data1;
	u8 data2;
	u8 data3;
	u8 data4;
} HALMAC_BT_COEX_CMD, *PHALMAC_BT_COEX_CMD;

typedef enum _HALMAC_PRI_CH_IDX {
	HALMAC_CH_IDX_UNDEFINE = 0,
	HALMAC_CH_IDX_1 = 1,
	HALMAC_CH_IDX_2 = 2,
	HALMAC_CH_IDX_3 = 3,
	HALMAC_CH_IDX_4 = 4,
	HALMAC_CH_IDX_MAX = 5,
} HALMAC_PRI_CH_IDX;

typedef struct _HALMAC_CH_INFO {
	HALMAC_CS_ACTION_ID	action_id;
	HALMAC_BW bw;
	HALMAC_PRI_CH_IDX pri_ch_idx;
	u8 channel;
	u8 timeout;
	u8 extra_info;
} HALMAC_CH_INFO, *PHALMAC_CH_INFO;

typedef struct _HALMAC_CH_EXTRA_INFO {
	u8 extra_info;
	HALMAC_CS_EXTRA_ACTION_ID extra_action_id;
	u8 extra_info_size;
	u8 *extra_info_data;
} HALMAC_CH_EXTRA_INFO, *PHALMAC_CH_EXTRA_INFO;

typedef enum _HALMAC_CS_PERIODIC_OPTION {
	HALMAC_CS_PERIODIC_NONE,
	HALMAC_CS_PERIODIC_NORMAL,
	HALMAC_CS_PERIODIC_2_PHASE,
	HALMAC_CS_PERIODIC_SEAMLESS,
} HALMAC_CS_PERIODIC_OPTION;

typedef struct _HALMAC_CH_SWITCH_OPTION {
	HALMAC_BW dest_bw;
	HALMAC_CS_PERIODIC_OPTION periodic_option;
	HALMAC_PRI_CH_IDX dest_pri_ch_idx;
	/* u32 tsf_high; */
	u32 tsf_low;
	u8 switch_en;
	u8 dest_ch_en;
	u8 absolute_time_en;
	u8 dest_ch;
	u8 normal_period;
	u8 normal_cycle;
	u8 phase_2_period;
} HALMAC_CH_SWITCH_OPTION, *PHALMAC_CH_SWITCH_OPTION;

typedef struct _HALMAC_FW_VERSION {
	u16 version;
	u8 sub_version;
	u8 sub_index;
} HALMAC_FW_VERSION, *PHALMAC_FW_VERSION;

typedef enum _HALMAC_RF_TYPE {
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
} HALMAC_RF_TYPE;

typedef struct _HALMAC_GENERAL_INFO {
	u8 rfe_type;
	HALMAC_RF_TYPE rf_type;
} HALMAC_GENERAL_INFO, *PHALMAC_GENERAL_INFO;

typedef struct _HALMAC_PWR_TRACKING_PARA {
	u8 enable;
	u8 tx_pwr_index;
	u8 pwr_tracking_offset_value;
	u8 tssi_value;
} HALMAC_PWR_TRACKING_PARA, *PHALMAC_PWR_TRACKING_PARA;

typedef struct _HALMAC_PWR_TRACKING_OPTION {
	u8 type;
	u8 bbswing_index;
	HALMAC_PWR_TRACKING_PARA pwr_tracking_para[4]; /* pathA, pathB, pathC, pathD */
} HALMAC_PWR_TRACKING_OPTION, *PHALMAC_PWR_TRACKING_OPTION;

typedef struct _HALMAC_NLO_CFG {
	u8 num_of_ssid;
	u8 num_of_hidden_ap;
	u8 rsvd[2];
	u32	pattern_check;
	u32	rsvd1;
	u32	rsvd2;
	u8 ssid_len[HALMAC_SUPPORT_NLO_NUM];
	u8 ChiperType[HALMAC_SUPPORT_NLO_NUM];
	u8 rsvd3[HALMAC_SUPPORT_NLO_NUM];
	u8 loc_probeReq[HALMAC_SUPPORT_PROBE_REQ_NUM];
	u8 rsvd4[56];
	u8 ssid[HALMAC_SUPPORT_NLO_NUM][HALMAC_MAX_SSID_LEN];
} HALMAC_NLO_CFG, *PHALMAC_NLO_CFG;


typedef enum _HALMAC_DATA_RATE {
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
	HALMAC_VHT_NSS4_MCS9
} HALMAC_DATA_RATE;

typedef enum _HALMAC_RF_PATH {
	HALMAC_RF_PATH_A,
	HALMAC_RF_PATH_B,
	HALMAC_RF_PATH_C,
	HALMAC_RF_PATH_D
} HALMAC_RF_PATH;

typedef enum _HALMAC_SND_PKT_SEL {
	HALMAC_UNI_NDPA,
	HALMAC_BMC_NDPA,
	HALMAC_NON_FINAL_BFRPRPOLL,
	HALMAC_FINAL_BFRPTPOLL,
} HALMAC_SND_PKT_SEL;

#if HALMAC_PLATFORM_TESTPROGRAM

typedef enum _HALMAC_PWR_SEQ_ID {
	HALMAC_PWR_SEQ_ENABLE,
	HALMAC_PWR_SEQ_DISABLE,
	HALMAC_PWR_SEQ_ENTER_LPS,
	HALMAC_PWR_SEQ_ENTER_DEEP_LPS,
	HALMAC_PWR_SEQ_LEAVE_LPS,
	HALMAC_PWR_SEQ_MAX
} HALMAC_PWR_SEQ_ID;

typedef enum _HAL_TX_ID {
	HAL_TX_ID_VO,
	HAL_TX_ID_VI,
	HAL_TX_ID_BE,
	HAL_TX_ID_BK,
	HAL_TX_ID_BCN,
	HAL_TX_ID_H2C,
	HAL_TX_ID_MAX
} HAL_TX_ID;

typedef enum _HAL_QSEL {
	HAL_QSEL_TID0,
	HAL_QSEL_TID1,
	HAL_QSEL_TID2,
	HAL_QSEL_TID3,
	HAL_QSEL_TID4,
	HAL_QSEL_TID5,
	HAL_QSEL_TID6,
	HAL_QSEL_TID7,

	HAL_QSEL_BEACON = 0x10,
	HAL_QSEL_HIGH = 0x11,
	HAL_QSEL_MGT = 0x12,
	HAL_QSEL_CMD = 0x13
} HAL_QSEL;

typedef enum _HAL_RTS_MODE {
	HAL_RTS_MODE_NONE,
	HAL_RTS_MODE_CTS2SELF,
	HAL_RTS_MODE_RTS,
} HAL_RTS_MODE;

typedef enum _HAL_DATA_BW {
	HAL_DATA_BW_20M,
	HAL_DATA_BW_40M,
	HAL_DATA_BW_80M,
	HAL_DATA_BW_160M,
} HAL_DATA_BW;

typedef enum _HAL_RTS_SHORT {
	HAL_RTS_SHORT_SHORT,
	HAL_RTS_SHORT_LONG,
} HAL_RTS_SHORT;

typedef enum _HAL_SECURITY_TYPE {
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
} HAL_SECURITY_TYPE;

typedef enum _HAL_SECURITY_METHOD {
	HAL_SECURITY_METHOD_HW = 0,
	HAL_SECURITY_METHOD_SW = 1,
	HAL_SECURITY_METHOD_UNDEFINE = 0x7F,
} HAL_SECURITY_METHOD;

typedef struct _HAL_SECURITY_INFO {
	HAL_SECURITY_TYPE type;
	HAL_SECURITY_METHOD tx_method;
	HAL_SECURITY_METHOD	rx_method;
} HAL_SECURITY_INFO, *PHAL_SECURITY_INFO;

typedef struct _HAL_TXDESC_INFO {
	u32 txdesc_length;
	u32 packet_size; /* payload + wlheader */
	HAL_TX_ID tx_id;
	HALMAC_DATA_RATE data_rate;
	HAL_RTS_MODE rts_mode;
	HAL_DATA_BW data_bw;
	HAL_RTS_SHORT rts_short;
	HAL_SECURITY_TYPE security_type;
	HAL_SECURITY_METHOD encryption_method;
	u16 seq_num;
	u8 retry_limit_en;
	u8 retry_limit_number;
	u8 rts_threshold;
	u8 qos;
	u8 ht;
	u8 ampdu;
	u8 early_mode;
	u8 bm_cast;
	u8 data_short;
	u8 mac_id;
} HAL_TXDESC_INFO, *PHAL_TXDESC_INFO;

typedef struct _HAL_RXDESC_INFO {
	u8 c2h;
	u8 *pWifi_pkt;
	u32	packet_size;
	u8 crc_err;
	u8 icv_err;
} HAL_RXDESC_INFO, *PHAL_RXDESC_INFO;

typedef struct _HAL_TXDESC_PARSER {
	u8 txdesc_len;
	u16	txpkt_size;
} HAL_TXDESC_PARSER, *PHAL_TXDESC_PARSER;

typedef struct _HAL_RXDESC_PARSER {
	u32 driver_info_size;
	u16	rxpkt_size;
	u8 rxdesc_len;
	u8 c2h;
	u8 crc_err;
	u8 icv_err;
} HAL_RXDESC_PARSER, *PHAL_RXDESC_PARSER;

typedef struct _HAL_RF_REG_INFO {
	HALMAC_RF_PATH rf_path;
	u32 offset;
	u32 bit_mask;
	u32 data;
} HAL_RF_REG_INFO, *PHAL_RF_REG_INFO;

typedef struct _HALMAC_SDIO_HIMR_INFO {
	u8 rx_request;
	u8 aval_msk;
} HALMAC_SDIO_HIMR_INFO, *PHALMAC_SDIO_HIMR_INFO;

typedef struct _HALMAC_BEACON_INFO {
} HALMAC_BEACON_INFO, *PHALMAC_BEACON_INFO;

typedef struct _HALMAC_MGNT_INFO {
	u8 mu_enable;
	u8 bip;
	u8 unicast;
	u32	packet_size;
} HALMAC_MGNT_INFO, *PHALMAC_MGNT_INFO;

typedef struct _HALMAC_CTRL_INFO {
	u8 snd_enable;
	HALMAC_SND_PKT_SEL snd_pkt_sel; /* 0:unicast ndpa 1:broadcast ndpa 3:non-final BF Rpt Poll 4:final BF Rpt Poll */
	u8 *pPacket_desc;
	u32 desc_size;
	u16 seq_num;
	u8 bw;
	u16 paid;
} HALMAC_CTRL_INFO, *PHALMAC_CTRL_INFO;

typedef struct _HALMAC_HIGH_QUEUE_INFO {
	u8 *pPacket_desc;
	u32	desc_size;
} HALMAC_HIGH_QUEUE_INFO, *PHALMAC_HIGH_QUEUE_INFO;

typedef struct _HALMAC_CHIP_TYPE {
	HALMAC_CHIP_ID chip_id;
	HALMAC_CHIP_VER chip_version;
} HALMAC_CHIP_TYPE, *PHALMAC_CHIP_TYPE;

typedef struct _HALMAC_CAM_ENTRY_FORMAT {
	u16	key_id : 2;
	u16	type : 3;
	u16	mic : 1;
	u16	grp : 1;
	u16	spp_mode : 1;
	u16	rpt_md : 1;
	u16	ext_sectype : 1;
	u16 mgnt : 1;
	u16	rsvd1 : 4;
	u16 valid : 1;
	u8 mac_address[6];
	u32	key[4];
	u32	rsvd[2];
} HALMAC_CAM_ENTRY_FORMAT, *PHALMAC_CAM_ENTRY_FORMAT;

typedef struct _HALMAC_CAM_ENTRY_INFO {
	HAL_SECURITY_TYPE security_type;
	u32 key[4];
	u32 key_ext[4];
	u8 mac_address[6];
	u8 unicast;
	u8 key_id;
	u8 valid;
} HALMAC_CAM_ENTRY_INFO, *PHALMAC_CAM_ENTRY_INFO;

#endif /* End of test program */

typedef enum _HALMAC_DBG_MSG_INFO {
	HALMAC_DBG_ERR,
	HALMAC_DBG_WARN,
	HALMAC_DBG_TRACE,
} HALMAC_DBG_MSG_INFO;

typedef enum _HALMAC_DBG_MSG_TYPE {
	HALMAC_MSG_INIT,
	HALMAC_MSG_EFUSE,
	HALMAC_MSG_FW,
	HALMAC_MSG_H2C,
	HALMAC_MSG_PWR,
	HALMAC_MSG_SND,
	HALMAC_MSG_COMMON,
} HALMAC_DBG_MSG_TYPE;

typedef enum _HALMAC_CMD_PROCESS_STATUS {
	HALMAC_CMD_PROCESS_IDLE = 0x01,                 /* Init status */
	HALMAC_CMD_PROCESS_SENDING = 0x02,              /* Wait ack */
	HALMAC_CMD_PROCESS_RCVD = 0x03,                 /* Rcvd ack */
	HALMAC_CMD_PROCESS_DONE = 0x04,                 /* Event done */
	HALMAC_CMD_PROCESS_ERROR = 0x05,                /* Return code error */
	HALMAC_CMD_PROCESS_UNDEFINE = 0x7F,
} HALMAC_CMD_PROCESS_STATUS;

typedef enum _HALMAC_FEATURE_ID {
	HALMAC_FEATURE_CFG_PARA,                /* Support */
	HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE,     /* Support */
	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE,      /* Support */
	HALMAC_FEATURE_UPDATE_PACKET,           /* Support */
	HALMAC_FEATURE_UPDATE_DATAPACK,
	HALMAC_FEATURE_RUN_DATAPACK,
	HALMAC_FEATURE_CHANNEL_SWITCH,  /* Support */
	HALMAC_FEATURE_IQK,             /* Support */
	HALMAC_FEATURE_POWER_TRACKING,  /* Support */
	HALMAC_FEATURE_PSD,             /* Support */
	HALMAC_FEATURE_ALL,             /* Support, only for reset */
} HALMAC_FEATURE_ID;

typedef enum _HALMAC_DRV_RSVD_PG_NUM {
	HALMAC_RSVD_PG_NUM16,   /* 2K */
	HALMAC_RSVD_PG_NUM24,   /* 3K */
	HALMAC_RSVD_PG_NUM32,   /* 4K */
} HALMAC_DRV_RSVD_PG_NUM;


/* Platform API setting */
typedef struct _HALMAC_PLATFORM_API {
	/* R/W register */
	u8 (*SDIO_CMD52_READ)(VOID *pDriver_adapter, u32 offset);
	u8 (*SDIO_CMD53_READ_8)(VOID *pDriver_adapter, u32 offset);
	u16 (*SDIO_CMD53_READ_16)(VOID *pDriver_adapter, u32 offset);
	u32 (*SDIO_CMD53_READ_32)(VOID *pDriver_adapter, u32 offset);
	VOID (*SDIO_CMD52_WRITE)(VOID *pDriver_adapter, u32 offset, u8 value);
	VOID (*SDIO_CMD53_WRITE_8)(VOID *pDriver_adapter, u32 offset, u8 value);
	VOID (*SDIO_CMD53_WRITE_16)(VOID *pDriver_adapter, u32 offset, u16 value);
	VOID (*SDIO_CMD53_WRITE_32)(VOID *pDriver_adapter, u32 offset, u32 value);
	u8 (*REG_READ_8)(VOID *pDriver_adapter, u32 offset);
	u16 (*REG_READ_16)(VOID *pDriver_adapter, u32 offset);
	u32 (*REG_READ_32)(VOID *pDriver_adapter, u32 offset);
	VOID (*REG_WRITE_8)(VOID *pDriver_adapter, u32 offset, u8 value);
	VOID (*REG_WRITE_16)(VOID *pDriver_adapter, u32 offset, u16 value);
	VOID (*REG_WRITE_32)(VOID *pDriver_adapter, u32 offset, u32 value);

	/* send pBuf to reserved page, the tx_desc is not included in pBuf, driver need to fill tx_desc with qsel = bcn */
	u8 (*SEND_RSVD_PAGE)(VOID *pDriver_adapter, u8 *pBuf, u32 size);
	/* send pBuf to h2c queue, the tx_desc is not included in pBuf, driver need to fill tx_desc with qsel = h2c */
	u8 (*SEND_H2C_PKT)(VOID *pDriver_adapter, u8 *pBuf, u32 size);

	u8 (*RTL_FREE)(VOID *pDriver_adapter, VOID *pBuf, u32 size);
	VOID* (*RTL_MALLOC)(VOID *pDriver_adapter, u32 size);
	u8 (*RTL_MEMCPY)(VOID *pDriver_adapter, VOID *dest, VOID *src, u32 size);
	u8 (*RTL_MEMSET)(VOID *pDriver_adapter, VOID *pAddress, u8 value, u32 size);
	VOID (*RTL_DELAY_US)(VOID *pDriver_adapter, u32 us);

	u8 (*MUTEX_INIT)(VOID *pDriver_adapter, HALMAC_MUTEX *pMutex);
	u8 (*MUTEX_DEINIT)(VOID *pDriver_adapter, HALMAC_MUTEX *pMutex);
	u8 (*MUTEX_LOCK)(VOID *pDriver_adapter, HALMAC_MUTEX *pMutex);
	u8 (*MUTEX_UNLOCK)(VOID *pDriver_adapter, HALMAC_MUTEX *pMutex);

	u8 (*MSG_PRINT)(VOID *pDriver_adapter, u32 msg_type, u8 msg_level, s8 *fmt, ...);

	u8 (*EVENT_INDICATION)(VOID *pDriver_adapter, HALMAC_FEATURE_ID feature_id, HALMAC_CMD_PROCESS_STATUS process_status, u8 *buf, u32 size);

#if HALMAC_PLATFORM_TESTPROGRAM
	VOID* (*PCI_ALLOC_COMM_BUFF)(VOID *pDriver_adapter, u32 size, u32 *physical_addr, u8 cache_en);
	VOID (*PCI_FREE_COMM_BUFF)(VOID *pDriver_adapter, u32 size, u32 physical_addr, VOID *virtual_addr, u8 cache_en);
	u8 (*WRITE_DATA_SDIO_ADDR)(VOID *pDriver_adapter, u8 *pBuf, u32 size, u32 addr);
	u8 (*WRITE_DATA_USB_BULKOUT_ID)(VOID *pDriver_adapter, u8 *pBuf, u32 size, u8 bulkout_id);
	u8 (*WRITE_DATA_PCIE_QUEUE)(VOID *pDriver_adapter, u8 *pBuf, u32 size, u8 queue);
	u8 (*READ_DATA)(VOID *pDriver_adapter, u8 *pBuf, u32 *read_length);
#endif
} HALMAC_PLATFORM_API, *PHALMAC_PLATFORM_API;

/*1->Little endian 0->Big endian*/
#if HALMAC_SYSTEM_ENDIAN

/* User can not use members in Address_L_H, use Address[6] is mandatory */
typedef union _HALMAC_WLAN_ADDR {
	u8 Address[6]; /* WLAN address (MACID, BSSID, Brodcast ID). Address[0] is lowest, Address[5] is highest*/
	struct {
		union {
			u32	Address_Low;
			u8 Address_Low_B[4];
		};
		union {
			u16	Address_High;
			u8 Address_High_B[2];
		};
	} Address_L_H;
} HALMAC_WLAN_ADDR, *PHALMAC_WLAN_ADDR;

#else

/* User can not use members in Address_L_H, use Address[6] is mandatory */
typedef union _HALMAC_WLAN_ADDR {
	u8 Address[6]; /* WLAN address (MACID, BSSID, Brodcast ID). Address[0] is lowest, Address[5] is highest*/
	struct {
		union {
			u32	Address_Low;
			u8 Address_Low_B[4];
		};
		union {
			u16	Address_High;
			u8 Address_High_B[2];
		};
	} Address_L_H;
} HALMAC_WLAN_ADDR, *PHALMAC_WLAN_ADDR;

#endif

typedef enum _HALMAC_SND_ROLE {
	HAL_BFER = 0,
	HAL_BFEE = 1,
} HALMAC_SND_ROLE;

typedef enum _HALMAC_CSI_SEG_LEN {
	HAL_CSI_SEG_4K = 0,
	HAL_CSI_SEG_8K = 1,
	HAL_CSI_SEG_11K = 2,
} HALMAC_CSI_SEG_LEN;


typedef struct _HALMAC_CFG_MUMIMO_PARA {
	HALMAC_SND_ROLE role;
	u8 sounding_sts[6];
	u16 grouping_bitmap;
	u8 mu_tx_en;
	u32 given_gid_tab[2];
	u32 given_user_pos[4];
} HALMAC_CFG_MUMIMO_PARA, *PHALMAC_CFG_MUMIMO_PARA;

typedef struct _HALMAC_SU_BFER_INIT_PARA {
	u8 userid;
	u16 paid;
	u16 csi_para;
	PHALMAC_WLAN_ADDR pbfer_address;
} HALMAC_SU_BFER_INIT_PARA, *PHALMAC_SU_BFER_INIT_PARA;

typedef struct _HALMAC_MU_BFEE_INIT_PARA {
	u8 userid;
	u16 paid;
	u32 user_position_l;
	u32 user_position_h;
} HALMAC_MU_BFEE_INIT_PARA, *PHALMAC_MU_BFEE_INIT_PARA;

typedef struct _HALMAC_MU_BFER_INIT_PARA {
	u16 paid;
	u16 csi_para;
	u16 my_aid;
	HALMAC_CSI_SEG_LEN csi_length_sel;
	PHALMAC_WLAN_ADDR pbfer_address;
} HALMAC_MU_BFER_INIT_PARA, *PHALMAC_MU_BFER_INIT_PARA;

typedef struct _HALMAC_SND_INFO {
	u16 paid;
	u8 userid;
	HALMAC_DATA_RATE ndpa_rate;
	u16 csi_para;
	u16 my_aid;
	HALMAC_DATA_RATE csi_rate;
	HALMAC_CSI_SEG_LEN csi_length_sel;
	HALMAC_SND_ROLE role;
	HALMAC_WLAN_ADDR bfer_address;
	HALMAC_BW bw;
	u8 txbf_en;
	PHALMAC_SU_BFER_INIT_PARA pSu_bfer_init;
	PHALMAC_MU_BFER_INIT_PARA pMu_bfer_init;
	PHALMAC_MU_BFEE_INIT_PARA pMu_bfee_init;
} HALMAC_SND_INFO, *PHALMAC_SND_INFO;

typedef struct _HALMAC_CS_INFO {
	u8 *ch_info_buf;
	u8 *ch_info_buf_w;
	u8 extra_info_en;
	u32	buf_size;       /* buffer size */
	u32	avai_buf_size;  /* buffer size */
	u32	total_size;
	u32	accu_timeout;
	u32	ch_num;
} HALMAC_CS_INFO, *PHALMAC_CS_INFO;

typedef struct _HALMAC_RESTORE_INFO {
	u32	mac_register;
	u32	value;
	u8 length;
} HALMAC_RESTORE_INFO, *PHALMAC_RESTORE_INFO;

typedef struct _HALMAC_EVENT_TRIGGER {
	u32	physical_efuse_map : 1;
	u32	logical_efuse_map : 1;
	u32	rsvd1 : 28;
} HALMAC_EVENT_TRIGGER, *PHALMAC_EVENT_TRIGGER;

typedef struct _HALMAC_H2C_HEADER_INFO {
	u16	sub_cmd_id;
	u16	content_size;
	u8 ack;
} HALMAC_H2C_HEADER_INFO, *PHALMAC_H2C_HEADER_INFO;

typedef enum _HALMAC_DLFW_STATE {
	HALMAC_DLFW_NONE = 0,
	HALMAC_DLFW_DONE = 1,
	HALMAC_GEN_INFO_SENT = 2,
	HALMAC_DLFW_UNDEFINED = 0x7F,
} HALMAC_DLFW_STATE;

typedef enum _HALMAC_EFUSE_CMD_CONSTRUCT_STATE {
	HALMAC_EFUSE_CMD_CONSTRUCT_IDLE = 0,
	HALMAC_EFUSE_CMD_CONSTRUCT_BUSY = 1,
	HALMAC_EFUSE_CMD_CONSTRUCT_H2C_SENT = 2,
	HALMAC_EFUSE_CMD_CONSTRUCT_STATE_NUM = 3,
	HALMAC_EFUSE_CMD_CONSTRUCT_UNDEFINED = 0x7F,
} HALMAC_EFUSE_CMD_CONSTRUCT_STATE;

typedef enum _HALMAC_CFG_PARA_CMD_CONSTRUCT_STATE {
	HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE = 0,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING = 1,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT = 2,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_NUM = 3,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_UNDEFINED = 0x7F,
} HALMAC_CFG_PARA_CMD_CONSTRUCT_STATE;

typedef enum _HALMAC_SCAN_CMD_CONSTRUCT_STATE {
	HALMAC_SCAN_CMD_CONSTRUCT_IDLE = 0,
	HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED = 1,
	HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING = 2,
	HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT = 3,
	HALMAC_SCAN_CMD_CONSTRUCT_STATE_NUM = 4,
	HALMAC_SCAN_CMD_CONSTRUCT_UNDEFINED = 0x7F,
} HALMAC_SCAN_CMD_CONSTRUCT_STATE;

typedef enum _HALMAC_API_STATE {
	HALMAC_API_STATE_INIT = 0,
	HALMAC_API_STATE_HALT = 1,
	HALMAC_API_STATE_UNDEFINED = 0x7F,
} HALMAC_API_STATE;

typedef struct _HALMAC_EFUSE_STATE_SET {
	HALMAC_EFUSE_CMD_CONSTRUCT_STATE efuse_cmd_construct_state;
	HALMAC_CMD_PROCESS_STATUS process_status;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_EFUSE_STATE_SET, *PHALMAC_EFUSE_STATE_SET;

typedef struct _HALMAC_CFG_PARA_STATE_SET {
	HALMAC_CFG_PARA_CMD_CONSTRUCT_STATE cfg_para_cmd_construct_state;
	HALMAC_CMD_PROCESS_STATUS process_status;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_CFG_PARA_STATE_SET, *PHALMAC_CFG_PARA_STATE_SET;

typedef struct _HALMAC_SCAN_STATE_SET {
	HALMAC_SCAN_CMD_CONSTRUCT_STATE scan_cmd_construct_state;
	HALMAC_CMD_PROCESS_STATUS process_status;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_SCAN_STATE_SET, *PHALMAC_SCAN_STATE_SET;

typedef struct _HALMAC_UPDATE_PACKET_STATE_SET {
	HALMAC_CMD_PROCESS_STATUS process_status;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_UPDATE_PACKET_STATE_SET, *PHALMAC_UPDATE_PACKET_STATE_SET;

typedef struct _HALMAC_IQK_STATE_SET {
	HALMAC_CMD_PROCESS_STATUS process_status;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_IQK_STATE_SET, *PHALMAC_IQK_STATE_SET;

typedef struct _HALMAC_POWER_TRACKING_STATE_SET {
	HALMAC_CMD_PROCESS_STATUS	process_status;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_POWER_TRACKING_STATE_SET, *PHALMAC_POWER_TRACKING_STATE_SET;

typedef struct _HALMAC_PSD_STATE_SET {
	HALMAC_CMD_PROCESS_STATUS process_status;
	u16 data_size;
	u16 segment_size;
	u8 *pData;
	u8 fw_return_code;
	u16 seq_num;
} HALMAC_PSD_STATE_SET, *PHALMAC_PSD_STATE_SET;

typedef struct _HALMAC_STATE {
	HALMAC_EFUSE_STATE_SET efuse_state_set; /* State machine + cmd process status */
	HALMAC_CFG_PARA_STATE_SET cfg_para_state_set; /* State machine + cmd process status */
	HALMAC_SCAN_STATE_SET scan_state_set; /* State machine + cmd process status */
	HALMAC_UPDATE_PACKET_STATE_SET update_packet_set; /* cmd process status */
	HALMAC_IQK_STATE_SET iqk_set; /* cmd process status */
	HALMAC_POWER_TRACKING_STATE_SET power_tracking_set; /* cmd process status */
	HALMAC_PSD_STATE_SET psd_set; /* cmd process status */
	HALMAC_API_STATE api_state; /* Halmac api state */
	HALMAC_MAC_POWER mac_power; /* 0 : power off, 1 : power on*/
	HALMAC_PS_STATE ps_state; /* power saving state */
	HALMAC_DLFW_STATE dlfw_state; /* download FW state */
} HALMAC_STATE, *PHALMAC_STATE;

typedef struct _HALMAC_VER {
	u8 major_ver;
	u8 prototype_ver;
	u8 minor_ver;
} HALMAC_VER, *PHALMAC_VER;


typedef enum _HALMAC_API_ID {
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
	HALMAC_API_WRITE_EFUSE = 0x1B,
	HALMAC_API_READ_EFUSE = 0x1C,
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
	HALMAC_API_SWITCH_EFUSE_BANK = 0x62,
	HALMAC_API_WRITE_EFUSE_BT = 0x63,
	HALMAC_API_DUMP_EFUSE_MAP_BT = 0x64,
	HALMAC_API_MAX
} HALMAC_API_ID;


typedef struct _HALMAC_API_RECORD {
	HALMAC_API_ID api_array[API_ARRAY_SIZE];
	u8 array_wptr;
} HALMAC_API_RECORD, *PHALMAC_API_RECORD;

typedef enum _HALMAC_LA_MODE {
	HALMAC_LA_MODE_DISABLE = 0,
	HALMAC_LA_MODE_PARTIAL = 1,
	HALMAC_LA_MODE_FULL = 2,
	HALMAC_LA_MODE_UNDEFINE = 0x7F,
} HALMAC_LA_MODE;

typedef enum _HALMAC_USB_MODE {
	HALMAC_USB_MODE_U2 = 1,
	HALMAC_USB_MODE_U3 = 2,
} HALMAC_USB_MODE;

typedef enum _HALMAC_HW_ID {
	HALMAC_HW_RQPN_MAPPING = 0,
	HALMAC_HW_EFUSE_SIZE = 1,
	HALMAC_HW_EEPROM_SIZE = 2,
	HALMAC_HW_TXFIFO_SIZE = 3,
	HALMAC_HW_RSVD_PG_BNDY = 4,
	HALMAC_HW_CAM_ENTRY_NUM = 5,
	HALMAC_HW_HRPWM = 6,
	HALMAC_HW_HCPWM = 7,
	HALMAC_HW_HRPWM2 = 8,
	HALMAC_HW_HCPWM2 = 9,
	HALMAC_HW_WLAN_EFUSE_AVAILABLE_SIZE = 10,
	HALMAC_HW_TXFF_ALLOCATION = 11,
	HALMAC_HW_USB_MODE = 12,
	HALMAC_HW_SEQ_EN = 13,
	HALMAC_HW_BANDWIDTH = 14,
	HALMAC_HW_CHANNEL = 15,
	HALMAC_HW_PRI_CHANNEL_IDX = 16,
	HALMAC_HW_EN_BB_RF = 17,
	HALMAC_HW_BT_BANK_EFUSE_SIZE = 18,
	HALMAC_HW_BT_BANK1_EFUSE_SIZE = 19,
	HALMAC_HW_BT_BANK2_EFUSE_SIZE = 20,
	HALMAC_HW_ID_UNDEFINE = 0x7F,
} HALMAC_HW_ID;
typedef enum _HALMAC_EFUSE_BANK {
	HALMAC_EFUSE_BANK_WIFI = 0,
	HALMAC_EFUSE_BANK_BT = 1,
	HALMAC_EFUSE_BANK_BT_1 = 2,
	HALMAC_EFUSE_BANK_BT_2 = 3,
	HALMAC_EFUSE_BANK_MAX,
	HALMAC_EFUSE_BANK_UNDEFINE = 0X7F,
} HALMAC_EFUSE_BANK;

typedef struct _HALMAC_TXFF_ALLOCATION {
	u16 tx_fifo_pg_num;
	u16 rsvd_pg_num;
	u16 rsvd_drv_pg_num;
	u16 ac_q_pg_num;
	u16 high_queue_pg_num;
	u16 low_queue_pg_num;
	u16 normal_queue_pg_num;
	u16 extra_queue_pg_num;
	u16 pub_queue_pg_num;
	u16 rsvd_pg_bndy;
	u16	rsvd_drv_pg_bndy;
	u16	rsvd_h2c_extra_info_pg_bndy;
	u16	rsvd_h2c_queue_pg_bndy;
	u16	rsvd_cpu_instr_pg_bndy;
	u16	rsvd_fw_txbuff_pg_bndy;
	HALMAC_LA_MODE la_mode;
} HALMAC_TXFF_ALLOCATION, *PHALMAC_TXFF_ALLOCATION;

typedef struct _HALMAC_RQPN_MAP {
	HALMAC_DMA_MAPPING dma_map_vo;
	HALMAC_DMA_MAPPING dma_map_vi;
	HALMAC_DMA_MAPPING dma_map_be;
	HALMAC_DMA_MAPPING dma_map_bk;
	HALMAC_DMA_MAPPING dma_map_mg;
	HALMAC_DMA_MAPPING dma_map_hi;
} HALMAC_RQPN_MAP, *PHALMAC_RQPN_MAP;

/* Hal mac adapter */
typedef struct _HALMAC_ADAPTER {
	HALMAC_DMA_MAPPING halmac_ptcl_queue[HALMAC_PTCL_QUEUE_NUM]; /* Dma mapping of protocol queues */
	HALMAC_FWLPS_OPTION	fwlps_option; /* low power state option */
	HALMAC_WLAN_ADDR pHal_mac_addr[2]; /* mac address information, suppot 2 ports */
	HALMAC_WLAN_ADDR pHal_bss_addr[2]; /* bss address information, suppot 2 ports */
	HALMAC_MUTEX h2c_seq_mutex; /* Protect h2c_packet_seq packet*/
	HALMAC_MUTEX EfuseMutex; /* Protect Efuse map memory of halmac_adapter */
	HALMAC_CONFIG_PARA_INFO config_para_info;
	HALMAC_CS_INFO ch_sw_info;
	HALMAC_EVENT_TRIGGER event_trigger;
	HALMAC_HW_CONFIG_INFO hw_config_info; /* HW related information */
	HALMAC_SDIO_FREE_SPACE sdio_free_space;
	HALMAC_SND_INFO snd_info;
	VOID *pHalAdapter_backup; /* Backup HalAdapter address */
	VOID *pDriver_adapter; /* Driver or FW adapter address. Do not write this memory*/
	u8 *pHalEfuse_map;
	VOID *pHalmac_api; /* Record function pointer of halmac api */
	PHALMAC_PLATFORM_API pHalmac_platform_api; /* Record function pointer of platform api */
	u32 efuse_end; /* Record efuse used memory */
	u32 h2c_buf_free_space;
	u32	h2c_buff_size;
	u32	max_download_size;
	HALMAC_CHIP_ID chip_id; /* Chip ID, 8822B, 8821C... */
	HALMAC_CHIP_VER chip_version; /* A cut, B cut... */
	HALMAC_FW_VERSION fw_version;
	HALMAC_STATE halmac_state;
	HALMAC_INTERFACE halmac_interface; /* Interface information, get from driver */
	HALMAC_TRX_MODE	trx_mode; /* Noraml, WMM, P2P, LoopBack... */
	HALMAC_TXFF_ALLOCATION txff_allocation;
	u8 h2c_packet_seq; /* current h2c packet sequence number */
	u16 ack_h2c_packet_seq; /*the acked h2c packet sequence number */
	u8 hal_efuse_map_valid;
	u8 efuse_segment_size;
	u8 rpwm_record; /* record rpwm value */
	u8 low_clk; /*LPS 32K or IPS 32K*/
	u8 halmac_bulkout_num; /* USB bulkout num */
	HALMAC_API_RECORD api_record; /* API record */
	u8 gen_info_valid;
	HALMAC_GENERAL_INFO general_info;
#if HALMAC_PLATFORM_TESTPROGRAM
	HALMAC_TXAGG_BUFF_INFO halmac_tx_buf_info[4];
	HALMAC_MUTEX agg_buff_mutex; /*used for tx_agg_buffer */
	u8 max_agg_num;
	u8 send_bcn_reg_cr_backup;
#endif
} HALMAC_ADAPTER, *PHALMAC_ADAPTER;


/* Fuction pointer of  Hal mac API */
typedef struct _HALMAC_API {
	HALMAC_RET_STATUS (*halmac_mac_power_switch)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_MAC_POWER halmac_power);
	HALMAC_RET_STATUS (*halmac_download_firmware)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHamacl_fw, u32 halmac_fw_size);
	HALMAC_RET_STATUS (*halmac_get_fw_version)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_FW_VERSION pFw_version);
	HALMAC_RET_STATUS (*halmac_cfg_mac_addr)(PHALMAC_ADAPTER pHalmac_adapter, u8 halmac_port, PHALMAC_WLAN_ADDR pHal_address);
	HALMAC_RET_STATUS (*halmac_cfg_bssid)(PHALMAC_ADAPTER pHalmac_adapter, u8 halmac_port, PHALMAC_WLAN_ADDR pHal_address);
	HALMAC_RET_STATUS (*halmac_cfg_multicast_addr)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_WLAN_ADDR pHal_address);
	HALMAC_RET_STATUS (*halmac_pre_init_system_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_system_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_trx_cfg)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_TRX_MODE Mode);
	HALMAC_RET_STATUS (*halmac_init_h2c)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_cfg_rx_aggregation)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_RXAGG_CFG phalmac_rxagg_cfg);
	HALMAC_RET_STATUS (*halmac_init_protocol_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_edca_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_cfg_operation_mode)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_WIRELESS_MODE wireless_mode);
	HALMAC_RET_STATUS (*halmac_cfg_ch_bw)(PHALMAC_ADAPTER pHalmac_adapter, u8 channel, HALMAC_PRI_CH_IDX pri_ch_idx, HALMAC_BW bw);
	HALMAC_RET_STATUS (*halmac_cfg_bw)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_BW bw);
	HALMAC_RET_STATUS (*halmac_init_wmac_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_mac_cfg)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_TRX_MODE Mode);
	HALMAC_RET_STATUS (*halmac_init_sdio_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_usb_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_pcie_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_interface_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_deinit_sdio_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_deinit_usb_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_deinit_pcie_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_deinit_interface_cfg)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_get_efuse_size)(PHALMAC_ADAPTER pHalmac_adapter, u32 *halmac_size);
	HALMAC_RET_STATUS (*halmac_get_efuse_available_size)(PHALMAC_ADAPTER pHalmac_adapter, u32 *halmac_size);
	HALMAC_RET_STATUS (*halmac_dump_efuse_map)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_EFUSE_READ_CFG cfg);
	HALMAC_RET_STATUS (*halmac_dump_efuse_map_bt)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_EFUSE_BANK halmac_efues_bank, u32 bt_efuse_map_size, u8 *pBT_efuse_map);
	HALMAC_RET_STATUS (*halmac_write_efuse)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 halmac_value);
	HALMAC_RET_STATUS (*halmac_read_efuse)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 *pValue);
	HALMAC_RET_STATUS (*halmac_switch_efuse_bank)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_EFUSE_BANK halmac_efues_bank);
	HALMAC_RET_STATUS (*halmac_write_efuse_bt)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 halmac_value, HALMAC_EFUSE_BANK halmac_efues_bank);
	HALMAC_RET_STATUS (*halmac_get_logical_efuse_size)(PHALMAC_ADAPTER pHalmac_adapter, u32 *halmac_size);
	HALMAC_RET_STATUS (*halmac_dump_logical_efuse_map)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_EFUSE_READ_CFG cfg);
	HALMAC_RET_STATUS (*halmac_write_logical_efuse)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 halmac_value);
	HALMAC_RET_STATUS (*halmac_read_logical_efuse)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 *pValue);
	HALMAC_RET_STATUS (*halmac_pg_efuse_by_map)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_PG_EFUSE_INFO pPg_efuse_info, HALMAC_EFUSE_READ_CFG cfg);
	HALMAC_RET_STATUS (*halmac_get_c2h_info)(PHALMAC_ADAPTER pHalmac_adapter, u8 *halmac_buf, u32 halmac_size);
	HALMAC_RET_STATUS (*halmac_cfg_fwlps_option)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_FWLPS_OPTION pLps_option);
	HALMAC_RET_STATUS (*halmac_cfg_fwips_option)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_FWIPS_OPTION pIps_option);
	HALMAC_RET_STATUS (*halmac_enter_wowlan)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_WOWLAN_OPTION pWowlan_option);
	HALMAC_RET_STATUS (*halmac_leave_wowlan)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_enter_ps)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_PS_STATE ps_state);
	HALMAC_RET_STATUS (*halmac_leave_ps)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_h2c_lb)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_debug)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_cfg_parameter)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_PHY_PARAMETER_INFO para_info, u8 full_fifo);
	HALMAC_RET_STATUS (*halmac_update_packet)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_PACKET_ID pkt_id, u8 *pkt, u32 pkt_size);
	HALMAC_RET_STATUS (*halmac_bcn_ie_filter)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_BCN_IE_INFO pBcn_ie_info);
	u8 (*halmac_reg_read_8)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset);
	HALMAC_RET_STATUS (*halmac_reg_write_8)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 halmac_data);
	u16 (*halmac_reg_read_16)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset);
	HALMAC_RET_STATUS (*halmac_reg_write_16)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u16 halmac_data);
	u32 (*halmac_reg_read_32)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset);
	HALMAC_RET_STATUS (*halmac_reg_write_32)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u32 halmac_data);
	HALMAC_RET_STATUS (*halmac_tx_allowed_sdio)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHalmac_buf, u32 halmac_size);
	HALMAC_RET_STATUS (*halmac_set_bulkout_num)(PHALMAC_ADAPTER pHalmac_adapter, u8 bulkout_num);
	HALMAC_RET_STATUS (*halmac_get_sdio_tx_addr)(PHALMAC_ADAPTER pHalmac_adapter, u8 *halmac_buf, u32 halmac_size, u32 *pcmd53_addr);
	HALMAC_RET_STATUS (*halmac_get_usb_bulkout_id)(PHALMAC_ADAPTER pHalmac_adapter, u8 *halmac_buf, u32 halmac_size, u8 *bulkout_id);
	HALMAC_RET_STATUS (*halmac_timer_2s)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_fill_txdesc_checksum)(PHALMAC_ADAPTER pHalmac_adapter, u8 *cur_desc);
	HALMAC_RET_STATUS (*halmac_update_datapack)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_DATA_TYPE halmac_data_type, PHALMAC_PHY_PARAMETER_INFO para_info);
	HALMAC_RET_STATUS (*halmac_run_datapack)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_DATA_TYPE halmac_data_type);
	HALMAC_RET_STATUS (*halmac_cfg_drv_info)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_DRV_INFO halmac_drv_info);
	HALMAC_RET_STATUS (*halmac_send_bt_coex)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBt_buf, u32 bt_size, u8 ack);
	HALMAC_RET_STATUS (*halmac_verify_platform_api)(PHALMAC_ADAPTER pHalmac_adapte);
	u32 (*halmac_get_fifo_size)(PHALMAC_ADAPTER pHalmac_adapter, HAL_FIFO_SEL halmac_fifo_sel);
	HALMAC_RET_STATUS (*halmac_dump_fifo)(PHALMAC_ADAPTER pHalmac_adapter, HAL_FIFO_SEL halmac_fifo_sel, u8 *pFifo_map, u32 halmac_fifo_dump_size);
	HALMAC_RET_STATUS (*halmac_cfg_txbf)(PHALMAC_ADAPTER pHalmac_adapter, u8 userid, HALMAC_BW bw, u8 txbf_en);
	HALMAC_RET_STATUS (*halmac_cfg_mumimo)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_CFG_MUMIMO_PARA pCfgmu);
	HALMAC_RET_STATUS (*halmac_cfg_sounding)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_SND_ROLE role, HALMAC_DATA_RATE datarate);
	HALMAC_RET_STATUS (*halmac_del_sounding)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_SND_ROLE role);
	HALMAC_RET_STATUS (*halmac_su_bfer_entry_init)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_SU_BFER_INIT_PARA pSu_bfer_init);
	HALMAC_RET_STATUS (*halmac_su_bfee_entry_init)(PHALMAC_ADAPTER pHalmac_adapter, u8 userid, u16 paid);
	HALMAC_RET_STATUS (*halmac_mu_bfer_entry_init)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_MU_BFER_INIT_PARA pMu_bfer_init);
	HALMAC_RET_STATUS (*halmac_mu_bfee_entry_init)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_MU_BFEE_INIT_PARA pMu_bfee_init);
	HALMAC_RET_STATUS (*halmac_su_bfer_entry_del)(PHALMAC_ADAPTER pHalmac_adapter, u8 userid);
	HALMAC_RET_STATUS (*halmac_su_bfee_entry_del)(PHALMAC_ADAPTER pHalmac_adapter, u8 userid);
	HALMAC_RET_STATUS (*halmac_mu_bfer_entry_del)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_mu_bfee_entry_del)(PHALMAC_ADAPTER pHalmac_adapter, u8 userid);
	HALMAC_RET_STATUS (*halmac_add_ch_info)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_CH_INFO pCh_info);
	HALMAC_RET_STATUS (*halmac_add_extra_ch_info)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_CH_EXTRA_INFO pCh_extra_info);
	HALMAC_RET_STATUS (*halmac_ctrl_ch_switch)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_CH_SWITCH_OPTION pCs_option);
	HALMAC_RET_STATUS (*halmac_clear_ch_info)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_send_general_info)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_GENERAL_INFO pgGeneral_info);
	HALMAC_RET_STATUS (*halmac_start_iqk)(PHALMAC_ADAPTER pHalmac_adapter, u8 clear);
	HALMAC_RET_STATUS (*halmac_ctrl_pwr_tracking)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_PWR_TRACKING_OPTION pPwr_tracking_opt);
	HALMAC_RET_STATUS (*halmac_psd)(PHALMAC_ADAPTER pHalmac_adapter, u16 start_psd, u16 end_psd);
	HALMAC_RET_STATUS (*halmac_cfg_tx_agg_align)(PHALMAC_ADAPTER pHalmac_adapter, u8 enable, u16 align_size);
	HALMAC_RET_STATUS (*halmac_query_status)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_FEATURE_ID feature_id, HALMAC_CMD_PROCESS_STATUS *pProcess_status, u8 *data, u32 *size);
	HALMAC_RET_STATUS (*halmac_reset_feature)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_FEATURE_ID feature_id);
	HALMAC_RET_STATUS (*halmac_check_fw_status)(PHALMAC_ADAPTER pHalmac_adapter, u8 *fw_status);
	HALMAC_RET_STATUS (*halmac_dump_fw_dmem)(PHALMAC_ADAPTER pHalmac_adapter, u8 *dmem, u32 *size);
	HALMAC_RET_STATUS (*halmac_cfg_max_dl_size)(PHALMAC_ADAPTER pHalmac_adapter, u32 size);
	HALMAC_RET_STATUS (*halmac_cfg_la_mode)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_LA_MODE la_mode);
	HALMAC_RET_STATUS (*halmac_get_hw_value)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_HW_ID hw_id, VOID *pvalue);
	HALMAC_RET_STATUS (*halmac_set_hw_value)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_HW_ID hw_id, VOID *pvalue);
	HALMAC_RET_STATUS (*halmac_cfg_drv_rsvd_pg_num)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_DRV_RSVD_PG_NUM pg_num);
	HALMAC_RET_STATUS (*halmac_get_chip_version)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_VER *version);
	HALMAC_RET_STATUS (*halmac_chk_txdesc)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHalmac_buf, u32 halmac_size);
#if HALMAC_PLATFORM_TESTPROGRAM
	HALMAC_RET_STATUS (*halmac_gen_txdesc)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pPcket_buffer, PHAL_TXDESC_INFO pTxdesc_info);
	HALMAC_RET_STATUS (*halmac_txdesc_parser)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pTxdesc, PHAL_TXDESC_PARSER pTxdesc_parser);
	HALMAC_RET_STATUS (*halmac_rxdesc_parser)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pRxdesc, PHAL_RXDESC_PARSER pRxdesc_parser);
	HALMAC_RET_STATUS (*halmac_get_txdesc_size)(PHALMAC_ADAPTER pHalmac_adapter, PHAL_TXDESC_INFO pTxdesc_info, u32 *size);
	HALMAC_RET_STATUS (*halmac_send_packet)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, u32 size, PHAL_TXDESC_INFO pTxdesc_Info);
	HALMAC_RET_STATUS (*halmac_get_pcie_packet)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, u32 *size);
	HALMAC_RET_STATUS (*halmac_gen_txagg_desc)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pPcket_buffer, u32 agg_num);
	HALMAC_RET_STATUS (*halmac_parse_packet)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, PHAL_RXDESC_INFO pRxdesc_info, u8 **next_pkt);
	u32 (*halmac_bb_reg_read)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u8 len);
	HALMAC_RET_STATUS (*halmac_bb_reg_write)(PHALMAC_ADAPTER pHalmac_adapter, u32 halmac_offset, u32 halmac_data, u8 len);
	u32 (*halmac_rf_reg_read)(PHALMAC_ADAPTER pHalmac_adapter, PHAL_RF_REG_INFO pRf_reg_info);
	HALMAC_RET_STATUS (*halmac_rf_reg_write)(PHALMAC_ADAPTER pHalmac_adapter, PHAL_RF_REG_INFO pRf_reg_info);
	HALMAC_RET_STATUS (*halmac_init_antenna_selection)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_bb_preconfig)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_init_crystal_capacity)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_trx_antenna_setting)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_himr_setting_sdio)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_SDIO_HIMR_INFO sdio_himr_sdio);
	HALMAC_RET_STATUS (*halmac_config_security)(PHALMAC_ADAPTER pHalmac_adapter, PHAL_SECURITY_INFO pSecurity_info);
	HALMAC_RET_STATUS (*halmac_write_cam)(PHALMAC_ADAPTER pHalmac_adapter, u32 entry_index, PHALMAC_CAM_ENTRY_INFO pCam_entry_info);
	HALMAC_RET_STATUS (*halmac_read_cam)(PHALMAC_ADAPTER pHalmac_adapter, u32 entry_index, PHALMAC_CAM_ENTRY_FORMAT pContent);
	HALMAC_RET_STATUS (*halmac_dump_cam_table)(PHALMAC_ADAPTER pHalmac_adapter, u32 entry_num, PHALMAC_CAM_ENTRY_FORMAT pCam_table);
	HALMAC_RET_STATUS (*halmac_load_cam_table)(PHALMAC_ADAPTER pHalmac_adapter, u8 entry_num, PHALMAC_CAM_ENTRY_FORMAT pCam_table);
	HALMAC_RET_STATUS (*halmac_send_beacon)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, u32 size, PHALMAC_BEACON_INFO pbeacon_info);
	HALMAC_RET_STATUS (*halmac_get_management_txdesc)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, u32 *pSize, PHALMAC_MGNT_INFO pmgnt_info);
	HALMAC_RET_STATUS (*halmac_send_control)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, u32 size, PHALMAC_CTRL_INFO pctrl_info);
	HALMAC_RET_STATUS (*halmac_send_hiqueue)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pBuf, u32 size, PHALMAC_HIGH_QUEUE_INFO pHigh_info);
	HALMAC_RET_STATUS (*halmac_run_pwrseq)(PHALMAC_ADAPTER pHalmac_adapter, HALMAC_PWR_SEQ_ID seq);
	HALMAC_RET_STATUS (*halmac_media_status_rpt)(PHALMAC_ADAPTER pHalmac_adapter, u8 op_mode, u8 mac_id_ind, u8 mac_id, u8 mac_id_end);
	HALMAC_RET_STATUS (*halmac_stop_beacon)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_check_trx_status)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_set_agg_num)(PHALMAC_ADAPTER pHalmac_adapter, u8 agg_num);
	HALMAC_RET_STATUS (*halmac_timer_10ms)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_download_firmware_fpag)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHamacl_fw, u32 halmac_fw_size, u32 iram_address);
	HALMAC_RET_STATUS (*halmac_download_rom_fpga)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHamacl_fw, u32 halmac_fw_size, u32 rom_address);
	HALMAC_RET_STATUS (*halmac_download_flash)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHamacl_fw, u32 halmac_fw_size, u32 rom_address);
	HALMAC_RET_STATUS (*halmac_erase_flash)(PHALMAC_ADAPTER pHalmac_adapter);
	HALMAC_RET_STATUS (*halmac_check_flash)(PHALMAC_ADAPTER pHalmac_adapter, u8 *pHamacl_fw, u32 halmac_fw_size);
	HALMAC_RET_STATUS (*halmac_send_nlo)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_NLO_CFG pNlo_cfg);
	HALMAC_RET_STATUS (*halmac_get_chip_type)(PHALMAC_ADAPTER pHalmac_adapter, PHALMAC_CHIP_TYPE pChip_type);
	u32 (*halmac_get_rx_agg_num)(PHALMAC_ADAPTER pHalmac_adapter, u32 pkt_size, u8 *pPkt_buff);
#endif
} HALMAC_API, *PHALMAC_API;

#define HALMAC_GET_API(phalmac_adapter) ((PHALMAC_API)phalmac_adapter->pHalmac_api)

static HALMAC_INLINE HALMAC_RET_STATUS
halmac_adapter_validate(
	PHALMAC_ADAPTER pHalmac_adapter
)
{
	if ((NULL == pHalmac_adapter) || (pHalmac_adapter->pHalAdapter_backup != pHalmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	return HALMAC_RET_SUCCESS;
}

static HALMAC_INLINE HALMAC_RET_STATUS
halmac_api_validate(
	PHALMAC_ADAPTER pHalmac_adapter
)
{
	if (HALMAC_API_STATE_INIT != pHalmac_adapter->halmac_state.api_state)
		return HALMAC_RET_API_INVALID;

	return HALMAC_RET_SUCCESS;
}

static HALMAC_INLINE HALMAC_RET_STATUS
halmac_fw_validate(
	PHALMAC_ADAPTER pHalmac_adapter
)
{
	if (HALMAC_DLFW_DONE != pHalmac_adapter->halmac_state.dlfw_state && HALMAC_GEN_INFO_SENT != pHalmac_adapter->halmac_state.dlfw_state)
		return HALMAC_RET_NO_DLFW;

	return HALMAC_RET_SUCCESS;
}

#endif
