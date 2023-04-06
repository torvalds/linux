// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/gunyah.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/auxiliary_bus.h>
#include <linux/gunyah_rsc_mgr.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include <asm/gunyah.h>

#include "rsc_mgr.h"
#include "vm_mgr.h"

#define RM_RPC_API_VERSION_MASK		GENMASK(3, 0)
#define RM_RPC_HEADER_WORDS_MASK	GENMASK(7, 4)
#define RM_RPC_API_VERSION		FIELD_PREP(RM_RPC_API_VERSION_MASK, 1)
#define RM_RPC_HEADER_WORDS		FIELD_PREP(RM_RPC_HEADER_WORDS_MASK, \
						(sizeof(struct gh_rm_rpc_hdr) / sizeof(u32)))
#define RM_RPC_API			(RM_RPC_API_VERSION | RM_RPC_HEADER_WORDS)

#define RM_RPC_TYPE_CONTINUATION	0x0
#define RM_RPC_TYPE_REQUEST		0x1
#define RM_RPC_TYPE_REPLY		0x2
#define RM_RPC_TYPE_NOTIF		0x3
#define RM_RPC_TYPE_MASK		GENMASK(1, 0)

#define GH_RM_MAX_NUM_FRAGMENTS		62
#define RM_RPC_FRAGMENTS_MASK		GENMASK(7, 2)

struct gh_rm_rpc_hdr {
	u8 api;
	u8 type;
	__le16 seq;
	__le32 msg_id;
} __packed;

struct gh_rm_rpc_reply_hdr {
	struct gh_rm_rpc_hdr hdr;
	__le32 err_code; /* GH_RM_ERROR_* */
} __packed;

#define GH_RM_MAX_MSG_SIZE	(GH_MSGQ_MAX_MSG_SIZE - sizeof(struct gh_rm_rpc_hdr))

/* RM Error codes */
enum gh_rm_error {
	GH_RM_ERROR_OK			= 0x0,
	GH_RM_ERROR_UNIMPLEMENTED	= 0xFFFFFFFF,
	GH_RM_ERROR_NOMEM		= 0x1,
	GH_RM_ERROR_NORESOURCE		= 0x2,
	GH_RM_ERROR_DENIED		= 0x3,
	GH_RM_ERROR_INVALID		= 0x4,
	GH_RM_ERROR_BUSY		= 0x5,
	GH_RM_ERROR_ARGUMENT_INVALID	= 0x6,
	GH_RM_ERROR_HANDLE_INVALID	= 0x7,
	GH_RM_ERROR_VALIDATE_FAILED	= 0x8,
	GH_RM_ERROR_MAP_FAILED		= 0x9,
	GH_RM_ERROR_MEM_INVALID		= 0xA,
	GH_RM_ERROR_MEM_INUSE		= 0xB,
	GH_RM_ERROR_MEM_RELEASED	= 0xC,
	GH_RM_ERROR_VMID_INVALID	= 0xD,
	GH_RM_ERROR_LOOKUP_FAILED	= 0xE,
	GH_RM_ERROR_IRQ_INVALID		= 0xF,
	GH_RM_ERROR_IRQ_INUSE		= 0x10,
	GH_RM_ERROR_IRQ_RELEASED	= 0x11,
};

/**
 * struct gh_rm_connection - Represents a complete message from resource manager
 * @payload: Combined payload of all the fragments (msg headers stripped off).
 * @size: Size of the payload received so far.
 * @msg_id: Message ID from the header.
 * @type: RM_RPC_TYPE_REPLY or RM_RPC_TYPE_NOTIF.
 * @num_fragments: total number of fragments expected to be received.
 * @fragments_received: fragments received so far.
 * @reply: Fields used for request/reply sequences
 * @notification: Fields used for notifiations
 */
struct gh_rm_connection {
	void *payload;
	size_t size;
	__le32 msg_id;
	u8 type;

	u8 num_fragments;
	u8 fragments_received;

