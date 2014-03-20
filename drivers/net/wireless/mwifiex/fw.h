/*
 * Marvell Wireless LAN device driver: Firmware specific macros & structures
 *
 * Copyright (C) 2011, Marvell International Ltd.
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

#ifndef _MWIFIEX_FW_H_
#define _MWIFIEX_FW_H_

#include <linux/if_ether.h>


#define INTF_HEADER_LEN     4

struct rfc_1042_hdr {
	u8 llc_dsap;
	u8 llc_ssap;
	u8 llc_ctrl;
	u8 snap_oui[3];
	__be16 snap_type;
};

struct rx_packet_hdr {
	struct ethhdr eth803_hdr;
	struct rfc_1042_hdr rfc1042_hdr;
};

struct tx_packet_hdr {
	struct ethhdr eth803_hdr;
	struct rfc_1042_hdr rfc1042_hdr;
};

#define B_SUPPORTED_RATES               5
#define G_SUPPORTED_RATES               9
#define BG_SUPPORTED_RATES              13
#define A_SUPPORTED_RATES               9
#define HOSTCMD_SUPPORTED_RATES         14
#define N_SUPPORTED_RATES               3
#define ALL_802_11_BANDS           (BAND_A | BAND_B | BAND_G | BAND_GN | \
				    BAND_AN | BAND_GAC | BAND_AAC)

#define FW_MULTI_BANDS_SUPPORT  (BIT(8) | BIT(9) | BIT(10) | BIT(11) | \
				 BIT(12) | BIT(13))
#define IS_SUPPORT_MULTI_BANDS(adapter)        \
	(adapter->fw_cap_info & FW_MULTI_BANDS_SUPPORT)

/* shift bit 12 and bit 13 in fw_cap_info from the firmware to bit 13 and 14
 * for 11ac so that bit 11 is for GN, bit 12 for AN, bit 13 for GAC, and bit
 * bit 14 for AAC, in order to be compatible with the band capability
 * defined in the driver after right shift of 8 bits.
 */
#define GET_FW_DEFAULT_BANDS(adapter)  \
	    (((((adapter->fw_cap_info & 0x3000) << 1) | \
	       (adapter->fw_cap_info & ~0xF000)) >> 8) & \
	     ALL_802_11_BANDS)

#define HostCmd_WEP_KEY_INDEX_MASK              0x3fff

#define KEY_INFO_ENABLED        0x01
enum KEY_TYPE_ID {
	KEY_TYPE_ID_WEP = 0,
	KEY_TYPE_ID_TKIP,
	KEY_TYPE_ID_AES,
	KEY_TYPE_ID_WAPI,
	KEY_TYPE_ID_AES_CMAC,
};
#define KEY_MCAST	BIT(0)
#define KEY_UNICAST	BIT(1)
#define KEY_ENABLED	BIT(2)
#define KEY_IGTK	BIT(10)

#define WAPI_KEY_LEN			50

#define MAX_POLL_TRIES			100
#define MAX_FIRMWARE_POLL_TRIES			100

#define FIRMWARE_READY_SDIO				0xfedc
#define FIRMWARE_READY_PCIE				0xfedcba00

enum mwifiex_usb_ep {
	MWIFIEX_USB_EP_CMD_EVENT = 1,
	MWIFIEX_USB_EP_DATA = 2,
};

enum MWIFIEX_802_11_PRIVACY_FILTER {
	MWIFIEX_802_11_PRIV_FILTER_ACCEPT_ALL,
	MWIFIEX_802_11_PRIV_FILTER_8021X_WEP
};

#define CAL_SNR(RSSI, NF)		((s16)((s16)(RSSI)-(s16)(NF)))
#define CAL_RSSI(SNR, NF)		((s16)((s16)(SNR)+(s16)(NF)))

#define UAP_BSS_PARAMS_I			0
#define UAP_CUSTOM_IE_I				1
#define MWIFIEX_AUTO_IDX_MASK			0xffff
#define MWIFIEX_DELETE_MASK			0x0000
#define MGMT_MASK_ASSOC_REQ			0x01
#define MGMT_MASK_REASSOC_REQ			0x04
#define MGMT_MASK_ASSOC_RESP			0x02
#define MGMT_MASK_REASSOC_RESP			0x08
#define MGMT_MASK_PROBE_REQ			0x10
#define MGMT_MASK_PROBE_RESP			0x20
#define MGMT_MASK_BEACON			0x100

#define TLV_TYPE_UAP_SSID			0x0000
#define TLV_TYPE_UAP_RATES			0x0001

#define PROPRIETARY_TLV_BASE_ID                 0x0100
#define TLV_TYPE_KEY_MATERIAL       (PROPRIETARY_TLV_BASE_ID + 0)
#define TLV_TYPE_CHANLIST           (PROPRIETARY_TLV_BASE_ID + 1)
#define TLV_TYPE_NUMPROBES          (PROPRIETARY_TLV_BASE_ID + 2)
#define TLV_TYPE_RSSI_LOW           (PROPRIETARY_TLV_BASE_ID + 4)
#define TLV_TYPE_PASSTHROUGH        (PROPRIETARY_TLV_BASE_ID + 10)
#define TLV_TYPE_WMMQSTATUS         (PROPRIETARY_TLV_BASE_ID + 16)
#define TLV_TYPE_WILDCARDSSID       (PROPRIETARY_TLV_BASE_ID + 18)
#define TLV_TYPE_TSFTIMESTAMP       (PROPRIETARY_TLV_BASE_ID + 19)
#define TLV_TYPE_RSSI_HIGH          (PROPRIETARY_TLV_BASE_ID + 22)
#define TLV_TYPE_AUTH_TYPE          (PROPRIETARY_TLV_BASE_ID + 31)
#define TLV_TYPE_STA_MAC_ADDR       (PROPRIETARY_TLV_BASE_ID + 32)
#define TLV_TYPE_CHANNELBANDLIST    (PROPRIETARY_TLV_BASE_ID + 42)
#define TLV_TYPE_UAP_BEACON_PERIOD  (PROPRIETARY_TLV_BASE_ID + 44)
#define TLV_TYPE_UAP_DTIM_PERIOD    (PROPRIETARY_TLV_BASE_ID + 45)
#define TLV_TYPE_UAP_BCAST_SSID     (PROPRIETARY_TLV_BASE_ID + 48)
#define TLV_TYPE_UAP_RTS_THRESHOLD  (PROPRIETARY_TLV_BASE_ID + 51)
#define TLV_TYPE_UAP_AO_TIMER       (PROPRIETARY_TLV_BASE_ID + 57)
#define TLV_TYPE_UAP_WEP_KEY        (PROPRIETARY_TLV_BASE_ID + 59)
#define TLV_TYPE_UAP_WPA_PASSPHRASE (PROPRIETARY_TLV_BASE_ID + 60)
#define TLV_TYPE_UAP_ENCRY_PROTOCOL (PROPRIETARY_TLV_BASE_ID + 64)
#define TLV_TYPE_UAP_AKMP           (PROPRIETARY_TLV_BASE_ID + 65)
#define TLV_TYPE_UAP_FRAG_THRESHOLD (PROPRIETARY_TLV_BASE_ID + 70)
#define TLV_TYPE_RATE_DROP_CONTROL  (PROPRIETARY_TLV_BASE_ID + 82)
#define TLV_TYPE_RATE_SCOPE         (PROPRIETARY_TLV_BASE_ID + 83)
#define TLV_TYPE_POWER_GROUP        (PROPRIETARY_TLV_BASE_ID + 84)
#define TLV_TYPE_UAP_RETRY_LIMIT    (PROPRIETARY_TLV_BASE_ID + 93)
#define TLV_TYPE_WAPI_IE            (PROPRIETARY_TLV_BASE_ID + 94)
#define TLV_TYPE_UAP_MGMT_FRAME     (PROPRIETARY_TLV_BASE_ID + 104)
#define TLV_TYPE_MGMT_IE            (PROPRIETARY_TLV_BASE_ID + 105)
#define TLV_TYPE_AUTO_DS_PARAM      (PROPRIETARY_TLV_BASE_ID + 113)
#define TLV_TYPE_PS_PARAM           (PROPRIETARY_TLV_BASE_ID + 114)
#define TLV_TYPE_UAP_PS_AO_TIMER    (PROPRIETARY_TLV_BASE_ID + 123)
#define TLV_TYPE_PWK_CIPHER         (PROPRIETARY_TLV_BASE_ID + 145)
#define TLV_TYPE_GWK_CIPHER         (PROPRIETARY_TLV_BASE_ID + 146)
#define TLV_TYPE_COALESCE_RULE      (PROPRIETARY_TLV_BASE_ID + 154)

