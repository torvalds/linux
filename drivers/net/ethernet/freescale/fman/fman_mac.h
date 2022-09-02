/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* FM MAC ... */
#ifndef __FM_MAC_H
#define __FM_MAC_H

#include "fman.h"

#include <linux/slab.h>
#include <linux/phy.h>
#include <linux/if_ether.h>

struct fman_mac;
struct mac_device;

/* Ethernet Address */
typedef u8 enet_addr_t[ETH_ALEN];

#define ENET_ADDR_TO_UINT64(_enet_addr)		\
	(u64)(((u64)(_enet_addr)[0] << 40) |		\
	      ((u64)(_enet_addr)[1] << 32) |		\
	      ((u64)(_enet_addr)[2] << 24) |		\
	      ((u64)(_enet_addr)[3] << 16) |		\
	      ((u64)(_enet_addr)[4] << 8) |		\
	      ((u64)(_enet_addr)[5]))

#define MAKE_ENET_ADDR_FROM_UINT64(_addr64, _enet_addr) \
	do { \
		int i; \
		for (i = 0; i < ETH_ALEN; i++) \
			(_enet_addr)[i] = \
			(u8)((_addr64) >> ((5 - i) * 8)); \
	} while (0)

/* defaults */
#define DEFAULT_RESET_ON_INIT                 false

/* PFC defines */
#define FSL_FM_PAUSE_TIME_ENABLE	0xf000
#define FSL_FM_PAUSE_TIME_DISABLE	0
#define FSL_FM_PAUSE_THRESH_DEFAULT	0

#define FM_MAC_NO_PFC   0xff

/* HASH defines */
#define ETH_HASH_ENTRY_OBJ(ptr)	\
	hlist_entry_safe(ptr, struct eth_hash_entry, node)

/* FM MAC Exceptions */
enum fman_mac_exceptions {
	FM_MAC_EX_10G_MDIO_SCAN_EVENT = 0
	/* 10GEC MDIO scan event interrupt */
	, FM_MAC_EX_10G_MDIO_CMD_CMPL
	/* 10GEC MDIO command completion interrupt */
	, FM_MAC_EX_10G_REM_FAULT
	/* 10GEC, mEMAC Remote fault interrupt */
	, FM_MAC_EX_10G_LOC_FAULT
	/* 10GEC, mEMAC Local fault interrupt */
	, FM_MAC_EX_10G_TX_ECC_ER
	/* 10GEC, mEMAC Transmit frame ECC error interrupt */
	, FM_MAC_EX_10G_TX_FIFO_UNFL
	/* 10GEC, mEMAC Transmit FIFO underflow interrupt */
	, FM_MAC_EX_10G_TX_FIFO_OVFL
	/* 10GEC, mEMAC Transmit FIFO overflow interrupt */
	, FM_MAC_EX_10G_TX_ER
	/* 10GEC Transmit frame error interrupt */
	, FM_MAC_EX_10G_RX_FIFO_OVFL
	/* 10GEC, mEMAC Receive FIFO overflow interrupt */
	, FM_MAC_EX_10G_RX_ECC_ER
	/* 10GEC, mEMAC Receive frame ECC error interrupt */
	, FM_MAC_EX_10G_RX_JAB_FRM
	/* 10GEC Receive jabber frame interrupt */
	, FM_MAC_EX_10G_RX_OVRSZ_FRM
	/* 10GEC Receive oversized frame interrupt */
	, FM_MAC_EX_10G_RX_RUNT_FRM
	/* 10GEC Receive runt frame interrupt */
	, FM_MAC_EX_10G_RX_FRAG_FRM
	/* 10GEC Receive fragment frame interrupt */
	, FM_MAC_EX_10G_RX_LEN_ER
	/* 10GEC Receive payload length error interrupt */
	, FM_MAC_EX_10G_RX_CRC_ER
	/* 10GEC Receive CRC error interrupt */
	, FM_MAC_EX_10G_RX_ALIGN_ER
	/* 10GEC Receive alignment error interrupt */
	, FM_MAC_EX_1G_BAB_RX
	/* dTSEC Babbling receive error */
	, FM_MAC_EX_1G_RX_CTL
	/* dTSEC Receive control (pause frame) interrupt */
	, FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET
	/* dTSEC Graceful transmit stop complete */
	, FM_MAC_EX_1G_BAB_TX
	/* dTSEC Babbling transmit error */
	, FM_MAC_EX_1G_TX_CTL
	/* dTSEC Transmit control (pause frame) interrupt */
	, FM_MAC_EX_1G_TX_ERR
	/* dTSEC Transmit error */
	, FM_MAC_EX_1G_LATE_COL
	/* dTSEC Late collision */
	, FM_MAC_EX_1G_COL_RET_LMT
	/* dTSEC Collision retry limit */
	, FM_MAC_EX_1G_TX_FIFO_UNDRN
	/* dTSEC Transmit FIFO underrun */
	, FM_MAC_EX_1G_MAG_PCKT
	/* dTSEC Magic Packet detection */
	, FM_MAC_EX_1G_MII_MNG_RD_COMPLET
	/* dTSEC MII management read completion */
	, FM_MAC_EX_1G_MII_MNG_WR_COMPLET
	/* dTSEC MII management write completion */
	, FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET
	/* dTSEC Graceful receive stop complete */
	, FM_MAC_EX_1G_DATA_ERR
	/* dTSEC Internal data error on transmit */
	, FM_MAC_1G_RX_DATA_ERR
	/* dTSEC Internal data error on receive */
	, FM_MAC_EX_1G_1588_TS_RX_ERR
	/* dTSEC Time-Stamp Receive Error */
	, FM_MAC_EX_1G_RX_MIB_CNT_OVFL
	/* dTSEC MIB counter overflow */
	, FM_MAC_EX_TS_FIFO_ECC_ERR
	/* mEMAC Time-stamp FIFO ECC error interrupt;
	 * not supported on T4240/B4860 rev1 chips
	 */
	, FM_MAC_EX_MAGIC_PACKET_INDICATION = FM_MAC_EX_1G_MAG_PCKT
	/* mEMAC Magic Packet Indication Interrupt */
};

