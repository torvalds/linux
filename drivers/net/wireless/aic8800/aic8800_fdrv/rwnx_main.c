/**
 ******************************************************************************
 *
 * @file rwnx_main.c
 *
 * @brief Entry point of the RWNX driver
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/inetdevice.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <net/netlink.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <linux/ctype.h>
#include <linux/random.h>
#include "rwnx_defs.h"
#include "rwnx_dini.h"
#include "rwnx_msg_tx.h"
#include "reg_access.h"
#include "hal_desc.h"
#include "rwnx_debugfs.h"
#include "rwnx_cfgfile.h"
#include "rwnx_irqs.h"
#include "rwnx_radar.h"
#include "rwnx_version.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "rwnx_tdls.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "rwnx_version.h"
#include "rwnx_main.h"
#include "aicwf_txrxif.h"
#include "aicwf_compat_8800dc.h"

#ifdef CONFIG_USB_WIRELESS_EXT
#include "aicwf_wext_linux.h"
#endif

#ifdef AICWF_SDIO_SUPPORT
#include "aicwf_sdio.h"
#endif
#ifdef AICWF_USB_SUPPORT
#include "aicwf_usb.h"
#endif
#include <linux/semaphore.h>

#define RW_DRV_DESCRIPTION  "RivieraWaves 11nac driver for Linux cfg80211"
#define RW_DRV_COPYRIGHT    "Copyright(c) 2015-2017 RivieraWaves"
#define RW_DRV_AUTHOR       "RivieraWaves S.A.S"

#define RWNX_PRINT_CFM_ERR(req) \
        printk(KERN_CRIT "%s: Status Error(%d)\n", #req, (&req##_cfm)->status)

#define RWNX_HT_CAPABILITIES                                    \
{                                                               \
    .ht_supported   = true,                                     \
    .cap            = 0,                                        \
    .ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K,               \
    .ampdu_density  = IEEE80211_HT_MPDU_DENSITY_16,             \
    .mcs        = {                                             \
        .rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },        \
        .rx_highest = cpu_to_le16(65),                          \
        .tx_params = IEEE80211_HT_MCS_TX_DEFINED,               \
    },                                                          \
}

#define RWNX_VHT_CAPABILITIES                                   \
{                                                               \
    .vht_supported = false,                                     \
    .cap       =                                                \
      (7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT),\
    .vht_mcs       = {                                          \
        .rx_mcs_map = cpu_to_le16(                              \
                      IEEE80211_VHT_MCS_SUPPORT_0_9    << 0  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 2  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 4  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 6  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 8  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 10 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 12 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 14),  \
        .tx_mcs_map = cpu_to_le16(                              \
                      IEEE80211_VHT_MCS_SUPPORT_0_9    << 0  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 2  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 4  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 6  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 8  |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 10 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 12 |  \
                      IEEE80211_VHT_MCS_NOT_SUPPORTED  << 14),  \
    }                                                           \
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0) || defined(CONFIG_HE_FOR_OLD_KERNEL)
#define RWNX_HE_CAPABILITIES                                    \
{                                                               \
    .has_he = false,                                            \
    .he_cap_elem = {                                            \
        .mac_cap_info[0] = 0,                                   \
        .mac_cap_info[1] = 0,                                   \
        .mac_cap_info[2] = 0,                                   \
        .mac_cap_info[3] = 0,                                   \
        .mac_cap_info[4] = 0,                                   \
        .mac_cap_info[5] = 0,                                   \
        .phy_cap_info[0] = 0,                                   \
        .phy_cap_info[1] = 0,                                   \
        .phy_cap_info[2] = 0,                                   \
        .phy_cap_info[3] = 0,                                   \
        .phy_cap_info[4] = 0,                                   \
        .phy_cap_info[5] = 0,                                   \
        .phy_cap_info[6] = 0,                                   \
        .phy_cap_info[7] = 0,                                   \
        .phy_cap_info[8] = 0,                                   \
        .phy_cap_info[9] = 0,                                   \
        .phy_cap_info[10] = 0,                                  \
    },                                                          \
    .he_mcs_nss_supp = {                                        \
        .rx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .tx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .rx_mcs_160 = cpu_to_le16(0xffff),                      \
        .tx_mcs_160 = cpu_to_le16(0xffff),                      \
        .rx_mcs_80p80 = cpu_to_le16(0xffff),                    \
        .tx_mcs_80p80 = cpu_to_le16(0xffff),                    \
    },                                                          \
    .ppe_thres = {0x08, 0x1c, 0x07},                            \
}
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#define RWNX_HE_CAPABILITIES                                    \
{                                                               \
    .has_he = false,                                            \
    .he_cap_elem = {                                            \
        .mac_cap_info[0] = 0,                                   \
        .mac_cap_info[1] = 0,                                   \
        .mac_cap_info[2] = 0,                                   \
        .mac_cap_info[3] = 0,                                   \
        .mac_cap_info[4] = 0,                                   \
        .phy_cap_info[0] = 0,                                   \
        .phy_cap_info[1] = 0,                                   \
        .phy_cap_info[2] = 0,                                   \
        .phy_cap_info[3] = 0,                                   \
        .phy_cap_info[4] = 0,                                   \
        .phy_cap_info[5] = 0,                                   \
        .phy_cap_info[6] = 0,                                   \
        .phy_cap_info[7] = 0,                                   \
        .phy_cap_info[8] = 0,                                   \
    },                                                          \
    .he_mcs_nss_supp = {                                        \
        .rx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .tx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .rx_mcs_160 = cpu_to_le16(0xffff),                      \
        .tx_mcs_160 = cpu_to_le16(0xffff),                      \
        .rx_mcs_80p80 = cpu_to_le16(0xffff),                    \
        .tx_mcs_80p80 = cpu_to_le16(0xffff),                    \
    },                                                          \
    .ppe_thres = {0x08, 0x1c, 0x07},                            \
}
#endif
#endif

#define RATE(_bitrate, _hw_rate, _flags) {      \
    .bitrate    = (_bitrate),                   \
    .flags      = (_flags),                     \
    .hw_value   = (_hw_rate),                   \
}

#define CHAN(_freq) {                           \
    .center_freq    = (_freq),                  \
    .max_power  = 30, /* FIXME */               \
}

static struct ieee80211_rate rwnx_ratetable[] = {
    RATE(10,  0x00, 0),
    RATE(20,  0x01, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(55,  0x02, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(110, 0x03, IEEE80211_RATE_SHORT_PREAMBLE),
    RATE(60,  0x04, 0),
    RATE(90,  0x05, 0),
    RATE(120, 0x06, 0),
    RATE(180, 0x07, 0),
    RATE(240, 0x08, 0),
    RATE(360, 0x09, 0),
    RATE(480, 0x0A, 0),
    RATE(540, 0x0B, 0),
};

/* The channels indexes here are not used anymore */
static struct ieee80211_channel rwnx_2ghz_channels[] = {
    CHAN(2412),
    CHAN(2417),
    CHAN(2422),
    CHAN(2427),
    CHAN(2432),
    CHAN(2437),
    CHAN(2442),
    CHAN(2447),
    CHAN(2452),
    CHAN(2457),
    CHAN(2462),
    CHAN(2467),
    CHAN(2472),
    CHAN(2484),
    // Extra channels defined only to be used for PHY measures.
    // Enabled only if custregd and custchan parameters are set
    CHAN(2390),
    CHAN(2400),
    CHAN(2410),
    CHAN(2420),
    CHAN(2430),
    CHAN(2440),
    CHAN(2450),
    CHAN(2460),
    CHAN(2470),
    CHAN(2480),
    CHAN(2490),
    CHAN(2500),
    CHAN(2510),
};

//#ifdef USE_5G
static struct ieee80211_channel rwnx_5ghz_channels[] = {
    CHAN(5180),             // 36 -   20MHz
    CHAN(5200),             // 40 -   20MHz
    CHAN(5220),             // 44 -   20MHz
    CHAN(5240),             // 48 -   20MHz
    CHAN(5260),             // 52 -   20MHz
    CHAN(5280),             // 56 -   20MHz
    CHAN(5300),             // 60 -   20MHz
    CHAN(5320),             // 64 -   20MHz
    CHAN(5500),             // 100 -  20MHz
    CHAN(5520),             // 104 -  20MHz
    CHAN(5540),             // 108 -  20MHz
    CHAN(5560),             // 112 -  20MHz
    CHAN(5580),             // 116 -  20MHz
    CHAN(5600),             // 120 -  20MHz
    CHAN(5620),             // 124 -  20MHz
    CHAN(5640),             // 128 -  20MHz
    CHAN(5660),             // 132 -  20MHz
    CHAN(5680),             // 136 -  20MHz
    CHAN(5700),             // 140 -  20MHz
    CHAN(5720),             // 144 -  20MHz
    CHAN(5745),             // 149 -  20MHz
    CHAN(5765),             // 153 -  20MHz
    CHAN(5785),             // 157 -  20MHz
    CHAN(5805),             // 161 -  20MHz
    CHAN(5825),             // 165 -  20MHz
    // Extra channels defined only to be used for PHY measures.
    // Enabled only if custregd and custchan parameters are set
    CHAN(5190),
    CHAN(5210),
    CHAN(5230),
    CHAN(5250),
    CHAN(5270),
    CHAN(5290),
    CHAN(5310),
    CHAN(5330),
    CHAN(5340),
    CHAN(5350),
    CHAN(5360),
    CHAN(5370),
    CHAN(5380),
    CHAN(5390),
    CHAN(5400),
    CHAN(5410),
    CHAN(5420),
    CHAN(5430),
    CHAN(5440),
    CHAN(5450),
    CHAN(5460),
    CHAN(5470),
    CHAN(5480),
    CHAN(5490),
    CHAN(5510),
    CHAN(5530),
    CHAN(5550),
    CHAN(5570),
    CHAN(5590),
    CHAN(5610),
    CHAN(5630),
    CHAN(5650),
    CHAN(5670),
    CHAN(5690),
    CHAN(5710),
    CHAN(5730),
    CHAN(5750),
    CHAN(5760),
    CHAN(5770),
    CHAN(5780),
    CHAN(5790),
    CHAN(5800),
    CHAN(5810),
    CHAN(5820),
    CHAN(5830),
    CHAN(5840),
    CHAN(5850),
    CHAN(5860),
    CHAN(5870),
    CHAN(5880),
    CHAN(5890),
    CHAN(5900),
    CHAN(5910),
    CHAN(5920),
    CHAN(5930),
    CHAN(5940),
    CHAN(5950),
    CHAN(5960),
    CHAN(5970),
};
//#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)) || defined(CONFIG_HE_FOR_OLD_KERNEL)
struct ieee80211_sband_iftype_data rwnx_he_capa = {
    .types_mask = BIT(NL80211_IFTYPE_STATION)|BIT(NL80211_IFTYPE_AP),
    .he_cap = RWNX_HE_CAPABILITIES,
};
#endif

static struct ieee80211_supported_band rwnx_band_2GHz = {
    .channels   = rwnx_2ghz_channels,
    .n_channels = ARRAY_SIZE(rwnx_2ghz_channels) - 13, // -13 to exclude extra channels
    .bitrates   = rwnx_ratetable,
    .n_bitrates = ARRAY_SIZE(rwnx_ratetable),
    .ht_cap     = RWNX_HT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    .vht_cap    = RWNX_VHT_CAPABILITIES,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    .iftype_data = &rwnx_he_capa,
    .n_iftype_data = 1,
#endif
};

//#ifdef USE_5G
static struct ieee80211_supported_band rwnx_band_5GHz = {
    .channels   = rwnx_5ghz_channels,
    .n_channels = ARRAY_SIZE(rwnx_5ghz_channels) - 59, // -59 to exclude extra channels
    .bitrates   = &rwnx_ratetable[4],
    .n_bitrates = ARRAY_SIZE(rwnx_ratetable) - 4,
    .ht_cap     = RWNX_HT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    .vht_cap    = RWNX_VHT_CAPABILITIES,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    .iftype_data = &rwnx_he_capa,
    .n_iftype_data = 1,
#endif
};
//#endif

static struct ieee80211_iface_limit rwnx_limits[] = {
    { .max = 1,
      .types = BIT(NL80211_IFTYPE_STATION)},
    { .max = 1,
      .types = BIT(NL80211_IFTYPE_AP)},
#ifdef CONFIG_USE_P2P0
    { .max = 2,
#else
    { .max = 1,
#endif
      .types = BIT(NL80211_IFTYPE_P2P_CLIENT) | BIT(NL80211_IFTYPE_P2P_GO)},
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
#ifndef CONFIG_USE_P2P0
    { .max = 1,
      .types = BIT(NL80211_IFTYPE_P2P_DEVICE),
    }
#endif
#endif
};

static struct ieee80211_iface_limit rwnx_limits_dfs[] = {
    { .max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP)}
};

static const struct ieee80211_iface_combination rwnx_combinations[] = {
    {
        .limits                 = rwnx_limits,
        .n_limits               = ARRAY_SIZE(rwnx_limits),
        .num_different_channels = NX_CHAN_CTXT_CNT,
        .max_interfaces         = NX_VIRT_DEV_MAX,
    },
    /* Keep this combination as the last one */
    {
        .limits                 = rwnx_limits_dfs,
        .n_limits               = ARRAY_SIZE(rwnx_limits_dfs),
        .num_different_channels = 1,
        .max_interfaces         = NX_VIRT_DEV_MAX,
        .radar_detect_widths = (BIT(NL80211_CHAN_WIDTH_20_NOHT) |
                                BIT(NL80211_CHAN_WIDTH_20) |
                                BIT(NL80211_CHAN_WIDTH_40) |
                                BIT(NL80211_CHAN_WIDTH_80)),
    }
};

/* There isn't a lot of sense in it, but you can transmit anything you like */
static struct ieee80211_txrx_stypes
rwnx_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
    [NL80211_IFTYPE_STATION] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4)),
    },
    [NL80211_IFTYPE_AP] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_AP_VLAN] = {
        /* copy AP */
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_CLIENT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_P2P_GO] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    [NL80211_IFTYPE_P2P_DEVICE] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
#endif
    [NL80211_IFTYPE_MESH_POINT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4)),
    },
};


static u32 cipher_suites[] = {
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_CCMP,
    WLAN_CIPHER_SUITE_AES_CMAC, // reserved entries to enable AES-CMAC and/or SMS4
    0,
};
#define NB_RESERVED_CIPHER 1;

static const int rwnx_ac2hwq[1][NL80211_NUM_ACS] = {
    {
        [NL80211_TXQ_Q_VO] = RWNX_HWQ_VO,
        [NL80211_TXQ_Q_VI] = RWNX_HWQ_VI,
        [NL80211_TXQ_Q_BE] = RWNX_HWQ_BE,
        [NL80211_TXQ_Q_BK] = RWNX_HWQ_BK
    }
};

const int rwnx_tid2hwq[IEEE80211_NUM_TIDS] = {
    RWNX_HWQ_BE,
    RWNX_HWQ_BK,
    RWNX_HWQ_BK,
    RWNX_HWQ_BE,
    RWNX_HWQ_VI,
    RWNX_HWQ_VI,
    RWNX_HWQ_VO,
    RWNX_HWQ_VO,
    /* TID_8 is used for management frames */
    RWNX_HWQ_VO,
    /* At the moment, all others TID are mapped to BE */
    RWNX_HWQ_BE,
    RWNX_HWQ_BE,
    RWNX_HWQ_BE,
    RWNX_HWQ_BE,
    RWNX_HWQ_BE,
    RWNX_HWQ_BE,
    RWNX_HWQ_BE,
};

static const int rwnx_hwq2uapsd[NL80211_NUM_ACS] = {
    [RWNX_HWQ_VO] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,
    [RWNX_HWQ_VI] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VI,
    [RWNX_HWQ_BE] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BE,
    [RWNX_HWQ_BK] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BK,
};

#define P2P_ALIVE_TIME_MS       (1*1000)
#define P2P_ALIVE_TIME_COUNT    200

extern uint8_t scanning;
extern uint8_t p2p_working;
struct semaphore aicwf_deinit_sem;
atomic_t aicwf_deinit_atomic;

int aicwf_dbg_level = LOGERROR|LOGINFO;
module_param(aicwf_dbg_level, int, 0660);

int testmode = 0;
char aic_fw_path[200];

extern void set_testmode(int);


void rwnx_skb_align_8bytes(struct sk_buff *skb){
#ifdef CONFIG_ALIGN_8BYTES
	int align __maybe_unused;
	u8 *data;
	size_t len = 0;

	align = ((unsigned long)(skb->data + 14)) & 7;
	if (align) {
		if (WARN_ON(skb_headroom(skb) < 7)) {
			dev_kfree_skb(skb);
			skb = NULL;
		} else {
			//printk("AIDEN align1:%d rx_skb->data + 14:%p\r\n", align, skb->data + 14);
			data = skb->data;
			len = skb_headlen(skb);
			skb->data -= align;
			memmove(skb->data, data, len);
			skb_set_tail_pointer(skb, len);
			//printk("AIDEN align2:%d rx_skb->data + 14:%p\r\n", align, skb->data + 14);
		}
	}
#endif
}

int rwnx_init_cmd_array(void);
void rwnx_free_cmd_array(void);

void rwnx_data_dump(char* tag, void* data, unsigned long len){
	unsigned long i = 0;
	char* data_ = (char* )data;

	AICWFDBG(LOGDATA, "%s %s len:(%lu)\r\n", __func__, tag, len);

	for (i = 0; i < len; i += 16){
	AICWFDBG(LOGDATA, "%02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
		data_[0 + i],
		data_[1 + i],
		data_[2 + i],
		data_[3 + i],
		data_[4 + i],
		data_[5 + i],
		data_[6 + i],
		data_[7 + i],
		data_[8 + i],
		data_[9 + i],
		data_[10 + i],
		data_[11 + i],
		data_[12 + i],
		data_[13 + i],
		data_[14 + i],
		data_[15 + i]);
		}

}

#define ASSOC_REQ 0x00
#define ASSOC_RSP 0x10
#define PROBE_REQ 0x40
#define PROBE_RSP 0x50
#define ACTION 0xD0
#define AUTH 0xB0
#define DEAUTH 0xC0
#define QOS 0x88

#define ACTION_MAC_HDR_LEN 24
#define QOS_MAC_HDR_LEN 26

