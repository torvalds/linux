// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/macsec.c - MACsec device
 *
 * Copyright (c) 2015 Sabrina Dubroca <sd@queasysnail.net>
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/module.h>
#include <crypto/aead.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/refcount.h>
#include <net/genetlink.h>
#include <net/sock.h>
#include <net/gro_cells.h>
#include <net/macsec.h>
#include <linux/phy.h>
#include <linux/byteorder/generic.h>
#include <linux/if_arp.h>

#include <uapi/linux/if_macsec.h>

#define MACSEC_SCI_LEN 8

/* SecTAG length = macsec_eth_header without the optional SCI */
#define MACSEC_TAG_LEN 6

struct macsec_eth_header {
	struct ethhdr eth;
	/* SecTAG */
	u8  tci_an;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8  short_length:6,
		  unused:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8        unused:2,
	    short_length:6;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__be32 packet_number;
	u8 secure_channel_id[8]; /* optional */
} __packed;

#define MACSEC_TCI_VERSION 0x80
#define MACSEC_TCI_ES      0x40 /* end station */
#define MACSEC_TCI_SC      0x20 /* SCI present */
#define MACSEC_TCI_SCB     0x10 /* epon */
#define MACSEC_TCI_E       0x08 /* encryption */
#define MACSEC_TCI_C       0x04 /* changed text */
#define MACSEC_AN_MASK     0x03 /* association number */
#define MACSEC_TCI_CONFID  (MACSEC_TCI_E | MACSEC_TCI_C)

/* minimum secure data length deemed "not short", see IEEE 802.1AE-2006 9.7 */
#define MIN_NON_SHORT_LEN 48

#define GCM_AES_IV_LEN 12
#define DEFAULT_ICV_LEN 16

#define for_each_rxsc(secy, sc)				\
	for (sc = rcu_dereference_bh(secy->rx_sc);	\
	     sc;					\
	     sc = rcu_dereference_bh(sc->next))
#define for_each_rxsc_rtnl(secy, sc)			\
	for (sc = rtnl_dereference(secy->rx_sc);	\
	     sc;					\
	     sc = rtnl_dereference(sc->next))

#define pn_same_half(pn1, pn2) (!(((pn1) >> 31) ^ ((pn2) >> 31)))

struct gcm_iv_xpn {
	union {
		u8 short_secure_channel_id[4];
		ssci_t ssci;
	};
	__be64 pn;
} __packed;

struct gcm_iv {
	union {
		u8 secure_channel_id[8];
		sci_t sci;
	};
	__be32 pn;
};

#define MACSEC_VALIDATE_DEFAULT MACSEC_VALIDATE_STRICT

struct pcpu_secy_stats {
	struct macsec_dev_stats stats;
	struct u64_stats_sync syncp;
};

/**
 * struct macsec_dev - private data
 * @secy: SecY config
 * @real_dev: pointer to underlying netdevice
 * @stats: MACsec device stats
 * @secys: linked list of SecY's on the underlying device
 * @gro_cells: pointer to the Generic Receive Offload cell
 * @offload: status of offloading on the MACsec device
 */
struct macsec_dev {
	struct macsec_secy secy;
	struct net_device *real_dev;
	struct pcpu_secy_stats __percpu *stats;
	struct list_head secys;
	struct gro_cells gro_cells;
	enum macsec_offload offload;
};

/**
 * struct macsec_rxh_data - rx_handler private argument
 * @secys: linked list of SecY's on this underlying device
 */
struct macsec_rxh_data {
	struct list_head secys;
};

static struct macsec_dev *macsec_priv(const struct net_device *dev)
{
	return (struct macsec_dev *)netdev_priv(dev);
}

static struct macsec_rxh_data *macsec_data_rcu(const struct net_device *dev)
{
	return rcu_dereference_bh(dev->rx_handler_data);
}

static struct macsec_rxh_data *macsec_data_rtnl(const struct net_device *dev)
{
	return rtnl_dereference(dev->rx_handler_data);
}

struct macsec_cb {
	struct aead_request *req;
	union {
		struct macsec_tx_sa *tx_sa;
		struct macsec_rx_sa *rx_sa;
	};
	u8 assoc_num;
	bool valid;
	bool has_sci;
};

static struct macsec_rx_sa *macsec_rxsa_get(struct macsec_rx_sa __rcu *ptr)
{
	struct macsec_rx_sa *sa = rcu_dereference_bh(ptr);

	if (!sa || !sa->active)
		return NULL;

	if (!refcount_inc_not_zero(&sa->refcnt))
		return NULL;

	return sa;
}

static void free_rx_sc_rcu(struct rcu_head *head)
{
	struct macsec_rx_sc *rx_sc = container_of(head, struct macsec_rx_sc, rcu_head);

	free_percpu(rx_sc->stats);
	kfree(rx_sc);
}

static struct macsec_rx_sc *macsec_rxsc_get(struct macsec_rx_sc *sc)
{
	return refcount_inc_not_zero(&sc->refcnt) ? sc : NULL;
}

static void macsec_rxsc_put(struct macsec_rx_sc *sc)
{
	if (refcount_dec_and_test(&sc->refcnt))
		call_rcu(&sc->rcu_head, free_rx_sc_rcu);
}

static void free_rxsa(struct rcu_head *head)
{
	struct macsec_rx_sa *sa = container_of(head, struct macsec_rx_sa, rcu);

	crypto_free_aead(sa->key.tfm);
	free_percpu(sa->stats);
	kfree(sa);
}

static void macsec_rxsa_put(struct macsec_rx_sa *sa)
{
	if (refcount_dec_and_test(&sa->refcnt))
		call_rcu(&sa->rcu, free_rxsa);
}

static struct macsec_tx_sa *macsec_txsa_get(struct macsec_tx_sa __rcu *ptr)
{
	struct macsec_tx_sa *sa = rcu_dereference_bh(ptr);

	if (!sa || !sa->active)
		return NULL;

	if (!refcount_inc_not_zero(&sa->refcnt))
		return NULL;

	return sa;
}

static void free_txsa(struct rcu_head *head)
{
	struct macsec_tx_sa *sa = container_of(head, struct macsec_tx_sa, rcu);

	crypto_free_aead(sa->key.tfm);
	free_percpu(sa->stats);
	kfree(sa);
}

static void macsec_txsa_put(struct macsec_tx_sa *sa)
{
	if (refcount_dec_and_test(&sa->refcnt))
		call_rcu(&sa->rcu, free_txsa);
}

static struct macsec_cb *macsec_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct macsec_cb) > sizeof(skb->cb));
	return (struct macsec_cb *)skb->cb;
}

#define MACSEC_PORT_ES (htons(0x0001))
#define MACSEC_PORT_SCB (0x0000)
#define MACSEC_UNDEF_SCI ((__force sci_t)0xffffffffffffffffULL)
#define MACSEC_UNDEF_SSCI ((__force ssci_t)0xffffffff)

#define MACSEC_GCM_AES_128_SAK_LEN 16
#define MACSEC_GCM_AES_256_SAK_LEN 32

#define DEFAULT_SAK_LEN MACSEC_GCM_AES_128_SAK_LEN
#define DEFAULT_XPN false
#define DEFAULT_SEND_SCI true
#define DEFAULT_ENCRYPT false
#define DEFAULT_ENCODING_SA 0
#define MACSEC_XPN_MAX_REPLAY_WINDOW (((1 << 30) - 1))

static bool send_sci(const struct macsec_secy *secy)
{
	const struct macsec_tx_sc *tx_sc = &secy->tx_sc;

	return tx_sc->send_sci ||
		(secy->n_rx_sc > 1 && !tx_sc->end_station && !tx_sc->scb);
}

static sci_t make_sci(u8 *addr, __be16 port)
{
	sci_t sci;

	memcpy(&sci, addr, ETH_ALEN);
	memcpy(((char *)&sci) + ETH_ALEN, &port, sizeof(port));

	return sci;
}

static sci_t macsec_frame_sci(struct macsec_eth_header *hdr, bool sci_present)
{
	sci_t sci;

	if (sci_present)
		memcpy(&sci, hdr->secure_channel_id,
		       sizeof(hdr->secure_channel_id));
	else
		sci = make_sci(hdr->eth.h_source, MACSEC_PORT_ES);

	return sci;
}

static unsigned int macsec_sectag_len(bool sci_present)
{
	return MACSEC_TAG_LEN + (sci_present ? MACSEC_SCI_LEN : 0);
}

static unsigned int macsec_hdr_len(bool sci_present)
{
	return macsec_sectag_len(sci_present) + ETH_HLEN;
}

static unsigned int macsec_extra_len(bool sci_present)
{
	return macsec_sectag_len(sci_present) + sizeof(__be16);
}

/* Fill SecTAG according to IEEE 802.1AE-2006 10.5.3 */
static void macsec_fill_sectag(struct macsec_eth_header *h,
			       const struct macsec_secy *secy, u32 pn,
			       bool sci_present)
{
	const struct macsec_tx_sc *tx_sc = &secy->tx_sc;

	memset(&h->tci_an, 0, macsec_sectag_len(sci_present));
	h->eth.h_proto = htons(ETH_P_MACSEC);

	if (sci_present) {
		h->tci_an |= MACSEC_TCI_SC;
		memcpy(&h->secure_channel_id, &secy->sci,
		       sizeof(h->secure_channel_id));
	} else {
		if (tx_sc->end_station)
			h->tci_an |= MACSEC_TCI_ES;
		if (tx_sc->scb)
			h->tci_an |= MACSEC_TCI_SCB;
	}

	h->packet_number = htonl(pn);

	/* with GCM, C/E clear for !encrypt, both set for encrypt */
	if (tx_sc->encrypt)
		h->tci_an |= MACSEC_TCI_CONFID;
	else if (secy->icv_len != DEFAULT_ICV_LEN)
		h->tci_an |= MACSEC_TCI_C;

	h->tci_an |= tx_sc->encoding_sa;
}

static void macsec_set_shortlen(struct macsec_eth_header *h, size_t data_len)
{
	if (data_len < MIN_NON_SHORT_LEN)
		h->short_length = data_len;
}

/* Checks if a MACsec interface is being offloaded to an hardware engine */
static bool macsec_is_offloaded(struct macsec_dev *macsec)
{
	if (macsec->offload == MACSEC_OFFLOAD_MAC ||
	    macsec->offload == MACSEC_OFFLOAD_PHY)
		return true;

	return false;
}

/* Checks if underlying layers implement MACsec offloading functions. */
static bool macsec_check_offload(enum macsec_offload offload,
				 struct macsec_dev *macsec)
{
	if (!macsec || !macsec->real_dev)
		return false;

	if (offload == MACSEC_OFFLOAD_PHY)
		return macsec->real_dev->phydev &&
		       macsec->real_dev->phydev->macsec_ops;
	else if (offload == MACSEC_OFFLOAD_MAC)
		return macsec->real_dev->features & NETIF_F_HW_MACSEC &&
		       macsec->real_dev->macsec_ops;

	return false;
}

static const struct macsec_ops *__macsec_get_ops(enum macsec_offload offload,
						 struct macsec_dev *macsec,
						 struct macsec_context *ctx)
{
	if (ctx) {
		memset(ctx, 0, sizeof(*ctx));
		ctx->offload = offload;

		if (offload == MACSEC_OFFLOAD_PHY)
			ctx->phydev = macsec->real_dev->phydev;
		else if (offload == MACSEC_OFFLOAD_MAC)
			ctx->netdev = macsec->real_dev;
	}

	if (offload == MACSEC_OFFLOAD_PHY)
		return macsec->real_dev->phydev->macsec_ops;
	else
		return macsec->real_dev->macsec_ops;
}

/* Returns a pointer to the MACsec ops struct if any and updates the MACsec
 * context device reference if provided.
 */
static const struct macsec_ops *macsec_get_ops(struct macsec_dev *macsec,
					       struct macsec_context *ctx)
{
	if (!macsec_check_offload(macsec->offload, macsec))
		return NULL;

	return __macsec_get_ops(macsec->offload, macsec, ctx);
}

/* validate MACsec packet according to IEEE 802.1AE-2018 9.12 */
static bool macsec_validate_skb(struct sk_buff *skb, u16 icv_len, bool xpn)
{
	struct macsec_eth_header *h = (struct macsec_eth_header *)skb->data;
	int len = skb->len - 2 * ETH_ALEN;
	int extra_len = macsec_extra_len(!!(h->tci_an & MACSEC_TCI_SC)) + icv_len;

	/* a) It comprises at least 17 octets */
	if (skb->len <= 16)
		return false;

	/* b) MACsec EtherType: already checked */

	/* c) V bit is clear */
	if (h->tci_an & MACSEC_TCI_VERSION)
		return false;

	/* d) ES or SCB => !SC */
	if ((h->tci_an & MACSEC_TCI_ES || h->tci_an & MACSEC_TCI_SCB) &&
	    (h->tci_an & MACSEC_TCI_SC))
		return false;

	/* e) Bits 7 and 8 of octet 4 of the SecTAG are clear */
	if (h->unused)
		return false;

	/* rx.pn != 0 if not XPN (figure 10-5 with 802.11AEbw-2013 amendment) */
	if (!h->packet_number && !xpn)
		return false;

	/* length check, f) g) h) i) */
	if (h->short_length)
		return len == extra_len + h->short_length;
	return len >= extra_len + MIN_NON_SHORT_LEN;
}

#define MACSEC_NEEDED_HEADROOM (macsec_extra_len(true))
#define MACSEC_NEEDED_TAILROOM MACSEC_STD_ICV_LEN

static void macsec_fill_iv_xpn(unsigned char *iv, ssci_t ssci, u64 pn,
			       salt_t salt)
{
	struct gcm_iv_xpn *gcm_iv = (struct gcm_iv_xpn *)iv;

	gcm_iv->ssci = ssci ^ salt.ssci;
	gcm_iv->pn = cpu_to_be64(pn) ^ salt.pn;
}

static void macsec_fill_iv(unsigned char *iv, sci_t sci, u32 pn)
{
	struct gcm_iv *gcm_iv = (struct gcm_iv *)iv;

	gcm_iv->sci = sci;
	gcm_iv->pn = htonl(pn);
}

static struct macsec_eth_header *macsec_ethhdr(struct sk_buff *skb)
{
	return (struct macsec_eth_header *)skb_mac_header(skb);
}

static void __macsec_pn_wrapped(struct macsec_secy *secy,
				struct macsec_tx_sa *tx_sa)
{
	pr_debug("PN wrapped, transitioning to !oper\n");
	tx_sa->active = false;
	if (secy->protect_frames)
		secy->operational = false;
}

void macsec_pn_wrapped(struct macsec_secy *secy, struct macsec_tx_sa *tx_sa)
{
	spin_lock_bh(&tx_sa->lock);
	__macsec_pn_wrapped(secy, tx_sa);
	spin_unlock_bh(&tx_sa->lock);
}
EXPORT_SYMBOL_GPL(macsec_pn_wrapped);

static pn_t tx_sa_update_pn(struct macsec_tx_sa *tx_sa,
			    struct macsec_secy *secy)
{
	pn_t pn;

	spin_lock_bh(&tx_sa->lock);

	pn = tx_sa->next_pn_halves;
	if (secy->xpn)
		tx_sa->next_pn++;
	else
		tx_sa->next_pn_halves.lower++;

	if (tx_sa->next_pn == 0)
		__macsec_pn_wrapped(secy, tx_sa);
	spin_unlock_bh(&tx_sa->lock);

	return pn;
}

static void macsec_encrypt_finish(struct sk_buff *skb, struct net_device *dev)
{
	struct macsec_dev *macsec = netdev_priv(dev);

	skb->dev = macsec->real_dev;
	skb_reset_mac_header(skb);
	skb->protocol = eth_hdr(skb)->h_proto;
}

static void macsec_count_tx(struct sk_buff *skb, struct macsec_tx_sc *tx_sc,
			    struct macsec_tx_sa *tx_sa)
{
	struct pcpu_tx_sc_stats *txsc_stats = this_cpu_ptr(tx_sc->stats);

	u64_stats_update_begin(&txsc_stats->syncp);
	if (tx_sc->encrypt) {
		txsc_stats->stats.OutOctetsEncrypted += skb->len;
		txsc_stats->stats.OutPktsEncrypted++;
		this_cpu_inc(tx_sa->stats->OutPktsEncrypted);
	} else {
		txsc_stats->stats.OutOctetsProtected += skb->len;
		txsc_stats->stats.OutPktsProtected++;
		this_cpu_inc(tx_sa->stats->OutPktsProtected);
	}
	u64_stats_update_end(&txsc_stats->syncp);
}

static void count_tx(struct net_device *dev, int ret, int len)
{
	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct pcpu_sw_netstats *stats = this_cpu_ptr(dev->tstats);

		u64_stats_update_begin(&stats->syncp);
		stats->tx_packets++;
		stats->tx_bytes += len;
		u64_stats_update_end(&stats->syncp);
	}
}

