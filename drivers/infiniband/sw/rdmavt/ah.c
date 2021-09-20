// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2016 - 2019 Intel Corporation.
 */

#include <linux/slab.h>
#include "ah.h"
#include "vt.h" /* for prints */

/**
 * rvt_check_ah - validate the attributes of AH
 * @ibdev: the ib device
 * @ah_attr: the attributes of the AH
 *
 * If driver supports a more detailed check_ah function call back to it
 * otherwise just check the basics.
 *
 * Return: 0 on success
 */
int rvt_check_ah(struct ib_device *ibdev,
		 struct rdma_ah_attr *ah_attr)
{
	int err;
	int port_num = rdma_ah_get_port_num(ah_attr);
	struct ib_port_attr port_attr;
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	u8 ah_flags = rdma_ah_get_ah_flags(ah_attr);
	u8 static_rate = rdma_ah_get_static_rate(ah_attr);

	err = ib_query_port(ibdev, port_num, &port_attr);
	if (err)
		return -EINVAL;
	if (port_num < 1 ||
	    port_num > ibdev->phys_port_cnt)
		return -EINVAL;
	if (static_rate != IB_RATE_PORT_CURRENT &&
	    ib_rate_to_mbps(static_rate) < 0)
		return -EINVAL;
	if ((ah_flags & IB_AH_GRH) &&
	    rdma_ah_read_grh(ah_attr)->sgid_index >= port_attr.gid_tbl_len)
		return -EINVAL;
	if (rdi->driver_f.check_ah)
		return rdi->driver_f.check_ah(ibdev, ah_attr);
	return 0;
}
EXPORT_SYMBOL(rvt_check_ah);

/**
 * rvt_create_ah - create an address handle
 * @ibah: the IB address handle
 * @init_attr: the attributes of the AH
 * @udata: pointer to user's input output buffer information.
 *
 * This may be called from interrupt context.
 *
 * Return: 0 on success
 */
int rvt_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr,
		  struct ib_udata *udata)
{
	struct rvt_ah *ah = ibah_to_rvtah(ibah);
	struct rvt_dev_info *dev = ib_to_rvt(ibah->device);
	unsigned long flags;

	if (rvt_check_ah(ibah->device, init_attr->ah_attr))
		return -EINVAL;

	spin_lock_irqsave(&dev->n_ahs_lock, flags);
	if (dev->n_ahs_allocated == dev->dparms.props.max_ah) {
		spin_unlock_irqrestore(&dev->n_ahs_lock, flags);
		return -ENOMEM;
	}

	dev->n_ahs_allocated++;
	spin_unlock_irqrestore(&dev->n_ahs_lock, flags);

	rdma_copy_ah_attr(&ah->attr, init_attr->ah_attr);

	if (dev->driver_f.notify_new_ah)
		dev->driver_f.notify_new_ah(ibah->device,
					    init_attr->ah_attr, ah);

	return 0;
}

/**
 * rvt_destroy_ah - Destroy an address handle
 * @ibah: address handle
 * @destroy_flags: destroy address handle flags (see enum rdma_destroy_ah_flags)
 * Return: 0 on success
 */
int rvt_destroy_ah(struct ib_ah *ibah, u32 destroy_flags)
{
	struct rvt_dev_info *dev = ib_to_rvt(ibah->device);
	struct rvt_ah *ah = ibah_to_rvtah(ibah);
	unsigned long flags;

	spin_lock_irqsave(&dev->n_ahs_lock, flags);
	dev->n_ahs_allocated--;
	spin_unlock_irqrestore(&dev->n_ahs_lock, flags);

	rdma_destroy_ah_attr(&ah->attr);
	return 0;
}

/**
 * rvt_modify_ah - modify an ah with given attrs
 * @ibah: address handle to modify
 * @ah_attr: attrs to apply
 *
 * Return: 0 on success
 */
int rvt_modify_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct rvt_ah *ah = ibah_to_rvtah(ibah);

	if (rvt_check_ah(ibah->device, ah_attr))
		return -EINVAL;

	ah->attr = *ah_attr;

	return 0;
}

/**
 * rvt_query_ah - return attrs for ah
 * @ibah: address handle to query
 * @ah_attr: return info in this
 *
 * Return: always 0
 */
int rvt_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct rvt_ah *ah = ibah_to_rvtah(ibah);

	*ah_attr = ah->attr;

	return 0;
}
