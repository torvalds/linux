/**
 ******************************************************************************
 *
 * @file ecrnx_defs.h
 *
 * @brief Main driver structure declarations for fullmac driver
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_DEFS_H_
#define _ECRNX_DEFS_H_

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
#include <linux/vmalloc.h>
#include <linux/module.h>
#endif
#include "ecrnx_mod_params.h"
#include "ecrnx_debugfs.h"
#include "ecrnx_tx.h"
#include "ecrnx_rx.h"
#include "ecrnx_radar.h"
#include "ecrnx_utils.h"
#include "ecrnx_mu_group.h"
#include "ecrnx_platform.h"
#include "ecrnx_cmds.h"

#include "ecrnx_p2p.h"
#include "ecrnx_debug.h"
#include "ecrnx_cfgfile.h"


#define WPI_HDR_LEN    18
#define WPI_PN_LEN     16
#define WPI_PN_OFST     2
#define WPI_MIC_LEN    16
#define WPI_KEY_LEN    32
#define WPI_SUBKEY_LEN 16 // WPI key is actually two 16bytes key

#define LEGACY_PS_ID   0
#define UAPSD_ID       1

#define PS_SP_INTERRUPTED  255
#define ECRNX_RXSIZE       1024

#define CONFIG_ESWIN_RX_REORDER        1

#if defined(CONFIG_ECRNX_HE)
extern struct ieee80211_sta_he_cap ecrnx_he_cap;
#endif

/**
 * struct ecrnx_bcn - Information of the beacon in used (AP mode)
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
struct ecrnx_bcn {
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
 * struct ecrnx_key - Key information
 *
 * @hw_idx: Idx of the key from hardware point of view
 */
struct ecrnx_key {
    u8 hw_idx;
};

/**
 * Structure containing information about a Mesh Path
 */
struct ecrnx_mesh_path {
    struct list_head list;          /* For ecrnx_vif.mesh_paths */
    u8 path_idx;                    /* Path Index */
    struct mac_addr tgt_mac_addr;   /* Target MAC Address */
    struct ecrnx_sta *nhop_sta;      /* Pointer to the Next Hop STA */
};

struct ecrnx_mesh_proxy {
    struct list_head list;          /* For ecrnx_vif.mesh_proxy */
    struct mac_addr ext_sta_addr;   /* Address of the External STA */
    struct mac_addr proxy_addr;     /* Proxy MAC Address */
    bool local;                     /* Indicate if interface is a proxy for the device */
};

/**
 * struct ecrnx_csa - Information for CSA (Channel Switch Announcement)
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
struct ecrnx_csa {
    struct ecrnx_vif *vif;
    struct ecrnx_bcn bcn;
    struct ecrnx_ipc_elem_var elem;
    struct cfg80211_chan_def chandef;
    int count;
    int status;
    int ch_idx;
    struct work_struct work;
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
 * This is also linked within the ecrnx_hw vifs list.
 *
 */
struct ecrnx_tdls {
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
 * enum ecrnx_ap_flags - AP flags
 *
 * @ECRNX_AP_ISOLATE Isolate clients (i.e. Don't brige packets transmitted by
 *                                   one client for another one)
 */
enum ecrnx_ap_flags {
    ECRNX_AP_ISOLATE = BIT(0),
    ECRNX_AP_USER_MESH_PM = BIT(1),
    ECRNX_AP_CREATE_MESH_PATH = BIT(2),
};

/**
 * enum ecrnx_sta_flags - STATION flags
 *
 * @ECRNX_STA_EXT_AUTH: External authentication is in progress
 */
enum ecrnx_sta_flags {
    ECRNX_STA_EXT_AUTH = BIT(0),
    ECRNX_STA_FT_OVER_DS = BIT(1),
    ECRNX_STA_FT_OVER_AIR = BIT(2),
};

#ifdef CONFIG_ECRNX_WIFO_CAIL
/**
 * struct ecrnx_iwpriv_amt_vif - iwpriv amt VIF information
 *
 * @ndev: Pointer to the associated net device
 */
struct ecrnx_iwpriv_amt_vif {
    struct net_device *ndev;