	union {
		/**
		 * @ret: Linux return code, there was an error processing connection
		 * @seq: Sequence ID for the main message.
		 * @rm_error: For request/reply sequences with standard replies
		 * @seq_done: Signals caller that the RM reply has been received
		 */
		struct {
			int ret;
			u16 seq;
			enum gh_rm_error rm_error;
			struct completion seq_done;
		} reply;

		/**
		 * @rm: Pointer to the RM that launched the connection
		 * @work: Triggered when all fragments of a notification received
		 */
		struct {
			struct gh_rm *rm;
			struct work_struct work;
		} notification;
	};
};

/**
 * struct gh_rm - private data for communicating w/Gunyah resource manager
 * @dev: pointer to device
 * @tx_ghrsc: message queue resource to TX to RM
 * @rx_ghrsc: message queue resource to RX from RM
 * @msgq: mailbox instance of above
 * @active_rx_connection: ongoing gh_rm_connection for which we're receiving fragments
 * @last_tx_ret: return value of last mailbox tx
 * @call_xarray: xarray to allocate & lookup sequence IDs for Request/Response flows
 * @next_seq: next ID to allocate (for xa_alloc_cyclic)
 * @cache: cache for allocating Tx messages
 * @send_lock: synchronization to allow only one request to be sent at a time
 * @nh: notifier chain for clients interested in RM notification messages
 * @miscdev: /dev/gunyah
 * @irq_domain: Domain to translate Gunyah hwirqs to Linux irqs
 */
struct gh_rm {
	struct device *dev;
	struct gh_resource tx_ghrsc;
	struct gh_resource rx_ghrsc;
	struct gh_msgq msgq;
	struct mbox_client msgq_client;
	struct gh_rm_connection *active_rx_connection;
	int last_tx_ret;

	struct xarray call_xarray;
	u32 next_seq;

	struct kmem_cache *cache;
	struct mutex send_lock;
	struct blocking_notifier_head nh;

	struct auxiliary_device adev;
	struct miscdevice miscdev;
	struct irq_domain *irq_domain;
};

/**
 * gh_rm_remap_error() - Remap Gunyah resource manager errors into a Linux error code
 * @gh_error: "Standard" return value from Gunyah resource manager
 */
static inline int gh_rm_remap_error(enum gh_rm_error rm_error)
{
	switch (rm_error) {
	case GH_RM_ERROR_OK:
		return 0;
	case GH_RM_ERROR_UNIMPLEMENTED:
		return -EOPNOTSUPP;
	case GH_RM_ERROR_NOMEM:
		return -ENOMEM;
	case GH_RM_ERROR_NORESOURCE:
		return -ENODEV;
	case GH_RM_ERROR_DENIED:
		return -EPERM;
	case GH_RM_ERROR_BUSY:
		return -EBUSY;
	case GH_RM_ERROR_INVALID:
	case GH_RM_ERROR_ARGUMENT_INVALID:
	case GH_RM_ERROR_HANDLE_INVALID:
	case GH_RM_ERROR_VALIDATE_FAILED:
	case GH_RM_ERROR_MAP_FAILED:
	case GH_RM_ERROR_MEM_INVALID:
	case GH_RM_ERROR_MEM_INUSE:
	case GH_RM_ERROR_MEM_RELEASED:
	case GH_RM_ERROR_VMID_INVALID:
	case GH_RM_ERROR_LOOKUP_FAILED:
	case GH_RM_ERROR_IRQ_INVALID:
	case GH_RM_ERROR_IRQ_INUSE:
	case GH_RM_ERROR_IRQ_RELEASED:
		return -EINVAL;
	default:
		return -EBADMSG;
	}
}

struct gh_irq_chip_data {
	u32 gh_virq;
};

