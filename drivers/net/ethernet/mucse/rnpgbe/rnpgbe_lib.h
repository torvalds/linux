/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_LIB_H
#define _RNPGBE_LIB_H

#include <linux/bits.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

struct mucse;
struct mucse_hw;
struct mucse_ring;

#define RING_OFFSET(n)            (0x1000 + 0x100 * (n))
#define RNPGBE_RX_START           0x10
#define RNPGBE_TX_START           0x18
#define RNPGBE_DMA_INT_MASK       0x24
#define TX_INT_MASK               BIT(1)
#define RX_INT_MASK               BIT(0)
#define INT_VALID                 (BIT(16) | BIT(17))
#define RNPGBE_DMA_INT_TRIG       0x2c /* lost-interrupt recovery trigger */
#define RNPGBE_RX_BASE_ADDR_HI    0x30
#define RNPGBE_RX_BASE_ADDR_LO    0x34
#define RNPGBE_RX_LEN             0x38
#define RNPGBE_RX_HEAD            0x3c
#define RNPGBE_RX_TAIL            0x40
#define M_DEFAULT_RX_FETCH        0x100020
#define RNPGBE_RX_FETCH           0x44
#define M_DEFAULT_INT_TIMER_R     30
#define RNPGBE_RX_INT_TIMER       0x48
#define M_DEFAULT_RX_INT_PKTCNT   64
#define RNPGBE_RX_INT_PKTCNT      0x4c
#define RNPGBE_RX_TIMEOUT_TH      0x54
#define RNPGBE_RX_SG_UNIT_BYTES   16
#define M_DEFAULT_SG              96
#define RNPGBE_RX_DESC_DATA_LEN   (M_DEFAULT_SG * RNPGBE_RX_SG_UNIT_BYTES)
#define RNPGBE_RX_SG_LEN          0x58
#define RNPGBE_TX_BASE_ADDR_HI    0x60
#define RNPGBE_TX_BASE_ADDR_LO    0x64
#define RNPGBE_TX_LEN             0x68
#define RNPGBE_TX_HEAD            0x6c
#define RNPGBE_TX_TAIL            0x70
#define M_DEFAULT_TX_FETCH        0x80008
#define RNPGBE_TX_FETCH_CTRL      0x74
#define M_DEFAULT_INT_TIMER       100
#define RNPGBE_TX_INT_TIMER       0x78
#define M_DEFAULT_INT_PKTCNT      48
#define RNPGBE_TX_INT_PKTCNT      0x7c
/* |  31:24   | .... |    15:8   |    7:0    | */
/* |  pfvfnum |      | tx vector | rx vector | */
#define RING_VECTOR(n)            (0x04 * (n))

#define M_MAX_TXD_PWR             12
#define M_MAX_DATA_PER_TXD        (0x1 << M_MAX_TXD_PWR)
#define TXD_USE_COUNT(S)          DIV_ROUND_UP((S), M_MAX_DATA_PER_TXD)
#define DESC_NEEDED               (MAX_SKB_FRAGS + 4)
/* Leave two descriptors between the tail and head. */
#define RESV_DESC_NEEDED          2
#define RNPGBE_SKB_PAD            (NET_SKB_PAD + NET_IP_ALIGN)

/* Hardware requires this to be nonzero */
#define M_DEFAULT_MAC_IP_LEN      20
#define mucse_for_each_ring(pos, head)\
	for (typeof((head).ring) __pos = (head).ring;\
	     __pos ? ({ pos = __pos; 1; }) : 0;\
	     __pos = __pos->next)
int rnpgbe_init_interrupt_scheme(struct mucse *mucse);
void rnpgbe_clear_interrupt_scheme(struct mucse *mucse);
int rnpgbe_request_mbx_irq(struct mucse *mucse);
void rnpgbe_free_mbx_irq(struct mucse *mucse);
int rnpgbe_request_irq(struct mucse *mucse);
void rnpgbe_free_irq(struct mucse *mucse);
void rnpgbe_down(struct mucse *mucse);
int rnpgbe_up_complete(struct mucse *mucse);
int rnpgbe_configure_tx(struct mucse *mucse);
int rnpgbe_configure_rx(struct mucse *mucse);
void rnpgbe_clean_all_tx_rings(struct mucse *mucse);
int rnpgbe_setup_all_tx_resources(struct mucse *mucse);
void rnpgbe_free_all_tx_resources(struct mucse *mucse);
netdev_tx_t rnpgbe_xmit_frame_ring(struct sk_buff *skb,
				   struct mucse_ring *tx_ring);
void rnpgbe_get_stats64(struct net_device *netdev,
			struct rtnl_link_stats64 *stats);
int rnpgbe_setup_all_rx_resources(struct mucse *mucse);
void rnpgbe_free_all_rx_resources(struct mucse *mucse);
void rnpgbe_service_task(struct work_struct *work);
#endif
