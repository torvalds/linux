// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <linux/notifier.h>
#include <linux/irqdomain.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <linux/gunyah/gh_dbl.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/gunyah/gh_errno.h>
#include <linux/gunyah/gh_common.h>
#include <linux/gunyah/gh_rm_drv.h>

#include "gh_rm_drv_private.h"

#define GH_RM_MAX_NUM_FRAGMENTS	62

#define GH_RM_NO_IRQ_ALLOC	-1

#define GH_RM_MAX_MSG_SIZE_BYTES \
	(GH_MSGQ_MAX_MSG_SIZE_BYTES - sizeof(struct gh_rm_rpc_hdr))

/**
 * struct gh_rm_connection - Represents a complete message from resource manager
 * @payload: Combined payload of all the fragments without any RPC headers
 * @size: Size of the payload.
 * @msg_id: Message ID from the header.
 * @ret: Linux return code, set in case there was an error processing the connection.
 * @type: GH_RM_RPC_TYPE_RPLY or GH_RM_RPC_TYPE_NOTIF.
 * @num_fragments: total number of fragments expected to be received for this connection.
 * @fragments_recieved: fragments received so far.
 * @rm_error: For request/reply sequences with standard replies.
 * @seq: Sequence ID for the main message.
 */
struct gh_rm_connection {
	void *payload;
	size_t size;
	u32 msg_id;
	int ret;
	u8 type;

	u8 num_fragments;
	u8 fragments_received;

	/* only for req/reply sequence */
	u32 rm_error;
	u16 seq;
	struct completion seq_done;
};

struct gh_rm_notif_validate {
	struct gh_rm_connection *conn;
	struct work_struct work;
};

const static struct {
	enum gh_vm_names val;
	const char *image_name;
	const char *vm_name;
} vm_name_map[] = {
	{GH_PRIMARY_VM, "pvm", ""},
	{GH_TRUSTED_VM, "trustedvm", "qcom,trustedvm"},
	{GH_CPUSYS_VM, "cpusys_vm", "qcom,cpusysvm"},
	{GH_OEM_VM, "oem_vm", "qcom,oemvm"},
};

static struct task_struct *gh_rm_drv_recv_task;
static struct gh_msgq_desc *gh_rm_msgq_desc;
static gh_virtio_mmio_cb_t gh_virtio_mmio_fn;
static gh_wdog_manage_cb_t gh_wdog_manage_fn;
static gh_vcpu_affinity_set_cb_t gh_vcpu_affinity_set_fn;
static gh_vcpu_affinity_reset_cb_t gh_vcpu_affinity_reset_fn;
static gh_vpm_grp_set_cb_t gh_vpm_grp_set_fn;
static gh_vpm_grp_reset_cb_t gh_vpm_grp_reset_fn;
static gh_all_res_populated_cb_t gh_all_res_populated_fn;

static DEFINE_MUTEX(gh_rm_call_idr_lock);
static DEFINE_MUTEX(gh_virtio_mmio_fn_lock);
static DEFINE_IDR(gh_rm_call_idr);
static DEFINE_MUTEX(gh_rm_send_lock);

static struct device_node *gh_rm_intc;
static struct irq_domain *gh_rm_irq_domain;
static u32 gh_rm_base_virq;

SRCU_NOTIFIER_HEAD_STATIC(gh_rm_notifier);

/* non-static: used by gh_rm_iface */
bool gh_rm_core_initialized;

static void gh_rm_get_svm_res_work_fn(struct work_struct *work);
static DECLARE_WORK(gh_rm_get_svm_res_work, gh_rm_get_svm_res_work_fn);

enum gh_vm_names gh_get_image_name(const char *str)
{
	int vmid;

	for (vmid = 0; vmid < ARRAY_SIZE(vm_name_map); ++vmid) {
		if (!strcmp(str, vm_name_map[vmid].image_name))
			return vm_name_map[vmid].val;
	}
	pr_err("Can find vm index for image name %s\n", str);
	return GH_VM_MAX;
}
EXPORT_SYMBOL(gh_get_image_name);

enum gh_vm_names gh_get_vm_name(const char *str)
{
	int vmid;

	for (vmid = 0; vmid < ARRAY_SIZE(vm_name_map); ++vmid) {
		if (!strcmp(str, vm_name_map[vmid].vm_name))
			return vm_name_map[vmid].val;
	}
	pr_err("Can find vm index for vm name %s\n", str);
	return GH_VM_MAX;
}
EXPORT_SYMBOL(gh_get_vm_name);

static struct gh_rm_connection *gh_rm_alloc_connection(u32 msg_id,
							bool needed)
{
	struct gh_rm_connection *connection;

	connection = kzalloc(sizeof(*connection), GFP_KERNEL);
	if (!connection)
		return ERR_PTR(-ENOMEM);

	if (needed)
		init_completion(&connection->seq_done);

	connection->msg_id = msg_id;

	return connection;
}

static int
gh_rm_init_connection_buff(struct gh_rm_connection *connection,
				void *recv_buff, size_t hdr_size,
				size_t payload_size)
{
	struct gh_rm_rpc_hdr *hdr = recv_buff;
	size_t max_buf_size;

	connection->num_fragments = hdr->fragments;
	connection->fragments_received = 0;
	connection->type = hdr->type;

	/* Some of the 'reply' types doesn't contain any payload */
	if (!payload_size)
		return 0;

	max_buf_size = (GH_MSGQ_MAX_MSG_SIZE_BYTES - hdr_size) *
			(hdr->fragments + 1);

	if (payload_size > max_buf_size) {
		pr_err("%s: Payload size exceeds max buff size\n", __func__);
		return -EINVAL;
	}

	/* If the data is split into multiple fragments, allocate a large
	 * enough buffer to hold the payloads for all the fragments.
	 */
	connection->payload = kzalloc(max_buf_size, GFP_KERNEL);
	if (!connection->payload)
		return -ENOMEM;

	memcpy(connection->payload, recv_buff + hdr_size, payload_size);
	connection->size = payload_size;

	return 0;
}

int gh_rm_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&gh_rm_notifier, nb);
}
EXPORT_SYMBOL(gh_rm_register_notifier);

int gh_rm_unregister_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&gh_rm_notifier, nb);
}
EXPORT_SYMBOL(gh_rm_unregister_notifier);

