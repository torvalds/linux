/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Red Hat */

#ifndef _ICE_ADAPTER_H_
#define _ICE_ADAPTER_H_

#include <linux/spinlock_types.h>
#include <linux/refcount_types.h>

struct pci_dev;
struct ice_pf;

/**
 * struct ice_adapter - PCI adapter resources shared across PFs
 * @ptp_gltsyn_time_lock: Spinlock protecting access to the GLTSYN_TIME
 *                        register of the PTP clock.
 * @refcount: Reference count. struct ice_pf objects hold the references.
 * @ctrl_pf: Control PF of the adapter
 */
struct ice_adapter {
	refcount_t refcount;
	/* For access to the GLTSYN_TIME register */
	spinlock_t ptp_gltsyn_time_lock;

	struct ice_pf *ctrl_pf;
};

struct ice_adapter *ice_adapter_get(const struct pci_dev *pdev);
void ice_adapter_put(const struct pci_dev *pdev);

#endif /* _ICE_ADAPTER_H */
