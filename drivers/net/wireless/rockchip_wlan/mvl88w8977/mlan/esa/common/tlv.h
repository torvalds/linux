/** @file tlv.h
 *
 *  @brief Definitions of the Marvell TLV and parsing functions.
 *
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/*****************************************************************************
 *
 * File: tlv.h
 *
 *
 *
 * Author(s):    Kapil Chhabra
 * Date:         2005-01-27
 * Description:  Definitions of the Marvell TLV and parsing functions.
 *
 *****************************************************************************/
#ifndef TLV_H__
#define TLV_H__

#include "IEEE_types.h"

#define PROPRIETARY_TLV_BASE_ID         0x0100

/* Terminating TLV Type */
#define MRVL_TERMINATE_TLV_ID           0xffff

/* Defines for MRVL TLV IDs*/

/* IEEE TLVs*/
#define MRVL_SSID_TLV_ID                0x0000
#define MRVL_RATES_TLV_ID               0x0001
#define MRVL_PHYPARAMFHSET_TLV_ID       0x0002
#define MRVL_PHYPARAMDSSET_TLV_ID       0x0003
#define MRVL_CFPARAMSET_TLV_ID          0x0004
#define MRVL_IBSSPARAMSET_TLV_ID        0x0006
#define MRVL_COUNTRY_TLV_ID             0x0007
#define MRVL_PWR_CONSTRAINT_TLV_ID      0x0020
#define MRVL_PWR_CAPABILITY_TLV_ID      0x0021
#define MRVL_SUPPORTEDCHANNELS_TLV_ID   0x0024
#define MRVL_QUIET_TLV_ID               0x0028
#define MRVL_IBSSDFS_TLV_ID             0x0029
#define MRVL_HT_CAPABILITY_TLV_ID       0x002d
#define MRVL_QOSCAPABILITY_TLV_ID       0x002e
#define MRVL_RSN_TLV_ID                 0x0030
#define MRVL_SUPPORTED_REGCLASS_TLV_ID  0x003b
#define MRVL_HT_INFORMATION_TLV_ID      0x003d
#define MRVL_SECONDARY_CHAN_OFFSET      0x003e
#define MRVL_2040_BSS_COEX_TLV_ID       0x0048
#define MRVL_OVERLAP_BSS_SCAN_TLV_ID    0x004a
#define MRVL_EXTENDED_CAP_TLV_ID        0x007f
#define MRVL_VHT_CAPABILITIES_TLV_ID    0x00bf
#define MRVL_VHT_OPERATION_TLV_ID       0x00c0
#define MRVL_AID_TLV_ID                 0x00c5
#define MRVL_VHT_OPMODENTF_TLV_ID       0x00c7
#define MRVL_VENDORSPECIFIC_TLV_ID      0x00dd

