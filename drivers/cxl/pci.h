/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#ifndef __CXL_PCI_H__
#define __CXL_PCI_H__

#define CXL_MEMORY_PROGIF	0x10

/*
 * See section 8.1 Configuration Space Registers in the CXL 2.0
 * Specification
 */
#define PCI_DVSEC_HEADER1_LENGTH_MASK	GENMASK(31, 20)
#define PCI_DVSEC_VENDOR_ID_CXL		0x1E98
#define PCI_DVSEC_ID_CXL		0x0

#define PCI_DVSEC_ID_CXL_REGLOC_DVSEC_ID	0x8
#define PCI_DVSEC_ID_CXL_REGLOC_BLOCK1_OFFSET	0xC

/* BAR Indicator Register (BIR) */
#define CXL_REGLOC_BIR_MASK GENMASK(2, 0)

/* Register Block Identifier (RBI) */
#define CXL_REGLOC_RBI_MASK GENMASK(15, 8)
#define CXL_REGLOC_RBI_EMPTY 0
#define CXL_REGLOC_RBI_COMPONENT 1
#define CXL_REGLOC_RBI_VIRT 2
#define CXL_REGLOC_RBI_MEMDEV 3
#define CXL_REGLOC_RBI_TYPES CXL_REGLOC_RBI_MEMDEV + 1

#define CXL_REGLOC_ADDR_MASK GENMASK(31, 16)

#endif /* __CXL_PCI_H__ */
