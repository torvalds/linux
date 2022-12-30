/**
 ******************************************************************************
 *
 * @file ecrnx_tx.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
#ifndef _ECRNX_TX_H_
#define _ECRNX_TX_H_

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <linux/netdevice.h>
#include "lmac_types.h"
#include "ipc_shared.h"
#include "ecrnx_txq.h"
#include "hal_desc.h"

#define ECRNX_HWQ_BK                     0
#define ECRNX_HWQ_BE                     1
#define ECRNX_HWQ_VI                     2
#define ECRNX_HWQ_VO                     3
#define ECRNX_HWQ_BCMC                   4
#define ECRNX_HWQ_NB                     NX_TXQ_CNT
#define ECRNX_HWQ_ALL_ACS (ECRNX_HWQ_BK | ECRNX_HWQ_BE | ECRNX_HWQ_VI | ECRNX_HWQ_VO)
#define ECRNX_HWQ_ALL_ACS_BIT ( BIT(ECRNX_HWQ_BK) | BIT(ECRNX_HWQ_BE) |    \
                               BIT(ECRNX_HWQ_VI) | BIT(ECRNX_HWQ_VO) )

#define ECRNX_TX_LIFETIME_MS             100
#define ECRNX_TX_MAX_RATES               NX_TX_MAX_RATES


#define AMSDU_PADDING(x) ((4 - ((x) & 0x3)) & 0x3)

#define TXU_CNTRL_RETRY        BIT(0)
#define TXU_CNTRL_MORE_DATA    BIT(2)
#define TXU_CNTRL_MGMT         BIT(3)
#define TXU_CNTRL_MGMT_NO_CCK  BIT(4)
#define TXU_CNTRL_AMSDU        BIT(6)
#define TXU_CNTRL_MGMT_ROBUST  BIT(7)
#define TXU_CNTRL_USE_4ADDR    BIT(8)
#define TXU_CNTRL_EOSP         BIT(9)
#define TXU_CNTRL_MESH_FWD     BIT(10)
#define TXU_CNTRL_TDLS         BIT(11)
#define TXU_CNTRL_NO_ENCRYPT   BIT(15)

extern const int ecrnx_tid2hwq[IEEE80211_NUM_TIDS];

/**
 * struct ecrnx_amsdu_txhdr - Structure added in skb headroom (instead of
 * ecrnx_txhdr) for amsdu subframe buffer (except for the first subframe
 * that has a normal ecrnx_txhdr)
 *
 * @list     List of other amsdu subframe (ecrnx_sw_txhdr.amsdu.hdrs)
 * @map_len  Length to be downloaded for this subframe
 * @dma_addr Buffer address form embedded point of view
 * @skb      skb
 * @pad      padding added before this subframe
 *           (only use when amsdu must be dismantled)
 * @msdu_len Size, in bytes, of the MSDU (without padding nor amsdu header)
 */
struct ecrnx_amsdu_txhdr {
    struct list_head list;
    size_t map_len;
#ifdef CONFIG_ECRNX_ESWIN
    u8 *send_pos; // offset from skb->data for send to slave.
#else    
    dma_addr_t dma_addr;
#endif
    struct sk_buff *skb;
    u16 pad;
    u16 msdu_len;
};

/**
 * struct ecrnx_amsdu - Structure to manage creation of an A-MSDU, updated
 * only In the first subframe of an A-MSDU
 *
 * @hdrs List of subframe of ecrnx_amsdu_txhdr
 * @len  Current size for this A-MDSU (doesn't take padding into account)
 *       0 means that no amsdu is in progress
 * @nb   Number of subframe in the amsdu
 * @pad  Padding to add before adding a new subframe
 */
struct ecrnx_amsdu {
    struct list_head hdrs;
    u16 len;
    u8 nb;
    u8 pad;
};

/**
 * struct ecrnx_sw_txhdr - Software part of tx header
 *
 * @ecrnx_sta sta to which this buffer is addressed
 * @ecrnx_vif vif that send the buffer
 * @txq pointer to TXQ used to send the buffer
 * @hw_queue Index of the HWQ used to push the buffer.
 *           May be different than txq->hwq->id on confirmation.
 * @frame_len Size of the frame (doesn't not include mac header)
 *            (Only used to update stat, can't we use skb->len instead ?)
 * @headroom Headroom added in skb to add ecrnx_txhdr
 *           (Only used to remove it before freeing skb, is it needed ?)
 * @amsdu Description of amsdu whose first subframe is this buffer
 *        (amsdu.nb = 0 means this buffer is not part of amsdu)
 * @skb skb received from transmission
 * @map_len  Length mapped for DMA (only ecrnx_hw_txhdr and data are mapped)
 * @dma_addr DMA address after mapping
 * @desc Buffer description that will be copied in shared mem for FW
 */