struct eth_hash_entry {
	u64 addr;		/* Ethernet Address  */
	struct list_head node;
};

typedef void (fman_mac_exception_cb)(struct mac_device *dev_id,
				     enum fman_mac_exceptions exceptions);

/* FMan MAC config input */
struct fman_mac_params {
	/* MAC ID; numbering of dTSEC and 1G-mEMAC:
	 * 0 - FM_MAX_NUM_OF_1G_MACS;
	 * numbering of 10G-MAC (TGEC) and 10G-mEMAC:
	 * 0 - FM_MAX_NUM_OF_10G_MACS
	 */
	u8 mac_id;
	/* Note that the speed should indicate the maximum rate that
	 * this MAC should support rather than the actual speed;
	 */
	u16 max_speed;
	/* A handle to the FM object this port related to */
	void *fm;
	fman_mac_exception_cb *event_cb;    /* MDIO Events Callback Routine */
	fman_mac_exception_cb *exception_cb;/* Exception Callback Routine */
	/* SGMII/QSGII interface with 1000BaseX auto-negotiation between MAC
	 * and phy or backplane; Note: 1000BaseX auto-negotiation relates only
	 * to interface between MAC and phy/backplane, SGMII phy can still
	 * synchronize with far-end phy at 10Mbps, 100Mbps or 1000Mbps
	*/
	bool basex_if;
};

struct eth_hash_t {
	u16 size;
	struct list_head *lsts;
};

static inline struct eth_hash_entry
*dequeue_addr_from_hash_entry(struct list_head *addr_lst)
{
	struct eth_hash_entry *hash_entry = NULL;

	if (!list_empty(addr_lst)) {
		hash_entry = ETH_HASH_ENTRY_OBJ(addr_lst->next);
		list_del_init(&hash_entry->node);
	}
	return hash_entry;
}

static inline void free_hash_table(struct eth_hash_t *hash)
{
	struct eth_hash_entry *hash_entry;
	int i = 0;

	if (hash) {
		if (hash->lsts) {
			for (i = 0; i < hash->size; i++) {
				hash_entry =
				dequeue_addr_from_hash_entry(&hash->lsts[i]);
				while (hash_entry) {
					kfree(hash_entry);
					hash_entry =
					dequeue_addr_from_hash_entry(&hash->
								     lsts[i]);
				}
			}

			kfree(hash->lsts);
		}

		kfree(hash);
	}
}

static inline struct eth_hash_t *alloc_hash_table(u16 size)
{
	u32 i;
	struct eth_hash_t *hash;

	/* Allocate address hash table */
	hash = kmalloc(sizeof(*hash), GFP_KERNEL);
	if (!hash)
		return NULL;

	hash->size = size;

	hash->lsts = kmalloc_array(hash->size, sizeof(struct list_head),
				   GFP_KERNEL);
	if (!hash->lsts) {
		kfree(hash);
		return NULL;
	}

	for (i = 0; i < hash->size; i++)
		INIT_LIST_HEAD(&hash->lsts[i]);

	return hash;
}

#endif /* __FM_MAC_H */
