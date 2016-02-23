/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _HW_H_
#define _HW_H_

#include "targaddrs.h"

#define ATH10K_FW_DIR			"ath10k"

#define QCA988X_2_0_DEVICE_ID   (0x003c)
#define QCA6164_2_1_DEVICE_ID   (0x0041)
#define QCA6174_2_1_DEVICE_ID   (0x003e)
#define QCA99X0_2_0_DEVICE_ID   (0x0040)
#define QCA9377_1_0_DEVICE_ID   (0x0042)

/* QCA988X 1.0 definitions (unsupported) */
#define QCA988X_HW_1_0_CHIP_ID_REV	0x0

/* QCA988X 2.0 definitions */
#define QCA988X_HW_2_0_VERSION		0x4100016c
#define QCA988X_HW_2_0_CHIP_ID_REV	0x2
#define QCA988X_HW_2_0_FW_DIR		ATH10K_FW_DIR "/QCA988X/hw2.0"
#define QCA988X_HW_2_0_FW_FILE		"firmware.bin"
#define QCA988X_HW_2_0_OTP_FILE		"otp.bin"
#define QCA988X_HW_2_0_BOARD_DATA_FILE	"board.bin"
#define QCA988X_HW_2_0_PATCH_LOAD_ADDR	0x1234

/* QCA6174 target BMI version signatures */
#define QCA6174_HW_1_0_VERSION		0x05000000
#define QCA6174_HW_1_1_VERSION		0x05000001
#define QCA6174_HW_1_3_VERSION		0x05000003
#define QCA6174_HW_2_1_VERSION		0x05010000
#define QCA6174_HW_3_0_VERSION		0x05020000
#define QCA6174_HW_3_2_VERSION		0x05030000

/* QCA9377 target BMI version signatures */
#define QCA9377_HW_1_0_DEV_VERSION	0x05020000
#define QCA9377_HW_1_1_DEV_VERSION	0x05020001

enum qca6174_pci_rev {
	QCA6174_PCI_REV_1_1 = 0x11,
	QCA6174_PCI_REV_1_3 = 0x13,
	QCA6174_PCI_REV_2_0 = 0x20,
	QCA6174_PCI_REV_3_0 = 0x30,
};

enum qca6174_chip_id_rev {
	QCA6174_HW_1_0_CHIP_ID_REV = 0,
	QCA6174_HW_1_1_CHIP_ID_REV = 1,
	QCA6174_HW_1_3_CHIP_ID_REV = 2,
	QCA6174_HW_2_1_CHIP_ID_REV = 4,
	QCA6174_HW_2_2_CHIP_ID_REV = 5,
	QCA6174_HW_3_0_CHIP_ID_REV = 8,
	QCA6174_HW_3_1_CHIP_ID_REV = 9,
	QCA6174_HW_3_2_CHIP_ID_REV = 10,
};

enum qca9377_chip_id_rev {
	QCA9377_HW_1_0_CHIP_ID_REV = 0x0,
	QCA9377_HW_1_1_CHIP_ID_REV = 0x1,
};

#define QCA6174_HW_2_1_FW_DIR		"ath10k/QCA6174/hw2.1"
#define QCA6174_HW_2_1_FW_FILE		"firmware.bin"
#define QCA6174_HW_2_1_OTP_FILE		"otp.bin"
#define QCA6174_HW_2_1_BOARD_DATA_FILE	"board.bin"
#define QCA6174_HW_2_1_PATCH_LOAD_ADDR	0x1234

#define QCA6174_HW_3_0_FW_DIR		"ath10k/QCA6174/hw3.0"
#define QCA6174_HW_3_0_FW_FILE		"firmware.bin"
#define QCA6174_HW_3_0_OTP_FILE		"otp.bin"
#define QCA6174_HW_3_0_BOARD_DATA_FILE	"board.bin"
#define QCA6174_HW_3_0_PATCH_LOAD_ADDR	0x1234

/* QCA99X0 1.0 definitions (unsupported) */
#define QCA99X0_HW_1_0_CHIP_ID_REV     0x0

/* QCA99X0 2.0 definitions */
#define QCA99X0_HW_2_0_DEV_VERSION     0x01000000
#define QCA99X0_HW_2_0_CHIP_ID_REV     0x1
#define QCA99X0_HW_2_0_FW_DIR          ATH10K_FW_DIR "/QCA99X0/hw2.0"
#define QCA99X0_HW_2_0_FW_FILE         "firmware.bin"
#define QCA99X0_HW_2_0_OTP_FILE        "otp.bin"
#define QCA99X0_HW_2_0_BOARD_DATA_FILE "board.bin"
#define QCA99X0_HW_2_0_PATCH_LOAD_ADDR	0x1234

