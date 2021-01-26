// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/hyperv.h>
#include <linux/version.h>
#include <linux/random.h>
#include <linux/clockchips.h>
#include <clocksource/hyperv_timer.h>
#include <asm/mshyperv.h>
#include "hyperv_vmbus.h"

/* The one and only */
struct hv_context hv_context;

/*
 * hv_init - Main initialization routine.
 *
 * This routine must be called before any other routines in here are called
 */
int hv_init(void)
{
	hv_context.cpu_context = alloc_percpu(struct hv_per_cpu_context);
	if (!hv_context.cpu_context)
		return -ENOMEM;
	return 0;
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
	struct hv_input_post_message *aligned_msg;
	struct hv_per_cpu_context *hv_cpu;
	u64 status;

	if (payload_size > HV_MESSAGE_PAYLOAD_BYTE_COUNT)
		return -EMSGSIZE;

	hv_cpu = get_cpu_ptr(hv_context.cpu_context);
	aligned_msg = hv_cpu->post_msg_page;
	aligned_msg->connectionid = connection_id;
	aligned_msg->reserved = 0;
	aligned_msg->message_type = message_type;
	aligned_msg->payload_size = payload_size;
	memcpy((void *)aligned_msg->payload, payload, payload_size);

	status = hv_do_hypercall(HVCALL_POST_MESSAGE, aligned_msg, NULL);

	/* Preemption must remain disabled until after the hypercall
	 * so some other thread can't get scheduled onto this cpu and
	 * corrupt the per-cpu post_msg_page
	 */
	put_cpu_ptr(hv_cpu);

	return status & 0xFFFF;
}

int hv_synic_alloc(void)
{
	int cpu;
	struct hv_per_cpu_context *hv_cpu;

	/*
	 * First, zero all per-cpu memory areas so hv_synic_free() can
	 * detect what memory has been allocated and cleanup properly
	 * after any failures.
	 */
	for_each_present_cpu(cpu) {
		hv_cpu = per_cpu_ptr(hv_context.cpu_context, cpu);
		memset(hv_cpu, 0, sizeof(*hv_cpu));
	}

	hv_context.hv_numa_map = kcalloc(nr_node_ids, sizeof(struct cpumask),
					 GFP_KERNEL);
	if (hv_context.hv_numa_map == NULL) {
		pr_err("Unable to allocate NUMA map\n");
		goto err;
	}

	for_each_present_cpu(cpu) {
		hv_cpu = per_cpu_ptr(hv_context.cpu_context, cpu);

		tasklet_init(&hv_cpu->msg_dpc,
			     vmbus_on_msg_dpc, (unsigned long) hv_cpu);

		hv_cpu->synic_message_page =
			(void *)get_zeroed_page(GFP_ATOMIC);
		if (hv_cpu->synic_message_page == NULL) {
			pr_err("Unable to allocate SYNIC message page\n");
			goto err;
		}

		hv_cpu->synic_event_page = (void *)get_zeroed_page(GFP_ATOMIC);
		if (hv_cpu->synic_event_page == NULL) {
			pr_err("Unable to allocate SYNIC event page\n");
			goto err;
		}

		hv_cpu->post_msg_page = (void *)get_zeroed_page(GFP_ATOMIC);
		if (hv_cpu->post_msg_page == NULL) {
			pr_err("Unable to allocate post msg page\n");
			goto err;
		}
	}

	return 0;
err:
	/*
	 * Any memory allocations that succeeded will be freed when
	 * the caller cleans up by calling hv_synic_free()
	 */
	return -ENOMEM;
}


void hv_synic_free(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		struct hv_per_cpu_context *hv_cpu
			= per_cpu_ptr(hv_context.cpu_context, cpu);

		free_page((unsigned long)hv_cpu->synic_event_page);
		free_page((unsigned long)hv_cpu->synic_message_page);
		free_page((unsigned long)hv_cpu->post_msg_page);
	}

	kfree(hv_context.hv_numa_map);
}

