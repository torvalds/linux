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
#ifndef _HALMAC_TYPE_H_
#define _HALMAC_TYPE_H_

#include "halmac_2_platform.h"
#include "halmac_fw_info.h"
#include "halmac_intf_phy_cmd.h"

#define HALMAC_SCAN_CH_NUM_MAX 28
#define HALMAC_BCN_IE_BMP_SIZE 24 /* ID0~ID191, 192/8=24 */
#define HALMAC_PHY_PARAMETER_SIZE 12
#define HALMAC_PHY_PARAMETER_MAX_NUM 128
#define HALMAC_MAX_SSID_LEN 32
#define HALMAC_SUPPORT_NLO_NUM 16
#define HALMAC_SUPPORT_PROBE_REQ_NUM 8
#define HALMC_DDMA_POLLING_COUNT 1000
#define API_ARRAY_SIZE 32

/* platform api */
#define PLATFORM_SDIO_CMD52_READ                                               \
	halmac_adapter->halmac_platform_api->SDIO_CMD52_READ
#define PLATFORM_SDIO_CMD53_READ_8                                             \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_READ_8
#define PLATFORM_SDIO_CMD53_READ_16                                            \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_READ_16
#define PLATFORM_SDIO_CMD53_READ_32                                            \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_READ_32
#define PLATFORM_SDIO_CMD53_READ_N                                             \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_READ_N
#define PLATFORM_SDIO_CMD52_WRITE                                              \
	halmac_adapter->halmac_platform_api->SDIO_CMD52_WRITE
#define PLATFORM_SDIO_CMD53_WRITE_8                                            \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_WRITE_8
#define PLATFORM_SDIO_CMD53_WRITE_16                                           \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_WRITE_16
#define PLATFORM_SDIO_CMD53_WRITE_32                                           \
	halmac_adapter->halmac_platform_api->SDIO_CMD53_WRITE_32

#define PLATFORM_REG_READ_8 halmac_adapter->halmac_platform_api->REG_READ_8
#define PLATFORM_REG_READ_16 halmac_adapter->halmac_platform_api->REG_READ_16
#define PLATFORM_REG_READ_32 halmac_adapter->halmac_platform_api->REG_READ_32
#define PLATFORM_REG_WRITE_8 halmac_adapter->halmac_platform_api->REG_WRITE_8
#define PLATFORM_REG_WRITE_16 halmac_adapter->halmac_platform_api->REG_WRITE_16
#define PLATFORM_REG_WRITE_32 halmac_adapter->halmac_platform_api->REG_WRITE_32

#define PLATFORM_SEND_RSVD_PAGE                                                \
	halmac_adapter->halmac_platform_api->SEND_RSVD_PAGE
#define PLATFORM_SEND_H2C_PKT halmac_adapter->halmac_platform_api->SEND_H2C_PKT

#define PLATFORM_EVENT_INDICATION                                              \
	halmac_adapter->halmac_platform_api->EVENT_INDICATION

#define HALMAC_RT_TRACE(drv_adapter, comp, level, fmt, ...)                    \
	RT_TRACE(drv_adapter, COMP_HALMAC, level, fmt, ##__VA_ARGS__)

#define HALMAC_REG_READ_8 halmac_api->halmac_reg_read_8
#define HALMAC_REG_READ_16 halmac_api->halmac_reg_read_16
#define HALMAC_REG_READ_32 halmac_api->halmac_reg_read_32
#define HALMAC_REG_WRITE_8 halmac_api->halmac_reg_write_8
#define HALMAC_REG_WRITE_16 halmac_api->halmac_reg_write_16
#define HALMAC_REG_WRITE_32 halmac_api->halmac_reg_write_32
#define HALMAC_REG_SDIO_CMD53_READ_N halmac_api->halmac_reg_sdio_cmd53_read_n

/* Swap Little-endian <-> Big-endia*/

/*1->Little endian 0->Big endian*/
#if HALMAC_SYSTEM_ENDIAN
#else
#endif

#define HALMAC_ALIGN(x, a) HALMAC_ALIGN_MASK(x, (a) - 1)
#define HALMAC_ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))

/* HALMAC API return status*/
enum halmac_ret_status {
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
	HALMAC_RET_BIP_NO_SUPPORT = 0x56,
	HALMAC_RET_ENTRY_INDEX_ERROR = 0x57,
	HALMAC_RET_ENTRY_KEY_ID_ERROR = 0x58,
	HALMAC_RET_DRV_DL_ERR = 0x59,
	HALMAC_RET_OQT_NOT_ENOUGH = 0x5A,
	HALMAC_RET_PWR_UNCHANGE = 0x5B,
	HALMAC_RET_FW_NO_SUPPORT = 0x60,
	HALMAC_RET_TXFIFO_NO_EMPTY = 0x61,
};

enum halmac_mac_clock_hw_def {
	HALMAC_MAC_CLOCK_HW_DEF_80M = 0,
	HALMAC_MAC_CLOCK_HW_DEF_40M = 1,
	HALMAC_MAC_CLOCK_HW_DEF_20M = 2,
};

/* Rx aggregation parameters */
enum halmac_normal_rxagg_th_to {
	HALMAC_NORMAL_RXAGG_THRESHOLD = 0xFF,
	HALMAC_NORMAL_RXAGG_TIMEOUT = 0x01,
};

enum halmac_loopback_rxagg_th_to {
	HALMAC_LOOPBACK_RXAGG_THRESHOLD = 0xFF,
	HALMAC_LOOPBACK_RXAGG_TIMEOUT = 0x01,
};

/* Chip ID*/
enum halmac_chip_id {
	HALMAC_CHIP_ID_8822B = 0,
	HALMAC_CHIP_ID_8821C = 1,
	HALMAC_CHIP_ID_8814B = 2,
	HALMAC_CHIP_ID_8197F = 3,
	HALMAC_CHIP_ID_UNDEFINE = 0x7F,
};

enum halmac_chip_id_hw_def {
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
	HALMAC_CHIP_ID_HW_DEF_8814B = 0x10,
	HALMAC_CHIP_ID_HW_DEF_UNDEFINE = 0x7F,
	HALMAC_CHIP_ID_HW_DEF_PS = 0xEA,
};

/* Chip Version*/
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

/* Network type select */
enum halmac_network_type_select {
	HALMAC_NETWORK_NO_LINK = 0,
	HALMAC_NETWORK_ADHOC = 1,
	HALMAC_NETWORK_INFRASTRUCTURE = 2,
	HALMAC_NETWORK_AP = 3,
	HALMAC_NETWORK_UNDEFINE = 0x7F,
};

/* Transfer mode select */
enum halmac_trnsfer_mode_select {
	HALMAC_TRNSFER_NORMAL = 0x0,
	HALMAC_TRNSFER_LOOPBACK_DIRECT = 0xB,
	HALMAC_TRNSFER_LOOPBACK_DELAY = 0x3,
	HALMAC_TRNSFER_UNDEFINE = 0x7F,
};

/* Queue select */
enum halmac_dma_mapping {
	HALMAC_DMA_MAPPING_EXTRA = 0,
	HALMAC_DMA_MAPPING_LOW = 1,
	HALMAC_DMA_MAPPING_NORMAL = 2,
	HALMAC_DMA_MAPPING_HIGH = 3,
	HALMAC_DMA_MAPPING_UNDEFINE = 0x7F,
};

#define HALMAC_MAP2_HQ HALMAC_DMA_MAPPING_HIGH
#define HALMAC_MAP2_NQ HALMAC_DMA_MAPPING_NORMAL
#define HALMAC_MAP2_LQ HALMAC_DMA_MAPPING_LOW
#define HALMAC_MAP2_EXQ HALMAC_DMA_MAPPING_EXTRA
#define HALMAC_MAP2_UNDEF HALMAC_DMA_MAPPING_UNDEFINE

/* TXDESC queue select TID */
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

	HALMAC_TXDESC_QSEL_UNDEFINE = 0x7F,
};

