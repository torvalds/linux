/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 */

#ifndef AM65_CPSW_NUSS_H_
#define AM65_CPSW_NUSS_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#define HOST_PORT_NUM		0

#define AM65_CPSW_MAX_TX_QUEUES	8
#define AM65_CPSW_MAX_RX_QUEUES	1
#define AM65_CPSW_MAX_RX_FLOWS	1

struct am65_cpsw_slave_data {
	bool				mac_only;
	struct cpsw_sl			*mac_sl;
	struct device_node		*phy_node;
	struct phy_device		*phy;
	phy_interface_t			phy_if;
	struct phy			*ifphy;
	bool				rx_pause;
	bool				tx_pause;
	u8				mac_addr[ETH_ALEN];
};

struct am65_cpsw_port {
	struct am65_cpsw_common		*common;
	struct net_device		*ndev;
	const char			*name;
	u32				port_id;
	void __iomem			*port_base;
	void __iomem			*stat_base;
	bool				disabled;
	struct am65_cpsw_slave_data	slave;
};

struct am65_cpsw_host {
	struct am65_cpsw_common		*common;
	void __iomem			*port_base;
	void __iomem			*stat_base;
};

struct am65_cpsw_tx_chn {
	struct napi_struct napi_tx;
	struct am65_cpsw_common	*common;
	struct k3_cppi_desc_pool *desc_pool;
	struct k3_udma_glue_tx_channel *tx_chn;
	int irq;
	u32 id;
	u32 descs_num;
	char tx_chn_name[128];
};

struct am65_cpsw_rx_chn {
	struct device *dev;
	struct k3_cppi_desc_pool *desc_pool;
	struct k3_udma_glue_rx_channel *rx_chn;
	u32 descs_num;
	int irq;
};

#define AM65_CPSW_QUIRK_I2027_NO_TX_CSUM BIT(0)

struct am65_cpsw_pdata {
	u32	quirks;
};

struct am65_cpsw_common {
	struct device		*dev;
	const struct am65_cpsw_pdata *pdata;

	void __iomem		*ss_base;
	void __iomem		*cpsw_base;

	u32			port_num;
	struct am65_cpsw_host   host;
	struct am65_cpsw_port	*ports;
	u32			disabled_ports_mask;

	int			usage_count; /* number of opened ports */
	struct cpsw_ale		*ale;
	int			tx_ch_num;
	u32			rx_flow_id_base;

	struct am65_cpsw_tx_chn	tx_chns[AM65_CPSW_MAX_TX_QUEUES];
	struct completion	tdown_complete;
	atomic_t		tdown_cnt;

	struct am65_cpsw_rx_chn	rx_chns;
	struct napi_struct	napi_rx;

	u32			nuss_ver;
	u32			cpsw_ver;

	bool			pf_p0_rx_ptype_rrobin;
};

struct am65_cpsw_ndev_stats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_packets;
	u64 rx_bytes;
	struct u64_stats_sync syncp;
};

struct am65_cpsw_ndev_priv {
	u32			msg_enable;
	struct am65_cpsw_port	*port;
	struct am65_cpsw_ndev_stats __percpu *stats;
};

#define am65_ndev_to_priv(ndev) \
	((struct am65_cpsw_ndev_priv *)netdev_priv(ndev))
#define am65_ndev_to_port(ndev) (am65_ndev_to_priv(ndev)->port)
#define am65_ndev_to_common(ndev) (am65_ndev_to_port(ndev)->common)
#define am65_ndev_to_slave(ndev) (&am65_ndev_to_port(ndev)->slave)

#define am65_common_get_host(common) (&(common)->host)
#define am65_common_get_port(common, id) (&(common)->ports[(id) - 1])

#define am65_cpsw_napi_to_common(pnapi) \
	container_of(pnapi, struct am65_cpsw_common, napi_rx)
#define am65_cpsw_napi_to_tx_chn(pnapi) \
	container_of(pnapi, struct am65_cpsw_tx_chn, napi_tx)

#define AM65_CPSW_DRV_NAME "am65-cpsw-nuss"

#define AM65_CPSW_IS_CPSW2G(common) ((common)->port_num == 1)

extern const struct ethtool_ops am65_cpsw_ethtool_ops_slave;

void am65_cpsw_nuss_adjust_link(struct net_device *ndev);
void am65_cpsw_nuss_set_p0_ptype(struct am65_cpsw_common *common);
void am65_cpsw_nuss_remove_tx_chns(struct am65_cpsw_common *common);
int am65_cpsw_nuss_update_tx_chns(struct am65_cpsw_common *common, int num_tx);

#endif /* AM65_CPSW_NUSS_H_ */
