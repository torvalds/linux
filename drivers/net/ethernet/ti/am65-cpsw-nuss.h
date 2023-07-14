/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 */

#ifndef AM65_CPSW_NUSS_H_
#define AM65_CPSW_NUSS_H_

#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/soc/ti/k3-ringacc.h>
#include <net/devlink.h>
#include "am65-cpsw-qos.h"

struct am65_cpts;

#define HOST_PORT_NUM		0

#define AM65_CPSW_MAX_TX_QUEUES	8
#define AM65_CPSW_MAX_RX_QUEUES	1
#define AM65_CPSW_MAX_RX_FLOWS	1

#define AM65_CPSW_PORT_VLAN_REG_OFFSET	0x014

struct am65_cpsw_slave_data {
	bool				mac_only;
	struct cpsw_sl			*mac_sl;
	struct device_node		*phy_node;
	phy_interface_t			phy_if;
	struct phy			*ifphy;
	struct phy			*serdes_phy;
	bool				rx_pause;
	bool				tx_pause;
	u8				mac_addr[ETH_ALEN];
	int				port_vlan;
	struct phylink			*phylink;
	struct phylink_config		phylink_config;
};

struct am65_cpsw_port {
	struct am65_cpsw_common		*common;
	struct net_device		*ndev;
	const char			*name;
	u32				port_id;
	void __iomem			*port_base;
	void __iomem			*sgmii_base;
	void __iomem			*stat_base;
	void __iomem			*fetch_ram_base;
	bool				disabled;
	struct am65_cpsw_slave_data	slave;
	bool				tx_ts_enabled;
	bool				rx_ts_enabled;
	struct am65_cpsw_qos		qos;
	struct devlink_port		devlink_port;
	/* Only for suspend resume context */
	u32				vid_context;
};

struct am65_cpsw_host {
	struct am65_cpsw_common		*common;
	void __iomem			*port_base;
	void __iomem			*stat_base;
	/* Only for suspend resume context */
	u32				vid_context;
};

struct am65_cpsw_tx_chn {
	struct device *dma_dev;
	struct napi_struct napi_tx;
	struct am65_cpsw_common	*common;
	struct k3_cppi_desc_pool *desc_pool;
	struct k3_udma_glue_tx_channel *tx_chn;
	spinlock_t lock; /* protect TX rings in multi-port mode */
	int irq;
	u32 id;
	u32 descs_num;
	char tx_chn_name[128];
	u32 rate_mbps;
};

struct am65_cpsw_rx_chn {
	struct device *dev;
	struct device *dma_dev;
	struct k3_cppi_desc_pool *desc_pool;
	struct k3_udma_glue_rx_channel *rx_chn;
	u32 descs_num;
	int irq;
};

#define AM65_CPSW_QUIRK_I2027_NO_TX_CSUM BIT(0)
#define AM64_CPSW_QUIRK_DMA_RX_TDOWN_IRQ BIT(1)

struct am65_cpsw_pdata {
	u32	quirks;
	u64	extra_modes;
	enum k3_ring_mode fdqring_mode;
	const char	*ale_dev_id;
};

enum cpsw_devlink_param_id {
	AM65_CPSW_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	AM65_CPSW_DL_PARAM_SWITCH_MODE,
};

struct am65_cpsw_devlink {
	struct am65_cpsw_common *common;
};

struct am65_cpsw_common {
	struct device		*dev;
	struct device		*mdio_dev;
	struct am65_cpsw_pdata	pdata;

	void __iomem		*ss_base;
	void __iomem		*cpsw_base;

	u32			port_num;
	struct am65_cpsw_host   host;
	struct am65_cpsw_port	*ports;
	u32			disabled_ports_mask;
	struct net_device	*dma_ndev;

	int			usage_count; /* number of opened ports */
	struct cpsw_ale		*ale;
	int			tx_ch_num;
	u32			tx_ch_rate_msk;
	u32			rx_flow_id_base;

	struct am65_cpsw_tx_chn	tx_chns[AM65_CPSW_MAX_TX_QUEUES];
	struct completion	tdown_complete;
	atomic_t		tdown_cnt;

	struct am65_cpsw_rx_chn	rx_chns;
	struct napi_struct	napi_rx;

	bool			rx_irq_disabled;

	u32			nuss_ver;
	u32			cpsw_ver;
	unsigned long		bus_freq;
	bool			pf_p0_rx_ptype_rrobin;
	struct am65_cpts	*cpts;
	int			est_enabled;

	bool		is_emac_mode;
	u16			br_members;
	int			default_vlan;
	struct devlink *devlink;
	struct net_device *hw_bridge_dev;
	struct notifier_block am65_cpsw_netdevice_nb;
	unsigned char switch_id[MAX_PHYS_ITEM_ID_LEN];
	/* only for suspend/resume context restore */
	u32			*ale_context;
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
	bool offload_fwd_mark;
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

bool am65_cpsw_port_dev_check(const struct net_device *dev);

#endif /* AM65_CPSW_NUSS_H_ */
