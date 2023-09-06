// SPDX-License-Identifier: GPL-2.0
/*
 * Data Object Exchange
 *	PCIe r6.0, sec 6.30 DOE
 *
 * Copyright (C) 2021 Huawei
 *	Jonathan Cameron <Jonathan.Cameron@huawei.com>
 *
 * Copyright (C) 2022 Intel Corporation
 *	Ira Weiny <ira.weiny@intel.com>
 */

#define dev_fmt(fmt) "DOE: " fmt

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci-doe.h>
#include <linux/workqueue.h>

#include "pci.h"

#define PCI_DOE_PROTOCOL_DISCOVERY 0

/* Timeout of 1 second from 6.30.2 Operation, PCI Spec r6.0 */
#define PCI_DOE_TIMEOUT HZ
#define PCI_DOE_POLL_INTERVAL	(PCI_DOE_TIMEOUT / 128)

#define PCI_DOE_FLAG_CANCEL	0
#define PCI_DOE_FLAG_DEAD	1

/* Max data object length is 2^18 dwords */
#define PCI_DOE_MAX_LENGTH	(1 << 18)

/**
 * struct pci_doe_mb - State for a single DOE mailbox
 *
 * This state is used to manage a single DOE mailbox capability.  All fields
 * should be considered opaque to the consumers and the structure passed into
 * the helpers below after being created by pci_doe_create_mb().
 *
 * @pdev: PCI device this mailbox belongs to
 * @cap_offset: Capability offset
 * @prots: Array of protocols supported (encoded as long values)
 * @wq: Wait queue for work item
 * @work_queue: Queue of pci_doe_work items
 * @flags: Bit array of PCI_DOE_FLAG_* flags
 */
struct pci_doe_mb {
	struct pci_dev *pdev;
	u16 cap_offset;
	struct xarray prots;

	wait_queue_head_t wq;
	struct workqueue_struct *work_queue;
	unsigned long flags;
};

struct pci_doe_protocol {
	u16 vid;
	u8 type;
};

/**
 * struct pci_doe_task - represents a single query/response
 *
 * @prot: DOE Protocol
 * @request_pl: The request payload
 * @request_pl_sz: Size of the request payload (bytes)
 * @response_pl: The response payload
 * @response_pl_sz: Size of the response payload (bytes)
 * @rv: Return value.  Length of received response or error (bytes)
 * @complete: Called when task is complete
 * @private: Private data for the consumer
 * @work: Used internally by the mailbox
 * @doe_mb: Used internally by the mailbox
 */
struct pci_doe_task {
	struct pci_doe_protocol prot;
	const __le32 *request_pl;
	size_t request_pl_sz;
	__le32 *response_pl;
	size_t response_pl_sz;
	int rv;
	void (*complete)(struct pci_doe_task *task);
	void *private;

	/* initialized by pci_doe_submit_task() */
	struct work_struct work;
	struct pci_doe_mb *doe_mb;
};

static int pci_doe_wait(struct pci_doe_mb *doe_mb, unsigned long timeout)
{
	if (wait_event_timeout(doe_mb->wq,
			       test_bit(PCI_DOE_FLAG_CANCEL, &doe_mb->flags),
			       timeout))
		return -EIO;
	return 0;
}

static void pci_doe_write_ctrl(struct pci_doe_mb *doe_mb, u32 val)
{
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;

	pci_write_config_dword(pdev, offset + PCI_DOE_CTRL, val);
}

static int pci_doe_abort(struct pci_doe_mb *doe_mb)
{
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;
	unsigned long timeout_jiffies;

	pci_dbg(pdev, "[%x] Issuing Abort\n", offset);

	timeout_jiffies = jiffies + PCI_DOE_TIMEOUT;
	pci_doe_write_ctrl(doe_mb, PCI_DOE_CTRL_ABORT);

	do {
		int rc;
		u32 val;

		rc = pci_doe_wait(doe_mb, PCI_DOE_POLL_INTERVAL);
		if (rc)
			return rc;
		pci_read_config_dword(pdev, offset + PCI_DOE_STATUS, &val);

		/* Abort success! */
		if (!FIELD_GET(PCI_DOE_STATUS_ERROR, val) &&
		    !FIELD_GET(PCI_DOE_STATUS_BUSY, val))
			return 0;

	} while (!time_after(jiffies, timeout_jiffies));

	/* Abort has timed out and the MB is dead */
	pci_err(pdev, "[%x] ABORT timed out\n", offset);
	return -EIO;
}