void rwnx_frame_parser(char* tag, char* data, unsigned long len){
	char print_data[100];
	int print_index = 0;

	memset(print_data, 0, 100);

	if(data[0] == ASSOC_REQ){
		sprintf(&print_data[print_index], "%s", "ASSOC_REQ \r\n");
		print_index = strlen(print_data);
	}else if(data[0] == ASSOC_RSP){
		sprintf(&print_data[print_index], "%s", "ASSOC_RSP \r\n");
		print_index = strlen(print_data);
	}else if(data[0] == PROBE_REQ){
		sprintf(&print_data[print_index], "%s", "PROBE_REQ \r\n");
		print_index = strlen(print_data);
	}else if(data[0] == PROBE_RSP){
		sprintf(&print_data[print_index], "%s", "PROBE_RSP \r\n");
		print_index = strlen(print_data);
	}else if(data[0] == ACTION){
		sprintf(&print_data[print_index], "%s", "ACTION ");
		print_index = strlen(print_data);
		if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x00){
			sprintf(&print_data[print_index], "%s", "GO_NEG_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x01){
			sprintf(&print_data[print_index], "%s", "GO_NEG_RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x02){
			sprintf(&print_data[print_index], "%s", "GO_NEG_CFM \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x03){
			sprintf(&print_data[print_index], "%s", "P2P_INV_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x04){
			sprintf(&print_data[print_index], "%s", "P2P_INV_RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x05){
			sprintf(&print_data[print_index], "%s", "DD_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x06){
			sprintf(&print_data[print_index], "%s", "DD_RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x07){
			sprintf(&print_data[print_index], "%s", "PD_REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x08){
			sprintf(&print_data[print_index], "%s", "PD_RSP \r\n");
		}else{
			sprintf(&print_data[print_index], "%s(0x%x 0x%x) \r\n", "UNKNOW",
				data[ACTION_MAC_HDR_LEN], data[ACTION_MAC_HDR_LEN + 6]);
		}
		print_index = strlen(print_data);
	}else if(data[0] == AUTH){
		sprintf(&print_data[print_index], "%s", "AUTH \r\n");
		print_index = strlen(print_data);
	}else if(data[0] == DEAUTH){
		sprintf(&print_data[print_index], "%s", "DEAUTH \r\n");
		print_index = strlen(print_data);
	}else if(data[0] == QOS){
		if(data[QOS_MAC_HDR_LEN + 6] == 0x88 && data[QOS_MAC_HDR_LEN + 7] == 0x8E){
			sprintf(&print_data[print_index], "%s", "QOS_802.1X ");
			if(data[QOS_MAC_HDR_LEN + 9] == 0x03){
				sprintf(&print_data[print_index], "%s", "EAPOL \r\n");
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x00){
				sprintf(&print_data[print_index], "%s", "EAP_PACKAGE ");
				print_index = strlen(print_data);
				if(data[QOS_MAC_HDR_LEN + 12] == 0x01){
					sprintf(&print_data[print_index], "%s", "EAP_REQ \r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x02){
					sprintf(&print_data[print_index], "%s", "EAP_RSP \r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x04){
					sprintf(&print_data[print_index], "%s", "EAP_FAIL \r\n");
				}else{
					sprintf(&print_data[print_index], "%s", "UNKNOW \r\n");
				}
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x01){
				sprintf(&print_data[print_index], "%s","EAP_START \r\n");
			}
			print_index = strlen(print_data);
		}
	}

	if(print_index > 0){
		AICWFDBG(LOGDATA, "%s %s", tag, print_data);
	}

#if 0
	if(data[0] == ASSOC_REQ){
		printk("%s %s ASSOC_REQ \r\n", __func__, tag);
	}else if(data[0] == ASSOC_RSP){
		printk("%s %s ASSOC_RSP \r\n", __func__, tag);
	}else if(data[0] == PROBE_REQ){
		printk("%s %s PROBE_REQ \r\n", __func__, tag);
	}else if(data[0] == PROBE_RSP){
		printk("%s %s PROBE_RSP \r\n", __func__, tag);
	}else if(data[0] == ACTION){
		printk("%s %s ACTION ", __func__, tag);
		if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x00){
			printk("GO NEG REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x01){
			printk("GO NEG RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x02){
			printk("GO NEG CFM \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x03){
			printk("P2P INV REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x04){
			printk("P2P INV RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x05){
			printk("DD REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x06){
			printk("DD RSP \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x07){
			printk("PD REQ \r\n");
		}else if(data[ACTION_MAC_HDR_LEN] == 0x04 && data[ACTION_MAC_HDR_LEN + 6] == 0x08){
			printk("PD RSP \r\n");
		}else{
			printk("\r\n");
		}

	}else if(data[0] == AUTH){
		printk("%s %s AUTH \r\n", __func__, tag);
	}else if(data[0] == DEAUTH){
		printk("%s %s DEAUTH \r\n", __func__, tag);
	}else if(data[0] == QOS){
		if(data[QOS_MAC_HDR_LEN + 6] == 0x88 && data[QOS_MAC_HDR_LEN + 7] == 0x8E){
			printk("%s %s QOS 802.1X ", __func__, tag);
			if(data[QOS_MAC_HDR_LEN + 9] == 0x03){
				printk("EAPOL");
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x00){
				printk("EAP PACKAGE ");
				if(data[QOS_MAC_HDR_LEN + 12] == 0x01){
					printk("EAP REQ\r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x02){
					printk("EAP RSP\r\n");
				}else if(data[QOS_MAC_HDR_LEN + 12] == 0x04){
					printk("EAP FAIL\r\n");
				}else{
					printk("\r\n");
				}
			}else if(data[QOS_MAC_HDR_LEN + 9] == 0x01){
				printk("EAP START \r\n");

			}
		}
	}
#endif
}

/*********************************************************************
 * helper
 *********************************************************************/
struct rwnx_sta *rwnx_get_sta(struct rwnx_hw *rwnx_hw, const u8 *mac_addr)
{
    int i;

    for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
        struct rwnx_sta *sta = &rwnx_hw->sta_table[i];
        if (sta->valid && (memcmp(mac_addr, &sta->mac_addr, 6) == 0))
            return sta;
    }

    return NULL;
}

void rwnx_enable_wapi(struct rwnx_hw *rwnx_hw)
{
    cipher_suites[rwnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_SMS4;
    rwnx_hw->wiphy->n_cipher_suites ++;
    rwnx_hw->wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;
}

void rwnx_enable_mfp(struct rwnx_hw *rwnx_hw)
{
    cipher_suites[rwnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_AES_CMAC;
    rwnx_hw->wiphy->n_cipher_suites ++;
}

#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
extern u8_l vendor_extension_data[256];
extern u8_l vendor_extension_len;

void rwnx_insert_vendor_extension_in_bcn(struct rwnx_bcn *bcn){
	u8_l temp_ie[256];
	u8_l vendor_extension_subelement[3] = {0x00,0x37,0x2A};
	u8_l vendor_extension_id[2] = {0x10,0x49};
	u8_l wps_ie[1] = {0xDD};
	u8_l wps_len_index = 0;
	int index = 0;
	int vendor_extension_subelement_len = 0;
	int find_wps_ie = 0;

	memset(temp_ie, 0, 256);

	//find wps_ie
	for(index = 0; index < bcn->tail_len; index++){
		if(bcn->tail[index] == wps_ie[0]){
			find_wps_ie = 1;
			wps_len_index = index + 1;
		}

		if(find_wps_ie && bcn->tail[index] == vendor_extension_id[0]){
			if(bcn->tail[index + 1] == vendor_extension_id[1]){
				break;
			}
		}
	}


	//find vendor_extension_subelement
	for(index = 0; index < bcn->tail_len; index++){
		if(bcn->tail[index] == vendor_extension_id[0]){
			index++;
			if(index == bcn->tail_len){
				return;
			}
			if(bcn->tail[index] == vendor_extension_id[1] &&
				bcn->tail[index + 3] == vendor_extension_subelement[0]&&
				bcn->tail[index + 4] == vendor_extension_subelement[1]&&
				bcn->tail[index + 5] == vendor_extension_subelement[2]){
				index = index + 2;
				vendor_extension_subelement_len = bcn->tail[index];
				printk("%s find vendor_extension_subelement,index:%d len:%d\r\n", __func__, index, bcn->tail[index]);
				break;
			}
		}
	}
	index = index + vendor_extension_subelement_len;

	//insert vendor extension
	memcpy(&temp_ie[0], bcn->tail, index + 1);
	memcpy(&temp_ie[index + 1], vendor_extension_data, vendor_extension_len/*sizeof(vendor_extension_data)*/);//insert vendor extension data
	memcpy(&temp_ie[index + 1 + vendor_extension_len/*sizeof(vendor_extension_data)*/], &bcn->tail[index + 1], bcn->tail_len - index);

	memcpy(bcn->tail, temp_ie, bcn->tail_len + vendor_extension_len/*sizeof(vendor_extension_data)*/);
	bcn->tail_len = bcn->tail_len + vendor_extension_len/*sizeof(vendor_extension_data)*/;

	bcn->tail[wps_len_index] = bcn->tail[wps_len_index] + vendor_extension_len;
	//rwnx_data_dump((char*)__func__, (void*)ie_req->ie, ie_req->add_ie_len);
}
#endif

u8 *rwnx_build_bcn(struct rwnx_bcn *bcn, struct cfg80211_beacon_data *new)
{
    u8 *buf, *pos;

    if (new->head) {
        u8 *head = kmalloc(new->head_len, GFP_KERNEL);

        if (!head)
            return NULL;

        if (bcn->head)
            kfree(bcn->head);

        bcn->head = head;
        bcn->head_len = new->head_len;
        memcpy(bcn->head, new->head, new->head_len);
    }
    if (new->tail) {
        u8 *tail = kmalloc(new->tail_len, GFP_KERNEL);

        if (!tail)
            return NULL;

        if (bcn->tail)
            kfree(bcn->tail);

        bcn->tail = tail;
        bcn->tail_len = new->tail_len;
        memcpy(bcn->tail, new->tail, new->tail_len);
    }

    if (!bcn->head)
        return NULL;

    bcn->tim_len = 6;
    bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;
#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
    buf = kmalloc(bcn->len + vendor_extension_len, GFP_KERNEL);
#else
	buf = kmalloc(bcn->len, GFP_KERNEL);
#endif
    if (!buf)
        return NULL;

    // Build the beacon buffer
    pos = buf;
    memcpy(pos, bcn->head, bcn->head_len);
    pos += bcn->head_len;
    *pos++ = WLAN_EID_TIM;
    *pos++ = 4;
    *pos++ = 0;
    *pos++ = bcn->dtim;
    *pos++ = 0;
    *pos++ = 0;
    if (bcn->tail) {
#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
		rwnx_insert_vendor_extension_in_bcn(bcn);
#endif
        memcpy(pos, bcn->tail, bcn->tail_len);
        pos += bcn->tail_len;
    }
    if (bcn->ies) {
        memcpy(pos, bcn->ies, bcn->ies_len);
    }

    return buf;
}


static void rwnx_del_bcn(struct rwnx_bcn *bcn)
{
    if (bcn->head) {
        kfree(bcn->head);
        bcn->head = NULL;
    }
    bcn->head_len = 0;

    if (bcn->tail) {
        kfree(bcn->tail);
        bcn->tail = NULL;
    }
    bcn->tail_len = 0;

    if (bcn->ies) {
        kfree(bcn->ies);
        bcn->ies = NULL;
    }
    bcn->ies_len = 0;
    bcn->tim_len = 0;
    bcn->dtim = 0;
    bcn->len = 0;
}

/**
 * Link channel ctxt to a vif and thus increments count for this context.
 */
void rwnx_chanctx_link(struct rwnx_vif *vif, u8 ch_idx,
                       struct cfg80211_chan_def *chandef)
{
    struct rwnx_chanctx *ctxt;

    if (ch_idx >= NX_CHAN_CTXT_CNT) {
        WARN(1, "Invalid channel ctxt id %d", ch_idx);
        return;
    }

    vif->ch_index = ch_idx;
    ctxt = &vif->rwnx_hw->chanctx_table[ch_idx];
    ctxt->count++;

    // For now chandef is NULL for STATION interface
    if (chandef) {
        if (!ctxt->chan_def.chan)
            ctxt->chan_def = *chandef;
        else {
            // TODO. check that chandef is the same as the one already
            // set for this ctxt
        }
    }
}

/**
 * Unlink channel ctxt from a vif and thus decrements count for this context
 */
void rwnx_chanctx_unlink(struct rwnx_vif *vif)
{
    struct rwnx_chanctx *ctxt;

    if (vif->ch_index == RWNX_CH_NOT_SET)
        return;

    ctxt = &vif->rwnx_hw->chanctx_table[vif->ch_index];

    if (ctxt->count == 0) {
        WARN(1, "Chan ctxt ref count is already 0");
    } else {
        ctxt->count--;
    }

    if (ctxt->count == 0) {
        if (vif->ch_index == vif->rwnx_hw->cur_chanctx) {
            /* If current chan ctxt is no longer linked to a vif
               disable radar detection (no need to check if it was activated) */
            rwnx_radar_detection_enable(&vif->rwnx_hw->radar,
                                        RWNX_RADAR_DETECT_DISABLE,
                                        RWNX_RADAR_RIU);
        }
        /* set chan to null, so that if this ctxt is relinked to a vif that
           don't have channel information, don't use wrong information */
        ctxt->chan_def.chan = NULL;
    }
    vif->ch_index = RWNX_CH_NOT_SET;
}

int rwnx_chanctx_valid(struct rwnx_hw *rwnx_hw, u8 ch_idx)
{
    if (ch_idx >= NX_CHAN_CTXT_CNT ||
        rwnx_hw->chanctx_table[ch_idx].chan_def.chan == NULL) {
        return 0;
    }

    return 1;
}

static void rwnx_del_csa(struct rwnx_vif *vif)
{
    //struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
    struct rwnx_csa *csa = vif->ap.csa;

    if (!csa)
        return;

    rwnx_del_bcn(&csa->bcn);
    kfree(csa);
    vif->ap.csa = NULL;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static void rwnx_csa_finish(struct work_struct *ws)
{

    struct rwnx_csa *csa = container_of(ws, struct rwnx_csa, work);
    struct rwnx_vif *vif = csa->vif;
    struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
    int error = csa->status;
    u8 *buf, *pos;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

    buf = kmalloc(csa->bcn.len, GFP_KERNEL);
    if (!buf) {
        printk ("%s buf fail\n", __func__);
        return;
    }
    pos = buf;

    memcpy(pos, csa->bcn.head, csa->bcn.head_len);
    pos += csa->bcn.head_len;
    *pos++ = WLAN_EID_TIM;
    *pos++ = 4;
    *pos++ = 0;
    *pos++ = csa->bcn.dtim;
    *pos++ = 0;
    *pos++ = 0;
    if (csa->bcn.tail) {
        memcpy(pos, csa->bcn.tail, csa->bcn.tail_len);
        pos += csa->bcn.tail_len;
    }
    if (csa->bcn.ies) {
        memcpy(pos, csa->bcn.ies, csa->bcn.ies_len);
    }

    if (!error) {
        error = rwnx_send_bcn(rwnx_hw, buf, vif->vif_index, csa->bcn.len);
        if (error)
            return;
        error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, csa->elem.dma_addr,
                                     csa->bcn.len, csa->bcn.head_len,
                                     csa->bcn.tim_len, NULL);
    }

    if (error) {
        #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
        cfg80211_stop_iface(rwnx_hw->wiphy, &vif->wdev, GFP_KERNEL);
        #else
        cfg80211_disconnected(vif->ndev, 0, NULL, 0, 0, GFP_KERNEL);
        #endif
    } else {
        mutex_lock(&vif->wdev.mtx);
        __acquire(&vif->wdev.mtx);
        spin_lock_bh(&rwnx_hw->cb_lock);
        rwnx_chanctx_unlink(vif);
        rwnx_chanctx_link(vif, csa->ch_idx, &csa->chandef);
        if (rwnx_hw->cur_chanctx == csa->ch_idx) {
            rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
            rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
        } else
            rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
        spin_unlock_bh(&rwnx_hw->cb_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
        cfg80211_ch_switch_notify(vif->ndev, &csa->chandef);
#endif
        mutex_unlock(&vif->wdev.mtx);
        __release(&vif->wdev.mtx);
    }
    rwnx_del_csa(vif);
}
#endif

/**
 * rwnx_external_auth_enable - Enable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be enabled
 *
 * External authentication requires to start TXQ for unknown STA in
 * order to send auth frame pusehd by user space.
 * Note: It is assumed that fw is on the correct channel.
 */
void rwnx_external_auth_enable(struct rwnx_vif *vif)
{
    vif->sta.external_auth = true;
    rwnx_txq_unk_vif_init(vif);
    rwnx_txq_start(rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE), 0);
}

/**
 * rwnx_external_auth_disable - Disable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be disabled
 */
void rwnx_external_auth_disable(struct rwnx_vif *vif)
{
    if (!vif->sta.external_auth)
        return;

    vif->sta.external_auth = false;
    rwnx_txq_unk_vif_deinit(vif);
}

/**
 * rwnx_update_mesh_power_mode -
 *
 * @vif: mesh VIF  for which power mode is updated
 *
 * Does nothing if vif is not a mesh point interface.
 * Since firmware doesn't support one power save mode per link select the
 * most "active" power mode among all mesh links.
 * Indeed as soon as we have to be active on one link we might as well be
 * active on all links.
 *
 * If there is no link then the power mode for next peer is used;
 */
void rwnx_update_mesh_power_mode(struct rwnx_vif *vif)
{
    enum nl80211_mesh_power_mode mesh_pm;
    struct rwnx_sta *sta;
    struct mesh_config mesh_conf;
    struct mesh_update_cfm cfm;
    u32 mask;

    if (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT)
        return;

    if (list_empty(&vif->ap.sta_list)) {
        mesh_pm = vif->ap.next_mesh_pm;
    } else {
        mesh_pm = NL80211_MESH_POWER_DEEP_SLEEP;
        list_for_each_entry(sta, &vif->ap.sta_list, list) {
            if (sta->valid && (sta->mesh_pm < mesh_pm)) {
                mesh_pm = sta->mesh_pm;
            }
        }
    }

    if (mesh_pm == vif->ap.mesh_pm)
        return;

    mask = BIT(NL80211_MESHCONF_POWER_MODE - 1);
    mesh_conf.power_mode = mesh_pm;
    if (rwnx_send_mesh_update_req(vif->rwnx_hw, vif, mask, &mesh_conf, &cfm) ||
        cfm.status)
        return;

    vif->ap.mesh_pm = mesh_pm;
}

#ifdef CONFIG_BR_SUPPORT
void netdev_br_init(struct net_device *netdev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(netdev);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
#endif

	/* if(check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) */
	{
		/* struct net_bridge	*br = netdev->br_port->br; */ /* ->dev->dev_addr; */
		#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		if (netdev->br_port)
		#else
		if (rcu_dereference(rwnx_vif->ndev->rx_handler_data))
		#endif
		{
			struct net_device *br_netdev;

			br_netdev = dev_get_by_name(&init_net, CONFIG_BR_SUPPORT_BRNAME);
			if (br_netdev) {
				memcpy(rwnx_vif->br_mac, br_netdev->dev_addr, ETH_ALEN);
				dev_put(br_netdev);
				AICWFDBG(LOGINFO, FUNC_NDEV_FMT" bind bridge dev "NDEV_FMT"("MAC_FMT")\n"
					, FUNC_NDEV_ARG(netdev), NDEV_ARG(br_netdev), MAC_ARG(br_netdev->dev_addr));
			} else {
				AICWFDBG(LOGINFO, FUNC_NDEV_FMT" can't get bridge dev by name \"%s\"\n"
					, FUNC_NDEV_ARG(netdev), CONFIG_BR_SUPPORT_BRNAME);
			}
		}

		rwnx_vif->ethBrExtInfo.addPPPoETag = 1;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_unlock();
#endif
}
#endif /* CONFIG_BR_SUPPORT */


/*********************************************************************
 * netdev callbacks
 ********************************************************************/
/**
 * int (*ndo_open)(struct net_device *dev);
 *     This function is called when network device transistions to the up
 *     state.
 *
 * - Start FW if this is the first interface opened
 * - Add interface at fw level
 */
static int rwnx_open(struct net_device *dev)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
    struct mm_add_if_cfm add_if_cfm;
    int error = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    // Check if it is the first opened VIF
    if (rwnx_hw->vif_started == 0)
    {
        // Start the FW
       if ((error = rwnx_send_start(rwnx_hw)))
           return error;

       /* Device is now started */
       set_bit(RWNX_DEV_STARTED, &rwnx_hw->drv_flags);
	   atomic_set(&rwnx_vif->drv_conn_state, RWNX_DRV_STATUS_DISCONNECTED);
    }

    if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) {
        /* For AP_vlan use same fw and drv indexes. We ensure that this index
           will not be used by fw for another vif by taking index >= NX_VIRT_DEV_MAX */
        add_if_cfm.inst_nbr = rwnx_vif->drv_vif_index;
        netif_tx_stop_all_queues(dev);

        /* Save the index retrieved from LMAC */
        spin_lock_bh(&rwnx_hw->cb_lock);
        rwnx_vif->vif_index = add_if_cfm.inst_nbr;
        rwnx_vif->up = true;
        rwnx_hw->vif_started++;
        rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
        spin_unlock_bh(&rwnx_hw->cb_lock);
    } else {
        /* Forward the information to the LMAC,
         *     p2p value not used in FMAC configuration, iftype is sufficient */

        if ((error = rwnx_send_add_if(rwnx_hw, rwnx_vif->wdev.address,
                                      RWNX_VIF_TYPE(rwnx_vif), false, &add_if_cfm))) {
            AICWFDBG(LOGERROR, "add if fail\n");
            return error;
        }

        if (add_if_cfm.status != 0) {
            RWNX_PRINT_CFM_ERR(add_if);
            return -EIO;
        }

        /* Save the index retrieved from LMAC */
        spin_lock_bh(&rwnx_hw->cb_lock);
        rwnx_vif->vif_index = add_if_cfm.inst_nbr;
        rwnx_vif->up = true;
        rwnx_hw->vif_started++;
        rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
        spin_unlock_bh(&rwnx_hw->cb_lock);
#ifdef CONFIG_USE_P2P0
        if(rwnx_vif->is_p2p_vif){
            rwnx_hw->p2p_dev_vif = rwnx_vif;
            rwnx_hw->is_p2p_alive = 1;
        }
#endif

    }

    if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)
    {
        #ifdef CONFIG_COEX
        rwnx_send_coex_req(rwnx_hw, 1, 0);
        #endif
    }

    if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR){
        rwnx_hw->monitor_vif = rwnx_vif->vif_index;
        if (rwnx_vif->ch_index != RWNX_CH_NOT_SET){
            //Configure the monitor channel
            error = rwnx_send_config_monitor_req(rwnx_hw, &rwnx_hw->chanctx_table[rwnx_vif->ch_index].chan_def, NULL);
        }
        #if defined(CONFIG_RWNX_MON_XMIT)
        rwnx_txq_unk_vif_init(rwnx_vif);
        #endif
        #if defined(CONFIG_RWNX_MON_RXFILTER)
        rwnx_send_set_filter(rwnx_hw, (FIF_BCN_PRBRESP_PROMISC | FIF_OTHER_BSS | FIF_PSPOLL | FIF_PROBE_REQ));
        #endif
    }

    //netif_carrier_off(dev);
    #if defined(CONFIG_RWNX_MON_XMIT)
    netif_carrier_on(dev);
	AICWFDBG(LOGINFO, "monitor xmit: netif_carrier_on\n");
    #endif

#ifdef CONFIG_BR_SUPPORT
    netdev_br_init(dev);
#endif /* CONFIG_BR_SUPPORT */

    //netif_carrier_off(dev);
    netif_start_queue(dev);

    return error;
}

/**
 * int (*ndo_stop)(struct net_device *dev);
 *     This function is called when network device transistions to the down
 *     state.
 *
 * - Remove interface at fw level
 * - Reset FW if this is the last interface opened
 */
static int rwnx_close(struct net_device *dev)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
    struct aicwf_bus *bus_if = NULL;
	int ret = 0;
#if defined(AICWF_USB_SUPPORT)
    struct aic_usb_dev *usbdev = NULL;
    bus_if = dev_get_drvdata(rwnx_hw->dev);
    if (bus_if)
        usbdev = bus_if->bus_priv.usb;
#endif
#if defined(AICWF_SDIO_SUPPORT)
    struct aic_sdio_dev *sdiodev = NULL;
    bus_if = dev_get_drvdata(rwnx_hw->dev);
    if (bus_if)
        sdiodev = bus_if->bus_priv.sdio;
#endif

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#if defined(AICWF_USB_SUPPORT) || defined(AICWF_SDIO_SUPPORT)
    if (scanning){
        scanning = false;
    }

	if(p2p_working){
		p2p_working = false;
	}
#endif
#if 0
    netdev_info(dev, "CLOSE");
#endif
	AICWFDBG(LOGINFO, "%s %s Enter\n", __func__, dev->name);

#ifdef CONFIG_USE_P2P0
    if(rwnx_hw->p2p_dev_vif){
        atomic_set(&rwnx_hw->p2p_alive_timer_count, P2P_ALIVE_TIME_MS);
        rwnx_hw->is_p2p_alive = 0;
    }
#endif

    rwnx_radar_cancel_cac(&rwnx_hw->radar);

    /* Abort scan request on the vif */
    if (rwnx_hw->scan_request &&
        rwnx_hw->scan_request->wdev == &rwnx_vif->wdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        struct cfg80211_scan_info info = {
            .aborted = true,
        };

        cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
        cfg80211_scan_done(rwnx_hw->scan_request, true);
#endif
        rwnx_hw->scan_request = NULL;
		ret = rwnx_send_scanu_cancel_req(rwnx_hw, NULL);
		mdelay(35);//make sure firmware take affect
		if (ret) {
			AICWFDBG(LOGERROR, "scanu_cancel fail\n");
			return ret;
		}
    }

#if defined(AICWF_USB_SUPPORT)
    if (usbdev != NULL) {
        if (usbdev->bus_if->state != BUS_DOWN_ST && usbdev->state != USB_DOWN_ST)
			AICWFDBG(LOGINFO, "state: %d %d \r\n", (int)usbdev->bus_if->state, (int)usbdev->state);

		if(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
			RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT){
			if(atomic_read(&rwnx_vif->drv_conn_state) == (int)RWNX_DRV_STATUS_CONNECTING){
				rwnx_send_sm_disconnect_req(rwnx_hw, rwnx_vif, 3);
				atomic_set(&rwnx_vif->drv_conn_state, RWNX_DRV_STATUS_DISCONNECTED);
			}
		}
#ifdef CONFIG_USE_P2P0
        if(!rwnx_vif->is_p2p_vif || ( rwnx_vif->is_p2p_vif && rwnx_hw->is_p2p_alive)){
#endif
    		rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index, false);
#ifdef CONFIG_USE_P2P0
        }
#endif

    }
#endif
#if defined(AICWF_SDIO_SUPPORT)
    if (sdiodev != NULL ) {
        if (sdiodev->bus_if->state != BUS_DOWN_ST)
            rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index, false);
    }
#endif

    if (rwnx_hw->roc_elem && (rwnx_hw->roc_elem->wdev == &rwnx_vif->wdev)) {
		AICWFDBG(LOGINFO, "%s clear roc\n", __func__);
        /* Initialize RoC element pointer to NULL, indicate that RoC can be started */
        rwnx_hw->roc_elem = NULL;
    }

    /* Ensure that we won't process disconnect ind */
    spin_lock_bh(&rwnx_hw->cb_lock);

    rwnx_vif->up = false;
    if (netif_carrier_ok(dev)) {
        if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
            RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
            cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING,
                                  NULL, 0, true, GFP_ATOMIC);
            netif_tx_stop_all_queues(dev);
            netif_carrier_off(dev);
        } else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) {
            netif_carrier_off(dev);
        } else {
            netdev_warn(dev, "AP not stopped when disabling interface");
		}
#ifdef CONFIG_BR_SUPPORT
        /* if (OPMODE & (WIFI_STATION_STATE | WIFI_ADHOC_STATE)) */
        {
            /* void nat25_db_cleanup(_adapter *priv); */
            nat25_db_cleanup(rwnx_vif);
        }
#endif /* CONFIG_BR_SUPPORT */
	}


    rwnx_hw->vif_table[rwnx_vif->vif_index] = NULL;
    spin_unlock_bh(&rwnx_hw->cb_lock);

    rwnx_chanctx_unlink(rwnx_vif);

    if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR)
        rwnx_hw->monitor_vif = RWNX_INVALID_VIF;

    if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)
    {
    #ifdef CONFIG_COEX
        rwnx_send_coex_req(rwnx_hw, 0, 1);
    #endif
    }

    rwnx_hw->vif_started--;
    if (rwnx_hw->vif_started == 0) {
        /* This also lets both ipc sides remain in sync before resetting */
        #if 0
        rwnx_ipc_tx_drain(rwnx_hw);
        #else
        #if defined(AICWF_USB_SUPPORT)
        if (usbdev->bus_if->state != BUS_DOWN_ST && usbdev->state != USB_DOWN_ST) {
        #else
        if (sdiodev->bus_if->state != BUS_DOWN_ST) {
        #endif
            rwnx_send_reset(rwnx_hw);
            #if defined(AICWF_USB_SUPPORT)
            if (rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801 ||
                    ((rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
                      rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW) && testmode == 0)) {
            #endif
                // Set parameters to firmware
                rwnx_send_me_config_req(rwnx_hw);
                // Set channel parameters to firmware
                rwnx_send_me_chan_config_req(rwnx_hw);
            #if defined(AICWF_USB_SUPPORT)
            }
            #endif
        }
        #endif
        clear_bit(RWNX_DEV_STARTED, &rwnx_hw->drv_flags);
    }

    return 0;
}

#ifdef CONFIG_RFTEST
enum {
    SET_TX,
    SET_TXSTOP,
    SET_TXTONE,
    SET_RX,
    GET_RX_RESULT,
    SET_RXSTOP,
    SET_RX_METER,
    SET_POWER,
    SET_XTAL_CAP,
    SET_XTAL_CAP_FINE,
    GET_EFUSE_BLOCK,
    SET_FREQ_CAL,
    SET_FREQ_CAL_FINE,
    GET_FREQ_CAL,
    SET_MAC_ADDR,
    GET_MAC_ADDR,
    SET_BT_MAC_ADDR,
    GET_BT_MAC_ADDR,
    SET_VENDOR_INFO,
    GET_VENDOR_INFO,
    RDWR_PWRMM,
    RDWR_PWRIDX,
    RDWR_PWRLVL = RDWR_PWRIDX,
    RDWR_PWROFST,
    RDWR_DRVIBIT,
    RDWR_EFUSE_PWROFST,
    RDWR_EFUSE_DRVIBIT,
    SET_PAPR,
    SET_CAL_XTAL,
    GET_CAL_XTAL_RES,
    SET_COB_CAL,
    GET_COB_CAL_RES,

    #ifdef CONFIG_USB_BT
    BT_CMD_BASE = 0x100,
    BT_RESET,
    BT_TXDH,
    BT_RXDH,
    BT_STOP,
    GET_BT_RX_RESULT,
    #endif
};

typedef struct
{
    u8_l chan;
    u8_l bw;
    u8_l mode;
    u8_l rate;
    u16_l length;
}cmd_rf_settx_t;

typedef struct
{
    u8_l val;
}cmd_rf_setfreq_t;

typedef struct
{
    u8_l chan;
    u8_l bw;
}cmd_rf_rx_t;

typedef struct
{
    u8_l block;
}cmd_rf_getefuse_t;
typedef struct
{
    u8_l dutid;
    u8_l chip_num;
}cmd_rf_setcobcal_t;
typedef struct
{
    u16_l dut_rcv_golden_num;
    u8_l golden_rcv_dut_num;
    s8_l rssi_static;
    s8_l snr_static;
    s8_l dut_rssi_static;
    u16_l reserved;
}cob_result_ptr_t;
#endif

#define CMD_MAXARGS 30

#if 0
#define isblank(c)      ((c) == ' ' || (c) == '\t')
#define isascii(c)      (((unsigned char)(c)) <= 0x7F)

static int isdigit(unsigned char c)
{
    return ((c >= '0') && (c <='9'));
}

static int isxdigit(unsigned char c)
{
    if ((c >= '0') && (c <='9'))
        return 1;
    if ((c >= 'a') && (c <='f'))
        return 1;
    if ((c >= 'A') && (c <='F'))
        return 1;
    return 0;
}

static int islower(unsigned char c)
{
    return ((c >= 'a') && (c <='z'));
}

static unsigned char toupper(unsigned char c)
{
    if (islower(c))
        c -= 'a'-'A';
    return c;
}
#endif


static int parse_line (char *line, char *argv[])
{
    int nargs = 0;

    while (nargs < CMD_MAXARGS) {
        /* skip any white space */
        while ((*line == ' ') || (*line == '\t')) {
            ++line;
        }

        if (*line == '\0') {    /* end of line, no more args    */
            argv[nargs] = 0;
            return (nargs);
        }

        /* Argument include space should be bracketed by quotation mark */
        if (*line == '\"') {
            /* Skip quotation mark */
            line++;

            /* Begin of argument string */
            argv[nargs++] = line;

            /* Until end of argument */
            while(*line && (*line != '\"')) {
                ++line;
            }
        } else {
            argv[nargs++] = line;    /* begin of argument string    */

            /* find end of string */
            while(*line && (*line != ' ') && (*line != '\t')) {
                ++line;
            }
        }

        if (*line == '\0') {    /* end of line, no more args    */
            argv[nargs] = 0;
            return (nargs);
        }

        *line++ = '\0';         /* terminate current arg     */
    }

    printk("** Too many args (max. %d) **\n", CMD_MAXARGS);

    return (nargs);
}

unsigned int command_strtoul(const char *cp, char **endp, unsigned int base)
{
    unsigned int result = 0, value, is_neg=0;

    if (*cp == '0') {
        cp++;
        if ((*cp == 'x') && isxdigit(cp[1])) {
            base = 16;
            cp++;
        }
        if (!base) {
            base = 8;
        }
    }
    if (!base) {
        base = 10;
    }
    if (*cp == '-') {
        is_neg = 1;
        cp++;
    }
    while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp - '0' : (islower(*cp) ? toupper(*cp) : *cp) - 'A' + 10) < base) {
        result = result * base + value;
        cp++;
    }
    if (is_neg)
        result = (unsigned int)((int)result * (-1));

    if (endp)
        *endp = (char *)cp;
    return result;
}


int handle_private_cmd(struct net_device *net, char *command, u32 cmd_len)
{
    int bytes_written = 0;
    char* para = NULL;
    char* cmd = NULL;
    char *argv[CMD_MAXARGS + 1];
    int argc;
    #ifdef CONFIG_RFTEST
    struct dbg_rftest_cmd_cfm cfm = {{0,}};
    u8_l mac_addr[6];
    cmd_rf_settx_t settx_param;
    cmd_rf_rx_t setrx_param;
    int freq;
    cmd_rf_getefuse_t getefuse_param;
    cmd_rf_setfreq_t cmd_setfreq;
    cmd_rf_setcobcal_t setcob_cal;
    cob_result_ptr_t *cob_result_ptr;
    u8_l ana_pwr;
    u8_l dig_pwr;
    u8_l pwr;
    u8_l papr;
    u8_l xtal_cap;
    u8_l xtal_cap_fine;
    u8_l vendor_info;
    #ifdef CONFIG_USB_BT
    int bt_index;
    u8_l dh_cmd_reset[4];
    u8_l dh_cmd_txdh[18];
    u8_l dh_cmd_rxdh[17];
    u8_l dh_cmd_stop[5];
    #endif
    #endif
	u8_l buf[2];
	s8_l freq_ = 0;
	u8_l func = 0;
	u8_l state = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if ((argc = parse_line(command, argv)) == 0) {
        return -1;
    }

    do {
        #ifdef AICWF_SDIO_SUPPORT
        struct rwnx_hw *p_rwnx_hw = g_rwnx_plat->sdiodev->rwnx_hw;
        #endif
        #ifdef AICWF_USB_SUPPORT
	#ifdef CONFIG_RFTEST
        struct rwnx_hw *p_rwnx_hw = g_rwnx_plat->usbdev->rwnx_hw;
	#endif
        #endif
        #ifdef CONFIG_RFTEST
        if (strcasecmp(argv[0], "GET_RX_RESULT") ==0) {
            AICWFDBG(LOGINFO, "get_rx_result\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_RX_RESULT, 0, NULL, &cfm);
            memcpy(command, &cfm.rftest_result[0], 8);
            bytes_written = 8;
        } else if (strcasecmp(argv[0], "SET_TX") == 0) {
            AICWFDBG(LOGINFO, "set_tx\n");
            if (argc < 6) {
                printk("wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            settx_param.chan = command_strtoul(argv[1], NULL, 10);
            settx_param.bw = command_strtoul(argv[2], NULL, 10);
            settx_param.mode = command_strtoul(argv[3], NULL, 10);
            settx_param.rate = command_strtoul(argv[4], NULL, 10);
            settx_param.length = command_strtoul(argv[5], NULL, 10);
            AICWFDBG(LOGINFO, "txparam:%d,%d,%d,%d,%d\n", settx_param.chan, settx_param.bw,
                settx_param.mode, settx_param.rate, settx_param.length);
            rwnx_send_rftest_req(p_rwnx_hw, SET_TX, sizeof(cmd_rf_settx_t), (u8_l *)&settx_param, NULL);
        } else if (strcasecmp(argv[0], "SET_TXSTOP") == 0) {
            AICWFDBG(LOGINFO, "settx_stop\n");
            rwnx_send_rftest_req(p_rwnx_hw, SET_TXSTOP, 0, NULL, NULL);
        } else if (strcasecmp(argv[0], "SET_TXTONE") == 0) {
            AICWFDBG(LOGINFO, "set_tx_tone,argc:%d\n",argc);
            if ((argc == 2) || (argc == 3)) {
                AICWFDBG(LOGINFO, "argv 1:%s\n",argv[1]);
                //u8_l func = (u8_l)command_strtoul(argv[1], NULL, 16);
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
                //s8_l freq;
                if (argc == 3) {
                    AICWFDBG(LOGINFO, "argv 2:%s\n",argv[2]);
                    freq_ = (u8_l)command_strtoul(argv[2], NULL, 10);
                } else {
                    freq_ = 0;
                };
                //u8_l buf[2] = {func, (u8_l)freq};
                buf[0] = func;
				buf[1] = (u8_l)freq_;
                rwnx_send_rftest_req(p_rwnx_hw, SET_TXTONE, argc - 1, buf, NULL);
            } else {
                AICWFDBG(LOGINFO, "wrong args\n");
                bytes_written = -EINVAL;
                break;
            }
        } else if (strcasecmp(argv[0], "SET_RX") == 0) {
            AICWFDBG(LOGINFO, "set_rx\n");
            if (argc < 3) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            setrx_param.chan = command_strtoul(argv[1], NULL, 10);
            setrx_param.bw = command_strtoul(argv[2], NULL, 10);
            rwnx_send_rftest_req(p_rwnx_hw, SET_RX, sizeof(cmd_rf_rx_t), (u8_l *)&setrx_param, NULL);
        } else if (strcasecmp(argv[0], "SET_RXSTOP") == 0) {
            AICWFDBG(LOGINFO, "set_rxstop\n");
            rwnx_send_rftest_req(p_rwnx_hw, SET_RXSTOP, 0, NULL, NULL);
        } else if (strcasecmp(argv[0], "SET_RX_METER") == 0) {
            AICWFDBG(LOGINFO, "set_rx_meter\n");
            freq = (int)command_strtoul(argv[1], NULL, 10);
            rwnx_send_rftest_req(p_rwnx_hw, SET_RX_METER, sizeof(freq), (u8_l *)&freq, NULL);
        } else if (strcasecmp(argv[0], "SET_FREQ_CAL") == 0) {
            AICWFDBG(LOGINFO, "set_freq_cal\n");
            if (argc < 2) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            cmd_setfreq.val = command_strtoul(argv[1], NULL, 16);
            AICWFDBG(LOGINFO, "param:%x\r\n", cmd_setfreq.val);
            rwnx_send_rftest_req(p_rwnx_hw, SET_FREQ_CAL, sizeof(cmd_rf_setfreq_t), (u8_l *)&cmd_setfreq, &cfm);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "SET_FREQ_CAL_FINE") == 0) {
            AICWFDBG(LOGINFO, "set_freq_cal_fine\n");
            if (argc < 2) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            cmd_setfreq.val = command_strtoul(argv[1], NULL, 16);
            AICWFDBG(LOGINFO, "param:%x\r\n", cmd_setfreq.val);
            rwnx_send_rftest_req(p_rwnx_hw, SET_FREQ_CAL_FINE, sizeof(cmd_rf_setfreq_t), (u8_l *)&cmd_setfreq, &cfm);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "GET_EFUSE_BLOCK") == 0) {
            AICWFDBG(LOGINFO, "get_efuse_block\n");
            if (argc < 2) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            getefuse_param.block = command_strtoul(argv[1], NULL, 10);
            rwnx_send_rftest_req(p_rwnx_hw, GET_EFUSE_BLOCK, sizeof(cmd_rf_getefuse_t), (u8_l *)&getefuse_param, &cfm);
            AICWFDBG(LOGINFO, "get val=%x\r\n", cfm.rftest_result[0]);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "SET_POWER") == 0) {
            AICWFDBG(LOGINFO, "set_power\n");
            ana_pwr = command_strtoul(argv[1], NULL, 16);
            dig_pwr = command_strtoul(argv[2], NULL, 16);
            pwr = (ana_pwr << 4 | dig_pwr);
            if (ana_pwr > 0xf || dig_pwr > 0xf) {
                AICWFDBG(LOGERROR, "invalid param\r\n");
                bytes_written = -EINVAL;
                break;
            }
            AICWFDBG(LOGINFO, "pwr =%x\r\n", pwr);
            rwnx_send_rftest_req(p_rwnx_hw, SET_POWER, sizeof(pwr), (u8_l *)&pwr, NULL);
        } else if (strcasecmp(argv[0], "SET_PAPR") == 0) {
            printk("set papr\n");
            if (argc > 1) {
                papr = command_strtoul(argv[1], NULL, 10);
                printk("papr %d\r\n", func);
                rwnx_send_rftest_req(p_rwnx_hw, SET_PAPR, sizeof(papr), (u8_l *)&papr, NULL);
            } else {
                printk("wrong args\n");
                bytes_written = -EINVAL;
                break;
            }
        } else if (strcasecmp(argv[0], "SET_XTAL_CAP")==0) {
            AICWFDBG(LOGINFO, "set_xtal_cap\n");
            if (argc < 2) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            xtal_cap = command_strtoul(argv[1], NULL, 10);
            AICWFDBG(LOGINFO, "xtal_cap =%x\r\n", xtal_cap);
            rwnx_send_rftest_req(p_rwnx_hw, SET_XTAL_CAP, sizeof(xtal_cap), (u8_l *)&xtal_cap, &cfm);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "SET_XTAL_CAP_FINE")==0) {
            AICWFDBG(LOGINFO, "set_xtal_cap_fine\n");
            if (argc < 2) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            xtal_cap_fine = command_strtoul(argv[1], NULL, 10);
            AICWFDBG(LOGINFO, "xtal_cap_fine =%x\r\n", xtal_cap_fine);
            rwnx_send_rftest_req(p_rwnx_hw, SET_XTAL_CAP_FINE, sizeof(xtal_cap_fine), (u8_l *)&xtal_cap_fine, &cfm);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "SET_MAC_ADDR")==0) {
            AICWFDBG(LOGINFO, "set_mac_addr\n");
            if (argc < 7) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            mac_addr[5] = command_strtoul(argv[1], NULL, 16);
            mac_addr[4] = command_strtoul(argv[2], NULL, 16);
            mac_addr[3] = command_strtoul(argv[3], NULL, 16);
            mac_addr[2] = command_strtoul(argv[4], NULL, 16);
            mac_addr[1] = command_strtoul(argv[5], NULL, 16);
            mac_addr[0] = command_strtoul(argv[6], NULL, 16);
            AICWFDBG(LOGINFO, "set macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
            rwnx_send_rftest_req(p_rwnx_hw, SET_MAC_ADDR, sizeof(mac_addr), (u8_l *)&mac_addr, NULL);
        } else if (strcasecmp(argv[0], "GET_MAC_ADDR")==0) {
            u32_l addr0, addr1;
            AICWFDBG(LOGINFO, "get mac addr\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_MAC_ADDR, 0, NULL, &cfm);
            memcpy(command, &cfm.rftest_result[0], 8);
            bytes_written = 8;
            addr0 = cfm.rftest_result[0];
            if ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC) ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW)) {
                int rem_cnt = (cfm.rftest_result[1] >> 16) & 0x00FF;
                addr1 = cfm.rftest_result[1] & 0x0000FFFF;
                AICWFDBG(LOGINFO, "0x%x,0x%x (remain:%x)\n", addr0, addr1, rem_cnt);
            } else {
                addr1 = cfm.rftest_result[1];
                AICWFDBG(LOGINFO, "0x%x,0x%x\n", addr0, addr1);
            }
        } else if (strcasecmp(argv[0], "SET_BT_MAC_ADDR") == 0) {
            AICWFDBG(LOGINFO, "set_bt_mac_addr\n");
            if (argc < 7) {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            mac_addr[5] = command_strtoul(argv[1], NULL, 16);
            mac_addr[4] = command_strtoul(argv[2], NULL, 16);
            mac_addr[3] = command_strtoul(argv[3], NULL, 16);
            mac_addr[2] = command_strtoul(argv[4], NULL, 16);
            mac_addr[1] = command_strtoul(argv[5], NULL, 16);
            mac_addr[0] = command_strtoul(argv[6], NULL, 16);
            AICWFDBG(LOGINFO, "set bt macaddr:%x,%x,%x,%x,%x,%x\n", mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);
            rwnx_send_rftest_req(p_rwnx_hw, SET_BT_MAC_ADDR, sizeof(mac_addr), (u8_l *)&mac_addr, NULL);
        } else if (strcasecmp(argv[0], "GET_BT_MAC_ADDR")==0) {
            u32_l addr0, addr1;
            AICWFDBG(LOGINFO, "get bt mac addr\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_BT_MAC_ADDR, 0, NULL, &cfm);
            memcpy(command, &cfm.rftest_result[0], 8);
            bytes_written = 8;
            addr0 = cfm.rftest_result[0];
            if ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC) ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW)) {
                int rem_cnt = (cfm.rftest_result[1] >> 16) & 0x00FF;
                addr1 = cfm.rftest_result[1] & 0x0000FFFF;
                AICWFDBG(LOGINFO, "0x%x,0x%x (remain:%x)\n", addr0, addr1, rem_cnt);
            } else {
                addr1 = cfm.rftest_result[1];
                AICWFDBG(LOGINFO, "0x%x,0x%x\n", addr0, addr1);
            }
        } else if (strcasecmp(argv[0], "SET_VENDOR_INFO")==0) {
            vendor_info = command_strtoul(argv[1], NULL, 16);
            AICWFDBG(LOGINFO, "set vendor info:%x\n", vendor_info);
            rwnx_send_rftest_req(p_rwnx_hw, SET_VENDOR_INFO, 1, &vendor_info, &cfm);
            if ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC) ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW)) {
                memcpy(command, &cfm.rftest_result[0], 2);
                bytes_written = 2;
            } else {
                memcpy(command, &cfm.rftest_result[0], 1);
                bytes_written = 1;
            }
            AICWFDBG(LOGINFO, "0x%x\n", cfm.rftest_result[0]);
        } else if (strcasecmp(argv[0], "GET_VENDOR_INFO")==0) {
            AICWFDBG(LOGINFO, "get vendor info\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_VENDOR_INFO, 0, NULL, &cfm);
            if ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC) ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW)) {
                memcpy(command, &cfm.rftest_result[0], 2);
                bytes_written = 2;
            } else {
                memcpy(command, &cfm.rftest_result[0], 1);
                bytes_written = 1;
            }
            AICWFDBG(LOGINFO, "0x%x\n", cfm.rftest_result[0]);
        } else if (strcasecmp(argv[0], "GET_FREQ_CAL") == 0) {
            unsigned int val;
            AICWFDBG(LOGINFO, "get freq cal\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_FREQ_CAL, 0, NULL, &cfm);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
            val = cfm.rftest_result[0];
            if ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC) ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW)) {
                AICWFDBG(LOGINFO, "cap=0x%x (remain:%x), cap_fine=%x (remain:%x)\n",
                        val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) & 0xff);
            } else {
                AICWFDBG(LOGINFO, "cap=0x%x, cap_fine=0x%x\n", val & 0xff, (val >> 8) & 0xff);
            }
        } else if (strcasecmp(argv[0], "RDWR_PWRMM") == 0) {
            AICWFDBG(LOGINFO, "read/write txpwr manul mode\n");
            if (argc <= 1) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWRMM, 0, NULL, &cfm);
            } else { // write
                u8_l pwrmm = (u8_l)command_strtoul(argv[1], NULL, 16);
                pwrmm = (pwrmm) ? 1 : 0;
                AICWFDBG(LOGINFO, "set pwrmm = %x\r\n", pwrmm);
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWRMM, sizeof(pwrmm), (u8_l *)&pwrmm, &cfm);
            }
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "RDWR_PWRIDX") == 0) {
            u8_l func = 0;
            #ifdef AICWF_USB_SUPPORT
            if (g_rwnx_plat->usbdev->chipid != PRODUCT_ID_AIC8801) {
                    AICWFDBG(LOGERROR, "unsupported cmd\n");
                    bytes_written = -EINVAL;
                    break;
            }
            #endif
            AICWFDBG(LOGINFO, "read/write txpwr index\n");
            if (argc > 1) {
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
            }
            if (func == 0) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWRIDX, 0, NULL, &cfm);
            } else if (func <= 2) { // write 2.4g/5g pwr idx
                if (argc > 3) {
                    u8_l type = (u8_l)command_strtoul(argv[2], NULL, 16);
                    u8_l pwridx = (u8_l)command_strtoul(argv[3], NULL, 10);
                    u8_l buf[3] = {func, type, pwridx};
                    AICWFDBG(LOGINFO, "set pwridx:[%x][%x]=%x\r\n", func, type, pwridx);
                    rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWRIDX, sizeof(buf), buf, &cfm);
                } else {
                    AICWFDBG(LOGERROR, "wrong args\n");
                    bytes_written = -EINVAL;
                    break;
                }
            } else {
                AICWFDBG(LOGERROR, "wrong func: %x\n", func);
                bytes_written = -EINVAL;
                break;
            }
            memcpy(command, &cfm.rftest_result[0], 9);
            bytes_written = 9;
        } else if (strcasecmp(argv[0], "RDWR_PWRLVL") == 0) {
            u8_l func = 0;
            #ifdef AICWF_USB_SUPPORT
            if ((g_rwnx_plat->usbdev->chipid != PRODUCT_ID_AIC8800DC)
                && (g_rwnx_plat->usbdev->chipid != PRODUCT_ID_AIC8800DW)) {
                    AICWFDBG(LOGERROR, "unsupported cmd\n");
                    bytes_written = -EINVAL;
                    break;
            }
            #endif
            printk("read/write txpwr level\n");
            if (argc > 1) {
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
            }
            if (func == 0) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWRLVL, 0, NULL, &cfm);
            } else if (func <= 2) { // write 2.4g/5g pwr lvl
                if (argc > 4) {
                    u8_l grp = (u8_l)command_strtoul(argv[2], NULL, 16);
                    u8_l idx, size;
                    u8_l buf[14] = {func, grp,};
                    if (argc > 12) { // set all grp
                        printk("set pwrlvl %s:\n"
                               "  [%x] =", (func == 1) ? "2.4g" : "5g", grp);
                        if (grp == 1) { // TXPWR_LVL_GRP_11N_11AC
                            size = 10;
                        } else {
                            size = 12;
                        }
                        for (idx = 0; idx < size; idx++) {
                            s8_l pwrlvl = (s8_l)command_strtoul(argv[3 + idx], NULL, 10);
                            buf[2 + idx] = (u8_l)pwrlvl;
                            if (idx && !(idx & 0x3)) {
                                printk(" ");
                            }
                            printk(" %2d", pwrlvl);
                        }
                        printk("\n");
                        size += 2;
                    } else { // set grp[idx]
                        u8_l idx = (u8_l)command_strtoul(argv[3], NULL, 10);
                        s8_l pwrlvl = (s8_l)command_strtoul(argv[4], NULL, 10);
                        buf[2] = idx;
                        buf[3] = (u8_l)pwrlvl;
                        size = 4;
                        printk("set pwrlvl %s:\n"
                               "  [%x][%d] = %d\n", (func == 1) ? "2.4g" : "5g", grp, idx, pwrlvl);
                    }
                    rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWRLVL, size, buf, &cfm);
                } else {
                    printk("wrong args\n");
                }
            } else {
                printk("wrong func: %x\n", func);
            }
            memcpy(command, &cfm.rftest_result[0], 3 * 12);
            bytes_written = 3 * 12;
        } else if (strcasecmp(argv[0], "RDWR_PWROFST") == 0) {
            u8_l func = 0;
            AICWFDBG(LOGINFO, "read/write txpwr offset\n");
            if (argc > 1) {
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
            }
            if (func == 0) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWROFST, 0, NULL, &cfm);
            } else if (func <= 2) { // write 2.4g/5g pwr ofst
                if (argc > 3) {
                    u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
                    s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
                    u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};
                    AICWFDBG(LOGINFO, "set pwrofst:[%x][%x]=%d\r\n", func, chgrp, pwrofst);
                    rwnx_send_rftest_req(p_rwnx_hw, RDWR_PWROFST, sizeof(buf), buf, &cfm);
                } else {
                    AICWFDBG(LOGERROR, "wrong args\n");
                    bytes_written = -EINVAL;
                    break;
                }
            } else {
                AICWFDBG(LOGERROR, "wrong func: %x\n", func);
                bytes_written = -EINVAL;
                break;
            }
            memcpy(command, &cfm.rftest_result[0], 7);
            bytes_written = 7;
        } else if (strcasecmp(argv[0], "RDWR_DRVIBIT") == 0) {
            u8_l func = 0;
            AICWFDBG(LOGINFO, "read/write pa drv_ibit\n");
            if (argc > 1) {
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
            }
            if (func == 0) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_DRVIBIT, 0, NULL, &cfm);
            } else if (func == 1) { // write 2.4g pa drv_ibit
                if (argc > 2) {
                    u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
                    u8_l buf[2] = {func, ibit};
                    AICWFDBG(LOGINFO, "set drvibit:[%x]=%x\r\n", func, ibit);
                    rwnx_send_rftest_req(p_rwnx_hw, RDWR_DRVIBIT, sizeof(buf), buf, &cfm);
                } else {
                    AICWFDBG(LOGERROR, "wrong args\n");
                    bytes_written = -EINVAL;
                    break;
                }
            } else {
                AICWFDBG(LOGERROR, "wrong func: %x\n", func);
                bytes_written = -EINVAL;
                break;
            }
            memcpy(command, &cfm.rftest_result[0], 16);
            bytes_written = 16;
        } else if (strcasecmp(argv[0], "RDWR_EFUSE_PWROFST") == 0) {
            u8_l func = 0;
            AICWFDBG(LOGINFO, "read/write txpwr offset into efuse\n");
            if (argc > 1) {
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
            }
            if (func == 0) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_EFUSE_PWROFST, 0, NULL, &cfm);
            } else if (func <= 2) { // write 2.4g/5g pwr ofst
                if (argc > 3) {
                    u8_l chgrp = (u8_l)command_strtoul(argv[2], NULL, 16);
                    s8_l pwrofst = (u8_l)command_strtoul(argv[3], NULL, 10);
                    u8_l buf[3] = {func, chgrp, (u8_l)pwrofst};
                    AICWFDBG(LOGINFO, "set efuse pwrofst:[%x][%x]=%d\r\n", func, chgrp, pwrofst);
                    rwnx_send_rftest_req(p_rwnx_hw, RDWR_EFUSE_PWROFST, sizeof(buf), buf, &cfm);
                } else {
                    AICWFDBG(LOGERROR, "wrong args\n");
                    bytes_written = -EINVAL;
                    break;
                }
            } else {
                AICWFDBG(LOGERROR, "wrong func: %x\n", func);
                bytes_written = -EINVAL;
                break;
            }
            if ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC) ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW)) { // 6 = 3 (2.4g) * 2
                memcpy(command, &cfm.rftest_result[0], 6);
                bytes_written = 6;
            } else { // 7 = 3(2.4g) + 4(5g)
                memcpy(command, &cfm.rftest_result[0], 7);
                bytes_written = 7;
            }
        } else if (strcasecmp(argv[0], "RDWR_EFUSE_DRVIBIT") == 0) {
            u8_l func = 0;
            AICWFDBG(LOGINFO, "read/write pa drv_ibit into efuse\n");
            if (argc > 1) {
                func = (u8_l)command_strtoul(argv[1], NULL, 16);
            }
            if (func == 0) { // read cur
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_EFUSE_DRVIBIT, 0, NULL, &cfm);
            } else if (func == 1) { // write 2.4g pa drv_ibit
                if (argc > 2) {
                u8_l ibit = (u8_l)command_strtoul(argv[2], NULL, 16);
                u8_l buf[2] = {func, ibit};
                AICWFDBG(LOGINFO, "set efuse drvibit:[%x]=%x\r\n", func, ibit);
                rwnx_send_rftest_req(p_rwnx_hw, RDWR_EFUSE_DRVIBIT, sizeof(buf), buf, &cfm);
                } else {
                    AICWFDBG(LOGERROR, "wrong args\n");
                    bytes_written = -EINVAL;
                    break;
                }
            } else {
                AICWFDBG(LOGERROR, "wrong func: %x\n", func);
                bytes_written = -EINVAL;
                break;
            }
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
        } else if (strcasecmp(argv[0], "SET_CAL_XTAL") == 0) {
            AICWFDBG(LOGINFO, "set_cal_xtal\n");
            rwnx_send_rftest_req(p_rwnx_hw, SET_CAL_XTAL, 0, NULL, NULL);
        } else if (strcasecmp(argv[0], "GET_CAL_XTAL_RES") == 0) {
            AICWFDBG(LOGINFO, "get_cal_xtal_res\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_CAL_XTAL_RES, 0, NULL, &cfm);
            memcpy(command, &cfm.rftest_result[0], 4);
            bytes_written = 4;
            AICWFDBG(LOGINFO, "cap=0x%x, cap_fine=0x%x\n", cfm.rftest_result[0] & 0x0000ffff, (cfm.rftest_result[0] >> 16) & 0x0000ffff);
	} else if (strcasecmp(argv[0], "SET_COB_CAL") == 0) {
	    AICWFDBG(LOGINFO, "set_cob_cal\n");
	    if (argc < 3) {
	    	AICWFDBG(LOGERROR, "wrong param\n");
	    	bytes_written = -EINVAL;
		break;
	    }
	    setcob_cal.dutid = command_strtoul(argv[1], NULL, 10);
	    setcob_cal.chip_num = command_strtoul(argv[2], NULL, 10);
	    rwnx_send_rftest_req(p_rwnx_hw, SET_COB_CAL, sizeof(cmd_rf_setcobcal_t), (u8_l *)&setcob_cal, NULL);
	} else if (strcasecmp(argv[0], "GET_COB_CAL_RES")==0) {
            AICWFDBG(LOGINFO, "get_cob_cal_res\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_COB_CAL_RES, 0, NULL, &cfm);
            state = (cfm.rftest_result[0] >> 16) & 0x000000ff;
            if (!state){
            	AICWFDBG(LOGINFO, "cap= 0x%x, cap_fine= 0x%x, freq_ofst= %d Hz\n",
            	cfm.rftest_result[0] & 0x000000ff, (cfm.rftest_result[0] >> 8) & 0x000000ff, cfm.rftest_result[1]);
                cob_result_ptr = (cob_result_ptr_t *) & (cfm.rftest_result[2]);
                AICWFDBG(LOGINFO, "golden_rcv_dut= %d , tx_rssi= %d dBm, snr = %d dB\ndut_rcv_godlden= %d , rx_rssi= %d dBm",
                cob_result_ptr->golden_rcv_dut_num, cob_result_ptr->rssi_static, cob_result_ptr->snr_static,
		cob_result_ptr->dut_rcv_golden_num, cob_result_ptr->dut_rssi_static);
                memcpy(command, &cfm.rftest_result, 16);
                bytes_written = 16;
            } else {
                AICWFDBG(LOGERROR, "cob not idle\n");
                bytes_written = -EINVAL;
                break;
            }
        } else if (strcasecmp(argv[0], "DO_COB_TEST") == 0) {
	    AICWFDBG(LOGINFO, "do_cob_test\n");
		setcob_cal.dutid = 1;
                setcob_cal.chip_num = 1;
		rwnx_send_rftest_req(p_rwnx_hw, SET_COB_CAL, sizeof(cmd_rf_setcobcal_t), (u8_l *)&setcob_cal, NULL);
		msleep(2000);
            rwnx_send_rftest_req(p_rwnx_hw, GET_COB_CAL_RES, 0, NULL, &cfm);
            state = (cfm.rftest_result[0] >> 16) & 0x000000ff;
            if (!state){
                AICWFDBG(LOGINFO, "cap= 0x%x, cap_fine= 0x%x, freq_ofst= %d Hz\n",
                cfm.rftest_result[0] & 0x000000ff, (cfm.rftest_result[0] >> 8) & 0x000000ff, cfm.rftest_result[1]);
                cob_result_ptr = (cob_result_ptr_t *) & (cfm.rftest_result[2]);
                AICWFDBG(LOGINFO, "golden_rcv_dut= %d , tx_rssi= %d dBm, snr = %d dB\ndut_rcv_godlden= %d , rx_rssi= %d dBm",
                cob_result_ptr->golden_rcv_dut_num, cob_result_ptr->rssi_static, cob_result_ptr->snr_static,
                cob_result_ptr->dut_rcv_golden_num, cob_result_ptr->dut_rssi_static);
                memcpy(command, &cfm.rftest_result, 16);
                bytes_written = 16;
            } else {
                AICWFDBG(LOGERROR, "cob not idle\n");
                bytes_written = -EINVAL;
                break;
            }

	}
        #ifdef CONFIG_USB_BT
        else if (strcasecmp(argv[0], "BT_RESET") == 0) {
            if (argc == 5) {
                AICWFDBG(LOGINFO, "btrf reset\n");
                for (bt_index = 0; bt_index < 4; bt_index++) {
                    dh_cmd_reset[bt_index] = command_strtoul(argv[bt_index+1], NULL, 16);
                    AICWFDBG(LOGINFO, "0x%x ",dh_cmd_reset[bt_index]);
                }
                AICWFDBG(LOGINFO, "\n");
            } else {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            rwnx_send_rftest_req(p_rwnx_hw, BT_RESET, sizeof(dh_cmd_reset), (u8_l *)&dh_cmd_reset, NULL);
        } else if (strcasecmp(argv[0], "BT_TXDH") == 0) {
            if (argc == 19) {
                AICWFDBG(LOGINFO, "btrf txdh\n");
                for (bt_index = 0; bt_index < 18; bt_index++) {
                    dh_cmd_txdh[bt_index] = command_strtoul(argv[bt_index+1], NULL, 16);
                    AICWFDBG(LOGINFO, "0x%x ", dh_cmd_txdh[bt_index]);
                }
                AICWFDBG(LOGINFO, "\n");
            } else {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            rwnx_send_rftest_req(p_rwnx_hw, BT_TXDH, sizeof(dh_cmd_txdh), (u8_l *)&dh_cmd_txdh, NULL);
        } else if (strcasecmp(argv[0], "BT_RXDH") == 0) {
            if (argc == 18) {
                AICWFDBG(LOGINFO, "btrf rxdh\n");
                for (bt_index = 0; bt_index < 17; bt_index++) {
                    dh_cmd_rxdh[bt_index] = command_strtoul(argv[bt_index+1], NULL, 16);
                    AICWFDBG(LOGINFO, "0x%x ", dh_cmd_rxdh[bt_index]);
                }
                AICWFDBG(LOGINFO, "\n");
            } else {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            rwnx_send_rftest_req(p_rwnx_hw, BT_RXDH, sizeof(dh_cmd_rxdh), (u8_l *)&dh_cmd_rxdh, NULL);
        } else if (strcasecmp(argv[0], "BT_STOP") == 0) {
            if (argc == 6) {
                AICWFDBG(LOGINFO, "btrf stop\n");
                for (bt_index = 0; bt_index < 5; bt_index++) {
                    dh_cmd_stop[bt_index] = command_strtoul(argv[bt_index+1], NULL, 16);
                    AICWFDBG(LOGINFO, "0x%x ", dh_cmd_stop[bt_index]);
                }
                AICWFDBG(LOGINFO, "\n");
            } else {
                AICWFDBG(LOGERROR, "wrong param\n");
                bytes_written = -EINVAL;
                break;
            }
            rwnx_send_rftest_req(p_rwnx_hw, BT_STOP, sizeof(dh_cmd_stop), (u8_l *)&dh_cmd_stop, NULL);
        } else if (strcasecmp(argv[0], "GET_BT_RX_RESULT") ==0) {
            AICWFDBG(LOGINFO, "get_bt_rx_result\n");
            rwnx_send_rftest_req(p_rwnx_hw, GET_BT_RX_RESULT, 0, NULL, &cfm);
            memcpy(command, &cfm.rftest_result[0], 12);
            bytes_written = 12;
        }
        #endif
        else {
            AICWFDBG(LOGERROR, "wrong cmd:%s in %s\n", cmd, __func__);
            bytes_written = -EINVAL;
            break;
        }
        #endif
    } while(0);
    kfree(cmd);
    kfree(para);
    return bytes_written;
}

//Android private command

#define RWNX_COUNTRY_CODE_LEN 2
#define CMD_SET_COUNTRY         "COUNTRY"
#define CMD_SET_VENDOR_EX_IE    "SET_VENDOR_EX_IE"
#define CMD_SET_AP_WPS_P2P_IE   "SET_AP_WPS_P2P_IE"
#define CMD_SET_TESTMODE        "SET_TESTMODE"


struct ieee80211_regdomain *getRegdomainFromRwnxDB(struct wiphy *wiphy, char *alpha2);
struct ieee80211_regdomain *getRegdomainFromRwnxDBIndex(struct wiphy *wiphy, int index);
extern int reg_regdb_size;

#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
extern u8_l vendor_extension_data[256];
extern u8_l vendor_extension_len;

void set_vendor_extension_ie(char *command){

	char databyte[3]={0x00, 0x00, 0x00};
	int skip = strlen(CMD_SET_VENDOR_EX_IE) + 1;
	int command_index = skip;
	int data_index = 0;

	memset(vendor_extension_data, 0, 256);
	vendor_extension_len = 0;
	memcpy(databyte, command + command_index, 2);
	vendor_extension_len = command_strtoul(databyte, NULL, 16);
	AICWFDBG(LOGINFO, "%s len:%d \r\n", __func__, vendor_extension_len);

	//parser command and save data in vendor_extension_data
	for(data_index = 0;data_index < vendor_extension_len; data_index++){
		command_index = command_index + 3;
		memcpy(databyte, command + command_index, 2);
		vendor_extension_data[data_index] = command_strtoul(databyte, NULL, 16);
	}

}
#endif//CONFIG_SET_VENDOR_EXTENSION_IE

int android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
#define PRIVATE_COMMAND_MAX_LEN 8192
#define PRIVATE_COMMAND_DEF_LEN 4096

	struct rwnx_vif *vif = netdev_priv(net);
    int ret = 0;
    char *command = NULL;
    int bytes_written = 0;
    android_wifi_priv_cmd priv_cmd;
    int buf_size = 0;
	int skip = 0;
	char *country = NULL;
	struct ieee80211_regdomain *regdomain;
	//int index = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    ///todo: add our lock
    //net_os_wake_lock(net);


/*    if (!capable(CAP_NET_ADMIN)) {
        ret = -EPERM;
        goto exit;
    }*/
    if (!ifr->ifr_data) {
        ret = -EINVAL;
        goto exit;
    }

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
    if (in_compat_syscall())
#else
    if (is_compat_task())
#endif
    {
        compat_android_wifi_priv_cmd compat_priv_cmd;
        if (copy_from_user(&compat_priv_cmd, ifr->ifr_data, sizeof(compat_android_wifi_priv_cmd))) {
	    ret = -EFAULT;
            goto exit;
        }
        priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
        priv_cmd.used_len = compat_priv_cmd.used_len;
        priv_cmd.total_len = compat_priv_cmd.total_len;
    } else
#endif /* CONFIG_COMPAT */
    {
        if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(android_wifi_priv_cmd))) {
	    ret = -EFAULT;
            goto exit;
        }
    }
    if ((priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN) || (priv_cmd.total_len < 0)) {
        AICWFDBG(LOGERROR, "%s: buf length invalid:%d\n", __FUNCTION__, priv_cmd.total_len);
        ret = -EINVAL;
        goto exit;
    }

    buf_size = max(priv_cmd.total_len, PRIVATE_COMMAND_DEF_LEN);
    command = kmalloc((buf_size + 1), GFP_KERNEL);

    if (!command)
    {
        AICWFDBG(LOGERROR, "%s: failed to allocate memory\n", __FUNCTION__);
        ret = -ENOMEM;
        goto exit;
    }
    if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
        ret = -EFAULT;
        goto exit;
    }
    command[priv_cmd.total_len] = '\0';

    /* outputs */
    AICWFDBG(LOGINFO, "%s: Android private cmd \"%s\" on %s\n", __FUNCTION__, command, ifr->ifr_name);
    AICWFDBG(LOGINFO, "cmd = %d\n", cmd);
    AICWFDBG(LOGINFO, "buf_size=%d\n", buf_size);


