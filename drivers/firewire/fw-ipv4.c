/*
 * IPv4 over IEEE 1394, per RFC 2734
 *
 * Copyright (C) 2009 Jay Fenlason <fenlason@redhat.com>
 *
 * based on eth1394 by Ben Collins et al
 */

#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/highmem.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <asm/unaligned.h>
#include <net/arp.h>

/* Things to potentially make runtime cofigurable */
/* must be at least as large as our maximum receive size */
#define FIFO_SIZE 4096
/* Network timeout in glibbles */
#define IPV4_TIMEOUT       100000

/* Runitme configurable paramaters */
static int ipv4_mpd = 25;
static int ipv4_max_xmt = 0;
/* 16k for receiving arp and broadcast packets.  Enough? */
static int ipv4_iso_page_count = 4;

MODULE_AUTHOR("Jay Fenlason (fenlason@redhat.com)");
MODULE_DESCRIPTION("Firewire IPv4 Driver (IPv4-over-IEEE1394 as per RFC 2734)");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(ieee1394, ipv4_id_table);
module_param_named(max_partial_datagrams, ipv4_mpd, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_partial_datagrams, "Maximum number of received"
 " incomplete fragmented datagrams (default = 25).");

/* Max xmt is useful for forcing fragmentation, which makes testing easier. */
module_param_named(max_transmit, ipv4_max_xmt, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_transmit, "Maximum datagram size to transmit"
 " (larger datagrams will be fragmented) (default = 0 (use hardware defaults).");