static struct irq_chip gh_rm_irq_chip = {
	.name			= "Gunyah",
	.irq_enable		= irq_chip_enable_parent,
	.irq_disable		= irq_chip_disable_parent,
	.irq_ack		= irq_chip_ack_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_mask_ack		= irq_chip_mask_ack_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_set_wake		= irq_chip_set_wake_parent,
	.irq_set_vcpu_affinity	= irq_chip_set_vcpu_affinity_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static int gh_rm_irq_domain_alloc(struct irq_domain *d, unsigned int virq, unsigned int nr_irqs,
				 void *arg)
{
	struct gh_irq_chip_data *chip_data, *spec = arg;
	struct irq_fwspec parent_fwspec;
	struct gh_rm *rm = d->host_data;
	u32 gh_virq = spec->gh_virq;
	int ret;

	if (nr_irqs != 1 || gh_virq == U32_MAX)
		return -EINVAL;

	chip_data = kzalloc(sizeof(*chip_data), GFP_KERNEL);
	if (!chip_data)
		return -ENOMEM;

	chip_data->gh_virq = gh_virq;

	ret = irq_domain_set_hwirq_and_chip(d, virq, chip_data->gh_virq, &gh_rm_irq_chip,
						chip_data);
	if (ret)
		goto err_free_irq_data;

	parent_fwspec.fwnode = d->parent->fwnode;
	ret = arch_gh_fill_irq_fwspec_params(chip_data->gh_virq, &parent_fwspec);
	if (ret) {
		dev_err(rm->dev, "virq translation failed %u: %d\n", chip_data->gh_virq, ret);
		goto err_free_irq_data;
	}

	ret = irq_domain_alloc_irqs_parent(d, virq, nr_irqs, &parent_fwspec);
	if (ret)
		goto err_free_irq_data;

	return ret;
err_free_irq_data:
	kfree(chip_data);
	return ret;
}

static void gh_rm_irq_domain_free_single(struct irq_domain *d, unsigned int virq)
{
	struct gh_irq_chip_data *chip_data;
	struct irq_data *irq_data;

	irq_data = irq_domain_get_irq_data(d, virq);
	if (!irq_data)
		return;

	chip_data = irq_data->chip_data;

	kfree(chip_data);
	irq_data->chip_data = NULL;
}

static void gh_rm_irq_domain_free(struct irq_domain *d, unsigned int virq, unsigned int nr_irqs)
{
	unsigned int i;

	for (i = 0; i < nr_irqs; i++)
		gh_rm_irq_domain_free_single(d, virq);
}

static const struct irq_domain_ops gh_rm_irq_domain_ops = {
	.alloc		= gh_rm_irq_domain_alloc,
	.free		= gh_rm_irq_domain_free,
};

struct gh_resource *gh_rm_alloc_resource(struct gh_rm *rm, struct gh_rm_hyp_resource *hyp_resource)
{
	struct gh_resource *ghrsc;

	ghrsc = kzalloc(sizeof(*ghrsc), GFP_KERNEL);
	if (!ghrsc)
		return NULL;

	ghrsc->type = hyp_resource->type;
	ghrsc->capid = le64_to_cpu(hyp_resource->cap_id);
	ghrsc->irq = IRQ_NOTCONNECTED;
	ghrsc->rm_label = le32_to_cpu(hyp_resource->resource_label);
	if (hyp_resource->virq && le32_to_cpu(hyp_resource->virq) != U32_MAX) {
		struct gh_irq_chip_data irq_data = {
			.gh_virq = le32_to_cpu(hyp_resource->virq),
		};

		ghrsc->irq = irq_domain_alloc_irqs(rm->irq_domain, 1, NUMA_NO_NODE, &irq_data);
		if (ghrsc->irq < 0) {
			dev_err(rm->dev,
				"Failed to allocate interrupt for resource %d label: %d: %d\n",
				ghrsc->type, ghrsc->rm_label, ghrsc->irq);
			ghrsc->irq = IRQ_NOTCONNECTED;
		}
	}