/* Some of these TLV ids are used in ROM and should not be updated.
**  You can confirm if it is not being used in rom then it can be updated.
*/
/* Proprietary TLVs */
#define MRVL_KEYPARAMSET_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x00)
#define MRVL_CHANNELLIST_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x01)
#define MRVL_NUMPROBES_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x02)
#define MRVL_OMNI_TLV_ID                (PROPRIETARY_TLV_BASE_ID + 0x03)
#define MRVL_RSSITHRESHOLD_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x04)
#define MRVL_SNRTHRESHOLD_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x05)
#define MRVL_BCNLOWRSSITHRESHOLD_TLV_ID    MRVL_RSSITHRESHOLD_TLV_ID
#define MRVL_BCNLOWSNRTHRESHOLD_TLV_ID     MRVL_SNRTHRESHOLD_TLV_ID
#define MRVL_FAILURECOUNT_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x06)
#define MRVL_BEACONMISSED_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x07)
#define MRVL_LEDGPIO_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 0x08)
#define MRVL_LEDBEHAVIOR_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x09)
#define MRVL_PASSTHROUGH_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x0a)
#define MRVL_REASSOCAP_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x0b)
#define MRVL_POWER_TBL_2_4GHZ_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x0c)
#define MRVL_POWER_TBL_5GHZ_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x0d)
/* sending Brocast SSID */
#define MRVL_BCASTPROBE_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x0e)
/* number of SSIDs for which directed probes need to be generated */
#define MRVL_NUMSSIDPROBE_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x0f)
#define MRVL_WMMQSTATUS_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x10)
#define MRVL_CRYPTO_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 0x11)
#define MRVL_WILDCARD_SSID_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x12)
#define MRVL_TSFTIMESTAMP_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x13)
#define MRVL_POWER_ADAPT_CFG_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x14)
#define MRVL_HOSTSLEEP_FILTER_TYPE1_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x15)
#define MRVL_BCNHIGHRSSITHRESHOLD_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x16)
#define MRVL_BCNHIGHSNRTHRESHOLD_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x17)
#define MRVL_AUTOTX_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 0x18)
#define MRVL_WSC_SELECTED_REGISTRAR_TLV (PROPRIETARY_TLV_BASE_ID + 0x19)
#define MRVL_WSC_ENROLLEE_TMO_TLV       (PROPRIETARY_TLV_BASE_ID + 0x1a)
#define MRVL_WSC_ENROLLEE_PROBE_REQ_TLV (PROPRIETARY_TLV_BASE_ID + 0x1b)
#define MRVL_WSC_REGISTRAR_BEACON_TLV   (PROPRIETARY_TLV_BASE_ID + 0x1c)
#define MRVL_WSC_REGISTRAR_PROBE_RESP_TLV  (PROPRIETARY_TLV_BASE_ID + 0x1d)
#define MRVL_STARTBGSCANLATER_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x1e)
#define MRVL_AUTHTYPE_TLV_ID            (PROPRIETARY_TLV_BASE_ID + 0x1f)
#define MRVL_STA_MAC_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 0x20)
#define MRVL_CUSTOM_ADHOC_PROBE_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x21)
#define MRVL_CUSTOM_ADHOC_PYXIS_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x22)
#define MRVL_CUSTOM_BSSID_LIST_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x23)
#define MRVL_CUSTOM_LINK_INDICATION_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x24)
#define MRVL_MESHIE_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 0x25)
#define MRVL_DATA_LOWRSSITHRESHOLD_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x26)
#define MRVL_DATA_LOWSNRTHRESHOLD_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x27)
#define MRVL_DATA_HIGHRSSITHRESHOLD_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x28)
#define MRVL_DATA_HIGHSNRTHRESHOLD_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x29)
#define MRVL_CHANNELBANDLIST_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x2a)
#define MRVL_AP_MAC_ADDRESS_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x2b)
#define MRVL_BEACON_PERIOD_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x2c)
#define MRVL_DTIM_PERIOD_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x2d)
#define MRVL_BASIC_RATES_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x2e)
#define MRVL_TX_POWER_TLV_ID            (PROPRIETARY_TLV_BASE_ID + 0x2f)
#define MRVL_BCAST_SSID_CTL_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x30)
#define MRVL_PREAMBLE_CTL_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x31)
#define MRVL_ANTENNA_CTL_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x32)
#define MRVL_RTS_THRESHOLD_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x33)
#define MRVL_RADIO_CTL_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x34)
#define MRVL_TX_DATA_RATE_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x35)
#define MRVL_PKT_FWD_CTL_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x36)
#define MRVL_STA_INFO_TLV_ID            (PROPRIETARY_TLV_BASE_ID + 0x37)
#define MRVL_STA_MAC_ADDR_FILTER_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x38)
#define MRVL_STA_AGEOUT_TIMER_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x39)
#define MRVL_SECURITY_CFG_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x3a)
#define MRVL_WEP_KEY_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 0x3b)
#define MRVL_WPA_PASSPHRASE_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x3c)
#define MRVL_SCAN_TIMING_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x3d)
#define MRVL_NEIGHBOR_ENTRY_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x3e)
#define MRVL_NEIGHBOR_SCAN_PERIOD_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x3f)
#define MRVL_ENCRYPTION_PROTOCOL_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x40)
#define MRVL_AKMP_TLV_ID                (PROPRIETARY_TLV_BASE_ID + 0x41)
#define MRVL_CIPHER_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 0x42)
#define MRVL_OFFLOAD_ENABLE_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x43)
#define MRVL_SUPPLICANT_PMK_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x44)
#define MRVL_SUPPLICANT_PASSPHRASE_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x45)
#define MRVL_FRAG_THRESHOLD_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x46)
#define MRVL_GRP_REKEY_TIME_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x47)
#define MRVL_ICV_ERROR_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x48)
#define MRVL_PRE_BEACONMISSED_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x49)
#define MRVL_OLD_HT_CAPABILITY_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x4a)
#define MRVL_OLD_HT_INFORMATION_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x4b)
#define MRVL_OLD_SECONDARY_CHAN_OFFSET_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x4c)
#define MRVL_OLD_2040_BSS_COEX_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x4d)
#define MRVL_OLD_OVERLAP_BSS_SCAN_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x4e)
#define MRVL_OLD_EXTENDED_CAP_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x4f)
#define MRVL_HT_OPERATIONAL_MCSSET_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x50)
#define MRVL_RATEDROPPATTERN_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x51)
#define MRVL_RATEDROPCONTROL_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x52)
#define MRVL_RATESCOPE_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x53)
#define MRVL_TYPES_POWER_GROUP_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x54)
#define MRVL_MAX_STA_CNT_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x55)
#define MRVL_BSS_SCAN_RSP_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x56)
#define MRVL_BSS_SCAN_INFO_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x57)
#define MRVL_CHANRPT_BCN_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x58)
#define MRVL_CHANRPT_CHAN_LOAD_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x59)
#define MRVL_CHANRPT_NOISE_HIST_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x5a)
#define MRVL_CHANRPT_11H_BASIC_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x5b)
#define MRVL_CHANRPT_FRAME_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x5c)
#define MRVL_RETRY_LIMIT_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x5d)
#define MRVL_WAPI_TLV_ID                (PROPRIETARY_TLV_BASE_ID + 0x5e)
#define MRVL_ASSOC_REASON_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x5f)
#define MRVL_ROBUST_COEX_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x60)
#define MRVL_ROBUST_COEX_PERIOD_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x61)
#define MRVL_MCBC_DATA_RATE_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x62)
#define MRVL_MEASUREMENT_TIMING_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x63)
#define MRVL_RSN_REPLAY_PROT_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x64)
#define MRVL_WAPI_INFO_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x67)
#define MRVL_MGMT_FRAME_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x68)
#define MRVL_MGMT_IE_LIST_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x69)
#define MRVL_AP_SLEEP_PARAM_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x6a)
#define MRVL_AP_INACT_SLEEP_PARAM_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x6b)
#define MRVL_AP_BT_COEX_COMMON_CFG_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x6c)
#define MRVL_AP_BT_COEX_SCO_CFG_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x6d)
#define MRVL_AP_BT_COEX_ACL_CFG_TLV_ID        (PROPRIETARY_TLV_BASE_ID + 0x6e)
#define MRVL_AP_BT_COEX_STATS_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x6f)
#define MRVL_MGMT_PASSTHRU_MASK_TLV_ID         (PROPRIETARY_TLV_BASE_ID + 0x70)
#define MRVL_AUTO_DEEP_SLEEP_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 0x71)
#define MRVL_ENHANCED_STA_POWER_SAVE_MODE_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x72)
#define MRVL_HOSTWAKE_STADB_CFG_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x73)
#define MRVL_HOSTWAKE_OUI_CFG_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x74)
#define MRVL_EAPOL_PWK_HSK_TIMEOUT_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x75)
#define MRVL_EAPOL_PWK_HSK_RETRIES_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x76)
#define MRVL_EAPOL_GWK_HSK_TIMEOUT_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x77)
#define MRVL_EAPOL_GWK_HSK_RETRIES_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x78)