static void macsec_encrypt_done(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;
	struct net_device *dev = skb->dev;
	struct macsec_dev *macsec = macsec_priv(dev);
	struct macsec_tx_sa *sa = macsec_skb_cb(skb)->tx_sa;
	int len, ret;

	aead_request_free(macsec_skb_cb(skb)->req);

	rcu_read_lock_bh();
	macsec_encrypt_finish(skb, dev);
	macsec_count_tx(skb, &macsec->secy.tx_sc, macsec_skb_cb(skb)->tx_sa);
	len = skb->len;
	ret = dev_queue_xmit(skb);
	count_tx(dev, ret, len);
	rcu_read_unlock_bh();

	macsec_txsa_put(sa);
	dev_put(dev);
}

static struct aead_request *macsec_alloc_req(struct crypto_aead *tfm,
					     unsigned char **iv,
					     struct scatterlist **sg,
					     int num_frags)
{
	size_t size, iv_offset, sg_offset;
	struct aead_request *req;
	void *tmp;

	size = sizeof(struct aead_request) + crypto_aead_reqsize(tfm);
	iv_offset = size;
	size += GCM_AES_IV_LEN;

	size = ALIGN(size, __alignof__(struct scatterlist));
	sg_offset = size;
	size += sizeof(struct scatterlist) * num_frags;

	tmp = kmalloc(size, GFP_ATOMIC);
	if (!tmp)
		return NULL;

	*iv = (unsigned char *)(tmp + iv_offset);
	*sg = (struct scatterlist *)(tmp + sg_offset);
	req = tmp;

	aead_request_set_tfm(req, tfm);

	return req;
}

static struct sk_buff *macsec_encrypt(struct sk_buff *skb,
				      struct net_device *dev)
{
	int ret;
	struct scatterlist *sg;
	struct sk_buff *trailer;
	unsigned char *iv;
	struct ethhdr *eth;
	struct macsec_eth_header *hh;
	size_t unprotected_len;
	struct aead_request *req;
	struct macsec_secy *secy;
	struct macsec_tx_sc *tx_sc;
	struct macsec_tx_sa *tx_sa;
	struct macsec_dev *macsec = macsec_priv(dev);
	bool sci_present;
	pn_t pn;

	secy = &macsec->secy;
	tx_sc = &secy->tx_sc;

	/* 10.5.1 TX SA assignment */
	tx_sa = macsec_txsa_get(tx_sc->sa[tx_sc->encoding_sa]);
	if (!tx_sa) {
		secy->operational = false;
		kfree_skb(skb);
		return ERR_PTR(-EINVAL);
	}

	if (unlikely(skb_headroom(skb) < MACSEC_NEEDED_HEADROOM ||
		     skb_tailroom(skb) < MACSEC_NEEDED_TAILROOM)) {
		struct sk_buff *nskb = skb_copy_expand(skb,
						       MACSEC_NEEDED_HEADROOM,
						       MACSEC_NEEDED_TAILROOM,
						       GFP_ATOMIC);
		if (likely(nskb)) {
			consume_skb(skb);
			skb = nskb;
		} else {
			macsec_txsa_put(tx_sa);
			kfree_skb(skb);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb) {
			macsec_txsa_put(tx_sa);
			return ERR_PTR(-ENOMEM);
		}
	}

	unprotected_len = skb->len;
	eth = eth_hdr(skb);
	sci_present = send_sci(secy);
	hh = skb_push(skb, macsec_extra_len(sci_present));
	memmove(hh, eth, 2 * ETH_ALEN);

	pn = tx_sa_update_pn(tx_sa, secy);
	if (pn.full64 == 0) {
		macsec_txsa_put(tx_sa);
		kfree_skb(skb);
		return ERR_PTR(-ENOLINK);
	}
	macsec_fill_sectag(hh, secy, pn.lower, sci_present);
	macsec_set_shortlen(hh, unprotected_len - 2 * ETH_ALEN);

	skb_put(skb, secy->icv_len);

	if (skb->len - ETH_HLEN > macsec_priv(dev)->real_dev->mtu) {
		struct pcpu_secy_stats *secy_stats = this_cpu_ptr(macsec->stats);

		u64_stats_update_begin(&secy_stats->syncp);
		secy_stats->stats.OutPktsTooLong++;
		u64_stats_update_end(&secy_stats->syncp);

		macsec_txsa_put(tx_sa);
		kfree_skb(skb);
		return ERR_PTR(-EINVAL);
	}

	ret = skb_cow_data(skb, 0, &trailer);
	if (unlikely(ret < 0)) {
		macsec_txsa_put(tx_sa);
		kfree_skb(skb);
		return ERR_PTR(ret);
	}

	req = macsec_alloc_req(tx_sa->key.tfm, &iv, &sg, ret);
	if (!req) {
		macsec_txsa_put(tx_sa);
		kfree_skb(skb);
		return ERR_PTR(-ENOMEM);
	}

	if (secy->xpn)
		macsec_fill_iv_xpn(iv, tx_sa->ssci, pn.full64, tx_sa->key.salt);
	else
		macsec_fill_iv(iv, secy->sci, pn.lower);

	sg_init_table(sg, ret);
	ret = skb_to_sgvec(skb, sg, 0, skb->len);
	if (unlikely(ret < 0)) {
		aead_request_free(req);
		macsec_txsa_put(tx_sa);
		kfree_skb(skb);
		return ERR_PTR(ret);
	}

	if (tx_sc->encrypt) {
		int len = skb->len - macsec_hdr_len(sci_present) -
			  secy->icv_len;
		aead_request_set_crypt(req, sg, sg, len, iv);
		aead_request_set_ad(req, macsec_hdr_len(sci_present));
	} else {
		aead_request_set_crypt(req, sg, sg, 0, iv);
		aead_request_set_ad(req, skb->len - secy->icv_len);
	}

	macsec_skb_cb(skb)->req = req;
	macsec_skb_cb(skb)->tx_sa = tx_sa;
	aead_request_set_callback(req, 0, macsec_encrypt_done, skb);

	dev_hold(skb->dev);
	ret = crypto_aead_encrypt(req);
	if (ret == -EINPROGRESS) {
		return ERR_PTR(ret);
	} else if (ret != 0) {
		dev_put(skb->dev);
		kfree_skb(skb);
		aead_request_free(req);
		macsec_txsa_put(tx_sa);
		return ERR_PTR(-EINVAL);
	}

	dev_put(skb->dev);
	aead_request_free(req);
	macsec_txsa_put(tx_sa);

	return skb;
}

static bool macsec_post_decrypt(struct sk_buff *skb, struct macsec_secy *secy, u32 pn)
{
	struct macsec_rx_sa *rx_sa = macsec_skb_cb(skb)->rx_sa;
	struct pcpu_rx_sc_stats *rxsc_stats = this_cpu_ptr(rx_sa->sc->stats);
	struct macsec_eth_header *hdr = macsec_ethhdr(skb);
	u32 lowest_pn = 0;

	spin_lock(&rx_sa->lock);
	if (rx_sa->next_pn_halves.lower >= secy->replay_window)
		lowest_pn = rx_sa->next_pn_halves.lower - secy->replay_window;

	/* Now perform replay protection check again
	 * (see IEEE 802.1AE-2006 figure 10-5)
	 */
	if (secy->replay_protect && pn < lowest_pn &&
	    (!secy->xpn || pn_same_half(pn, lowest_pn))) {
		spin_unlock(&rx_sa->lock);
		u64_stats_update_begin(&rxsc_stats->syncp);
		rxsc_stats->stats.InPktsLate++;
		u64_stats_update_end(&rxsc_stats->syncp);
		return false;
	}

	if (secy->validate_frames != MACSEC_VALIDATE_DISABLED) {
		u64_stats_update_begin(&rxsc_stats->syncp);
		if (hdr->tci_an & MACSEC_TCI_E)
			rxsc_stats->stats.InOctetsDecrypted += skb->len;
		else
			rxsc_stats->stats.InOctetsValidated += skb->len;
		u64_stats_update_end(&rxsc_stats->syncp);
	}

	if (!macsec_skb_cb(skb)->valid) {
		spin_unlock(&rx_sa->lock);

		/* 10.6.5 */
		if (hdr->tci_an & MACSEC_TCI_C ||
		    secy->validate_frames == MACSEC_VALIDATE_STRICT) {
			u64_stats_update_begin(&rxsc_stats->syncp);
			rxsc_stats->stats.InPktsNotValid++;
			u64_stats_update_end(&rxsc_stats->syncp);
			return false;
		}

		u64_stats_update_begin(&rxsc_stats->syncp);
		if (secy->validate_frames == MACSEC_VALIDATE_CHECK) {
			rxsc_stats->stats.InPktsInvalid++;
			this_cpu_inc(rx_sa->stats->InPktsInvalid);
		} else if (pn < lowest_pn) {
			rxsc_stats->stats.InPktsDelayed++;
		} else {
			rxsc_stats->stats.InPktsUnchecked++;
		}
		u64_stats_update_end(&rxsc_stats->syncp);
	} else {
		u64_stats_update_begin(&rxsc_stats->syncp);
		if (pn < lowest_pn) {
			rxsc_stats->stats.InPktsDelayed++;
		} else {
			rxsc_stats->stats.InPktsOK++;
			this_cpu_inc(rx_sa->stats->InPktsOK);
		}
		u64_stats_update_end(&rxsc_stats->syncp);

		// Instead of "pn >=" - to support pn overflow in xpn
		if (pn + 1 > rx_sa->next_pn_halves.lower) {
			rx_sa->next_pn_halves.lower = pn + 1;
		} else if (secy->xpn &&
			   !pn_same_half(pn, rx_sa->next_pn_halves.lower)) {
			rx_sa->next_pn_halves.upper++;
			rx_sa->next_pn_halves.lower = pn + 1;
		}

		spin_unlock(&rx_sa->lock);
	}

	return true;
}

static void macsec_reset_skb(struct sk_buff *skb, struct net_device *dev)
{
	skb->pkt_type = PACKET_HOST;
	skb->protocol = eth_type_trans(skb, dev);

	skb_reset_network_header(skb);
	if (!skb_transport_header_was_set(skb))
		skb_reset_transport_header(skb);
	skb_reset_mac_len(skb);
}

static void macsec_finalize_skb(struct sk_buff *skb, u8 icv_len, u8 hdr_len)
{
	skb->ip_summed = CHECKSUM_NONE;
	memmove(skb->data + hdr_len, skb->data, 2 * ETH_ALEN);
	skb_pull(skb, hdr_len);
	pskb_trim_unique(skb, skb->len - icv_len);
}

static void count_rx(struct net_device *dev, int len)
{
	struct pcpu_sw_netstats *stats = this_cpu_ptr(dev->tstats);

	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += len;
	u64_stats_update_end(&stats->syncp);
}

static void macsec_decrypt_done(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;
	struct net_device *dev = skb->dev;
	struct macsec_dev *macsec = macsec_priv(dev);
	struct macsec_rx_sa *rx_sa = macsec_skb_cb(skb)->rx_sa;
	struct macsec_rx_sc *rx_sc = rx_sa->sc;
	int len;
	u32 pn;

	aead_request_free(macsec_skb_cb(skb)->req);

	if (!err)
		macsec_skb_cb(skb)->valid = true;

	rcu_read_lock_bh();
	pn = ntohl(macsec_ethhdr(skb)->packet_number);
	if (!macsec_post_decrypt(skb, &macsec->secy, pn)) {
		rcu_read_unlock_bh();
		kfree_skb(skb);
		goto out;
	}

	macsec_finalize_skb(skb, macsec->secy.icv_len,
			    macsec_extra_len(macsec_skb_cb(skb)->has_sci));
	macsec_reset_skb(skb, macsec->secy.netdev);

	len = skb->len;
	if (gro_cells_receive(&macsec->gro_cells, skb) == NET_RX_SUCCESS)
		count_rx(dev, len);

	rcu_read_unlock_bh();

out:
	macsec_rxsa_put(rx_sa);
	macsec_rxsc_put(rx_sc);
	dev_put(dev);
}

static struct sk_buff *macsec_decrypt(struct sk_buff *skb,
				      struct net_device *dev,
				      struct macsec_rx_sa *rx_sa,
				      sci_t sci,
				      struct macsec_secy *secy)
{
	int ret;
	struct scatterlist *sg;
	struct sk_buff *trailer;
	unsigned char *iv;
	struct aead_request *req;
	struct macsec_eth_header *hdr;
	u32 hdr_pn;
	u16 icv_len = secy->icv_len;

	macsec_skb_cb(skb)->valid = false;
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	ret = skb_cow_data(skb, 0, &trailer);
	if (unlikely(ret < 0)) {
		kfree_skb(skb);
		return ERR_PTR(ret);
	}
	req = macsec_alloc_req(rx_sa->key.tfm, &iv, &sg, ret);
	if (!req) {
		kfree_skb(skb);
		return ERR_PTR(-ENOMEM);
	}

	hdr = (struct macsec_eth_header *)skb->data;
	hdr_pn = ntohl(hdr->packet_number);

	if (secy->xpn) {
		pn_t recovered_pn = rx_sa->next_pn_halves;

		recovered_pn.lower = hdr_pn;
		if (hdr_pn < rx_sa->next_pn_halves.lower &&
		    !pn_same_half(hdr_pn, rx_sa->next_pn_halves.lower))
			recovered_pn.upper++;

		macsec_fill_iv_xpn(iv, rx_sa->ssci, recovered_pn.full64,
				   rx_sa->key.salt);
	} else {
		macsec_fill_iv(iv, sci, hdr_pn);
	}

	sg_init_table(sg, ret);
	ret = skb_to_sgvec(skb, sg, 0, skb->len);
	if (unlikely(ret < 0)) {
		aead_request_free(req);
		kfree_skb(skb);
		return ERR_PTR(ret);
	}

	if (hdr->tci_an & MACSEC_TCI_E) {
		/* confidentiality: ethernet + macsec header
		 * authenticated, encrypted payload
		 */
		int len = skb->len - macsec_hdr_len(macsec_skb_cb(skb)->has_sci);

		aead_request_set_crypt(req, sg, sg, len, iv);
		aead_request_set_ad(req, macsec_hdr_len(macsec_skb_cb(skb)->has_sci));
		skb = skb_unshare(skb, GFP_ATOMIC);
		if (!skb) {
			aead_request_free(req);
			return ERR_PTR(-ENOMEM);
		}
	} else {
		/* integrity only: all headers + data authenticated */
		aead_request_set_crypt(req, sg, sg, icv_len, iv);
		aead_request_set_ad(req, skb->len - icv_len);
	}

	macsec_skb_cb(skb)->req = req;
	skb->dev = dev;
	aead_request_set_callback(req, 0, macsec_decrypt_done, skb);

	dev_hold(dev);
	ret = crypto_aead_decrypt(req);
	if (ret == -EINPROGRESS) {
		return ERR_PTR(ret);
	} else if (ret != 0) {
		/* decryption/authentication failed
		 * 10.6 if validateFrames is disabled, deliver anyway
		 */
		if (ret != -EBADMSG) {
			kfree_skb(skb);
			skb = ERR_PTR(ret);
		}
	} else {
		macsec_skb_cb(skb)->valid = true;
	}
	dev_put(dev);

	aead_request_free(req);

	return skb;
}

static struct macsec_rx_sc *find_rx_sc(struct macsec_secy *secy, sci_t sci)
{
	struct macsec_rx_sc *rx_sc;

	for_each_rxsc(secy, rx_sc) {
		if (rx_sc->sci == sci)
			return rx_sc;
	}

	return NULL;
}

static struct macsec_rx_sc *find_rx_sc_rtnl(struct macsec_secy *secy, sci_t sci)
{
	struct macsec_rx_sc *rx_sc;

	for_each_rxsc_rtnl(secy, rx_sc) {
		if (rx_sc->sci == sci)
			return rx_sc;
	}

	return NULL;
}

static enum rx_handler_result handle_not_macsec(struct sk_buff *skb)
{
	/* Deliver to the uncontrolled port by default */
	enum rx_handler_result ret = RX_HANDLER_PASS;
	struct ethhdr *hdr = eth_hdr(skb);
	struct macsec_rxh_data *rxd;
	struct macsec_dev *macsec;

	rcu_read_lock();
	rxd = macsec_data_rcu(skb->dev);

	list_for_each_entry_rcu(macsec, &rxd->secys, secys) {
		struct sk_buff *nskb;
		struct pcpu_secy_stats *secy_stats = this_cpu_ptr(macsec->stats);
		struct net_device *ndev = macsec->secy.netdev;

		/* If h/w offloading is enabled, HW decodes frames and strips
		 * the SecTAG, so we have to deduce which port to deliver to.
		 */
		if (macsec_is_offloaded(macsec) && netif_running(ndev)) {
			if (ether_addr_equal_64bits(hdr->h_dest,
						    ndev->dev_addr)) {
				/* exact match, divert skb to this port */
				skb->dev = ndev;
				skb->pkt_type = PACKET_HOST;
				ret = RX_HANDLER_ANOTHER;
				goto out;
			} else if (is_multicast_ether_addr_64bits(
					   hdr->h_dest)) {
				/* multicast frame, deliver on this port too */
				nskb = skb_clone(skb, GFP_ATOMIC);
				if (!nskb)
					break;

				nskb->dev = ndev;
				if (ether_addr_equal_64bits(hdr->h_dest,
							    ndev->broadcast))
					nskb->pkt_type = PACKET_BROADCAST;
				else
					nskb->pkt_type = PACKET_MULTICAST;

				netif_rx(nskb);
			}
			continue;
		}

		/* 10.6 If the management control validateFrames is not
		 * Strict, frames without a SecTAG are received, counted, and
		 * delivered to the Controlled Port
		 */
		if (macsec->secy.validate_frames == MACSEC_VALIDATE_STRICT) {
			u64_stats_update_begin(&secy_stats->syncp);
			secy_stats->stats.InPktsNoTag++;
			u64_stats_update_end(&secy_stats->syncp);
			continue;
		}

		/* deliver on this port */
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			break;

		nskb->dev = ndev;

		if (netif_rx(nskb) == NET_RX_SUCCESS) {
			u64_stats_update_begin(&secy_stats->syncp);
			secy_stats->stats.InPktsUntagged++;
			u64_stats_update_end(&secy_stats->syncp);
		}
	}

out:
	rcu_read_unlock();
	return ret;
}

