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

#include "icssg_switch_map.h"

#define ICSS_SLICE0	0
#define ICSS_SLICE1	1

#define ICSSG_MAX_RFLOWS	8	/* per slice */

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

	/* DMA related */
	struct prueth_tx_chn tx_chns[PRUETH_MAX_TX_QUEUES];
	struct completion tdown_complete;
	atomic_t tdown_cnt;
	struct prueth_rx_chn rx_chns;
	int rx_flow_id_base;
	int tx_ch_num;

	spinlock_t lock;	/* serialize access */

	unsigned long state;
	struct completion cmd_complete;
	/* Mutex to serialize access to firmware command interface */
	struct mutex cmd_lock;
	struct work_struct rx_mode_work;
	struct workqueue_struct	*cmd_wq;

	struct pruss_mem_region dram;
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

#endif /* __NET_TI_ICSSG_PRUETH_H */