/* iso page count controls how many pages will be used for receiving broadcast packets. */
module_param_named(iso_pages, ipv4_iso_page_count, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(iso_pages, "Number of pages to use for receiving broadcast packets"
 " (default = 4).");

/* uncomment this line to do debugging */
#define fw_debug(s, args...) printk(KERN_DEBUG KBUILD_MODNAME ": " s, ## args)

/* comment out these lines to do debugging. */
/* #undef fw_debug */
/* #define fw_debug(s...) */
/* #define print_hex_dump(l...) */

/* Define a fake hardware header format for the networking core.  Note that
 * header size cannot exceed 16 bytes as that is the size of the header cache.
 * Also, we do not need the source address in the header so we omit it and
 * keep the header to under 16 bytes */
#define IPV4_ALEN (8)
/* This must equal sizeof(struct ipv4_ether_hdr) */
#define IPV4_HLEN (10)

/* FIXME: what's a good size for this? */
#define INVALID_FIFO_ADDR (u64)~0ULL

/* Things specified by standards */
#define BROADCAST_CHANNEL 31

#define S100_BUFFER_SIZE 512
#define MAX_BUFFER_SIZE 4096

#define IPV4_GASP_SPECIFIER_ID	0x00005EU
#define IPV4_GASP_VERSION	0x00000001U

#define IPV4_GASP_OVERHEAD (2 * sizeof(u32)) /* for GASP header */

#define IPV4_UNFRAG_HDR_SIZE	sizeof(u32)
#define IPV4_FRAG_HDR_SIZE	(2 * sizeof(u32))
#define IPV4_FRAG_OVERHEAD	sizeof(u32)

#define ALL_NODES (0xffc0 | 0x003f)

#define IPV4_HDR_UNFRAG		0	/* unfragmented		*/
#define IPV4_HDR_FIRSTFRAG	1	/* first fragment	*/
#define IPV4_HDR_LASTFRAG	2	/* last fragment	*/
#define IPV4_HDR_INTFRAG	3	/* interior fragment	*/

/* Our arp packet (ARPHRD_IEEE1394) */
/* FIXME: note that this is probably bogus on weird-endian machines */
struct ipv4_arp {
	u16 hw_type;		/* 0x0018	*/
	u16 proto_type;		/* 0x0806       */
	u8 hw_addr_len;		/* 16		*/
	u8 ip_addr_len;         /* 4		*/
	u16 opcode;	        /* ARP Opcode	*/
	/* Above is exactly the same format as struct arphdr */

	u64 s_uniq_id;		/* Sender's 64bit EUI			*/
	u8 max_rec;             /* Sender's max packet size		*/
	u8 sspd;		/* Sender's max speed			*/
	u16 fifo_hi;            /* hi 16bits of sender's FIFO addr	*/
	u32 fifo_lo;            /* lo 32bits of sender's FIFO addr	*/
	u32 sip;		/* Sender's IP Address			*/
	u32 tip;		/* IP Address of requested hw addr	*/
} __attribute__((packed));

struct ipv4_ether_hdr {
	unsigned char	h_dest[IPV4_ALEN];	/* destination address */
	unsigned short  h_proto;                /* packet type ID field */
}  __attribute__((packed));

static inline struct ipv4_ether_hdr *ipv4_ether_hdr(const struct sk_buff *skb)
{
	return (struct ipv4_ether_hdr *)skb_mac_header(skb);
}

enum ipv4_tx_type {
	IPV4_UNKNOWN = 0,
	IPV4_GASP = 1,
	IPV4_WRREQ = 2,
};

enum ipv4_broadcast_state {
	IPV4_BROADCAST_ERROR,
	IPV4_BROADCAST_RUNNING,
	IPV4_BROADCAST_STOPPED,
};

#define ipv4_get_hdr_lf(h)		(((h)->w0&0xC0000000)>>30)
#define ipv4_get_hdr_ether_type(h)	(((h)->w0&0x0000FFFF)    )
#define ipv4_get_hdr_dg_size(h)		(((h)->w0&0x0FFF0000)>>16)
#define ipv4_get_hdr_fg_off(h)		(((h)->w0&0x00000FFF)    )
#define ipv4_get_hdr_dgl(h)		(((h)->w1&0xFFFF0000)>>16)

#define ipv4_set_hdr_lf(lf)		(( lf)<<30)
#define ipv4_set_hdr_ether_type(et)	(( et)    )
#define ipv4_set_hdr_dg_size(dgs)	((dgs)<<16)
#define ipv4_set_hdr_fg_off(fgo)	((fgo)    )

#define ipv4_set_hdr_dgl(dgl)		((dgl)<<16)

struct ipv4_hdr {
	u32 w0;
	u32 w1;
};

static inline void ipv4_make_uf_hdr( struct ipv4_hdr *hdr, unsigned ether_type) {
	hdr->w0 = ipv4_set_hdr_lf(IPV4_HDR_UNFRAG)
		   |ipv4_set_hdr_ether_type(ether_type);
	fw_debug ( "Setting unfragmented header %p to %x\n", hdr, hdr->w0 );
}

static inline void ipv4_make_ff_hdr ( struct ipv4_hdr *hdr, unsigned ether_type, unsigned dg_size, unsigned dgl ) {
	hdr->w0 = ipv4_set_hdr_lf(IPV4_HDR_FIRSTFRAG)
		   |ipv4_set_hdr_dg_size(dg_size)
		   |ipv4_set_hdr_ether_type(ether_type);
	hdr->w1 = ipv4_set_hdr_dgl(dgl);
	fw_debug ( "Setting fragmented header %p to first_frag %x,%x (et %x, dgs %x, dgl %x)\n", hdr, hdr->w0, hdr->w1,
 ether_type, dg_size, dgl );
}

static inline void ipv4_make_sf_hdr ( struct ipv4_hdr *hdr, unsigned lf, unsigned dg_size, unsigned fg_off, unsigned dgl) {
	hdr->w0 = ipv4_set_hdr_lf(lf)
		 |ipv4_set_hdr_dg_size(dg_size)
		 |ipv4_set_hdr_fg_off(fg_off);
	hdr->w1 = ipv4_set_hdr_dgl(dgl);
	fw_debug ( "Setting fragmented header %p to %x,%x (lf %x, dgs %x, fo %x dgl %x)\n",
 hdr, hdr->w0, hdr->w1,
 lf, dg_size, fg_off, dgl );
}

/* End of IP1394 headers */

/* Fragment types */
#define ETH1394_HDR_LF_UF	0	/* unfragmented		*/
#define ETH1394_HDR_LF_FF	1	/* first fragment	*/
#define ETH1394_HDR_LF_LF	2	/* last fragment	*/
#define ETH1394_HDR_LF_IF	3	/* interior fragment	*/

#define IP1394_HW_ADDR_LEN	16	/* As per RFC		*/

/* This list keeps track of what parts of the datagram have been filled in */
struct ipv4_fragment_info {
        struct list_head fragment_info;
	u16 offset;
	u16 len;
};

struct ipv4_partial_datagram {
	struct list_head pdg_list;
	struct list_head fragment_info;
	struct sk_buff *skb;
	/* FIXME Why not use skb->data? */
	char *pbuf;
	u16 datagram_label;
	u16 ether_type;
	u16 datagram_size;
};

/*
 * We keep one of these for each IPv4 capable device attached to a fw_card.
 * The list of them is stored in the fw_card structure rather than in the
 * ipv4_priv because the remote IPv4 nodes may be probed before the card is,
 * so we need a place to store them before the ipv4_priv structure is
 * allocated.
 */
struct ipv4_node {
	struct list_head ipv4_nodes;
	/* guid of the remote node */
	u64 guid;
	/* FIFO address to transmit datagrams to, or INVALID_FIFO_ADDR */
	u64 fifo;

	spinlock_t pdg_lock;	/* partial datagram lock		*/
	/* List of partial datagrams received from this node */
	struct list_head pdg_list;
	/* Number of entries in pdg_list at the moment */
	unsigned pdg_size;

	/* max payload to transmit to this remote node */
	/* This already includes the IPV4_FRAG_HDR_SIZE overhead */
	u16 max_payload;
	/* outgoing datagram label */
	u16 datagram_label;
	/* Current node_id of the remote node */
	u16 nodeid;
	/* current generation of the remote node */
	u8 generation;
	/* max speed that this node can receive at */
	u8 xmt_speed;
};

struct ipv4_priv {
	spinlock_t lock;

	enum ipv4_broadcast_state broadcast_state;
	struct fw_iso_context *broadcast_rcv_context;
	struct fw_iso_buffer broadcast_rcv_buffer;
	void **broadcast_rcv_buffer_ptrs;
	unsigned broadcast_rcv_next_ptr;
	unsigned num_broadcast_rcv_ptrs;
	unsigned rcv_buffer_size;
	/*
	 * This value is the maximum unfragmented datagram size that can be
	 * sent by the hardware.  It already has the GASP overhead and the
	 * unfragmented datagram header overhead calculated into it.
	 */
	unsigned broadcast_xmt_max_payload;
	u16 broadcast_xmt_datagramlabel;

	/*
	 * The csr address that remote nodes must send datagrams to for us to
	 * receive them.
	 */
	struct fw_address_handler handler;
	u64 local_fifo;

	/* Wake up to xmt	 */
        /* struct work_struct wake;*/
	/* List of packets to be sent */
	struct list_head packet_list;
	/*
	 * List of packets that were broadcasted.  When we get an ISO interrupt
	 * one of them has been sent
	 */
	struct list_head broadcasted_list;
	/* List of packets that have been sent but not yet acked */
	struct list_head sent_list;

	struct fw_card *card;
};

/* This is our task struct. It's used for the packet complete callback.  */
struct ipv4_packet_task {
	/*
	 * ptask can actually be on priv->packet_list, priv->broadcasted_list,
	 * or priv->sent_list depending on its current state.
	 */
	struct list_head packet_list;
	struct fw_transaction transaction;
	struct ipv4_hdr hdr;
	struct sk_buff *skb;
	struct ipv4_priv *priv;
	enum ipv4_tx_type tx_type;
	int outstanding_pkts;
	unsigned max_payload;
	u64 fifo_addr;
	u16 dest_node;
	u8 generation;
	u8 speed;
};

static struct kmem_cache *ipv4_packet_task_cache;

static const char ipv4_driver_name[] = "firewire-ipv4";

static const struct ieee1394_device_id ipv4_id_table[] = {
	{
		.match_flags  = IEEE1394_MATCH_SPECIFIER_ID |
				IEEE1394_MATCH_VERSION,
		.specifier_id = IPV4_GASP_SPECIFIER_ID,
		.version      = IPV4_GASP_VERSION,
	},
	{ }
};

static u32 ipv4_unit_directory_data[] = {
	0x00040000,					/* unit directory */
	0x12000000 | IPV4_GASP_SPECIFIER_ID,	/* specifier ID */
	0x81000003,					/* text descriptor */
	0x13000000 | IPV4_GASP_VERSION,		/* version */
	0x81000005,					/* text descriptor */

	0x00030000,					/* Three quadlets */
	0x00000000,					/* Text */
	0x00000000,					/* Language 0 */
	0x49414e41,					/* I A N A */
	0x00030000,					/* Three quadlets */
	0x00000000,					/* Text */
	0x00000000,					/* Language 0 */
	0x49507634,					/* I P v 4 */
};

static struct fw_descriptor ipv4_unit_directory = {
	.length = ARRAY_SIZE(ipv4_unit_directory_data),
	.key = 0xd1000000,
	.data = ipv4_unit_directory_data
};

static int ipv4_send_packet(struct ipv4_packet_task *ptask );

/* ------------------------------------------------------------------ */
/******************************************
 * HW Header net device functions
 ******************************************/
  /* These functions have been adapted from net/ethernet/eth.c */

/* Create a fake MAC header for an arbitrary protocol layer.
 * saddr=NULL means use device source address
 * daddr=NULL means leave destination address (eg unresolved arp). */

static int ipv4_header ( struct sk_buff *skb, struct net_device *dev,
		       unsigned short type, const void *daddr,
		       const void *saddr, unsigned len) {
	struct ipv4_ether_hdr *eth;

	eth = (struct ipv4_ether_hdr *)skb_push(skb, sizeof(*eth));
	eth->h_proto = htons(type);

	if (dev->flags & (IFF_LOOPBACK | IFF_NOARP)) {
		memset(eth->h_dest, 0, dev->addr_len);
		return dev->hard_header_len;
	}

	if (daddr) {
		memcpy(eth->h_dest, daddr, dev->addr_len);
		return dev->hard_header_len;
	}

	return -dev->hard_header_len;
}

/* Rebuild the faked MAC header. This is called after an ARP
 * (or in future other address resolution) has completed on this
 * sk_buff. We now let ARP fill in the other fields.
 *
 * This routine CANNOT use cached dst->neigh!
 * Really, it is used only when dst->neigh is wrong.
 */

static int ipv4_rebuild_header(struct sk_buff *skb)
{
	struct ipv4_ether_hdr *eth;

	eth = (struct ipv4_ether_hdr *)skb->data;
	if (eth->h_proto == htons(ETH_P_IP))
		return arp_find((unsigned char *)&eth->h_dest, skb);

	fw_notify ( "%s: unable to resolve type %04x addresses\n",
		   skb->dev->name,ntohs(eth->h_proto) );
	return 0;
}

static int ipv4_header_cache(const struct neighbour *neigh, struct hh_cache *hh) {
	unsigned short type = hh->hh_type;
	struct net_device *dev;
	struct ipv4_ether_hdr *eth;

	if (type == htons(ETH_P_802_3))
		return -1;
	dev = neigh->dev;
	eth = (struct ipv4_ether_hdr *)((u8 *)hh->hh_data + 16 - sizeof(*eth));
	eth->h_proto = type;
	memcpy(eth->h_dest, neigh->ha, dev->addr_len);

	hh->hh_len = IPV4_HLEN;
	return 0;
}

/* Called by Address Resolution module to notify changes in address. */
static void ipv4_header_cache_update(struct hh_cache *hh, const struct net_device *dev, const unsigned char * haddr ) {
	memcpy((u8 *)hh->hh_data + 16 - IPV4_HLEN, haddr, dev->addr_len);
}

static int ipv4_header_parse(const struct sk_buff *skb, unsigned char *haddr) {
	memcpy(haddr, skb->dev->dev_addr, IPV4_ALEN);
	return IPV4_ALEN;
}

static const struct header_ops ipv4_header_ops = {
	.create         = ipv4_header,
	.rebuild        = ipv4_rebuild_header,
	.cache		= ipv4_header_cache,
	.cache_update	= ipv4_header_cache_update,
	.parse          = ipv4_header_parse,
};

/* ------------------------------------------------------------------ */

/* FIXME: is this correct for all cases? */
static bool ipv4_frag_overlap(struct ipv4_partial_datagram *pd, unsigned offset, unsigned len)
{
        struct ipv4_fragment_info *fi;
	unsigned end = offset + len;

	list_for_each_entry(fi, &pd->fragment_info, fragment_info) {
		if (offset < fi->offset + fi->len && end > fi->offset) {
			fw_debug ( "frag_overlap pd %p fi %p (%x@%x) with %x@%x\n", pd, fi, fi->len, fi->offset, len, offset );
			return true;
		}
	}
	fw_debug ( "frag_overlap %p does not overlap with %x@%x\n", pd, len, offset );
	return false;
}

/* Assumes that new fragment does not overlap any existing fragments */
static struct ipv4_fragment_info *ipv4_frag_new ( struct ipv4_partial_datagram *pd, unsigned offset, unsigned len ) {
	struct ipv4_fragment_info *fi, *fi2, *new;
	struct list_head *list;

	fw_debug ( "frag_new pd %p %x@%x\n", pd, len, offset );
	list = &pd->fragment_info;
	list_for_each_entry(fi, &pd->fragment_info, fragment_info) {
		if (fi->offset + fi->len == offset) {
			/* The new fragment can be tacked on to the end */
			/* Did the new fragment plug a hole? */
			fi2 = list_entry(fi->fragment_info.next, struct ipv4_fragment_info, fragment_info);
			if (fi->offset + fi->len == fi2->offset) {
				fw_debug ( "pd %p: hole filling %p (%x@%x) and %p(%x@%x): now %x@%x\n", pd, fi, fi->len, fi->offset,
				fi2, fi2->len, fi2->offset, fi->len + len + fi2->len, fi->offset );
				/* glue fragments together */
				fi->len += len + fi2->len;
				list_del(&fi2->fragment_info);
				kfree(fi2);
			} else {
				fw_debug ( "pd %p: extending %p from %x@%x to %x@%x\n", pd, fi, fi->len, fi->offset, fi->len+len, fi->offset );
				fi->len += len;
			}
			return fi;
		}
		if (offset + len == fi->offset) {
			/* The new fragment can be tacked on to the beginning */
			/* Did the new fragment plug a hole? */
			fi2 = list_entry(fi->fragment_info.prev, struct ipv4_fragment_info, fragment_info);
			if (fi2->offset + fi2->len == fi->offset) {
				/* glue fragments together */
				fw_debug ( "pd %p: extending %p and merging with %p from %x@%x to %x@%x\n",
 pd, fi2, fi, fi2->len, fi2->offset, fi2->len + fi->len + len, fi2->offset );
				fi2->len += fi->len + len;
				list_del(&fi->fragment_info);
				kfree(fi);
				return fi2;
			}
			fw_debug ( "pd %p: extending %p from %x@%x to %x@%x\n", pd, fi, fi->len, fi->offset, offset, fi->len + len );
			fi->offset = offset;
			fi->len += len;
			return fi;
		}
		if (offset > fi->offset + fi->len) {
			list = &fi->fragment_info;
			break;
		}
		if (offset + len < fi->offset) {
			list = fi->fragment_info.prev;
			break;
		}
	}

	new = kmalloc(sizeof(*new), GFP_ATOMIC);
	if (!new) {
		fw_error ( "out of memory in fragment handling!\n" );
		return NULL;
	}

	new->offset = offset;
	new->len = len;
	list_add(&new->fragment_info, list);
	fw_debug ( "pd %p: new frag %p %x@%x\n", pd, new, new->len, new->offset );
	list_for_each_entry( fi, &pd->fragment_info, fragment_info )
		fw_debug ( "fi %p %x@%x\n", fi, fi->len, fi->offset );
	return new;
}

/* ------------------------------------------------------------------ */

static struct ipv4_partial_datagram *ipv4_pd_new(struct net_device *netdev,
 struct ipv4_node *node, u16 datagram_label, unsigned dg_size, u32 *frag_buf,
 unsigned frag_off, unsigned frag_len) {
	struct ipv4_partial_datagram *new;
	struct ipv4_fragment_info *fi;

	new = kmalloc(sizeof(*new), GFP_ATOMIC);
	if (!new)
		goto fail;
	INIT_LIST_HEAD(&new->fragment_info);
	fi = ipv4_frag_new ( new, frag_off, frag_len);
	if ( fi == NULL )
		goto fail_w_new;
	new->datagram_label = datagram_label;
	new->datagram_size = dg_size;
	new->skb = dev_alloc_skb(dg_size + netdev->hard_header_len + 15);
	if ( new->skb == NULL )
		goto fail_w_fi;
	skb_reserve(new->skb, (netdev->hard_header_len + 15) & ~15);
	new->pbuf = skb_put(new->skb, dg_size);
	memcpy(new->pbuf + frag_off, frag_buf, frag_len);
	list_add_tail(&new->pdg_list, &node->pdg_list);
	fw_debug ( "pd_new: new pd %p { dgl %u, dg_size %u, skb %p, pbuf %p } on node %p\n",
 new, new->datagram_label, new->datagram_size, new->skb, new->pbuf, node );
	return new;

fail_w_fi:
	kfree(fi);
fail_w_new:
	kfree(new);
fail:
	fw_error("ipv4_pd_new: no memory\n");
	return NULL;
}

static struct ipv4_partial_datagram *ipv4_pd_find(struct ipv4_node *node, u16 datagram_label) {
	struct ipv4_partial_datagram *pd;

	list_for_each_entry(pd, &node->pdg_list, pdg_list) {
	        if ( pd->datagram_label == datagram_label ) {
			fw_debug ( "pd_find(node %p, label %u): pd %p\n", node, datagram_label, pd );
			return pd;
		}
	}
	fw_debug ( "pd_find(node %p, label %u) no entry\n", node, datagram_label );
	return NULL;
}


static void ipv4_pd_delete ( struct ipv4_partial_datagram *old ) {
	struct ipv4_fragment_info *fi, *n;

	fw_debug ( "pd_delete %p\n", old );
	list_for_each_entry_safe(fi, n, &old->fragment_info, fragment_info) {
		fw_debug ( "Freeing fi %p\n", fi );
		kfree(fi);
	}
	list_del(&old->pdg_list);
	dev_kfree_skb_any(old->skb);
	kfree(old);
}

static bool ipv4_pd_update ( struct ipv4_node *node, struct ipv4_partial_datagram *pd,
 u32 *frag_buf, unsigned frag_off, unsigned frag_len) {
	fw_debug ( "pd_update node %p, pd %p, frag_buf %p, %x@%x\n", node, pd, frag_buf, frag_len, frag_off );
	if ( ipv4_frag_new ( pd, frag_off, frag_len ) == NULL)
		return false;
	memcpy(pd->pbuf + frag_off, frag_buf, frag_len);

	/*
	 * Move list entry to beginnig of list so that oldest partial
	 * datagrams percolate to the end of the list
	 */
	list_move_tail(&pd->pdg_list, &node->pdg_list);
	fw_debug ( "New pd list:\n" );
	list_for_each_entry ( pd, &node->pdg_list, pdg_list ) {
		fw_debug ( "pd %p\n", pd );
	}
	return true;
}

static bool ipv4_pd_is_complete ( struct ipv4_partial_datagram *pd ) {
	struct ipv4_fragment_info *fi;
	bool ret;

	fi = list_entry(pd->fragment_info.next, struct ipv4_fragment_info, fragment_info);

	ret = (fi->len == pd->datagram_size);
	fw_debug ( "pd_is_complete (pd %p, dgs %x): fi %p (%x@%x) %s\n", pd, pd->datagram_size, fi, fi->len, fi->offset, ret ? "yes" : "no" );
	return ret;
}

/* ------------------------------------------------------------------ */

static int ipv4_node_new ( struct fw_card *card, struct fw_device *device ) {
	struct ipv4_node *node;

	node = kmalloc ( sizeof(*node), GFP_KERNEL );
	if ( ! node ) {
		fw_error ( "allocate new node failed\n" );
		return -ENOMEM;
	}
	node->guid = (u64)device->config_rom[3] << 32 | device->config_rom[4];
	node->fifo = INVALID_FIFO_ADDR;
	INIT_LIST_HEAD(&node->pdg_list);
	spin_lock_init(&node->pdg_lock);
	node->pdg_size = 0;
	node->generation = device->generation;
	rmb();
	node->nodeid = device->node_id;
	 /* FIXME what should it really be? */
	node->max_payload = S100_BUFFER_SIZE - IPV4_UNFRAG_HDR_SIZE;
	node->datagram_label = 0U;
	node->xmt_speed = device->max_speed;
	list_add_tail ( &node->ipv4_nodes, &card->ipv4_nodes );
	fw_debug ( "node_new: %p { guid %016llx, generation %u, nodeid %x, max_payload %x, xmt_speed %x } added\n",
 node, (unsigned long long)node->guid, node->generation, node->nodeid, node->max_payload, node->xmt_speed );
	return 0;
}

static struct ipv4_node *ipv4_node_find_by_guid(struct ipv4_priv *priv, u64 guid) {
	struct ipv4_node *node;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(node, &priv->card->ipv4_nodes, ipv4_nodes)
		if (node->guid == guid) {
			/* FIXME: lock the node first? */
			spin_unlock_irqrestore ( &priv->lock, flags );
			fw_debug ( "node_find_by_guid (%016llx) found %p\n", (unsigned long long)guid, node );
			return node;
		}

	spin_unlock_irqrestore ( &priv->lock, flags );
	fw_debug ( "node_find_by_guid (%016llx) not found\n", (unsigned long long)guid );
	return NULL;
}

static struct ipv4_node *ipv4_node_find_by_nodeid(struct ipv4_priv *priv, u16 nodeid) {
	struct ipv4_node *node;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(node, &priv->card->ipv4_nodes, ipv4_nodes)
		if (node->nodeid == nodeid) {
			/* FIXME: lock the node first? */
			spin_unlock_irqrestore ( &priv->lock, flags );
			fw_debug ( "node_find_by_nodeid (%x) found %p\n", nodeid, node );
			return node;
		}
	fw_debug ( "node_find_by_nodeid (%x) not found\n", nodeid );
	spin_unlock_irqrestore ( &priv->lock, flags );
	return NULL;
}

/* This is only complicated because we can't assume priv exists */
static void ipv4_node_delete ( struct fw_card *card, struct fw_device *device ) {
	struct net_device *netdev;
	struct ipv4_priv *priv;
	struct ipv4_node *node;
	u64 guid;
	unsigned long flags;
	struct ipv4_partial_datagram *pd, *pd_next;

	guid = (u64)device->config_rom[3] << 32 | device->config_rom[4];
	netdev = card->netdev;
	if ( netdev )
		priv = netdev_priv ( netdev );
	else
		priv = NULL;
	if ( priv )
		spin_lock_irqsave ( &priv->lock, flags );
	list_for_each_entry( node, &card->ipv4_nodes, ipv4_nodes ) {
		if ( node->guid == guid ) {
			list_del ( &node->ipv4_nodes );
			list_for_each_entry_safe( pd, pd_next, &node->pdg_list, pdg_list )
				ipv4_pd_delete ( pd );
			break;
		}
	}
	if ( priv )
		spin_unlock_irqrestore ( &priv->lock, flags );
}

/* ------------------------------------------------------------------ */


static int ipv4_finish_incoming_packet ( struct net_device *netdev,
 struct sk_buff *skb, u16 source_node_id, bool is_broadcast, u16 ether_type ) {
	struct ipv4_priv *priv;
	static u64 broadcast_hw = ~0ULL;
	int status;
	u64 guid;

	fw_debug ( "ipv4_finish_incoming_packet(%p, %p, %x, %s, %x\n",
 netdev, skb, source_node_id, is_broadcast ? "true" : "false", ether_type );
	priv = netdev_priv(netdev);
	/* Write metadata, and then pass to the receive level */
	skb->dev = netdev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;  /* don't check it */

	/*
	 * Parse the encapsulation header. This actually does the job of
	 * converting to an ethernet frame header, as well as arp
	 * conversion if needed. ARP conversion is easier in this
	 * direction, since we are using ethernet as our backend.
	 */
	/*
	 * If this is an ARP packet, convert it. First, we want to make
	 * use of some of the fields, since they tell us a little bit
	 * about the sending machine.
	 */
	if (ether_type == ETH_P_ARP) {
		struct ipv4_arp *arp1394;
		struct arphdr *arp;
		unsigned char *arp_ptr;
		u64 fifo_addr;
		u8 max_rec;
		u8 sspd;
		u16 max_payload;
		struct ipv4_node *node;
		static const u16 ipv4_speed_to_max_payload[] = {
			/* S100, S200, S400, S800, S1600, S3200 */
			    512, 1024, 2048, 4096,  4096,  4096
		};

		/* fw_debug ( "ARP packet\n" ); */
		arp1394 = (struct ipv4_arp *)skb->data;
		arp = (struct arphdr *)skb->data;
		arp_ptr = (unsigned char *)(arp + 1);
		fifo_addr = (u64)ntohs(arp1394->fifo_hi) << 32 |
 ntohl(arp1394->fifo_lo);
		max_rec = priv->card->max_receive;
		if ( arp1394->max_rec < max_rec )
			max_rec = arp1394->max_rec;
		sspd = arp1394->sspd;
		/*
		 * Sanity check. MacOSX seems to be sending us 131 in this
		 * field (atleast on my Panther G5). Not sure why.
		 */
		if (sspd > 5 ) {
			fw_notify ( "sspd %x out of range\n", sspd );
			sspd = 0;
		}

		max_payload = min(ipv4_speed_to_max_payload[sspd],
 (u16)(1 << (max_rec + 1))) - IPV4_UNFRAG_HDR_SIZE;

		guid = be64_to_cpu(get_unaligned(&arp1394->s_uniq_id));
		node = ipv4_node_find_by_guid(priv, guid);
		if (!node) {
			fw_notify ( "No node for ARP packet from %llx\n", guid );
			goto failed_proto;
		}
		if ( node->nodeid != source_node_id || node->generation != priv->card->generation ) {
			fw_notify ( "Internal error: node->nodeid (%x) != soucre_node_id (%x) or node->generation (%x) != priv->card->generation(%x)\n",
 node->nodeid, source_node_id, node->generation, priv->card->generation );
			node->nodeid = source_node_id;
			node->generation = priv->card->generation;
		}

		/* FIXME: for debugging */
		if ( sspd > SCODE_400 )
			sspd = SCODE_400;
		/* Update our speed/payload/fifo_offset table */
		/*
		 * FIXME: this does not handle cases where two high-speed endpoints must use a slower speed because of
		 * a lower speed hub between them.  We need to look at the actual topology map here.
		 */
		fw_debug ( "Setting node %p fifo %llx (was %llx), max_payload %x (was %x), speed %x (was %x)\n",
 node, fifo_addr, node->fifo, max_payload, node->max_payload, sspd, node->xmt_speed );
		node->fifo =	fifo_addr;
		node->max_payload = max_payload;
		/*
		 * Only allow speeds to go down from their initial value.
		 * Otherwise a local node that can only do S400 or slower may
		 * be told to transmit at S800 to a faster remote node.
		 */
		if ( node->xmt_speed > sspd )
			node->xmt_speed = sspd;

		/*
		 * Now that we're done with the 1394 specific stuff, we'll
		 * need to alter some of the data.  Believe it or not, all
		 * that needs to be done is sender_IP_address needs to be
		 * moved, the destination hardware address get stuffed
		 * in and the hardware address length set to 8.
		 *
		 * IMPORTANT: The code below overwrites 1394 specific data
		 * needed above so keep the munging of the data for the
		 * higher level IP stack last.
		 */

		arp->ar_hln = 8;
		arp_ptr += arp->ar_hln;		/* skip over sender unique id */
		*(u32 *)arp_ptr = arp1394->sip; /* move sender IP addr */
		arp_ptr += arp->ar_pln;		/* skip over sender IP addr */

		if (arp->ar_op == htons(ARPOP_REQUEST))
			memset(arp_ptr, 0, sizeof(u64));
		else
			memcpy(arp_ptr, netdev->dev_addr, sizeof(u64));
	}

	/* Now add the ethernet header. */
	guid = cpu_to_be64(priv->card->guid);
	if (dev_hard_header(skb, netdev, ether_type, is_broadcast ? &broadcast_hw : &guid, NULL,
 skb->len) >= 0) {
		struct ipv4_ether_hdr *eth;
		u16 *rawp;
		__be16 protocol;

		skb_reset_mac_header(skb);
		skb_pull(skb, sizeof(*eth));
		eth = ipv4_ether_hdr(skb);
		if (*eth->h_dest & 1) {
			if (memcmp(eth->h_dest, netdev->broadcast, netdev->addr_len) == 0) {
				fw_debug ( "Broadcast\n" );
				skb->pkt_type = PACKET_BROADCAST;
			}
#if 0
			else
				skb->pkt_type = PACKET_MULTICAST;
#endif
		} else {
			if (memcmp(eth->h_dest, netdev->dev_addr, netdev->addr_len)) {
				u64 a1, a2;

				memcpy ( &a1, eth->h_dest, sizeof(u64));
				memcpy ( &a2, netdev->dev_addr, sizeof(u64));
				fw_debug ( "Otherhost %llx %llx %x\n", a1, a2, netdev->addr_len );
				skb->pkt_type = PACKET_OTHERHOST;
			}
		}
		if (ntohs(eth->h_proto) >= 1536) {
			fw_debug ( " proto %x %x\n", eth->h_proto, ntohs(eth->h_proto) );
			protocol = eth->h_proto;
		} else {
			rawp = (u16 *)skb->data;
			if (*rawp == 0xFFFF) {
				fw_debug ( "proto 802_3\n" );
				protocol = htons(ETH_P_802_3);
			} else {
				fw_debug ( "proto 802_2\n" );
				protocol = htons(ETH_P_802_2);
			}
		}
		skb->protocol = protocol;
	}
	status = netif_rx(skb);
	if ( status == NET_RX_DROP) {
		netdev->stats.rx_errors++;
		netdev->stats.rx_dropped++;
	} else {
		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += skb->len;
	}
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
	return 0;

 failed_proto:
	netdev->stats.rx_errors++;
	netdev->stats.rx_dropped++;
	dev_kfree_skb_any(skb);
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
	netdev->last_rx = jiffies;
	return 0;
}

/* ------------------------------------------------------------------ */

static int ipv4_incoming_packet ( struct ipv4_priv *priv, u32 *buf, int len, u16 source_node_id, bool is_broadcast ) {
	struct sk_buff *skb;
	struct net_device *netdev;
	struct ipv4_hdr hdr;
	unsigned lf;
	unsigned long flags;
	struct ipv4_node *node;
	struct ipv4_partial_datagram *pd;
	int fg_off;
	int dg_size;
	u16 datagram_label;
	int retval;
	u16 ether_type;

	fw_debug ( "ipv4_incoming_packet(%p, %p, %d, %x, %s)\n", priv, buf, len, source_node_id, is_broadcast ? "true" : "false" );
	netdev = priv->card->netdev;

	hdr.w0 = ntohl(buf[0]);
	lf = ipv4_get_hdr_lf(&hdr);
	if ( lf == IPV4_HDR_UNFRAG ) {
		/*
		 * An unfragmented datagram has been received by the ieee1394
		 * bus. Build an skbuff around it so we can pass it to the
		 * high level network layer.
		 */
		ether_type = ipv4_get_hdr_ether_type(&hdr);
		fw_debug ( "header w0 = %x, lf = %x, ether_type = %x\n", hdr.w0, lf, ether_type );
		buf++;
		len -= IPV4_UNFRAG_HDR_SIZE;

		skb = dev_alloc_skb(len + netdev->hard_header_len + 15);
		if (unlikely(!skb)) {
			fw_error ( "Out of memory for incoming packet\n");
			netdev->stats.rx_dropped++;
			return -1;
		}
		skb_reserve(skb, (netdev->hard_header_len + 15) & ~15);
		memcpy(skb_put(skb, len), buf, len );
		return ipv4_finish_incoming_packet(netdev, skb, source_node_id, is_broadcast, ether_type );
	}
	/* A datagram fragment has been received, now the fun begins. */
	hdr.w1 = ntohl(buf[1]);
	buf +=2;
	len -= IPV4_FRAG_HDR_SIZE;
	if ( lf ==IPV4_HDR_FIRSTFRAG ) {
		ether_type = ipv4_get_hdr_ether_type(&hdr);
		fg_off = 0;
	} else {
		fg_off = ipv4_get_hdr_fg_off(&hdr);
		ether_type = 0; /* Shut up compiler! */
	}
	datagram_label = ipv4_get_hdr_dgl(&hdr);
	dg_size = ipv4_get_hdr_dg_size(&hdr); /* ??? + 1 */
	fw_debug ( "fragmented: %x.%x = lf %x, ether_type %x, fg_off %x, dgl %x, dg_size %x\n", hdr.w0, hdr.w1, lf, ether_type, fg_off, datagram_label, dg_size );
	node = ipv4_node_find_by_nodeid ( priv, source_node_id);
	spin_lock_irqsave(&node->pdg_lock, flags);
	pd = ipv4_pd_find( node, datagram_label );
	if (pd == NULL) {
		while ( node->pdg_size >= ipv4_mpd ) {
			/* remove the oldest */
			ipv4_pd_delete ( list_first_entry(&node->pdg_list, struct ipv4_partial_datagram, pdg_list) );
			node->pdg_size--;
		}
		pd = ipv4_pd_new ( netdev, node, datagram_label, dg_size,
 buf, fg_off, len);
		if ( pd == NULL) {
			retval = -ENOMEM;
			goto bad_proto;
		}
		node->pdg_size++;
	} else {
		if (ipv4_frag_overlap(pd, fg_off, len) || pd->datagram_size != dg_size) {
			/*
			 * Differing datagram sizes or overlapping fragments,
			 * Either way the remote machine is playing silly buggers
			 * with us: obliterate the old datagram and start a new one.
			 */
			ipv4_pd_delete ( pd );
			pd = ipv4_pd_new ( netdev, node, datagram_label,
 dg_size, buf, fg_off, len);
			if ( pd == NULL ) {
				retval = -ENOMEM;
				node->pdg_size--;
				goto bad_proto;
			}
		} else {
			bool worked;

			worked = ipv4_pd_update ( node, pd,
 buf, fg_off, len );
			if ( ! worked ) {
				/*
				 * Couldn't save off fragment anyway
				 * so might as well obliterate the
				 * datagram now.
				 */
				ipv4_pd_delete ( pd );
				node->pdg_size--;
				goto bad_proto;
			}
		}
	} /* new datagram or add to existing one */

	if ( lf == IPV4_HDR_FIRSTFRAG )
		pd->ether_type = ether_type;
	if ( ipv4_pd_is_complete ( pd ) ) {
		ether_type = pd->ether_type;
		node->pdg_size--;
		skb = skb_get(pd->skb);
		ipv4_pd_delete ( pd );
		spin_unlock_irqrestore(&node->pdg_lock, flags);
		return ipv4_finish_incoming_packet ( netdev, skb, source_node_id, false, ether_type );
	}
	/*
	 * Datagram is not complete, we're done for the
	 * moment.
	 */
	spin_unlock_irqrestore(&node->pdg_lock, flags);
	return 0;

 bad_proto:
	spin_unlock_irqrestore(&node->pdg_lock, flags);
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
	return 0;
}

static void ipv4_receive_packet ( struct fw_card *card, struct fw_request *r,
 int tcode, int destination, int source, int generation, int speed,
 unsigned long long offset, void *payload, size_t length, void *callback_data ) {
	struct ipv4_priv *priv;
	int status;

	fw_debug ( "ipv4_receive_packet(%p,%p,%x,%x,%x,%x,%x,%llx,%p,%lx,%p)\n",
 card, r, tcode, destination, source, generation, speed, offset, payload,
 (unsigned long)length, callback_data);
	print_hex_dump ( KERN_DEBUG, "header: ", DUMP_PREFIX_OFFSET, 32, 1, payload, length, false );
	priv = callback_data;
	if (   tcode != TCODE_WRITE_BLOCK_REQUEST
	    || destination != card->node_id
	    || generation != card->generation
	    || offset != priv->handler.offset ) {
		fw_send_response(card, r, RCODE_CONFLICT_ERROR);
		fw_debug("Conflict error card node_id=%x, card generation=%x, local offset %llx\n",
 card->node_id, card->generation, (unsigned long long)priv->handler.offset );
		return;
	}
	status = ipv4_incoming_packet ( priv, payload, length, source, false );
	if ( status != 0 ) {
		fw_error ( "Incoming packet failure\n" );
		fw_send_response ( card, r, RCODE_CONFLICT_ERROR );
		return;
	}
	fw_send_response ( card, r, RCODE_COMPLETE );
}

static void ipv4_receive_broadcast(struct fw_iso_context *context, u32 cycle,
 size_t header_length, void *header, void *data) {
	struct ipv4_priv *priv;
	struct fw_iso_packet packet;
	struct fw_card *card;
	u16 *hdr_ptr;
	u32 *buf_ptr;
	int retval;
	u32 length;
	u16 source_node_id;
	u32 specifier_id;
	u32 ver;
	unsigned long offset;
	unsigned long flags;

	fw_debug ( "ipv4_receive_broadcast ( context=%p, cycle=%x, header_length=%lx, header=%p, data=%p )\n", context, cycle, (unsigned long)header_length, header, data );
	print_hex_dump ( KERN_DEBUG, "header: ", DUMP_PREFIX_OFFSET, 32, 1, header, header_length, false );
	priv = data;
	card = priv->card;
	hdr_ptr = header;
	length = ntohs(hdr_ptr[0]);
	spin_lock_irqsave(&priv->lock,flags);
	offset = priv->rcv_buffer_size * priv->broadcast_rcv_next_ptr;
	buf_ptr = priv->broadcast_rcv_buffer_ptrs[priv->broadcast_rcv_next_ptr++];
	if ( priv->broadcast_rcv_next_ptr == priv->num_broadcast_rcv_ptrs )
		priv->broadcast_rcv_next_ptr = 0;
	spin_unlock_irqrestore(&priv->lock,flags);
	fw_debug ( "length %u at %p\n", length, buf_ptr );
	print_hex_dump ( KERN_DEBUG, "buffer: ", DUMP_PREFIX_OFFSET, 32, 1, buf_ptr, length, false );

	specifier_id =    (be32_to_cpu(buf_ptr[0]) & 0xffff) << 8
			| (be32_to_cpu(buf_ptr[1]) & 0xff000000) >> 24;
	ver = be32_to_cpu(buf_ptr[1]) & 0xFFFFFF;
	source_node_id = be32_to_cpu(buf_ptr[0]) >> 16;
	/* fw_debug ( "source %x SpecID %x ver %x\n", source_node_id, specifier_id, ver ); */
	if ( specifier_id == IPV4_GASP_SPECIFIER_ID && ver == IPV4_GASP_VERSION ) {
		buf_ptr += 2;
		length -= IPV4_GASP_OVERHEAD;
		ipv4_incoming_packet(priv, buf_ptr, length, source_node_id, true);
	} else
		fw_debug ( "Ignoring packet: not GASP\n" );
	packet.payload_length = priv->rcv_buffer_size;
	packet.interrupt = 1;
	packet.skip = 0;
	packet.tag = 3;
	packet.sy = 0;
	packet.header_length = IPV4_GASP_OVERHEAD;
	spin_lock_irqsave(&priv->lock,flags);
	retval = fw_iso_context_queue ( priv->broadcast_rcv_context, &packet,
 &priv->broadcast_rcv_buffer, offset );
	spin_unlock_irqrestore(&priv->lock,flags);
	if ( retval < 0 )
		fw_error ( "requeue failed\n" );
}

static void debug_ptask ( struct ipv4_packet_task *ptask ) {
	static const char *tx_types[] = { "Unknown", "GASP", "Write" };

	fw_debug ( "packet %p { hdr { w0 %x w1 %x }, skb %p, priv %p,"
 " tx_type %s, outstanding_pkts %d, max_payload %x, fifo %llx,"
 " speed %x, dest_node %x, generation %x }\n",
 ptask, ptask->hdr.w0, ptask->hdr.w1, ptask->skb, ptask->priv,
 ptask->tx_type > IPV4_WRREQ ? "Invalid" : tx_types[ptask->tx_type],
 ptask->outstanding_pkts,  ptask->max_payload,
 ptask->fifo_addr, ptask->speed, ptask->dest_node, ptask->generation );
	print_hex_dump ( KERN_DEBUG, "packet :", DUMP_PREFIX_OFFSET, 32, 1,
 ptask->skb->data, ptask->skb->len, false );
}

static void ipv4_transmit_packet_done ( struct ipv4_packet_task *ptask ) {
	struct ipv4_priv *priv;
	unsigned long flags;

	priv = ptask->priv;
	spin_lock_irqsave ( &priv->lock, flags );
	list_del ( &ptask->packet_list );
	spin_unlock_irqrestore ( &priv->lock, flags );
	ptask->outstanding_pkts--;
	if ( ptask->outstanding_pkts > 0 ) {
		u16 dg_size;
		u16 fg_off;
		u16 datagram_label;
		u16 lf;
		struct sk_buff *skb;

		/* Update the ptask to point to the next fragment and send it */
		lf = ipv4_get_hdr_lf(&ptask->hdr);
		switch (lf) {
		case IPV4_HDR_LASTFRAG:
		case IPV4_HDR_UNFRAG:
		default:
			fw_error ( "Outstanding packet %x lf %x, header %x,%x\n", ptask->outstanding_pkts, lf, ptask->hdr.w0, ptask->hdr.w1 );
			BUG();

		case IPV4_HDR_FIRSTFRAG:
			/* Set frag type here for future interior fragments */
			dg_size = ipv4_get_hdr_dg_size(&ptask->hdr);
			fg_off = ptask->max_payload - IPV4_FRAG_HDR_SIZE;
			datagram_label = ipv4_get_hdr_dgl(&ptask->hdr);
			break;

		case IPV4_HDR_INTFRAG:
			dg_size = ipv4_get_hdr_dg_size(&ptask->hdr);
			fg_off = ipv4_get_hdr_fg_off(&ptask->hdr) + ptask->max_payload - IPV4_FRAG_HDR_SIZE;
			datagram_label = ipv4_get_hdr_dgl(&ptask->hdr);
			break;
		}
		skb = ptask->skb;
		skb_pull ( skb, ptask->max_payload );
		if ( ptask->outstanding_pkts > 1 ) {
			ipv4_make_sf_hdr ( &ptask->hdr,
  IPV4_HDR_INTFRAG, dg_size, fg_off, datagram_label );
		} else {
			ipv4_make_sf_hdr ( &ptask->hdr,
  IPV4_HDR_LASTFRAG, dg_size, fg_off, datagram_label );
			ptask->max_payload = skb->len + IPV4_FRAG_HDR_SIZE;

		}
		ipv4_send_packet ( ptask );
	} else {
		dev_kfree_skb_any ( ptask->skb );
		kmem_cache_free( ipv4_packet_task_cache, ptask );
	}
}

static void ipv4_write_complete ( struct fw_card *card, int rcode,
 void *payload, size_t length, void *data ) {
	struct ipv4_packet_task *ptask;

	ptask = data;
	fw_debug ( "ipv4_write_complete ( %p, %x, %p, %lx, %p )\n",
 card, rcode, payload, (unsigned long)length, data );
	debug_ptask ( ptask );

	if ( rcode == RCODE_COMPLETE ) {
		ipv4_transmit_packet_done ( ptask );
	} else {
		fw_error ( "ipv4_write_complete: failed: %x\n", rcode );
		/* ??? error recovery */
	}
}

static int ipv4_send_packet ( struct ipv4_packet_task *ptask ) {
	struct ipv4_priv *priv;
	unsigned tx_len;
	struct ipv4_hdr *bufhdr;
	unsigned long flags;
	struct net_device *netdev;
#if 0 /* stefanr */
	int retval;
#endif

	fw_debug ( "ipv4_send_packet\n" );
	debug_ptask ( ptask );
	priv = ptask->priv;
	tx_len = ptask->max_payload;
	switch (ipv4_get_hdr_lf(&ptask->hdr)) {
	case IPV4_HDR_UNFRAG:
		bufhdr = (struct ipv4_hdr *)skb_push(ptask->skb, IPV4_UNFRAG_HDR_SIZE);
		bufhdr->w0 = htonl(ptask->hdr.w0);
		break;

	case IPV4_HDR_FIRSTFRAG:
	case IPV4_HDR_INTFRAG:
	case IPV4_HDR_LASTFRAG:
		bufhdr = (struct ipv4_hdr *)skb_push(ptask->skb, IPV4_FRAG_HDR_SIZE);
		bufhdr->w0 = htonl(ptask->hdr.w0);
		bufhdr->w1 = htonl(ptask->hdr.w1);
		break;

	default:
		BUG();
	}
	if ( ptask->tx_type == IPV4_GASP ) {
		u32 *packets;
		int generation;
		int nodeid;

		/* ptask->generation may not have been set yet */
		generation = priv->card->generation;
		smp_rmb();
		nodeid = priv->card->node_id;
		packets = (u32 *)skb_push(ptask->skb, sizeof(u32)*2);
		packets[0] = htonl(nodeid << 16 | (IPV4_GASP_SPECIFIER_ID>>8));
		packets[1] = htonl((IPV4_GASP_SPECIFIER_ID & 0xFF) << 24 | IPV4_GASP_VERSION);
		fw_send_request ( priv->card, &ptask->transaction, TCODE_STREAM_DATA,
 fw_stream_packet_destination_id(3, BROADCAST_CHANNEL, 0),
 generation, SCODE_100, 0ULL, ptask->skb->data, tx_len + 8, ipv4_write_complete, ptask );
		spin_lock_irqsave(&priv->lock,flags);
		list_add_tail ( &ptask->packet_list, &priv->broadcasted_list );
		spin_unlock_irqrestore(&priv->lock,flags);
#if 0 /* stefanr */
		return retval;
#else
		return 0;
#endif
	}
	fw_debug("send_request (%p, %p, WRITE_BLOCK, %x, %x, %x, %llx, %p, %d, %p, %p\n",
 priv->card, &ptask->transaction, ptask->dest_node, ptask->generation,
 ptask->speed, (unsigned long long)ptask->fifo_addr, ptask->skb->data, tx_len,
 ipv4_write_complete, ptask );
	fw_send_request ( priv->card, &ptask->transaction,
 TCODE_WRITE_BLOCK_REQUEST, ptask->dest_node, ptask->generation, ptask->speed,
 ptask->fifo_addr, ptask->skb->data, tx_len, ipv4_write_complete, ptask );
	spin_lock_irqsave(&priv->lock,flags);
	list_add_tail ( &ptask->packet_list, &priv->sent_list );
	spin_unlock_irqrestore(&priv->lock,flags);
	netdev = priv->card->netdev;
	netdev->trans_start = jiffies;
	return 0;
}

static int ipv4_broadcast_start ( struct ipv4_priv *priv ) {
	struct fw_iso_context *context;
	int retval;
	unsigned num_packets;
	unsigned max_receive;
	struct fw_iso_packet packet;
	unsigned long offset;
	unsigned u;
	/* unsigned transmit_speed; */

#if 0 /* stefanr */
	if ( priv->card->broadcast_channel != (BROADCAST_CHANNEL_VALID|BROADCAST_CHANNEL_INITIAL)) {
		fw_notify ( "Invalid broadcast channel %x\n", priv->card->broadcast_channel );
		/* FIXME: try again later? */
		/* return -EINVAL; */
	}
#endif
	if ( priv->local_fifo == INVALID_FIFO_ADDR ) {
		struct fw_address_region region;

		priv->handler.length = FIFO_SIZE;
		priv->handler.address_callback = ipv4_receive_packet;
		priv->handler.callback_data = priv;
		/* FIXME: this is OHCI, but what about others? */
		region.start = 0xffff00000000ULL;
		region.end =   0xfffffffffffcULL;

		retval = fw_core_add_address_handler ( &priv->handler, &region );
		if ( retval < 0 )
			goto failed_initial;
		priv->local_fifo = priv->handler.offset;
	}

	/*
	 * FIXME: rawiso limits us to PAGE_SIZE.  This only matters if we ever have
	 * a machine with PAGE_SIZE < 4096
	 */
	max_receive = 1U << (priv->card->max_receive + 1);
	num_packets = ( ipv4_iso_page_count * PAGE_SIZE ) / max_receive;
	if ( ! priv->broadcast_rcv_context ) {
		void **ptrptr;

		context = fw_iso_context_create ( priv->card,
 FW_ISO_CONTEXT_RECEIVE, BROADCAST_CHANNEL,
 priv->card->link_speed, 8, ipv4_receive_broadcast, priv );
		if (IS_ERR(context)) {
			retval = PTR_ERR(context);
			goto failed_context_create;
		}
		retval = fw_iso_buffer_init ( &priv->broadcast_rcv_buffer,
 priv->card, ipv4_iso_page_count, DMA_FROM_DEVICE );
		if ( retval < 0 )
			goto failed_buffer_init;
		ptrptr = kmalloc ( sizeof(void*)*num_packets, GFP_KERNEL );
		if ( ! ptrptr ) {
			retval = -ENOMEM;
			goto failed_ptrs_alloc;
		}
		priv->broadcast_rcv_buffer_ptrs = ptrptr;
		for ( u = 0; u < ipv4_iso_page_count; u++ ) {
			void *ptr;
			unsigned v;

			ptr = kmap ( priv->broadcast_rcv_buffer.pages[u] );
			for ( v = 0; v < num_packets / ipv4_iso_page_count; v++ )
				*ptrptr++ = (void *)((char *)ptr + v * max_receive);
		}
		priv->broadcast_rcv_context = context;
	} else
		context = priv->broadcast_rcv_context;

	packet.payload_length = max_receive;
	packet.interrupt = 1;
	packet.skip = 0;
	packet.tag = 3;
	packet.sy = 0;
	packet.header_length = IPV4_GASP_OVERHEAD;
	offset = 0;
	for ( u = 0; u < num_packets; u++ ) {
		retval = fw_iso_context_queue ( context, &packet,
 &priv->broadcast_rcv_buffer, offset );
		if ( retval < 0 )
			goto failed_rcv_queue;
		offset += max_receive;
	}
	priv->num_broadcast_rcv_ptrs = num_packets;
	priv->rcv_buffer_size = max_receive;
	priv->broadcast_rcv_next_ptr = 0U;
	retval = fw_iso_context_start ( context, -1, 0, FW_ISO_CONTEXT_MATCH_ALL_TAGS ); /* ??? sync */
	if ( retval < 0 )
		goto failed_rcv_queue;
	/* FIXME: adjust this when we know the max receive speeds of all other IP nodes on the bus. */
	/* since we only xmt at S100 ??? */
	priv->broadcast_xmt_max_payload = S100_BUFFER_SIZE - IPV4_GASP_OVERHEAD - IPV4_UNFRAG_HDR_SIZE;
	priv->broadcast_state = IPV4_BROADCAST_RUNNING;
	return 0;

 failed_rcv_queue:
	kfree ( priv->broadcast_rcv_buffer_ptrs );
	priv->broadcast_rcv_buffer_ptrs = NULL;
 failed_ptrs_alloc:
	fw_iso_buffer_destroy ( &priv->broadcast_rcv_buffer, priv->card );
 failed_buffer_init:
	fw_iso_context_destroy ( context );
	priv->broadcast_rcv_context = NULL;
 failed_context_create:
	fw_core_remove_address_handler ( &priv->handler );
 failed_initial:
	priv->local_fifo = INVALID_FIFO_ADDR;
	return retval;
}

/* This is called after an "ifup" */
static int ipv4_open(struct net_device *dev) {
	struct ipv4_priv *priv;
	int ret;

	priv = netdev_priv(dev);
	if (priv->broadcast_state == IPV4_BROADCAST_ERROR) {
		ret = ipv4_broadcast_start ( priv );
		if (ret)
			return ret;
	}
	netif_start_queue(dev);
	return 0;
}

/* This is called after an "ifdown" */
static int ipv4_stop(struct net_device *netdev)
{
	/* flush priv->wake */
	/* flush_scheduled_work(); */

	netif_stop_queue(netdev);
	return 0;
}

/* Transmit a packet (called by kernel) */
static int ipv4_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct ipv4_ether_hdr hdr_buf;
	struct ipv4_priv *priv = netdev_priv(netdev);
	__be16 proto;
	u16 dest_node;
	enum ipv4_tx_type tx_type;
	unsigned max_payload;
	u16 dg_size;
	u16 *datagram_label_ptr;
	struct ipv4_packet_task *ptask;
	struct ipv4_node *node = NULL;

	ptask = kmem_cache_alloc(ipv4_packet_task_cache, GFP_ATOMIC);
	if (ptask == NULL)
		goto fail;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto fail;

	/*
	 * Get rid of the fake ipv4 header, but first make a copy.
	 * We might need to rebuild the header on tx failure.
	 */
	memcpy(&hdr_buf, skb->data, sizeof(hdr_buf));
	skb_pull(skb, sizeof(hdr_buf));

	proto = hdr_buf.h_proto;
	dg_size = skb->len;

	/*
	 * Set the transmission type for the packet.  ARP packets and IP
	 * broadcast packets are sent via GASP.
	 */
	if (   memcmp(hdr_buf.h_dest, netdev->broadcast, IPV4_ALEN) == 0
	    || proto == htons(ETH_P_ARP)
	    || (   proto == htons(ETH_P_IP)
		&& IN_MULTICAST(ntohl(ip_hdr(skb)->daddr)) ) ) {
		/* fw_debug ( "transmitting arp or multicast packet\n" );*/
		tx_type = IPV4_GASP;
		dest_node = ALL_NODES;
		max_payload = priv->broadcast_xmt_max_payload;
		/* BUG_ON(max_payload < S100_BUFFER_SIZE - IPV4_GASP_OVERHEAD); */
		datagram_label_ptr = &priv->broadcast_xmt_datagramlabel;
		ptask->fifo_addr = INVALID_FIFO_ADDR;
		ptask->generation = 0U;
		ptask->dest_node = 0U;
		ptask->speed = 0;
	} else {
		__be64 guid = get_unaligned((u64 *)hdr_buf.h_dest);
		u8 generation;

		node = ipv4_node_find_by_guid(priv, be64_to_cpu(guid));
		if (!node) {
			fw_debug ( "Normal packet but no node\n" );
			goto fail;
		}

		if (node->fifo == INVALID_FIFO_ADDR) {
			fw_debug ( "Normal packet but no fifo addr\n" );
			goto fail;
		}

		/* fw_debug ( "Transmitting normal packet to %x at %llxx\n", node->nodeid, node->fifo ); */
		generation = node->generation;
		dest_node = node->nodeid;
		max_payload = node->max_payload;
		/* BUG_ON(max_payload < S100_BUFFER_SIZE - IPV4_FRAG_HDR_SIZE); */

		datagram_label_ptr = &node->datagram_label;
		tx_type = IPV4_WRREQ;
		ptask->fifo_addr = node->fifo;
		ptask->generation = generation;
		ptask->dest_node = dest_node;
		ptask->speed = node->xmt_speed;
	}

	/* If this is an ARP packet, convert it */
	if (proto == htons(ETH_P_ARP)) {
		/* Convert a standard ARP packet to 1394 ARP. The first 8 bytes (the entire
		 * arphdr) is the same format as the ip1394 header, so they overlap.  The rest
		 * needs to be munged a bit.  The remainder of the arphdr is formatted based
		 * on hwaddr len and ipaddr len.  We know what they'll be, so it's easy to
		 * judge.
		 *
		 * Now that the EUI is used for the hardware address all we need to do to make
		 * this work for 1394 is to insert 2 quadlets that contain max_rec size,
		 * speed, and unicast FIFO address information between the sender_unique_id
		 * and the IP addresses.
		 */
		struct arphdr *arp = (struct arphdr *)skb->data;
		unsigned char *arp_ptr = (unsigned char *)(arp + 1);
		struct ipv4_arp *arp1394 = (struct ipv4_arp *)skb->data;
		u32 ipaddr;

		ipaddr = *(u32*)(arp_ptr + IPV4_ALEN);
		arp1394->hw_addr_len    = 16;
		arp1394->max_rec        = priv->card->max_receive;
		arp1394->sspd		= priv->card->link_speed;
		arp1394->fifo_hi	= htons(priv->local_fifo >> 32);
		arp1394->fifo_lo        = htonl(priv->local_fifo & 0xFFFFFFFF);
		arp1394->sip		= ipaddr;
	}
	if ( ipv4_max_xmt && max_payload > ipv4_max_xmt )
		max_payload = ipv4_max_xmt;

	ptask->hdr.w0 = 0;
	ptask->hdr.w1 = 0;
	ptask->skb = skb;
	ptask->priv = priv;
        ptask->tx_type = tx_type;
	/* Does it all fit in one packet? */
	if ( dg_size <= max_payload ) {
		ipv4_make_uf_hdr(&ptask->hdr, be16_to_cpu(proto));
		ptask->outstanding_pkts = 1;
		max_payload = dg_size + IPV4_UNFRAG_HDR_SIZE;
	} else {
		u16 datagram_label;

		max_payload -= IPV4_FRAG_OVERHEAD;
		datagram_label = (*datagram_label_ptr)++;
		ipv4_make_ff_hdr(&ptask->hdr, be16_to_cpu(proto), dg_size, datagram_label );
		ptask->outstanding_pkts = DIV_ROUND_UP(dg_size, max_payload);
		max_payload += IPV4_FRAG_HDR_SIZE;
	}
	ptask->max_payload = max_payload;
	ipv4_send_packet ( ptask );
	return NETDEV_TX_OK;

 fail:
	if (ptask)
		kmem_cache_free(ipv4_packet_task_cache, ptask);

	if (skb != NULL)
		dev_kfree_skb(skb);

	netdev->stats.tx_dropped++;
	netdev->stats.tx_errors++;

	/*
	 * FIXME: According to a patch from 2003-02-26, "returning non-zero
	 * causes serious problems" here, allegedly.  Before that patch,
	 * -ERRNO was returned which is not appropriate under Linux 2.6.
	 * Perhaps more needs to be done?  Stop the queue in serious
	 * conditions and restart it elsewhere?
	 */
	return NETDEV_TX_OK;
}

