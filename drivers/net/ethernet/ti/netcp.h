/*
 * NetCP driver local header
 *
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Sandeep Nair <sandeep_n@ti.com>
 *		Sandeep Paulraj <s-paulraj@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *		Wingman Kwok <w-kwok2@ti.com>
 *		Murali Karicheri <m-karicheri2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __NETCP_H__
#define __NETCP_H__

#include <linux/netdevice.h>
#include <linux/soc/ti/knav_dma.h>

/* Maximum Ethernet frame size supported by Keystone switch */
#define NETCP_MAX_FRAME_SIZE		9504

#define SGMII_LINK_MAC_MAC_AUTONEG	0
#define SGMII_LINK_MAC_PHY		1
#define SGMII_LINK_MAC_MAC_FORCED	2
#define SGMII_LINK_MAC_FIBER		3
#define SGMII_LINK_MAC_PHY_NO_MDIO	4
#define XGMII_LINK_MAC_PHY		10
#define XGMII_LINK_MAC_MAC_FORCED	11

struct netcp_device;

struct netcp_tx_pipe {
	struct netcp_device	*netcp_device;
	void			*dma_queue;
	unsigned int		dma_queue_id;
	/* To port for packet forwarded to switch. Used only by ethss */
	u8			switch_to_port;
#define	SWITCH_TO_PORT_IN_TAGINFO	BIT(0)
	u8			flags;
	void			*dma_channel;
	const char		*dma_chan_name;
};

#define ADDR_NEW			BIT(0)
#define ADDR_VALID			BIT(1)

enum netcp_addr_type {
	ADDR_ANY,
	ADDR_DEV,
	ADDR_UCAST,
	ADDR_MCAST,
	ADDR_BCAST
};

struct netcp_addr {
	struct netcp_intf	*netcp;
	unsigned char		addr[ETH_ALEN];
	enum netcp_addr_type	type;
	unsigned int		flags;
	struct list_head	node;
};

struct netcp_intf {
	struct device		*dev;
	struct device		*ndev_dev;
	struct net_device	*ndev;
	bool			big_endian;
	unsigned int		tx_compl_qid;
	void			*tx_pool;
	struct list_head	txhook_list_head;
	unsigned int		tx_pause_threshold;
	void			*tx_compl_q;

	unsigned int		tx_resume_threshold;
	void			*rx_queue;
	void			*rx_pool;
	struct list_head	rxhook_list_head;
	unsigned int		rx_queue_id;
	void			*rx_fdq[KNAV_DMA_FDQ_PER_CHAN];
	struct napi_struct	rx_napi;
	struct napi_struct	tx_napi;

	void			*rx_channel;
	const char		*dma_chan_name;
	u32			rx_pool_size;
	u32			rx_pool_region_id;
	u32			tx_pool_size;
	u32			tx_pool_region_id;
	struct list_head	module_head;
	struct list_head	interface_list;
	struct list_head	addr_list;
	bool			netdev_registered;
	bool			primary_module_attached;

	/* Lock used for protecting Rx/Tx hook list management */
	spinlock_t		lock;
	struct netcp_device	*netcp_device;
	struct device_node	*node_interface;

	/* DMA configuration data */
	u32			msg_enable;
	u32			rx_queue_depths[KNAV_DMA_FDQ_PER_CHAN];
};

#define	NETCP_PSDATA_LEN		KNAV_DMA_NUM_PS_WORDS
struct netcp_packet {
	struct sk_buff		*skb;
	u32			*epib;
	u32			*psdata;
	unsigned int		psdata_len;
	struct netcp_intf	*netcp;
	struct netcp_tx_pipe	*tx_pipe;
	bool			rxtstamp_complete;
	void			*ts_context;

	int	(*txtstamp_complete)(void *ctx, struct netcp_packet *pkt);
};