static int pci_doe_send_req(struct pci_doe_mb *doe_mb,
			    struct pci_doe_task *task)
{
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;
	size_t length, remainder;
	u32 val;
	int i;

	/*
	 * Check the DOE busy bit is not set. If it is set, this could indicate
	 * someone other than Linux (e.g. firmware) is using the mailbox. Note
	 * it is expected that firmware and OS will negotiate access rights via
	 * an, as yet to be defined, method.
	 */
	pci_read_config_dword(pdev, offset + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_BUSY, val))
		return -EBUSY;

	if (FIELD_GET(PCI_DOE_STATUS_ERROR, val))
		return -EIO;

	/* Length is 2 DW of header + length of payload in DW */
	length = 2 + DIV_ROUND_UP(task->request_pl_sz, sizeof(__le32));
	if (length > PCI_DOE_MAX_LENGTH)
		return -EIO;
	if (length == PCI_DOE_MAX_LENGTH)
		length = 0;

	/* Write DOE Header */
	val = FIELD_PREP(PCI_DOE_DATA_OBJECT_HEADER_1_VID, task->prot.vid) |
		FIELD_PREP(PCI_DOE_DATA_OBJECT_HEADER_1_TYPE, task->prot.type);
	pci_write_config_dword(pdev, offset + PCI_DOE_WRITE, val);
	pci_write_config_dword(pdev, offset + PCI_DOE_WRITE,
			       FIELD_PREP(PCI_DOE_DATA_OBJECT_HEADER_2_LENGTH,
					  length));

	/* Write payload */
	for (i = 0; i < task->request_pl_sz / sizeof(__le32); i++)
		pci_write_config_dword(pdev, offset + PCI_DOE_WRITE,
				       le32_to_cpu(task->request_pl[i]));

	/* Write last payload dword */
	remainder = task->request_pl_sz % sizeof(__le32);
	if (remainder) {
		val = 0;
		memcpy(&val, &task->request_pl[i], remainder);
		le32_to_cpus(&val);
		pci_write_config_dword(pdev, offset + PCI_DOE_WRITE, val);
	}

	pci_doe_write_ctrl(doe_mb, PCI_DOE_CTRL_GO);

	return 0;
}

static bool pci_doe_data_obj_ready(struct pci_doe_mb *doe_mb)
{
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;
	u32 val;

	pci_read_config_dword(pdev, offset + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_DATA_OBJECT_READY, val))
		return true;
	return false;
}

