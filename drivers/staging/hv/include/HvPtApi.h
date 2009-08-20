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

#ifndef __HVVPTPI_H
#define __HVVPTPI_H

/*
 * Versioning definitions used for guests reporting themselves to the
 * hypervisor, and visa versa.
 */

/* Version info reported by guest OS's */
enum hv_guest_os_vendor {
	HvGuestOsVendorMicrosoft	= 0x0001
};

enum hv_guest_os_microsoft_ids {
	HvGuestOsMicrosoftUndefined	= 0x00,
	HvGuestOsMicrosoftMSDOS		= 0x01,
	HvGuestOsMicrosoftWindows3x	= 0x02,
	HvGuestOsMicrosoftWindows9x	= 0x03,
	HvGuestOsMicrosoftWindowsNT	= 0x04,
	HvGuestOsMicrosoftWindowsCE	= 0x05
};

/*
 * Declare the MSR used to identify the guest OS.
 */
#define HV_X64_MSR_GUEST_OS_ID	0x40000000

union hv_x64_msr_guest_os_id_contents {
	u64 AsUINT64;
	struct {
		u64 BuildNumber:16;
		u64 ServiceVersion:8; /* Service Pack, etc. */
		u64 MinorVersion:8;
		u64 MajorVersion:8;
		u64 OsId:8; /* enum hv_guest_os_microsoft_ids (if Vendor=MS) */
		u64 VendorId:16; /* enum hv_guest_os_vendor */
	};
};

/*
 * Declare the MSR used to setup pages used to communicate with the hypervisor.
 */
#define HV_X64_MSR_HYPERCALL	0x40000001

union hv_x64_msr_hypercall_contents {
	u64 AsUINT64;
	struct {
		u64 Enable:1;
		u64 Reserved:11;
		u64 GuestPhysicalAddress:52;
	};
};

#endif
