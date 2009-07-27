/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#pragma once


/* Versioning definitions used for guests reporting themselves to the */
/* hypervisor, and visa versa. */
/* ================================================================== */



/* Version info reported by guest OS's */

typedef enum _HV_GUEST_OS_VENDOR
{
    HvGuestOsVendorMicrosoft        = 0x0001

} HV_GUEST_OS_VENDOR, *PHV_GUEST_OS_VENDOR;

typedef enum _HV_GUEST_OS_MICROSOFT_IDS
{
    HvGuestOsMicrosoftUndefined     = 0x00,
    HvGuestOsMicrosoftMSDOS         = 0x01,
    HvGuestOsMicrosoftWindows3x     = 0x02,
    HvGuestOsMicrosoftWindows9x     = 0x03,
    HvGuestOsMicrosoftWindowsNT     = 0x04,
    HvGuestOsMicrosoftWindowsCE     = 0x05

} HV_GUEST_OS_MICROSOFT_IDS, *PHV_GUEST_OS_MICROSOFT_IDS;


/* Declare the MSR used to identify the guest OS. */

#define HV_X64_MSR_GUEST_OS_ID 0x40000000

typedef union _HV_X64_MSR_GUEST_OS_ID_CONTENTS
{
    u64 AsUINT64;
    struct
    {
        u64 BuildNumber    : 16;
        u64 ServiceVersion : 8; /* Service Pack, etc. */
        u64 MinorVersion   : 8;
        u64 MajorVersion   : 8;
        u64 OsId           : 8; /* HV_GUEST_OS_MICROSOFT_IDS (If Vendor=MS) */
        u64 VendorId       : 16; /* HV_GUEST_OS_VENDOR */
    };
} HV_X64_MSR_GUEST_OS_ID_CONTENTS, *PHV_X64_MSR_GUEST_OS_ID_CONTENTS;


/* Declare the MSR used to setup pages used to communicate with the hypervisor. */

#define HV_X64_MSR_HYPERCALL 0x40000001

typedef union _HV_X64_MSR_HYPERCALL_CONTENTS
{
    u64 AsUINT64;
    struct
    {
        u64 Enable               : 1;
        u64 Reserved             : 11;
        u64 GuestPhysicalAddress : 52;
    };
} HV_X64_MSR_HYPERCALL_CONTENTS, *PHV_X64_MSR_HYPERCALL_CONTENTS;
