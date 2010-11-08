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
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "osd.h"
#include "logging.h"
#include "vmbus_private.h"

/* The one and only */
struct hv_context gHvContext = {
	.SynICInitialized	= false,
	.HypercallPage		= NULL,
	.SignalEventParam	= NULL,
	.SignalEventBuffer	= NULL,
};

/*
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
	op = HVCPUID_VERSION_FEATURES;
	cpuid(op, &eax, &ebx, &ecx, &edx);

	return ecx & HV_PRESENT_BIT;
}

/*
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
	op = HVCPUID_VENDOR_MAXFUNCTION;
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
	op = HVCPUID_INTERFACE;
	cpuid(op, &eax, &ebx, &ecx, &edx);

	DPRINT_INFO(VMBUS, "Interface ID: %c%c%c%c",
		    (eax & 0xFF),
		    ((eax >> 8) & 0xFF),
		    ((eax >> 16) & 0xFF),
		    ((eax >> 24) & 0xFF));

	if (maxLeaf >= HVCPUID_VERSION) {
		eax = 0;
		ebx = 0;
		ecx = 0;
		edx = 0;
		op = HVCPUID_VERSION;
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

/*
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

/*
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
	 * We only support running on top of Hyper-V
	 */
	rdmsrl(HV_X64_MSR_GUEST_OS_ID, gHvContext.GuestId);

	if (gHvContext.GuestId != 0) {
		DPRINT_ERR(VMBUS, "Unknown guest id (0x%llx)!!",
				gHvContext.GuestId);
		goto Cleanup;
	}

	/* Write our OS info */
	wrmsrl(HV_X64_MSR_GUEST_OS_ID, HV_LINUX_GUEST_ID);
	gHvContext.GuestId = HV_LINUX_GUEST_ID;

	/* See if the hypercall page is already set */
	rdmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.as_uint64);

	/*
	* Allocate the hypercall page memory
	* virtAddr = osd_PageAlloc(1);
	*/
	virtAddr = osd_VirtualAllocExec(PAGE_SIZE);

	if (!virtAddr) {
		DPRINT_ERR(VMBUS,
			   "unable to allocate hypercall page!!");
		goto Cleanup;
	}

	hypercallMsr.enable = 1;

	hypercallMsr.guest_physical_address = vmalloc_to_pfn(virtAddr);
	wrmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.as_uint64);

	/* Confirm that hypercall page did get setup. */
	hypercallMsr.as_uint64 = 0;
	rdmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.as_uint64);

	if (!hypercallMsr.enable) {
		DPRINT_ERR(VMBUS, "unable to set hypercall page!!");
		goto Cleanup;
	}

	gHvContext.HypercallPage = virtAddr;

	DPRINT_INFO(VMBUS, "Hypercall page VA=%p, PA=0x%0llx",
		    gHvContext.HypercallPage,
		    (u64)hypercallMsr.guest_physical_address << PAGE_SHIFT);

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
	gHvContext.SignalEventParam->connectionid.asu32 = 0;
	gHvContext.SignalEventParam->connectionid.u.id =
						VMBUS_EVENT_CONNECTION_ID;
	gHvContext.SignalEventParam->flag_number = 0;
	gHvContext.SignalEventParam->rsvdz = 0;

	return ret;

Cleanup:
	if (virtAddr) {
		if (hypercallMsr.enable) {
			hypercallMsr.as_uint64 = 0;
			wrmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.as_uint64);
		}

		vfree(virtAddr);
	}
	ret = -1;
	return ret;
}

/*
 * HvCleanup - Cleanup routine.
 *
 * This routine is called normally during driver unloading or exiting.
 */
void HvCleanup(void)
{
	union hv_x64_msr_hypercall_contents hypercallMsr;

	kfree(gHvContext.SignalEventBuffer);
	gHvContext.SignalEventBuffer = NULL;
	gHvContext.SignalEventParam = NULL;

	if (gHvContext.HypercallPage) {
		hypercallMsr.as_uint64 = 0;
		wrmsrl(HV_X64_MSR_HYPERCALL, hypercallMsr.as_uint64);
		vfree(gHvContext.HypercallPage);
		gHvContext.HypercallPage = NULL;
	}
}

/*
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

	alignedMsg->connectionid = connectionId;
	alignedMsg->message_type = messageType;
	alignedMsg->payload_size = payloadSize;
	memcpy((void *)alignedMsg->payload, payload, payloadSize);

	status = HvDoHypercall(HVCALL_POST_MESSAGE, alignedMsg, NULL) & 0xFFFF;

	kfree((void *)addr);

	return status;
}


/*
 * HvSignalEvent - Signal an event on the specified connection using the hypervisor event IPC.
 *
 * This involves a hypercall.
 */
