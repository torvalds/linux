/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#ifndef __CXL_PCI_H__
#define __CXL_PCI_H__
#include <linux/pci.h>
#include "cxl.h"

#define CXL_MEMORY_PROGIF	0x10

/*
 * See section 8.1 Configuration Space Registers in the CXL 2.0
 * Specification. Names are taken straight from the specification with "CXL" and
 * "DVSEC" redundancies removed. When obvious, abbreviations may be used.
 */
#define PCI_DVSEC_HEADER1_LENGTH_MASK	GENMASK(31, 20)
#define PCI_DVSEC_VENDOR_ID_CXL		0x1E98

/* CXL 2.0 8.1.3: PCIe DVSEC for CXL Device */
#define CXL_DVSEC_PCIE_DEVICE					0
#define   CXL_DVSEC_CAP_OFFSET		0xA
#define     CXL_DVSEC_MEM_CAPABLE	BIT(2)
#define     CXL_DVSEC_HDM_COUNT_MASK	GENMASK(5, 4)
#define   CXL_DVSEC_CTRL_OFFSET		0xC
#define     CXL_DVSEC_MEM_ENABLE	BIT(2)
#define   CXL_DVSEC_RANGE_SIZE_HIGH(i)	(0x18 + (i * 0x10))
#define   CXL_DVSEC_RANGE_SIZE_LOW(i)	(0x1C + (i * 0x10))
#define     CXL_DVSEC_MEM_INFO_VALID	BIT(0)
#define     CXL_DVSEC_MEM_ACTIVE	BIT(1)
#define     CXL_DVSEC_MEM_SIZE_LOW_MASK	GENMASK(31, 28)
#define   CXL_DVSEC_RANGE_BASE_HIGH(i)	(0x20 + (i * 0x10))
#define   CXL_DVSEC_RANGE_BASE_LOW(i)	(0x24 + (i * 0x10))
#define     CXL_DVSEC_MEM_BASE_LOW_MASK	GENMASK(31, 28)

#define CXL_DVSEC_RANGE_MAX		2

/* CXL 2.0 8.1.4: Non-CXL Function Map DVSEC */
#define CXL_DVSEC_FUNCTION_MAP					2

/* CXL 2.0 8.1.5: CXL 2.0 Extensions DVSEC for Ports */
#define CXL_DVSEC_PORT_EXTENSIONS				3

/* CXL 2.0 8.1.6: GPF DVSEC for CXL Port */
#define CXL_DVSEC_PORT_GPF					4

/* CXL 2.0 8.1.7: GPF DVSEC for CXL Device */
#define CXL_DVSEC_DEVICE_GPF					5

/* CXL 2.0 8.1.8: PCIe DVSEC for Flex Bus Port */
#define CXL_DVSEC_PCIE_FLEXBUS_PORT				7

/* CXL 2.0 8.1.9: Register Locator DVSEC */
#define CXL_DVSEC_REG_LOCATOR					8
#define   CXL_DVSEC_REG_LOCATOR_BLOCK1_OFFSET			0xC
#define     CXL_DVSEC_REG_LOCATOR_BIR_MASK			GENMASK(2, 0)
#define	    CXL_DVSEC_REG_LOCATOR_BLOCK_ID_MASK			GENMASK(15, 8)
#define     CXL_DVSEC_REG_LOCATOR_BLOCK_OFF_LOW_MASK		GENMASK(31, 16)

/*
 * NOTE: Currently all the functions which are enabled for CXL require their
 * vectors to be in the first 16.  Use this as the default max.
 */
#define CXL_PCI_DEFAULT_MAX_VECTORS 16

/* Register Block Identifier (RBI) */
enum cxl_regloc_type {
	CXL_REGLOC_RBI_EMPTY = 0,
	CXL_REGLOC_RBI_COMPONENT,
	CXL_REGLOC_RBI_VIRT,
	CXL_REGLOC_RBI_MEMDEV,
	CXL_REGLOC_RBI_PMU,
	CXL_REGLOC_RBI_TYPES
};

struct cdat_header {
	__le32 length;
	u8 revision;
	u8 checksum;
	u8 reserved[6];
	__le32 sequence;
} __packed;

struct cdat_entry_header {
	u8 type;
	u8 reserved;
	__le16 length;
} __packed;

/*
 * CXL v3.0 6.2.3 Table 6-4
 * The table indicates that if PCIe Flit Mode is set, then CXL is in 256B flits
 * mode, otherwise it's 68B flits mode.
 */
static inline bool cxl_pci_flit_256(struct pci_dev *pdev)
{
	u16 lnksta2;

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA2, &lnksta2);
	return lnksta2 & PCI_EXP_LNKSTA2_FLIT;
}

int devm_cxl_port_enumerate_dports(struct cxl_port *port);
struct cxl_dev_state;
int cxl_hdm_decode_init(struct cxl_dev_state *cxlds, struct cxl_hdm *cxlhdm,
			struct cxl_endpoint_dvsec_info *info);
void read_cdat_data(struct cxl_port *port);
void cxl_cor_error_detected(struct pci_dev *pdev);
pci_ers_result_t cxl_error_detected(struct pci_dev *pdev,
				    pci_channel_state_t state);
#endif /* __CXL_PCI_H__ */