#if 1//Handle Android command
	if(!strncasecmp(command, CMD_SET_COUNTRY, strlen(CMD_SET_COUNTRY))) {
		skip = strlen(CMD_SET_COUNTRY) + 1;
		country = command + skip;
		if (!country || strlen(country) < RWNX_COUNTRY_CODE_LEN) {
			AICWFDBG(LOGERROR, "%s: invalid country code\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
#if 0
		for(index = 0; index < reg_regdb_size; index++){
			regdomain = getRegdomainFromRwnxDBIndex(vif->rwnx_hw->wiphy, index);
			if((ret = regulatory_set_wiphy_regd(vif->rwnx_hw->wiphy, regdomain))){
				AICWFDBG(LOGERROR, "regulatory_set_wiphy_regd fail \r\n");
			}else{
				AICWFDBG(LOGINFO, "regulatory_set_wiphy_regd ok \r\n");
			}
		}
#endif
		AICWFDBG(LOGINFO, "%s country code:%c%c\n", __func__, toupper(country[0]), toupper(country[1]));
		regdomain = getRegdomainFromRwnxDB(vif->rwnx_hw->wiphy, country);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
		if((ret = regulatory_set_wiphy_regd(vif->rwnx_hw->wiphy, regdomain))){
			AICWFDBG(LOGERROR, "regulatory_set_wiphy_regd fail \r\n");
		}
#else
		wiphy_apply_custom_regulatory(vif->rwnx_hw->wiphy, regdomain);
#endif
	}
#ifdef CONFIG_SET_VENDOR_EXTENSION_IE
	else if(!strncasecmp(command, CMD_SET_VENDOR_EX_IE, strlen(CMD_SET_VENDOR_EX_IE))){
		set_vendor_extension_ie(command);
	}
#endif//CONFIG_SET_VENDOR_EXTENSION_IE
	else if(!strncasecmp(command, CMD_SET_AP_WPS_P2P_IE, strlen(CMD_SET_AP_WPS_P2P_IE))){
		ret = 0;
		goto exit;
	}else if(!strncasecmp(command, CMD_SET_TESTMODE, strlen(CMD_SET_TESTMODE))){
    	if(g_rwnx_plat && g_rwnx_plat->usbdev->rwnx_hw){
            if (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW ||
                (g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC)){
                set_testmode(!testmode);
	            rwnx_send_reboot(g_rwnx_plat->usbdev->rwnx_hw);
            }
        }
        ret = 0;
		goto exit;
    }
#endif//Handle Android command


    bytes_written = handle_private_cmd(net, command, priv_cmd.total_len);
    if (bytes_written >= 0) {
        if ((bytes_written == 0) && (priv_cmd.total_len > 0)) {
            command[0] = '\0';
        }
        if (bytes_written >= priv_cmd.total_len) {
            AICWFDBG(LOGINFO, "%s: err. bytes_written:%d >= buf_size:%d \n",
                __FUNCTION__, bytes_written, buf_size);
            goto exit;
        }
        bytes_written++;
        priv_cmd.used_len = bytes_written;
        if (copy_to_user(priv_cmd.buf, command, bytes_written)) {
            AICWFDBG(LOGERROR, "%s: failed to copy data to user buffer\n", __FUNCTION__);
            ret = -EFAULT;
        }
    }
    else {
        /* Propagate the error */
        ret = bytes_written;
    }

exit:
    ///todo: add our unlock
    //net_os_wake_unlock(net);
    kfree(command);
    return ret;
}

#define IOCTL_HOSTAPD   (SIOCIWFIRSTPRIV+28)
#define IOCTL_WPAS      (SIOCIWFIRSTPRIV+30)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
static int rwnx_do_ioctl(struct net_device *net, struct ifreq *req, void __user *data, int cmd)
#else
static int rwnx_do_ioctl(struct net_device *net, struct ifreq *req, int cmd)
#endif
{
    int ret = 0;
    ///TODO: add ioctl command handler later
    switch(cmd)
    {
        case IOCTL_HOSTAPD:
            AICWFDBG(LOGINFO, "IOCTL_HOSTAPD\n");
            break;
        case IOCTL_WPAS:
            AICWFDBG(LOGINFO, "IOCTL_WPAS\n");
            break;
        case SIOCDEVPRIVATE:
            AICWFDBG(LOGINFO, "IOCTL SIOCDEVPRIVATE\n");
            break;
        case (SIOCDEVPRIVATE+1):
            AICWFDBG(LOGINFO, "IOCTL PRIVATE\n");
            ret = android_priv_cmd(net, req, cmd);
	    break;
        default:
            ret = -EOPNOTSUPP;
    }
    return ret;
}

/**
 * struct net_device_stats* (*ndo_get_stats)(struct net_device *dev);
 *	Called when a user wants to get the network device usage
 *	statistics. Drivers must do one of the following:
 *	1. Define @ndo_get_stats64 to fill in a zero-initialised
 *	   rtnl_link_stats64 structure passed by the caller.
 *	2. Define @ndo_get_stats to update a net_device_stats structure
 *	   (which should normally be dev->stats) and return a pointer to
 *	   it. The structure may be changed asynchronously only if each
 *	   field is written atomically.
 *	3. Update dev->stats asynchronously and atomically, and define
 *	   neither operation.
 */
static struct net_device_stats *rwnx_get_stats(struct net_device *dev)
{
    struct rwnx_vif *vif = netdev_priv(dev);

    return &vif->net_stats;
}

/**
 * u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb,
 *                         struct net_device *sb_dev);
 *	Called to decide which queue to when device supports multiple
 *	transmit queues.
 */
u16 rwnx_select_queue(struct net_device *dev, struct sk_buff *skb,
                      struct net_device *sb_dev)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    return rwnx_select_txq(rwnx_vif, skb);
}

/**
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 *	This function  is called when the Media Access Control address
 *	needs to be changed. If this interface is not defined, the
 *	mac address can not be changed.
 */
static int rwnx_set_mac_address(struct net_device *dev, void *addr)
{
    struct sockaddr *sa = addr;
    int ret;
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	printk("%s enter \r\n", __func__);

    ret = eth_mac_addr(dev, sa);
    memcpy(rwnx_vif->wdev.address, dev->dev_addr, 6);

    return ret;
}

static const struct net_device_ops rwnx_netdev_ops = {
    .ndo_open               = rwnx_open,
    .ndo_stop               = rwnx_close,
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    .ndo_siocdevprivate     = rwnx_do_ioctl,
    #else
    .ndo_do_ioctl           = rwnx_do_ioctl,
    #endif
    .ndo_start_xmit         = rwnx_start_xmit,
    .ndo_get_stats          = rwnx_get_stats,
#ifndef CONFIG_ONE_TXQ
    .ndo_select_queue       = rwnx_select_queue,
#endif
#ifdef CONFIG_SUPPORT_REALTIME_CHANGE_MAC
    .ndo_set_mac_address    = rwnx_set_mac_address
#endif
//    .ndo_set_features       = rwnx_set_features,
//    .ndo_set_rx_mode        = rwnx_set_multicast_list,
};

static const struct net_device_ops rwnx_netdev_monitor_ops = {
    .ndo_open               = rwnx_open,
    .ndo_stop               = rwnx_close,
    #ifdef CONFIG_RWNX_MON_XMIT
    .ndo_start_xmit         = rwnx_start_monitor_if_xmit,
    .ndo_select_queue       = rwnx_select_queue,
    #endif
    .ndo_get_stats          = rwnx_get_stats,
    .ndo_set_mac_address    = rwnx_set_mac_address,
};

static void rwnx_netdev_setup(struct net_device *dev)
{
    ether_setup(dev);
    dev->priv_flags &= ~IFF_TX_SKB_SHARING;
    dev->netdev_ops = &rwnx_netdev_ops;
#if LINUX_VERSION_CODE <  KERNEL_VERSION(4, 12, 0)
    dev->destructor = free_netdev;
#else
    dev->needs_free_netdev = true;
#endif
    dev->watchdog_timeo = RWNX_TX_LIFETIME_MS;

    dev->needed_headroom = sizeof(struct rwnx_txhdr) + RWNX_SWTXHDR_ALIGN_SZ - 14;
#ifdef CONFIG_RWNX_AMSDUS_TX
    dev->needed_headroom = max(dev->needed_headroom,
                               (unsigned short)(sizeof(struct rwnx_amsdu_txhdr)
                                                + sizeof(struct ethhdr) + 4
                                                + sizeof(rfc1042_header) + 2));
#endif /* CONFIG_RWNX_AMSDUS_TX */

    dev->hw_features = 0;
}


/*********************************************************************
 * Cfg80211 callbacks (and helper)
 *********************************************************************/
static struct rwnx_vif *rwnx_interface_add(struct rwnx_hw *rwnx_hw,
                                               const char *name,
                                               unsigned char name_assign_type,
                                               enum nl80211_iftype type,
                                               struct vif_params *params)
{
    struct net_device *ndev;
    struct rwnx_vif *vif;
    int min_idx, max_idx;
    int vif_idx = -1;
    int i;
    int nx_nb_ndev_txq = NX_NB_NDEV_TXQ;

    if((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8801) ||
		((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW) && chip_id < 3)){
		    nx_nb_ndev_txq = NX_NB_NDEV_TXQ_FOR_OLD_IC;
    }

	AICWFDBG(LOGINFO, "rwnx_interface_add: %s, %d, %d\r\n", name, type, NL80211_IFTYPE_P2P_DEVICE);
    // Look for an available VIF
    if (type == NL80211_IFTYPE_AP_VLAN) {
        min_idx = NX_VIRT_DEV_MAX;
        max_idx = NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX;
    } else {
        min_idx = 0;
        max_idx = NX_VIRT_DEV_MAX;
    }

    for (i = min_idx; i < max_idx; i++) {
        if ((rwnx_hw->avail_idx_map) & BIT(i)) {
            vif_idx = i;
            break;
        }
    }
    if (vif_idx < 0)
        return NULL;

    #ifndef CONFIG_RWNX_MON_DATA
    list_for_each_entry(vif, &rwnx_hw->vifs, list) {
        // Check if monitor interface already exists or type is monitor
        if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR) ||
           (type == NL80211_IFTYPE_MONITOR)) {
            wiphy_err(rwnx_hw->wiphy,
                    "Monitor+Data interface support (MON_DATA) disabled\n");
            return NULL;
        }
    }
    #endif

#ifndef CONFIG_ONE_TXQ
    ndev = alloc_netdev_mqs(sizeof(*vif), name, name_assign_type,
                                rwnx_netdev_setup, nx_nb_ndev_txq, 1);
#else
    ndev = alloc_netdev_mqs(sizeof(*vif), name, name_assign_type,
                                    rwnx_netdev_setup, 1, 1);
#endif

    if (!ndev)
        return NULL;

    vif = netdev_priv(ndev);
    vif->key_has_add = 0;
    ndev->ieee80211_ptr = &vif->wdev;
    vif->wdev.wiphy = rwnx_hw->wiphy;
    vif->rwnx_hw = rwnx_hw;
    vif->ndev = ndev;
    vif->drv_vif_index = vif_idx;
    SET_NETDEV_DEV(ndev, wiphy_dev(vif->wdev.wiphy));
    vif->wdev.netdev = ndev;
    vif->wdev.iftype = type;
    vif->up = false;
    vif->ch_index = RWNX_CH_NOT_SET;
    memset(&vif->net_stats, 0, sizeof(vif->net_stats));
    vif->is_p2p_vif = 0;
#ifdef CONFIG_BR_SUPPORT
    spin_lock_init(&vif->br_ext_lock);
#endif /* CONFIG_BR_SUPPORT */

    switch (type) {
    case NL80211_IFTYPE_STATION:
        vif->sta.ap = NULL;
        vif->sta.tdls_sta = NULL;
        vif->sta.external_auth = false;
        break;
    case NL80211_IFTYPE_P2P_CLIENT:
        vif->sta.ap = NULL;
        vif->sta.tdls_sta = NULL;
        vif->sta.external_auth = false;
        vif->is_p2p_vif = 1;
        break;
    case NL80211_IFTYPE_MESH_POINT:
        INIT_LIST_HEAD(&vif->ap.mpath_list);
        INIT_LIST_HEAD(&vif->ap.proxy_list);
        vif->ap.create_path = false;
        vif->ap.generation = 0;
        vif->ap.mesh_pm = NL80211_MESH_POWER_ACTIVE;
        vif->ap.next_mesh_pm = NL80211_MESH_POWER_ACTIVE;
        // no break
    case NL80211_IFTYPE_AP:
        INIT_LIST_HEAD(&vif->ap.sta_list);
        memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
        break;
    case NL80211_IFTYPE_P2P_GO:
        INIT_LIST_HEAD(&vif->ap.sta_list);
        memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
        vif->is_p2p_vif = 1;
        break;
    case NL80211_IFTYPE_AP_VLAN:
    {
        struct rwnx_vif *master_vif;
        bool found = false;
        list_for_each_entry(master_vif, &rwnx_hw->vifs, list) {
            if ((RWNX_VIF_TYPE(master_vif) == NL80211_IFTYPE_AP) &&
                !(!memcmp(master_vif->ndev->dev_addr, params->macaddr,
                           ETH_ALEN))) {
                 found=true;
                 break;
            }
        }

        if (!found)
            goto err;

         vif->ap_vlan.master = master_vif;
         vif->ap_vlan.sta_4a = NULL;
         break;
    }
    case NL80211_IFTYPE_MONITOR:
        ndev->type = ARPHRD_IEEE80211_RADIOTAP;
        ndev->netdev_ops = &rwnx_netdev_monitor_ops;
        break;
    default:
        break;
    }

    if (type == NL80211_IFTYPE_AP_VLAN) {
        memcpy(ndev->dev_addr, params->macaddr, ETH_ALEN);
        memcpy(vif->wdev.address, params->macaddr, ETH_ALEN);
    }
    else {
        memcpy(ndev->dev_addr, rwnx_hw->wiphy->perm_addr, ETH_ALEN);
        ndev->dev_addr[5] ^= vif_idx;
        memcpy(vif->wdev.address, ndev->dev_addr, ETH_ALEN);
    }

	AICWFDBG(LOGINFO, "interface add:%x %x %x %x %x %x\n", vif->wdev.address[0], vif->wdev.address[1],
        vif->wdev.address[2], vif->wdev.address[3], vif->wdev.address[4], vif->wdev.address[5]);

    if (params) {
        vif->use_4addr = params->use_4addr;
        ndev->ieee80211_ptr->use_4addr = params->use_4addr;
    } else
        vif->use_4addr = false;

#ifdef CONFIG_USB_WIRELESS_EXT
	aicwf_set_wireless_ext(ndev, rwnx_hw);
#endif


    if (register_netdevice(ndev))
        goto err;

    spin_lock_bh(&rwnx_hw->cb_lock);
    list_add_tail(&vif->list, &rwnx_hw->vifs);
    spin_unlock_bh(&rwnx_hw->cb_lock);
    rwnx_hw->avail_idx_map &= ~BIT(vif_idx);

    return vif;

err:
    free_netdev(ndev);
    return NULL;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
void aicwf_p2p_alive_timeout(ulong data)
#else
void aicwf_p2p_alive_timeout(struct timer_list *t)
#endif
{
    struct rwnx_hw *rwnx_hw;
    struct rwnx_vif *rwnx_vif;
    struct rwnx_vif *rwnx_vif1, *tmp;
    u8_l p2p = 0;
    #if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
    rwnx_vif = (struct rwnx_vif *)data;
    rwnx_hw = rwnx_vif->rwnx_hw;
    #else
    rwnx_hw = from_timer(rwnx_hw, t, p2p_alive_timer);
    rwnx_vif = rwnx_hw->p2p_dev_vif;
    #endif

	//printk("%s enter %d \r\n", __func__, atomic_read(&rwnx_hw->p2p_alive_timer_count));

#if 1 //AIDEN workaround
	if(atomic_read(&rwnx_hw->p2p_alive_timer_count) > 2){
		p2p_working = 0;
	}
#endif

    list_for_each_entry_safe(rwnx_vif1, tmp, &rwnx_hw->vifs, list) {
        if ((rwnx_hw->avail_idx_map & BIT(rwnx_vif1->drv_vif_index)) == 0) {
            switch(RWNX_VIF_TYPE(rwnx_vif1)) {
            case NL80211_IFTYPE_P2P_CLIENT:
            case NL80211_IFTYPE_P2P_GO:
                rwnx_hw->is_p2p_alive = 1;
                p2p = 1;
                break;
            default:
                break;
            }
       }
    }

    if (p2p){
        atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);
    }else{
        atomic_inc(&rwnx_hw->p2p_alive_timer_count);
    }

    if (atomic_read(&rwnx_hw->p2p_alive_timer_count) < P2P_ALIVE_TIME_COUNT) {
        mod_timer(&rwnx_hw->p2p_alive_timer,
            jiffies + msecs_to_jiffies(P2P_ALIVE_TIME_MS));
        return;
    } else
        atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);

    rwnx_hw->is_p2p_alive = 0;
    rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index, true);

     /* Ensure that we won't process disconnect ind */
     spin_lock_bh(&rwnx_hw->cb_lock);

     rwnx_vif->up = false;
     rwnx_hw->vif_table[rwnx_vif->vif_index] = NULL;
     rwnx_hw->vif_started--;
     spin_unlock_bh(&rwnx_hw->cb_lock);
}


/*********************************************************************
 * Cfg80211 callbacks (and helper)
 *********************************************************************/
static struct wireless_dev *rwnx_virtual_interface_add(struct rwnx_hw *rwnx_hw,
                                               const char *name,
                                               unsigned char name_assign_type,
                                               enum nl80211_iftype type,
                                               struct vif_params *params)
{
    struct wireless_dev *wdev = NULL;
    struct rwnx_vif *vif;
    int min_idx, max_idx;
    int vif_idx = -1;
    int i;

    AICWFDBG(LOGINFO, "rwnx_virtual_interface_add: %d, %s\n", type, name);

    if (type == NL80211_IFTYPE_AP_VLAN) {
        min_idx = NX_VIRT_DEV_MAX;
        max_idx = NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX;
    } else {
        min_idx = 0;
        max_idx = NX_VIRT_DEV_MAX;
    }

    for (i = min_idx; i < max_idx; i++) {
        if ((rwnx_hw->avail_idx_map) & BIT(i)) {
            vif_idx = i;
            break;
        }
    }

    if (vif_idx < 0) {
        AICWFDBG(LOGERROR, "virtual_interface_add %s fail\n", name);
        return NULL;
    }

    vif = kzalloc(sizeof(struct rwnx_vif), GFP_KERNEL);
    if (unlikely(!vif)) {
        AICWFDBG(LOGERROR, "Could not allocate wireless device\n");
        return NULL;
    }
    wdev = &vif->wdev;
    wdev->wiphy = rwnx_hw->wiphy;
    wdev->iftype = type;

    AICWFDBG(LOGINFO, "rwnx_virtual_interface_add, ifname=%s, wdev=%p, vif_idx=%d\n", name, wdev, vif_idx);

    #ifndef CONFIG_USE_P2P0
    vif->is_p2p_vif = 1;
    vif->rwnx_hw = rwnx_hw;
    vif->vif_index = vif_idx;
    vif->wdev.wiphy = rwnx_hw->wiphy;
    vif->drv_vif_index = vif_idx;
    vif->up = false;
    vif->ch_index = RWNX_CH_NOT_SET;
    memset(&vif->net_stats, 0, sizeof(vif->net_stats));
    vif->use_4addr = false;

    spin_lock_bh(&rwnx_hw->cb_lock);
    list_add_tail(&vif->list, &rwnx_hw->vifs);
    spin_unlock_bh(&rwnx_hw->cb_lock);

    if (rwnx_hw->is_p2p_alive == 0) {
        #if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
        init_timer(&rwnx_hw->p2p_alive_timer);
        rwnx_hw->p2p_alive_timer.data = (unsigned long)vif;
        rwnx_hw->p2p_alive_timer.function = aicwf_p2p_alive_timeout;
        #else
        timer_setup(&rwnx_hw->p2p_alive_timer, aicwf_p2p_alive_timeout, 0);
        #endif
        rwnx_hw->is_p2p_alive = 0;
        rwnx_hw->is_p2p_connected = 0;
        rwnx_hw->p2p_dev_vif = vif;
        atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);
    }
    #endif
    rwnx_hw->avail_idx_map &= ~BIT(vif_idx);

    memcpy(vif->wdev.address, rwnx_hw->wiphy->perm_addr, ETH_ALEN);
    vif->wdev.address[5] ^= vif_idx;
    AICWFDBG(LOGERROR, "p2p dev addr=%x %x %x %x %x %x\n", vif->wdev.address[0], vif->wdev.address[1], \
        vif->wdev.address[2], vif->wdev.address[3], vif->wdev.address[4], vif->wdev.address[5]);

    return wdev;
}

/*
 * @brief Retrieve the rwnx_sta object allocated for a given MAC address
 * and a given role.
 */

static struct rwnx_sta *rwnx_retrieve_sta(struct rwnx_hw *rwnx_hw,
                                          struct rwnx_vif *rwnx_vif, u8 *addr,
                                          __le16 fc, bool ap)
{
    if (ap) {
        /* only deauth, disassoc and action are bufferable MMPDUs */
        bool bufferable = ieee80211_is_deauth(fc) ||
                          ieee80211_is_disassoc(fc) ||
                          ieee80211_is_action(fc);

        /* Check if the packet is bufferable or not */
        if (bufferable)
        {
            /* Check if address is a broadcast or a multicast address */
            if (is_broadcast_ether_addr(addr) || is_multicast_ether_addr(addr)) {
                /* Returned STA pointer */
                struct rwnx_sta *rwnx_sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];

                if (rwnx_sta->valid)
                    return rwnx_sta;
            } else {
                /* Returned STA pointer */
                struct rwnx_sta *rwnx_sta;

                /* Go through list of STAs linked with the provided VIF */
				spin_lock_bh(&rwnx_vif->rwnx_hw->cb_lock);
                list_for_each_entry(rwnx_sta, &rwnx_vif->ap.sta_list, list) {
					AICWFDBG(LOGDEBUG, "%s mac_addr:%x %x %x %x %x %x addr:%x %x %x %x %x %x \r\n", __func__,
						rwnx_sta->mac_addr[0],rwnx_sta->mac_addr[1],rwnx_sta->mac_addr[2],
						rwnx_sta->mac_addr[3],rwnx_sta->mac_addr[4],rwnx_sta->mac_addr[5],
						addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
                    if (rwnx_sta->valid &&
                        ether_addr_equal(rwnx_sta->mac_addr, addr)) {
                        /* Return the found STA */
						spin_unlock_bh(&rwnx_vif->rwnx_hw->cb_lock);
                        return rwnx_sta;
                    }
                }
				spin_unlock_bh(&rwnx_vif->rwnx_hw->cb_lock);
            }
        }
    } else {
        return rwnx_vif->sta.ap;
    }

    return NULL;
}

