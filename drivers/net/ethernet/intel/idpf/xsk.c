// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <net/libeth/xsk.h>

#include "idpf.h"
#include "xsk.h"

int idpf_xsk_pool_setup(struct idpf_vport *vport, struct netdev_bpf *bpf)
{
	struct xsk_buff_pool *pool = bpf->xsk.pool;
	u32 qid = bpf->xsk.queue_id;
	bool restart;
	int ret;

	restart = idpf_xdp_enabled(vport) && netif_running(vport->netdev);
	if (!restart)
		goto pool;

	ret = idpf_qp_switch(vport, qid, false);
	if (ret) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: failed to disable queue pair %u: %pe",
				       netdev_name(vport->netdev), qid,
				       ERR_PTR(ret));
		return ret;
	}

pool:
	ret = libeth_xsk_setup_pool(vport->netdev, qid, pool);
	if (ret) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: failed to configure XSk pool for pair %u: %pe",
				       netdev_name(vport->netdev), qid,
				       ERR_PTR(ret));
		return ret;
	}

	if (!restart)
		return 0;

	ret = idpf_qp_switch(vport, qid, true);
	if (ret) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: failed to enable queue pair %u: %pe",
				       netdev_name(vport->netdev), qid,
				       ERR_PTR(ret));
		goto err_dis;
	}

	return 0;

err_dis:
	libeth_xsk_setup_pool(vport->netdev, qid, false);

	return ret;
}