#define MWIFIEX_TX_DATA_BUF_SIZE_2K        2048

#define SSN_MASK         0xfff0

#define BA_RESULT_SUCCESS        0x0
#define BA_RESULT_TIMEOUT        0x2

#define IS_BASTREAM_SETUP(ptr)  (ptr->ba_status)

#define BA_STREAM_NOT_ALLOWED   0xff

#define IS_11N_ENABLED(priv) ((priv->adapter->config_bands & BAND_GN || \
			priv->adapter->config_bands & BAND_AN) && \
			priv->curr_bss_params.bss_descriptor.bcn_ht_cap)
#define INITIATOR_BIT(DelBAParamSet) (((DelBAParamSet) &\
			BIT(DELBA_INITIATOR_POS)) >> DELBA_INITIATOR_POS)

#define MWIFIEX_TX_DATA_BUF_SIZE_4K        4096
#define MWIFIEX_TX_DATA_BUF_SIZE_8K        8192

#define ISSUPP_11NENABLED(FwCapInfo) (FwCapInfo & BIT(11))

#define MWIFIEX_DEF_HT_CAP	(IEEE80211_HT_CAP_DSSSCCK40 | \
				 (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT) | \
				 IEEE80211_HT_CAP_SM_PS)

#define MWIFIEX_DEF_AMPDU	IEEE80211_HT_AMPDU_PARM_FACTOR

/* dev_cap bitmap
 * BIT
 * 0-16		reserved
 * 17		IEEE80211_HT_CAP_SUP_WIDTH_20_40
 * 18-22	reserved
 * 23		IEEE80211_HT_CAP_SGI_20
 * 24		IEEE80211_HT_CAP_SGI_40
 * 25		IEEE80211_HT_CAP_TX_STBC
 * 26		IEEE80211_HT_CAP_RX_STBC
 * 27-28	reserved
 * 29		IEEE80211_HT_CAP_GRN_FLD
 * 30-31	reserved
 */
#define ISSUPP_CHANWIDTH40(Dot11nDevCap) (Dot11nDevCap & BIT(17))
#define ISSUPP_SHORTGI20(Dot11nDevCap) (Dot11nDevCap & BIT(23))
#define ISSUPP_SHORTGI40(Dot11nDevCap) (Dot11nDevCap & BIT(24))
#define ISSUPP_TXSTBC(Dot11nDevCap) (Dot11nDevCap & BIT(25))
#define ISSUPP_RXSTBC(Dot11nDevCap) (Dot11nDevCap & BIT(26))
#define ISSUPP_GREENFIELD(Dot11nDevCap) (Dot11nDevCap & BIT(29))
#define ISENABLED_40MHZ_INTOLERANT(Dot11nDevCap) (Dot11nDevCap & BIT(8))
#define ISSUPP_RXLDPC(Dot11nDevCap) (Dot11nDevCap & BIT(22))

/* httxcfg bitmap
 * 0		reserved
 * 1		20/40 Mhz enable(1)/disable(0)
 * 2-3		reserved
 * 4		green field enable(1)/disable(0)
 * 5		short GI in 20 Mhz enable(1)/disable(0)
 * 6		short GI in 40 Mhz enable(1)/disable(0)
 * 7-15		reserved
 */
#define MWIFIEX_FW_DEF_HTTXCFG (BIT(1) | BIT(4) | BIT(5) | BIT(6))

#define GET_RXMCSSUPP(DevMCSSupported) (DevMCSSupported & 0x0f)
#define SETHT_MCS32(x) (x[4] |= 1)
#define HT_STREAM_2X2	0x22

#define SET_SECONDARYCHAN(RadioType, SECCHAN) (RadioType |= (SECCHAN << 4))

#define LLC_SNAP_LEN    8

/* HW_SPEC fw_cap_info */

#define ISSUPP_11ACENABLED(fw_cap_info) (fw_cap_info & (BIT(12)|BIT(13)))

#define GET_VHTCAP_CHWDSET(vht_cap_info)    ((vht_cap_info >> 2) & 0x3)
#define GET_VHTNSSMCS(mcs_mapset, nss) ((mcs_mapset >> (2 * (nss - 1))) & 0x3)
#define SET_VHTNSSMCS(mcs_mapset, nss, value) (mcs_mapset |= (value & 0x3) << \
					      (2 * (nss - 1)))
#define NO_NSS_SUPPORT		0x3

#define GET_DEVTXMCSMAP(dev_mcs_map)      (dev_mcs_map >> 16)
#define GET_DEVRXMCSMAP(dev_mcs_map)      (dev_mcs_map & 0xFFFF)

#define MOD_CLASS_HR_DSSS       0x03
#define MOD_CLASS_OFDM          0x07
#define MOD_CLASS_HT            0x08
#define HT_BW_20    0
#define HT_BW_40    1

#define DFS_CHAN_MOVE_TIME      10000

#define HostCmd_CMD_GET_HW_SPEC                       0x0003
#define HostCmd_CMD_802_11_SCAN                       0x0006
#define HostCmd_CMD_802_11_GET_LOG                    0x000b
#define HostCmd_CMD_MAC_MULTICAST_ADR                 0x0010
#define HostCmd_CMD_802_11_EEPROM_ACCESS              0x0059
#define HostCmd_CMD_802_11_ASSOCIATE                  0x0012
#define HostCmd_CMD_802_11_SNMP_MIB                   0x0016
#define HostCmd_CMD_MAC_REG_ACCESS                    0x0019
#define HostCmd_CMD_BBP_REG_ACCESS                    0x001a
#define HostCmd_CMD_RF_REG_ACCESS                     0x001b
#define HostCmd_CMD_PMIC_REG_ACCESS                   0x00ad
#define HostCmd_CMD_RF_TX_PWR                         0x001e
#define HostCmd_CMD_RF_ANTENNA                        0x0020
#define HostCmd_CMD_802_11_DEAUTHENTICATE             0x0024
#define HostCmd_CMD_MAC_CONTROL                       0x0028
#define HostCmd_CMD_802_11_AD_HOC_START               0x002b
#define HostCmd_CMD_802_11_AD_HOC_JOIN                0x002c
#define HostCmd_CMD_802_11_AD_HOC_STOP                0x0040
#define HostCmd_CMD_802_11_MAC_ADDRESS                0x004D
#define HostCmd_CMD_802_11D_DOMAIN_INFO               0x005b
#define HostCmd_CMD_802_11_KEY_MATERIAL               0x005e
#define HostCmd_CMD_802_11_BG_SCAN_QUERY              0x006c
#define HostCmd_CMD_WMM_GET_STATUS                    0x0071
#define HostCmd_CMD_802_11_SUBSCRIBE_EVENT            0x0075
#define HostCmd_CMD_802_11_TX_RATE_QUERY              0x007f
#define HostCmd_CMD_802_11_IBSS_COALESCING_STATUS     0x0083
#define HostCmd_CMD_CFG_DATA                          0x008f
#define HostCmd_CMD_VERSION_EXT                       0x0097
#define HostCmd_CMD_MEF_CFG                           0x009a
#define HostCmd_CMD_RSSI_INFO                         0x00a4
#define HostCmd_CMD_FUNC_INIT                         0x00a9
#define HostCmd_CMD_FUNC_SHUTDOWN                     0x00aa
#define HostCmd_CMD_UAP_SYS_CONFIG                    0x00b0
#define HostCmd_CMD_UAP_BSS_START                     0x00b1
#define HostCmd_CMD_UAP_BSS_STOP                      0x00b2
#define HostCmd_CMD_UAP_STA_DEAUTH                    0x00b5
#define HostCmd_CMD_11N_CFG                           0x00cd
#define HostCmd_CMD_11N_ADDBA_REQ                     0x00ce
#define HostCmd_CMD_11N_ADDBA_RSP                     0x00cf
#define HostCmd_CMD_11N_DELBA                         0x00d0
#define HostCmd_CMD_RECONFIGURE_TX_BUFF               0x00d9
#define HostCmd_CMD_AMSDU_AGGR_CTRL                   0x00df
#define HostCmd_CMD_TXPWR_CFG                         0x00d1
#define HostCmd_CMD_TX_RATE_CFG                       0x00d6
#define HostCmd_CMD_802_11_PS_MODE_ENH                0x00e4
#define HostCmd_CMD_802_11_HS_CFG_ENH                 0x00e5
#define HostCmd_CMD_P2P_MODE_CFG                      0x00eb
#define HostCmd_CMD_CAU_REG_ACCESS                    0x00ed
#define HostCmd_CMD_SET_BSS_MODE                      0x00f7
#define HostCmd_CMD_PCIE_DESC_DETAILS                 0x00fa
#define HostCmd_CMD_COALESCE_CFG                      0x010a
#define HostCmd_CMD_MGMT_FRAME_REG                    0x010c
#define HostCmd_CMD_REMAIN_ON_CHAN                    0x010d
#define HostCmd_CMD_11AC_CFG			      0x0112

