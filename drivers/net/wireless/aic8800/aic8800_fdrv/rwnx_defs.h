// SPDX-License-Identifier: GPL-2.0-or-later
/**
 ******************************************************************************
 *
 * @file rwnx_defs.h
 *
 * @brief Main driver structure declarations for fullmac driver
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#ifndef _RWNX_DEFS_H_
#define _RWNX_DEFS_H_

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <linux/slab.h>

#include "rwnx_mod_params.h"
#include "rwnx_debugfs.h"
#include "rwnx_tx.h"
#include "rwnx_rx.h"
#include "rwnx_radar.h"
#include "rwnx_utils.h"
#include "rwnx_mu_group.h"
#include "rwnx_platform.h"
#include "rwnx_cmds.h"

#ifdef AICWF_SDIO_SUPPORT
#include "aicwf_sdio.h"
#include "sdio_host.h"
#endif

#ifdef AICWF_USB_SUPPORT
#include "usb_host.h"
#endif

#ifdef CONFIG_BR_SUPPORT
#include "aic_br_ext.h"
#endif /* CONFIG_BR_SUPPORT */

#define WPI_HDR_LEN    18
#define WPI_PN_LEN     16
#define WPI_PN_OFST     2
#define WPI_MIC_LEN    16
#define WPI_KEY_LEN    32
#define WPI_SUBKEY_LEN 16 // WPI key is actually two 16bytes key

#define LEGACY_PS_ID   0
#define UAPSD_ID       1

#define PS_SP_INTERRUPTED  255
#define MAC_ADDR_LEN 6

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
enum nl80211_ac {
        NL80211_AC_VO,
        NL80211_AC_VI,
        NL80211_AC_BE,
        NL80211_AC_BK,
        NL80211_NUM_ACS
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
struct ieee80211_vht_operation {
        u8 vht_op_info_chwidth;
        u8 vht_op_info_chan_center_freq_seg1_idx;
        u8 vht_op_info_chan_center_freq_seg2_idx;
        __le16 vht_basic_mcs_set;
} __packed;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
#define NL80211_IFTYPE_P2P_DEVICE 10
#define IEEE80211_RADIOTAP_AMPDU_STATUS 20
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0) || defined(CONFIG_VHT_FOR_OLD_KERNEL)
#define IEEE80211_RADIOTAP_VHT                                  21
#define IEEE80211_RADIOTAP_VHT_KNOWN_GI                         0x0004
#define IEEE80211_RADIOTAP_VHT_KNOWN_BANDWIDTH                  0x0040

#define IEEE80211_RADIOTAP_VHT_FLAG_STBC                        0x01
#define IEEE80211_RADIOTAP_VHT_FLAG_SGI                         0x04

#define NL80211_FEATURE_CELL_BASE_REG_HINTS              1 << 3
#define NL80211_FEATURE_P2P_DEVICE_NEEDS_CHANNEL         1 << 4
#define NL80211_FEATURE_SAE                              1 << 5
#define NL80211_FEATURE_LOW_PRIORITY_SCAN                1 << 6
#define NL80211_FEATURE_SCAN_FLUSH                       1 << 7
#define NL80211_FEATURE_AP_SCAN                          1 << 8
#define NL80211_FEATURE_VIF_TXPOWER                      1 << 9
#define NL80211_FEATURE_NEED_OBSS_SCAN                   1 << 10
#define NL80211_FEATURE_P2P_GO_CTWIN                     1 << 11
#define NL80211_FEATURE_P2P_GO_OPPPS                     1 << 12

