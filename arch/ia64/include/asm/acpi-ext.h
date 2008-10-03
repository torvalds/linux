/*
 * (c) Copyright 2003, 2006 Hewlett-Packard Development Company, L.P.
 *	Alex Williamson <alex.williamson@hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vendor specific extensions to ACPI.
 */

#ifndef _ASM_IA64_ACPI_EXT_H
#define _ASM_IA64_ACPI_EXT_H

#include <linux/types.h>
#include <acpi/actypes.h>

extern acpi_status hp_acpi_csr_space (acpi_handle, u64 *base, u64 *length);

#endif /* _ASM_IA64_ACPI_EXT_H */
