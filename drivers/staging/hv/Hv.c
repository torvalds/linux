/*
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
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include "osd.h"
#include "logging.h"
#include "VmbusPrivate.h"

/* The one and only */
struct hv_context gHvContext = {
	.SynICInitialized	= false,
	.HypercallPage		= NULL,
	.SignalEventParam	= NULL,
	.SignalEventBuffer	= NULL,
};

/**
 * HvQueryHypervisorPresence - Query the cpuid for presense of windows hypervisor
 */
static int HvQueryHypervisorPresence(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	unsigned int op;

	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	op = HvCpuIdFunctionVersionAndFeatures;
	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ecx & HV_PRESENT_BIT;
}

/**
 * HvQueryHypervisorInfo - Get version info of the windows hypervisor
 */
static int HvQueryHypervisorInfo(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	unsigned int maxLeaf;
	unsigned int op;

	/*
	* Its assumed that this is called after confirming that Viridian
	* is present. Query id and revision.
	*/
	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	op = HvCpuIdFunctionHvVendorAndMaxFunction;
	cpuid(op, &eax, &ebx, &ecx, &edx);

	DPRINT_INFO(VMBUS, "Vendor ID: %c%c%c%c%c%c%c%c%c%c%c%c",
		    (ebx & 0xFF),
		    ((ebx >> 8) & 0xFF),
		    ((ebx >> 16) & 0xFF),
		    ((ebx >> 24) & 0xFF),
		    (ecx & 0xFF),
		    ((ecx >> 8) & 0xFF),
		    ((ecx >> 16) & 0xFF),
		    ((ecx >> 24) & 0xFF),
		    (edx & 0xFF),
		    ((edx >> 8) & 0xFF),
		    ((edx >> 16) & 0xFF),
		    ((edx >> 24) & 0xFF));

	maxLeaf = eax;
	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	op = HvCpuIdFunctionHvInterface;
	cpuid(op, &eax, &ebx, &ecx, &edx);

	DPRINT_INFO(VMBUS, "Interface ID: %c%c%c%c",
		    (eax & 0xFF),
		    ((eax >> 8) & 0xFF),
		    ((eax >> 16) & 0xFF),
		    ((eax >> 24) & 0xFF));

	if (maxLeaf >= HvCpuIdFunctionMsHvVersion) {
		eax = 0;
		ebx = 0;
		ecx = 0;
		edx = 0;
		op = HvCpuIdFunctionMsHvVersion;
		cpuid(op, &eax, &ebx, &ecx, &edx);
		DPRINT_INFO(VMBUS, "OS Build:%d-%d.%d-%d-%d.%d",\
			    eax,
			    ebx >> 16,
			    ebx & 0xFFFF,
			    ecx,
			    edx >> 24,
			    edx & 0xFFFFFF);
	}
	return maxLeaf;
}

/**
 * HvDoHypercall - Invoke the specified hypercall
 */
static u64 HvDoHypercall(u64 Control, void *Input, void *Output)
{
#ifdef CONFIG_X86_64
	u64 hvStatus = 0;
	u64 inputAddress = (Input) ? virt_to_phys(Input) : 0;
	u64 outputAddress = (Output) ? virt_to_phys(Output) : 0;
	volatile void *hypercallPage = gHvContext.HypercallPage;

	DPRINT_DBG(VMBUS, "Hypercall <control %llx input phys %llx virt %p "
		   "output phys %llx virt %p hypercall %p>",
		   Control, inputAddress, Input,
		   outputAddress, Output, hypercallPage);

	__asm__ __volatile__("mov %0, %%r8" : : "r" (outputAddress) : "r8");
	__asm__ __volatile__("call *%3" : "=a" (hvStatus) :
			     "c" (Control), "d" (inputAddress),
			     "m" (hypercallPage));

	DPRINT_DBG(VMBUS, "Hypercall <return %llx>",  hvStatus);

	return hvStatus;

#else

	u32 controlHi = Control >> 32;
	u32 controlLo = Control & 0xFFFFFFFF;
	u32 hvStatusHi = 1;
	u32 hvStatusLo = 1;
	u64 inputAddress = (Input) ? virt_to_phys(Input) : 0;
	u32 inputAddressHi = inputAddress >> 32;
	u32 inputAddressLo = inputAddress & 0xFFFFFFFF;
	u64 outputAddress = (Output) ? virt_to_phys(Output) : 0;
	u32 outputAddressHi = outputAddress >> 32;
	u32 outputAddressLo = outputAddress & 0xFFFFFFFF;
	volatile void *hypercallPage = gHvContext.HypercallPage;

	DPRINT_DBG(VMBUS, "Hypercall <control %llx input %p output %p>",
		   Control, Input, Output);

	__asm__ __volatile__ ("call *%8" : "=d"(hvStatusHi),
			      "=a"(hvStatusLo) : "d" (controlHi),
			      "a" (controlLo), "b" (inputAddressHi),
			      "c" (inputAddressLo), "D"(outputAddressHi),
			      "S"(outputAddressLo), "m" (hypercallPage));

	DPRINT_DBG(VMBUS, "Hypercall <return %llx>",
		   hvStatusLo | ((u64)hvStatusHi << 32));

	return hvStatusLo | ((u64)hvStatusHi << 32);
#endif /* !x86_64 */
}