static int pci_doe_recv_resp(struct pci_doe_mb *doe_mb, struct pci_doe_task *task)
{
	size_t length, payload_length, remainder, received;
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;
	int i = 0;
	u32 val;

	/* Read the first dword to get the protocol */
	pci_read_config_dword(pdev, offset + PCI_DOE_READ, &val);
	if ((FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_VID, val) != task->prot.vid) ||
	    (FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_TYPE, val) != task->prot.type)) {
		dev_err_ratelimited(&pdev->dev, "[%x] expected [VID, Protocol] = [%04x, %02x], got [%04x, %02x]\n",
				    doe_mb->cap_offset, task->prot.vid, task->prot.type,
				    FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_VID, val),
				    FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_1_TYPE, val));
		return -EIO;
	}

	pci_write_config_dword(pdev, offset + PCI_DOE_READ, 0);
	/* Read the second dword to get the length */
	pci_read_config_dword(pdev, offset + PCI_DOE_READ, &val);
	pci_write_config_dword(pdev, offset + PCI_DOE_READ, 0);

	length = FIELD_GET(PCI_DOE_DATA_OBJECT_HEADER_2_LENGTH, val);
	/* A value of 0x0 indicates max data object length */
	if (!length)
		length = PCI_DOE_MAX_LENGTH;
	if (length < 2)
		return -EIO;

	/* First 2 dwords have already been read */
	length -= 2;
	received = task->response_pl_sz;
	payload_length = DIV_ROUND_UP(task->response_pl_sz, sizeof(__le32));
	remainder = task->response_pl_sz % sizeof(__le32);

	/* remainder signifies number of data bytes in last payload dword */
	if (!remainder)
		remainder = sizeof(__le32);

	if (length < payload_length) {
		received = length * sizeof(__le32);
		payload_length = length;
		remainder = sizeof(__le32);
	}

	if (payload_length) {
		/* Read all payload dwords except the last */
		for (; i < payload_length - 1; i++) {
			pci_read_config_dword(pdev, offset + PCI_DOE_READ,
					      &val);
			task->response_pl[i] = cpu_to_le32(val);
			pci_write_config_dword(pdev, offset + PCI_DOE_READ, 0);
		}

		/* Read last payload dword */
		pci_read_config_dword(pdev, offset + PCI_DOE_READ, &val);
		cpu_to_le32s(&val);
		memcpy(&task->response_pl[i], &val, remainder);
		/* Prior to the last ack, ensure Data Object Ready */
		if (!pci_doe_data_obj_ready(doe_mb))
			return -EIO;
		pci_write_config_dword(pdev, offset + PCI_DOE_READ, 0);
		i++;
	}

	/* Flush excess length */
	for (; i < length; i++) {
		pci_read_config_dword(pdev, offset + PCI_DOE_READ, &val);
		pci_write_config_dword(pdev, offset + PCI_DOE_READ, 0);
	}

	/* Final error check to pick up on any since Data Object Ready */
	pci_read_config_dword(pdev, offset + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_ERROR, val))
		return -EIO;

	return received;
}

static void signal_task_complete(struct pci_doe_task *task, int rv)
{
	task->rv = rv;
	destroy_work_on_stack(&task->work);
	task->complete(task);
}

static void signal_task_abort(struct pci_doe_task *task, int rv)
{
	struct pci_doe_mb *doe_mb = task->doe_mb;
	struct pci_dev *pdev = doe_mb->pdev;

	if (pci_doe_abort(doe_mb)) {
		/*
		 * If the device can't process an abort; set the mailbox dead
		 *	- no more submissions
		 */
		pci_err(pdev, "[%x] Abort failed marking mailbox dead\n",
			doe_mb->cap_offset);
		set_bit(PCI_DOE_FLAG_DEAD, &doe_mb->flags);
	}
	signal_task_complete(task, rv);
}

static void doe_statemachine_work(struct work_struct *work)
{
	struct pci_doe_task *task = container_of(work, struct pci_doe_task,
						 work);
	struct pci_doe_mb *doe_mb = task->doe_mb;
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;
	unsigned long timeout_jiffies;
	u32 val;
	int rc;

	if (test_bit(PCI_DOE_FLAG_DEAD, &doe_mb->flags)) {
		signal_task_complete(task, -EIO);
		return;
	}

	/* Send request */
	rc = pci_doe_send_req(doe_mb, task);
	if (rc) {
		/*
		 * The specification does not provide any guidance on how to
		 * resolve conflicting requests from other entities.
		 * Furthermore, it is likely that busy will not be detected
		 * most of the time.  Flag any detection of status busy with an
		 * error.
		 */
		if (rc == -EBUSY)
			dev_err_ratelimited(&pdev->dev, "[%x] busy detected; another entity is sending conflicting requests\n",
					    offset);
		signal_task_abort(task, rc);
		return;
	}

	timeout_jiffies = jiffies + PCI_DOE_TIMEOUT;
	/* Poll for response */
retry_resp:
	pci_read_config_dword(pdev, offset + PCI_DOE_STATUS, &val);
	if (FIELD_GET(PCI_DOE_STATUS_ERROR, val)) {
		signal_task_abort(task, -EIO);
		return;
	}

	if (!FIELD_GET(PCI_DOE_STATUS_DATA_OBJECT_READY, val)) {
		if (time_after(jiffies, timeout_jiffies)) {
			signal_task_abort(task, -EIO);
			return;
		}
		rc = pci_doe_wait(doe_mb, PCI_DOE_POLL_INTERVAL);
		if (rc) {
			signal_task_abort(task, rc);
			return;
		}
		goto retry_resp;
	}

	rc  = pci_doe_recv_resp(doe_mb, task);
	if (rc < 0) {
		signal_task_abort(task, rc);
		return;
	}

	signal_task_complete(task, rc);
}

