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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/hyperv.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <asm/hyperv.h>
#include "hyperv_vmbus.h"

/* The one and only */
struct hv_context hv_context = {
	.synic_initialized	= false,
	.hypercall_page		= NULL,
};

/*
 * query_hypervisor_info - Get version info of the windows hypervisor
 */
unsigned int host_info_eax;
unsigned int host_info_ebx;
unsigned int host_info_ecx;
unsigned int host_info_edx;

static int query_hypervisor_info(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	unsigned int max_leaf;
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

	max_leaf = eax;

	if (max_leaf >= HVCPUID_VERSION) {
		eax = 0;
		ebx = 0;
		ecx = 0;
		edx = 0;
		op = HVCPUID_VERSION;
		cpuid(op, &eax, &ebx, &ecx, &edx);
		host_info_eax = eax;
		host_info_ebx = ebx;
		host_info_ecx = ecx;
		host_info_edx = edx;
	}
	return max_leaf;
}

/*
 * do_hypercall- Invoke the specified hypercall
 */
static u64 do_hypercall(u64 control, void *input, void *output)
{
#ifdef CONFIG_X86_64
	u64 hv_status = 0;
	u64 input_address = (input) ? virt_to_phys(input) : 0;
	u64 output_address = (output) ? virt_to_phys(output) : 0;
	void *hypercall_page = hv_context.hypercall_page;

	__asm__ __volatile__("mov %0, %%r8" : : "r" (output_address) : "r8");
	__asm__ __volatile__("call *%3" : "=a" (hv_status) :
			     "c" (control), "d" (input_address),
			     "m" (hypercall_page));

	return hv_status;

#else

	u32 control_hi = control >> 32;
	u32 control_lo = control & 0xFFFFFFFF;
	u32 hv_status_hi = 1;
	u32 hv_status_lo = 1;
	u64 input_address = (input) ? virt_to_phys(input) : 0;
	u32 input_address_hi = input_address >> 32;
	u32 input_address_lo = input_address & 0xFFFFFFFF;
	u64 output_address = (output) ? virt_to_phys(output) : 0;
	u32 output_address_hi = output_address >> 32;
	u32 output_address_lo = output_address & 0xFFFFFFFF;
	void *hypercall_page = hv_context.hypercall_page;

	__asm__ __volatile__ ("call *%8" : "=d"(hv_status_hi),
			      "=a"(hv_status_lo) : "d" (control_hi),
			      "a" (control_lo), "b" (input_address_hi),
			      "c" (input_address_lo), "D"(output_address_hi),
			      "S"(output_address_lo), "m" (hypercall_page));

	return hv_status_lo | ((u64)hv_status_hi << 32);
#endif /* !x86_64 */
}

/*
 * hv_init - Main initialization routine.
 *
 * This routine must be called before any other routines in here are called
 */
int hv_init(void)
{
	int max_leaf;
	union hv_x64_msr_hypercall_contents hypercall_msr;
	void *virtaddr = NULL;

	memset(hv_context.synic_event_page, 0, sizeof(void *) * NR_CPUS);
	memset(hv_context.synic_message_page, 0,
	       sizeof(void *) * NR_CPUS);
	memset(hv_context.vp_index, 0,
	       sizeof(int) * NR_CPUS);
	memset(hv_context.event_dpc, 0,
	       sizeof(void *) * NR_CPUS);

	max_leaf = query_hypervisor_info();

	/*
	 * Write our OS ID.
	 */
	hv_context.guestid = generate_guest_id(0, LINUX_VERSION_CODE, 0);
	wrmsrl(HV_X64_MSR_GUEST_OS_ID, hv_context.guestid);

	/* See if the hypercall page is already set */
	rdmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	virtaddr = __vmalloc(PAGE_SIZE, GFP_KERNEL, PAGE_KERNEL_EXEC);

	if (!virtaddr)
		goto cleanup;

	hypercall_msr.enable = 1;

	hypercall_msr.guest_physical_address = vmalloc_to_pfn(virtaddr);
	wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	/* Confirm that hypercall page did get setup. */
	hypercall_msr.as_uint64 = 0;
	rdmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	if (!hypercall_msr.enable)
		goto cleanup;

	hv_context.hypercall_page = virtaddr;

	return 0;

cleanup:
	if (virtaddr) {
		if (hypercall_msr.enable) {
			hypercall_msr.as_uint64 = 0;
			wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);
		}

		vfree(virtaddr);
	}

	return -ENOTSUPP;
}

/*
 * hv_cleanup - Cleanup routine.
 *
 * This routine is called normally during driver unloading or exiting.
 */
void hv_cleanup(void)
{
	union hv_x64_msr_hypercall_contents hypercall_msr;

	/* Reset our OS id */
	wrmsrl(HV_X64_MSR_GUEST_OS_ID, 0);

	if (hv_context.hypercall_page) {
		hypercall_msr.as_uint64 = 0;
		wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);
		vfree(hv_context.hypercall_page);
		hv_context.hypercall_page = NULL;
	}
}

/*
 * hv_post_message - Post a message using the hypervisor message IPC.
 *
 * This involves a hypercall.
 */
