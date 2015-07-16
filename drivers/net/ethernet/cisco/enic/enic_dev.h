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

#ifndef _ENIC_DEV_H_
#define _ENIC_DEV_H_

#include "vnic_dev.h"
#include "vnic_vic.h"

/*
 * Calls the devcmd function given by argument vnicdevcmdfn.
 * If vf argument is valid, it proxies the devcmd
 */
#define ENIC_DEVCMD_PROXY_BY_INDEX(vf, err, enic, vnicdevcmdfn, ...) \
	do { \
		spin_lock_bh(&enic->devcmd_lock); \
		if (enic_is_valid_vf(enic, vf)) { \
			vnic_dev_cmd_proxy_by_index_start(enic->vdev, vf); \
			err = vnicdevcmdfn(enic->vdev, ##__VA_ARGS__); \
			vnic_dev_cmd_proxy_end(enic->vdev); \
		} else { \
			err = vnicdevcmdfn(enic->vdev, ##__VA_ARGS__); \
		} \
		spin_unlock_bh(&enic->devcmd_lock); \
	} while (0)

int enic_dev_fw_info(struct enic *enic, struct vnic_devcmd_fw_info **fw_info);
int enic_dev_stats_dump(struct enic *enic, struct vnic_stats **vstats);
int enic_dev_add_station_addr(struct enic *enic);
int enic_dev_del_station_addr(struct enic *enic);
int enic_dev_packet_filter(struct enic *enic, int directed, int multicast,
	int broadcast, int promisc, int allmulti);
int enic_dev_add_addr(struct enic *enic, const u8 *addr);
int enic_dev_del_addr(struct enic *enic, const u8 *addr);
int enic_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid);
int enic_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid);
int enic_dev_notify_unset(struct enic *enic);
int enic_dev_hang_notify(struct enic *enic);
int enic_dev_set_ig_vlan_rewrite_mode(struct enic *enic);
int enic_dev_enable(struct enic *enic);
int enic_dev_disable(struct enic *enic);
int enic_dev_intr_coal_timer_info(struct enic *enic);
int enic_dev_status_to_errno(int devcmd_status);

#endif /* _ENIC_DEV_H_ */