/* QCA9377 1.0 definitions */
#define QCA9377_HW_1_0_FW_DIR          ATH10K_FW_DIR "/QCA9377/hw1.0"
#define QCA9377_HW_1_0_FW_FILE         "firmware.bin"
#define QCA9377_HW_1_0_OTP_FILE        "otp.bin"
#define QCA9377_HW_1_0_BOARD_DATA_FILE "board.bin"
#define QCA9377_HW_1_0_PATCH_LOAD_ADDR	0x1234

#define ATH10K_FW_API2_FILE		"firmware-2.bin"
#define ATH10K_FW_API3_FILE		"firmware-3.bin"

/* added support for ATH10K_FW_IE_WMI_OP_VERSION */
#define ATH10K_FW_API4_FILE		"firmware-4.bin"

/* HTT id conflict fix for management frames over HTT */
#define ATH10K_FW_API5_FILE		"firmware-5.bin"

#define ATH10K_FW_UTF_FILE		"utf.bin"
#define ATH10K_FW_UTF_API2_FILE		"utf-2.bin"

/* includes also the null byte */
#define ATH10K_FIRMWARE_MAGIC               "QCA-ATH10K"
#define ATH10K_BOARD_MAGIC                  "QCA-ATH10K-BOARD"

#define ATH10K_BOARD_API2_FILE         "board-2.bin"

#define REG_DUMP_COUNT_QCA988X 60

#define QCA988X_CAL_DATA_LEN		2116

struct ath10k_fw_ie {
	__le32 id;
	__le32 len;
	u8 data[0];
};

enum ath10k_fw_ie_type {
	ATH10K_FW_IE_FW_VERSION = 0,
	ATH10K_FW_IE_TIMESTAMP = 1,
	ATH10K_FW_IE_FEATURES = 2,
	ATH10K_FW_IE_FW_IMAGE = 3,
	ATH10K_FW_IE_OTP_IMAGE = 4,

	/* WMI "operations" interface version, 32 bit value. Supported from
	 * FW API 4 and above.
	 */
	ATH10K_FW_IE_WMI_OP_VERSION = 5,

	/* HTT "operations" interface version, 32 bit value. Supported from
	 * FW API 5 and above.
	 */
	ATH10K_FW_IE_HTT_OP_VERSION = 6,

	/* Code swap image for firmware binary */
	ATH10K_FW_IE_FW_CODE_SWAP_IMAGE = 7,
};

enum ath10k_fw_wmi_op_version {
	ATH10K_FW_WMI_OP_VERSION_UNSET = 0,

	ATH10K_FW_WMI_OP_VERSION_MAIN = 1,
	ATH10K_FW_WMI_OP_VERSION_10_1 = 2,
	ATH10K_FW_WMI_OP_VERSION_10_2 = 3,
	ATH10K_FW_WMI_OP_VERSION_TLV = 4,
	ATH10K_FW_WMI_OP_VERSION_10_2_4 = 5,
	ATH10K_FW_WMI_OP_VERSION_10_4 = 6,

	/* keep last */
	ATH10K_FW_WMI_OP_VERSION_MAX,
};

enum ath10k_fw_htt_op_version {
	ATH10K_FW_HTT_OP_VERSION_UNSET = 0,

	ATH10K_FW_HTT_OP_VERSION_MAIN = 1,

	/* also used in 10.2 and 10.2.4 branches */
	ATH10K_FW_HTT_OP_VERSION_10_1 = 2,

	ATH10K_FW_HTT_OP_VERSION_TLV = 3,

	ATH10K_FW_HTT_OP_VERSION_10_4 = 4,

	/* keep last */
	ATH10K_FW_HTT_OP_VERSION_MAX,
};

enum ath10k_bd_ie_type {
	/* contains sub IEs of enum ath10k_bd_ie_board_type */
	ATH10K_BD_IE_BOARD = 0,
};

enum ath10k_bd_ie_board_type {
	ATH10K_BD_IE_BOARD_NAME = 0,
	ATH10K_BD_IE_BOARD_DATA = 1,
};

enum ath10k_hw_rev {
	ATH10K_HW_QCA988X,
	ATH10K_HW_QCA6174,
	ATH10K_HW_QCA99X0,
	ATH10K_HW_QCA9377,
};

