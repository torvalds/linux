// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#include <linux/slab.h>
#include "pd.h"

/**
 * rvt_alloc_pd - allocate a protection domain
 * @ibpd: PD
 * @udata: optional user data
 *
 * Allocate and keep track of a PD.
 *
 * Return: 0 on success
 */
int rvt_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ib_device *ibdev = ibpd->device;
	struct rvt_dev_info *dev = ib_to_rvt(ibdev);
	struct rvt_pd *pd = ibpd_to_rvtpd(ibpd);
	int ret = 0;

	/*
	 * While we could continue allocating protecetion domains, being
	 * constrained only by system resources. The IBTA spec defines that
	 * there is a max_pd limit that can be set and we need to check for
	 * that.
	 */

	spin_lock(&dev->n_pds_lock);
	if (dev->n_pds_allocated == dev->dparms.props.max_pd) {
		spin_unlock(&dev->n_pds_lock);
		ret = -ENOMEM;
		goto bail;
	}

	dev->n_pds_allocated++;
	spin_unlock(&dev->n_pds_lock);

	/* ib_alloc_pd() will initialize pd->ibpd. */
	pd->user = !!udata;

bail:
	return ret;
}

/**
 * rvt_dealloc_pd - Free PD
 * @ibpd: Free up PD
 * @udata: Valid user data or NULL for kernel object
 *
 * Return: always 0
 */
int rvt_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct rvt_dev_info *dev = ib_to_rvt(ibpd->device);

	spin_lock(&dev->n_pds_lock);
	dev->n_pds_allocated--;
	spin_unlock(&dev->n_pds_lock);
	return 0;
}
