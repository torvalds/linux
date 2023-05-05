// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ratelimit.h>

#include <linux/gunyah/gh_msgq.h>
#include <linux/gunyah_rsc_mgr.h>
#include "hcall_msgq.h"

/* HVC call specific mask: 0 to 31 */
#define GH_MSGQ_HVC_FLAGS_MASK GENMASK_ULL(31, 0)

struct gh_msgq_cap_table;

struct gh_msgq_desc {
	int label;
	struct gh_msgq_cap_table *cap_table;
};

struct gh_msgq_cap_table {
	struct gh_msgq_desc *client_desc;
	spinlock_t cap_entry_lock;

	gh_capid_t tx_cap_id;
	gh_capid_t rx_cap_id;
	int tx_irq;
	int rx_irq;
	const char *tx_irq_name;
	const char *rx_irq_name;
	spinlock_t tx_lock;
	spinlock_t rx_lock;

	bool tx_full;
	bool rx_empty;
	wait_queue_head_t tx_wq;
	wait_queue_head_t rx_wq;

	int label;
	struct list_head entry;
};

static LIST_HEAD(gh_msgq_cap_list);
static DEFINE_SPINLOCK(gh_msgq_cap_list_lock);

struct gh_msgq_cap_table *gh_msgq_alloc_entry(int label)
{
	int ret;
	struct gh_msgq_cap_table *cap_table_entry = NULL;

	cap_table_entry = kzalloc(sizeof(struct gh_msgq_cap_table), GFP_ATOMIC);
	if (!cap_table_entry)
		return ERR_PTR(-ENOMEM);

	cap_table_entry->tx_cap_id = GH_CAPID_INVAL;
	cap_table_entry->rx_cap_id = GH_CAPID_INVAL;
	cap_table_entry->tx_full = false;
	cap_table_entry->rx_empty = true;
	cap_table_entry->label = label;
	init_waitqueue_head(&cap_table_entry->tx_wq);
	init_waitqueue_head(&cap_table_entry->rx_wq);
	spin_lock_init(&cap_table_entry->tx_lock);
	spin_lock_init(&cap_table_entry->rx_lock);
	spin_lock_init(&cap_table_entry->cap_entry_lock);

	cap_table_entry->tx_irq_name =
		kasprintf(GFP_ATOMIC, "gh_msgq_tx_%d", label);
	if (!cap_table_entry->tx_irq_name) {
		ret = -ENOMEM;
		goto err;
	}

	cap_table_entry->rx_irq_name =
		kasprintf(GFP_ATOMIC, "gh_msgq_rx_%d", label);
	if (!cap_table_entry->rx_irq_name) {
		ret = -ENOMEM;
		goto err;
	}

	list_add(&cap_table_entry->entry, &gh_msgq_cap_list);
	return cap_table_entry;
err:
	kfree(cap_table_entry->tx_irq_name);
	kfree(cap_table_entry->rx_irq_name);
	kfree(cap_table_entry);
	return ERR_PTR(ret);
}

static irqreturn_t gh_msgq_rx_isr(int irq, void *dev)
{
	struct gh_msgq_cap_table *cap_table_entry = dev;

	spin_lock(&cap_table_entry->rx_lock);
	cap_table_entry->rx_empty = false;
	spin_unlock(&cap_table_entry->rx_lock);

	wake_up_interruptible(&cap_table_entry->rx_wq);

	return IRQ_HANDLED;
}

static irqreturn_t gh_msgq_tx_isr(int irq, void *dev)
{
	struct gh_msgq_cap_table *cap_table_entry = dev;

	spin_lock(&cap_table_entry->tx_lock);
	cap_table_entry->tx_full = false;
	spin_unlock(&cap_table_entry->tx_lock);

	wake_up_interruptible(&cap_table_entry->tx_wq);

	return IRQ_HANDLED;
}