struct ath10k_hw_regs {
	u32 rtc_state_cold_reset_mask;
	u32 rtc_soc_base_address;
	u32 rtc_wmac_base_address;
	u32 soc_core_base_address;
	u32 ce_wrapper_base_address;
	u32 ce0_base_address;
	u32 ce1_base_address;
	u32 ce2_base_address;
	u32 ce3_base_address;
	u32 ce4_base_address;
	u32 ce5_base_address;
	u32 ce6_base_address;
	u32 ce7_base_address;
	u32 soc_reset_control_si0_rst_mask;
	u32 soc_reset_control_ce_rst_mask;
	u32 soc_chip_id_address;
	u32 scratch_3_address;
	u32 fw_indicator_address;
	u32 pcie_local_base_address;
	u32 ce_wrap_intr_sum_host_msi_lsb;
	u32 ce_wrap_intr_sum_host_msi_mask;
	u32 pcie_intr_fw_mask;
	u32 pcie_intr_ce_mask_all;
	u32 pcie_intr_clr_address;
};

extern const struct ath10k_hw_regs qca988x_regs;
extern const struct ath10k_hw_regs qca6174_regs;
extern const struct ath10k_hw_regs qca99x0_regs;

struct ath10k_hw_values {
	u32 rtc_state_val_on;
	u8 ce_count;
	u8 msi_assign_ce_max;
	u8 num_target_ce_config_wlan;
	u16 ce_desc_meta_data_mask;
	u8 ce_desc_meta_data_lsb;
};

extern const struct ath10k_hw_values qca988x_values;
extern const struct ath10k_hw_values qca6174_values;
extern const struct ath10k_hw_values qca99x0_values;

void ath10k_hw_fill_survey_time(struct ath10k *ar, struct survey_info *survey,
				u32 cc, u32 rcc, u32 cc_prev, u32 rcc_prev);

#define QCA_REV_988X(ar) ((ar)->hw_rev == ATH10K_HW_QCA988X)
#define QCA_REV_6174(ar) ((ar)->hw_rev == ATH10K_HW_QCA6174)
#define QCA_REV_99X0(ar) ((ar)->hw_rev == ATH10K_HW_QCA99X0)
#define QCA_REV_9377(ar) ((ar)->hw_rev == ATH10K_HW_QCA9377)

/* Known pecularities:
 *  - raw appears in nwifi decap, raw and nwifi appear in ethernet decap
 *  - raw have FCS, nwifi doesn't
 *  - ethernet frames have 802.11 header decapped and parts (base hdr, cipher
 *    param, llc/snap) are aligned to 4byte boundaries each */
enum ath10k_hw_txrx_mode {
	ATH10K_HW_TXRX_RAW = 0,

	/* Native Wifi decap mode is used to align IP frames to 4-byte
	 * boundaries and avoid a very expensive re-alignment in mac80211.
	 */
	ATH10K_HW_TXRX_NATIVE_WIFI = 1,
	ATH10K_HW_TXRX_ETHERNET = 2,

	/* Valid for HTT >= 3.0. Used for management frames in TX_FRM. */
	ATH10K_HW_TXRX_MGMT = 3,
};

enum ath10k_mcast2ucast_mode {
	ATH10K_MCAST2UCAST_DISABLED = 0,
	ATH10K_MCAST2UCAST_ENABLED = 1,
};

struct ath10k_pktlog_hdr {
	__le16 flags;
	__le16 missed_cnt;
	__le16 log_type;
	__le16 size;
	__le32 timestamp;
	u8 payload[0];
} __packed;

struct ath10k_pktlog_10_4_hdr {
	__le16 flags;
	__le16 missed_cnt;
	__le16 log_type;
	__le16 size;
	__le32 timestamp;
	__le32 type_specific_data;
	u8 payload[0];
} __packed;

enum ath10k_hw_rate_ofdm {
	ATH10K_HW_RATE_OFDM_48M = 0,
	ATH10K_HW_RATE_OFDM_24M,
	ATH10K_HW_RATE_OFDM_12M,
	ATH10K_HW_RATE_OFDM_6M,
	ATH10K_HW_RATE_OFDM_54M,
	ATH10K_HW_RATE_OFDM_36M,
	ATH10K_HW_RATE_OFDM_18M,
	ATH10K_HW_RATE_OFDM_9M,
};

enum ath10k_hw_rate_cck {
	ATH10K_HW_RATE_CCK_LP_11M = 0,
	ATH10K_HW_RATE_CCK_LP_5_5M,
	ATH10K_HW_RATE_CCK_LP_2M,
	ATH10K_HW_RATE_CCK_LP_1M,
	ATH10K_HW_RATE_CCK_SP_11M,
	ATH10K_HW_RATE_CCK_SP_5_5M,
	ATH10K_HW_RATE_CCK_SP_2M,
};

enum ath10k_hw_4addr_pad {
	ATH10K_HW_4ADDR_PAD_AFTER,
	ATH10K_HW_4ADDR_PAD_BEFORE,
};