/*
 * FIXME: What to do if we timeout? I think a host reset is probably in order,
 * so that's what we do. Should we increment the stat counters too?
 */
static void ipv4_tx_timeout(struct net_device *dev) {
	struct ipv4_priv *priv;

	priv = netdev_priv(dev);
	fw_error ( "%s: Timeout, resetting host\n", dev->name );
#if 0 /* stefanr */
	fw_core_initiate_bus_reset ( priv->card, 1 );
#endif
}

static int ipv4_change_mtu ( struct net_device *dev, int new_mtu ) {
#if 0
	int max_mtu;
	struct ipv4_priv *priv;
#endif

	if (new_mtu < 68)
		return -EINVAL;

#if 0
	priv = netdev_priv(dev);
	/* This is not actually true because we can fragment packets at the firewire layer */
	max_mtu = (1 << (priv->card->max_receive + 1))
		                - sizeof(struct ipv4_hdr) - IPV4_GASP_OVERHEAD;
	if (new_mtu > max_mtu) {
		fw_notify ( "%s: Local node constrains MTU to %d\n", dev->name, max_mtu);
		return -ERANGE;
	}
#endif
	dev->mtu = new_mtu;
	return 0;
}

static void ipv4_get_drvinfo(struct net_device *dev,
struct ethtool_drvinfo *info) {
	strcpy(info->driver, ipv4_driver_name);
	strcpy(info->bus_info, "ieee1394"); /* FIXME provide more detail? */
}