/*
 * hv_synic_init - Initialize the Synthetic Interrupt Controller.
 *
 * If it is already initialized by another entity (ie x2v shim), we need to
 * retrieve the initialized message and event pages.  Otherwise, we create and
 * initialize the message and event pages.
 */
void hv_synic_enable_regs(unsigned int cpu)
{
	struct hv_per_cpu_context *hv_cpu
		= per_cpu_ptr(hv_context.cpu_context, cpu);
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_sint shared_sint;
	union hv_synic_scontrol sctrl;

	/* Setup the Synic's message page */
	hv_get_simp(simp.as_uint64);
	simp.simp_enabled = 1;
	simp.base_simp_gpa = virt_to_phys(hv_cpu->synic_message_page)
		>> HV_HYP_PAGE_SHIFT;

	hv_set_simp(simp.as_uint64);

	/* Setup the Synic's event page */
	hv_get_siefp(siefp.as_uint64);
	siefp.siefp_enabled = 1;
	siefp.base_siefp_gpa = virt_to_phys(hv_cpu->synic_event_page)
		>> HV_HYP_PAGE_SHIFT;

	hv_set_siefp(siefp.as_uint64);

	/* Setup the shared SINT. */
	hv_get_synint_state(VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	shared_sint.vector = HYPERVISOR_CALLBACK_VECTOR;
	shared_sint.masked = false;
	shared_sint.auto_eoi = hv_recommend_using_aeoi();
	hv_set_synint_state(VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	/* Enable the global synic bit */
	hv_get_synic_state(sctrl.as_uint64);
	sctrl.enable = 1;

	hv_set_synic_state(sctrl.as_uint64);
}

int hv_synic_init(unsigned int cpu)
{
	hv_synic_enable_regs(cpu);

	hv_stimer_legacy_init(cpu, VMBUS_MESSAGE_SINT);

	return 0;
}

/*
 * hv_synic_cleanup - Cleanup routine for hv_synic_init().
 */
void hv_synic_disable_regs(unsigned int cpu)
{
	union hv_synic_sint shared_sint;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_scontrol sctrl;

	hv_get_synint_state(VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	shared_sint.masked = 1;

	/* Need to correctly cleanup in the case of SMP!!! */
	/* Disable the interrupt */
	hv_set_synint_state(VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	hv_get_simp(simp.as_uint64);
	simp.simp_enabled = 0;
	simp.base_simp_gpa = 0;

	hv_set_simp(simp.as_uint64);

	hv_get_siefp(siefp.as_uint64);
	siefp.siefp_enabled = 0;
	siefp.base_siefp_gpa = 0;

	hv_set_siefp(siefp.as_uint64);

	/* Disable the global synic bit */
	hv_get_synic_state(sctrl.as_uint64);
	sctrl.enable = 0;
	hv_set_synic_state(sctrl.as_uint64);
}

int hv_synic_cleanup(unsigned int cpu)
{
	struct vmbus_channel *channel, *sc;
	bool channel_found = false;

	/*
	 * Hyper-V does not provide a way to change the connect CPU once
	 * it is set; we must prevent the connect CPU from going offline.
	 */
	if (cpu == VMBUS_CONNECT_CPU)
		return -EBUSY;

	/*
	 * Search for channels which are bound to the CPU we're about to
	 * cleanup.  In case we find one and vmbus is still connected, we
	 * fail; this will effectively prevent CPU offlining.
	 *
	 * TODO: Re-bind the channels to different CPUs.
	 */
	mutex_lock(&vmbus_connection.channel_mutex);
	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (channel->target_cpu == cpu) {
			channel_found = true;
			break;
		}
		list_for_each_entry(sc, &channel->sc_list, sc_list) {
			if (sc->target_cpu == cpu) {
				channel_found = true;
				break;
			}
		}
		if (channel_found)
			break;
	}
	mutex_unlock(&vmbus_connection.channel_mutex);

	if (channel_found && vmbus_connection.conn_state == CONNECTED)
		return -EBUSY;

	hv_stimer_legacy_cleanup(cpu);

	hv_synic_disable_regs(cpu);

	return 0;
}