#define MRVL_OPCHAN_CONTROL_DESC_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x79)
#define MRVL_OPCHAN_CHANGRP_CTRL_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x7a)
#define MRVL_PS_STA_AGEOUT_TIMER_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x7b)

#define MRVL_WFD_DISC_PERIOD_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x7c)
#define MRVL_WFD_SCAN_ENABLE_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x7d)
#define MRVL_WFD_SCAN_PEER_DEVICE_ID_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x7e)
#define MRVL_WFD_REQ_DEVICE_TYPE_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x7f)
#define MRVL_WFD_DEVICE_STATE_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x80)
#define MRVL_WFD_INTENT_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x81)
#define MRVL_WFD_CAPABILITY_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x82)
#define MRVL_WFD_NOA_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 0x83)
#define MRVL_WFD_OPP_PS_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x84)
#define MRVL_WFD_INVITATION_LIST_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x85)
#define MRVL_WFD_LISTEN_CHANNEL_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x86)
#define MRVL_WFD_OPERATING_CHANNEL_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x87)
#define MRVL_WFD_PERSISTENT_GROUP_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x88)
#define MRVL_CHANNEL_TRPC_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x89)

#define MRVL_IEEE_ACTION_FRAME_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0x8c)
#define MRVL_WIFI_DIRECT_PRESENCE_REQ_PARAMS_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x8d)
#define MRVL_WIFI_DIRECT_EXTENDED_LISTEN_TIME_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0x8e)
#define MRVL_WIFI_DIRECT_PROVISIONING_PARAMS_TLV_ID  (PROPRIETARY_TLV_BASE_ID + 0x8f)
#define MRVL_WIFI_DIRECT_WPS_PARAMS_TLV_ID     (PROPRIETARY_TLV_BASE_ID + 0x90)
#define MRVL_WIFI_DIRECT_ACTION_FRAME_SEND_TIMEOUT_TLV_ID   (PROPRIETARY_TLV_BASE_ID + 0xb3)

