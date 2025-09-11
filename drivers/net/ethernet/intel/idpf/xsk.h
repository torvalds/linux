/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IDPF_XSK_H_
#define _IDPF_XSK_H_

#include <linux/types.h>

struct idpf_vport;
struct netdev_bpf;

int idpf_xsk_pool_setup(struct idpf_vport *vport, struct netdev_bpf *xdp);

#endif /* !_IDPF_XSK_H_ */