/* 802.11ac VHT Capabilities */
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895                  0x00000000
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991                  0x00000001
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454                 0x00000002
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ                0x00000004
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ       0x00000008
#define IEEE80211_VHT_CAP_RXLDPC                                0x00000010
#define IEEE80211_VHT_CAP_SHORT_GI_80                           0x00000020
#define IEEE80211_VHT_CAP_SHORT_GI_160                          0x00000040
#define IEEE80211_VHT_CAP_TXSTBC                                0x00000080
#define IEEE80211_VHT_CAP_RXSTBC_1                              0x00000100
#define IEEE80211_VHT_CAP_RXSTBC_2                              0x00000200
#define IEEE80211_VHT_CAP_RXSTBC_3                              0x00000300
#define IEEE80211_VHT_CAP_RXSTBC_4                              0x00000400
#define IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE                 0x00000800
#define IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE                 0x00001000
#define IEEE80211_VHT_CAP_BEAMFORMER_ANTENNAS_MAX               0x00006000
#define IEEE80211_VHT_CAP_SOUNDING_DIMENTION_MAX                0x00030000
#define IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE                 0x00080000
#define IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE                 0x00100000
#define IEEE80211_VHT_CAP_VHT_TXOP_PS                           0x00200000
#define IEEE80211_VHT_CAP_HTC_VHT                               0x00400000
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT      23
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK       \
                (7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT)
#define IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB     0x08000000
#define IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB       0x0c000000
#define IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN                    0x10000000
#define IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN                    0x20000000

enum ieee80211_vht_mcs_support {
        IEEE80211_VHT_MCS_SUPPORT_0_7   = 0,
        IEEE80211_VHT_MCS_SUPPORT_0_8   = 1,
        IEEE80211_VHT_MCS_SUPPORT_0_9   = 2,
        IEEE80211_VHT_MCS_NOT_SUPPORTED = 3,
};

enum nl80211_chan_width {
        NL80211_CHAN_WIDTH_20_NOHT,
        NL80211_CHAN_WIDTH_20,
        NL80211_CHAN_WIDTH_40,
        NL80211_CHAN_WIDTH_80,
        NL80211_CHAN_WIDTH_80P80,
        NL80211_CHAN_WIDTH_160,
};

struct cfg80211_chan_def {
        struct ieee80211_channel *chan;
        enum nl80211_chan_width width;
        u32 center_freq1;
        u32 center_freq2;
};