static rx_handler_result_t macsec_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct net_device *dev = skb->dev;
	struct macsec_eth_header *hdr;
	struct macsec_secy *secy = NULL;
	struct macsec_rx_sc *rx_sc;
	struct macsec_rx_sa *rx_sa;
	struct macsec_rxh_data *rxd;
	struct macsec_dev *macsec;
	unsigned int len;
	sci_t sci;
	u32 hdr_pn;
	bool cbit;
	struct pcpu_rx_sc_stats *rxsc_stats;
	struct pcpu_secy_stats *secy_stats;
	bool pulled_sci;
	int ret;

	if (skb_headroom(skb) < ETH_HLEN)
		goto drop_direct;

	hdr = macsec_ethhdr(skb);
	if (hdr->eth.h_proto != htons(ETH_P_MACSEC))
		return handle_not_macsec(skb);

	skb = skb_unshare(skb, GFP_ATOMIC);
	*pskb = skb;
	if (!skb)
		return RX_HANDLER_CONSUMED;

	pulled_sci = pskb_may_pull(skb, macsec_extra_len(true));
	if (!pulled_sci) {
		if (!pskb_may_pull(skb, macsec_extra_len(false)))
			goto drop_direct;
	}

	hdr = macsec_ethhdr(skb);

	/* Frames with a SecTAG that has the TCI E bit set but the C
	 * bit clear are discarded, as this reserved encoding is used
	 * to identify frames with a SecTAG that are not to be
	 * delivered to the Controlled Port.
	 */
	if ((hdr->tci_an & (MACSEC_TCI_C | MACSEC_TCI_E)) == MACSEC_TCI_E)
		return RX_HANDLER_PASS;

	/* now, pull the extra length */
	if (hdr->tci_an & MACSEC_TCI_SC) {
		if (!pulled_sci)
			goto drop_direct;
	}

	/* ethernet header is part of crypto processing */
	skb_push(skb, ETH_HLEN);

	macsec_skb_cb(skb)->has_sci = !!(hdr->tci_an & MACSEC_TCI_SC);
	macsec_skb_cb(skb)->assoc_num = hdr->tci_an & MACSEC_AN_MASK;
	sci = macsec_frame_sci(hdr, macsec_skb_cb(skb)->has_sci);

	rcu_read_lock();
	rxd = macsec_data_rcu(skb->dev);

	list_for_each_entry_rcu(macsec, &rxd->secys, secys) {
		struct macsec_rx_sc *sc = find_rx_sc(&macsec->secy, sci);

		sc = sc ? macsec_rxsc_get(sc) : NULL;

		if (sc) {
			secy = &macsec->secy;
			rx_sc = sc;
			break;
		}
	}

	if (!secy)
		goto nosci;

	dev = secy->netdev;
	macsec = macsec_priv(dev);
	secy_stats = this_cpu_ptr(macsec->stats);
	rxsc_stats = this_cpu_ptr(rx_sc->stats);

	if (!macsec_validate_skb(skb, secy->icv_len, secy->xpn)) {
		u64_stats_update_begin(&secy_stats->syncp);
		secy_stats->stats.InPktsBadTag++;
		u64_stats_update_end(&secy_stats->syncp);
		goto drop_nosa;
	}

	rx_sa = macsec_rxsa_get(rx_sc->sa[macsec_skb_cb(skb)->assoc_num]);
	if (!rx_sa) {
		/* 10.6.1 if the SA is not in use */

		/* If validateFrames is Strict or the C bit in the
		 * SecTAG is set, discard
		 */
		if (hdr->tci_an & MACSEC_TCI_C ||
		    secy->validate_frames == MACSEC_VALIDATE_STRICT) {
			u64_stats_update_begin(&rxsc_stats->syncp);
			rxsc_stats->stats.InPktsNotUsingSA++;
			u64_stats_update_end(&rxsc_stats->syncp);
			goto drop_nosa;
		}

		/* not Strict, the frame (with the SecTAG and ICV
		 * removed) is delivered to the Controlled Port.
		 */
		u64_stats_update_begin(&rxsc_stats->syncp);
		rxsc_stats->stats.InPktsUnusedSA++;
		u64_stats_update_end(&rxsc_stats->syncp);
		goto deliver;
	}

	/* First, PN check to avoid decrypting obviously wrong packets */
	hdr_pn = ntohl(hdr->packet_number);
	if (secy->replay_protect) {
		bool late;

		spin_lock(&rx_sa->lock);
		late = rx_sa->next_pn_halves.lower >= secy->replay_window &&
		       hdr_pn < (rx_sa->next_pn_halves.lower - secy->replay_window);

		if (secy->xpn)
			late = late && pn_same_half(rx_sa->next_pn_halves.lower, hdr_pn);
		spin_unlock(&rx_sa->lock);

		if (late) {
			u64_stats_update_begin(&rxsc_stats->syncp);
			rxsc_stats->stats.InPktsLate++;
			u64_stats_update_end(&rxsc_stats->syncp);
			goto drop;
		}
	}

	macsec_skb_cb(skb)->rx_sa = rx_sa;

	/* Disabled && !changed text => skip validation */
	if (hdr->tci_an & MACSEC_TCI_C ||
	    secy->validate_frames != MACSEC_VALIDATE_DISABLED)
		skb = macsec_decrypt(skb, dev, rx_sa, sci, secy);

	if (IS_ERR(skb)) {
		/* the decrypt callback needs the reference */
		if (PTR_ERR(skb) != -EINPROGRESS) {
			macsec_rxsa_put(rx_sa);
			macsec_rxsc_put(rx_sc);
		}
		rcu_read_unlock();
		*pskb = NULL;
		return RX_HANDLER_CONSUMED;
	}

	if (!macsec_post_decrypt(skb, secy, hdr_pn))
		goto drop;

deliver:
	macsec_finalize_skb(skb, secy->icv_len,
			    macsec_extra_len(macsec_skb_cb(skb)->has_sci));
	macsec_reset_skb(skb, secy->netdev);

	if (rx_sa)
		macsec_rxsa_put(rx_sa);
	macsec_rxsc_put(rx_sc);

	skb_orphan(skb);
	len = skb->len;
	ret = gro_cells_receive(&macsec->gro_cells, skb);
	if (ret == NET_RX_SUCCESS)
		count_rx(dev, len);
	else
		macsec->secy.netdev->stats.rx_dropped++;

	rcu_read_unlock();

	*pskb = NULL;
	return RX_HANDLER_CONSUMED;

drop:
	macsec_rxsa_put(rx_sa);
drop_nosa:
	macsec_rxsc_put(rx_sc);
	rcu_read_unlock();
drop_direct:
	kfree_skb(skb);
	*pskb = NULL;
	return RX_HANDLER_CONSUMED;

nosci:
	/* 10.6.1 if the SC is not found */
	cbit = !!(hdr->tci_an & MACSEC_TCI_C);
	if (!cbit)
		macsec_finalize_skb(skb, DEFAULT_ICV_LEN,
				    macsec_extra_len(macsec_skb_cb(skb)->has_sci));

	list_for_each_entry_rcu(macsec, &rxd->secys, secys) {
		struct sk_buff *nskb;

		secy_stats = this_cpu_ptr(macsec->stats);

		/* If validateFrames is Strict or the C bit in the
		 * SecTAG is set, discard
		 */
		if (cbit ||
		    macsec->secy.validate_frames == MACSEC_VALIDATE_STRICT) {
			u64_stats_update_begin(&secy_stats->syncp);
			secy_stats->stats.InPktsNoSCI++;
			u64_stats_update_end(&secy_stats->syncp);
			continue;
		}

		/* not strict, the frame (with the SecTAG and ICV
		 * removed) is delivered to the Controlled Port.
		 */
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			break;

		macsec_reset_skb(nskb, macsec->secy.netdev);

		ret = netif_rx(nskb);
		if (ret == NET_RX_SUCCESS) {
			u64_stats_update_begin(&secy_stats->syncp);
			secy_stats->stats.InPktsUnknownSCI++;
			u64_stats_update_end(&secy_stats->syncp);
		} else {
			macsec->secy.netdev->stats.rx_dropped++;
		}
	}

	rcu_read_unlock();
	*pskb = skb;
	return RX_HANDLER_PASS;
}

static struct crypto_aead *macsec_alloc_tfm(char *key, int key_len, int icv_len)
{
	struct crypto_aead *tfm;
	int ret;

	/* Pick a sync gcm(aes) cipher to ensure order is preserved. */
	tfm = crypto_alloc_aead("gcm(aes)", 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm))
		return tfm;

	ret = crypto_aead_setkey(tfm, key, key_len);
	if (ret < 0)
		goto fail;

	ret = crypto_aead_setauthsize(tfm, icv_len);
	if (ret < 0)
		goto fail;

	return tfm;
fail:
	crypto_free_aead(tfm);
	return ERR_PTR(ret);
}

static int init_rx_sa(struct macsec_rx_sa *rx_sa, char *sak, int key_len,
		      int icv_len)
{
	rx_sa->stats = alloc_percpu(struct macsec_rx_sa_stats);
	if (!rx_sa->stats)
		return -ENOMEM;

	rx_sa->key.tfm = macsec_alloc_tfm(sak, key_len, icv_len);
	if (IS_ERR(rx_sa->key.tfm)) {
		free_percpu(rx_sa->stats);
		return PTR_ERR(rx_sa->key.tfm);
	}

	rx_sa->ssci = MACSEC_UNDEF_SSCI;
	rx_sa->active = false;
	rx_sa->next_pn = 1;
	refcount_set(&rx_sa->refcnt, 1);
	spin_lock_init(&rx_sa->lock);

	return 0;
}

static void clear_rx_sa(struct macsec_rx_sa *rx_sa)
{
	rx_sa->active = false;

	macsec_rxsa_put(rx_sa);
}

static void free_rx_sc(struct macsec_rx_sc *rx_sc)
{
	int i;

	for (i = 0; i < MACSEC_NUM_AN; i++) {
		struct macsec_rx_sa *sa = rtnl_dereference(rx_sc->sa[i]);

		RCU_INIT_POINTER(rx_sc->sa[i], NULL);
		if (sa)
			clear_rx_sa(sa);
	}

	macsec_rxsc_put(rx_sc);
}

static struct macsec_rx_sc *del_rx_sc(struct macsec_secy *secy, sci_t sci)
{
	struct macsec_rx_sc *rx_sc, __rcu **rx_scp;

	for (rx_scp = &secy->rx_sc, rx_sc = rtnl_dereference(*rx_scp);
	     rx_sc;
	     rx_scp = &rx_sc->next, rx_sc = rtnl_dereference(*rx_scp)) {
		if (rx_sc->sci == sci) {
			if (rx_sc->active)
				secy->n_rx_sc--;
			rcu_assign_pointer(*rx_scp, rx_sc->next);
			return rx_sc;
		}
	}

	return NULL;
}

static struct macsec_rx_sc *create_rx_sc(struct net_device *dev, sci_t sci,
					 bool active)
{
	struct macsec_rx_sc *rx_sc;
	struct macsec_dev *macsec;
	struct net_device *real_dev = macsec_priv(dev)->real_dev;
	struct macsec_rxh_data *rxd = macsec_data_rtnl(real_dev);
	struct macsec_secy *secy;

	list_for_each_entry(macsec, &rxd->secys, secys) {
		if (find_rx_sc_rtnl(&macsec->secy, sci))
			return ERR_PTR(-EEXIST);
	}

	rx_sc = kzalloc(sizeof(*rx_sc), GFP_KERNEL);
	if (!rx_sc)
		return ERR_PTR(-ENOMEM);

	rx_sc->stats = netdev_alloc_pcpu_stats(struct pcpu_rx_sc_stats);
	if (!rx_sc->stats) {
		kfree(rx_sc);
		return ERR_PTR(-ENOMEM);
	}

	rx_sc->sci = sci;
	rx_sc->active = active;
	refcount_set(&rx_sc->refcnt, 1);

	secy = &macsec_priv(dev)->secy;
	rcu_assign_pointer(rx_sc->next, secy->rx_sc);
	rcu_assign_pointer(secy->rx_sc, rx_sc);

	if (rx_sc->active)
		secy->n_rx_sc++;

	return rx_sc;
}

static int init_tx_sa(struct macsec_tx_sa *tx_sa, char *sak, int key_len,
		      int icv_len)
{
	tx_sa->stats = alloc_percpu(struct macsec_tx_sa_stats);
	if (!tx_sa->stats)
		return -ENOMEM;

	tx_sa->key.tfm = macsec_alloc_tfm(sak, key_len, icv_len);
	if (IS_ERR(tx_sa->key.tfm)) {
		free_percpu(tx_sa->stats);
		return PTR_ERR(tx_sa->key.tfm);
	}

	tx_sa->ssci = MACSEC_UNDEF_SSCI;
	tx_sa->active = false;
	refcount_set(&tx_sa->refcnt, 1);
	spin_lock_init(&tx_sa->lock);

	return 0;
}

static void clear_tx_sa(struct macsec_tx_sa *tx_sa)
{
	tx_sa->active = false;

	macsec_txsa_put(tx_sa);
}

static struct genl_family macsec_fam;

static struct net_device *get_dev_from_nl(struct net *net,
					  struct nlattr **attrs)
{
	int ifindex = nla_get_u32(attrs[MACSEC_ATTR_IFINDEX]);
	struct net_device *dev;

	dev = __dev_get_by_index(net, ifindex);
	if (!dev)
		return ERR_PTR(-ENODEV);

	if (!netif_is_macsec(dev))
		return ERR_PTR(-ENODEV);

	return dev;
}

static enum macsec_offload nla_get_offload(const struct nlattr *nla)
{
	return (__force enum macsec_offload)nla_get_u8(nla);
}

static sci_t nla_get_sci(const struct nlattr *nla)
{
	return (__force sci_t)nla_get_u64(nla);
}

static int nla_put_sci(struct sk_buff *skb, int attrtype, sci_t value,
		       int padattr)
{
	return nla_put_u64_64bit(skb, attrtype, (__force u64)value, padattr);
}

static ssci_t nla_get_ssci(const struct nlattr *nla)
{
	return (__force ssci_t)nla_get_u32(nla);
}

static int nla_put_ssci(struct sk_buff *skb, int attrtype, ssci_t value)
{
	return nla_put_u32(skb, attrtype, (__force u64)value);
}

static struct macsec_tx_sa *get_txsa_from_nl(struct net *net,
					     struct nlattr **attrs,
					     struct nlattr **tb_sa,
					     struct net_device **devp,
					     struct macsec_secy **secyp,
					     struct macsec_tx_sc **scp,
					     u8 *assoc_num)
{
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_tx_sc *tx_sc;
	struct macsec_tx_sa *tx_sa;

	if (!tb_sa[MACSEC_SA_ATTR_AN])
		return ERR_PTR(-EINVAL);

	*assoc_num = nla_get_u8(tb_sa[MACSEC_SA_ATTR_AN]);

	dev = get_dev_from_nl(net, attrs);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	if (*assoc_num >= MACSEC_NUM_AN)
		return ERR_PTR(-EINVAL);

	secy = &macsec_priv(dev)->secy;
	tx_sc = &secy->tx_sc;

	tx_sa = rtnl_dereference(tx_sc->sa[*assoc_num]);
	if (!tx_sa)
		return ERR_PTR(-ENODEV);

	*devp = dev;
	*scp = tx_sc;
	*secyp = secy;
	return tx_sa;
}

static struct macsec_rx_sc *get_rxsc_from_nl(struct net *net,
					     struct nlattr **attrs,
					     struct nlattr **tb_rxsc,
					     struct net_device **devp,
					     struct macsec_secy **secyp)
{
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	sci_t sci;

	dev = get_dev_from_nl(net, attrs);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	secy = &macsec_priv(dev)->secy;

	if (!tb_rxsc[MACSEC_RXSC_ATTR_SCI])
		return ERR_PTR(-EINVAL);

	sci = nla_get_sci(tb_rxsc[MACSEC_RXSC_ATTR_SCI]);
	rx_sc = find_rx_sc_rtnl(secy, sci);
	if (!rx_sc)
		return ERR_PTR(-ENODEV);

	*secyp = secy;
	*devp = dev;

	return rx_sc;
}

