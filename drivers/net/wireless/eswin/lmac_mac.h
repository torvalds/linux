/**
 ****************************************************************************************
 *
 * @file lmac_mac_types.h
 *
 * @brief MAC related definitions.
 *
 * Adapted from mac_types.h to used lmac_types.h instead of standard types
 * eg: perl -pi -e '$_ =~ s/uint(\d{1,2})_t/u$1_l/g; \
 *                  $_ =~ s/int(\d{1,2})_t/s$1_l/g; \
 *                  $_ =~ s/CO_BIT/BIT/g;' lmac_mac.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef LMAC_MAC_H_
#define LMAC_MAC_H_

#include "lmac_types.h"

/// Interface types
enum mac_vif_type
{
    /// ESS STA interface
    VIF_STA,
    /// IBSS STA interface
    VIF_IBSS,
    /// AP interface
    VIF_AP,
    /// Mesh Point interface
    VIF_MESH_POINT,
    /// Monitor interface
    VIF_MONITOR,
    /// Unknown type
    VIF_UNKNOWN
};

/// MAC address length in bytes.
#define MAC_ADDR_LEN 6

/// MAC address structure.
struct mac_addr
{
    /// Array of 16-bit words that make up the MAC address.
    u16_l array[MAC_ADDR_LEN/2];
};

/// SSID maximum length.
#define MAC_SSID_LEN 32

/// SSID.
struct mac_ssid
{
    /// Actual length of the SSID.
    u8_l length;
    /// Array containing the SSID name.
    u8_l array[MAC_SSID_LEN];
};

/// BSS type
enum mac_bss_type
{
    INFRASTRUCTURE_MODE = 1,
    INDEPENDENT_BSS_MODE,
    ANY_BSS_MODE
};

/// Channel Band
enum mac_chan_band
{
    /// 2.4GHz Band
    PHY_BAND_2G4,
    /// 5GHz band
    PHY_BAND_5G,
    /// Number of bands
    PHY_BAND_MAX,
};

/// Operating Channel Bandwidth
enum mac_chan_bandwidth
{
    /// 20MHz BW
    PHY_CHNL_BW_20,
    /// 40MHz BW
    PHY_CHNL_BW_40,
    /// 80MHz BW
    PHY_CHNL_BW_80,
    /// 160MHz BW
    PHY_CHNL_BW_160,
    /// 80+80MHz BW
    PHY_CHNL_BW_80P80,
    /// Reserved BW
    PHY_CHNL_BW_OTHER,
};

/// max number of channels in the 2.4 GHZ band
#define MAC_DOMAINCHANNEL_24G_MAX 14

/// max number of channels in the 5 GHZ band
#define MAC_DOMAINCHANNEL_5G_MAX 28

/// Channel Flag
enum mac_chan_flags
{
    /// Cannot initiate radiation on this channel
    CHAN_NO_IR = BIT(0),
    /// Channel is not allowed
    CHAN_DISABLED = BIT(1),
    /// Radar detection required on this channel
    CHAN_RADAR = BIT(2),
};

/// Primary Channel definition
struct mac_chan_def
{
    /// Frequency of the channel (in MHz)
    u16_l freq;
    /// RF band (@ref mac_chan_band)
    u8_l band;
    /// Additional information (@ref mac_chan_flags)
    u8_l flags;
    /// Max transmit power allowed on this channel (dBm)
    s8_l tx_power;
};

/// Operating Channel
struct mac_chan_op
{
    /// Band (@ref mac_chan_band)
    u8_l band;
    /// Channel type (@ref mac_chan_bandwidth)
    u8_l type;
    /// Frequency for Primary 20MHz channel (in MHz)
    u16_l prim20_freq;
    /// Frequency center of the contiguous channel or center of Primary 80+80 (in MHz)
    u16_l center1_freq;
    /// Frequency center of the non-contiguous secondary 80+80 (in MHz)
    u16_l center2_freq;
    /// Max transmit power allowed on this channel (dBm)
    s8_l tx_power;
    /// Additional information (@ref mac_chan_flags)
    u8_l flags;
};

/// Cipher suites (order is important as it is used by MACHW)
enum mac_cipher_suite
{
    /// 00-0F-AC 1
    MAC_CIPHER_WEP40 = 0,
    /// 00-0F-AC 2
    MAC_CIPHER_TKIP = 1,
    /// 00-0F-AC 4
    MAC_CIPHER_CCMP = 2,
    /// 00-0F-AC 5
    MAC_CIPHER_WEP104 = 3,
    /// 00-14-72 1
    MAC_CIPHER_WPI_SMS4 = 4,
    /// 00-0F-AC 6  (aka AES_CMAC)
    MAC_CIPHER_BIP_CMAC_128 = 5,

    // following cipher are not supported by MACHW
    /// 00-0F-AC 08
    MAC_CIPHER_GCMP_128,
    /// 00-0F-AC 09
    MAC_CIPHER_GCMP_256,
    /// 00-0F-AC 10
    MAC_CIPHER_CCMP_256,
    /// 00-0F-AC 11
    MAC_CIPHER_BIP_GMAC_128,
    /// 00-0F-AC 12
    MAC_CIPHER_BIP_GMAC_256,
    /// 00-0F-AC 13
    MAC_CIPHER_BIP_CMAC_256,

    MAC_CIPHER_INVALID = 0xFF
};

/// Authentication and Key Management suite
enum mac_akm_suite
{
    /// No security
    MAC_AKM_NONE,
    /// Pre RSN (WEP or WPA)
    MAC_AKM_PRE_RSN,
    /// 00-0F-AC 1
    MAC_AKM_8021X,
    /// 00-0F-AC 2
    MAC_AKM_PSK,
    /// 00-0F-AC 3
    MAC_AKM_FT_8021X,
    /// 00-0F-AC 4
    MAC_AKM_FT_PSK,
    /// 00-0F-AC 5
    MAC_AKM_8021X_SHA256,
    /// 00-0F-AC 6
    MAC_AKM_PSK_SHA256,
    /// 00-0F-AC 7
    MAC_AKM_TDLS,
    /// 00-0F-AC 8
    MAC_AKM_SAE,
    /// 00-0F-AC 9
    MAC_AKM_FT_OVER_SAE,
    /// 00-0F-AC 11
    MAC_AKM_8021X_SUITE_B,
    /// 00-0F-AC 12
    MAC_AKM_8021X_SUITE_B_192,
    /// 00-0F-AC 14
    MAC_AKM_FILS_SHA256,
    /// 00-0F-AC 15
    MAC_AKM_FILS_SHA384,
    /// 00-0F-AC 16
    MAC_AKM_FT_FILS_SHA256,
    /// 00-0F-AC 17
    MAC_AKM_FT_FILS_SHA384,
    /// 00-0F-AC 18
    MAC_AKM_OWE,

    /// 00-14-72 1
    MAC_AKM_WAPI_CERT,
    /// 00-14-72 2
    MAC_AKM_WAPI_PSK,
};

/// Scan result element, parsed from beacon or probe response frames.
struct mac_scan_result
{
    /// Scan result is valid
    bool valid_flag;
    /// Network BSSID.
    struct mac_addr bssid;
    /// Network name.
    struct mac_ssid ssid;
    /// Network type (@ref mac_bss_type).
    u16_l bsstype;
    /// Network channel.
    struct mac_chan_def *chan;
    /// Network beacon period (in TU).
    u16_l beacon_period;
    /// Capability information
    u16_l cap_info;
    /// Supported AKM (bit-field of @ref mac_akm_suite)
    u32_l akm;
    /// Group cipher (bit-field of @ref mac_cipher_suite)
    u16_l group_cipher;
    /// Group cipher (bit-field of @ref mac_cipher_suite)
    u16_l pairwise_cipher;
    /// RSSI of the scanned BSS (in dBm)
    s8_l rssi;
    /// Multi-BSSID index (0 if this is the reference (i.e. transmitted) BSSID)
    u8_l multi_bssid_index;
    /// Maximum BSSID indicator
    u8_l max_bssid_indicator;
};

/// Legacy rate 802.11 definitions
enum mac_legacy_rates
{
    /// DSSS/CCK 1Mbps
    MAC_RATE_1MBPS   =   2,
    /// DSSS/CCK 2Mbps
    MAC_RATE_2MBPS   =   4,
    /// DSSS/CCK 5.5Mbps
    MAC_RATE_5_5MBPS =  11,
    /// OFDM 6Mbps
    MAC_RATE_6MBPS   =  12,
    /// OFDM 9Mbps
    MAC_RATE_9MBPS   =  18,
    /// DSSS/CCK 11Mbps
    MAC_RATE_11MBPS  =  22,
    /// OFDM 12Mbps
    MAC_RATE_12MBPS  =  24,
    /// OFDM 18Mbps
    MAC_RATE_18MBPS  =  36,
    /// OFDM 24Mbps
    MAC_RATE_24MBPS  =  48,
    /// OFDM 36Mbps
    MAC_RATE_36MBPS  =  72,
    /// OFDM 48Mbps
    MAC_RATE_48MBPS  =  96,
    /// OFDM 54Mbps
    MAC_RATE_54MBPS  = 108
};

/// BSS Membership Selector definitions
enum mac_bss_membership
{
    /// HT PHY
    MAC_BSS_MEMBERSHIP_HT_PHY = 127,
    /// VHT PHY
    MAC_BSS_MEMBERSHIP_VHT_PHY = 126,
};

/// MAC rateset maximum length
#define MAC_RATESET_LEN 12

/// Structure containing the legacy rateset of a station
struct mac_rateset
{
    /// Number of legacy rates supported
    u8_l length;
    /// Array of legacy rates
    u8_l array[MAC_RATESET_LEN];
};

/// MAC Security Key maximum length
#define MAC_SEC_KEY_LEN 32  // TKIP keys 256 bits (max length) with MIC keys

/// Structure defining a security key
struct mac_sec_key
{
    /// Key material length
    u8_l length;
    /// Key material
    u32_l array[MAC_SEC_KEY_LEN/4];
};

/// Access Category enumeration
enum mac_ac
{
    /// Background
    AC_BK = 0,
    /// Best-effort
    AC_BE,
    /// Video
    AC_VI,
    /// Voice
    AC_VO,
    /// Number of access categories
    AC_MAX
};

/// Traffic ID enumeration
enum mac_tid
{
    /// TID_0. Mapped to @ref AC_BE as per 802.11 standard.
    TID_0,
    /// TID_1. Mapped to @ref AC_BK as per 802.11 standard.
    TID_1,
    /// TID_2. Mapped to @ref AC_BK as per 802.11 standard.
    TID_2,
    /// TID_3. Mapped to @ref AC_BE as per 802.11 standard.
    TID_3,
    /// TID_4. Mapped to @ref AC_VI as per 802.11 standard.
    TID_4,
    /// TID_5. Mapped to @ref AC_VI as per 802.11 standard.
    TID_5,
    /// TID_6. Mapped to @ref AC_VO as per 802.11 standard.
    TID_6,
    /// TID_7. Mapped to @ref AC_VO as per 802.11 standard.
    TID_7,
    /// Non standard Management TID used internally
    TID_MGT,
    /// Number of TID supported
    TID_MAX
};

/// MCS bitfield maximum size (in bytes)
#define MAX_MCS_LEN 16 // 16 * 8 = 128

/// MAC HT capability information element
struct mac_htcapability
{
    /// HT capability information
    u16_l ht_capa_info;
    /// A-MPDU parameters
    u8_l a_mpdu_param;
    /// Supported MCS
    u8_l mcs_rate[MAX_MCS_LEN];
    /// HT extended capability information
    u16_l ht_extended_capa;
    /// Beamforming capability information
    u32_l tx_beamforming_capa;
    /// Antenna selection capability information
    u8_l asel_capa;
};

/// MAC VHT capability information element
struct mac_vhtcapability
{
    /// VHT capability information
    u32_l vht_capa_info;
    /// RX MCS map
    u16_l rx_mcs_map;
    /// RX highest data rate
    u16_l rx_highest;
    /// TX MCS map
    u16_l tx_mcs_map;
    /// TX highest data rate
    u16_l tx_highest;
};

/// Length (in bytes) of the MAC HE capability field
#define MAC_HE_MAC_CAPA_LEN 6
/// Length (in bytes) of the PHY HE capability field
#define MAC_HE_PHY_CAPA_LEN 11
/// Maximum length (in bytes) of the PPE threshold data
#define MAC_HE_PPE_THRES_MAX_LEN 25

/// Structure listing the per-NSS, per-BW supported MCS combinations
struct mac_he_mcs_nss_supp
{
    /// per-NSS supported MCS in RX, for BW <= 80MHz
    u16_l rx_mcs_80;
    /// per-NSS supported MCS in TX, for BW <= 80MHz
    u16_l tx_mcs_80;
    /// per-NSS supported MCS in RX, for BW = 160MHz
    u16_l rx_mcs_160;
    /// per-NSS supported MCS in TX, for BW = 160MHz
    u16_l tx_mcs_160;
    /// per-NSS supported MCS in RX, for BW = 80+80MHz
    u16_l rx_mcs_80p80;
    /// per-NSS supported MCS in TX, for BW = 80+80MHz
    u16_l tx_mcs_80p80;
};

/// MAC HE capability information element
struct mac_hecapability
{
    /// MAC HE capabilities
    u8_l mac_cap_info[MAC_HE_MAC_CAPA_LEN];
    /// PHY HE capabilities
    u8_l phy_cap_info[MAC_HE_PHY_CAPA_LEN];
    /// Supported MCS combinations
    struct mac_he_mcs_nss_supp mcs_supp;
    /// PPE Thresholds data
    u8_l ppe_thres[MAC_HE_PPE_THRES_MAX_LEN];
};

/// Station flags
enum mac_sta_flags
{
    /// Bit indicating that a STA has QoS (WMM) capability
    STA_QOS_CAPA = BIT(0),
    /// Bit indicating that a STA has HT capability
    STA_HT_CAPA = BIT(1),
    /// Bit indicating that a STA has VHT capability
    STA_VHT_CAPA = BIT(2),
    /// Bit indicating that a STA has MFP capability
    STA_MFP_CAPA = BIT(3),
    /// Bit indicating that the STA included the Operation Notification IE
    STA_OPMOD_NOTIF = BIT(4),
    /// Bit indicating that a STA has HE capability
    STA_HE_CAPA = BIT(5),
    /// Bit Inidcating supprot for short Preamble in ERP
    STA_SHORT_PREAMBLE_CAPA = BIT(6),
};

/// Connection flags
enum mac_connection_flags
{
    /// Flag indicating whether the control port is controlled by host or not
    CONTROL_PORT_HOST = BIT(0),
    /// Flag indicating whether the control port frame shall be sent unencrypted
    CONTROL_PORT_NO_ENC = BIT(1),
    /// Flag indicating whether HT and VHT shall be disabled or not
    DISABLE_HT = BIT(2),
    /// Flag indicating whether WPA or WPA2 authentication is in use
    WPA_WPA2_IN_USE = BIT(3),
    /// Flag indicating whether MFP is in use
    MFP_IN_USE = BIT(4),
    REASSOCIATION = BIT(5),
    FT_OVER_DS = BIT(6),
};

#endif // LMAC_MAC_H_