enum nl80211_mesh_power_mode {
        NL80211_MESH_POWER_UNKNOWN,
        NL80211_MESH_POWER_ACTIVE,
        NL80211_MESH_POWER_LIGHT_SLEEP,
        NL80211_MESH_POWER_DEEP_SLEEP,
        __NL80211_MESH_POWER_AFTER_LAST,
        NL80211_MESH_POWER_MAX = __NL80211_MESH_POWER_AFTER_LAST - 1
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
#define NL80211_MESHCONF_POWER_MODE 26

/*
 * TDLS capabililites to be enabled in the 5th byte of the
 * @WLAN_EID_EXT_CAPABILITY information element
 */
#define WLAN_EXT_CAPA5_TDLS_ENABLED	BIT(5)
#define WLAN_EXT_CAPA5_TDLS_PROHIBITED	BIT(6)

#define WLAN_EXT_CAPA8_OPMODE_NOTIF	BIT(6)

/* TDLS specific payload type in the LLC/SNAP header */
#define WLAN_TDLS_SNAP_RFTYPE	0x2

#endif

/**
 * struct rwnx_bcn - Information of the beacon in used (AP mode)
 *
 * @head: head portion of beacon (before TIM IE)
 * @tail: tail portion of beacon (after TIM IE)
 * @ies: extra IEs (not used ?)
 * @head_len: length of head data
 * @tail_len: length of tail data
 * @ies_len: length of extra IEs data
 * @tim_len: length of TIM IE
 * @len: Total beacon len (head + tim + tail + extra)
 * @dtim: dtim period
 */
struct rwnx_bcn {
    u8 *head;
    u8 *tail;
    u8 *ies;
    size_t head_len;
    size_t tail_len;
    size_t ies_len;
    size_t tim_len;
    size_t len;
    u8 dtim;
};

/**
 * struct rwnx_key - Key information
 *
 * @hw_idx: Idx of the key from hardware point of view
 */
struct rwnx_key {
    u8 hw_idx;
};

/**
 * Structure containing information about a Mesh Path
 */
struct rwnx_mesh_path {
    struct list_head list;          /* For rwnx_vif.mesh_paths */
    u8 path_idx;                    /* Path Index */
    struct mac_addr tgt_mac_addr;   /* Target MAC Address */
    struct rwnx_sta *p_nhop_sta;    /* Pointer to the Next Hop STA */
};

struct rwnx_mesh_proxy {
    struct list_head list;          /* For rwnx_vif.mesh_proxy */
    struct mac_addr ext_sta_addr;   /* Address of the External STA */
    struct mac_addr proxy_addr;     /* Proxy MAC Address */
    bool local;                     /* Indicate if interface is a proxy for the device */
};

/**
 * struct rwnx_csa - Information for CSA (Channel Switch Announcement)
 *
 * @vif: Pointer to the vif doing the CSA
 * @bcn: Beacon to use after CSA
 * @elem: IPC buffer to send the new beacon to the fw
 * @chandef: defines the channel to use after the switch
 * @count: Current csa counter
 * @status: Status of the CSA at fw level
 * @ch_idx: Index of the new channel context
 * @work: work scheduled at the end of CSA
 */
struct rwnx_csa {
    struct rwnx_vif *vif;
    struct rwnx_bcn bcn;
    struct rwnx_ipc_elem_var elem;
    struct cfg80211_chan_def chandef;
    int count;
    int status;
    int ch_idx;
    struct work_struct work;
};

struct apm_probe_sta {
       u8 sta_mac_addr[6];
       u8 vif_idx;
       u64 probe_id;
       struct work_struct apmprobestaWork;
       struct workqueue_struct *apmprobesta_wq;
};

/// Possible States of the TDLS link.
enum tdls_status_tag {
        /// TDLS link is not active (no TDLS peer connected)
        TDLS_LINK_IDLE,
        /// TDLS Setup Request transmitted
        TDLS_SETUP_REQ_TX,
        /// TDLS Setup Response transmitted
        TDLS_SETUP_RSP_TX,
        /// TDLS link is active (TDLS peer connected)
        TDLS_LINK_ACTIVE,
        /// TDLS Max Number of states.
        TDLS_STATE_MAX
};

/*
 * Structure used to save information relative to the TDLS peer.
 * This is also linked within the rwnx_hw vifs list.
 *
 */
struct rwnx_tdls {
    bool active;                /* Indicate if TDLS link is active */
    bool initiator;             /* Indicate if TDLS peer is the TDLS initiator */
    bool chsw_en;               /* Indicate if channel switch is enabled */
    u8 last_tid;                /* TID of the latest MPDU transmitted over the
                                   TDLS direct link to the TDLS STA */
    u16 last_sn;                /* Sequence number of the latest MPDU transmitted
                                   over the TDLS direct link to the TDLS STA */
    bool ps_on;                 /* Indicate if the power save is enabled on the
                                   TDLS STA */
    bool chsw_allowed;          /* Indicate if TDLS channel switch is allowed */
};


/**
 * enum rwnx_ap_flags - AP flags
 *
 * @RWNX_AP_ISOLATE Isolate clients (i.e. Don't brige packets transmitted by
 *                                   one client for another one)
 */
enum rwnx_ap_flags {
    RWNX_AP_ISOLATE = BIT(0),
};

/*
 * Structure used to save information relative to the managed interfaces.
 * This is also linked within the rwnx_hw vifs list.
 *
 */
struct rwnx_vif {
    struct list_head list;
    struct rwnx_hw *rwnx_hw;
    struct wireless_dev wdev;
    struct net_device *ndev;
    struct net_device_stats net_stats;
    struct rwnx_key key[6];
    atomic_t drv_conn_state;
    u8 drv_vif_index;           /* Identifier of the VIF in driver */
    u8 vif_index;               /* Identifier of the station in FW */
    u8 ch_index;                /* Channel context identifier */
    bool up;                    /* Indicate if associated netdev is up
                                   (i.e. Interface is created at fw level) */
    bool use_4addr;             /* Should we use 4addresses mode */
    bool is_resending;          /* Indicate if a frame is being resent on this interface */
    bool user_mpm;              /* In case of Mesh Point VIF, indicate if MPM is handled by userspace */
    bool roc_tdls;              /* Indicate if the ROC has been called by a
                                   TDLS station */
    u8 tdls_status;             /* Status of the TDLS link */
    bool tdls_chsw_prohibited;  /* Indicate if TDLS Channel Switch is prohibited */
    bool wep_enabled;           /* 1 if WEP is enabled */
    bool wep_auth_err;          /* 1 if auth status code is not supported auth alg when WEP enabled */
    enum nl80211_auth_type last_auth_type; /* Authentication type (algorithm) sent in the last connection
                                              when WEP enabled */
    union
    {
        struct
        {
            struct rwnx_sta *ap; /* Pointer to the peer STA entry allocated for
                                    the AP */
            struct rwnx_sta *tdls_sta; /* Pointer to the TDLS station */
            bool external_auth;  /* Indicate if external authentication is in progress */
            u8 group_cipher_type;
            u8 paired_cipher_type;
            //connected network info start
            char ssid[33];//ssid max is 32, but this has one spare for '\0'
            int ssid_len;
            u8 bssid[ETH_ALEN];
            //connected network info end
        } sta;
        struct
        {
            u16 flags;                 /* see rwnx_ap_flags */
            struct list_head sta_list; /* List of STA connected to the AP */
            struct rwnx_bcn bcn;       /* beacon */
            u8 bcmc_index;             /* Index of the BCMC sta to use */
            #if (defined CONFIG_HE_FOR_OLD_KERNEL) || (defined CONFIG_VHT_FOR_OLD_KERNEL)
			u8 aic_index;
            #endif
            struct rwnx_csa *csa;