static struct macsec_rx_sa *get_rxsa_from_nl(struct net *net,
					     struct nlattr **attrs,
					     struct nlattr **tb_rxsc,
					     struct nlattr **tb_sa,
					     struct net_device **devp,
					     struct macsec_secy **secyp,
					     struct macsec_rx_sc **scp,
					     u8 *assoc_num)
{
	struct macsec_rx_sc *rx_sc;
	struct macsec_rx_sa *rx_sa;

	if (!tb_sa[MACSEC_SA_ATTR_AN])
		return ERR_PTR(-EINVAL);

	*assoc_num = nla_get_u8(tb_sa[MACSEC_SA_ATTR_AN]);
	if (*assoc_num >= MACSEC_NUM_AN)
		return ERR_PTR(-EINVAL);

	rx_sc = get_rxsc_from_nl(net, attrs, tb_rxsc, devp, secyp);
	if (IS_ERR(rx_sc))
		return ERR_CAST(rx_sc);

	rx_sa = rtnl_dereference(rx_sc->sa[*assoc_num]);
	if (!rx_sa)
		return ERR_PTR(-ENODEV);

	*scp = rx_sc;
	return rx_sa;
}

static const struct nla_policy macsec_genl_policy[NUM_MACSEC_ATTR] = {
	[MACSEC_ATTR_IFINDEX] = { .type = NLA_U32 },
	[MACSEC_ATTR_RXSC_CONFIG] = { .type = NLA_NESTED },
	[MACSEC_ATTR_SA_CONFIG] = { .type = NLA_NESTED },
	[MACSEC_ATTR_OFFLOAD] = { .type = NLA_NESTED },
};

static const struct nla_policy macsec_genl_rxsc_policy[NUM_MACSEC_RXSC_ATTR] = {
	[MACSEC_RXSC_ATTR_SCI] = { .type = NLA_U64 },
	[MACSEC_RXSC_ATTR_ACTIVE] = { .type = NLA_U8 },
};

static const struct nla_policy macsec_genl_sa_policy[NUM_MACSEC_SA_ATTR] = {
	[MACSEC_SA_ATTR_AN] = { .type = NLA_U8 },
	[MACSEC_SA_ATTR_ACTIVE] = { .type = NLA_U8 },
	[MACSEC_SA_ATTR_PN] = NLA_POLICY_MIN_LEN(4),
	[MACSEC_SA_ATTR_KEYID] = { .type = NLA_BINARY,
				   .len = MACSEC_KEYID_LEN, },
	[MACSEC_SA_ATTR_KEY] = { .type = NLA_BINARY,
				 .len = MACSEC_MAX_KEY_LEN, },
	[MACSEC_SA_ATTR_SSCI] = { .type = NLA_U32 },
	[MACSEC_SA_ATTR_SALT] = { .type = NLA_BINARY,
				  .len = MACSEC_SALT_LEN, },
};

static const struct nla_policy macsec_genl_offload_policy[NUM_MACSEC_OFFLOAD_ATTR] = {
	[MACSEC_OFFLOAD_ATTR_TYPE] = { .type = NLA_U8 },
};

/* Offloads an operation to a device driver */
static int macsec_offload(int (* const func)(struct macsec_context *),
			  struct macsec_context *ctx)
{
	int ret;

	if (unlikely(!func))
		return 0;

	if (ctx->offload == MACSEC_OFFLOAD_PHY)
		mutex_lock(&ctx->phydev->lock);

	/* Phase I: prepare. The drive should fail here if there are going to be
	 * issues in the commit phase.
	 */
	ctx->prepare = true;
	ret = (*func)(ctx);
	if (ret)
		goto phy_unlock;

	/* Phase II: commit. This step cannot fail. */
	ctx->prepare = false;
	ret = (*func)(ctx);
	/* This should never happen: commit is not allowed to fail */
	if (unlikely(ret))
		WARN(1, "MACsec offloading commit failed (%d)\n", ret);

phy_unlock:
	if (ctx->offload == MACSEC_OFFLOAD_PHY)
		mutex_unlock(&ctx->phydev->lock);

	return ret;
}

static int parse_sa_config(struct nlattr **attrs, struct nlattr **tb_sa)
{
	if (!attrs[MACSEC_ATTR_SA_CONFIG])
		return -EINVAL;

	if (nla_parse_nested_deprecated(tb_sa, MACSEC_SA_ATTR_MAX, attrs[MACSEC_ATTR_SA_CONFIG], macsec_genl_sa_policy, NULL))
		return -EINVAL;

	return 0;
}

static int parse_rxsc_config(struct nlattr **attrs, struct nlattr **tb_rxsc)
{
	if (!attrs[MACSEC_ATTR_RXSC_CONFIG])
		return -EINVAL;

	if (nla_parse_nested_deprecated(tb_rxsc, MACSEC_RXSC_ATTR_MAX, attrs[MACSEC_ATTR_RXSC_CONFIG], macsec_genl_rxsc_policy, NULL))
		return -EINVAL;

	return 0;
}

static bool validate_add_rxsa(struct nlattr **attrs)
{
	if (!attrs[MACSEC_SA_ATTR_AN] ||
	    !attrs[MACSEC_SA_ATTR_KEY] ||
	    !attrs[MACSEC_SA_ATTR_KEYID])
		return false;

	if (nla_get_u8(attrs[MACSEC_SA_ATTR_AN]) >= MACSEC_NUM_AN)
		return false;

	if (attrs[MACSEC_SA_ATTR_PN] &&
	    nla_get_u64(attrs[MACSEC_SA_ATTR_PN]) == 0)
		return false;

	if (attrs[MACSEC_SA_ATTR_ACTIVE]) {
		if (nla_get_u8(attrs[MACSEC_SA_ATTR_ACTIVE]) > 1)
			return false;
	}

	if (nla_len(attrs[MACSEC_SA_ATTR_KEYID]) != MACSEC_KEYID_LEN)
		return false;

	return true;
}

static int macsec_add_rxsa(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct nlattr **attrs = info->attrs;
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct macsec_rx_sa *rx_sa;
	unsigned char assoc_num;
	int pn_len;
	struct nlattr *tb_rxsc[MACSEC_RXSC_ATTR_MAX + 1];
	struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];
	int err;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_sa_config(attrs, tb_sa))
		return -EINVAL;

	if (parse_rxsc_config(attrs, tb_rxsc))
		return -EINVAL;

	if (!validate_add_rxsa(tb_sa))
		return -EINVAL;

	rtnl_lock();
	rx_sc = get_rxsc_from_nl(genl_info_net(info), attrs, tb_rxsc, &dev, &secy);
	if (IS_ERR(rx_sc)) {
		rtnl_unlock();
		return PTR_ERR(rx_sc);
	}

	assoc_num = nla_get_u8(tb_sa[MACSEC_SA_ATTR_AN]);

	if (nla_len(tb_sa[MACSEC_SA_ATTR_KEY]) != secy->key_len) {
		pr_notice("macsec: nl: add_rxsa: bad key length: %d != %d\n",
			  nla_len(tb_sa[MACSEC_SA_ATTR_KEY]), secy->key_len);
		rtnl_unlock();
		return -EINVAL;
	}

	pn_len = secy->xpn ? MACSEC_XPN_PN_LEN : MACSEC_DEFAULT_PN_LEN;
	if (tb_sa[MACSEC_SA_ATTR_PN] &&
	    nla_len(tb_sa[MACSEC_SA_ATTR_PN]) != pn_len) {
		pr_notice("macsec: nl: add_rxsa: bad pn length: %d != %d\n",
			  nla_len(tb_sa[MACSEC_SA_ATTR_PN]), pn_len);
		rtnl_unlock();
		return -EINVAL;
	}

	if (secy->xpn) {
		if (!tb_sa[MACSEC_SA_ATTR_SSCI] || !tb_sa[MACSEC_SA_ATTR_SALT]) {
			rtnl_unlock();
			return -EINVAL;
		}

		if (nla_len(tb_sa[MACSEC_SA_ATTR_SALT]) != MACSEC_SALT_LEN) {
			pr_notice("macsec: nl: add_rxsa: bad salt length: %d != %d\n",
				  nla_len(tb_sa[MACSEC_SA_ATTR_SALT]),
				  MACSEC_SALT_LEN);
			rtnl_unlock();
			return -EINVAL;
		}
	}

	rx_sa = rtnl_dereference(rx_sc->sa[assoc_num]);
	if (rx_sa) {
		rtnl_unlock();
		return -EBUSY;
	}

	rx_sa = kmalloc(sizeof(*rx_sa), GFP_KERNEL);
	if (!rx_sa) {
		rtnl_unlock();
		return -ENOMEM;
	}

	err = init_rx_sa(rx_sa, nla_data(tb_sa[MACSEC_SA_ATTR_KEY]),
			 secy->key_len, secy->icv_len);
	if (err < 0) {
		kfree(rx_sa);
		rtnl_unlock();
		return err;
	}

	if (tb_sa[MACSEC_SA_ATTR_PN]) {
		spin_lock_bh(&rx_sa->lock);
		rx_sa->next_pn = nla_get_u64(tb_sa[MACSEC_SA_ATTR_PN]);
		spin_unlock_bh(&rx_sa->lock);
	}

	if (tb_sa[MACSEC_SA_ATTR_ACTIVE])
		rx_sa->active = !!nla_get_u8(tb_sa[MACSEC_SA_ATTR_ACTIVE]);

	rx_sa->sc = rx_sc;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			err = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.sa.assoc_num = assoc_num;
		ctx.sa.rx_sa = rx_sa;
		ctx.secy = secy;
		memcpy(ctx.sa.key, nla_data(tb_sa[MACSEC_SA_ATTR_KEY]),
		       secy->key_len);

		err = macsec_offload(ops->mdo_add_rxsa, &ctx);
		if (err)
			goto cleanup;
	}

	if (secy->xpn) {
		rx_sa->ssci = nla_get_ssci(tb_sa[MACSEC_SA_ATTR_SSCI]);
		nla_memcpy(rx_sa->key.salt.bytes, tb_sa[MACSEC_SA_ATTR_SALT],
			   MACSEC_SALT_LEN);
	}

	nla_memcpy(rx_sa->key.id, tb_sa[MACSEC_SA_ATTR_KEYID], MACSEC_KEYID_LEN);
	rcu_assign_pointer(rx_sc->sa[assoc_num], rx_sa);

	rtnl_unlock();

	return 0;

cleanup:
	macsec_rxsa_put(rx_sa);
	rtnl_unlock();
	return err;
}

static bool validate_add_rxsc(struct nlattr **attrs)
{
	if (!attrs[MACSEC_RXSC_ATTR_SCI])
		return false;

	if (attrs[MACSEC_RXSC_ATTR_ACTIVE]) {
		if (nla_get_u8(attrs[MACSEC_RXSC_ATTR_ACTIVE]) > 1)
			return false;
	}

	return true;
}

static int macsec_add_rxsc(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	sci_t sci = MACSEC_UNDEF_SCI;
	struct nlattr **attrs = info->attrs;
	struct macsec_rx_sc *rx_sc;
	struct nlattr *tb_rxsc[MACSEC_RXSC_ATTR_MAX + 1];
	struct macsec_secy *secy;
	bool active = true;
	int ret;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_rxsc_config(attrs, tb_rxsc))
		return -EINVAL;

	if (!validate_add_rxsc(tb_rxsc))
		return -EINVAL;

	rtnl_lock();
	dev = get_dev_from_nl(genl_info_net(info), attrs);
	if (IS_ERR(dev)) {
		rtnl_unlock();
		return PTR_ERR(dev);
	}

	secy = &macsec_priv(dev)->secy;
	sci = nla_get_sci(tb_rxsc[MACSEC_RXSC_ATTR_SCI]);

	if (tb_rxsc[MACSEC_RXSC_ATTR_ACTIVE])
		active = nla_get_u8(tb_rxsc[MACSEC_RXSC_ATTR_ACTIVE]);

	rx_sc = create_rx_sc(dev, sci, active);
	if (IS_ERR(rx_sc)) {
		rtnl_unlock();
		return PTR_ERR(rx_sc);
	}

	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.rx_sc = rx_sc;
		ctx.secy = secy;

		ret = macsec_offload(ops->mdo_add_rxsc, &ctx);
		if (ret)
			goto cleanup;
	}

	rtnl_unlock();

	return 0;

cleanup:
	del_rx_sc(secy, sci);
	free_rx_sc(rx_sc);
	rtnl_unlock();
	return ret;
}

static bool validate_add_txsa(struct nlattr **attrs)
{
	if (!attrs[MACSEC_SA_ATTR_AN] ||
	    !attrs[MACSEC_SA_ATTR_PN] ||
	    !attrs[MACSEC_SA_ATTR_KEY] ||
	    !attrs[MACSEC_SA_ATTR_KEYID])
		return false;

	if (nla_get_u8(attrs[MACSEC_SA_ATTR_AN]) >= MACSEC_NUM_AN)
		return false;

	if (nla_get_u64(attrs[MACSEC_SA_ATTR_PN]) == 0)
		return false;

	if (attrs[MACSEC_SA_ATTR_ACTIVE]) {
		if (nla_get_u8(attrs[MACSEC_SA_ATTR_ACTIVE]) > 1)
			return false;
	}

	if (nla_len(attrs[MACSEC_SA_ATTR_KEYID]) != MACSEC_KEYID_LEN)
		return false;

	return true;
}

static int macsec_add_txsa(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct nlattr **attrs = info->attrs;
	struct macsec_secy *secy;
	struct macsec_tx_sc *tx_sc;
	struct macsec_tx_sa *tx_sa;
	unsigned char assoc_num;
	int pn_len;
	struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];
	bool was_operational;
	int err;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_sa_config(attrs, tb_sa))
		return -EINVAL;

	if (!validate_add_txsa(tb_sa))
		return -EINVAL;

	rtnl_lock();
	dev = get_dev_from_nl(genl_info_net(info), attrs);
	if (IS_ERR(dev)) {
		rtnl_unlock();
		return PTR_ERR(dev);
	}

	secy = &macsec_priv(dev)->secy;
	tx_sc = &secy->tx_sc;

	assoc_num = nla_get_u8(tb_sa[MACSEC_SA_ATTR_AN]);

	if (nla_len(tb_sa[MACSEC_SA_ATTR_KEY]) != secy->key_len) {
		pr_notice("macsec: nl: add_txsa: bad key length: %d != %d\n",
			  nla_len(tb_sa[MACSEC_SA_ATTR_KEY]), secy->key_len);
		rtnl_unlock();
		return -EINVAL;
	}

	pn_len = secy->xpn ? MACSEC_XPN_PN_LEN : MACSEC_DEFAULT_PN_LEN;
	if (nla_len(tb_sa[MACSEC_SA_ATTR_PN]) != pn_len) {
		pr_notice("macsec: nl: add_txsa: bad pn length: %d != %d\n",
			  nla_len(tb_sa[MACSEC_SA_ATTR_PN]), pn_len);
		rtnl_unlock();
		return -EINVAL;
	}

	if (secy->xpn) {
		if (!tb_sa[MACSEC_SA_ATTR_SSCI] || !tb_sa[MACSEC_SA_ATTR_SALT]) {
			rtnl_unlock();
			return -EINVAL;
		}

		if (nla_len(tb_sa[MACSEC_SA_ATTR_SALT]) != MACSEC_SALT_LEN) {
			pr_notice("macsec: nl: add_txsa: bad salt length: %d != %d\n",
				  nla_len(tb_sa[MACSEC_SA_ATTR_SALT]),
				  MACSEC_SALT_LEN);
			rtnl_unlock();
			return -EINVAL;
		}
	}

	tx_sa = rtnl_dereference(tx_sc->sa[assoc_num]);
	if (tx_sa) {
		rtnl_unlock();
		return -EBUSY;
	}

	tx_sa = kmalloc(sizeof(*tx_sa), GFP_KERNEL);
	if (!tx_sa) {
		rtnl_unlock();
		return -ENOMEM;
	}

	err = init_tx_sa(tx_sa, nla_data(tb_sa[MACSEC_SA_ATTR_KEY]),
			 secy->key_len, secy->icv_len);
	if (err < 0) {
		kfree(tx_sa);
		rtnl_unlock();
		return err;
	}

	spin_lock_bh(&tx_sa->lock);
	tx_sa->next_pn = nla_get_u64(tb_sa[MACSEC_SA_ATTR_PN]);
	spin_unlock_bh(&tx_sa->lock);

	if (tb_sa[MACSEC_SA_ATTR_ACTIVE])
		tx_sa->active = !!nla_get_u8(tb_sa[MACSEC_SA_ATTR_ACTIVE]);

	was_operational = secy->operational;
	if (assoc_num == tx_sc->encoding_sa && tx_sa->active)
		secy->operational = true;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			err = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.sa.assoc_num = assoc_num;
		ctx.sa.tx_sa = tx_sa;
		ctx.secy = secy;
		memcpy(ctx.sa.key, nla_data(tb_sa[MACSEC_SA_ATTR_KEY]),
		       secy->key_len);

		err = macsec_offload(ops->mdo_add_txsa, &ctx);
		if (err)
			goto cleanup;
	}

	if (secy->xpn) {
		tx_sa->ssci = nla_get_ssci(tb_sa[MACSEC_SA_ATTR_SSCI]);
		nla_memcpy(tx_sa->key.salt.bytes, tb_sa[MACSEC_SA_ATTR_SALT],
			   MACSEC_SALT_LEN);
	}

	nla_memcpy(tx_sa->key.id, tb_sa[MACSEC_SA_ATTR_KEYID], MACSEC_KEYID_LEN);
	rcu_assign_pointer(tx_sc->sa[assoc_num], tx_sa);

	rtnl_unlock();

	return 0;

