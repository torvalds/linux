/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#ifndef __CXL_PCI_H__
#define __CXL_PCI_H__

#define CXL_MEMORY_PROGIF	0x10

/*
 * See section 8.1 Configuration Space Registers in the CXL 2.0
 * Specification
 */
#define PCI_DVSEC_VENDOR_ID_CXL		0x1E98
#define PCI_DVSEC_ID_CXL		0x0

#define PCI_DVSEC_ID_CXL_REGLOC_OFFSET		0x8

#endif /* __CXL_PCI_H__ */