#define PROTOCOL_NO_SECURITY        0x01
#define PROTOCOL_STATIC_WEP         0x02
#define PROTOCOL_WPA                0x08
#define PROTOCOL_WPA2               0x20
#define PROTOCOL_WPA2_MIXED         0x28
#define PROTOCOL_EAP                0x40
#define KEY_MGMT_NONE               0x04
#define KEY_MGMT_PSK                0x02
#define KEY_MGMT_EAP                0x01
#define CIPHER_TKIP                 0x04
#define CIPHER_AES_CCMP             0x08
#define VALID_CIPHER_BITMAP         0x0c

enum ENH_PS_MODES {
	EN_PS = 1,
	DIS_PS = 2,
	EN_AUTO_DS = 3,
	DIS_AUTO_DS = 4,
	SLEEP_CONFIRM = 5,
	GET_PS = 0,
	EN_AUTO_PS = 0xff,
	DIS_AUTO_PS = 0xfe,
};

enum P2P_MODES {
	P2P_MODE_DISABLE = 0,
	P2P_MODE_DEVICE = 1,
	P2P_MODE_GO = 2,
	P2P_MODE_CLIENT = 3,
};

#define HostCmd_RET_BIT                       0x8000
#define HostCmd_ACT_GEN_GET                   0x0000
#define HostCmd_ACT_GEN_SET                   0x0001
#define HostCmd_ACT_GEN_REMOVE                0x0004
#define HostCmd_ACT_BITWISE_SET               0x0002
#define HostCmd_ACT_BITWISE_CLR               0x0003
#define HostCmd_RESULT_OK                     0x0000

#define HostCmd_ACT_MAC_RX_ON                 0x0001
#define HostCmd_ACT_MAC_TX_ON                 0x0002
#define HostCmd_ACT_MAC_WEP_ENABLE            0x0008
#define HostCmd_ACT_MAC_ETHERNETII_ENABLE     0x0010
#define HostCmd_ACT_MAC_PROMISCUOUS_ENABLE    0x0080
#define HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE  0x0100
#define HostCmd_ACT_MAC_ADHOC_G_PROTECTION_ON     0x2000

#define HostCmd_BSS_MODE_IBSS               0x0002
#define HostCmd_BSS_MODE_ANY                0x0003

#define HostCmd_SCAN_RADIO_TYPE_BG          0
#define HostCmd_SCAN_RADIO_TYPE_A           1

#define HS_CFG_CANCEL			0xffffffff
#define HS_CFG_COND_DEF			0x00000000
#define HS_CFG_GPIO_DEF			0xff
#define HS_CFG_GAP_DEF			0
#define HS_CFG_COND_BROADCAST_DATA	0x00000001
#define HS_CFG_COND_UNICAST_DATA	0x00000002
#define HS_CFG_COND_MAC_EVENT		0x00000004
#define HS_CFG_COND_MULTICAST_DATA	0x00000008

#define MWIFIEX_TIMEOUT_FOR_AP_RESP		0xfffc
#define MWIFIEX_STATUS_CODE_AUTH_TIMEOUT	2

#define CMD_F_HOSTCMD           (1 << 0)
#define CMD_F_CANCELED          (1 << 1)

#define HostCmd_CMD_ID_MASK             0x0fff

#define HostCmd_SEQ_NUM_MASK            0x00ff

#define HostCmd_BSS_NUM_MASK            0x0f00

#define HostCmd_BSS_TYPE_MASK           0xf000

#define HostCmd_ACT_SET_RX              0x0001
#define HostCmd_ACT_SET_TX              0x0002
#define HostCmd_ACT_SET_BOTH            0x0003

#define RF_ANTENNA_AUTO                 0xFFFF

#define HostCmd_SET_SEQ_NO_BSS_INFO(seq, num, type) {   \
	(((seq) & 0x00ff) |                             \
	 (((num) & 0x000f) << 8)) |                     \
	(((type) & 0x000f) << 12);                  }

#define HostCmd_GET_SEQ_NO(seq)       \
	((seq) & HostCmd_SEQ_NUM_MASK)

#define HostCmd_GET_BSS_NO(seq)         \
	(((seq) & HostCmd_BSS_NUM_MASK) >> 8)

#define HostCmd_GET_BSS_TYPE(seq)       \
	(((seq) & HostCmd_BSS_TYPE_MASK) >> 12)

#define EVENT_DUMMY_HOST_WAKEUP_SIGNAL  0x00000001
#define EVENT_LINK_LOST                 0x00000003
#define EVENT_LINK_SENSED               0x00000004
#define EVENT_MIB_CHANGED               0x00000006
#define EVENT_INIT_DONE                 0x00000007
#define EVENT_DEAUTHENTICATED           0x00000008
#define EVENT_DISASSOCIATED             0x00000009
#define EVENT_PS_AWAKE                  0x0000000a
#define EVENT_PS_SLEEP                  0x0000000b
#define EVENT_MIC_ERR_MULTICAST         0x0000000d
#define EVENT_MIC_ERR_UNICAST           0x0000000e
#define EVENT_DEEP_SLEEP_AWAKE          0x00000010
#define EVENT_ADHOC_BCN_LOST            0x00000011

