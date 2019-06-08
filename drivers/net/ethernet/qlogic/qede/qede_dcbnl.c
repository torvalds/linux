// SPDX-License-Identifier: GPL-2.0-only
/* QLogic qede NIC Driver
* Copyright (c) 2015 QLogic Corporation
*/

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/dcbnl.h>
#include "qede.h"

static u8 qede_dcbnl_getstate(struct net_device *netdev)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getstate(edev->cdev);
}

static u8 qede_dcbnl_setstate(struct net_device *netdev, u8 state)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setstate(edev->cdev, state);
}

static void qede_dcbnl_getpermhwaddr(struct net_device *netdev,
				     u8 *perm_addr)
{
	memcpy(perm_addr, netdev->dev_addr, netdev->addr_len);
}

static void qede_dcbnl_getpgtccfgtx(struct net_device *netdev, int prio,
				    u8 *prio_type, u8 *pgid, u8 *bw_pct,
				    u8 *up_map)
{
	struct qede_dev *edev = netdev_priv(netdev);

	edev->ops->dcb->getpgtccfgtx(edev->cdev, prio, prio_type,
				     pgid, bw_pct, up_map);
}

static void qede_dcbnl_getpgbwgcfgtx(struct net_device *netdev,
				     int pgid, u8 *bw_pct)
{
	struct qede_dev *edev = netdev_priv(netdev);

	edev->ops->dcb->getpgbwgcfgtx(edev->cdev, pgid, bw_pct);
}

static void qede_dcbnl_getpgtccfgrx(struct net_device *netdev, int prio,
				    u8 *prio_type, u8 *pgid, u8 *bw_pct,
				    u8 *up_map)
{
	struct qede_dev *edev = netdev_priv(netdev);

	edev->ops->dcb->getpgtccfgrx(edev->cdev, prio, prio_type, pgid, bw_pct,
				     up_map);
}

static void qede_dcbnl_getpgbwgcfgrx(struct net_device *netdev,
				     int pgid, u8 *bw_pct)
{
	struct qede_dev *edev = netdev_priv(netdev);

	edev->ops->dcb->getpgbwgcfgrx(edev->cdev, pgid, bw_pct);
}

static void qede_dcbnl_getpfccfg(struct net_device *netdev, int prio,
				 u8 *setting)
{
	struct qede_dev *edev = netdev_priv(netdev);

	edev->ops->dcb->getpfccfg(edev->cdev, prio, setting);
}

static void qede_dcbnl_setpfccfg(struct net_device *netdev, int prio,
				 u8 setting)
{
	struct qede_dev *edev = netdev_priv(netdev);

	edev->ops->dcb->setpfccfg(edev->cdev, prio, setting);
}

static u8 qede_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getcap(edev->cdev, capid, cap);
}

static int qede_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getnumtcs(edev->cdev, tcid, num);
}

static u8 qede_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getpfcstate(edev->cdev);
}

static int qede_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getapp(edev->cdev, idtype, id);
}

static u8 qede_dcbnl_getdcbx(struct net_device *netdev)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getdcbx(edev->cdev);
}

static void qede_dcbnl_setpgtccfgtx(struct net_device *netdev, int prio,
				    u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setpgtccfgtx(edev->cdev, prio, pri_type, pgid,
					    bw_pct, up_map);
}

static void qede_dcbnl_setpgtccfgrx(struct net_device *netdev, int prio,
				    u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setpgtccfgrx(edev->cdev, prio, pri_type, pgid,
					    bw_pct, up_map);
}

static void qede_dcbnl_setpgbwgcfgtx(struct net_device *netdev, int pgid,
				     u8 bw_pct)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setpgbwgcfgtx(edev->cdev, pgid, bw_pct);
}

static void qede_dcbnl_setpgbwgcfgrx(struct net_device *netdev, int pgid,
				     u8 bw_pct)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setpgbwgcfgrx(edev->cdev, pgid, bw_pct);
}

static u8 qede_dcbnl_setall(struct net_device *netdev)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setall(edev->cdev);
}

static int qede_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setnumtcs(edev->cdev, tcid, num);
}

static void qede_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setpfcstate(edev->cdev, state);
}

static int qede_dcbnl_setapp(struct net_device *netdev, u8 idtype, u16 idval,
			     u8 up)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setapp(edev->cdev, idtype, idval, up);
}

static u8 qede_dcbnl_setdcbx(struct net_device *netdev, u8 state)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setdcbx(edev->cdev, state);
}

static u8 qede_dcbnl_getfeatcfg(struct net_device *netdev, int featid,
				u8 *flags)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->getfeatcfg(edev->cdev, featid, flags);
}

static u8 qede_dcbnl_setfeatcfg(struct net_device *netdev, int featid, u8 flags)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->setfeatcfg(edev->cdev, featid, flags);
}

static int qede_dcbnl_peer_getappinfo(struct net_device *netdev,
				      struct dcb_peer_app_info *info,
				      u16 *count)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->peer_getappinfo(edev->cdev, info, count);
}