struct ecrnx_sw_txhdr {
    struct ecrnx_sta *ecrnx_sta;
    struct ecrnx_vif *ecrnx_vif;
    struct ecrnx_txq *txq;
    u8 hw_queue;
    u16 frame_len;
    u16 headroom;
#ifdef CONFIG_ECRNX_AMSDUS_TX
    struct ecrnx_amsdu amsdu;
#endif
    struct sk_buff *skb;

#ifdef CONFIG_ECRNX_ESWIN
    u32 offset; // offset from skb->data for send to slave.
#else
    size_t map_len;
    dma_addr_t dma_addr;
#endif
    struct txdesc_api desc;
    unsigned long jiffies;
};

/**
 * struct ecrnx_txhdr - Stucture to control transimission of packet
 * (Added in skb headroom)
 *
 * @sw_hdr: Information from driver
 * @cache_guard:
 * @hw_hdr: Information for/from hardware
 */
struct ecrnx_txhdr {
    struct ecrnx_sw_txhdr *sw_hdr;
    char cache_guard[L1_CACHE_BYTES];
    struct ecrnx_hw_txhdr hw_hdr;
};
#define ECRNX_TX_ALIGN_SIZE 4
#define ECRNX_TX_ALIGN_MASK (ECRNX_TX_ALIGN_SIZE - 1)
#define ECRNX_SWTXHDR_ALIGN_PADS(x) \
                    ((ECRNX_TX_ALIGN_SIZE - ((x) & ECRNX_TX_ALIGN_MASK)) \
                     & ECRNX_TX_ALIGN_MASK)

#define ECRNX_TX_TXDESC_API_ALIGN  ((sizeof(struct txdesc_api) + 3) & (~ECRNX_TX_ALIGN_MASK))
/**
 * ECRNX_TX_MAX_HEADROOM - Maximum size needed in skb headroom to prepare a buffer
 * for transmission
 * The headroom is used to store the 'struct ecrnx_txhdr' and moreover the part that is used
 * by the firmware to provide tx status (i.e. struct ecrnx_hw_txhdr) must be aligned on
 * 32bits because firmware used DMA to update it.
 */
#ifdef CONFIG_ECRNX_ESWIN
#define ECRNX_TX_MAX_HEADROOM (ECRNX_TX_TXDESC_API_ALIGN + sizeof(struct ecrnx_txhdr) + ECRNX_TX_ALIGN_SIZE)
#else
#define ECRNX_TX_MAX_HEADROOM (sizeof(struct ecrnx_txhdr) + ECRNX_TX_ALIGN_SIZE)
#endif

/**
 * ECRNX_TX_HEADROOM - Headroom to use to store struct ecrnx_txhdr
 *
 * Takes into account current aligment of data buffer to ensure that struct ecrnx_txhdr
 * (and as a consequence its field hw_hdr) will be aligned on 32bits boundary.
 */
#ifdef CONFIG_ECRNX_ESWIN
#define ECRNX_TX_HEADROOM(skb) (ECRNX_TX_TXDESC_API_ALIGN + sizeof(struct ecrnx_txhdr) + ((long)skb->data & ECRNX_TX_ALIGN_MASK))
#else
#define ECRNX_TX_HEADROOM(skb) (sizeof(struct ecrnx_txhdr) + ((long)skb->data & ECRNX_TX_ALIGN_MASK))
#endif

/**
 * ECRNX_TX_DMA_MAP_LEN - Length, in bytes, to map for DMA transfer
 * To be called with skb BEFORE reserving headroom to store struct ecrnx_txhdr.
 */
#define ECRNX_TX_DMA_MAP_LEN(skb) (                                      \
        (sizeof(struct ecrnx_txhdr) - offsetof(struct ecrnx_txhdr, hw_hdr)) + \
        ((long)skb->data & ECRNX_TX_ALIGN_MASK) + skb->len)