/* Target specific defines for MAIN firmware */
#define TARGET_NUM_VDEVS			8
#define TARGET_NUM_PEER_AST			2
#define TARGET_NUM_WDS_ENTRIES			32
#define TARGET_DMA_BURST_SIZE			0
#define TARGET_MAC_AGGR_DELIM			0
#define TARGET_AST_SKID_LIMIT			16
#define TARGET_NUM_STATIONS			16
#define TARGET_NUM_PEERS			((TARGET_NUM_STATIONS) + \
						 (TARGET_NUM_VDEVS))
#define TARGET_NUM_OFFLOAD_PEERS		0
#define TARGET_NUM_OFFLOAD_REORDER_BUFS         0
#define TARGET_NUM_PEER_KEYS			2
#define TARGET_NUM_TIDS				((TARGET_NUM_PEERS) * 2)
#define TARGET_TX_CHAIN_MASK			(BIT(0) | BIT(1) | BIT(2))
#define TARGET_RX_CHAIN_MASK			(BIT(0) | BIT(1) | BIT(2))
#define TARGET_RX_TIMEOUT_LO_PRI		100
#define TARGET_RX_TIMEOUT_HI_PRI		40

#define TARGET_SCAN_MAX_PENDING_REQS		4
#define TARGET_BMISS_OFFLOAD_MAX_VDEV		3
#define TARGET_ROAM_OFFLOAD_MAX_VDEV		3
#define TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES	8
#define TARGET_GTK_OFFLOAD_MAX_VDEV		3
#define TARGET_NUM_MCAST_GROUPS			0
#define TARGET_NUM_MCAST_TABLE_ELEMS		0
#define TARGET_MCAST2UCAST_MODE			ATH10K_MCAST2UCAST_DISABLED
#define TARGET_TX_DBG_LOG_SIZE			1024
#define TARGET_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK 0
#define TARGET_VOW_CONFIG			0
#define TARGET_NUM_MSDU_DESC			(1024 + 400)
#define TARGET_MAX_FRAG_ENTRIES			0

/* Target specific defines for 10.X firmware */
#define TARGET_10X_NUM_VDEVS			16
#define TARGET_10X_NUM_PEER_AST			2
#define TARGET_10X_NUM_WDS_ENTRIES		32
#define TARGET_10X_DMA_BURST_SIZE		0
#define TARGET_10X_MAC_AGGR_DELIM		0
#define TARGET_10X_AST_SKID_LIMIT		128
#define TARGET_10X_NUM_STATIONS			128
#define TARGET_10X_NUM_PEERS			((TARGET_10X_NUM_STATIONS) + \
						 (TARGET_10X_NUM_VDEVS))
#define TARGET_10X_NUM_OFFLOAD_PEERS		0
#define TARGET_10X_NUM_OFFLOAD_REORDER_BUFS	0
#define TARGET_10X_NUM_PEER_KEYS		2
#define TARGET_10X_NUM_TIDS_MAX			256
#define TARGET_10X_NUM_TIDS			min((TARGET_10X_NUM_TIDS_MAX), \
						    (TARGET_10X_NUM_PEERS) * 2)
#define TARGET_10X_TX_CHAIN_MASK		(BIT(0) | BIT(1) | BIT(2))
#define TARGET_10X_RX_CHAIN_MASK		(BIT(0) | BIT(1) | BIT(2))
#define TARGET_10X_RX_TIMEOUT_LO_PRI		100
#define TARGET_10X_RX_TIMEOUT_HI_PRI		40
#define TARGET_10X_SCAN_MAX_PENDING_REQS	4
#define TARGET_10X_BMISS_OFFLOAD_MAX_VDEV	2
#define TARGET_10X_ROAM_OFFLOAD_MAX_VDEV	2
#define TARGET_10X_ROAM_OFFLOAD_MAX_AP_PROFILES	8
#define TARGET_10X_GTK_OFFLOAD_MAX_VDEV		3
#define TARGET_10X_NUM_MCAST_GROUPS		0
#define TARGET_10X_NUM_MCAST_TABLE_ELEMS	0
#define TARGET_10X_MCAST2UCAST_MODE		ATH10K_MCAST2UCAST_DISABLED
#define TARGET_10X_TX_DBG_LOG_SIZE		1024
#define TARGET_10X_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK 1
#define TARGET_10X_VOW_CONFIG			0
#define TARGET_10X_NUM_MSDU_DESC		(1024 + 400)
#define TARGET_10X_MAX_FRAG_ENTRIES		0

/* 10.2 parameters */
#define TARGET_10_2_DMA_BURST_SIZE		0

