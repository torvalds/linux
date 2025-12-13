/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSM Ethernet driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_PRUETH_H
#define __NET_TI_PRUETH_H

#include <linux/phy.h>
#include <linux/types.h>
#include <linux/pruss_driver.h>
#include <linux/remoteproc/pruss.h>

#include "icssm_switch.h"
#include "icssm_prueth_ptp.h"

/* ICSSM size of redundancy tag */
#define ICSSM_LRE_TAG_SIZE	6

/* PRUSS local memory map */
#define ICSS_LOCAL_SHARED_RAM	0x00010000
#define EMAC_MAX_PKTLEN		(ETH_HLEN + VLAN_HLEN + ETH_DATA_LEN)
/* Below macro is for 1528 Byte Frame support, to Allow even with
 * Redundancy tag
 */
#define EMAC_MAX_FRM_SUPPORT (ETH_HLEN + VLAN_HLEN + ETH_DATA_LEN + \
			      ICSSM_LRE_TAG_SIZE)

/* PRU Ethernet Type - Ethernet functionality (protocol
 * implemented) provided by the PRU firmware being loaded.
 */
enum pruss_ethtype {
	PRUSS_ETHTYPE_EMAC = 0,
	PRUSS_ETHTYPE_HSR,
	PRUSS_ETHTYPE_PRP,
	PRUSS_ETHTYPE_SWITCH,
	PRUSS_ETHTYPE_MAX,
};

#define PRUETH_IS_EMAC(p)	((p)->eth_type == PRUSS_ETHTYPE_EMAC)
#define PRUETH_IS_SWITCH(p)	((p)->eth_type == PRUSS_ETHTYPE_SWITCH)

/**
 * struct prueth_queue_desc - Queue descriptor
 * @rd_ptr:	Read pointer, points to a buffer descriptor in Shared PRU RAM.
 * @wr_ptr:	Write pointer, points to a buffer descriptor in Shared PRU RAM.
 * @busy_s:	Slave queue busy flag, set by slave(us) to request access from
 *		master(PRU).
 * @status:	Bit field status register, Bits:
 *			0: Master queue busy flag.
 *			1: Packet has been placed in collision queue.
 *			2: Packet has been discarded due to overflow.
 * @max_fill_level:	Maximum queue usage seen.
 * @overflow_cnt:	Count of queue overflows.
 *
 * Each port has up to 4 queues with variable length. The queue is processed
 * as ring buffer with read and write pointers. Both pointers are address
 * pointers and increment by 4 for each buffer descriptor position. Queue has
 * a length defined in constants and a status.
 */
struct prueth_queue_desc {
	u16 rd_ptr;
	u16 wr_ptr;
	u8 busy_s;
	u8 status;
	u8 max_fill_level;
	u8 overflow_cnt;
};

/**
 * struct prueth_queue_info - Information about a queue in memory
 * @buffer_offset: buffer offset in OCMC RAM
 * @queue_desc_offset: queue descriptor offset in Shared RAM
 * @buffer_desc_offset: buffer descriptors offset in Shared RAM
 * @buffer_desc_end: end address of buffer descriptors in Shared RAM
 */
struct prueth_queue_info {
	u16 buffer_offset;
	u16 queue_desc_offset;
	u16 buffer_desc_offset;
	u16 buffer_desc_end;
};

/**
 * struct prueth_packet_info - Info about a packet in buffer
 * @shadow: this packet is stored in the collision queue
 * @port: port packet is on
 * @length: length of packet
 * @broadcast: this packet is a broadcast packet
 * @error: this packet has an error
 * @lookup_success: src mac found in FDB
 * @flood: packet is to be flooded
 * @timestamp: Specifies if timestamp is appended to the packet
 */
struct prueth_packet_info {
	bool shadow;
	unsigned int port;
	unsigned int length;
	bool broadcast;
	bool error;
	bool lookup_success;
	bool flood;
	bool timestamp;
};

/* In switch mode there are 3 real ports i.e. 3 mac addrs.
 * however Linux sees only the host side port. The other 2 ports
 * are the switch ports.
 * In emac mode there are 2 real ports i.e. 2 mac addrs.
 * Linux sees both the ports.
 */
enum prueth_port {
	PRUETH_PORT_HOST = 0,	/* host side port */
	PRUETH_PORT_MII0,	/* physical port MII 0 */
	PRUETH_PORT_MII1,	/* physical port MII 1 */
	PRUETH_PORT_INVALID,	/* Invalid prueth port */
};

