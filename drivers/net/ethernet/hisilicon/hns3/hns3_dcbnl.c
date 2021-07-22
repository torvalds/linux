// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include "hnae3.h"
#include "hns3_enet.h"

static int hns3_dcbnl_ieee_getets(struct net_device *ndev, struct ieee_ets *ets)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (hns3_nic_resetting(ndev))
		return -EBUSY;

	if (h->kinfo.dcb_ops->ieee_getets)
		return h->kinfo.dcb_ops->ieee_getets(h, ets);

	return -EOPNOTSUPP;
}

static int hns3_dcbnl_ieee_setets(struct net_device *ndev, struct ieee_ets *ets)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (hns3_nic_resetting(ndev))
		return -EBUSY;

	if (h->kinfo.dcb_ops->ieee_setets)
		return h->kinfo.dcb_ops->ieee_setets(h, ets);

	return -EOPNOTSUPP;
}

static int hns3_dcbnl_ieee_getpfc(struct net_device *ndev, struct ieee_pfc *pfc)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (hns3_nic_resetting(ndev))
		return -EBUSY;

	if (h->kinfo.dcb_ops->ieee_getpfc)
		return h->kinfo.dcb_ops->ieee_getpfc(h, pfc);

	return -EOPNOTSUPP;
}

static int hns3_dcbnl_ieee_setpfc(struct net_device *ndev, struct ieee_pfc *pfc)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (hns3_nic_resetting(ndev))
		return -EBUSY;

	if (h->kinfo.dcb_ops->ieee_setpfc)
		return h->kinfo.dcb_ops->ieee_setpfc(h, pfc);

	return -EOPNOTSUPP;
}

/* DCBX configuration */
static u8 hns3_dcbnl_getdcbx(struct net_device *ndev)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (h->kinfo.dcb_ops->getdcbx)
		return h->kinfo.dcb_ops->getdcbx(h);

	return 0;
}

/* return 0 if successful, otherwise fail */
static u8 hns3_dcbnl_setdcbx(struct net_device *ndev, u8 mode)
{
	struct hnae3_handle *h = hns3_get_handle(ndev);

	if (h->kinfo.dcb_ops->setdcbx)
		return h->kinfo.dcb_ops->setdcbx(h, mode);

	return 1;
}

static const struct dcbnl_rtnl_ops hns3_dcbnl_ops = {
	.ieee_getets	= hns3_dcbnl_ieee_getets,
	.ieee_setets	= hns3_dcbnl_ieee_setets,
	.ieee_getpfc	= hns3_dcbnl_ieee_getpfc,
	.ieee_setpfc	= hns3_dcbnl_ieee_setpfc,
	.getdcbx	= hns3_dcbnl_getdcbx,
	.setdcbx	= hns3_dcbnl_setdcbx,
};

/* hclge_dcbnl_setup - DCBNL setup
 * @handle: the corresponding vport handle
 * Set up DCBNL
 */
void hns3_dcbnl_setup(struct hnae3_handle *handle)
{
	struct net_device *dev = handle->kinfo.netdev;

	if ((!handle->kinfo.dcb_ops) || (handle->flags & HNAE3_SUPPORT_VF))
		return;

	dev->dcbnl_ops = &hns3_dcbnl_ops;
}