static int __gh_msgq_recv(struct gh_msgq_cap_table *cap_table_entry,
				void *buff, size_t buff_size,
				size_t *recv_size, u64 rx_flags)
{
	struct gh_hcall_msgq_recv_resp resp = {};
	unsigned long flags;
	int gh_ret;
	int ret = 0;

	/* Discard the driver specific flags, and keep only HVC specifics */
	rx_flags &= GH_MSGQ_HVC_FLAGS_MASK;

	spin_lock_irqsave(&cap_table_entry->rx_lock, flags);
	gh_ret = gh_hcall_msgq_recv(cap_table_entry->rx_cap_id, buff,
					buff_size, &resp);

	switch (gh_ret) {
	case GH_ERROR_OK:
		*recv_size = resp.recv_size;
		cap_table_entry->rx_empty = !resp.not_empty;
		ret = 0;
		break;
	case GH_ERROR_MSGQUEUE_EMPTY:
		cap_table_entry->rx_empty = true;
		ret = -EAGAIN;
		break;
	default:
		ret = gh_error_remap(gh_ret);
	}

	spin_unlock_irqrestore(&cap_table_entry->rx_lock, flags);

	if (ret != 0 && ret != -EAGAIN)
		pr_err("%s: Failed to recv from msgq. Hypercall error: %d\n",
			__func__, gh_ret);

	return ret;
}

/**
 * gh_msgq_recv: Receive a message from the client running on a different VM
 * @client_desc: The client descriptor that was obtained via gh_msgq_register()
 * @buff: Pointer to the buffer where the received data must be placed
 * @buff_size: The size of the buffer space available
 * @recv_size: The actual amount of data that is copied into buff
 * @flags: Optional flags to pass to receive the data. For the list of flags,
 *         see linux/gunyah/gh_msgq.h
 *
 * The function returns 0 if the data is successfully received and recv_size
 * would contain the actual amount of data copied into buff.
 * It returns -EINVAL if the caller passes invalid arguments, -EAGAIN
 * if the message queue is not yet ready to communicate, and -EPERM if the
 * caller doesn't have permissions to receive the data. In all these failure
 * cases, recv_size is unmodified.
 *
 * Note: this function may sleep and should not be called from interrupt
 *       context
 */