#define MRVL_CIPHER_PWK_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x91)
#define MRVL_CIPHER_GWK_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x92)
#define MRVL_AP_BSS_STATUS_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x93)
#define MRVL_TX_DATA_PAUSE_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0x94)
#define MRVL_STICKY_TIM_CONFIG_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x96)
#define MRVL_STICKY_TIM_STA_MAC_ADDR_TLV_ID    (PROPRIETARY_TLV_BASE_ID + 0x97)
#define MRVL_2040_BSS_COEX_CONTROL_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0x98)
#define MRVL_KEYPARAMSET_V2_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 0x9c)
#define MRVL_RXBA_SYNC_TLV_ID                  (PROPRIETARY_TLV_BASE_ID + 0x99)
#define MRVL_PKT_COALESCE_RULE_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0x9a)
#define MRVL_NETWORK_LIST_CFG_TLV              (PROPRIETARY_TLV_BASE_ID + 0X9b)
#define MRVL_MEF_CFG_TLV_ID                    (PROPRIETARY_TLV_BASE_ID + 0x9d)

#define MRVL_WFD_SCAN_CFG_TLV                   (PROPRIETARY_TLV_BASE_ID + 158)
#define MRVL_WFD_SENDTIMEOUT_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 159)
#define MRVL_WFD_GROUPID_TLV_ID                 (PROPRIETARY_TLV_BASE_ID + 160)
#define MRVL_WFD_DEVICE_ID_TLV_ID               (PROPRIETARY_TLV_BASE_ID + 161)
#define MRVL_WFD_INTENDEDINTF_ADDR_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 162)
#define MRVL_WFD_STATUS_TLV_ID                  (PROPRIETARY_TLV_BASE_ID + 163)
#define MRVL_WFD_DEVICE_INFO_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 164)
#define MRVL_WFD_CFG_TIMEOUT_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 165)
#define MRVL_WFD_INVITATION_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 166)
#define MRVL_WFD_GROUP_BSSID_TLV_ID             (PROPRIETARY_TLV_BASE_ID + 167)
#define MRVL_WFD_WPA_PSK_TLV_ID                 (PROPRIETARY_TLV_BASE_ID + 168)
#define MRVL_MAX_MGMT_IE_TLV_ID                 (PROPRIETARY_TLV_BASE_ID + 170)
#define MRVL_REGION_DOMAIN_CODE_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 171)
#define MRVL_AOIP_IBSS_MODE_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 172)
#define MRVL_AOIP_MANAGE_PEERS_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 173)
#define MRVL_AOIP_STA_INFO_TLV_ID               (PROPRIETARY_TLV_BASE_ID + 174)
#define MRVL_AOIP_REMOTE_ADDR_MODE_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 175)
#define MRVL_BGSCAN_REPEAT_CNT_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 0xb0)

#define MRVL_TLV_USB_AGGR_PARAM                 (PROPRIETARY_TLV_BASE_ID + 177)

#define MRVL_PS_PARAMS_IN_HS_TLV_ID            (PROPRIETARY_TLV_BASE_ID + 0xb5)
#define MRVL_HS_WAKE_HOLDOFF_TLV_ID            (PROPRIETARY_TLV_BASE_ID + 0xb6)

#define MRVL_MULTI_CHAN_INFO_TLV_ID            (PROPRIETARY_TLV_BASE_ID + 0xb7)
#define MRVL_MULTI_CHAN_GROUP_INFO_TLV_ID      (PROPRIETARY_TLV_BASE_ID + 0xb8)
#define MRVL_RESTRICT_CLIENT_MODE_TLV_ID       (PROPRIETARY_TLV_BASE_ID + 0xc1)

#define MRVL_WFD_SERVICE_HASH_TLV_ID           (PROPRIETARY_TLV_BASE_ID + 195)
#define MRVL_WFD_SERVICES_LIST_TLV_ID          (PROPRIETARY_TLV_BASE_ID + 196)
#define MRVL_API_VER_INFO_TLV_ID			   (PROPRIETARY_TLV_BASE_ID + 199)

#define MRVL_FLOOR_RATE_TLV_ID                 (PROPRIETARY_TLV_BASE_ID + 0xb9)
#define MRVL_SCAN_CHAN_GAP_TLV_ID              (PROPRIETARY_TLV_BASE_ID + 0xC5)
#define MRVL_CHAN_STATS_TLV_ID                 (PROPRIETARY_TLV_BASE_ID + 0xC6)

/* This struct is used in ROM and should not be changed at all */
typedef MLAN_PACK_START struct {
	UINT16 Type;
	UINT16 Length;
} MLAN_PACK_END MrvlIEParamSet_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 Value[1];
} MLAN_PACK_END MrvlIEGeneric_t;

