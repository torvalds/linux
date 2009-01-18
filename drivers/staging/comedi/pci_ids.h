/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef __COMPAT_LINUX_PCI_IDS_H
#define __COMPAT_LINUX_PCI_IDS_H

#include <linux/pci_ids.h>

#ifndef PCI_VENDOR_ID_AMCC
#define PCI_VENDOR_ID_AMCC	0x10e8
#endif

#ifndef PCI_VENDOR_ID_CBOARDS
#define PCI_VENDOR_ID_CBOARDS	0x1307
#endif

#ifndef PCI_VENDOR_ID_QUANCOM
#define PCI_VENDOR_ID_QUANCOM	0x8008
#endif

#ifndef PCI_DEVICE_ID_QUANCOM_GPIB
#define PCI_DEVICE_ID_QUANCOM_GPIB	0x3302
#endif

#endif // __COMPAT_LINUX_PCI_IDS_H