/**
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the struct
 *	wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *	also set the address member in the wdev.
 */
static struct wireless_dev *rwnx_cfg80211_add_iface(struct wiphy *wiphy,
                                                    const char *name,
                                                    unsigned char name_assign_type,
                                                    enum nl80211_iftype type,
                                                    struct vif_params *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct wireless_dev *wdev;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0))
    unsigned char name_assign_type = NET_NAME_UNKNOWN;
#endif

    if (type != NL80211_IFTYPE_P2P_DEVICE) {
        struct rwnx_vif *vif= rwnx_interface_add(rwnx_hw, name, name_assign_type, type, params);
        if (!vif)
            return ERR_PTR(-EINVAL);
        return &vif->wdev;

    } else {
        wdev = rwnx_virtual_interface_add(rwnx_hw, name, name_assign_type, type, params);
        if (!wdev)
            return ERR_PTR(-EINVAL);
        return wdev;
    }
}

/**
 * @del_virtual_intf: remove the virtual interface
 */
static int rwnx_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
    struct net_device *dev = wdev->netdev;
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
#else
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
#endif
   // printk("del_iface: %p\n",wdev);

	AICWFDBG(LOGINFO, "del_iface: %p, %x\n",wdev, wdev->address[5]);

    if (!dev || !rwnx_vif->ndev) {
#if 0
	if (rwnx_vif == rwnx_hw->p2p_dev_vif) {
		if (timer_pending(&rwnx_hw->p2p_alive_timer)) {
			del_timer_sync(&rwnx_hw->p2p_alive_timer);
		}
	}
#endif
        cfg80211_unregister_wdev(wdev);
        spin_lock_bh(&rwnx_hw->cb_lock);
        list_del(&rwnx_vif->list);
        spin_unlock_bh(&rwnx_hw->cb_lock);
        rwnx_hw->avail_idx_map |= BIT(rwnx_vif->drv_vif_index);
        rwnx_vif->ndev = NULL;
        kfree(rwnx_vif);
        return 0;
    }
#if 0
    netdev_info(dev, "Remove Interface");
#endif

	AICWFDBG(LOGINFO, "%s Remove Interface \r\n", dev->name);
    if (dev->reg_state == NETREG_REGISTERED) {
        /* Will call rwnx_close if interface is UP */
        unregister_netdevice(dev);
    }

    spin_lock_bh(&rwnx_hw->cb_lock);
    list_del(&rwnx_vif->list);
    spin_unlock_bh(&rwnx_hw->cb_lock);
    rwnx_hw->avail_idx_map |= BIT(rwnx_vif->drv_vif_index);
    rwnx_vif->ndev = NULL;

    /* Clear the priv in adapter */
    dev->ieee80211_ptr = NULL;

    return 0;
}

/**
 * @change_virtual_intf: change type/configuration of virtual interface,
 *	keep the struct wireless_dev's iftype updated.
 */
static int rwnx_cfg80211_change_iface(struct wiphy *wiphy,
                                      struct net_device *dev,
                                      enum nl80211_iftype type,
                                      struct vif_params *params)
{
#ifndef CONFIG_RWNX_MON_DATA
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#endif
    struct rwnx_vif *vif = netdev_priv(dev);
    struct mm_add_if_cfm add_if_cfm;
    bool_l p2p = false;
    int ret;

    RWNX_DBG(RWNX_FN_ENTRY_STR);
	AICWFDBG(LOGINFO, "change_if: %d to %d, %d, %d", vif->wdev.iftype, type, NL80211_IFTYPE_P2P_CLIENT, NL80211_IFTYPE_STATION);

#ifdef CONFIG_COEX
    if (type == NL80211_IFTYPE_AP || type == NL80211_IFTYPE_P2P_GO)
        rwnx_send_coex_req(vif->rwnx_hw, 1, 0);
    if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP || RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO)
        rwnx_send_coex_req(vif->rwnx_hw, 0, 1);
#endif
#ifndef CONFIG_RWNX_MON_DATA
    if ((type == NL80211_IFTYPE_MONITOR) &&
       (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
        struct rwnx_vif *vif_el;
        list_for_each_entry(vif_el, &rwnx_hw->vifs, list) {
            // Check if data interface already exists
            if ((vif_el != vif) &&
               (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
                wiphy_err(rwnx_hw->wiphy,
                        "Monitor+Data interface support (MON_DATA) disabled\n");
                return -EIO;
            }
        }
    }
#endif

    // Reset to default case (i.e. not monitor)
    dev->type = ARPHRD_ETHER;
    dev->netdev_ops = &rwnx_netdev_ops;

    switch (type) {
    case NL80211_IFTYPE_STATION:
    case NL80211_IFTYPE_P2P_CLIENT:
        vif->sta.ap = NULL;
        vif->sta.tdls_sta = NULL;
        vif->sta.external_auth = false;
        break;
    case NL80211_IFTYPE_MESH_POINT:
        INIT_LIST_HEAD(&vif->ap.mpath_list);
        INIT_LIST_HEAD(&vif->ap.proxy_list);
        vif->ap.create_path = false;
        vif->ap.generation = 0;
        // no break
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_P2P_GO:
        INIT_LIST_HEAD(&vif->ap.sta_list);
        memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
        break;
    case NL80211_IFTYPE_AP_VLAN:
        return -EPERM;
    case NL80211_IFTYPE_MONITOR:
        dev->type = ARPHRD_IEEE80211_RADIOTAP;
        dev->netdev_ops = &rwnx_netdev_monitor_ops;
        break;
    default:
        break;
    }

    vif->wdev.iftype = type;
    if (params->use_4addr != -1)
        vif->use_4addr = params->use_4addr;
    if (type == NL80211_IFTYPE_P2P_CLIENT || type == NL80211_IFTYPE_P2P_GO){
        p2p = true;
    }
	if (vif->up) {
	    /* Abort scan request on the vif */
	    if (vif->rwnx_hw->scan_request &&
	        vif->rwnx_hw->scan_request->wdev == &vif->wdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	        struct cfg80211_scan_info info = {
	            .aborted = true,
	        };
	        cfg80211_scan_done(vif->rwnx_hw->scan_request, &info);
#else
	        cfg80211_scan_done(vif->rwnx_hw->scan_request, true);
#endif
	        if ((ret = rwnx_send_scanu_cancel_req(vif->rwnx_hw, NULL))) {
				AICWFDBG(LOGERROR, "scanu_cancel fail\n");
	            return ret;
	        }
	        vif->rwnx_hw->scan_request = NULL;
	    }
	    if ((ret = rwnx_send_remove_if(vif->rwnx_hw, vif->vif_index, false))) {
			AICWFDBG(LOGERROR, "remove_if fail\n");
	        return ret;
	    }
		vif->rwnx_hw->vif_table[vif->vif_index] = NULL;
		AICWFDBG(LOGINFO, "change_if from %d \n", vif->vif_index);
	    if ((ret = rwnx_send_add_if(vif->rwnx_hw, vif->wdev.address,
	                                              RWNX_VIF_TYPE(vif), p2p, &add_if_cfm))) {
	            AICWFDBG(LOGERROR, "add if fail\n");
	            return ret;
	    }
	    if (add_if_cfm.status != 0) {
				AICWFDBG(LOGERROR, "add if status fail\n");
	            return -EIO;
	    }

		AICWFDBG(LOGINFO, "change_if to %d \n", add_if_cfm.inst_nbr);
	    /* Save the index retrieved from LMAC */
	    spin_lock_bh(&vif->rwnx_hw->cb_lock);
	    vif->vif_index = add_if_cfm.inst_nbr;
	    vif->rwnx_hw->vif_table[add_if_cfm.inst_nbr] = vif;
	    spin_unlock_bh(&vif->rwnx_hw->cb_lock);
	}

    if (type == NL80211_IFTYPE_MONITOR) {
        vif->rwnx_hw->monitor_vif = vif->vif_index;
        #if defined(CONFIG_RWNX_MON_XMIT)
        rwnx_txq_unk_vif_init(vif);
        #endif
        #if defined(CONFIG_RWNX_MON_RXFILTER)
        rwnx_send_set_filter(vif->rwnx_hw, (FIF_BCN_PRBRESP_PROMISC | FIF_OTHER_BSS | FIF_PSPOLL | FIF_PROBE_REQ));
        #endif
    } else {
        vif->rwnx_hw->monitor_vif = RWNX_INVALID_VIF;
    }

    return 0;
}

static int rwnx_cfgp2p_start_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
    int ret = 0;

    //do nothing
    AICWFDBG(LOGINFO, "P2P interface started\n");

    return ret;
}

static void rwnx_cfgp2p_stop_p2p_device(struct wiphy *wiphy, struct wireless_dev *wdev)
{
#if 0
    int ret = 0;
    struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

    if (!cfg)
        return;

    CFGP2P_DBG(("Enter\n"));

    ret = wl_cfg80211_scan_stop(cfg, wdev);
    if (unlikely(ret < 0)) {
        CFGP2P_ERR(("P2P scan stop failed, ret=%d\n", ret));
    }

    if (!cfg->p2p)
        return;

    /* Cancel any on-going listen */
    wl_cfgp2p_cancel_listen(cfg, bcmcfg_to_prmry_ndev(cfg), wdev, TRUE);

    ret = wl_cfgp2p_disable_discovery(cfg);
    if (unlikely(ret < 0)) {
        CFGP2P_ERR(("P2P disable discovery failed, ret=%d\n", ret));
    }

    p2p_on(cfg) = false;
#endif
	int ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
	/* Abort scan request on the vif */
	if (rwnx_hw->scan_request &&
		rwnx_hw->scan_request->wdev == &rwnx_vif->wdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
		cfg80211_scan_done(rwnx_hw->scan_request, true);
#endif
		rwnx_hw->scan_request = NULL;
		ret = rwnx_send_scanu_cancel_req(rwnx_hw, NULL);
		if (ret){
			AICWFDBG(LOGERROR, "scanu_cancel fail\n");
		}
	}

	if (rwnx_vif == rwnx_hw->p2p_dev_vif) {
		rwnx_hw->is_p2p_alive = 0;
		if (timer_pending(&rwnx_hw->p2p_alive_timer)) {
			del_timer_sync(&rwnx_hw->p2p_alive_timer);
		}
		if (rwnx_vif->up) {
			rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index, true);
			/* Ensure that we won't process disconnect ind */
			spin_lock_bh(&rwnx_hw->cb_lock);
			rwnx_vif->up = false;
			rwnx_hw->vif_table[rwnx_vif->vif_index] = NULL;
			rwnx_hw->vif_started--;
			spin_unlock_bh(&rwnx_hw->cb_lock);
		}
	}

	AICWFDBG(LOGINFO, "Exit. P2P interface stopped\n");

    return;
}

int rwnx_send_check_p2p(struct cfg80211_scan_request *param){
	int index = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
	int i = 0;

	for(i = 0;i < index;i++){
        if (!memcmp("DIRECT-", param->ssids[i].ssid,
            (sizeof("DIRECT-") - 1))) {
            //printk("AIDEN rwnx_send_check_p2p!!\r\n");
			return 1;
        }
	}
		return 0;
}

/**
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 */
static int rwnx_cfg80211_scan(struct wiphy *wiphy,
	#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	struct net_device *dev,
	#endif
                              struct cfg80211_scan_request *request)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    struct rwnx_vif *rwnx_vif = container_of(request->wdev, struct rwnx_vif,
                                            wdev);
#else
    struct rwnx_vif* rwnx_vif = netdev_priv(request->dev);
#endif
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

	if(testmode){
		AICWFDBG(LOGERROR, "%s in testmode return busy\r\n", __func__);
		return -EBUSY;
	}

	if((int)atomic_read(&rwnx_vif->drv_conn_state) == (int)RWNX_DRV_STATUS_CONNECTING){
		AICWFDBG(LOGERROR, "%s in connecting return busy\r\n", __func__);
		return -EBUSY;
	}

#ifndef CONFIG_STA_SCAN_WHEN_P2P_WORKING
	if (p2p_working && RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT &&
		!rwnx_send_check_p2p(request)) {
		AICWFDBG(LOGINFO, "p2p is working, scan abort\n");
		return -EBUSY;
	}
#endif
    if (scanning) {
		AICWFDBG(LOGINFO, "is scanning, abort\n");
        if ((error =  rwnx_send_scanu_cancel_req(rwnx_hw, NULL)))
            return error;
        msleep(150);
    }

	if((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) &&  rwnx_vif->sta.external_auth){
		AICWFDBG(LOGINFO, "scan about: external auth\r\n");
		return -EBUSY;
	}

    rwnx_hw->scan_request = request;
    if ((error = rwnx_send_scanu_req(rwnx_hw, rwnx_vif, request)))
        return error;

    return 0;
}

bool key_flag = false;
/**
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *	when adding a group key.
 */
static int rwnx_cfg80211_add_key(struct wiphy *wiphy, struct net_device *netdev,
                                 u8 key_index, bool pairwise, const u8 *mac_addr,
                                 struct key_params *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif = netdev_priv(netdev);
    int i, error = 0;
    struct mm_key_add_cfm key_add_cfm;
    u8_l cipher = 0;
    struct rwnx_sta *sta = NULL;
    struct rwnx_key *rwnx_key;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (mac_addr) {
        sta = rwnx_get_sta(rwnx_hw, mac_addr);
        if (!sta)
            return -EINVAL;
        rwnx_key = &sta->key;
        if (vif->wdev.iftype == NL80211_IFTYPE_STATION || vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)
            vif->sta.paired_cipher_type = params->cipher;
    }
    else {
        rwnx_key = &vif->key[key_index];
        vif->key_has_add = 1;
        if (vif->wdev.iftype == NL80211_IFTYPE_STATION || vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)
            vif->sta.group_cipher_type = params->cipher;
    }

    /* Retrieve the cipher suite selector */
    switch (params->cipher) {
    case WLAN_CIPHER_SUITE_WEP40:
        cipher = MAC_CIPHER_WEP40;
        break;
    case WLAN_CIPHER_SUITE_WEP104:
        cipher = MAC_CIPHER_WEP104;
        break;
    case WLAN_CIPHER_SUITE_TKIP:
        cipher = MAC_CIPHER_TKIP;
        break;
    case WLAN_CIPHER_SUITE_CCMP:
        cipher = MAC_CIPHER_CCMP;
        break;
    case WLAN_CIPHER_SUITE_AES_CMAC:
        cipher = MAC_CIPHER_BIP_CMAC_128;
        break;
    case WLAN_CIPHER_SUITE_SMS4:
    {
        // Need to reverse key order
        u8 tmp, *key = (u8 *)params->key;
        cipher = MAC_CIPHER_WPI_SMS4;
        for (i = 0; i < WPI_SUBKEY_LEN/2; i++) {
            tmp = key[i];
            key[i] = key[WPI_SUBKEY_LEN - 1 - i];
            key[WPI_SUBKEY_LEN - 1 - i] = tmp;
        }
        for (i = 0; i < WPI_SUBKEY_LEN/2; i++) {
            tmp = key[i + WPI_SUBKEY_LEN];
            key[i + WPI_SUBKEY_LEN] = key[WPI_KEY_LEN - 1 - i];
            key[WPI_KEY_LEN - 1 - i] = tmp;
        }
        break;
    }
    default:
        return -EINVAL;
    }

    key_flag = false;
    if ((error = rwnx_send_key_add(rwnx_hw, vif->vif_index,
                                   (sta ? sta->sta_idx : 0xFF), pairwise,
                                   (u8 *)params->key, params->key_len,
                                   key_index, cipher, &key_add_cfm)))
        return error;

    if (key_add_cfm.status != 0) {
        RWNX_PRINT_CFM_ERR(key_add);
        return -EIO;
    }

    /* Save the index retrieved from LMAC */
    rwnx_key->hw_idx = key_add_cfm.hw_key_idx;

    return 0;
}

/**
 * @get_key: get information about the key with the given parameters.
 *	@mac_addr will be %NULL when requesting information for a group
 *	key. All pointers given to the @callback function need not be valid
 *	after it returns. This function should return an error if it is
 *	not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 */
static int rwnx_cfg80211_get_key(struct wiphy *wiphy, struct net_device *netdev,
                                 u8 key_index, bool pairwise, const u8 *mac_addr,
                                 void *cookie,
                                 void (*callback)(void *cookie, struct key_params*))
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    return -1;
}


/**
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index, return -ENOENT if the key doesn't exist.
 */
static int rwnx_cfg80211_del_key(struct wiphy *wiphy, struct net_device *netdev,
                                 u8 key_index, bool pairwise, const u8 *mac_addr)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif = netdev_priv(netdev);
    int error;
    struct rwnx_sta *sta = NULL;
    struct rwnx_key *rwnx_key;
	if (!key_flag && vif->wdev.iftype == NL80211_IFTYPE_STATION)
		return 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);
    if (mac_addr) {
        sta = rwnx_get_sta(rwnx_hw, mac_addr);
        if (!sta)
            return -EINVAL;
        rwnx_key = &sta->key;
        if (vif->wdev.iftype == NL80211_IFTYPE_STATION || vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)
            vif->sta.paired_cipher_type = 0xff;
    }
    else {
        rwnx_key = &vif->key[key_index];
        vif->key_has_add = 0;
        if (vif->wdev.iftype == NL80211_IFTYPE_STATION || vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)
            vif->sta.group_cipher_type = 0xff;
    }

    error = rwnx_send_key_del(rwnx_hw, rwnx_key->hw_idx);

    rwnx_key->hw_idx = 0;
    return error;
}

/**
 * @set_default_key: set the default key on an interface
 */
static int rwnx_cfg80211_set_default_key(struct wiphy *wiphy,
                                         struct net_device *netdev,
                                         u8 key_index, bool unicast, bool multicast)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    return 0;
}

/**
 * @set_default_mgmt_key: set the default management frame key on an interface
 */
static int rwnx_cfg80211_set_default_mgmt_key(struct wiphy *wiphy,
                                              struct net_device *netdev,
                                              u8 key_index)
{
    return 0;
}

/**
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *	If the connection fails for some reason, call cfg80211_connect_result()
 *	with the status from the AP.
 *	(invoked with the wireless_dev mutex held)
 */

static int rwnx_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
                                 struct cfg80211_connect_params *sme)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct sm_connect_cfm sm_connect_cfm;
    int error = 0;
    int is_wep = ((sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) ||
	                (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104) ||
	                (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
		            (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104));

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#if 1
#if 0
	if((int)atomic_read(&rwnx_vif->drv_conn_state) == (int)RWNX_DRV_STATUS_CONNECTED){
		AICWFDBG(LOGERROR, "%s driver was connected return it \r\n", __func__);
		return -EALREADY;
	}
#endif
	if((int)atomic_read(&rwnx_vif->drv_conn_state) == (int)RWNX_DRV_STATUS_DISCONNECTING){
		AICWFDBG(LOGERROR, "%s driver is disconnecting return it \r\n", __func__);
		return -EALREADY;
	}
#endif

	atomic_set(&rwnx_vif->drv_conn_state, (int)RWNX_DRV_STATUS_CONNECTING);

    if(is_wep) {
        if(sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC) {
            if(rwnx_vif->wep_enabled && rwnx_vif->wep_auth_err) {
                if(rwnx_vif->last_auth_type == NL80211_AUTHTYPE_SHARED_KEY)
                    sme->auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
                else
                    sme->auth_type = NL80211_AUTHTYPE_SHARED_KEY;
            } else {
                    if((rwnx_vif->wep_enabled && !rwnx_vif->wep_auth_err))
                        sme->auth_type = rwnx_vif->last_auth_type;
                    else
                        sme->auth_type = NL80211_AUTHTYPE_SHARED_KEY;
            }
			AICWFDBG(LOGINFO, "auto: use sme->auth_type = %d\r\n", sme->auth_type);
        } else {
            if (rwnx_vif->wep_enabled && rwnx_vif->wep_auth_err && (sme->auth_type == rwnx_vif->last_auth_type)) {
                if(sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY) {
                    sme->auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
					AICWFDBG(LOGINFO, "start connect, auth_type changed, shared --> open\n");
                } else if(sme->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM) {
                    sme->auth_type = NL80211_AUTHTYPE_SHARED_KEY;
					AICWFDBG(LOGINFO, "start connect, auth_type changed, open --> shared\n");
                }
            }
        }
    }

    /* For SHARED-KEY authentication, must install key first */
    if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY && sme->key)
    {
        struct key_params key_params;
        key_params.key = (u8*)sme->key;
        key_params.seq = NULL;
        key_params.key_len = sme->key_len;
        key_params.seq_len = 0;
        key_params.cipher = sme->crypto.cipher_group;
        rwnx_cfg80211_add_key(wiphy, dev, sme->key_idx, false, NULL, &key_params);
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(CONFIG_WPA3_FOR_OLD_KERNEL)
    else if ((sme->auth_type == NL80211_AUTHTYPE_SAE) &&
             !(sme->flags & CONNECT_REQ_EXTERNAL_AUTH_SUPPORT)) {
        netdev_err(dev, "Doesn't support SAE without external authentication\n");
        return -EINVAL;
    }
#endif

    if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
        rwnx_hw->is_p2p_connected = 1;
    }

    if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION || rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT) {
        rwnx_vif->sta.paired_cipher_type = 0xff;
        rwnx_vif->sta.group_cipher_type = 0xff;
    }


    /* Forward the information to the LMAC */
    if ((error = rwnx_send_sm_connect_req(rwnx_hw, rwnx_vif, sme, &sm_connect_cfm)))
        return error;

    // Check the status
    switch (sm_connect_cfm.status)
    {
        case CO_OK:
            error = 0;
            break;
        case CO_BUSY:
            error = -EINPROGRESS;
            break;
        case CO_OP_IN_PROGRESS:
            error = -EALREADY;
            break;
        default:
            error = -EIO;
            break;
    }

    return error;
}

/**
 * @disconnect: Disconnect from the BSS/ESS.
 *	(invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
                                    u16 reason_code)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);

    RWNX_DBG(RWNX_FN_ENTRY_STR);
	AICWFDBG(LOGINFO, "%s drv_vif_index:%d disconnect reason:%d \r\n",
		__func__, rwnx_vif->drv_vif_index, reason_code);

#if 0
	while(atomic_read(&rwnx_vif->drv_conn_state) == RWNX_DRV_STATUS_CONNECTING){
		AICWFDBG(LOGERROR, "%s driver connecting waiting 100ms \r\n", __func__);
		msleep(100);
		retry--;
		if(retry == 0){
			break;
		}
		if(rwnx_hw->usbdev->state == USB_DOWN_ST){
			break;
		}
	}
	if(atomic_read(&rwnx_vif->drv_conn_state) == RWNX_DRV_STATUS_CONNECTED){

		atomic_set(&rwnx_vif->drv_conn_state, RWNX_DRV_STATUS_DISCONNECTING);
	}
#endif
	if(atomic_read(&rwnx_vif->drv_conn_state) == RWNX_DRV_STATUS_CONNECTING){
		AICWFDBG(LOGINFO, "%s call cfg80211_connect_result reason:%d \r\n",
			__func__, reason_code);
		msleep(500);
	}

	if(atomic_read(&rwnx_vif->drv_conn_state) == RWNX_DRV_STATUS_CONNECTED){
		atomic_set(&rwnx_vif->drv_conn_state, RWNX_DRV_STATUS_DISCONNECTING);

		#ifdef CONFIG_USB_WIRELESS_EXT
		memset(rwnx_hw->wext_essid, 0, 32);
		#endif
		key_flag = true;
		return(rwnx_send_sm_disconnect_req(rwnx_hw, rwnx_vif, reason_code));
	}else{
		cfg80211_connect_result(dev,  NULL, NULL, 0, NULL, 0,
			reason_code?reason_code:WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC);
		atomic_set(&rwnx_vif->drv_conn_state, RWNX_DRV_STATUS_DISCONNECTED);
		return 0;
	}

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(CONFIG_WPA3_FOR_OLD_KERNEL)
/**
 * @external_auth: indicates result of offloaded authentication processing from
 *     user space
 */
static int rwnx_cfg80211_external_auth(struct wiphy *wiphy, struct net_device *dev,
                                       struct cfg80211_external_auth_params *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	//printk("%s Enter \r\n");
    if (!rwnx_vif->sta.external_auth)
        return -EINVAL;

    rwnx_external_auth_disable(rwnx_vif);
    return rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif,
                                                   params->status);
}
#endif

/**
 * @add_station: Add a new station.
 */
static int rwnx_cfg80211_add_station(struct wiphy *wiphy,
	struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
		u8 *mac,
#else
		const u8 *mac,
#endif
	struct station_parameters *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct me_sta_add_cfm me_sta_add_cfm;
    int error = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    WARN_ON(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN);

    /* Do not add TDLS station */
    if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
        return 0;

    /* Indicate we are in a STA addition process - This will allow handling
     * potential PS mode change indications correctly
     */
    rwnx_hw->adding_sta = true;

    /* Forward the information to the LMAC */
    if ((error = rwnx_send_me_sta_add(rwnx_hw, params, mac, rwnx_vif->vif_index,
                                      &me_sta_add_cfm)))
        return error;

    // Check the status
    switch (me_sta_add_cfm.status)
    {
        case CO_OK:
        {
            struct rwnx_sta *sta = &rwnx_hw->sta_table[me_sta_add_cfm.sta_idx];
            int tid;
            sta->aid = params->aid;

            sta->sta_idx = me_sta_add_cfm.sta_idx;
            sta->ch_idx = rwnx_vif->ch_index;
            sta->vif_idx = rwnx_vif->vif_index;
            sta->vlan_idx = sta->vif_idx;
            sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
            sta->ht = params->ht_capa ? 1 : 0;
            sta->vht = params->vht_capa ? 1 : 0;
            sta->acm = 0;
            sta->key.hw_idx = 0;

            if (params->local_pm != NL80211_MESH_POWER_UNKNOWN)
                sta->mesh_pm = params->local_pm;
            else
                sta->mesh_pm = rwnx_vif->ap.next_mesh_pm;
            rwnx_update_mesh_power_mode(rwnx_vif);

            for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
                int uapsd_bit = rwnx_hwq2uapsd[rwnx_tid2hwq[tid]];
                if (params->uapsd_queues & uapsd_bit)
                    sta->uapsd_tids |= 1 << tid;
                else
                    sta->uapsd_tids &= ~(1 << tid);
            }
            memcpy(sta->mac_addr, mac, ETH_ALEN);
#ifdef CONFIG_DEBUG_FS
            rwnx_dbgfs_register_rc_stat(rwnx_hw, sta);
#endif
            /* Ensure that we won't process PS change or channel switch ind*/
            spin_lock_bh(&rwnx_hw->cb_lock);
            rwnx_txq_sta_init(rwnx_hw, sta, rwnx_txq_vif_get_status(rwnx_vif));
            list_add_tail(&sta->list, &rwnx_vif->ap.sta_list);
            sta->valid = true;
            rwnx_ps_bh_enable(rwnx_hw, sta, sta->ps.active || me_sta_add_cfm.pm_state);
            spin_unlock_bh(&rwnx_hw->cb_lock);

            error = 0;
            if(atomic_read(&rwnx_hw->sta_flowctrl[sta->sta_idx].tx_pending_cnt) > 0)
                AICWFDBG(LOGDEBUG, "sta idx %d fc error %d.\n", sta->sta_idx, atomic_read(&rwnx_hw->sta_flowctrl[sta->sta_idx].tx_pending_cnt));

            if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP || rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO) {
                struct station_info sinfo;
                memset(&sinfo, 0, sizeof(struct station_info));
                sinfo.assoc_req_ies = NULL;
                sinfo.assoc_req_ies_len = 0;
                #if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
                sinfo.filled |= STATION_INFO_ASSOC_REQ_IES;
                #endif
		        cfg80211_new_sta(rwnx_vif->ndev, sta->mac_addr, &sinfo, GFP_KERNEL);
            }
#ifdef CONFIG_RWNX_BFMER
            if (rwnx_hw->mod_params->bfmer)
                rwnx_send_bfmer_enable(rwnx_hw, sta, params->vht_capa);

            rwnx_mu_group_sta_init(sta, params->vht_capa);
#endif /* CONFIG_RWNX_BFMER */

            #define PRINT_STA_FLAG(f)                               \
                (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "["#f"]" : "")

            netdev_info(dev, "Add sta %d (%pM) flags=%s%s%s%s%s%s%s",
                        sta->sta_idx, mac,
                        PRINT_STA_FLAG(AUTHORIZED),
                        PRINT_STA_FLAG(SHORT_PREAMBLE),
                        PRINT_STA_FLAG(WME),
                        PRINT_STA_FLAG(MFP),
                        PRINT_STA_FLAG(AUTHENTICATED),
                        PRINT_STA_FLAG(TDLS_PEER),
                        PRINT_STA_FLAG(ASSOCIATED));
            #undef PRINT_STA_FLAG
            break;
        }
        default:
            error = -EBUSY;
            break;
    }

    rwnx_hw->adding_sta = false;

    return error;
}

/**
 * @del_station: Remove a station
 */
static int rwnx_cfg80211_del_station_compat(struct wiphy *wiphy,
                                            struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
		u8 *mac
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0))
		const u8 *mac
#else
		struct station_del_parameters *params
#endif
)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_sta *cur, *tmp;
    int error = 0, found = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    const u8 *mac = NULL;
#endif
#ifdef AICWF_RX_REORDER
    struct reord_ctrl_info *reord_info, *reord_tmp;
    u8 *macaddr;
    struct aicwf_rx_priv *rx_priv;
#endif

    //RWNX_DBG(RWNX_FN_ENTRY_STR);
    AICWFDBG(LOGDEBUG ,"%s: %pM\n", __func__, mac);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    if (params)
        mac = params->mac;
#endif

	do {
		spin_lock_bh(&rwnx_hw->cb_lock);
		if(list_empty(&rwnx_vif->ap.sta_list)) {
			spin_unlock_bh(&rwnx_hw->cb_lock);
			break;
		}

	    list_for_each_entry_safe(cur, tmp, &rwnx_vif->ap.sta_list, list) {
	        if ((!mac) || (!memcmp(cur->mac_addr, mac, ETH_ALEN)))  {
				found = 1;
				break;
	    	}
	    }

		if(found) {
            		cur->ps.active = false;
            		cur->valid = false;
            		list_del(&cur->list);
        	}
		spin_unlock_bh(&rwnx_hw->cb_lock);

		if(found) {
			netdev_info(dev, "Del sta %d (%pM)", cur->sta_idx, cur->mac_addr);
			if (cur->vif_idx != cur->vlan_idx) {
				struct rwnx_vif *vlan_vif;
				vlan_vif = rwnx_hw->vif_table[cur->vlan_idx];
				if (vlan_vif->up) {
					if ((RWNX_VIF_TYPE(vlan_vif) == NL80211_IFTYPE_AP_VLAN) &&
						(vlan_vif->use_4addr)) {
						vlan_vif->ap_vlan.sta_4a = NULL;
					} else {
						WARN(1, "Deleting sta belonging to VLAN other than AP_VLAN 4A");
					}
				}
			}
            		if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP || rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO) {
                		cfg80211_del_sta(rwnx_vif->ndev, cur->mac_addr, GFP_KERNEL);
            		}

#ifdef AICWF_RX_REORDER
#ifdef AICWF_SDIO_SUPPORT
			rx_priv = rwnx_hw->sdiodev->rx_priv;
#else
			rx_priv = rwnx_hw->usbdev->rx_priv;
#endif
			if ((rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)) {
				BUG();//should be other function
			}
			else if ((rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO)){
				macaddr = cur->mac_addr;
				AICWFDBG(LOGINFO, "deinit:macaddr:%x,%x,%x,%x,%x,%x\r\n", macaddr[0],macaddr[1],macaddr[2], \
									   macaddr[3],macaddr[4],macaddr[5]);
				list_for_each_entry_safe(reord_info, reord_tmp,
					&rx_priv->stas_reord_list, list) {
                    			AICWFDBG(LOGINFO, "reord_mac:%x,%x,%x,%x,%x,%x\r\n", reord_info->mac_addr[0],reord_info->mac_addr[1],reord_info->mac_addr[2], \
										   reord_info->mac_addr[3],reord_info->mac_addr[4],reord_info->mac_addr[5]);
					if (!memcmp(reord_info->mac_addr, macaddr, 6)) {
						reord_deinit_sta(rx_priv, reord_info);
						break;
					}
				}
			}
#endif

			rwnx_txq_sta_deinit(rwnx_hw, cur);
			error = rwnx_send_me_sta_del(rwnx_hw, cur->sta_idx, false);
			if ((error != 0) && (error != -EPIPE))
				return error;

#ifdef CONFIG_RWNX_BFMER
			// Disable Beamformer if supported
			rwnx_bfmer_report_del(rwnx_hw, cur);
			rwnx_mu_group_sta_del(rwnx_hw, cur);
#endif /* CONFIG_RWNX_BFMER */

#ifdef CONFIG_DEBUG_FS
			rwnx_dbgfs_unregister_rc_stat(rwnx_hw, cur);
#endif
		}

		if(mac)
			break;
	}	while (1);

    rwnx_update_mesh_power_mode(rwnx_vif);

	if(!found && mac != NULL)
		return -ENOENT;
	else
    	return 0;
}


void apm_staloss_work_process(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw, apmStalossWork);
	struct rwnx_sta *cur, *tmp;
	int error = 0;

#ifdef AICWF_RX_REORDER
	struct reord_ctrl_info *reord_info, *reord_tmp;
	u8 *macaddr;
	struct aicwf_rx_priv *rx_priv;
#endif
	struct rwnx_vif *rwnx_vif;
    bool_l found = false;
	const u8 *mac = rwnx_hw->sta_mac_addr;

	RWNX_DBG(RWNX_FN_ENTRY_STR);

    // Look for VIF entry
    list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
        if (rwnx_vif->vif_index == rwnx_hw->apm_vif_idx) {
            found = true;
            break;
        }
    }

	AICWFDBG(LOGINFO, "apm vif idx=%d, found=%d, mac addr=%pM\n", rwnx_hw->apm_vif_idx, found, mac);
    if (!found || !rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_AP && RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_GO))
    {
        return;
    }

	found = false;
	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry_safe(cur, tmp, &rwnx_vif->ap.sta_list, list) {
		if ((mac) && (!memcmp(cur->mac_addr, mac, ETH_ALEN))) {
			found = true;
			break;
		}
	}
        if(found) {
		cur->ps.active = false;
        	cur->valid = false;
	        list_del(&cur->list);
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if(found) {
		netdev_info(rwnx_vif->ndev, "Del sta %d (%pM)", cur->sta_idx, cur->mac_addr);
		if (cur->vif_idx != cur->vlan_idx) {
			struct rwnx_vif *vlan_vif;
			vlan_vif = rwnx_hw->vif_table[cur->vlan_idx];
			if (vlan_vif->up) {
				if ((RWNX_VIF_TYPE(vlan_vif) == NL80211_IFTYPE_AP_VLAN) &&
					(vlan_vif->use_4addr)) {
					vlan_vif->ap_vlan.sta_4a = NULL;
				} else {
					WARN(1, "Deleting sta belonging to VLAN other than AP_VLAN 4A");
				}
			}
		}
		   // if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP || rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO) {
		//		cfg80211_del_sta(rwnx_vif->ndev, cur->mac_addr, GFP_KERNEL);
		//	}

#ifdef AICWF_RX_REORDER
#ifdef AICWF_SDIO_SUPPORT
		rx_priv = rwnx_hw->sdiodev->rx_priv;
#else
		rx_priv = rwnx_hw->usbdev->rx_priv;
#endif
		if ((rwnx_vif->wdev.iftype == NL80211_IFTYPE_STATION) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_CLIENT)) {
			BUG();//should be other function
		} else if ((rwnx_vif->wdev.iftype == NL80211_IFTYPE_AP) || (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO)) {
			macaddr = cur->mac_addr;
			AICWFDBG(LOGINFO, "deinit:macaddr:%x,%x,%x,%x,%x,%x\r\n", macaddr[0], macaddr[1], macaddr[2], \
								   macaddr[3], macaddr[4], macaddr[5]);
			//spin_lock_bh(&rx_priv->stas_reord_lock);
			list_for_each_entry_safe(reord_info, reord_tmp,
				&rx_priv->stas_reord_list, list) {
				AICWFDBG(LOGINFO, "reord_mac:%x,%x,%x,%x,%x,%x\r\n", reord_info->mac_addr[0], reord_info->mac_addr[1], reord_info->mac_addr[2], \
									   reord_info->mac_addr[3], reord_info->mac_addr[4], reord_info->mac_addr[5]);
				if (!memcmp(reord_info->mac_addr, macaddr, 6)) {
					reord_deinit_sta(rx_priv, reord_info);
					break;
				}
			}
			//spin_unlock_bh(&rx_priv->stas_reord_lock);
		}
#endif

		rwnx_txq_sta_deinit(rwnx_hw, cur);
		error = rwnx_send_me_sta_del(rwnx_hw, cur->sta_idx, false);
		if ((error != 0) && (error != -EPIPE))
			return;

#ifdef CONFIG_RWNX_BFMER
		// Disable Beamformer if supported
		rwnx_bfmer_report_del(rwnx_hw, cur);
		rwnx_mu_group_sta_del(rwnx_hw, cur);
#endif /* CONFIG_RWNX_BFMER */


#ifdef CONFIG_DEBUG_FS
		rwnx_dbgfs_unregister_rc_stat(rwnx_hw, cur);
#endif
	}else {
		printk("sta not found: %pM\n", mac);
		return;
	}

	rwnx_update_mesh_power_mode(rwnx_vif);
}


