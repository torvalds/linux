/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IDPF_XSK_H_
#define _IDPF_XSK_H_

#include <linux/types.h>

enum virtchnl2_queue_type;
struct idpf_buf_queue;
struct idpf_q_vector;
struct idpf_rx_queue;
struct idpf_tx_queue;
struct idpf_vport;
struct net_device;
struct netdev_bpf;

void idpf_xsk_setup_queue(const struct idpf_vport *vport, void *q,
			  enum virtchnl2_queue_type type);
void idpf_xsk_clear_queue(void *q, enum virtchnl2_queue_type type);
void idpf_xsk_init_wakeup(struct idpf_q_vector *qv);

int idpf_xskfq_init(struct idpf_buf_queue *bufq);
void idpf_xskfq_rel(struct idpf_buf_queue *bufq);
void idpf_xsksq_clean(struct idpf_tx_queue *xdpq);

int idpf_xskrq_poll(struct idpf_rx_queue *rxq, u32 budget);
bool idpf_xsk_xmit(struct idpf_tx_queue *xsksq);

int idpf_xsk_pool_setup(struct idpf_vport *vport, struct netdev_bpf *xdp);
int idpf_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags);

#endif /* !_IDPF_XSK_H_ */