    unsigned char   rxdata[ECRNX_RXSIZE];
    int             rxlen;
    wait_queue_head_t   rxdataq;
    int                 rxdatas;
};
#endif

#define ECRNX_REORD_RX_MSDU_CNT      (256)
#define ECRNX_REORD_TIMEOUT          (50)
#define ECRNX_REORD_WINSIZE          (64)
#define SN_LESS(a, b)           (((a-b)&0x800)!=0)
#define SN_EQUAL(a, b)          (a == b)


struct reord_cntrl {
    bool active;
    bool valid;
    u16 win_size;
    u16 win_start;
    struct ecrnx_hw *ecrnx_hw;
    struct ecrnx_vif *ecrnx_vif;
    spinlock_t reord_list_lock;
    struct list_head reord_list;
    struct timer_list reord_timer;
    struct work_struct reord_timer_work;
};

struct reord_msdu_info {
    struct sk_buff *skb;
    u8 tid;
    u8 need_pn_check;
    u8 is_ga;
    u16 sn;
    struct hw_rxhdr *hw_rxhdr;
    struct list_head reord_pending_list;
    struct list_head rx_msdu_list;
    struct reord_cntrl *preorder_ctrl;
};

/**
 * struct ecrnx_vif - VIF information
 *
 * @list: List element for ecrnx_hw->vifs
 * @ecrnx_hw: Pointer to driver main data
 * @wdev: Wireless device
 * @ndev: Pointer to the associated net device
 * @net_stats: Stats structure for the net device
 * @key: Conversion table between protocol key index and MACHW key index
 * @drv_vif_index: VIF index at driver level (only use to identify active
 * vifs in ecrnx_hw->avail_idx_map)
 * @vif_index: VIF index at fw level (used to index ecrnx_hw->vif_table, and
 * ecrnx_sta->vif_idx)
 * @ch_index: Channel context index (within ecrnx_hw->chanctx_table)
 * @up: Indicate if associated netdev is up (i.e. Interface is created at fw level)
 * @use_4addr: Whether 4address mode should be use or not
 * @is_resending: Whether a frame is being resent on this interface
 * @roc_tdls: Indicate if the ROC has been called by a TDLS station
 * @tdls_status: Status of the TDLS link
 * @tdls_chsw_prohibited: Whether TDLS Channel Switch is prohibited or not
 * @generation: Generation ID. Increased each time a sta is added/removed
 *
 * STA / P2P_CLIENT interfaces
 * @flags: see ecrnx_sta_flags
 * @ap: Pointer to the peer STA entry allocated for the AP
 * @tdls_sta: Pointer to the TDLS station
 * @ft_assoc_ies: Association Request Elements (only allocated for FT connection)
 * @ft_assoc_ies_len: Size, in bytes, of the Association request elements.
 * @ft_target_ap: Target AP for a BSS transition for FT over DS
 *
 * AP/P2P GO/ MESH POINT interfaces
 * @flags: see ecrnx_ap_flags
 * @sta_list: List of station connected to the interface
 * @bcn: Beacon data
 * @bcn_interval: beacon interval in TU
 * @bcmc_index: Index of the BroadCast/MultiCast station
 * @csa: Information about current Channel Switch Announcement (NULL if no CSA)
 * @mpath_list: List of Mesh Paths (MESH Point only)
 * @proxy_list: List of Proxies Information (MESH Point only)
 * @mesh_pm: Mesh power save mode currently set in firmware
 * @next_mesh_pm: Mesh power save mode for next peer
 *
 * AP_VLAN interfaces
 * @mater: Pointer to the master interface
 * @sta_4a: When AP_VLAN interface are used for WDS (i.e. wireless connection
 * between several APs) this is the 'gateway' sta to 'master' AP
 */
struct ecrnx_vif {
    struct list_head list;
    struct ecrnx_hw *ecrnx_hw;
    struct wireless_dev wdev;
    struct net_device *ndev;
    struct net_device_stats net_stats;
    struct ecrnx_key key[6];
    u8 drv_vif_index;           /* Identifier of the VIF in driver */
    u8 vif_index;               /* Identifier of the station in FW */
    u8 ch_index;                /* Channel context identifier */
    bool up;                    /* Indicate if associated netdev is up
                                   (i.e. Interface is created at fw level) */
    bool use_4addr;             /* Should we use 4addresses mode */
    bool is_resending;          /* Indicate if a frame is being resent on this interface */
    bool roc_tdls;              /* Indicate if the ROC has been called by a
                                   TDLS station */
    u8 tdls_status;             /* Status of the TDLS link */
    bool tdls_chsw_prohibited;  /* Indicate if TDLS Channel Switch is prohibited */
    int generation;

