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
 * the helpers below after being created by devm_pci_doe_create()
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
	size_t length;
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
	length = 2 + task->request_pl_sz / sizeof(u32);
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
	for (i = 0; i < task->request_pl_sz / sizeof(u32); i++)
		pci_write_config_dword(pdev, offset + PCI_DOE_WRITE,
				       task->request_pl[i]);

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
	struct pci_dev *pdev = doe_mb->pdev;
	int offset = doe_mb->cap_offset;
	size_t length, payload_length;
	u32 val;
	int i;

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
	payload_length = min(length, task->response_pl_sz / sizeof(u32));
	/* Read the rest of the response payload */
	for (i = 0; i < payload_length; i++) {
		pci_read_config_dword(pdev, offset + PCI_DOE_READ,
				      &task->response_pl[i]);
		/* Prior to the last ack, ensure Data Object Ready */
		if (i == (payload_length - 1) && !pci_doe_data_obj_ready(doe_mb))
			return -EIO;
		pci_write_config_dword(pdev, offset + PCI_DOE_READ, 0);
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

	return min(length, task->response_pl_sz / sizeof(u32)) * sizeof(u32);
}

static void signal_task_complete(struct pci_doe_task *task, int rv)
{
	task->rv = rv;
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
	u32 response_pl;
	DECLARE_COMPLETION_ONSTACK(c);
	struct pci_doe_task task = {
		.prot.vid = PCI_VENDOR_ID_PCI_SIG,
		.prot.type = PCI_DOE_PROTOCOL_DISCOVERY,
		.request_pl = &request_pl,
		.request_pl_sz = sizeof(request_pl),
		.response_pl = &response_pl,
		.response_pl_sz = sizeof(response_pl),
		.complete = pci_doe_task_complete,
		.private = &c,
	};
	int rc;

	rc = pci_doe_submit_task(doe_mb, &task);
	if (rc < 0)
		return rc;

	wait_for_completion(&c);

	if (task.rv != sizeof(response_pl))
		return -EIO;

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

static void pci_doe_xa_destroy(void *mb)
{
	struct pci_doe_mb *doe_mb = mb;

	xa_destroy(&doe_mb->prots);
}

static void pci_doe_destroy_workqueue(void *mb)
{
	struct pci_doe_mb *doe_mb = mb;

	destroy_workqueue(doe_mb->work_queue);
}

static void pci_doe_flush_mb(void *mb)
{
	struct pci_doe_mb *doe_mb = mb;

	/* Stop all pending work items from starting */
	set_bit(PCI_DOE_FLAG_DEAD, &doe_mb->flags);

	/* Cancel an in progress work item, if necessary */
	set_bit(PCI_DOE_FLAG_CANCEL, &doe_mb->flags);
	wake_up(&doe_mb->wq);

	/* Flush all work items */
	flush_workqueue(doe_mb->work_queue);
}

/**
 * pcim_doe_create_mb() - Create a DOE mailbox object
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
struct pci_doe_mb *pcim_doe_create_mb(struct pci_dev *pdev, u16 cap_offset)
{
	struct pci_doe_mb *doe_mb;
	struct device *dev = &pdev->dev;
	int rc;

	doe_mb = devm_kzalloc(dev, sizeof(*doe_mb), GFP_KERNEL);
	if (!doe_mb)
		return ERR_PTR(-ENOMEM);

	doe_mb->pdev = pdev;
	doe_mb->cap_offset = cap_offset;
	init_waitqueue_head(&doe_mb->wq);

	xa_init(&doe_mb->prots);
	rc = devm_add_action(dev, pci_doe_xa_destroy, doe_mb);
	if (rc)
		return ERR_PTR(rc);

	doe_mb->work_queue = alloc_ordered_workqueue("%s %s DOE [%x]", 0,
						dev_driver_string(&pdev->dev),
						pci_name(pdev),
						doe_mb->cap_offset);
	if (!doe_mb->work_queue) {
		pci_err(pdev, "[%x] failed to allocate work queue\n",
			doe_mb->cap_offset);
		return ERR_PTR(-ENOMEM);
	}
	rc = devm_add_action_or_reset(dev, pci_doe_destroy_workqueue, doe_mb);
	if (rc)
		return ERR_PTR(rc);

	/* Reset the mailbox by issuing an abort */
	rc = pci_doe_abort(doe_mb);
	if (rc) {
		pci_err(pdev, "[%x] failed to reset mailbox with abort command : %d\n",
			doe_mb->cap_offset, rc);
		return ERR_PTR(rc);
	}

	/*
	 * The state machine and the mailbox should be in sync now;
	 * Set up mailbox flush prior to using the mailbox to query protocols.
	 */
	rc = devm_add_action_or_reset(dev, pci_doe_flush_mb, doe_mb);
	if (rc)
		return ERR_PTR(rc);

	rc = pci_doe_cache_protocols(doe_mb);
	if (rc) {
		pci_err(pdev, "[%x] failed to cache protocols : %d\n",
			doe_mb->cap_offset, rc);
		return ERR_PTR(rc);
	}

	return doe_mb;
}
EXPORT_SYMBOL_GPL(pcim_doe_create_mb);

/**
 * pci_doe_supports_prot() - Return if the DOE instance supports the given
 *			     protocol
 * @doe_mb: DOE mailbox capability to query
 * @vid: Protocol Vendor ID
 * @type: Protocol type
 *
 * RETURNS: True if the DOE mailbox supports the protocol specified
 */
bool pci_doe_supports_prot(struct pci_doe_mb *doe_mb, u16 vid, u8 type)
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
EXPORT_SYMBOL_GPL(pci_doe_supports_prot);

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
 * Excess data will be discarded.
 *
 * RETURNS: 0 when task has been successfully queued, -ERRNO on error
 */
int pci_doe_submit_task(struct pci_doe_mb *doe_mb, struct pci_doe_task *task)
{
	if (!pci_doe_supports_prot(doe_mb, task->prot.vid, task->prot.type))
		return -EINVAL;

	/*
	 * DOE requests must be a whole number of DW and the response needs to
	 * be big enough for at least 1 DW
	 */
	if (task->request_pl_sz % sizeof(u32) ||
	    task->response_pl_sz < sizeof(u32))
		return -EINVAL;

	if (test_bit(PCI_DOE_FLAG_DEAD, &doe_mb->flags))
		return -EIO;

	task->doe_mb = doe_mb;
	INIT_WORK(&task->work, doe_statemachine_work);
	queue_work(doe_mb->work_queue, &task->work);
	return 0;
}
EXPORT_SYMBOL_GPL(pci_doe_submit_task);