static void pci_doe_task_complete(struct pci_doe_task *task)
{
	complete(task->private);
}

static int pci_doe_discovery(struct pci_doe_mb *doe_mb, u8 *index, u16 *vid,
			     u8 *protocol)
{
	u32 request_pl = FIELD_PREP(PCI_DOE_DATA_OBJECT_DISC_REQ_3_INDEX,
				    *index);
	__le32 request_pl_le = cpu_to_le32(request_pl);
	__le32 response_pl_le;
	u32 response_pl;
	int rc;

	rc = pci_doe(doe_mb, PCI_VENDOR_ID_PCI_SIG, PCI_DOE_PROTOCOL_DISCOVERY,
		     &request_pl_le, sizeof(request_pl_le),
		     &response_pl_le, sizeof(response_pl_le));
	if (rc < 0)
		return rc;

	if (rc != sizeof(response_pl_le))
		return -EIO;

	response_pl = le32_to_cpu(response_pl_le);
	*vid = FIELD_GET(PCI_DOE_DATA_OBJECT_DISC_RSP_3_VID, response_pl);
	*protocol = FIELD_GET(PCI_DOE_DATA_OBJECT_DISC_RSP_3_PROTOCOL,
			      response_pl);
	*index = FIELD_GET(PCI_DOE_DATA_OBJECT_DISC_RSP_3_NEXT_INDEX,
			   response_pl);

	return 0;
}

static void *pci_doe_xa_prot_entry(u16 vid, u8 prot)
{
	return xa_mk_value((vid << 8) | prot);
}

static int pci_doe_cache_protocols(struct pci_doe_mb *doe_mb)
{
	u8 index = 0;
	u8 xa_idx = 0;

	do {
		int rc;
		u16 vid;
		u8 prot;

		rc = pci_doe_discovery(doe_mb, &index, &vid, &prot);
		if (rc)
			return rc;

		pci_dbg(doe_mb->pdev,
			"[%x] Found protocol %d vid: %x prot: %x\n",
			doe_mb->cap_offset, xa_idx, vid, prot);

		rc = xa_insert(&doe_mb->prots, xa_idx++,
			       pci_doe_xa_prot_entry(vid, prot), GFP_KERNEL);
		if (rc)
			return rc;
	} while (index);

	return 0;
}

static void pci_doe_cancel_tasks(struct pci_doe_mb *doe_mb)
{
	/* Stop all pending work items from starting */
	set_bit(PCI_DOE_FLAG_DEAD, &doe_mb->flags);

	/* Cancel an in progress work item, if necessary */
	set_bit(PCI_DOE_FLAG_CANCEL, &doe_mb->flags);
	wake_up(&doe_mb->wq);
}

/**
 * pci_doe_create_mb() - Create a DOE mailbox object
 *
 * @pdev: PCI device to create the DOE mailbox for
 * @cap_offset: Offset of the DOE mailbox
 *
 * Create a single mailbox object to manage the mailbox protocol at the
 * cap_offset specified.
 *
 * RETURNS: created mailbox object on success
 *	    ERR_PTR(-errno) on failure
 */