/**
 * HvInit - Main initialization routine.
 *
 * This routine must be called before any other routines in here are called
 */
int HvInit(void)
{
	int ret = 0;
	int maxLeaf;
	union hv_x64_msr_hypercall_contents hypercallMsr;
	void *virtAddr = NULL;

	DPRINT_ENTER(VMBUS);

	memset(gHvContext.synICEventPage, 0, sizeof(void *) * MAX_NUM_CPUS);
	memset(gHvContext.synICMessagePage, 0, sizeof(void *) * MAX_NUM_CPUS);

	if (!HvQueryHypervisorPresence()) {
		DPRINT_ERR(VMBUS, "No Windows hypervisor detected!!");
		goto Cleanup;
	}

	DPRINT_INFO(VMBUS,
		    "Windows hypervisor detected! Retrieving more info...");

	maxLeaf = HvQueryHypervisorInfo();
	/* HvQueryHypervisorFeatures(maxLeaf); */

	/*
	 * Determine if we are running on xenlinux (ie x2v shim) or native
	 * linux
	 */
	rdmsrl(HV_X64_MSR_GUEST_OS_ID, gHvContext.GuestId);
	if (gHvContext.GuestId == 0) {
		/* Write our OS info */
		wrmsrl(HV_X64_MSR_GUEST_OS_ID, HV_LINUX_GUEST_ID);
		gHvContext.GuestId = HV_LINUX_GUEST_ID;
	}

	/* See if the hypercall page is already set */
	rdmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.AsUINT64);
	if (gHvContext.GuestId == HV_LINUX_GUEST_ID) {
		/* Allocate the hypercall page memory */
		/* virtAddr = osd_PageAlloc(1); */
		virtAddr = osd_VirtualAllocExec(PAGE_SIZE);

		if (!virtAddr) {
			DPRINT_ERR(VMBUS,
				   "unable to allocate hypercall page!!");
			goto Cleanup;
		}

		hypercallMsr.Enable = 1;
		/* hypercallMsr.GuestPhysicalAddress =
		 * 		virt_to_phys(virtAddr) >> PAGE_SHIFT; */
		hypercallMsr.GuestPhysicalAddress = vmalloc_to_pfn(virtAddr);
		wrmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.AsUINT64);

		/* Confirm that hypercall page did get setup. */
		hypercallMsr.AsUINT64 = 0;
		rdmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.AsUINT64);
		if (!hypercallMsr.Enable) {
			DPRINT_ERR(VMBUS, "unable to set hypercall page!!");
			goto Cleanup;
		}

		gHvContext.HypercallPage = virtAddr;
	} else {
		DPRINT_ERR(VMBUS, "Unknown guest id (0x%llx)!!",
				gHvContext.GuestId);
		goto Cleanup;
	}

	DPRINT_INFO(VMBUS, "Hypercall page VA=%p, PA=0x%0llx",
		    gHvContext.HypercallPage,
		    (u64)hypercallMsr.GuestPhysicalAddress << PAGE_SHIFT);

	/* Setup the global signal event param for the signal event hypercall */
	gHvContext.SignalEventBuffer =
			kmalloc(sizeof(struct hv_input_signal_event_buffer),
				GFP_KERNEL);
	if (!gHvContext.SignalEventBuffer)
		goto Cleanup;

	gHvContext.SignalEventParam =
		(struct hv_input_signal_event *)
			(ALIGN_UP((unsigned long)gHvContext.SignalEventBuffer,
				  HV_HYPERCALL_PARAM_ALIGN));
	gHvContext.SignalEventParam->ConnectionId.Asu32 = 0;
	gHvContext.SignalEventParam->ConnectionId.u.Id =
						VMBUS_EVENT_CONNECTION_ID;
	gHvContext.SignalEventParam->FlagNumber = 0;
	gHvContext.SignalEventParam->RsvdZ = 0;

	/* DPRINT_DBG(VMBUS, "My id %llu", HvGetCurrentPartitionId()); */

	DPRINT_EXIT(VMBUS);

	return ret;

