/*
    comedi/drivers/comedi_pci.h
    Various PCI functions for drivers.

    Copyright (C) 2007 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _COMEDI_PCI_H_
#define _COMEDI_PCI_H_

#include <linux/pci.h>

/*
 * Enable the PCI device and request the regions.
 */
static inline int comedi_pci_enable(struct pci_dev *pdev, const char *res_name)
{
	int rc;

	rc = pci_enable_device(pdev);
	if (rc < 0)
		return rc;

	rc = pci_request_regions(pdev, res_name);
	if (rc < 0)
		pci_disable_device(pdev);

	return rc;
}

/*
 * Release the regions and disable the PCI device.
 *
 * This must be matched with a previous successful call to comedi_pci_enable().
 */
static inline void comedi_pci_disable(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

#endif
