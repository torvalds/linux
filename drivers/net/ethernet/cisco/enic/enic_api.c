/**
 * Copyright 2013 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include "vnic_dev.h"
#include "vnic_devcmd.h"

#include "enic_res.h"
#include "enic.h"
#include "enic_api.h"

int enic_api_devcmd_proxy_by_index(struct net_device *netdev, int vf,
	enum vnic_devcmd_cmd cmd, u64 *a0, u64 *a1, int wait)
{
	int err;
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;

	spin_lock(&enic->enic_api_lock);
	spin_lock(&enic->devcmd_lock);

	vnic_dev_cmd_proxy_by_index_start(vdev, vf);
	err = vnic_dev_cmd(vdev, cmd, a0, a1, wait);
	vnic_dev_cmd_proxy_end(vdev);

	spin_unlock(&enic->devcmd_lock);
	spin_unlock(&enic->enic_api_lock);

	return err;
}
EXPORT_SYMBOL(enic_api_devcmd_proxy_by_index);