	return ghrsc;
}

void gh_rm_free_resource(struct gh_resource *ghrsc)
{
	irq_dispose_mapping(ghrsc->irq);
	kfree(ghrsc);
}

static int gh_rm_init_connection_payload(struct gh_rm_connection *connection, void *msg,
					size_t hdr_size, size_t msg_size)
{
	size_t max_buf_size, payload_size;
	struct gh_rm_rpc_hdr *hdr = msg;

	if (msg_size < hdr_size)
		return -EINVAL;

	payload_size = msg_size - hdr_size;

	connection->num_fragments = FIELD_GET(RM_RPC_FRAGMENTS_MASK, hdr->type);
	connection->fragments_received = 0;

	/* There's not going to be any payload, no need to allocate buffer. */
	if (!payload_size && !connection->num_fragments)
		return 0;

	if (connection->num_fragments > GH_RM_MAX_NUM_FRAGMENTS)
		return -EINVAL;

	max_buf_size = payload_size + (connection->num_fragments * GH_RM_MAX_MSG_SIZE);

	connection->payload = kzalloc(max_buf_size, GFP_KERNEL);
	if (!connection->payload)
		return -ENOMEM;

	memcpy(connection->payload, msg + hdr_size, payload_size);
	connection->size = payload_size;
	return 0;
}

static void gh_rm_abort_connection(struct gh_rm *rm)
{
	switch (rm->active_rx_connection->type) {
	case RM_RPC_TYPE_REPLY:
		rm->active_rx_connection->reply.ret = -EIO;
		complete(&rm->active_rx_connection->reply.seq_done);
		break;
	case RM_RPC_TYPE_NOTIF:
		fallthrough;
	default:
		kfree(rm->active_rx_connection->payload);
		kfree(rm->active_rx_connection);
	}

	rm->active_rx_connection = NULL;
}

static void gh_rm_notif_work(struct work_struct *work)
{
	struct gh_rm_connection *connection = container_of(work, struct gh_rm_connection,
								notification.work);
	struct gh_rm *rm = connection->notification.rm;

	blocking_notifier_call_chain(&rm->nh, connection->msg_id, connection->payload);

	gh_rm_put(rm);
	kfree(connection->payload);
	kfree(connection);
}

static void gh_rm_process_notif(struct gh_rm *rm, void *msg, size_t msg_size)
{
	struct gh_rm_connection *connection;
	struct gh_rm_rpc_hdr *hdr = msg;
	int ret;

	if (rm->active_rx_connection)
		gh_rm_abort_connection(rm);

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return;

	connection->type = RM_RPC_TYPE_NOTIF;
	connection->msg_id = hdr->msg_id;

	gh_rm_get(rm);
	connection->notification.rm = rm;
	INIT_WORK(&connection->notification.work, gh_rm_notif_work);

	ret = gh_rm_init_connection_payload(connection, msg, sizeof(*hdr), msg_size);
	if (ret) {
		dev_err(rm->dev, "Failed to initialize connection for notification: %d\n", ret);
		gh_rm_put(rm);
		kfree(connection);
		return;
	}

	rm->active_rx_connection = connection;
}

static void gh_rm_process_rply(struct gh_rm *rm, void *msg, size_t msg_size)
{
	struct gh_rm_rpc_reply_hdr *reply_hdr = msg;
	struct gh_rm_connection *connection;
	u16 seq_id;

	seq_id = le16_to_cpu(reply_hdr->hdr.seq);
	connection = xa_load(&rm->call_xarray, seq_id);

	if (!connection || connection->msg_id != reply_hdr->hdr.msg_id)
		return;

	if (rm->active_rx_connection)
		gh_rm_abort_connection(rm);

	if (gh_rm_init_connection_payload(connection, msg, sizeof(*reply_hdr), msg_size)) {
		dev_err(rm->dev, "Failed to alloc connection buffer for sequence %d\n", seq_id);
		/* Send connection complete and error the client. */
		connection->reply.ret = -ENOMEM;
		complete(&connection->reply.seq_done);
		return;
	}

	connection->reply.rm_error = le32_to_cpu(reply_hdr->err_code);
	rm->active_rx_connection = connection;
}

static void gh_rm_process_cont(struct gh_rm *rm, struct gh_rm_connection *connection,
				void *msg, size_t msg_size)
{
	struct gh_rm_rpc_hdr *hdr = msg;
	size_t payload_size = msg_size - sizeof(*hdr);

	if (!rm->active_rx_connection)
		return;

	/*
	 * hdr->fragments and hdr->msg_id preserves the value from first reply
	 * or notif message. To detect mishandling, check it's still intact.
	 */
	if (connection->msg_id != hdr->msg_id ||
		connection->num_fragments != FIELD_GET(RM_RPC_FRAGMENTS_MASK, hdr->type)) {
		gh_rm_abort_connection(rm);
		return;
	}

	memcpy(connection->payload + connection->size, msg + sizeof(*hdr), payload_size);
	connection->size += payload_size;
	connection->fragments_received++;
}

static void gh_rm_try_complete_connection(struct gh_rm *rm)
{
	struct gh_rm_connection *connection = rm->active_rx_connection;

	if (!connection || connection->fragments_received != connection->num_fragments)
		return;

	switch (connection->type) {
	case RM_RPC_TYPE_REPLY:
		complete(&connection->reply.seq_done);
		break;
	case RM_RPC_TYPE_NOTIF:
		schedule_work(&connection->notification.work);
		break;
	default:
		dev_err_ratelimited(rm->dev, "Invalid message type (%d) received\n",
					connection->type);
		gh_rm_abort_connection(rm);
		break;
	}

	rm->active_rx_connection = NULL;
}

static void gh_rm_msgq_rx_data(struct mbox_client *cl, void *mssg)
{
	struct gh_rm *rm = container_of(cl, struct gh_rm, msgq_client);
	struct gh_msgq_rx_data *rx_data = mssg;
	size_t msg_size = rx_data->length;
	void *msg = rx_data->data;
	struct gh_rm_rpc_hdr *hdr;

	if (msg_size < sizeof(*hdr) || msg_size > GH_MSGQ_MAX_MSG_SIZE)
		return;

	hdr = msg;
	if (hdr->api != RM_RPC_API) {
		dev_err(rm->dev, "Unknown RM RPC API version: %x\n", hdr->api);
		return;
	}

	switch (FIELD_GET(RM_RPC_TYPE_MASK, hdr->type)) {
	case RM_RPC_TYPE_NOTIF:
		gh_rm_process_notif(rm, msg, msg_size);
		break;
	case RM_RPC_TYPE_REPLY:
		gh_rm_process_rply(rm, msg, msg_size);
		break;
	case RM_RPC_TYPE_CONTINUATION:
		gh_rm_process_cont(rm, rm->active_rx_connection, msg, msg_size);
		break;
	default:
		dev_err(rm->dev, "Invalid message type (%lu) received\n",
			FIELD_GET(RM_RPC_TYPE_MASK, hdr->type));
		return;
	}

	gh_rm_try_complete_connection(rm);
}

static void gh_rm_msgq_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	struct gh_rm *rm = container_of(cl, struct gh_rm, msgq_client);