int hv_post_message(union hv_connection_id connection_id,
		  enum hv_message_type message_type,
		  void *payload, size_t payload_size)
{
	struct aligned_input {
		u64 alignment8;
		struct hv_input_post_message msg;
	};

	struct hv_input_post_message *aligned_msg;
	u16 status;
	unsigned long addr;

	if (payload_size > HV_MESSAGE_PAYLOAD_BYTE_COUNT)
		return -EMSGSIZE;

	addr = (unsigned long)kmalloc(sizeof(struct aligned_input), GFP_ATOMIC);
	if (!addr)
		return -ENOMEM;

	aligned_msg = (struct hv_input_post_message *)
			(ALIGN(addr, HV_HYPERCALL_PARAM_ALIGN));

	aligned_msg->connectionid = connection_id;
	aligned_msg->message_type = message_type;
	aligned_msg->payload_size = payload_size;
	memcpy((void *)aligned_msg->payload, payload, payload_size);

	status = do_hypercall(HVCALL_POST_MESSAGE, aligned_msg, NULL)
		& 0xFFFF;

	kfree((void *)addr);

	return status;
}


/*
 * hv_signal_event -
 * Signal an event on the specified connection using the hypervisor event IPC.
 *
 * This involves a hypercall.
 */
u16 hv_signal_event(void *con_id)
{
	u16 status;

	status = (do_hypercall(HVCALL_SIGNAL_EVENT, con_id, NULL) & 0xFFFF);

	return status;
}


int hv_synic_alloc(void)
{
	size_t size = sizeof(struct tasklet_struct);
	int cpu;

	for_each_online_cpu(cpu) {
		hv_context.event_dpc[cpu] = kmalloc(size, GFP_ATOMIC);
		if (hv_context.event_dpc[cpu] == NULL) {
			pr_err("Unable to allocate event dpc\n");
			goto err;
		}
		tasklet_init(hv_context.event_dpc[cpu], vmbus_on_event, cpu);

		hv_context.synic_message_page[cpu] =
			(void *)get_zeroed_page(GFP_ATOMIC);

		if (hv_context.synic_message_page[cpu] == NULL) {
			pr_err("Unable to allocate SYNIC message page\n");
			goto err;
		}

		hv_context.synic_event_page[cpu] =
			(void *)get_zeroed_page(GFP_ATOMIC);

		if (hv_context.synic_event_page[cpu] == NULL) {
			pr_err("Unable to allocate SYNIC event page\n");
			goto err;
		}
	}

	return 0;
err:
	return -ENOMEM;
}

void hv_synic_free_cpu(int cpu)
{
	kfree(hv_context.event_dpc[cpu]);
	if (hv_context.synic_message_page[cpu])
		free_page((unsigned long)hv_context.synic_event_page[cpu]);
	if (hv_context.synic_message_page[cpu])
		free_page((unsigned long)hv_context.synic_message_page[cpu]);
}

void hv_synic_free(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		hv_synic_free_cpu(cpu);
}

/*
 * hv_synic_init - Initialize the Synthethic Interrupt Controller.
 *
 * If it is already initialized by another entity (ie x2v shim), we need to
 * retrieve the initialized message and event pages.  Otherwise, we create and
 * initialize the message and event pages.
 */
void hv_synic_init(void *arg)
{
	u64 version;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_sint shared_sint;
	union hv_synic_scontrol sctrl;
	u64 vp_index;

	int cpu = smp_processor_id();

	if (!hv_context.hypercall_page)
		return;

	/* Check the version */
	rdmsrl(HV_X64_MSR_SVERSION, version);

	/* Setup the Synic's message page */
	rdmsrl(HV_X64_MSR_SIMP, simp.as_uint64);
	simp.simp_enabled = 1;
	simp.base_simp_gpa = virt_to_phys(hv_context.synic_message_page[cpu])
		>> PAGE_SHIFT;

	wrmsrl(HV_X64_MSR_SIMP, simp.as_uint64);

	/* Setup the Synic's event page */
	rdmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);
	siefp.siefp_enabled = 1;
	siefp.base_siefp_gpa = virt_to_phys(hv_context.synic_event_page[cpu])
		>> PAGE_SHIFT;

	wrmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);

	/* Setup the shared SINT. */
	rdmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	shared_sint.as_uint64 = 0;
	shared_sint.vector = HYPERVISOR_CALLBACK_VECTOR;
	shared_sint.masked = false;
	shared_sint.auto_eoi = true;

	wrmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	/* Enable the global synic bit */
	rdmsrl(HV_X64_MSR_SCONTROL, sctrl.as_uint64);
	sctrl.enable = 1;

	wrmsrl(HV_X64_MSR_SCONTROL, sctrl.as_uint64);

	hv_context.synic_initialized = true;

	/*
	 * Setup the mapping between Hyper-V's notion
	 * of cpuid and Linux' notion of cpuid.
	 * This array will be indexed using Linux cpuid.
	 */
	rdmsrl(HV_X64_MSR_VP_INDEX, vp_index);
	hv_context.vp_index[cpu] = (u32)vp_index;
	return;
}

/*
 * hv_synic_cleanup - Cleanup routine for hv_synic_init().
 */
void hv_synic_cleanup(void *arg)
{
	union hv_synic_sint shared_sint;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	int cpu = smp_processor_id();

	if (!hv_context.synic_initialized)
		return;

	rdmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	shared_sint.masked = 1;

	/* Need to correctly cleanup in the case of SMP!!! */
	/* Disable the interrupt */
	wrmsrl(HV_X64_MSR_SINT0 + VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	rdmsrl(HV_X64_MSR_SIMP, simp.as_uint64);
	simp.simp_enabled = 0;
	simp.base_simp_gpa = 0;

	wrmsrl(HV_X64_MSR_SIMP, simp.as_uint64);

	rdmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);
	siefp.siefp_enabled = 0;
	siefp.base_siefp_gpa = 0;

	wrmsrl(HV_X64_MSR_SIEFP, siefp.as_uint64);

	free_page((unsigned long)hv_context.synic_message_page[cpu]);
	free_page((unsigned long)hv_context.synic_event_page[cpu]);
}