#define EVENT_WMM_STATUS_CHANGE         0x00000017
#define EVENT_BG_SCAN_REPORT            0x00000018
#define EVENT_RSSI_LOW                  0x00000019
#define EVENT_SNR_LOW                   0x0000001a
#define EVENT_MAX_FAIL                  0x0000001b
#define EVENT_RSSI_HIGH                 0x0000001c
#define EVENT_SNR_HIGH                  0x0000001d
#define EVENT_IBSS_COALESCED            0x0000001e
#define EVENT_DATA_RSSI_LOW             0x00000024
#define EVENT_DATA_SNR_LOW              0x00000025
#define EVENT_DATA_RSSI_HIGH            0x00000026
#define EVENT_DATA_SNR_HIGH             0x00000027
#define EVENT_LINK_QUALITY              0x00000028
#define EVENT_PORT_RELEASE              0x0000002b
#define EVENT_UAP_STA_DEAUTH            0x0000002c
#define EVENT_UAP_STA_ASSOC             0x0000002d
#define EVENT_UAP_BSS_START             0x0000002e
#define EVENT_PRE_BEACON_LOST           0x00000031
#define EVENT_ADDBA                     0x00000033
#define EVENT_DELBA                     0x00000034
#define EVENT_BA_STREAM_TIEMOUT         0x00000037
#define EVENT_AMSDU_AGGR_CTRL           0x00000042
#define EVENT_UAP_BSS_IDLE              0x00000043
#define EVENT_UAP_BSS_ACTIVE            0x00000044
#define EVENT_WEP_ICV_ERR               0x00000046
#define EVENT_HS_ACT_REQ                0x00000047
#define EVENT_BW_CHANGE                 0x00000048
#define EVENT_UAP_MIC_COUNTERMEASURES   0x0000004c
#define EVENT_HOSTWAKE_STAIE		0x0000004d
#define EVENT_CHANNEL_SWITCH_ANN        0x00000050
#define EVENT_REMAIN_ON_CHAN_EXPIRED    0x0000005f

#define EVENT_ID_MASK                   0xffff
#define BSS_NUM_MASK                    0xf

#define EVENT_GET_BSS_NUM(event_cause)          \
	(((event_cause) >> 16) & BSS_NUM_MASK)

#define EVENT_GET_BSS_TYPE(event_cause)         \
	(((event_cause) >> 24) & 0x00ff)

#define MWIFIEX_MAX_PATTERN_LEN		20
#define MWIFIEX_MAX_OFFSET_LEN		100
#define STACK_NBYTES			100
#define TYPE_DNUM			1
#define TYPE_BYTESEQ			2
#define MAX_OPERAND			0x40
#define TYPE_EQ				(MAX_OPERAND+1)
#define TYPE_EQ_DNUM			(MAX_OPERAND+2)
#define TYPE_EQ_BIT			(MAX_OPERAND+3)
#define TYPE_AND			(MAX_OPERAND+4)
#define TYPE_OR				(MAX_OPERAND+5)
#define MEF_MODE_HOST_SLEEP			1
#define MEF_ACTION_ALLOW_AND_WAKEUP_HOST	3
#define MWIFIEX_CRITERIA_BROADCAST	BIT(0)
#define MWIFIEX_CRITERIA_UNICAST	BIT(1)
#define MWIFIEX_CRITERIA_MULTICAST	BIT(3)

struct mwifiex_ie_types_header {
	__le16 type;
	__le16 len;
} __packed;

struct mwifiex_ie_types_data {
	struct mwifiex_ie_types_header header;
	u8 data[1];
} __packed;

#define MWIFIEX_TxPD_POWER_MGMT_NULL_PACKET 0x01
#define MWIFIEX_TxPD_POWER_MGMT_LAST_PACKET 0x08

struct txpd {
	u8 bss_type;
	u8 bss_num;
	__le16 tx_pkt_length;
	__le16 tx_pkt_offset;
	__le16 tx_pkt_type;
	__le32 tx_control;
	u8 priority;
	u8 flags;
	u8 pkt_delay_2ms;
	u8 reserved1;
} __packed;

struct rxpd {
	u8 bss_type;
	u8 bss_num;
	__le16 rx_pkt_length;
	__le16 rx_pkt_offset;
	__le16 rx_pkt_type;
	__le16 seq_num;
	u8 priority;
	u8 rx_rate;
	s8 snr;
	s8 nf;

	/* For: Non-802.11 AC cards
	 *
	 * Ht Info [Bit 0] RxRate format: LG=0, HT=1
	 * [Bit 1]  HT Bandwidth: BW20 = 0, BW40 = 1
	 * [Bit 2]  HT Guard Interval: LGI = 0, SGI = 1
	 *
	 * For: 802.11 AC cards
	 * [Bit 1] [Bit 0] RxRate format: legacy rate = 00 HT = 01 VHT = 10
	 * [Bit 3] [Bit 2] HT/VHT Bandwidth BW20 = 00 BW40 = 01
	 *						BW80 = 10  BW160 = 11
	 * [Bit 4] HT/VHT Guard interval LGI = 0 SGI = 1
	 * [Bit 5] STBC support Enabled = 1
	 * [Bit 6] LDPC support Enabled = 1
	 * [Bit 7] Reserved
	 */
	u8 ht_info;
	u8 reserved;
} __packed;

struct uap_txpd {
	u8 bss_type;
	u8 bss_num;
	__le16 tx_pkt_length;
	__le16 tx_pkt_offset;
	__le16 tx_pkt_type;
	__le32 tx_control;
	u8 priority;
	u8 flags;
	u8 pkt_delay_2ms;
	u8 reserved1;
	__le32 reserved2;
};

struct uap_rxpd {
	u8 bss_type;
	u8 bss_num;
	__le16 rx_pkt_length;
	__le16 rx_pkt_offset;
	__le16 rx_pkt_type;
	__le16 seq_num;
	u8 priority;
	u8 reserved1;
};

enum mwifiex_chan_scan_mode_bitmasks {
	MWIFIEX_PASSIVE_SCAN = BIT(0),
	MWIFIEX_DISABLE_CHAN_FILT = BIT(1),
};

struct mwifiex_chan_scan_param_set {
	u8 radio_type;
	u8 chan_number;
	u8 chan_scan_mode_bitmap;
	__le16 min_scan_time;
	__le16 max_scan_time;
} __packed;

struct mwifiex_ie_types_chan_list_param_set {
	struct mwifiex_ie_types_header header;
	struct mwifiex_chan_scan_param_set chan_scan_param[1];
} __packed;

struct chan_band_param_set {
	u8 radio_type;
	u8 chan_number;
};

struct mwifiex_ie_types_chan_band_list_param_set {
	struct mwifiex_ie_types_header header;
	struct chan_band_param_set chan_band_param[1];
} __packed;

struct mwifiex_ie_types_rates_param_set {
	struct mwifiex_ie_types_header header;
	u8 rates[1];
} __packed;

struct mwifiex_ie_types_ssid_param_set {
	struct mwifiex_ie_types_header header;
	u8 ssid[1];
} __packed;

struct mwifiex_ie_types_num_probes {
	struct mwifiex_ie_types_header header;
	__le16 num_probes;
} __packed;

struct mwifiex_ie_types_wildcard_ssid_params {
	struct mwifiex_ie_types_header header;
	u8 max_ssid_length;
	u8 ssid[1];
} __packed;

#define TSF_DATA_SIZE            8
struct mwifiex_ie_types_tsf_timestamp {
	struct mwifiex_ie_types_header header;
	u8 tsf_data[1];
} __packed;

struct mwifiex_cf_param_set {
	u8 cfp_cnt;
	u8 cfp_period;
	__le16 cfp_max_duration;
	__le16 cfp_duration_remaining;
} __packed;

struct mwifiex_ibss_param_set {
	__le16 atim_window;
} __packed;

struct mwifiex_ie_types_ss_param_set {
	struct mwifiex_ie_types_header header;
	union {
		struct mwifiex_cf_param_set cf_param_set[1];
		struct mwifiex_ibss_param_set ibss_param_set[1];
	} cf_ibss;
} __packed;

struct mwifiex_fh_param_set {
	__le16 dwell_time;
	u8 hop_set;
	u8 hop_pattern;
	u8 hop_index;
} __packed;

struct mwifiex_ds_param_set {
	u8 current_chan;
} __packed;

struct mwifiex_ie_types_phy_param_set {
	struct mwifiex_ie_types_header header;
	union {
		struct mwifiex_fh_param_set fh_param_set[1];
		struct mwifiex_ds_param_set ds_param_set[1];
	} fh_ds;
} __packed;

struct mwifiex_ie_types_auth_type {
	struct mwifiex_ie_types_header header;
	__le16 auth_type;
} __packed;

