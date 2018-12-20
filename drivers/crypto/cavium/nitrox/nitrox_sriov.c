// SPDX-License-Identifier: GPL-2.0
#include <linux/pci.h>
#include <linux/delay.h>

#include "nitrox_dev.h"
#include "nitrox_hal.h"
#include "nitrox_common.h"
#include "nitrox_isr.h"

static inline bool num_vfs_valid(int num_vfs)
{
	bool valid = false;

	switch (num_vfs) {
	case 16:
	case 32:
	case 64:
	case 128:
		valid = true;
		break;
	}

	return valid;
}

static inline enum vf_mode num_vfs_to_mode(int num_vfs)
{
	enum vf_mode mode = 0;

	switch (num_vfs) {
	case 0:
		mode = __NDEV_MODE_PF;
		break;
	case 16:
		mode = __NDEV_MODE_VF16;
		break;
	case 32:
		mode = __NDEV_MODE_VF32;
		break;
	case 64:
		mode = __NDEV_MODE_VF64;
		break;
	case 128:
		mode = __NDEV_MODE_VF128;
		break;
	}

	return mode;
}

static void pf_sriov_cleanup(struct nitrox_device *ndev)
{
	 /* PF has no queues in SR-IOV mode */
	atomic_set(&ndev->state, __NDEV_NOT_READY);
	/* unregister crypto algorithms */
	nitrox_crypto_unregister();

	/* cleanup PF resources */
	nitrox_unregister_interrupts(ndev);
	nitrox_common_sw_cleanup(ndev);
}

static int pf_sriov_init(struct nitrox_device *ndev)
{
	int err;

	/* allocate resources for PF */
	err = nitrox_common_sw_init(ndev);
	if (err)
		return err;

	err = nitrox_register_interrupts(ndev);
	if (err) {
		nitrox_common_sw_cleanup(ndev);
		return err;
	}

	/* configure the packet queues */
	nitrox_config_pkt_input_rings(ndev);
	nitrox_config_pkt_solicit_ports(ndev);

	/* set device to ready state */
	atomic_set(&ndev->state, __NDEV_READY);

	/* register crypto algorithms */
	return nitrox_crypto_register();
}

static int nitrox_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct nitrox_device *ndev = pci_get_drvdata(pdev);
	int err;

	if (!num_vfs_valid(num_vfs)) {
		dev_err(DEV(ndev), "Invalid num_vfs %d\n", num_vfs);
		return -EINVAL;
	}

	if (pci_num_vf(pdev) == num_vfs)
		return num_vfs;

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_err(DEV(ndev), "failed to enable PCI sriov %d\n", err);
		return err;
	}
	dev_info(DEV(ndev), "Enabled VF(s) %d\n", num_vfs);

	ndev->num_vfs = num_vfs;
	ndev->mode = num_vfs_to_mode(num_vfs);
	/* set bit in flags */
	set_bit(__NDEV_SRIOV_BIT, &ndev->flags);

	/* cleanup PF resources */
	pf_sriov_cleanup(ndev);

	config_nps_core_vfcfg_mode(ndev, ndev->mode);

	return num_vfs;
}

static int nitrox_sriov_disable(struct pci_dev *pdev)
{
	struct nitrox_device *ndev = pci_get_drvdata(pdev);

	if (!test_bit(__NDEV_SRIOV_BIT, &ndev->flags))
		return 0;

	if (pci_vfs_assigned(pdev)) {
		dev_warn(DEV(ndev), "VFs are attached to VM. Can't disable SR-IOV\n");
		return -EPERM;
	}
	pci_disable_sriov(pdev);
	/* clear bit in flags */
	clear_bit(__NDEV_SRIOV_BIT, &ndev->flags);

	ndev->num_vfs = 0;
	ndev->mode = __NDEV_MODE_PF;

	config_nps_core_vfcfg_mode(ndev, ndev->mode);

	return pf_sriov_init(ndev);
}

int nitrox_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (!num_vfs)
		return nitrox_sriov_disable(pdev);

	return nitrox_sriov_enable(pdev, num_vfs);
}
