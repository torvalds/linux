/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSG Ethernet driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_ICSSG_PRUETH_H
#define __NET_TI_ICSSG_PRUETH_H

#include <linux/etherdevice.h>
#include <linux/genalloc.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/remoteproc/pruss.h>
#include <linux/pruss_driver.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/remoteproc.h>

#include <linux/dma-mapping.h>
#include <linux/dma/ti-cppi5.h>
#include <linux/dma/k3-udma-glue.h>

#include <net/devlink.h>

#include "icssg_config.h"
#include "icss_iep.h"
#include "icssg_switch_map.h"

#define PRUETH_MAX_MTU          (2000 - ETH_HLEN - ETH_FCS_LEN)
#define PRUETH_MIN_PKT_SIZE     (VLAN_ETH_ZLEN)
#define PRUETH_MAX_PKT_SIZE     (PRUETH_MAX_MTU + ETH_HLEN + ETH_FCS_LEN)

#define ICSS_SLICE0	0
#define ICSS_SLICE1	1

#define ICSS_FW_PRU	0
#define ICSS_FW_RTU	1

#define ICSSG_MAX_RFLOWS	8	/* per slice */

/* Number of ICSSG related stats */
#define ICSSG_NUM_STATS 60
#define ICSSG_NUM_STANDARD_STATS 31
#define ICSSG_NUM_ETHTOOL_STATS (ICSSG_NUM_STATS - ICSSG_NUM_STANDARD_STATS)

/* Firmware status codes */
#define ICSS_HS_FW_READY 0x55555555
#define ICSS_HS_FW_DEAD 0xDEAD0000	/* lower 16 bits contain error code */

/* Firmware command codes */
#define ICSS_HS_CMD_BUSY 0x40000000
#define ICSS_HS_CMD_DONE 0x80000000
#define ICSS_HS_CMD_CANCEL 0x10000000

/* Firmware commands */
#define ICSS_CMD_SPAD 0x20
#define ICSS_CMD_RXTX 0x10
#define ICSS_CMD_ADD_FDB 0x1
#define ICSS_CMD_DEL_FDB 0x2
#define ICSS_CMD_SET_RUN 0x4
#define ICSS_CMD_GET_FDB_SLOT 0x5
#define ICSS_CMD_ENABLE_VLAN 0x5
#define ICSS_CMD_DISABLE_VLAN 0x6
#define ICSS_CMD_ADD_FILTER 0x7
#define ICSS_CMD_ADD_MAC 0x8

/* In switch mode there are 3 real ports i.e. 3 mac addrs.
 * however Linux sees only the host side port. The other 2 ports
 * are the switch ports.
 * In emac mode there are 2 real ports i.e. 2 mac addrs.
 * Linux sees both the ports.
 */
enum prueth_port {
	PRUETH_PORT_HOST = 0,	/* host side port */
	PRUETH_PORT_MII0,	/* physical port RG/SG MII 0 */
	PRUETH_PORT_MII1,	/* physical port RG/SG MII 1 */
	PRUETH_PORT_INVALID,	/* Invalid prueth port */
};

enum prueth_mac {
	PRUETH_MAC0 = 0,
	PRUETH_MAC1,
	PRUETH_NUM_MACS,
	PRUETH_MAC_INVALID,
};

struct prueth_tx_chn {
	struct device *dma_dev;
	struct napi_struct napi_tx;
	struct k3_cppi_desc_pool *desc_pool;
	struct k3_udma_glue_tx_channel *tx_chn;
	struct prueth_emac *emac;
	u32 id;
	u32 descs_num;
	unsigned int irq;
	char name[32];
};

struct prueth_rx_chn {
	struct device *dev;
	struct device *dma_dev;
	struct k3_cppi_desc_pool *desc_pool;
	struct k3_udma_glue_rx_channel *rx_chn;
	u32 descs_num;
	unsigned int irq[ICSSG_MAX_RFLOWS];	/* separate irq per flow */
	char name[32];
};

/* There are 4 Tx DMA channels, but the highest priority is CH3 (thread 3)
 * and lower three are lower priority channels or threads.
 */
#define PRUETH_MAX_TX_QUEUES	4

#define PRUETH_MAX_TX_TS_REQUESTS	50 /* Max simultaneous TX_TS requests */

/* data for each emac port */
struct prueth_emac {
	bool fw_running;
	struct prueth *prueth;
	struct net_device *ndev;
	u8 mac_addr[6];
	struct napi_struct napi_rx;
	u32 msg_enable;

	int link;
	int speed;
	int duplex;

	const char *phy_id;
	struct device_node *phy_node;
	phy_interface_t phy_if;
	enum prueth_port port_id;
	struct icss_iep *iep;
	unsigned int rx_ts_enabled : 1;
	unsigned int tx_ts_enabled : 1;

	/* DMA related */
	struct prueth_tx_chn tx_chns[PRUETH_MAX_TX_QUEUES];
	struct completion tdown_complete;
	atomic_t tdown_cnt;
	struct prueth_rx_chn rx_chns;
	int rx_flow_id_base;
	int tx_ch_num;