enum halmac_ptcl_queue {
	HALMAC_PTCL_QUEUE_VO = 0x0,
	HALMAC_PTCL_QUEUE_VI = 0x1,
	HALMAC_PTCL_QUEUE_BE = 0x2,
	HALMAC_PTCL_QUEUE_BK = 0x3,
	HALMAC_PTCL_QUEUE_MG = 0x4,
	HALMAC_PTCL_QUEUE_HI = 0x5,
	HALMAC_PTCL_QUEUE_NUM = 0x6,
	HALMAC_PTCL_QUEUE_UNDEFINE = 0x7F,
};

enum halmac_queue_select {
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
};

/* USB burst size */
enum halmac_usb_burst_size {
	HALMAC_USB_BURST_SIZE_3_0 = 0x0,
	HALMAC_USB_BURST_SIZE_2_0_HSPEED = 0x1,
	HALMAC_USB_BURST_SIZE_2_0_FSPEED = 0x2,
	HALMAC_USB_BURST_SIZE_2_0_OTHERS = 0x3,
	HALMAC_USB_BURST_SIZE_UNDEFINE = 0x7F,
};

/* HAL API  function parameters*/
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
};

struct halmac_rxagg_cfg {
	enum halmac_rx_agg_mode mode;
	struct halmac_rxagg_th threshold;
};

enum halmac_mac_power {
	HALMAC_MAC_POWER_OFF = 0x0,
	HALMAC_MAC_POWER_ON = 0x1,
	HALMAC_MAC_POWER_UNDEFINE = 0x7F,
};

