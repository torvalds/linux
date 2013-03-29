/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic_sriov.h"
#include "qlcnic.h"
#include <linux/types.h>

int qlcnic_sriov_init(struct qlcnic_adapter *adapter, int num_vfs)
{
	struct qlcnic_sriov *sriov;

	if (!qlcnic_sriov_enable_check(adapter))
		return -EIO;

	sriov  = kzalloc(sizeof(struct qlcnic_sriov), GFP_KERNEL);
	if (!sriov)
		return -ENOMEM;

	adapter->ahw->sriov = sriov;
	sriov->num_vfs = num_vfs;
	return 0;
}

void __qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	if (!qlcnic_sriov_enable_check(adapter))
		return;

	kfree(adapter->ahw->sriov);
}

void qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_sriov_pf_cleanup(adapter);
}
