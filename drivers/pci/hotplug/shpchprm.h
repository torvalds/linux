/*
 * SHPCHPRM : SHPCHP Resource Manager for ACPI/non-ACPI platform
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>, <kristen.c.accardi@intel.com>
 *
 */

#ifndef _SHPCHPRM_H_
#define _SHPCHPRM_H_

#ifdef	CONFIG_HOTPLUG_PCI_SHPC_PHPRM_LEGACY
#include "shpchprm_legacy.h"
#else
#include "shpchprm_nonacpi.h"
#endif

int shpchprm_init(enum php_ctlr_type ct);
void shpchprm_cleanup(void);
int shpchprm_print_pirt(void);
int shpchprm_find_available_resources(struct controller *ctrl);
int shpchprm_set_hpp(struct controller *ctrl, struct pci_func *func, u8 card_type);
void shpchprm_enable_card(struct controller *ctrl, struct pci_func *func, u8 card_type);
int shpchprm_get_physical_slot_number(struct controller *ctrl, u32 *sun, u8 busnum, u8 devnum);

#ifdef	DEBUG
#define RES_CHECK(this, bits)	\
	{ if (((this) & (bits - 1))) \
		printk("%s:%d ERR: potential res loss!\n", __FUNCTION__, __LINE__); }
#else
#define RES_CHECK(this, bits)
#endif

#endif				/* _SHPCHPRM_H_ */
