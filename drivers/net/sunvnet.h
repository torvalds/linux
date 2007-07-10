#ifndef _SUNVNET_H
#define _SUNVNET_H

#define DESC_NCOOKIES(entry_size)	\
	((entry_size) - sizeof(struct vio_net_desc))

/* length of time before we decide the hardware is borked,
 * and dev->tx_timeout() should be called to fix the problem
 */
#define VNET_TX_TIMEOUT			(5 * HZ)

#define VNET_TX_RING_SIZE		512
#define VNET_TX_WAKEUP_THRESH(dr)	((dr)->pending / 4)

/* VNET packets are sent in buffers with the first 6 bytes skipped
 * so that after the ethernet header the IPv4/IPv6 headers are aligned
 * properly.
 */
#define VNET_PACKET_SKIP		6

struct vnet_tx_entry {
	void			*buf;
	unsigned int		ncookies;
	struct ldc_trans_cookie	cookies[2];
};

struct vnet;
struct vnet_port {
	struct vio_driver_state	vio;

	struct hlist_node	hash;
	u8			raddr[ETH_ALEN];

	struct vnet		*vp;

	struct vnet_tx_entry	tx_bufs[VNET_TX_RING_SIZE];

	struct list_head	list;
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

struct vnet {
	/* Protects port_list and port_hash.  */
	spinlock_t		lock;

	struct net_device	*dev;

	u32			msg_enable;
	struct vio_dev		*vdev;

	struct list_head	port_list;

	struct hlist_head	port_hash[VNET_PORT_HASH_SIZE];
};

#endif /* _SUNVNET_H */