/* MultiChannel TLV*/
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 Status;		// 1 = Active, 0 = Inactive
	UINT8 TlvBuffer[1];
} MLAN_PACK_END MrvlIEMultiChanInfo_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 ChanGroupId;
	UINT8 ChanBufWt;
	ChanBandInfo_t ChanBandInfo;
	UINT32 ChanTime;
	UINT32 Reserved;
	UINT8 HidPortNum;
	UINT8 NumIntf;
	UINT8 BssTypeNumList[1];
} MLAN_PACK_END MrvlIEMultiChanGroupInfo_t;

/* Key Material TLV */
typedef MLAN_PACK_START struct MrvlIEKeyParamSet_t {
	MrvlIEParamSet_t hdr;
	UINT16 keyMgtId;
} MLAN_PACK_END MrvlIEKeyParamSet_t;

#ifdef KEY_MATERIAL_V2

typedef MLAN_PACK_START struct wep_key_t {
	UINT16 len;
	UINT8 key[1];
} MLAN_PACK_END wep_key_t;

typedef MLAN_PACK_START struct wpax_key_t {
	UINT8 pn[8];
	UINT16 len;
	UINT8 key[1];
} MLAN_PACK_END wpax_key_t;

typedef MLAN_PACK_START struct wapi_key_t {
	UINT8 pn[16];
	UINT16 len;
	UINT8 key[16];
	UINT8 micKey[16];
} MLAN_PACK_END wapi_key_t;

typedef MLAN_PACK_START struct MrvlIEKeyParamSet_v2_t {
	MrvlIEParamSet_t hdr;
	IEEEtypes_MacAddr_t macAddr;
	UINT8 keyIdx;
	UINT8 keyType;
	UINT16 keyInfo;

	MLAN_PACK_START union {
		wep_key_t wep;
		wpax_key_t wpax;
		wapi_key_t wapi;
	} MLAN_PACK_END keySet;

} MLAN_PACK_END MrvlIEKeyParamSet_v2_t;
#endif

/* Marvell Power Constraint TLV */
typedef MLAN_PACK_START struct MrvlIEPowerConstraint_t {
	MrvlIEParamSet_t IEParam;
	UINT8 channel;
	UINT8 dBm;
} MLAN_PACK_END MrvlIEPowerConstraint_t;

/* Marvell WSC Selected Registar TLV */
typedef MLAN_PACK_START struct MrvlIEWSCSelectedRegistrar_t {
	MrvlIEParamSet_t IEParam;
	UINT16 devPwdID;
} MLAN_PACK_END MrvlIEWSCSelectedRegistrar_t;

/* Marvell WSC Enrollee TMO TLV */
typedef MLAN_PACK_START struct MrvlIEWSCEnrolleeTmo_t {
	MrvlIEParamSet_t IEParam;
	UINT16 tmo;
} MLAN_PACK_END MrvlIEWSCEnrolleeTmo_t;

/****************
 * AES CRYPTION FEATURE
 *
 * DEFINE STARTS --------------
 */
typedef MLAN_PACK_START struct MrvlIEAesCrypt_t {
	MrvlIEParamSet_t hdr;
	UINT8 payload[40];
} MLAN_PACK_END MrvlIEAesCrypt_t;

/* DEFINE ENDS ----------------
 */

/* Marvell Power Capability TLV */
typedef MLAN_PACK_START struct MrvlIEPowerCapability_t {
	MrvlIEParamSet_t IEParam;
	UINT8 minPwr;
	UINT8 maxPwr;
} MLAN_PACK_END MrvlIEPowerCapability_t;

/* Marvell TLV for OMNI Serial Number and Hw Revision Information. */
typedef MLAN_PACK_START struct MrvlIE_OMNI_t {
	MrvlIEParamSet_t IEParam;
	UINT8 SerialNumber[16];
	UINT8 HWRev;
	UINT8 Reserved[3];
} MLAN_PACK_END MrvlIE_OMNI_t;

/* Marvell LED Behavior TLV */
typedef MLAN_PACK_START struct MrvlIELedBehavior_t {
	MrvlIEParamSet_t IEParam;
	UINT8 FirmwareState;
	UINT8 LedNumber;
	UINT8 LedState;
	UINT8 LedArgs;
} MLAN_PACK_END MrvlIELedBehavior_t;

