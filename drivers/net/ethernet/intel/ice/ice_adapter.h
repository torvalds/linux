/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Red Hat */

#ifndef _ICE_ADAPTER_H_
#define _ICE_ADAPTER_H_

#include <linux/refcount_types.h>

struct pci_dev;

/**
 * struct ice_adapter - PCI adapter resources shared across PFs
 * @refcount: Reference count. struct ice_pf objects hold the references.
 */
struct ice_adapter {
	refcount_t refcount;
};

struct ice_adapter *ice_adapter_get(const struct pci_dev *pdev);
void ice_adapter_put(const struct pci_dev *pdev);

#endif /* _ICE_ADAPTER_H */
