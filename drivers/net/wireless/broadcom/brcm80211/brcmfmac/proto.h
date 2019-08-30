/*
 * Copyright (c) 2013 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef BRCMFMAC_PROTO_H
#define BRCMFMAC_PROTO_H


enum proto_addr_mode {
	ADDR_INDIRECT	= 0,
	ADDR_DIRECT
};

struct brcmf_skb_reorder_data {
	u8 *reorder;
};

struct brcmf_proto {
	int (*hdrpull)(struct brcmf_pub *drvr, bool do_fws,
		       struct sk_buff *skb, struct brcmf_if **ifp);
	int (*query_dcmd)(struct brcmf_pub *drvr, int ifidx, uint cmd,
			  void *buf, uint len, int *fwerr);
	int (*set_dcmd)(struct brcmf_pub *drvr, int ifidx, uint cmd, void *buf,
			uint len, int *fwerr);
	int (*tx_queue_data)(struct brcmf_pub *drvr, int ifidx,
			     struct sk_buff *skb);
	int (*txdata)(struct brcmf_pub *drvr, int ifidx, u8 offset,
		      struct sk_buff *skb);
	void (*configure_addr_mode)(struct brcmf_pub *drvr, int ifidx,
				    enum proto_addr_mode addr_mode);
	void (*delete_peer)(struct brcmf_pub *drvr, int ifidx,
			    u8 peer[ETH_ALEN]);
	void (*add_tdls_peer)(struct brcmf_pub *drvr, int ifidx,
			      u8 peer[ETH_ALEN]);
	void (*rxreorder)(struct brcmf_if *ifp, struct sk_buff *skb);
	void (*add_if)(struct brcmf_if *ifp);
	void (*del_if)(struct brcmf_if *ifp);
	void (*reset_if)(struct brcmf_if *ifp);
	int (*init_done)(struct brcmf_pub *drvr);
	void (*debugfs_create)(struct brcmf_pub *drvr);
	void *pd;
};


int brcmf_proto_attach(struct brcmf_pub *drvr);
void brcmf_proto_detach_pre_delif(struct brcmf_pub *drvr);
void brcmf_proto_detach_post_delif(struct brcmf_pub *drvr);

static inline int brcmf_proto_hdrpull(struct brcmf_pub *drvr, bool do_fws,
				      struct sk_buff *skb,
				      struct brcmf_if **ifp)
{
	struct brcmf_if *tmp = NULL;

	/* assure protocol is always called with
	 * non-null initialized pointer.
	 */
	if (ifp)
		*ifp = NULL;
	else
		ifp = &tmp;
	return drvr->proto->hdrpull(drvr, do_fws, skb, ifp);
}
static inline int brcmf_proto_query_dcmd(struct brcmf_pub *drvr, int ifidx,
					 uint cmd, void *buf, uint len,
					 int *fwerr)
{
	return drvr->proto->query_dcmd(drvr, ifidx, cmd, buf, len,fwerr);
}
static inline int brcmf_proto_set_dcmd(struct brcmf_pub *drvr, int ifidx,
				       uint cmd, void *buf, uint len,
				       int *fwerr)
{
	return drvr->proto->set_dcmd(drvr, ifidx, cmd, buf, len, fwerr);
}

static inline int brcmf_proto_tx_queue_data(struct brcmf_pub *drvr, int ifidx,
					    struct sk_buff *skb)
{
	return drvr->proto->tx_queue_data(drvr, ifidx, skb);
}

static inline int brcmf_proto_txdata(struct brcmf_pub *drvr, int ifidx,
				     u8 offset, struct sk_buff *skb)
{
	return drvr->proto->txdata(drvr, ifidx, offset, skb);
}
static inline void
brcmf_proto_configure_addr_mode(struct brcmf_pub *drvr, int ifidx,
				enum proto_addr_mode addr_mode)
{
	drvr->proto->configure_addr_mode(drvr, ifidx, addr_mode);
}
static inline void
brcmf_proto_delete_peer(struct brcmf_pub *drvr, int ifidx, u8 peer[ETH_ALEN])
{
	drvr->proto->delete_peer(drvr, ifidx, peer);
}
static inline void
brcmf_proto_add_tdls_peer(struct brcmf_pub *drvr, int ifidx, u8 peer[ETH_ALEN])
{
	drvr->proto->add_tdls_peer(drvr, ifidx, peer);
}
static inline bool brcmf_proto_is_reorder_skb(struct sk_buff *skb)
{
	struct brcmf_skb_reorder_data *rd;

	rd = (struct brcmf_skb_reorder_data *)skb->cb;
	return !!rd->reorder;
}

static inline void
brcmf_proto_rxreorder(struct brcmf_if *ifp, struct sk_buff *skb)
{
	ifp->drvr->proto->rxreorder(ifp, skb);
}

static inline void
brcmf_proto_add_if(struct brcmf_pub *drvr, struct brcmf_if *ifp)
{
	if (!drvr->proto->add_if)
		return;
	drvr->proto->add_if(ifp);
}

static inline void
brcmf_proto_del_if(struct brcmf_pub *drvr, struct brcmf_if *ifp)
{
	if (!drvr->proto->del_if)
		return;
	drvr->proto->del_if(ifp);
}

static inline void
brcmf_proto_reset_if(struct brcmf_pub *drvr, struct brcmf_if *ifp)
{
	if (!drvr->proto->reset_if)
		return;
	drvr->proto->reset_if(ifp);
}

static inline int
brcmf_proto_init_done(struct brcmf_pub *drvr)
{
	if (!drvr->proto->init_done)
		return 0;
	return drvr->proto->init_done(drvr);
}

static inline void
brcmf_proto_debugfs_create(struct brcmf_pub *drvr)
{
	drvr->proto->debugfs_create(drvr);
}

#endif /* BRCMFMAC_PROTO_H */
