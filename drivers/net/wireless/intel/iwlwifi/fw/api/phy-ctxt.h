/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018, 2020-2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_phy_ctxt_h__
#define __iwl_fw_api_phy_ctxt_h__

/* Supported bands */
#define PHY_BAND_5  (0)
#define PHY_BAND_24 (1)
#define PHY_BAND_6 (2)

/* Supported channel width, vary if there is VHT support */
#define IWL_PHY_CHANNEL_MODE20	0x0
#define IWL_PHY_CHANNEL_MODE40	0x1
#define IWL_PHY_CHANNEL_MODE80	0x2
#define IWL_PHY_CHANNEL_MODE160	0x3
/* and 320 MHz for EHT */
#define IWL_PHY_CHANNEL_MODE320	0x4

/*
 * Control channel position:
 * For legacy set bit means upper channel, otherwise lower.
 * For VHT - bit-2 marks if the control is lower/upper relative to center-freq
 *   bits-1:0 mark the distance from the center freq. for 20Mhz, offset is 0.
 * For EHT - bit-3 is used for extended distance
 *                                           center_freq
 *                                                |
 * 40Mhz                                     |____|____|
 * 80Mhz                                |____|____|____|____|
 * 160Mhz                     |____|____|____|____|____|____|____|____|
 * 320MHz |____|____|____|____|____|____|____|____|____|____|____|____|____|____|____|____|
 * code    1011 1010 1001 1000 0011 0010 0001 0000 0100 0101 0110 0111 1100 1101 1110 1111
 */
#define IWL_PHY_CTRL_POS_ABOVE		0x4
#define IWL_PHY_CTRL_POS_OFFS_EXT	0x8
#define IWL_PHY_CTRL_POS_OFFS_MSK	0x3

/*
 * struct iwl_fw_channel_info_v1 - channel information
 *
 * @band: PHY_BAND_*
 * @channel: channel number
 * @width: PHY_[VHT|LEGACY]_CHANNEL_*
 * @ctrl channel: PHY_[VHT|LEGACY]_CTRL_*
 */
struct iwl_fw_channel_info_v1 {
	u8 band;
	u8 channel;
	u8 width;
	u8 ctrl_pos;
} __packed; /* CHANNEL_CONFIG_API_S_VER_1 */

/*
 * struct iwl_fw_channel_info - channel information
 *
 * @channel: channel number
 * @band: PHY_BAND_*
 * @width: PHY_[VHT|LEGACY]_CHANNEL_*
 * @ctrl channel: PHY_[VHT|LEGACY]_CTRL_*
 * @reserved: for future use and alignment
 */
struct iwl_fw_channel_info {
	__le32 channel;
	u8 band;
	u8 width;
	u8 ctrl_pos;
	u8 reserved;
} __packed; /*CHANNEL_CONFIG_API_S_VER_2 */

#define PHY_RX_CHAIN_DRIVER_FORCE_POS	(0)
#define PHY_RX_CHAIN_DRIVER_FORCE_MSK \
	(0x1 << PHY_RX_CHAIN_DRIVER_FORCE_POS)
#define PHY_RX_CHAIN_VALID_POS		(1)
#define PHY_RX_CHAIN_VALID_MSK \
	(0x7 << PHY_RX_CHAIN_VALID_POS)
#define PHY_RX_CHAIN_FORCE_SEL_POS	(4)
#define PHY_RX_CHAIN_FORCE_SEL_MSK \
	(0x7 << PHY_RX_CHAIN_FORCE_SEL_POS)
#define PHY_RX_CHAIN_FORCE_MIMO_SEL_POS	(7)
#define PHY_RX_CHAIN_FORCE_MIMO_SEL_MSK \
	(0x7 << PHY_RX_CHAIN_FORCE_MIMO_SEL_POS)
#define PHY_RX_CHAIN_CNT_POS		(10)
#define PHY_RX_CHAIN_CNT_MSK \
	(0x3 << PHY_RX_CHAIN_CNT_POS)
#define PHY_RX_CHAIN_MIMO_CNT_POS	(12)
#define PHY_RX_CHAIN_MIMO_CNT_MSK \
	(0x3 << PHY_RX_CHAIN_MIMO_CNT_POS)
#define PHY_RX_CHAIN_MIMO_FORCE_POS	(14)
#define PHY_RX_CHAIN_MIMO_FORCE_MSK \
	(0x1 << PHY_RX_CHAIN_MIMO_FORCE_POS)

/* TODO: fix the value, make it depend on firmware at runtime? */
#define NUM_PHY_CTX	3

/* TODO: complete missing documentation */
/**
 * struct iwl_phy_context_cmd_tail - tail of iwl_phy_ctx_cmd for alignment with
 *	various channel structures.
 *
 * @txchain_info: ???
 * @rxchain_info: ???
 * @acquisition_data: ???
 * @dsp_cfg_flags: set to 0
 */
struct iwl_phy_context_cmd_tail {
	__le32 txchain_info;
	__le32 rxchain_info;
	__le32 acquisition_data;
	__le32 dsp_cfg_flags;
} __packed;

/**
 * struct iwl_phy_context_cmd_v1 - config of the PHY context
 * ( PHY_CONTEXT_CMD = 0x8 )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, see &enum iwl_ctxt_action
 * @apply_time: 0 means immediate apply and context switch.
 *	other value means apply new params after X usecs
 * @tx_param_color: ???
 * @ci: channel info
 * @tail: command tail
 */
struct iwl_phy_context_cmd_v1 {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	/* PHY_CONTEXT_DATA_API_S_VER_3 */
	__le32 apply_time;
	__le32 tx_param_color;
	struct iwl_fw_channel_info ci;
	struct iwl_phy_context_cmd_tail tail;
} __packed; /* PHY_CONTEXT_CMD_API_VER_1 */

/**
 * struct iwl_phy_context_cmd - config of the PHY context
 * ( PHY_CONTEXT_CMD = 0x8 )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, see &enum iwl_ctxt_action
 * @lmac_id: the lmac id the phy context belongs to
 * @ci: channel info
 * @rxchain_info: ???
 * @sbb_bandwidth: 0 disabled, 1 - 40Mhz ... 4 - 320MHz
 * @sbb_ctrl_channel_loc: location of the control channel
 * @puncture_mask: bitmap of punctured subchannels
 * @dsp_cfg_flags: set to 0
 * @reserved: reserved to align to 64 bit
 */
struct iwl_phy_context_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	/* PHY_CONTEXT_DATA_API_S_VER_3, PHY_CONTEXT_DATA_API_S_VER_4 */
	struct iwl_fw_channel_info ci;
	__le32 lmac_id;
	union {
		__le32 rxchain_info; /* reserved in _VER_4 */
		struct {             /* used for _VER_5/_VER_6 */
			u8 sbb_bandwidth;
			u8 sbb_ctrl_channel_loc;
			__le16 puncture_mask; /* added in VER_6 */
		};
	};
	__le32 dsp_cfg_flags;
	__le32 reserved;
} __packed; /* PHY_CONTEXT_CMD_API_VER_3,
	     * PHY_CONTEXT_CMD_API_VER_4,
	     * PHY_CONTEXT_CMD_API_VER_5,
	     * PHY_CONTEXT_CMD_API_VER_6
	     */

#endif /* __iwl_fw_api_phy_ctxt_h__ */
