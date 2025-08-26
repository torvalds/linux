/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IDPF_XDP_H_
#define _IDPF_XDP_H_

#include <linux/types.h>

struct idpf_vport;

int idpf_xdp_rxq_info_init_all(const struct idpf_vport *vport);
void idpf_xdp_rxq_info_deinit_all(const struct idpf_vport *vport);

int idpf_xdpsqs_get(const struct idpf_vport *vport);
void idpf_xdpsqs_put(const struct idpf_vport *vport);

#endif /* _IDPF_XDP_H_ */