/* Marvell LED GPIO Mapping TLV */
typedef MLAN_PACK_START struct MrvlIELedGpio_t {
	MrvlIEParamSet_t IEParam;
	UINT8 LEDNumber;
	UINT8 GPIONumber;
} MLAN_PACK_END MrvlIELedGpio_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	/*
	 ** Set a place holder for the TSF values.  Sized to max BSS for message
	 **   allocation. The TLV will return a variable number of TSF values.
	 */
	UINT64 TSFValue[IEEEtypes_MAX_BSS_DESCRIPTS];
} MLAN_PACK_END MrvlIETsfArray_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 maxLen;
	IEEEtypes_SsId_t ssid;
} MLAN_PACK_END MrvlIEWildcardSsid_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 snrThreshold;
	UINT8 reportFrequency;
} MLAN_PACK_END MrvlIELowSnrThreshold_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 rssiThreshold;
	UINT8 reportFrequency;
} MLAN_PACK_END MrvlIELowRssiThreshold_t;

/* Marvell AutoTx TLV */
#define MAX_KEEPALIVE_PKT_LEN   (0x60)
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 Interval;	/* in seconds */
	UINT8 Priority;
	UINT8 Reserved;
	UINT16 EtherFrmLen;
	UINT8 DestAddr[6];
	UINT8 SrcAddr[6];
	UINT8 EtherFrmBody[MAX_KEEPALIVE_PKT_LEN];	//Last 4 bytes are 32bit FCS
} MLAN_PACK_END MrvlIEAutoTx_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	IEEEtypes_DFS_Map_t map;
} MLAN_PACK_END MrvlIEChanRpt11hBasic_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 scanReqId;
} MLAN_PACK_END MrvlIEChanRptBeacon_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 ccaBusyFraction;
} MLAN_PACK_END MrvlIEChanRptChanLoad_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	SINT16 anpi;
	UINT8 rpiDensities[11];
} MLAN_PACK_END MrvlIEChanRptNoiseHist_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	IEEEtypes_MacAddr_t sourceAddr;
	IEEEtypes_MacAddr_t bssid;
	SINT16 rssi;
	UINT16 frameCnt;
} MLAN_PACK_END MrvlIEChanRptFrame_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	SINT16 rssi;
	SINT16 anpi;
	UINT8 ccaBusyFraction;
#ifdef SCAN_REPORT_THROUGH_EVENT
	BandConfig_t band;
	UINT8 channel;
	UINT8 reserved;
	UINT64 tsf;
#endif
} MLAN_PACK_END MrvlIEBssScanStats_t;

typedef MLAN_PACK_START struct {
    /** Header */
	MrvlIEParamSet_t IEParam;
	UINT32 mode;
	UINT32 maxOff;
	UINT32 maxOn;
} MLAN_PACK_END MrvlIETypes_MeasurementTiming_t;

typedef MLAN_PACK_START struct {
    /** Header */
	MrvlIEParamSet_t IEParam;
	UINT32 mode;
	UINT32 dwell;
	UINT32 maxOff;
	UINT32 minLink;
	UINT32 rspTimeout;
} MLAN_PACK_END MrvlIETypes_ConfigScanTiming_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 KeyIndex;
	UINT8 IsDefaultIndex;
	UINT8 Value[1];
} MLAN_PACK_END MrvlIETypes_WepKey_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 PMK[32];
} MLAN_PACK_END MrvlIETypes_PMK_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 ssid[1];
} MLAN_PACK_END MrvlIETypes_Ssid_Param_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 Passphrase[64];
} MLAN_PACK_END MrvlIETypes_Passphrase_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 BSSID[6];
} MLAN_PACK_END MrvlIETypes_BSSID_t;

typedef MLAN_PACK_START struct {
	UINT16 Type;
	UINT16 Length;
	IEEEtypes_MacAddr_t Bssid;
	UINT16 Rsvd;
	SINT16 Rssi;		//Signal strength
	UINT16 Age;
	UINT32 QualifiedNeighborBitmap;
	UINT32 BlackListDuration;
} MLAN_PACK_END MrvlIETypes_NeighbourEntry_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 SearchMode;
	UINT16 State;
	UINT32 ScanPeriod;
} MLAN_PACK_END MrvlIETypes_NeighbourScanPeriod_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 Rssi;
	UINT8 Frequency;
} MLAN_PACK_END MrvlIETypes_BeaconHighRssiThreshold_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 Rssi;
	UINT8 Frequency;
} MLAN_PACK_END MrvlIETypes_BeaconLowRssiThreshold_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 Value;
	UINT8 Frequency;
} MLAN_PACK_END MrvlIETypes_RoamingAgent_Threshold_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 AssocReason;
} MLAN_PACK_END MrvlIETypes_AssocReason_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	IEEEtypes_MacAddr_t macAddr;
	UINT8 txPauseState;
	UINT8 totalQueued;
} MLAN_PACK_END MrvlIETypes_TxDataPause_t;