int gh_msgq_recv(void *msgq_client_desc,
			void *buff, size_t buff_size,
			size_t *recv_size, unsigned long flags)
{
	struct gh_msgq_desc *client_desc = msgq_client_desc;
	struct gh_msgq_cap_table *cap_table_entry;
	int ret;

	if (!client_desc || !buff || !buff_size || !recv_size)
		return -EINVAL;

	if (buff_size > GH_MSGQ_MAX_MSG_SIZE_BYTES)
		return -E2BIG;

	if (client_desc->cap_table == NULL)
		return -EAGAIN;

	cap_table_entry = client_desc->cap_table;

	spin_lock(&cap_table_entry->cap_entry_lock);

	if (cap_table_entry->client_desc != client_desc) {
		pr_err("%s: Invalid client descriptor\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	if ((cap_table_entry->rx_cap_id == GH_CAPID_INVAL) &&
		(flags & GH_MSGQ_NONBLOCK)) {
		pr_err_ratelimited(
			"%s: Recv info for label %d not yet initialized\n",
			__func__, client_desc->label);
		ret = -EAGAIN;
		goto err;
	}

	spin_unlock(&cap_table_entry->cap_entry_lock);

	if (wait_event_interruptible(cap_table_entry->rx_wq,
				cap_table_entry->rx_cap_id != GH_CAPID_INVAL))
		return -ERESTARTSYS;

	spin_lock(&cap_table_entry->cap_entry_lock);

	if (!cap_table_entry->rx_irq) {
		pr_err_ratelimited("%s: Rx IRQ for label %d not yet setup\n",
			__func__, client_desc->label);
		ret = -EAGAIN;
		goto err;
	}

	spin_unlock(&cap_table_entry->cap_entry_lock);

	do {
		if (cap_table_entry->rx_empty && (flags & GH_MSGQ_NONBLOCK))
			return -EAGAIN;

		if (wait_event_interruptible(cap_table_entry->rx_wq,
					!cap_table_entry->rx_empty))
			return -ERESTARTSYS;

		ret = __gh_msgq_recv(cap_table_entry, buff, buff_size,
					recv_size, flags);
	} while (ret == -EAGAIN);

	if (!ret)
		print_hex_dump_debug(__func__, DUMP_PREFIX_OFFSET,
				     4, 1, buff, *recv_size, false);

	return ret;

err:
	spin_unlock(&cap_table_entry->cap_entry_lock);
	return ret;
}
EXPORT_SYMBOL(gh_msgq_recv);

static int __gh_msgq_send(struct gh_msgq_cap_table *cap_table_entry,
				void *buff, size_t size, u64 tx_flags)
{
	struct gh_hcall_msgq_send_resp resp = {};
	unsigned long flags;
	int gh_ret;
	int ret = 0;

	/* Discard the driver specific flags, and keep only HVC specifics */
	tx_flags &= GH_MSGQ_HVC_FLAGS_MASK;

	print_hex_dump_debug("gh_msgq_send: ", DUMP_PREFIX_OFFSET,
			     4, 1, buff, size, false);

	spin_lock_irqsave(&cap_table_entry->tx_lock, flags);
	gh_ret = gh_hcall_msgq_send(cap_table_entry->tx_cap_id,
					size, buff, tx_flags, &resp);

	switch (gh_ret) {
	case GH_ERROR_OK:
		cap_table_entry->tx_full = !resp.not_full;
		ret = 0;
		break;
	case GH_ERROR_MSGQUEUE_FULL:
		cap_table_entry->tx_full = true;
		ret = -EAGAIN;
		break;
	default:
		ret = gh_error_remap(gh_ret);
	}

	spin_unlock_irqrestore(&cap_table_entry->tx_lock, flags);

	if (ret != 0 && ret != -EAGAIN)
		pr_err("%s: Failed to send on msgq. Hypercall error: %d\n",
			__func__, gh_ret);

	return ret;
}

/**
 * gh_msgq_send: Send a message to the client on a different VM
 * @client_desc: The client descriptor that was obtained via gh_msgq_register()
 * @buff: Pointer to the buffer that needs to be sent
 * @size: The size of the buffer
 * @flags: Optional flags to pass to send the data. For the list of flags,
 *         see linux/gunyah/gh_msgq.h
 *
 * The function returns -EINVAL if the caller passes invalid arguments,
 * -EAGAIN if the message queue is not yet ready to communicate, and -EPERM if
 * the caller doesn't have permissions to send the data.
 *
 */
int gh_msgq_send(void *msgq_client_desc,
			void *buff, size_t size, unsigned long flags)
{
	struct gh_msgq_desc *client_desc = msgq_client_desc;
	struct gh_msgq_cap_table *cap_table_entry;
	int ret;

	if (!client_desc || !buff || !size)
		return -EINVAL;

	if (size > GH_MSGQ_MAX_MSG_SIZE_BYTES)
		return -E2BIG;

	if (client_desc->cap_table == NULL)
		return -EAGAIN;

	cap_table_entry = client_desc->cap_table;

	spin_lock(&cap_table_entry->cap_entry_lock);

	if (cap_table_entry->client_desc != client_desc) {
		pr_err("%s: Invalid client descriptor\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	if ((cap_table_entry->tx_cap_id == GH_CAPID_INVAL) &&
		(flags & GH_MSGQ_NONBLOCK)) {
		pr_err_ratelimited(
			"%s: Send info for label %d not yet initialized\n",
			__func__, client_desc->label);
		ret = -EAGAIN;
		goto err;
	}

	spin_unlock(&cap_table_entry->cap_entry_lock);

	if (wait_event_interruptible(cap_table_entry->tx_wq,
				cap_table_entry->tx_cap_id != GH_CAPID_INVAL))
		return -ERESTARTSYS;

	spin_lock(&cap_table_entry->cap_entry_lock);

	if (!cap_table_entry->tx_irq) {
		pr_err_ratelimited("%s: Tx IRQ for label %d not yet setup\n",
			__func__, client_desc->label);
		ret = -EAGAIN;
		goto err;
	}

	spin_unlock(&cap_table_entry->cap_entry_lock);

	do {
		if (cap_table_entry->tx_full && (flags & GH_MSGQ_NONBLOCK))
			return -EAGAIN;

		if (wait_event_interruptible(cap_table_entry->tx_wq,
					!cap_table_entry->tx_full))
			return -ERESTARTSYS;

		ret = __gh_msgq_send(cap_table_entry, buff, size, flags);
	} while (ret == -EAGAIN);

	return ret;
err:
	spin_unlock(&cap_table_entry->cap_entry_lock);
	return ret;
}
EXPORT_SYMBOL(gh_msgq_send);

/**
 * gh_msgq_register: Register as a client to the use the message queue
 * @label: The label associated to the message queue that the client wants
 *         to communicate
 *
 * The function returns a descriptor for the clients to send and receive the
 * messages. Else, returns -EBUSY if some other client is already regitsered
 * to this label, and -EINVAL for invalid arguments. The caller should check
 * the return value using IS_ERR_OR_NULL() and PTR_ERR() to extract the error
 * code.
 */
void *gh_msgq_register(int label)
{
	struct gh_msgq_cap_table *cap_table_entry = NULL, *tmp_entry;
	struct gh_msgq_desc *client_desc;

	if (label < 0)
		return ERR_PTR(-EINVAL);

	spin_lock(&gh_msgq_cap_list_lock);
	list_for_each_entry(tmp_entry, &gh_msgq_cap_list, entry) {
		if (label == tmp_entry->label) {
			cap_table_entry = tmp_entry;
			break;
		}
	}

	if (cap_table_entry == NULL) {
		cap_table_entry = gh_msgq_alloc_entry(label);
		if (IS_ERR(cap_table_entry)) {
			spin_unlock(&gh_msgq_cap_list_lock);
			return cap_table_entry;
		}
	}
	spin_unlock(&gh_msgq_cap_list_lock);

	spin_lock(&cap_table_entry->cap_entry_lock);

	/* Multiple clients cannot register to the same label (msgq) */
	if (cap_table_entry->client_desc) {
		spin_unlock(&cap_table_entry->cap_entry_lock);
		pr_err("%s: Client already exists for label %d\n",
				__func__, label);
		return ERR_PTR(-EBUSY);
	}

	client_desc = kzalloc(sizeof(*client_desc), GFP_ATOMIC);
	if (!client_desc) {
		spin_unlock(&cap_table_entry->cap_entry_lock);
		return ERR_PTR(-ENOMEM);
	}

	client_desc->label = label;
	client_desc->cap_table = cap_table_entry;

	cap_table_entry->client_desc = client_desc;
	spin_unlock(&cap_table_entry->cap_entry_lock);

	pr_info("gh_msgq: Registered client for label: %d\n", label);

	return client_desc;
}
EXPORT_SYMBOL(gh_msgq_register);

/**
 * gh_msgq_unregister: Unregister as a client to the use the message queue
 * @client_desc: The descriptor that was passed via gh_msgq_register()
 *
 * The function returns 0 is the client was unregistered successfully. Else,
 * -EINVAL for invalid arguments.
 */
int gh_msgq_unregister(void *msgq_client_desc)
{
	struct gh_msgq_desc *client_desc = msgq_client_desc;
	struct gh_msgq_cap_table *cap_table_entry;

	if (!client_desc)
		return -EINVAL;

	cap_table_entry = client_desc->cap_table;

	spin_lock(&cap_table_entry->cap_entry_lock);

	/* Is the client trying to free someone else's msgq? */
	if (cap_table_entry->client_desc != client_desc) {
		pr_err("%s: Trying to free invalid client descriptor!\n",
			__func__);
		spin_unlock(&cap_table_entry->cap_entry_lock);
		return -EINVAL;
	}

	cap_table_entry->client_desc = NULL;

	spin_unlock(&cap_table_entry->cap_entry_lock);

	pr_info("%s: Unregistered client for label: %d\n",
		__func__, client_desc->label);

	kfree(client_desc);

	return 0;
}
EXPORT_SYMBOL(gh_msgq_unregister);

int gh_msgq_populate_cap_info(int label, u64 cap_id, int direction, int irq)
{
	struct gh_msgq_cap_table *cap_table_entry = NULL, *tmp_entry;
	int ret;

	if (label < 0) {
		pr_err("%s: Invalid label passed\n", __func__);
		return -EINVAL;
	}

	if (irq < 0) {
		pr_err("%s: Invalid IRQ number passed\n", __func__);
		return -ENXIO;
	}

	spin_lock(&gh_msgq_cap_list_lock);
	list_for_each_entry(tmp_entry, &gh_msgq_cap_list, entry) {
		if (label == tmp_entry->label) {
			cap_table_entry = tmp_entry;
			break;
		}
	}

	if (cap_table_entry == NULL) {
		cap_table_entry = gh_msgq_alloc_entry(label);
		if (IS_ERR(cap_table_entry)) {
			spin_unlock(&gh_msgq_cap_list_lock);
			return PTR_ERR(cap_table_entry);
		}
	}
	spin_unlock(&gh_msgq_cap_list_lock);

	if (direction == GH_MSGQ_DIRECTION_TX) {
		ret = request_irq(irq, gh_msgq_tx_isr, 0,
				cap_table_entry->tx_irq_name, cap_table_entry);
		if (ret < 0)
			goto err;

		spin_lock(&cap_table_entry->cap_entry_lock);
		cap_table_entry->tx_cap_id = cap_id;
		cap_table_entry->tx_irq = irq;
		spin_unlock(&cap_table_entry->cap_entry_lock);

		wake_up_interruptible(&cap_table_entry->tx_wq);
	} else if (direction == GH_MSGQ_DIRECTION_RX) {
		ret = request_irq(irq, gh_msgq_rx_isr, 0,
				cap_table_entry->rx_irq_name, cap_table_entry);
		if (ret < 0)
			goto err;

		spin_lock(&cap_table_entry->cap_entry_lock);
		cap_table_entry->rx_cap_id = cap_id;
		cap_table_entry->rx_irq = irq;
		spin_unlock(&cap_table_entry->cap_entry_lock);

		wake_up_interruptible(&cap_table_entry->rx_wq);
	} else {
		pr_err("%s: Invalid direction passed\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	irq_set_irq_wake(irq, 1);

	pr_debug(
		"%s: label: %d; cap_id: %llu; dir: %d; irq: %d\n",
		__func__, label, cap_id, direction, irq);

	return 0;

err:
	spin_lock(&gh_msgq_cap_list_lock);
	list_del(&cap_table_entry->entry);
	spin_unlock(&gh_msgq_cap_list_lock);
	kfree(cap_table_entry->tx_irq_name);
	kfree(cap_table_entry->rx_irq_name);
	kfree(cap_table_entry);
	return ret;
}
EXPORT_SYMBOL(gh_msgq_populate_cap_info);

/**
 * gh_msgq_reset_cap_info: Reset the msgq cap info
 * @label: The label associated to the message queue that the client wants
 *         to communicate
 * @direction: The direction of msgq
 * @irq: The irq associated with the msgq
 *
 * The function resets all the msgq related info.
 */
int gh_msgq_reset_cap_info(enum gh_msgq_label label, int direction, int *irq)
{
	struct gh_msgq_cap_table *cap_table_entry = NULL, *tmp_entry;
	int ret;

	if (label < 0) {
		pr_err("%s: Invalid label passed\n", __func__);
		return -EINVAL;
	}

	if (!irq)
		return -EINVAL;

	spin_lock(&gh_msgq_cap_list_lock);
	list_for_each_entry(tmp_entry, &gh_msgq_cap_list, entry) {
		if (label == tmp_entry->label) {
			cap_table_entry = tmp_entry;
			break;
		}
	}
	spin_unlock(&gh_msgq_cap_list_lock);

	if (cap_table_entry == NULL)
		return -EINVAL;

	if (direction == GH_MSGQ_DIRECTION_TX) {
		if (!cap_table_entry->tx_irq) {
			pr_err("%s: Tx IRQ not setup\n", __func__);
			ret = -ENXIO;
			goto err_unlock;
		}

		*irq = cap_table_entry->tx_irq;
		spin_lock(&cap_table_entry->cap_entry_lock);
		cap_table_entry->tx_cap_id = GH_CAPID_INVAL;
		cap_table_entry->tx_irq = 0;
		spin_unlock(&cap_table_entry->cap_entry_lock);
	} else if (direction == GH_MSGQ_DIRECTION_RX) {
		if (!cap_table_entry->rx_irq) {
			pr_err("%s: Rx IRQ not setup\n", __func__);
			ret = -ENXIO;
			goto err_unlock;
		}

		*irq = cap_table_entry->rx_irq;
		spin_lock(&cap_table_entry->cap_entry_lock);
		cap_table_entry->rx_cap_id = GH_CAPID_INVAL;
		cap_table_entry->rx_irq = 0;
		spin_unlock(&cap_table_entry->cap_entry_lock);
	} else {
		pr_err("%s: Invalid direction passed\n", __func__);
		ret = -EINVAL;
		goto err_unlock;
	}

	if (*irq)
		free_irq(*irq, cap_table_entry);

	return 0;

err_unlock:
	return ret;
}
EXPORT_SYMBOL(gh_msgq_reset_cap_info);

static void gh_msgq_cleanup(void)
{
	struct gh_msgq_cap_table *cap_table_entry;
	struct gh_msgq_cap_table *temp;

	spin_lock(&gh_msgq_cap_list_lock);
	list_for_each_entry_safe(cap_table_entry, temp, &gh_msgq_cap_list, entry) {
		kfree(cap_table_entry->tx_irq_name);
		kfree(cap_table_entry->rx_irq_name);
		kfree(cap_table_entry);
	}
	spin_unlock(&gh_msgq_cap_list_lock);
}

static int __init ghd_msgq_init(void)
{
	return 0;
}
module_init(ghd_msgq_init);

static void __exit ghd_msgq_exit(void)
{
	gh_msgq_cleanup();
}
module_exit(ghd_msgq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah Message Queue Driver");