static struct ethtool_ops ipv4_ethtool_ops = {
	.get_drvinfo = ipv4_get_drvinfo,
};

static const struct net_device_ops ipv4_netdev_ops = {
	.ndo_open       = ipv4_open,
	.ndo_stop	= ipv4_stop,
	.ndo_start_xmit = ipv4_tx,
	.ndo_tx_timeout = ipv4_tx_timeout,
	.ndo_change_mtu = ipv4_change_mtu,
};

static void ipv4_init_dev ( struct net_device *dev ) {
	dev->header_ops		= &ipv4_header_ops;
	dev->netdev_ops         = &ipv4_netdev_ops;
	SET_ETHTOOL_OPS(dev, &ipv4_ethtool_ops);

	dev->watchdog_timeo	= IPV4_TIMEOUT;
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST;
	dev->features		= NETIF_F_HIGHDMA;
	dev->addr_len		= IPV4_ALEN;
	dev->hard_header_len	= IPV4_HLEN;
	dev->type		= ARPHRD_IEEE1394;

	/* FIXME: This value was copied from ether_setup(). Is it too much? */
	dev->tx_queue_len	= 1000;
}

static int ipv4_probe ( struct device *dev ) {
	struct fw_unit * unit;
	struct fw_device *device;
	struct fw_card *card;
	struct net_device *netdev;
	struct ipv4_priv *priv;
	unsigned max_mtu;
	__be64 guid;

	fw_debug("ipv4 Probing\n" );
	unit = fw_unit ( dev );
	device = fw_device ( unit->device.parent );
	card = device->card;

	if ( ! device->is_local ) {
		int added;

		fw_debug ( "Non-local, adding remote node entry\n" );
		added = ipv4_node_new ( card, device );
		return added;
	}
	fw_debug("ipv4 Local: adding netdev\n" );
	netdev = alloc_netdev ( sizeof(*priv), "fw-ipv4-%d", ipv4_init_dev );
	if ( netdev == NULL) {
		fw_error( "Out of memory\n");
		goto out;
	}

	SET_NETDEV_DEV(netdev, card->device);
	priv = netdev_priv(netdev);

	spin_lock_init(&priv->lock);
	priv->broadcast_state = IPV4_BROADCAST_ERROR;
	priv->broadcast_rcv_context = NULL;
	priv->broadcast_xmt_max_payload = 0;
	priv->broadcast_xmt_datagramlabel = 0;

	priv->local_fifo = INVALID_FIFO_ADDR;

	/* INIT_WORK(&priv->wake, ipv4_handle_queue);*/
	INIT_LIST_HEAD(&priv->packet_list);
	INIT_LIST_HEAD(&priv->broadcasted_list);
	INIT_LIST_HEAD(&priv->sent_list );

	priv->card = card;

	/*
	 * Use the RFC 2734 default 1500 octets or the maximum payload
	 * as initial MTU
	 */
	max_mtu = (1 << (card->max_receive + 1))
		  - sizeof(struct ipv4_hdr) - IPV4_GASP_OVERHEAD;
	netdev->mtu = min(1500U, max_mtu);

	/* Set our hardware address while we're at it */
	guid = cpu_to_be64(card->guid);
	memcpy(netdev->dev_addr, &guid, sizeof(u64));
	memset(netdev->broadcast, 0xff, sizeof(u64));
	if ( register_netdev ( netdev ) ) {
		fw_error ( "Cannot register the driver\n");
		goto out;
	}

	fw_notify ( "%s: IPv4 over Firewire on device %016llx\n",
 netdev->name, card->guid );
	card->netdev = netdev;

	return 0 /* ipv4_new_node ( ud ) */;
 out:
	if ( netdev )
		free_netdev ( netdev );
	return -ENOENT;
}