enum halmac_ps_state {
	HALMAC_PS_STATE_ACT = 0x0,
	HALMAC_PS_STATE_LPS = 0x1,
	HALMAC_PS_STATE_IPS = 0x2,
	HALMAC_PS_STATE_UNDEFINE = 0x7F,
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

struct halmac_fwlps_option {
	u8 mode;
	u8 clk_request;
	u8 rlbm;
	u8 smart_ps;
	u8 awake_interval;
	u8 all_queue_uapsd;
	u8 pwr_state;
	u8 low_pwr_rx_beacon;
	u8 ant_auto_switch;
	u8 ps_allow_bt_high_priority;
	u8 protect_bcn;
	u8 silence_period;
	u8 fast_bt_connect;
	u8 two_antenna_en;
	u8 adopt_user_setting;
	u8 drv_bcn_early_shift;
	bool enter_32K;
};

struct halmac_fwips_option {
	u8 adopt_user_setting;
};

struct halmac_wowlan_option {
	u8 adopt_user_setting;
};

struct halmac_bcn_ie_info {
	u8 func_en;
	u8 size_th;
	u8 timeout;
	u8 ie_bmp[HALMAC_BCN_IE_BMP_SIZE];
};

enum halmac_reg_type {
	HALMAC_REG_TYPE_MAC = 0x0,
	HALMAC_REG_TYPE_BB = 0x1,
	HALMAC_REG_TYPE_RF = 0x2,
	HALMAC_REG_TYPE_UNDEFINE = 0x7F,
};

enum halmac_parameter_cmd {
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

struct halmac_h2c_info {
	u16 h2c_seq_num; /* H2C sequence number */
	u8 in_use; /* 0 : empty 1 : used */
	enum halmac_h2c_return_code status;
};

struct halmac_pg_efuse_info {
	u8 *efuse_map;
	u32 efuse_map_size;
	u8 *efuse_mask;
	u32 efuse_mask_size;
};

struct halmac_txagg_buff_info {
	u8 *tx_agg_buf;
	u8 *curr_pkt_buf;
	u32 avai_buf_size;
	u32 total_pkt_size;
	u8 agg_num;
};

struct halmac_config_para_info {
	u32 para_buf_size; /* Parameter buffer size */
	u8 *cfg_para_buf; /* Buffer for config parameter */
	u8 *para_buf_w; /* Write pointer of the parameter buffer */
	u32 para_num; /* Parameter numbers in parameter buffer */
	u32 avai_para_buf_size; /* Free size of parameter buffer */
	u32 offset_accumulation;
	u32 value_accumulation;
	enum halmac_data_type data_type; /*DataType which is passed to FW*/
	u8 datapack_segment; /*DataPack Segment, from segment0...*/
	bool full_fifo_mode; /* Used full tx fifo to save cfg parameter */
};

struct halmac_hw_config_info {
	u32 efuse_size; /* Record efuse size */
	u32 eeprom_size; /* Record eeprom size */
	u32 bt_efuse_size; /* Record BT efuse size */
	u32 tx_fifo_size; /* Record tx fifo size */
	u32 rx_fifo_size; /* Record rx fifo size */
	u8 txdesc_size; /* Record tx desc size */
	u8 rxdesc_size; /* Record rx desc size */
	u32 page_size; /* Record page size */
	u16 tx_align_size;
	u8 page_size_2_power;
	u8 cam_entry_num; /* Record CAM entry number */
};

struct halmac_sdio_free_space {
	u16 high_queue_number; /* Free space of HIQ */
	u16 normal_queue_number; /* Free space of MIDQ */
	u16 low_queue_number; /* Free space of LOWQ */
	u16 public_queue_number; /* Free space of PUBQ */
	u16 extra_queue_number; /* Free space of EXBQ */
	u8 ac_oqt_number;
	u8 non_ac_oqt_number;
	u8 ac_empty;
};

enum hal_fifo_sel {
	HAL_FIFO_SEL_TX,
	HAL_FIFO_SEL_RX,
	HAL_FIFO_SEL_RSVD_PAGE,
	HAL_FIFO_SEL_REPORT,
	HAL_FIFO_SEL_LLT,
};

enum halmac_drv_info {
	HALMAC_DRV_INFO_NONE, /* No information is appended in rx_pkt */
	HALMAC_DRV_INFO_PHY_STATUS, /* PHY status is appended after rx_desc */
	HALMAC_DRV_INFO_PHY_SNIFFER, /* PHY status and sniffer info appended */
	HALMAC_DRV_INFO_PHY_PLCP, /* PHY status and plcp header are appended */
	HALMAC_DRV_INFO_UNDEFINE,
};

struct halmac_bt_coex_cmd {
	u8 element_id;
	u8 op_code;
	u8 op_code_ver;
	u8 req_num;
	u8 data0;
	u8 data1;
	u8 data2;
	u8 data3;
	u8 data4;
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
	bool switch_en;
	u8 dest_ch_en;
	u8 absolute_time_en;
	u8 dest_ch;
	u8 normal_period;
	u8 normal_cycle;
	u8 phase_2_period;
};

struct halmac_fw_version {
	u16 version;
	u8 sub_version;
	u8 sub_index;
	u16 h2c_version;
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
	struct halmac_pwr_tracking_para
		pwr_tracking_para[4]; /* pathA, pathB, pathC, pathD */
};

struct halmac_nlo_cfg {
	u8 num_of_ssid;
	u8 num_of_hidden_ap;
	u8 rsvd[2];
	u32 pattern_check;
	u32 rsvd1;
	u32 rsvd2;
	u8 ssid_len[HALMAC_SUPPORT_NLO_NUM];
	u8 chiper_type[HALMAC_SUPPORT_NLO_NUM];
	u8 rsvd3[HALMAC_SUPPORT_NLO_NUM];
	u8 loc_probe_req[HALMAC_SUPPORT_PROBE_REQ_NUM];
	u8 rsvd4[56];
	u8 ssid[HALMAC_SUPPORT_NLO_NUM][HALMAC_MAX_SSID_LEN];
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
	HALMAC_VHT_NSS4_MCS9
};

enum halmac_rf_path {
	HALMAC_RF_PATH_A,
	HALMAC_RF_PATH_B,
	HALMAC_RF_PATH_C,
	HALMAC_RF_PATH_D
};

enum halmac_snd_pkt_sel {
	HALMAC_UNI_NDPA,
	HALMAC_BMC_NDPA,
	HALMAC_NON_FINAL_BFRPRPOLL,
	HALMAC_FINAL_BFRPTPOLL,
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

enum halmac_dbg_msg_info {
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
	HALMAC_MSG_USB
};

enum halmac_cmd_process_status {
	HALMAC_CMD_PROCESS_IDLE = 0x01, /* Init status */
	HALMAC_CMD_PROCESS_SENDING = 0x02, /* Wait ack */
	HALMAC_CMD_PROCESS_RCVD = 0x03, /* Rcvd ack */
	HALMAC_CMD_PROCESS_DONE = 0x04, /* Event done */
	HALMAC_CMD_PROCESS_ERROR = 0x05, /* Return code error */
	HALMAC_CMD_PROCESS_UNDEFINE = 0x7F,
};

enum halmac_feature_id {
	HALMAC_FEATURE_CFG_PARA, /* Support */
	HALMAC_FEATURE_DUMP_PHYSICAL_EFUSE, /* Support */
	HALMAC_FEATURE_DUMP_LOGICAL_EFUSE, /* Support */
	HALMAC_FEATURE_UPDATE_PACKET, /* Support */
	HALMAC_FEATURE_UPDATE_DATAPACK,
	HALMAC_FEATURE_RUN_DATAPACK,
	HALMAC_FEATURE_CHANNEL_SWITCH, /* Support */
	HALMAC_FEATURE_IQK, /* Support */
	HALMAC_FEATURE_POWER_TRACKING, /* Support */
	HALMAC_FEATURE_PSD, /* Support */
	HALMAC_FEATURE_ALL, /* Support, only for reset */
};

enum halmac_drv_rsvd_pg_num {
	HALMAC_RSVD_PG_NUM16, /* 2K */
	HALMAC_RSVD_PG_NUM24, /* 3K */
	HALMAC_RSVD_PG_NUM32, /* 4K */
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
	HALMAC_PORTIDMAX
};

struct halmac_p2pps {
	/*DW0*/
	u8 offload_en : 1;
	u8 role : 1;
	u8 ctwindow_en : 1;
	u8 noa_en : 1;
	u8 noa_sel : 1;
	u8 all_sta_sleep : 1;
	u8 discovery : 1;
	u8 rsvd2 : 1;
	u8 p2p_port_id;
	u8 p2p_group;
	u8 p2p_macid;

	/*DW1*/
	u8 ctwindow_length;
	u8 rsvd3;
	u8 rsvd4;
	u8 rsvd5;

