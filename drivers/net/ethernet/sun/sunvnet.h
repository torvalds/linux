#ifndef _SUNVNET_H
#define _SUNVNET_H

#include <linux/interrupt.h>

#define DESC_NCOOKIES(entry_size)	\
	((entry_size) - sizeof(struct vio_net_desc))

/* length of time before we decide the hardware is borked,
 * and dev->tx_timeout() should be called to fix the problem
 */
#define VNET_TX_TIMEOUT			(5 * HZ)

/* length of time (or less) we expect pending descriptors to be marked
 * as VIO_DESC_DONE and skbs ready to be freed
 */
#define	VNET_CLEAN_TIMEOUT		((HZ/100)+1)

#define VNET_MAXPACKET			1518ULL /* ETH_FRAMELEN + VLAN_HDR */
#define VNET_TX_RING_SIZE		512
#define VNET_TX_WAKEUP_THRESH(dr)	((dr)->pending / 4)

/* VNET packets are sent in buffers with the first 6 bytes skipped
 * so that after the ethernet header the IPv4/IPv6 headers are aligned
 * properly.
 */
#define VNET_PACKET_SKIP		6

struct vnet_tx_entry {
	struct sk_buff		*skb;
	unsigned int		ncookies;
	struct ldc_trans_cookie	cookies[2];
};

struct vnet;
struct vnet_port {
	struct vio_driver_state	vio;

	struct hlist_node	hash;
	u8			raddr[ETH_ALEN];
	u8			switch_port;
	u8			__pad;

	struct vnet		*vp;

	struct vnet_tx_entry	tx_bufs[VNET_TX_RING_SIZE];

	struct list_head	list;

	u32			stop_rx_idx;
	bool			stop_rx;
	bool			start_cons;

	struct timer_list	clean_timer;

	u64			rmtu;
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
	/* Protects port_list and port_hash.  */
	spinlock_t		lock;

	struct net_device	*dev;

	u32			msg_enable;

	struct list_head	port_list;

	struct hlist_head	port_hash[VNET_PORT_HASH_SIZE];

	struct vnet_mcast_entry	*mcast_list;

	struct list_head	list;
	u64			local_mac;

	struct tasklet_struct	vnet_tx_wakeup;
};

#endif /* _SUNVNET_H */
