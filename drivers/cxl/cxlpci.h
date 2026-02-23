/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#ifndef __CXL_PCI_H__
#define __CXL_PCI_H__
#include <linux/pci.h>
#include "cxl.h"

#define CXL_MEMORY_PROGIF	0x10

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

/*
 * Table Access DOE, CDAT Read Entry Response
 *
 * Spec refs:
 *
 * CXL 3.1 8.1.11, Table 8-14: Read Entry Response
 * CDAT Specification 1.03: 2 CDAT Data Structures
 */

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
 * The DOE CDAT read response contains a CDAT read entry (either the
 * CDAT header or a structure).
 */
union cdat_data {
	struct cdat_header header;
	struct cdat_entry_header entry;
} __packed;

/* There is an additional CDAT response header of 4 bytes. */
struct cdat_doe_rsp {
	__le32 doe_header;
	u8 data[];
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

struct cxl_dev_state;
void read_cdat_data(struct cxl_port *port);

#ifdef CONFIG_CXL_RAS
void cxl_cor_error_detected(struct pci_dev *pdev);
pci_ers_result_t cxl_error_detected(struct pci_dev *pdev,
				    pci_channel_state_t state);
void devm_cxl_dport_rch_ras_setup(struct cxl_dport *dport);
void devm_cxl_port_ras_setup(struct cxl_port *port);
#else
static inline void cxl_cor_error_detected(struct pci_dev *pdev) { }

static inline pci_ers_result_t cxl_error_detected(struct pci_dev *pdev,
						  pci_channel_state_t state)
{
	return PCI_ERS_RESULT_NONE;
}

static inline void devm_cxl_dport_rch_ras_setup(struct cxl_dport *dport)
{
}

static inline void devm_cxl_port_ras_setup(struct cxl_port *port)
{
}
#endif

#endif /* __CXL_PCI_H__ */