cleanup:
	secy->operational = was_operational;
	macsec_txsa_put(tx_sa);
	rtnl_unlock();
	return err;
}

static int macsec_del_rxsa(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct macsec_rx_sa *rx_sa;
	u8 assoc_num;
	struct nlattr *tb_rxsc[MACSEC_RXSC_ATTR_MAX + 1];
	struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];
	int ret;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_sa_config(attrs, tb_sa))
		return -EINVAL;

	if (parse_rxsc_config(attrs, tb_rxsc))
		return -EINVAL;

	rtnl_lock();
	rx_sa = get_rxsa_from_nl(genl_info_net(info), attrs, tb_rxsc, tb_sa,
				 &dev, &secy, &rx_sc, &assoc_num);
	if (IS_ERR(rx_sa)) {
		rtnl_unlock();
		return PTR_ERR(rx_sa);
	}

	if (rx_sa->active) {
		rtnl_unlock();
		return -EBUSY;
	}

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.sa.assoc_num = assoc_num;
		ctx.sa.rx_sa = rx_sa;
		ctx.secy = secy;

		ret = macsec_offload(ops->mdo_del_rxsa, &ctx);
		if (ret)
			goto cleanup;
	}

	RCU_INIT_POINTER(rx_sc->sa[assoc_num], NULL);
	clear_rx_sa(rx_sa);

	rtnl_unlock();

	return 0;

cleanup:
	rtnl_unlock();
	return ret;
}

static int macsec_del_rxsc(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	sci_t sci;
	struct nlattr *tb_rxsc[MACSEC_RXSC_ATTR_MAX + 1];
	int ret;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_rxsc_config(attrs, tb_rxsc))
		return -EINVAL;

	if (!tb_rxsc[MACSEC_RXSC_ATTR_SCI])
		return -EINVAL;

	rtnl_lock();
	dev = get_dev_from_nl(genl_info_net(info), info->attrs);
	if (IS_ERR(dev)) {
		rtnl_unlock();
		return PTR_ERR(dev);
	}

	secy = &macsec_priv(dev)->secy;
	sci = nla_get_sci(tb_rxsc[MACSEC_RXSC_ATTR_SCI]);

	rx_sc = del_rx_sc(secy, sci);
	if (!rx_sc) {
		rtnl_unlock();
		return -ENODEV;
	}

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.rx_sc = rx_sc;
		ctx.secy = secy;
		ret = macsec_offload(ops->mdo_del_rxsc, &ctx);
		if (ret)
			goto cleanup;
	}

	free_rx_sc(rx_sc);
	rtnl_unlock();

	return 0;

cleanup:
	rtnl_unlock();
	return ret;
}

static int macsec_del_txsa(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_tx_sc *tx_sc;
	struct macsec_tx_sa *tx_sa;
	u8 assoc_num;
	struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];
	int ret;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_sa_config(attrs, tb_sa))
		return -EINVAL;

	rtnl_lock();
	tx_sa = get_txsa_from_nl(genl_info_net(info), attrs, tb_sa,
				 &dev, &secy, &tx_sc, &assoc_num);
	if (IS_ERR(tx_sa)) {
		rtnl_unlock();
		return PTR_ERR(tx_sa);
	}

	if (tx_sa->active) {
		rtnl_unlock();
		return -EBUSY;
	}

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.sa.assoc_num = assoc_num;
		ctx.sa.tx_sa = tx_sa;
		ctx.secy = secy;

		ret = macsec_offload(ops->mdo_del_txsa, &ctx);
		if (ret)
			goto cleanup;
	}

	RCU_INIT_POINTER(tx_sc->sa[assoc_num], NULL);
	clear_tx_sa(tx_sa);

	rtnl_unlock();

	return 0;

cleanup:
	rtnl_unlock();
	return ret;
}

static bool validate_upd_sa(struct nlattr **attrs)
{
	if (!attrs[MACSEC_SA_ATTR_AN] ||
	    attrs[MACSEC_SA_ATTR_KEY] ||
	    attrs[MACSEC_SA_ATTR_KEYID] ||
	    attrs[MACSEC_SA_ATTR_SSCI] ||
	    attrs[MACSEC_SA_ATTR_SALT])
		return false;

	if (nla_get_u8(attrs[MACSEC_SA_ATTR_AN]) >= MACSEC_NUM_AN)
		return false;

	if (attrs[MACSEC_SA_ATTR_PN] && nla_get_u64(attrs[MACSEC_SA_ATTR_PN]) == 0)
		return false;

	if (attrs[MACSEC_SA_ATTR_ACTIVE]) {
		if (nla_get_u8(attrs[MACSEC_SA_ATTR_ACTIVE]) > 1)
			return false;
	}

	return true;
}

static int macsec_upd_txsa(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_tx_sc *tx_sc;
	struct macsec_tx_sa *tx_sa;
	u8 assoc_num;
	struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];
	bool was_operational, was_active;
	pn_t prev_pn;
	int ret = 0;

	prev_pn.full64 = 0;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_sa_config(attrs, tb_sa))
		return -EINVAL;

	if (!validate_upd_sa(tb_sa))
		return -EINVAL;

	rtnl_lock();
	tx_sa = get_txsa_from_nl(genl_info_net(info), attrs, tb_sa,
				 &dev, &secy, &tx_sc, &assoc_num);
	if (IS_ERR(tx_sa)) {
		rtnl_unlock();
		return PTR_ERR(tx_sa);
	}

	if (tb_sa[MACSEC_SA_ATTR_PN]) {
		int pn_len;

		pn_len = secy->xpn ? MACSEC_XPN_PN_LEN : MACSEC_DEFAULT_PN_LEN;
		if (nla_len(tb_sa[MACSEC_SA_ATTR_PN]) != pn_len) {
			pr_notice("macsec: nl: upd_txsa: bad pn length: %d != %d\n",
				  nla_len(tb_sa[MACSEC_SA_ATTR_PN]), pn_len);
			rtnl_unlock();
			return -EINVAL;
		}

		spin_lock_bh(&tx_sa->lock);
		prev_pn = tx_sa->next_pn_halves;
		tx_sa->next_pn = nla_get_u64(tb_sa[MACSEC_SA_ATTR_PN]);
		spin_unlock_bh(&tx_sa->lock);
	}

	was_active = tx_sa->active;
	if (tb_sa[MACSEC_SA_ATTR_ACTIVE])
		tx_sa->active = nla_get_u8(tb_sa[MACSEC_SA_ATTR_ACTIVE]);

	was_operational = secy->operational;
	if (assoc_num == tx_sc->encoding_sa)
		secy->operational = tx_sa->active;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.sa.assoc_num = assoc_num;
		ctx.sa.tx_sa = tx_sa;
		ctx.secy = secy;

		ret = macsec_offload(ops->mdo_upd_txsa, &ctx);
		if (ret)
			goto cleanup;
	}

	rtnl_unlock();

	return 0;

cleanup:
	if (tb_sa[MACSEC_SA_ATTR_PN]) {
		spin_lock_bh(&tx_sa->lock);
		tx_sa->next_pn_halves = prev_pn;
		spin_unlock_bh(&tx_sa->lock);
	}
	tx_sa->active = was_active;
	secy->operational = was_operational;
	rtnl_unlock();
	return ret;
}

static int macsec_upd_rxsa(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct macsec_rx_sa *rx_sa;
	u8 assoc_num;
	struct nlattr *tb_rxsc[MACSEC_RXSC_ATTR_MAX + 1];
	struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];
	bool was_active;
	pn_t prev_pn;
	int ret = 0;

	prev_pn.full64 = 0;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_rxsc_config(attrs, tb_rxsc))
		return -EINVAL;

	if (parse_sa_config(attrs, tb_sa))
		return -EINVAL;

	if (!validate_upd_sa(tb_sa))
		return -EINVAL;

	rtnl_lock();
	rx_sa = get_rxsa_from_nl(genl_info_net(info), attrs, tb_rxsc, tb_sa,
				 &dev, &secy, &rx_sc, &assoc_num);
	if (IS_ERR(rx_sa)) {
		rtnl_unlock();
		return PTR_ERR(rx_sa);
	}

	if (tb_sa[MACSEC_SA_ATTR_PN]) {
		int pn_len;

		pn_len = secy->xpn ? MACSEC_XPN_PN_LEN : MACSEC_DEFAULT_PN_LEN;
		if (nla_len(tb_sa[MACSEC_SA_ATTR_PN]) != pn_len) {
			pr_notice("macsec: nl: upd_rxsa: bad pn length: %d != %d\n",
				  nla_len(tb_sa[MACSEC_SA_ATTR_PN]), pn_len);
			rtnl_unlock();
			return -EINVAL;
		}

		spin_lock_bh(&rx_sa->lock);
		prev_pn = rx_sa->next_pn_halves;
		rx_sa->next_pn = nla_get_u64(tb_sa[MACSEC_SA_ATTR_PN]);
		spin_unlock_bh(&rx_sa->lock);
	}

	was_active = rx_sa->active;
	if (tb_sa[MACSEC_SA_ATTR_ACTIVE])
		rx_sa->active = nla_get_u8(tb_sa[MACSEC_SA_ATTR_ACTIVE]);

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.sa.assoc_num = assoc_num;
		ctx.sa.rx_sa = rx_sa;
		ctx.secy = secy;

		ret = macsec_offload(ops->mdo_upd_rxsa, &ctx);
		if (ret)
			goto cleanup;
	}

	rtnl_unlock();
	return 0;

cleanup:
	if (tb_sa[MACSEC_SA_ATTR_PN]) {
		spin_lock_bh(&rx_sa->lock);
		rx_sa->next_pn_halves = prev_pn;
		spin_unlock_bh(&rx_sa->lock);
	}
	rx_sa->active = was_active;
	rtnl_unlock();
	return ret;
}

static int macsec_upd_rxsc(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	struct macsec_secy *secy;
	struct macsec_rx_sc *rx_sc;
	struct nlattr *tb_rxsc[MACSEC_RXSC_ATTR_MAX + 1];
	unsigned int prev_n_rx_sc;
	bool was_active;
	int ret;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (parse_rxsc_config(attrs, tb_rxsc))
		return -EINVAL;

	if (!validate_add_rxsc(tb_rxsc))
		return -EINVAL;

	rtnl_lock();
	rx_sc = get_rxsc_from_nl(genl_info_net(info), attrs, tb_rxsc, &dev, &secy);
	if (IS_ERR(rx_sc)) {
		rtnl_unlock();
		return PTR_ERR(rx_sc);
	}

	was_active = rx_sc->active;
	prev_n_rx_sc = secy->n_rx_sc;
	if (tb_rxsc[MACSEC_RXSC_ATTR_ACTIVE]) {
		bool new = !!nla_get_u8(tb_rxsc[MACSEC_RXSC_ATTR_ACTIVE]);

		if (rx_sc->active != new)
			secy->n_rx_sc += new ? 1 : -1;

		rx_sc->active = new;
	}

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(netdev_priv(dev))) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.rx_sc = rx_sc;
		ctx.secy = secy;

		ret = macsec_offload(ops->mdo_upd_rxsc, &ctx);
		if (ret)
			goto cleanup;
	}

	rtnl_unlock();

	return 0;

cleanup:
	secy->n_rx_sc = prev_n_rx_sc;
	rx_sc->active = was_active;
	rtnl_unlock();
	return ret;
}

static bool macsec_is_configured(struct macsec_dev *macsec)
{
	struct macsec_secy *secy = &macsec->secy;
	struct macsec_tx_sc *tx_sc = &secy->tx_sc;
	int i;

	if (secy->rx_sc)
		return true;

	for (i = 0; i < MACSEC_NUM_AN; i++)
		if (tx_sc->sa[i])
			return true;

	return false;
}

static int macsec_upd_offload(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb_offload[MACSEC_OFFLOAD_ATTR_MAX + 1];
	enum macsec_offload offload, prev_offload;
	int (*func)(struct macsec_context *ctx);
	struct nlattr **attrs = info->attrs;
	struct net_device *dev;
	const struct macsec_ops *ops;
	struct macsec_context ctx;
	struct macsec_dev *macsec;
	int ret;

	if (!attrs[MACSEC_ATTR_IFINDEX])
		return -EINVAL;

	if (!attrs[MACSEC_ATTR_OFFLOAD])
		return -EINVAL;

	if (nla_parse_nested_deprecated(tb_offload, MACSEC_OFFLOAD_ATTR_MAX,
					attrs[MACSEC_ATTR_OFFLOAD],
					macsec_genl_offload_policy, NULL))
		return -EINVAL;

	dev = get_dev_from_nl(genl_info_net(info), attrs);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	macsec = macsec_priv(dev);

	if (!tb_offload[MACSEC_OFFLOAD_ATTR_TYPE])
		return -EINVAL;

	offload = nla_get_u8(tb_offload[MACSEC_OFFLOAD_ATTR_TYPE]);
	if (macsec->offload == offload)
		return 0;

	/* Check if the offloading mode is supported by the underlying layers */
	if (offload != MACSEC_OFFLOAD_OFF &&
	    !macsec_check_offload(offload, macsec))
		return -EOPNOTSUPP;

	/* Check if the net device is busy. */
	if (netif_running(dev))
		return -EBUSY;

	rtnl_lock();

	prev_offload = macsec->offload;
	macsec->offload = offload;

	/* Check if the device already has rules configured: we do not support
	 * rules migration.
	 */
	if (macsec_is_configured(macsec)) {
		ret = -EBUSY;
		goto rollback;
	}

	ops = __macsec_get_ops(offload == MACSEC_OFFLOAD_OFF ? prev_offload : offload,
			       macsec, &ctx);
	if (!ops) {
		ret = -EOPNOTSUPP;
		goto rollback;
	}

	if (prev_offload == MACSEC_OFFLOAD_OFF)
		func = ops->mdo_add_secy;
	else
		func = ops->mdo_del_secy;

	ctx.secy = &macsec->secy;
	ret = macsec_offload(func, &ctx);
	if (ret)
		goto rollback;

	/* Force features update, since they are different for SW MACSec and
	 * HW offloading cases.
	 */
	netdev_update_features(dev);

	rtnl_unlock();
	return 0;

rollback:
	macsec->offload = prev_offload;

	rtnl_unlock();
	return ret;
}

static void get_tx_sa_stats(struct net_device *dev, int an,
			    struct macsec_tx_sa *tx_sa,
			    struct macsec_tx_sa_stats *sum)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	int cpu;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.sa.assoc_num = an;
			ctx.sa.tx_sa = tx_sa;
			ctx.stats.tx_sa_stats = sum;
			ctx.secy = &macsec_priv(dev)->secy;
			macsec_offload(ops->mdo_get_tx_sa_stats, &ctx);
		}
		return;
	}

	for_each_possible_cpu(cpu) {
		const struct macsec_tx_sa_stats *stats =
			per_cpu_ptr(tx_sa->stats, cpu);

		sum->OutPktsProtected += stats->OutPktsProtected;
		sum->OutPktsEncrypted += stats->OutPktsEncrypted;
	}
}

static int copy_tx_sa_stats(struct sk_buff *skb, struct macsec_tx_sa_stats *sum)
{
	if (nla_put_u32(skb, MACSEC_SA_STATS_ATTR_OUT_PKTS_PROTECTED,
			sum->OutPktsProtected) ||
	    nla_put_u32(skb, MACSEC_SA_STATS_ATTR_OUT_PKTS_ENCRYPTED,
			sum->OutPktsEncrypted))
		return -EMSGSIZE;

	return 0;
}

static void get_rx_sa_stats(struct net_device *dev,
			    struct macsec_rx_sc *rx_sc, int an,
			    struct macsec_rx_sa *rx_sa,
			    struct macsec_rx_sa_stats *sum)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	int cpu;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.sa.assoc_num = an;
			ctx.sa.rx_sa = rx_sa;
			ctx.stats.rx_sa_stats = sum;
			ctx.secy = &macsec_priv(dev)->secy;
			ctx.rx_sc = rx_sc;
			macsec_offload(ops->mdo_get_rx_sa_stats, &ctx);
		}
		return;
	}

	for_each_possible_cpu(cpu) {
		const struct macsec_rx_sa_stats *stats =
			per_cpu_ptr(rx_sa->stats, cpu);

		sum->InPktsOK         += stats->InPktsOK;
		sum->InPktsInvalid    += stats->InPktsInvalid;
		sum->InPktsNotValid   += stats->InPktsNotValid;
		sum->InPktsNotUsingSA += stats->InPktsNotUsingSA;
		sum->InPktsUnusedSA   += stats->InPktsUnusedSA;
	}
}

