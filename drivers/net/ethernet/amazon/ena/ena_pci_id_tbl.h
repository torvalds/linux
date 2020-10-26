/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright 2015-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef ENA_PCI_ID_TBL_H_
#define ENA_PCI_ID_TBL_H_

#ifndef PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#endif

#ifndef PCI_DEV_ID_ENA_PF
#define PCI_DEV_ID_ENA_PF	0x0ec2
#endif

#ifndef PCI_DEV_ID_ENA_LLQ_PF
#define PCI_DEV_ID_ENA_LLQ_PF	0x1ec2
#endif

#ifndef PCI_DEV_ID_ENA_VF
#define PCI_DEV_ID_ENA_VF	0xec20
#endif

#ifndef PCI_DEV_ID_ENA_LLQ_VF
#define PCI_DEV_ID_ENA_LLQ_VF	0xec21
#endif

#ifndef PCI_DEV_ID_ENA_RESRV0
#define PCI_DEV_ID_ENA_RESRV0	0x0051
#endif

#define ENA_PCI_ID_TABLE_ENTRY(devid) \
	{PCI_DEVICE(PCI_VENDOR_ID_AMAZON, devid)},

static const struct pci_device_id ena_pci_tbl[] = {
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_RESRV0)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_PF)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_LLQ_PF)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_VF)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_LLQ_VF)
	{ }
};

#endif /* ENA_PCI_ID_TBL_H_ */
