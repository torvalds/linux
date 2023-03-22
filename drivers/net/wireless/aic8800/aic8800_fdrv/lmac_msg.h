/**
 ****************************************************************************************
 *
 * @file lmac_msg.h
 *
 * @brief Main definitions for message exchanges with LMAC
 *
 * Copyright (C) RivieraWaves 2011-2019
 *
 ****************************************************************************************
 */

#ifndef LMAC_MSG_H_
#define LMAC_MSG_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
// for MAC related elements (mac_addr, mac_ssid...)
#include "lmac_mac.h"

/*
 ****************************************************************************************
 */
/////////////////////////////////////////////////////////////////////////////////
// COMMUNICATION WITH LMAC LAYER
/////////////////////////////////////////////////////////////////////////////////
/* Task identifiers for communication between LMAC and DRIVER */
enum
{
    TASK_NONE = (u8_l) -1,

    // MAC Management task.
    TASK_MM = 0,
    // DEBUG task
    TASK_DBG,
    /// SCAN task
    TASK_SCAN,
    /// TDLS task
    TASK_TDLS,
    /// SCANU task
    TASK_SCANU,
    /// ME task
    TASK_ME,
    /// SM task
    TASK_SM,
    /// APM task
    TASK_APM,
    /// BAM task
    TASK_BAM,
    /// MESH task
    TASK_MESH,
    /// RXU task
    TASK_RXU,
    /// RM task
    TASK_RM,
#if defined CONFIG_RWNX_FULLMAC || defined CONFIG_RWNX_FHOST
    // This is used to define the last task that is running on the EMB processor
    TASK_LAST_EMB = TASK_RM,
#else
#error "Need to define SOFTMAC or FULLMAC"
#endif
    // nX API task
    TASK_API,
    TASK_MAX,
};


/// For MAC HW States copied from "hal_machw.h"
enum
{
    /// MAC HW IDLE State.
    HW_IDLE = 0,
    /// MAC HW RESERVED State.
    HW_RESERVED,
    /// MAC HW DOZE State.
    HW_DOZE,
    /// MAC HW ACTIVE State.
    HW_ACTIVE
};

/// Power Save mode setting
enum mm_ps_mode_state
{
    MM_PS_MODE_OFF,
    MM_PS_MODE_ON,
    MM_PS_MODE_ON_DYN,
};

/// Status/error codes used in the MAC software.
enum
{
    CO_OK,
    CO_FAIL,
    CO_EMPTY,
    CO_FULL,
    CO_BAD_PARAM,
    CO_NOT_FOUND,
    CO_NO_MORE_ELT_AVAILABLE,
    CO_NO_ELT_IN_USE,
    CO_BUSY,
    CO_OP_IN_PROGRESS,
};

/// Remain on channel operation codes
enum mm_remain_on_channel_op
{
    MM_ROC_OP_START = 0,
    MM_ROC_OP_CANCEL,
};

#define DRV_TASK_ID 100

/// Message Identifier. The number of messages is limited to 0xFFFF.
/// The message ID is divided in two parts:
/// - bits[15..10] : task index (no more than 64 tasks supported).
/// - bits[9..0] : message index (no more that 1024 messages per task).
typedef u16 lmac_msg_id_t;

typedef u16 lmac_task_id_t;

/// Build the first message ID of a task.
#define LMAC_FIRST_MSG(task) ((lmac_msg_id_t)((task) << 10))

#define MSG_T(msg) ((lmac_task_id_t)((msg) >> 10))
#define MSG_I(msg) ((msg) & ((1<<10)-1))

/// Message structure.
struct lmac_msg
{
    lmac_msg_id_t     id;         ///< Message id.
    lmac_task_id_t    dest_id;    ///< Destination kernel identifier.
    lmac_task_id_t    src_id;     ///< Source kernel identifier.
    u16        param_len;  ///< Parameter embedded struct length.
    u32        param[];   ///< Parameter embedded struct. Must be word-aligned.
};

/// List of messages related to the task.
enum mm_msg_tag
{
    /// RESET Request.
    MM_RESET_REQ = LMAC_FIRST_MSG(TASK_MM),
    /// RESET Confirmation.
    MM_RESET_CFM,
    /// START Request.
    MM_START_REQ,
    /// START Confirmation.
    MM_START_CFM,
    /// Read Version Request.
    MM_VERSION_REQ,
    /// Read Version Confirmation.
    MM_VERSION_CFM,
    /// ADD INTERFACE Request.
    MM_ADD_IF_REQ,
    /// ADD INTERFACE Confirmation.
    MM_ADD_IF_CFM,
    /// REMOVE INTERFACE Request.
    MM_REMOVE_IF_REQ,
    /// REMOVE INTERFACE Confirmation.
    MM_REMOVE_IF_CFM,
    /// STA ADD Request.
    MM_STA_ADD_REQ,
    /// STA ADD Confirm.
    MM_STA_ADD_CFM,
    /// STA DEL Request.
    MM_STA_DEL_REQ,
    /// STA DEL Confirm.
    MM_STA_DEL_CFM,
    /// RX FILTER CONFIGURATION Request.
    MM_SET_FILTER_REQ,
    /// RX FILTER CONFIGURATION Confirmation.
    MM_SET_FILTER_CFM,
    /// CHANNEL CONFIGURATION Request.
    MM_SET_CHANNEL_REQ,
    /// CHANNEL CONFIGURATION Confirmation.
    MM_SET_CHANNEL_CFM,
    /// DTIM PERIOD CONFIGURATION Request.
    MM_SET_DTIM_REQ,
    /// DTIM PERIOD CONFIGURATION Confirmation.
    MM_SET_DTIM_CFM,
    /// BEACON INTERVAL CONFIGURATION Request.
    MM_SET_BEACON_INT_REQ,
    /// BEACON INTERVAL CONFIGURATION Confirmation.
    MM_SET_BEACON_INT_CFM,
    /// BASIC RATES CONFIGURATION Request.
    MM_SET_BASIC_RATES_REQ,
    /// BASIC RATES CONFIGURATION Confirmation.
    MM_SET_BASIC_RATES_CFM,
    /// BSSID CONFIGURATION Request.
    MM_SET_BSSID_REQ,
    /// BSSID CONFIGURATION Confirmation.
    MM_SET_BSSID_CFM,
    /// EDCA PARAMETERS CONFIGURATION Request.
    MM_SET_EDCA_REQ,
    /// EDCA PARAMETERS CONFIGURATION Confirmation.
    MM_SET_EDCA_CFM,
    /// ABGN MODE CONFIGURATION Request.
    MM_SET_MODE_REQ,
    /// ABGN MODE CONFIGURATION Confirmation.
    MM_SET_MODE_CFM,
    /// Request setting the VIF active state (i.e associated or AP started)
    MM_SET_VIF_STATE_REQ,
    /// Confirmation of the @ref MM_SET_VIF_STATE_REQ message.
    MM_SET_VIF_STATE_CFM,
    /// SLOT TIME PARAMETERS CONFIGURATION Request.
    MM_SET_SLOTTIME_REQ,
    /// SLOT TIME PARAMETERS CONFIGURATION Confirmation.
    MM_SET_SLOTTIME_CFM,
    /// Power Mode Change Request.
    MM_SET_IDLE_REQ,
    /// Power Mode Change Confirm.
    MM_SET_IDLE_CFM,
    /// KEY ADD Request.
    MM_KEY_ADD_REQ,
    /// KEY ADD Confirm.
    MM_KEY_ADD_CFM,
    /// KEY DEL Request.
    MM_KEY_DEL_REQ,
    /// KEY DEL Confirm.
    MM_KEY_DEL_CFM,
    /// Block Ack agreement info addition
    MM_BA_ADD_REQ,
    /// Block Ack agreement info addition confirmation
    MM_BA_ADD_CFM,
    /// Block Ack agreement info deletion
    MM_BA_DEL_REQ,
    /// Block Ack agreement info deletion confirmation
    MM_BA_DEL_CFM,
    /// Indication of the primary TBTT to the upper MAC. Upon the reception of this
    // message the upper MAC has to push the beacon(s) to the beacon transmission queue.
    MM_PRIMARY_TBTT_IND,
    /// Indication of the secondary TBTT to the upper MAC. Upon the reception of this
    // message the upper MAC has to push the beacon(s) to the beacon transmission queue.
    MM_SECONDARY_TBTT_IND,
    /// Request for changing the TX power
    MM_SET_POWER_REQ,
    /// Confirmation of the TX power change
    MM_SET_POWER_CFM,
    /// Request to the LMAC to trigger the embedded logic analyzer and forward the debug
    /// dump.
    MM_DBG_TRIGGER_REQ,
    /// Set Power Save mode
    MM_SET_PS_MODE_REQ,
    /// Set Power Save mode confirmation
    MM_SET_PS_MODE_CFM,
    /// Request to add a channel context
    MM_CHAN_CTXT_ADD_REQ,
    /// Confirmation of the channel context addition
    MM_CHAN_CTXT_ADD_CFM,
    /// Request to delete a channel context
    MM_CHAN_CTXT_DEL_REQ,
    /// Confirmation of the channel context deletion
    MM_CHAN_CTXT_DEL_CFM,
    /// Request to link a channel context to a VIF
    MM_CHAN_CTXT_LINK_REQ,
    /// Confirmation of the channel context link
    MM_CHAN_CTXT_LINK_CFM,
    /// Request to unlink a channel context from a VIF
    MM_CHAN_CTXT_UNLINK_REQ,
    /// Confirmation of the channel context unlink
    MM_CHAN_CTXT_UNLINK_CFM,
    /// Request to update a channel context
    MM_CHAN_CTXT_UPDATE_REQ,
    /// Confirmation of the channel context update
    MM_CHAN_CTXT_UPDATE_CFM,
    /// Request to schedule a channel context
    MM_CHAN_CTXT_SCHED_REQ,
    /// Confirmation of the channel context scheduling
    MM_CHAN_CTXT_SCHED_CFM,
    /// Request to change the beacon template in LMAC
    MM_BCN_CHANGE_REQ,
    /// Confirmation of the beacon change
    MM_BCN_CHANGE_CFM,
    /// Request to update the TIM in the beacon (i.e to indicate traffic bufferized at AP)
    MM_TIM_UPDATE_REQ,
    /// Confirmation of the TIM update
    MM_TIM_UPDATE_CFM,
    /// Connection loss indication
    MM_CONNECTION_LOSS_IND,
    /// Channel context switch indication to the upper layers
    MM_CHANNEL_SWITCH_IND,
    /// Channel context pre-switch indication to the upper layers
    MM_CHANNEL_PRE_SWITCH_IND,
    /// Request to remain on channel or cancel remain on channel
    MM_REMAIN_ON_CHANNEL_REQ,
    /// Confirmation of the (cancel) remain on channel request
    MM_REMAIN_ON_CHANNEL_CFM,
    /// Remain on channel expired indication
    MM_REMAIN_ON_CHANNEL_EXP_IND,
    /// Indication of a PS state change of a peer device
    MM_PS_CHANGE_IND,
    /// Indication that some buffered traffic should be sent to the peer device
    MM_TRAFFIC_REQ_IND,
    /// Request to modify the STA Power-save mode options
    MM_SET_PS_OPTIONS_REQ,
    /// Confirmation of the PS options setting
    MM_SET_PS_OPTIONS_CFM,
    /// Indication of PS state change for a P2P VIF
    MM_P2P_VIF_PS_CHANGE_IND,
    /// Indication that CSA counter has been updated
    MM_CSA_COUNTER_IND,
    /// Channel occupation report indication
    MM_CHANNEL_SURVEY_IND,
    /// Message containing Beamformer Information
    MM_BFMER_ENABLE_REQ,
    /// Request to Start/Stop/Update NOA - GO Only
    MM_SET_P2P_NOA_REQ,
    /// Request to Start/Stop/Update Opportunistic PS - GO Only
    MM_SET_P2P_OPPPS_REQ,
    /// Start/Stop/Update NOA Confirmation
    MM_SET_P2P_NOA_CFM,
    /// Start/Stop/Update Opportunistic PS Confirmation
    MM_SET_P2P_OPPPS_CFM,
    /// P2P NoA Update Indication - GO Only
    MM_P2P_NOA_UPD_IND,
    /// Request to set RSSI threshold and RSSI hysteresis
    MM_CFG_RSSI_REQ,
    /// Indication that RSSI level is below or above the threshold
    MM_RSSI_STATUS_IND,
    /// Indication that CSA is done
    MM_CSA_FINISH_IND,
    /// Indication that CSA is in prorgess (resp. done) and traffic must be stopped (resp. restarted)
    MM_CSA_TRAFFIC_IND,
    /// Request to update the group information of a station
    MM_MU_GROUP_UPDATE_REQ,
    /// Confirmation of the @ref MM_MU_GROUP_UPDATE_REQ message
    MM_MU_GROUP_UPDATE_CFM,
    /// Request to initialize the antenna diversity algorithm
    MM_ANT_DIV_INIT_REQ,
    /// Request to stop the antenna diversity algorithm
    MM_ANT_DIV_STOP_REQ,
    /// Request to update the antenna switch status
    MM_ANT_DIV_UPDATE_REQ,
    /// Request to switch the antenna connected to path_0
    MM_SWITCH_ANTENNA_REQ,
    /// Indication that a packet loss has occurred
    MM_PKTLOSS_IND,

