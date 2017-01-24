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
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hyperv.h>

#include "hyperv_vmbus.h"

static void init_vp_index(struct vmbus_channel *channel, u16 dev_type);

static const struct vmbus_device vmbus_devs[] = {
	/* IDE */
	{ .dev_type = HV_IDE,
	  HV_IDE_GUID,
	  .perf_device = true,
	},

	/* SCSI */
	{ .dev_type = HV_SCSI,
	  HV_SCSI_GUID,
	  .perf_device = true,
	},

	/* Fibre Channel */
	{ .dev_type = HV_FC,
	  HV_SYNTHFC_GUID,
	  .perf_device = true,
	},

	/* Synthetic NIC */
	{ .dev_type = HV_NIC,
	  HV_NIC_GUID,
	  .perf_device = true,
	},

	/* Network Direct */
	{ .dev_type = HV_ND,
	  HV_ND_GUID,
	  .perf_device = true,
	},

	/* PCIE */
	{ .dev_type = HV_PCIE,
	  HV_PCIE_GUID,
	  .perf_device = true,
	},

	/* Synthetic Frame Buffer */
	{ .dev_type = HV_FB,
	  HV_SYNTHVID_GUID,
	  .perf_device = false,
	},

	/* Synthetic Keyboard */
	{ .dev_type = HV_KBD,
	  HV_KBD_GUID,
	  .perf_device = false,
	},

	/* Synthetic MOUSE */
	{ .dev_type = HV_MOUSE,
	  HV_MOUSE_GUID,
	  .perf_device = false,
	},

	/* KVP */
	{ .dev_type = HV_KVP,
	  HV_KVP_GUID,
	  .perf_device = false,
	},

	/* Time Synch */
	{ .dev_type = HV_TS,
	  HV_TS_GUID,
	  .perf_device = false,
	},

	/* Heartbeat */
	{ .dev_type = HV_HB,
	  HV_HEART_BEAT_GUID,
	  .perf_device = false,
	},

	/* Shutdown */
	{ .dev_type = HV_SHUTDOWN,
	  HV_SHUTDOWN_GUID,
	  .perf_device = false,
	},

	/* File copy */
	{ .dev_type = HV_FCOPY,
	  HV_FCOPY_GUID,
	  .perf_device = false,
	},

	/* Backup */
	{ .dev_type = HV_BACKUP,
	  HV_VSS_GUID,
	  .perf_device = false,
	},

	/* Dynamic Memory */
	{ .dev_type = HV_DM,
	  HV_DM_GUID,
	  .perf_device = false,
	},

	/* Unknown GUID */
	{ .dev_type = HV_UNKNOWN,
	  .perf_device = false,
	},
};

static const struct {
	uuid_le guid;
} vmbus_unsupported_devs[] = {
	{ HV_AVMA1_GUID },
	{ HV_AVMA2_GUID },
	{ HV_RDV_GUID	},
};

static bool is_unsupported_vmbus_devs(const uuid_le *guid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vmbus_unsupported_devs); i++)
		if (!uuid_le_cmp(*guid, vmbus_unsupported_devs[i].guid))
			return true;
	return false;
}

static u16 hv_get_dev_type(const struct vmbus_channel *channel)
{
	const uuid_le *guid = &channel->offermsg.offer.if_type;
	u16 i;

	if (is_hvsock_channel(channel) || is_unsupported_vmbus_devs(guid))
		return HV_UNKNOWN;

	for (i = HV_IDE; i < HV_UNKNOWN; i++) {
		if (!uuid_le_cmp(*guid, vmbus_devs[i].guid))
			return i;
	}
	pr_info("Unknown GUID: %pUl\n", guid);
	return i;
}

/**
 * vmbus_prep_negotiate_resp() - Create default response for Hyper-V Negotiate message
 * @icmsghdrp: Pointer to msg header structure
 * @icmsg_negotiate: Pointer to negotiate message structure
 * @buf: Raw buffer channel data
 *
 * @icmsghdrp is of type &struct icmsg_hdr.
 * @negop is of type &struct icmsg_negotiate.
 * Set up and fill in default negotiate response message.
 *
 * The fw_version specifies the  framework version that
 * we can support and srv_version specifies the service
 * version we can support.
 *
 * Mainly used by Hyper-V drivers.
 */