struct mwifiex_ie_types_vendor_param_set {
	struct mwifiex_ie_types_header header;
	u8 ie[MWIFIEX_MAX_VSIE_LEN];
};

struct mwifiex_ie_types_rsn_param_set {
	struct mwifiex_ie_types_header header;
	u8 rsn_ie[1];
} __packed;

#define KEYPARAMSET_FIXED_LEN 6

struct mwifiex_ie_type_key_param_set {
	__le16 type;
	__le16 length;
	__le16 key_type_id;
	__le16 key_info;
	__le16 key_len;
	u8 key[50];
} __packed;

#define IGTK_PN_LEN		8

struct mwifiex_cmac_param {
	u8 ipn[IGTK_PN_LEN];
	u8 key[WLAN_KEY_LEN_AES_CMAC];
} __packed;

struct host_cmd_ds_802_11_key_material {
	__le16 action;
	struct mwifiex_ie_type_key_param_set key_param_set;
} __packed;

struct host_cmd_ds_gen {
	__le16 command;
	__le16 size;
	__le16 seq_num;
	__le16 result;
};

#define S_DS_GEN        sizeof(struct host_cmd_ds_gen)

enum sleep_resp_ctrl {
	RESP_NOT_NEEDED = 0,
	RESP_NEEDED,
};

struct mwifiex_ps_param {
	__le16 null_pkt_interval;
	__le16 multiple_dtims;
	__le16 bcn_miss_timeout;
	__le16 local_listen_interval;
	__le16 adhoc_wake_period;
	__le16 mode;
	__le16 delay_to_ps;
};

#define BITMAP_AUTO_DS         0x01
#define BITMAP_STA_PS          0x10

struct mwifiex_ie_types_auto_ds_param {
	struct mwifiex_ie_types_header header;
	__le16 deep_sleep_timeout;
} __packed;

struct mwifiex_ie_types_ps_param {
	struct mwifiex_ie_types_header header;
	struct mwifiex_ps_param param;
} __packed;

struct host_cmd_ds_802_11_ps_mode_enh {
	__le16 action;

	union {
		struct mwifiex_ps_param opt_ps;
		__le16 ps_bitmap;
	} params;
} __packed;

struct host_cmd_ds_get_hw_spec {
	__le16 hw_if_version;
	__le16 version;
	__le16 reserved;
	__le16 num_of_mcast_adr;
	u8 permanent_addr[ETH_ALEN];
	__le16 region_code;
	__le16 number_of_antenna;
	__le32 fw_release_number;
	__le32 reserved_1;
	__le32 reserved_2;
	__le32 reserved_3;
	__le32 fw_cap_info;
	__le32 dot_11n_dev_cap;
	u8 dev_mcs_support;
	__le16 mp_end_port;	/* SDIO only, reserved for other interfacces */
	__le16 mgmt_buf_count;	/* mgmt IE buffer count */
	__le32 reserved_5;
	__le32 reserved_6;
	__le32 dot_11ac_dev_cap;
	__le32 dot_11ac_mcs_support;
} __packed;

struct host_cmd_ds_802_11_rssi_info {
	__le16 action;
	__le16 ndata;
	__le16 nbcn;
	__le16 reserved[9];
	long long reserved_1;
};

struct host_cmd_ds_802_11_rssi_info_rsp {
	__le16 action;
	__le16 ndata;
	__le16 nbcn;
	__le16 data_rssi_last;
	__le16 data_nf_last;
	__le16 data_rssi_avg;
	__le16 data_nf_avg;
	__le16 bcn_rssi_last;
	__le16 bcn_nf_last;
	__le16 bcn_rssi_avg;
	__le16 bcn_nf_avg;
	long long tsf_bcn;
};

struct host_cmd_ds_802_11_mac_address {
	__le16 action;
	u8 mac_addr[ETH_ALEN];
};

struct host_cmd_ds_mac_control {
	__le16 action;
	__le16 reserved;
};

struct host_cmd_ds_mac_multicast_adr {
	__le16 action;
	__le16 num_of_adrs;
	u8 mac_list[MWIFIEX_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];
} __packed;

struct host_cmd_ds_802_11_deauthenticate {
	u8 mac_addr[ETH_ALEN];
	__le16 reason_code;
} __packed;

struct host_cmd_ds_802_11_associate {
	u8 peer_sta_addr[ETH_ALEN];
	__le16 cap_info_bitmap;
	__le16 listen_interval;
	__le16 beacon_period;
	u8 dtim_period;
} __packed;

struct ieee_types_assoc_rsp {
	__le16 cap_info_bitmap;
	__le16 status_code;
	__le16 a_id;
	u8 ie_buffer[1];
} __packed;

struct host_cmd_ds_802_11_associate_rsp {
	struct ieee_types_assoc_rsp assoc_rsp;
} __packed;

struct ieee_types_cf_param_set {
	u8 element_id;
	u8 len;
	u8 cfp_cnt;
	u8 cfp_period;
	__le16 cfp_max_duration;
	__le16 cfp_duration_remaining;
} __packed;

struct ieee_types_ibss_param_set {
	u8 element_id;
	u8 len;
	__le16 atim_window;
} __packed;

union ieee_types_ss_param_set {
	struct ieee_types_cf_param_set cf_param_set;
	struct ieee_types_ibss_param_set ibss_param_set;
} __packed;

struct ieee_types_fh_param_set {
	u8 element_id;
	u8 len;
	__le16 dwell_time;
	u8 hop_set;
	u8 hop_pattern;
	u8 hop_index;
} __packed;

struct ieee_types_ds_param_set {
	u8 element_id;
	u8 len;
	u8 current_chan;
} __packed;

union ieee_types_phy_param_set {
	struct ieee_types_fh_param_set fh_param_set;
	struct ieee_types_ds_param_set ds_param_set;
} __packed;

struct ieee_types_oper_mode_ntf {
	u8 element_id;
	u8 len;
	u8 oper_mode;
} __packed;

struct host_cmd_ds_802_11_ad_hoc_start {
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 bss_mode;
	__le16 beacon_period;
	u8 dtim_period;
	union ieee_types_ss_param_set ss_param_set;
	union ieee_types_phy_param_set phy_param_set;
	u16 reserved1;
	__le16 cap_info_bitmap;
	u8 data_rate[HOSTCMD_SUPPORTED_RATES];
} __packed;

struct host_cmd_ds_802_11_ad_hoc_result {
	u8 pad[3];
	u8 bssid[ETH_ALEN];
} __packed;

struct adhoc_bss_desc {
	u8 bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 bss_mode;
	__le16 beacon_period;
	u8 dtim_period;
	u8 time_stamp[8];
	u8 local_time[8];
	union ieee_types_phy_param_set phy_param_set;
	union ieee_types_ss_param_set ss_param_set;
	__le16 cap_info_bitmap;
	u8 data_rates[HOSTCMD_SUPPORTED_RATES];

	/*
	 *  DO NOT ADD ANY FIELDS TO THIS STRUCTURE.
	 *  It is used in the Adhoc join command and will cause a
	 *  binary layout mismatch with the firmware
	 */
} __packed;

struct host_cmd_ds_802_11_ad_hoc_join {
	struct adhoc_bss_desc bss_descriptor;
	u16 reserved1;
	u16 reserved2;
} __packed;

struct host_cmd_ds_802_11_get_log {
	__le32 mcast_tx_frame;
	__le32 failed;
	__le32 retry;
	__le32 multi_retry;
	__le32 frame_dup;
	__le32 rts_success;
	__le32 rts_failure;
	__le32 ack_failure;
	__le32 rx_frag;
	__le32 mcast_rx_frame;
	__le32 fcs_error;
	__le32 tx_frame;
	__le32 reserved;
	__le32 wep_icv_err_cnt[4];
};