/* Target specific defines for WMI-TLV firmware */
#define TARGET_TLV_NUM_VDEVS			4
#define TARGET_TLV_NUM_STATIONS			32
#define TARGET_TLV_NUM_PEERS			35
#define TARGET_TLV_NUM_TDLS_VDEVS		1
#define TARGET_TLV_NUM_TIDS			((TARGET_TLV_NUM_PEERS) * 2)
#define TARGET_TLV_NUM_MSDU_DESC		(1024 + 32)
#define TARGET_TLV_NUM_WOW_PATTERNS		22

/* Diagnostic Window */
#define CE_DIAG_PIPE	7

#define NUM_TARGET_CE_CONFIG_WLAN ar->hw_values->num_target_ce_config_wlan

/* Target specific defines for 10.4 firmware */
#define TARGET_10_4_NUM_VDEVS			16
#define TARGET_10_4_NUM_STATIONS		32
#define TARGET_10_4_NUM_PEERS			((TARGET_10_4_NUM_STATIONS) + \
						 (TARGET_10_4_NUM_VDEVS))
#define TARGET_10_4_ACTIVE_PEERS		0

#define TARGET_10_4_NUM_QCACHE_PEERS_MAX	512
#define TARGET_10_4_QCACHE_ACTIVE_PEERS		50
#define TARGET_10_4_NUM_OFFLOAD_PEERS		0
#define TARGET_10_4_NUM_OFFLOAD_REORDER_BUFFS	0
#define TARGET_10_4_NUM_PEER_KEYS		2
#define TARGET_10_4_TGT_NUM_TIDS		((TARGET_10_4_NUM_PEERS) * 2)
#define TARGET_10_4_AST_SKID_LIMIT		32
#define TARGET_10_4_TX_CHAIN_MASK		(BIT(0) | BIT(1) | \
						 BIT(2) | BIT(3))
#define TARGET_10_4_RX_CHAIN_MASK		(BIT(0) | BIT(1) | \
						 BIT(2) | BIT(3))

/* 100 ms for video, best-effort, and background */
#define TARGET_10_4_RX_TIMEOUT_LO_PRI		100

/* 40 ms for voice */
#define TARGET_10_4_RX_TIMEOUT_HI_PRI		40

#define TARGET_10_4_RX_DECAP_MODE		ATH10K_HW_TXRX_NATIVE_WIFI
#define TARGET_10_4_SCAN_MAX_REQS		4
#define TARGET_10_4_BMISS_OFFLOAD_MAX_VDEV	3
#define TARGET_10_4_ROAM_OFFLOAD_MAX_VDEV	3
#define TARGET_10_4_ROAM_OFFLOAD_MAX_PROFILES   8

/* Note: mcast to ucast is disabled by default */
#define TARGET_10_4_NUM_MCAST_GROUPS		0
#define TARGET_10_4_NUM_MCAST_TABLE_ELEMS	0
#define TARGET_10_4_MCAST2UCAST_MODE		0

#define TARGET_10_4_TX_DBG_LOG_SIZE		1024
#define TARGET_10_4_NUM_WDS_ENTRIES		32
#define TARGET_10_4_DMA_BURST_SIZE		0
#define TARGET_10_4_MAC_AGGR_DELIM		0
#define TARGET_10_4_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK 1
#define TARGET_10_4_VOW_CONFIG			0
#define TARGET_10_4_GTK_OFFLOAD_MAX_VDEV	3
#define TARGET_10_4_NUM_MSDU_DESC		(1024 + 400)
#define TARGET_10_4_11AC_TX_MAX_FRAGS		2
#define TARGET_10_4_MAX_PEER_EXT_STATS		16
#define TARGET_10_4_SMART_ANT_CAP		0
#define TARGET_10_4_BK_MIN_FREE			0
#define TARGET_10_4_BE_MIN_FREE			0
#define TARGET_10_4_VI_MIN_FREE			0
#define TARGET_10_4_VO_MIN_FREE			0
#define TARGET_10_4_RX_BATCH_MODE		1
#define TARGET_10_4_THERMAL_THROTTLING_CONFIG	0
#define TARGET_10_4_ATF_CONFIG			0
#define TARGET_10_4_IPHDR_PAD_CONFIG		1
#define TARGET_10_4_QWRAP_CONFIG		0

/* Number of Copy Engines supported */
#define CE_COUNT ar->hw_values->ce_count

/*
 * Granted MSIs are assigned as follows:
 * Firmware uses the first
 * Remaining MSIs, if any, are used by Copy Engines
 * This mapping is known to both Target firmware and Host software.
 * It may be changed as long as Host and Target are kept in sync.
 */
/* MSI for firmware (errors, etc.) */
#define MSI_ASSIGN_FW		0

/* MSIs for Copy Engines */
#define MSI_ASSIGN_CE_INITIAL	1
#define MSI_ASSIGN_CE_MAX	ar->hw_values->msi_assign_ce_max