	/*DW2*/
	u32 noa_duration_para;

	/*DW3*/
	u32 noa_interval_para;

	/*DW4*/
	u32 noa_start_time_para;

	/*DW5*/
	u32 noa_count_para;
};

/* Platform API setting */
struct halmac_platform_api {
	/* R/W register */
	u8 (*SDIO_CMD52_READ)(void *driver_adapter, u32 offset);
	u8 (*SDIO_CMD53_READ_8)(void *driver_adapter, u32 offset);
	u16 (*SDIO_CMD53_READ_16)(void *driver_adapter, u32 offset);
	u32 (*SDIO_CMD53_READ_32)(void *driver_adapter, u32 offset);
	u8 (*SDIO_CMD53_READ_N)(void *driver_adapter, u32 offset, u32 size,
				u8 *data);
	void (*SDIO_CMD52_WRITE)(void *driver_adapter, u32 offset, u8 value);
	void (*SDIO_CMD53_WRITE_8)(void *driver_adapter, u32 offset, u8 value);
	void (*SDIO_CMD53_WRITE_16)(void *driver_adapter, u32 offset,
				    u16 value);
	void (*SDIO_CMD53_WRITE_32)(void *driver_adapter, u32 offset,
				    u32 value);
	u8 (*REG_READ_8)(void *driver_adapter, u32 offset);
	u16 (*REG_READ_16)(void *driver_adapter, u32 offset);
	u32 (*REG_READ_32)(void *driver_adapter, u32 offset);
	void (*REG_WRITE_8)(void *driver_adapter, u32 offset, u8 value);
	void (*REG_WRITE_16)(void *driver_adapter, u32 offset, u16 value);
	void (*REG_WRITE_32)(void *driver_adapter, u32 offset, u32 value);

	/* send buf to reserved page, the tx_desc is not included in buf,
	 * driver need to fill tx_desc with qsel = bcn
	 */
	bool (*SEND_RSVD_PAGE)(void *driver_adapter, u8 *buf, u32 size);
	/* send buf to h2c queue, the tx_desc is not included in buf,
	 * driver need to fill tx_desc with qsel = h2c
	 */
	bool (*SEND_H2C_PKT)(void *driver_adapter, u8 *buf, u32 size);