/* Enumeration for rate format */
enum _mwifiex_rate_format {
	MWIFIEX_RATE_FORMAT_LG = 0,
	MWIFIEX_RATE_FORMAT_HT,
	MWIFIEX_RATE_FORMAT_VHT,
	MWIFIEX_RATE_FORMAT_AUTO = 0xFF,
};

struct host_cmd_ds_tx_rate_query {
	u8 tx_rate;
	/* Tx Rate Info: For 802.11 AC cards
	 *
	 * [Bit 0-1] tx rate formate: LG = 0, HT = 1, VHT = 2
	 * [Bit 2-3] HT/VHT Bandwidth: BW20 = 0, BW40 = 1, BW80 = 2, BW160 = 3
	 * [Bit 4]   HT/VHT Guard Interval: LGI = 0, SGI = 1
	 *
	 * For non-802.11 AC cards
	 * Ht Info [Bit 0] RxRate format: LG=0, HT=1
	 * [Bit 1]  HT Bandwidth: BW20 = 0, BW40 = 1
	 * [Bit 2]  HT Guard Interval: LGI = 0, SGI = 1
	 */
	u8 ht_info;
} __packed;

enum Host_Sleep_Action {
	HS_CONFIGURE = 0x0001,
	HS_ACTIVATE  = 0x0002,
};

struct mwifiex_hs_config_param {
	__le32 conditions;
	u8 gpio;
	u8 gap;
} __packed;

struct hs_activate_param {
	__le16 resp_ctrl;
} __packed;

struct host_cmd_ds_802_11_hs_cfg_enh {
	__le16 action;

	union {
		struct mwifiex_hs_config_param hs_config;
		struct hs_activate_param hs_activate;
	} params;
} __packed;

enum SNMP_MIB_INDEX {
	OP_RATE_SET_I = 1,
	DTIM_PERIOD_I = 3,
	RTS_THRESH_I = 5,
	SHORT_RETRY_LIM_I = 6,
	LONG_RETRY_LIM_I = 7,
	FRAG_THRESH_I = 8,
	DOT11D_I = 9,
	DOT11H_I = 10,
};

#define MAX_SNMP_BUF_SIZE   128

struct host_cmd_ds_802_11_snmp_mib {
	__le16 query_type;
	__le16 oid;
	__le16 buf_size;
	u8 value[1];
} __packed;

struct mwifiex_rate_scope {
	__le16 type;
	__le16 length;
	__le16 hr_dsss_rate_bitmap;
	__le16 ofdm_rate_bitmap;
	__le16 ht_mcs_rate_bitmap[8];
} __packed;

struct mwifiex_rate_drop_pattern {
	__le16 type;
	__le16 length;
	__le32 rate_drop_mode;
} __packed;

struct host_cmd_ds_tx_rate_cfg {
	__le16 action;
	__le16 cfg_index;
} __packed;

struct mwifiex_power_group {
	u8 modulation_class;
	u8 first_rate_code;
	u8 last_rate_code;
	s8 power_step;
	s8 power_min;
	s8 power_max;
	u8 ht_bandwidth;
	u8 reserved;
} __packed;

struct mwifiex_types_power_group {
	__le16 type;
	__le16 length;
} __packed;

struct host_cmd_ds_txpwr_cfg {
	__le16 action;
	__le16 cfg_index;
	__le32 mode;
} __packed;

struct host_cmd_ds_rf_tx_pwr {
	__le16 action;
	__le16 cur_level;
	u8 max_power;
	u8 min_power;
} __packed;

struct host_cmd_ds_rf_ant_mimo {
	__le16 action_tx;
	__le16 tx_ant_mode;
	__le16 action_rx;
	__le16 rx_ant_mode;
};

struct host_cmd_ds_rf_ant_siso {
	__le16 action;
	__le16 ant_mode;
};

struct mwifiex_bcn_param {
	u8 bssid[ETH_ALEN];
	u8 rssi;
	__le64 timestamp;
	__le16 beacon_period;
	__le16 cap_info_bitmap;
} __packed;

#define MWIFIEX_USER_SCAN_CHAN_MAX             50

#define MWIFIEX_MAX_SSID_LIST_LENGTH         10

struct mwifiex_scan_cmd_config {
	/*
	 *  BSS mode to be sent in the firmware command
	 */
	u8 bss_mode;

	/* Specific BSSID used to filter scan results in the firmware */
	u8 specific_bssid[ETH_ALEN];

	/* Length of TLVs sent in command starting at tlvBuffer */
	u32 tlv_buf_len;

	/*
	 *  SSID TLV(s) and ChanList TLVs to be sent in the firmware command
	 *
	 *  TLV_TYPE_CHANLIST, mwifiex_ie_types_chan_list_param_set
	 *  WLAN_EID_SSID, mwifiex_ie_types_ssid_param_set
	 */
	u8 tlv_buf[1];	/* SSID TLV(s) and ChanList TLVs are stored
				   here */
} __packed;

struct mwifiex_user_scan_chan {
	u8 chan_number;
	u8 radio_type;
	u8 scan_type;
	u8 reserved;
	u32 scan_time;
} __packed;

struct mwifiex_user_scan_cfg {
	/*
	 *  BSS mode to be sent in the firmware command
	 */
	u8 bss_mode;
	/* Configure the number of probe requests for active chan scans */
	u8 num_probes;
	u8 reserved;
	/* BSSID filter sent in the firmware command to limit the results */
	u8 specific_bssid[ETH_ALEN];
	/* SSID filter list used in the firmware to limit the scan results */
	struct cfg80211_ssid *ssid_list;
	u8 num_ssids;
	/* Variable number (fixed maximum) of channels to scan up */
	struct mwifiex_user_scan_chan chan_list[MWIFIEX_USER_SCAN_CHAN_MAX];
} __packed;

struct ie_body {
	u8 grp_key_oui[4];
	u8 ptk_cnt[2];
	u8 ptk_body[4];
} __packed;

struct host_cmd_ds_802_11_scan {
	u8 bss_mode;
	u8 bssid[ETH_ALEN];
	u8 tlv_buffer[1];
} __packed;

struct host_cmd_ds_802_11_scan_rsp {
	__le16 bss_descript_size;
	u8 number_of_sets;
	u8 bss_desc_and_tlv_buffer[1];
} __packed;

struct host_cmd_ds_802_11_bg_scan_query {
	u8 flush;
} __packed;

struct host_cmd_ds_802_11_bg_scan_query_rsp {
	__le32 report_condition;
	struct host_cmd_ds_802_11_scan_rsp scan_resp;
} __packed;

struct mwifiex_ietypes_domain_param_set {
	struct mwifiex_ie_types_header header;
	u8 country_code[IEEE80211_COUNTRY_STRING_LEN];
	struct ieee80211_country_ie_triplet triplet[1];
} __packed;

struct host_cmd_ds_802_11d_domain_info {
	__le16 action;
	struct mwifiex_ietypes_domain_param_set domain;
} __packed;

struct host_cmd_ds_802_11d_domain_info_rsp {
	__le16 action;
	struct mwifiex_ietypes_domain_param_set domain;
} __packed;

struct host_cmd_ds_11n_addba_req {
	u8 add_req_result;
	u8 peer_mac_addr[ETH_ALEN];
	u8 dialog_token;
	__le16 block_ack_param_set;
	__le16 block_ack_tmo;
	__le16 ssn;
} __packed;

struct host_cmd_ds_11n_addba_rsp {
	u8 add_rsp_result;
	u8 peer_mac_addr[ETH_ALEN];
	u8 dialog_token;
	__le16 status_code;
	__le16 block_ack_param_set;
	__le16 block_ack_tmo;
	__le16 ssn;
} __packed;

struct host_cmd_ds_11n_delba {
	u8 del_result;
	u8 peer_mac_addr[ETH_ALEN];
	__le16 del_ba_param_set;
	__le16 reason_code;
	u8 reserved;
} __packed;

struct host_cmd_ds_11n_batimeout {
	u8 tid;
	u8 peer_mac_addr[ETH_ALEN];
	u8 origninator;
} __packed;