/* as of IP3.7.1 */
#define RTC_STATE_V_ON				ar->hw_values->rtc_state_val_on

#define RTC_STATE_COLD_RESET_MASK		ar->regs->rtc_state_cold_reset_mask
#define RTC_STATE_V_LSB				0
#define RTC_STATE_V_MASK			0x00000007
#define RTC_STATE_ADDRESS			0x0000
#define PCIE_SOC_WAKE_V_MASK			0x00000001
#define PCIE_SOC_WAKE_ADDRESS			0x0004
#define PCIE_SOC_WAKE_RESET			0x00000000
#define SOC_GLOBAL_RESET_ADDRESS		0x0008

#define RTC_SOC_BASE_ADDRESS			ar->regs->rtc_soc_base_address
#define RTC_WMAC_BASE_ADDRESS			ar->regs->rtc_wmac_base_address
#define MAC_COEX_BASE_ADDRESS			0x00006000
#define BT_COEX_BASE_ADDRESS			0x00007000
#define SOC_PCIE_BASE_ADDRESS			0x00008000
#define SOC_CORE_BASE_ADDRESS			ar->regs->soc_core_base_address
#define WLAN_UART_BASE_ADDRESS			0x0000c000
#define WLAN_SI_BASE_ADDRESS			0x00010000
#define WLAN_GPIO_BASE_ADDRESS			0x00014000
#define WLAN_ANALOG_INTF_BASE_ADDRESS		0x0001c000
#define WLAN_MAC_BASE_ADDRESS			0x00020000
#define EFUSE_BASE_ADDRESS			0x00030000
#define FPGA_REG_BASE_ADDRESS			0x00039000
#define WLAN_UART2_BASE_ADDRESS			0x00054c00
#define CE_WRAPPER_BASE_ADDRESS			ar->regs->ce_wrapper_base_address
#define CE0_BASE_ADDRESS			ar->regs->ce0_base_address
#define CE1_BASE_ADDRESS			ar->regs->ce1_base_address
#define CE2_BASE_ADDRESS			ar->regs->ce2_base_address
#define CE3_BASE_ADDRESS			ar->regs->ce3_base_address
#define CE4_BASE_ADDRESS			ar->regs->ce4_base_address
#define CE5_BASE_ADDRESS			ar->regs->ce5_base_address
#define CE6_BASE_ADDRESS			ar->regs->ce6_base_address
#define CE7_BASE_ADDRESS			ar->regs->ce7_base_address
#define DBI_BASE_ADDRESS			0x00060000
#define WLAN_ANALOG_INTF_PCIE_BASE_ADDRESS	0x0006c000
#define PCIE_LOCAL_BASE_ADDRESS		ar->regs->pcie_local_base_address

#define SOC_RESET_CONTROL_ADDRESS		0x00000000
#define SOC_RESET_CONTROL_OFFSET		0x00000000
#define SOC_RESET_CONTROL_SI0_RST_MASK		ar->regs->soc_reset_control_si0_rst_mask
#define SOC_RESET_CONTROL_CE_RST_MASK		ar->regs->soc_reset_control_ce_rst_mask
#define SOC_RESET_CONTROL_CPU_WARM_RST_MASK	0x00000040
#define SOC_CPU_CLOCK_OFFSET			0x00000020
#define SOC_CPU_CLOCK_STANDARD_LSB		0
#define SOC_CPU_CLOCK_STANDARD_MASK		0x00000003
#define SOC_CLOCK_CONTROL_OFFSET		0x00000028
#define SOC_CLOCK_CONTROL_SI0_CLK_MASK		0x00000001
#define SOC_SYSTEM_SLEEP_OFFSET			0x000000c4
#define SOC_LPO_CAL_OFFSET			0x000000e0
#define SOC_LPO_CAL_ENABLE_LSB			20
#define SOC_LPO_CAL_ENABLE_MASK			0x00100000
#define SOC_LF_TIMER_CONTROL0_ADDRESS		0x00000050
#define SOC_LF_TIMER_CONTROL0_ENABLE_MASK	0x00000004

#define SOC_CHIP_ID_ADDRESS			ar->regs->soc_chip_id_address
#define SOC_CHIP_ID_REV_LSB			8
#define SOC_CHIP_ID_REV_MASK			0x00000f00

#define WLAN_RESET_CONTROL_COLD_RST_MASK	0x00000008
#define WLAN_RESET_CONTROL_WARM_RST_MASK	0x00000004
#define WLAN_SYSTEM_SLEEP_DISABLE_LSB		0
#define WLAN_SYSTEM_SLEEP_DISABLE_MASK		0x00000001

