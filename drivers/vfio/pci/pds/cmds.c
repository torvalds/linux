// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#include <linux/io.h>
#include <linux/types.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>

#include "vfio_dev.h"
#include "cmds.h"

int pds_vfio_register_client_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	char devname[PDS_DEVNAME_LEN];
	struct pdsc *pdsc;
	int ci;

	snprintf(devname, sizeof(devname), "%s.%d-%u", PDS_VFIO_LM_DEV_NAME,
		 pci_domain_nr(pdev->bus),
		 PCI_DEVID(pdev->bus->number, pdev->devfn));

	pdsc = pdsc_get_pf_struct(pdev);
	if (IS_ERR(pdsc))
		return PTR_ERR(pdsc);

	ci = pds_client_register(pdsc, devname);
	if (ci < 0)
		return ci;

	pds_vfio->client_id = ci;

	return 0;
}

void pds_vfio_unregister_client_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	struct pdsc *pdsc;
	int err;

	pdsc = pdsc_get_pf_struct(pdev);
	if (IS_ERR(pdsc))
		return;

	err = pds_client_unregister(pdsc, pds_vfio->client_id);
	if (err)
		dev_err(&pdev->dev, "unregister from DSC failed: %pe\n",
			ERR_PTR(err));

	pds_vfio->client_id = 0;
}