void apm_probe_sta_work_process(struct work_struct *work)
{
       struct apm_probe_sta *probe_sta = container_of(work, struct apm_probe_sta, apmprobestaWork);
       struct rwnx_vif *rwnx_vif = container_of(probe_sta, struct rwnx_vif, sta_probe);
       bool found = false;
       struct rwnx_sta *cur, *tmp;

       u8 *mac = rwnx_vif->sta_probe.sta_mac_addr;

       RWNX_DBG(RWNX_FN_ENTRY_STR);

	   spin_lock_bh(&rwnx_vif->rwnx_hw->cb_lock);
       list_for_each_entry_safe(cur, tmp, &rwnx_vif->ap.sta_list, list) {
               if (!memcmp(cur->mac_addr, mac, ETH_ALEN)) {
                       found = true;
                       break;
               }
       }
	   spin_unlock_bh(&rwnx_vif->rwnx_hw->cb_lock);

       printk("sta %pM found = %d\n", mac, found);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
       if(found)
               cfg80211_probe_status(rwnx_vif->ndev, mac, (u64)rwnx_vif->sta_probe.probe_id, 1, 0, false, GFP_ATOMIC);
       else
               cfg80211_probe_status(rwnx_vif->ndev, mac, (u64)rwnx_vif->sta_probe.probe_id, 0, 0, false, GFP_ATOMIC);
#else
       if(found)
                cfg80211_probe_status(rwnx_vif->ndev, mac, (u64)rwnx_vif->sta_probe.probe_id, 1, GFP_ATOMIC);
        else
                cfg80211_probe_status(rwnx_vif->ndev, mac, (u64)rwnx_vif->sta_probe.probe_id, 0, GFP_ATOMIC);
#endif
       rwnx_vif->sta_probe.probe_id ++;
}

/**
 * @change_station: Modify a given station. Note that flags changes are not much
 *	validated in cfg80211, in particular the auth/assoc/authorized flags
 *	might come to the driver in invalid combinations -- make sure to check
 *	them, also against the existing state! Drivers must call
 *	cfg80211_check_station_change() to validate the information.
 */
static int rwnx_cfg80211_change_station(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *mac,
#else
	const u8 *mac,
#endif
	struct station_parameters *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif = netdev_priv(dev);
    struct rwnx_sta *sta;

    sta = rwnx_get_sta(rwnx_hw, mac);
    if (!sta)
    {
        /* Add the TDLS station */
        if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
        {
            struct rwnx_vif *rwnx_vif = netdev_priv(dev);
            struct me_sta_add_cfm me_sta_add_cfm;
            int error = 0;

            /* Indicate we are in a STA addition process - This will allow handling
             * potential PS mode change indications correctly
             */
            rwnx_hw->adding_sta = true;

            /* Forward the information to the LMAC */
            if ((error = rwnx_send_me_sta_add(rwnx_hw, params, mac, rwnx_vif->vif_index,
                                              &me_sta_add_cfm)))
                return error;

            // Check the status
            switch (me_sta_add_cfm.status)
            {
                case CO_OK:
                {
                    int tid;
                    sta = &rwnx_hw->sta_table[me_sta_add_cfm.sta_idx];
                    sta->aid = params->aid;
                    sta->sta_idx = me_sta_add_cfm.sta_idx;
                    sta->ch_idx = rwnx_vif->ch_index;
                    sta->vif_idx = rwnx_vif->vif_index;
                    sta->vlan_idx = sta->vif_idx;
                    sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
                    sta->ht = params->ht_capa ? 1 : 0;
                    sta->vht = params->vht_capa ? 1 : 0;
                    sta->acm = 0;
                    for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
                        int uapsd_bit = rwnx_hwq2uapsd[rwnx_tid2hwq[tid]];
                        if (params->uapsd_queues & uapsd_bit)
                            sta->uapsd_tids |= 1 << tid;
                        else
                            sta->uapsd_tids &= ~(1 << tid);
                    }
                    memcpy(sta->mac_addr, mac, ETH_ALEN);
#ifdef CONFIG_DEBUG_FS
                    rwnx_dbgfs_register_rc_stat(rwnx_hw, sta);
#endif
                    /* Ensure that we won't process PS change or channel switch ind*/
                    spin_lock_bh(&rwnx_hw->cb_lock);
                    rwnx_txq_sta_init(rwnx_hw, sta, rwnx_txq_vif_get_status(rwnx_vif));
                    if (rwnx_vif->tdls_status == TDLS_SETUP_RSP_TX) {
                        rwnx_vif->tdls_status = TDLS_LINK_ACTIVE;
                        sta->tdls.initiator = true;
                        sta->tdls.active = true;
                    }
                    /* Set TDLS channel switch capability */
                    if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
                        !rwnx_vif->tdls_chsw_prohibited)
                        sta->tdls.chsw_allowed = true;
                    rwnx_vif->sta.tdls_sta = sta;
                    sta->valid = true;
                    spin_unlock_bh(&rwnx_hw->cb_lock);
#ifdef CONFIG_RWNX_BFMER
                    if (rwnx_hw->mod_params->bfmer)
                        rwnx_send_bfmer_enable(rwnx_hw, sta, params->vht_capa);

                    rwnx_mu_group_sta_init(sta, NULL);
#endif /* CONFIG_RWNX_BFMER */

                    #define PRINT_STA_FLAG(f)                               \
                        (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "["#f"]" : "")

                    netdev_info(dev, "Add %s TDLS sta %d (%pM) flags=%s%s%s%s%s%s%s",
                                sta->tdls.initiator ? "initiator" : "responder",
                                sta->sta_idx, mac,
                                PRINT_STA_FLAG(AUTHORIZED),
                                PRINT_STA_FLAG(SHORT_PREAMBLE),
                                PRINT_STA_FLAG(WME),
                                PRINT_STA_FLAG(MFP),
                                PRINT_STA_FLAG(AUTHENTICATED),
                                PRINT_STA_FLAG(TDLS_PEER),
                                PRINT_STA_FLAG(ASSOCIATED));
                    #undef PRINT_STA_FLAG

                    break;
                }
                default:
                    error = -EBUSY;
                    break;
            }

            rwnx_hw->adding_sta = false;
        } else  {
            return -EINVAL;
        }
    }

    if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))
        rwnx_send_me_set_control_port_req(rwnx_hw,
                (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) != 0,
                sta->sta_idx);

    if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
        if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
            if (params->plink_state < NUM_NL80211_PLINK_STATES) {
                rwnx_send_mesh_peer_update_ntf(rwnx_hw, vif, sta->sta_idx, params->plink_state);
            }
        }

        if (params->local_pm != NL80211_MESH_POWER_UNKNOWN) {
            sta->mesh_pm = params->local_pm;
            rwnx_update_mesh_power_mode(vif);
        }
    }

    if (params->vlan) {
        uint8_t vlan_idx;

        vif = netdev_priv(params->vlan);
        vlan_idx = vif->vif_index;

        if (sta->vlan_idx != vlan_idx) {
            struct rwnx_vif *old_vif;
            old_vif = rwnx_hw->vif_table[sta->vlan_idx];
            rwnx_txq_sta_switch_vif(sta, old_vif, vif);
            sta->vlan_idx = vlan_idx;

            if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP_VLAN) &&
                (vif->use_4addr)) {
                WARN((vif->ap_vlan.sta_4a),
                     "4A AP_VLAN interface with more than one sta");
                vif->ap_vlan.sta_4a = sta;
            }

            if ((RWNX_VIF_TYPE(old_vif) == NL80211_IFTYPE_AP_VLAN) &&
                (old_vif->use_4addr)) {
                old_vif->ap_vlan.sta_4a = NULL;
            }
        }
    }

    return 0;
}

/**
 * @start_ap: Start acting in AP mode defined by the parameters.
 */
static int rwnx_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *dev,
                                  struct cfg80211_ap_settings *settings)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct apm_start_cfm apm_start_cfm;
    struct rwnx_ipc_elem_var elem;
    struct rwnx_sta *sta;
    int error = 0;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    INIT_WORK(&rwnx_vif->sta_probe.apmprobestaWork, apm_probe_sta_work_process);
    rwnx_vif->sta_probe.apmprobesta_wq = create_singlethread_workqueue("apmprobe_wq");
    if (!rwnx_vif->sta_probe.apmprobesta_wq) {
	    txrx_err("insufficient memory to create apmprobe_wq.\n");
	    return -ENOBUFS;
    }
    if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO)
	    rwnx_hw->is_p2p_connected = 1;
    /* Forward the information to the LMAC */
    if ((error = rwnx_send_apm_start_req(rwnx_hw, rwnx_vif, settings,
                                         &apm_start_cfm, &elem)))
        goto end;

    // Check the status
    switch (apm_start_cfm.status)
    {
        case CO_OK:
        {
            u8 txq_status = 0;
            rwnx_vif->ap.bcmc_index = apm_start_cfm.bcmc_idx;
            rwnx_vif->ap.flags = 0;
            #if (defined CONFIG_HE_FOR_OLD_KERNEL) || (defined CONFIG_VHT_FOR_OLD_KERNEL)
            rwnx_vif->ap.aic_index = 0;
            #endif
            sta = &rwnx_hw->sta_table[apm_start_cfm.bcmc_idx];
            sta->valid = true;
            sta->aid = 0;
            sta->sta_idx = apm_start_cfm.bcmc_idx;
            sta->ch_idx = apm_start_cfm.ch_idx;
            sta->vif_idx = rwnx_vif->vif_index;
            sta->qos = false;
            sta->acm = 0;
            sta->ps.active = false;
            rwnx_mu_group_sta_init(sta, NULL);
            spin_lock_bh(&rwnx_hw->cb_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
	    rwnx_chanctx_link(rwnx_vif, apm_start_cfm.ch_idx,
                              NULL);
#else
            rwnx_chanctx_link(rwnx_vif, apm_start_cfm.ch_idx,
                              &settings->chandef);
#endif
            if (rwnx_hw->cur_chanctx != apm_start_cfm.ch_idx) {
                txq_status = RWNX_TXQ_STOP_CHAN;
            }
            rwnx_txq_vif_init(rwnx_hw, rwnx_vif, txq_status);
            spin_unlock_bh(&rwnx_hw->cb_lock);

            netif_tx_start_all_queues(dev);
            netif_carrier_on(dev);
            error = 0;
            /* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
            if (txq_status == 0) {
                rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
            }
            break;
        }
        case CO_BUSY:
            error = -EINPROGRESS;
            break;
        case CO_OP_IN_PROGRESS:
            error = -EALREADY;
            break;
        default:
            error = -EIO;
            break;
    }

    if (error) {
        netdev_info(dev, "Failed to start AP (%d)", error);
    } else {
        netdev_info(dev, "AP started: ch=%d, bcmc_idx=%d channel=%d bw=%d",
                    rwnx_vif->ch_index, rwnx_vif->ap.bcmc_index,
                    ((settings->chandef).chan)->center_freq,
                    ((settings->chandef).width));
    }

  end:
    //rwnx_ipc_elem_var_deallocs(rwnx_hw, &elem);

    return error;
}


/**
 * @change_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when AP mode wasn't started.
 */
static int rwnx_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
                                       struct cfg80211_beacon_data *info)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif = netdev_priv(dev);
    struct rwnx_bcn *bcn = &vif->ap.bcn;
    struct rwnx_ipc_elem_var elem;
    u8 *buf;
    int error = 0;
    RWNX_DBG(RWNX_FN_ENTRY_STR);

	//elem init
	elem.dma_addr = 0;

    // Build the beacon
    buf = rwnx_build_bcn(bcn, info);
    if (!buf)
        return -ENOMEM;

    rwnx_send_bcn(rwnx_hw, buf, vif->vif_index, bcn->len);

#if 0
    // Sync buffer for FW
    if ((error = rwnx_ipc_elem_var_allocs(rwnx_hw, &elem, bcn->len, DMA_TO_DEVICE,
                                          buf, NULL, NULL)))
        return error;
#endif
    // Forward the information to the LMAC
    error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, elem.dma_addr,
                                 bcn->len, bcn->head_len, bcn->tim_len, NULL);

#if 0
    rwnx_ipc_elem_var_deallocs(rwnx_hw, &elem);
#else


#endif
    return error;
}

/**
 * * @stop_ap: Stop being an AP, including stopping beaconing.
 */
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_sta *sta;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    netif_tx_stop_all_queues(dev);
    netif_carrier_off(dev);

    if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_P2P_GO)
        rwnx_hw->is_p2p_connected = 0;
    rwnx_radar_cancel_cac(&rwnx_hw->radar);
    rwnx_send_apm_stop_req(rwnx_hw, rwnx_vif);
    spin_lock_bh(&rwnx_hw->cb_lock);
    rwnx_chanctx_unlink(rwnx_vif);
    spin_unlock_bh(&rwnx_hw->cb_lock);

    /* delete any remaining STA*/
    while (!list_empty(&rwnx_vif->ap.sta_list)) {
        rwnx_cfg80211_del_station_compat(wiphy, dev, NULL);
    }

    /* delete BC/MC STA */
    sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
    rwnx_txq_vif_deinit(rwnx_hw, rwnx_vif);
    rwnx_del_bcn(&rwnx_vif->ap.bcn);
    rwnx_del_csa(rwnx_vif);

    flush_workqueue(rwnx_vif->sta_probe.apmprobesta_wq);
    destroy_workqueue(rwnx_vif->sta_probe.apmprobesta_wq);

    netdev_info(dev, "AP Stopped");

    return 0;
}

/**
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *	interfaces are active this callback should reject the configuration.
 *	If no interfaces are active or the device is down, the channel should
 *	be stored for when a monitor interface becomes active.
 *
 * Also called internaly with chandef set to NULL simply to retrieve the channel
 * configured at firmware level.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static inline bool
cfg80211_chandef_identical(const struct cfg80211_chan_def *chandef1,
			   const struct cfg80211_chan_def *chandef2)
{
	return (chandef1->chan == chandef2->chan &&
		chandef1->width == chandef2->width &&
		chandef1->center_freq1 == chandef2->center_freq1 &&
		chandef1->center_freq2 == chandef2->center_freq2);
}
#endif

static int rwnx_cfg80211_set_monitor_channel(struct wiphy *wiphy,
                                             struct cfg80211_chan_def *chandef)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif;
    struct me_config_monitor_cfm cfm;
    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (rwnx_hw->monitor_vif == RWNX_INVALID_VIF)
        return -EINVAL;

    rwnx_vif = rwnx_hw->vif_table[rwnx_hw->monitor_vif];

    // Do nothing if monitor interface is already configured with the requested channel
    if (rwnx_chanctx_valid(rwnx_hw, rwnx_vif->ch_index)) {
        struct rwnx_chanctx *ctxt;
        ctxt = &rwnx_vif->rwnx_hw->chanctx_table[rwnx_vif->ch_index];
        if (chandef && cfg80211_chandef_identical(&ctxt->chan_def, chandef))
            return 0;
    }

    // Always send command to firmware. It allows to retrieve channel context index
    // and its configuration.
    if (rwnx_send_config_monitor_req(rwnx_hw, chandef, &cfm))
        return -EIO;

    // Always re-set channel context info
    rwnx_chanctx_unlink(rwnx_vif);



    // If there is also a STA interface not yet connected then monitor interface
    // will only have a channel context after the connection of the STA interface.
    if (cfm.chan_index != RWNX_CH_NOT_SET)
    {
        struct cfg80211_chan_def mon_chandef;

        if (rwnx_hw->vif_started > 1) {
            // In this case we just want to update the channel context index not
            // the channel configuration
            rwnx_chanctx_link(rwnx_vif, cfm.chan_index, NULL);
            return -EBUSY;
        }

        mon_chandef.chan = ieee80211_get_channel(wiphy, cfm.chan.prim20_freq);
        mon_chandef.center_freq1 = cfm.chan.center1_freq;
        mon_chandef.center_freq2 = cfm.chan.center2_freq;
        mon_chandef.width =  chnl2bw[cfm.chan.type];
        rwnx_chanctx_link(rwnx_vif, cfm.chan_index, &mon_chandef);
    }

    return 0;
}

/**
 * @probe_client: probe an associated client, must return a cookie that it
 *	later passes to cfg80211_probe_status().
 */
int rwnx_cfg80211_probe_client(struct wiphy *wiphy, struct net_device *dev,
            const u8 *peer, u64 *cookie)
{
    //struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif = netdev_priv(dev);
    struct rwnx_sta *sta = NULL;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if((RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP) && (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_P2P_GO) &&
            (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP_VLAN))
            return -EINVAL;

	spin_lock_bh(&vif->rwnx_hw->cb_lock);
    list_for_each_entry(sta, &vif->ap.sta_list, list){
        if (sta->valid && ether_addr_equal(sta->mac_addr, peer))
            break;
    }
	spin_unlock_bh(&vif->rwnx_hw->cb_lock);

    if (!sta)
        return -ENOENT;


    memcpy(vif->sta_probe.sta_mac_addr, peer, 6);
    queue_work(vif->sta_probe.apmprobesta_wq, &vif->sta_probe.apmprobestaWork);

    *cookie = vif->sta_probe.probe_id;

    return 0;
}

/**
 * @mgmt_frame_register: Notify driver that a management frame type was
 *	registered. Note that this callback may not sleep, and cannot run
 *	concurrently with itself.
 */
void rwnx_cfg80211_mgmt_frame_register(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
                   struct net_device *dev,
#else
                   struct wireless_dev *wdev,
#endif
                   u16 frame_type, bool reg)
{
}

/**
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 */
static int rwnx_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
    return 0;
}


/**
 * @set_tx_power: set the transmit power according to the parameters,
 *	the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *	wdev may be %NULL if power was set for the wiphy, and will
 *	always be %NULL unless the driver supports per-vif TX power
 *	(as advertised by the nl80211 feature flag.)
 */
static int rwnx_cfg80211_set_tx_power(struct wiphy *wiphy,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
 struct wireless_dev *wdev,
#endif
                                      enum nl80211_tx_power_setting type, int mbm)
{
    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
    struct wireless_dev *wdev = NULL;
    #endif
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif;
    s8 pwr;
    int res = 0;

    if (type == NL80211_TX_POWER_AUTOMATIC) {
        pwr = 0x7f;
    } else {
        pwr = MBM_TO_DBM(mbm);
    }

    if (wdev) {
        vif = container_of(wdev, struct rwnx_vif, wdev);
        res = rwnx_send_set_power(rwnx_hw, vif->vif_index, pwr, NULL);
    } else {
        list_for_each_entry(vif, &rwnx_hw->vifs, list) {
            res = rwnx_send_set_power(rwnx_hw, vif->vif_index, pwr, NULL);
            if (res)
                break;
        }
    }

    return res;
}

#if 0
/**
 * @set_power_mgmt: set the power save to one of those two modes:
 *  Power-save off
 *  Power-save on - Dynamic mode
 */
static int rwnx_cfg80211_set_power_mgmt(struct wiphy *wiphy,
                                        struct net_device *dev,
                                        bool enabled, int timeout)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    u8 ps_mode;

    RWNX_DBG(RWNX_FN_ENTRY_STR);
    if (timeout >= 0)
        netdev_info(dev, "Ignore timeout value %d", timeout);

    if (!(rwnx_hw->version_cfm.features & BIT(MM_FEAT_PS_BIT)))
        enabled = false;

    if (enabled) {
        /* Switch to Dynamic Power Save */
        ps_mode = MM_PS_MODE_ON_DYN;
    } else {
        /* Exit Power Save */
        ps_mode = MM_PS_MODE_OFF;
    }

    return rwnx_send_me_set_ps_mode(rwnx_hw, ps_mode);
}
#endif

static int rwnx_cfg80211_set_txq_params(struct wiphy *wiphy, struct net_device *dev,
                                        struct ieee80211_txq_params *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    u8 hw_queue, aifs, cwmin, cwmax;
    u32 param;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
    hw_queue = rwnx_ac2hwq[0][params->queue];
#else
    hw_queue = rwnx_ac2hwq[0][params->ac];
#endif

    aifs  = params->aifs;
    cwmin = fls(params->cwmin);
    cwmax = fls(params->cwmax);

    /* Store queue information in general structure */
    param  = (u32) (aifs << 0);
    param |= (u32) (cwmin << 4);
    param |= (u32) (cwmax << 8);
    param |= (u32) (params->txop) << 12;

    /* Send the MM_SET_EDCA_REQ message to the FW */
    return rwnx_send_set_edca(rwnx_hw, hw_queue, param, false, rwnx_vif->vif_index);
}


/**
 * @remain_on_channel: Request the driver to remain awake on the specified
 *	channel for the specified duration to complete an off-channel
 *	operation (e.g., public action frame exchange). When the driver is
 *	ready on the requested channel, it must indicate this with an event
 *	notification by calling cfg80211_ready_on_channel().
 */
static int
rwnx_cfg80211_remain_on_channel(struct wiphy *wiphy,
                            #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
                                struct wireless_dev *wdev,
                            #else
                                struct net_device *dev,
                            #endif
                                struct ieee80211_channel *chan,
                            #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
                                enum nl80211_channel_type channel_type,
                            #endif
                                unsigned int duration, u64 *cookie)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
    struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
#else
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct wireless_dev *wdev = &rwnx_vif->wdev;
#endif
    struct rwnx_roc_elem *roc_elem;
    struct mm_add_if_cfm add_if_cfm;
    struct mm_remain_on_channel_cfm roc_cfm;
    int error;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    /* For debug purpose (use ftrace kernel option) */
#ifdef CREATE_TRACE_POINTS
    trace_roc(rwnx_vif->vif_index, chan->center_freq, duration);
#endif

#if 1// Fix up layer to send 50ms duration.
	if(duration < 100){
		AICWFDBG(LOGINFO, "%s duration time change to 200ms \r\n", __func__);
		duration = 200;
	}
#endif

    /* Check that no other RoC procedure has been launched */
    if (rwnx_hw->roc_elem) {
        msleep(2);
        if (rwnx_hw->roc_elem) {
			AICWFDBG(LOGERROR, "remain_on_channel fail\n");
#if 0//AIDEN test
			roc_elem = rwnx_hw->roc_elem;
			kfree(roc_elem);
			rwnx_hw->roc_elem = NULL;
			//msleep(500);
#else
            return -EBUSY;
#endif
        }
    }
	//msleep(500);
	AICWFDBG(LOGINFO, "remain:%d,%d,%d,duration:%d\n",rwnx_vif->vif_index, rwnx_vif->is_p2p_vif, rwnx_hw->is_p2p_alive, duration);
	//if (rwnx_vif->is_p2p_vif) {
	AICWFDBG(LOGINFO, "rwnx_vif:%p rwnx_hw->p2p_dev_vif:%p rwnx_vif->up:%d\r\n",
			rwnx_vif, rwnx_hw->p2p_dev_vif, rwnx_vif->up);
#ifdef CONFIG_USE_P2P0
    if (rwnx_vif->is_p2p_vif) {
#else
    if (rwnx_vif == rwnx_hw->p2p_dev_vif && !rwnx_vif->up) {
#endif
		if (!rwnx_hw->is_p2p_alive) {
			error = rwnx_send_add_if (rwnx_hw, rwnx_vif->wdev.address, //wdev->netdev->dev_addr,
								  RWNX_VIF_TYPE(rwnx_vif), false, &add_if_cfm);
			if (error)
				return -EIO;

			if (add_if_cfm.status != 0) {
				return -EIO;
			}
            /* Save the index retrieved from LMAC */
            spin_lock_bh(&rwnx_hw->cb_lock);
            rwnx_vif->vif_index = add_if_cfm.inst_nbr;
            rwnx_vif->up = true;
            rwnx_hw->vif_started++;
            rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
            spin_unlock_bh(&rwnx_hw->cb_lock);
            rwnx_hw->is_p2p_alive = 1;
            mod_timer(&rwnx_hw->p2p_alive_timer, jiffies + msecs_to_jiffies(1000));
            atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);
        }
        else {
            mod_timer(&rwnx_hw->p2p_alive_timer, jiffies + msecs_to_jiffies(1000));
            atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);
        }
    }

    /* Allocate a temporary RoC element */
    roc_elem = kmalloc(sizeof(struct rwnx_roc_elem), GFP_KERNEL);

    /* Verify that element has well been allocated */
    if (!roc_elem) {
        return -ENOMEM;
    }

    /* Initialize the RoC information element */
    roc_elem->wdev = wdev;
    roc_elem->chan = chan;
    roc_elem->duration = duration;
    roc_elem->mgmt_roc = false;
    roc_elem->on_chan = false;

    /* Initialize the OFFCHAN TX queue to allow off-channel transmissions */
    rwnx_txq_offchan_init(rwnx_vif);

    /* Forward the information to the FMAC */
    rwnx_hw->roc_elem = roc_elem;
    error = rwnx_send_roc(rwnx_hw, rwnx_vif, chan, duration, &roc_cfm);

    /* If no error, keep all the information for handling of end of procedure */
    if (error == 0) {

        /* Set the cookie value */
        *cookie = (u64)(rwnx_hw->roc_cookie_cnt);
        if(roc_cfm.status) {
            // failed to roc
            rwnx_hw->roc_elem = NULL;
            kfree(roc_elem);
            rwnx_txq_offchan_deinit(rwnx_vif);
            return -EBUSY;
        }
    } else {
        /* Free the allocated element */
        rwnx_hw->roc_elem = NULL;
        kfree(roc_elem);
        rwnx_txq_offchan_deinit(rwnx_vif);
    }

    return error;
}

/**
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *	This allows the operation to be terminated prior to timeout based on
 *	the duration value.
 */
static int rwnx_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
                                            #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
                                                  struct net_device *dev,
                                            #else
                                                  struct wireless_dev *wdev,
                                            #endif
                                                  u64 cookie)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#ifdef CREATE_TRACE_POINTS
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
#else
    struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
#endif
#endif
    RWNX_DBG(RWNX_FN_ENTRY_STR);
#ifdef CREATE_TRACE_POINTS
    /* For debug purpose (use ftrace kernel option) */
    trace_cancel_roc(rwnx_vif->vif_index);
#endif
    /* Check if a RoC procedure is pending */
    if (!rwnx_hw->roc_elem) {
        return 0;
    }

    /* Forward the information to the FMAC */
    return rwnx_send_cancel_roc(rwnx_hw);
}

/**
 * @dump_survey: get site survey information.
 */
static int rwnx_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *netdev,
                                     int idx, struct survey_info *info)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct ieee80211_supported_band *sband;
    struct rwnx_survey_info *rwnx_survey;

    //RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (idx >= ARRAY_SIZE(rwnx_hw->survey))
        return -ENOENT;

    rwnx_survey = &rwnx_hw->survey[idx];

    // Check if provided index matches with a supported 2.4GHz channel
    sband = wiphy->bands[NL80211_BAND_2GHZ];
    if (sband && idx >= sband->n_channels) {
        idx -= sband->n_channels;
        sband = NULL;
    }

	//#ifdef USE_5G
	if (rwnx_hw->band_5g_support) {
	    if (!sband) {
	        // Check if provided index matches with a supported 5GHz channel
	        sband = wiphy->bands[NL80211_BAND_5GHZ];

	        if (!sband || idx >= sband->n_channels)
	            return -ENOENT;
	    }
	}else{
	//#else
		if (!sband || idx >= sband->n_channels)
            return -ENOENT;
	}
	//#endif

    // Fill the survey
    info->channel = &sband->channels[idx];
    info->filled = rwnx_survey->filled;

    if (rwnx_survey->filled != 0) {
        SURVEY_TIME(info) = (u64)rwnx_survey->chan_time_ms;
        SURVEY_TIME_BUSY(info) = (u64)rwnx_survey->chan_time_busy_ms;
        info->noise = rwnx_survey->noise_dbm;

        // Set the survey report as not used
        rwnx_survey->filled = 0;
    }

    return 0;
}

/**
 * @get_channel: Get the current operating channel for the virtual interface.
 *	For monitor interfaces, it should return %NULL unless there's a single
 *	current monitoring channel.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static int rwnx_cfg80211_get_channel(struct wiphy *wiphy,
                                     struct wireless_dev *wdev,
                                     struct cfg80211_chan_def *chandef) {
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
    struct rwnx_chanctx *ctxt;

    if (!rwnx_vif->up) {
        return -ENODATA;
    }

    if (rwnx_vif->vif_index == rwnx_hw->monitor_vif)
    {
        //retrieve channel from firmware
        rwnx_cfg80211_set_monitor_channel(wiphy, NULL);
    }

    //Check if channel context is valid
    if (!rwnx_chanctx_valid(rwnx_hw, rwnx_vif->ch_index)){
        return -ENODATA;
    }

    ctxt = &rwnx_hw->chanctx_table[rwnx_vif->ch_index];
    *chandef = ctxt->chan_def;

    return 0;
}
#else
struct ieee80211_channel *rwnx_cfg80211_get_channel(struct wiphy *wiphy)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct ieee80211_channel *chan = NULL;
    struct rwnx_vif *vif;
    bool_l found = false;
    //may TBD

    list_for_each_entry(vif, &rwnx_hw->vifs, list)
    {
        if(vif->wdev.iftype == NL80211_IFTYPE_AP)
        {
            found = true;
            break;
        }
    }

    if(found && rwnx_hw->set_chan.center_freq) {
        chan = kzalloc(sizeof(struct ieee80211_channel), GFP_KERNEL);
        memcpy((u8 *)chan, (u8 *)&rwnx_hw->set_chan, sizeof(struct ieee80211_channel));
    }

    return chan;
}
#endif


/**
 * @mgmt_tx: Transmit a management frame.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
static int rwnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
                                 struct cfg80211_mgmt_tx_params *params,
                                 u64 *cookie)
#else
static int rwnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
                                 struct ieee80211_channel *channel, bool offchan,
                                 unsigned int wait, const u8* buf, size_t len,
                            #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
                                 bool no_cck,
                            #endif
                            #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
                                 bool dont_wait_for_ack,
                            #endif
                                 u64 *cookie)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct wireless_dev *wdev = &rwnx_vif->wdev;
#else
    struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
#endif
    struct rwnx_sta *rwnx_sta = NULL;
    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    struct ieee80211_channel *channel = params->chan;
    const u8 *buf = params->buf;
    //size_t len = params->len;
    #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
    struct ieee80211_mgmt *mgmt = (void *)buf;
    bool ap = false;
    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    bool offchan = false;
    #endif

    /* Check if provided VIF is an AP or a STA one */
    switch (RWNX_VIF_TYPE(rwnx_vif)) {
        case NL80211_IFTYPE_AP_VLAN:
            rwnx_vif = rwnx_vif->ap_vlan.master;
        case NL80211_IFTYPE_AP:
        case NL80211_IFTYPE_P2P_GO:
        case NL80211_IFTYPE_MESH_POINT:
            ap = true;
            break;
        case NL80211_IFTYPE_STATION:
        case NL80211_IFTYPE_P2P_CLIENT:
        default:
            break;
    }


    /* Get STA on which management frame has to be sent */
    rwnx_sta = rwnx_retrieve_sta(rwnx_hw, rwnx_vif, mgmt->da,
                                 mgmt->frame_control, ap);

	AICWFDBG(LOGDEBUG, "%s rwnx_sta:%p RWNX_VIF_TYPE(rwnx_vif):%d \r\n", __func__, rwnx_sta, RWNX_VIF_TYPE(rwnx_vif));

#ifdef CREATE_TRACE_POINTS
    trace_mgmt_tx((channel) ? channel->center_freq : 0,
                  rwnx_vif->vif_index, (rwnx_sta) ? rwnx_sta->sta_idx : 0xFF,
                  mgmt);
#endif
    if (ap || rwnx_sta)
        goto send_frame;

    /* Not an AP interface sending frame to unknown STA:
     * This is allowed for external authetication */
    if (rwnx_vif->sta.external_auth && ieee80211_is_auth(mgmt->frame_control))
        goto send_frame;

    /* Otherwise ROC is needed */
    if (!channel) {
		AICWFDBG(LOGERROR, "mgmt_tx fail since channel\n");
        return -EINVAL;
    }

    /* Check that a RoC is already pending */
    if (rwnx_hw->roc_elem) {
        /* Get VIF used for current ROC */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
        struct rwnx_vif *rwnx_roc_vif = netdev_priv(rwnx_hw->roc_elem->wdev->netdev);
#else
        struct rwnx_vif *rwnx_roc_vif = container_of(rwnx_hw->roc_elem->wdev, struct rwnx_vif, wdev);//netdev_priv(rwnx_hw->roc_elem->wdev->netdev);
#endif

        /* Check if RoC channel is the same than the required one */
        if ((rwnx_hw->roc_elem->chan->center_freq != channel->center_freq)
            || (rwnx_vif->vif_index != rwnx_roc_vif->vif_index)) {
            AICWFDBG(LOGERROR, "mgmt rx chan invalid: %d, %d", rwnx_hw->roc_elem->chan->center_freq, channel->center_freq);
            return -EINVAL;
        }
    } else {
        u64 cookie;
        int error;

		AICWFDBG(LOGINFO, "mgmt rx remain on chan\n");

        /* Start a ROC procedure for 30ms */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
        error = rwnx_cfg80211_remain_on_channel(wiphy, wdev, channel,
                                                30, &cookie);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
        error = rwnx_cfg80211_remain_on_channel(wiphy, wdev, channel, NL80211_CHAN_NO_HT,
                                                30, &cookie);
#else
        error = rwnx_cfg80211_remain_on_channel(wiphy, dev, channel, NL80211_CHAN_NO_HT,
                                                30, &cookie);
#endif

        if (error) {
			AICWFDBG(LOGERROR, "mgmt rx chan err\n");
            return error;
        }
        /* Need to keep in mind that RoC has been launched internally in order to
         * avoid to call the cfg80211 callback once expired */
        rwnx_hw->roc_elem->mgmt_roc = true;
    }

    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    offchan = true;
    #endif

send_frame:
    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
    return rwnx_start_mgmt_xmit(rwnx_vif, rwnx_sta, params, offchan, cookie);
    #else
    return rwnx_start_mgmt_xmit(rwnx_vif, rwnx_sta, channel, offchan, wait, buf, len, no_cck, dont_wait_for_ack, cookie);
    #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
}

/**
 * @start_radar_detection: Start radar detection in the driver.
 */
static
int rwnx_cfg80211_start_radar_detection(struct wiphy *wiphy,
                                        struct net_device *dev,
                                        struct cfg80211_chan_def *chandef
                                    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
                                        , u32 cac_time_ms
                                    #endif
                                        )
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct apm_start_cac_cfm cfm;

    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
    rwnx_radar_start_cac(&rwnx_hw->radar, cac_time_ms, rwnx_vif);
    #endif
    rwnx_send_apm_start_cac_req(rwnx_hw, rwnx_vif, chandef, &cfm);

    if (cfm.status == CO_OK) {
        spin_lock_bh(&rwnx_hw->cb_lock);
        rwnx_chanctx_link(rwnx_vif, cfm.ch_idx, chandef);
        if (rwnx_hw->cur_chanctx == rwnx_vif->ch_index)
            rwnx_radar_detection_enable(&rwnx_hw->radar,
                                        RWNX_RADAR_DETECT_REPORT,
                                        RWNX_RADAR_RIU);
        spin_unlock_bh(&rwnx_hw->cb_lock);
    } else {
        return -EIO;
    }

    return 0;
}

