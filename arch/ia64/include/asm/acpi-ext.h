/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * (c) Copyright 2003, 2006 Hewlett-Packard Development Company, L.P.
 *	Alex Williamson <alex.williamson@hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * Vendor specific extensions to ACPI.
 */

#ifndef _ASM_IA64_ACPI_EXT_H
#define _ASM_IA64_ACPI_EXT_H

#include <linux/types.h>

extern acpi_status hp_acpi_csr_space (acpi_handle, u64 *base, u64 *length);

#endif /* _ASM_IA64_ACPI_EXT_H */