static int copy_rx_sa_stats(struct sk_buff *skb,
			    struct macsec_rx_sa_stats *sum)
{
	if (nla_put_u32(skb, MACSEC_SA_STATS_ATTR_IN_PKTS_OK, sum->InPktsOK) ||
	    nla_put_u32(skb, MACSEC_SA_STATS_ATTR_IN_PKTS_INVALID,
			sum->InPktsInvalid) ||
	    nla_put_u32(skb, MACSEC_SA_STATS_ATTR_IN_PKTS_NOT_VALID,
			sum->InPktsNotValid) ||
	    nla_put_u32(skb, MACSEC_SA_STATS_ATTR_IN_PKTS_NOT_USING_SA,
			sum->InPktsNotUsingSA) ||
	    nla_put_u32(skb, MACSEC_SA_STATS_ATTR_IN_PKTS_UNUSED_SA,
			sum->InPktsUnusedSA))
		return -EMSGSIZE;

	return 0;
}

static void get_rx_sc_stats(struct net_device *dev,
			    struct macsec_rx_sc *rx_sc,
			    struct macsec_rx_sc_stats *sum)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	int cpu;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.stats.rx_sc_stats = sum;
			ctx.secy = &macsec_priv(dev)->secy;
			ctx.rx_sc = rx_sc;
			macsec_offload(ops->mdo_get_rx_sc_stats, &ctx);
		}
		return;
	}

	for_each_possible_cpu(cpu) {
		const struct pcpu_rx_sc_stats *stats;
		struct macsec_rx_sc_stats tmp;
		unsigned int start;

		stats = per_cpu_ptr(rx_sc->stats, cpu);
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			memcpy(&tmp, &stats->stats, sizeof(tmp));
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		sum->InOctetsValidated += tmp.InOctetsValidated;
		sum->InOctetsDecrypted += tmp.InOctetsDecrypted;
		sum->InPktsUnchecked   += tmp.InPktsUnchecked;
		sum->InPktsDelayed     += tmp.InPktsDelayed;
		sum->InPktsOK          += tmp.InPktsOK;
		sum->InPktsInvalid     += tmp.InPktsInvalid;
		sum->InPktsLate        += tmp.InPktsLate;
		sum->InPktsNotValid    += tmp.InPktsNotValid;
		sum->InPktsNotUsingSA  += tmp.InPktsNotUsingSA;
		sum->InPktsUnusedSA    += tmp.InPktsUnusedSA;
	}
}

static int copy_rx_sc_stats(struct sk_buff *skb, struct macsec_rx_sc_stats *sum)
{
	if (nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_OCTETS_VALIDATED,
			      sum->InOctetsValidated,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_OCTETS_DECRYPTED,
			      sum->InOctetsDecrypted,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_UNCHECKED,
			      sum->InPktsUnchecked,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_DELAYED,
			      sum->InPktsDelayed,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_OK,
			      sum->InPktsOK,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_INVALID,
			      sum->InPktsInvalid,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_LATE,
			      sum->InPktsLate,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_NOT_VALID,
			      sum->InPktsNotValid,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_NOT_USING_SA,
			      sum->InPktsNotUsingSA,
			      MACSEC_RXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_RXSC_STATS_ATTR_IN_PKTS_UNUSED_SA,
			      sum->InPktsUnusedSA,
			      MACSEC_RXSC_STATS_ATTR_PAD))
		return -EMSGSIZE;

	return 0;
}

static void get_tx_sc_stats(struct net_device *dev,
			    struct macsec_tx_sc_stats *sum)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	int cpu;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.stats.tx_sc_stats = sum;
			ctx.secy = &macsec_priv(dev)->secy;
			macsec_offload(ops->mdo_get_tx_sc_stats, &ctx);
		}
		return;
	}

	for_each_possible_cpu(cpu) {
		const struct pcpu_tx_sc_stats *stats;
		struct macsec_tx_sc_stats tmp;
		unsigned int start;

		stats = per_cpu_ptr(macsec_priv(dev)->secy.tx_sc.stats, cpu);
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			memcpy(&tmp, &stats->stats, sizeof(tmp));
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		sum->OutPktsProtected   += tmp.OutPktsProtected;
		sum->OutPktsEncrypted   += tmp.OutPktsEncrypted;
		sum->OutOctetsProtected += tmp.OutOctetsProtected;
		sum->OutOctetsEncrypted += tmp.OutOctetsEncrypted;
	}
}

static int copy_tx_sc_stats(struct sk_buff *skb, struct macsec_tx_sc_stats *sum)
{
	if (nla_put_u64_64bit(skb, MACSEC_TXSC_STATS_ATTR_OUT_PKTS_PROTECTED,
			      sum->OutPktsProtected,
			      MACSEC_TXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_TXSC_STATS_ATTR_OUT_PKTS_ENCRYPTED,
			      sum->OutPktsEncrypted,
			      MACSEC_TXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_TXSC_STATS_ATTR_OUT_OCTETS_PROTECTED,
			      sum->OutOctetsProtected,
			      MACSEC_TXSC_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_TXSC_STATS_ATTR_OUT_OCTETS_ENCRYPTED,
			      sum->OutOctetsEncrypted,
			      MACSEC_TXSC_STATS_ATTR_PAD))
		return -EMSGSIZE;

	return 0;
}

static void get_secy_stats(struct net_device *dev, struct macsec_dev_stats *sum)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	int cpu;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.stats.dev_stats = sum;
			ctx.secy = &macsec_priv(dev)->secy;
			macsec_offload(ops->mdo_get_dev_stats, &ctx);
		}
		return;
	}

	for_each_possible_cpu(cpu) {
		const struct pcpu_secy_stats *stats;
		struct macsec_dev_stats tmp;
		unsigned int start;

		stats = per_cpu_ptr(macsec_priv(dev)->stats, cpu);
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			memcpy(&tmp, &stats->stats, sizeof(tmp));
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		sum->OutPktsUntagged  += tmp.OutPktsUntagged;
		sum->InPktsUntagged   += tmp.InPktsUntagged;
		sum->OutPktsTooLong   += tmp.OutPktsTooLong;
		sum->InPktsNoTag      += tmp.InPktsNoTag;
		sum->InPktsBadTag     += tmp.InPktsBadTag;
		sum->InPktsUnknownSCI += tmp.InPktsUnknownSCI;
		sum->InPktsNoSCI      += tmp.InPktsNoSCI;
		sum->InPktsOverrun    += tmp.InPktsOverrun;
	}
}

static int copy_secy_stats(struct sk_buff *skb, struct macsec_dev_stats *sum)
{
	if (nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_OUT_PKTS_UNTAGGED,
			      sum->OutPktsUntagged,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_IN_PKTS_UNTAGGED,
			      sum->InPktsUntagged,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_OUT_PKTS_TOO_LONG,
			      sum->OutPktsTooLong,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_IN_PKTS_NO_TAG,
			      sum->InPktsNoTag,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_IN_PKTS_BAD_TAG,
			      sum->InPktsBadTag,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_IN_PKTS_UNKNOWN_SCI,
			      sum->InPktsUnknownSCI,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_IN_PKTS_NO_SCI,
			      sum->InPktsNoSCI,
			      MACSEC_SECY_STATS_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_STATS_ATTR_IN_PKTS_OVERRUN,
			      sum->InPktsOverrun,
			      MACSEC_SECY_STATS_ATTR_PAD))
		return -EMSGSIZE;

	return 0;
}

static int nla_put_secy(struct macsec_secy *secy, struct sk_buff *skb)
{
	struct macsec_tx_sc *tx_sc = &secy->tx_sc;
	struct nlattr *secy_nest = nla_nest_start_noflag(skb,
							 MACSEC_ATTR_SECY);
	u64 csid;

	if (!secy_nest)
		return 1;

	switch (secy->key_len) {
	case MACSEC_GCM_AES_128_SAK_LEN:
		csid = secy->xpn ? MACSEC_CIPHER_ID_GCM_AES_XPN_128 : MACSEC_DEFAULT_CIPHER_ID;
		break;
	case MACSEC_GCM_AES_256_SAK_LEN:
		csid = secy->xpn ? MACSEC_CIPHER_ID_GCM_AES_XPN_256 : MACSEC_CIPHER_ID_GCM_AES_256;
		break;
	default:
		goto cancel;
	}

	if (nla_put_sci(skb, MACSEC_SECY_ATTR_SCI, secy->sci,
			MACSEC_SECY_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, MACSEC_SECY_ATTR_CIPHER_SUITE,
			      csid, MACSEC_SECY_ATTR_PAD) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_ICV_LEN, secy->icv_len) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_OPER, secy->operational) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_PROTECT, secy->protect_frames) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_REPLAY, secy->replay_protect) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_VALIDATE, secy->validate_frames) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_ENCRYPT, tx_sc->encrypt) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_INC_SCI, tx_sc->send_sci) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_ES, tx_sc->end_station) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_SCB, tx_sc->scb) ||
	    nla_put_u8(skb, MACSEC_SECY_ATTR_ENCODING_SA, tx_sc->encoding_sa))
		goto cancel;

	if (secy->replay_protect) {
		if (nla_put_u32(skb, MACSEC_SECY_ATTR_WINDOW, secy->replay_window))
			goto cancel;
	}

	nla_nest_end(skb, secy_nest);
	return 0;

cancel:
	nla_nest_cancel(skb, secy_nest);
	return 1;
}

static noinline_for_stack int
dump_secy(struct macsec_secy *secy, struct net_device *dev,
	  struct sk_buff *skb, struct netlink_callback *cb)
{
	struct macsec_tx_sc_stats tx_sc_stats = {0, };
	struct macsec_tx_sa_stats tx_sa_stats = {0, };
	struct macsec_rx_sc_stats rx_sc_stats = {0, };
	struct macsec_rx_sa_stats rx_sa_stats = {0, };
	struct macsec_dev *macsec = netdev_priv(dev);
	struct macsec_dev_stats dev_stats = {0, };
	struct macsec_tx_sc *tx_sc = &secy->tx_sc;
	struct nlattr *txsa_list, *rxsc_list;
	struct macsec_rx_sc *rx_sc;
	struct nlattr *attr;
	void *hdr;
	int i, j;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &macsec_fam, NLM_F_MULTI, MACSEC_CMD_GET_TXSC);
	if (!hdr)
		return -EMSGSIZE;

	genl_dump_check_consistent(cb, hdr);

	if (nla_put_u32(skb, MACSEC_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	attr = nla_nest_start_noflag(skb, MACSEC_ATTR_OFFLOAD);
	if (!attr)
		goto nla_put_failure;
	if (nla_put_u8(skb, MACSEC_OFFLOAD_ATTR_TYPE, macsec->offload))
		goto nla_put_failure;
	nla_nest_end(skb, attr);

	if (nla_put_secy(secy, skb))
		goto nla_put_failure;

	attr = nla_nest_start_noflag(skb, MACSEC_ATTR_TXSC_STATS);
	if (!attr)
		goto nla_put_failure;

	get_tx_sc_stats(dev, &tx_sc_stats);
	if (copy_tx_sc_stats(skb, &tx_sc_stats)) {
		nla_nest_cancel(skb, attr);
		goto nla_put_failure;
	}
	nla_nest_end(skb, attr);

	attr = nla_nest_start_noflag(skb, MACSEC_ATTR_SECY_STATS);
	if (!attr)
		goto nla_put_failure;
	get_secy_stats(dev, &dev_stats);
	if (copy_secy_stats(skb, &dev_stats)) {
		nla_nest_cancel(skb, attr);
		goto nla_put_failure;
	}
	nla_nest_end(skb, attr);

	txsa_list = nla_nest_start_noflag(skb, MACSEC_ATTR_TXSA_LIST);
	if (!txsa_list)
		goto nla_put_failure;
	for (i = 0, j = 1; i < MACSEC_NUM_AN; i++) {
		struct macsec_tx_sa *tx_sa = rtnl_dereference(tx_sc->sa[i]);
		struct nlattr *txsa_nest;
		u64 pn;
		int pn_len;

		if (!tx_sa)
			continue;

		txsa_nest = nla_nest_start_noflag(skb, j++);
		if (!txsa_nest) {
			nla_nest_cancel(skb, txsa_list);
			goto nla_put_failure;
		}

		attr = nla_nest_start_noflag(skb, MACSEC_SA_ATTR_STATS);
		if (!attr) {
			nla_nest_cancel(skb, txsa_nest);
			nla_nest_cancel(skb, txsa_list);
			goto nla_put_failure;
		}
		memset(&tx_sa_stats, 0, sizeof(tx_sa_stats));
		get_tx_sa_stats(dev, i, tx_sa, &tx_sa_stats);
		if (copy_tx_sa_stats(skb, &tx_sa_stats)) {
			nla_nest_cancel(skb, attr);
			nla_nest_cancel(skb, txsa_nest);
			nla_nest_cancel(skb, txsa_list);
			goto nla_put_failure;
		}
		nla_nest_end(skb, attr);

		if (secy->xpn) {
			pn = tx_sa->next_pn;
			pn_len = MACSEC_XPN_PN_LEN;
		} else {
			pn = tx_sa->next_pn_halves.lower;
			pn_len = MACSEC_DEFAULT_PN_LEN;
		}

		if (nla_put_u8(skb, MACSEC_SA_ATTR_AN, i) ||
		    nla_put(skb, MACSEC_SA_ATTR_PN, pn_len, &pn) ||
		    nla_put(skb, MACSEC_SA_ATTR_KEYID, MACSEC_KEYID_LEN, tx_sa->key.id) ||
		    (secy->xpn && nla_put_ssci(skb, MACSEC_SA_ATTR_SSCI, tx_sa->ssci)) ||
		    nla_put_u8(skb, MACSEC_SA_ATTR_ACTIVE, tx_sa->active)) {
			nla_nest_cancel(skb, txsa_nest);
			nla_nest_cancel(skb, txsa_list);
			goto nla_put_failure;
		}

		nla_nest_end(skb, txsa_nest);
	}
	nla_nest_end(skb, txsa_list);

	rxsc_list = nla_nest_start_noflag(skb, MACSEC_ATTR_RXSC_LIST);
	if (!rxsc_list)
		goto nla_put_failure;

	j = 1;
	for_each_rxsc_rtnl(secy, rx_sc) {
		int k;
		struct nlattr *rxsa_list;
		struct nlattr *rxsc_nest = nla_nest_start_noflag(skb, j++);

		if (!rxsc_nest) {
			nla_nest_cancel(skb, rxsc_list);
			goto nla_put_failure;
		}

		if (nla_put_u8(skb, MACSEC_RXSC_ATTR_ACTIVE, rx_sc->active) ||
		    nla_put_sci(skb, MACSEC_RXSC_ATTR_SCI, rx_sc->sci,
				MACSEC_RXSC_ATTR_PAD)) {
			nla_nest_cancel(skb, rxsc_nest);
			nla_nest_cancel(skb, rxsc_list);
			goto nla_put_failure;
		}

		attr = nla_nest_start_noflag(skb, MACSEC_RXSC_ATTR_STATS);
		if (!attr) {
			nla_nest_cancel(skb, rxsc_nest);
			nla_nest_cancel(skb, rxsc_list);
			goto nla_put_failure;
		}
		memset(&rx_sc_stats, 0, sizeof(rx_sc_stats));
		get_rx_sc_stats(dev, rx_sc, &rx_sc_stats);
		if (copy_rx_sc_stats(skb, &rx_sc_stats)) {
			nla_nest_cancel(skb, attr);
			nla_nest_cancel(skb, rxsc_nest);
			nla_nest_cancel(skb, rxsc_list);
			goto nla_put_failure;
		}
		nla_nest_end(skb, attr);

		rxsa_list = nla_nest_start_noflag(skb,
						  MACSEC_RXSC_ATTR_SA_LIST);
		if (!rxsa_list) {
			nla_nest_cancel(skb, rxsc_nest);
			nla_nest_cancel(skb, rxsc_list);
			goto nla_put_failure;
		}

		for (i = 0, k = 1; i < MACSEC_NUM_AN; i++) {
			struct macsec_rx_sa *rx_sa = rtnl_dereference(rx_sc->sa[i]);
			struct nlattr *rxsa_nest;
			u64 pn;
			int pn_len;

			if (!rx_sa)
				continue;

			rxsa_nest = nla_nest_start_noflag(skb, k++);
			if (!rxsa_nest) {
				nla_nest_cancel(skb, rxsa_list);
				nla_nest_cancel(skb, rxsc_nest);
				nla_nest_cancel(skb, rxsc_list);
				goto nla_put_failure;
			}

			attr = nla_nest_start_noflag(skb,
						     MACSEC_SA_ATTR_STATS);
			if (!attr) {
				nla_nest_cancel(skb, rxsa_list);
				nla_nest_cancel(skb, rxsc_nest);
				nla_nest_cancel(skb, rxsc_list);
				goto nla_put_failure;
			}
			memset(&rx_sa_stats, 0, sizeof(rx_sa_stats));
			get_rx_sa_stats(dev, rx_sc, i, rx_sa, &rx_sa_stats);
			if (copy_rx_sa_stats(skb, &rx_sa_stats)) {
				nla_nest_cancel(skb, attr);
				nla_nest_cancel(skb, rxsa_list);
				nla_nest_cancel(skb, rxsc_nest);
				nla_nest_cancel(skb, rxsc_list);
				goto nla_put_failure;
			}
			nla_nest_end(skb, attr);

			if (secy->xpn) {
				pn = rx_sa->next_pn;
				pn_len = MACSEC_XPN_PN_LEN;
			} else {
				pn = rx_sa->next_pn_halves.lower;
				pn_len = MACSEC_DEFAULT_PN_LEN;
			}

			if (nla_put_u8(skb, MACSEC_SA_ATTR_AN, i) ||
			    nla_put(skb, MACSEC_SA_ATTR_PN, pn_len, &pn) ||
			    nla_put(skb, MACSEC_SA_ATTR_KEYID, MACSEC_KEYID_LEN, rx_sa->key.id) ||
			    (secy->xpn && nla_put_ssci(skb, MACSEC_SA_ATTR_SSCI, rx_sa->ssci)) ||
			    nla_put_u8(skb, MACSEC_SA_ATTR_ACTIVE, rx_sa->active)) {
				nla_nest_cancel(skb, rxsa_nest);
				nla_nest_cancel(skb, rxsc_nest);
				nla_nest_cancel(skb, rxsc_list);
				goto nla_put_failure;
			}
			nla_nest_end(skb, rxsa_nest);
		}

		nla_nest_end(skb, rxsa_list);
		nla_nest_end(skb, rxsc_nest);
	}

	nla_nest_end(skb, rxsc_list);

	genlmsg_end(skb, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int macsec_generation = 1; /* protected by RTNL */

static int macsec_dump_txsc(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int dev_idx, d;

	dev_idx = cb->args[0];

	d = 0;
	rtnl_lock();

	cb->seq = macsec_generation;

	for_each_netdev(net, dev) {
		struct macsec_secy *secy;

		if (d < dev_idx)
			goto next;

		if (!netif_is_macsec(dev))
			goto next;

		secy = &macsec_priv(dev)->secy;
		if (dump_secy(secy, dev, skb, cb) < 0)
			goto done;
next:
		d++;
	}

done:
	rtnl_unlock();
	cb->args[0] = d;
	return skb->len;
}

static const struct genl_small_ops macsec_genl_ops[] = {
	{
		.cmd = MACSEC_CMD_GET_TXSC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = macsec_dump_txsc,
	},
	{
		.cmd = MACSEC_CMD_ADD_RXSC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_add_rxsc,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_DEL_RXSC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_del_rxsc,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_UPD_RXSC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_upd_rxsc,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_ADD_TXSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_add_txsa,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_DEL_TXSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_del_txsa,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_UPD_TXSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_upd_txsa,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_ADD_RXSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_add_rxsa,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_DEL_RXSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_del_rxsa,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_UPD_RXSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_upd_rxsa,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = MACSEC_CMD_UPD_OFFLOAD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = macsec_upd_offload,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family macsec_fam __ro_after_init = {
	.name		= MACSEC_GENL_NAME,
	.hdrsize	= 0,
	.version	= MACSEC_GENL_VERSION,
	.maxattr	= MACSEC_ATTR_MAX,
	.policy = macsec_genl_policy,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.small_ops	= macsec_genl_ops,
	.n_small_ops	= ARRAY_SIZE(macsec_genl_ops),
};

static netdev_tx_t macsec_start_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct macsec_dev *macsec = netdev_priv(dev);
	struct macsec_secy *secy = &macsec->secy;
	struct pcpu_secy_stats *secy_stats;
	int ret, len;

	if (macsec_is_offloaded(netdev_priv(dev))) {
		skb->dev = macsec->real_dev;
		return dev_queue_xmit(skb);
	}

	/* 10.5 */
	if (!secy->protect_frames) {
		secy_stats = this_cpu_ptr(macsec->stats);
		u64_stats_update_begin(&secy_stats->syncp);
		secy_stats->stats.OutPktsUntagged++;
		u64_stats_update_end(&secy_stats->syncp);
		skb->dev = macsec->real_dev;
		len = skb->len;
		ret = dev_queue_xmit(skb);
		count_tx(dev, ret, len);
		return ret;
	}

	if (!secy->operational) {
		kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	skb = macsec_encrypt(skb, dev);
	if (IS_ERR(skb)) {
		if (PTR_ERR(skb) != -EINPROGRESS)
			dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	macsec_count_tx(skb, &macsec->secy.tx_sc, macsec_skb_cb(skb)->tx_sa);

	macsec_encrypt_finish(skb, dev);
	len = skb->len;
	ret = dev_queue_xmit(skb);
	count_tx(dev, ret, len);
	return ret;
}

#define SW_MACSEC_FEATURES \
	(NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_FRAGLIST)

/* If h/w offloading is enabled, use real device features save for
 *   VLAN_FEATURES - they require additional ops
 *   HW_MACSEC - no reason to report it
 */
#define REAL_DEV_FEATURES(dev) \
	((dev)->features & ~(NETIF_F_VLAN_FEATURES | NETIF_F_HW_MACSEC))

static int macsec_dev_init(struct net_device *dev)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;
	int err;

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	err = gro_cells_init(&macsec->gro_cells, dev);
	if (err) {
		free_percpu(dev->tstats);
		return err;
	}

	if (macsec_is_offloaded(macsec)) {
		dev->features = REAL_DEV_FEATURES(real_dev);
	} else {
		dev->features = real_dev->features & SW_MACSEC_FEATURES;
		dev->features |= NETIF_F_LLTX | NETIF_F_GSO_SOFTWARE;
	}

	dev->needed_headroom = real_dev->needed_headroom +
			       MACSEC_NEEDED_HEADROOM;
	dev->needed_tailroom = real_dev->needed_tailroom +
			       MACSEC_NEEDED_TAILROOM;

	if (is_zero_ether_addr(dev->dev_addr))
		eth_hw_addr_inherit(dev, real_dev);
	if (is_zero_ether_addr(dev->broadcast))
		memcpy(dev->broadcast, real_dev->broadcast, dev->addr_len);

	return 0;
}

static void macsec_dev_uninit(struct net_device *dev)
{
	struct macsec_dev *macsec = macsec_priv(dev);

	gro_cells_destroy(&macsec->gro_cells);
	free_percpu(dev->tstats);
}

static netdev_features_t macsec_fix_features(struct net_device *dev,
					     netdev_features_t features)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;

	if (macsec_is_offloaded(macsec))
		return REAL_DEV_FEATURES(real_dev);

	features &= (real_dev->features & SW_MACSEC_FEATURES) |
		    NETIF_F_GSO_SOFTWARE | NETIF_F_SOFT_FEATURES;
	features |= NETIF_F_LLTX;

	return features;
}

static int macsec_dev_open(struct net_device *dev)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;
	int err;

	err = dev_uc_add(real_dev, dev->dev_addr);
	if (err < 0)
		return err;

	if (dev->flags & IFF_ALLMULTI) {
		err = dev_set_allmulti(real_dev, 1);
		if (err < 0)
			goto del_unicast;
	}

	if (dev->flags & IFF_PROMISC) {
		err = dev_set_promiscuity(real_dev, 1);
		if (err < 0)
			goto clear_allmulti;
	}

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			err = -EOPNOTSUPP;
			goto clear_allmulti;
		}

		ctx.secy = &macsec->secy;
		err = macsec_offload(ops->mdo_dev_open, &ctx);
		if (err)
			goto clear_allmulti;
	}

	if (netif_carrier_ok(real_dev))
		netif_carrier_on(dev);

	return 0;
clear_allmulti:
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, -1);
del_unicast:
	dev_uc_del(real_dev, dev->dev_addr);
	netif_carrier_off(dev);
	return err;
}

