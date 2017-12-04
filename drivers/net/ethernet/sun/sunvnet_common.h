/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SUNVNETCOMMON_H
#define _SUNVNETCOMMON_H

#include <linux/interrupt.h>

/* length of time (or less) we expect pending descriptors to be marked
 * as VIO_DESC_DONE and skbs ready to be freed
 */
#define	VNET_CLEAN_TIMEOUT		((HZ / 100) + 1)

#define VNET_MAXPACKET			(65535ULL + ETH_HLEN + VLAN_HLEN)
#define VNET_TX_RING_SIZE		512
#define VNET_TX_WAKEUP_THRESH(dr)	((dr)->pending / 4)

#define	VNET_MINTSO	 2048	/* VIO protocol's minimum TSO len */
#define	VNET_MAXTSO	65535	/* VIO protocol's maximum TSO len */

#define VNET_MAX_MTU	65535

/* VNET packets are sent in buffers with the first 6 bytes skipped
 * so that after the ethernet header the IPv4/IPv6 headers are aligned
 * properly.
 */
#define VNET_PACKET_SKIP		6

#define	VNET_MAXCOOKIES			(VNET_MAXPACKET / PAGE_SIZE + 1)

#define	VNET_MAX_TXQS		16

struct vnet_tx_entry {
	struct sk_buff		*skb;
	unsigned int		ncookies;
	struct ldc_trans_cookie	cookies[VNET_MAXCOOKIES];
};

struct vnet;

struct vnet_port_stats {
	/* keep them all the same size */
	u32 rx_bytes;
	u32 tx_bytes;
	u32 rx_packets;
	u32 tx_packets;
	u32 event_up;
	u32 event_reset;
	u32 q_placeholder;
};

#define NUM_VNET_PORT_STATS  (sizeof(struct vnet_port_stats) / sizeof(u32))

/* Structure to describe a vnet-port or vsw-port in the MD.
 * If the vsw bit is set, this structure represents a vswitch
 * port, and the net_device can be found from ->dev. If the
 * vsw bit is not set, the net_device is available from ->vp->dev.
 * See the VNET_PORT_TO_NET_DEVICE macro below.
 */
struct vnet_port {
	struct vio_driver_state	vio;

	struct vnet_port_stats stats;

	struct hlist_node	hash;
	u8			raddr[ETH_ALEN];
	unsigned		switch_port:1;
	unsigned		tso:1;
	unsigned		vsw:1;
	unsigned		__pad:13;

	struct vnet		*vp;
	struct net_device	*dev;

	struct vnet_tx_entry	tx_bufs[VNET_TX_RING_SIZE];

	struct list_head	list;

	u32			stop_rx_idx;
	bool			stop_rx;
	bool			start_cons;

	struct timer_list	clean_timer;

	u64			rmtu;
	u16			tsolen;

	struct napi_struct	napi;
	u32			napi_stop_idx;
	bool			napi_resume;
	int			rx_event;
	u16			q_index;
};

static inline struct vnet_port *to_vnet_port(struct vio_driver_state *vio)
{
	return container_of(vio, struct vnet_port, vio);
}

#define VNET_PORT_HASH_SIZE	16
#define VNET_PORT_HASH_MASK	(VNET_PORT_HASH_SIZE - 1)

static inline unsigned int vnet_hashfn(u8 *mac)
{
	unsigned int val = mac[4] ^ mac[5];

	return val & (VNET_PORT_HASH_MASK);
}

struct vnet_mcast_entry {
	u8			addr[ETH_ALEN];
	u8			sent;
	u8			hit;
	struct vnet_mcast_entry	*next;
};

struct vnet {
	spinlock_t		lock; /* Protects port_list and port_hash.  */
	struct net_device	*dev;
	u32			msg_enable;
	u8			q_used[VNET_MAX_TXQS];
	struct list_head	port_list;
	struct hlist_head	port_hash[VNET_PORT_HASH_SIZE];
	struct vnet_mcast_entry	*mcast_list;
	struct list_head	list;
	u64			local_mac;
	int			nports;
};

/* Def used by common code to get the net_device from the proper location */
#define VNET_PORT_TO_NET_DEVICE(__port) \
	((__port)->vsw ? (__port)->dev : (__port)->vp->dev)

/* Common funcs */
void sunvnet_clean_timer_expire_common(struct timer_list *t);
int sunvnet_open_common(struct net_device *dev);
int sunvnet_close_common(struct net_device *dev);
void sunvnet_set_rx_mode_common(struct net_device *dev, struct vnet *vp);
int sunvnet_set_mac_addr_common(struct net_device *dev, void *p);
void sunvnet_tx_timeout_common(struct net_device *dev);
int sunvnet_start_xmit_common(struct sk_buff *skb, struct net_device *dev,
			   struct vnet_port *(*vnet_tx_port)
			   (struct sk_buff *, struct net_device *));
#ifdef CONFIG_NET_POLL_CONTROLLER
void sunvnet_poll_controller_common(struct net_device *dev, struct vnet *vp);
#endif
void sunvnet_event_common(void *arg, int event);
int sunvnet_send_attr_common(struct vio_driver_state *vio);
int sunvnet_handle_attr_common(struct vio_driver_state *vio, void *arg);
void sunvnet_handshake_complete_common(struct vio_driver_state *vio);
int sunvnet_poll_common(struct napi_struct *napi, int budget);
void sunvnet_port_free_tx_bufs_common(struct vnet_port *port);
void vnet_port_reset(struct vnet_port *port);
bool sunvnet_port_is_up_common(struct vnet_port *vnet);
void sunvnet_port_add_txq_common(struct vnet_port *port);
void sunvnet_port_rm_txq_common(struct vnet_port *port);

#endif /* _SUNVNETCOMMON_H */