	kmem_cache_free(rm->cache, mssg);
	rm->last_tx_ret = r;
}

static int gh_rm_send_request(struct gh_rm *rm, u32 message_id,
			      const void *req_buff, size_t req_buf_size,
			      struct gh_rm_connection *connection)
{
	size_t buf_size_remaining = req_buf_size;
	const void *req_buf_curr = req_buff;
	struct gh_msgq_tx_data *msg;
	struct gh_rm_rpc_hdr *hdr, hdr_template;
	u32 cont_fragments = 0;
	size_t payload_size;
	void *payload;
	int ret;

	if (req_buf_size > GH_RM_MAX_NUM_FRAGMENTS * GH_RM_MAX_MSG_SIZE) {
		dev_warn(rm->dev, "Limit exceeded for the number of fragments: %u\n",
			cont_fragments);
		dump_stack();
		return -E2BIG;
	}

	if (req_buf_size)
		cont_fragments = (req_buf_size - 1) / GH_RM_MAX_MSG_SIZE;

	hdr_template.api = RM_RPC_API;
	hdr_template.type = FIELD_PREP(RM_RPC_TYPE_MASK, RM_RPC_TYPE_REQUEST) |
			FIELD_PREP(RM_RPC_FRAGMENTS_MASK, cont_fragments);
	hdr_template.seq = cpu_to_le16(connection->reply.seq);
	hdr_template.msg_id = cpu_to_le32(message_id);