static int ipv4_remove ( struct device *dev ) {
	struct fw_unit * unit;
	struct fw_device *device;
	struct fw_card *card;
	struct net_device *netdev;
	struct ipv4_priv *priv;
	struct ipv4_node *node;
	struct ipv4_partial_datagram *pd, *pd_next;
	struct ipv4_packet_task *ptask, *pt_next;

	fw_debug("ipv4 Removing\n" );
	unit = fw_unit ( dev );
	device = fw_device ( unit->device.parent );
	card = device->card;

	if ( ! device->is_local ) {
		fw_debug ( "Node %x is non-local, removing remote node entry\n", device->node_id );
		ipv4_node_delete ( card, device );
		return 0;
	}
	netdev = card->netdev;
	if ( netdev ) {
		fw_debug ( "Node %x is local: deleting netdev\n", device->node_id );
		priv = netdev_priv ( netdev );
		unregister_netdev ( netdev );
		fw_debug ( "unregistered\n" );
		if ( priv->local_fifo != INVALID_FIFO_ADDR )
			fw_core_remove_address_handler ( &priv->handler );
		fw_debug ( "address handler gone\n" );
		if ( priv->broadcast_rcv_context ) {
			fw_iso_context_stop ( priv->broadcast_rcv_context );
			fw_iso_buffer_destroy ( &priv->broadcast_rcv_buffer, priv->card );
			fw_iso_context_destroy ( priv->broadcast_rcv_context );
			fw_debug ( "rcv stopped\n" );
		}
		list_for_each_entry_safe( ptask, pt_next, &priv->packet_list, packet_list ) {
			dev_kfree_skb_any ( ptask->skb );
			kmem_cache_free( ipv4_packet_task_cache, ptask );
		}
		list_for_each_entry_safe( ptask, pt_next, &priv->broadcasted_list, packet_list ) {
			dev_kfree_skb_any ( ptask->skb );
			kmem_cache_free( ipv4_packet_task_cache, ptask );
		}
		list_for_each_entry_safe( ptask, pt_next, &priv->sent_list, packet_list ) {
			dev_kfree_skb_any ( ptask->skb );
			kmem_cache_free( ipv4_packet_task_cache, ptask );
		}
		fw_debug ( "lists emptied\n" );
		list_for_each_entry( node, &card->ipv4_nodes, ipv4_nodes ) {
			if ( node->pdg_size ) {
				list_for_each_entry_safe( pd, pd_next, &node->pdg_list, pdg_list )
					ipv4_pd_delete ( pd );
				node->pdg_size = 0;
			}
			node->fifo = INVALID_FIFO_ADDR;
		}
		fw_debug ( "nodes cleaned up\n" );
		free_netdev ( netdev );
		card->netdev = NULL;
		fw_debug ( "done\n" );
	}
	return 0;
}