Cleanup:
	if (virtAddr) {
		if (hypercallMsr.Enable) {
			hypercallMsr.AsUINT64 = 0;
			wrmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.AsUINT64);
		}

		vfree(virtAddr);
	}
	ret = -1;
	DPRINT_EXIT(VMBUS);

	return ret;
}

/**
 * HvCleanup - Cleanup routine.
 *
 * This routine is called normally during driver unloading or exiting.
 */
void HvCleanup(void)
{
	union hv_x64_msr_hypercall_contents hypercallMsr;

	DPRINT_ENTER(VMBUS);

	if (gHvContext.SignalEventBuffer) {
		gHvContext.SignalEventBuffer = NULL;
		gHvContext.SignalEventParam = NULL;
		kfree(gHvContext.SignalEventBuffer);
	}

	if (gHvContext.GuestId == HV_LINUX_GUEST_ID) {
		if (gHvContext.HypercallPage) {
			hypercallMsr.AsUINT64 = 0;
			wrmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.AsUINT64);
			vfree(gHvContext.HypercallPage);
			gHvContext.HypercallPage = NULL;
		}
	}

	DPRINT_EXIT(VMBUS);

}

/**
 * HvPostMessage - Post a message using the hypervisor message IPC.
 *
 * This involves a hypercall.
 */
u16 HvPostMessage(union hv_connection_id connectionId,
		  enum hv_message_type messageType,
		  void *payload, size_t payloadSize)
{
	struct alignedInput {
		u64 alignment8;
		struct hv_input_post_message msg;
	};

	struct hv_input_post_message *alignedMsg;
	u16 status;
	unsigned long addr;

	if (payloadSize > HV_MESSAGE_PAYLOAD_BYTE_COUNT)
		return -1;

	addr = (unsigned long)kmalloc(sizeof(struct alignedInput), GFP_ATOMIC);
	if (!addr)
		return -1;

	alignedMsg = (struct hv_input_post_message *)
			(ALIGN_UP(addr, HV_HYPERCALL_PARAM_ALIGN));

	alignedMsg->ConnectionId = connectionId;
	alignedMsg->MessageType = messageType;
	alignedMsg->PayloadSize = payloadSize;
	memcpy((void *)alignedMsg->Payload, payload, payloadSize);

	status = HvDoHypercall(HvCallPostMessage, alignedMsg, NULL) & 0xFFFF;

	kfree((void *)addr);

	return status;
}


/**
 * HvSignalEvent - Signal an event on the specified connection using the hypervisor event IPC.
 *
 * This involves a hypercall.
 */
u16 HvSignalEvent(void)
{
	u16 status;

	status = HvDoHypercall(HvCallSignalEvent, gHvContext.SignalEventParam,
			       NULL) & 0xFFFF;
	return status;
}

/**
 * HvSynicInit - Initialize the Synthethic Interrupt Controller.
 *
 * If it is already initialized by another entity (ie x2v shim), we need to
 * retrieve the initialized message and event pages.  Otherwise, we create and
 * initialize the message and event pages.
 */