struct host_cmd_ds_11n_cfg {
	__le16 action;
	__le16 ht_tx_cap;
	__le16 ht_tx_info;
	__le16 misc_config;	/* Needed for 802.11AC cards only */
} __packed;

struct host_cmd_ds_txbuf_cfg {
	__le16 action;
	__le16 buff_size;
	__le16 mp_end_port;	/* SDIO only, reserved for other interfacces */
	__le16 reserved3;
} __packed;

struct host_cmd_ds_amsdu_aggr_ctrl {
	__le16 action;
	__le16 enable;
	__le16 curr_buf_size;
} __packed;

struct host_cmd_ds_sta_deauth {
	u8 mac[ETH_ALEN];
	__le16 reason;
} __packed;

struct mwifiex_ie_types_pwr_capability {
	struct mwifiex_ie_types_header header;
	s8 min_pwr;
	s8 max_pwr;
};

struct mwifiex_ie_types_local_pwr_constraint {
	struct mwifiex_ie_types_header header;
	u8 chan;
	u8 constraint;
};

struct mwifiex_ie_types_wmm_param_set {
	struct mwifiex_ie_types_header header;
	u8 wmm_ie[1];
};

struct mwifiex_ie_types_wmm_queue_status {
	struct mwifiex_ie_types_header header;
	u8 queue_index;
	u8 disabled;
	__le16 medium_time;
	u8 flow_required;
	u8 flow_created;
	u32 reserved;
};

struct ieee_types_vendor_header {
	u8 element_id;
	u8 len;
	u8 oui[4];	/* 0~2: oui, 3: oui_type */
	u8 oui_subtype;
	u8 version;
} __packed;

struct ieee_types_wmm_parameter {
	/*
	 * WMM Parameter IE - Vendor Specific Header:
	 *   element_id  [221/0xdd]
	 *   Len         [24]
	 *   Oui         [00:50:f2]
	 *   OuiType     [2]
	 *   OuiSubType  [1]
	 *   Version     [1]
	 */
	struct ieee_types_vendor_header vend_hdr;
	u8 qos_info_bitmap;
	u8 reserved;
	struct ieee_types_wmm_ac_parameters ac_params[IEEE80211_NUM_ACS];
} __packed;

struct ieee_types_wmm_info {

	/*
	 * WMM Info IE - Vendor Specific Header:
	 *   element_id  [221/0xdd]
	 *   Len         [7]
	 *   Oui         [00:50:f2]
	 *   OuiType     [2]
	 *   OuiSubType  [0]
	 *   Version     [1]
	 */
	struct ieee_types_vendor_header vend_hdr;

	u8 qos_info_bitmap;
} __packed;

struct host_cmd_ds_wmm_get_status {
	u8 queue_status_tlv[sizeof(struct mwifiex_ie_types_wmm_queue_status) *
			      IEEE80211_NUM_ACS];
	u8 wmm_param_tlv[sizeof(struct ieee_types_wmm_parameter) + 2];
} __packed;

struct mwifiex_wmm_ac_status {
	u8 disabled;
	u8 flow_required;
	u8 flow_created;
};

struct mwifiex_ie_types_htcap {
	struct mwifiex_ie_types_header header;
	struct ieee80211_ht_cap ht_cap;
} __packed;

struct mwifiex_ie_types_vhtcap {
	struct mwifiex_ie_types_header header;
	struct ieee80211_vht_cap vht_cap;
} __packed;

struct mwifiex_ie_types_oper_mode_ntf {
	struct mwifiex_ie_types_header header;
	u8 oper_mode;
} __packed;

/* VHT Operations IE */
struct mwifiex_ie_types_vht_oper {
	struct mwifiex_ie_types_header header;
	u8 chan_width;
	u8 chan_center_freq_1;
	u8 chan_center_freq_2;
	/* Basic MCS set map, each 2 bits stands for a NSS */
	__le16 basic_mcs_map;
} __packed;

struct mwifiex_ie_types_wmmcap {
	struct mwifiex_ie_types_header header;
	struct mwifiex_types_wmm_info wmm_info;
} __packed;

struct mwifiex_ie_types_htinfo {
	struct mwifiex_ie_types_header header;
	struct ieee80211_ht_operation ht_oper;
} __packed;

struct mwifiex_ie_types_2040bssco {
	struct mwifiex_ie_types_header header;
	u8 bss_co_2040;
} __packed;

struct mwifiex_ie_types_extcap {
	struct mwifiex_ie_types_header header;
	u8 ext_capab[0];
} __packed;

struct host_cmd_ds_mac_reg_access {
	__le16 action;
	__le16 offset;
	__le32 value;
} __packed;

struct host_cmd_ds_bbp_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
} __packed;

struct host_cmd_ds_rf_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
} __packed;

struct host_cmd_ds_pmic_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
} __packed;

struct host_cmd_ds_802_11_eeprom_access {
	__le16 action;

	__le16 offset;
	__le16 byte_count;
	u8 value;
} __packed;

struct mwifiex_assoc_event {
	u8 sta_addr[ETH_ALEN];
	__le16 type;
	__le16 len;
	__le16 frame_control;
	__le16 cap_info;
	__le16 listen_interval;
	u8 data[0];
} __packed;

struct host_cmd_ds_sys_config {
	__le16 action;
	u8 tlv[0];
};

struct host_cmd_11ac_vht_cfg {
	__le16 action;
	u8 band_config;
	u8 misc_config;
	__le32 cap_info;
	__le32 mcs_tx_set;
	__le32 mcs_rx_set;
} __packed;

struct host_cmd_tlv_akmp {
	struct mwifiex_ie_types_header header;
	__le16 key_mgmt;
	__le16 key_mgmt_operation;
} __packed;

struct host_cmd_tlv_pwk_cipher {
	struct mwifiex_ie_types_header header;
	__le16 proto;
	u8 cipher;
	u8 reserved;
} __packed;

struct host_cmd_tlv_gwk_cipher {
	struct mwifiex_ie_types_header header;
	u8 cipher;
	u8 reserved;
} __packed;

struct host_cmd_tlv_passphrase {
	struct mwifiex_ie_types_header header;
	u8 passphrase[0];
} __packed;

struct host_cmd_tlv_wep_key {
	struct mwifiex_ie_types_header header;
	u8 key_index;
	u8 is_default;
	u8 key[1];
};

struct host_cmd_tlv_auth_type {
	struct mwifiex_ie_types_header header;
	u8 auth_type;
} __packed;

struct host_cmd_tlv_encrypt_protocol {
	struct mwifiex_ie_types_header header;
	__le16 proto;
} __packed;

struct host_cmd_tlv_ssid {
	struct mwifiex_ie_types_header header;
	u8 ssid[0];
} __packed;

struct host_cmd_tlv_rates {
	struct mwifiex_ie_types_header header;
	u8 rates[0];
} __packed;

struct host_cmd_tlv_bcast_ssid {
	struct mwifiex_ie_types_header header;
	u8 bcast_ctl;
} __packed;

struct host_cmd_tlv_beacon_period {
	struct mwifiex_ie_types_header header;
	__le16 period;
} __packed;

struct host_cmd_tlv_dtim_period {
	struct mwifiex_ie_types_header header;
	u8 period;
} __packed;

struct host_cmd_tlv_frag_threshold {
	struct mwifiex_ie_types_header header;
	__le16 frag_thr;
} __packed;

struct host_cmd_tlv_rts_threshold {
	struct mwifiex_ie_types_header header;
	__le16 rts_thr;
} __packed;

struct host_cmd_tlv_retry_limit {
	struct mwifiex_ie_types_header header;
	u8 limit;
} __packed;

struct host_cmd_tlv_mac_addr {
	struct mwifiex_ie_types_header header;
	u8 mac_addr[ETH_ALEN];
} __packed;

