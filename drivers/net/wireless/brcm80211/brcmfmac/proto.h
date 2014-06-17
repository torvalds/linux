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

struct brcmf_proto {
	int (*hdrpull)(struct brcmf_pub *drvr, bool do_fws, u8 *ifidx,
		       struct sk_buff *skb);
	int (*query_dcmd)(struct brcmf_pub *drvr, int ifidx, uint cmd,
			  void *buf, uint len);
	int (*set_dcmd)(struct brcmf_pub *drvr, int ifidx, uint cmd, void *buf,
			uint len);
	int (*txdata)(struct brcmf_pub *drvr, int ifidx, u8 offset,
		      struct sk_buff *skb);
	void *pd;
};


int brcmf_proto_attach(struct brcmf_pub *drvr);
void brcmf_proto_detach(struct brcmf_pub *drvr);

static inline int brcmf_proto_hdrpull(struct brcmf_pub *drvr, bool do_fws,
				      u8 *ifidx, struct sk_buff *skb)
{
	return drvr->proto->hdrpull(drvr, do_fws, ifidx, skb);
}
static inline int brcmf_proto_query_dcmd(struct brcmf_pub *drvr, int ifidx,
					 uint cmd, void *buf, uint len)
{
	return drvr->proto->query_dcmd(drvr, ifidx, cmd, buf, len);
}
static inline int brcmf_proto_set_dcmd(struct brcmf_pub *drvr, int ifidx,
				       uint cmd, void *buf, uint len)
{
	return drvr->proto->set_dcmd(drvr, ifidx, cmd, buf, len);
}
static inline int brcmf_proto_txdata(struct brcmf_pub *drvr, int ifidx,
				       u8 offset, struct sk_buff *skb)
{
	return drvr->proto->txdata(drvr, ifidx, offset, skb);
}


#endif /* BRCMFMAC_PROTO_H */