    MM_SET_ARPOFFLOAD_REQ,
    MM_SET_ARPOFFLOAD_CFM,
    MM_SET_AGG_DISABLE_REQ,
    MM_SET_AGG_DISABLE_CFM,
    MM_SET_COEX_REQ,
    MM_SET_COEX_CFM,
    MM_SET_RF_CONFIG_REQ,
    MM_SET_RF_CONFIG_CFM,
    MM_SET_RF_CALIB_REQ,
    MM_SET_RF_CALIB_CFM,

    /// MU EDCA PARAMETERS Configuration Request.
    MM_SET_MU_EDCA_REQ,
    /// MU EDCA PARAMETERS Configuration Confirmation.
    MM_SET_MU_EDCA_CFM,
    /// UORA PARAMETERS Configuration Request.
    MM_SET_UORA_REQ,
    /// UORA PARAMETERS Configuration Confirmation.
    MM_SET_UORA_CFM,
    /// TXOP RTS THRESHOLD Configuration Request.
    MM_SET_TXOP_RTS_THRES_REQ,
    /// TXOP RTS THRESHOLD Configuration Confirmation.
    MM_SET_TXOP_RTS_THRES_CFM,
    /// HE BSS Color Configuration Request.
    MM_SET_BSS_COLOR_REQ,
    /// HE BSS Color Configuration Confirmation.
    MM_SET_BSS_COLOR_CFM,

    MM_GET_MAC_ADDR_REQ,
    MM_GET_MAC_ADDR_CFM,

    MM_GET_STA_INFO_REQ,
    MM_GET_STA_INFO_CFM,

    MM_SET_TXPWR_IDX_LVL_REQ,
    MM_SET_TXPWR_IDX_LVL_CFM,

    MM_SET_TXPWR_OFST_REQ,
    MM_SET_TXPWR_OFST_CFM,

    MM_SET_STACK_START_REQ,
    MM_SET_STACK_START_CFM,

    MM_APM_STALOSS_IND,

    MM_SET_VENDOR_HWCONFIG_REQ,
    MM_SET_VENDOR_HWCONFIG_CFM,

    MM_GET_FW_VERSION_REQ,
    MM_GET_FW_VERSION_CFM,

    /// MAX number of messages
    MM_MAX,
};

/// Interface types
enum
{
    /// ESS STA interface
    MM_STA,
    /// IBSS STA interface
    MM_IBSS,
    /// AP interface
    MM_AP,
    // Mesh Point interface
    MM_MESH_POINT,
    // Monitor interface
    MM_MONITOR,
};

///BA agreement types
enum
{
    ///BlockAck agreement for TX
    BA_AGMT_TX,
    ///BlockAck agreement for RX
    BA_AGMT_RX,
};

///BA agreement related status
enum
{
    ///Correct BA agreement establishment
    BA_AGMT_ESTABLISHED,
    ///BA agreement already exists for STA+TID requested, cannot override it (should have been deleted first)
    BA_AGMT_ALREADY_EXISTS,
    ///Correct BA agreement deletion
    BA_AGMT_DELETED,
    ///BA agreement for the (STA, TID) doesn't exist so nothing to delete
    BA_AGMT_DOESNT_EXIST,
};

/// Features supported by LMAC - Positions
enum mm_features
{
    /// Beaconing
    MM_FEAT_BCN_BIT         = 0,
    /// Autonomous Beacon Transmission
    MM_FEAT_AUTOBCN_BIT,
    /// Scan in LMAC
    MM_FEAT_HWSCAN_BIT,
    /// Connection Monitoring
    MM_FEAT_CMON_BIT,
    /// Multi Role
    MM_FEAT_MROLE_BIT,
    /// Radar Detection
    MM_FEAT_RADAR_BIT,
    /// Power Save
    MM_FEAT_PS_BIT,
    /// UAPSD
    MM_FEAT_UAPSD_BIT,
    /// DPSM
    MM_FEAT_DPSM_BIT,
    /// A-MPDU
    MM_FEAT_AMPDU_BIT,
    /// A-MSDU
    MM_FEAT_AMSDU_BIT,
    /// Channel Context
    MM_FEAT_CHNL_CTXT_BIT,
    /// Packet reordering
    MM_FEAT_REORD_BIT,
    /// P2P
    MM_FEAT_P2P_BIT,
    /// P2P Go
    MM_FEAT_P2P_GO_BIT,
    /// UMAC Present
    MM_FEAT_UMAC_BIT,
    /// VHT support
    MM_FEAT_VHT_BIT,
    /// Beamformee
    MM_FEAT_BFMEE_BIT,
    /// Beamformer
    MM_FEAT_BFMER_BIT,
    /// WAPI
    MM_FEAT_WAPI_BIT,
    /// MFP
    MM_FEAT_MFP_BIT,
    /// Mu-MIMO RX support
    MM_FEAT_MU_MIMO_RX_BIT,
    /// Mu-MIMO TX support
    MM_FEAT_MU_MIMO_TX_BIT,
    /// Wireless Mesh Networking
    MM_FEAT_MESH_BIT,
    /// TDLS support
    MM_FEAT_TDLS_BIT,
    /// Antenna Diversity support
    MM_FEAT_ANT_DIV_BIT,
    /// UF support
    MM_FEAT_UF_BIT,
    /// A-MSDU maximum size (bit0)
    MM_AMSDU_MAX_SIZE_BIT0,
    /// A-MSDU maximum size (bit1)
    MM_AMSDU_MAX_SIZE_BIT1,
    /// MON_DATA support
    MM_FEAT_MON_DATA_BIT,
    /// HE (802.11ax) support
    MM_FEAT_HE_BIT,
};

/// Maximum number of words in the configuration buffer
#define PHY_CFG_BUF_SIZE     16

/// Structure containing the parameters of the PHY configuration
struct phy_cfg_tag
{
    /// Buffer containing the parameters specific for the PHY used
    u32_l parameters[PHY_CFG_BUF_SIZE];
};

/// Structure containing the parameters of the Trident PHY configuration
struct phy_trd_cfg_tag
{
    /// MDM type(nxm)(upper nibble) and MDM2RF path mapping(lower nibble)
    u8_l path_mapping;
    /// TX DC offset compensation
    u32_l tx_dc_off_comp;
};

/// Structure containing the parameters of the Karst PHY configuration
struct phy_karst_cfg_tag
{
    /// TX IQ mismatch compensation in 2.4GHz
    u32_l tx_iq_comp_2_4G[2];
    /// RX IQ mismatch compensation in 2.4GHz
    u32_l rx_iq_comp_2_4G[2];
    /// TX IQ mismatch compensation in 5GHz
    u32_l tx_iq_comp_5G[2];
    /// RX IQ mismatch compensation in 5GHz
    u32_l rx_iq_comp_5G[2];
    /// RF path used by default (0 or 1)
    u8_l path_used;
};

