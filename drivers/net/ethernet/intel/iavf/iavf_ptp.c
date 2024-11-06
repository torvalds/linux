// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024 Intel Corporation. */

#include "iavf.h"
#include "iavf_ptp.h"

/**
 * iavf_ptp_cap_supported - Check if a PTP capability is supported
 * @adapter: private adapter structure
 * @cap: the capability bitmask to check
 *
 * Return: true if every capability set in cap is also set in the enabled
 *         capabilities reported by the PF, false otherwise.
 */
bool iavf_ptp_cap_supported(const struct iavf_adapter *adapter, u32 cap)
{
	if (!IAVF_PTP_ALLOWED(adapter))
		return false;

	/* Only return true if every bit in cap is set in hw_caps.caps */
	return (adapter->ptp.hw_caps.caps & cap) == cap;
}

/**
 * iavf_ptp_register_clock - Register a new PTP for userspace
 * @adapter: private adapter structure
 *
 * Allocate and register a new PTP clock device if necessary.
 *
 * Return: 0 if success, error otherwise.
 */
static int iavf_ptp_register_clock(struct iavf_adapter *adapter)
{
	struct ptp_clock_info *ptp_info = &adapter->ptp.info;
	struct device *dev = &adapter->pdev->dev;
	struct ptp_clock *clock;

	snprintf(ptp_info->name, sizeof(ptp_info->name), "%s-%s-clk",
		 KBUILD_MODNAME, dev_name(dev));
	ptp_info->owner = THIS_MODULE;

	clock = ptp_clock_register(ptp_info, dev);
	if (IS_ERR(clock))
		return PTR_ERR(clock);

	adapter->ptp.clock = clock;

	dev_dbg(&adapter->pdev->dev, "PTP clock %s registered\n",
		adapter->ptp.info.name);

	return 0;
}

/**
 * iavf_ptp_init - Initialize PTP support if capability was negotiated
 * @adapter: private adapter structure
 *
 * Initialize PTP functionality, based on the capabilities that the PF has
 * enabled for this VF.
 */
void iavf_ptp_init(struct iavf_adapter *adapter)
{
	int err;

	if (!iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_READ_PHC)) {
		pci_notice(adapter->pdev,
			   "Device does not have PTP clock support\n");
		return;
	}

	err = iavf_ptp_register_clock(adapter);
	if (err) {
		pci_err(adapter->pdev,
			"Failed to register PTP clock device (%p)\n",
			ERR_PTR(err));
		return;
	}

	for (int i = 0; i < adapter->num_active_queues; i++) {
		struct iavf_ring *rx_ring = &adapter->rx_rings[i];

		rx_ring->ptp = &adapter->ptp;
	}
}

/**
 * iavf_ptp_release - Disable PTP support
 * @adapter: private adapter structure
 *
 * Release all PTP resources that were previously initialized.
 */
void iavf_ptp_release(struct iavf_adapter *adapter)
{
	if (!adapter->ptp.clock)
		return;

	pci_dbg(adapter->pdev, "removing PTP clock %s\n",
		adapter->ptp.info.name);
	ptp_clock_unregister(adapter->ptp.clock);
	adapter->ptp.clock = NULL;
}

/**
 * iavf_ptp_process_caps - Handle change in PTP capabilities
 * @adapter: private adapter structure
 *
 * Handle any state changes necessary due to change in PTP capabilities, such
 * as after a device reset or change in configuration from the PF.
 */
void iavf_ptp_process_caps(struct iavf_adapter *adapter)
{
	bool phc = iavf_ptp_cap_supported(adapter, VIRTCHNL_1588_PTP_CAP_READ_PHC);

	/* Check if the device gained or lost necessary access to support the
	 * PTP hardware clock. If so, driver must respond appropriately by
	 * creating or destroying the PTP clock device.
	 */
	if (adapter->ptp.clock && !phc)
		iavf_ptp_release(adapter);
	else if (!adapter->ptp.clock && phc)
		iavf_ptp_init(adapter);
}
