/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IDPF_XDP_H_
#define _IDPF_XDP_H_

#include <linux/types.h>

struct bpf_prog;
struct idpf_vport;
struct net_device;
struct netdev_bpf;

int idpf_xdp_rxq_info_init_all(const struct idpf_vport *vport);
void idpf_xdp_rxq_info_deinit_all(const struct idpf_vport *vport);
void idpf_xdp_copy_prog_to_rqs(const struct idpf_vport *vport,
			       struct bpf_prog *xdp_prog);

int idpf_xdpsqs_get(const struct idpf_vport *vport);
void idpf_xdpsqs_put(const struct idpf_vport *vport);

int idpf_xdp(struct net_device *dev, struct netdev_bpf *xdp);

#endif /* _IDPF_XDP_H_ */
