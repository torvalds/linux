/*
 * Copyright 2011 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "vnic_dev.h"
#include "vnic_vic.h"
#include "enic_res.h"
#include "enic.h"
#include "enic_dev.h"

int enic_dev_fw_info(struct enic *enic, struct vnic_devcmd_fw_info **fw_info)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_fw_info(enic->vdev, fw_info);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_stats_dump(struct enic *enic, struct vnic_stats **vstats)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_stats_dump(enic->vdev, vstats);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_add_station_addr(struct enic *enic)
{
	int err;

	if (!is_valid_ether_addr(enic->netdev->dev_addr))
		return -EADDRNOTAVAIL;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_add_addr(enic->vdev, enic->netdev->dev_addr);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_del_station_addr(struct enic *enic)
{
	int err;

	if (!is_valid_ether_addr(enic->netdev->dev_addr))
		return -EADDRNOTAVAIL;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_del_addr(enic->vdev, enic->netdev->dev_addr);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_packet_filter(struct enic *enic, int directed, int multicast,
	int broadcast, int promisc, int allmulti)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_packet_filter(enic->vdev, directed,
		multicast, broadcast, promisc, allmulti);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_add_addr(struct enic *enic, const u8 *addr)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_add_addr(enic->vdev, addr);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_del_addr(struct enic *enic, const u8 *addr)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_del_addr(enic->vdev, addr);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_notify_unset(struct enic *enic)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_notify_unset(enic->vdev);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_hang_notify(struct enic *enic)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_hang_notify(enic->vdev);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_set_ig_vlan_rewrite_mode(struct enic *enic)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_set_ig_vlan_rewrite_mode(enic->vdev,
		IG_VLAN_REWRITE_MODE_PRIORITY_TAG_DEFAULT_VLAN);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_enable(struct enic *enic)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_enable_wait(enic->vdev);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_disable(struct enic *enic)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_disable(enic->vdev);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_intr_coal_timer_info(struct enic *enic)
{
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_intr_coal_timer_info(enic->vdev);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

/* rtnl lock is held */
int enic_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct enic *enic = netdev_priv(netdev);
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = enic_add_vlan(enic, vid);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

/* rtnl lock is held */
int enic_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct enic *enic = netdev_priv(netdev);
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = enic_del_vlan(enic, vid);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

int enic_dev_status_to_errno(int devcmd_status)
{
	switch (devcmd_status) {
	case ERR_SUCCESS:
		return 0;
	case ERR_EINVAL:
		return -EINVAL;
	case ERR_EFAULT:
		return -EFAULT;
	case ERR_EPERM:
		return -EPERM;
	case ERR_EBUSY:
		return -EBUSY;
	case ERR_ECMDUNKNOWN:
	case ERR_ENOTSUPPORTED:
		return -EOPNOTSUPP;
	case ERR_EBADSTATE:
		return -EINVAL;
	case ERR_ENOMEM:
		return -ENOMEM;
	case ERR_ETIMEDOUT:
		return -ETIMEDOUT;
	case ERR_ELINKDOWN:
		return -ENETDOWN;
	case ERR_EINPROGRESS:
		return -EINPROGRESS;
	case ERR_EMAXRES:
	default:
		return (devcmd_status < 0) ? devcmd_status : -1;
	}
}