    unsigned char   rxdata[ECRNX_RXSIZE];
    int             rxlen;
    wait_queue_head_t   rxdataq;
    int                 rxdatas;

	u32 mgmt_reg_stypes;	//GavinGao

    union
    {
        struct
        {
            u32 flags;
            struct ecrnx_sta *ap; /* Pointer to the peer STA entry allocated for
                                    the AP */
            struct ecrnx_sta *tdls_sta; /* Pointer to the TDLS station */
            u8 *ft_assoc_ies;
            int ft_assoc_ies_len;
            u8 ft_target_ap[ETH_ALEN];
        } sta;
        struct
        {
            u32 flags;
            struct list_head sta_list; /* List of STA connected to the AP */
            struct ecrnx_bcn bcn;       /* beacon */
            int bcn_interval;
            u8 bcmc_index;             /* Index of the BCMC sta to use */
            struct ecrnx_csa *csa;

            struct list_head mpath_list; /* List of Mesh Paths used on this interface */
            struct list_head proxy_list; /* List of Proxies Information used on this interface */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
            enum nl80211_mesh_power_mode mesh_pm; /* mesh power save mode currently set in firmware */
            enum nl80211_mesh_power_mode next_mesh_pm; /* mesh power save mode for next peer */
#endif
        } ap;
        struct
        {
            struct ecrnx_vif *master;   /* pointer on master interface */
            struct ecrnx_sta *sta_4a;
        } ap_vlan;
    };
    uint64_t rx_pn[TID_MAX];
};

#define ECRNX_INVALID_VIF 0xFF
#define ECRNX_VIF_TYPE(vif) (vif->wdev.iftype)

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
struct ecrnx_sta_ps {
    bool active;
    u16 pkt_ready[2];
    u16 sp_cnt[2];
};

/**
 * struct ecrnx_rx_rate_stats - Store statistics for RX rates
 *
 * @table: Table indicating how many frame has been receive which each
 * rate index. Rate index is the same as the one used by RC algo for TX
 * @size: Size of the table array
 * @cpt: number of frames received
 */
struct ecrnx_rx_rate_stats {
    int *table;
    int size;
    int cpt;
    int rate_cnt;
};

/**
 * struct ecrnx_sta_stats - Structure Used to store statistics specific to a STA
 *
 * @last_rx: Hardware vector of the last received frame
 * @rx_rate: Statistics of the received rates
 */
struct ecrnx_sta_stats {
    u32 rx_pkts;
    u32 tx_pkts;
    u64 rx_bytes;
    u64 tx_bytes;
    unsigned long last_act;
    struct hw_vect last_rx;
#ifdef CONFIG_ECRNX_DEBUGFS
    struct ecrnx_rx_rate_stats rx_rate;
#endif
};

/*
 * Structure used to save information relative to the managed stations.
 */
struct ecrnx_sta {
    struct list_head list;
    bool valid;
    u8 mac_addr[ETH_ALEN];
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
    struct ecrnx_key key;
    struct ecrnx_sta_ps ps;  /* Information when STA is in PS (AP only) */
#ifdef CONFIG_ECRNX_BFMER
    struct ecrnx_bfmer_report *bfm_report;     /* Beamforming report to be used for
                                                 VHT TX Beamforming */
#ifdef CONFIG_ECRNX_MUMIMO_TX
    struct ecrnx_sta_group_info group_info; /* MU grouping information for the STA */
#endif /* CONFIG_ECRNX_MUMIMO_TX */
#endif /* CONFIG_ECRNX_BFMER */

