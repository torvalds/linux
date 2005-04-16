/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_PCIBUS_PROVIDER_H
#define _ASM_IA64_SN_PCI_PCIBUS_PROVIDER_H

/*
 * SN pci asic types.  Do not ever renumber these or reuse values.  The
 * values must agree with what prom thinks they are.
 */

#define PCIIO_ASIC_TYPE_UNKNOWN	0
#define PCIIO_ASIC_TYPE_PPB	1
#define PCIIO_ASIC_TYPE_PIC	2
#define PCIIO_ASIC_TYPE_TIOCP	3

/*
 * Common pciio bus provider data.  There should be one of these as the
 * first field in any pciio based provider soft structure (e.g. pcibr_soft
 * tioca_soft, etc).
 */

struct pcibus_bussoft {
	uint32_t		bs_asic_type;	/* chipset type */
	uint32_t		bs_xid;		/* xwidget id */
	uint64_t		bs_persist_busnum; /* Persistent Bus Number */
	uint64_t		bs_legacy_io;	/* legacy io pio addr */
	uint64_t		bs_legacy_mem;	/* legacy mem pio addr */
	uint64_t		bs_base;	/* widget base */
	struct xwidget_info	*bs_xwidget_info;
};

/*
 * DMA mapping flags
 */

#define SN_PCIDMA_CONSISTENT    0x0001

#endif				/* _ASM_IA64_SN_PCI_PCIBUS_PROVIDER_H */
