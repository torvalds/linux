/**
 ******************************************************************************
 *
 * @file ecrnx_rx.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
#ifndef _ECRNX_RX_H_
#define _ECRNX_RX_H_

#include <linux/workqueue.h>
#include "hal_desc.h"
enum rx_status_bits
{
    /// The buffer can be forwarded to the networking stack
    RX_STAT_FORWARD = 1 << 0,
    /// A new buffer has to be allocated
    RX_STAT_ALLOC = 1 << 1,
    /// The buffer has to be deleted
    RX_STAT_DELETE = 1 << 2,
    /// The length of the buffer has to be updated
    RX_STAT_LEN_UPDATE = 1 << 3,
    /// The length in the Ethernet header has to be updated
    RX_STAT_ETH_LEN_UPDATE = 1 << 4,
    /// Simple copy
    RX_STAT_COPY = 1 << 5,
    /// Spurious frame (inform upper layer and discard)
    RX_STAT_SPURIOUS = 1 << 6,
    /// packet for monitor interface
    RX_STAT_MONITOR = 1 << 7,
};

#if defined(CONFIG_ECRNX_ESWIN_USB)
typedef enum
{
    MSG_TYPE_DATA = 1,
    MSG_TYPE_CTRL
}MSG_TYPE_E;
#endif

/*
 * Decryption status subfields.
 * {
 */
// @}

#define _ASOCREQ_IE_OFFSET_     4   /* excluding wlan_hdr */
#define _REASOCREQ_IE_OFFSET_   10
#define STATION_INFO_ASSOC_REQ_IES 0
#define WLAN_HDR_A3_LEN            24
#define get_addr2_ptr(pbuf)	((unsigned char *)((unsigned int)(pbuf) + 10))

/* keep it same with the FW */
#define RX_CNTRL_REORD_WIN_SIZE 42
#ifdef CONFIG_ECRNX_MON_DATA
#define RX_MACHDR_BACKUP_LEN    64
/// MAC header backup descriptor
struct mon_machdrdesc
{
    /// Length of the buffer
    u32 buf_len;
    /// Buffer containing mac header, LLC and SNAP
    u8 buffer[RX_MACHDR_BACKUP_LEN];
};
#endif

struct hw_rxhdr {
    /** RX vector */
    struct hw_vect hwvect;

    /** PHY channel information */
    struct phy_channel_info_desc phy_info;

    /** RX flags */
    u32    flags_is_amsdu     : 1;
    u32    flags_is_80211_mpdu: 1;
    u32    flags_is_4addr     : 1;
    u32    flags_new_peer     : 1;
    u32    flags_user_prio    : 3;
    u32    flags_rsvd0        : 1;
    u32    flags_vif_idx      : 8;    // 0xFF if invalid VIF index
    u32    flags_sta_idx      : 8;    // 0xFF if invalid STA index
    u32    flags_dst_idx      : 8;    // 0xFF if unknown destination STA
#ifdef CONFIG_ECRNX_MON_DATA
    /// MAC header backup descriptor (used only for MSDU when there is a monitor and a data interface)
    struct mon_machdrdesc mac_hdr_backup;
#endif
    /** Pattern indicating if the buffer is available for the driver */
    u32    pattern;
};

struct ecrnx_defer_rx {
    struct sk_buff_head sk_list;
    struct work_struct work;
};

/**
 * struct ecrnx_defer_rx_cb - Control buffer for deferred buffers
 *
 * @vif: VIF that received the buffer
 */
struct ecrnx_defer_rx_cb {
    struct ecrnx_vif *vif;
};

u8 ecrnx_unsup_rx_vec_ind(void *pthis, void *hostid);
u8 ecrnx_rxdataind(void *pthis, void *hostid);
void ecrnx_rx_deferred(struct work_struct *ws);
void ecrnx_rx_defer_skb(struct ecrnx_hw *ecrnx_hw, struct ecrnx_vif *ecrnx_vif,
                       struct sk_buff *skb);
#ifdef CONFIG_ECRNX_ESWIN_SDIO
int ecrnx_rx_callback(void *priv, struct sk_buff *skb);
#elif defined(CONFIG_ECRNX_ESWIN_USB)
int ecrnx_rx_callback(void *priv, struct sk_buff *skb, uint8_t endpoint);
#endif

void ecrnx_rx_reord_deinit(struct ecrnx_hw *ecrnx_hw);
void ecrnx_rx_reord_init(struct ecrnx_hw *ecrnx_hw);
void ecrnx_rx_reord_sta_init(struct ecrnx_hw* ecrnx_hw, struct ecrnx_vif *ecrnx_vif, u8 sta_idx);
void ecrnx_rx_reord_sta_deinit(struct ecrnx_hw* ecrnx_hw, u8 sta_idx, bool is_del);


#endif /* _ECRNX_RX_H_ */