bool vmbus_prep_negotiate_resp(struct icmsg_hdr *icmsghdrp,
				struct icmsg_negotiate *negop, u8 *buf,
				int fw_version, int srv_version)
{
	int icframe_major, icframe_minor;
	int icmsg_major, icmsg_minor;
	int fw_major, fw_minor;
	int srv_major, srv_minor;
	int i;
	bool found_match = false;

	icmsghdrp->icmsgsize = 0x10;
	fw_major = (fw_version >> 16);
	fw_minor = (fw_version & 0xFFFF);

	srv_major = (srv_version >> 16);
	srv_minor = (srv_version & 0xFFFF);

	negop = (struct icmsg_negotiate *)&buf[
		sizeof(struct vmbuspipe_hdr) +
		sizeof(struct icmsg_hdr)];

	icframe_major = negop->icframe_vercnt;
	icframe_minor = 0;

	icmsg_major = negop->icmsg_vercnt;
	icmsg_minor = 0;

	/*
	 * Select the framework version number we will
	 * support.
	 */

	for (i = 0; i < negop->icframe_vercnt; i++) {
		if ((negop->icversion_data[i].major == fw_major) &&
		   (negop->icversion_data[i].minor == fw_minor)) {
			icframe_major = negop->icversion_data[i].major;
			icframe_minor = negop->icversion_data[i].minor;
			found_match = true;
		}
	}

	if (!found_match)
		goto fw_error;

	found_match = false;

	for (i = negop->icframe_vercnt;
		 (i < negop->icframe_vercnt + negop->icmsg_vercnt); i++) {
		if ((negop->icversion_data[i].major == srv_major) &&
		   (negop->icversion_data[i].minor == srv_minor)) {
			icmsg_major = negop->icversion_data[i].major;
			icmsg_minor = negop->icversion_data[i].minor;
			found_match = true;
		}
	}

	/*
	 * Respond with the framework and service
	 * version numbers we can support.
	 */

fw_error:
	if (!found_match) {
		negop->icframe_vercnt = 0;
		negop->icmsg_vercnt = 0;
	} else {
		negop->icframe_vercnt = 1;
		negop->icmsg_vercnt = 1;
	}

	negop->icversion_data[0].major = icframe_major;
	negop->icversion_data[0].minor = icframe_minor;
	negop->icversion_data[1].major = icmsg_major;
	negop->icversion_data[1].minor = icmsg_minor;
	return found_match;
}

EXPORT_SYMBOL_GPL(vmbus_prep_negotiate_resp);

/*
 * alloc_channel - Allocate and initialize a vmbus channel object
 */
static struct vmbus_channel *alloc_channel(void)
{
	struct vmbus_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_ATOMIC);
	if (!channel)
		return NULL;

	channel->acquire_ring_lock = true;
	spin_lock_init(&channel->inbound_lock);
	spin_lock_init(&channel->lock);

	INIT_LIST_HEAD(&channel->sc_list);
	INIT_LIST_HEAD(&channel->percpu_list);

	return channel;
}

/*
 * free_channel - Release the resources used by the vmbus channel object
 */
static void free_channel(struct vmbus_channel *channel)
{
	kfree(channel);
}

static void percpu_channel_enq(void *arg)
{
	struct vmbus_channel *channel = arg;
	int cpu = smp_processor_id();

	list_add_tail(&channel->percpu_list, &hv_context.percpu_list[cpu]);
}

static void percpu_channel_deq(void *arg)
{
	struct vmbus_channel *channel = arg;

	list_del(&channel->percpu_list);
}


static void vmbus_release_relid(u32 relid)
{
	struct vmbus_channel_relid_released msg;

	memset(&msg, 0, sizeof(struct vmbus_channel_relid_released));
	msg.child_relid = relid;
	msg.header.msgtype = CHANNELMSG_RELID_RELEASED;
	vmbus_post_msg(&msg, sizeof(struct vmbus_channel_relid_released));
}

void hv_event_tasklet_disable(struct vmbus_channel *channel)
{
	struct tasklet_struct *tasklet;
	tasklet = hv_context.event_dpc[channel->target_cpu];
	tasklet_disable(tasklet);
}

void hv_event_tasklet_enable(struct vmbus_channel *channel)
{
	struct tasklet_struct *tasklet;
	tasklet = hv_context.event_dpc[channel->target_cpu];
	tasklet_enable(tasklet);

	/* In case there is any pending event */
	tasklet_schedule(tasklet);
}

