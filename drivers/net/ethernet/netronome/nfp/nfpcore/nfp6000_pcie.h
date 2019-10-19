/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

/*
 * nfp6000_pcie.h
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#ifndef NFP6000_PCIE_H
#define NFP6000_PCIE_H

#include "nfp_cpp.h"

struct nfp_cpp *nfp_cpp_from_nfp6000_pcie(struct pci_dev *pdev);

#endif /* NFP6000_PCIE_H */