enum prueth_mac {
	PRUETH_MAC0 = 0,
	PRUETH_MAC1,
	PRUETH_NUM_MACS,
	PRUETH_MAC_INVALID,
};

/* In both switch & emac modes there are 3 port queues
 * EMAC mode:
 *     RX packets for both MII0 & MII1 ports come on
 *     QUEUE_HOST.
 *     TX packets for MII0 go on QUEUE_MII0, TX packets
 *     for MII1 go on QUEUE_MII1.
 * Switch mode:
 *     Host port RX packets come on QUEUE_HOST
 *     TX packets might have to go on MII0 or MII1 or both.
 *     MII0 TX queue is QUEUE_MII0 and MII1 TX queue is
 *     QUEUE_MII1.
 */
enum prueth_port_queue_id {
	PRUETH_PORT_QUEUE_HOST = 0,
	PRUETH_PORT_QUEUE_MII0,
	PRUETH_PORT_QUEUE_MII1,
	PRUETH_PORT_QUEUE_MAX,
};

/* Each port queue has 4 queues and 1 collision queue */
enum prueth_queue_id {
	PRUETH_QUEUE1 = 0,
	PRUETH_QUEUE2,
	PRUETH_QUEUE3,
	PRUETH_QUEUE4,
	PRUETH_COLQUEUE,        /* collision queue */
};

/**
 * struct prueth_firmware - PRU Ethernet FW data
 * @fw_name: firmware names of firmware to run on PRU
 */
struct prueth_firmware {
	const char *fw_name[PRUSS_ETHTYPE_MAX];
};

/* PRUeth memory range identifiers */
enum prueth_mem {
	PRUETH_MEM_DRAM0 = 0,
	PRUETH_MEM_DRAM1,
	PRUETH_MEM_SHARED_RAM,
	PRUETH_MEM_OCMC,
	PRUETH_MEM_MAX,
};

enum pruss_device {
	PRUSS_AM57XX = 0,
	PRUSS_AM43XX,
	PRUSS_AM33XX,
	PRUSS_K2G
};

/**
 * struct prueth_private_data - PRU Ethernet private data
 * @driver_data: PRU Ethernet device name
 * @fw_pru: firmware names to be used for PRUSS ethernet usecases
 */
struct prueth_private_data {
	enum pruss_device driver_data;
	const struct prueth_firmware fw_pru[PRUSS_NUM_PRUS];
};

struct prueth_emac_stats {
	u64 tx_packets;
	u64 tx_dropped;
	u64 tx_bytes;
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_length_errors;
	u64 rx_over_errors;
};

/* data for each emac port */
struct prueth_emac {
	struct prueth *prueth;
	struct net_device *ndev;
	struct napi_struct napi;

	struct rproc *pru;
	struct phy_device *phydev;
	struct prueth_queue_desc __iomem *rx_queue_descs;
	struct prueth_queue_desc __iomem *tx_queue_descs;

	int link;
	int speed;
	int duplex;
	int rx_irq;

	enum prueth_port_queue_id tx_port_queue;
	enum prueth_queue_id rx_queue_start;
	enum prueth_queue_id rx_queue_end;
	enum prueth_port port_id;
	enum prueth_mem dram;
	const char *phy_id;
	u32 msg_enable;
	u8 mac_addr[6];
	phy_interface_t phy_if;

	/* spin lock used to protect
	 * during link configuration
	 */
	spinlock_t lock;

	struct hrtimer tx_hrtimer;
	struct prueth_emac_stats stats;
};

struct prueth {
	struct device *dev;
	struct pruss *pruss;
	struct rproc *pru0, *pru1;
	struct pruss_mem_region mem[PRUETH_MEM_MAX];
	struct gen_pool *sram_pool;
	struct regmap *mii_rt;
	struct icss_iep *iep;

	const struct prueth_private_data *fw_data;
	struct prueth_fw_offsets *fw_offsets;

	struct device_node *eth_node[PRUETH_NUM_MACS];
	struct prueth_emac *emac[PRUETH_NUM_MACS];
	struct net_device *registered_netdevs[PRUETH_NUM_MACS];

	unsigned int eth_type;
	size_t ocmc_ram_size;
	u8 emac_configured;
};

void icssm_parse_packet_info(struct prueth *prueth, u32 buffer_descriptor,
			     struct prueth_packet_info *pkt_info);
int icssm_emac_rx_packet(struct prueth_emac *emac, u16 *bd_rd_ptr,
			 struct prueth_packet_info *pkt_info,
			 const struct prueth_queue_info *rxqueue);

#endif /* __NET_TI_PRUETH_H */