            struct list_head mpath_list; /* List of Mesh Paths used on this interface */
            struct list_head proxy_list; /* List of Proxies Information used on this interface */
            bool create_path;            /* Indicate if we are waiting for a MESH_CREATE_PATH_CFM
                                            message */
            int generation;              /* Increased each time the list of Mesh Paths is updated */
            enum nl80211_mesh_power_mode mesh_pm; /* mesh power save mode currently set in firmware */
            enum nl80211_mesh_power_mode next_mesh_pm; /* mesh power save mode for next peer */
        } ap;
        struct
        {
            struct rwnx_vif *master;   /* pointer on master interface */
            struct rwnx_sta *sta_4a;
        } ap_vlan;
    };

	u8_l key_has_add;
	u8_l is_p2p_vif;
	struct apm_probe_sta sta_probe;

    #ifdef CONFIG_BR_SUPPORT
	spinlock_t			    br_ext_lock;
	/* unsigned int			macclone_completed; */
	struct nat25_network_db_entry	*nethash[NAT25_HASH_SIZE];
	int				pppoe_connection_in_progress;
	unsigned char			pppoe_addr[MACADDRLEN];
	unsigned char			scdb_mac[MACADDRLEN];
	unsigned char			scdb_ip[4];
	struct nat25_network_db_entry	*scdb_entry;
	unsigned char			br_mac[MACADDRLEN];
	unsigned char			br_ip[4];