	bool (*EVENT_INDICATION)(void *driver_adapter,
				 enum halmac_feature_id feature_id,
				 enum halmac_cmd_process_status process_status,
				 u8 *buf, u32 size);
};

/*1->Little endian 0->Big endian*/
#if HALMAC_SYSTEM_ENDIAN

#else

#endif

/* User can not use members in address_l_h, use address[6] is mandatory */
union halmac_wlan_addr {
	u8 address[6]; /* WLAN address (MACID, BSSID, Brodcast ID).
			* address[0] is lowest, address[5] is highest
			*/
	struct {
		union {
			u32 address_low;
			__le32 le_address_low;
			u8 address_low_b[4];
		};
		union {
			u16 address_high;
			__le16 le_address_high;
			u8 address_high_b[2];
		};
	} address_l_h;
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
	bool sounding_sts[6];
	u16 grouping_bitmap;
	bool mu_tx_en;
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
	u32 user_position_l;
	u32 user_position_h;
};

struct halmac_mu_bfer_init_para {
	u16 paid;
	u16 csi_para;
	u16 my_aid;
	enum halmac_csi_seg_len csi_length_sel;
	union halmac_wlan_addr bfer_address;
};

struct halmac_snd_info {
	u16 paid;
	u8 userid;
	enum halmac_data_rate ndpa_rate;
	u16 csi_para;
	u16 my_aid;
	enum halmac_data_rate csi_rate;
	enum halmac_csi_seg_len csi_length_sel;
	enum halmac_snd_role role;
	union halmac_wlan_addr bfer_address;
	enum halmac_bw bw;
	u8 txbf_en;
	struct halmac_su_bfer_init_para *su_bfer_init;
	struct halmac_mu_bfer_init_para *mu_bfer_init;
	struct halmac_mu_bfee_init_para *mu_bfee_init;
};

struct halmac_cs_info {
	u8 *ch_info_buf;
	u8 *ch_info_buf_w;
	u8 extra_info_en;
	u32 buf_size; /* buffer size */
	u32 avai_buf_size; /* buffer size */
	u32 total_size;
	u32 accu_timeout;
	u32 ch_num;
};

struct halmac_restore_info {
	u32 mac_register;
	u32 value;
	u8 length;
};

struct halmac_event_trigger {
	u32 physical_efuse_map : 1;
	u32 logical_efuse_map : 1;
	u32 rsvd1 : 28;
};

struct halmac_h2c_header_info {
	u16 sub_cmd_id;
	u16 content_size;
	bool ack;
};

enum halmac_dlfw_state {
	HALMAC_DLFW_NONE = 0,
	HALMAC_DLFW_DONE = 1,
	HALMAC_GEN_INFO_SENT = 2,
	HALMAC_DLFW_UNDEFINED = 0x7F,
};

enum halmac_efuse_cmd_construct_state {
	HALMAC_EFUSE_CMD_CONSTRUCT_IDLE = 0,
	HALMAC_EFUSE_CMD_CONSTRUCT_BUSY = 1,
	HALMAC_EFUSE_CMD_CONSTRUCT_H2C_SENT = 2,
	HALMAC_EFUSE_CMD_CONSTRUCT_STATE_NUM = 3,
	HALMAC_EFUSE_CMD_CONSTRUCT_UNDEFINED = 0x7F,
};

enum halmac_cfg_para_cmd_construct_state {
	HALMAC_CFG_PARA_CMD_CONSTRUCT_IDLE = 0,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_CONSTRUCTING = 1,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_H2C_SENT = 2,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_NUM = 3,
	HALMAC_CFG_PARA_CMD_CONSTRUCT_UNDEFINED = 0x7F,
};

enum halmac_scan_cmd_construct_state {
	HALMAC_SCAN_CMD_CONSTRUCT_IDLE = 0,
	HALMAC_SCAN_CMD_CONSTRUCT_BUFFER_CLEARED = 1,
	HALMAC_SCAN_CMD_CONSTRUCT_CONSTRUCTING = 2,
	HALMAC_SCAN_CMD_CONSTRUCT_H2C_SENT = 3,
	HALMAC_SCAN_CMD_CONSTRUCT_STATE_NUM = 4,
	HALMAC_SCAN_CMD_CONSTRUCT_UNDEFINED = 0x7F,
};

enum halmac_api_state {
	HALMAC_API_STATE_INIT = 0,
	HALMAC_API_STATE_HALT = 1,
	HALMAC_API_STATE_UNDEFINED = 0x7F,
};

struct halmac_efuse_state_set {
	enum halmac_efuse_cmd_construct_state efuse_cmd_construct_state;
	enum halmac_cmd_process_status process_status;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_cfg_para_state_set {
	enum halmac_cfg_para_cmd_construct_state cfg_para_cmd_construct_state;
	enum halmac_cmd_process_status process_status;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_scan_state_set {
	enum halmac_scan_cmd_construct_state scan_cmd_construct_state;
	enum halmac_cmd_process_status process_status;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_update_packet_state_set {
	enum halmac_cmd_process_status process_status;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_iqk_state_set {
	enum halmac_cmd_process_status process_status;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_power_tracking_state_set {
	enum halmac_cmd_process_status process_status;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_psd_state_set {
	enum halmac_cmd_process_status process_status;
	u16 data_size;
	u16 segment_size;
	u8 *data;
	u8 fw_return_code;
	u16 seq_num;
};

struct halmac_state {
	struct halmac_efuse_state_set
		efuse_state_set; /* State machine + cmd process status */
	struct halmac_cfg_para_state_set
		cfg_para_state_set; /* State machine + cmd process status */
	struct halmac_scan_state_set
		scan_state_set; /* State machine + cmd process status */
	struct halmac_update_packet_state_set
		update_packet_set; /* cmd process status */
	struct halmac_iqk_state_set iqk_set; /* cmd process status */
	struct halmac_power_tracking_state_set
		power_tracking_set; /* cmd process status */
	struct halmac_psd_state_set psd_set; /* cmd process status */
	enum halmac_api_state api_state; /* Halmac api state */
	enum halmac_mac_power mac_power; /* 0 : power off, 1 : power on*/
	enum halmac_ps_state ps_state; /* power saving state */
	enum halmac_dlfw_state dlfw_state; /* download FW state */
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
	HALMAC_API_DL_DRV_RSVD_PG = 0x65,
	HALMAC_API_PCIE_SWITCH = 0x66,
	HALMAC_API_PHY_CFG = 0x67,
	HALMAC_API_CFG_RX_FIFO_EXPANDING_MODE = 0x68,
	HALMAC_API_CFG_CSI_RATE = 0x69,
	HALMAC_API_MAX
};

struct halmac_api_record {
	enum halmac_api_id api_array[API_ARRAY_SIZE];
	u8 array_wptr;
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

enum halmac_hw_id {
	/* Get HW value */
	HALMAC_HW_RQPN_MAPPING = 0x00,
	HALMAC_HW_EFUSE_SIZE = 0x01,
	HALMAC_HW_EEPROM_SIZE = 0x02,
	HALMAC_HW_BT_BANK_EFUSE_SIZE = 0x03,
	HALMAC_HW_BT_BANK1_EFUSE_SIZE = 0x04,
	HALMAC_HW_BT_BANK2_EFUSE_SIZE = 0x05,
	HALMAC_HW_TXFIFO_SIZE = 0x06,
	HALMAC_HW_RSVD_PG_BNDY = 0x07,
	HALMAC_HW_CAM_ENTRY_NUM = 0x08,
	HALMAC_HW_IC_VERSION = 0x09,
	HALMAC_HW_PAGE_SIZE = 0x0A,
	HALMAC_HW_TX_AGG_ALIGN_SIZE = 0x0B,
	HALMAC_HW_RX_AGG_ALIGN_SIZE = 0x0C,
	HALMAC_HW_DRV_INFO_SIZE = 0x0D,
	HALMAC_HW_TXFF_ALLOCATION = 0x0E,
	HALMAC_HW_RSVD_EFUSE_SIZE = 0x0F,
	HALMAC_HW_FW_HDR_SIZE = 0x10,
	HALMAC_HW_TX_DESC_SIZE = 0x11,
	HALMAC_HW_RX_DESC_SIZE = 0x12,
	HALMAC_HW_WLAN_EFUSE_AVAILABLE_SIZE = 0x13,
	/* Set HW value */
	HALMAC_HW_USB_MODE = 0x60,
	HALMAC_HW_SEQ_EN = 0x61,
	HALMAC_HW_BANDWIDTH = 0x62,
	HALMAC_HW_CHANNEL = 0x63,
	HALMAC_HW_PRI_CHANNEL_IDX = 0x64,
	HALMAC_HW_EN_BB_RF = 0x65,
	HALMAC_HW_SDIO_TX_PAGE_THRESHOLD = 0x66,
	HALMAC_HW_AMPDU_CONFIG = 0x67,

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

struct halmac_txff_allocation {
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
	u16 rsvd_drv_pg_bndy;
	u16 rsvd_h2c_extra_info_pg_bndy;
	u16 rsvd_h2c_queue_pg_bndy;
	u16 rsvd_cpu_instr_pg_bndy;
	u16 rsvd_fw_txbuff_pg_bndy;
	enum halmac_la_mode la_mode;
	enum halmac_rx_fifo_expanding_mode rx_fifo_expanding_mode;
};

struct halmac_rqpn_map {
	enum halmac_dma_mapping dma_map_vo;
	enum halmac_dma_mapping dma_map_vi;
	enum halmac_dma_mapping dma_map_be;
	enum halmac_dma_mapping dma_map_bk;
	enum halmac_dma_mapping dma_map_mg;
	enum halmac_dma_mapping dma_map_hi;
};

struct halmac_security_setting {
	u8 tx_encryption;
	u8 rx_decryption;
	u8 bip_enable;
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
	u32 threshold;
	enum halmac_dma_mapping dma_queue_sel;
};

struct halmac_ampdu_config {
	u8 max_agg_num;
};

struct halmac_port_cfg {
	u8 port0_sync_tsf;
	u8 port1_sync_tsf;
};

struct halmac_rqpn_ {
	enum halmac_trx_mode mode;
	enum halmac_dma_mapping dma_map_vo;
	enum halmac_dma_mapping dma_map_vi;
	enum halmac_dma_mapping dma_map_be;
	enum halmac_dma_mapping dma_map_bk;
	enum halmac_dma_mapping dma_map_mg;
	enum halmac_dma_mapping dma_map_hi;
};

struct halmac_pg_num_ {
	enum halmac_trx_mode mode;
	u16 hq_num;
	u16 nq_num;
	u16 lq_num;
	u16 exq_num;
	u16 gap_num; /*used for loopback mode*/
};

struct halmac_intf_phy_para_ {
	u16 offset;
	u16 value;
	u16 ip_sel;
	u16 cut;
	u16 plaform;
};

struct halmac_iqk_para_ {
	u8 clear;
	u8 segment_iqk;
};

/* Hal mac adapter */
struct halmac_adapter {
	/* Dma mapping of protocol queues */
	enum halmac_dma_mapping halmac_ptcl_queue[HALMAC_PTCL_QUEUE_NUM];
	/* low power state option */
	struct halmac_fwlps_option fwlps_option;
	/* mac address information, suppot 2 ports */
	union halmac_wlan_addr hal_mac_addr[HALMAC_PORTIDMAX];
	/* bss address information, suppot 2 ports */
	union halmac_wlan_addr hal_bss_addr[HALMAC_PORTIDMAX];
	/* Protect h2c_packet_seq packet*/
	spinlock_t h2c_seq_lock;
	/* Protect Efuse map memory of halmac_adapter */
	spinlock_t efuse_lock;
	struct halmac_config_para_info config_para_info;
	struct halmac_cs_info ch_sw_info;
	struct halmac_event_trigger event_trigger;
	/* HW related information */
	struct halmac_hw_config_info hw_config_info;
	struct halmac_sdio_free_space sdio_free_space;
	struct halmac_snd_info snd_info;
	/* Backup HalAdapter address */
	void *hal_adapter_backup;
	/* Driver or FW adapter address. Do not write this memory*/
	void *driver_adapter;
	u8 *hal_efuse_map;
	/* Record function pointer of halmac api */
	void *halmac_api;
	/* Record function pointer of platform api */
	struct halmac_platform_api *halmac_platform_api;
	/* Record efuse used memory */
	u32 efuse_end;
	u32 h2c_buf_free_space;
	u32 h2c_buff_size;
	u32 max_download_size;
	/* Chip ID, 8822B, 8821C... */
	enum halmac_chip_id chip_id;
	/* A cut, B cut... */
	enum halmac_chip_ver chip_version;
	struct halmac_fw_version fw_version;
	struct halmac_state halmac_state;
	/* Interface information, get from driver */
	enum halmac_interface halmac_interface;
	/* Noraml, WMM, P2P, LoopBack... */
	enum halmac_trx_mode trx_mode;
	struct halmac_txff_allocation txff_allocation;
	u8 h2c_packet_seq; /* current h2c packet sequence number */
	u16 ack_h2c_packet_seq; /*the acked h2c packet sequence number */
	bool hal_efuse_map_valid;
	u8 efuse_segment_size;
	u8 rpwm_record; /* record rpwm value */
	bool low_clk; /*LPS 32K or IPS 32K*/
	u8 halmac_bulkout_num; /* USB bulkout num */
	struct halmac_api_record api_record; /* API record */
	bool gen_info_valid;
	struct halmac_general_info general_info;
	u8 drv_info_size;
	enum halmac_sdio_cmd53_4byte_mode sdio_cmd53_4byte;
};

/* Function pointer of  Hal mac API */
struct halmac_api {
	enum halmac_ret_status (*halmac_mac_power_switch)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_mac_power halmac_power);
	enum halmac_ret_status (*halmac_download_firmware)(
		struct halmac_adapter *halmac_adapter, u8 *hamacl_fw,
		u32 halmac_fw_size);
	enum halmac_ret_status (*halmac_free_download_firmware)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_dlfw_mem dlfw_mem, u8 *hamacl_fw,
		u32 halmac_fw_size);
	enum halmac_ret_status (*halmac_get_fw_version)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_fw_version *fw_version);
	enum halmac_ret_status (*halmac_cfg_mac_addr)(
		struct halmac_adapter *halmac_adapter, u8 halmac_port,
		union halmac_wlan_addr *hal_address);
	enum halmac_ret_status (*halmac_cfg_bssid)(
		struct halmac_adapter *halmac_adapter, u8 halmac_port,
		union halmac_wlan_addr *hal_address);
	enum halmac_ret_status (*halmac_cfg_multicast_addr)(
		struct halmac_adapter *halmac_adapter,
		union halmac_wlan_addr *hal_address);
	enum halmac_ret_status (*halmac_pre_init_system_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_system_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_trx_cfg)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_trx_mode mode);
	enum halmac_ret_status (*halmac_init_h2c)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_cfg_rx_aggregation)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_rxagg_cfg *phalmac_rxagg_cfg);
	enum halmac_ret_status (*halmac_init_protocol_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_edca_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_cfg_operation_mode)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_wireless_mode wireless_mode);
	enum halmac_ret_status (*halmac_cfg_ch_bw)(
		struct halmac_adapter *halmac_adapter, u8 channel,
		enum halmac_pri_ch_idx pri_ch_idx, enum halmac_bw bw);
	enum halmac_ret_status (*halmac_cfg_bw)(
		struct halmac_adapter *halmac_adapter, enum halmac_bw bw);
	enum halmac_ret_status (*halmac_init_wmac_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_mac_cfg)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_trx_mode mode);
	enum halmac_ret_status (*halmac_init_sdio_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_usb_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_pcie_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_init_interface_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_deinit_sdio_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_deinit_usb_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_deinit_pcie_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_deinit_interface_cfg)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_get_efuse_size)(
		struct halmac_adapter *halmac_adapter, u32 *halmac_size);
	enum halmac_ret_status (*halmac_get_efuse_available_size)(
		struct halmac_adapter *halmac_adapter, u32 *halmac_size);
	enum halmac_ret_status (*halmac_dump_efuse_map)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status (*halmac_dump_efuse_map_bt)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_efuse_bank halmac_efues_bank, u32 bt_efuse_map_size,
		u8 *bt_efuse_map);
	enum halmac_ret_status (*halmac_write_efuse)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u8 halmac_value);
	enum halmac_ret_status (*halmac_read_efuse)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u8 *value);
	enum halmac_ret_status (*halmac_switch_efuse_bank)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_efuse_bank halmac_efues_bank);
	enum halmac_ret_status (*halmac_write_efuse_bt)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u8 halmac_value, enum halmac_efuse_bank halmac_efues_bank);
	enum halmac_ret_status (*halmac_get_logical_efuse_size)(
		struct halmac_adapter *halmac_adapter, u32 *halmac_size);
	enum halmac_ret_status (*halmac_dump_logical_efuse_map)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status (*halmac_write_logical_efuse)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u8 halmac_value);
	enum halmac_ret_status (*halmac_read_logical_efuse)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u8 *value);
	enum halmac_ret_status (*halmac_pg_efuse_by_map)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_pg_efuse_info *pg_efuse_info,
		enum halmac_efuse_read_cfg cfg);
	enum halmac_ret_status (*halmac_get_c2h_info)(
		struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		u32 halmac_size);
	enum halmac_ret_status (*halmac_cfg_fwlps_option)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_fwlps_option *lps_option);
	enum halmac_ret_status (*halmac_cfg_fwips_option)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_fwips_option *ips_option);
	enum halmac_ret_status (*halmac_enter_wowlan)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_wowlan_option *wowlan_option);
	enum halmac_ret_status (*halmac_leave_wowlan)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_enter_ps)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_ps_state ps_state);
	enum halmac_ret_status (*halmac_leave_ps)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_h2c_lb)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_debug)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_cfg_parameter)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_phy_parameter_info *para_info, u8 full_fifo);
	enum halmac_ret_status (*halmac_update_packet)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_packet_id pkt_id, u8 *pkt, u32 pkt_size);
	enum halmac_ret_status (*halmac_bcn_ie_filter)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_bcn_ie_info *bcn_ie_info);
	u8 (*halmac_reg_read_8)(struct halmac_adapter *halmac_adapter,
				u32 halmac_offset);
	enum halmac_ret_status (*halmac_reg_write_8)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u8 halmac_data);
	u16 (*halmac_reg_read_16)(struct halmac_adapter *halmac_adapter,
				  u32 halmac_offset);
	enum halmac_ret_status (*halmac_reg_write_16)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u16 halmac_data);
	u32 (*halmac_reg_read_32)(struct halmac_adapter *halmac_adapter,
				  u32 halmac_offset);
	u32 (*halmac_reg_read_indirect_32)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset);
	u8 (*halmac_reg_sdio_cmd53_read_n)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u32 halmac_size, u8 *halmac_data);
	enum halmac_ret_status (*halmac_reg_write_32)(
		struct halmac_adapter *halmac_adapter, u32 halmac_offset,
		u32 halmac_data);
	enum halmac_ret_status (*halmac_tx_allowed_sdio)(
		struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		u32 halmac_size);
	enum halmac_ret_status (*halmac_set_bulkout_num)(
		struct halmac_adapter *halmac_adapter, u8 bulkout_num);
	enum halmac_ret_status (*halmac_get_sdio_tx_addr)(
		struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		u32 halmac_size, u32 *pcmd53_addr);
	enum halmac_ret_status (*halmac_get_usb_bulkout_id)(
		struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		u32 halmac_size, u8 *bulkout_id);
	enum halmac_ret_status (*halmac_timer_2s)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_fill_txdesc_checksum)(
		struct halmac_adapter *halmac_adapter, u8 *cur_desc);
	enum halmac_ret_status (*halmac_update_datapack)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_data_type halmac_data_type,
		struct halmac_phy_parameter_info *para_info);
	enum halmac_ret_status (*halmac_run_datapack)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_data_type halmac_data_type);
	enum halmac_ret_status (*halmac_cfg_drv_info)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_drv_info halmac_drv_info);
	enum halmac_ret_status (*halmac_send_bt_coex)(
		struct halmac_adapter *halmac_adapter, u8 *bt_buf, u32 bt_size,
		u8 ack);
	enum halmac_ret_status (*halmac_verify_platform_api)(
		struct halmac_adapter *halmac_adapte);
	u32 (*halmac_get_fifo_size)(struct halmac_adapter *halmac_adapter,
				    enum hal_fifo_sel halmac_fifo_sel);
	enum halmac_ret_status (*halmac_dump_fifo)(
		struct halmac_adapter *halmac_adapter,
		enum hal_fifo_sel halmac_fifo_sel, u32 halmac_start_addr,
		u32 halmac_fifo_dump_size, u8 *fifo_map);
	enum halmac_ret_status (*halmac_cfg_txbf)(
		struct halmac_adapter *halmac_adapter, u8 userid,
		enum halmac_bw bw, u8 txbf_en);
	enum halmac_ret_status (*halmac_cfg_mumimo)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_cfg_mumimo_para *cfgmu);
	enum halmac_ret_status (*halmac_cfg_sounding)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_snd_role role, enum halmac_data_rate datarate);
	enum halmac_ret_status (*halmac_del_sounding)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_snd_role role);
	enum halmac_ret_status (*halmac_su_bfer_entry_init)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_su_bfer_init_para *su_bfer_init);
	enum halmac_ret_status (*halmac_su_bfee_entry_init)(
		struct halmac_adapter *halmac_adapter, u8 userid, u16 paid);
	enum halmac_ret_status (*halmac_mu_bfer_entry_init)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_mu_bfer_init_para *mu_bfer_init);
	enum halmac_ret_status (*halmac_mu_bfee_entry_init)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_mu_bfee_init_para *mu_bfee_init);
	enum halmac_ret_status (*halmac_su_bfer_entry_del)(
		struct halmac_adapter *halmac_adapter, u8 userid);
	enum halmac_ret_status (*halmac_su_bfee_entry_del)(
		struct halmac_adapter *halmac_adapter, u8 userid);
	enum halmac_ret_status (*halmac_mu_bfer_entry_del)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_mu_bfee_entry_del)(
		struct halmac_adapter *halmac_adapter, u8 userid);
	enum halmac_ret_status (*halmac_add_ch_info)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_ch_info *ch_info);
	enum halmac_ret_status (*halmac_add_extra_ch_info)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_ch_extra_info *ch_extra_info);
	enum halmac_ret_status (*halmac_ctrl_ch_switch)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_ch_switch_option *cs_option);
	enum halmac_ret_status (*halmac_p2pps)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_p2pps *p2p_ps);
	enum halmac_ret_status (*halmac_clear_ch_info)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_send_general_info)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_general_info *pg_general_info);
	enum halmac_ret_status (*halmac_start_iqk)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_iqk_para_ *iqk_para);
	enum halmac_ret_status (*halmac_ctrl_pwr_tracking)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_pwr_tracking_option *pwr_tracking_opt);
	enum halmac_ret_status (*halmac_psd)(
		struct halmac_adapter *halmac_adapter, u16 start_psd,
		u16 end_psd);
	enum halmac_ret_status (*halmac_cfg_tx_agg_align)(
		struct halmac_adapter *halmac_adapter, u8 enable,
		u16 align_size);
	enum halmac_ret_status (*halmac_query_status)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_feature_id feature_id,
		enum halmac_cmd_process_status *process_status, u8 *data,
		u32 *size);
	enum halmac_ret_status (*halmac_reset_feature)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_feature_id feature_id);
	enum halmac_ret_status (*halmac_check_fw_status)(
		struct halmac_adapter *halmac_adapter, bool *fw_status);
	enum halmac_ret_status (*halmac_dump_fw_dmem)(
		struct halmac_adapter *halmac_adapter, u8 *dmem, u32 *size);
	enum halmac_ret_status (*halmac_cfg_max_dl_size)(
		struct halmac_adapter *halmac_adapter, u32 size);
	enum halmac_ret_status (*halmac_cfg_la_mode)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_la_mode la_mode);
	enum halmac_ret_status (*halmac_cfg_rx_fifo_expanding_mode)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_rx_fifo_expanding_mode rx_fifo_expanding_mode);
	enum halmac_ret_status (*halmac_config_security)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_security_setting *sec_setting);
	u8 (*halmac_get_used_cam_entry_num)(
		struct halmac_adapter *halmac_adapter,
		enum hal_security_type sec_type);
	enum halmac_ret_status (*halmac_write_cam)(
		struct halmac_adapter *halmac_adapter, u32 entry_index,
		struct halmac_cam_entry_info *cam_entry_info);
	enum halmac_ret_status (*halmac_read_cam_entry)(
		struct halmac_adapter *halmac_adapter, u32 entry_index,
		struct halmac_cam_entry_format *content);
	enum halmac_ret_status (*halmac_clear_cam_entry)(
		struct halmac_adapter *halmac_adapter, u32 entry_index);
	enum halmac_ret_status (*halmac_get_hw_value)(
		struct halmac_adapter *halmac_adapter, enum halmac_hw_id hw_id,
		void *pvalue);
	enum halmac_ret_status (*halmac_set_hw_value)(
		struct halmac_adapter *halmac_adapter, enum halmac_hw_id hw_id,
		void *pvalue);
	enum halmac_ret_status (*halmac_cfg_drv_rsvd_pg_num)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_drv_rsvd_pg_num pg_num);
	enum halmac_ret_status (*halmac_get_chip_version)(
		struct halmac_adapter *halmac_adapter,
		struct halmac_ver *version);
	enum halmac_ret_status (*halmac_chk_txdesc)(
		struct halmac_adapter *halmac_adapter, u8 *halmac_buf,
		u32 halmac_size);
	enum halmac_ret_status (*halmac_dl_drv_rsvd_page)(
		struct halmac_adapter *halmac_adapter, u8 pg_offset,
		u8 *hal_buf, u32 size);
	enum halmac_ret_status (*halmac_pcie_switch)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_pcie_cfg pcie_cfg);
	enum halmac_ret_status (*halmac_phy_cfg)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_intf_phy_platform platform);
	enum halmac_ret_status (*halmac_cfg_csi_rate)(
		struct halmac_adapter *halmac_adapter, u8 rssi, u8 current_rate,
		u8 fixrate_en, u8 *new_rate);
	enum halmac_ret_status (*halmac_sdio_cmd53_4byte)(
		struct halmac_adapter *halmac_adapter,
		enum halmac_sdio_cmd53_4byte_mode cmd53_4byte_mode);
	enum halmac_ret_status (*halmac_interface_integration_tuning)(
		struct halmac_adapter *halmac_adapter);
	enum halmac_ret_status (*halmac_txfifo_is_empty)(
		struct halmac_adapter *halmac_adapter, u32 chk_num);
};

#define HALMAC_GET_API(phalmac_adapter)                                        \
	((struct halmac_api *)phalmac_adapter->halmac_api)

static inline enum halmac_ret_status
halmac_adapter_validate(struct halmac_adapter *halmac_adapter)
{
	if ((!halmac_adapter) ||
	    (halmac_adapter->hal_adapter_backup != halmac_adapter))
		return HALMAC_RET_ADAPTER_INVALID;

	return HALMAC_RET_SUCCESS;
}

static inline enum halmac_ret_status
halmac_api_validate(struct halmac_adapter *halmac_adapter)
{
	if (halmac_adapter->halmac_state.api_state != HALMAC_API_STATE_INIT)
		return HALMAC_RET_API_INVALID;

	return HALMAC_RET_SUCCESS;
}

static inline enum halmac_ret_status
halmac_fw_validate(struct halmac_adapter *halmac_adapter)
{
	if (halmac_adapter->halmac_state.dlfw_state != HALMAC_DLFW_DONE &&
	    halmac_adapter->halmac_state.dlfw_state != HALMAC_GEN_INFO_SENT)
		return HALMAC_RET_NO_DLFW;

	return HALMAC_RET_SUCCESS;
}

#endif