void hv_process_channel_removal(struct vmbus_channel *channel, u32 relid)
{
	unsigned long flags;
	struct vmbus_channel *primary_channel;

	BUG_ON(!channel->rescind);
	BUG_ON(!mutex_is_locked(&vmbus_connection.channel_mutex));

	hv_event_tasklet_disable(channel);
	if (channel->target_cpu != get_cpu()) {
		put_cpu();
		smp_call_function_single(channel->target_cpu,
					 percpu_channel_deq, channel, true);
	} else {
		percpu_channel_deq(channel);
		put_cpu();
	}
	hv_event_tasklet_enable(channel);

	if (channel->primary_channel == NULL) {
		list_del(&channel->listentry);

		primary_channel = channel;
	} else {
		primary_channel = channel->primary_channel;
		spin_lock_irqsave(&primary_channel->lock, flags);
		list_del(&channel->sc_list);
		primary_channel->num_sc--;
		spin_unlock_irqrestore(&primary_channel->lock, flags);
	}

	/*
	 * We need to free the bit for init_vp_index() to work in the case
	 * of sub-channel, when we reload drivers like hv_netvsc.
	 */
	if (channel->affinity_policy == HV_LOCALIZED)
		cpumask_clear_cpu(channel->target_cpu,
				  &primary_channel->alloced_cpus_in_node);

	vmbus_release_relid(relid);

	free_channel(channel);
}

void vmbus_free_channels(void)
{
	struct vmbus_channel *channel, *tmp;

	mutex_lock(&vmbus_connection.channel_mutex);
	list_for_each_entry_safe(channel, tmp, &vmbus_connection.chn_list,
		listentry) {
		/* hv_process_channel_removal() needs this */
		channel->rescind = true;

		vmbus_device_unregister(channel->device_obj);
	}
	mutex_unlock(&vmbus_connection.channel_mutex);
}

/*
 * vmbus_process_offer - Process the offer by creating a channel/device
 * associated with this offer
 */
static void vmbus_process_offer(struct vmbus_channel *newchannel)
{
	struct vmbus_channel *channel;
	bool fnew = true;
	unsigned long flags;
	u16 dev_type;
	int ret;

	/* Make sure this is a new offer */
	mutex_lock(&vmbus_connection.channel_mutex);

	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (!uuid_le_cmp(channel->offermsg.offer.if_type,
			newchannel->offermsg.offer.if_type) &&
			!uuid_le_cmp(channel->offermsg.offer.if_instance,
				newchannel->offermsg.offer.if_instance)) {
			fnew = false;
			break;
		}
	}

	if (fnew)
		list_add_tail(&newchannel->listentry,
			      &vmbus_connection.chn_list);

	mutex_unlock(&vmbus_connection.channel_mutex);

	if (!fnew) {
		/*
		 * Check to see if this is a sub-channel.
		 */
		if (newchannel->offermsg.offer.sub_channel_index != 0) {
			/*
			 * Process the sub-channel.
			 */
			newchannel->primary_channel = channel;
			spin_lock_irqsave(&channel->lock, flags);
			list_add_tail(&newchannel->sc_list, &channel->sc_list);
			channel->num_sc++;
			spin_unlock_irqrestore(&channel->lock, flags);
		} else
			goto err_free_chan;
	}

	dev_type = hv_get_dev_type(newchannel);

	init_vp_index(newchannel, dev_type);

	hv_event_tasklet_disable(newchannel);
	if (newchannel->target_cpu != get_cpu()) {
		put_cpu();
		smp_call_function_single(newchannel->target_cpu,
					 percpu_channel_enq,
					 newchannel, true);
	} else {
		percpu_channel_enq(newchannel);
		put_cpu();
	}
	hv_event_tasklet_enable(newchannel);

	/*
	 * This state is used to indicate a successful open
	 * so that when we do close the channel normally, we
	 * can cleanup properly
	 */
	newchannel->state = CHANNEL_OPEN_STATE;

	if (!fnew) {
		if (channel->sc_creation_callback != NULL)
			channel->sc_creation_callback(newchannel);
		return;
	}

	/*
	 * Start the process of binding this offer to the driver
	 * We need to set the DeviceObject field before calling
	 * vmbus_child_dev_add()
	 */
	newchannel->device_obj = vmbus_device_create(
		&newchannel->offermsg.offer.if_type,
		&newchannel->offermsg.offer.if_instance,
		newchannel);
	if (!newchannel->device_obj)
		goto err_deq_chan;

	newchannel->device_obj->device_id = dev_type;
	/*
	 * Add the new device to the bus. This will kick off device-driver
	 * binding which eventually invokes the device driver's AddDevice()
	 * method.
	 */
	mutex_lock(&vmbus_connection.channel_mutex);
	ret = vmbus_device_register(newchannel->device_obj);
	mutex_unlock(&vmbus_connection.channel_mutex);

	if (ret != 0) {
		pr_err("unable to add child device object (relid %d)\n",
			newchannel->offermsg.child_relid);
		kfree(newchannel->device_obj);
		goto err_deq_chan;
	}
	return;

