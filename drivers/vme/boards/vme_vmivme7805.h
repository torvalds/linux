/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vmivme_7805.h
 *
 * Support for the VMIVME-7805 board access to the Universe II bridge.
 *
 * Author: Arthur Benilov <arthur.benilov@iba-group.com>
 * Copyright 2010 Ion Beam Application, Inc.
 */


#ifndef _VMIVME_7805_H
#define _VMIVME_7805_H

#ifndef PCI_VENDOR_ID_VMIC
#define PCI_VENDOR_ID_VMIC		0x114A
#endif

#ifndef PCI_DEVICE_ID_VTIMR
#define PCI_DEVICE_ID_VTIMR		0x0004
#endif

#define VME_CONTROL			0x0000
#define BM_VME_CONTROL_MASTER_ENDIAN	0x0001
#define BM_VME_CONTROL_SLAVE_ENDIAN	0x0002
#define BM_VME_CONTROL_ABLE		0x0004
#define BM_VME_CONTROL_BERRI		0x0040
#define BM_VME_CONTROL_BERRST		0x0080
#define BM_VME_CONTROL_BPENA		0x0400
#define BM_VME_CONTROL_VBENA		0x0800

#endif /* _VMIVME_7805_H */

