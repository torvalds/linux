/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_vfr.h"
#include "bnxt_devlink.h"

static const struct devlink_ops bnxt_dl_ops = {
#ifdef CONFIG_BNXT_SRIOV
	.eswitch_mode_set = bnxt_dl_eswitch_mode_set,
	.eswitch_mode_get = bnxt_dl_eswitch_mode_get,
#endif /* CONFIG_BNXT_SRIOV */
};

int bnxt_dl_register(struct bnxt *bp)
{
	struct devlink *dl;
	int rc;

	if (!pci_find_ext_capability(bp->pdev, PCI_EXT_CAP_ID_SRIOV))
		return 0;

	if (bp->hwrm_spec_code < 0x10803) {
		netdev_warn(bp->dev, "Firmware does not support SR-IOV E-Switch SWITCHDEV mode.\n");
		return -ENOTSUPP;
	}

	dl = devlink_alloc(&bnxt_dl_ops, sizeof(struct bnxt_dl));
	if (!dl) {
		netdev_warn(bp->dev, "devlink_alloc failed");
		return -ENOMEM;
	}

	bnxt_link_bp_to_dl(bp, dl);
	bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;
	rc = devlink_register(dl, &bp->pdev->dev);
	if (rc) {
		bnxt_link_bp_to_dl(bp, NULL);
		devlink_free(dl);
		netdev_warn(bp->dev, "devlink_register failed. rc=%d", rc);
		return rc;
	}

	return 0;
}

void bnxt_dl_unregister(struct bnxt *bp)
{
	struct devlink *dl = bp->dl;

	if (!dl)
		return;

	devlink_unregister(dl);
	devlink_free(dl);
}