/**
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *	driver. If the SME is in the driver/firmware, this information can be
 *	used in building Authentication and Reassociation Request frames.
 */
static
int rwnx_cfg80211_update_ft_ies(struct wiphy *wiphy,
                            struct net_device *dev,
                            struct cfg80211_update_ft_ies_params *ftie)
{
    return 0;
}

/**
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 */
static
int rwnx_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
                                  struct net_device *dev,
                                  int32_t rssi_thold, uint32_t rssi_hyst)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);

    return rwnx_send_cfg_rssi_req(rwnx_hw, rwnx_vif->vif_index, rssi_thold, rssi_hyst);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
/**
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *	responsible for veryfing if the switch is possible. Since this is
 *	inherently tricky driver may decide to disconnect an interface later
 *	with cfg80211_stop_iface(). This doesn't mean driver can accept
 *	everything. It should do it's best to verify requests and reject them
 *	as soon as possible.
 */
int rwnx_cfg80211_channel_switch(struct wiphy *wiphy,
                                 struct net_device *dev,
                                 struct cfg80211_csa_settings *params)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *vif = netdev_priv(dev);
    struct rwnx_ipc_elem_var elem;
    struct rwnx_bcn *bcn, *bcn_after;
    struct rwnx_csa *csa;
    u16 csa_oft[BCN_MAX_CSA_CPT];
    u8 *buf;
    int i, error = 0;

	//elem init
	elem.dma_addr = 0;

    if (vif->ap.csa)
        return -EBUSY;

    if (params->n_counter_offsets_beacon > BCN_MAX_CSA_CPT)
        return -EINVAL;

    /* Build the new beacon with CSA IE */
    bcn = &vif->ap.bcn;
    buf = rwnx_build_bcn(bcn, &params->beacon_csa);
    if (!buf)
        return -ENOMEM;

    memset(csa_oft, 0, sizeof(csa_oft));
    for (i = 0; i < params->n_counter_offsets_beacon; i++)
    {
        csa_oft[i] = params->counter_offsets_beacon[i] + bcn->head_len +
            bcn->tim_len;
    }

    /* If count is set to 0 (i.e anytime after this beacon) force it to 2 */
    if (params->count == 0) {
        params->count = 2;
        for (i = 0; i < params->n_counter_offsets_beacon; i++)
        {
            buf[csa_oft[i]] = 2;
        }
    }

    #if 0
    if ((error = rwnx_ipc_elem_var_allocs(rwnx_hw, &elem, bcn->len,
                                          DMA_TO_DEVICE, buf, NULL, NULL))) {
        goto end;
    }
    #else
    error = rwnx_send_bcn(rwnx_hw, buf, vif->vif_index, bcn->len);
    if (error) {
        goto end;
    }
    #endif

    /* Build the beacon to use after CSA. It will only be sent to fw once
       CSA is over, but do it before sending the beacon as it must be ready
       when CSA is finished. */
    csa = kzalloc(sizeof(struct rwnx_csa), GFP_KERNEL);
    if (!csa) {
        error = -ENOMEM;
        goto end;
    }

    bcn_after = &csa->bcn;
    buf = rwnx_build_bcn(bcn_after, &params->beacon_after);
    if (!buf) {
        error = -ENOMEM;
        rwnx_del_csa(vif);
        goto end;
    }

    vif->ap.csa = csa;
    csa->vif = vif;
    csa->chandef = params->chandef;

    /* Send new Beacon. FW will extract channel and count from the beacon */
    error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, elem.dma_addr,
                                 bcn->len, bcn->head_len, bcn->tim_len, csa_oft);

    if (error) {
        rwnx_del_csa(vif);
        goto end;
    } else {
        INIT_WORK(&csa->work, rwnx_csa_finish);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
        cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count, false);
#else
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count);
#endif
    }

  end:
    return error;
}
#endif

#if 0
/*
 * @tdls_mgmt: prepare TDLS action frame packets and forward them to FW
 */

static int
rwnx_cfg80211_tdls_mgmt(struct wiphy *wiphy,
	struct net_device *dev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	const u8 *peer,
#else
	u8 *peer,
#endif
	u8 action_code,
	u8 dialog_token,
	u16 status_code,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	u32 peer_capability,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
	bool initiator,
#endif
	const u8 *buf,
	size_t len)
{
    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
    u32 peer_capability = 0;
    #endif
    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    bool initiator = false;
    #endif
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    int ret = 0;

    /* make sure we support TDLS */
    if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
        return -ENOTSUPP;

    /* make sure we are in station mode (and connected) */
    if ((RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION) ||
        (!rwnx_vif->up) || (!rwnx_vif->sta.ap))
        return -ENOTSUPP;

    /* only one TDLS link is supported */
    if ((action_code == WLAN_TDLS_SETUP_REQUEST) &&
        (rwnx_vif->sta.tdls_sta) &&
        (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
        printk("%s: only one TDLS link is supported!\n", __func__);
        return -ENOTSUPP;
    }

    if ((action_code == WLAN_TDLS_DISCOVERY_REQUEST) &&
        (rwnx_hw->mod_params->ps_on)) {
        printk("%s: discovery request is not supported when "
                "power-save is enabled!\n", __func__);
        return -ENOTSUPP;
    }

    switch (action_code) {
    case WLAN_TDLS_SETUP_RESPONSE:
        /* only one TDLS link is supported */
        if ((status_code == 0) &&
            (rwnx_vif->sta.tdls_sta) &&
            (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
            printk("%s: only one TDLS link is supported!\n", __func__);
            status_code = WLAN_STATUS_REQUEST_DECLINED;
        }
        /* fall-through */
    case WLAN_TDLS_SETUP_REQUEST:
    case WLAN_TDLS_TEARDOWN:
    case WLAN_TDLS_DISCOVERY_REQUEST:
    case WLAN_TDLS_SETUP_CONFIRM:
    case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
        ret = rwnx_tdls_send_mgmt_packet_data(rwnx_hw, rwnx_vif, peer, action_code,
                dialog_token, status_code, peer_capability, initiator, buf, len, 0, NULL);
        break;

    default:
        printk("%s: Unknown TDLS mgmt/action frame %pM\n",
                __func__, peer);
        ret = -EOPNOTSUPP;
        break;
    }

    if (action_code == WLAN_TDLS_SETUP_REQUEST) {
        rwnx_vif->tdls_status = TDLS_SETUP_REQ_TX;
    } else if (action_code == WLAN_TDLS_SETUP_RESPONSE) {
        rwnx_vif->tdls_status = TDLS_SETUP_RSP_TX;
    } else if ((action_code == WLAN_TDLS_SETUP_CONFIRM) && (ret == CO_OK)) {
        rwnx_vif->tdls_status = TDLS_LINK_ACTIVE;
        /* Set TDLS active */
        rwnx_vif->sta.tdls_sta->tdls.active = true;
    }

    return ret;
}
#endif
#if 0
/*
 * @tdls_oper: execute TDLS operation
 */
static int
rwnx_cfg80211_tdls_oper(struct wiphy *wiphy,
	struct net_device *dev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	const u8 *peer,
#else
	u8 *peer,
#endif
	enum nl80211_tdls_operation oper
)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    int error;

    if (oper != NL80211_TDLS_DISABLE_LINK)
        return 0;

    if (!rwnx_vif->sta.tdls_sta) {
        printk("%s: TDLS station %pM does not exist\n", __func__, peer);
        return -ENOLINK;
    }

    if (memcmp(rwnx_vif->sta.tdls_sta->mac_addr, peer, ETH_ALEN) == 0) {
        /* Disable Channel Switch */
        if (!rwnx_send_tdls_cancel_chan_switch_req(rwnx_hw, rwnx_vif,
                                                   rwnx_vif->sta.tdls_sta,
                                                   NULL))
            rwnx_vif->sta.tdls_sta->tdls.chsw_en = false;

        netdev_info(dev, "Del TDLS sta %d (%pM)",
                rwnx_vif->sta.tdls_sta->sta_idx,
                rwnx_vif->sta.tdls_sta->mac_addr);
        /* Ensure that we won't process PS change ind */
        spin_lock_bh(&rwnx_hw->cb_lock);
        rwnx_vif->sta.tdls_sta->ps.active = false;
        rwnx_vif->sta.tdls_sta->valid = false;
        spin_unlock_bh(&rwnx_hw->cb_lock);
        rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.tdls_sta);
        error = rwnx_send_me_sta_del(rwnx_hw, rwnx_vif->sta.tdls_sta->sta_idx, true);
        if ((error != 0) && (error != -EPIPE))
            return error;

#ifdef CONFIG_RWNX_BFMER
            // Disable Beamformer if supported
            rwnx_bfmer_report_del(rwnx_hw, rwnx_vif->sta.tdls_sta);
            rwnx_mu_group_sta_del(rwnx_hw, rwnx_vif->sta.tdls_sta);
#endif /* CONFIG_RWNX_BFMER */

        /* Set TDLS not active */
        rwnx_vif->sta.tdls_sta->tdls.active = false;
#ifdef CONFIG_DEBUG_FS
        rwnx_dbgfs_unregister_rc_stat(rwnx_hw, rwnx_vif->sta.tdls_sta);
#endif
        // Remove TDLS station
        rwnx_vif->tdls_status = TDLS_LINK_IDLE;
        rwnx_vif->sta.tdls_sta = NULL;
    }

    return 0;
}
#endif
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/*
 * @tdls_channel_switch: enable TDLS channel switch
 */
static int
rwnx_cfg80211_tdls_channel_switch(struct wiphy *wiphy,
                                      struct net_device *dev,
                                      const u8 *addr, u8 oper_class,
                                      struct cfg80211_chan_def *chandef)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_sta *rwnx_sta = rwnx_vif->sta.tdls_sta;
    struct tdls_chan_switch_cfm cfm;
    int error;

    if ((!rwnx_sta) || (memcmp(addr, rwnx_sta->mac_addr, ETH_ALEN))) {
        printk("%s: TDLS station %pM doesn't exist\n", __func__, addr);
        return -ENOLINK;
    }

    if (!rwnx_sta->tdls.chsw_allowed) {
        printk("%s: TDLS station %pM does not support TDLS channel switch\n", __func__, addr);
        return -ENOTSUPP;
    }

    error = rwnx_send_tdls_chan_switch_req(rwnx_hw, rwnx_vif, rwnx_sta,
                                           rwnx_sta->tdls.initiator,
                                           oper_class, chandef, &cfm);
    if (error)
        return error;

    if (!cfm.status) {
        rwnx_sta->tdls.chsw_en = true;
        return 0;
    } else {
        printk("%s: TDLS channel switch already enabled and only one is supported\n", __func__);
        return -EALREADY;
    }
}
#endif
#if 0
/*
 * @tdls_cancel_channel_switch: disable TDLS channel switch
 */
static void
rwnx_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
                                              struct net_device *dev,
                                              const u8 *addr)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_sta *rwnx_sta = rwnx_vif->sta.tdls_sta;
    struct tdls_cancel_chan_switch_cfm cfm;

    if (!rwnx_sta)
        return;

    if (!rwnx_send_tdls_cancel_chan_switch_req(rwnx_hw, rwnx_vif,
                                               rwnx_sta, &cfm))
        rwnx_sta->tdls.chsw_en = false;
}
#endif /* version >= 3.19 */
#endif
/**
 * @change_bss: Modify parameters for a given BSS (mainly for AP mode).
 */
int rwnx_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *dev,
                             struct bss_parameters *params)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    int res =  -EOPNOTSUPP;

    if (((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP) ||
         (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
        (params->ap_isolate > -1)) {

        if (params->ap_isolate)
            rwnx_vif->ap.flags |= RWNX_AP_ISOLATE;
        else
            rwnx_vif->ap.flags &= ~RWNX_AP_ISOLATE;

        res = 0;
    }

    return res;
}

static int rwnx_fill_station_info(struct rwnx_sta *sta, struct rwnx_vif *vif,
                                  struct station_info *sinfo)
{

	struct rwnx_sta_stats *stats = &sta->stats;
	struct rx_vector_1 *rx_vect1 = &stats->last_rx.rx_vect1;
	union rwnx_rate_ctrl_info *rate_info;
	struct mm_get_sta_info_cfm cfm;

	rwnx_send_get_sta_info_req(vif->rwnx_hw, sta->sta_idx, &cfm);
	sinfo->tx_failed = cfm.txfailed;
	rate_info = (union rwnx_rate_ctrl_info *)&cfm.rate_info;


	AICWFDBG(LOGDEBUG, "%s ModTx:%d TxIndex:%d ModRx:%d RxHTIndex:%d RxVHTIndex:%d RxHEIndex:%d RSSI:%d \r\n", __func__,
		rate_info->formatModTx, rate_info->mcsIndexTx, rx_vect1->format_mod,
		rx_vect1->ht.mcs,
		rx_vect1->vht.mcs,
		rx_vect1->he.mcs,
		(s8)cfm.rssi);


	switch (rate_info->formatModTx) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		sinfo->txrate.flags = 0;
		sinfo->txrate.legacy = tx_legrates_lut_rate[rate_info->mcsIndexTx];
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
		sinfo->txrate.flags = RATE_INFO_FLAGS_MCS;
		sinfo->txrate.mcs = rate_info->mcsIndexTx;
		break;
	case FORMATMOD_VHT:
		sinfo->txrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		sinfo->txrate.mcs = rate_info->mcsIndexTx;
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	case FORMATMOD_HE_MU:
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
		sinfo->txrate.flags = RATE_INFO_FLAGS_HE_MCS;
		sinfo->txrate.mcs = rate_info->mcsIndexTx;
		break;
#else
	//kernel not support he
	case FORMATMOD_HE_MU:
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
		sinfo->txrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		sinfo->txrate.mcs = rate_info->mcsIndexTx;
		break;
#endif
	default:
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	switch (rate_info->bwTx) {
	case PHY_CHNL_BW_20:
		sinfo->txrate.bw = RATE_INFO_BW_20;
		break;
	case PHY_CHNL_BW_40:
		sinfo->txrate.bw = RATE_INFO_BW_40;
		break;
	case PHY_CHNL_BW_80:
		sinfo->txrate.bw = RATE_INFO_BW_80;
		break;
	case PHY_CHNL_BW_160:
		sinfo->txrate.bw = RATE_INFO_BW_160;
		break;
	default:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		sinfo->txrate.bw = RATE_INFO_BW_HE_RU;
#else
		sinfo->txrate.bw = RATE_INFO_BW_20;
#endif
		break;
	}
#endif

	sinfo->txrate.nss = 1;
	sinfo->filled |= (BIT(NL80211_STA_INFO_TX_BITRATE) | BIT(NL80211_STA_INFO_TX_FAILED));

	sinfo->inactive_time = jiffies_to_msecs(jiffies - vif->rwnx_hw->stats.last_tx);
	sinfo->rx_bytes = vif->net_stats.rx_bytes;
	sinfo->tx_bytes = vif->net_stats.tx_bytes;
	sinfo->tx_packets = vif->net_stats.tx_packets;
	sinfo->rx_packets = vif->net_stats.rx_packets;
	sinfo->signal = (s8)cfm.rssi;
	sinfo->rxrate.nss = 1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	switch (rx_vect1->ch_bw) {
	case PHY_CHNL_BW_20:
		sinfo->rxrate.bw = RATE_INFO_BW_20;
		break;
	case PHY_CHNL_BW_40:
		sinfo->rxrate.bw = RATE_INFO_BW_40;
		break;
	case PHY_CHNL_BW_80:
		sinfo->rxrate.bw = RATE_INFO_BW_80;
		break;
	case PHY_CHNL_BW_160:
		sinfo->rxrate.bw = RATE_INFO_BW_160;
		break;
	default:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		sinfo->rxrate.bw = RATE_INFO_BW_HE_RU;
#else
		sinfo->rxrate.bw = RATE_INFO_BW_20;
#endif
		break;
	}
#endif

	switch (rx_vect1->format_mod) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		sinfo->rxrate.flags = 0;
		sinfo->rxrate.legacy = legrates_lut_rate[legrates_lut[rx_vect1->leg_rate]];
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_MCS;
		if (rx_vect1->ht.short_gi)
			sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		sinfo->rxrate.mcs = rx_vect1->ht.mcs;
		break;
	case FORMATMOD_VHT:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		if (rx_vect1->vht.short_gi)
			sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		sinfo->rxrate.mcs = rx_vect1->vht.mcs;
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	case FORMATMOD_HE_MU:
		sinfo->rxrate.he_ru_alloc = rx_vect1->he.ru_size;
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_HE_MCS;
		sinfo->rxrate.mcs = rx_vect1->he.mcs;
		sinfo->rxrate.he_gi = rx_vect1->he.gi_type;
		sinfo->rxrate.he_dcm = rx_vect1->he.dcm;
		break;
#else
	//kernel not support he
	case FORMATMOD_HE_MU:
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		sinfo->rxrate.mcs = rx_vect1->he.mcs;
		break;
#endif
	default:
		return -EINVAL;
	}


#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
	sinfo->filled |= (STATION_INFO_INACTIVE_TIME |
					 STATION_INFO_RX_BYTES64 |
					 STATION_INFO_TX_BYTES64 |
					 STATION_INFO_RX_PACKETS |
					 STATION_INFO_TX_PACKETS |
					 STATION_INFO_SIGNAL |
					 STATION_INFO_RX_BITRATE);
#else
	sinfo->filled |= (BIT(NL80211_STA_INFO_INACTIVE_TIME) |
					 BIT(NL80211_STA_INFO_RX_BYTES64)    |
					 BIT(NL80211_STA_INFO_TX_BYTES64)    |
					 BIT(NL80211_STA_INFO_RX_PACKETS)    |
					 BIT(NL80211_STA_INFO_TX_PACKETS)    |
					 BIT(NL80211_STA_INFO_SIGNAL)        |
					 BIT(NL80211_STA_INFO_RX_BITRATE));
#endif

	return 0;
}


/**
 * @get_station: get station information for the station identified by @mac
 */
static int rwnx_cfg80211_get_station(struct wiphy *wiphy,
	struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *mac,
#else
	const u8 *mac,
#endif
	struct station_info *sinfo)
{
    struct rwnx_vif *vif = netdev_priv(dev);
    struct rwnx_sta *sta = NULL;

    if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
        return -EINVAL;
    else if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
             (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
        if (vif->sta.ap && ether_addr_equal(vif->sta.ap->mac_addr, mac))
            sta = vif->sta.ap;
    }
    else
    {
        struct rwnx_sta *sta_iter;
		spin_lock_bh(&vif->rwnx_hw->cb_lock);
        list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
            if (sta_iter->valid && ether_addr_equal(sta_iter->mac_addr, mac)) {
                sta = sta_iter;
                break;
            }
        }
		spin_unlock_bh(&vif->rwnx_hw->cb_lock);
    }

    if (sta)
        return rwnx_fill_station_info(sta, vif, sinfo);

    return -ENOENT;
}


/**
 * @dump_station: dump station callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
                                      int idx, u8 *mac, struct station_info *sinfo)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_sta *sta_iter, *sta = NULL;
    struct mesh_peer_info_cfm peer_info_cfm;
    int i = 0;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(sta_iter, &rwnx_vif->ap.sta_list, list) {
        if (i < idx) {
            i++;
            continue;
        }

        sta = sta_iter;
        break;
    }

    if (sta == NULL)
        return -ENOENT;

    /* Forward the information to the UMAC */
    if (rwnx_send_mesh_peer_info_req(rwnx_hw, rwnx_vif, sta->sta_idx,
                                     &peer_info_cfm))
        return -ENOMEM;

    /* Copy peer MAC address */
    memcpy(mac, &sta->mac_addr, ETH_ALEN);

    /* Fill station information */
    sinfo->llid = peer_info_cfm.local_link_id;
    sinfo->plid = peer_info_cfm.peer_link_id;
    sinfo->plink_state = peer_info_cfm.link_state;
    sinfo->local_pm = peer_info_cfm.local_ps_mode;
    sinfo->peer_pm = peer_info_cfm.peer_ps_mode;
    sinfo->nonpeer_pm = peer_info_cfm.non_peer_ps_mode;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
    sinfo->filled = (STATION_INFO_LLID |
                     STATION_INFO_PLID |
                     STATION_INFO_PLINK_STATE |
                     STATION_INFO_LOCAL_PM |
                     STATION_INFO_PEER_PM |
                     STATION_INFO_NONPEER_PM);
#else
    sinfo->filled = (BIT(NL80211_STA_INFO_LLID) |
                     BIT(NL80211_STA_INFO_PLID) |
                     BIT(NL80211_STA_INFO_PLINK_STATE) |
                     BIT(NL80211_STA_INFO_LOCAL_PM) |
                     BIT(NL80211_STA_INFO_PEER_PM) |
                     BIT(NL80211_STA_INFO_NONPEER_PM));
#endif

    return 0;
}

/**
 * @add_mpath: add a fixed mesh path
 */
static int rwnx_cfg80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *dst,
	u8 *next_hop
#else
	const u8 *dst,
	const u8 *next_hop
#endif
)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, next_hop, &cfm);
}

/**
 * @del_mpath: delete a given mesh path
 */
static int rwnx_cfg80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
	u8 *dst
#else
	const u8 *dst
#endif
)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, NULL, &cfm);
}

/**
 * @change_mpath: change a given mesh path
 */
static int rwnx_cfg80211_change_mpath(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
		u8 *dst,
		u8 *next_hop
#else
		const u8 *dst,
		const u8 *next_hop
#endif
)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct mesh_path_update_cfm cfm;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, next_hop, &cfm);
}

/**
 * @get_mpath: get a mesh path for the given parameters
 */
static int rwnx_cfg80211_get_mpath(struct wiphy *wiphy, struct net_device *dev,
                                   u8 *dst, u8 *next_hop, struct mpath_info *pinfo)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_mesh_path *mesh_path = NULL;
    struct rwnx_mesh_path *cur;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &rwnx_vif->ap.mpath_list, list) {
        /* Compare the path target address and the provided destination address */
        if (memcmp(dst, &cur->tgt_mac_addr, ETH_ALEN)) {
            continue;
        }

        mesh_path = cur;
        break;
    }

    if (mesh_path == NULL)
        return -ENOENT;

    /* Copy next HOP MAC address */
    if (mesh_path->p_nhop_sta)
        memcpy(next_hop, &mesh_path->p_nhop_sta->mac_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = rwnx_vif->ap.generation;

    return 0;
}

/**
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_mpath(struct wiphy *wiphy, struct net_device *dev,
                                    int idx, u8 *dst, u8 *next_hop,
                                    struct mpath_info *pinfo)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_mesh_path *mesh_path = NULL;
    struct rwnx_mesh_path *cur;
    int i = 0;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &rwnx_vif->ap.mpath_list, list) {
        if (i < idx) {
            i++;
            continue;
        }

        mesh_path = cur;
        break;
    }

    if (mesh_path == NULL)
        return -ENOENT;

    /* Copy target and next hop MAC address */
    memcpy(dst, &mesh_path->tgt_mac_addr, ETH_ALEN);
    if (mesh_path->p_nhop_sta)
        memcpy(next_hop, &mesh_path->p_nhop_sta->mac_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = rwnx_vif->ap.generation;

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 * @get_mpp: get a mesh proxy path for the given parameters
 */
static int rwnx_cfg80211_get_mpp(struct wiphy *wiphy, struct net_device *dev,
                                 u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_mesh_proxy *mesh_proxy = NULL;
    struct rwnx_mesh_proxy *cur;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &rwnx_vif->ap.proxy_list, list) {
        if (cur->local) {
            continue;
        }

        /* Compare the path target address and the provided destination address */
        if (memcmp(dst, &cur->ext_sta_addr, ETH_ALEN)) {
            continue;
        }

        mesh_proxy = cur;
        break;
    }

    if (mesh_proxy == NULL)
        return -ENOENT;

    memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = rwnx_vif->ap.generation;

    return 0;
}

/**
 * @dump_mpp: dump mesh proxy path callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_mpp(struct wiphy *wiphy, struct net_device *dev,
                                  int idx, u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_mesh_proxy *mesh_proxy = NULL;
    struct rwnx_mesh_proxy *cur;
    int i = 0;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    list_for_each_entry(cur, &rwnx_vif->ap.proxy_list, list) {
        if (cur->local) {
            continue;
        }

        if (i < idx) {
            i++;
            continue;
        }

        mesh_proxy = cur;
        break;
    }

    if (mesh_proxy == NULL)
        return -ENOENT;

    /* Copy target MAC address */
    memcpy(dst, &mesh_proxy->ext_sta_addr, ETH_ALEN);
    memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

    /* Fill path information */
    pinfo->filled = 0;
    pinfo->generation = rwnx_vif->ap.generation;

    return 0;
}
#endif /* version >= 3.19 */

/**
 * @get_mesh_config: Get the current mesh configuration
 */
static int rwnx_cfg80211_get_mesh_config(struct wiphy *wiphy, struct net_device *dev,
                                         struct mesh_config *conf)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    return 0;
}

/**
 * @update_mesh_config: Update mesh parameters on a running mesh.
 */
static int rwnx_cfg80211_update_mesh_config(struct wiphy *wiphy, struct net_device *dev,
                                            u32 mask, const struct mesh_config *nconf)
{
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct mesh_update_cfm cfm;
    int status;

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    if (mask & CO_BIT(NL80211_MESHCONF_POWER_MODE - 1)) {
        rwnx_vif->ap.next_mesh_pm = nconf->power_mode;

        if (!list_empty(&rwnx_vif->ap.sta_list)) {
            // If there are mesh links we don't want to update the power mode
            // It will be updated with rwnx_update_mesh_power_mode() when the
            // ps mode of a link is updated or when a new link is added/removed
            mask &= ~BIT(NL80211_MESHCONF_POWER_MODE - 1);

            if (!mask)
                return 0;
        }
    }

    status = rwnx_send_mesh_update_req(rwnx_hw, rwnx_vif, mask, nconf, &cfm);

    if (!status && (cfm.status != 0))
        status = -EINVAL;

    return status;
}

/**
 * @join_mesh: join the mesh network with the specified parameters
 * (invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_join_mesh(struct wiphy *wiphy, struct net_device *dev,
                                   const struct mesh_config *conf, const struct mesh_setup *setup)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct mesh_start_cfm mesh_start_cfm;
    int error = 0;
    u8 txq_status = 0;
    /* STA for BC/MC traffic */
    struct rwnx_sta *sta;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
        return -ENOTSUPP;

    /* Forward the information to the UMAC */
    if ((error = rwnx_send_mesh_start_req(rwnx_hw, rwnx_vif, conf, setup, &mesh_start_cfm))) {
        return error;
    }

    /* Check the status */
    switch (mesh_start_cfm.status) {
        case CO_OK:
            rwnx_vif->ap.bcmc_index = mesh_start_cfm.bcmc_idx;
            rwnx_vif->ap.flags = 0;
            rwnx_vif->use_4addr = true;
            rwnx_vif->user_mpm = setup->user_mpm;

            sta = &rwnx_hw->sta_table[mesh_start_cfm.bcmc_idx];
            sta->valid = true;
            sta->aid = 0;
            sta->sta_idx = mesh_start_cfm.bcmc_idx;
            sta->ch_idx = mesh_start_cfm.ch_idx;
            sta->vif_idx = rwnx_vif->vif_index;
            sta->qos = true;
            sta->acm = 0;
            sta->ps.active = false;
            rwnx_mu_group_sta_init(sta, NULL);
            spin_lock_bh(&rwnx_hw->cb_lock);
            rwnx_chanctx_link(rwnx_vif, mesh_start_cfm.ch_idx,
                              (struct cfg80211_chan_def *)(&setup->chandef));
            if (rwnx_hw->cur_chanctx != mesh_start_cfm.ch_idx) {
                txq_status = RWNX_TXQ_STOP_CHAN;
            }
            rwnx_txq_vif_init(rwnx_hw, rwnx_vif, txq_status);
            spin_unlock_bh(&rwnx_hw->cb_lock);

            netif_tx_start_all_queues(dev);
            netif_carrier_on(dev);

            /* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
            if (rwnx_hw->cur_chanctx == mesh_start_cfm.ch_idx) {
                rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
            }
            break;

        case CO_BUSY:
            error = -EINPROGRESS;
            break;

        default:
            error = -EIO;
            break;
    }

    /* Print information about the operation */
    if (error) {
        netdev_info(dev, "Failed to start MP (%d)", error);
    } else {
        netdev_info(dev, "MP started: ch=%d, bcmc_idx=%d",
                    rwnx_vif->ch_index, rwnx_vif->ap.bcmc_index);
    }

    return error;
}

/**
 * @leave_mesh: leave the current mesh network
 * (invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_leave_mesh(struct wiphy *wiphy, struct net_device *dev)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
    struct rwnx_vif *rwnx_vif = netdev_priv(dev);
    struct mesh_stop_cfm mesh_stop_cfm;
    int error = 0;

    error = rwnx_send_mesh_stop_req(rwnx_hw, rwnx_vif, &mesh_stop_cfm);

    if (error == 0) {
        /* Check the status */
        switch (mesh_stop_cfm.status) {
            case CO_OK:
                spin_lock_bh(&rwnx_hw->cb_lock);
                rwnx_chanctx_unlink(rwnx_vif);
                rwnx_radar_cancel_cac(&rwnx_hw->radar);
                spin_unlock_bh(&rwnx_hw->cb_lock);
                /* delete BC/MC STA */
                rwnx_txq_vif_deinit(rwnx_hw, rwnx_vif);
                rwnx_del_bcn(&rwnx_vif->ap.bcn);

                netif_tx_stop_all_queues(dev);
                netif_carrier_off(dev);

                break;

            default:
                error = -EIO;
                break;
        }
    }

    if (error) {
        netdev_info(dev, "Failed to stop MP");
    } else {
        netdev_info(dev, "MP Stopped");
    }

    return 0;
}

static struct cfg80211_ops rwnx_cfg80211_ops = {
    .add_virtual_intf = rwnx_cfg80211_add_iface,
    .del_virtual_intf = rwnx_cfg80211_del_iface,
    .change_virtual_intf = rwnx_cfg80211_change_iface,
    .start_p2p_device = rwnx_cfgp2p_start_p2p_device,
    .stop_p2p_device = rwnx_cfgp2p_stop_p2p_device,
    .scan = rwnx_cfg80211_scan,
    .connect = rwnx_cfg80211_connect,
    .disconnect = rwnx_cfg80211_disconnect,
    .add_key = rwnx_cfg80211_add_key,
    .get_key = rwnx_cfg80211_get_key,
    .del_key = rwnx_cfg80211_del_key,
    .set_default_key = rwnx_cfg80211_set_default_key,
    .set_default_mgmt_key = rwnx_cfg80211_set_default_mgmt_key,
    .add_station = rwnx_cfg80211_add_station,
    .del_station = rwnx_cfg80211_del_station_compat,
    .change_station = rwnx_cfg80211_change_station,
    .mgmt_tx = rwnx_cfg80211_mgmt_tx,
    .start_ap = rwnx_cfg80211_start_ap,
    .change_beacon = rwnx_cfg80211_change_beacon,
    .stop_ap = rwnx_cfg80211_stop_ap,
    .set_monitor_channel = rwnx_cfg80211_set_monitor_channel,
    .probe_client = rwnx_cfg80211_probe_client,
//    .mgmt_frame_register = rwnx_cfg80211_mgmt_frame_register,
    .set_wiphy_params = rwnx_cfg80211_set_wiphy_params,
    .set_txq_params = rwnx_cfg80211_set_txq_params,
    .set_tx_power = rwnx_cfg80211_set_tx_power,
//    .get_tx_power = rwnx_cfg80211_get_tx_power,
//    .set_power_mgmt = rwnx_cfg80211_set_power_mgmt,
    .get_station = rwnx_cfg80211_get_station,
    .remain_on_channel = rwnx_cfg80211_remain_on_channel,
    .cancel_remain_on_channel = rwnx_cfg80211_cancel_remain_on_channel,
    .dump_survey = rwnx_cfg80211_dump_survey,
    .get_channel = rwnx_cfg80211_get_channel,
    .start_radar_detection = rwnx_cfg80211_start_radar_detection,
    .update_ft_ies = rwnx_cfg80211_update_ft_ies,
    .set_cqm_rssi_config = rwnx_cfg80211_set_cqm_rssi_config,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
    .channel_switch = rwnx_cfg80211_channel_switch,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    //.tdls_channel_switch = rwnx_cfg80211_tdls_channel_switch,
    //.tdls_cancel_channel_switch = rwnx_cfg80211_tdls_cancel_channel_switch,
#endif
    //.tdls_mgmt = rwnx_cfg80211_tdls_mgmt,
    //.tdls_oper = rwnx_cfg80211_tdls_oper,
    .change_bss = rwnx_cfg80211_change_bss,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(CONFIG_WPA3_FOR_OLD_KERNEL)
    .external_auth = rwnx_cfg80211_external_auth,
#endif
};


/*********************************************************************
 * Init/Exit functions
 *********************************************************************/
static void rwnx_wdev_unregister(struct rwnx_hw *rwnx_hw)
{
    struct rwnx_vif *rwnx_vif, *tmp;

    rtnl_lock();
    list_for_each_entry_safe(rwnx_vif, tmp, &rwnx_hw->vifs, list) {
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
        rwnx_cfg80211_del_iface(rwnx_hw->wiphy, &rwnx_vif->wdev);
    #else
        rwnx_cfg80211_del_iface(rwnx_hw->wiphy, rwnx_vif->ndev);
    #endif
    }
    rtnl_unlock();
}

static void rwnx_set_vers(struct rwnx_hw *rwnx_hw)
{
    u32 vers = rwnx_hw->version_cfm.version_lmac;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    snprintf(rwnx_hw->wiphy->fw_version,
             sizeof(rwnx_hw->wiphy->fw_version), "%d.%d.%d.%d",
             (vers & (0xff << 24)) >> 24, (vers & (0xff << 16)) >> 16,
             (vers & (0xff <<  8)) >>  8, (vers & (0xff <<  0)) >>  0);
}

static void rwnx_reg_notifier(struct wiphy *wiphy,
                              struct regulatory_request *request)
{
    struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

    // For now trust all initiator
    rwnx_radar_set_domain(&rwnx_hw->radar, request->dfs_region);
	if (rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801 ||
		((rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC||
		 rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW) && testmode == 0)){
    		rwnx_send_me_chan_config_req(rwnx_hw);
		}
}