static void ipv4_update ( struct fw_unit *unit ) {
	struct fw_device *device;
	struct fw_card *card;

	fw_debug ( "ipv4_update unit %p\n", unit );
	device = fw_device ( unit->device.parent );
	card = device->card;
	if ( ! device->is_local ) {
		struct ipv4_node *node;
		u64 guid;
		struct net_device *netdev;
		struct ipv4_priv *priv;

		netdev = card->netdev;
		if ( netdev ) {
			priv = netdev_priv ( netdev );
			guid = (u64)device->config_rom[3] << 32 | device->config_rom[4];
			node = ipv4_node_find_by_guid ( priv, guid );
			if ( ! node ) {
				fw_error ( "ipv4_update: no node for device %llx\n", guid );
				return;
			}
			fw_debug ( "Non-local, updating remote node entry for guid %llx old generation %x, old nodeid %x\n", guid, node->generation, node->nodeid );
			node->generation = device->generation;
			rmb();
			node->nodeid = device->node_id;
			fw_debug ( "New generation %x, new nodeid %x\n", node->generation, node->nodeid );
		} else
			fw_error ( "nonlocal, but no netdev?  How can that be?\n" );
	} else {
		/* FIXME: What do we need to do on bus reset? */
		fw_debug ( "Local, doing nothing\n" );
	}
}

static struct fw_driver ipv4_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = ipv4_driver_name,
		.bus = &fw_bus_type,
		.probe = ipv4_probe,
		.remove = ipv4_remove,
	},
	.update = ipv4_update,
	.id_table = ipv4_id_table,
};

static int __init ipv4_init ( void ) {
	int added;

	added = fw_core_add_descriptor ( &ipv4_unit_directory );
	if ( added < 0 )
		fw_error ( "Failed to add descriptor" );
	ipv4_packet_task_cache = kmem_cache_create("packet_task",
 sizeof(struct ipv4_packet_task), 0, 0, NULL);
	fw_debug("Adding ipv4 module\n" );
	return driver_register ( &ipv4_driver.driver );
}

static void __exit ipv4_cleanup ( void ) {
	fw_core_remove_descriptor ( &ipv4_unit_directory );
	fw_debug("Removing ipv4 module\n" );
	driver_unregister ( &ipv4_driver.driver );
}

module_init(ipv4_init);
module_exit(ipv4_cleanup);
