/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IDPF_XSK_H_
#define _IDPF_XSK_H_

#include <linux/types.h>

enum virtchnl2_queue_type;
struct idpf_tx_queue;
struct idpf_vport;
struct netdev_bpf;

void idpf_xsk_setup_queue(const struct idpf_vport *vport, void *q,
			  enum virtchnl2_queue_type type);
void idpf_xsk_clear_queue(void *q, enum virtchnl2_queue_type type);

void idpf_xsksq_clean(struct idpf_tx_queue *xdpq);
bool idpf_xsk_xmit(struct idpf_tx_queue *xsksq);

int idpf_xsk_pool_setup(struct idpf_vport *vport, struct netdev_bpf *xdp);

#endif /* !_IDPF_XSK_H_ */