err_deq_chan:
	mutex_lock(&vmbus_connection.channel_mutex);
	list_del(&newchannel->listentry);
	mutex_unlock(&vmbus_connection.channel_mutex);

	hv_event_tasklet_disable(newchannel);
	if (newchannel->target_cpu != get_cpu()) {
		put_cpu();
		smp_call_function_single(newchannel->target_cpu,
					 percpu_channel_deq, newchannel, true);
	} else {
		percpu_channel_deq(newchannel);
		put_cpu();
	}
	hv_event_tasklet_enable(newchannel);

	vmbus_release_relid(newchannel->offermsg.child_relid);

err_free_chan:
	free_channel(newchannel);
}

/*
 * We use this state to statically distribute the channel interrupt load.
 */
static int next_numa_node_id;

/*
 * Starting with Win8, we can statically distribute the incoming
 * channel interrupt load by binding a channel to VCPU.
 * We do this in a hierarchical fashion:
 * First distribute the primary channels across available NUMA nodes
 * and then distribute the subchannels amongst the CPUs in the NUMA
 * node assigned to the primary channel.
 *
 * For pre-win8 hosts or non-performance critical channels we assign the
 * first CPU in the first NUMA node.
 */
static void init_vp_index(struct vmbus_channel *channel, u16 dev_type)
{
	u32 cur_cpu;
	bool perf_chn = vmbus_devs[dev_type].perf_device;
	struct vmbus_channel *primary = channel->primary_channel;
	int next_node;
	struct cpumask available_mask;
	struct cpumask *alloced_mask;

	if ((vmbus_proto_version == VERSION_WS2008) ||
	    (vmbus_proto_version == VERSION_WIN7) || (!perf_chn)) {
		/*
		 * Prior to win8, all channel interrupts are
		 * delivered on cpu 0.
		 * Also if the channel is not a performance critical
		 * channel, bind it to cpu 0.
		 */
		channel->numa_node = 0;
		channel->target_cpu = 0;
		channel->target_vp = hv_context.vp_index[0];
		return;
	}

	/*
	 * Based on the channel affinity policy, we will assign the NUMA
	 * nodes.
	 */

	if ((channel->affinity_policy == HV_BALANCED) || (!primary)) {
		while (true) {
			next_node = next_numa_node_id++;
			if (next_node == nr_node_ids) {
				next_node = next_numa_node_id = 0;
				continue;
			}
			if (cpumask_empty(cpumask_of_node(next_node)))
				continue;
			break;
		}
		channel->numa_node = next_node;
		primary = channel;
	}
	alloced_mask = &hv_context.hv_numa_map[primary->numa_node];

	if (cpumask_weight(alloced_mask) ==
	    cpumask_weight(cpumask_of_node(primary->numa_node))) {
		/*
		 * We have cycled through all the CPUs in the node;
		 * reset the alloced map.
		 */
		cpumask_clear(alloced_mask);
	}

	cpumask_xor(&available_mask, alloced_mask,
		    cpumask_of_node(primary->numa_node));

	cur_cpu = -1;

	if (primary->affinity_policy == HV_LOCALIZED) {
		/*
		 * Normally Hyper-V host doesn't create more subchannels
		 * than there are VCPUs on the node but it is possible when not
		 * all present VCPUs on the node are initialized by guest.
		 * Clear the alloced_cpus_in_node to start over.
		 */
		if (cpumask_equal(&primary->alloced_cpus_in_node,
				  cpumask_of_node(primary->numa_node)))
			cpumask_clear(&primary->alloced_cpus_in_node);
	}

	while (true) {
		cur_cpu = cpumask_next(cur_cpu, &available_mask);
		if (cur_cpu >= nr_cpu_ids) {
			cur_cpu = -1;
			cpumask_copy(&available_mask,
				     cpumask_of_node(primary->numa_node));
			continue;
		}

		if (primary->affinity_policy == HV_LOCALIZED) {
			/*
			 * NOTE: in the case of sub-channel, we clear the
			 * sub-channel related bit(s) in
			 * primary->alloced_cpus_in_node in
			 * hv_process_channel_removal(), so when we
			 * reload drivers like hv_netvsc in SMP guest, here
			 * we're able to re-allocate
			 * bit from primary->alloced_cpus_in_node.
			 */
			if (!cpumask_test_cpu(cur_cpu,
					      &primary->alloced_cpus_in_node)) {
				cpumask_set_cpu(cur_cpu,
						&primary->alloced_cpus_in_node);
				cpumask_set_cpu(cur_cpu, alloced_mask);
				break;
			}
		} else {
			cpumask_set_cpu(cur_cpu, alloced_mask);
			break;
		}
	}

	channel->target_cpu = cur_cpu;
	channel->target_vp = hv_context.vp_index[cur_cpu];
}