int HvSynicInit(u32 irqVector)
{
	u64 version;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_sint sharedSint;
	union hv_synic_scontrol sctrl;
	u64 guestID;
	int ret = 0;

	DPRINT_ENTER(VMBUS);

	if (!gHvContext.HypercallPage) {
		DPRINT_EXIT(VMBUS);
		return ret;
	}

	/* Check the version */
	rdmsrl(HV_X64_MSR_SVERSION, version);

	DPRINT_INFO(VMBUS, "SynIC version: %llx", version);

	/* TODO: Handle SMP */
	if (gHvContext.GuestId == HV_XENLINUX_GUEST_ID) {
		DPRINT_INFO(VMBUS, "Skipping SIMP and SIEFP setup since "
				"it is already set.");

		rdmsrl(HV_X64_MSR_SIMP, simp.AsUINT64);
		rdmsrl(HV_X64_MSR_SIEFP, siefp.AsUINT64);

		DPRINT_DBG(VMBUS, "Simp: %llx, Sifep: %llx",
			   simp.AsUINT64, siefp.AsUINT64);

		/*
		 * Determine if we are running on xenlinux (ie x2v shim) or
		 * native linux
		 */
		rdmsrl(HV_X64_MSR_GUEST_OS_ID, guestID);
		if (guestID == HV_LINUX_GUEST_ID) {
			gHvContext.synICMessagePage[0] =
				phys_to_virt(simp.BaseSimpGpa << PAGE_SHIFT);
			gHvContext.synICEventPage[0] =
				phys_to_virt(siefp.BaseSiefpGpa << PAGE_SHIFT);
		} else {
			DPRINT_ERR(VMBUS, "unknown guest id!!");
			goto Cleanup;
		}
		DPRINT_DBG(VMBUS, "MAPPED: Simp: %p, Sifep: %p",
			   gHvContext.synICMessagePage[0],
			   gHvContext.synICEventPage[0]);
	} else {
		gHvContext.synICMessagePage[0] = osd_PageAlloc(1);
		if (gHvContext.synICMessagePage[0] == NULL) {
			DPRINT_ERR(VMBUS,
				   "unable to allocate SYNIC message page!!");
			goto Cleanup;
		}

		gHvContext.synICEventPage[0] = osd_PageAlloc(1);
		if (gHvContext.synICEventPage[0] == NULL) {
			DPRINT_ERR(VMBUS,
				   "unable to allocate SYNIC event page!!");
			goto Cleanup;
		}

		/* Setup the Synic's message page */
		rdmsrl(HV_X64_MSR_SIMP, simp.AsUINT64);
		simp.SimpEnabled = 1;
		simp.BaseSimpGpa = virt_to_phys(gHvContext.synICMessagePage[0])
					>> PAGE_SHIFT;

		DPRINT_DBG(VMBUS, "HV_X64_MSR_SIMP msr set to: %llx",
				simp.AsUINT64);

		wrmsrl(HV_X64_MSR_SIMP, simp.AsUINT64);

		/* Setup the Synic's event page */
		rdmsrl(HV_X64_MSR_SIEFP, siefp.AsUINT64);
		siefp.SiefpEnabled = 1;
		siefp.BaseSiefpGpa = virt_to_phys(gHvContext.synICEventPage[0])
					>> PAGE_SHIFT;

		DPRINT_DBG(VMBUS, "HV_X64_MSR_SIEFP msr set to: %llx",
				siefp.AsUINT64);

		wrmsrl(HV_X64_MSR_SIEFP, siefp.AsUINT64);
	}

	/* Setup the interception SINT. */
	/* wrmsrl((HV_X64_MSR_SINT0 + HV_SYNIC_INTERCEPTION_SINT_INDEX), */
	/*	  interceptionSint.AsUINT64); */

	/* Setup the shared SINT. */
	rdmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.AsUINT64);

	sharedSint.AsUINT64 = 0;
	sharedSint.Vector = irqVector; /* HV_SHARED_SINT_IDT_VECTOR + 0x20; */
	sharedSint.Masked = false;
	sharedSint.AutoEoi = true;

	DPRINT_DBG(VMBUS, "HV_X64_MSR_SINT1 msr set to: %llx",
		   sharedSint.AsUINT64);

	wrmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.AsUINT64);

	/* Enable the global synic bit */
	rdmsrl(HV_X64_MSR_SCONTROL, sctrl.AsUINT64);
	sctrl.Enable = 1;

	wrmsrl(HV_X64_MSR_SCONTROL, sctrl.AsUINT64);

	gHvContext.SynICInitialized = true;

	DPRINT_EXIT(VMBUS);

	return ret;

Cleanup:
	ret = -1;

	if (gHvContext.GuestId == HV_LINUX_GUEST_ID) {
		if (gHvContext.synICEventPage[0])
			osd_PageFree(gHvContext.synICEventPage[0], 1);

		if (gHvContext.synICMessagePage[0])
			osd_PageFree(gHvContext.synICMessagePage[0], 1);
	}

	DPRINT_EXIT(VMBUS);

	return ret;
}

/**
 * HvSynicCleanup - Cleanup routine for HvSynicInit().
 */
void HvSynicCleanup(void)
{
	union hv_synic_sint sharedSint;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;

	DPRINT_ENTER(VMBUS);

	if (!gHvContext.SynICInitialized) {
		DPRINT_EXIT(VMBUS);
		return;
	}

	rdmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.AsUINT64);

	sharedSint.Masked = 1;

	/* Disable the interrupt */
	wrmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.AsUINT64);

	/*
	 * Disable and free the resources only if we are running as
	 * native linux since in xenlinux, we are sharing the
	 * resources with the x2v shim
	 */
	if (gHvContext.GuestId == HV_LINUX_GUEST_ID) {
		rdmsrl(HV_X64_MSR_SIMP, simp.AsUINT64);
		simp.SimpEnabled = 0;
		simp.BaseSimpGpa = 0;

		wrmsrl(HV_X64_MSR_SIMP, simp.AsUINT64);

		rdmsrl(HV_X64_MSR_SIEFP, siefp.AsUINT64);
		siefp.SiefpEnabled = 0;
		siefp.BaseSiefpGpa = 0;

		wrmsrl(HV_X64_MSR_SIEFP, siefp.AsUINT64);

		osd_PageFree(gHvContext.synICMessagePage[0], 1);
		osd_PageFree(gHvContext.synICEventPage[0], 1);
	}

	DPRINT_EXIT(VMBUS);
}