static void rwnx_enable_mesh(struct rwnx_hw *rwnx_hw)
{
    struct wiphy *wiphy = rwnx_hw->wiphy;

    if (!rwnx_mod_params.mesh)
        return;

    rwnx_cfg80211_ops.get_station = rwnx_cfg80211_get_station;
    rwnx_cfg80211_ops.dump_station = rwnx_cfg80211_dump_station;
    rwnx_cfg80211_ops.add_mpath = rwnx_cfg80211_add_mpath;
    rwnx_cfg80211_ops.del_mpath = rwnx_cfg80211_del_mpath;
    rwnx_cfg80211_ops.change_mpath = rwnx_cfg80211_change_mpath;
    rwnx_cfg80211_ops.get_mpath = rwnx_cfg80211_get_mpath;
    rwnx_cfg80211_ops.dump_mpath = rwnx_cfg80211_dump_mpath;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    rwnx_cfg80211_ops.get_mpp = rwnx_cfg80211_get_mpp;
    rwnx_cfg80211_ops.dump_mpp = rwnx_cfg80211_dump_mpp;
#endif
    rwnx_cfg80211_ops.get_mesh_config = rwnx_cfg80211_get_mesh_config;
    rwnx_cfg80211_ops.update_mesh_config = rwnx_cfg80211_update_mesh_config;
    rwnx_cfg80211_ops.join_mesh = rwnx_cfg80211_join_mesh;
    rwnx_cfg80211_ops.leave_mesh = rwnx_cfg80211_leave_mesh;

    wiphy->flags |= (WIPHY_FLAG_MESH_AUTH | WIPHY_FLAG_IBSS_RSN);
    wiphy->features |= NL80211_FEATURE_USERSPACE_MPM;
    wiphy->interface_modes |= BIT(NL80211_IFTYPE_MESH_POINT);

    rwnx_limits[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
    rwnx_limits_dfs[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
}

extern int rwnx_init_aic(struct rwnx_hw *rwnx_hw);
#ifdef AICWF_USB_SUPPORT
u32 patch_tbl[][2] =
{
    {0x0044, 0x00000002}, //hosttype
    {0x0048, 0x00000060},
    {0x004c, 0x00000046},
    {0x0050, 0x00000000}, //ipc base
    {0x0054, 0x001a0000}, //buf base
    {0x0058, 0x001a0140}, //desc base
    {0x005c, 0x00001020}, //desc size
    {0x0060, 0x001a1020}, //pkt base
    {0x0064, 0x000207e0}, //pkt size
    {0x0068, 0x00000008},
    {0x006c, 0x00000040},
    {0x0070, 0x00000040},
    {0x0074, 0x00000020},
    {0x0078, 0x00000000},
    {0x007c, 0x00000040},
    {0x0080, 0x00190000},
    {0x0084, 0x0000fc00},//63kB
    {0x0088, 0x0019fc00},
    #ifdef USE_5G
    {0x00b4, 0xf3010001},
    #else
    {0x00b4, 0xf3010000},
    #endif
    //{0x00b8, 0x0f010a01}, //tx enhanced en, tx enhanced lo rate
    //{0x00bc, 0x00000008}, //tx enhanced hi rate
};
#else
//#ifdef CONFIG_ROM_PATCH_EN
#if 0
u32 patch_tbl[][2] =
{
    {0x0044, 0x02000001},
    {0x0058, 0x001a0000},
    {0x005c, 0x00002020},
    {0x0060, 0x001a2020}, //pkt base
    {0x0064, 0x00021820}, //pkt size
    {0x0080, 0x00190000},
    {0x0084, 0x0000fc00},//63kB
    {0x0088, 0x0019fc00}
};
#else
u32 patch_tbl[][2] =
{
    #ifdef USE_5G
    {0x00b4, 0xf3010001},
    #else
    {0x00b4, 0xf3010000},
    #endif
};
#endif
#endif

u32 patch_tbl_1[14][2] =
{
    {0x171b24, 0x1c4021}, //61
    {0x171c00, 0x1c40b1}, //116
    {0x172124, 0x1c43ed}, //12*8 + 1720c4
    {0x171bfc, 0x1c4849}, //115,  171a30 + 115 * 4
    {0x171ee4, 0x1c4941}, //301
    {0x171ee8, 0x1c4b09}, //302
    {0x172134, 0x1c4d65}, //14/15/16/17/18 * 8 + 1720c4{0x172134, 0x1c4d65},
    {0x17213c, 0x1c4d65},
    {0x172144, 0x1c4d65},
    {0x17214c, 0x1c4d65},
    {0x172154, 0x1c4d65},
    {0x1721d0, 0x1c53dd}, // 1721c4 + 1*8 + 4
    {0x1721f0, 0x1c5415}, // 1721c4 + 5*8 + 4
    {0x171eb0, 0x1c54a1}, // 288
};

u32 func_tbl[1721] =
{
    0x8cc88cc3,
    0xd8084283,
    0x1ac0d205,
    0xbfcc283f,
    0x20012000,
    0x20014770,
    0x20004770,
    0xbf004770,
    0x481d4a1c,
    0xb538491d,
    0x68056913,
    0xf8b16914,
    0xf8b100b0,
    0xf5a310b2,
    0xeb0363fa,
    0x1b1b1345,
    0x1a5b1a1b,
    0xdb1a2b00,
    0x681b4b15,
    0x68dcb143,
    0x1ae36913,
    0x63faf5a3,
    0x1a5b1a1b,
    0xdb012b00,
    0xbd382001,
    0x681b4b0f,
    0x3000f9b3,
    0xda062b00,
    0x1ae46913,
    0x549cf504,
    0x2c003408,
    0x2000db01,
    0x4909bd38,
    0xf44f4809,
    0xf66b7202,
    0x2000fad9,
    0xbf00bd38,
    0x40501000,
    0x40328040,
    0x0017192c,
    0x00178bf0,
    0x00173250,
    0x001c5a3c,
    0x001c56b4,
    0x4ff0e92d,
    0x8c05461c,
    0x3062f893,
    0x9020f8d2,
    0xf8d06987,
    0xb089b01c,
    0x02ad4616,
    0x2012e9dd,
    0xf8b4b9d3,
    0xf1bcc068,
    0xd0150f00,
    0x6ab38b52,
    0xf202fb0c,
    0x07189205,
    0xf20cfb05,
    0xd0199206,
    0xebb72300,
    0xf44f0a09,
    0x930776fa,
    0xf5039b05,
    0xe9c473c8,
    0xe071a31f,
    0xf0402800,
    0x950680ba,
    0x0a01f04f,
    0x6ab28b50,
    0xf000fb0a,
    0x90050712,
    0x8122f040,
    0xf8df6af2,
    0xf8dfc2bc,
    0xf8dc82bc,
    0x48983000,
    0x1203f3c2,
    0x037ff023,
    0x2002f818,
    0xf8cc4313,
    0x6ab33000,
    0xf3c34a93,
    0xea4113c0,
    0xf04f5103,
    0x60014300,
    0xf3bf6013,
    0xbf008f4f,
    0x00586813,
    0x4b8dd5fc,
    0xf9b16819,
    0x29001000,
    0x80aef2c0,
    0x68124a88,
    0xfa82fa1f,
    0x07126ab2,
    0x809df040,
    0xf8df6af2,
    0x4882c25c,
    0x1000f8dc,
    0x1203f3c2,
    0x017ff021,
    0x2002f818,
    0xf8cc430a,
    0x6ab22000,
    0xf3c2497c,
    0x051212c0,
    0x0218f042,
    0x4600f04f,
    0x600e6002,
    0x8f4ff3bf,
    0x680abf00,
    0xd5fc0056,
    0xf9b3681b,
    0x2b003000,
    0x4a72db6e,
    0x3062f894,
    0x22006816,
    0xebaab2b6,
    0x92070a06,
    0x0909ebb7,
    0x0a0aeb19,
    0xd0872b00,
    0x79e5ea4f,
    0x464b462a,
    0x46594638,
    0xfc50f679,
    0x92021bba,
    0xfb009a07,
    0xeb6bf309,
    0xfb050202,
    0x92033301,
    0x0105fba0,
    0xe9dd4419,
    0x42992302,
    0x4290bf08,
    0xe9cdbf38,
    0x4b5e0102,
    0x6702e9dd,
    0x9b066819,
    0x018918f6,
    0x9905d430,
    0xf5a11a71,
    0x4b597ac8,
    0x69194a59,
    0x691b6812,
    0x44511a89,
    0xf5a31acb,
    0x3b1453a5,
    0x6a632b00,
    0x1949bfb8,
    0xd022428b,
    0x6a1a4b52,
    0xd04142a2,
    0xf1044d51,
    0xf8d50018,
    0x479831e0,
    0x30a8f8d5,
    0xb0094620,
    0x4ff0e8bd,
    0xf8904718,
    0xf1baa002,
    0xd1010f00,
    0xa003f890,
    0xf00afb05,
    0xe73d9006,
    0x98054946,
    0xeba11a09,
    0x44b20a0a,
    0xb009e7cb,
    0x8ff0e8bd,
    0x0058680b,
    0x4941d48d,
    0xf44f4841,
    0xf66b72ae,
    0x2200f98f,
    0x3062f894,
    0xf5aa9207,
    0xf44f7afa,
    0xe78776fa,
    0x00516812,
    0xaf4ef53f,
    0x48384937,
    0x72aef44f,
    0xf97cf66b,
    0x7afaf44f,
    0xe7474b2c,
    0x62614a2c,
    0x01926812,
    0x4a32d4b8,
    0x68124d32,
    0x1024f893,
    0x4207f3c2,
    0x2d23f885,
    0x4a2fbb61,
    0x6812492f,
    0x20016809,
    0xf885b2d2,
    0xf8832d24,
    0x780b0024,
    0xd1054283,
    0x4a2b492a,
    0x6813600b,
    0x60134303,
    0x2d23f895,
    0x3d24f895,
    0xd21b429a,
    0x681b4b17,
    0x3000f9b3,
    0xda0d2b00,
    0x2d23f895,
    0x3d24f895,
    0xd307429a,
    0x48214920,
    0xf44f4d15,
    0xf66b7221,
    0xe787f96f,
    0xe7854d12,
    0xf44f2200,
    0x920776fa,
    0xe7354692,
    0x4b124814,
    0x1d23f895,
    0x2d24f895,
    0x6806681b,
    0xb2f64816,
    0x96000c1b,
    0xff82f66a,
    0xbf00e7d4,
    0x40328160,
    0x4032816c,
    0x00173250,
    0x4032004c,
    0x40501000,
    0x403280a4,
    0x00178d3c,
    0x00171a30,
    0xfffffe70,
    0x001c5a20,
    0x001c56e0,
    0x40328044,
    0x001e4000,
    0x40320090,
    0x00173270,
    0x40328070,
    0x40328074,
    0x001c5a58,
    0x001c5718,
    0x001c5704,
    0x40328164,
    0x00041830,
    0x4ff0e92d,
    0x9354f8df,
    0xb020f8d9,
    0xf44fb083,
    0xf6692000,
    0xf1bbfd9b,
    0xf0000f00,
    0x4cb480f1,
    0xa33cf8df,
    0x4fb37fa1,
    0x8338f8df,
    0x29004eb2,
    0x80e6f000,
    0xd50e0708,
    0xf8db4bb0,
    0x681b0070,
    0x2010f8da,
    0x4403685b,
    0x2b001a9b,
    0x80c7f2c0,
    0x01f7f001,
    0x074a77a1,
    0x8095f100,
    0xd515078b,
    0x3004f8db,
    0x0302f023,
    0x3004f8cb,
    0x301df899,
    0xd1082b05,
    0x48a34da2,
    0x31d8f8d5,
    0x23004798,
    0xf8897fa1,
    0xf001301d,
    0x77a101fd,
    0xd52d07cd,
    0x01586833,
    0x809ff140,
    0x0c096831,
    0x7f7cf411,
    0x80a6f000,
    0x4b983910,
    0x01fff001,
    0x1281eb01,
    0x03c2eb03,
    0x2021f893,
    0xf0002a00,
    0x4a93809d,
    0x68137f99,
    0xf9b34a92,
    0xf44f3000,
    0xfb0060a4,
    0x2b002201,
    0xf2c09200,
    0x683b809c,
    0x2fe0f413,
    0x80a5f000,
    0xf0017fa1,
    0x77a101fe,
    0xd59e068a,
    0x49894e88,
    0xf3c56835,
    0x463a1741,
    0xf66a2002,
    0x2300ff23,
    0x1547f3c5,
    0x3078f88b,
    0xf0402f00,
    0x4e7b80af,
    0x4d824a81,
    0xf4236813,
    0x60137300,
    0x3004f8db,
    0x21d8f8d6,
    0x0301f023,
    0x3004f8cb,
    0x0028f10b,
    0x682a4790,
    0x2b027813,
    0x8114f000,
    0x680b4974,
    0x0301f023,
    0x7813600b,
    0xd1122b01,
    0x41fff501,
    0x4a733134,
    0x3010f8da,
    0xf8b26808,
    0xf8d610b2,
    0xeb0321e0,
    0x1a591340,
    0x0018f10b,
    0x682b4790,
    0x2b02781b,
    0x80b4f000,
    0xf0017fa1,
    0x77a101df,
    0x4968e74f,
    0x20024d5d,
    0xfedcf66a,
    0x68634966,
    0xf022680a,
    0x600a0204,
    0xf4436822,
    0x431a7300,
    0xf8d5614a,
    0x60631498,
    0x0063f89b,
    0x3494f8d5,
    0x4798465a,
    0x7fa1683b,
    0x737cf023,
    0xf8d8603b,
    0xf0013000,
    0xf44301fb,
    0xf8c80380,
    0x77a13000,
    0x4856e742,
    0xfe66f66a,
    0x2200e782,
    0x006cf89b,
    0xf6584611,
    0xb160fd17,
    0xe72f7fa1,
    0xf66a4850,
    0xe775fe59,
    0xf66a484f,
    0xe771fe55,
    0xe8bdb003,
    0xf8da8ff0,
    0x7fa13010,
    0x3070f8cb,
    0x4593e71e,
    0xaf61f43f,
    0x48494948,
    0x22e6f240,
    0xf818f66b,
    0xf413683b,
    0xf47f2fe0,
    0x6832af5b,
    0x49446833,
    0xf3c30fd0,
    0x46057380,
    0x20024602,
    0xf66a9301,
    0x9b01fe81,
    0x0205ea53,
    0xf43f4628,
    0x4d2baf49,
    0x46199a00,
    0x323cf8d5,
    0x68334798,
    0x4300f023,
    0x68336033,
    0x4380f023,
    0xe7396033,
    0x2010f8da,
    0x529cf502,
    0x32084631,
    0xf8dae005,
    0x1ad33010,
    0xf2c02b00,
    0x680b80b2,
    0xd5f6071b,
    0x8080f8df,
    0x3000f8d8,
    0x0308f023,
    0x3000f8c8,
    0x3000f8d8,
    0xd51206de,
    0x49274e15,
    0xf66a2002,
    0xf8d6fe4b,
    0xf005323c,
    0x08780101,
    0x4798465a,
    0x3000f8d8,
    0x0310f023,
    0x3000f8c8,
    0x4b1fe722,
    0x4e0b491f,
    0x601d2501,
    0xf66a2002,
    0x4b1dfe35,
    0xe717601d,
    0x22304b1c,
    0xf657601a,
    0xe745fddb,
    0x00178bb8,
    0x40320074,
    0x40320070,
    0x00173244,
    0x00171a30,
    0x00178d48,
    0x00175aa8,
    0x00173250,
    0x00177738,
    0x4032008c,
    0x001c57ec,
    0x4033b390,
    0x00173270,
    0x0017192c,
    0x001c578c,
    0x4032004c,
    0x001c5790,
    0x001c57a8,
    0x001c57bc,
    0x001c5a70,
    0x001c57d0,
    0x001c57e0,
    0x001c580c,
    0x40328564,
    0x001c5818,
    0x40328568,
    0x40320038,
    0x00178d3c,
    0x40501000,
    0x4032006c,
    0x8310f3ef,
    0xd40307d8,
    0x4b31b672,
    0x601a2201,
    0x4f314b30,
    0x3201681a,
    0x2100601a,
    0x6039683a,
    0x080ff002,
    0x2010f8da,
    0x46164631,
    0xf247460a,
    0xe0045030,
    0x1010f8da,
    0x42811b89,
    0x6839d81b,
    0xd1f70709,
    0x49264825,
    0xc000f8d0,
    0x4616680f,
    0x2010f8da,
    0x0f00f1b8,
    0x4a22d11a,
    0x60112104,
    0xb132681a,
    0x3a01491a,
    0x680b601a,
    0xb103b90a,
    0x682ab662,
    0x491ce6b0,
    0x20029200,
    0xfdb0f66a,
    0x9a004b14,
    0x4919e7d3,
    0xf66a2002,
    0xe74bfda9,
    0x070cea07,
    0xd4e0077f,
    0x1600e9cd,
    0x46164680,
    0x077ae001,
    0x9a00d412,
    0x7000f8d8,
    0xf8da6812,
    0xf2471010,
    0x1b895030,
    0xea074281,
    0xd9f00702,
    0x2002490b,
    0xfd8cf66a,
    0xe7ea4b02,
    0xe7c49e01,
    0x0017569c,
    0x00172600,
    0x40320038,
    0x4032806c,
    0x40328074,
    0x40328070,
    0x001c5828,
    0x001c57f8,
    0x001c5834,
    0xf890b5f8,
    0x2b003064,
    0xf890d03a,
    0x4604308a,
    0xd1362b00,
    0xf8944e32,
    0x4a32306c,
    0x6a674932,
    0xeb036a09,
    0xeb021383,
    0x42a103c3,
    0x443d685d,
    0xf8d6d044,
    0x462931e0,
    0x0018f104,
    0x46204798,
    0xfafaf65e,
    0x1080f8d4,
    0x3214f8d6,
    0x46204439,
    0xf8d64798,
    0x462a30a4,
    0x46204639,
    0xb9784798,
    0x3078f894,
    0x49216862,
    0xb2db3301,
    0x0201f042,
    0xf8846809,
    0x60623078,
    0x4293780a,
    0xd029d811,
    0x3b01bdf8,
    0x2b01b2db,
    0x308af880,
    0x2b02d912,
    0xd1c04e13,
    0x0063f890,
    0x31c0f8d6,
    0x47982100,
    0xf8d6e7b9,
    0xf8941164,
    0x4622006c,
    0x40f8e8bd,
    0xbb84f658,
    0x40f8e8bd,
    0xbb3af65e,
    0x62654b0c,
    0x30b5f893,
    0xd1ba2b00,
    0x681b4b0a,
    0x2b02781b,
    0xe7b4d1af,
    0xe8bd4620,
    0xf66540f8,
    0xbf00b8d1,
    0x00171a30,
    0x00175aa8,
    0x00178d3c,
    0x00173244,
    0x0017192c,
    0x00173270,
    0x4c5cb538,
    0x68224b5c,
    0xf002495c,
    0x701a020f,
    0xf66a2002,
    0x6823fcef,
    0xd02e0718,
    0x4a594958,
    0x2000680b,
    0x4300f023,
    0x6020600b,
    0x07596813,
    0x4b55d5fc,
    0x4a554952,
    0x60182004,
    0xf043680b,
    0x600b4300,
    0xf0436813,
    0x60136300,
    0x20024950,
    0xfcd0f66a,
    0x4a4f4b47,
    0x494f4847,
    0x601c2420,
    0x24016813,
    0x0310f043,
    0x70446013,
    0x781b680b,
    0xd0382b03,
    0xd0062b01,
    0x4a44bd38,
    0xf0236813,
    0x60136300,
    0x4a45e7e2,
    0x035b6813,
    0x4a44d5fc,
    0x311c6911,
    0x1acb6913,
    0xdafb2b00,
    0x681c4b41,
    0x4b41b174,
    0xf9b3681b,
    0x2b003000,
    0x4a3fdb4e,
    0xf8b268e3,
    0x4a3a10b2,
    0x21041a5b,
    0x60916313,
    0x4a3c493b,
    0xf443680b,
    0x600b4300,
    0xf4436d13,
    0x65134300,
    0xf0236dd3,
    0xf0430303,
    0xf0434300,
    0x65d30301,
    0x4b2fbd38,
    0xb174681c,
    0x681b4b2e,
    0x3000f9b3,
    0xdb152b00,
    0x68e34a2c,
    0x10b2f8b2,
    0x1a5b4a27,
    0x63132104,
    0x4b2b6091,
    0xf042681a,
    0x601a0201,
    0x075a681b,
    0x4b28d5ae,
    0x7200f44f,
    0xbd38601a,
    0x4d214b1e,
    0x68e3691a,
    0x10b2f8b5,
    0x1a521a9a,
    0xdae32a00,
    0x48224921,
    0x32dbf240,
    0xfddef66a,
    0xf8b568e3,
    0xe7d910b2,
    0x69124d17,
    0xf8b568e3,
    0x1a9a10b2,
    0x2a001a52,
    0x4918daab,
    0xf2404818,
    0xf66a32e9,
    0x68e3fdcb,
    0x10b2f8b5,
    0xbf00e7a1,
    0x40320038,
    0x00172604,
    0x001c5844,
    0x40328074,
    0x4032806c,
    0x40328070,
    0x40328048,
    0x001c584c,
    0x40580010,
    0x00173274,
    0x40041020,
    0x40501000,
    0x00178bf0,
    0x00173250,
    0x0017192c,
    0x40240014,
    0x40506000,
    0x40044084,
    0x40044100,
    0x001c5a8c,
    0x001c5854,
    0xf890b380,
    0xb36b3064,
    0x47f0e92d,
    0x4a7b4b7a,
    0xf8df4e7b,
    0xf8df920c,
    0x4d7a8240,
    0x460c487a,
    0x2702497a,
    0xc470f8d1,
    0x7180f8c3,
    0x49786892,
    0xc044f8c2,
    0x601f2201,
    0xf8d97032,
    0x680b2010,
    0xf0236072,
    0x600b0302,
    0x1000f8d8,
    0xc0b2f8b5,
    0x68018f8b,
    0xebb34463,
    0xea4f1f41,
    0xd3021a41,
    0x87f0e8bd,
    0x496b4770,
    0xf66a4638,
    0xf8d9fbdf,
    0x68f32010,
    0x2b001a9b,
    0xf2804b67,
    0xf89380a2,
    0xf8932d23,
    0x2a012d24,
    0xf893d90a,
    0x68612d23,
    0xf0002a00,
    0xf893809e,
    0x3a012d23,
    0xaa02fb01,
    0x3d23f893,
    0x4652495d,
    0xf66a2002,
    0xf8d8fbbf,
    0xf8b53000,
    0x8f9a00b2,
    0x4b594955,
    0xc178f8df,
    0x680b691c,
    0xebaa4402,
    0xf0220202,
    0xf0030703,
    0x433b0303,
    0x600b4f53,
    0x60b2683b,
    0x0301f043,
    0xf8dc603b,
    0xf8dc3000,
    0xf3c31000,
    0xf4210309,
    0xf043717f,
    0xf0210301,
    0x430b0103,
    0x3000f8cc,
    0x4422683b,
    0x60f2075b,
    0x4b47d503,
    0x7200f44f,
    0x4a46601a,
    0x68134c46,
    0xf043493d,
    0x60130308,
    0xf0436823,
    0x60230302,
    0xf5a2680b,
    0x3a1022fe,
    0x0301f043,
    0x6911600b,
    0x7196f501,
    0x1a5b6913,
    0xdbfb2b00,
    0x681b4b3b,
    0x2b01781b,
    0x493ad188,
    0x6a4b4c3a,
    0xf0236824,
    0xf04303ff,
    0x624b03df,
    0xf4236a4b,
    0xf443437f,
    0x624b435f,
    0x4b34b194,
    0xf9b3681b,
    0x2b003000,
    0x68e2db31,
    0x48316861,
    0xfb04f66a,
    0x10b2f8b5,
    0x4a2568e3,
    0x21041a5b,
    0x60916313,
    0x4a28492c,
    0xf443680b,
    0x600b4300,
    0xf4436d13,
    0x65134300,
    0xf0236dd3,
    0xf0430303,
    0xf0434300,
    0x65d30301,
    0xf4436813,
    0x60135380,
    0x4922e74e,
    0x3d23f893,
    0x46524638,
    0xfb2ef66a,
    0xf893e76d,
    0x3a012d24,
    0xaa02fb01,
    0x6913e760,
    0x1ad368e2,
    0x28001a18,
    0x4919dac8,
    0xf2404819,
    0xf66a4231,
    0xe7c0fca1,
    0xe000e100,
    0xe000ed00,
    0x00173254,
    0x0017192c,
    0x40328040,
    0x00171a30,
    0x40320084,
    0x001c589c,
    0x001e4000,
    0x001c58b8,
    0x40501000,
    0x40044084,
    0x40044100,
    0x40580010,
    0x40580018,
    0x00173274,
    0x40506000,
    0x00178bf0,
    0x00173250,
    0x001c58c8,
    0x40240014,
    0x001c58a4,
    0x001c5aac,
    0x001c5854,
    0x00173244,
    0x4ff0e92d,
    0x4abd4bbc,
    0xf852681b,
    0xf9b34020,
    0x4abb3000,
    0x8b02ed2d,
    0x02c0eb02,
    0xee082b00,
    0xb08b2a10,
    0xea4f4683,
    0xf2c005c0,
    0x462082c4,
    0xf8d0f669,
    0xf8534bb2,
    0xf1b9903b,
    0xf0000f00,
    0x230080e5,
    0x3303e9cd,
    0xf5059302,
    0x9301629e,
    0xf10b469a,
    0x9208039e,
    0x464c9305,
    0xf898e079,
    0x06df3004,
    0x80d2f140,
    0xf4226932,
    0x9a010900,
    0x9010f8c6,
    0x065d3201,
    0xf1009201,
    0x4fa180e0,
    0xf8d74620,
    0x47983418,
    0xf4036b63,
    0xf5b31360,
    0xbf081f20,
    0xf3ef46a2,
    0x07d98310,
    0xb672d403,
    0x22014b99,
    0x4d99601a,
    0xee18682b,
    0x33010a10,
    0xf669602b,
    0x6b63f951,
    0x1360f403,
    0x1f60f5b3,
    0x80a9f000,
    0xb133682b,
    0x3b014a8f,
    0x602b6812,
    0xb102b90b,
    0xf8dfb662,
    0xf8d88240,
    0x78193000,
    0xf419b391,
    0xd1050300,
    0x002ef9b4,
    0x28008de2,
    0x817ef2c0,
    0x302cf894,
    0x202af894,
    0xf8b44884,
    0xeb03c008,
    0x441303c3,
    0x530ef203,
    0xf8502901,
    0xeba22023,
    0xf840020c,
    0xf0002023,
    0x6ce080d5,
    0xf650b138,
    0xf8d8fd4f,
    0x781e3000,
    0xf0002e01,
    0x4621810f,
    0xf66a4658,
    0x6b63fc95,
    0x1360f403,
    0x1f60f5b3,
    0x80eaf000,
    0xf8534b6d,
    0x2c00403b,
    0xf8d4d05c,
    0x6d268048,
    0x0f00f1b8,
    0xaf7ff47f,
    0x8310f3ef,
    0xd40307d9,
    0x4b67b672,
    0x601a2201,
    0x682b4d66,
    0x0a10ee18,
    0x602b3301,
    0xf8ecf669,
    0xb133682b,
    0x3b014a60,
    0x602b6812,
    0xb102b90b,
    0x4f5cb662,
    0x9178f8df,
    0x817cf8df,
    0xf6764620,
    0xf8d7ff21,
    0x46203418,
    0xf8d74798,
    0x220033ac,
    0xf18bfa5f,
    0x47984620,
    0x302cf894,
    0x102af894,
    0xeb038922,
    0x440b03c3,
    0x530ef203,
    0x1023f859,
    0xf8491a8a,
    0xf8d82023,
    0x781b3000,
    0xd0642b01,
    0xd0532b02,
    0xd1082b03,
    0x3054f894,
    0xf0002b01,
    0x8de381db,
    0xf10007d9,
    0x4621814c,
    0xf66a4658,
    0x4b3ffc31,
    0x403bf853,
    0xd1a22c00,
    0xecbdb00b,
    0xe8bd8b02,
    0x4b388ff0,
    0xf9b3681b,
    0x2b003000,
    0x808bf2c0,
    0xe9dd4650,
    0xf6761201,
    0x4640fee7,
    0xfbeef659,
    0xe9cd2300,
    0x469a3301,
    0xf8d8e742,
    0xb1b220dc,
    0x8ce38811,
    0x1311eba3,
    0x030bf3c3,
    0x71fef240,
    0xd816428b,
    0xea4f2b3f,
    0xd8121113,
    0x0241eb02,
    0x030ff003,
    0xfa428852,
    0x07d8f303,
    0x9b02d509,
    0x93023301,
    0x0304f44f,
    0x0903ea49,
    0x9010f8c6,
    0xf44fe6fb,
    0xe7f72380,
    0x3054f894,
    0xf0002b01,
    0x8de38117,
    0xd5ae07da,
    0xf6682080,
    0xf8d8ff75,
    0x781b3000,
    0xf894e79c,
    0x2b013054,
    0x80f7f000,
    0x07db8de3,
    0x2080d59f,
    0xff66f668,
    0x3000f8d8,
    0xe78b781b,
    0x3054f894,
    0xf47f2b01,
    0xf890af26,
    0xf0433f20,
    0xf1060302,
    0xf8800110,
    0x22043f20,
    0xf6512012,
    0x8ba1fd2b,
    0x46224809,
    0xf91ef66a,
    0xbf00e713,
    0x00173250,
    0x0003fc40,
    0x00175980,
    0x00171a30,
    0x0017569c,
    0x00172600,
    0x00173278,
    0x001c5904,
    0x00173274,
    0x2b009b03,
    0x808bf040,
    0xf48bfa5f,
    0xf6682080,
    0x4620ff2f,
    0xfebef659,
    0x93032300,
    0xf1bae706,
    0xf47f0f00,
    0x49aaaf71,
    0xf24048aa,
    0xf66a42e7,
    0xe769fac7,
    0xe9d34ba8,
    0x69130201,
    0x46804798,
    0xf43f2800,
    0xf650aee8,
    0x4603fd49,
    0xf0002800,
    0x22008096,
    0x600249a1,
    0x605a6808,
    0x609a4440,
    0xf3ef6018,
    0x07d28210,
    0x812ef140,
    0x6829489c,
    0x31016802,
    0x0201f042,
    0x60026029,
    0x68124a98,
    0xd4fb0790,
    0x68124a97,
    0xf0002a00,
    0x4e968116,
    0x2a006872,
    0x8149f000,
    0x4a926053,
    0x681289b0,
    0x4b906073,
    0x30013201,
    0x601a81b0,
    0x68134a8c,
    0x0301f023,
    0x29006013,
    0xaeadf43f,
    0x39014b8b,
    0x6029681b,
    0xf47f2900,
    0x2b00aea6,
    0xaea3f43f,
    0xe6a0b662,
    0x49866d20,
    0x64a36503,
    0xf8946103,
    0x63e3502b,
    0xf3c29b05,
    0x20a4020e,
    0x0201f042,
    0x3005fb10,
    0x63a4f44f,
    0x1305fb03,
    0xeb0185e2,
    0x4a7c00c0,
    0x46219304,
    0xffe2f668,
    0xf4036b63,
    0xf5b31360,
    0xd0021f60,
    0x93032301,
    0x9804e686,
    0xfd60f656,
    0xf43f2800,
    0x4d73af6f,
    0x31fff895,
    0xf47f2b00,
    0x9b04af69,
    0xf8cd9a08,
    0xfa5fb00c,
    0xf8ddf48b,
    0x46d39014,
    0x4698189e,
    0xe00946ba,
    0xff74f668,
    0x3424f8da,
    0x46214638,
    0xf8954798,
    0xb92331ff,
    0x7039f858,
    0x2f004630,
    0x46dad1f0,
    0xb00cf8dd,
    0x2080e74a,
    0xfe7af668,
    0x4640e6af,
    0xfb92f650,
    0xf899e647,
    0x22043f20,
    0x0302f043,
    0x0110f106,
    0xf8892012,
    0xf6513f20,
    0x8de3fc43,
    0xf53f07da,
    0xe6fdaefc,
    0xf6539306,
    0x9b06f95d,
    0x28009007,
    0x80c0f000,
    0x4b509309,
    0x2a00681a,
    0x80c2f000,
    0xf6684618,
    0x9b07ff39,
    0xf04f2204,
    0x21120e00,
    0x70994684,
    0xf883701a,
    0xf883e001,
    0xf106e003,
    0x18980110,
    0xc018f8cd,
    0xfdc4f678,
    0x8a194b42,
    0xf5b19b09,
    0xd85a7fc3,
    0xb29b1c4b,
    0x00ca9309,
    0x4b3e9806,
    0x68188181,
    0x4b3d9907,
    0x0c02eb00,
    0x0e01f04f,
    0x1004f8cc,
    0x400b5881,
    0x6380f043,
    0x0308f043,
    0xf8995083,
    0x4a333782,
    0x82119909,
    0xf8894473,
    0x9b063782,
    0x22082100,
    0xc004f8c3,
    0xe00ef883,
    0x609a6019,
    0x8310f3ef,
    0xd40307db,
    0x4b25b672,
    0xe000f8c3,
    0x482a682b,
    0x33019906,
    0xf668602b,
    0xf8d7fea5,
    0x47983444,
    0xb133682b,
    0x3b014a1d,
    0x602b6812,
    0xb102b90b,
    0x8de3b662,
    0xf53f07d8,
    0xe67cae7b,
    0x69324b1f,
    0x601a681b,
    0xffaef64e,
    0x4b1de61d,
    0x421c681b,
    0xad37f47f,
    0x481b490a,
    0x6293f44f,
    0xf988f66a,
    0x2200e52f,
    0x46119309,
    0x4a17e7a4,
    0xbb6a6812,
    0x4e094a15,
    0xe6e86013,
    0x4a08b672,
    0xe6cd6016,
    0x001c5ad0,
    0x001c58ec,
    0x001755b4,
    0x001719e4,
    0x40240060,
    0x40240064,
    0x0017559c,
    0x0017569c,
    0x00177738,
    0x001c4001,
    0x00175780,
    0x0017469c,
    0x001755d4,
    0x31ff0000,
    0x001746a4,
    0x00180000,
    0x0017a5a0,
    0x001c58d4,
    0x40240068,
    0x9306480b,
    0xff78f669,
    0x9b066829,
    0x4809e7ca,
    0xf6699306,
    0x6829ff71,
    0xe6b09b06,
    0xb00b4806,
    0x8b02ecbd,
    0x4ff0e8bd,
    0xbcdef64f,
    0xe7f64803,
    0x001c591c,
    0x001c5924,
    0x001c5938,
    0x001c594c,
    0xf240b530,
    0xb0834003,
    0x4619460d,
    0xf6682308,
    0xe9d5f98d,
    0x46043200,
    0x3200e9c0,
    0xe9cd4611,
    0x48042200,
    0xff48f669,
    0xf6684620,
    0x2000f9af,
    0xbd30b003,
    0x001c5970,
    0x460cb570,
    0x4619b084,
    0x4012f240,
    0xf6682308,
    0x6822f971,
    0x429a4b12,
    0xd0104605,
    0x686168a3,
    0xe9c54810,
    0xe9cd2300,
    0x92003301,
    0xf669461a,
    0x4628ff27,
    0xf98ef668,
    0xb0042000,
    0xe9d4bd70,
    0x68116301,
    0x404b4808,
    0x404b4033,
    0xe9d46013,
    0x680a1300,
    0x68a29200,
    0xff12f669,
    0xe7dd6822,
    0x40344058,
    0x001c59cc,
    0x001c59b0,
    0xbf002332,
    0xf0133b01,
    0xd1fa03ff,
    0xbf004770,
    0xbf0023c8,
    0xf0133b01,
    0xd1fa03ff,
    0xbf004770,
    0x49724a71,
    0xf8df6813,
    0xf023c208,
    0xb5f00302,
    0x680b6013,
    0x4c6f4a6e,
    0xf4234f6f,
    0x600b6300,
    0x496e6813,
    0xf4232800,
    0xbf1c7380,
    0x468c4627,
    0x20326013,
    0x3801bf00,
    0x00fff010,
    0xf8dfd1fa,
    0x4a63e18c,
    0x3000f8de,
    0xf4234e65,
    0xf8ce5380,
    0x68133000,
    0x7300f423,
    0xf8de6013,
    0xf4433000,
    0xf8ce6380,
    0xf8de3000,
    0xf4433000,
    0xf8ce6300,
    0x46723000,
    0x1cc125ff,
    0x4664b2c9,
    0xf0236813,
    0x430b03ff,
    0xf8546013,
    0x60333b04,
    0xf4436813,
    0x60137380,
    0xbf00bf00,
    0xbf00bf00,
    0x049b6813,
    0x3901d5fc,
    0x428db2c9,
    0x3004d1e8,
    0x28803504,
    0xf10cb2ed,
    0xd1de0c10,
    0x3000f8de,
    0x6380f423,
    0x3000f8ce,
    0xbf0023c8,
    0xf0133b01,
    0xd1fa03ff,
    0x493f4c3e,
    0x48436822,
    0x6200f422,
    0x680a6022,
    0x0280f042,
    0x680a600a,
    0x7280f442,
    0x600a3f04,
    0xf022680a,
    0x431a021f,
    0xf857600a,
    0x60022f04,
    0xf042680a,
    0x600a0220,
    0xbf00bf00,
    0xbf00bf00,
    0x0552680a,
    0x3301d5fc,
    0xd1e92b10,
    0xf023680b,
    0x600b0380,
    0xbf0023c8,
    0xf0133b01,
    0xd1fa03ff,
    0x4a2d4927,
    0xf423680b,
    0x600b7380,
    0xf4436813,
    0x60131300,
    0xbf002332,
    0xf0133b01,
    0xd1fa03ff,
    0x4a1f4b1e,
    0x4d256819,
    0x48264c25,
    0xf4414e26,
    0x60195180,
    0xf4416811,
    0x60117100,
    0xf4416819,
    0x60196100,
    0x4b216811,
    0x7180f441,
    0x682a6011,
    0xf442491f,
    0x602a5280,
    0xf4226822,
    0x60222280,
    0xf0226802,
    0x60025200,
    0x60306818,
    0x45bbf5a5,
    0x685c3d78,
    0x689d602c,
    0x4a16600d,
    0x601568dd,
    0x691d4815,
    0xe9d36005,
    0x4c145005,
    0x602569db,
    0x61136108,
    0xbf00bdf0,
    0x40580018,
    0x40344060,
    0x4034406c,
    0x001718a4,
    0x00171824,
    0x00171624,
    0x40344064,
    0x40344070,
    0x40344058,
    0x40342014,
    0x40342018,
    0x4034201c,
    0x4033c218,
    0x001718e4,
    0x4033c220,
    0x4033c224,
    0x4033c228,
    0x4033c22c,
    0x00171324,
    0x6c616821,
    0x6e6f615f,
    0x656d6974,
    0x69745f72,
    0x705f656d,
    0x28747361,
    0x656d6974,
    0x743e2d72,
    0x20656d69,
    0x3035202b,
    0x00293030,
    0x616d786e,
    0x69745f63,
    0x6f5f656d,
    0x69615f6e,
    0x61765f72,
    0x5f64696c,
    0x66746567,
    0x21202928,
    0x0030203d,
    0x6d697464,
    0x2c64253a,
    0x252c6425,
    0x64252c64,
    0x00000a0d,
    0x6f76282a,
    0x6974616c,
    0x7520656c,
    0x38746e69,
    0x2a20745f,
    0x5f672629,
    0x5f6e6f61,
    0x72616873,
    0x642e6465,
    0x5f6d6974,
    0x5f746e63,
    0x736e6f61,
    0x65726168,
    0x203c2064,
    0x6f76282a,
    0x6974616c,
    0x7520656c,
    0x38746e69,
    0x2a20745f,
    0x5f672629,
    0x5f6e6f61,
    0x72616873,
    0x642e6465,
    0x5f6d6974,
    0x69726570,
    0x615f646f,
    0x68736e6f,
    0x64657261,
    0x00000000,
    0x00002c4c,
    0x654b849b,
    0x78646979,
    0x766e6920,
    0x64696c61,
    0x3230252c,
    0x00000a58,
    0x6e49849b,
    0x696c6176,
    0x656b2064,
    0x78646979,
    0x0000000a,
    0x6e49849b,
    0x696c6176,
    0x54532064,
    0x64253a41,
    0x0000000a,
    0x5f666976,
    0x20617473,
    0x76203d3d,
    0x00006669,
    0x64253d54,
    0x0d64252c,
    0x0000000a,
    0x3a4e4342,
    0x0a0d6425,
    0x00000000,
    0x206e6362,
    0x656e6f64,
    0x6d697420,
    0x74756f65,
    0x00000a0d,
    0x20736366,
    0x0a0d6b6f,
    0x00000000,
    0x20736366,
    0x20746f6e,
    0x0a0d6b6f,
    0x00000000,
    0x656c6469,
    0x72726520,
    0x00000a0d,
    0x656c6469,
    0x746e6920,
    0x72726520,
    0x00000a0d,
    0x2c642564,
    0x00000000,
    0x0a0d6564,
    0x00000000,
    0x6c616821,
    0x63616d5f,
    0x745f7768,
    0x5f656d69,
    0x74736170,
    0x6d697428,
    0x3e2d7265,
    0x656d6974,
    0x67202d20,
    0x6669775f,
    0x65735f69,
    0x6e697474,
    0x702e7367,
    0x6f5f7277,
    0x5f6e6570,
    0x64737973,
    0x79616c65,
    0x00000029,
    0x78253d74,
    0x00000a0d,
    0x20746f6e,
    0x74736170,
    0x6425203a,
    0x0d64252c,
    0x0000000a,
    0x73736170,
    0x6425203a,
    0x0d64252c,
    0x0000000a,
    0x3a706c73,
    0x252c7825,
    0x000a0d78,
    0x655f656b,
    0x675f7476,
    0x29287465,
    0x65202620,
    0x625f7476,
    0x00007469,
    0x65647874,
    0x665f6373,
    0x74737269,
    0x203d2120,
    0x4c4c554e,
    0x00000000,
    0x73212121,
    0x20646e65,
    0x206d6663,
    0x78253a31,
    0x0d78252c,
    0x0000000a,
    0x0d677562,
    0x0000000a,
    0x6f696473,
    0x69617420,
    0x7265206c,
    0x0d726f72,
    0x0000000a,
    0x3a727265,
    0x206f6e20,
    0x2067736d,
    0x21746b70,
    0x00000a0d,
    0x21727265,
    0x74202121,
    0x63206c78,
    0x6e206d66,
    0x7562206f,
    0x72656666,
    0x726f6620,
    0x62737520,
    0x00000a0d,
    0x4244819d,
    0x57203a47,
    0x69746972,
    0x6d20676e,
    0x726f6d65,
    0x69772079,
    0x30206874,
    0x38302578,
    0x202f2078,
    0x203a6425,
    0x2578305b,
    0x5d783830,
    0x30203d20,
    0x38302578,
    0x202f2078,
    0x000a6425,
    0x6b73616d,
    0x69727720,
    0x253a6574,
    0x78252c78,
    0x2c78252c,
    0x0a0d7825,
    0x00000000,
    0x4244819d,
    0x57203a47,
    0x69746972,
    0x6d20676e,
    0x726f6d65,
    0x69772079,
    0x6d206874,
    0x3a6b7361,
    0x30257830,
    0x202c7838,
    0x61746164,
    0x2578303a,
    0x20783830,
    0x6425202f,
    0x305b203a,
    0x38302578,
    0x3d205d78,
    0x25783020,
    0x20783830,
    0x6425202f,
    0x0000000a,
    0x5f6c6168,
    0x6863616d,
    0x78725f77,
    0x6e63625f,
    0x7275645f,
    0x6f697461,
    0x0000006e,
    0x5f6c6168,
    0x6863616d,
    0x6c735f77,
    0x5f706565,
    0x63656863,
    0x61705f6b,
    0x00686374,
    0x745f6d6d,
    0x5f747462,
    0x706d6f63,
    0x5f657475,
    0x63746170,
    0x00000068,
    0x735f6d6d,
    0x7065656c,
    0x6f666e69,
    0x5f78725f,
    0x5f747665,
    0x63746170,
    0x00000068,
    0x786e7772,
    0x656c735f,
    0x635f7065,
    0x61676b6c,
    0x635f6574,
    0x69666e6f,
    0x61705f67,
    0x00686374,
    0x786e7772,
    0x656c735f,
    0x645f7065,
    0x73706565,
    0x7065656c,
    0x6e6f635f,
    0x5f676966,
    0x63746170,
    0x00000068,
    0x5f6c7874,
    0x5f6d6663,
    0x5f747665,
    0x63746170,
    0x00000068,

};

struct aic_feature_t {
	int hwinfo;
    int fwlog_en;
};

#define CHIP_REV_U02        0x3
#define CHIP_REV_U03        0x7
#define CHIP_SUB_REV_U04    0x20
u8 chip_id = 0;
u8 chip_sub_id = 0; // rom_id for 8800dc
u8 chip_mcu_id = 0;

#ifdef CONFIG_PMIC_SETTING
u32 syscfg_tbl_pmic_u02[][2] = {
    {0x40040000, 0x00001AC8}, // 1) fix panic
    {0x40040084, 0x00011580},
    {0x40040080, 0x00000001},
    {0x40100058, 0x00000000},
};
#endif /* CONFIG_PMIC_SETTING */

u32 syscfg_tbl_u04[][2] = {
    {0x40040000, 0x0000042C}, // protect usb replenish rxq / flush rxq, skip flush rxq before start_app
    {0x40040004, 0x0000DD44},
    {0x40040008, 0x00000448},
    {0x4004000C, 0x0000044C},
    {0x0019B800, 0xB9F0F19B},
    {0x0019B804, 0x0019B81D},
    {0x0019B808, 0xBF00FA79},
    {0x0019B80C, 0xF007BF00},
    {0x0019B810, 0x4605B672}, // code
    {0x0019B814, 0x21E0F04F},
    {0x0019B818, 0xBE0BF664},
    {0x0019B81C, 0xF665B510},
    {0x0019B820, 0x4804FC9D},
    {0x0019B824, 0xFA9EF66C},
    {0x0019B828, 0xFCA8F665},
    {0x0019B82C, 0x4010E8BD},
    {0x0019B830, 0xBAC6F66C},
    {0x0019B834, 0x0019A0C4},
    {0x40040084, 0x0019B800}, // out base
    {0x40040080, 0x0000000F},
    {0x40100058, 0x00000000},
};

u32 syscfg_tbl[][2] = {
    {0x40500014, 0x00000101}, // 1)
    {0x40500018, 0x0000010D}, // 2)
    {0x40500004, 0x00000010}, // 3) the order should not be changed
    #ifdef CONFIG_PMIC_SETTING
    {0x50000000, 0x03220204}, // 2) pmic interface init
    {0x50019150, 0x00000002}, // 3) for 26m xtal, set div1
    {0x50017008, 0x00000000}, // 4) stop wdg
    #endif /* CONFIG_PMIC_SETTING */
};

u32 syscfg_tbl_masked[][3] = {
    {0x40506024, 0x000000FF, 0x000000DF}, // for clk gate lp_level
    #ifdef CONFIG_PMIC_SETTING
    //{0x50017008, 0x00000002, 0x00000000}, // stop wdg
    #endif /* CONFIG_PMIC_SETTING */
};


u32 rf_tbl_masked[][3] = {
    {0x40344058, 0x00800000, 0x00000000},// pll trx
};

u32 wdt_tbl_masked[][3] = {
    {0x4010300c, 0x00000001, 0x00000001},
};

static void system_config_8800(struct rwnx_hw *rwnx_hw){
    int syscfg_num;
    int ret, cnt;
    const u32 mem_addr = 0x40500000;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
		AICWFDBG(LOGERROR, "%x rd fail: %d\n", mem_addr, ret);
        return;
    }
    chip_id = (u8)(rd_mem_addr_cfm.memdata >> 16);
    //printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, 0x00000004, &rd_mem_addr_cfm);
    if (ret) {
		AICWFDBG(LOGERROR, "[0x00000004] rd fail: %d\n", ret);
        return;
    }
    chip_sub_id = (u8)(rd_mem_addr_cfm.memdata >> 4);
    //printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
	AICWFDBG(LOGINFO, "chip_id=%x, chip_sub_id=%x\n", chip_id, chip_sub_id);


#ifdef CONFIG_PMIC_SETTING
	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801){
	    if (chip_id == CHIP_REV_U02) {
	        syscfg_num = sizeof(syscfg_tbl_pmic_u02) / sizeof(u32) / 2;
	        for (cnt = 0; cnt < syscfg_num; cnt++) {
	            ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl_pmic_u02[cnt][0], syscfg_tbl_pmic_u02[cnt][1]);
	            if (ret) {
					AICWFDBG(LOGERROR, "%x write fail: %d\n", syscfg_tbl_pmic_u02[cnt][0], ret);
	                return;
	            }
	        }
	    }
	}