static void vmbus_wait_for_unload(void)
{
	int cpu;
	void *page_addr;
	struct hv_message *msg;
	struct vmbus_channel_message_header *hdr;
	u32 message_type;

	/*
	 * CHANNELMSG_UNLOAD_RESPONSE is always delivered to the CPU which was
	 * used for initial contact or to CPU0 depending on host version. When
	 * we're crashing on a different CPU let's hope that IRQ handler on
	 * the cpu which receives CHANNELMSG_UNLOAD_RESPONSE is still
	 * functional and vmbus_unload_response() will complete
	 * vmbus_connection.unload_event. If not, the last thing we can do is
	 * read message pages for all CPUs directly.
	 */
	while (1) {
		if (completion_done(&vmbus_connection.unload_event))
			break;

		for_each_online_cpu(cpu) {
			page_addr = hv_context.synic_message_page[cpu];
			msg = (struct hv_message *)page_addr +
				VMBUS_MESSAGE_SINT;

			message_type = READ_ONCE(msg->header.message_type);
			if (message_type == HVMSG_NONE)
				continue;

			hdr = (struct vmbus_channel_message_header *)
				msg->u.payload;

			if (hdr->msgtype == CHANNELMSG_UNLOAD_RESPONSE)
				complete(&vmbus_connection.unload_event);

			vmbus_signal_eom(msg, message_type);
		}

		mdelay(10);
	}

	/*
	 * We're crashing and already got the UNLOAD_RESPONSE, cleanup all
	 * maybe-pending messages on all CPUs to be able to receive new
	 * messages after we reconnect.
	 */
	for_each_online_cpu(cpu) {
		page_addr = hv_context.synic_message_page[cpu];
		msg = (struct hv_message *)page_addr + VMBUS_MESSAGE_SINT;
		msg->header.message_type = HVMSG_NONE;
	}
}

/*
 * vmbus_unload_response - Handler for the unload response.
 */
static void vmbus_unload_response(struct vmbus_channel_message_header *hdr)
{
	/*
	 * This is a global event; just wakeup the waiting thread.
	 * Once we successfully unload, we can cleanup the monitor state.
	 */
	complete(&vmbus_connection.unload_event);
}

void vmbus_initiate_unload(bool crash)
{
	struct vmbus_channel_message_header hdr;

	/* Pre-Win2012R2 hosts don't support reconnect */
	if (vmbus_proto_version < VERSION_WIN8_1)
		return;

	init_completion(&vmbus_connection.unload_event);
	memset(&hdr, 0, sizeof(struct vmbus_channel_message_header));
	hdr.msgtype = CHANNELMSG_UNLOAD;
	vmbus_post_msg(&hdr, sizeof(struct vmbus_channel_message_header));

	/*
	 * vmbus_initiate_unload() is also called on crash and the crash can be
	 * happening in an interrupt context, where scheduling is impossible.
	 */
	if (!crash)
		wait_for_completion(&vmbus_connection.unload_event);
	else
		vmbus_wait_for_unload();
}

/*
 * vmbus_onoffer - Handler for channel offers from vmbus in parent partition.
 *
 */
