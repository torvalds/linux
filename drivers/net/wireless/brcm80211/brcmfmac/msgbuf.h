/* Copyright (c) 2014 Broadcom Corporation
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
#ifndef BRCMFMAC_MSGBUF_H
#define BRCMFMAC_MSGBUF_H

#ifdef CONFIG_BRCMFMAC_PROTO_MSGBUF

#define BRCMF_H2D_MSGRING_CONTROL_SUBMIT_MAX_ITEM	20
#define BRCMF_H2D_MSGRING_RXPOST_SUBMIT_MAX_ITEM	256
#define BRCMF_D2H_MSGRING_CONTROL_COMPLETE_MAX_ITEM	20
#define BRCMF_D2H_MSGRING_TX_COMPLETE_MAX_ITEM		1024
#define BRCMF_D2H_MSGRING_RX_COMPLETE_MAX_ITEM		256
#define BRCMF_H2D_TXFLOWRING_MAX_ITEM			512

#define BRCMF_H2D_MSGRING_CONTROL_SUBMIT_ITEMSIZE	40
#define BRCMF_H2D_MSGRING_RXPOST_SUBMIT_ITEMSIZE	32
#define BRCMF_D2H_MSGRING_CONTROL_COMPLETE_ITEMSIZE	24
#define BRCMF_D2H_MSGRING_TX_COMPLETE_ITEMSIZE		16
#define BRCMF_D2H_MSGRING_RX_COMPLETE_ITEMSIZE		32
#define BRCMF_H2D_TXFLOWRING_ITEMSIZE			48


int brcmf_proto_msgbuf_rx_trigger(struct device *dev);
void brcmf_msgbuf_delete_flowring(struct brcmf_pub *drvr, u8 flowid);
int brcmf_proto_msgbuf_attach(struct brcmf_pub *drvr);
void brcmf_proto_msgbuf_detach(struct brcmf_pub *drvr);
#else
static inline int brcmf_proto_msgbuf_attach(struct brcmf_pub *drvr)
{
	return 0;
}
static inline void brcmf_proto_msgbuf_detach(struct brcmf_pub *drvr) {}
#endif

#endif /* BRCMFMAC_MSGBUF_H */