#define WLAN_GPIO_PIN0_ADDRESS			0x00000028
#define WLAN_GPIO_PIN0_CONFIG_MASK		0x00007800
#define WLAN_GPIO_PIN1_ADDRESS			0x0000002c
#define WLAN_GPIO_PIN1_CONFIG_MASK		0x00007800
#define WLAN_GPIO_PIN10_ADDRESS			0x00000050
#define WLAN_GPIO_PIN11_ADDRESS			0x00000054
#define WLAN_GPIO_PIN12_ADDRESS			0x00000058
#define WLAN_GPIO_PIN13_ADDRESS			0x0000005c

#define CLOCK_GPIO_OFFSET			0xffffffff
#define CLOCK_GPIO_BT_CLK_OUT_EN_LSB		0
#define CLOCK_GPIO_BT_CLK_OUT_EN_MASK		0

#define SI_CONFIG_OFFSET			0x00000000
#define SI_CONFIG_BIDIR_OD_DATA_LSB		18
#define SI_CONFIG_BIDIR_OD_DATA_MASK		0x00040000
#define SI_CONFIG_I2C_LSB			16
#define SI_CONFIG_I2C_MASK			0x00010000
#define SI_CONFIG_POS_SAMPLE_LSB		7
#define SI_CONFIG_POS_SAMPLE_MASK		0x00000080
#define SI_CONFIG_INACTIVE_DATA_LSB		5
#define SI_CONFIG_INACTIVE_DATA_MASK		0x00000020
#define SI_CONFIG_INACTIVE_CLK_LSB		4
#define SI_CONFIG_INACTIVE_CLK_MASK		0x00000010
#define SI_CONFIG_DIVIDER_LSB			0
#define SI_CONFIG_DIVIDER_MASK			0x0000000f
#define SI_CS_OFFSET				0x00000004
#define SI_CS_DONE_ERR_MASK			0x00000400
#define SI_CS_DONE_INT_MASK			0x00000200
#define SI_CS_START_LSB				8
#define SI_CS_START_MASK			0x00000100
#define SI_CS_RX_CNT_LSB			4
#define SI_CS_RX_CNT_MASK			0x000000f0
#define SI_CS_TX_CNT_LSB			0
#define SI_CS_TX_CNT_MASK			0x0000000f

#define SI_TX_DATA0_OFFSET			0x00000008
#define SI_TX_DATA1_OFFSET			0x0000000c
#define SI_RX_DATA0_OFFSET			0x00000010
#define SI_RX_DATA1_OFFSET			0x00000014

#define CORE_CTRL_CPU_INTR_MASK			0x00002000
#define CORE_CTRL_PCIE_REG_31_MASK		0x00000800
#define CORE_CTRL_ADDRESS			0x0000
#define PCIE_INTR_ENABLE_ADDRESS		0x0008
#define PCIE_INTR_CAUSE_ADDRESS			0x000c
#define PCIE_INTR_CLR_ADDRESS			ar->regs->pcie_intr_clr_address
#define SCRATCH_3_ADDRESS			ar->regs->scratch_3_address
#define CPU_INTR_ADDRESS			0x0010

#define CCNT_TO_MSEC(ar, x) ((x) / ar->hw_params.channel_counters_freq_hz)

/* Firmware indications to the Host via SCRATCH_3 register. */
#define FW_INDICATOR_ADDRESS			ar->regs->fw_indicator_address
#define FW_IND_EVENT_PENDING			1
#define FW_IND_INITIALIZED			2

/* HOST_REG interrupt from firmware */
#define PCIE_INTR_FIRMWARE_MASK			ar->regs->pcie_intr_fw_mask
#define PCIE_INTR_CE_MASK_ALL			ar->regs->pcie_intr_ce_mask_all

#define DRAM_BASE_ADDRESS			0x00400000

#define PCIE_BAR_REG_ADDRESS			0x40030

#define MISSING 0