static void vmbus_onoffer(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_offer_channel *offer;
	struct vmbus_channel *newchannel;

	offer = (struct vmbus_channel_offer_channel *)hdr;

	/* Allocate the channel object and save this offer. */
	newchannel = alloc_channel();
	if (!newchannel) {
		pr_err("Unable to allocate channel object\n");
		return;
	}

	/*
	 * By default we setup state to enable batched
	 * reading. A specific service can choose to
	 * disable this prior to opening the channel.
	 */
	newchannel->batched_reading = true;

	/*
	 * Setup state for signalling the host.
	 */
	newchannel->sig_event = (struct hv_input_signal_event *)
				(ALIGN((unsigned long)
				&newchannel->sig_buf,
				HV_HYPERCALL_PARAM_ALIGN));

	newchannel->sig_event->connectionid.asu32 = 0;
	newchannel->sig_event->connectionid.u.id = VMBUS_EVENT_CONNECTION_ID;
	newchannel->sig_event->flag_number = 0;
	newchannel->sig_event->rsvdz = 0;

	if (vmbus_proto_version != VERSION_WS2008) {
		newchannel->is_dedicated_interrupt =
				(offer->is_dedicated_interrupt != 0);
		newchannel->sig_event->connectionid.u.id =
				offer->connection_id;
	}

	memcpy(&newchannel->offermsg, offer,
	       sizeof(struct vmbus_channel_offer_channel));
	newchannel->monitor_grp = (u8)offer->monitorid / 32;
	newchannel->monitor_bit = (u8)offer->monitorid % 32;

	vmbus_process_offer(newchannel);
}

/*
 * vmbus_onoffer_rescind - Rescind offer handler.
 *
 * We queue a work item to process this offer synchronously
 */
static void vmbus_onoffer_rescind(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_rescind_offer *rescind;
	struct vmbus_channel *channel;
	unsigned long flags;
	struct device *dev;

	rescind = (struct vmbus_channel_rescind_offer *)hdr;

	mutex_lock(&vmbus_connection.channel_mutex);
	channel = relid2channel(rescind->child_relid);

	if (channel == NULL) {
		/*
		 * This is very impossible, because in
		 * vmbus_process_offer(), we have already invoked
		 * vmbus_release_relid() on error.
		 */
		goto out;
	}

	spin_lock_irqsave(&channel->lock, flags);
	channel->rescind = true;
	spin_unlock_irqrestore(&channel->lock, flags);

	if (channel->device_obj) {
		if (channel->chn_rescind_callback) {
			channel->chn_rescind_callback(channel);
			goto out;
		}
		/*
		 * We will have to unregister this device from the
		 * driver core.
		 */
		dev = get_device(&channel->device_obj->device);
		if (dev) {
			vmbus_device_unregister(channel->device_obj);
			put_device(dev);
		}
	} else {
		hv_process_channel_removal(channel,
			channel->offermsg.child_relid);
	}

out:
	mutex_unlock(&vmbus_connection.channel_mutex);
}

void vmbus_hvsock_device_unregister(struct vmbus_channel *channel)
{
	mutex_lock(&vmbus_connection.channel_mutex);

	BUG_ON(!is_hvsock_channel(channel));

	channel->rescind = true;
	vmbus_device_unregister(channel->device_obj);

	mutex_unlock(&vmbus_connection.channel_mutex);
}
EXPORT_SYMBOL_GPL(vmbus_hvsock_device_unregister);


/*
 * vmbus_onoffers_delivered -
 * This is invoked when all offers have been delivered.
 *
 * Nothing to do here.
 */
static void vmbus_onoffers_delivered(
			struct vmbus_channel_message_header *hdr)
{
}