struct host_cmd_tlv_channel_band {
	struct mwifiex_ie_types_header header;
	u8 band_config;
	u8 channel;
} __packed;

struct host_cmd_tlv_ageout_timer {
	struct mwifiex_ie_types_header header;
	__le32 sta_ao_timer;
} __packed;

struct host_cmd_ds_version_ext {
	u8 version_str_sel;
	char version_str[128];
} __packed;

struct host_cmd_ds_mgmt_frame_reg {
	__le16 action;
	__le32 mask;
} __packed;

struct host_cmd_ds_p2p_mode_cfg {
	__le16 action;
	__le16 mode;
} __packed;

struct host_cmd_ds_remain_on_chan {
	__le16 action;
	u8 status;
	u8 reserved;
	u8 band_cfg;
	u8 channel;
	__le32 duration;
} __packed;

struct host_cmd_ds_802_11_ibss_status {
	__le16 action;
	__le16 enable;
	u8 bssid[ETH_ALEN];
	__le16 beacon_interval;
	__le16 atim_window;
	__le16 use_g_rate_protect;
} __packed;

struct mwifiex_fw_mef_entry {
	u8 mode;
	u8 action;
	__le16 exprsize;
	u8 expr[0];
} __packed;

struct host_cmd_ds_mef_cfg {
	__le32 criteria;
	__le16 num_entries;
	struct mwifiex_fw_mef_entry mef_entry[0];
} __packed;

#define CONNECTION_TYPE_INFRA   0
#define CONNECTION_TYPE_ADHOC   1
#define CONNECTION_TYPE_AP      2

struct host_cmd_ds_set_bss_mode {
	u8 con_type;
} __packed;

struct host_cmd_ds_pcie_details {
	/* TX buffer descriptor ring address */
	u32 txbd_addr_lo;
	u32 txbd_addr_hi;
	/* TX buffer descriptor ring count */
	u32 txbd_count;

	/* RX buffer descriptor ring address */
	u32 rxbd_addr_lo;
	u32 rxbd_addr_hi;
	/* RX buffer descriptor ring count */
	u32 rxbd_count;

	/* Event buffer descriptor ring address */
	u32 evtbd_addr_lo;
	u32 evtbd_addr_hi;
	/* Event buffer descriptor ring count */
	u32 evtbd_count;

	/* Sleep cookie buffer physical address */
	u32 sleep_cookie_addr_lo;
	u32 sleep_cookie_addr_hi;
} __packed;

struct mwifiex_ie_types_rssi_threshold {
	struct mwifiex_ie_types_header header;
	u8 abs_value;
	u8 evt_freq;
} __packed;

struct host_cmd_ds_802_11_subsc_evt {
	__le16 action;
	__le16 events;
} __packed;

struct mwifiex_ie {
	__le16 ie_index;
	__le16 mgmt_subtype_mask;
	__le16 ie_length;
	u8 ie_buffer[IEEE_MAX_IE_SIZE];
} __packed;

#define MAX_MGMT_IE_INDEX	16
struct mwifiex_ie_list {
	__le16 type;
	__le16 len;
	struct mwifiex_ie ie_list[MAX_MGMT_IE_INDEX];
} __packed;

struct coalesce_filt_field_param {
	u8 operation;
	u8 operand_len;
	__le16 offset;
	u8 operand_byte_stream[4];
};

struct coalesce_receive_filt_rule {
	struct mwifiex_ie_types_header header;
	u8 num_of_fields;
	u8 pkt_type;
	__le16 max_coalescing_delay;
	struct coalesce_filt_field_param params[0];
} __packed;

struct host_cmd_ds_coalesce_cfg {
	__le16 action;
	__le16 num_of_rules;
	struct coalesce_receive_filt_rule rule[0];
} __packed;

struct host_cmd_ds_command {
	__le16 command;
	__le16 size;
	__le16 seq_num;
	__le16 result;
	union {
		struct host_cmd_ds_get_hw_spec hw_spec;
		struct host_cmd_ds_mac_control mac_ctrl;
		struct host_cmd_ds_802_11_mac_address mac_addr;
		struct host_cmd_ds_mac_multicast_adr mc_addr;
		struct host_cmd_ds_802_11_get_log get_log;
		struct host_cmd_ds_802_11_rssi_info rssi_info;
		struct host_cmd_ds_802_11_rssi_info_rsp rssi_info_rsp;
		struct host_cmd_ds_802_11_snmp_mib smib;
		struct host_cmd_ds_tx_rate_query tx_rate;
		struct host_cmd_ds_tx_rate_cfg tx_rate_cfg;
		struct host_cmd_ds_txpwr_cfg txp_cfg;
		struct host_cmd_ds_rf_tx_pwr txp;
		struct host_cmd_ds_rf_ant_mimo ant_mimo;
		struct host_cmd_ds_rf_ant_siso ant_siso;
		struct host_cmd_ds_802_11_ps_mode_enh psmode_enh;
		struct host_cmd_ds_802_11_hs_cfg_enh opt_hs_cfg;
		struct host_cmd_ds_802_11_scan scan;
		struct host_cmd_ds_802_11_scan_rsp scan_resp;
		struct host_cmd_ds_802_11_bg_scan_query bg_scan_query;
		struct host_cmd_ds_802_11_bg_scan_query_rsp bg_scan_query_resp;
		struct host_cmd_ds_802_11_associate associate;
		struct host_cmd_ds_802_11_associate_rsp associate_rsp;
		struct host_cmd_ds_802_11_deauthenticate deauth;
		struct host_cmd_ds_802_11_ad_hoc_start adhoc_start;
		struct host_cmd_ds_802_11_ad_hoc_result adhoc_result;
		struct host_cmd_ds_802_11_ad_hoc_join adhoc_join;
		struct host_cmd_ds_802_11d_domain_info domain_info;
		struct host_cmd_ds_802_11d_domain_info_rsp domain_info_resp;
		struct host_cmd_ds_11n_addba_req add_ba_req;
		struct host_cmd_ds_11n_addba_rsp add_ba_rsp;
		struct host_cmd_ds_11n_delba del_ba;
		struct host_cmd_ds_txbuf_cfg tx_buf;
		struct host_cmd_ds_amsdu_aggr_ctrl amsdu_aggr_ctrl;
		struct host_cmd_ds_11n_cfg htcfg;
		struct host_cmd_ds_wmm_get_status get_wmm_status;
		struct host_cmd_ds_802_11_key_material key_material;
		struct host_cmd_ds_version_ext verext;
		struct host_cmd_ds_mgmt_frame_reg reg_mask;
		struct host_cmd_ds_remain_on_chan roc_cfg;
		struct host_cmd_ds_p2p_mode_cfg mode_cfg;
		struct host_cmd_ds_802_11_ibss_status ibss_coalescing;
		struct host_cmd_ds_mef_cfg mef_cfg;
		struct host_cmd_ds_mac_reg_access mac_reg;
		struct host_cmd_ds_bbp_reg_access bbp_reg;
		struct host_cmd_ds_rf_reg_access rf_reg;
		struct host_cmd_ds_pmic_reg_access pmic_reg;
		struct host_cmd_ds_set_bss_mode bss_mode;
		struct host_cmd_ds_pcie_details pcie_host_spec;
		struct host_cmd_ds_802_11_eeprom_access eeprom;
		struct host_cmd_ds_802_11_subsc_evt subsc_evt;
		struct host_cmd_ds_sys_config uap_sys_config;
		struct host_cmd_ds_sta_deauth sta_deauth;
		struct host_cmd_11ac_vht_cfg vht_cfg;
		struct host_cmd_ds_coalesce_cfg coalesce_cfg;
	} params;
} __packed;

struct mwifiex_opt_sleep_confirm {
	__le16 command;
	__le16 size;
	__le16 seq_num;
	__le16 result;
	__le16 action;
	__le16 resp_ctrl;
} __packed;
#endif /* !_MWIFIEX_FW_H_ */