static struct pci_doe_mb *pci_doe_create_mb(struct pci_dev *pdev,
					    u16 cap_offset)
{
	struct pci_doe_mb *doe_mb;
	int rc;

	doe_mb = kzalloc(sizeof(*doe_mb), GFP_KERNEL);
	if (!doe_mb)
		return ERR_PTR(-ENOMEM);

	doe_mb->pdev = pdev;
	doe_mb->cap_offset = cap_offset;
	init_waitqueue_head(&doe_mb->wq);
	xa_init(&doe_mb->prots);

	doe_mb->work_queue = alloc_ordered_workqueue("%s %s DOE [%x]", 0,
						dev_bus_name(&pdev->dev),
						pci_name(pdev),
						doe_mb->cap_offset);
	if (!doe_mb->work_queue) {
		pci_err(pdev, "[%x] failed to allocate work queue\n",
			doe_mb->cap_offset);
		rc = -ENOMEM;
		goto err_free;
	}

	/* Reset the mailbox by issuing an abort */
	rc = pci_doe_abort(doe_mb);
	if (rc) {
		pci_err(pdev, "[%x] failed to reset mailbox with abort command : %d\n",
			doe_mb->cap_offset, rc);
		goto err_destroy_wq;
	}

	/*
	 * The state machine and the mailbox should be in sync now;
	 * Use the mailbox to query protocols.
	 */
	rc = pci_doe_cache_protocols(doe_mb);
	if (rc) {
		pci_err(pdev, "[%x] failed to cache protocols : %d\n",
			doe_mb->cap_offset, rc);
		goto err_cancel;
	}

	return doe_mb;

err_cancel:
	pci_doe_cancel_tasks(doe_mb);
	xa_destroy(&doe_mb->prots);
err_destroy_wq:
	destroy_workqueue(doe_mb->work_queue);
err_free:
	kfree(doe_mb);
	return ERR_PTR(rc);
}

/**
 * pci_doe_destroy_mb() - Destroy a DOE mailbox object
 *
 * @doe_mb: DOE mailbox
 *
 * Destroy all internal data structures created for the DOE mailbox.
 */
static void pci_doe_destroy_mb(struct pci_doe_mb *doe_mb)
{
	pci_doe_cancel_tasks(doe_mb);
	xa_destroy(&doe_mb->prots);
	destroy_workqueue(doe_mb->work_queue);
	kfree(doe_mb);
}

/**
 * pci_doe_supports_prot() - Return if the DOE instance supports the given
 *			     protocol
 * @doe_mb: DOE mailbox capability to query
 * @vid: Protocol Vendor ID
 * @type: Protocol type
 *
 * RETURNS: True if the DOE mailbox supports the protocol specified
 */
static bool pci_doe_supports_prot(struct pci_doe_mb *doe_mb, u16 vid, u8 type)
{
	unsigned long index;
	void *entry;

	/* The discovery protocol must always be supported */
	if (vid == PCI_VENDOR_ID_PCI_SIG && type == PCI_DOE_PROTOCOL_DISCOVERY)
		return true;

	xa_for_each(&doe_mb->prots, index, entry)
		if (entry == pci_doe_xa_prot_entry(vid, type))
			return true;

	return false;
}

/**
 * pci_doe_submit_task() - Submit a task to be processed by the state machine
 *
 * @doe_mb: DOE mailbox capability to submit to
 * @task: task to be queued
 *
 * Submit a DOE task (request/response) to the DOE mailbox to be processed.
 * Returns upon queueing the task object.  If the queue is full this function
 * will sleep until there is room in the queue.
 *
 * task->complete will be called when the state machine is done processing this
 * task.
 *
 * @task must be allocated on the stack.
 *
 * Excess data will be discarded.
 *
 * RETURNS: 0 when task has been successfully queued, -ERRNO on error
 */
static int pci_doe_submit_task(struct pci_doe_mb *doe_mb,
			       struct pci_doe_task *task)
{
	if (!pci_doe_supports_prot(doe_mb, task->prot.vid, task->prot.type))
		return -EINVAL;

	if (test_bit(PCI_DOE_FLAG_DEAD, &doe_mb->flags))
		return -EIO;

	task->doe_mb = doe_mb;
	INIT_WORK_ONSTACK(&task->work, doe_statemachine_work);
	queue_work(doe_mb->work_queue, &task->work);
	return 0;
}