/*
 * vmbus_onopen_result - Open result handler.
 *
 * This is invoked when we received a response to our channel open request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_onopen_result(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_open_result *result;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_open_channel *openmsg;
	unsigned long flags;

	result = (struct vmbus_channel_open_result *)hdr;

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_OPENCHANNEL) {
			openmsg =
			(struct vmbus_channel_open_channel *)msginfo->msg;
			if (openmsg->child_relid == result->child_relid &&
			    openmsg->openid == result->openid) {
				memcpy(&msginfo->response.open_result,
				       result,
				       sizeof(
					struct vmbus_channel_open_result));
				complete(&msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/*
 * vmbus_ongpadl_created - GPADL created handler.
 *
 * This is invoked when we received a response to our gpadl create request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_ongpadl_created(struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_gpadl_created *gpadlcreated;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_gpadl_header *gpadlheader;
	unsigned long flags;

	gpadlcreated = (struct vmbus_channel_gpadl_created *)hdr;

	/*
	 * Find the establish msg, copy the result and signal/unblock the wait
	 * event
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_GPADL_HEADER) {
			gpadlheader =
			(struct vmbus_channel_gpadl_header *)requestheader;

			if ((gpadlcreated->child_relid ==
			     gpadlheader->child_relid) &&
			    (gpadlcreated->gpadl == gpadlheader->gpadl)) {
				memcpy(&msginfo->response.gpadl_created,
				       gpadlcreated,
				       sizeof(
					struct vmbus_channel_gpadl_created));
				complete(&msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/*
 * vmbus_ongpadl_torndown - GPADL torndown handler.
 *
 * This is invoked when we received a response to our gpadl teardown request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_ongpadl_torndown(
			struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_gpadl_torndown *gpadl_torndown;
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_gpadl_teardown *gpadl_teardown;
	unsigned long flags;

	gpadl_torndown = (struct vmbus_channel_gpadl_torndown *)hdr;

	/*
	 * Find the open msg, copy the result and signal/unblock the wait event
	 */
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype == CHANNELMSG_GPADL_TEARDOWN) {
			gpadl_teardown =
			(struct vmbus_channel_gpadl_teardown *)requestheader;

			if (gpadl_torndown->gpadl == gpadl_teardown->gpadl) {
				memcpy(&msginfo->response.gpadl_torndown,
				       gpadl_torndown,
				       sizeof(
					struct vmbus_channel_gpadl_torndown));
				complete(&msginfo->waitevent);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/*
 * vmbus_onversion_response - Version response handler
 *
 * This is invoked when we received a response to our initiate contact request.
 * Find the matching request, copy the response and signal the requesting
 * thread.
 */
static void vmbus_onversion_response(
		struct vmbus_channel_message_header *hdr)
{
	struct vmbus_channel_msginfo *msginfo;
	struct vmbus_channel_message_header *requestheader;
	struct vmbus_channel_version_response *version_response;
	unsigned long flags;

	version_response = (struct vmbus_channel_version_response *)hdr;
	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);

	list_for_each_entry(msginfo, &vmbus_connection.chn_msg_list,
				msglistentry) {
		requestheader =
			(struct vmbus_channel_message_header *)msginfo->msg;

		if (requestheader->msgtype ==
		    CHANNELMSG_INITIATE_CONTACT) {
			memcpy(&msginfo->response.version_response,
			      version_response,
			      sizeof(struct vmbus_channel_version_response));
			complete(&msginfo->waitevent);
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);
}

/* Channel message dispatch table */
struct vmbus_channel_message_table_entry
	channel_message_table[CHANNELMSG_COUNT] = {
	{CHANNELMSG_INVALID,			0, NULL},
	{CHANNELMSG_OFFERCHANNEL,		0, vmbus_onoffer},
	{CHANNELMSG_RESCIND_CHANNELOFFER,	0, vmbus_onoffer_rescind},
	{CHANNELMSG_REQUESTOFFERS,		0, NULL},
	{CHANNELMSG_ALLOFFERS_DELIVERED,	1, vmbus_onoffers_delivered},
	{CHANNELMSG_OPENCHANNEL,		0, NULL},
	{CHANNELMSG_OPENCHANNEL_RESULT,		1, vmbus_onopen_result},
	{CHANNELMSG_CLOSECHANNEL,		0, NULL},
	{CHANNELMSG_GPADL_HEADER,		0, NULL},
	{CHANNELMSG_GPADL_BODY,			0, NULL},
	{CHANNELMSG_GPADL_CREATED,		1, vmbus_ongpadl_created},
	{CHANNELMSG_GPADL_TEARDOWN,		0, NULL},
	{CHANNELMSG_GPADL_TORNDOWN,		1, vmbus_ongpadl_torndown},
	{CHANNELMSG_RELID_RELEASED,		0, NULL},
	{CHANNELMSG_INITIATE_CONTACT,		0, NULL},
	{CHANNELMSG_VERSION_RESPONSE,		1, vmbus_onversion_response},
	{CHANNELMSG_UNLOAD,			0, NULL},
	{CHANNELMSG_UNLOAD_RESPONSE,		1, vmbus_unload_response},
	{CHANNELMSG_18,				0, NULL},
	{CHANNELMSG_19,				0, NULL},
	{CHANNELMSG_20,				0, NULL},
	{CHANNELMSG_TL_CONNECT_REQUEST,		0, NULL},
};

/*
 * vmbus_onmessage - Handler for channel protocol messages.
 *
 * This is invoked in the vmbus worker thread context.
 */
void vmbus_onmessage(void *context)
{
	struct hv_message *msg = context;
	struct vmbus_channel_message_header *hdr;
	int size;

	hdr = (struct vmbus_channel_message_header *)msg->u.payload;
	size = msg->header.payload_size;

	if (hdr->msgtype >= CHANNELMSG_COUNT) {
		pr_err("Received invalid channel message type %d size %d\n",
			   hdr->msgtype, size);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE,
				     (unsigned char *)msg->u.payload, size);
		return;
	}

	if (channel_message_table[hdr->msgtype].message_handler)
		channel_message_table[hdr->msgtype].message_handler(hdr);
	else
		pr_err("Unhandled channel message type %d\n", hdr->msgtype);
}

/*
 * vmbus_request_offers - Send a request to get all our pending offers.
 */
int vmbus_request_offers(void)
{
	struct vmbus_channel_message_header *msg;
	struct vmbus_channel_msginfo *msginfo;
	int ret;

	msginfo = kmalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_message_header),
			  GFP_KERNEL);
	if (!msginfo)
		return -ENOMEM;

	msg = (struct vmbus_channel_message_header *)msginfo->msg;

	msg->msgtype = CHANNELMSG_REQUESTOFFERS;


	ret = vmbus_post_msg(msg,
			       sizeof(struct vmbus_channel_message_header));
	if (ret != 0) {
		pr_err("Unable to request offers - %d\n", ret);

		goto cleanup;
	}