/// Structure containing the parameters of the @ref MM_START_REQ message
struct mm_start_req
{
    /// PHY configuration
    struct phy_cfg_tag phy_cfg;
    /// UAPSD timeout
    u32_l uapsd_timeout;
    /// Local LP clock accuracy (in ppm)
    u16_l lp_clk_accuracy;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_REQ message
struct mm_set_channel_req
{
    /// Channel information
    struct mac_chan_op chan;
    /// Index of the RF for which the channel has to be set (0: operating (primary), 1: secondary
    /// RF (used for additional radar detection). This parameter is reserved if no secondary RF
    /// is available in the system
    u8_l index;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_CFM message
struct mm_set_channel_cfm
{
    /// Radio index to be used in policy table
    u8_l radio_idx;
    /// TX power configured (in dBm)
    s8_l power;
};

/// Structure containing the parameters of the @ref MM_SET_DTIM_REQ message
struct mm_set_dtim_req
{
    /// DTIM period
    u8_l dtim_period;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_REQ message
struct mm_set_power_req
{
    /// Index of the interface for which the parameter is configured
    u8_l inst_nbr;
    /// TX power (in dBm)
    s8_l power;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_CFM message
struct mm_set_power_cfm
{
    /// Radio index to be used in policy table
    u8_l radio_idx;
    /// TX power configured (in dBm)
    s8_l power;
};

/// Structure containing the parameters of the @ref MM_SET_BEACON_INT_REQ message
struct mm_set_beacon_int_req
{
    /// Beacon interval
    u16_l beacon_int;
    /// Index of the interface for which the parameter is configured
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BASIC_RATES_REQ message
struct mm_set_basic_rates_req
{
    /// Basic rate set (as expected by bssBasicRateSet field of Rates MAC HW register)
    u32_l rates;
    /// Index of the interface for which the parameter is configured
    u8_l inst_nbr;
    /// Band on which the interface will operate
    u8_l band;
};

/// Structure containing the parameters of the @ref MM_SET_BSSID_REQ message
struct mm_set_bssid_req
{
    /// BSSID to be configured in HW
    struct mac_addr bssid;
    /// Index of the interface for which the parameter is configured
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_FILTER_REQ message
struct mm_set_filter_req
{
    /// RX filter to be put into rxCntrlReg HW register
    u32_l filter;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_REQ message.
struct mm_add_if_req
{
    /// Type of the interface (AP, STA, ADHOC, ...)
    u8_l type;
    /// MAC ADDR of the interface to start
    struct mac_addr addr;
    /// P2P Interface
    bool_l p2p;
};

/// Structure containing the parameters of the @ref MM_SET_EDCA_REQ message
struct mm_set_edca_req
{
    /// EDCA parameters of the queue (as expected by edcaACxReg HW register)
    u32_l ac_param;
    /// Flag indicating if UAPSD can be used on this queue
    bool_l uapsd;
    /// HW queue for which the parameters are configured
    u8_l hw_queue;
    /// Index of the interface for which the parameters are configured
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_MU_EDCA_REQ message
struct mm_set_mu_edca_req
{
    /// MU EDCA parameters of the different HE queues
    u32_l param[AC_MAX];
};

/// Structure containing the parameters of the @ref MM_SET_UORA_REQ message
struct mm_set_uora_req
{
    /// Minimum exponent of OFDMA Contention Window.
    u8_l eocw_min;
    /// Maximum exponent of OFDMA Contention Window.
    u8_l eocw_max;
};

/// Structure containing the parameters of the @ref MM_SET_TXOP_RTS_THRES_REQ message
struct mm_set_txop_rts_thres_req
{
    /// TXOP RTS threshold
    u16_l txop_dur_rts_thres;
    /// Index of the interface for which the parameter is configured
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BSS_COLOR_REQ message
struct mm_set_bss_color_req
{
    /// HE BSS color, formatted as per BSS_COLOR MAC HW register
    u32_l bss_color;
};

struct mm_set_idle_req
{
    u8_l hw_idle;
};

/// Structure containing the parameters of the @ref MM_SET_SLOTTIME_REQ message
struct mm_set_slottime_req
{
    /// Slot time expressed in us
    u8_l slottime;
};

/// Structure containing the parameters of the @ref MM_SET_MODE_REQ message
struct mm_set_mode_req
{
    /// abgnMode field of macCntrl1Reg register
    u8_l abgnmode;
};

/// Structure containing the parameters of the @ref MM_SET_VIF_STATE_REQ message
struct mm_set_vif_state_req
{
    /// Association Id received from the AP (valid only if the VIF is of STA type)
    u16_l aid;
    /// Flag indicating if the VIF is active or not
    bool_l active;
    /// Interface index
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_CFM message.
struct mm_add_if_cfm
{
    /// Status of operation (different from 0 if unsuccessful)
    u8_l status;
    /// Interface index assigned by the LMAC
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMOVE_IF_REQ message.
struct mm_remove_if_req
{
    /// Interface index assigned by the LMAC
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_VERSION_CFM message.
struct mm_version_cfm
{
    /// Version of the LMAC FW
    u32_l version_lmac;
    /// Version1 of the MAC HW (as encoded in version1Reg MAC HW register)
    u32_l version_machw_1;
    /// Version2 of the MAC HW (as encoded in version2Reg MAC HW register)
    u32_l version_machw_2;
    /// Version1 of the PHY (depends on actual PHY)
    u32_l version_phy_1;
    /// Version2 of the PHY (depends on actual PHY)
    u32_l version_phy_2;
    /// Supported Features
    u32_l features;
    /// Maximum number of supported stations
    u16_l max_sta_nb;
    /// Maximum number of supported virtual interfaces
    u8_l max_vif_nb;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_REQ message.
struct mm_sta_add_req
{
    /// Maximum A-MPDU size, in bytes, for HE frames
    u32_l ampdu_size_max_he;
    /// Maximum A-MPDU size, in bytes, for VHT frames
    u32_l ampdu_size_max_vht;
    /// PAID/GID
    u32_l paid_gid;
    /// Maximum A-MPDU size, in bytes, for HT frames
    u16_l ampdu_size_max_ht;
    /// MAC address of the station to be added
    struct mac_addr mac_addr;
    /// A-MPDU spacing, in us
    u8_l ampdu_spacing_min;
    /// Interface index
    u8_l inst_nbr;
    /// TDLS station
    bool_l tdls_sta;
    /// Indicate if the station is TDLS link initiator station
    bool_l tdls_sta_initiator;
    /// Indicate if the TDLS Channel Switch is allowed
    bool_l tdls_chsw_allowed;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_CFM message.
struct mm_sta_add_cfm
{
    /// Status of the operation (different from 0 if unsuccessful)
    u8_l status;
    /// Index assigned by the LMAC to the newly added station
    u8_l sta_idx;
    /// MAC HW index of the newly added station
    u8_l hw_sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_REQ message.
struct mm_sta_del_req
{
    /// Index of the station to be deleted
    u8_l sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_CFM message.
struct mm_sta_del_cfm
{
    /// Status of the operation (different from 0 if unsuccessful)
    u8_l     status;
};

/// Structure containing the parameters of the SET_POWER_MODE REQ message.
struct mm_setpowermode_req
{
    u8_l mode;
    u8_l sta_idx;
};

/// Structure containing the parameters of the SET_POWER_MODE CFM message.
struct mm_setpowermode_cfm
{
    u8_l     status;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD REQ message.
struct mm_key_add_req
{
    /// Key index (valid only for default keys)
    u8_l key_idx;
    /// STA index (valid only for pairwise or mesh group keys)
    u8_l sta_idx;
    /// Key material
    struct mac_sec_key key;
    /// Cipher suite (WEP64, WEP128, TKIP, CCMP)
    u8_l cipher_suite;
    /// Index of the interface for which the key is set (valid only for default keys or mesh group keys)
    u8_l inst_nbr;
    /// A-MSDU SPP parameter
    u8_l spp;
    /// Indicate if provided key is a pairwise key or not
    bool_l pairwise;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD_CFM message.
struct mm_key_add_cfm
{
    /// Status of the operation (different from 0 if unsuccessful)
    u8_l status;
    /// HW index of the key just added
    u8_l hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_KEY_DEL_REQ message.
struct mm_key_del_req
{
    /// HW index of the key to be deleted
    u8_l hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_REQ message.
struct mm_ba_add_req
{
    ///Type of agreement (0: TX, 1: RX)
    u8_l  type;
    ///Index of peer station with which the agreement is made
    u8_l  sta_idx;
    ///TID for which the agreement is made with peer station
    u8_l  tid;
    ///Buffer size - number of MPDUs that can be held in its buffer per TID
    u8_l  bufsz;
    /// Start sequence number negotiated during BA setup - the one in first aggregated MPDU counts more
    u16_l ssn;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_CFM message.
struct mm_ba_add_cfm
{
    ///Index of peer station for which the agreement is being confirmed
    u8_l sta_idx;
    ///TID for which the agreement is being confirmed
    u8_l tid;
    /// Status of ba establishment
    u8_l status;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_REQ message.
struct mm_ba_del_req
{
    ///Type of agreement (0: TX, 1: RX)
    u8_l type;
    ///Index of peer station for which the agreement is being deleted
    u8_l sta_idx;
    ///TID for which the agreement is being deleted
    u8_l tid;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_CFM message.
struct mm_ba_del_cfm
{
    ///Index of peer station for which the agreement deletion is being confirmed
    u8_l sta_idx;
    ///TID for which the agreement deletion is being confirmed
    u8_l tid;
    /// Status of ba deletion
    u8_l status;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_req
{
    /// Operating channel
    struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_cfm
{
    /// Status of the addition
    u8_l status;
    /// Index of the new channel context
    u8_l index;
};


/// Structure containing the parameters of the @ref MM_CHAN_CTXT_DEL_REQ message
struct mm_chan_ctxt_del_req
{
    /// Index of the new channel context to be deleted
    u8_l index;
};


/// Structure containing the parameters of the @ref MM_CHAN_CTXT_LINK_REQ message
struct mm_chan_ctxt_link_req
{
    /// VIF index
    u8_l vif_index;
    /// Channel context index
    u8_l chan_index;
    /// Indicate if this is a channel switch (unlink current ctx first if true)
    u8_l chan_switch;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UNLINK_REQ message
struct mm_chan_ctxt_unlink_req
{
    /// VIF index
    u8_l vif_index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UPDATE_REQ message
struct mm_chan_ctxt_update_req
{
    /// Channel context index
    u8_l chan_index;
    /// New channel information
    struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_SCHED_REQ message
struct mm_chan_ctxt_sched_req
{
    /// VIF index
    u8_l vif_index;
    /// Channel context index
    u8_l chan_index;
    /// Type of the scheduling request (0: normal scheduling, 1: derogatory
    /// scheduling)
    u8_l type;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SWITCH_IND message
struct mm_channel_switch_ind
{
    /// Index of the channel context we will switch to
    u8_l chan_index;
    /// Indicate if the switch has been triggered by a Remain on channel request
    bool_l roc;
    /// VIF on which remain on channel operation has been started (if roc == 1)
    u8_l vif_index;
    /// Indicate if the switch has been triggered by a TDLS Remain on channel request
    bool_l roc_tdls;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_PRE_SWITCH_IND message
struct mm_channel_pre_switch_ind
{
    /// Index of the channel context we will switch to
    u8_l chan_index;
};

/// Structure containing the parameters of the @ref MM_CONNECTION_LOSS_IND message.
struct mm_connection_loss_ind
{
    /// VIF instance number
    u8_l inst_nbr;
};


/// Structure containing the parameters of the @ref MM_DBG_TRIGGER_REQ message.
struct mm_dbg_trigger_req
{
    /// Error trace to be reported by the LMAC
    char error[64];
};

/// Structure containing the parameters of the @ref MM_SET_PS_MODE_REQ message.
struct mm_set_ps_mode_req
{
    /// Power Save is activated or deactivated
    u8_l  new_state;
};

/// Structure containing the parameters of the @ref MM_BCN_CHANGE_REQ message.
#define BCN_MAX_CSA_CPT 2
struct mm_bcn_change_req
{
    /// Pointer, in host memory, to the new beacon template
    u32_l bcn_ptr;
    /// Length of the beacon template
    u16_l bcn_len;
    /// Offset of the TIM IE in the beacon
    u16_l tim_oft;
    /// Length of the TIM IE
    u8_l tim_len;
    /// Index of the VIF for which the beacon is updated
    u8_l inst_nbr;
    /// Offset of CSA (channel switch announcement) counters (0 means no counter)
    u8_l csa_oft[BCN_MAX_CSA_CPT];
};


/// Structure containing the parameters of the @ref MM_TIM_UPDATE_REQ message.
struct mm_tim_update_req
{
    /// Association ID of the STA the bit of which has to be updated (0 for BC/MC traffic)
    u16_l aid;
    /// Flag indicating the availability of data packets for the given STA
    u8_l tx_avail;
    /// Index of the VIF for which the TIM is updated
    u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_REQ message.
struct mm_remain_on_channel_req
{
    /// Operation Code
    u8_l op_code;
    /// VIF Index
    u8_l vif_index;
    /// Band (2.4GHz or 5GHz)
    u8_l band;
    /// Channel type: 20,40,80,160 or 80+80 MHz
    u8_l type;
    /// Frequency for Primary 20MHz channel (in MHz)
    u16_l prim20_freq;
    /// Frequency for Center of the contiguous channel or center of Primary 80+80
    u16_l center1_freq;
    /// Frequency for Center of the non-contiguous secondary 80+80
    u16_l center2_freq;
    /// Duration (in ms)
    u32_l duration_ms;
    /// TX power (in dBm)
    s8_l tx_power;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_CFM message
struct mm_remain_on_channel_cfm
{
    /// Operation Code
    u8_l op_code;
    /// Status of the operation
    u8_l status;
    /// Channel Context index
    u8_l chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_EXP_IND message
struct mm_remain_on_channel_exp_ind
{
    /// VIF Index
    u8_l vif_index;
    /// Channel Context index
    u8_l chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_SET_UAPSD_TMR_REQ message.
struct mm_set_uapsd_tmr_req
{
    /// action: Start or Stop the timer
    u8_l  action;
    /// timeout value, in milliseconds
    u32_l  timeout;
};

/// Structure containing the parameters of the @ref MM_SET_UAPSD_TMR_CFM message.
struct mm_set_uapsd_tmr_cfm
{
    /// Status of the operation (different from 0 if unsuccessful)
    u8_l     status;
};


/// Structure containing the parameters of the @ref MM_PS_CHANGE_IND message
struct mm_ps_change_ind
{
    /// Index of the peer device that is switching its PS state
    u8_l sta_idx;
    /// New PS state of the peer device (0: active, 1: sleeping)
    u8_l ps_state;
};

/// Structure containing the parameters of the @ref MM_P2P_VIF_PS_CHANGE_IND message
struct mm_p2p_vif_ps_change_ind
{
    /// Index of the P2P VIF that is switching its PS state
    u8_l vif_index;
    /// New PS state of the P2P VIF interface (0: active, 1: sleeping)
    u8_l ps_state;
};

/// Structure containing the parameters of the @ref MM_TRAFFIC_REQ_IND message
struct mm_traffic_req_ind
{
    /// Index of the peer device that needs traffic
    u8_l sta_idx;
    /// Number of packets that need to be sent (if 0, all buffered traffic shall be sent and
    /// if set to @ref PS_SP_INTERRUPTED, it means that current service period has been interrupted)
    u8_l pkt_cnt;
    /// Flag indicating if the traffic request concerns U-APSD queues or not
    bool_l uapsd;
};

/// Structure containing the parameters of the @ref MM_SET_PS_OPTIONS_REQ message.
struct mm_set_ps_options_req
{
    /// VIF Index
    u8_l vif_index;
    /// Listen interval (0 if wake up shall be based on DTIM period)
    u16_l listen_interval;
    /// Flag indicating if we shall listen the BC/MC traffic or not
    bool_l dont_listen_bc_mc;
};

/// Structure containing the parameters of the @ref MM_CSA_COUNTER_IND message
struct mm_csa_counter_ind
{
    /// Index of the VIF
    u8_l vif_index;
    /// Updated CSA counter value
    u8_l csa_count;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SURVEY_IND message
struct mm_channel_survey_ind
{
    /// Frequency of the channel
    u16_l freq;
    /// Noise in dbm
    s8_l noise_dbm;
    /// Amount of time spent of the channel (in ms)
    u32_l chan_time_ms;
    /// Amount of time the primary channel was sensed busy
    u32_l chan_time_busy_ms;
};

/// Structure containing the parameters of the @ref MM_BFMER_ENABLE_REQ message.
struct mm_bfmer_enable_req
{
    /**
     * Address of the beamforming report space allocated in host memory
     * (Valid only if vht_su_bfmee is true)
     */
    u32_l host_bfr_addr;
    /**
     * Size of the beamforming report space allocated in host memory. This space should
     * be twice the maximum size of the expected beamforming reports as the FW will
     * divide it in two in order to be able to upload a new report while another one is
     * used in transmission
     */
    u16_l host_bfr_size;
    /// AID
    u16_l aid;
    /// Station Index
    u8_l sta_idx;
    /// Maximum number of spatial streams the station can receive
    u8_l rx_nss;
    /**
     * Indicate if peer STA is MU Beamformee (VHT) capable
     * (Valid only if vht_su_bfmee is true)
     */
    bool_l vht_mu_bfmee;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_REQ message.
struct mm_set_p2p_noa_req
{
    /// VIF Index
    u8_l vif_index;
    /// Allocated NOA Instance Number - Valid only if count = 0
    u8_l noa_inst_nb;
    /// Count
    u8_l count;
    /// Indicate if NoA can be paused for traffic reason
    bool_l dyn_noa;
    /// Duration (in us)
    u32_l duration_us;
    /// Interval (in us)
    u32_l interval_us;
    /// Start Time offset from next TBTT (in us)
    u32_l start_offset;
};

#ifdef AICWF_ARP_OFFLOAD
struct mm_set_arpoffload_en_req
{
	u32_l ipaddr;
	u8_l enable;
	u8_l vif_idx;
};

struct mm_set_arpoffload_en_cfm
{
	u8_l status;
};
#endif

struct mm_set_agg_disable_req
{
    u8_l disable;
    u8_l staidx;
};

struct mm_set_coex_req
{
    u8_l bt_on;
    u8_l disable_coexnull;
    u8_l enable_nullcts;
    u8_l enable_periodic_timer;
    u8_l coex_timeslot_set;
    u32_l coex_timeslot[2];
};

#if 0
struct mm_set_rf_config_req
{
    u8_l def_band;
    u8_l config_type;
    u16_l offset;
    u16_l len;
    u16_l set;
    u32_l rx_gain_24g[48][4];
    u32_l rx_gain_5g[32][4];
    u32_l tx_gain[32];
};
#endif
struct mm_set_rf_config_req
{
    u8_l table_sel;
    u8_l table_ofst;
    u8_l table_num;
	u8_l deft_page;
    u32_l data[64];
};


struct mm_set_rf_calib_req
{
    u32_l cal_cfg_24g;
    u32_l cal_cfg_5g;
    u32_l param_alpha;
    u32_l bt_calib_en;
    u32_l bt_calib_param;
    u8_l xtal_cap;
    u8_l xtal_cap_fine;

};

struct mm_set_rf_calib_cfm
{
    u32_l rxgain_24g_addr;
    u32_l rxgain_5g_addr;
    u32_l txgain_24g_addr;
    u32_l txgain_5g_addr;
};

struct mm_get_mac_addr_req
{
    u32_l get;
};

struct mm_get_mac_addr_cfm
{
    u8_l mac_addr[6];
};

struct mm_get_sta_info_req
{
    u8_l sta_idx;
};

struct mm_get_sta_info_cfm
{
    u32_l rate_info;
    u32_l txfailed;
	u8 rssi;
};

typedef struct
{
    u8_l enable;
    u8_l dsss;
    u8_l ofdmlowrate_2g4;
    u8_l ofdm64qam_2g4;
    u8_l ofdm256qam_2g4;
    u8_l ofdm1024qam_2g4;
    u8_l ofdmlowrate_5g;
    u8_l ofdm64qam_5g;
    u8_l ofdm256qam_5g;
    u8_l ofdm1024qam_5g;
} txpwr_lvl_conf_t;

typedef struct
{
    u8_l enable;
    s8_l pwrlvl_11b_11ag_2g4[12];
    s8_l pwrlvl_11n_11ac_2g4[10];
    s8_l pwrlvl_11ax_2g4[12];
} txpwr_lvl_conf_v2_t;

struct mm_set_txpwr_lvl_req
{
  union {
    txpwr_lvl_conf_t txpwr_lvl;
    txpwr_lvl_conf_v2_t txpwr_lvl_v2;
  };
};

typedef struct
{
    u8_l loss_enable;
    u8_l loss_value;
} txpwr_loss_conf_t;

typedef struct
{
    int8_t enable;
    int8_t dsss;
    int8_t ofdmlowrate_2g4;
    int8_t ofdm64qam_2g4;
    int8_t ofdm256qam_2g4;
    int8_t ofdm1024qam_2g4;
    int8_t ofdmlowrate_5g;
    int8_t ofdm64qam_5g;
    int8_t ofdm256qam_5g;
    int8_t ofdm1024qam_5g;

} txpwr_idx_conf_t;

struct mm_set_txpwr_idx_req
{
    txpwr_idx_conf_t txpwr_idx;
};

typedef struct
{
    int8_t enable;
    int8_t chan_1_4;
    int8_t chan_5_9;
    int8_t chan_10_13;
    int8_t chan_36_64;
    int8_t chan_100_120;
    int8_t chan_122_140;
    int8_t chan_142_165;
} txpwr_ofst_conf_t;

struct mm_set_txpwr_ofst_req
{
    txpwr_ofst_conf_t txpwr_ofst;
};

typedef struct
{
    u8_l enable;
    u8_l xtal_cap;
    u8_l xtal_cap_fine;
} xtal_cap_conf_t;


struct mm_set_stack_start_req
{
    u8_l is_stack_start;
    u8_l efuse_valid;
    u8_l set_vendor_info;
    u8_l fwtrace_redir;
};

struct mm_set_stack_start_cfm
{
    u8_l is_5g_support;
    u8_l vendor_info;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_REQ message.
struct mm_set_p2p_oppps_req
{
    /// VIF Index
    u8_l vif_index;
    /// CTWindow
    u8_l ctwindow;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_CFM message.
struct mm_set_p2p_noa_cfm
{
    /// Request status
    u8_l status;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_CFM message.
struct mm_set_p2p_oppps_cfm
{
    /// Request status
    u8_l status;
};

/// Structure containing the parameters of the @ref MM_P2P_NOA_UPD_IND message.
struct mm_p2p_noa_upd_ind
{
    /// VIF Index
    u8_l vif_index;
    /// NOA Instance Number
    u8_l noa_inst_nb;
    /// NoA Type
    u8_l noa_type;
    /// Count
    u8_l count;
    /// Duration (in us)
    u32_l duration_us;
    /// Interval (in us)
    u32_l interval_us;
    /// Start Time
    u32_l start_time;
};

/// Structure containing the parameters of the @ref MM_CFG_RSSI_REQ message
struct mm_cfg_rssi_req
{
    /// Index of the VIF
    u8_l vif_index;
    /// RSSI threshold
    s8_l rssi_thold;
    /// RSSI hysteresis
    u8_l rssi_hyst;
};

/// Structure containing the parameters of the @ref MM_RSSI_STATUS_IND message
struct mm_rssi_status_ind
{
    /// Index of the VIF
    u8_l vif_index;
    /// Status of the RSSI
    bool_l rssi_status;
    /// Current RSSI
    s8_l rssi;
};

/// Structure containing the parameters of the @ref MM_PKTLOSS_IND message
struct mm_pktloss_ind
{
    /// Index of the VIF
    u8_l vif_index;
    /// Address of the STA for which there is a packet loss
    struct mac_addr mac_addr;
    /// Number of packets lost
    u32 num_packets;
};

/// Structure containing the parameters of the @ref MM_CSA_FINISH_IND message
struct mm_csa_finish_ind
{
    /// Index of the VIF
    u8_l vif_index;
    /// Status of the operation
    u8_l status;
    /// New channel ctx index
    u8_l chan_idx;
};

/// Structure containing the parameters of the @ref MM_CSA_TRAFFIC_IND message
struct mm_csa_traffic_ind
{
    /// Index of the VIF
    u8_l vif_index;
    /// Is tx traffic enable or disable
    bool_l enable;
};

/// Structure containing the parameters of the @ref MM_MU_GROUP_UPDATE_REQ message.
/// Size allocated for the structure depends of the number of group
struct mm_mu_group_update_req
{
    /// Station index
    u8_l sta_idx;
    /// Number of groups the STA belongs to
    u8_l group_cnt;
    /// Group information
    struct
    {
        /// Group Id
        u8_l group_id;
        /// User position
        u8_l user_pos;
    } groups[0];
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Scan messages
///////////////////////////////////////////////////////////////////////////////
enum scan_msg_tag
{
    /// Scanning start Request.
    SCAN_START_REQ = LMAC_FIRST_MSG(TASK_SCAN),
    /// Scanning start Confirmation.
    SCAN_START_CFM,
    /// End of scanning indication.
    SCAN_DONE_IND,
    /// Cancel scan request
    SCAN_CANCEL_REQ,
    /// Cancel scan confirmation
    SCAN_CANCEL_CFM,

    /// MAX number of messages
    SCAN_MAX,
};

/// Maximum number of SSIDs in a scan request
#define SCAN_SSID_MAX   3

/// Maximum number of channels in a scan request
#define SCAN_CHANNEL_MAX (MAC_DOMAINCHANNEL_24G_MAX + MAC_DOMAINCHANNEL_5G_MAX)

/// Maximum length of the ProbeReq IEs (SoftMAC mode)
#define SCAN_MAX_IE_LEN 300

/// Maximum number of PHY bands supported
#define SCAN_BAND_MAX 2

/// Structure containing the parameters of the @ref SCAN_START_REQ message
struct scan_start_req
{
    /// List of channel to be scanned
    struct mac_chan_def chan[SCAN_CHANNEL_MAX];
    /// List of SSIDs to be scanned
    struct mac_ssid ssid[SCAN_SSID_MAX];
    /// BSSID to be scanned
    struct mac_addr bssid;
    /// Pointer (in host memory) to the additional IEs that need to be added to the ProbeReq
    /// (following the SSID element)
    u32_l add_ies;
    /// Length of the additional IEs
    u16_l add_ie_len;
    /// Index of the VIF that is scanning
    u8_l vif_idx;
    /// Number of channels to scan
    u8_l chan_cnt;
    /// Number of SSIDs to scan for
    u8_l ssid_cnt;
    /// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
    bool no_cck;
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_start_cfm
{
    /// Status of the request
    u8_l status;
};

/// Structure containing the parameters of the @ref SCAN_CANCEL_REQ message
struct scan_cancel_req
{
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_cancel_cfm
{
    /// Status of the request
    u8_l status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Scanu messages
///////////////////////////////////////////////////////////////////////////////
/// Messages that are logically related to the task.
enum
{
    /// Scan request from host.
    SCANU_START_REQ = LMAC_FIRST_MSG(TASK_SCANU),
    /// Scanning start Confirmation.
    SCANU_START_CFM,
    /// Join request
    SCANU_JOIN_REQ,
    /// Join confirmation.
    SCANU_JOIN_CFM,
    /// Scan result indication.
    SCANU_RESULT_IND,
    /// Fast scan request from any other module.
    SCANU_FAST_REQ,
    /// Confirmation of fast scan request.
    SCANU_FAST_CFM,

	SCANU_VENDOR_IE_REQ,
	SCANU_VENDOR_IE_CFM,
	SCANU_START_CFM_ADDTIONAL,
	SCANU_CANCEL_REQ,
	SCANU_CANCEL_CFM,

    /// MAX number of messages
    SCANU_MAX,
};

/// Maximum length of the additional ProbeReq IEs (FullMAC mode)
#define SCANU_MAX_IE_LEN  200

/// Structure containing the parameters of the @ref SCANU_START_REQ message
struct scanu_start_req
{
    /// List of channel to be scanned
    struct mac_chan_def chan[SCAN_CHANNEL_MAX];
    /// List of SSIDs to be scanned
    struct mac_ssid ssid[SCAN_SSID_MAX];
    /// BSSID to be scanned (or WILDCARD BSSID if no BSSID is searched in particular)
    struct mac_addr bssid;
    /// Address (in host memory) of the additional IEs that need to be added to the ProbeReq
    /// (following the SSID element)
    u32_l add_ies;
    /// Length of the additional IEs
    u16_l add_ie_len;
    /// Index of the VIF that is scanning
    u8_l vif_idx;
    /// Number of channels to scan
    u8_l chan_cnt;
    /// Number of SSIDs to scan for
    u8_l ssid_cnt;
    /// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
    bool no_cck;
};

struct scanu_vendor_ie_req
{
	u16_l add_ie_len;
	u8_l vif_idx;
	u8_l  ie[256];
};

/// Structure containing the parameters of the @ref SCANU_START_CFM message
struct scanu_start_cfm
{
    /// Index of the VIF that was scanning
    u8_l vif_idx;
    /// Status of the request
    u8_l status;
    /// Number of scan results available
    u8_l result_cnt;
};

/// Parameters of the @SCANU_RESULT_IND message
struct scanu_result_ind
{
    /// Length of the frame
    u16_l length;
    /// Frame control field of the frame.
    u16_l framectrl;
    /// Center frequency on which we received the packet
    u16_l center_freq;
    /// PHY band
    u8_l band;
    /// Index of the station that sent the frame. 0xFF if unknown.
    u8_l sta_idx;
    /// Index of the VIF that received the frame. 0xFF if unknown.
    u8_l inst_nbr;
    /// RSSI of the received frame.
    s8_l rssi;
    /// Frame payload.
    u32_l payload[];
};

/// Structure containing the parameters of the message.
struct scanu_fast_req
{
    /// The SSID to scan in the channel.
    struct mac_ssid ssid;
    /// BSSID.
    struct mac_addr bssid;
    /// Probe delay.
    u16_l probe_delay;
    /// Minimum channel time.
    u16_l minch_time;
    /// Maximum channel time.
    u16_l maxch_time;
    /// The channel number to scan.
    u16_l ch_nbr;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For ME messages
///////////////////////////////////////////////////////////////////////////////
/// Messages that are logically related to the task.
enum
{
    /// Configuration request from host.
    ME_CONFIG_REQ = LMAC_FIRST_MSG(TASK_ME),
    /// Configuration confirmation.
    ME_CONFIG_CFM,
    /// Configuration request from host.
    ME_CHAN_CONFIG_REQ,
    /// Configuration confirmation.
    ME_CHAN_CONFIG_CFM,
    /// Set control port state for a station.
    ME_SET_CONTROL_PORT_REQ,
    /// Control port setting confirmation.
    ME_SET_CONTROL_PORT_CFM,
    /// TKIP MIC failure indication.
    ME_TKIP_MIC_FAILURE_IND,
    /// Add a station to the FW (AP mode)
    ME_STA_ADD_REQ,
    /// Confirmation of the STA addition
    ME_STA_ADD_CFM,
    /// Delete a station from the FW (AP mode)
    ME_STA_DEL_REQ,
    /// Confirmation of the STA deletion
    ME_STA_DEL_CFM,
    /// Indication of a TX RA/TID queue credit update
    ME_TX_CREDITS_UPDATE_IND,
    /// Request indicating to the FW that there is traffic buffered on host
    ME_TRAFFIC_IND_REQ,
    /// Confirmation that the @ref ME_TRAFFIC_IND_REQ has been executed
    ME_TRAFFIC_IND_CFM,
    /// Request of RC statistics to a station
    ME_RC_STATS_REQ,
    /// RC statistics confirmation
    ME_RC_STATS_CFM,
    /// RC fixed rate request
    ME_RC_SET_RATE_REQ,
    /// Configure monitor interface
    ME_CONFIG_MONITOR_REQ,
    /// Configure monitor interface response
    ME_CONFIG_MONITOR_CFM,
    /// Setting power Save mode request from host
    ME_SET_PS_MODE_REQ,
    /// Set power Save mode confirmation
    ME_SET_PS_MODE_CFM,
    /// Setting Low Power level request from host
    ME_SET_LP_LEVEL_REQ,
    /// Set Low Power level confirmation
    ME_SET_LP_LEVEL_CFM,
    /// MAX number of messages
    ME_MAX,
};

/// Structure containing the parameters of the @ref ME_START_REQ message
struct me_config_req
{
    /// HT Capabilities
    struct mac_htcapability ht_cap;
    /// VHT Capabilities
    struct mac_vhtcapability vht_cap;
    /// HE capabilities
    struct mac_hecapability he_cap;
    /// Lifetime of packets sent under a BlockAck agreement (expressed in TUs)
    u16_l tx_lft;
    /// Maximum supported BW
    u8_l phy_bw_max;
    /// Boolean indicating if HT is supported or not
    bool_l ht_supp;
    /// Boolean indicating if VHT is supported or not
    bool_l vht_supp;
    /// Boolean indicating if HE is supported or not
    bool_l he_supp;
    /// Boolean indicating if HE OFDMA UL is enabled or not
    bool_l he_ul_on;
    /// Boolean indicating if PS mode shall be enabled or not
    bool_l ps_on;
    /// Boolean indicating if Antenna Diversity shall be enabled or not
    bool_l ant_div_on;
    /// Boolean indicating if Dynamic PS mode shall be used or not
    bool_l dpsm;
};

/// Structure containing the parameters of the @ref ME_CHAN_CONFIG_REQ message
struct me_chan_config_req
{
    /// List of 2.4GHz supported channels
    struct mac_chan_def chan2G4[MAC_DOMAINCHANNEL_24G_MAX];
    /// List of 5GHz supported channels
    struct mac_chan_def chan5G[MAC_DOMAINCHANNEL_5G_MAX];
    /// Number of 2.4GHz channels in the list
    u8_l chan2G4_cnt;
    /// Number of 5GHz channels in the list
    u8_l chan5G_cnt;
};

/// Structure containing the parameters of the @ref ME_SET_CONTROL_PORT_REQ message
struct me_set_control_port_req
{
    /// Index of the station for which the control port is opened
    u8_l sta_idx;
    /// Control port state
    bool_l control_port_open;
};

/// Structure containing the parameters of the @ref ME_TKIP_MIC_FAILURE_IND message
struct me_tkip_mic_failure_ind
{
    /// Address of the sending STA
    struct mac_addr addr;
    /// TSC value
    u64_l tsc;
    /// Boolean indicating if the packet was a group or unicast one (true if group)
    bool_l ga;
    /// Key Id
    u8_l keyid;
    /// VIF index
    u8_l vif_idx;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_REQ message
struct me_sta_add_req
{
    /// MAC address of the station to be added
    struct mac_addr mac_addr;
    /// Supported legacy rates
    struct mac_rateset rate_set;
    /// HT Capabilities
    struct mac_htcapability ht_cap;
    /// VHT Capabilities
    struct mac_vhtcapability vht_cap;
    /// HE capabilities
    struct mac_hecapability he_cap;
    /// Flags giving additional information about the station (@ref mac_sta_flags)
    u32_l flags;
    /// Association ID of the station
    u16_l aid;
    /// Bit field indicating which queues have U-APSD enabled
    u8_l uapsd_queues;
    /// Maximum size, in frames, of a APSD service period
    u8_l max_sp_len;
    /// Operation mode information (valid if bit @ref STA_OPMOD_NOTIF is
    /// set in the flags)
    u8_l opmode;
    /// Index of the VIF the station is attached to
    u8_l vif_idx;
    /// Whether the the station is TDLS station
    bool_l tdls_sta;
    /// Indicate if the station is TDLS link initiator station
    bool_l tdls_sta_initiator;
    /// Indicate if the TDLS Channel Switch is allowed
    bool_l tdls_chsw_allowed;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_CFM message
struct me_sta_add_cfm
{
    /// Station index
    u8_l sta_idx;
    /// Status of the station addition
    u8_l status;
    /// PM state of the station
    u8_l pm_state;
};

/// Structure containing the parameters of the @ref ME_STA_DEL_REQ message.
struct me_sta_del_req
{
    /// Index of the station to be deleted
    u8_l sta_idx;
    /// Whether the the station is TDLS station
    bool_l tdls_sta;
};

/// Structure containing the parameters of the @ref ME_TX_CREDITS_UPDATE_IND message.
struct me_tx_credits_update_ind
{
    /// Index of the station for which the credits are updated
    u8_l sta_idx;
    /// TID for which the credits are updated
    u8_l tid;
    /// Offset to be applied on the credit count
    s8_l credits;
};

/// Structure containing the parameters of the @ref ME_TRAFFIC_IND_REQ message.
struct me_traffic_ind_req
{
    /// Index of the station for which UAPSD traffic is available on host
    u8_l sta_idx;
    /// Flag indicating the availability of UAPSD packets for the given STA
    u8_l tx_avail;
    /// Indicate if traffic is on uapsd-enabled queues
    bool_l uapsd;
};

struct mm_apm_staloss_ind
{
        u8_l sta_idx;
        u8_l vif_idx;
        u8_l mac_addr[6];
};

enum vendor_hwconfig_tag{
	ACS_TXOP_REQ = 0,
	CHANNEL_ACCESS_REQ,
	MAC_TIMESCALE_REQ,
	CCA_THRESHOLD_REQ,
};

struct mm_set_acs_txop_req
{
    u32_l hwconfig_id;
	u16_l txop_bk;
	u16_l txop_be;
	u16_l txop_vi;
	u16_l txop_vo;
};

struct mm_set_channel_access_req
{
    u32_l hwconfig_id;
	u32_l edca[4];
	u8_l  vif_idx;
	u8_l  retry_cnt;
	u8_l  rts_en;
	u8_l  long_nav_en;
	u8_l  cfe_en;
};

struct mm_set_mac_timescale_req
{
    u32_l hwconfig_id;
	u8_l  sifsA_time;
	u8_l  sifsB_time;
	u8_l  slot_time;
	u8_l  rx_startdelay_ofdm;
	u8_l  rx_startdelay_long;
	u8_l  rx_startdelay_short;
};

struct mm_set_cca_threshold_req
{
    u32_l hwconfig_id;
	u8_l  auto_cca_en;
	s8_l  cca20p_rise_th;
	s8_l  cca20s_rise_th;
	s8_l  cca20p_fall_th;
	s8_l  cca20s_fall_th;

};

struct mm_set_txop_req
{
	u16_l txop_bk;
	u16_l txop_be;
	u16_l txop_vi;
	u16_l txop_vo;
	u8_l  long_nav_en;
	u8_l  cfe_en;
};

struct mm_set_vendor_trx_param_req
{
	u32_l edca[4];
	u8_l vif_idx;
	u8_l retry_cnt;
};

struct mm_get_fw_version_cfm
{
    u8_l fw_version_len;
    u8_l fw_version[63];
};

/// Structure containing the parameters of the @ref ME_RC_STATS_REQ message.
struct me_rc_stats_req
{
    /// Index of the station for which the RC statistics are requested
    u8_l sta_idx;
};

/// Structure containing the rate control statistics
struct rc_rate_stats
{
    /// Number of attempts (per sampling interval)
    u16_l attempts;
    /// Number of success (per sampling interval)
    u16_l success;
    /// Estimated probability of success (EWMA)
    u16_l probability;
    /// Rate configuration of the sample
    u16_l rate_config;
    union
    {
        struct {
            /// Number of times the sample has been skipped (per sampling interval)
            u8_l  sample_skipped;
            /// Whether the old probability is available
            bool_l  old_prob_available;
            /// Whether the rate can be used in the retry chain
            bool_l rate_allowed;
        };
        struct {
            /// RU size and UL length received in the latest HE trigger frame
            u16_l ru_and_length;
        };
    };
};

/// Number of RC samples
#define RC_MAX_N_SAMPLE 10
/// Index of the HE statistics element in the table
#define RC_HE_STATS_IDX RC_MAX_N_SAMPLE

/// Structure containing the parameters of the @ref ME_RC_STATS_CFM message.
struct me_rc_stats_cfm
{
    /// Index of the station for which the RC statistics are provided
    u8_l sta_idx;
    /// Number of samples used in the RC algorithm
    u16_l no_samples;
    /// Number of MPDUs transmitted (per sampling interval)
    u16_l ampdu_len;
    /// Number of AMPDUs transmitted (per sampling interval)
    u16_l ampdu_packets;
    /// Average number of MPDUs in each AMPDU frame (EWMA)
    u32_l avg_ampdu_len;
    // Current step 0 of the retry chain
    u8_l sw_retry_step;
    /// Trial transmission period
    u8_l sample_wait;
    /// Retry chain steps
    u16_l retry_step_idx[4];
    /// RC statistics - Max number of RC samples, plus one for the HE TB statistics
    struct rc_rate_stats rate_stats[RC_MAX_N_SAMPLE + 1];
    /// Throughput - Max number of RC samples, plus one for the HE TB statistics
    u32_l tp[RC_MAX_N_SAMPLE + 1];
};

/// Structure containing the parameters of the @ref ME_RC_SET_RATE_REQ message.
struct me_rc_set_rate_req
{
    /// Index of the station for which the fixed rate is set
    u8_l sta_idx;
    /// Rate configuration to be set
    u16_l fixed_rate_cfg;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_REQ message.
struct me_config_monitor_req
{
    /// Channel to configure
    struct mac_chan_op chan;
    /// Is channel data valid
    bool_l chan_set;
    /// Enable report of unsupported HT frames
    bool_l uf;
    /// Enable auto-reply as the mac_addr matches
    bool_l auto_reply;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_CFM message.
struct me_config_monitor_cfm
{
    /// Channel context index
    u8_l chan_index;
    /// Channel parameters
    struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref ME_SET_PS_MODE_REQ message.
struct me_set_ps_mode_req
{
    /// Power Save is activated or deactivated
    u8_l  ps_state;
};

/// Structure containing the parameters of the @ref ME_SET_LP_LEVEL_REQ message.
struct me_set_lp_level_req
{
    /// Low Power level
    u8_l lp_level;
};


///////////////////////////////////////////////////////////////////////////////
/////////// For SM messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the SM task
enum sm_msg_tag
{
    /// Request to connect to an AP
    SM_CONNECT_REQ = LMAC_FIRST_MSG(TASK_SM),
    /// Confirmation of connection
    SM_CONNECT_CFM,
    /// Indicates that the SM associated to the AP
    SM_CONNECT_IND,
    /// Request to disconnect
    SM_DISCONNECT_REQ,
    /// Confirmation of disconnection
    SM_DISCONNECT_CFM,
    /// Indicates that the SM disassociated the AP
    SM_DISCONNECT_IND,
    /// Request to start external authentication
    SM_EXTERNAL_AUTH_REQUIRED_IND,
    /// Response to external authentication request
    SM_EXTERNAL_AUTH_REQUIRED_RSP,

    /// MAX number of messages
    SM_MAX,
};

/// Structure containing the parameters of @ref SM_CONNECT_REQ message.
struct sm_connect_req
{
    /// SSID to connect to
    struct mac_ssid ssid;
    /// BSSID to connect to (if not specified, set this field to WILDCARD BSSID)
    struct mac_addr bssid;
    /// Channel on which we have to connect (if not specified, set -1 in the chan.freq field)
    struct mac_chan_def chan;
    /// Connection flags (see @ref mac_connection_flags)
    u32_l flags;
    /// Control port Ethertype (in network endianness)
    u16_l ctrl_port_ethertype;
    /// Length of the association request IEs
    u16_l ie_len;
    /// Listen interval to be used for this connection
    u16_l listen_interval;
    /// Flag indicating if the we have to wait for the BC/MC traffic after beacon or not
    bool_l dont_wait_bcmc;
    /// Authentication type
    u8_l auth_type;
    /// UAPSD queues (bit0: VO, bit1: VI, bit2: BE, bit3: BK)
    u8_l uapsd_queues;
    /// VIF index
    u8_l vif_idx;
    /// Buffer containing the additional information elements to be put in the
    /// association request
    u32_l ie_buf[64];
};

/// Structure containing the parameters of the @ref SM_CONNECT_CFM message.
struct sm_connect_cfm
{
    /// Status. If 0, it means that the connection procedure will be performed and that
    /// a subsequent @ref SM_CONNECT_IND message will be forwarded once the procedure is
    /// completed
    u8_l status;
};

#define SM_ASSOC_IE_LEN   800
/// Structure containing the parameters of the @ref SM_CONNECT_IND message.
struct sm_connect_ind
{
    /// Status code of the connection procedure
    u16_l status_code;
    /// BSSID
    struct mac_addr bssid;
    /// Flag indicating if the indication refers to an internal roaming or from a host request
    bool_l roamed;
    /// Index of the VIF for which the association process is complete
    u8_l vif_idx;
    /// Index of the STA entry allocated for the AP
    u8_l ap_idx;
    /// Index of the LMAC channel context the connection is attached to
    u8_l ch_idx;
    /// Flag indicating if the AP is supporting QoS
    bool_l qos;
    /// ACM bits set in the AP WMM parameter element
    u8_l acm;
    /// Length of the AssocReq IEs
    u16_l assoc_req_ie_len;
    /// Length of the AssocRsp IEs
    u16_l assoc_rsp_ie_len;
    /// IE buffer
    u32_l assoc_ie_buf[SM_ASSOC_IE_LEN/4];

    u16_l aid;
    u8_l band;
    u16_l center_freq;
    u8_l width;
    u32_l center_freq1;
    u32_l center_freq2;

    /// EDCA parameters
    u32_l ac_param[AC_MAX];
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_REQ message.
struct sm_disconnect_req
{
    /// Reason of the deauthentication.
    u16_l reason_code;
    /// Index of the VIF.
    u8_l vif_idx;
};

/// Structure containing the parameters of SM_ASSOCIATION_IND the message
struct sm_association_ind
{
    // MAC ADDR of the STA
    struct mac_addr     me_mac_addr;
};


/// Structure containing the parameters of the @ref SM_DISCONNECT_IND message.
struct sm_disconnect_ind
{
    /// Reason of the disconnection.
    u16_l reason_code;
    /// Index of the VIF.
    u8_l vif_idx;
    /// FT over DS is ongoing
    bool_l ft_over_ds;
};

/// Structure containing the parameters of the @ref SM_EXTERNAL_AUTH_REQUIRED_IND
struct sm_external_auth_required_ind
{
    /// Index of the VIF.
    u8_l vif_idx;
    /// SSID to authenticate to
    struct mac_ssid ssid;
    /// BSSID to authenticate to
    struct mac_addr bssid;
    /// AKM suite of the respective authentication
    u32_l akm;
};

/// Structure containing the parameters of the @ref SM_EXTERNAL_AUTH_REQUIRED_RSP
struct sm_external_auth_required_rsp
{
    /// Index of the VIF.
    u8_l vif_idx;
    /// Authentication status
    u16_l status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For APM messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the APM task
enum apm_msg_tag
{
    /// Request to start the AP.
    APM_START_REQ = LMAC_FIRST_MSG(TASK_APM),
    /// Confirmation of the AP start.
    APM_START_CFM,
    /// Request to stop the AP.
    APM_STOP_REQ,
    /// Confirmation of the AP stop.
    APM_STOP_CFM,
    /// Request to start CAC
    APM_START_CAC_REQ,
    /// Confirmation of the CAC start
    APM_START_CAC_CFM,
    /// Request to stop CAC
    APM_STOP_CAC_REQ,
    /// Confirmation of the CAC stop
    APM_STOP_CAC_CFM,

	APM_SET_BEACON_IE_REQ,
	APM_SET_BEACON_IE_CFM,
    /// MAX number of messages
    APM_MAX,
};

/// Structure containing the parameters of the @ref APM_START_REQ message.
struct apm_start_req
{
    /// Basic rate set
    struct mac_rateset basic_rates;
    /// Control channel on which we have to enable the AP
    struct mac_chan_def chan;
    /// Center frequency of the first segment
    u32_l center_freq1;
    /// Center frequency of the second segment (only in 80+80 configuration)
    u32_l center_freq2;
    /// Width of channel
    u8_l ch_width;
    /// Address, in host memory, to the beacon template
    u32_l bcn_addr;
    /// Length of the beacon template
    u16_l bcn_len;
    /// Offset of the TIM IE in the beacon
    u16_l tim_oft;
    /// Beacon interval
    u16_l bcn_int;
    /// Flags (@ref mac_connection_flags)
    u32_l flags;
    /// Control port Ethertype
    u16_l ctrl_port_ethertype;
    /// Length of the TIM IE
    u8_l tim_len;
    /// Index of the VIF for which the AP is started
    u8_l vif_idx;
};

struct apm_set_bcn_ie_req
{
    u8_l vif_idx;
    u16_l bcn_ie_len;
    u8_l bcn_ie[512];
};

/// Structure containing the parameters of the @ref APM_START_CFM message.
struct apm_start_cfm
{
    /// Status of the AP starting procedure
    u8_l status;
    /// Index of the VIF for which the AP is started
    u8_l vif_idx;
    /// Index of the channel context attached to the VIF
    u8_l ch_idx;
    /// Index of the STA used for BC/MC traffic
    u8_l bcmc_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_REQ message.
struct apm_stop_req
{
    /// Index of the VIF for which the AP has to be stopped
    u8_l vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_REQ message.
struct apm_start_cac_req
{
    /// Control channel on which we have to start the CAC
    struct mac_chan_def chan;
    /// Center frequency of the first segment
    u32_l center_freq1;
    /// Center frequency of the second segment (only in 80+80 configuration)
    u32_l center_freq2;
    /// Width of channel
    u8_l ch_width;
    /// Index of the VIF for which the CAC is started
    u8_l vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_CFM message.
struct apm_start_cac_cfm
{
    /// Status of the CAC starting procedure
    u8_l status;
    /// Index of the channel context attached to the VIF for CAC
    u8_l ch_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_CAC_REQ message.
struct apm_stop_cac_req
{
    /// Index of the VIF for which the CAC has to be stopped
    u8_l vif_idx;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For MESH messages
///////////////////////////////////////////////////////////////////////////////

/// Maximum length of the Mesh ID
#define MESH_MESHID_MAX_LEN     (32)

/// Message API of the MESH task
enum mesh_msg_tag
{
    /// Request to start the MP
    MESH_START_REQ = LMAC_FIRST_MSG(TASK_MESH),
    /// Confirmation of the MP start.
    MESH_START_CFM,

    /// Request to stop the MP.
    MESH_STOP_REQ,
    /// Confirmation of the MP stop.
    MESH_STOP_CFM,

    // Request to update the MP
    MESH_UPDATE_REQ,
    /// Confirmation of the MP update
    MESH_UPDATE_CFM,

    /// Request information about a given link
    MESH_PEER_INFO_REQ,
    /// Response to the MESH_PEER_INFO_REQ message
    MESH_PEER_INFO_CFM,

    /// Request automatic establishment of a path with a given mesh STA
    MESH_PATH_CREATE_REQ,
    /// Confirmation to the MESH_PATH_CREATE_REQ message
    MESH_PATH_CREATE_CFM,

    /// Request a path update (delete path, modify next hop mesh STA)
    MESH_PATH_UPDATE_REQ,
    /// Confirmation to the MESH_PATH_UPDATE_REQ message
    MESH_PATH_UPDATE_CFM,

    /// Indication from Host that the indicated Mesh Interface is a proxy for an external STA
    MESH_PROXY_ADD_REQ,

    /// Indicate that a connection has been established or lost
    MESH_PEER_UPDATE_IND,
    /// Notification that a connection has been established or lost (when MPM handled by userspace)
    MESH_PEER_UPDATE_NTF = MESH_PEER_UPDATE_IND,

    /// Indicate that a path is now active or inactive
    MESH_PATH_UPDATE_IND,
    /// Indicate that proxy information have been updated
    MESH_PROXY_UPDATE_IND,

    /// MAX number of messages
    MESH_MAX,
};

/// Structure containing the parameters of the @ref MESH_START_REQ message.
struct mesh_start_req
{
    /// Basic rate set
    struct mac_rateset basic_rates;
    /// Control channel on which we have to enable the AP
    struct mac_chan_def chan;
    /// Center frequency of the first segment
    u32_l center_freq1;
    /// Center frequency of the second segment (only in 80+80 configuration)
    u32_l center_freq2;
    /// Width of channel
    u8_l ch_width;
    /// DTIM Period
    u8_l dtim_period;
    /// Beacon Interval
    u16_l bcn_int;
    /// Index of the VIF for which the MP is started
    u8_l vif_index;
    /// Length of the Mesh ID
    u8_l mesh_id_len;
    /// Mesh ID
    u8_l mesh_id[MESH_MESHID_MAX_LEN];
    /// Address of the IEs to download
    u32_l ie_addr;
    /// Length of the provided IEs
    u8_l ie_len;
    /// Indicate if Mesh Peering Management (MPM) protocol is handled in userspace
    bool_l user_mpm;
    /// Indicate if Mesh Point is using authentication
    bool_l is_auth;
    /// Indicate which authentication method is used
    u8_l auth_id;
};

/// Structure containing the parameters of the @ref MESH_START_CFM message.
struct mesh_start_cfm
{
    /// Status of the MP starting procedure
    u8_l status;
    /// Index of the VIF for which the MP is started
    u8_l vif_idx;
    /// Index of the channel context attached to the VIF
    u8_l ch_idx;
    /// Index of the STA used for BC/MC traffic
    u8_l bcmc_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_REQ message.
struct mesh_stop_req
{
    /// Index of the VIF for which the MP has to be stopped
    u8_l vif_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_CFM message.
struct mesh_stop_cfm
{
    /// Index of the VIF for which the MP has to be stopped
    u8_l vif_idx;
   /// Status
    u8_l status;
};

/// Bit fields for mesh_update_req message's flags value
enum mesh_update_flags_bit
{
    /// Root Mode
    MESH_UPDATE_FLAGS_ROOT_MODE_BIT = 0,
    /// Gate Mode
    MESH_UPDATE_FLAGS_GATE_MODE_BIT,
    /// Mesh Forwarding
    MESH_UPDATE_FLAGS_MESH_FWD_BIT,
    /// Local Power Save Mode
    MESH_UPDATE_FLAGS_LOCAL_PSM_BIT,
};

/// Structure containing the parameters of the @ref MESH_UPDATE_REQ message.
struct mesh_update_req
{
    /// Flags, indicate fields which have been updated
    u8_l flags;
    /// VIF Index
    u8_l vif_idx;
    /// Root Mode
    u8_l root_mode;
    /// Gate Announcement
    bool_l gate_announ;
    /// Mesh Forwarding
    bool_l mesh_forward;
    /// Local PS Mode
    u8_l local_ps_mode;
};

/// Structure containing the parameters of the @ref MESH_UPDATE_CFM message.
struct mesh_update_cfm
{
    /// Status
    u8_l status;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_REQ message.
struct mesh_peer_info_req
{
    ///Index of the station allocated for the peer
    u8_l sta_idx;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_CFM message.
struct mesh_peer_info_cfm
{
    /// Response status
    u8_l status;
    /// Index of the station allocated for the peer
    u8_l sta_idx;
    /// Local Link ID
    u16_l local_link_id;
    /// Peer Link ID
    u16_l peer_link_id;
    /// Local PS Mode
    u8_l local_ps_mode;
    /// Peer PS Mode
    u8_l peer_ps_mode;
    /// Non-peer PS Mode
    u8_l non_peer_ps_mode;
    /// Link State
    u8_l link_state;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_REQ message.
struct mesh_path_create_req
{
    /// Index of the interface on which path has to be created
    u8_l vif_idx;
    /// Indicate if originator MAC Address is provided
    bool_l has_orig_addr;
    /// Path Target MAC Address
    struct mac_addr tgt_mac_addr;
    /// Originator MAC Address
    struct mac_addr orig_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_CFM message.
struct mesh_path_create_cfm
{
    /// Confirmation status
    u8_l status;
    /// VIF Index
    u8_l vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_REQ message.
struct mesh_path_update_req
{
    /// Indicate if path must be deleted
    bool_l delete;
    /// Index of the interface on which path has to be created
    u8_l vif_idx;
    /// Path Target MAC Address
    struct mac_addr tgt_mac_addr;
    /// Next Hop MAC Address
    struct mac_addr nhop_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_CFM message.
struct mesh_path_update_cfm
{
    /// Confirmation status
    u8_l status;
    /// VIF Index
    u8_l vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PROXY_ADD_REQ message.
struct mesh_proxy_add_req
{
    /// VIF Index
    u8_l vif_idx;
    /// MAC Address of the External STA
    struct mac_addr ext_sta_addr;
};

/// Structure containing the parameters of the @ref MESH_PROXY_UPDATE_IND
struct mesh_proxy_update_ind
{
    /// Indicate if proxy information has been added or deleted
    bool_l delete;
    /// Indicate if we are a proxy for the external STA
    bool_l local;
    /// VIF Index
    u8_l vif_idx;
    /// MAC Address of the External STA
    struct mac_addr ext_sta_addr;
    /// MAC Address of the proxy (only valid if local is false)
    struct mac_addr proxy_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_IND message.
struct mesh_peer_update_ind
{
    /// Indicate if connection has been established or lost
    bool_l estab;
    /// VIF Index
    u8_l vif_idx;
    /// STA Index
    u8_l sta_idx;
    /// Peer MAC Address
    struct mac_addr peer_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_NTF message.
struct mesh_peer_update_ntf
{
    /// VIF Index
    u8_l vif_idx;
    /// STA Index
    u8_l sta_idx;
    /// Mesh Link State
    u8_l state;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_IND message.
struct mesh_path_update_ind
{
    /// Indicate if path is deleted or not
    bool_l delete;
    /// Indicate if path is towards an external STA (not part of MBSS)
    bool_l ext_sta;
    /// VIF Index
    u8_l vif_idx;
    /// Path Index
    u8_l path_idx;
    /// Target MAC Address
    struct mac_addr tgt_mac_addr;
    /// External STA MAC Address (only if ext_sta is true)
    struct mac_addr ext_sta_mac_addr;
    /// Next Hop STA Index
    u8_l nhop_sta_idx;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Debug messages
///////////////////////////////////////////////////////////////////////////////

/// Messages related to Debug Task
enum dbg_msg_tag
{
    /// Memory read request
    DBG_MEM_READ_REQ = LMAC_FIRST_MSG(TASK_DBG),
    /// Memory read confirm
    DBG_MEM_READ_CFM,
    /// Memory write request
    DBG_MEM_WRITE_REQ,
    /// Memory write confirm
    DBG_MEM_WRITE_CFM,
    /// Module filter request
    DBG_SET_MOD_FILTER_REQ,
    /// Module filter confirm
    DBG_SET_MOD_FILTER_CFM,
    /// Severity filter request
    DBG_SET_SEV_FILTER_REQ,
    /// Severity filter confirm
    DBG_SET_SEV_FILTER_CFM,
    /// LMAC/MAC HW fatal error indication
    DBG_ERROR_IND,
    /// Request to get system statistics
    DBG_GET_SYS_STAT_REQ,
    /// COnfirmation of system statistics
    DBG_GET_SYS_STAT_CFM,
    /// Memory block write request
    DBG_MEM_BLOCK_WRITE_REQ,
    /// Memory block write confirm
    DBG_MEM_BLOCK_WRITE_CFM,
    /// Start app request
    DBG_START_APP_REQ,
    /// Start app confirm
    DBG_START_APP_CFM,
    /// Start npc request
    DBG_START_NPC_REQ,
    /// Start npc confirm
    DBG_START_NPC_CFM,
    /// Memory mask write request
    DBG_MEM_MASK_WRITE_REQ,
    /// Memory mask write confirm
    DBG_MEM_MASK_WRITE_CFM,

    DBG_RFTEST_CMD_REQ,
    DBG_RFTEST_CMD_CFM,
    DBG_BINDING_REQ,
    DBG_BINDING_CFM,
    DBG_BINDING_IND,

    DBG_CUSTOM_MSG_REQ,
    DBG_CUSTOM_MSG_CFM,
    DBG_CUSTOM_MSG_IND,

    DBG_GPIO_WRITE_REQ,
    DBG_GPIO_WRITE_CFM,
    DBG_GPIO_READ_REQ,
    DBG_GPIO_READ_CFM,
    DBG_GPIO_INIT_REQ,
    DBG_GPIO_INIT_CFM,

    /// Max number of Debug messages
    DBG_MAX,
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_REQ message.
struct dbg_mem_read_req
{
    u32_l memaddr;
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_CFM message.
struct dbg_mem_read_cfm
{
    u32_l memaddr;
    u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_REQ message.
struct dbg_mem_write_req
{
    u32_l memaddr;
    u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_CFM message.
struct dbg_mem_write_cfm
{
    u32_l memaddr;
    u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_MASK_WRITE_REQ message.
struct dbg_mem_mask_write_req
{
    u32_l memaddr;
    u32_l memmask;
    u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_MASK_WRITE_CFM message.
struct dbg_mem_mask_write_cfm
{
    u32_l memaddr;
    u32_l memdata;
};

struct dbg_rftest_cmd_req
{
    u32_l cmd;
    u32_l argc;
    u8_l argv[30];
};

struct dbg_rftest_cmd_cfm
{
    u32_l rftest_result[16];
};

struct dbg_gpio_write_req {
    uint8_t gpio_idx;
    uint8_t gpio_val;
};

struct dbg_gpio_read_req {
    uint8_t gpio_idx;
};

struct dbg_gpio_read_cfm {
    uint8_t gpio_idx;
    uint8_t gpio_val;
};

struct dbg_gpio_init_req {
    uint8_t gpio_idx;
    uint8_t gpio_dir; //1 output, 0 input;
    uint8_t gpio_val; //for output, 1 high, 0 low;
};

/// Structure containing the parameters of the @ref DBG_SET_MOD_FILTER_REQ message.
struct dbg_set_mod_filter_req
{
    /// Bit field indicating for each module if the traces are enabled or not
    u32_l mod_filter;
};

/// Structure containing the parameters of the @ref DBG_SEV_MOD_FILTER_REQ message.
struct dbg_set_sev_filter_req
{
    /// Bit field indicating the severity threshold for the traces
    u32_l sev_filter;
};

/// Structure containing the parameters of the @ref DBG_GET_SYS_STAT_CFM message.
struct dbg_get_sys_stat_cfm
{
    /// Time spent in CPU sleep since last reset of the system statistics
    u32_l cpu_sleep_time;
    /// Time spent in DOZE since last reset of the system statistics
    u32_l doze_time;
    /// Total time spent since last reset of the system statistics
    u32_l stats_time;
};

/// Structure containing the parameters of the @ref DBG_MEM_BLOCK_WRITE_REQ message.
struct dbg_mem_block_write_req
{
    u32_l memaddr;
    u32_l memsize;
    u32_l memdata[512 / sizeof(u32_l)];//1024 for 8801
};

/// Structure containing the parameters of the @ref DBG_MEM_BLOCK_WRITE_CFM message.
struct dbg_mem_block_write_cfm
{
    u32_l wstatus;
};

/// Structure containing the parameters of the @ref DBG_START_APP_REQ message.
struct dbg_start_app_req
{
    u32_l bootaddr;
    u32_l boottype;
};

/// Structure containing the parameters of the @ref DBG_START_APP_CFM message.
struct dbg_start_app_cfm
{
    u32_l bootstatus;
};

enum {
    HOST_START_APP_AUTO = 1,
    HOST_START_APP_CUSTOM,
#ifdef CONFIG_USB_BT
    HOST_START_APP_REBOOT,
#endif // (CONFIG_USB_BT)
	HOST_START_APP_FNCALL = 4,
	HOST_START_APP_DUMMY  = 5,
};



///////////////////////////////////////////////////////////////////////////////
/////////// For TDLS messages
///////////////////////////////////////////////////////////////////////////////

/// List of messages related to the task.
enum tdls_msg_tag
{
    /// TDLS channel Switch Request.
    TDLS_CHAN_SWITCH_REQ = LMAC_FIRST_MSG(TASK_TDLS),
    /// TDLS channel switch confirmation.
    TDLS_CHAN_SWITCH_CFM,
    /// TDLS channel switch indication.
    TDLS_CHAN_SWITCH_IND,
    /// TDLS channel switch to base channel indication.
    TDLS_CHAN_SWITCH_BASE_IND,
    /// TDLS cancel channel switch request.
    TDLS_CANCEL_CHAN_SWITCH_REQ,
    /// TDLS cancel channel switch confirmation.
    TDLS_CANCEL_CHAN_SWITCH_CFM,
    /// TDLS peer power save indication.
    TDLS_PEER_PS_IND,
    /// TDLS peer traffic indication request.
    TDLS_PEER_TRAFFIC_IND_REQ,
    /// TDLS peer traffic indication confirmation.
    TDLS_PEER_TRAFFIC_IND_CFM,
    /// MAX number of messages
    TDLS_MAX
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_REQ message
struct tdls_chan_switch_req
{
    /// Index of the VIF
    u8_l vif_index;
    /// STA Index
    u8_l sta_idx;
    /// MAC address of the TDLS station
    struct mac_addr peer_mac_addr;
    bool_l initiator;
    /// Band (2.4GHz or 5GHz)
    u8_l band;
    /// Channel type: 20,40,80,160 or 80+80 MHz
    u8_l type;
    /// Frequency for Primary 20MHz channel (in MHz)
    u16_l prim20_freq;
    /// Frequency for Center of the contiguous channel or center of Primary 80+80
    u16_l center1_freq;
    /// Frequency for Center of the non-contiguous secondary 80+80
    u16_l center2_freq;
    /// TX power (in dBm)
    s8_l tx_power;
    /// Operating class
    u8_l op_class;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_REQ message
struct tdls_cancel_chan_switch_req
{
    /// Index of the VIF
    u8_l vif_index;
    /// STA Index
    u8_l sta_idx;
    /// MAC address of the TDLS station
    struct mac_addr peer_mac_addr;
};


/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_CFM message
struct tdls_chan_switch_cfm
{
    /// Status of the operation
    u8_l status;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_CFM message
struct tdls_cancel_chan_switch_cfm
{
    /// Status of the operation
    u8_l status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_IND message
struct tdls_chan_switch_ind
{
    /// VIF Index
    u8_l vif_index;
    /// Channel Context Index
    u8_l chan_ctxt_index;
    /// Status of the operation
    u8_l status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_BASE_IND message
struct tdls_chan_switch_base_ind
{
    /// VIF Index
    u8_l vif_index;
    /// Channel Context index
    u8_l chan_ctxt_index;
};

/// Structure containing the parameters of the @ref TDLS_PEER_PS_IND message
struct tdls_peer_ps_ind
{
    /// VIF Index
    u8_l vif_index;
    /// STA Index
    u8_l sta_idx;
    /// MAC ADDR of the TDLS STA
    struct mac_addr peer_mac_addr;
    /// Flag to indicate if the TDLS peer is going to sleep
    bool ps_on;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_REQ message
struct tdls_peer_traffic_ind_req
{
    /// VIF Index
    u8_l vif_index;
    /// STA Index
    u8_l sta_idx;
    // MAC ADDR of the TDLS STA
    struct mac_addr peer_mac_addr;
    /// Dialog token
    u8_l dialog_token;
    /// TID of the latest MPDU transmitted over the TDLS direct link to the TDLS STA
    u8_l last_tid;
    /// Sequence number of the latest MPDU transmitted over the TDLS direct link
    /// to the TDLS STA
    u16_l last_sn;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_CFM message
struct tdls_peer_traffic_ind_cfm
{
    /// Status of the operation
    u8_l status;
};


#endif // LMAC_MSG_H_
