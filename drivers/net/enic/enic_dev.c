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

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_fw_info(enic->vdev, fw_info);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_stats_dump(struct enic *enic, struct vnic_stats **vstats)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_stats_dump(enic->vdev, vstats);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_add_station_addr(struct enic *enic)
{
	int err;

	if (!is_valid_ether_addr(enic->netdev->dev_addr))
		return -EADDRNOTAVAIL;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_add_addr(enic->vdev, enic->netdev->dev_addr);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_del_station_addr(struct enic *enic)
{
	int err;

	if (!is_valid_ether_addr(enic->netdev->dev_addr))
		return -EADDRNOTAVAIL;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_del_addr(enic->vdev, enic->netdev->dev_addr);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_packet_filter(struct enic *enic, int directed, int multicast,
	int broadcast, int promisc, int allmulti)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_packet_filter(enic->vdev, directed,
		multicast, broadcast, promisc, allmulti);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_add_addr(struct enic *enic, u8 *addr)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_add_addr(enic->vdev, addr);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_del_addr(struct enic *enic, u8 *addr)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_del_addr(enic->vdev, addr);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_notify_unset(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_notify_unset(enic->vdev);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_hang_notify(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_hang_notify(enic->vdev);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_set_ig_vlan_rewrite_mode(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_set_ig_vlan_rewrite_mode(enic->vdev,
		IG_VLAN_REWRITE_MODE_PRIORITY_TAG_DEFAULT_VLAN);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_enable(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_enable_wait(enic->vdev);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_disable(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_disable(enic->vdev);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_vnic_dev_deinit(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_deinit(enic->vdev);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_init_prov(struct enic *enic, struct vic_provinfo *vp)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_init_prov(enic->vdev,
		(u8 *)vp, vic_provinfo_size(vp));
	spin_unlock(&enic->devcmd_lock);

	return err;
}

int enic_dev_init_done(struct enic *enic, int *done, int *error)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	err = vnic_dev_init_done(enic->vdev, done, error);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

/* rtnl lock is held */
void enic_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct enic *enic = netdev_priv(netdev);

	spin_lock(&enic->devcmd_lock);
	enic_add_vlan(enic, vid);
	spin_unlock(&enic->devcmd_lock);
}

/* rtnl lock is held */
void enic_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct enic *enic = netdev_priv(netdev);

	spin_lock(&enic->devcmd_lock);
	enic_del_vlan(enic, vid);
	spin_unlock(&enic->devcmd_lock);
}