cleanup:
	kfree(msginfo);

	return ret;
}

/*
 * Retrieve the (sub) channel on which to send an outgoing request.
 * When a primary channel has multiple sub-channels, we try to
 * distribute the load equally amongst all available channels.
 */
struct vmbus_channel *vmbus_get_outgoing_channel(struct vmbus_channel *primary)
{
	struct list_head *cur, *tmp;
	int cur_cpu;
	struct vmbus_channel *cur_channel;
	struct vmbus_channel *outgoing_channel = primary;
	int next_channel;
	int i = 1;

	if (list_empty(&primary->sc_list))
		return outgoing_channel;

	next_channel = primary->next_oc++;

	if (next_channel > (primary->num_sc)) {
		primary->next_oc = 0;
		return outgoing_channel;
	}

	cur_cpu = hv_context.vp_index[get_cpu()];
	put_cpu();
	list_for_each_safe(cur, tmp, &primary->sc_list) {
		cur_channel = list_entry(cur, struct vmbus_channel, sc_list);
		if (cur_channel->state != CHANNEL_OPENED_STATE)
			continue;

		if (cur_channel->target_vp == cur_cpu)
			return cur_channel;

		if (i == next_channel)
			return cur_channel;

		i++;
	}

	return outgoing_channel;
}
EXPORT_SYMBOL_GPL(vmbus_get_outgoing_channel);

static void invoke_sc_cb(struct vmbus_channel *primary_channel)
{
	struct list_head *cur, *tmp;
	struct vmbus_channel *cur_channel;

	if (primary_channel->sc_creation_callback == NULL)
		return;

	list_for_each_safe(cur, tmp, &primary_channel->sc_list) {
		cur_channel = list_entry(cur, struct vmbus_channel, sc_list);

		primary_channel->sc_creation_callback(cur_channel);
	}
}

void vmbus_set_sc_create_callback(struct vmbus_channel *primary_channel,
				void (*sc_cr_cb)(struct vmbus_channel *new_sc))
{
	primary_channel->sc_creation_callback = sc_cr_cb;
}
EXPORT_SYMBOL_GPL(vmbus_set_sc_create_callback);

bool vmbus_are_subchannels_present(struct vmbus_channel *primary)
{
	bool ret;

	ret = !list_empty(&primary->sc_list);

	if (ret) {
		/*
		 * Invoke the callback on sub-channel creation.
		 * This will present a uniform interface to the
		 * clients.
		 */
		invoke_sc_cb(primary);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(vmbus_are_subchannels_present);

void vmbus_set_chn_rescind_callback(struct vmbus_channel *channel,
		void (*chn_rescind_cb)(struct vmbus_channel *))
{
	channel->chn_rescind_callback = chn_rescind_cb;
}
EXPORT_SYMBOL_GPL(vmbus_set_chn_rescind_callback);