static inline u32 *netcp_push_psdata(struct netcp_packet *p_info,
				     unsigned int bytes)
{
	u32 *buf;
	unsigned int words;

	if ((bytes & 0x03) != 0)
		return NULL;
	words = bytes >> 2;

	if ((p_info->psdata_len + words) > NETCP_PSDATA_LEN)
		return NULL;

	p_info->psdata_len += words;
	buf = &p_info->psdata[NETCP_PSDATA_LEN - p_info->psdata_len];
	return buf;
}

static inline int netcp_align_psdata(struct netcp_packet *p_info,
				     unsigned int byte_align)
{
	int padding;

	switch (byte_align) {
	case 0:
		padding = -EINVAL;
		break;
	case 1:
	case 2:
	case 4:
		padding = 0;
		break;
	case 8:
		padding = (p_info->psdata_len << 2) % 8;
		break;
	case 16:
		padding = (p_info->psdata_len << 2) % 16;
		break;
	default:
		padding = (p_info->psdata_len << 2) % byte_align;
		break;
	}
	return padding;
}

struct netcp_module {
	const char		*name;
	struct module		*owner;
	bool			primary;

	/* probe/remove: called once per NETCP instance */
	int	(*probe)(struct netcp_device *netcp_device,
			 struct device *device, struct device_node *node,
			 void **inst_priv);
	int	(*remove)(struct netcp_device *netcp_device, void *inst_priv);

	/* attach/release: called once per network interface */
	int	(*attach)(void *inst_priv, struct net_device *ndev,
			  struct device_node *node, void **intf_priv);
	int	(*release)(void *intf_priv);
	int	(*open)(void *intf_priv, struct net_device *ndev);
	int	(*close)(void *intf_priv, struct net_device *ndev);
	int	(*add_addr)(void *intf_priv, struct netcp_addr *naddr);
	int	(*del_addr)(void *intf_priv, struct netcp_addr *naddr);
	int	(*add_vid)(void *intf_priv, int vid);
	int	(*del_vid)(void *intf_priv, int vid);
	int	(*ioctl)(void *intf_priv, struct ifreq *req, int cmd);

	/* used internally */
	struct list_head	module_list;
	struct list_head	interface_list;
};

int netcp_register_module(struct netcp_module *module);
void netcp_unregister_module(struct netcp_module *module);
void *netcp_module_get_intf_data(struct netcp_module *module,
				 struct netcp_intf *intf);

int netcp_txpipe_init(struct netcp_tx_pipe *tx_pipe,
		      struct netcp_device *netcp_device,
		      const char *dma_chan_name, unsigned int dma_queue_id);
int netcp_txpipe_open(struct netcp_tx_pipe *tx_pipe);
int netcp_txpipe_close(struct netcp_tx_pipe *tx_pipe);

typedef int netcp_hook_rtn(int order, void *data, struct netcp_packet *packet);
int netcp_register_txhook(struct netcp_intf *netcp_priv, int order,
			  netcp_hook_rtn *hook_rtn, void *hook_data);
int netcp_unregister_txhook(struct netcp_intf *netcp_priv, int order,
			    netcp_hook_rtn *hook_rtn, void *hook_data);
int netcp_register_rxhook(struct netcp_intf *netcp_priv, int order,
			  netcp_hook_rtn *hook_rtn, void *hook_data);
int netcp_unregister_rxhook(struct netcp_intf *netcp_priv, int order,
			    netcp_hook_rtn *hook_rtn, void *hook_data);
void *netcp_device_find_module(struct netcp_device *netcp_device,
			       const char *name);

/* SGMII functions */
int netcp_sgmii_reset(void __iomem *sgmii_ofs, int port);
bool netcp_sgmii_rtreset(void __iomem *sgmii_ofs, int port, bool set);
int netcp_sgmii_get_port_link(void __iomem *sgmii_ofs, int port);
int netcp_sgmii_config(void __iomem *sgmii_ofs, int port, u32 interface);

/* XGBE SERDES init functions */
int netcp_xgbe_serdes_init(void __iomem *serdes_regs, void __iomem *xgbe_regs);

#endif	/* __NETCP_H__ */