#endif
	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801){
	    if ((chip_id == CHIP_REV_U03) && (chip_sub_id == CHIP_SUB_REV_U04)) {
	        syscfg_num = sizeof(syscfg_tbl_u04) / sizeof(u32) / 2;
			AICWFDBG(LOGINFO, "cfg u04\n");
	        for (cnt = 0; cnt < syscfg_num; cnt++) {
	            ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl_u04[cnt][0], syscfg_tbl_u04[cnt][1]);
	            if (ret) {
					AICWFDBG(LOGERROR, "%x write fail: %d\n", syscfg_tbl_u04[cnt][0], ret);
	                return;
	            }
	        }
	    }
	}

	syscfg_num = sizeof(syscfg_tbl) / sizeof(u32) / 2;

    for (cnt = 0; cnt < syscfg_num; cnt++) {
        ret = rwnx_send_dbg_mem_write_req(rwnx_hw, syscfg_tbl[cnt][0], syscfg_tbl[cnt][1]);
        if (ret) {
			AICWFDBG(LOGERROR, "%x write fail: %d\n", syscfg_tbl[cnt][0], ret);
            return;
        }
    }
    syscfg_num = sizeof(syscfg_tbl_masked) / sizeof(u32) / 3;
    for (cnt = 0; cnt < syscfg_num; cnt++) {
	    if (syscfg_tbl_masked[cnt][0] == 0x00000000) {
            break;
        }

        ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
            syscfg_tbl_masked[cnt][0], syscfg_tbl_masked[cnt][1], syscfg_tbl_masked[cnt][2]);
        if (ret) {
			AICWFDBG(LOGERROR, "%x mask write fail: %d\n", syscfg_tbl_masked[cnt][0], ret);
            return;
        }
    }


}


static void system_config(struct rwnx_hw *rwnx_hw)
{
	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801){
		system_config_8800(rwnx_hw);
	}else if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW){
		system_config_8800dc(rwnx_hw);
	}
}

static int wdt_config(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;
    ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
                wdt_tbl_masked[0][0], wdt_tbl_masked[0][1], wdt_tbl_masked[0][2]);
    if (ret) {
        printk("wdt config %x write fail: %d\n", wdt_tbl_masked[0][0], ret);
    }
    return ret;
}
#if 0
static void rf_config(struct rwnx_hw *rwnx_hw)
{
    int ret;
    ret = rwnx_send_dbg_mem_mask_write_req(rwnx_hw,
                rf_tbl_masked[0][0], rf_tbl_masked[0][1], rf_tbl_masked[0][2]);
    if (ret) {
        printk("rf config %x write fail: %d\n", rf_tbl_masked[0][0], ret);
    }
}
#endif
#if 0
static int start_from_bootrom(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

	/* memory access */
#ifdef CONFIG_ROM_PATCH_EN
	const u32 rd_addr = ROM_FMAC_FW_ADDR;
	const u32 fw_addr = ROM_FMAC_FW_ADDR;
#else
	const u32 rd_addr = RAM_FMAC_FW_ADDR;
	const u32 fw_addr = RAM_FMAC_FW_ADDR;
#endif
	struct dbg_mem_read_cfm rd_cfm;
	printk("Read FW mem: %08x\n", rd_addr);
	if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_addr, &rd_cfm))) {
		return -1;
	}
	printk("cfm: [%08x] = %08x\n", rd_cfm.memaddr, rd_cfm.memdata);

	/* fw start */
	printk("Start app: %08x\n", fw_addr);
	if ((ret = rwnx_send_dbg_start_app_req(rwnx_hw, fw_addr, HOST_START_APP_AUTO))) {
		return -1;
	}
	return 0;
}
#endif
/**
 *
 */


#ifdef CONFIG_GPIO_WAKEUP
static const struct wiphy_wowlan_support aic_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_MAGIC_PKT,
};
#endif

extern int get_hardware_info(void);

#ifdef AICWF_USB_SUPPORT
u32 usbcfg_tbl[][2] = {
    {0x40200028, 0x0021047e},
    {0x40200024, 0x0000011d},
};

static void aicwf_usb_config(struct rwnx_hw *rwnx_hw)
{
    int usbcfg_num = 0;
    int ret = 0, cnt = 0;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
    const u32 mem_addr = 0x40200024;

    ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        AICWFDBG(LOGERROR, "%x rd fail: %d\n", mem_addr, ret);
        return;
    }
    AICWFDBG(LOGINFO, "usb config read %x\n", rd_mem_addr_cfm.memdata);
    if ((rd_mem_addr_cfm.memdata & 0xffff) == 0x119) {
        cnt = 0;
        usbcfg_num = sizeof(usbcfg_tbl) / sizeof(u32) / 2;
        for (cnt = 0; cnt < usbcfg_num; cnt++) {
            ret = rwnx_send_dbg_mem_write_req(rwnx_hw, usbcfg_tbl[cnt][0], usbcfg_tbl[cnt][1]);
            if (ret) {
                AICWFDBG(LOGERROR, "%x write fail: %d\n", usbcfg_tbl[cnt][0], ret);
                return;
            }
        }
    }
}
#endif // (AICWF_USB_SUPPORT)

static int start_from_bootrom(struct rwnx_hw *rwnx_hw)
{
    int ret = 0;

	/* memory access */
#ifdef CONFIG_ROM_PATCH_EN
	const u32 rd_addr = RAM_LMAC_FW_ADDR;
	const u32 fw_addr = RAM_LMAC_FW_ADDR;
#else
	const u32 rd_addr = RAM_FMAC_FW_ADDR;
	const u32 fw_addr = RAM_FMAC_FW_ADDR;
#endif
    u32 boot_type;
	struct dbg_mem_read_cfm rd_cfm;
	AICWFDBG(LOGINFO, "Read FW mem: %08x\n", rd_addr);
	if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, rd_addr, &rd_cfm))) {
		return -1;
	}
	AICWFDBG(LOGINFO, "cfm: [%08x] = %08x\n", rd_cfm.memaddr, rd_cfm.memdata);

    if (testmode == 0) {
        boot_type = HOST_START_APP_DUMMY;
    } else {
        boot_type = HOST_START_APP_AUTO;
    }
	/* fw start */
	AICWFDBG(LOGINFO, "Start app: %08x, %d\n", fw_addr, boot_type);
	if ((ret = rwnx_send_dbg_start_app_req(rwnx_hw, fw_addr, boot_type))) {
		return -1;
	}
	return 0;
}


int rwnx_ic_system_init(struct rwnx_hw *rwnx_hw){

	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801){
		system_config(rwnx_hw);
	}else if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW){
#ifdef AICWF_USB_SUPPORT
		aicwf_usb_config(rwnx_hw);
#endif
		system_config(rwnx_hw);
		if (rwnx_platform_on(rwnx_hw, NULL))
			return -1;

#if defined(CONFIG_START_FROM_BOOTROM)
        if (chip_sub_id < 2) {
            if (wdt_config(rwnx_hw)) {
                return -1;
            }
        }
		if (start_from_bootrom(rwnx_hw))
			return -1;
#endif
	}
	return 0;
}


int rwnx_ic_rf_init(struct rwnx_hw *rwnx_hw){
	struct mm_set_rf_calib_cfm cfm;
	int ret = 0;

	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801){
		if ((ret = rwnx_send_txpwr_idx_req(rwnx_hw))) {
			return -1;
		}

		if ((ret = rwnx_send_txpwr_ofst_req(rwnx_hw))) {
			return -1;
		}

		if ((ret = rwnx_send_rf_calib_req(rwnx_hw, &cfm)))
			return -1;

	}else if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
			rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW){

		if ((ret = aicwf_set_rf_config_8800dc(rwnx_hw, &cfm)))
			return -1;


	}
	return 0;
}

extern void *aicwf_prealloc_txq_alloc(size_t size);

int rwnx_cfg80211_init(struct rwnx_plat *rwnx_plat, void **platform_data)
{
    struct rwnx_hw *rwnx_hw;
    struct rwnx_conf_file init_conf;
    int ret = 0;
    struct wiphy *wiphy;
    struct rwnx_vif *vif;
    int i;
    u8 dflt_mac[ETH_ALEN] = { 0x88, 0x00, 0x33, 0x77, 0x10, 0x99};
    struct mm_get_fw_version_cfm fw_version;
    u8_l mac_addr_efuse[ETH_ALEN];
#ifndef USE_5G
    struct aic_feature_t feature;
#endif
    struct mm_set_stack_start_cfm set_start_cfm;

    int nx_remote_sta_max = NX_REMOTE_STA_MAX;

    RWNX_DBG(RWNX_FN_ENTRY_STR);


if((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8801) ||
    ((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
    g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW) && chip_id < 3)){
        nx_remote_sta_max = NX_REMOTE_STA_MAX_FOR_OLD_IC;
}


//#ifndef CONFIG_RFTEST
#ifdef CONFIG_MAC_RANDOM_IF_NO_MAC_IN_EFUSE
    get_random_bytes(&dflt_mac[4], 2);
#endif
//#endif
    /* create a new wiphy for use with cfg80211 */
    AICWFDBG(LOGINFO, "%s sizeof(struct rwnx_hw):%d \r\n", __func__, (int)sizeof(struct rwnx_hw));
    wiphy = wiphy_new(&rwnx_cfg80211_ops, sizeof(struct rwnx_hw));

    if (!wiphy) {
        dev_err(rwnx_platform_get_dev(rwnx_plat), "Failed to create new wiphy\n");
        ret = -ENOMEM;
        goto err_out;
    }

    rwnx_hw = wiphy_priv(wiphy);
    rwnx_hw->wiphy = wiphy;
    rwnx_hw->plat = rwnx_plat;
    rwnx_hw->dev = rwnx_platform_get_dev(rwnx_plat);
#ifdef AICWF_SDIO_SUPPORT
    rwnx_hw->sdiodev = rwnx_plat->sdiodev;
    rwnx_plat->sdiodev->rwnx_hw = rwnx_hw;
    rwnx_hw->cmd_mgr = &rwnx_plat->sdiodev->cmd_mgr;
#else
    rwnx_hw->usbdev = rwnx_plat->usbdev;
    rwnx_plat->usbdev->rwnx_hw = rwnx_hw;
    rwnx_hw->cmd_mgr = &rwnx_plat->usbdev->cmd_mgr;
#endif
    rwnx_hw->mod_params = &rwnx_mod_params;
    rwnx_hw->tcp_pacing_shift = 7;

    rwnx_init_aic(rwnx_hw);
    /* set device pointer for wiphy */
    set_wiphy_dev(wiphy, rwnx_hw->dev);

    /* Create cache to allocate sw_txhdr */
    rwnx_hw->sw_txhdr_cache = KMEM_CACHE(rwnx_sw_txhdr, 0);
    if (!rwnx_hw->sw_txhdr_cache) {
        wiphy_err(wiphy, "Cannot allocate cache for sw TX header\n");
        ret = -ENOMEM;
        goto err_cache;
    }

#if 0
    if ((ret = rwnx_parse_configfile(rwnx_hw, RWNX_CONFIG_FW_NAME, &init_conf))) {
        wiphy_err(wiphy, "rwnx_parse_configfile failed\n");
        goto err_config;
    }
#else
    memcpy(init_conf.mac_addr, dflt_mac, ETH_ALEN);
#endif

    rwnx_hw->vif_started = 0;
    rwnx_hw->monitor_vif = RWNX_INVALID_VIF;
    rwnx_hw->adding_sta = false;

    rwnx_hw->scan_ie.addr = NULL;

    for (i = 0; i < NX_VIRT_DEV_MAX + nx_remote_sta_max; i++){
        rwnx_hw->avail_idx_map |= BIT(i);
    }

    rwnx_hwq_init(rwnx_hw);
    
#ifdef CONFIG_PREALLOC_TXQ
    rwnx_hw->txq = (struct rwnx_txq*)aicwf_prealloc_txq_alloc(sizeof(struct rwnx_txq)*NX_NB_TXQ);
#endif

    for (i = 0; i < NX_NB_TXQ; i++) {
        rwnx_hw->txq[i].idx = TXQ_INACTIVE;
    }

    rwnx_mu_group_init(rwnx_hw);

    /* Initialize RoC element pointer to NULL, indicate that RoC can be started */
    rwnx_hw->roc_elem = NULL;
    /* Cookie can not be 0 */
    rwnx_hw->roc_cookie_cnt = 1;

    INIT_LIST_HEAD(&rwnx_hw->vifs);
    mutex_init(&rwnx_hw->mutex);
    mutex_init(&rwnx_hw->dbgdump_elem.mutex);
    spin_lock_init(&rwnx_hw->tx_lock);
    spin_lock_init(&rwnx_hw->cb_lock);

	INIT_WORK(&rwnx_hw->apmStalossWork, apm_staloss_work_process);
	rwnx_hw->apmStaloss_wq = create_singlethread_workqueue("apmStaloss_wq");
	if (!rwnx_hw->apmStaloss_wq) {
		txrx_err("insufficient memory to create apmStaloss workqueue.\n");
		goto err_cache;
	}

    wiphy->mgmt_stypes = rwnx_default_mgmt_stypes;

#ifdef CONFIG_FWLOG_EN
    rwnx_hw->fwlog_en = true;
#else
    rwnx_hw->fwlog_en = false;
#endif
	//init ic system
	if((ret = rwnx_ic_system_init(rwnx_hw))){
		goto err_lmac_reqs;
	}

#ifdef USE_5G
	ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, 0, CO_BIT(5), rwnx_hw->fwlog_en, &set_start_cfm);
	if(rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
			rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW){
		set_start_cfm.is_5g_support = false;
	}
#else
	ret = rwnx_send_set_stack_start_req(rwnx_hw, 1, get_hardware_info(), feature.hwinfo, rwnx_hw->fwlog_en, &set_start_cfm);
#endif

    if (ret){
        goto err_lmac_reqs;
    }

	AICWFDBG(LOGINFO, "is 5g support = %d, vendor_info = 0x%02X\n", set_start_cfm.is_5g_support, set_start_cfm.vendor_info);
	rwnx_hw->band_5g_support = set_start_cfm.is_5g_support;

    ret = rwnx_send_get_fw_version_req(rwnx_hw, &fw_version);
    memcpy(wiphy->fw_version, fw_version.fw_version, fw_version.fw_version_len>32? 32 : fw_version.fw_version_len>32);
	AICWFDBG(LOGINFO, "Firmware Version: %s\r\n", fw_version.fw_version);

    wiphy->bands[NL80211_BAND_2GHZ] = &rwnx_band_2GHz;
//#ifdef USE_5G
	if(rwnx_hw->band_5g_support){
    	wiphy->bands[NL80211_BAND_5GHZ] = &rwnx_band_5GHz;
	}
//#endif
    wiphy->interface_modes =
    BIT(NL80211_IFTYPE_STATION)     |
    BIT(NL80211_IFTYPE_AP)          |
    BIT(NL80211_IFTYPE_AP_VLAN)     |
    BIT(NL80211_IFTYPE_P2P_CLIENT)  |
    BIT(NL80211_IFTYPE_P2P_GO)      |
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
    #ifndef CONFIG_USE_P2P0
    BIT(NL80211_IFTYPE_P2P_DEVICE)  |
    #endif
    #endif
    BIT(NL80211_IFTYPE_MONITOR);

#ifdef CONFIG_GPIO_WAKEUP
		/* Set WoWLAN flags */
		printk("%s Wowlan support\r\n", __func__);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		wiphy->wowlan = &aic_wowlan_support;
#else
		wiphy->wowlan.flags = aic_wowlan_support.flags;
#endif
#endif

    wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
        #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
        WIPHY_FLAG_HAS_CHANNEL_SWITCH |
        #endif
        WIPHY_FLAG_4ADDR_STATION |
        WIPHY_FLAG_4ADDR_AP;

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    wiphy->max_num_csa_counters = BCN_MAX_CSA_CPT;
    #endif

    wiphy->max_remain_on_channel_duration = rwnx_hw->mod_params->roc_dur_max;

    wiphy->features |= NL80211_FEATURE_NEED_OBSS_SCAN |
        NL80211_FEATURE_SK_TX_STATUS |
        NL80211_FEATURE_VIF_TXPOWER |
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
        NL80211_FEATURE_ACTIVE_MONITOR |
        #endif
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
        NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
        #endif
        0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) || defined(CONFIG_WPA3_FOR_OLD_KERNEL)
    wiphy->features |= NL80211_FEATURE_SAE;
#endif

    if (rwnx_mod_params.tdls)
        /* TDLS support */
        wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;

    wiphy->iface_combinations   = rwnx_combinations;
    /* -1 not to include combination with radar detection, will be re-added in
       rwnx_handle_dynparams if supported */
    wiphy->n_iface_combinations = ARRAY_SIZE(rwnx_combinations) - 1;
    wiphy->reg_notifier = rwnx_reg_notifier;

    wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

    wiphy->cipher_suites = cipher_suites;
    wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites) - NB_RESERVED_CIPHER;

    rwnx_hw->ext_capa[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
    rwnx_hw->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
    wiphy->extended_capabilities = rwnx_hw->ext_capa;
    wiphy->extended_capabilities_mask = rwnx_hw->ext_capa;
    wiphy->extended_capabilities_len = ARRAY_SIZE(rwnx_hw->ext_capa);
#endif
    tasklet_init(&rwnx_hw->task, rwnx_task, (unsigned long)rwnx_hw);

	//init ic rf
	if((ret = rwnx_ic_rf_init(rwnx_hw))){
		goto err_lmac_reqs;
	}

    if ((ret = rwnx_send_get_macaddr_req(rwnx_hw, (struct mm_get_mac_addr_cfm *)mac_addr_efuse)))
        goto err_lmac_reqs;
    if (mac_addr_efuse[0] | mac_addr_efuse[1] | mac_addr_efuse[2] | mac_addr_efuse[3])
    {
        memcpy(init_conf.mac_addr, mac_addr_efuse, ETH_ALEN);
    }else{
    	AICWFDBG(LOGERROR, "no mac address in efuse!");
	}

	AICWFDBG(LOGINFO, "get macaddr:%x,%x\r\n", mac_addr_efuse[0], mac_addr_efuse[5]);

    memcpy(wiphy->perm_addr, init_conf.mac_addr, ETH_ALEN);

    /* Reset FW */
    if ((ret = rwnx_send_reset(rwnx_hw)))
        goto err_lmac_reqs;

    if ((ret = rwnx_send_version_req(rwnx_hw, &rwnx_hw->version_cfm)))
        goto err_lmac_reqs;
    rwnx_set_vers(rwnx_hw);

    if ((ret = rwnx_handle_dynparams(rwnx_hw, rwnx_hw->wiphy)))
        goto err_lmac_reqs;

    rwnx_enable_mesh(rwnx_hw);
    rwnx_radar_detection_init(&rwnx_hw->radar);

    /* Set parameters to firmware */

	if (rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801 ||
		((rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW) && testmode == 0)) {
    	rwnx_send_me_config_req(rwnx_hw);
	}

    /* Only monitor mode supported when custom channels are enabled */
    if (rwnx_mod_params.custchan) {
        rwnx_limits[0].types = BIT(NL80211_IFTYPE_MONITOR);
        rwnx_limits_dfs[0].types = BIT(NL80211_IFTYPE_MONITOR);
    }

    if ((ret = wiphy_register(wiphy))) {
        wiphy_err(wiphy, "Could not register wiphy device\n");
        goto err_register_wiphy;
    }

    /* Update regulatory (if needed) and set channel parameters to firmware
       (must be done after WiPHY registration) */
    rwnx_custregd(rwnx_hw, wiphy);
	if (rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8801 ||
		((rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		rwnx_hw->usbdev->chipid == PRODUCT_ID_AIC8800DW) && testmode == 0)) {
    	rwnx_send_me_chan_config_req(rwnx_hw);
		#ifdef CONFIG_COEX
    	rwnx_send_coex_req(rwnx_hw, 0, 1);
    	#endif
	}
    *platform_data = rwnx_hw;

#ifdef CONFIG_DEBUG_FS
    if ((ret = rwnx_dbgfs_register(rwnx_hw, "rwnx"))) {
        wiphy_err(wiphy, "Failed to register debugfs entries");
        goto err_debugfs;
    }
#endif
    rtnl_lock();

    /* Add an initial station interface */
    vif = rwnx_interface_add(rwnx_hw, "wlan%d", NET_NAME_UNKNOWN,
                                NL80211_IFTYPE_STATION, NULL);

    #ifdef CONFIG_RWNX_MON_DATA
    /* Add an initial station interface */
    vif = rwnx_interface_add(rwnx_hw, "wlan%d", 1,
                                NL80211_IFTYPE_MONITOR, NULL);
    #endif

    rtnl_unlock();

    if (!vif) {
        wiphy_err(wiphy, "Failed to instantiate a network device\n");
        ret = -ENOMEM;
        goto err_add_interface;
    }

#if 0
    wiphy_info(wiphy, "New interface create %s", vif->ndev->name);
#endif

	AICWFDBG(LOGINFO, "New interface create %s\r\n", vif->ndev->name);

#ifdef  CONFIG_USE_P2P0

        rtnl_lock();
        /* Add an initial p2p0 interface */
        vif = rwnx_interface_add(rwnx_hw, "p2p%d", NET_NAME_UNKNOWN,
                                    NL80211_IFTYPE_STATION, NULL);
        vif->is_p2p_vif = 1;
        rtnl_unlock();

        if (!vif) {
            wiphy_err(wiphy, "Failed to instantiate a network device\n");
            ret = -ENOMEM;
            goto err_add_interface;
        }

        //wiphy_info(wiphy, "New interface create %s", vif->ndev->name);
        AICWFDBG(LOGINFO, "New interface create %s \r\n", vif->ndev->name);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
        init_timer(&rwnx_hw->p2p_alive_timer);
        rwnx_hw->p2p_alive_timer.data = (unsigned long)vif;
        rwnx_hw->p2p_alive_timer.function = aicwf_p2p_alive_timeout;
#else
        timer_setup(&rwnx_hw->p2p_alive_timer, aicwf_p2p_alive_timeout, 0);
#endif
        rwnx_hw->is_p2p_alive = 0;
        rwnx_hw->is_p2p_connected = 0;
        atomic_set(&rwnx_hw->p2p_alive_timer_count, 0);
#endif


    return 0;

err_add_interface:
#ifdef CONFIG_DEBUG_FS
    rwnx_dbgfs_unregister(rwnx_hw);
err_debugfs:
#endif
if(rwnx_hw->wiphy){
    wiphy_unregister(rwnx_hw->wiphy);
}
err_register_wiphy:
err_lmac_reqs:
	AICWFDBG(LOGERROR, "err_lmac_reqs\n");
    //rwnx_fw_trace_dump(rwnx_hw);
    rwnx_platform_off(rwnx_hw, NULL);
//err_platon:
//err_config:
    kmem_cache_destroy(rwnx_hw->sw_txhdr_cache);
err_cache:
    wiphy_free(wiphy);
err_out:
    return ret;
}

/**
 *
 */

void rwnx_cfg80211_deinit(struct rwnx_hw *rwnx_hw)
{
    struct mm_set_stack_start_cfm set_start_cfm;

    RWNX_DBG(RWNX_FN_ENTRY_STR);

    rwnx_send_set_stack_start_req(rwnx_hw, 0, 0, 0, 0, &set_start_cfm);

    rwnx_hw->fwlog_en = 0;

#ifdef CONFIG_DEBUG_FS
    rwnx_dbgfs_unregister(rwnx_hw);
#endif
	flush_workqueue(rwnx_hw->apmStaloss_wq);
	destroy_workqueue(rwnx_hw->apmStaloss_wq);

    rwnx_wdev_unregister(rwnx_hw);
	if(rwnx_hw->wiphy){
		AICWFDBG(LOGINFO, "%s wiphy_unregister \r\n", __func__);
    	wiphy_unregister(rwnx_hw->wiphy);
	}
    rwnx_radar_detection_deinit(&rwnx_hw->radar);
    rwnx_platform_off(rwnx_hw, NULL);
    kmem_cache_destroy(rwnx_hw->sw_txhdr_cache);
	if(rwnx_hw->wiphy){
    	wiphy_free(rwnx_hw->wiphy);
	}
}

static void aicsmac_driver_register(void)
{
#ifdef AICWF_SDIO_SUPPORT
    aicwf_sdio_register();
#endif
#ifdef AICWF_USB_SUPPORT
    aicwf_usb_register();
#endif
#ifdef AICWF_PCIE_SUPPORT
    aicwf_pcie_register();
#endif
}

//static DECLARE_WORK(aicsmac_driver_work, aicsmac_driver_register);

struct completion hostif_register_done;

#define REGISTRATION_TIMEOUT                     9000

void aicwf_hostif_ready(void)
{
	g_rwnx_plat->enabled = true;
	complete(&hostif_register_done);
}

static int __init rwnx_mod_init(void)
{

    RWNX_DBG(RWNX_FN_ENTRY_STR);
    rwnx_print_version();
	AICWFDBG(LOGINFO, "RELEASE DATE:%s \r\n", RELEASE_DATE);
	rwnx_init_cmd_array();

	sema_init(&aicwf_deinit_sem, 1);
	atomic_set(&aicwf_deinit_atomic, 1);

	init_completion(&hostif_register_done);

	aicsmac_driver_register();

#ifdef AICWF_SDIO_SUPPORT
	if ((wait_for_completion_timeout(&hostif_register_done, msecs_to_jiffies(REGISTRATION_TIMEOUT)) == 0)) {
		AICWFDBG(LOGERROR, "register_driver timeout or error\n");
        aicwf_sdio_exit();
	return -ENODEV;
}

#endif /* AICWF_SDIO_SUPPORT */
#ifdef AICWF_USB_SUPPORT
       //aicwf_usb_exit();
#endif /*AICWF_USB_SUPPORT */


#ifdef AICWF_PCIE_SUPPORT
    return rwnx_platform_register_drv();
#else
    return 0;
#endif
}

/**
 *
 */
static void __exit rwnx_mod_exit(void)
{
    RWNX_DBG(RWNX_FN_ENTRY_STR);

#ifdef AICWF_PCIE_SUPPORT
    rwnx_platform_unregister_drv();
#endif

#ifdef AICWF_SDIO_SUPPORT
    aicwf_sdio_exit();
#endif

#ifdef AICWF_USB_SUPPORT
    aicwf_usb_exit();
#endif
	rwnx_free_cmd_array();
	AICWFDBG(LOGINFO, "%s exit\r\n", __func__);
}

module_init(rwnx_mod_init);
module_exit(rwnx_mod_exit);


MODULE_FIRMWARE(RWNX_CONFIG_FW_NAME);

MODULE_DESCRIPTION(RW_DRV_DESCRIPTION);
MODULE_VERSION(RWNX_VERS_MOD);
MODULE_AUTHOR(RW_DRV_COPYRIGHT " " RW_DRV_AUTHOR);
MODULE_LICENSE("GPL");

