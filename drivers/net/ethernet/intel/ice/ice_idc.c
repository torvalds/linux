// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

/* Inter-Driver Communication */
#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

/**
 * ice_reserve_rdma_qvector - Reserve vector resources for RDMA driver
 * @pf: board private structure to initialize
 */
static int ice_reserve_rdma_qvector(struct ice_pf *pf)
{
	if (test_bit(ICE_FLAG_RDMA_ENA, pf->flags)) {
		int index;

		index = ice_get_res(pf, pf->irq_tracker, pf->num_rdma_msix,
				    ICE_RES_RDMA_VEC_ID);
		if (index < 0)
			return index;
		pf->num_avail_sw_msix -= pf->num_rdma_msix;
		pf->rdma_base_vector = (u16)index;
	}
	return 0;
}

/**
 * ice_init_rdma - initializes PF for RDMA use
 * @pf: ptr to ice_pf
 */
int ice_init_rdma(struct ice_pf *pf)
{
	struct device *dev = &pf->pdev->dev;
	int ret;

	/* Reserve vector resources */
	ret = ice_reserve_rdma_qvector(pf);
	if (ret < 0)
		dev_err(dev, "failed to reserve vectors for RDMA\n");

	return ret;
}