/**
 * ECRNX_TX_DATA_OFT - Offset, in bytes, between the location of the 'struct ecrnx_hw_txhdr'
 * and the beginning of frame (i.e. ethernet of 802.11 header). Cannot simply use
 * sizeof(struct ecrnx_hw_txhdr) because of padding added by compiler to align fields
 * in structure.
 */
#define ECRNX_TX_DATA_OFT(sw_txhdr) (                                    \
        (sizeof(struct ecrnx_txhdr) - offsetof(struct ecrnx_txhdr, hw_hdr)) \
        + (sw_txhdr->headroom & ECRNX_TX_ALIGN_MASK))

/**
 * SKB buffer format before it is pushed to MACSW
 *
 * For DATA frame
 *                    |--------------------|
 *                    | headroom           |
 *    skb->data ----> |--------------------| <------ skb->data
 *                    | struct ecrnx_txhdr  |
 *                    | * ecrnx_sw_txhdr *  |
 *                    | * [L1 guard]       |
 *               +--> | * ecrnx_hw_txhdr    | <---- desc.host.status_desc_addr
 *               :    |                    |
 *     memory    :    |--------------------|
 *     mapped    :    | padding (optional) |
 *     for DMA   :    |--------------------|
 *               :    | Ethernet Header    |
 *               :    |--------------------| <---- desc.host.packet_addr[0]
 *               :    | Data               |
 *               :    |                    |
 *               :    |                    |
 *               :    |                    |
 *               +--> |--------------------|
 *                    | tailroom           |
 *                    |--------------------|
 *
 *
 * For MGMT frame (skb is created by the driver so buffer is always aligned
 *                 with no headroom/tailroom)
 *
 *    skb->data ----> |--------------------| <------ skb->data
 *                    | struct ecrnx_txhdr  |
 *                    | * ecrnx_sw_txhdr *  |
 *                    | * [L1 guard]       |
 *               +--> | * ecrnx_hw_txhdr    | <---- desc.host.status_desc_addr
 *     memory    :    |                    |
 *     mapped    :    |--------------------| <---- desc.host.packet_addr[0]
 *     for DMA   :    | 802.11 HDR         |
 *               :    |--------------------|
 *               :    | Data               |
 *               :    |                    |
 *               +--> |--------------------|
 *
 */

u16 ecrnx_select_txq(struct ecrnx_vif *ecrnx_vif, struct sk_buff *skb);
int ecrnx_start_xmit(struct sk_buff *skb, struct net_device *dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
int ecrnx_start_mgmt_xmit(struct ecrnx_vif *vif, struct ecrnx_sta *sta,
                         struct cfg80211_mgmt_tx_params *params, bool offchan,
                         u64 *cookie);
#else
int ecrnx_start_mgmt_xmit(struct ecrnx_vif *vif, struct ecrnx_sta *sta,
                         struct ieee80211_channel *channel, bool offchan,
                         unsigned int wait, const u8* buf, size_t len,
                    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
                         bool no_cck,
                    #endif
                    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
                         bool dont_wait_for_ack,
                    #endif
                         u64 *cookie);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */

int ecrnx_txdatacfm(void *pthis, void *host_id);
int ecrnx_handle_tx_datacfm(void *priv, void *host_id);

struct ecrnx_hw;
struct ecrnx_sta;
void ecrnx_set_traffic_status(struct ecrnx_hw *ecrnx_hw,
                             struct ecrnx_sta *sta,
                             bool available,
                             u8 ps_id);
void ecrnx_ps_bh_enable(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta,
                       bool enable);
void ecrnx_ps_bh_traffic_req(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta,
                            u16 pkt_req, u8 ps_id);

void ecrnx_switch_vif_sta_txq(struct ecrnx_sta *sta, struct ecrnx_vif *old_vif,
                             struct ecrnx_vif *new_vif);

int ecrnx_dbgfs_print_sta(char *buf, size_t size, struct ecrnx_sta *sta,
                         struct ecrnx_hw *ecrnx_hw);
void ecrnx_txq_credit_update(struct ecrnx_hw *ecrnx_hw, int sta_idx, u8 tid,
                            s8 update);
void ecrnx_tx_push(struct ecrnx_hw *ecrnx_hw, struct ecrnx_txhdr *txhdr, int flags);

#endif /* _ECRNX_TX_H_ */
