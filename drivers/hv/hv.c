// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/hyperv.h>
#include <linux/random.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <clocksource/hyperv_timer.h>
#include <asm/mshyperv.h>
#include <linux/set_memory.h>
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
	unsigned long flags;
	u64 status;

	if (payload_size > HV_MESSAGE_PAYLOAD_BYTE_COUNT)
		return -EMSGSIZE;

	local_irq_save(flags);

	/*
	 * A TDX VM with the paravisor must use the decrypted post_msg_page: see
	 * the comment in struct hv_per_cpu_context. A SNP VM with the paravisor
	 * can use the encrypted hyperv_pcpu_input_arg because it copies the
	 * input into the GHCB page, which has been decrypted by the paravisor.
	 */
	if (hv_isolation_type_tdx() && ms_hyperv.paravisor_present)
		aligned_msg = this_cpu_ptr(hv_context.cpu_context)->post_msg_page;
	else
		aligned_msg = *this_cpu_ptr(hyperv_pcpu_input_arg);

	aligned_msg->connectionid = connection_id;
	aligned_msg->reserved = 0;
	aligned_msg->message_type = message_type;
	aligned_msg->payload_size = payload_size;
	memcpy((void *)aligned_msg->payload, payload, payload_size);

	if (ms_hyperv.paravisor_present) {
		if (hv_isolation_type_tdx())
			status = hv_tdx_hypercall(HVCALL_POST_MESSAGE,
						  virt_to_phys(aligned_msg), 0);
		else if (hv_isolation_type_snp())
			status = hv_ghcb_hypercall(HVCALL_POST_MESSAGE,
						   aligned_msg, NULL,
						   sizeof(*aligned_msg));
		else
			status = HV_STATUS_INVALID_PARAMETER;
	} else {
		status = hv_do_hypercall(HVCALL_POST_MESSAGE,
					 aligned_msg, NULL);
	}

	local_irq_restore(flags);

	return hv_result(status);
}

int hv_synic_alloc(void)
{
	int cpu, ret = -ENOMEM;
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
	if (!hv_context.hv_numa_map) {
		pr_err("Unable to allocate NUMA map\n");
		goto err;
	}

	for_each_present_cpu(cpu) {
		hv_cpu = per_cpu_ptr(hv_context.cpu_context, cpu);

		tasklet_init(&hv_cpu->msg_dpc,
			     vmbus_on_msg_dpc, (unsigned long)hv_cpu);

		if (ms_hyperv.paravisor_present && hv_isolation_type_tdx()) {
			hv_cpu->post_msg_page = (void *)get_zeroed_page(GFP_ATOMIC);
			if (!hv_cpu->post_msg_page) {
				pr_err("Unable to allocate post msg page\n");
				goto err;
			}

			ret = set_memory_decrypted((unsigned long)hv_cpu->post_msg_page, 1);
			if (ret) {
				pr_err("Failed to decrypt post msg page: %d\n", ret);
				/* Just leak the page, as it's unsafe to free the page. */
				hv_cpu->post_msg_page = NULL;
				goto err;
			}

			memset(hv_cpu->post_msg_page, 0, PAGE_SIZE);
		}

		/*
		 * Synic message and event pages are allocated by paravisor.
		 * Skip these pages allocation here.
		 */
		if (!ms_hyperv.paravisor_present && !hv_root_partition) {
			hv_cpu->synic_message_page =
				(void *)get_zeroed_page(GFP_ATOMIC);
			if (!hv_cpu->synic_message_page) {
				pr_err("Unable to allocate SYNIC message page\n");
				goto err;
			}

			hv_cpu->synic_event_page =
				(void *)get_zeroed_page(GFP_ATOMIC);
			if (!hv_cpu->synic_event_page) {
				pr_err("Unable to allocate SYNIC event page\n");

				free_page((unsigned long)hv_cpu->synic_message_page);
				hv_cpu->synic_message_page = NULL;
				goto err;
			}
		}

		if (!ms_hyperv.paravisor_present &&
		    (hv_isolation_type_snp() || hv_isolation_type_tdx())) {
			ret = set_memory_decrypted((unsigned long)
				hv_cpu->synic_message_page, 1);
			if (ret) {
				pr_err("Failed to decrypt SYNIC msg page: %d\n", ret);
				hv_cpu->synic_message_page = NULL;

				/*
				 * Free the event page here so that hv_synic_free()
				 * won't later try to re-encrypt it.
				 */
				free_page((unsigned long)hv_cpu->synic_event_page);
				hv_cpu->synic_event_page = NULL;
				goto err;
			}

			ret = set_memory_decrypted((unsigned long)
				hv_cpu->synic_event_page, 1);
			if (ret) {
				pr_err("Failed to decrypt SYNIC event page: %d\n", ret);
				hv_cpu->synic_event_page = NULL;
				goto err;
			}

			memset(hv_cpu->synic_message_page, 0, PAGE_SIZE);
			memset(hv_cpu->synic_event_page, 0, PAGE_SIZE);
		}
	}

	return 0;

err:
	/*
	 * Any memory allocations that succeeded will be freed when
	 * the caller cleans up by calling hv_synic_free()
	 */
	return ret;
}

