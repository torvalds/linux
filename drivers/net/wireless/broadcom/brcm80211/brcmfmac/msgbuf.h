// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef BRCMFMAC_MSGBUF_H
#define BRCMFMAC_MSGBUF_H

#ifdef CONFIG_BRCMFMAC_PROTO_MSGBUF

#define BRCMF_H2D_MSGRING_CONTROL_SUBMIT_MAX_ITEM	64
#define BRCMF_H2D_MSGRING_RXPOST_SUBMIT_MAX_ITEM	1024
#define BRCMF_D2H_MSGRING_CONTROL_COMPLETE_MAX_ITEM	64
#define BRCMF_D2H_MSGRING_TX_COMPLETE_MAX_ITEM		1024
#define BRCMF_D2H_MSGRING_RX_COMPLETE_MAX_ITEM		1024
#define BRCMF_H2D_TXFLOWRING_MAX_ITEM			512

#define BRCMF_H2D_MSGRING_CONTROL_SUBMIT_ITEMSIZE	40
#define BRCMF_H2D_MSGRING_RXPOST_SUBMIT_ITEMSIZE	32
#define BRCMF_D2H_MSGRING_CONTROL_COMPLETE_ITEMSIZE	24
#define BRCMF_D2H_MSGRING_TX_COMPLETE_ITEMSIZE_PRE_V7	16
#define BRCMF_D2H_MSGRING_TX_COMPLETE_ITEMSIZE		24
#define BRCMF_D2H_MSGRING_RX_COMPLETE_ITEMSIZE_PRE_V7	32
#define BRCMF_D2H_MSGRING_RX_COMPLETE_ITEMSIZE		40
#define BRCMF_H2D_TXFLOWRING_ITEMSIZE			48

struct msgbuf_buf_addr {
	__le32		low_addr;
	__le32		high_addr;
};

int brcmf_proto_msgbuf_rx_trigger(struct device *dev);
void brcmf_msgbuf_delete_flowring(struct brcmf_pub *drvr, u16 flowid);
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
