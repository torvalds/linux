/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

#define ENA_PCI_ID_TABLE_ENTRY(devid) \
	{PCI_DEVICE(PCI_VENDOR_ID_AMAZON, devid)},

static const struct pci_device_id ena_pci_tbl[] = {
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_PF)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_LLQ_PF)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_VF)
	ENA_PCI_ID_TABLE_ENTRY(PCI_DEV_ID_ENA_LLQ_VF)
	{ }
};

#endif /* ENA_PCI_ID_TBL_H_ */