	spinlock_t lock;	/* serialize access */

	/* TX HW Timestamping */
	/* TX TS cookie will be index to the tx_ts_skb array */
	struct sk_buff *tx_ts_skb[PRUETH_MAX_TX_TS_REQUESTS];
	atomic_t tx_ts_pending;
	int tx_ts_irq;

	u8 cmd_seq;
	/* shutdown related */
	u32 cmd_data[4];
	struct completion cmd_complete;
	/* Mutex to serialize access to firmware command interface */
	struct mutex cmd_lock;
	struct work_struct rx_mode_work;
	struct workqueue_struct	*cmd_wq;

	struct pruss_mem_region dram;

	struct delayed_work stats_work;
	u64 stats[ICSSG_NUM_STATS];
};

/**
 * struct prueth_pdata - PRUeth platform data
 * @fdqring_mode: Free desc queue mode
 * @quirk_10m_link_issue: 10M link detect errata
 */
struct prueth_pdata {
	enum k3_ring_mode fdqring_mode;
	u32	quirk_10m_link_issue:1;
};

/**
 * struct prueth - PRUeth structure
 * @dev: device
 * @pruss: pruss handle
 * @pru: rproc instances of PRUs
 * @rtu: rproc instances of RTUs
 * @txpru: rproc instances of TX_PRUs
 * @shram: PRUSS shared RAM region
 * @sram_pool: MSMC RAM pool for buffers
 * @msmcram: MSMC RAM region
 * @eth_node: DT node for the port
 * @emac: private EMAC data structure
 * @registered_netdevs: list of registered netdevs
 * @miig_rt: regmap to mii_g_rt block
 * @mii_rt: regmap to mii_rt block
 * @pru_id: ID for each of the PRUs
 * @pdev: pointer to ICSSG platform device
 * @pdata: pointer to platform data for ICSSG driver
 * @icssg_hwcmdseq: seq counter or HWQ messages
 * @emacs_initialized: num of EMACs/ext ports that are up/running
 * @iep0: pointer to IEP0 device
 * @iep1: pointer to IEP1 device
 */
struct prueth {
	struct device *dev;
	struct pruss *pruss;
	struct rproc *pru[PRUSS_NUM_PRUS];
	struct rproc *rtu[PRUSS_NUM_PRUS];
	struct rproc *txpru[PRUSS_NUM_PRUS];
	struct pruss_mem_region shram;
	struct gen_pool *sram_pool;
	struct pruss_mem_region msmcram;

	struct device_node *eth_node[PRUETH_NUM_MACS];
	struct prueth_emac *emac[PRUETH_NUM_MACS];
	struct net_device *registered_netdevs[PRUETH_NUM_MACS];
	struct regmap *miig_rt;
	struct regmap *mii_rt;

	enum pruss_pru_id pru_id[PRUSS_NUM_PRUS];
	struct platform_device *pdev;
	struct prueth_pdata pdata;
	u8 icssg_hwcmdseq;
	int emacs_initialized;
	struct icss_iep *iep0;
	struct icss_iep *iep1;
};

struct emac_tx_ts_response {
	u32 reserved[2];
	u32 cookie;
	u32 lo_ts;
	u32 hi_ts;
};

/* get PRUSS SLICE number from prueth_emac */
static inline int prueth_emac_slice(struct prueth_emac *emac)
{
	switch (emac->port_id) {
	case PRUETH_PORT_MII0:
		return ICSS_SLICE0;
	case PRUETH_PORT_MII1:
		return ICSS_SLICE1;
	default:
		return -EINVAL;
	}
}

extern const struct ethtool_ops icssg_ethtool_ops;

/* Classifier helpers */
void icssg_class_set_mac_addr(struct regmap *miig_rt, int slice, u8 *mac);
void icssg_class_set_host_mac_addr(struct regmap *miig_rt, const u8 *mac);
void icssg_class_disable(struct regmap *miig_rt, int slice);
void icssg_class_default(struct regmap *miig_rt, int slice, bool allmulti);
void icssg_ft1_set_mac_addr(struct regmap *miig_rt, int slice, u8 *mac_addr);

/* config helpers */
void icssg_config_ipg(struct prueth_emac *emac);
int icssg_config(struct prueth *prueth, struct prueth_emac *emac,
		 int slice);
int emac_set_port_state(struct prueth_emac *emac,
			enum icssg_port_state_cmd state);
void icssg_config_set_speed(struct prueth_emac *emac);

/* Buffer queue helpers */
int icssg_queue_pop(struct prueth *prueth, u8 queue);
void icssg_queue_push(struct prueth *prueth, int queue, u16 addr);
u32 icssg_queue_level(struct prueth *prueth, int queue);

#define prueth_napi_to_tx_chn(pnapi) \
	container_of(pnapi, struct prueth_tx_chn, napi_tx)

void emac_stats_work_handler(struct work_struct *work);
void emac_update_hardware_stats(struct prueth_emac *emac);
int emac_get_stat_by_name(struct prueth_emac *emac, char *stat_name);
#endif /* __NET_TI_ICSSG_PRUETH_H */