static int
gh_rm_validate_vm_exited_notif(void *payload, size_t payload_size)
{
	struct gh_rm_notif_vm_exited_payload *vm_exited_payload;

	if (payload_size < sizeof(*vm_exited_payload))
		return -EINVAL;

	vm_exited_payload = payload;

	switch (vm_exited_payload->exit_type) {
	case GH_RM_VM_EXIT_TYPE_VM_EXIT:
		if (payload_size !=
		    sizeof(*vm_exited_payload) + sizeof(struct gh_vm_exit_reason_vm_exit)) {
			pr_err("%s: Invalid size for type VM_EXIT: %u\n",
				__func__, payload_size);
			return -EINVAL;
		}
		break;
	case GH_RM_VM_EXIT_TYPE_WDT_BITE:
		break;
	case GH_RM_VM_EXIT_TYPE_HYP_ERROR:
		break;
	case GH_RM_VM_EXIT_TYPE_ASYNC_EXT_ABORT:
		break;
	case GH_RM_VM_EXIT_TYPE_VM_STOP_FORCED:
		break;
	default:
		if (gh_arch_validate_vm_exited_notif(payload_size, vm_exited_payload)) {
			pr_err("%s: Unknown exit type: %u\n", __func__,
				vm_exited_payload->exit_type);
			return -EINVAL;
		}
	}

	return 0;
}

