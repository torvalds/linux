// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include "octep_vf_config.h"
#include "octep_vf_main.h"

int octep_vf_setup_mbox(struct octep_vf_device *oct)
{
	int ring = 0;

	oct->mbox = vzalloc(sizeof(*oct->mbox));
	if (!oct->mbox)
		return -1;

	mutex_init(&oct->mbox->lock);

	oct->hw_ops.setup_mbox_regs(oct, ring);
	INIT_WORK(&oct->mbox->wk.work, octep_vf_mbox_work);
	oct->mbox->wk.ctxptr = oct;
	dev_info(&oct->pdev->dev, "setup vf mbox successfully\n");
	return 0;
}

void octep_vf_delete_mbox(struct octep_vf_device *oct)
{
	if (oct->mbox) {
		if (work_pending(&oct->mbox->wk.work))
			cancel_work_sync(&oct->mbox->wk.work);

		mutex_destroy(&oct->mbox->lock);
		vfree(oct->mbox);
		oct->mbox = NULL;
		dev_info(&oct->pdev->dev, "Deleted vf mbox successfully\n");
	}
}

int octep_vf_mbox_version_check(struct octep_vf_device *oct)
{
	return 0;
}

void octep_vf_mbox_work(struct work_struct *work)
{
}

int octep_vf_mbox_set_mtu(struct octep_vf_device *oct, int mtu)
{
	return 0;
}

int octep_vf_mbox_set_mac_addr(struct octep_vf_device *oct, char *mac_addr)
{
	return 0;
}

int octep_vf_mbox_get_mac_addr(struct octep_vf_device *oct, char *mac_addr)
{
	return 0;
}

int octep_vf_mbox_set_rx_state(struct octep_vf_device *oct, bool state)
{
	return 0;
}

int octep_vf_mbox_set_link_status(struct octep_vf_device *oct, bool status)
{
	return 0;
}

int octep_vf_mbox_get_link_status(struct octep_vf_device *oct, u8 *oper_up)
{
	return 0;
}

int octep_vf_mbox_dev_remove(struct octep_vf_device *oct)
{
	return 0;
}

int octep_vf_mbox_get_fw_info(struct octep_vf_device *oct)
{
	return 0;
}

int octep_vf_mbox_set_offloads(struct octep_vf_device *oct, u16 tx_offloads,
			       u16 rx_offloads)
{
	return 0;
}