    bool ht;               /* Flag indicating if the station
                               supports HT */
    bool vht;               /* Flag indicating if the station
                               supports VHT */
    u32 ac_param[AC_MAX];  /* EDCA parameters */
    struct ecrnx_tdls tdls; /* TDLS station information */
    struct ecrnx_sta_stats stats;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
    enum nl80211_mesh_power_mode mesh_pm; /*  link-specific mesh power save mode */
#endif
    int listen_interval;
    struct twt_setup_ind twt_ind; /*TWT Setup indication*/
    uint64_t rx_pn[TID_MAX];
    struct reord_cntrl reord_cntrl[TID_MAX];
};

#define ECRNX_INVALID_STA 0xFF
static inline const u8 *ecrnx_sta_addr(struct ecrnx_sta *sta) {
    return sta->mac_addr;
}

#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
struct ecrnx_amsdu_stats {
    int done;
    int failed;
};
#endif

struct ecrnx_stats {
    int cfm_balance[NX_TXQ_CNT];
    unsigned long last_rx, last_tx; /* jiffies */
    int ampdus_tx[IEEE80211_MAX_AMPDU_BUF];
    int ampdus_rx[IEEE80211_MAX_AMPDU_BUF];
    int ampdus_rx_map[4];
    int ampdus_rx_miss;
    int ampdus_rx_last;
#ifdef CONFIG_ECRNX_SPLIT_TX_BUF
    struct ecrnx_amsdu_stats amsdus[NX_TX_PAYLOAD_MAX];
#endif
    int amsdus_rx[64];
};

/**
 * struct ecrnx_roc - Remain On Channel information
 *

 Structure that will contains all RoC information received from cfg80211 */
struct ecrnx_roc {
    struct ecrnx_vif *vif;
    struct ieee80211_channel *chan;
    unsigned int duration;
    /* Used to avoid call of CFG80211 callback upon expiration of RoC */
    bool internal;
    /* Indicate if we have switch on the RoC channel */
    bool on_chan;
};

/* Structure containing channel survey information received from MAC */
struct ecrnx_survey_info {
    // Filled
    u32 filled;
    // Amount of time in ms the radio spent on the channel
    u32 chan_time_ms;
    // Amount of time the primary channel was sensed busy
    u32 chan_time_busy_ms;
    // Noise in dbm
    s8 noise_dbm;
};


/* Structure containing channel context information */
struct ecrnx_chanctx {
    struct cfg80211_chan_def chan_def; /* channel description */
    u8 count;                          /* number of vif using this ctxt */
};

#define ECRNX_CH_NOT_SET 0xFF
/**
 * ecrnx_phy_info - Phy information
 *
 * @phy_cnt: Number of phy interface
 * @cfg: Configuration send to firmware
 * @sec_chan: Channel configuration of the second phy interface (if phy_cnt > 1)
 * @limit_bw: Set to true to limit BW on requested channel. Only set to use
 * VHT with old radio that don't support 80MHz (deprecated)
 */
struct ecrnx_phy_info {
    u8 cnt;
    struct phy_cfg_tag cfg;
    struct mac_chan_op sec_chan;
    bool limit_bw;
};

struct ecrnx_hw {
    struct device *dev;
	 // Hardware info

#ifdef CONFIG_ECRNX_ESWIN
    void *plat;
#else
    struct ecrnx_plat *plat;
#endif

    struct ecrnx_phy_info phy;
    struct mm_version_cfm version_cfm;
    int machw_type;
    struct ecrnx_mod_params *mod_params;
    unsigned long flags;
    struct wiphy *wiphy;
    u8 ext_capa[10];
    struct list_head vifs;
    struct ecrnx_vif *vif_table[NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX]; /* indexed with fw id */
    u8 vif_started;
    u8 avail_idx_map;
    u8 monitor_vif;
    struct ecrnx_sta sta_table[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];