void hv_synic_free(void)
{
	int cpu, ret;

	for_each_present_cpu(cpu) {
		struct hv_per_cpu_context *hv_cpu =
			per_cpu_ptr(hv_context.cpu_context, cpu);

		/* It's better to leak the page if the encryption fails. */
		if (ms_hyperv.paravisor_present && hv_isolation_type_tdx()) {
			if (hv_cpu->post_msg_page) {
				ret = set_memory_encrypted((unsigned long)
					hv_cpu->post_msg_page, 1);
				if (ret) {
					pr_err("Failed to encrypt post msg page: %d\n", ret);
					hv_cpu->post_msg_page = NULL;
				}
			}
		}

		if (!ms_hyperv.paravisor_present &&
		    (hv_isolation_type_snp() || hv_isolation_type_tdx())) {
			if (hv_cpu->synic_message_page) {
				ret = set_memory_encrypted((unsigned long)
					hv_cpu->synic_message_page, 1);
				if (ret) {
					pr_err("Failed to encrypt SYNIC msg page: %d\n", ret);
					hv_cpu->synic_message_page = NULL;
				}
			}

			if (hv_cpu->synic_event_page) {
				ret = set_memory_encrypted((unsigned long)
					hv_cpu->synic_event_page, 1);
				if (ret) {
					pr_err("Failed to encrypt SYNIC event page: %d\n", ret);
					hv_cpu->synic_event_page = NULL;
				}
			}
		}

		free_page((unsigned long)hv_cpu->post_msg_page);
		free_page((unsigned long)hv_cpu->synic_event_page);
		free_page((unsigned long)hv_cpu->synic_message_page);
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
	struct hv_per_cpu_context *hv_cpu =
		per_cpu_ptr(hv_context.cpu_context, cpu);
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_sint shared_sint;
	union hv_synic_scontrol sctrl;

	/* Setup the Synic's message page */
	simp.as_uint64 = hv_get_msr(HV_MSR_SIMP);
	simp.simp_enabled = 1;

	if (ms_hyperv.paravisor_present || hv_root_partition) {
		/* Mask out vTOM bit. ioremap_cache() maps decrypted */
		u64 base = (simp.base_simp_gpa << HV_HYP_PAGE_SHIFT) &
				~ms_hyperv.shared_gpa_boundary;
		hv_cpu->synic_message_page =
			(void *)ioremap_cache(base, HV_HYP_PAGE_SIZE);
		if (!hv_cpu->synic_message_page)
			pr_err("Fail to map synic message page.\n");
	} else {
		simp.base_simp_gpa = virt_to_phys(hv_cpu->synic_message_page)
			>> HV_HYP_PAGE_SHIFT;
	}

	hv_set_msr(HV_MSR_SIMP, simp.as_uint64);

	/* Setup the Synic's event page */
	siefp.as_uint64 = hv_get_msr(HV_MSR_SIEFP);
	siefp.siefp_enabled = 1;

	if (ms_hyperv.paravisor_present || hv_root_partition) {
		/* Mask out vTOM bit. ioremap_cache() maps decrypted */
		u64 base = (siefp.base_siefp_gpa << HV_HYP_PAGE_SHIFT) &
				~ms_hyperv.shared_gpa_boundary;
		hv_cpu->synic_event_page =
			(void *)ioremap_cache(base, HV_HYP_PAGE_SIZE);
		if (!hv_cpu->synic_event_page)
			pr_err("Fail to map synic event page.\n");
	} else {
		siefp.base_siefp_gpa = virt_to_phys(hv_cpu->synic_event_page)
			>> HV_HYP_PAGE_SHIFT;
	}

	hv_set_msr(HV_MSR_SIEFP, siefp.as_uint64);

	/* Setup the shared SINT. */
	if (vmbus_irq != -1)
		enable_percpu_irq(vmbus_irq, 0);
	shared_sint.as_uint64 = hv_get_msr(HV_MSR_SINT0 + VMBUS_MESSAGE_SINT);

	shared_sint.vector = vmbus_interrupt;
	shared_sint.masked = false;

	/*
	 * On architectures where Hyper-V doesn't support AEOI (e.g., ARM64),
	 * it doesn't provide a recommendation flag and AEOI must be disabled.
	 */
#ifdef HV_DEPRECATING_AEOI_RECOMMENDED
	shared_sint.auto_eoi =
			!(ms_hyperv.hints & HV_DEPRECATING_AEOI_RECOMMENDED);
#else
	shared_sint.auto_eoi = 0;
#endif
	hv_set_msr(HV_MSR_SINT0 + VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	/* Enable the global synic bit */
	sctrl.as_uint64 = hv_get_msr(HV_MSR_SCONTROL);
	sctrl.enable = 1;

	hv_set_msr(HV_MSR_SCONTROL, sctrl.as_uint64);
}

int hv_synic_init(unsigned int cpu)
{
	hv_synic_enable_regs(cpu);

	hv_stimer_legacy_init(cpu, VMBUS_MESSAGE_SINT);

	return 0;
}

void hv_synic_disable_regs(unsigned int cpu)
{
	struct hv_per_cpu_context *hv_cpu =
		per_cpu_ptr(hv_context.cpu_context, cpu);
	union hv_synic_sint shared_sint;
	union hv_synic_simp simp;
	union hv_synic_siefp siefp;
	union hv_synic_scontrol sctrl;

	shared_sint.as_uint64 = hv_get_msr(HV_MSR_SINT0 + VMBUS_MESSAGE_SINT);

	shared_sint.masked = 1;

	/* Need to correctly cleanup in the case of SMP!!! */
	/* Disable the interrupt */
	hv_set_msr(HV_MSR_SINT0 + VMBUS_MESSAGE_SINT, shared_sint.as_uint64);

	simp.as_uint64 = hv_get_msr(HV_MSR_SIMP);
	/*
	 * In Isolation VM, sim and sief pages are allocated by
	 * paravisor. These pages also will be used by kdump
	 * kernel. So just reset enable bit here and keep page
	 * addresses.
	 */
	simp.simp_enabled = 0;
	if (ms_hyperv.paravisor_present || hv_root_partition) {
		iounmap(hv_cpu->synic_message_page);
		hv_cpu->synic_message_page = NULL;
	} else {
		simp.base_simp_gpa = 0;
	}

	hv_set_msr(HV_MSR_SIMP, simp.as_uint64);

	siefp.as_uint64 = hv_get_msr(HV_MSR_SIEFP);
	siefp.siefp_enabled = 0;

	if (ms_hyperv.paravisor_present || hv_root_partition) {
		iounmap(hv_cpu->synic_event_page);
		hv_cpu->synic_event_page = NULL;
	} else {
		siefp.base_siefp_gpa = 0;
	}

	hv_set_msr(HV_MSR_SIEFP, siefp.as_uint64);

	/* Disable the global synic bit */
	sctrl.as_uint64 = hv_get_msr(HV_MSR_SCONTROL);
	sctrl.enable = 0;
	hv_set_msr(HV_MSR_SCONTROL, sctrl.as_uint64);

	if (vmbus_irq != -1)
		disable_percpu_irq(vmbus_irq);
}

#define HV_MAX_TRIES 3
/*
 * Scan the event flags page of 'this' CPU looking for any bit that is set.  If we find one
 * bit set, then wait for a few milliseconds.  Repeat these steps for a maximum of 3 times.
 * Return 'true', if there is still any set bit after this operation; 'false', otherwise.
 *
 * If a bit is set, that means there is a pending channel interrupt.  The expectation is
 * that the normal interrupt handling mechanism will find and process the channel interrupt
 * "very soon", and in the process clear the bit.
 */
static bool hv_synic_event_pending(void)
{
	struct hv_per_cpu_context *hv_cpu = this_cpu_ptr(hv_context.cpu_context);
	union hv_synic_event_flags *event =
		(union hv_synic_event_flags *)hv_cpu->synic_event_page + VMBUS_MESSAGE_SINT;
	unsigned long *recv_int_page = event->flags; /* assumes VMBus version >= VERSION_WIN8 */
	bool pending;
	u32 relid;
	int tries = 0;

retry:
	pending = false;
	for_each_set_bit(relid, recv_int_page, HV_EVENT_FLAGS_COUNT) {
		/* Special case - VMBus channel protocol messages */
		if (relid == 0)
			continue;
		pending = true;
		break;
	}
	if (pending && tries++ < HV_MAX_TRIES) {
		usleep_range(10000, 20000);
		goto retry;
	}
	return pending;
}

/*
 * hv_synic_cleanup - Cleanup routine for hv_synic_init().
 */
int hv_synic_cleanup(unsigned int cpu)
{
	struct vmbus_channel *channel, *sc;
	bool channel_found = false;

	if (vmbus_connection.conn_state != CONNECTED)
		goto always_cleanup;

	/*
	 * Hyper-V does not provide a way to change the connect CPU once
	 * it is set; we must prevent the connect CPU from going offline
	 * while the VM is running normally. But in the panic or kexec()
	 * path where the vmbus is already disconnected, the CPU must be
	 * allowed to shut down.
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

	if (channel_found)
		return -EBUSY;

	/*
	 * channel_found == false means that any channels that were previously
	 * assigned to the CPU have been reassigned elsewhere with a call of
	 * vmbus_send_modifychannel().  Scan the event flags page looking for
	 * bits that are set and waiting with a timeout for vmbus_chan_sched()
	 * to process such bits.  If bits are still set after this operation
	 * and VMBus is connected, fail the CPU offlining operation.
	 */
	if (vmbus_proto_version >= VERSION_WIN10_V4_1 && hv_synic_event_pending())
		return -EBUSY;

always_cleanup:
	hv_stimer_legacy_cleanup(cpu);

	hv_synic_disable_regs(cpu);

	return 0;
}
