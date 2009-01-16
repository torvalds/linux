/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_ACPI_H
#define _ASM_IA64_SN_ACPI_H

extern int sn_acpi_rev;
#define SN_ACPI_BASE_SUPPORT()   (sn_acpi_rev >= 0x20101)

#endif /* _ASM_IA64_SN_ACPI_H */