	struct br_ext_info		ethBrExtInfo;
    #endif /* CONFIG_BR_SUPPORT */

};

#define RWNX_VIF_TYPE(rwnx_vif) (rwnx_vif->wdev.iftype)

/**
 * Structure used to store information relative to PS mode.
 *
 * @active: True when the sta is in PS mode.
 *          If false, other values should be ignored
 * @pkt_ready: Number of packets buffered for the sta in drv's txq
 *             (1 counter for Legacy PS and 1 for U-APSD)
 * @sp_cnt: Number of packets that remain to be pushed in the service period.
 *          0 means that no service period is in progress
 *          (1 counter for Legacy PS and 1 for U-APSD)
 */
struct rwnx_sta_ps {
    bool active;
    u16 pkt_ready[2];
    u16 sp_cnt[2];
};

/**
 * struct rwnx_rx_rate_stats - Store statistics for RX rates
 *
 * @table: Table indicating how many frame has been receive which each
 * rate index. Rate index is the same as the one used by RC algo for TX
 * @size: Size of the table array
 * @cpt: number of frames received
 */
struct rwnx_rx_rate_stats {
    int *table;
    int size;
    int cpt;
    int rate_cnt;
};

/**
 * struct rwnx_sta_stats - Structure Used to store statistics specific to a STA
 *
 * @last_rx: Hardware vector of the last received frame
 * @rx_rate: Statistics of the received rates
 */
struct rwnx_sta_stats {
//#ifdef CONFIG_RWNX_DEBUGFS
    struct hw_vect last_rx;
    struct rwnx_rx_rate_stats rx_rate;
//#endif
};

#if (defined CONFIG_HE_FOR_OLD_KERNEL) || (defined CONFIG_VHT_FOR_OLD_KERNEL)
struct aic_sta {
    u8 sta_idx;            /* Identifier of the station */
	bool he;               /* Flag indicating if the station supports HE */
    bool vht;               /* Flag indicating if the station supports VHT */
};
#endif

/*
 * Structure used to save information relative to the managed stations.
 */
struct rwnx_sta {
    struct list_head list;
    u16 aid;                /* association ID */
    u8 sta_idx;             /* Identifier of the station */
    u8 vif_idx;             /* Identifier of the VIF (fw id) the station
                               belongs to */
    u8 vlan_idx;            /* Identifier of the VLAN VIF (fw id) the station
                               belongs to (= vif_idx if no vlan in used) */
    enum nl80211_band band; /* Band */
    enum nl80211_chan_width width; /* Channel width */
    u16 center_freq;        /* Center frequency */
    u32 center_freq1;       /* Center frequency 1 */
    u32 center_freq2;       /* Center frequency 2 */
    u8 ch_idx;              /* Identifier of the channel
                               context the station belongs to */
    bool qos;               /* Flag indicating if the station
                               supports QoS */
    u8 acm;                 /* Bitfield indicating which queues
                               have AC mandatory */
    u16 uapsd_tids;         /* Bitfield indicating which tids are subject to
                               UAPSD */
    u8 mac_addr[ETH_ALEN];  /* MAC address of the station */
    struct rwnx_key key;
    bool valid;             /* Flag indicating if the entry is valid */
    struct rwnx_sta_ps ps;  /* Information when STA is in PS (AP only) */
#ifdef CONFIG_RWNX_BFMER
    struct rwnx_bfmer_report *bfm_report;     /* Beamforming report to be used for
                                                 VHT TX Beamforming */
#ifdef CONFIG_RWNX_MUMIMO_TX
    struct rwnx_sta_group_info group_info; /* MU grouping information for the STA */
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

