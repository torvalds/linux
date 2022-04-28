/*************************************************************************/ /*!
@File           vmm_impl.h
@Title          Common VM manager API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides common VM manager definitions that need to
                be shared by system virtualization layer itself and modules that
                implement the actual VM manager types.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef VMM_IMPL_H
#define VMM_IMPL_H

#include "img_types.h"
#include "pvrsrv_error.h"

typedef enum _VMM_CONF_PARAM_
{
	VMM_CONF_PRIO_OSID0 = 0,
	VMM_CONF_PRIO_OSID1 = 1,
	VMM_CONF_PRIO_OSID2 = 2,
	VMM_CONF_PRIO_OSID3 = 3,
	VMM_CONF_PRIO_OSID4 = 4,
	VMM_CONF_PRIO_OSID5 = 5,
	VMM_CONF_PRIO_OSID6 = 6,
	VMM_CONF_PRIO_OSID7 = 7,
	VMM_CONF_HCS_DEADLINE = 8
} VMM_CONF_PARAM;

/*
	Virtual machine manager (hypervisor) para-virtualization (PVZ) connection:
		- Type is implemented by host and guest drivers
			- Assumes synchronous function call semantics
			- Unidirectional semantics
				- For Host  (vmm -> host)
				- For Guest (guest -> vmm)
			- Parameters can be IN/OUT/INOUT

		- Host pvz entries are pre-implemented by IMG
			- For host implementation, see vmm_pvz_server.c
			- Called by host side hypercall handler or VMM

		- Guest pvz entries are supplied by 3rd-party
			- These are specific to hypervisor (VMM) type
			- These implement the actual hypercalls mechanism

	Para-virtualization (PVZ) call runtime sequence:
		1 - Guest driver in guest VM calls PVZ function
		1.1 - Guest PVZ connection calls
		1.2 - Guest VM Manager type which
		1.2.1 - Performs any pre-processing like parameter packing, etc.
		1.2.2 - Issues hypercall (blocking synchronous call)

		2 - VM Manager (hypervisor) receives hypercall
		2.1 - Hypercall handler:
		2.1.1 - Performs any pre-processing
		2.1.2 - If call terminates in VM Manager: perform action and return from hypercall
		2.1.3 - Otherwise forward to host driver (implementation specific call)

		3 - Host driver receives call from VM Manager
		3.1 - Host VM manager type:
		3.1.1 - Performs any pre-processing like parameter unpacking, etc.
		3.1.2 - Acquires host driver PVZ handler and calls the appropriate entry
		3.2 - Host PVZ connection calls corresponding host system virtualisation layer
		3.3 - Host driver system virtualisation layer:
		3.3.1 - Perform action requested by guest driver
		3.3.2 - Return to host VM Manager type
		3.4 - Host VM Manager type:
		3.4.1 - Prepare to return from hypercall
		3.4.2 - Perform any post-processing like result packing, etc.
		3.4.3 - Issue return from hypercall

		4 - VM Manager (hypervisor)
		4.1 - Perform any post-processing
		4.2 - Return control to guest driver

		5 - Guest driver in guest VM
		5.1 - Perform any post-processing like parameter unpacking, etc.
		5.2 - Continue execution in guest VM
 */
typedef struct _VMM_PVZ_CONNECTION_
{
	struct {
		/*
		   This pair must be implemented if the guest is responsible
		   for allocating the physical heap that backs its firmware
		   allocations, this is the default configuration. The physical
		   heap is allocated within the guest VM IPA space and this
		   IPA Addr/Size must be re-expressed as PA space Addr/Size
		   by the VM manager before forwarding request to host.
		   If not implemented, return PVRSRV_ERROR_NOT_IMPLEMENTED.
		 */
		PVRSRV_ERROR (*pfnMapDevPhysHeap)(IMG_UINT32 ui32FuncID,
										  IMG_UINT32 ui32DevID,
										  IMG_UINT64 ui64Size,
										  IMG_UINT64 ui64PAddr);

		PVRSRV_ERROR (*pfnUnmapDevPhysHeap)(IMG_UINT32 ui32FuncID,
											IMG_UINT32 ui32DevID);
	} sClientFuncTab;

	struct {
		/*
			Corresponding server side entries to handle guest PVZ calls
			NOTE:
				 - Additional PVZ function ui32OSID parameter
					 - OSID determination is responsibility of VM manager
					 - Actual OSID value must be supplied by VM manager
						- This can be done either in client/VMM/host side
					 - Must be done before host pvz function(s) are called
					 - Host pvz function validates incoming OSID values
		 */
		PVRSRV_ERROR (*pfnMapDevPhysHeap)(IMG_UINT32 ui32OSID,
										  IMG_UINT32 ui32FuncID,
										  IMG_UINT32 ui32DevID,
										  IMG_UINT64 ui64Size,
										  IMG_UINT64 ui64PAddr);

		PVRSRV_ERROR (*pfnUnmapDevPhysHeap)(IMG_UINT32 ui32OSID,
											IMG_UINT32 ui32FuncID,
											IMG_UINT32 ui32DevID);
	} sServerFuncTab;

	struct {
		/*
		   This is used by the VM manager to report pertinent runtime guest VM
		   information to the host; these events may in turn be forwarded to
		   the firmware
		 */
		PVRSRV_ERROR (*pfnOnVmOnline)(IMG_UINT32 ui32OSID);

		PVRSRV_ERROR (*pfnOnVmOffline)(IMG_UINT32 ui32OSID);

		PVRSRV_ERROR (*pfnVMMConfigure)(VMM_CONF_PARAM eVMMParamType, IMG_UINT32 ui32ParamValue);

	} sVmmFuncTab;
} VMM_PVZ_CONNECTION;

/*!
*******************************************************************************
 @Function      VMMCreatePvzConnection() and VMMDestroyPvzConnection()
 @Description   Both the guest and VM manager call this in order to obtain a
                PVZ connection to the VM and host respectively; that is, guest
                calls it to obtain connection to VM, VM calls it to obtain a
                connection to the host.
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR VMMCreatePvzConnection(VMM_PVZ_CONNECTION **psPvzConnection);
void VMMDestroyPvzConnection(VMM_PVZ_CONNECTION *psPvzConnection);

#endif /* VMM_IMPL_H */