	ret = mutex_lock_interruptible(&rm->send_lock);
	if (ret)
		return ret;

	/* Consider also the 'request' packet for the loop count */
	do {
		msg = kmem_cache_zalloc(rm->cache, GFP_KERNEL);
		if (!msg) {
			ret = -ENOMEM;
			goto out;
		}

		/* Fill header */
		hdr = (struct gh_rm_rpc_hdr *)msg->data;
		*hdr = hdr_template;

		/* Copy payload */
		payload = hdr + 1;
		payload_size = min(buf_size_remaining, GH_RM_MAX_MSG_SIZE);
		memcpy(payload, req_buf_curr, payload_size);
		req_buf_curr += payload_size;
		buf_size_remaining -= payload_size;

		/* Force the last fragment to immediately alert the receiver */
		msg->push = !buf_size_remaining;
		msg->length = sizeof(*hdr) + payload_size;

		ret = mbox_send_message(gh_msgq_chan(&rm->msgq), msg);
		if (ret < 0) {
			kmem_cache_free(rm->cache, msg);
			break;
		}

		if (rm->last_tx_ret) {
			ret = rm->last_tx_ret;
			break;
		}

		hdr_template.type = FIELD_PREP(RM_RPC_TYPE_MASK, RM_RPC_TYPE_CONTINUATION) |
					FIELD_PREP(RM_RPC_FRAGMENTS_MASK, cont_fragments);
	} while (buf_size_remaining);

out:
	mutex_unlock(&rm->send_lock);
	return ret < 0 ? ret : 0;
}

/**
 * gh_rm_call: Achieve request-response type communication with RPC
 * @rm: Pointer to Gunyah resource manager internal data
 * @message_id: The RM RPC message-id
 * @req_buff: Request buffer that contains the payload
 * @req_buf_size: Total size of the payload
 * @resp_buf: Pointer to a response buffer
 * @resp_buf_size: Size of the response buffer
 *
 * Make a request to the RM-VM and wait for reply back. For a successful
 * response, the function returns the payload. The size of the payload is set in
 * resp_buf_size. The resp_buf should be freed by the caller when 0 is returned
 * and resp_buf_size != 0.
 *
 * req_buff should be not NULL for req_buf_size >0. If req_buf_size == 0,
 * req_buff *can* be NULL and no additional payload is sent.
 *
 * Context: Process context. Will sleep waiting for reply.
 * Return: 0 on success. <0 if error.
 */
int gh_rm_call(struct gh_rm *rm, u32 message_id, void *req_buff, size_t req_buf_size,
		void **resp_buf, size_t *resp_buf_size)
{
	struct gh_rm_connection *connection;
	u32 seq_id;
	int ret;

	/* message_id 0 is reserved. req_buf_size implies req_buf is not NULL */
	if (!message_id || (!req_buff && req_buf_size) || !rm)
		return -EINVAL;


	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return -ENOMEM;

	connection->type = RM_RPC_TYPE_REPLY;
	connection->msg_id = cpu_to_le32(message_id);

	init_completion(&connection->reply.seq_done);

	/* Allocate a new seq number for this connection */
	ret = xa_alloc_cyclic(&rm->call_xarray, &seq_id, connection, xa_limit_16b, &rm->next_seq,
				GFP_KERNEL);
	if (ret < 0)
		goto free;
	connection->reply.seq = lower_16_bits(seq_id);

	/* Send the request to the Resource Manager */
	ret = gh_rm_send_request(rm, message_id, req_buff, req_buf_size, connection);
	if (ret < 0)
		goto out;

	/* Wait for response */
	ret = wait_for_completion_interruptible(&connection->reply.seq_done);
	if (ret)
		goto out;

	/* Check for internal (kernel) error waiting for the response */
	if (connection->reply.ret) {
		ret = connection->reply.ret;
		if (ret != -ENOMEM)
			kfree(connection->payload);
		goto out;
	}

	/* Got a response, did resource manager give us an error? */
	if (connection->reply.rm_error != GH_RM_ERROR_OK) {
		dev_warn(rm->dev, "RM rejected message %08x. Error: %d\n", message_id,
			connection->reply.rm_error);
		dump_stack();
		ret = gh_rm_remap_error(connection->reply.rm_error);
		kfree(connection->payload);
		goto out;
	}

	/* Everything looks good, return the payload */
	if (resp_buf_size)
		*resp_buf_size = connection->size;
	if (connection->size && resp_buf)
		*resp_buf = connection->payload;
	else {
		/* kfree in case RM sent us multiple fragments but never any data in
		 * those fragments. We would've allocated memory for it, but connection->size == 0
		 */
		kfree(connection->payload);
	}

out:
	xa_erase(&rm->call_xarray, connection->reply.seq);
free:
	kfree(connection);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_rm_call);