static int macsec_dev_stop(struct net_device *dev)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;

	netif_carrier_off(dev);

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.secy = &macsec->secy;
			macsec_offload(ops->mdo_dev_stop, &ctx);
		}
	}

	dev_mc_unsync(real_dev, dev);
	dev_uc_unsync(real_dev, dev);

	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, -1);

	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(real_dev, -1);

	dev_uc_del(real_dev, dev->dev_addr);

	return 0;
}

static void macsec_dev_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *real_dev = macsec_priv(dev)->real_dev;

	if (!(dev->flags & IFF_UP))
		return;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, dev->flags & IFF_ALLMULTI ? 1 : -1);

	if (change & IFF_PROMISC)
		dev_set_promiscuity(real_dev,
				    dev->flags & IFF_PROMISC ? 1 : -1);
}

static void macsec_dev_set_rx_mode(struct net_device *dev)
{
	struct net_device *real_dev = macsec_priv(dev)->real_dev;

	dev_mc_sync(real_dev, dev);
	dev_uc_sync(real_dev, dev);
}

static int macsec_set_mac_address(struct net_device *dev, void *p)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!(dev->flags & IFF_UP))
		goto out;

	err = dev_uc_add(real_dev, addr->sa_data);
	if (err < 0)
		return err;

	dev_uc_del(real_dev, dev->dev_addr);

out:
	eth_hw_addr_set(dev, addr->sa_data);

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.secy = &macsec->secy;
			macsec_offload(ops->mdo_upd_secy, &ctx);
		}
	}

	return 0;
}

static int macsec_change_mtu(struct net_device *dev, int new_mtu)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	unsigned int extra = macsec->secy.icv_len + macsec_extra_len(true);

	if (macsec->real_dev->mtu - extra < new_mtu)
		return -ERANGE;

	dev->mtu = new_mtu;

	return 0;
}

static void macsec_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *s)
{
	if (!dev->tstats)
		return;

	dev_fetch_sw_netstats(s, dev->tstats);

	s->rx_dropped = dev->stats.rx_dropped;
	s->tx_dropped = dev->stats.tx_dropped;
}

static int macsec_get_iflink(const struct net_device *dev)
{
	return macsec_priv(dev)->real_dev->ifindex;
}

static const struct net_device_ops macsec_netdev_ops = {
	.ndo_init		= macsec_dev_init,
	.ndo_uninit		= macsec_dev_uninit,
	.ndo_open		= macsec_dev_open,
	.ndo_stop		= macsec_dev_stop,
	.ndo_fix_features	= macsec_fix_features,
	.ndo_change_mtu		= macsec_change_mtu,
	.ndo_set_rx_mode	= macsec_dev_set_rx_mode,
	.ndo_change_rx_flags	= macsec_dev_change_rx_flags,
	.ndo_set_mac_address	= macsec_set_mac_address,
	.ndo_start_xmit		= macsec_start_xmit,
	.ndo_get_stats64	= macsec_get_stats64,
	.ndo_get_iflink		= macsec_get_iflink,
};

static const struct device_type macsec_type = {
	.name = "macsec",
};

static const struct nla_policy macsec_rtnl_policy[IFLA_MACSEC_MAX + 1] = {
	[IFLA_MACSEC_SCI] = { .type = NLA_U64 },
	[IFLA_MACSEC_PORT] = { .type = NLA_U16 },
	[IFLA_MACSEC_ICV_LEN] = { .type = NLA_U8 },
	[IFLA_MACSEC_CIPHER_SUITE] = { .type = NLA_U64 },
	[IFLA_MACSEC_WINDOW] = { .type = NLA_U32 },
	[IFLA_MACSEC_ENCODING_SA] = { .type = NLA_U8 },
	[IFLA_MACSEC_ENCRYPT] = { .type = NLA_U8 },
	[IFLA_MACSEC_PROTECT] = { .type = NLA_U8 },
	[IFLA_MACSEC_INC_SCI] = { .type = NLA_U8 },
	[IFLA_MACSEC_ES] = { .type = NLA_U8 },
	[IFLA_MACSEC_SCB] = { .type = NLA_U8 },
	[IFLA_MACSEC_REPLAY_PROTECT] = { .type = NLA_U8 },
	[IFLA_MACSEC_VALIDATION] = { .type = NLA_U8 },
};

static void macsec_free_netdev(struct net_device *dev)
{
	struct macsec_dev *macsec = macsec_priv(dev);

	free_percpu(macsec->stats);
	free_percpu(macsec->secy.tx_sc.stats);

}

static void macsec_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->min_mtu = 0;
	dev->max_mtu = ETH_MAX_MTU;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->netdev_ops = &macsec_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = macsec_free_netdev;
	SET_NETDEV_DEVTYPE(dev, &macsec_type);

	eth_zero_addr(dev->broadcast);
}

static int macsec_changelink_common(struct net_device *dev,
				    struct nlattr *data[])
{
	struct macsec_secy *secy;
	struct macsec_tx_sc *tx_sc;

	secy = &macsec_priv(dev)->secy;
	tx_sc = &secy->tx_sc;

	if (data[IFLA_MACSEC_ENCODING_SA]) {
		struct macsec_tx_sa *tx_sa;

		tx_sc->encoding_sa = nla_get_u8(data[IFLA_MACSEC_ENCODING_SA]);
		tx_sa = rtnl_dereference(tx_sc->sa[tx_sc->encoding_sa]);

		secy->operational = tx_sa && tx_sa->active;
	}

	if (data[IFLA_MACSEC_ENCRYPT])
		tx_sc->encrypt = !!nla_get_u8(data[IFLA_MACSEC_ENCRYPT]);

	if (data[IFLA_MACSEC_PROTECT])
		secy->protect_frames = !!nla_get_u8(data[IFLA_MACSEC_PROTECT]);

	if (data[IFLA_MACSEC_INC_SCI])
		tx_sc->send_sci = !!nla_get_u8(data[IFLA_MACSEC_INC_SCI]);

	if (data[IFLA_MACSEC_ES])
		tx_sc->end_station = !!nla_get_u8(data[IFLA_MACSEC_ES]);

	if (data[IFLA_MACSEC_SCB])
		tx_sc->scb = !!nla_get_u8(data[IFLA_MACSEC_SCB]);

	if (data[IFLA_MACSEC_REPLAY_PROTECT])
		secy->replay_protect = !!nla_get_u8(data[IFLA_MACSEC_REPLAY_PROTECT]);

	if (data[IFLA_MACSEC_VALIDATION])
		secy->validate_frames = nla_get_u8(data[IFLA_MACSEC_VALIDATION]);

	if (data[IFLA_MACSEC_CIPHER_SUITE]) {
		switch (nla_get_u64(data[IFLA_MACSEC_CIPHER_SUITE])) {
		case MACSEC_CIPHER_ID_GCM_AES_128:
		case MACSEC_DEFAULT_CIPHER_ID:
			secy->key_len = MACSEC_GCM_AES_128_SAK_LEN;
			secy->xpn = false;
			break;
		case MACSEC_CIPHER_ID_GCM_AES_256:
			secy->key_len = MACSEC_GCM_AES_256_SAK_LEN;
			secy->xpn = false;
			break;
		case MACSEC_CIPHER_ID_GCM_AES_XPN_128:
			secy->key_len = MACSEC_GCM_AES_128_SAK_LEN;
			secy->xpn = true;
			break;
		case MACSEC_CIPHER_ID_GCM_AES_XPN_256:
			secy->key_len = MACSEC_GCM_AES_256_SAK_LEN;
			secy->xpn = true;
			break;
		default:
			return -EINVAL;
		}
	}

	if (data[IFLA_MACSEC_WINDOW]) {
		secy->replay_window = nla_get_u32(data[IFLA_MACSEC_WINDOW]);

		/* IEEE 802.1AEbw-2013 10.7.8 - maximum replay window
		 * for XPN cipher suites */
		if (secy->xpn &&
		    secy->replay_window > MACSEC_XPN_MAX_REPLAY_WINDOW)
			return -EINVAL;
	}

	return 0;
}

static int macsec_changelink(struct net_device *dev, struct nlattr *tb[],
			     struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct macsec_tx_sc tx_sc;
	struct macsec_secy secy;
	int ret;

	if (!data)
		return 0;

	if (data[IFLA_MACSEC_CIPHER_SUITE] ||
	    data[IFLA_MACSEC_ICV_LEN] ||
	    data[IFLA_MACSEC_SCI] ||
	    data[IFLA_MACSEC_PORT])
		return -EINVAL;

	/* Keep a copy of unmodified secy and tx_sc, in case the offload
	 * propagation fails, to revert macsec_changelink_common.
	 */
	memcpy(&secy, &macsec->secy, sizeof(secy));
	memcpy(&tx_sc, &macsec->secy.tx_sc, sizeof(tx_sc));

	ret = macsec_changelink_common(dev, data);
	if (ret)
		goto cleanup;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;
		int ret;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (!ops) {
			ret = -EOPNOTSUPP;
			goto cleanup;
		}

		ctx.secy = &macsec->secy;
		ret = macsec_offload(ops->mdo_upd_secy, &ctx);
		if (ret)
			goto cleanup;
	}

	return 0;

cleanup:
	memcpy(&macsec->secy.tx_sc, &tx_sc, sizeof(tx_sc));
	memcpy(&macsec->secy, &secy, sizeof(secy));

	return ret;
}

static void macsec_del_dev(struct macsec_dev *macsec)
{
	int i;

	while (macsec->secy.rx_sc) {
		struct macsec_rx_sc *rx_sc = rtnl_dereference(macsec->secy.rx_sc);

		rcu_assign_pointer(macsec->secy.rx_sc, rx_sc->next);
		free_rx_sc(rx_sc);
	}

	for (i = 0; i < MACSEC_NUM_AN; i++) {
		struct macsec_tx_sa *sa = rtnl_dereference(macsec->secy.tx_sc.sa[i]);

		if (sa) {
			RCU_INIT_POINTER(macsec->secy.tx_sc.sa[i], NULL);
			clear_tx_sa(sa);
		}
	}
}