    // Channels
    struct ecrnx_chanctx chanctx_table[NX_CHAN_CTXT_CNT];
    u8 cur_chanctx;
    struct ecrnx_survey_info survey[SCAN_CHANNEL_MAX];

    /* RoC Management */
    struct ecrnx_roc *roc;
    u32 roc_cookie;
    struct cfg80211_scan_request *scan_request;
    spinlock_t scan_req_lock;
    spinlock_t connect_req_lock;
    struct ecrnx_radar radar;
	
#ifdef CONFIG_ECRNX_P2P
	struct ecrnx_p2p_listen p2p_listen;
#endif

    // TX path
    spinlock_t tx_lock;
    struct ecrnx_txq txq[NX_NB_TXQ];
    struct ecrnx_hwq hwq[NX_TXQ_CNT];
    struct timer_list txq_cleanup;
    struct kmem_cache *sw_txhdr_cache;
    u32 tcp_pacing_shift;
#ifdef CONFIG_ECRNX_MUMIMO_TX
    struct ecrnx_mu_info mu;
#endif

    // RX path
    struct ecrnx_defer_rx defer_rx;
    spinlock_t rx_lock;

    struct tasklet_struct task;


    /* IPC */
    struct ipc_host_env_tag *ipc_env;
    struct ecrnx_cmd_mgr cmd_mgr;
    spinlock_t cb_lock;
    struct ecrnx_ipc_elem_pool e2amsgs_pool;
    struct ecrnx_ipc_elem_pool dbgmsgs_pool;
    struct ecrnx_ipc_elem_pool e2aradars_pool;
    struct ecrnx_ipc_elem_var pattern_elem;
    struct ecrnx_ipc_dbgdump_elem dbgdump_elem;
    struct ecrnx_ipc_elem_pool e2arxdesc_pool;
    struct ecrnx_ipc_skb_elem *e2aunsuprxvec_elems;
    struct ecrnx_ipc_rxbuf_elems rxbuf_elems;
    struct ecrnx_ipc_elem_var scan_ie;
    
#ifdef CONFIG_ECRNX_DEBUGFS
    struct ecrnx_debugfs     debugfs;
#endif

    struct ecrnx_stats       stats;
    struct ecrnx_conf_file   conf_param;

#ifdef CONFIG_ECRNX_ESWIN
        struct list_head agg_rx_list;
        struct list_head defrag_rx_list;
#endif
#ifdef CONFIG_ESWIN_RX_REORDER
    spinlock_t rx_msdu_free_lock;
    struct list_head rx_msdu_free_list;
    struct list_head rx_reord_list;
    spinlock_t rx_reord_lock;
    struct reord_msdu_info * rx_reord_buf;
#endif