    bool ht;               /* Flag indicating if the station
                               supports HT */
    bool vht;               /* Flag indicating if the station
                               supports VHT */
    u32 ac_param[AC_MAX];  /* EDCA parameters */
    struct rwnx_tdls tdls; /* TDLS station information */
    struct rwnx_sta_stats stats;
    enum nl80211_mesh_power_mode mesh_pm; /*  link-specific mesh power save mode */
};

static inline const u8 *rwnx_sta_addr(struct rwnx_sta *rwnx_sta) {
    return rwnx_sta->mac_addr;
}

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
struct rwnx_amsdu_stats {
    int done;
    int failed;
};
#endif

struct rwnx_stats {
    int cfm_balance[NX_TXQ_CNT];
    unsigned long last_rx, last_tx; /* jiffies */
    int ampdus_tx[IEEE80211_MAX_AMPDU_BUF];
    int ampdus_rx[IEEE80211_MAX_AMPDU_BUF];
    int ampdus_rx_map[4];
    int ampdus_rx_miss;
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
    struct rwnx_amsdu_stats amsdus[NX_TX_PAYLOAD_MAX];
#endif
    int amsdus_rx[64];
};

struct rwnx_sec_phy_chan {
    u16 prim20_freq;
    u16 center_freq1;
    u16 center_freq2;
    enum nl80211_band band;
    u8 type;
};

/* Structure that will contains all RoC information received from cfg80211 */
struct rwnx_roc_elem {
    struct wireless_dev *wdev;
    struct ieee80211_channel *chan;
    unsigned int duration;
    /* Used to avoid call of CFG80211 callback upon expiration of RoC */
    bool mgmt_roc;
    /* Indicate if we have switch on the RoC channel */
    bool on_chan;
};

/* Structure containing channel survey information received from MAC */
struct rwnx_survey_info {
    // Filled
    u32 filled;
    // Amount of time in ms the radio spent on the channel
    u32 chan_time_ms;
    // Amount of time the primary channel was sensed busy
    u32 chan_time_busy_ms;
    // Noise in dbm
    s8 noise_dbm;
};

#define RWNX_CH_NOT_SET 0xFF
#define RWNX_INVALID_VIF 0xFF
#define RWNX_INVALID_STA 0xFF

/* Structure containing channel context information */
struct rwnx_chanctx {
    struct cfg80211_chan_def chan_def; /* channel description */
    u8 count;                          /* number of vif using this ctxt */
};

/**
 * rwnx_phy_info - Phy information
 *
 * @phy_cnt: Number of phy interface
 * @cfg: Configuration send to firmware
 * @sec_chan: Channel configuration of the second phy interface (if phy_cnt > 1)
 * @limit_bw: Set to true to limit BW on requested channel. Only set to use
 * VHT with old radio that don't support 80MHz (deprecated)
 */
struct rwnx_phy_info {
    u8 cnt;
    struct phy_cfg_tag cfg;
    struct rwnx_sec_phy_chan sec_chan;
    bool limit_bw;
};

/* rwnx driver status */

enum rwnx_drv_connect_status { 
	RWNX_DRV_STATUS_DISCONNECTED = 0,
	RWNX_DRV_STATUS_DISCONNECTING, 
	RWNX_DRV_STATUS_CONNECTING, 
	RWNX_DRV_STATUS_CONNECTED, 
};


struct sta_tx_flowctrl {
	atomic_t tx_pending_cnt;
    u8 flowctrl;
};

struct rwnx_hw {
    struct rwnx_mod_params *mod_params;
    struct device *dev;
#ifdef AICWF_SDIO_SUPPORT
    struct aic_sdio_dev *sdiodev;
#endif
#ifdef AICWF_USB_SUPPORT
    struct aic_usb_dev *usbdev;
#endif
    struct wiphy *wiphy;
    struct list_head vifs;
    struct rwnx_vif *vif_table[NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX]; /* indexed with fw id */
    struct rwnx_sta sta_table[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];
    #ifdef CONFIG_HE_FOR_OLD_KERNEL
	struct aic_sta aic_table[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];
    #endif
    struct rwnx_survey_info survey[SCAN_CHANNEL_MAX];
    struct cfg80211_scan_request *scan_request;
    struct rwnx_chanctx chanctx_table[NX_CHAN_CTXT_CNT];
    u8 cur_chanctx;

    u8 monitor_vif; /* FW id of the monitor interface, RWNX_INVALID_VIF if no monitor vif at fw level */

    /* RoC Management */
    struct rwnx_roc_elem *roc_elem;             /* Information provided by cfg80211 in its remain on channel request */
    u32 roc_cookie_cnt;                         /* Counter used to identify RoC request sent by cfg80211 */

    struct rwnx_cmd_mgr *cmd_mgr;

    unsigned long drv_flags;
    struct rwnx_plat *plat;

    spinlock_t tx_lock;
    spinlock_t cb_lock;
    struct mutex mutex;                         /* per-device perimeter lock */

    struct tasklet_struct task;
    struct mm_version_cfm version_cfm;          /* Lower layers versions - obtained via MM_VERSION_REQ */

    u32 tcp_pacing_shift;

    /* IPC */
    struct ipc_host_env_tag *ipc_env;
#ifdef AICWF_SDIO_SUPPORT
    struct sdio_host_env_tag sdio_env;
#endif
#ifdef AICWF_USB_SUPPORT
    struct usb_host_env_tag usb_env;
#endif