int gh_rm_notifier_register(struct gh_rm *rm, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&rm->nh, nb);
}
EXPORT_SYMBOL_GPL(gh_rm_notifier_register);

int gh_rm_notifier_unregister(struct gh_rm *rm, struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&rm->nh, nb);
}
EXPORT_SYMBOL_GPL(gh_rm_notifier_unregister);

struct device *gh_rm_get(struct gh_rm *rm)
{
	return get_device(rm->miscdev.this_device);
}
EXPORT_SYMBOL_GPL(gh_rm_get);

void gh_rm_put(struct gh_rm *rm)
{
	put_device(rm->miscdev.this_device);
}
EXPORT_SYMBOL_GPL(gh_rm_put);

static long gh_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *miscdev = filp->private_data;
	struct gh_rm *rm = container_of(miscdev, struct gh_rm, miscdev);

	return gh_dev_vm_mgr_ioctl(rm, cmd, arg);
}

static const struct file_operations gh_dev_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= gh_dev_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

static void gh_adev_release(struct device *dev)
{
	/* no-op */
}

static int gh_adev_init(struct gh_rm *rm, const char *name)
{
	struct auxiliary_device *adev = &rm->adev;
	int ret = 0;

	adev->name = name;
	adev->dev.parent = rm->dev;
	adev->dev.release = gh_adev_release;
	ret = auxiliary_device_init(adev);
	if (ret)
		return ret;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return ret;
}

static int gh_msgq_platform_probe_direction(struct platform_device *pdev, bool tx,
					    struct gh_resource *ghrsc)
{
	struct device_node *node = pdev->dev.of_node;
	int ret;
	int idx = tx ? 0 : 1;

	ghrsc->type = tx ? GH_RESOURCE_TYPE_MSGQ_TX : GH_RESOURCE_TYPE_MSGQ_RX;

	ghrsc->irq = platform_get_irq(pdev, idx);
	if (ghrsc->irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq%d: %d\n", idx, ghrsc->irq);
		return ghrsc->irq;
	}

	ret = of_property_read_u64_index(node, "reg", idx, &ghrsc->capid);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get capid%d: %d\n", idx, ret);
		return ret;
	}

	return 0;
}

static int gh_identify(void)
{
	struct gh_hypercall_hyp_identify_resp gh_api;

	if (!arch_is_gh_guest())
		return -ENODEV;

	gh_hypercall_hyp_identify(&gh_api);

	pr_info("Running under Gunyah hypervisor %llx/v%u\n",
		FIELD_GET(GH_API_INFO_VARIANT_MASK, gh_api.api_info),
		gh_api_version(&gh_api));

	/* We might move this out to individual drivers if there's ever an API version bump */
	if (gh_api_version(&gh_api) != GH_API_V1) {
		pr_info("Unsupported Gunyah version: %u\n", gh_api_version(&gh_api));
		return -ENODEV;
	}

	return 0;
}

