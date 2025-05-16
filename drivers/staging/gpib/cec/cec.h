/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#include "nec7210.h"
#include "gpibP.h"
#include "plx9050.h"

struct cec_priv  {
	struct nec7210_priv nec7210_priv;
	struct pci_dev *pci_device;
	// base address for plx9052 pci chip
	unsigned long plx_iobase;
	unsigned int irq;
};

// offset between consecutive nec7210 registers
static const int cec_reg_offset = 1;