static void macsec_common_dellink(struct net_device *dev, struct list_head *head)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(netdev_priv(dev), &ctx);
		if (ops) {
			ctx.secy = &macsec->secy;
			macsec_offload(ops->mdo_del_secy, &ctx);
		}
	}

	unregister_netdevice_queue(dev, head);
	list_del_rcu(&macsec->secys);
	macsec_del_dev(macsec);
	netdev_upper_dev_unlink(real_dev, dev);

	macsec_generation++;
}

static void macsec_dellink(struct net_device *dev, struct list_head *head)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct net_device *real_dev = macsec->real_dev;
	struct macsec_rxh_data *rxd = macsec_data_rtnl(real_dev);

	macsec_common_dellink(dev, head);

	if (list_empty(&rxd->secys)) {
		netdev_rx_handler_unregister(real_dev);
		kfree(rxd);
	}
}

static int register_macsec_dev(struct net_device *real_dev,
			       struct net_device *dev)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct macsec_rxh_data *rxd = macsec_data_rtnl(real_dev);

	if (!rxd) {
		int err;

		rxd = kmalloc(sizeof(*rxd), GFP_KERNEL);
		if (!rxd)
			return -ENOMEM;

		INIT_LIST_HEAD(&rxd->secys);

		err = netdev_rx_handler_register(real_dev, macsec_handle_frame,
						 rxd);
		if (err < 0) {
			kfree(rxd);
			return err;
		}
	}

	list_add_tail_rcu(&macsec->secys, &rxd->secys);
	return 0;
}

static bool sci_exists(struct net_device *dev, sci_t sci)
{
	struct macsec_rxh_data *rxd = macsec_data_rtnl(dev);
	struct macsec_dev *macsec;

	list_for_each_entry(macsec, &rxd->secys, secys) {
		if (macsec->secy.sci == sci)
			return true;
	}

	return false;
}

static sci_t dev_to_sci(struct net_device *dev, __be16 port)
{
	return make_sci(dev->dev_addr, port);
}

static int macsec_add_dev(struct net_device *dev, sci_t sci, u8 icv_len)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	struct macsec_secy *secy = &macsec->secy;

	macsec->stats = netdev_alloc_pcpu_stats(struct pcpu_secy_stats);
	if (!macsec->stats)
		return -ENOMEM;

	secy->tx_sc.stats = netdev_alloc_pcpu_stats(struct pcpu_tx_sc_stats);
	if (!secy->tx_sc.stats) {
		free_percpu(macsec->stats);
		return -ENOMEM;
	}

	if (sci == MACSEC_UNDEF_SCI)
		sci = dev_to_sci(dev, MACSEC_PORT_ES);

	secy->netdev = dev;
	secy->operational = true;
	secy->key_len = DEFAULT_SAK_LEN;
	secy->icv_len = icv_len;
	secy->validate_frames = MACSEC_VALIDATE_DEFAULT;
	secy->protect_frames = true;
	secy->replay_protect = false;
	secy->xpn = DEFAULT_XPN;

	secy->sci = sci;
	secy->tx_sc.active = true;
	secy->tx_sc.encoding_sa = DEFAULT_ENCODING_SA;
	secy->tx_sc.encrypt = DEFAULT_ENCRYPT;
	secy->tx_sc.send_sci = DEFAULT_SEND_SCI;
	secy->tx_sc.end_station = false;
	secy->tx_sc.scb = false;

	return 0;
}

static struct lock_class_key macsec_netdev_addr_lock_key;

static int macsec_newlink(struct net *net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	struct macsec_dev *macsec = macsec_priv(dev);
	rx_handler_func_t *rx_handler;
	u8 icv_len = DEFAULT_ICV_LEN;
	struct net_device *real_dev;
	int err, mtu;
	sci_t sci;

	if (!tb[IFLA_LINK])
		return -EINVAL;
	real_dev = __dev_get_by_index(net, nla_get_u32(tb[IFLA_LINK]));
	if (!real_dev)
		return -ENODEV;
	if (real_dev->type != ARPHRD_ETHER)
		return -EINVAL;

	dev->priv_flags |= IFF_MACSEC;

	macsec->real_dev = real_dev;

	if (data && data[IFLA_MACSEC_OFFLOAD])
		macsec->offload = nla_get_offload(data[IFLA_MACSEC_OFFLOAD]);
	else
		/* MACsec offloading is off by default */
		macsec->offload = MACSEC_OFFLOAD_OFF;

	/* Check if the offloading mode is supported by the underlying layers */
	if (macsec->offload != MACSEC_OFFLOAD_OFF &&
	    !macsec_check_offload(macsec->offload, macsec))
		return -EOPNOTSUPP;

	/* send_sci must be set to true when transmit sci explicitly is set */
	if ((data && data[IFLA_MACSEC_SCI]) &&
	    (data && data[IFLA_MACSEC_INC_SCI])) {
		u8 send_sci = !!nla_get_u8(data[IFLA_MACSEC_INC_SCI]);

		if (!send_sci)
			return -EINVAL;
	}

	if (data && data[IFLA_MACSEC_ICV_LEN])
		icv_len = nla_get_u8(data[IFLA_MACSEC_ICV_LEN]);
	mtu = real_dev->mtu - icv_len - macsec_extra_len(true);
	if (mtu < 0)
		dev->mtu = 0;
	else
		dev->mtu = mtu;

	rx_handler = rtnl_dereference(real_dev->rx_handler);
	if (rx_handler && rx_handler != macsec_handle_frame)
		return -EBUSY;

	err = register_netdevice(dev);
	if (err < 0)
		return err;

	netdev_lockdep_set_classes(dev);
	lockdep_set_class(&dev->addr_list_lock,
			  &macsec_netdev_addr_lock_key);

	err = netdev_upper_dev_link(real_dev, dev, extack);
	if (err < 0)
		goto unregister;

	/* need to be already registered so that ->init has run and
	 * the MAC addr is set
	 */
	if (data && data[IFLA_MACSEC_SCI])
		sci = nla_get_sci(data[IFLA_MACSEC_SCI]);
	else if (data && data[IFLA_MACSEC_PORT])
		sci = dev_to_sci(dev, nla_get_be16(data[IFLA_MACSEC_PORT]));
	else
		sci = dev_to_sci(dev, MACSEC_PORT_ES);

	if (rx_handler && sci_exists(real_dev, sci)) {
		err = -EBUSY;
		goto unlink;
	}

	err = macsec_add_dev(dev, sci, icv_len);
	if (err)
		goto unlink;

	if (data) {
		err = macsec_changelink_common(dev, data);
		if (err)
			goto del_dev;
	}

	/* If h/w offloading is available, propagate to the device */
	if (macsec_is_offloaded(macsec)) {
		const struct macsec_ops *ops;
		struct macsec_context ctx;

		ops = macsec_get_ops(macsec, &ctx);
		if (ops) {
			ctx.secy = &macsec->secy;
			err = macsec_offload(ops->mdo_add_secy, &ctx);
			if (err)
				goto del_dev;
		}
	}

	err = register_macsec_dev(real_dev, dev);
	if (err < 0)
		goto del_dev;

	netif_stacked_transfer_operstate(real_dev, dev);
	linkwatch_fire_event(dev);

	macsec_generation++;

	return 0;

del_dev:
	macsec_del_dev(macsec);
unlink:
	netdev_upper_dev_unlink(real_dev, dev);
unregister:
	unregister_netdevice(dev);
	return err;
}

static int macsec_validate_attr(struct nlattr *tb[], struct nlattr *data[],
				struct netlink_ext_ack *extack)
{
	u64 csid = MACSEC_DEFAULT_CIPHER_ID;
	u8 icv_len = DEFAULT_ICV_LEN;
	int flag;
	bool es, scb, sci;

	if (!data)
		return 0;

	if (data[IFLA_MACSEC_CIPHER_SUITE])
		csid = nla_get_u64(data[IFLA_MACSEC_CIPHER_SUITE]);

	if (data[IFLA_MACSEC_ICV_LEN]) {
		icv_len = nla_get_u8(data[IFLA_MACSEC_ICV_LEN]);
		if (icv_len != DEFAULT_ICV_LEN) {
			char dummy_key[DEFAULT_SAK_LEN] = { 0 };
			struct crypto_aead *dummy_tfm;

			dummy_tfm = macsec_alloc_tfm(dummy_key,
						     DEFAULT_SAK_LEN,
						     icv_len);
			if (IS_ERR(dummy_tfm))
				return PTR_ERR(dummy_tfm);
			crypto_free_aead(dummy_tfm);
		}
	}

	switch (csid) {
	case MACSEC_CIPHER_ID_GCM_AES_128:
	case MACSEC_CIPHER_ID_GCM_AES_256:
	case MACSEC_CIPHER_ID_GCM_AES_XPN_128:
	case MACSEC_CIPHER_ID_GCM_AES_XPN_256:
	case MACSEC_DEFAULT_CIPHER_ID:
		if (icv_len < MACSEC_MIN_ICV_LEN ||
		    icv_len > MACSEC_STD_ICV_LEN)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (data[IFLA_MACSEC_ENCODING_SA]) {
		if (nla_get_u8(data[IFLA_MACSEC_ENCODING_SA]) >= MACSEC_NUM_AN)
			return -EINVAL;
	}

	for (flag = IFLA_MACSEC_ENCODING_SA + 1;
	     flag < IFLA_MACSEC_VALIDATION;
	     flag++) {
		if (data[flag]) {
			if (nla_get_u8(data[flag]) > 1)
				return -EINVAL;
		}
	}

	es  = data[IFLA_MACSEC_ES] ? nla_get_u8(data[IFLA_MACSEC_ES]) : false;
	sci = data[IFLA_MACSEC_INC_SCI] ? nla_get_u8(data[IFLA_MACSEC_INC_SCI]) : false;
	scb = data[IFLA_MACSEC_SCB] ? nla_get_u8(data[IFLA_MACSEC_SCB]) : false;

	if ((sci && (scb || es)) || (scb && es))
		return -EINVAL;

	if (data[IFLA_MACSEC_VALIDATION] &&
	    nla_get_u8(data[IFLA_MACSEC_VALIDATION]) > MACSEC_VALIDATE_MAX)
		return -EINVAL;

	if ((data[IFLA_MACSEC_REPLAY_PROTECT] &&
	     nla_get_u8(data[IFLA_MACSEC_REPLAY_PROTECT])) &&
	    !data[IFLA_MACSEC_WINDOW])
		return -EINVAL;

	return 0;
}

static struct net *macsec_get_link_net(const struct net_device *dev)
{
	return dev_net(macsec_priv(dev)->real_dev);
}

static size_t macsec_get_size(const struct net_device *dev)
{
	return  nla_total_size_64bit(8) + /* IFLA_MACSEC_SCI */
		nla_total_size(1) + /* IFLA_MACSEC_ICV_LEN */
		nla_total_size_64bit(8) + /* IFLA_MACSEC_CIPHER_SUITE */
		nla_total_size(4) + /* IFLA_MACSEC_WINDOW */
		nla_total_size(1) + /* IFLA_MACSEC_ENCODING_SA */
		nla_total_size(1) + /* IFLA_MACSEC_ENCRYPT */
		nla_total_size(1) + /* IFLA_MACSEC_PROTECT */
		nla_total_size(1) + /* IFLA_MACSEC_INC_SCI */
		nla_total_size(1) + /* IFLA_MACSEC_ES */
		nla_total_size(1) + /* IFLA_MACSEC_SCB */
		nla_total_size(1) + /* IFLA_MACSEC_REPLAY_PROTECT */
		nla_total_size(1) + /* IFLA_MACSEC_VALIDATION */
		0;
}

static int macsec_fill_info(struct sk_buff *skb,
			    const struct net_device *dev)
{
	struct macsec_secy *secy = &macsec_priv(dev)->secy;
	struct macsec_tx_sc *tx_sc = &secy->tx_sc;
	u64 csid;

	switch (secy->key_len) {
	case MACSEC_GCM_AES_128_SAK_LEN:
		csid = secy->xpn ? MACSEC_CIPHER_ID_GCM_AES_XPN_128 : MACSEC_DEFAULT_CIPHER_ID;
		break;
	case MACSEC_GCM_AES_256_SAK_LEN:
		csid = secy->xpn ? MACSEC_CIPHER_ID_GCM_AES_XPN_256 : MACSEC_CIPHER_ID_GCM_AES_256;
		break;
	default:
		goto nla_put_failure;
	}

	if (nla_put_sci(skb, IFLA_MACSEC_SCI, secy->sci,
			IFLA_MACSEC_PAD) ||
	    nla_put_u8(skb, IFLA_MACSEC_ICV_LEN, secy->icv_len) ||
	    nla_put_u64_64bit(skb, IFLA_MACSEC_CIPHER_SUITE,
			      csid, IFLA_MACSEC_PAD) ||
	    nla_put_u8(skb, IFLA_MACSEC_ENCODING_SA, tx_sc->encoding_sa) ||
	    nla_put_u8(skb, IFLA_MACSEC_ENCRYPT, tx_sc->encrypt) ||
	    nla_put_u8(skb, IFLA_MACSEC_PROTECT, secy->protect_frames) ||
	    nla_put_u8(skb, IFLA_MACSEC_INC_SCI, tx_sc->send_sci) ||
	    nla_put_u8(skb, IFLA_MACSEC_ES, tx_sc->end_station) ||
	    nla_put_u8(skb, IFLA_MACSEC_SCB, tx_sc->scb) ||
	    nla_put_u8(skb, IFLA_MACSEC_REPLAY_PROTECT, secy->replay_protect) ||
	    nla_put_u8(skb, IFLA_MACSEC_VALIDATION, secy->validate_frames) ||
	    0)
		goto nla_put_failure;

	if (secy->replay_protect) {
		if (nla_put_u32(skb, IFLA_MACSEC_WINDOW, secy->replay_window))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops macsec_link_ops __read_mostly = {
	.kind		= "macsec",
	.priv_size	= sizeof(struct macsec_dev),
	.maxtype	= IFLA_MACSEC_MAX,
	.policy		= macsec_rtnl_policy,
	.setup		= macsec_setup,
	.validate	= macsec_validate_attr,
	.newlink	= macsec_newlink,
	.changelink	= macsec_changelink,
	.dellink	= macsec_dellink,
	.get_size	= macsec_get_size,
	.fill_info	= macsec_fill_info,
	.get_link_net	= macsec_get_link_net,
};

static bool is_macsec_master(struct net_device *dev)
{
	return rcu_access_pointer(dev->rx_handler) == macsec_handle_frame;
}

static int macsec_notify(struct notifier_block *this, unsigned long event,
			 void *ptr)
{
	struct net_device *real_dev = netdev_notifier_info_to_dev(ptr);
	LIST_HEAD(head);

	if (!is_macsec_master(real_dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_UP:
	case NETDEV_CHANGE: {
		struct macsec_dev *m, *n;
		struct macsec_rxh_data *rxd;

		rxd = macsec_data_rtnl(real_dev);
		list_for_each_entry_safe(m, n, &rxd->secys, secys) {
			struct net_device *dev = m->secy.netdev;

			netif_stacked_transfer_operstate(real_dev, dev);
		}
		break;
	}
	case NETDEV_UNREGISTER: {
		struct macsec_dev *m, *n;
		struct macsec_rxh_data *rxd;

		rxd = macsec_data_rtnl(real_dev);
		list_for_each_entry_safe(m, n, &rxd->secys, secys) {
			macsec_common_dellink(m->secy.netdev, &head);
		}

		netdev_rx_handler_unregister(real_dev);
		kfree(rxd);

		unregister_netdevice_many(&head);
		break;
	}
	case NETDEV_CHANGEMTU: {
		struct macsec_dev *m;
		struct macsec_rxh_data *rxd;

		rxd = macsec_data_rtnl(real_dev);
		list_for_each_entry(m, &rxd->secys, secys) {
			struct net_device *dev = m->secy.netdev;
			unsigned int mtu = real_dev->mtu - (m->secy.icv_len +
							    macsec_extra_len(true));

			if (dev->mtu > mtu)
				dev_set_mtu(dev, mtu);
		}
	}
	}

	return NOTIFY_OK;
}

static struct notifier_block macsec_notifier = {
	.notifier_call = macsec_notify,
};

static int __init macsec_init(void)
{
	int err;

	pr_info("MACsec IEEE 802.1AE\n");
	err = register_netdevice_notifier(&macsec_notifier);
	if (err)
		return err;

	err = rtnl_link_register(&macsec_link_ops);
	if (err)
		goto notifier;

	err = genl_register_family(&macsec_fam);
	if (err)
		goto rtnl;

	return 0;

rtnl:
	rtnl_link_unregister(&macsec_link_ops);
notifier:
	unregister_netdevice_notifier(&macsec_notifier);
	return err;
}

static void __exit macsec_exit(void)
{
	genl_unregister_family(&macsec_fam);
	rtnl_link_unregister(&macsec_link_ops);
	unregister_netdevice_notifier(&macsec_notifier);
	rcu_barrier();
}

module_init(macsec_init);
module_exit(macsec_exit);

MODULE_ALIAS_RTNL_LINK("macsec");
MODULE_ALIAS_GENL_FAMILY("macsec");

MODULE_DESCRIPTION("MACsec IEEE 802.1AE");
MODULE_LICENSE("GPL v2");