#define SYSTEM_SLEEP_OFFSET			SOC_SYSTEM_SLEEP_OFFSET
#define WLAN_SYSTEM_SLEEP_OFFSET		SOC_SYSTEM_SLEEP_OFFSET
#define WLAN_RESET_CONTROL_OFFSET		SOC_RESET_CONTROL_OFFSET
#define CLOCK_CONTROL_OFFSET			SOC_CLOCK_CONTROL_OFFSET
#define CLOCK_CONTROL_SI0_CLK_MASK		SOC_CLOCK_CONTROL_SI0_CLK_MASK
#define RESET_CONTROL_MBOX_RST_MASK		MISSING
#define RESET_CONTROL_SI0_RST_MASK		SOC_RESET_CONTROL_SI0_RST_MASK
#define GPIO_BASE_ADDRESS			WLAN_GPIO_BASE_ADDRESS
#define GPIO_PIN0_OFFSET			WLAN_GPIO_PIN0_ADDRESS
#define GPIO_PIN1_OFFSET			WLAN_GPIO_PIN1_ADDRESS
#define GPIO_PIN0_CONFIG_MASK			WLAN_GPIO_PIN0_CONFIG_MASK
#define GPIO_PIN1_CONFIG_MASK			WLAN_GPIO_PIN1_CONFIG_MASK
#define SI_BASE_ADDRESS				WLAN_SI_BASE_ADDRESS
#define SCRATCH_BASE_ADDRESS			SOC_CORE_BASE_ADDRESS
#define LOCAL_SCRATCH_OFFSET			0x18
#define CPU_CLOCK_OFFSET			SOC_CPU_CLOCK_OFFSET
#define LPO_CAL_OFFSET				SOC_LPO_CAL_OFFSET
#define GPIO_PIN10_OFFSET			WLAN_GPIO_PIN10_ADDRESS
#define GPIO_PIN11_OFFSET			WLAN_GPIO_PIN11_ADDRESS
#define GPIO_PIN12_OFFSET			WLAN_GPIO_PIN12_ADDRESS
#define GPIO_PIN13_OFFSET			WLAN_GPIO_PIN13_ADDRESS
#define CPU_CLOCK_STANDARD_LSB			SOC_CPU_CLOCK_STANDARD_LSB
#define CPU_CLOCK_STANDARD_MASK			SOC_CPU_CLOCK_STANDARD_MASK
#define LPO_CAL_ENABLE_LSB			SOC_LPO_CAL_ENABLE_LSB
#define LPO_CAL_ENABLE_MASK			SOC_LPO_CAL_ENABLE_MASK
#define ANALOG_INTF_BASE_ADDRESS		WLAN_ANALOG_INTF_BASE_ADDRESS
#define MBOX_BASE_ADDRESS			MISSING
#define INT_STATUS_ENABLE_ERROR_LSB		MISSING
#define INT_STATUS_ENABLE_ERROR_MASK		MISSING
#define INT_STATUS_ENABLE_CPU_LSB		MISSING
#define INT_STATUS_ENABLE_CPU_MASK		MISSING
#define INT_STATUS_ENABLE_COUNTER_LSB		MISSING
#define INT_STATUS_ENABLE_COUNTER_MASK		MISSING
#define INT_STATUS_ENABLE_MBOX_DATA_LSB		MISSING
#define INT_STATUS_ENABLE_MBOX_DATA_MASK	MISSING
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_LSB	MISSING
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_MASK	MISSING
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_LSB	MISSING
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_MASK	MISSING
#define COUNTER_INT_STATUS_ENABLE_BIT_LSB	MISSING
#define COUNTER_INT_STATUS_ENABLE_BIT_MASK	MISSING
#define INT_STATUS_ENABLE_ADDRESS		MISSING
#define CPU_INT_STATUS_ENABLE_BIT_LSB		MISSING
#define CPU_INT_STATUS_ENABLE_BIT_MASK		MISSING
#define HOST_INT_STATUS_ADDRESS			MISSING
#define CPU_INT_STATUS_ADDRESS			MISSING
#define ERROR_INT_STATUS_ADDRESS		MISSING
#define ERROR_INT_STATUS_WAKEUP_MASK		MISSING
#define ERROR_INT_STATUS_WAKEUP_LSB		MISSING
#define ERROR_INT_STATUS_RX_UNDERFLOW_MASK	MISSING
#define ERROR_INT_STATUS_RX_UNDERFLOW_LSB	MISSING
#define ERROR_INT_STATUS_TX_OVERFLOW_MASK	MISSING
#define ERROR_INT_STATUS_TX_OVERFLOW_LSB	MISSING
#define COUNT_DEC_ADDRESS			MISSING
#define HOST_INT_STATUS_CPU_MASK		MISSING
#define HOST_INT_STATUS_CPU_LSB			MISSING
#define HOST_INT_STATUS_ERROR_MASK		MISSING
#define HOST_INT_STATUS_ERROR_LSB		MISSING
#define HOST_INT_STATUS_COUNTER_MASK		MISSING
#define HOST_INT_STATUS_COUNTER_LSB		MISSING
#define RX_LOOKAHEAD_VALID_ADDRESS		MISSING
#define WINDOW_DATA_ADDRESS			MISSING
#define WINDOW_READ_ADDR_ADDRESS		MISSING
#define WINDOW_WRITE_ADDR_ADDRESS		MISSING

#define RTC_STATE_V_GET(x) (((x) & RTC_STATE_V_MASK) >> RTC_STATE_V_LSB)

#endif /* _HW_H_ */