static int qede_dcbnl_peer_getapptable(struct net_device *netdev,
				       struct dcb_app *app)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->peer_getapptable(edev->cdev, app);
}

static int qede_dcbnl_cee_peer_getpfc(struct net_device *netdev,
				      struct cee_pfc *pfc)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->cee_peer_getpfc(edev->cdev, pfc);
}

static int qede_dcbnl_cee_peer_getpg(struct net_device *netdev,
				     struct cee_pg *pg)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->cee_peer_getpg(edev->cdev, pg);
}

static int qede_dcbnl_ieee_getpfc(struct net_device *netdev,
				  struct ieee_pfc *pfc)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_getpfc(edev->cdev, pfc);
}

static int qede_dcbnl_ieee_setpfc(struct net_device *netdev,
				  struct ieee_pfc *pfc)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_setpfc(edev->cdev, pfc);
}

static int qede_dcbnl_ieee_getets(struct net_device *netdev,
				  struct ieee_ets *ets)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_getets(edev->cdev, ets);
}

static int qede_dcbnl_ieee_setets(struct net_device *netdev,
				  struct ieee_ets *ets)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_setets(edev->cdev, ets);
}

static int qede_dcbnl_ieee_getapp(struct net_device *netdev,
				  struct dcb_app *app)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_getapp(edev->cdev, app);
}

static int qede_dcbnl_ieee_setapp(struct net_device *netdev,
				  struct dcb_app *app)
{
	struct qede_dev *edev = netdev_priv(netdev);
	int err;

	err = dcb_ieee_setapp(netdev, app);
	if (err)
		return err;

	return edev->ops->dcb->ieee_setapp(edev->cdev, app);
}

static int qede_dcbnl_ieee_peer_getpfc(struct net_device *netdev,
				       struct ieee_pfc *pfc)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_peer_getpfc(edev->cdev, pfc);
}

static int qede_dcbnl_ieee_peer_getets(struct net_device *netdev,
				       struct ieee_ets *ets)
{
	struct qede_dev *edev = netdev_priv(netdev);

	return edev->ops->dcb->ieee_peer_getets(edev->cdev, ets);
}

static const struct dcbnl_rtnl_ops qede_dcbnl_ops = {
	.ieee_getpfc = qede_dcbnl_ieee_getpfc,
	.ieee_setpfc = qede_dcbnl_ieee_setpfc,
	.ieee_getets = qede_dcbnl_ieee_getets,
	.ieee_setets = qede_dcbnl_ieee_setets,
	.ieee_getapp = qede_dcbnl_ieee_getapp,
	.ieee_setapp = qede_dcbnl_ieee_setapp,
	.ieee_peer_getpfc = qede_dcbnl_ieee_peer_getpfc,
	.ieee_peer_getets = qede_dcbnl_ieee_peer_getets,
	.getstate = qede_dcbnl_getstate,
	.setstate = qede_dcbnl_setstate,
	.getpermhwaddr = qede_dcbnl_getpermhwaddr,
	.getpgtccfgtx = qede_dcbnl_getpgtccfgtx,
	.getpgbwgcfgtx = qede_dcbnl_getpgbwgcfgtx,
	.getpgtccfgrx = qede_dcbnl_getpgtccfgrx,
	.getpgbwgcfgrx = qede_dcbnl_getpgbwgcfgrx,
	.getpfccfg = qede_dcbnl_getpfccfg,
	.setpfccfg = qede_dcbnl_setpfccfg,
	.getcap = qede_dcbnl_getcap,
	.getnumtcs = qede_dcbnl_getnumtcs,
	.getpfcstate = qede_dcbnl_getpfcstate,
	.getapp = qede_dcbnl_getapp,
	.getdcbx = qede_dcbnl_getdcbx,
	.setpgtccfgtx = qede_dcbnl_setpgtccfgtx,
	.setpgtccfgrx = qede_dcbnl_setpgtccfgrx,
	.setpgbwgcfgtx = qede_dcbnl_setpgbwgcfgtx,
	.setpgbwgcfgrx = qede_dcbnl_setpgbwgcfgrx,
	.setall = qede_dcbnl_setall,
	.setnumtcs = qede_dcbnl_setnumtcs,
	.setpfcstate = qede_dcbnl_setpfcstate,
	.setapp = qede_dcbnl_setapp,
	.setdcbx = qede_dcbnl_setdcbx,
	.setfeatcfg = qede_dcbnl_setfeatcfg,
	.getfeatcfg = qede_dcbnl_getfeatcfg,
	.peer_getappinfo = qede_dcbnl_peer_getappinfo,
	.peer_getapptable = qede_dcbnl_peer_getapptable,
	.cee_peer_getpfc = qede_dcbnl_cee_peer_getpfc,
	.cee_peer_getpg = qede_dcbnl_cee_peer_getpg,
};

void qede_set_dcbnl_ops(struct net_device *dev)
{
	dev->dcbnl_ops = &qede_dcbnl_ops;
}
