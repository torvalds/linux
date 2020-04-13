/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_TXRX_H_
#define _IONIC_TXRX_H_

void ionic_rx_flush(struct ionic_cq *cq);
void ionic_tx_flush(struct ionic_cq *cq);

void ionic_rx_fill(struct ionic_queue *q);
void ionic_rx_empty(struct ionic_queue *q);
void ionic_tx_empty(struct ionic_queue *q);
int ionic_rx_napi(struct napi_struct *napi, int budget);
netdev_tx_t ionic_start_xmit(struct sk_buff *skb, struct net_device *netdev);

#endif /* _IONIC_TXRX_H_ */