/**
 * pci_doe() - Perform Data Object Exchange
 *
 * @doe_mb: DOE Mailbox
 * @vendor: Vendor ID
 * @type: Data Object Type
 * @request: Request payload
 * @request_sz: Size of request payload (bytes)
 * @response: Response payload
 * @response_sz: Size of response payload (bytes)
 *
 * Submit @request to @doe_mb and store the @response.
 * The DOE exchange is performed synchronously and may therefore sleep.
 *
 * Payloads are treated as opaque byte streams which are transmitted verbatim,
 * without byte-swapping.  If payloads contain little-endian register values,
 * the caller is responsible for conversion with cpu_to_le32() / le32_to_cpu().
 *
 * For convenience, arbitrary payload sizes are allowed even though PCIe r6.0
 * sec 6.30.1 specifies the Data Object Header 2 "Length" in dwords.  The last
 * (partial) dword is copied with byte granularity and padded with zeroes if
 * necessary.  Callers are thus relieved of using dword-sized bounce buffers.
 *
 * RETURNS: Length of received response or negative errno.
 * Received data in excess of @response_sz is discarded.
 * The length may be smaller than @response_sz and the caller
 * is responsible for checking that.
 */
int pci_doe(struct pci_doe_mb *doe_mb, u16 vendor, u8 type,
	    const void *request, size_t request_sz,
	    void *response, size_t response_sz)
{
	DECLARE_COMPLETION_ONSTACK(c);
	struct pci_doe_task task = {
		.prot.vid = vendor,
		.prot.type = type,
		.request_pl = request,
		.request_pl_sz = request_sz,
		.response_pl = response,
		.response_pl_sz = response_sz,
		.complete = pci_doe_task_complete,
		.private = &c,
	};
	int rc;

	rc = pci_doe_submit_task(doe_mb, &task);
	if (rc)
		return rc;

	wait_for_completion(&c);

	return task.rv;
}
EXPORT_SYMBOL_GPL(pci_doe);

/**
 * pci_find_doe_mailbox() - Find Data Object Exchange mailbox
 *
 * @pdev: PCI device
 * @vendor: Vendor ID
 * @type: Data Object Type
 *
 * Find first DOE mailbox of a PCI device which supports the given protocol.
 *
 * RETURNS: Pointer to the DOE mailbox or NULL if none was found.
 */
struct pci_doe_mb *pci_find_doe_mailbox(struct pci_dev *pdev, u16 vendor,
					u8 type)
{
	struct pci_doe_mb *doe_mb;
	unsigned long index;

	xa_for_each(&pdev->doe_mbs, index, doe_mb)
		if (pci_doe_supports_prot(doe_mb, vendor, type))
			return doe_mb;

	return NULL;
}
EXPORT_SYMBOL_GPL(pci_find_doe_mailbox);

void pci_doe_init(struct pci_dev *pdev)
{
	struct pci_doe_mb *doe_mb;
	u16 offset = 0;
	int rc;

	xa_init(&pdev->doe_mbs);

	while ((offset = pci_find_next_ext_capability(pdev, offset,
						      PCI_EXT_CAP_ID_DOE))) {
		doe_mb = pci_doe_create_mb(pdev, offset);
		if (IS_ERR(doe_mb)) {
			pci_err(pdev, "[%x] failed to create mailbox: %ld\n",
				offset, PTR_ERR(doe_mb));
			continue;
		}

		rc = xa_insert(&pdev->doe_mbs, offset, doe_mb, GFP_KERNEL);
		if (rc) {
			pci_err(pdev, "[%x] failed to insert mailbox: %d\n",
				offset, rc);
			pci_doe_destroy_mb(doe_mb);
		}
	}
}

void pci_doe_destroy(struct pci_dev *pdev)
{
	struct pci_doe_mb *doe_mb;
	unsigned long index;

	xa_for_each(&pdev->doe_mbs, index, doe_mb)
		pci_doe_destroy_mb(doe_mb);

	xa_destroy(&pdev->doe_mbs);
}

void pci_doe_disconnected(struct pci_dev *pdev)
{
	struct pci_doe_mb *doe_mb;
	unsigned long index;

	xa_for_each(&pdev->doe_mbs, index, doe_mb)
		pci_doe_cancel_tasks(doe_mb);
}