typedef MLAN_PACK_START struct {
	UINT16 startFreq;
	UINT8 chanWidth;
	UINT8 chanNum;
} MLAN_PACK_END MrvlIEChannelDesc_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	MrvlIEChannelDesc_t chanDesc;
	UINT16 controlFlags;
	UINT16 reserved;
	UINT8 activePower;
	UINT8 mdMinPower;
	UINT8 mdMaxPower;
	UINT8 mdPower;
} MLAN_PACK_END MrvlIETypes_OpChanControlDesc_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT32 chanGroupBitmap;
	ChanScanMode_t scanMode;
	UINT8 numChan;
	MrvlIEChannelDesc_t chanDesc[50];
} MLAN_PACK_END MrvlIETypes_ChanGroupControl_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	ChanBandInfo_t ChanBandInfo[IEEEtypes_MAX_BSS_DESCRIPTS];
} MLAN_PACK_END MrvlIEChanBandList_t;
#if 0
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	IEEEtypes_MacAddr_t srcAddr;
	IEEEtypes_MacAddr_t dstAddr;
	IEEEtypes_ActionFrame_t actionFrame;
} MLAN_PACK_END MrvlIEActionFrame_t;
#endif
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	HtEntry_t htEntry[IEEEtypes_MAX_BSS_DESCRIPTS];
} MLAN_PACK_END MrvlIEHtList_t;

/* This struct is used in ROM code and all the fields of
** this should be kept intact
*/
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 bmpRateOfHRDSSS;
	UINT16 bmpRateOfOFDM;
	UINT32 bmpRateOfHT_DW0;
	UINT32 bmpRateOfHT_DW1;
	UINT32 bmpRateOfHT_DW2;
	UINT32 bmpRateOfHT_DW3;
#ifdef DOT11AC
	UINT16 bmpRateOfVHT[8];	//per SS
#endif
} MLAN_PACK_END MrvlIE_TxRateScope_t;

typedef MLAN_PACK_START struct {
	UINT8 mod_class;
	UINT8 rate;
	UINT8 attemptLimit;
	UINT8 reserved;
} MLAN_PACK_END MrvlIE_RateInfoEntry_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	MrvlIE_RateInfoEntry_t rate_info[8];
} MLAN_PACK_END MrvlIE_RateDropTable_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT32 mode;
	// for 1x1 11n, 9 HT rate, 8 OFDM rate, 4 DSSS rate
	MrvlIE_RateDropTable_t rateDropTbls[9 + 8 + 4];
} MLAN_PACK_END MrvlIE_RateDropPattern_t;

#ifdef USB_FRAME_AGGR

#define USB_TX_AGGR_ENABLE ( 1 << 1 )
#define USB_RX_AGGR_ENABLE ( 1 << 0 )

#define USB_RX_AGGR_MODE_MASK   ( 1 << 0 )
#define USB_RX_AGGR_MODE_SIZE  (1)
#define USB_RX_AGGR_MODE_NUM   (0)

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 enable;
	UINT16 rx_mode;
	UINT16 rx_align;
	UINT16 rx_max;
	UINT16 rx_timeout;
	UINT16 tx_mode;
	UINT16 tx_align;
} MLAN_PACK_END MrvlIE_USBAggrTLV_t;

extern MrvlIE_USBAggrTLV_t g_Aggr_Conf;

#endif

typedef MLAN_PACK_START struct {
	UINT8 mod_class;
	UINT8 firstRateCode;
	UINT8 lastRateCode;
	SINT8 power_step;
	SINT8 min_power;
	SINT8 max_power;
	UINT8 ht_bandwidth;
	UINT8 reserved[1];
} MLAN_PACK_END MrvlIE_PowerGroupEntry_t;

#define MRVL_MAX_PWR_GROUP       15
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	MrvlIE_PowerGroupEntry_t PowerGroup[MRVL_MAX_PWR_GROUP];
} MLAN_PACK_END MrvlIE_PowerGroup_t;

#ifdef AP_STA_PS
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 NullPktInterval;
	UINT16 numDtims;
	UINT16 BCNMissTimeOut;
	UINT16 LocalListenInterval;
	UINT16 AdhocAwakePeriod;
	UINT16 PS_mode;
	UINT16 DelayToPS;
} MrvlIETypes_StaSleepParams_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 idleTime;
} MrvlIETypes_AutoDeepSleepParams_t;
#endif