u16 HvSignalEvent(void)
{
	u16 status;

	status = HvDoHypercall(HVCALL_SIGNAL_EVENT, gHvContext.SignalEventParam,
			       NULL) & 0xFFFF;
	return status;
}

/*
 * HvSynicInit - Initialize the Synthethic Interrupt Controller.
 *
 * If it is already initialized by another entity (ie x2v shim), we need to
 * retrieve the initialized message and event pages.  Otherwise, we create and
 * initialize the message and event pages.
 */
void HvSynicInit(void *irqarg)
{
	u64 version;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_sint sharedSint;
	union hv_synic_scontrol sctrl;

	u32 irqVector = *((u32 *)(irqarg));
	int cpu = smp_processor_id();

	if (!gHvContext.HypercallPage)
		return;

	/* Check the version */
	rdmsrl(HV_X64_MSR_SVERSION, version);

	DPRINT_INFO(VMBUS, "SynIC version: %llx", version);

	gHvContext.synICMessagePage[cpu] = (void *)get_zeroed_page(GFP_ATOMIC);

	if (gHvContext.synICMessagePage[cpu] == NULL) {
		DPRINT_ERR(VMBUS,
			   "unable to allocate SYNIC message page!!");
		goto Cleanup;
	}

	gHvContext.synICEventPage[cpu] = (void *)get_zeroed_page(GFP_ATOMIC);

	if (gHvContext.synICEventPage[cpu] == NULL) {
		DPRINT_ERR(VMBUS,
			   "unable to allocate SYNIC event page!!");
		goto Cleanup;
	}

	/* Setup the Synic's message page */
	rdmsrl(HV_X64_MSR_SIMP, simp.as_uint64);
	simp.simp_enabled = 1;
	simp.base_simp_gpa = virt_to_phys(gHvContext.synICMessagePage[cpu])
		>> PAGE_SHIFT;

	DPRINT_DBG(VMBUS, "HV_X64_MSR_SIMP msr set to: %llx", simp.as_uint64);

	wrmsrl(HV_X64_MSR_SIMP, simp.as_uint64);

	/* Setup the Synic's event page */
	rdmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);
	siefp.siefp_enabled = 1;
	siefp.base_siefp_gpa = virt_to_phys(gHvContext.synICEventPage[cpu])
		>> PAGE_SHIFT;

	DPRINT_DBG(VMBUS, "HV_X64_MSR_SIEFP msr set to: %llx", siefp.as_uint64);

	wrmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);

	/* Setup the interception SINT. */
	/* wrmsrl((HV_X64_MSR_SINT0 + HV_SYNIC_INTERCEPTION_SINT_INDEX), */
	/*	  interceptionSint.as_uint64); */

	/* Setup the shared SINT. */
	rdmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.as_uint64);

	sharedSint.as_uint64 = 0;
	sharedSint.vector = irqVector; /* HV_SHARED_SINT_IDT_VECTOR + 0x20; */
	sharedSint.masked = false;
	sharedSint.auto_eoi = true;

	DPRINT_DBG(VMBUS, "HV_X64_MSR_SINT1 msr set to: %llx",
		   sharedSint.as_uint64);

	wrmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.as_uint64);

	/* Enable the global synic bit */
	rdmsrl(HV_X64_MSR_SCONTROL, sctrl.as_uint64);
	sctrl.enable = 1;

	wrmsrl(HV_X64_MSR_SCONTROL, sctrl.as_uint64);

	gHvContext.SynICInitialized = true;
	return;

Cleanup:
	if (gHvContext.synICEventPage[cpu])
		osd_PageFree(gHvContext.synICEventPage[cpu], 1);

	if (gHvContext.synICMessagePage[cpu])
		osd_PageFree(gHvContext.synICMessagePage[cpu], 1);
	return;
}

/*
 * HvSynicCleanup - Cleanup routine for HvSynicInit().
 */
void HvSynicCleanup(void *arg)
{
	union hv_synic_sint sharedSint;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	int cpu = smp_processor_id();

	if (!gHvContext.SynICInitialized)
		return;

	rdmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.as_uint64);

	sharedSint.masked = 1;

	/* Need to correctly cleanup in the case of SMP!!! */
	/* Disable the interrupt */
	wrmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, sharedSint.as_uint64);

	rdmsrl(HV_X64_MSR_SIMP, simp.as_uint64);
	simp.simp_enabled = 0;
	simp.base_simp_gpa = 0;

	wrmsrl(HV_X64_MSR_SIMP, simp.as_uint64);

	rdmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);
	siefp.siefp_enabled = 0;
	siefp.base_siefp_gpa = 0;

	wrmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);

	osd_PageFree(gHvContext.synICMessagePage[cpu], 1);
	osd_PageFree(gHvContext.synICEventPage[cpu], 1);
}