    struct rwnx_ipc_elem_pool e2amsgs_pool;
    struct rwnx_ipc_elem_pool dbgmsgs_pool;
    struct rwnx_ipc_elem_pool e2aradars_pool;
    struct rwnx_ipc_elem_var pattern_elem;
    struct rwnx_ipc_dbgdump_elem dbgdump_elem;
    struct rwnx_ipc_elem_pool e2arxdesc_pool;
    struct rwnx_ipc_skb_elem *e2aunsuprxvec_elems;
    //struct rwnx_ipc_rxbuf_elems rxbuf_elems;
    struct rwnx_ipc_elem_var scan_ie;

    struct kmem_cache      *sw_txhdr_cache;

    struct rwnx_debugfs     debugfs;
    struct rwnx_stats       stats;

#ifdef CONFIG_PREALLOC_TXQ
    struct rwnx_txq *txq;
#else
    struct rwnx_txq txq[NX_NB_TXQ];
#endif
    struct rwnx_hwq hwq[NX_TXQ_CNT];

    u8 avail_idx_map;
    u8 vif_started;
    bool adding_sta;
    struct rwnx_phy_info phy;

    struct rwnx_radar radar;

    /* extended capabilities supported */
    u8 ext_capa[8];

#ifdef CONFIG_RWNX_MUMIMO_TX
    struct rwnx_mu_info mu;
#endif
    u8 is_p2p_alive;
    u8 is_p2p_connected;
    struct timer_list p2p_alive_timer;
    struct rwnx_vif *p2p_dev_vif;
    atomic_t p2p_alive_timer_count;
    bool band_5g_support;
    bool fwlog_en;

	struct work_struct apmStalossWork;
    struct workqueue_struct *apmStaloss_wq;
    u8 apm_vif_idx;
    u8 sta_mac_addr[6];

	struct sta_tx_flowctrl sta_flowctrl[NX_REMOTE_STA_MAX];
#if 0
	bool he_flag;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
    struct mac_chan_op ap_chan;
    struct ieee80211_channel set_chan;
#endif
#ifdef CONFIG_VHT_FOR_OLD_KERNEL
    struct ieee80211_sta_vht_cap vht_cap_2G;
    struct ieee80211_sta_vht_cap vht_cap_5G;
#endif

#ifdef CONFIG_USB_WIRELESS_EXT
	bool wext_scan;
	struct completion wext_scan_com;
	struct list_head wext_scanre_list;
	char wext_essid[32];
	int support_freqs[SCAN_CHANNEL_MAX];
	int support_freqs_number;
#endif
};

u8 *rwnx_build_bcn(struct rwnx_bcn *bcn, struct cfg80211_beacon_data *new);

void rwnx_chanctx_link(struct rwnx_vif *vif, u8 idx,
                        struct cfg80211_chan_def *chandef);
void rwnx_chanctx_unlink(struct rwnx_vif *vif);
int  rwnx_chanctx_valid(struct rwnx_hw *rwnx_hw, u8 idx);

extern u8 chip_id;
static inline bool is_multicast_sta(int sta_idx)
{

	if((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8801) || 
		((g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DC ||
		g_rwnx_plat->usbdev->chipid == PRODUCT_ID_AIC8800DW) && chip_id < 3)){
		return (sta_idx >= NX_REMOTE_STA_MAX_FOR_OLD_IC);
	}else{
		return (sta_idx >= NX_REMOTE_STA_MAX);
	}

}
struct rwnx_sta *rwnx_get_sta(struct rwnx_hw *rwnx_hw, const u8 *mac_addr);

static inline uint8_t master_vif_idx(struct rwnx_vif *vif)
{
    if (unlikely(vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN)) {
        return vif->ap_vlan.master->vif_index;
    } else {
        return vif->vif_index;
    }
}

void rwnx_external_auth_enable(struct rwnx_vif *vif);
void rwnx_external_auth_disable(struct rwnx_vif *vif);

#endif /* _RWNX_DEFS_H_ */