#ifdef MESH
typedef MLAN_PACK_START struct _MrvlMeshIE_Tlv_t {
	MrvlIEParamSet_t hdr;
	IEEEtypes_VendorSpecific_MeshIE_t meshIE;

} MLAN_PACK_END MrvlMeshIE_Tlv_t;
#endif

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 RBCMode;
	UINT8 Reserved[3];
} MLAN_PACK_END MrvlIERobustCoex_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 Mode;
	UINT16 Reserved;
	UINT32 BTTime;
	UINT32 Period;
} MLAN_PACK_END MrvlIERobustCoexPeriod_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT8 staMacAddr[IEEEtypes_ADDRESS_SIZE];
	IEEEtypes_IE_Param_t IeBuf;
} MLAN_PACK_END MrvlIEHostWakeStaDBCfg;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 ouiCmpLen;
	UINT8 ouiBuf[6];
} MLAN_PACK_END MrvlIEHostWakeOuiCfg;

#ifdef MICRO_AP_MODE
/* This struct is used in ROM and should not be changed at all */
typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t hdr;
	IEEEtypes_MacAddr_t macAddr;
	UINT8 pwrMode;
	SINT8 rssi;
} MLAN_PACK_END MrvlIEStaInfo_t;
#endif

typedef struct {
	MrvlIEParamSet_t Hdr;
	uint16 protocol;
	uint8 cipher;
	uint8 reserved;
} MrvlIETypes_PwkCipher_t;

typedef struct {
	MrvlIEParamSet_t Hdr;
	uint8 cipher;
	uint8 reserved;
} MrvlIETypes_GwkCipher_t;

typedef MLAN_PACK_START struct {
	uint8 modGroup;
	uint8 txPower;
} MLAN_PACK_END MrvlIE_ChanTrpcEntry_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t Hdr;
	MrvlIEChannelDesc_t chanDesc;
	MrvlIE_ChanTrpcEntry_t data[1];
} MLAN_PACK_END MrvlIETypes_ChanTrpcCfg_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	IEEEtypes_MacAddr_t mac[IEEEtypes_MAX_BSS_DESCRIPTS];
} MLAN_PACK_END MrvlIETypes_MacAddr_t;

#ifdef AP_BTCOEX
typedef enum _tagScoCoexBtTraffic {
	ONLY_SCO,
	ACL_BEFORE_SCO,
	ACL_AFTER_SCO,
	BT_TRAFFIC_RESERVED,
	BT_TRAFFIC_MAX
} ScoCoexBtTraffic_e;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT32 configBitmap;	/* Bit    0 : overrideCts2SelfProtection
				 ** Bit 1-31 : Reserved
				 */
	UINT32 apStaBtCoexEnabled;
	UINT32 reserved[3];	/* For future use. */
} MLAN_PACK_END MrvlIETypes_ApBTCoexCommonConfig_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 protectionFrmQTime[BT_TRAFFIC_MAX];	/* Index 0 for ONLY_SCO
							 **       1 for ACL_BEFORE_SCO
							 **       2 for ACL_AFTER_SCO
							 **       3 is Reserved
							 */
	UINT16 protectionFrmRate;
	UINT16 aclFrequency;
	UINT32 reserved[4];	/* For future use. */
} MLAN_PACK_END MrvlIETypes_ApBTCoexScoConfig_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT16 enabled;
	UINT16 btTime;
	UINT16 wlanTime;
	UINT16 protectionFrmRate;
	UINT32 reserved[4];	/* For future use. */
} MLAN_PACK_END MrvlIETypes_ApBTCoexAclConfig_t;

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t IEParam;
	UINT32 nullNotSent;
	UINT32 numOfNullQueued;
	UINT32 nullNotQueued;
	UINT32 numOfCfEndQueued;
	UINT32 cfEndNotQueued;
	UINT32 nullAllocationFail;
	UINT32 cfEndAllocationFail;
	UINT32 reserved[8];	/* For future use. */
} MLAN_PACK_END MrvlIETypes_ApBTCoexStats_t;
#endif //AP_BTCOEX

typedef MLAN_PACK_START struct {
	MrvlIEParamSet_t Hdr;

	IEEEtypes_MacAddr_t macAddr;
	UINT8 tid;
	UINT8 reserved;
	UINT16 startSeqNum;

	UINT16 bitMapLen;
	UINT8 bitMap[1];
} MLAN_PACK_END MrvlIETypes_RxBaSync_t;

#ifdef SCAN_CHAN_STATISTICS
typedef MLAN_PACK_START struct MrvlIEChannelStats {
	MrvlIEParamSet_t IEParam;
	UINT8 chanStat[1];
} MLAN_PACK_END MrvlIEChannelStats_t;
#endif

/* API Version Info Entry for MRVL_API_VER_INFO_TLV_ID */
typedef MLAN_PACK_START struct MrvlIE_ApiVersionEntry_t {
	UINT16 apiId;
	UINT8 major;
	UINT8 minor;
} MLAN_PACK_END MrvlIE_ApiVersionEntry_t;

/** API Version Ids */
#define KEY_API_VER_ID		0x1
#endif //_TLV_H_
