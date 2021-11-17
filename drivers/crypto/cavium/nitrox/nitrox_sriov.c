// SPDX-License-Identifier: GPL-2.0
#include <linux/pci.h>
#include <linux/delay.h>

#include "nitrox_dev.h"
#include "nitrox_hal.h"
#include "nitrox_common.h"
#include "nitrox_isr.h"
#include "nitrox_mbx.h"

/**
 * num_vfs_valid - validate VF count
 * @num_vfs: number of VF(s)
 */
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

static inline int vf_mode_to_nr_queues(enum vf_mode mode)
{
	int nr_queues = 0;

	switch (mode) {
	case __NDEV_MODE_PF:
		nr_queues = MAX_PF_QUEUES;
		break;
	case __NDEV_MODE_VF16:
		nr_queues = 8;
		break;
	case __NDEV_MODE_VF32:
		nr_queues = 4;
		break;
	case __NDEV_MODE_VF64:
		nr_queues = 2;
		break;
	case __NDEV_MODE_VF128:
		nr_queues = 1;
		break;
	}

	return nr_queues;
}

static void nitrox_pf_cleanup(struct nitrox_device *ndev)
{
	 /* PF has no queues in SR-IOV mode */
	atomic_set(&ndev->state, __NDEV_NOT_READY);
	/* unregister crypto algorithms */
	nitrox_crypto_unregister();

	/* cleanup PF resources */
	nitrox_unregister_interrupts(ndev);
	nitrox_common_sw_cleanup(ndev);
}

/**
 * nitrox_pf_reinit - re-initialize PF resources once SR-IOV is disabled
 * @ndev: NITROX device
 */
static int nitrox_pf_reinit(struct nitrox_device *ndev)
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

	/* configure the AQM queues */
	nitrox_config_aqm_rings(ndev);

	/* configure the packet queues */
	nitrox_config_pkt_input_rings(ndev);
	nitrox_config_pkt_solicit_ports(ndev);

	/* set device to ready state */
	atomic_set(&ndev->state, __NDEV_READY);

	/* register crypto algorithms */
	return nitrox_crypto_register();
}

static void nitrox_sriov_cleanup(struct nitrox_device *ndev)
{
	/* unregister interrupts for PF in SR-IOV */
	nitrox_sriov_unregister_interrupts(ndev);
	nitrox_mbox_cleanup(ndev);
}

static int nitrox_sriov_init(struct nitrox_device *ndev)
{
	int ret;

	/* register interrupts for PF in SR-IOV */
	ret = nitrox_sriov_register_interupts(ndev);
	if (ret)
		return ret;

	ret = nitrox_mbox_init(ndev);
	if (ret)
		goto sriov_init_fail;

	return 0;

sriov_init_fail:
	nitrox_sriov_cleanup(ndev);
	return ret;
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

	ndev->mode = num_vfs_to_mode(num_vfs);
	ndev->iov.num_vfs = num_vfs;
	ndev->iov.max_vf_queues = vf_mode_to_nr_queues(ndev->mode);
	/* set bit in flags */
	set_bit(__NDEV_SRIOV_BIT, &ndev->flags);

	/* cleanup PF resources */
	nitrox_pf_cleanup(ndev);

	/* PF SR-IOV mode initialization */
	err = nitrox_sriov_init(ndev);
	if (err)
		goto iov_fail;

	config_nps_core_vfcfg_mode(ndev, ndev->mode);
	return num_vfs;

iov_fail:
	pci_disable_sriov(pdev);
	/* clear bit in flags */
	clear_bit(__NDEV_SRIOV_BIT, &ndev->flags);
	ndev->iov.num_vfs = 0;
	ndev->mode = __NDEV_MODE_PF;
	/* reset back to working mode in PF */
	nitrox_pf_reinit(ndev);
	return err;
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

	ndev->iov.num_vfs = 0;
	ndev->iov.max_vf_queues = 0;
	ndev->mode = __NDEV_MODE_PF;

	/* cleanup PF SR-IOV resources */
	nitrox_sriov_cleanup(ndev);

	config_nps_core_vfcfg_mode(ndev, ndev->mode);

	return nitrox_pf_reinit(ndev);
}

int nitrox_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (!num_vfs)
		return nitrox_sriov_disable(pdev);

	return nitrox_sriov_enable(pdev, num_vfs);
}