static int gh_rm_drv_probe(struct platform_device *pdev)
{
	struct irq_domain *parent_irq_domain;
	struct device_node *parent_irq_node;
	struct gh_msgq_tx_data *msg;
	struct gh_rm *rm;
	int ret;

	ret = gh_identify();
	if (ret)
		return ret;

	rm = devm_kzalloc(&pdev->dev, sizeof(*rm), GFP_KERNEL);
	if (!rm)
		return -ENOMEM;

	platform_set_drvdata(pdev, rm);
	rm->dev = &pdev->dev;

	mutex_init(&rm->send_lock);
	BLOCKING_INIT_NOTIFIER_HEAD(&rm->nh);
	xa_init_flags(&rm->call_xarray, XA_FLAGS_ALLOC);
	rm->cache = kmem_cache_create("gh_rm", struct_size(msg, data, GH_MSGQ_MAX_MSG_SIZE), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!rm->cache)
		return -ENOMEM;

	ret = gh_msgq_platform_probe_direction(pdev, true, &rm->tx_ghrsc);
	if (ret)
		goto err_cache;

	ret = gh_msgq_platform_probe_direction(pdev, false, &rm->rx_ghrsc);
	if (ret)
		goto err_cache;

	rm->msgq_client.dev = &pdev->dev;
	rm->msgq_client.tx_block = true;
	rm->msgq_client.rx_callback = gh_rm_msgq_rx_data;
	rm->msgq_client.tx_done = gh_rm_msgq_tx_done;

	ret = gh_msgq_init(&pdev->dev, &rm->msgq, &rm->msgq_client, &rm->tx_ghrsc, &rm->rx_ghrsc);
	if (ret)
		goto err_cache;

	parent_irq_node = of_irq_find_parent(pdev->dev.of_node);
	if (!parent_irq_node) {
		dev_err(&pdev->dev, "Failed to find interrupt parent of resource manager\n");
		ret = -ENODEV;
		goto err_msgq;
	}

	parent_irq_domain = irq_find_host(parent_irq_node);
	if (!parent_irq_domain) {
		dev_err(&pdev->dev, "Failed to find interrupt parent domain of resource manager\n");
		ret = -ENODEV;
		goto err_msgq;
	}

	rm->irq_domain = irq_domain_add_hierarchy(parent_irq_domain, 0, 0, pdev->dev.of_node,
							&gh_rm_irq_domain_ops, NULL);
	if (!rm->irq_domain) {
		dev_err(&pdev->dev, "Failed to add irq domain\n");
		ret = -ENODEV;
		goto err_msgq;
	}
	rm->irq_domain->host_data = rm;

	rm->miscdev.parent = &pdev->dev;
	rm->miscdev.name = "gunyah";
	rm->miscdev.minor = MISC_DYNAMIC_MINOR;
	rm->miscdev.fops = &gh_dev_fops;

	ret = misc_register(&rm->miscdev);
	if (ret)
		goto err_irq_domain;

	ret = gh_adev_init(rm, "gh_rm_core");
	if (ret) {
		dev_err(&pdev->dev, "Failed to add gh_rm_core device\n");
		goto err_misc_device;
	}

	return 0;

err_misc_device:
	misc_deregister(&rm->miscdev);
err_irq_domain:
	irq_domain_remove(rm->irq_domain);
err_msgq:
	mbox_free_channel(gh_msgq_chan(&rm->msgq));
	gh_msgq_remove(&rm->msgq);
err_cache:
	kmem_cache_destroy(rm->cache);
	return ret;
}

static int gh_rm_drv_remove(struct platform_device *pdev)
{
	struct gh_rm *rm = platform_get_drvdata(pdev);

	auxiliary_device_delete(&rm->adev);
	auxiliary_device_uninit(&rm->adev);
	misc_deregister(&rm->miscdev);
	irq_domain_remove(rm->irq_domain);
	mbox_free_channel(gh_msgq_chan(&rm->msgq));
	gh_msgq_remove(&rm->msgq);
	kmem_cache_destroy(rm->cache);

	return 0;
}

static const struct of_device_id gh_rm_of_match[] = {
	{ .compatible = "gunyah-resource-manager" },
	{}
};
MODULE_DEVICE_TABLE(of, gh_rm_of_match);

static struct platform_driver gh_rm_driver = {
	.probe = gh_rm_drv_probe,
	.remove = gh_rm_drv_remove,
	.driver = {
		.name = "gh_rsc_mgr",
		.of_match_table = gh_rm_of_match,
	},
};
module_platform_driver(gh_rm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Resource Manager Driver");