    /* extended capabilities supported */
#if defined(CONFIG_ECRNX_DEBUGFS_CUSTOM)
	struct list_head debugfs_survey_info_tbl_ptr;
#endif
    u32 msg_tx;
    u32 msg_tx_done;
    u32 data_tx;
    u32 data_tx_done;
    u32 usb_rx;
    u32 msg_rx;
    u32 data_rx;
};

u8 *ecrnx_build_bcn(struct ecrnx_bcn *bcn, struct cfg80211_beacon_data *new);

void ecrnx_chanctx_link(struct ecrnx_vif *vif, u8 idx,
                        struct cfg80211_chan_def *chandef);
void ecrnx_chanctx_unlink(struct ecrnx_vif *vif);
int  ecrnx_chanctx_valid(struct ecrnx_hw *ecrnx_hw, u8 idx);

static inline bool is_multicast_sta(int sta_idx)
{
    return (sta_idx >= NX_REMOTE_STA_MAX);
}
struct ecrnx_sta *ecrnx_get_sta(struct ecrnx_hw *ecrnx_hw, const u8 *mac_addr);

static inline uint8_t master_vif_idx(struct ecrnx_vif *vif)
{
    if (unlikely(vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN)) {
        return vif->ap_vlan.master->vif_index;
    } else {
        return vif->vif_index;
    }
}

static inline void *ecrnx_get_shared_trace_buf(struct ecrnx_hw *ecrnx_hw)
{
#ifdef CONFIG_ECRNX_DEBUGFS
    return (void *)&(ecrnx_hw->debugfs.fw_trace.buf);
#else
    return NULL;
#endif
}

void ecrnx_external_auth_enable(struct ecrnx_vif *vif);
void ecrnx_external_auth_disable(struct ecrnx_vif *vif);


#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
/* 802.11ax HE MAC capabilities */
#define IEEE80211_HE_MAC_CAP0_HTC_HE				0x01
#define IEEE80211_HE_MAC_CAP0_TWT_REQ				0x02
#define IEEE80211_HE_MAC_CAP0_TWT_RES				0x04
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_NOT_SUPP		0x00
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_1		0x08
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_2		0x10
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_3		0x18
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_MASK			0x18
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_1		0x00
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_2		0x20
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_4		0x40
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_8		0x60
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_16		0x80
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_32		0xa0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_64		0xc0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_UNLIMITED	0xe0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_MASK		0xe0

#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_UNLIMITED		0x00
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_128			0x01
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_256			0x02
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_512			0x03
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_MASK		0x03
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_0US		0x00
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_8US		0x04
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US		0x08
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK		0x0c
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_1		0x00
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_2		0x10
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_3		0x20
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_4		0x30
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_5		0x40
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_6		0x50
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_7		0x60
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8		0x70
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_MASK		0x70

/* Link adaptation is split between byte HE_MAC_CAP1 and
 * HE_MAC_CAP2. It should be set only if IEEE80211_HE_MAC_CAP0_HTC_HE
 * in which case the following values apply:
 * 0 = No feedback.
 * 1 = reserved.
 * 2 = Unsolicited feedback.
 * 3 = both
 */
#define IEEE80211_HE_MAC_CAP1_LINK_ADAPTATION			0x80

#define IEEE80211_HE_MAC_CAP2_LINK_ADAPTATION			0x01
#define IEEE80211_HE_MAC_CAP2_ALL_ACK				0x02
#define IEEE80211_HE_MAC_CAP2_TRS				0x04
#define IEEE80211_HE_MAC_CAP2_BSR				0x08
#define IEEE80211_HE_MAC_CAP2_BCAST_TWT				0x10
#define IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP			0x20
#define IEEE80211_HE_MAC_CAP2_MU_CASCADING			0x40
#define IEEE80211_HE_MAC_CAP2_ACK_EN				0x80

#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL			0x02
#define IEEE80211_HE_MAC_CAP3_OFDMA_RA				0x04

/* The maximum length of an A-MDPU is defined by the combination of the Maximum
 * A-MDPU Length Exponent field in the HT capabilities, VHT capabilities and the
 * same field in the HE capabilities.
 */
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_USE_VHT	0x00
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_1		0x08
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_2		0x10
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_RESERVED	0x18
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK		0x18
#define IEEE80211_HE_MAC_CAP3_AMSDU_FRAG			0x20
#define IEEE80211_HE_MAC_CAP3_FLEX_TWT_SCHED			0x40
#define IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS		0x80

#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_SHIFT		3

#define IEEE80211_HE_MAC_CAP4_BSRP_BQRP_A_MPDU_AGG		0x01
#define IEEE80211_HE_MAC_CAP4_QTP				0x02
#define IEEE80211_HE_MAC_CAP4_BQR				0x04
#define IEEE80211_HE_MAC_CAP4_SRP_RESP				0x08
#define IEEE80211_HE_MAC_CAP4_NDP_FB_REP			0x10
#define IEEE80211_HE_MAC_CAP4_OPS				0x20
#define IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU			0x40
/* Multi TID agg TX is split between byte #4 and #5
 * The value is a combination of B39,B40,B41
 */
#define IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39		0x80

#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40		0x01
#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41		0x02
#define IEEE80211_HE_MAC_CAP5_SUBCHAN_SELECVITE_TRANSMISSION	0x04
#define IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU			0x08
#define IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX		0x10
#define IEEE80211_HE_MAC_CAP5_HE_DYNAMIC_SM_PS			0x20
#define IEEE80211_HE_MAC_CAP5_PUNCTURED_SOUNDING		0x40
#define IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX		0x80

#define IEEE80211_HE_VHT_MAX_AMPDU_FACTOR	20
#define IEEE80211_HE_HT_MAX_AMPDU_FACTOR	16

/* 802.11ax HE PHY capabilities */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G	0x04
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G		0x08
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G	0x10
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G	0x20
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G	0x40
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK			0xfe

#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_20MHZ	0x01
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_40MHZ	0x02
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_20MHZ	0x04
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_40MHZ	0x08
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK			0x0f
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A				0x10
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD			0x20
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US		0x40
/* Midamble RX/TX Max NSTS is split between byte #2 and byte #3 */
#define IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS			0x80

#define IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS			0x01
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US			0x02
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ			0x04
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ			0x08
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX				0x10
#define IEEE80211_HE_PHY_CAP2_DOPPLER_RX				0x20

/* Note that the meaning of UL MU below is different between an AP and a non-AP
 * sta, where in the AP case it indicates support for Rx and in the non-AP sta
 * case it indicates support for Tx.
 */
#define IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO			0x40
#define IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO			0x80

#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK			0x01
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK			0x02
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2				0x04
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK			0x08
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK			0x10
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2				0x20
#define IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA		0x40
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER				0x80

#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE				0x01
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER				0x02

/* Minimal allowed value of Max STS under 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4		0x0c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_5		0x10
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_6		0x14
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_7		0x18
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8		0x1c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK	0x1c

/* Minimal allowed value of Max STS above 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4		0x60
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_5		0x80
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_6		0xa0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_7		0xc0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8		0xe0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK	0xe0

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2	0x01
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_3	0x02
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_4	0x03
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_5	0x04
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_6	0x05
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_7	0x06
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_8	0x07
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK	0x07

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2	0x08
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_3	0x10
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_4	0x18
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_5	0x20
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_6	0x28
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_7	0x30
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_8	0x38
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK	0x38

#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK				0x40
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK				0x80

#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU			0x01
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU			0x02
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB			0x04
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB			0x08
#define IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB				0x10
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE			0x20
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO		0x40
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT			0x80

#define IEEE80211_HE_PHY_CAP7_SRP_BASED_SR				0x01
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR			0x02
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI		0x04
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1					0x08
#define IEEE80211_HE_PHY_CAP7_MAX_NC_2					0x10
#define IEEE80211_HE_PHY_CAP7_MAX_NC_3					0x18
#define IEEE80211_HE_PHY_CAP7_MAX_NC_4					0x20
#define IEEE80211_HE_PHY_CAP7_MAX_NC_5					0x28
#define IEEE80211_HE_PHY_CAP7_MAX_NC_6					0x30
#define IEEE80211_HE_PHY_CAP7_MAX_NC_7					0x38
#define IEEE80211_HE_PHY_CAP7_MAX_NC_MASK				0x38
#define IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ			0x40
#define IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ			0x80

#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI		0x01
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU			0x04
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU			0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI		0x10
#define IEEE80211_HE_PHY_CAP8_MIDAMBLE_RX_TX_2X_AND_1XLTF		0x20
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242				0x00
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484				0x40
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996				0x80
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996				0xc0
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK				0xc0

#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM		0x01
#define IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK		0x02
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU		0x04
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU		0x08

/* 802.11ax HE TX/RX MCS NSS Support  */
#define IEEE80211_TX_RX_MCS_NSS_SUPP_HIGHEST_MCS_POS			(3)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_POS			(6)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_POS			(11)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_MASK			0x07c0
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_MASK			0xf800


/* TX/RX HE MCS Support field Highest MCS subfield encoding */
enum ieee80211_he_highest_mcs_supported_subfield_enc {
	HIGHEST_MCS_SUPPORTED_MCS7 = 0,
	HIGHEST_MCS_SUPPORTED_MCS8,
	HIGHEST_MCS_SUPPORTED_MCS9,
	HIGHEST_MCS_SUPPORTED_MCS10,
	HIGHEST_MCS_SUPPORTED_MCS11,
};

#define IEEE80211_HE_PPE_THRES_MAX_LEN		25

/**
 * struct ieee80211_he_mcs_nss_supp - HE Tx/Rx HE MCS NSS Support Field
 *
 * This structure holds the data required for the Tx/Rx HE MCS NSS Support Field
 * described in P802.11ax_D2.0 section 9.4.2.237.4
 *
 * @rx_mcs_80: Rx MCS map 2 bits for each stream, total 8 streams, for channel
 *     widths less than 80MHz.
 * @tx_mcs_80: Tx MCS map 2 bits for each stream, total 8 streams, for channel
 *     widths less than 80MHz.
 * @rx_mcs_160: Rx MCS map 2 bits for each stream, total 8 streams, for channel
 *     width 160MHz.
 * @tx_mcs_160: Tx MCS map 2 bits for each stream, total 8 streams, for channel
 *     width 160MHz.
 * @rx_mcs_80p80: Rx MCS map 2 bits for each stream, total 8 streams, for
 *     channel width 80p80MHz.
 * @tx_mcs_80p80: Tx MCS map 2 bits for each stream, total 8 streams, for
 *     channel width 80p80MHz.
 */
struct ieee80211_he_mcs_nss_supp {
	__le16 rx_mcs_80;
	__le16 tx_mcs_80;
	__le16 rx_mcs_160;
	__le16 tx_mcs_160;
	__le16 rx_mcs_80p80;
	__le16 tx_mcs_80p80;
} __packed;

/**
 * struct ieee80211_he_cap_elem - HE capabilities element
 *
 * This structure is the "HE capabilities element" fixed fields as
 * described in P802.11ax_D2.0 section 9.4.2.237.2 and 9.4.2.237.3
 */
struct ieee80211_he_cap_elem {
	u8 mac_cap_info[6];
	u8 phy_cap_info[11];
} __packed;

/**
 * struct ieee80211_sta_he_cap - STA's HE capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ax HE capabilities for a STA.
 *
 * @has_he: true iff HE data is valid.
 * @he_cap_elem: Fixed portion of the HE capabilities element.
 * @he_mcs_nss_supp: The supported NSS/MCS combinations.
 * @ppe_thres: Holds the PPE Thresholds data.
 */
struct ieee80211_sta_he_cap {
	bool has_he;
	struct ieee80211_he_cap_elem he_cap_elem;
	struct ieee80211_he_mcs_nss_supp he_mcs_nss_supp;
	u8 ppe_thres[IEEE80211_HE_PPE_THRES_MAX_LEN];
};

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB	0x10
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB	0x20
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_0US			0x00
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_8US			0x40
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US			0x80
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_RESERVED		0xc0
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_MASK			0xc0
#endif


#if defined(CONFIG_ECRNX_HE)
extern struct ieee80211_sta_he_cap ecrnx_he_cap;
#endif

#define RW_DRV_DESCRIPTION  "ESWIN 11nac driver for Linux cfg80211"
#define RW_DRV_COPYRIGHT    "Copyright(c) 2015-2017 ESWIN"
#define RW_DRV_AUTHOR       "ESWIN S.A.S"

#define ECRNX_PRINT_CFM_ERR(req) \
        printk(KERN_CRIT "%s: Status Error(%d)\n", #req, (&req##_cfm)->status)

#define ECRNX_HT_CAPABILITIES                                    \
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

#define ECRNX_VHT_CAPABILITIES                                   \
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

#if CONFIG_ECRNX_HE
#define ECRNX_HE_CAPABILITIES                                    \
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
    .ppe_thres = {0x00},                                        \
}
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

#endif /* _ECRNX_DEFS_H_*/
