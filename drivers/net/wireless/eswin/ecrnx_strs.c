/**
 ******************************************************************************
 *
 * @file ecrnx_strs.c
 *
 * @brief Miscellaneous debug strings
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include "lmac_msg.h"

static const char *const ecrnx_mmid2str[MSG_I(MM_MAX)] = {
    [MSG_I(MM_RESET_REQ)]                 = "MM_RESET_REQ",
    [MSG_I(MM_RESET_CFM)]                 = "MM_RESET_CFM",
    [MSG_I(MM_START_REQ)]                 = "MM_START_REQ",
    [MSG_I(MM_START_CFM)]                 = "MM_START_CFM",
    [MSG_I(MM_VERSION_REQ)]               = "MM_VERSION_REQ",
    [MSG_I(MM_VERSION_CFM)]               = "MM_VERSION_CFM",
    [MSG_I(MM_ADD_IF_REQ)]                = "MM_ADD_IF_REQ",
    [MSG_I(MM_ADD_IF_CFM)]                = "MM_ADD_IF_CFM",
    [MSG_I(MM_REMOVE_IF_REQ)]             = "MM_REMOVE_IF_REQ",
    [MSG_I(MM_REMOVE_IF_CFM)]             = "MM_REMOVE_IF_CFM",
    [MSG_I(MM_STA_ADD_REQ)]               = "MM_STA_ADD_REQ",
    [MSG_I(MM_STA_ADD_CFM)]               = "MM_STA_ADD_CFM",
    [MSG_I(MM_STA_DEL_REQ)]               = "MM_STA_DEL_REQ",
    [MSG_I(MM_STA_DEL_CFM)]               = "MM_STA_DEL_CFM",
    [MSG_I(MM_SET_FILTER_REQ)]            = "MM_SET_FILTER_REQ",
    [MSG_I(MM_SET_FILTER_CFM)]            = "MM_SET_FILTER_CFM",
    [MSG_I(MM_SET_CHANNEL_REQ)]           = "MM_SET_CHANNEL_REQ",
    [MSG_I(MM_SET_CHANNEL_CFM)]           = "MM_SET_CHANNEL_CFM",
    [MSG_I(MM_SET_DTIM_REQ)]              = "MM_SET_DTIM_REQ",
    [MSG_I(MM_SET_DTIM_CFM)]              = "MM_SET_DTIM_CFM",
    [MSG_I(MM_SET_BEACON_INT_REQ)]        = "MM_SET_BEACON_INT_REQ",
    [MSG_I(MM_SET_BEACON_INT_CFM)]        = "MM_SET_BEACON_INT_CFM",
    [MSG_I(MM_SET_BASIC_RATES_REQ)]       = "MM_SET_BASIC_RATES_REQ",
    [MSG_I(MM_SET_BASIC_RATES_CFM)]       = "MM_SET_BASIC_RATES_CFM",
    [MSG_I(MM_SET_BSSID_REQ)]             = "MM_SET_BSSID_REQ",
    [MSG_I(MM_SET_BSSID_CFM)]             = "MM_SET_BSSID_CFM",
    [MSG_I(MM_SET_EDCA_REQ)]              = "MM_SET_EDCA_REQ",
    [MSG_I(MM_SET_EDCA_CFM)]              = "MM_SET_EDCA_CFM",
    [MSG_I(MM_SET_MODE_REQ)]              = "MM_SET_MODE_REQ",
    [MSG_I(MM_SET_MODE_CFM)]              = "MM_SET_MODE_CFM",
    [MSG_I(MM_SET_VIF_STATE_REQ)]         = "MM_SET_VIF_STATE_REQ",
    [MSG_I(MM_SET_VIF_STATE_CFM)]         = "MM_SET_VIF_STATE_CFM",
    [MSG_I(MM_SET_SLOTTIME_REQ)]          = "MM_SET_SLOTTIME_REQ",
    [MSG_I(MM_SET_SLOTTIME_CFM)]          = "MM_SET_SLOTTIME_CFM",
    [MSG_I(MM_SET_IDLE_REQ)]              = "MM_SET_IDLE_REQ",
    [MSG_I(MM_SET_IDLE_CFM)]              = "MM_SET_IDLE_CFM",
    [MSG_I(MM_KEY_ADD_REQ)]               = "MM_KEY_ADD_REQ",
    [MSG_I(MM_KEY_ADD_CFM)]               = "MM_KEY_ADD_CFM",
    [MSG_I(MM_KEY_DEL_REQ)]               = "MM_KEY_DEL_REQ",
    [MSG_I(MM_KEY_DEL_CFM)]               = "MM_KEY_DEL_CFM",
    [MSG_I(MM_BA_ADD_REQ)]                = "MM_BA_ADD_REQ",
    [MSG_I(MM_BA_ADD_CFM)]                = "MM_BA_ADD_CFM",
    [MSG_I(MM_BA_DEL_REQ)]                = "MM_BA_DEL_REQ",
    [MSG_I(MM_BA_DEL_CFM)]                = "MM_BA_DEL_CFM",
    [MSG_I(MM_PRIMARY_TBTT_IND)]          = "MM_PRIMARY_TBTT_IND",
    [MSG_I(MM_SECONDARY_TBTT_IND)]        = "MM_SECONDARY_TBTT_IND",
    [MSG_I(MM_SET_POWER_REQ)]             = "MM_SET_POWER_REQ",
    [MSG_I(MM_SET_POWER_CFM)]             = "MM_SET_POWER_CFM",
    [MSG_I(MM_DBG_TRIGGER_REQ)]           = "MM_DBG_TRIGGER_REQ",
    [MSG_I(MM_SET_PS_MODE_REQ)]           = "MM_SET_PS_MODE_REQ",
    [MSG_I(MM_SET_PS_MODE_CFM)]           = "MM_SET_PS_MODE_CFM",
    [MSG_I(MM_CHAN_CTXT_ADD_REQ)]         = "MM_CHAN_CTXT_ADD_REQ",
    [MSG_I(MM_CHAN_CTXT_ADD_CFM)]         = "MM_CHAN_CTXT_ADD_CFM",
    [MSG_I(MM_CHAN_CTXT_DEL_REQ)]         = "MM_CHAN_CTXT_DEL_REQ",
    [MSG_I(MM_CHAN_CTXT_DEL_CFM)]         = "MM_CHAN_CTXT_DEL_CFM",
    [MSG_I(MM_CHAN_CTXT_LINK_REQ)]        = "MM_CHAN_CTXT_LINK_REQ",
    [MSG_I(MM_CHAN_CTXT_LINK_CFM)]        = "MM_CHAN_CTXT_LINK_CFM",
    [MSG_I(MM_CHAN_CTXT_UNLINK_REQ)]      = "MM_CHAN_CTXT_UNLINK_REQ",
    [MSG_I(MM_CHAN_CTXT_UNLINK_CFM)]      = "MM_CHAN_CTXT_UNLINK_CFM",
    [MSG_I(MM_CHAN_CTXT_UPDATE_REQ)]      = "MM_CHAN_CTXT_UPDATE_REQ",
    [MSG_I(MM_CHAN_CTXT_UPDATE_CFM)]      = "MM_CHAN_CTXT_UPDATE_CFM",
    [MSG_I(MM_CHAN_CTXT_SCHED_REQ)]       = "MM_CHAN_CTXT_SCHED_REQ",
    [MSG_I(MM_CHAN_CTXT_SCHED_CFM)]       = "MM_CHAN_CTXT_SCHED_CFM",
    [MSG_I(MM_BCN_CHANGE_REQ)]            = "MM_BCN_CHANGE_REQ",
    [MSG_I(MM_BCN_CHANGE_CFM)]            = "MM_BCN_CHANGE_CFM",
    [MSG_I(MM_TIM_UPDATE_REQ)]            = "MM_TIM_UPDATE_REQ",
    [MSG_I(MM_TIM_UPDATE_CFM)]            = "MM_TIM_UPDATE_CFM",
    [MSG_I(MM_CONNECTION_LOSS_IND)]       = "MM_CONNECTION_LOSS_IND",
    [MSG_I(MM_CHANNEL_SWITCH_IND)]        = "MM_CHANNEL_SWITCH_IND",
    [MSG_I(MM_CHANNEL_PRE_SWITCH_IND)]    = "MM_CHANNEL_PRE_SWITCH_IND",
    [MSG_I(MM_REMAIN_ON_CHANNEL_REQ)]     = "MM_REMAIN_ON_CHANNEL_REQ",
    [MSG_I(MM_REMAIN_ON_CHANNEL_CFM)]     = "MM_REMAIN_ON_CHANNEL_CFM",
    [MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = "MM_REMAIN_ON_CHANNEL_EXP_IND",
    [MSG_I(MM_PS_CHANGE_IND)]             = "MM_PS_CHANGE_IND",
    [MSG_I(MM_TRAFFIC_REQ_IND)]           = "MM_TRAFFIC_REQ_IND",
    [MSG_I(MM_SET_PS_OPTIONS_REQ)]        = "MM_SET_PS_OPTIONS_REQ",
    [MSG_I(MM_SET_PS_OPTIONS_CFM)]        = "MM_SET_PS_OPTIONS_CFM",
    [MSG_I(MM_P2P_VIF_PS_CHANGE_IND)]     = "MM_P2P_VIF_PS_CHANGE_IND",
    [MSG_I(MM_CSA_COUNTER_IND)]           = "MM_CSA_COUNTER_IND",
    [MSG_I(MM_CHANNEL_SURVEY_IND)]        = "MM_CHANNEL_SURVEY_IND",
    [MSG_I(MM_SET_P2P_NOA_REQ)]           = "MM_SET_P2P_NOA_REQ",
    [MSG_I(MM_SET_P2P_OPPPS_REQ)]         = "MM_SET_P2P_OPPPS_REQ",
    [MSG_I(MM_SET_P2P_NOA_CFM)]           = "MM_SET_P2P_NOA_CFM",
    [MSG_I(MM_SET_P2P_OPPPS_CFM)]         = "MM_SET_P2P_OPPPS_CFM",
    [MSG_I(MM_CFG_RSSI_REQ)]              = "MM_CFG_RSSI_REQ",
    [MSG_I(MM_RSSI_STATUS_IND)]           = "MM_RSSI_STATUS_IND",
    [MSG_I(MM_CSA_FINISH_IND)]            = "MM_CSA_FINISH_IND",
    [MSG_I(MM_CSA_TRAFFIC_IND)]           = "MM_CSA_TRAFFIC_IND",
    [MSG_I(MM_MU_GROUP_UPDATE_REQ)]       = "MM_MU_GROUP_UPDATE_REQ",
    [MSG_I(MM_MU_GROUP_UPDATE_CFM)]       = "MM_MU_GROUP_UPDATE_CFM",
    [MSG_I(MM_GET_CAL_RESULT_REQ)]       = "MM_GET_CAL_RESULT_REQ",
    [MSG_I(MM_GET_CAL_RESULT_CFM)]       = "MM_GET_CAL_RESULT_CFM",
};

static const char *const ecrnx_dbgid2str[MSG_I(DBG_MAX)] = {
    [MSG_I(DBG_MEM_READ_REQ)]        = "DBG_MEM_READ_REQ",
    [MSG_I(DBG_MEM_READ_CFM)]        = "DBG_MEM_READ_CFM",
    [MSG_I(DBG_MEM_WRITE_REQ)]       = "DBG_MEM_WRITE_REQ",
    [MSG_I(DBG_MEM_WRITE_CFM)]       = "DBG_MEM_WRITE_CFM",
    [MSG_I(DBG_SET_MOD_FILTER_REQ)]  = "DBG_SET_MOD_FILTER_REQ",
    [MSG_I(DBG_SET_MOD_FILTER_CFM)]  = "DBG_SET_MOD_FILTER_CFM",
    [MSG_I(DBG_SET_SEV_FILTER_REQ)]  = "DBG_SET_SEV_FILTER_REQ",
    [MSG_I(DBG_SET_SEV_FILTER_CFM)]  = "DBG_SET_SEV_FILTER_CFM",
    [MSG_I(DBG_ERROR_IND)]           = "DBG_ERROR_IND",
    [MSG_I(DBG_GET_SYS_STAT_REQ)]    = "DBG_GET_SYS_STAT_REQ",
    [MSG_I(DBG_GET_SYS_STAT_CFM)]    = "DBG_GET_SYS_STAT_CFM",
};

static const char *const ecrnx_scanid2str[MSG_I(SCAN_MAX)] = {
    [MSG_I(SCAN_START_REQ)]          = "SCAN_START_REQ",
    [MSG_I(SCAN_START_CFM)]          = "SCAN_START_CFM",
    [MSG_I(SCAN_DONE_IND)]           = "SCAN_DONE_IND",
};

static const char *const ecrnx_tdlsid2str[MSG_I(TDLS_MAX)] = {
    [MSG_I(TDLS_CHAN_SWITCH_CFM)]        = "TDLS_CHAN_SWITCH_CFM",
    [MSG_I(TDLS_CHAN_SWITCH_REQ)]        = "TDLS_CHAN_SWITCH_REQ",
    [MSG_I(TDLS_CHAN_SWITCH_IND)]        = "TDLS_CHAN_SWITCH_IND",
    [MSG_I(TDLS_CHAN_SWITCH_BASE_IND)]   = "TDLS_CHAN_SWITCH_BASE_IND",
    [MSG_I(TDLS_CANCEL_CHAN_SWITCH_REQ)] = "TDLS_CANCEL_CHAN_SWITCH_REQ",
    [MSG_I(TDLS_CANCEL_CHAN_SWITCH_CFM)] = "TDLS_CANCEL_CHAN_SWITCH_CFM",
    [MSG_I(TDLS_PEER_PS_IND)]            = "TDLS_PEER_PS_IND",
    [MSG_I(TDLS_PEER_TRAFFIC_IND_REQ)]   = "TDLS_PEER_TRAFFIC_IND_REQ",
    [MSG_I(TDLS_PEER_TRAFFIC_IND_CFM)]   = "TDLS_PEER_TRAFFIC_IND_CFM",
};

#ifdef CONFIG_ECRNX_FULLMAC

static const char *const ecrnx_scanuid2str[MSG_I(SCANU_MAX)] = {
    [MSG_I(SCANU_START_REQ)]  = "SCANU_START_REQ",
    [MSG_I(SCANU_START_CFM)]  = "SCANU_START_CFM",
    [MSG_I(SCANU_JOIN_REQ)]   = "SCANU_JOIN_REQ",
    [MSG_I(SCANU_JOIN_CFM)]   = "SCANU_JOIN_CFM",
    [MSG_I(SCANU_RESULT_IND)] = "SCANU_RESULT_IND",
    [MSG_I(SCANU_FAST_REQ)]   = "SCANU_FAST_REQ",
    [MSG_I(SCANU_FAST_CFM)]   = "SCANU_FAST_CFM",
    [MSG_I(SCANU_CANCEL_REQ)]   = "SCANU_CANCEL_REQ",
    [MSG_I(SCANU_CANCEL_CFM)]   = "SCANU_CANCEL_CFM",
};

static const char *const ecrnx_meid2str[MSG_I(ME_MAX)] = {
    [MSG_I(ME_CONFIG_REQ)]           = "ME_CONFIG_REQ",
    [MSG_I(ME_CONFIG_CFM)]           = "ME_CONFIG_CFM",
    [MSG_I(ME_CHAN_CONFIG_REQ)]      = "ME_CHAN_CONFIG_REQ",
    [MSG_I(ME_CHAN_CONFIG_CFM)]      = "ME_CHAN_CONFIG_CFM",
    [MSG_I(ME_SET_CONTROL_PORT_REQ)] = "ME_SET_CONTROL_PORT_REQ",
    [MSG_I(ME_SET_CONTROL_PORT_CFM)] = "ME_SET_CONTROL_PORT_CFM",
    [MSG_I(ME_TKIP_MIC_FAILURE_IND)] = "ME_TKIP_MIC_FAILURE_IND",
    [MSG_I(ME_STA_ADD_REQ)]          = "ME_STA_ADD_REQ",
    [MSG_I(ME_STA_ADD_CFM)]          = "ME_STA_ADD_CFM",
    [MSG_I(ME_STA_DEL_REQ)]          = "ME_STA_DEL_REQ",
    [MSG_I(ME_STA_DEL_CFM)]          = "ME_STA_DEL_CFM",
    [MSG_I(ME_TX_CREDITS_UPDATE_IND)]= "ME_TX_CREDITS_UPDATE_IND",
    [MSG_I(ME_RC_STATS_REQ)]         = "ME_RC_STATS_REQ",
    [MSG_I(ME_RC_STATS_CFM)]         = "ME_RC_STATS_CFM",
    [MSG_I(ME_RC_SET_RATE_REQ)]      = "ME_RC_SET_RATE_REQ",
    [MSG_I(ME_TRAFFIC_IND_REQ)]      = "ME_TRAFFIC_IND_REQ",
    [MSG_I(ME_TRAFFIC_IND_CFM)]      = "ME_TRAFFIC_IND_CFM",
    [MSG_I(ME_SET_PS_MODE_REQ)]      = "ME_SET_PS_MODE_REQ",
    [MSG_I(ME_SET_PS_MODE_CFM)]      = "ME_SET_PS_MODE_CFM",
};

static const char *const ecrnx_smid2str[MSG_I(SM_MAX)] = {
    [MSG_I(SM_CONNECT_REQ)]       = "SM_CONNECT_REQ",
    [MSG_I(SM_CONNECT_CFM)]       = "SM_CONNECT_CFM",
    [MSG_I(SM_CONNECT_IND)]       = "SM_CONNECT_IND",
    [MSG_I(SM_DISCONNECT_REQ)]    = "SM_DISCONNECT_REQ",
    [MSG_I(SM_DISCONNECT_CFM)]    = "SM_DISCONNECT_CFM",
    [MSG_I(SM_DISCONNECT_IND)]    = "SM_DISCONNECT_IND",
    [MSG_I(SM_EXTERNAL_AUTH_REQUIRED_IND)] = "SM_EXTERNAL_AUTH_REQUIRED_IND",
    [MSG_I(SM_EXTERNAL_AUTH_REQUIRED_RSP)] = "SM_EXTERNAL_AUTH_REQUIRED_RSP",
};

static const char *const ecrnx_apmid2str[MSG_I(APM_MAX)] = {
    [MSG_I(APM_START_REQ)]     = "APM_START_REQ",
    [MSG_I(APM_START_CFM)]     = "APM_START_CFM",
    [MSG_I(APM_STOP_REQ)]      = "APM_STOP_REQ",
    [MSG_I(APM_STOP_CFM)]      = "APM_STOP_CFM",
    [MSG_I(APM_START_CAC_REQ)] = "APM_START_CAC_REQ",
    [MSG_I(APM_START_CAC_CFM)] = "APM_START_CAC_CFM",
    [MSG_I(APM_STOP_CAC_REQ)]  = "APM_STOP_CAC_REQ",
    [MSG_I(APM_STOP_CAC_CFM)]  = "APM_STOP_CAC_CFM",
};

static const char *const ecrnx_meshid2str[MSG_I(MESH_MAX)] = {
    [MSG_I(MESH_START_REQ)]        = "MESH_START_REQ",
    [MSG_I(MESH_START_CFM)]        = "MESH_START_CFM",
    [MSG_I(MESH_STOP_REQ)]         = "MESH_STOP_REQ",
    [MSG_I(MESH_STOP_CFM)]         = "MESH_STOP_CFM",
    [MSG_I(MESH_UPDATE_REQ)]       = "MESH_UPDATE_REQ",
    [MSG_I(MESH_UPDATE_CFM)]       = "MESH_UPDATE_CFM",
    [MSG_I(MESH_PATH_CREATE_REQ)]  = "MESH_PATH_CREATE_REQ",
    [MSG_I(MESH_PATH_CREATE_CFM)]  = "MESH_PATH_CREATE_CFM",
    [MSG_I(MESH_PATH_UPDATE_REQ)]  = "MESH_PATH_UPDATE_REQ",
    [MSG_I(MESH_PATH_UPDATE_CFM)]  = "MESH_PATH_UPDATE_CFM",
    [MSG_I(MESH_PROXY_ADD_REQ)]    = "MESH_PROXY_ADD_REQ",
    [MSG_I(MESH_PEER_UPDATE_IND)]  = "MESH_PEER_UPDATE_IND",
    [MSG_I(MESH_PATH_UPDATE_IND)]  = "MESH_PATH_UPDATE_IND",
    [MSG_I(MESH_PROXY_UPDATE_IND)] = "MESH_PROXY_UPDATE_IND",
};

static const char *const ecrnx_twtid2str[MSG_I(TWT_MAX)] = {
    [MSG_I(TWT_SETUP_REQ)]         = "TWT_SETUP_REQ",
    [MSG_I(TWT_SETUP_CFM)]         = "TWT_SETUP_CFM",
    [MSG_I(TWT_SETUP_IND)]         = "TWT_SETUP_IND",
    [MSG_I(TWT_TEARDOWN_REQ)]      = "TWT_TEARDOWN_REQ",
    [MSG_I(TWT_TEARDOWN_CFM)]      = "TWT_TEARDOWN_CFM",
};

#if defined(CONFIG_ECRNX_P2P)
static const char *const rwnx_p2plistenid2str[MSG_I(P2P_LISTEN_MAX)] = {
    [MSG_I(P2P_LISTEN_START_REQ)]  = "P2P_LISTEN_START_REQ",
    [MSG_I(P2P_LISTEN_START_CFM)]  = "P2P_LISTEN_START_CFM",
    [MSG_I(P2P_CANCEL_LISTEN_REQ)]   = "P2P_CANCEL_LISTEN_REQ",
    [MSG_I(P2P_CANCEL_LISTEN_CFM)]   = "P2P_CANCEL_LISTEN_CFM",
};
#endif

#endif /* CONFIG_ECRNX_FULLMAC */

const char *const *ecrnx_id2str[TASK_LAST_EMB + 1] = {
    [TASK_MM]    = ecrnx_mmid2str,
    [TASK_DBG]   = ecrnx_dbgid2str,
    [TASK_SCAN]  = ecrnx_scanid2str,
    [TASK_TDLS]  = ecrnx_tdlsid2str,
#ifdef CONFIG_ECRNX_FULLMAC
    [TASK_SCANU] = ecrnx_scanuid2str,
    [TASK_ME]    = ecrnx_meid2str,
    [TASK_SM]    = ecrnx_smid2str,
    [TASK_APM]   = ecrnx_apmid2str,
    [TASK_MESH]  = ecrnx_meshid2str,
#if defined(CONFIG_ECRNX_P2P)
    [TASK_P2P_LISTEN]   = rwnx_p2plistenid2str,
#endif
    [TASK_TWT]   = ecrnx_twtid2str,
#endif
};
