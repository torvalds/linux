/**
 ******************************************************************************
 *
 * @file rwnx_tx.h
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */
#ifndef _RWNX_TX_H_
#define _RWNX_TX_H_

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <linux/netdevice.h>
#include "lmac_types.h"
#include "ipc_shared.h"
#include "rwnx_txq.h"
#include "hal_desc.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
#define IEEE80211_NUM_TIDS              16
#endif

#define RWNX_HWQ_BK                     0
#define RWNX_HWQ_BE                     1
#define RWNX_HWQ_VI                     2
#define RWNX_HWQ_VO                     3
#define RWNX_HWQ_BCMC                   4
#define RWNX_HWQ_NB                     NX_TXQ_CNT
#define RWNX_HWQ_ALL_ACS (RWNX_HWQ_BK | RWNX_HWQ_BE | RWNX_HWQ_VI | RWNX_HWQ_VO)
#define RWNX_HWQ_ALL_ACS_BIT ( BIT(RWNX_HWQ_BK) | BIT(RWNX_HWQ_BE) |    \
                               BIT(RWNX_HWQ_VI) | BIT(RWNX_HWQ_VO) )

#define RWNX_TX_LIFETIME_MS             1000
#define RWNX_TX_MAX_RATES               NX_TX_MAX_RATES

#define RWNX_SWTXHDR_ALIGN_SZ           4
#define RWNX_SWTXHDR_ALIGN_MSK (RWNX_SWTXHDR_ALIGN_SZ - 1)
#define RWNX_SWTXHDR_ALIGN_PADS(x) \
                    ((RWNX_SWTXHDR_ALIGN_SZ - ((x) & RWNX_SWTXHDR_ALIGN_MSK)) \
                     & RWNX_SWTXHDR_ALIGN_MSK)
#if RWNX_SWTXHDR_ALIGN_SZ & RWNX_SWTXHDR_ALIGN_MSK
#error bad RWNX_SWTXHDR_ALIGN_SZ
#endif

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

extern const int rwnx_tid2hwq[IEEE80211_NUM_TIDS];

/**
 * struct rwnx_amsdu_txhdr - Structure added in skb headroom (instead of
 * rwnx_txhdr) for amsdu subframe buffer (except for the first subframe
 * that has a normal rwnx_txhdr)
 *
 * @list     List of other amsdu subframe (rwnx_sw_txhdr.amsdu.hdrs)
 * @map_len  Length to be downloaded for this subframe
 * @dma_addr Buffer address form embedded point of view
 * @skb      skb
 * @pad      padding added before this subframe
 *           (only use when amsdu must be dismantled)
 * @msdu_len Size, in bytes, of the MSDU (without padding nor amsdu header)
 */
struct rwnx_amsdu_txhdr {
    struct list_head list;
    size_t map_len;
    dma_addr_t dma_addr;
    struct sk_buff *skb;
    u16 pad;
    u16 msdu_len;
};

/**
 * struct rwnx_amsdu - Structure to manage creation of an A-MSDU, updated
 * only In the first subframe of an A-MSDU
 *
 * @hdrs List of subframe of rwnx_amsdu_txhdr
 * @len  Current size for this A-MDSU (doesn't take padding into account)
 *       0 means that no amsdu is in progress
 * @nb   Number of subframe in the amsdu
 * @pad  Padding to add before adding a new subframe
 */
struct rwnx_amsdu {
    struct list_head hdrs;
    u16 len;
    u8 nb;
    u8 pad;
};

/**
 * struct rwnx_sw_txhdr - Software part of tx header
 *
 * @rwnx_sta sta to which this buffer is addressed
 * @rwnx_vif vif that send the buffer
 * @txq pointer to TXQ used to send the buffer
 * @hw_queue Index of the HWQ used to push the buffer.
 *           May be different than txq->hwq->id on confirmation.
 * @frame_len Size of the frame (doesn't not include mac header)
 *            (Only used to update stat, can't we use skb->len instead ?)
 * @headroom Headroom added in skb to add rwnx_txhdr
 *           (Only used to remove it before freeing skb, is it needed ?)
 * @amsdu Description of amsdu whose first subframe is this buffer
 *        (amsdu.nb = 0 means this buffer is not part of amsdu)
 * @skb skb received from transmission
 * @map_len  Length mapped for DMA (only rwnx_hw_txhdr and data are mapped)
 * @dma_addr DMA address after mapping
 * @desc Buffer description that will be copied in shared mem for FW
 */
struct rwnx_sw_txhdr {
    struct rwnx_sta *rwnx_sta;
    struct rwnx_vif *rwnx_vif;
    struct rwnx_txq *txq;
    u8 hw_queue;
    u16 frame_len;
    u16 headroom;
#ifdef CONFIG_RWNX_AMSDUS_TX
    struct rwnx_amsdu amsdu;
#endif
	u32 need_cfm;
    struct sk_buff *skb;

    size_t map_len;
    dma_addr_t dma_addr;
    struct txdesc_api desc;
    u8 raw_frame;
    u8 fixed_rate;
    u16 rate_config;
};

/**
 * struct rwnx_txhdr - Stucture to control transimission of packet
 * (Added in skb headroom)
 *
 * @sw_hdr: Information from driver
 * @cache_guard:
 * @hw_hdr: Information for/from hardware
 */
struct rwnx_txhdr {
    struct rwnx_sw_txhdr *sw_hdr;
    char cache_guard[L1_CACHE_BYTES];
    struct rwnx_hw_txhdr hw_hdr;
};

u16 rwnx_select_txq(struct rwnx_vif *rwnx_vif, struct sk_buff *skb);
netdev_tx_t rwnx_start_xmit(struct sk_buff *skb, struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
                         struct cfg80211_mgmt_tx_params *params, bool offchan,
                         u64 *cookie);
#else
int rwnx_start_mgmt_xmit(struct rwnx_vif *vif, struct rwnx_sta *sta,
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
#ifdef CONFIG_RWNX_MON_XMIT
int rwnx_start_monitor_if_xmit(struct sk_buff *skb, struct net_device *dev);
#endif
int rwnx_txdatacfm(void *pthis, void *host_id);

struct rwnx_hw;
struct rwnx_sta;
void rwnx_set_traffic_status(struct rwnx_hw *rwnx_hw,
                             struct rwnx_sta *sta,
                             bool available,
                             u8 ps_id);
void rwnx_ps_bh_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
                       bool enable);
void rwnx_ps_bh_traffic_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
                            u16 pkt_req, u8 ps_id);

void rwnx_switch_vif_sta_txq(struct rwnx_sta *sta, struct rwnx_vif *old_vif,
                             struct rwnx_vif *new_vif);

int rwnx_dbgfs_print_sta(char *buf, size_t size, struct rwnx_sta *sta,
                         struct rwnx_hw *rwnx_hw);
void rwnx_txq_credit_update(struct rwnx_hw *rwnx_hw, int sta_idx, u8 tid,
                            s8 update);
void rwnx_tx_push(struct rwnx_hw *rwnx_hw, struct rwnx_txhdr *txhdr, int flags);

#endif /* _RWNX_TX_H_ */