static void gh_rm_validate_notif(struct work_struct *work)
{
	struct gh_rm_connection *connection = NULL;
	struct gh_rm_notif_validate *validate_work;
	size_t payload_size;
	void *payload;
	u32 notification;

	validate_work = container_of(work, struct gh_rm_notif_validate, work);
	connection = validate_work->conn;
	payload = connection->payload;
	payload_size = connection->size;
	notification = connection->msg_id;
	pr_debug("Notification received from RM-VM: %x\n", notification);

	switch (notification) {
	case GH_RM_NOTIF_VM_STATUS:
		if (payload_size != sizeof(struct gh_rm_notif_vm_status_payload)) {
			pr_err("%s: Invalid size for VM_STATUS notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_VM_EXITED:
		if (gh_rm_validate_vm_exited_notif(payload, payload_size))
			goto err;
		break;
	case GH_RM_NOTIF_VM_SHUTDOWN:
		if (payload_size != sizeof(struct gh_rm_notif_vm_shutdown_payload)) {
			pr_err("%s: Invalid size for VM_SHUTDOWN notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_VM_IRQ_LENT:
		if (payload_size != sizeof(struct gh_rm_notif_vm_irq_lent_payload)) {
			pr_err("%s: Invalid size for VM_IRQ_LENT notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_VM_IRQ_RELEASED:
		if (payload_size != sizeof(struct gh_rm_notif_vm_irq_released_payload)) {
			pr_err("%s: Invalid size for VM_IRQ_REL notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_VM_IRQ_ACCEPTED:
		if (payload_size != sizeof(struct gh_rm_notif_vm_irq_accepted_payload)) {
			pr_err("%s: Invalid size for VM_IRQ_ACCEPTED notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_MEM_SHARED:
		if (payload_size < sizeof(struct gh_rm_notif_mem_shared_payload)) {
			pr_err("%s: Invalid size for MEM_SHARED notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_MEM_RELEASED:
		if (payload_size != sizeof(struct gh_rm_notif_mem_released_payload)) {
			pr_err("%s: Invalid size for MEM_RELEASED notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_MEM_ACCEPTED:
		if (payload_size != sizeof(struct gh_rm_notif_mem_accepted_payload)) {
			pr_err("%s: Invalid size for MEM_ACCEPTED notif: %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	case GH_RM_NOTIF_VM_CONSOLE_CHARS:
		if (payload_size >= sizeof(struct gh_rm_notif_vm_console_chars)) {
			struct gh_rm_notif_vm_console_chars *console_chars;
			u16 num_bytes;

			console_chars = payload;
			num_bytes = console_chars->num_bytes;

			if (sizeof(*console_chars) + num_bytes != payload_size) {
				pr_err("%s: Invalid size for VM_CONSOLE_CHARS notify %u\n",
				       __func__, payload_size);
				goto err;
			}
		} else {
			pr_err("%s: Invalid size for VM_CONSOLE_CHARS notify %u\n",
				__func__, payload_size);
			goto err;
		}
		break;
	default:
		pr_err("%s: Unknown notification received: %u\n", __func__,
			notification);
		goto err;
	}

	srcu_notifier_call_chain(&gh_rm_notifier, notification, payload);
err:
	kfree(payload);
	kfree(connection);
	kfree(validate_work);
}

static
struct gh_rm_connection *gh_rm_process_notif(void *msg, size_t msg_size)
{
	struct gh_rm_rpc_hdr *hdr = msg;
	struct gh_rm_connection *connection;

	connection = gh_rm_alloc_connection(hdr->msg_id, false);
	if (!connection)
		return NULL;

	if (gh_rm_init_connection_buff(connection, msg, sizeof(*hdr), msg_size - sizeof(*hdr))) {
		kfree(connection);
		return NULL;
	}

	return connection;
}

static
struct gh_rm_connection *gh_rm_process_rply(void *recv_buff, size_t recv_buff_size)
{
	struct gh_rm_rpc_reply_hdr *reply_hdr = recv_buff;
	struct gh_rm_rpc_hdr *hdr = recv_buff;
	struct gh_rm_connection *connection;
	size_t payload_size;
	int ret = 0;

	if (mutex_lock_interruptible(&gh_rm_call_idr_lock)) {
		ret = -ERESTARTSYS;
		return ERR_PTR(ret);
	}

	connection = idr_find(&gh_rm_call_idr, hdr->seq);
	mutex_unlock(&gh_rm_call_idr_lock);

	if (!connection || connection->seq != hdr->seq ||
	    connection->msg_id != hdr->msg_id) {
		pr_err("%s: Failed to get the connection info for seq: %d\n",
			__func__, hdr->seq);
		ret = -EINVAL;
		return ERR_PTR(ret);
	}

	payload_size = recv_buff_size - sizeof(*reply_hdr);

	ret = gh_rm_init_connection_buff(connection, recv_buff,
					sizeof(*reply_hdr), payload_size);
	if (ret < 0)
		return ERR_PTR(ret);

	connection->rm_error = reply_hdr->err_code;

	return connection;
}

static int gh_rm_process_cont(struct gh_rm_connection *connection,
			void *recv_buff, size_t recv_buff_size)
{
	struct gh_rm_rpc_hdr *hdr = recv_buff;
	size_t payload_size;

	if (!connection) {
		pr_err("%s: not processing a fragmented connection\n",
			__func__);
		return -EINVAL;
	}

	if (connection->msg_id != hdr->msg_id) {
		pr_err("%s: got message id %x when expecting %x\n",
			__func__, hdr->msg_id, connection->msg_id);
	}

	/*
	 * hdr->fragments preserves the value from the first 'reply/notif'
	 * message. For the sake of sanity, check if it's still intact.
	 */
	if (connection->num_fragments != hdr->fragments) {
		pr_err("%s: Number of fragments mismatch for seq: %d\n",
			__func__, hdr->seq);
		return -EINVAL;
	}

	payload_size = recv_buff_size - sizeof(*hdr);

	/* Keep appending the data to the previous fragment's end */
	memcpy(connection->payload + connection->size, recv_buff + sizeof(*hdr), payload_size);
	connection->size += payload_size;
	connection->fragments_received++;

	return 0;
}

static bool gh_rm_complete_connection(struct gh_rm_connection *connection)
{
	struct gh_rm_notif_validate *validate_work;

	if (!connection)
		return false;

	if (connection->fragments_received != connection->num_fragments)
		return false;

	switch (connection->type) {
	case GH_RM_RPC_TYPE_RPLY:
		complete(&connection->seq_done);
		break;
	case GH_RM_RPC_TYPE_NOTIF:
		validate_work = kzalloc(sizeof(*validate_work), GFP_KERNEL);
		if (validate_work == NULL) {
			kfree(connection->payload);
			kfree(connection);
			break;
		}

		validate_work->conn = connection;
		INIT_WORK(&validate_work->work, gh_rm_validate_notif);

		schedule_work(&validate_work->work);
		break;
	default:
		pr_err("Invalid message type (%d) received\n", connection->type);
		break;
	}

	return true;
}

static void gh_rm_abort_connection(struct gh_rm_connection *connection)
{
	switch (connection->type) {
	case GH_RM_RPC_TYPE_RPLY:
		connection->ret = -EIO;
		complete(&connection->seq_done);
		break;
	case GH_RM_RPC_TYPE_NOTIF:
		fallthrough;
	default:
		kfree(connection->payload);
		kfree(connection);
	}
}

static int gh_rm_recv_task_fn(void *data)
{
	struct gh_rm_connection *connection = NULL;
	struct gh_rm_rpc_hdr *hdr = NULL;
	size_t recv_buff_size;
	void *recv_buff;
	int ret;

	recv_buff = kzalloc(GH_MSGQ_MAX_MSG_SIZE_BYTES, GFP_KERNEL);
	if (!recv_buff)
		return -ENOMEM;

	while (!kthread_should_stop()) {
		/* Block until a new message is received */
		ret = gh_msgq_recv(gh_rm_msgq_desc, recv_buff,
					GH_MSGQ_MAX_MSG_SIZE_BYTES,
					&recv_buff_size, 0);
		if (ret < 0) {
			pr_err("%s: Failed to receive the message: %d\n", __func__, ret);
			continue;
		} else if (recv_buff_size <= sizeof(struct gh_rm_rpc_hdr)) {
			pr_err("%s: Invalid message size received\n", __func__);
			continue;
		}

		hdr = recv_buff;
		switch (hdr->type) {
		case GH_RM_RPC_TYPE_NOTIF:
			if (connection) {
				/* Not possible per protocol. Do something better than BUG_ON */
				pr_warn("Received start of new notification without finishing existing message series.\n");
				gh_rm_abort_connection(connection);
			}
			connection = gh_rm_process_notif(recv_buff, recv_buff_size);
			break;
		case GH_RM_RPC_TYPE_RPLY:
			if (connection) {
				/* Not possible per protocol. Do something better than BUG_ON */
				pr_warn("Received start of new reply without finishing existing message series.\n");
				gh_rm_abort_connection(connection);
			}
			connection = gh_rm_process_rply(recv_buff, recv_buff_size);
			break;
		case GH_RM_RPC_TYPE_CONT:
			if (!connection) {
				/* Not possible per protocol. Do something better than BUG_ON */
				pr_warn("Received a continuation message without receiving initial message\n");
				break;
			}
			gh_rm_process_cont(connection, recv_buff, recv_buff_size);
			break;
		default:
			pr_err("%s: Invalid message type (%d) received\n",
				__func__, hdr->type);
		}
		print_hex_dump_debug("gh_rm_recv: ", DUMP_PREFIX_OFFSET,
				     4, 1, recv_buff, recv_buff_size, false);

		if (gh_rm_complete_connection(connection))
			connection = NULL;
	}

	kfree(recv_buff);
	return 0;
}

static int gh_rm_send_request(u32 message_id,
				const void *req_buff, size_t req_buff_size,
				struct gh_rm_connection *connection)
{
	size_t buff_size_remaining = req_buff_size;
	const void *req_buff_curr = req_buff;
	struct gh_rm_rpc_hdr *hdr;
	unsigned long tx_flags;
	u32 num_fragments = 0;
	size_t payload_size;
	void *msg;
	int i, ret = 0;

	/* Drivers need probe defer
	 * when do RM call before RM driver initialized
	 */
	if (gh_rm_msgq_desc == NULL)
		return -EPROBE_DEFER;

	if (req_buff_size)
		num_fragments = (req_buff_size - 1) / GH_RM_MAX_MSG_SIZE_BYTES;

	if (num_fragments > GH_RM_MAX_NUM_FRAGMENTS) {
		pr_err("%s: Limit exceeded for the number of fragments: %u\n",
			__func__, num_fragments);
		return -E2BIG;
	}

	msg = kzalloc(GH_MSGQ_MAX_MSG_SIZE_BYTES, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (mutex_lock_interruptible(&gh_rm_send_lock)) {
		ret = -ERESTARTSYS;
		goto free_msg;
	}

	for (i = 0; i <= num_fragments; i++) {
		if (buff_size_remaining > GH_RM_MAX_MSG_SIZE_BYTES) {
			payload_size = GH_RM_MAX_MSG_SIZE_BYTES;
			buff_size_remaining -= payload_size;
		} else {
			payload_size = buff_size_remaining;
		}

		memset(msg, 0, GH_MSGQ_MAX_MSG_SIZE_BYTES);

		/* Fill header */
		hdr = msg;
		hdr->version = GH_RM_RPC_HDR_VERSION_ONE;
		hdr->hdr_words = GH_RM_RPC_HDR_WORDS;
		hdr->type = i == 0 ? GH_RM_RPC_TYPE_REQ : GH_RM_RPC_TYPE_CONT;
		hdr->fragments = num_fragments;
		hdr->seq = connection->seq;
		hdr->msg_id = message_id;

		/* Copy payload */
		memcpy(msg + sizeof(*hdr), req_buff_curr, payload_size);
		req_buff_curr += payload_size;

		/* Force the last fragment to be sent immediately to the receiver */
		tx_flags = (i == num_fragments) ? GH_MSGQ_TX_PUSH : 0;

		/* delay sending console characters to RM */
		if (message_id == GH_RM_RPC_MSG_ID_CALL_VM_CONSOLE_WRITE ||
		    message_id == GH_RM_RPC_MSG_ID_CALL_VM_CONSOLE_FLUSH)
			udelay(800);

		ret = gh_msgq_send(gh_rm_msgq_desc, msg, sizeof(*hdr) + payload_size, tx_flags);

		if (ret)
			break;
	}

	mutex_unlock(&gh_rm_send_lock);
free_msg:
	kfree(msg);
	return ret;
}

/**
 * gh_rm_call: Achieve request-response type communication with RPC
 * @message_id: The RM RPC message-id
 * @req_buff: Request buffer that contains the payload
 * @req_buff_size: Total size of the payload
 * @resp_buff_size: Size of the response buffer
 * @rm_error: Returns Gunyah standard error code for the response
 *
 * Make a request to the RM-VM and expect a reply back. For a successful
 * response, the function returns the payload and its size for the response.
 * Some of the reply types doesn't contain any payload, in which case, the
 * caller would see a NULL returned. Hence, it's recommended that the caller
 * first read the error code and then dereference the returned payload
 * (if applicable). Also, the caller should kfree the returned pointer
 * when done.
 */
void *gh_rm_call(gh_rm_msgid_t message_id,
			void *req_buff, size_t req_buff_size,
			size_t *resp_buff_size, int *rm_error)
{
	struct gh_rm_connection *connection;
	bool seq_done_needed = true;
	int req_ret;
	void *ret;

	if (!message_id || !resp_buff_size || !rm_error)
		return ERR_PTR(-EINVAL);

	connection = gh_rm_alloc_connection(message_id, seq_done_needed);
	if (IS_ERR_OR_NULL(connection))
		return connection;

	/* Allocate a new seq number for this connection */
	if (mutex_lock_interruptible(&gh_rm_call_idr_lock)) {
		kfree(connection);
		return ERR_PTR(-ERESTARTSYS);
	}

	connection->seq = idr_alloc_cyclic(&gh_rm_call_idr, connection,
					0, U16_MAX, GFP_KERNEL);
	mutex_unlock(&gh_rm_call_idr_lock);

	pr_debug("%s TX msg_id: %x\n", __func__, message_id);
	print_hex_dump_debug("@"__stringify(__func__)" TX: ", DUMP_PREFIX_OFFSET, 4, 1,
			     req_buff, req_buff_size, false);
	/* Send the request to the Resource Manager VM */
	req_ret = gh_rm_send_request(message_id,
					req_buff, req_buff_size,
					connection);
	if (req_ret < 0) {
		ret = ERR_PTR(req_ret);
		goto out;
	}

	/* Wait for response */
	wait_for_completion(&connection->seq_done);

	mutex_lock(&gh_rm_call_idr_lock);
	idr_remove(&gh_rm_call_idr, connection->seq);
	mutex_unlock(&gh_rm_call_idr_lock);

	*rm_error = connection->rm_error;
	if (connection->rm_error) {
		pr_err("%s: Reply for seq:%d failed with RM err: %d\n",
			__func__, connection->seq, connection->rm_error);
		ret = ERR_PTR(gh_remap_error(connection->rm_error));
		kfree(connection->payload);
		goto out;
	}

	if (connection->ret) {
		ret = ERR_PTR(connection->ret);
		kfree(connection->payload);
		goto out;
	}

	print_hex_dump_debug("@"__stringify(__func__)" RX: ", DUMP_PREFIX_OFFSET, 4, 1,
			     connection->payload, connection->size,
			     false);

	ret = connection->payload;
	*resp_buff_size = connection->size;

out:
	kfree(connection);
	return ret;
}

/**
 * gh_rm_virq_to_irq: Get a Linux IRQ from a Gunyah-compatible vIRQ
 * @virq: Gunyah-compatible vIRQ
 * @type: IRQ trigger type (IRQ_TYPE_EDGE_RISING)
 *
 * Returns the mapped Linux IRQ# at Gunyah's IRQ domain (i.e. GIC SPI)
 */
int gh_rm_virq_to_irq(u32 virq, u32 type)
{
	return gh_get_irq(virq, type, of_node_to_fwnode(gh_rm_intc));
}
EXPORT_SYMBOL(gh_rm_virq_to_irq);

/**
 * gh_rm_irq_to_virq: Get a Gunyah-compatible vIRQ from a Linux IRQ
 * @irq: Linux-assigned IRQ#
 * @virq: out value where Gunyah-compatible vIRQ is stored
 *
 * Returns 0 upon success, -EINVAL if the Linux IRQ could not be mapped to
 * a Gunyah vIRQ (i.e., the IRQ does not correspond to any GIC-level IRQ)
 */
int gh_rm_irq_to_virq(int irq, u32 *virq)
{
	struct irq_data *irq_data;

	irq_data = irq_domain_get_irq_data(gh_rm_irq_domain, irq);
	if (!irq_data)
		return -EINVAL;

	if (virq)
		*virq = irq_data->hwirq;

	return 0;
}
EXPORT_SYMBOL(gh_rm_irq_to_virq);

static int gh_rm_get_irq(struct gh_vm_get_hyp_res_resp_entry *res_entry)
{
	int ret, virq = res_entry->virq;

	/* For resources, such as DBL source, there's no IRQ. The virq_handle
	 * wouldn't be defined for such cases. Hence ignore such cases
	 */
	if ((!res_entry->virq_handle && !virq) || virq == U32_MAX)
		return 0;

	/* Allocate and bind a new IRQ if RM-VM hasn't already done already */
	if (virq == GH_RM_NO_IRQ_ALLOC) {
		ret = virq = gh_get_virq(gh_rm_base_virq, virq);
		if (ret < 0)
			return ret;

		/* Bind the vIRQ */
		ret = gh_rm_vm_irq_accept(res_entry->virq_handle, virq);
		if (ret < 0) {
			pr_err("%s: IRQ accept failed: %d\n",
				__func__, ret);
			gh_put_virq(virq);
			return ret;
		}
	}

	return gh_rm_virq_to_irq(virq, IRQ_TYPE_EDGE_RISING);
}

/**
 * gh_rm_get_vm_id_info: Query Resource Manager VM to get vm identification info.
 * @vmid: The vmid of VM whose id information needs to be queried.
 *
 * The function encodes the error codes via ERR_PTR. Hence, the caller is
 * responsible to check it with IS_ERR_OR_NULL().
 */
int gh_rm_get_vm_id_info(gh_vmid_t vmid)
{
	struct gh_vm_get_id_resp_entry *id_entries = NULL, *entry;
	struct gh_vm_property vm_prop = {0};
	void *info = NULL;
	int ret = 0;
	u32 n_id, i;
	enum gh_vm_names vm_name;

	id_entries = gh_rm_vm_get_id(vmid, &n_id);
	if (IS_ERR_OR_NULL(id_entries))
		return PTR_ERR(id_entries);

	pr_debug("%s: %d Info are associated with vmid %d\n",
		 __func__, n_id, vmid);

	entry = id_entries;
	for (i = 0; i < n_id; i++) {
		pr_debug("%s: idx:%d id_type %d reserved %d id_size %d\n",
			__func__, i,
			entry->id_type,
			entry->reserved,
			entry->id_size);

		info = kzalloc(entry->id_size % 4 ? entry->id_size + 1 :
							entry->id_size,
			GFP_KERNEL);

		if (!info) {
			ret = -ENOMEM;
			break;
		}

		memcpy(info, entry->id_info, entry->id_size);

		pr_debug("%s: idx:%d id_info %s\n", __func__, i, info);
		switch (entry->id_type) {
		case GH_RM_ID_TYPE_GUID:
			vm_prop.guid = info;
		break;
		case GH_RM_ID_TYPE_URI:
			vm_prop.uri = info;
		break;
		case GH_RM_ID_TYPE_NAME:
			vm_prop.name = info;
		break;
		case GH_RM_ID_TYPE_SIGN_AUTH:
			vm_prop.sign_auth = info;
		break;
		default:
			pr_err("%s: Unknown id type: %u\n",
				__func__, entry->id_type);
			ret = -EINVAL;
			kfree(info);
		}
		entry = (void *)entry + sizeof(*entry) +
			     round_up(entry->id_size, 4);
	}

	if (!ret) {
		vm_prop.vmid = vmid;
		if (vm_prop.name)
			vm_name = gh_get_vm_name(vm_prop.name);
		else
			vm_name = GH_VM_MAX;
		if (vm_name == GH_VM_MAX) {
			pr_err("Invalid vm name %s of VMID %d\n", vm_prop.name,
			       vmid);
			ret = -EINVAL;
		} else {
			ret = gh_update_vm_prop_table(vm_name, &vm_prop);
		}
	}

	kfree(id_entries);
	return ret;
}
EXPORT_SYMBOL(gh_rm_get_vm_id_info);

/**
 * gh_rm_populate_hyp_res: Query Resource Manager VM to get hyp resources.
 * @vmid: The vmid of resources to be queried.
 *
 * The function encodes the error codes via ERR_PTR. Hence, the caller is
 * responsible to check it with IS_ERR_OR_NULL().
 */
int gh_rm_populate_hyp_res(gh_vmid_t vmid, const char *vm_name)
{
	struct gh_vm_get_hyp_res_resp_entry *res_entries = NULL;
	int linux_irq, ret = 0;
	gh_capid_t cap_id;
	gh_label_t label;
	u32 n_res, i;
	u64 base = 0, size = 0;

	res_entries = gh_rm_vm_get_hyp_res(vmid, &n_res);
	if (IS_ERR_OR_NULL(res_entries))
		return PTR_ERR(res_entries);

	pr_debug("%s: %d Resources are associated with vmid %d\n",
		 __func__, n_res, vmid);

	for (i = 0; i < n_res; i++) {
		pr_debug("%s: idx:%d res_entries.res_type = 0x%x, res_entries.partner_vmid = 0x%x, res_entries.resource_handle = 0x%x, res_entries.resource_label = 0x%x, res_entries.cap_id_low = 0x%x, res_entries.cap_id_high = 0x%x, res_entries.virq_handle = 0x%x, res_entries.virq = 0x%x res_entries.base_high = 0x%x, res_entries.base_low = 0x%x, res_entries.size_high = 0x%x, res_entries.size_low = 0x%x\n",
			__func__, i,
			res_entries[i].res_type,
			res_entries[i].partner_vmid,
			res_entries[i].resource_handle,
			res_entries[i].resource_label,
			res_entries[i].cap_id_low,
			res_entries[i].cap_id_high,
			res_entries[i].virq_handle,
			res_entries[i].virq,
			res_entries[i].base_high,
			res_entries[i].base_low,
			res_entries[i].size_high,
			res_entries[i].size_low);

		ret = linux_irq = gh_rm_get_irq(&res_entries[i]);
		if (ret < 0)
			goto out;

		cap_id = (u64) res_entries[i].cap_id_high << 32 |
				res_entries[i].cap_id_low;
		base = (u64) res_entries[i].base_high << 32 |
				res_entries[i].base_low;
		size = (u64) res_entries[i].size_high << 32 |
				res_entries[i].size_low;
		label = res_entries[i].resource_label;

		/* Populate MessageQ, DBL and vCPUs cap tables */
		do {
			switch (res_entries[i].res_type) {
			case GH_RM_RES_TYPE_MQ_TX:
				ret = gh_msgq_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_TX, linux_irq);
				break;
			case GH_RM_RES_TYPE_MQ_RX:
				ret = gh_msgq_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_RX, linux_irq);
				break;
			case GH_RM_RES_TYPE_VCPU:
				if (gh_vcpu_affinity_set_fn)
					ret = (*gh_vcpu_affinity_set_fn)(vmid, label,
								cap_id, linux_irq);
				break;
			case GH_RM_RES_TYPE_DB_TX:
				ret = gh_dbl_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_TX, linux_irq);
				break;
			case GH_RM_RES_TYPE_DB_RX:
				ret = gh_dbl_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_RX, linux_irq);
				break;
			case GH_RM_RES_TYPE_VPMGRP:
				if (gh_vpm_grp_set_fn)
					ret = (*gh_vpm_grp_set_fn)(vmid, cap_id, linux_irq);
				break;
			case GH_RM_RES_TYPE_VIRTIO_MMIO:
				mutex_lock(&gh_virtio_mmio_fn_lock);
				if (!gh_virtio_mmio_fn) {
					mutex_unlock(&gh_virtio_mmio_fn_lock);
					break;
				}

				ret = (*gh_virtio_mmio_fn)(vmid, vm_name, label,
						cap_id, linux_irq, base, size);
				mutex_unlock(&gh_virtio_mmio_fn_lock);
				break;
			case GH_RM_RES_TYPE_WATCHDOG:
				if (gh_wdog_manage_fn)
					ret = (*gh_wdog_manage_fn)(vmid, cap_id, true);
				break;
			default:
				pr_err("%s: Unknown resource type: %u\n",
					__func__, res_entries[i].res_type);
				ret = -EINVAL;
			}
		} while (ret == -EAGAIN);

		if (ret < 0)
			goto out;
	}

	if (gh_all_res_populated_fn)
		(*gh_all_res_populated_fn)(vmid, true);
out:
	kfree(res_entries);
	return ret;
}
EXPORT_SYMBOL(gh_rm_populate_hyp_res);

static void
gh_rm_put_irq(struct gh_vm_get_hyp_res_resp_entry *res_entry, int irq)
{
	if (!gh_put_irq(irq))
		gh_rm_vm_irq_release(res_entry->virq_handle);

}

/**
 * gh_rm_unpopulate_hyp_res: Unpopulate the resources that we got from
 *				gh_rm_populate_hyp_res().
 * @vmid: The vmid of resources to be queried.
 * @vm_name: The name of the VM
 *
 * Returns 0 on success and a negative error code upon failure.
 */
int gh_rm_unpopulate_hyp_res(gh_vmid_t vmid, const char *vm_name)
{
	struct gh_vm_get_hyp_res_resp_entry *res_entries = NULL;
	gh_label_t label;
	u32 n_res, i;
	int ret = 0, irq = -1;
	gh_capid_t cap_id;

	res_entries = gh_rm_vm_get_hyp_res(vmid, &n_res);
	if (IS_ERR_OR_NULL(res_entries))
		return PTR_ERR(res_entries);

	for (i = 0; i < n_res; i++) {
		label = res_entries[i].resource_label;
		cap_id = (u64) res_entries[i].cap_id_high << 32 |
				res_entries[i].cap_id_low;

		switch (res_entries[i].res_type) {
		case GH_RM_RES_TYPE_MQ_TX:
			ret = gh_msgq_reset_cap_info(label,
						GH_MSGQ_DIRECTION_TX, &irq);
			break;
		case GH_RM_RES_TYPE_MQ_RX:
			ret = gh_msgq_reset_cap_info(label,
						GH_MSGQ_DIRECTION_RX, &irq);
			break;
		case GH_RM_RES_TYPE_DB_TX:
			ret = gh_dbl_reset_cap_info(label,
						GH_RM_RES_TYPE_DB_TX, &irq);
			break;
		case GH_RM_RES_TYPE_DB_RX:
			ret = gh_dbl_reset_cap_info(label,
						GH_RM_RES_TYPE_DB_RX, &irq);
			break;
		case GH_RM_RES_TYPE_VCPU:
			if (gh_vcpu_affinity_reset_fn)
				ret = (*gh_vcpu_affinity_reset_fn)(vmid,
							label, cap_id, &irq);
			break;
		case GH_RM_RES_TYPE_VIRTIO_MMIO:
			/* Virtio cleanup is handled in gh_virtio_mmio_exit() */
			break;
		case GH_RM_RES_TYPE_VPMGRP:
			if (gh_vpm_grp_reset_fn)
				ret = (*gh_vpm_grp_reset_fn)(vmid, &irq);
			break;
		case GH_RM_RES_TYPE_WATCHDOG:
			if (gh_wdog_manage_fn)
				ret = (*gh_wdog_manage_fn)(vmid, cap_id, false);
			break;
		default:
			pr_err("%s: Unknown resource type: %u\n",
				__func__, res_entries[i].res_type);
			ret = -EINVAL;
		}

		if (ret < 0)
			goto out;

		if (irq >= 0)
			gh_rm_put_irq(&res_entries[i], irq);
	}

	if (gh_all_res_populated_fn)
		(*gh_all_res_populated_fn)(vmid, false);
out:
	kfree(res_entries);
	return ret;
}
EXPORT_SYMBOL(gh_rm_unpopulate_hyp_res);

/**
 * gh_rm_set_virtio_mmio_cb: Set callback that handles virtio MMIO resource
 * @fnptr: Pointer to callback function
 *
 * gh_rm_populate_hyp_res() queries RM-VM for all resources assigned to a VM and
 * as part of that response RM-VM will indicate resources assigned exclusively
 * to handle virtio communication between the two VMs. @fnptr callback is
 * invoked providing details of the virtio resource allocated for a particular
 * virtio device. @fnptr is expected to initialize additional state based on the
 * information provided.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_virtio_mmio_cb(gh_virtio_mmio_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	mutex_lock(&gh_virtio_mmio_fn_lock);
	if (gh_virtio_mmio_fn) {
		mutex_unlock(&gh_virtio_mmio_fn_lock);
		return -EBUSY;
	}

	gh_virtio_mmio_fn = fnptr;
	mutex_unlock(&gh_virtio_mmio_fn_lock);

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_virtio_mmio_cb);

/**
 * gh_rm_unset_virtio_mmio_cb: Unset callback that handles virtio MMIO resource
 */
void gh_rm_unset_virtio_mmio_cb(void)
{
	mutex_lock(&gh_virtio_mmio_fn_lock);
	gh_virtio_mmio_fn = NULL;
	mutex_unlock(&gh_virtio_mmio_fn_lock);
}
EXPORT_SYMBOL(gh_rm_unset_virtio_mmio_cb);

/**
 * gh_rm_set_wdog_manage_cb: Set callback that handles wdog resource
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the wdog resource.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_wdog_manage_cb(gh_wdog_manage_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_wdog_manage_fn)
		return -EBUSY;

	gh_wdog_manage_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_wdog_manage_cb);

/**
 * gh_rm_set_vcpu_affinity_cb: Set callback that handles vcpu affinity
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu resource.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_vcpu_affinity_cb(gh_vcpu_affinity_set_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vcpu_affinity_set_fn)
		return -EBUSY;

	gh_vcpu_affinity_set_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_vcpu_affinity_cb);

/**
 * gh_rm_reset_vcpu_affinity_cb: Reset callback that handles vcpu affinity
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu resource.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_reset_vcpu_affinity_cb(gh_vcpu_affinity_reset_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vcpu_affinity_reset_fn)
		return -EBUSY;

	gh_vcpu_affinity_reset_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_reset_vcpu_affinity_cb);

/**
 * gh_rm_set_vpm_grp_cb: Set callback that handles vpm grp state
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu grp state IRQ.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_vpm_grp_cb(gh_vpm_grp_set_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vpm_grp_set_fn)
		return -EBUSY;

	gh_vpm_grp_set_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_vpm_grp_cb);

/**
 * gh_rm_reset_vpm_grp_cb: Reset callback that handles vpm grp state
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu grp state IRQ.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_reset_vpm_grp_cb(gh_vpm_grp_reset_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vpm_grp_reset_fn)
		return -EBUSY;

	gh_vpm_grp_reset_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_reset_vpm_grp_cb);

/**
 * gh_rm_all_res_populated_cb: Set callback that handles all res populated
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked after all resources are populated/un-pupulated.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_all_res_populated_cb(gh_all_res_populated_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_all_res_populated_fn)
		return -EBUSY;

	gh_all_res_populated_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_all_res_populated_cb);

static void gh_rm_get_svm_res_work_fn(struct work_struct *work)
{
	gh_vmid_t vmid;
	int ret;

	ret = gh_rm_get_vmid(GH_PRIMARY_VM, &vmid);
	if (ret)
		pr_err("%s: Unable to get VMID for VM label %d\n",
						__func__, GH_PRIMARY_VM);
	else
		gh_rm_populate_hyp_res(vmid, NULL);
}

static int gh_vm_status_nb_handler(struct notifier_block *this,
					unsigned long cmd, void *data)
{
	struct gh_rm_notif_vm_status_payload *vm_status_payload = data;
	struct gh_vminfo vm_info = {0};
	enum gh_vm_names vm_name;
	u8 vm_status = vm_status_payload->vm_status;
	int ret;

	if (cmd != GH_RM_NOTIF_VM_STATUS)
		return NOTIFY_DONE;

	switch (vm_status) {
	case GH_RM_VM_STATUS_READY:
		pr_err("vm(%d) is ready\n", vm_status_payload->vmid);
		ret = gh_rm_get_vm_id_info(vm_status_payload->vmid);
		if (ret < 0) {
			pr_err("Failed to get vmid info for vmid = %d ret = %d\n",
				vm_status_payload->vmid, ret);
			return NOTIFY_DONE;
		}
		ret = gh_rm_get_vm_name(vm_status_payload->vmid, &vm_name);
		if (ret < 0) {
			pr_err("Failed to get vm name for vmid = %d ret = %d\n",
			       vm_status_payload->vmid, ret);
			return NOTIFY_DONE;
		}
		ret = gh_rm_get_vminfo(vm_name, &vm_info);
		if (ret < 0)
			pr_err("Failed to get vminfo of vmname = %s\n", vm_name);
		ret = gh_rm_populate_hyp_res(vm_status_payload->vmid,
					     vm_info.name);
		if (ret < 0) {
			pr_err("Failed to get hyp resources for vmid = %d vmname = %s ret = %d\n",
			       vm_status_payload->vmid, vm_name, ret);
			return NOTIFY_DONE;
		}
		break;
	case GH_RM_VM_STATUS_RUNNING:
		pr_err("vm(%d) started running\n", vm_status_payload->vmid);
		break;
	default:
		pr_err("Unknown notification receieved for vmid = %d vm_status = %d\n",
				vm_status_payload->vmid, vm_status);
	}

	return NOTIFY_DONE;
}


static struct notifier_block gh_vm_status_nb = {
	.notifier_call = gh_vm_status_nb_handler
};


static void gh_vm_check_peer(struct device *dev, struct device_node *rm_root)
{
	int peers_cnt, ret, i;
	const char **peers_array = NULL;
	const char *peer, *peer_data;
	gh_vmid_t vmid;
	enum gh_vm_names vm_name_index;
	struct gh_vminfo vm_info;
	uuid_t vm_guid;

	peers_cnt = of_property_count_strings(rm_root, "qcom,peers");
	peers_array = kcalloc(peers_cnt, sizeof(char *), GFP_KERNEL);
	if (!peers_array) {
		dev_err(dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		return;
	}

	ret = of_property_read_string_array(rm_root, "qcom,peers", peers_array,
					    peers_cnt);
	if (ret < 0) {
		dev_err(dev, "Failed to find qcom,peers\n");
		ret = -ENODEV;
		goto out;
	}

	for (i = 0; i < peers_cnt; i++) {
		peer = peers_array[i];
		if (peer == NULL)
			continue;
		if (strnstr(peer, "vm-name:", strlen("vm-name:")) != NULL) {
			peer_data = peer + strlen("vm-name:");
			dev_dbg(dev, "Trying to lookup name %s\n", peer_data);
			ret = gh_rm_vm_lookup(GH_VM_LOOKUP_NAME, peer_data,
					      strlen(peer_data), &vmid);
		} else if (strnstr(peer, "vm-uri:", strlen("vm-uri:")) !=
			   NULL) {
			peer_data = peer + strlen("vm-uri:");
			dev_dbg(dev, "Trying to lookup uri %s\n", peer_data);
			ret = gh_rm_vm_lookup(GH_VM_LOOKUP_URI, peer_data,
					      strlen(peer_data), &vmid);
		} else if (strnstr(peer, "vm-guid:", strlen("vm-guid:")) !=
			   NULL) {
			peer_data = peer + strlen("vm-guid:");
			dev_dbg(dev, "Trying to lookup guid %s\n", peer_data);
			ret = uuid_parse(peer_data, &vm_guid);
			if (ret != 0)
				dev_err(dev, "Invalid GUID:%s\n",
					peer + strlen("vm-guid:"));
			else
				ret = gh_rm_vm_lookup(GH_VM_LOOKUP_GUID,
						      (char *)&vm_guid,
						      sizeof(vm_guid), &vmid);
		} else {
			dev_err(dev, "Unknown peer type:%s\n", peer);
			continue;
		}
		if (ret < 0) {
			dev_err(dev,
				"lookup %s failed, VM is not running ret=%d\n",
				peer, ret);
			continue;
		}
		ret = gh_rm_get_vm_id_info(vmid);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get vmid info for vmid = %d ret = %d\n",
				vmid, ret);
			continue;
		}
		ret = gh_rm_get_vm_name(vmid, &vm_name_index);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get vmid info for vmid = %d ret = %d\n",
				vmid, ret);
			continue;
		}
		gh_rm_get_vminfo(vm_name_index, &vm_info);
		ret = gh_rm_populate_hyp_res(vmid, vm_info.name);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get hyp resources for vmid = %d ret = %d\n",
				vmid, ret);
			continue;
		}
	}
out:
	kfree(peers_array);
}

static int gh_vm_probe(struct device *dev, struct device_node *hyp_root)
{
	struct device_node *node;
	struct gh_vm_property temp_property = {0};
	int vmid, owner_vmid, ret;
	const char *vm_name;
	enum gh_vm_names vm_name_index;


	gh_init_vm_prop_table();

	node = of_find_compatible_node(hyp_root, NULL, "qcom,gunyah-vm-id-1.0");
	if (IS_ERR_OR_NULL(node)) {
		node = of_find_compatible_node(hyp_root, NULL, "qcom,haven-vm-id-1.0");
		if (IS_ERR_OR_NULL(node)) {
			dev_err(dev, "Could not find vm-id node\n");
			return -ENODEV;
		}
	}

	ret = of_property_read_u32(node, "qcom,vmid", &vmid);
	if (ret) {
		dev_err(dev, "Could not read vmid: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(node, "qcom,owner-vmid", &owner_vmid);
	if (ret) {
		/* We must be GH_PRIMARY_VM */
		temp_property.vmid = vmid;
		gh_update_vm_prop_table(GH_PRIMARY_VM, &temp_property);
		gh_rm_core_initialized = true;
	} else {
		ret = of_property_read_string(node, "qcom,image-name",
					      &vm_name);
		if (ret) {
			/* Just for compatible, if image-name cannot be found */
			/* Assume we are trusted VM */
			dev_dbg(dev,
				"Could not find qcom,image-name assume we are trustedvm\n");
			vm_name_index = GH_TRUSTED_VM;
		} else {
			vm_name_index = gh_get_vm_name(vm_name);
			if (vm_name_index == GH_VM_MAX) {
				dev_dbg(dev,
					"Could not find vm_name:%s assume we are trustedvm\n",
					vm_name);
				vm_name_index = GH_TRUSTED_VM;
			} else {
				dev_dbg(dev, "VM name index is %d\n",
					vm_name_index);
			}
		}
		temp_property.vmid = vmid;
		gh_update_vm_prop_table(vm_name_index, &temp_property);
		temp_property.vmid = owner_vmid;
		gh_update_vm_prop_table(GH_PRIMARY_VM, &temp_property);

		/* check peer to see if any VM has been bootup */
		gh_vm_check_peer(dev, node);
		gh_rm_register_notifier(&gh_vm_status_nb);
		gh_rm_core_initialized = true;
		/* Query RM for available resources */
		schedule_work(&gh_rm_get_svm_res_work);
	}

	return 0;
}

static const struct of_device_id gh_rm_drv_of_match[] = {
	{ .compatible = "qcom,resource-manager-1-0" },
	{ }
};

static int gh_rm_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	int ret;

	ret = gh_msgq_probe(pdev, GH_MSGQ_LABEL_RM);
	if (ret) {
		dev_err(dev, "Failed to probe message queue: %d\n", ret);
		return ret;
	}

	if (of_property_read_u32(node, "qcom,free-irq-start",
				 &gh_rm_base_virq)) {
		dev_err(dev, "Failed to get the vIRQ base\n");
		return -ENXIO;
	}

	gh_rm_intc = of_irq_find_parent(node);
	if (!gh_rm_intc) {
		dev_err(dev, "Failed to get the IRQ parent node\n");
		return -ENXIO;
	}
	gh_rm_irq_domain = irq_find_host(gh_rm_intc);
	if (!gh_rm_irq_domain) {
		dev_err(dev, "Failed to get IRQ domain associated with RM\n");
		return -ENXIO;
	}

	gh_rm_msgq_desc = gh_msgq_register(GH_MSGQ_LABEL_RM);
	if (IS_ERR_OR_NULL(gh_rm_msgq_desc))
		return PTR_ERR(gh_rm_msgq_desc);

	/* As we don't have a callback for message reception yet,
	 * spawn a kthread and always listen to incoming messages.
	 */
	gh_rm_drv_recv_task = kthread_run(gh_rm_recv_task_fn,
						NULL, "gh_rm_recv_task");
	if (IS_ERR_OR_NULL(gh_rm_drv_recv_task)) {
		ret = PTR_ERR(gh_rm_drv_recv_task);
		goto err_recv_task;
	}

	/* Probe the vmid */
	ret = gh_vm_probe(dev, node->parent);
	if (ret < 0 && ret != -ENODEV)
		goto err_recv_task;

	return 0;

err_recv_task:
	gh_msgq_unregister(gh_rm_msgq_desc);
	return ret;
}

static int gh_rm_drv_remove(struct platform_device *pdev)
{
	kthread_stop(gh_rm_drv_recv_task);
	gh_msgq_unregister(gh_rm_msgq_desc);
	idr_destroy(&gh_rm_call_idr);

	return 0;
}

static struct platform_driver gh_rm_driver = {
	.probe = gh_rm_drv_probe,
	.remove = gh_rm_drv_remove,
	.driver = {
		.name = "gh_rm_driver",
		.of_match_table = gh_rm_drv_of_match,
	},
};

module_platform_driver(gh_rm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah Resource Mgr. Driver");
