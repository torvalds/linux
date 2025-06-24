/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_IBDEV_H_
#define _IONIC_IBDEV_H_

#include <rdma/ib_verbs.h>
#include <ionic_api.h>

#include "ionic_lif_cfg.h"

#define IONIC_MIN_RDMA_VERSION	0
#define IONIC_MAX_RDMA_VERSION	2

struct ionic_ibdev {
	struct ib_device	ibdev;

	struct ionic_lif_cfg	lif_cfg;
};

#endif /* _IONIC_IBDEV_H_ */
