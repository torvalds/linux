// SPDX-License-Identifier: GPL-2.0+
/*
 * IBM Power Systems Virtual Management Channel Support.
 *
 * Copyright (c) 2004, 2018 IBM Corp.
 *   Dave Engebretsen engebret@us.ibm.com
 *   Steven Royer seroyer@linux.vnet.ibm.com
 *   Adam Reznechek adreznec@linux.vnet.ibm.com
 *   Bryant G. Ly <bryantly@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/sched/signal.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/vio.h>

#include "ibmvmc.h"

#define IBMVMC_DRIVER_VERSION "1.0"

/*
 * Static global variables
 */
static DECLARE_WAIT_QUEUE_HEAD(ibmvmc_read_wait);

static const char ibmvmc_driver_name[] = "ibmvmc";

static struct ibmvmc_struct ibmvmc;
static struct ibmvmc_hmc hmcs[MAX_HMCS];
static struct crq_server_adapter ibmvmc_adapter;

static int ibmvmc_max_buf_pool_size = DEFAULT_BUF_POOL_SIZE;
static int ibmvmc_max_hmcs = DEFAULT_HMCS;
static int ibmvmc_max_mtu = DEFAULT_MTU;

static inline long h_copy_rdma(s64 length, u64 sliobn, u64 slioba,
			       u64 dliobn, u64 dlioba)
{
	long rc = 0;

	/* Ensure all writes to source memory are visible before hcall */
	dma_wmb();
	pr_debug("ibmvmc: h_copy_rdma(0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx\n",
		 length, sliobn, slioba, dliobn, dlioba);
	rc = plpar_hcall_norets(H_COPY_RDMA, length, sliobn, slioba,
				dliobn, dlioba);
	pr_debug("ibmvmc: h_copy_rdma rc = 0x%lx\n", rc);

	return rc;
}

static inline void h_free_crq(uint32_t unit_address)
{
	long rc = 0;

	do {
		if (H_IS_LONG_BUSY(rc))
			msleep(get_longbusy_msecs(rc));

		rc = plpar_hcall_norets(H_FREE_CRQ, unit_address);
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));
}

/**
 * h_request_vmc: - request a hypervisor virtual management channel device
 * @vmc_index: drc index of the vmc device created
 *
 * Requests the hypervisor create a new virtual management channel device,
 * allowing this partition to send hypervisor virtualization control
 * commands.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static inline long h_request_vmc(u32 *vmc_index)
{
	long rc = 0;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	do {
		if (H_IS_LONG_BUSY(rc))
			msleep(get_longbusy_msecs(rc));

		/* Call to request the VMC device from phyp */
		rc = plpar_hcall(H_REQUEST_VMC, retbuf);
		pr_debug("ibmvmc: %s rc = 0x%lx\n", __func__, rc);
		*vmc_index = retbuf[0];
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));

	return rc;
}

/* routines for managing a command/response queue */
/**
 * ibmvmc_handle_event: - Interrupt handler for crq events
 * @irq:        number of irq to handle, not used
 * @dev_instance: crq_server_adapter that received interrupt
 *
 * Disables interrupts and schedules ibmvmc_task
 *
 * Always returns IRQ_HANDLED
 */
static irqreturn_t ibmvmc_handle_event(int irq, void *dev_instance)
{
	struct crq_server_adapter *adapter =
		(struct crq_server_adapter *)dev_instance;

	vio_disable_interrupts(to_vio_dev(adapter->dev));
	tasklet_schedule(&adapter->work_task);

	return IRQ_HANDLED;
}

/**
 * ibmvmc_release_crq_queue - Release CRQ Queue
 *
 * @adapter:	crq_server_adapter struct
 *
 * Return:
 *	0 - Success
 *	Non-Zero - Failure
 */
static void ibmvmc_release_crq_queue(struct crq_server_adapter *adapter)
{
	struct vio_dev *vdev = to_vio_dev(adapter->dev);
	struct crq_queue *queue = &adapter->queue;

	free_irq(vdev->irq, (void *)adapter);
	tasklet_kill(&adapter->work_task);

	if (adapter->reset_task)
		kthread_stop(adapter->reset_task);

	h_free_crq(vdev->unit_address);
	dma_unmap_single(adapter->dev,
			 queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
	free_page((unsigned long)queue->msgs);
}

/**
 * ibmvmc_reset_crq_queue - Reset CRQ Queue
 *
 * @adapter:	crq_server_adapter struct
 *
 * This function calls h_free_crq and then calls H_REG_CRQ and does all the
 * bookkeeping to get us back to where we can communicate.
 *
 * Return:
 *	0 - Success
 *	Non-Zero - Failure
 */
static int ibmvmc_reset_crq_queue(struct crq_server_adapter *adapter)
{
	struct vio_dev *vdev = to_vio_dev(adapter->dev);
	struct crq_queue *queue = &adapter->queue;
	int rc = 0;

	/* Close the CRQ */
	h_free_crq(vdev->unit_address);

	/* Clean out the queue */
	memset(queue->msgs, 0x00, PAGE_SIZE);
	queue->cur = 0;

	/* And re-open it again */
	rc = plpar_hcall_norets(H_REG_CRQ,
				vdev->unit_address,
				queue->msg_token, PAGE_SIZE);
	if (rc == 2)
		/* Adapter is good, but other end is not ready */
		dev_warn(adapter->dev, "Partner adapter not ready\n");
	else if (rc != 0)
		dev_err(adapter->dev, "couldn't register crq--rc 0x%x\n", rc);

	return rc;
}

/**
 * crq_queue_next_crq: - Returns the next entry in message queue
 * @queue:      crq_queue to use
 *
 * Returns pointer to next entry in queue, or NULL if there are no new
 * entried in the CRQ.
 */
static struct ibmvmc_crq_msg *crq_queue_next_crq(struct crq_queue *queue)
{
	struct ibmvmc_crq_msg *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;

		/* Ensure the read of the valid bit occurs before reading any
		 * other bits of the CRQ entry
		 */
		dma_rmb();
	} else {
		crq = NULL;
	}

	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}

/**
 * ibmvmc_send_crq - Send CRQ
 *
 * @adapter:	crq_server_adapter struct
 * @word1:	Word1 Data field
 * @word2:	Word2 Data field
 *
 * Return:
 *	0 - Success
 *	Non-Zero - Failure
 */
static long ibmvmc_send_crq(struct crq_server_adapter *adapter,
			    u64 word1, u64 word2)
{
	struct vio_dev *vdev = to_vio_dev(adapter->dev);
	long rc = 0;

	dev_dbg(adapter->dev, "(0x%x, 0x%016llx, 0x%016llx)\n",
		vdev->unit_address, word1, word2);

	/*
	 * Ensure the command buffer is flushed to memory before handing it
	 * over to the other side to prevent it from fetching any stale data.
	 */
	dma_wmb();
	rc = plpar_hcall_norets(H_SEND_CRQ, vdev->unit_address, word1, word2);
	dev_dbg(adapter->dev, "rc = 0x%lx\n", rc);

	return rc;
}

/**
 * alloc_dma_buffer - Create DMA Buffer
 *
 * @vdev:	vio_dev struct
 * @size:	Size field
 * @dma_handle:	DMA address field
 *
 * Allocates memory for the command queue and maps remote memory into an
 * ioba.
 *
 * Returns a pointer to the buffer
 */
static void *alloc_dma_buffer(struct vio_dev *vdev, size_t size,
			      dma_addr_t *dma_handle)
{
	/* allocate memory */
	void *buffer = kzalloc(size, GFP_ATOMIC);

	if (!buffer) {
		*dma_handle = 0;
		return NULL;
	}

	/* DMA map */
	*dma_handle = dma_map_single(&vdev->dev, buffer, size,
				     DMA_BIDIRECTIONAL);

	if (dma_mapping_error(&vdev->dev, *dma_handle)) {
		*dma_handle = 0;
		kfree_sensitive(buffer);
		return NULL;
	}

	return buffer;
}

/**
 * free_dma_buffer - Free DMA Buffer
 *
 * @vdev:	vio_dev struct
 * @size:	Size field
 * @vaddr:	Address field
 * @dma_handle:	DMA address field
 *
 * Releases memory for a command queue and unmaps mapped remote memory.
 */
static void free_dma_buffer(struct vio_dev *vdev, size_t size, void *vaddr,
			    dma_addr_t dma_handle)
{
	/* DMA unmap */
	dma_unmap_single(&vdev->dev, dma_handle, size, DMA_BIDIRECTIONAL);

	/* deallocate memory */
	kfree_sensitive(vaddr);
}

/**
 * ibmvmc_get_valid_hmc_buffer - Retrieve Valid HMC Buffer
 *
 * @hmc_index:	HMC Index Field
 *
 * Return:
 *	Pointer to ibmvmc_buffer
 */
static struct ibmvmc_buffer *ibmvmc_get_valid_hmc_buffer(u8 hmc_index)
{
	struct ibmvmc_buffer *buffer;
	struct ibmvmc_buffer *ret_buf = NULL;
	unsigned long i;

	if (hmc_index > ibmvmc.max_hmc_index)
		return NULL;

	buffer = hmcs[hmc_index].buffer;

	for (i = 0; i < ibmvmc_max_buf_pool_size; i++) {
		if (buffer[i].valid && buffer[i].free &&
		    buffer[i].owner == VMC_BUF_OWNER_ALPHA) {
			buffer[i].free = 0;
			ret_buf = &buffer[i];
			break;
		}
	}

	return ret_buf;
}

/**
 * ibmvmc_get_free_hmc_buffer - Get Free HMC Buffer
 *
 * @adapter:	crq_server_adapter struct
 * @hmc_index:	Hmc Index field
 *
 * Return:
 *	Pointer to ibmvmc_buffer
 */
static struct ibmvmc_buffer *ibmvmc_get_free_hmc_buffer(struct crq_server_adapter *adapter,
							u8 hmc_index)
{
	struct ibmvmc_buffer *buffer;
	struct ibmvmc_buffer *ret_buf = NULL;
	unsigned long i;

	if (hmc_index > ibmvmc.max_hmc_index) {
		dev_info(adapter->dev, "get_free_hmc_buffer: invalid hmc_index=0x%x\n",
			 hmc_index);
		return NULL;
	}

	buffer = hmcs[hmc_index].buffer;

	for (i = 0; i < ibmvmc_max_buf_pool_size; i++) {
		if (buffer[i].free &&
		    buffer[i].owner == VMC_BUF_OWNER_ALPHA) {
			buffer[i].free = 0;
			ret_buf = &buffer[i];
			break;
		}
	}

	return ret_buf;
}

/**
 * ibmvmc_free_hmc_buffer - Free an HMC Buffer
 *
 * @hmc:	ibmvmc_hmc struct
 * @buffer:	ibmvmc_buffer struct
 *
 */
static void ibmvmc_free_hmc_buffer(struct ibmvmc_hmc *hmc,
				   struct ibmvmc_buffer *buffer)
{
	unsigned long flags;

	spin_lock_irqsave(&hmc->lock, flags);
	buffer->free = 1;
	spin_unlock_irqrestore(&hmc->lock, flags);
}

/**
 * ibmvmc_count_hmc_buffers - Count HMC Buffers
 *
 * @hmc_index:	HMC Index field
 * @valid:	Valid number of buffers field
 * @free:	Free number of buffers field
 *
 */
static void ibmvmc_count_hmc_buffers(u8 hmc_index, unsigned int *valid,
				     unsigned int *free)
{
	struct ibmvmc_buffer *buffer;
	unsigned long i;
	unsigned long flags;

	if (hmc_index > ibmvmc.max_hmc_index)
		return;

	if (!valid || !free)
		return;

	*valid = 0; *free = 0;

	buffer = hmcs[hmc_index].buffer;
	spin_lock_irqsave(&hmcs[hmc_index].lock, flags);

	for (i = 0; i < ibmvmc_max_buf_pool_size; i++) {
		if (buffer[i].valid) {
			*valid = *valid + 1;
			if (buffer[i].free)
				*free = *free + 1;
		}
	}

	spin_unlock_irqrestore(&hmcs[hmc_index].lock, flags);
}

/**
 * ibmvmc_get_free_hmc - Get Free HMC
 *
 * Return:
 *	Pointer to an available HMC Connection
 *	Null otherwise
 */
static struct ibmvmc_hmc *ibmvmc_get_free_hmc(void)
{
	unsigned long i;
	unsigned long flags;

	/*
	 * Find an available HMC connection.
	 */
	for (i = 0; i <= ibmvmc.max_hmc_index; i++) {
		spin_lock_irqsave(&hmcs[i].lock, flags);
		if (hmcs[i].state == ibmhmc_state_free) {
			hmcs[i].index = i;
			hmcs[i].state = ibmhmc_state_initial;
			spin_unlock_irqrestore(&hmcs[i].lock, flags);
			return &hmcs[i];
		}
		spin_unlock_irqrestore(&hmcs[i].lock, flags);
	}

	return NULL;
}

/**
 * ibmvmc_return_hmc - Return an HMC Connection
 *
 * @hmc:		ibmvmc_hmc struct
 * @release_readers:	Number of readers connected to session
 *
 * This function releases the HMC connections back into the pool.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_return_hmc(struct ibmvmc_hmc *hmc, bool release_readers)
{
	struct ibmvmc_buffer *buffer;
	struct crq_server_adapter *adapter;
	struct vio_dev *vdev;
	unsigned long i;
	unsigned long flags;

	if (!hmc || !hmc->adapter)
		return -EIO;

	if (release_readers) {
		if (hmc->file_session) {
			struct ibmvmc_file_session *session = hmc->file_session;

			session->valid = 0;
			wake_up_interruptible(&ibmvmc_read_wait);
		}
	}

	adapter = hmc->adapter;
	vdev = to_vio_dev(adapter->dev);

	spin_lock_irqsave(&hmc->lock, flags);
	hmc->index = 0;
	hmc->state = ibmhmc_state_free;
	hmc->queue_head = 0;
	hmc->queue_tail = 0;
	buffer = hmc->buffer;
	for (i = 0; i < ibmvmc_max_buf_pool_size; i++) {
		if (buffer[i].valid) {
			free_dma_buffer(vdev,
					ibmvmc.max_mtu,
					buffer[i].real_addr_local,
					buffer[i].dma_addr_local);
			dev_dbg(adapter->dev, "Forgot buffer id 0x%lx\n", i);
		}
		memset(&buffer[i], 0, sizeof(struct ibmvmc_buffer));

		hmc->queue_outbound_msgs[i] = VMC_INVALID_BUFFER_ID;
	}

	spin_unlock_irqrestore(&hmc->lock, flags);

	return 0;
}

/**
 * ibmvmc_send_open - Interface Open
 * @buffer: Pointer to ibmvmc_buffer struct
 * @hmc: Pointer to ibmvmc_hmc struct
 *
 * This command is sent by the management partition as the result of a
 * management partition device request. It causes the hypervisor to
 * prepare a set of data buffers for the management application connection
 * indicated HMC idx. A unique HMC Idx would be used if multiple management
 * applications running concurrently were desired. Before responding to this
 * command, the hypervisor must provide the management partition with at
 * least one of these new buffers via the Add Buffer. This indicates whether
 * the messages are inbound or outbound from the hypervisor.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_send_open(struct ibmvmc_buffer *buffer,
			    struct ibmvmc_hmc *hmc)
{
	struct ibmvmc_crq_msg crq_msg;
	struct crq_server_adapter *adapter;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;
	int rc = 0;

	if (!hmc || !hmc->adapter)
		return -EIO;

	adapter = hmc->adapter;

	dev_dbg(adapter->dev, "send_open: 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
		(unsigned long)buffer->size, (unsigned long)adapter->liobn,
		(unsigned long)buffer->dma_addr_local,
		(unsigned long)adapter->riobn,
		(unsigned long)buffer->dma_addr_remote);

	rc = h_copy_rdma(buffer->size,
			 adapter->liobn,
			 buffer->dma_addr_local,
			 adapter->riobn,
			 buffer->dma_addr_remote);
	if (rc) {
		dev_err(adapter->dev, "Error: In send_open, h_copy_rdma rc 0x%x\n",
			rc);
		return -EIO;
	}

	hmc->state = ibmhmc_state_opening;

	crq_msg.valid = 0x80;
	crq_msg.type = VMC_MSG_OPEN;
	crq_msg.status = 0;
	crq_msg.var1.rsvd = 0;
	crq_msg.hmc_session = hmc->session;
	crq_msg.hmc_index = hmc->index;
	crq_msg.var2.buffer_id = cpu_to_be16(buffer->id);
	crq_msg.rsvd = 0;
	crq_msg.var3.rsvd = 0;

	ibmvmc_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	return rc;
}

/**
 * ibmvmc_send_close - Interface Close
 * @hmc: Pointer to ibmvmc_hmc struct
 *
 * This command is sent by the management partition to terminate a
 * management application to hypervisor connection. When this command is
 * sent, the management partition has quiesced all I/O operations to all
 * buffers associated with this management application connection, and
 * has freed any storage for these buffers.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_send_close(struct ibmvmc_hmc *hmc)
{
	struct ibmvmc_crq_msg crq_msg;
	struct crq_server_adapter *adapter;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;
	int rc = 0;

	if (!hmc || !hmc->adapter)
		return -EIO;

	adapter = hmc->adapter;

	dev_info(adapter->dev, "CRQ send: close\n");

	crq_msg.valid = 0x80;
	crq_msg.type = VMC_MSG_CLOSE;
	crq_msg.status = 0;
	crq_msg.var1.rsvd = 0;
	crq_msg.hmc_session = hmc->session;
	crq_msg.hmc_index = hmc->index;
	crq_msg.var2.rsvd = 0;
	crq_msg.rsvd = 0;
	crq_msg.var3.rsvd = 0;

	ibmvmc_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	return rc;
}

/**
 * ibmvmc_send_capabilities - Send VMC Capabilities
 *
 * @adapter:	crq_server_adapter struct
 *
 * The capabilities message is an administrative message sent after the CRQ
 * initialization sequence of messages and is used to exchange VMC capabilities
 * between the management partition and the hypervisor. The management
 * partition must send this message and the hypervisor must respond with VMC
 * capabilities Response message before HMC interface message can begin. Any
 * HMC interface messages received before the exchange of capabilities has
 * complete are dropped.
 *
 * Return:
 *	0 - Success
 */
static int ibmvmc_send_capabilities(struct crq_server_adapter *adapter)
{
	struct ibmvmc_admin_crq_msg crq_msg;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;

	dev_dbg(adapter->dev, "ibmvmc: CRQ send: capabilities\n");
	crq_msg.valid = 0x80;
	crq_msg.type = VMC_MSG_CAP;
	crq_msg.status = 0;
	crq_msg.rsvd[0] = 0;
	crq_msg.rsvd[1] = 0;
	crq_msg.max_hmc = ibmvmc_max_hmcs;
	crq_msg.max_mtu = cpu_to_be32(ibmvmc_max_mtu);
	crq_msg.pool_size = cpu_to_be16(ibmvmc_max_buf_pool_size);
	crq_msg.crq_size = cpu_to_be16(adapter->queue.size);
	crq_msg.version = cpu_to_be16(IBMVMC_PROTOCOL_VERSION);

	ibmvmc_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	ibmvmc.state = ibmvmc_state_capabilities;

	return 0;
}

/**
 * ibmvmc_send_add_buffer_resp - Add Buffer Response
 *
 * @adapter:	crq_server_adapter struct
 * @status:	Status field
 * @hmc_session: HMC Session field
 * @hmc_index:	HMC Index field
 * @buffer_id:	Buffer Id field
 *
 * This command is sent by the management partition to the hypervisor in
 * response to the Add Buffer message. The Status field indicates the result of
 * the command.
 *
 * Return:
 *	0 - Success
 */
static int ibmvmc_send_add_buffer_resp(struct crq_server_adapter *adapter,
				       u8 status, u8 hmc_session,
				       u8 hmc_index, u16 buffer_id)
{
	struct ibmvmc_crq_msg crq_msg;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;

	dev_dbg(adapter->dev, "CRQ send: add_buffer_resp\n");
	crq_msg.valid = 0x80;
	crq_msg.type = VMC_MSG_ADD_BUF_RESP;
	crq_msg.status = status;
	crq_msg.var1.rsvd = 0;
	crq_msg.hmc_session = hmc_session;
	crq_msg.hmc_index = hmc_index;
	crq_msg.var2.buffer_id = cpu_to_be16(buffer_id);
	crq_msg.rsvd = 0;
	crq_msg.var3.rsvd = 0;

	ibmvmc_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	return 0;
}

/**
 * ibmvmc_send_rem_buffer_resp - Remove Buffer Response
 *
 * @adapter:	crq_server_adapter struct
 * @status:	Status field
 * @hmc_session: HMC Session field
 * @hmc_index:	HMC Index field
 * @buffer_id:	Buffer Id field
 *
 * This command is sent by the management partition to the hypervisor in
 * response to the Remove Buffer message. The Buffer ID field indicates
 * which buffer the management partition selected to remove. The Status
 * field indicates the result of the command.
 *
 * Return:
 *	0 - Success
 */
static int ibmvmc_send_rem_buffer_resp(struct crq_server_adapter *adapter,
				       u8 status, u8 hmc_session,
				       u8 hmc_index, u16 buffer_id)
{
	struct ibmvmc_crq_msg crq_msg;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;

	dev_dbg(adapter->dev, "CRQ send: rem_buffer_resp\n");
	crq_msg.valid = 0x80;
	crq_msg.type = VMC_MSG_REM_BUF_RESP;
	crq_msg.status = status;
	crq_msg.var1.rsvd = 0;
	crq_msg.hmc_session = hmc_session;
	crq_msg.hmc_index = hmc_index;
	crq_msg.var2.buffer_id = cpu_to_be16(buffer_id);
	crq_msg.rsvd = 0;
	crq_msg.var3.rsvd = 0;

	ibmvmc_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	return 0;
}

/**
 * ibmvmc_send_msg - Signal Message
 *
 * @adapter:	crq_server_adapter struct
 * @buffer:	ibmvmc_buffer struct
 * @hmc:	ibmvmc_hmc struct
 * @msg_len:	message length field
 *
 * This command is sent between the management partition and the hypervisor
 * in order to signal the arrival of an HMC protocol message. The command
 * can be sent by both the management partition and the hypervisor. It is
 * used for all traffic between the management application and the hypervisor,
 * regardless of who initiated the communication.
 *
 * There is no response to this message.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_send_msg(struct crq_server_adapter *adapter,
			   struct ibmvmc_buffer *buffer,
			   struct ibmvmc_hmc *hmc, int msg_len)
{
	struct ibmvmc_crq_msg crq_msg;
	__be64 *crq_as_u64 = (__be64 *)&crq_msg;
	int rc = 0;

	dev_dbg(adapter->dev, "CRQ send: rdma to HV\n");
	rc = h_copy_rdma(msg_len,
			 adapter->liobn,
			 buffer->dma_addr_local,
			 adapter->riobn,
			 buffer->dma_addr_remote);
	if (rc) {
		dev_err(adapter->dev, "Error in send_msg, h_copy_rdma rc 0x%x\n",
			rc);
		return rc;
	}

	crq_msg.valid = 0x80;
	crq_msg.type = VMC_MSG_SIGNAL;
	crq_msg.status = 0;
	crq_msg.var1.rsvd = 0;
	crq_msg.hmc_session = hmc->session;
	crq_msg.hmc_index = hmc->index;
	crq_msg.var2.buffer_id = cpu_to_be16(buffer->id);
	crq_msg.var3.msg_len = cpu_to_be32(msg_len);
	dev_dbg(adapter->dev, "CRQ send: msg to HV 0x%llx 0x%llx\n",
		be64_to_cpu(crq_as_u64[0]), be64_to_cpu(crq_as_u64[1]));

	buffer->owner = VMC_BUF_OWNER_HV;
	ibmvmc_send_crq(adapter, be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

	return rc;
}

/**
 * ibmvmc_open - Open Session
 *
 * @inode:	inode struct
 * @file:	file struct
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_open(struct inode *inode, struct file *file)
{
	struct ibmvmc_file_session *session;

	pr_debug("%s: inode = 0x%lx, file = 0x%lx, state = 0x%x\n", __func__,
		 (unsigned long)inode, (unsigned long)file,
		 ibmvmc.state);

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->file = file;
	file->private_data = session;

	return 0;
}

/**
 * ibmvmc_close - Close Session
 *
 * @inode:	inode struct
 * @file:	file struct
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_close(struct inode *inode, struct file *file)
{
	struct ibmvmc_file_session *session;
	struct ibmvmc_hmc *hmc;
	int rc = 0;
	unsigned long flags;

	pr_debug("%s: file = 0x%lx, state = 0x%x\n", __func__,
		 (unsigned long)file, ibmvmc.state);

	session = file->private_data;
	if (!session)
		return -EIO;

	hmc = session->hmc;
	if (hmc) {
		if (!hmc->adapter)
			return -EIO;

		if (ibmvmc.state == ibmvmc_state_failed) {
			dev_warn(hmc->adapter->dev, "close: state_failed\n");
			return -EIO;
		}

		spin_lock_irqsave(&hmc->lock, flags);
		if (hmc->state >= ibmhmc_state_opening) {
			rc = ibmvmc_send_close(hmc);
			if (rc)
				dev_warn(hmc->adapter->dev, "close: send_close failed.\n");
		}
		spin_unlock_irqrestore(&hmc->lock, flags);
	}

	kfree_sensitive(session);

	return rc;
}

/**
 * ibmvmc_read - Read
 *
 * @file:	file struct
 * @buf:	Character buffer
 * @nbytes:	Size in bytes
 * @ppos:	Offset
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static ssize_t ibmvmc_read(struct file *file, char *buf, size_t nbytes,
			   loff_t *ppos)
{
	struct ibmvmc_file_session *session;
	struct ibmvmc_hmc *hmc;
	struct crq_server_adapter *adapter;
	struct ibmvmc_buffer *buffer;
	ssize_t n;
	ssize_t retval = 0;
	unsigned long flags;
	DEFINE_WAIT(wait);

	pr_debug("ibmvmc: read: file = 0x%lx, buf = 0x%lx, nbytes = 0x%lx\n",
		 (unsigned long)file, (unsigned long)buf,
		 (unsigned long)nbytes);

	if (nbytes == 0)
		return 0;

	if (nbytes > ibmvmc.max_mtu) {
		pr_warn("ibmvmc: read: nbytes invalid 0x%x\n",
			(unsigned int)nbytes);
		return -EINVAL;
	}

	session = file->private_data;
	if (!session) {
		pr_warn("ibmvmc: read: no session\n");
		return -EIO;
	}

	hmc = session->hmc;
	if (!hmc) {
		pr_warn("ibmvmc: read: no hmc\n");
		return -EIO;
	}

	adapter = hmc->adapter;
	if (!adapter) {
		pr_warn("ibmvmc: read: no adapter\n");
		return -EIO;
	}

	do {
		prepare_to_wait(&ibmvmc_read_wait, &wait, TASK_INTERRUPTIBLE);

		spin_lock_irqsave(&hmc->lock, flags);
		if (hmc->queue_tail != hmc->queue_head)
			/* Data is available */
			break;

		spin_unlock_irqrestore(&hmc->lock, flags);

		if (!session->valid) {
			retval = -EBADFD;
			goto out;
		}
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}

		schedule();

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
	} while (1);

	buffer = &(hmc->buffer[hmc->queue_outbound_msgs[hmc->queue_tail]]);
	hmc->queue_tail++;
	if (hmc->queue_tail == ibmvmc_max_buf_pool_size)
		hmc->queue_tail = 0;
	spin_unlock_irqrestore(&hmc->lock, flags);

	nbytes = min_t(size_t, nbytes, buffer->msg_len);
	n = copy_to_user((void *)buf, buffer->real_addr_local, nbytes);
	dev_dbg(adapter->dev, "read: copy to user nbytes = 0x%lx.\n", nbytes);
	ibmvmc_free_hmc_buffer(hmc, buffer);
	retval = nbytes;

	if (n) {
		dev_warn(adapter->dev, "read: copy to user failed.\n");
		retval = -EFAULT;
	}

 out:
	finish_wait(&ibmvmc_read_wait, &wait);
	dev_dbg(adapter->dev, "read: out %ld\n", retval);
	return retval;
}

/**
 * ibmvmc_poll - Poll
 *
 * @file:	file struct
 * @wait:	Poll Table
 *
 * Return:
 *	poll.h return values
 */
static unsigned int ibmvmc_poll(struct file *file, poll_table *wait)
{
	struct ibmvmc_file_session *session;
	struct ibmvmc_hmc *hmc;
	unsigned int mask = 0;

	session = file->private_data;
	if (!session)
		return 0;

	hmc = session->hmc;
	if (!hmc)
		return 0;

	poll_wait(file, &ibmvmc_read_wait, wait);

	if (hmc->queue_head != hmc->queue_tail)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

/**
 * ibmvmc_write - Write
 *
 * @file:	file struct
 * @buffer:	Character buffer
 * @count:	Count field
 * @ppos:	Offset
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static ssize_t ibmvmc_write(struct file *file, const char *buffer,
			    size_t count, loff_t *ppos)
{
	struct ibmvmc_buffer *vmc_buffer;
	struct ibmvmc_file_session *session;
	struct crq_server_adapter *adapter;
	struct ibmvmc_hmc *hmc;
	unsigned char *buf;
	unsigned long flags;
	size_t bytes;
	const char *p = buffer;
	size_t c = count;
	int ret = 0;

	session = file->private_data;
	if (!session)
		return -EIO;

	hmc = session->hmc;
	if (!hmc)
		return -EIO;

	spin_lock_irqsave(&hmc->lock, flags);
	if (hmc->state == ibmhmc_state_free) {
		/* HMC connection is not valid (possibly was reset under us). */
		ret = -EIO;
		goto out;
	}

	adapter = hmc->adapter;
	if (!adapter) {
		ret = -EIO;
		goto out;
	}

	if (count > ibmvmc.max_mtu) {
		dev_warn(adapter->dev, "invalid buffer size 0x%lx\n",
			 (unsigned long)count);
		ret = -EIO;
		goto out;
	}

	/* Waiting for the open resp message to the ioctl(1) - retry */
	if (hmc->state == ibmhmc_state_opening) {
		ret = -EBUSY;
		goto out;
	}

	/* Make sure the ioctl() was called & the open msg sent, and that
	 * the HMC connection has not failed.
	 */
	if (hmc->state != ibmhmc_state_ready) {
		ret = -EIO;
		goto out;
	}

	vmc_buffer = ibmvmc_get_valid_hmc_buffer(hmc->index);
	if (!vmc_buffer) {
		/* No buffer available for the msg send, or we have not yet
		 * completed the open/open_resp sequence.  Retry until this is
		 * complete.
		 */
		ret = -EBUSY;
		goto out;
	}
	if (!vmc_buffer->real_addr_local) {
		dev_err(adapter->dev, "no buffer storage assigned\n");
		ret = -EIO;
		goto out;
	}
	buf = vmc_buffer->real_addr_local;

	while (c > 0) {
		bytes = min_t(size_t, c, vmc_buffer->size);

		bytes -= copy_from_user(buf, p, bytes);
		if (!bytes) {
			ret = -EFAULT;
			goto out;
		}
		c -= bytes;
		p += bytes;
	}
	if (p == buffer)
		goto out;

	file->f_path.dentry->d_inode->i_mtime = current_time(file_inode(file));
	mark_inode_dirty(file->f_path.dentry->d_inode);

	dev_dbg(adapter->dev, "write: file = 0x%lx, count = 0x%lx\n",
		(unsigned long)file, (unsigned long)count);

	ibmvmc_send_msg(adapter, vmc_buffer, hmc, count);
	ret = p - buffer;
 out:
	spin_unlock_irqrestore(&hmc->lock, flags);
	return (ssize_t)(ret);
}

/**
 * ibmvmc_setup_hmc - Setup the HMC
 *
 * @session:	ibmvmc_file_session struct
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static long ibmvmc_setup_hmc(struct ibmvmc_file_session *session)
{
	struct ibmvmc_hmc *hmc;
	unsigned int valid, free, index;

	if (ibmvmc.state == ibmvmc_state_failed) {
		pr_warn("ibmvmc: Reserve HMC: state_failed\n");
		return -EIO;
	}

	if (ibmvmc.state < ibmvmc_state_ready) {
		pr_warn("ibmvmc: Reserve HMC: not state_ready\n");
		return -EAGAIN;
	}

	/* Device is busy until capabilities have been exchanged and we
	 * have a generic buffer for each possible HMC connection.
	 */
	for (index = 0; index <= ibmvmc.max_hmc_index; index++) {
		valid = 0;
		ibmvmc_count_hmc_buffers(index, &valid, &free);
		if (valid == 0) {
			pr_warn("ibmvmc: buffers not ready for index %d\n",
				index);
			return -ENOBUFS;
		}
	}

	/* Get an hmc object, and transition to ibmhmc_state_initial */
	hmc = ibmvmc_get_free_hmc();
	if (!hmc) {
		pr_warn("%s: free hmc not found\n", __func__);
		return -EBUSY;
	}

	hmc->session = hmc->session + 1;
	if (hmc->session == 0xff)
		hmc->session = 1;

	session->hmc = hmc;
	hmc->adapter = &ibmvmc_adapter;
	hmc->file_session = session;
	session->valid = 1;

	return 0;
}

/**
 * ibmvmc_ioctl_sethmcid - IOCTL Set HMC ID
 *
 * @session:	ibmvmc_file_session struct
 * @new_hmc_id:	HMC id field
 *
 * IOCTL command to setup the hmc id
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static long ibmvmc_ioctl_sethmcid(struct ibmvmc_file_session *session,
				  unsigned char __user *new_hmc_id)
{
	struct ibmvmc_hmc *hmc;
	struct ibmvmc_buffer *buffer;
	size_t bytes;
	char print_buffer[HMC_ID_LEN + 1];
	unsigned long flags;
	long rc = 0;

	/* Reserve HMC session */
	hmc = session->hmc;
	if (!hmc) {
		rc = ibmvmc_setup_hmc(session);
		if (rc)
			return rc;

		hmc = session->hmc;
		if (!hmc) {
			pr_err("ibmvmc: setup_hmc success but no hmc\n");
			return -EIO;
		}
	}

	if (hmc->state != ibmhmc_state_initial) {
		pr_warn("ibmvmc: sethmcid: invalid state to send open 0x%x\n",
			hmc->state);
		return -EIO;
	}

	bytes = copy_from_user(hmc->hmc_id, new_hmc_id, HMC_ID_LEN);
	if (bytes)
		return -EFAULT;

	/* Send Open Session command */
	spin_lock_irqsave(&hmc->lock, flags);
	buffer = ibmvmc_get_valid_hmc_buffer(hmc->index);
	spin_unlock_irqrestore(&hmc->lock, flags);

	if (!buffer || !buffer->real_addr_local) {
		pr_warn("ibmvmc: sethmcid: no buffer available\n");
		return -EIO;
	}

	/* Make sure buffer is NULL terminated before trying to print it */
	memset(print_buffer, 0, HMC_ID_LEN + 1);
	strncpy(print_buffer, hmc->hmc_id, HMC_ID_LEN);
	pr_info("ibmvmc: sethmcid: Set HMC ID: \"%s\"\n", print_buffer);

	memcpy(buffer->real_addr_local, hmc->hmc_id, HMC_ID_LEN);
	/* RDMA over ID, send open msg, change state to ibmhmc_state_opening */
	rc = ibmvmc_send_open(buffer, hmc);

	return rc;
}

/**
 * ibmvmc_ioctl_query - IOCTL Query
 *
 * @session:	ibmvmc_file_session struct
 * @ret_struct:	ibmvmc_query_struct
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static long ibmvmc_ioctl_query(struct ibmvmc_file_session *session,
			       struct ibmvmc_query_struct __user *ret_struct)
{
	struct ibmvmc_query_struct query_struct;
	size_t bytes;

	memset(&query_struct, 0, sizeof(query_struct));
	query_struct.have_vmc = (ibmvmc.state > ibmvmc_state_initial);
	query_struct.state = ibmvmc.state;
	query_struct.vmc_drc_index = ibmvmc.vmc_drc_index;

	bytes = copy_to_user(ret_struct, &query_struct,
			     sizeof(query_struct));
	if (bytes)
		return -EFAULT;

	return 0;
}

/**
 * ibmvmc_ioctl_requestvmc - IOCTL Request VMC
 *
 * @session:	ibmvmc_file_session struct
 * @ret_vmc_index:	VMC Index
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static long ibmvmc_ioctl_requestvmc(struct ibmvmc_file_session *session,
				    u32 __user *ret_vmc_index)
{
	/* TODO: (adreznec) Add locking to control multiple process access */
	size_t bytes;
	long rc;
	u32 vmc_drc_index;

	/* Call to request the VMC device from phyp*/
	rc = h_request_vmc(&vmc_drc_index);
	pr_debug("ibmvmc: requestvmc: H_REQUEST_VMC rc = 0x%lx\n", rc);

	if (rc == H_SUCCESS) {
		rc = 0;
	} else if (rc == H_FUNCTION) {
		pr_err("ibmvmc: requestvmc: h_request_vmc not supported\n");
		return -EPERM;
	} else if (rc == H_AUTHORITY) {
		pr_err("ibmvmc: requestvmc: hypervisor denied vmc request\n");
		return -EPERM;
	} else if (rc == H_HARDWARE) {
		pr_err("ibmvmc: requestvmc: hypervisor hardware fault\n");
		return -EIO;
	} else if (rc == H_RESOURCE) {
		pr_err("ibmvmc: requestvmc: vmc resource unavailable\n");
		return -ENODEV;
	} else if (rc == H_NOT_AVAILABLE) {
		pr_err("ibmvmc: requestvmc: system cannot be vmc managed\n");
		return -EPERM;
	} else if (rc == H_PARAMETER) {
		pr_err("ibmvmc: requestvmc: invalid parameter\n");
		return -EINVAL;
	}

	/* Success, set the vmc index in global struct */
	ibmvmc.vmc_drc_index = vmc_drc_index;

	bytes = copy_to_user(ret_vmc_index, &vmc_drc_index,
			     sizeof(*ret_vmc_index));
	if (bytes) {
		pr_warn("ibmvmc: requestvmc: copy to user failed.\n");
		return -EFAULT;
	}
	return rc;
}

/**
 * ibmvmc_ioctl - IOCTL
 *
 * @file:	file information
 * @cmd:	cmd field
 * @arg:	Argument field
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static long ibmvmc_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct ibmvmc_file_session *session = file->private_data;

	pr_debug("ibmvmc: ioctl file=0x%lx, cmd=0x%x, arg=0x%lx, ses=0x%lx\n",
		 (unsigned long)file, cmd, arg,
		 (unsigned long)session);

	if (!session) {
		pr_warn("ibmvmc: ioctl: no session\n");
		return -EIO;
	}

	switch (cmd) {
	case VMC_IOCTL_SETHMCID:
		return ibmvmc_ioctl_sethmcid(session,
			(unsigned char __user *)arg);
	case VMC_IOCTL_QUERY:
		return ibmvmc_ioctl_query(session,
			(struct ibmvmc_query_struct __user *)arg);
	case VMC_IOCTL_REQUESTVMC:
		return ibmvmc_ioctl_requestvmc(session,
			(unsigned int __user *)arg);
	default:
		pr_warn("ibmvmc: unknown ioctl 0x%x\n", cmd);
		return -EINVAL;
	}
}

static const struct file_operations ibmvmc_fops = {
	.owner		= THIS_MODULE,
	.read		= ibmvmc_read,
	.write		= ibmvmc_write,
	.poll		= ibmvmc_poll,
	.unlocked_ioctl	= ibmvmc_ioctl,
	.open           = ibmvmc_open,
	.release        = ibmvmc_close,
};

/**
 * ibmvmc_add_buffer - Add Buffer
 *
 * @adapter: crq_server_adapter struct
 * @crq:	ibmvmc_crq_msg struct
 *
 * This message transfers a buffer from hypervisor ownership to management
 * partition ownership. The LIOBA is obtained from the virtual TCE table
 * associated with the hypervisor side of the VMC device, and points to a
 * buffer of size MTU (as established in the capabilities exchange).
 *
 * Typical flow for ading buffers:
 * 1. A new management application connection is opened by the management
 *	partition.
 * 2. The hypervisor assigns new buffers for the traffic associated with
 *	that connection.
 * 3. The hypervisor sends VMC Add Buffer messages to the management
 *	partition, informing it of the new buffers.
 * 4. The hypervisor sends an HMC protocol message (to the management
 *	application) notifying it of the new buffers. This informs the
 *	application that it has buffers available for sending HMC
 *	commands.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_add_buffer(struct crq_server_adapter *adapter,
			     struct ibmvmc_crq_msg *crq)
{
	struct ibmvmc_buffer *buffer;
	u8 hmc_index;
	u8 hmc_session;
	u16 buffer_id;
	unsigned long flags;
	int rc = 0;

	if (!crq)
		return -1;

	hmc_session = crq->hmc_session;
	hmc_index = crq->hmc_index;
	buffer_id = be16_to_cpu(crq->var2.buffer_id);

	if (hmc_index > ibmvmc.max_hmc_index) {
		dev_err(adapter->dev, "add_buffer: invalid hmc_index = 0x%x\n",
			hmc_index);
		ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_INVALID_HMC_INDEX,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	if (buffer_id >= ibmvmc.max_buffer_pool_size) {
		dev_err(adapter->dev, "add_buffer: invalid buffer_id = 0x%x\n",
			buffer_id);
		ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_INVALID_BUFFER_ID,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	spin_lock_irqsave(&hmcs[hmc_index].lock, flags);
	buffer = &hmcs[hmc_index].buffer[buffer_id];

	if (buffer->real_addr_local || buffer->dma_addr_local) {
		dev_warn(adapter->dev, "add_buffer: already allocated id = 0x%lx\n",
			 (unsigned long)buffer_id);
		spin_unlock_irqrestore(&hmcs[hmc_index].lock, flags);
		ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_INVALID_BUFFER_ID,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	buffer->real_addr_local = alloc_dma_buffer(to_vio_dev(adapter->dev),
						   ibmvmc.max_mtu,
						   &buffer->dma_addr_local);

	if (!buffer->real_addr_local) {
		dev_err(adapter->dev, "add_buffer: alloc_dma_buffer failed.\n");
		spin_unlock_irqrestore(&hmcs[hmc_index].lock, flags);
		ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_INTERFACE_FAILURE,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	buffer->dma_addr_remote = be32_to_cpu(crq->var3.lioba);
	buffer->size = ibmvmc.max_mtu;
	buffer->owner = crq->var1.owner;
	buffer->free = 1;
	/* Must ensure valid==1 is observable only after all other fields are */
	dma_wmb();
	buffer->valid = 1;
	buffer->id = buffer_id;

	dev_dbg(adapter->dev, "add_buffer: successfully added a buffer:\n");
	dev_dbg(adapter->dev, "   index: %d, session: %d, buffer: 0x%x, owner: %d\n",
		hmc_index, hmc_session, buffer_id, buffer->owner);
	dev_dbg(adapter->dev, "   local: 0x%x, remote: 0x%x\n",
		(u32)buffer->dma_addr_local,
		(u32)buffer->dma_addr_remote);
	spin_unlock_irqrestore(&hmcs[hmc_index].lock, flags);

	ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_SUCCESS, hmc_session,
				    hmc_index, buffer_id);

	return rc;
}

/**
 * ibmvmc_rem_buffer - Remove Buffer
 *
 * @adapter: crq_server_adapter struct
 * @crq:	ibmvmc_crq_msg struct
 *
 * This message requests an HMC buffer to be transferred from management
 * partition ownership to hypervisor ownership. The management partition may
 * not be able to satisfy the request at a particular point in time if all its
 * buffers are in use. The management partition requires a depth of at least
 * one inbound buffer to allow management application commands to flow to the
 * hypervisor. It is, therefore, an interface error for the hypervisor to
 * attempt to remove the management partition's last buffer.
 *
 * The hypervisor is expected to manage buffer usage with the management
 * application directly and inform the management partition when buffers may be
 * removed. The typical flow for removing buffers:
 *
 * 1. The management application no longer needs a communication path to a
 *	particular hypervisor function. That function is closed.
 * 2. The hypervisor and the management application quiesce all traffic to that
 *	function. The hypervisor requests a reduction in buffer pool size.
 * 3. The management application acknowledges the reduction in buffer pool size.
 * 4. The hypervisor sends a Remove Buffer message to the management partition,
 *	informing it of the reduction in buffers.
 * 5. The management partition verifies it can remove the buffer. This is
 *	possible if buffers have been quiesced.
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
/*
 * The hypervisor requested that we pick an unused buffer, and return it.
 * Before sending the buffer back, we free any storage associated with the
 * buffer.
 */
static int ibmvmc_rem_buffer(struct crq_server_adapter *adapter,
			     struct ibmvmc_crq_msg *crq)
{
	struct ibmvmc_buffer *buffer;
	u8 hmc_index;
	u8 hmc_session;
	u16 buffer_id = 0;
	unsigned long flags;
	int rc = 0;

	if (!crq)
		return -1;

	hmc_session = crq->hmc_session;
	hmc_index = crq->hmc_index;

	if (hmc_index > ibmvmc.max_hmc_index) {
		dev_warn(adapter->dev, "rem_buffer: invalid hmc_index = 0x%x\n",
			 hmc_index);
		ibmvmc_send_rem_buffer_resp(adapter, VMC_MSG_INVALID_HMC_INDEX,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	spin_lock_irqsave(&hmcs[hmc_index].lock, flags);
	buffer = ibmvmc_get_free_hmc_buffer(adapter, hmc_index);
	if (!buffer) {
		dev_info(adapter->dev, "rem_buffer: no buffer to remove\n");
		spin_unlock_irqrestore(&hmcs[hmc_index].lock, flags);
		ibmvmc_send_rem_buffer_resp(adapter, VMC_MSG_NO_BUFFER,
					    hmc_session, hmc_index,
					    VMC_INVALID_BUFFER_ID);
		return -1;
	}

	buffer_id = buffer->id;

	if (buffer->valid)
		free_dma_buffer(to_vio_dev(adapter->dev),
				ibmvmc.max_mtu,
				buffer->real_addr_local,
				buffer->dma_addr_local);

	memset(buffer, 0, sizeof(struct ibmvmc_buffer));
	spin_unlock_irqrestore(&hmcs[hmc_index].lock, flags);

	dev_dbg(adapter->dev, "rem_buffer: removed buffer 0x%x.\n", buffer_id);
	ibmvmc_send_rem_buffer_resp(adapter, VMC_MSG_SUCCESS, hmc_session,
				    hmc_index, buffer_id);

	return rc;
}

static int ibmvmc_recv_msg(struct crq_server_adapter *adapter,
			   struct ibmvmc_crq_msg *crq)
{
	struct ibmvmc_buffer *buffer;
	struct ibmvmc_hmc *hmc;
	unsigned long msg_len;
	u8 hmc_index;
	u8 hmc_session;
	u16 buffer_id;
	unsigned long flags;
	int rc = 0;

	if (!crq)
		return -1;

	/* Hypervisor writes CRQs directly into our memory in big endian */
	dev_dbg(adapter->dev, "Recv_msg: msg from HV 0x%016llx 0x%016llx\n",
		be64_to_cpu(*((unsigned long *)crq)),
		be64_to_cpu(*(((unsigned long *)crq) + 1)));

	hmc_session = crq->hmc_session;
	hmc_index = crq->hmc_index;
	buffer_id = be16_to_cpu(crq->var2.buffer_id);
	msg_len = be32_to_cpu(crq->var3.msg_len);

	if (hmc_index > ibmvmc.max_hmc_index) {
		dev_err(adapter->dev, "Recv_msg: invalid hmc_index = 0x%x\n",
			hmc_index);
		ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_INVALID_HMC_INDEX,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	if (buffer_id >= ibmvmc.max_buffer_pool_size) {
		dev_err(adapter->dev, "Recv_msg: invalid buffer_id = 0x%x\n",
			buffer_id);
		ibmvmc_send_add_buffer_resp(adapter, VMC_MSG_INVALID_BUFFER_ID,
					    hmc_session, hmc_index, buffer_id);
		return -1;
	}

	hmc = &hmcs[hmc_index];
	spin_lock_irqsave(&hmc->lock, flags);

	if (hmc->state == ibmhmc_state_free) {
		dev_err(adapter->dev, "Recv_msg: invalid hmc state = 0x%x\n",
			hmc->state);
		/* HMC connection is not valid (possibly was reset under us). */
		spin_unlock_irqrestore(&hmc->lock, flags);
		return -1;
	}

	buffer = &hmc->buffer[buffer_id];

	if (buffer->valid == 0 || buffer->owner == VMC_BUF_OWNER_ALPHA) {
		dev_err(adapter->dev, "Recv_msg: not valid, or not HV.  0x%x 0x%x\n",
			buffer->valid, buffer->owner);
		spin_unlock_irqrestore(&hmc->lock, flags);
		return -1;
	}

	/* RDMA the data into the partition. */
	rc = h_copy_rdma(msg_len,
			 adapter->riobn,
			 buffer->dma_addr_remote,
			 adapter->liobn,
			 buffer->dma_addr_local);

	dev_dbg(adapter->dev, "Recv_msg: msg_len = 0x%x, buffer_id = 0x%x, queue_head = 0x%x, hmc_idx = 0x%x\n",
		(unsigned int)msg_len, (unsigned int)buffer_id,
		(unsigned int)hmc->queue_head, (unsigned int)hmc_index);
	buffer->msg_len = msg_len;
	buffer->free = 0;
	buffer->owner = VMC_BUF_OWNER_ALPHA;

	if (rc) {
		dev_err(adapter->dev, "Failure in recv_msg: h_copy_rdma = 0x%x\n",
			rc);
		spin_unlock_irqrestore(&hmc->lock, flags);
		return -1;
	}

	/* Must be locked because read operates on the same data */
	hmc->queue_outbound_msgs[hmc->queue_head] = buffer_id;
	hmc->queue_head++;
	if (hmc->queue_head == ibmvmc_max_buf_pool_size)
		hmc->queue_head = 0;

	if (hmc->queue_head == hmc->queue_tail)
		dev_err(adapter->dev, "outbound buffer queue wrapped.\n");

	spin_unlock_irqrestore(&hmc->lock, flags);

	wake_up_interruptible(&ibmvmc_read_wait);

	return 0;
}

/**
 * ibmvmc_process_capabilities - Process Capabilities
 *
 * @adapter:	crq_server_adapter struct
 * @crqp:	ibmvmc_crq_msg struct
 *
 */
static void ibmvmc_process_capabilities(struct crq_server_adapter *adapter,
					struct ibmvmc_crq_msg *crqp)
{
	struct ibmvmc_admin_crq_msg *crq = (struct ibmvmc_admin_crq_msg *)crqp;

	if ((be16_to_cpu(crq->version) >> 8) !=
			(IBMVMC_PROTOCOL_VERSION >> 8)) {
		dev_err(adapter->dev, "init failed, incompatible versions 0x%x 0x%x\n",
			be16_to_cpu(crq->version),
			IBMVMC_PROTOCOL_VERSION);
		ibmvmc.state = ibmvmc_state_failed;
		return;
	}

	ibmvmc.max_mtu = min_t(u32, ibmvmc_max_mtu, be32_to_cpu(crq->max_mtu));
	ibmvmc.max_buffer_pool_size = min_t(u16, ibmvmc_max_buf_pool_size,
					    be16_to_cpu(crq->pool_size));
	ibmvmc.max_hmc_index = min_t(u8, ibmvmc_max_hmcs, crq->max_hmc) - 1;
	ibmvmc.state = ibmvmc_state_ready;

	dev_info(adapter->dev, "Capabilities: mtu=0x%x, pool_size=0x%x, max_hmc=0x%x\n",
		 ibmvmc.max_mtu, ibmvmc.max_buffer_pool_size,
		 ibmvmc.max_hmc_index);
}

/**
 * ibmvmc_validate_hmc_session - Validate HMC Session
 *
 * @adapter:	crq_server_adapter struct
 * @crq:	ibmvmc_crq_msg struct
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_validate_hmc_session(struct crq_server_adapter *adapter,
				       struct ibmvmc_crq_msg *crq)
{
	unsigned char hmc_index;

	hmc_index = crq->hmc_index;

	if (crq->hmc_session == 0)
		return 0;

	if (hmc_index > ibmvmc.max_hmc_index)
		return -1;

	if (hmcs[hmc_index].session != crq->hmc_session) {
		dev_warn(adapter->dev, "Drop, bad session: expected 0x%x, recv 0x%x\n",
			 hmcs[hmc_index].session, crq->hmc_session);
		return -1;
	}

	return 0;
}

/**
 * ibmvmc_reset - Reset
 *
 * @adapter:	crq_server_adapter struct
 * @xport_event:	export_event field
 *
 * Closes all HMC sessions and conditionally schedules a CRQ reset.
 * @xport_event: If true, the partner closed their CRQ; we don't need to reset.
 *               If false, we need to schedule a CRQ reset.
 */
static void ibmvmc_reset(struct crq_server_adapter *adapter, bool xport_event)
{
	int i;

	if (ibmvmc.state != ibmvmc_state_sched_reset) {
		dev_info(adapter->dev, "*** Reset to initial state.\n");
		for (i = 0; i < ibmvmc_max_hmcs; i++)
			ibmvmc_return_hmc(&hmcs[i], xport_event);

		if (xport_event) {
			/* CRQ was closed by the partner.  We don't need to do
			 * anything except set ourself to the correct state to
			 * handle init msgs.
			 */
			ibmvmc.state = ibmvmc_state_crqinit;
		} else {
			/* The partner did not close their CRQ - instead, we're
			 * closing the CRQ on our end. Need to schedule this
			 * for process context, because CRQ reset may require a
			 * sleep.
			 *
			 * Setting ibmvmc.state here immediately prevents
			 * ibmvmc_open from completing until the reset
			 * completes in process context.
			 */
			ibmvmc.state = ibmvmc_state_sched_reset;
			dev_dbg(adapter->dev, "Device reset scheduled");
			wake_up_interruptible(&adapter->reset_wait_queue);
		}
	}
}

/**
 * ibmvmc_reset_task - Reset Task
 *
 * @data:	Data field
 *
 * Performs a CRQ reset of the VMC device in process context.
 * NOTE: This function should not be called directly, use ibmvmc_reset.
 */
static int ibmvmc_reset_task(void *data)
{
	struct crq_server_adapter *adapter = data;
	int rc;

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {
		wait_event_interruptible(adapter->reset_wait_queue,
			(ibmvmc.state == ibmvmc_state_sched_reset) ||
			kthread_should_stop());

		if (kthread_should_stop())
			break;

		dev_dbg(adapter->dev, "CRQ resetting in process context");
		tasklet_disable(&adapter->work_task);

		rc = ibmvmc_reset_crq_queue(adapter);

		if (rc != H_SUCCESS && rc != H_RESOURCE) {
			dev_err(adapter->dev, "Error initializing CRQ.  rc = 0x%x\n",
				rc);
			ibmvmc.state = ibmvmc_state_failed;
		} else {
			ibmvmc.state = ibmvmc_state_crqinit;

			if (ibmvmc_send_crq(adapter, 0xC001000000000000LL, 0)
			    != 0 && rc != H_RESOURCE)
				dev_warn(adapter->dev, "Failed to send initialize CRQ message\n");
		}

		vio_enable_interrupts(to_vio_dev(adapter->dev));
		tasklet_enable(&adapter->work_task);
	}

	return 0;
}

/**
 * ibmvmc_process_open_resp - Process Open Response
 *
 * @crq: ibmvmc_crq_msg struct
 * @adapter:    crq_server_adapter struct
 *
 * This command is sent by the hypervisor in response to the Interface
 * Open message. When this message is received, the indicated buffer is
 * again available for management partition use.
 */
static void ibmvmc_process_open_resp(struct ibmvmc_crq_msg *crq,
				     struct crq_server_adapter *adapter)
{
	unsigned char hmc_index;
	unsigned short buffer_id;

	hmc_index = crq->hmc_index;
	if (hmc_index > ibmvmc.max_hmc_index) {
		/* Why would PHYP give an index > max negotiated? */
		ibmvmc_reset(adapter, false);
		return;
	}

	if (crq->status) {
		dev_warn(adapter->dev, "open_resp: failed - status 0x%x\n",
			 crq->status);
		ibmvmc_return_hmc(&hmcs[hmc_index], false);
		return;
	}

	if (hmcs[hmc_index].state == ibmhmc_state_opening) {
		buffer_id = be16_to_cpu(crq->var2.buffer_id);
		if (buffer_id >= ibmvmc.max_buffer_pool_size) {
			dev_err(adapter->dev, "open_resp: invalid buffer_id = 0x%x\n",
				buffer_id);
			hmcs[hmc_index].state = ibmhmc_state_failed;
		} else {
			ibmvmc_free_hmc_buffer(&hmcs[hmc_index],
					       &hmcs[hmc_index].buffer[buffer_id]);
			hmcs[hmc_index].state = ibmhmc_state_ready;
			dev_dbg(adapter->dev, "open_resp: set hmc state = ready\n");
		}
	} else {
		dev_warn(adapter->dev, "open_resp: invalid hmc state (0x%x)\n",
			 hmcs[hmc_index].state);
	}
}

/**
 * ibmvmc_process_close_resp - Process Close Response
 *
 * @crq: ibmvmc_crq_msg struct
 * @adapter:    crq_server_adapter struct
 *
 * This command is sent by the hypervisor in response to the managemant
 * application Interface Close message.
 *
 * If the close fails, simply reset the entire driver as the state of the VMC
 * must be in tough shape.
 */
static void ibmvmc_process_close_resp(struct ibmvmc_crq_msg *crq,
				      struct crq_server_adapter *adapter)
{
	unsigned char hmc_index;

	hmc_index = crq->hmc_index;
	if (hmc_index > ibmvmc.max_hmc_index) {
		ibmvmc_reset(adapter, false);
		return;
	}

	if (crq->status) {
		dev_warn(adapter->dev, "close_resp: failed - status 0x%x\n",
			 crq->status);
		ibmvmc_reset(adapter, false);
		return;
	}

	ibmvmc_return_hmc(&hmcs[hmc_index], false);
}

/**
 * ibmvmc_crq_process - Process CRQ
 *
 * @adapter:    crq_server_adapter struct
 * @crq:	ibmvmc_crq_msg struct
 *
 * Process the CRQ message based upon the type of message received.
 *
 */
static void ibmvmc_crq_process(struct crq_server_adapter *adapter,
			       struct ibmvmc_crq_msg *crq)
{
	switch (crq->type) {
	case VMC_MSG_CAP_RESP:
		dev_dbg(adapter->dev, "CRQ recv: capabilities resp (0x%x)\n",
			crq->type);
		if (ibmvmc.state == ibmvmc_state_capabilities)
			ibmvmc_process_capabilities(adapter, crq);
		else
			dev_warn(adapter->dev, "caps msg invalid in state 0x%x\n",
				 ibmvmc.state);
		break;
	case VMC_MSG_OPEN_RESP:
		dev_dbg(adapter->dev, "CRQ recv: open resp (0x%x)\n",
			crq->type);
		if (ibmvmc_validate_hmc_session(adapter, crq) == 0)
			ibmvmc_process_open_resp(crq, adapter);
		break;
	case VMC_MSG_ADD_BUF:
		dev_dbg(adapter->dev, "CRQ recv: add buf (0x%x)\n",
			crq->type);
		if (ibmvmc_validate_hmc_session(adapter, crq) == 0)
			ibmvmc_add_buffer(adapter, crq);
		break;
	case VMC_MSG_REM_BUF:
		dev_dbg(adapter->dev, "CRQ recv: rem buf (0x%x)\n",
			crq->type);
		if (ibmvmc_validate_hmc_session(adapter, crq) == 0)
			ibmvmc_rem_buffer(adapter, crq);
		break;
	case VMC_MSG_SIGNAL:
		dev_dbg(adapter->dev, "CRQ recv: signal msg (0x%x)\n",
			crq->type);
		if (ibmvmc_validate_hmc_session(adapter, crq) == 0)
			ibmvmc_recv_msg(adapter, crq);
		break;
	case VMC_MSG_CLOSE_RESP:
		dev_dbg(adapter->dev, "CRQ recv: close resp (0x%x)\n",
			crq->type);
		if (ibmvmc_validate_hmc_session(adapter, crq) == 0)
			ibmvmc_process_close_resp(crq, adapter);
		break;
	case VMC_MSG_CAP:
	case VMC_MSG_OPEN:
	case VMC_MSG_CLOSE:
	case VMC_MSG_ADD_BUF_RESP:
	case VMC_MSG_REM_BUF_RESP:
		dev_warn(adapter->dev, "CRQ recv: unexpected msg (0x%x)\n",
			 crq->type);
		break;
	default:
		dev_warn(adapter->dev, "CRQ recv: unknown msg (0x%x)\n",
			 crq->type);
		break;
	}
}

/**
 * ibmvmc_handle_crq_init - Handle CRQ Init
 *
 * @crq:	ibmvmc_crq_msg struct
 * @adapter:	crq_server_adapter struct
 *
 * Handle the type of crq initialization based on whether
 * it is a message or a response.
 *
 */
static void ibmvmc_handle_crq_init(struct ibmvmc_crq_msg *crq,
				   struct crq_server_adapter *adapter)
{
	switch (crq->type) {
	case 0x01:	/* Initialization message */
		dev_dbg(adapter->dev, "CRQ recv: CRQ init msg - state 0x%x\n",
			ibmvmc.state);
		if (ibmvmc.state == ibmvmc_state_crqinit) {
			/* Send back a response */
			if (ibmvmc_send_crq(adapter, 0xC002000000000000,
					    0) == 0)
				ibmvmc_send_capabilities(adapter);
			else
				dev_err(adapter->dev, " Unable to send init rsp\n");
		} else {
			dev_err(adapter->dev, "Invalid state 0x%x mtu = 0x%x\n",
				ibmvmc.state, ibmvmc.max_mtu);
		}

		break;
	case 0x02:	/* Initialization response */
		dev_dbg(adapter->dev, "CRQ recv: initialization resp msg - state 0x%x\n",
			ibmvmc.state);
		if (ibmvmc.state == ibmvmc_state_crqinit)
			ibmvmc_send_capabilities(adapter);
		break;
	default:
		dev_warn(adapter->dev, "Unknown crq message type 0x%lx\n",
			 (unsigned long)crq->type);
	}
}

/**
 * ibmvmc_handle_crq - Handle CRQ
 *
 * @crq:	ibmvmc_crq_msg struct
 * @adapter:	crq_server_adapter struct
 *
 * Read the command elements from the command queue and execute the
 * requests based upon the type of crq message.
 *
 */
static void ibmvmc_handle_crq(struct ibmvmc_crq_msg *crq,
			      struct crq_server_adapter *adapter)
{
	switch (crq->valid) {
	case 0xC0:		/* initialization */
		ibmvmc_handle_crq_init(crq, adapter);
		break;
	case 0xFF:	/* Hypervisor telling us the connection is closed */
		dev_warn(adapter->dev, "CRQ recv: virtual adapter failed - resetting.\n");
		ibmvmc_reset(adapter, true);
		break;
	case 0x80:	/* real payload */
		ibmvmc_crq_process(adapter, crq);
		break;
	default:
		dev_warn(adapter->dev, "CRQ recv: unknown msg 0x%02x.\n",
			 crq->valid);
		break;
	}
}

static void ibmvmc_task(unsigned long data)
{
	struct crq_server_adapter *adapter =
		(struct crq_server_adapter *)data;
	struct vio_dev *vdev = to_vio_dev(adapter->dev);
	struct ibmvmc_crq_msg *crq;
	int done = 0;

	while (!done) {
		/* Pull all the valid messages off the CRQ */
		while ((crq = crq_queue_next_crq(&adapter->queue)) != NULL) {
			ibmvmc_handle_crq(crq, adapter);
			crq->valid = 0x00;
			/* CRQ reset was requested, stop processing CRQs.
			 * Interrupts will be re-enabled by the reset task.
			 */
			if (ibmvmc.state == ibmvmc_state_sched_reset)
				return;
		}

		vio_enable_interrupts(vdev);
		crq = crq_queue_next_crq(&adapter->queue);
		if (crq) {
			vio_disable_interrupts(vdev);
			ibmvmc_handle_crq(crq, adapter);
			crq->valid = 0x00;
			/* CRQ reset was requested, stop processing CRQs.
			 * Interrupts will be re-enabled by the reset task.
			 */
			if (ibmvmc.state == ibmvmc_state_sched_reset)
				return;
		} else {
			done = 1;
		}
	}
}

/**
 * ibmvmc_init_crq_queue - Init CRQ Queue
 *
 * @adapter:	crq_server_adapter struct
 *
 * Return:
 *	0 - Success
 *	Non-zero - Failure
 */
static int ibmvmc_init_crq_queue(struct crq_server_adapter *adapter)
{
	struct vio_dev *vdev = to_vio_dev(adapter->dev);
	struct crq_queue *queue = &adapter->queue;
	int rc = 0;
	int retrc = 0;

	queue->msgs = (struct ibmvmc_crq_msg *)get_zeroed_page(GFP_KERNEL);

	if (!queue->msgs)
		goto malloc_failed;

	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(adapter->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(adapter->dev, queue->msg_token))
		goto map_failed;

	retrc = plpar_hcall_norets(H_REG_CRQ,
				   vdev->unit_address,
				   queue->msg_token, PAGE_SIZE);
	rc = retrc;

	if (rc == H_RESOURCE)
		rc = ibmvmc_reset_crq_queue(adapter);

	if (rc == 2) {
		dev_warn(adapter->dev, "Partner adapter not ready\n");
		retrc = 0;
	} else if (rc != 0) {
		dev_err(adapter->dev, "Error %d opening adapter\n", rc);
		goto reg_crq_failed;
	}

	queue->cur = 0;
	spin_lock_init(&queue->lock);

	tasklet_init(&adapter->work_task, ibmvmc_task, (unsigned long)adapter);

	if (request_irq(vdev->irq,
			ibmvmc_handle_event,
			0, "ibmvmc", (void *)adapter) != 0) {
		dev_err(adapter->dev, "couldn't register irq 0x%x\n",
			vdev->irq);
		goto req_irq_failed;
	}

	rc = vio_enable_interrupts(vdev);
	if (rc != 0) {
		dev_err(adapter->dev, "Error %d enabling interrupts!!!\n", rc);
		goto req_irq_failed;
	}

	return retrc;

req_irq_failed:
	/* Cannot have any work since we either never got our IRQ registered,
	 * or never got interrupts enabled
	 */
	tasklet_kill(&adapter->work_task);
	h_free_crq(vdev->unit_address);
reg_crq_failed:
	dma_unmap_single(adapter->dev,
			 queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long)queue->msgs);
malloc_failed:
	return -ENOMEM;
}

/* Fill in the liobn and riobn fields on the adapter */
static int read_dma_window(struct vio_dev *vdev,
			   struct crq_server_adapter *adapter)
{
	const __be32 *dma_window;
	const __be32 *prop;

	/* TODO Using of_parse_dma_window would be better, but it doesn't give
	 * a way to read multiple windows without already knowing the size of
	 * a window or the number of windows
	 */
	dma_window =
		(const __be32 *)vio_get_attribute(vdev, "ibm,my-dma-window",
						NULL);
	if (!dma_window) {
		dev_warn(adapter->dev, "Couldn't find ibm,my-dma-window property\n");
		return -1;
	}

	adapter->liobn = be32_to_cpu(*dma_window);
	dma_window++;

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-address-cells",
						NULL);
	if (!prop) {
		dev_warn(adapter->dev, "Couldn't find ibm,#dma-address-cells property\n");
		dma_window++;
	} else {
		dma_window += be32_to_cpu(*prop);
	}

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-size-cells",
						NULL);
	if (!prop) {
		dev_warn(adapter->dev, "Couldn't find ibm,#dma-size-cells property\n");
		dma_window++;
	} else {
		dma_window += be32_to_cpu(*prop);
	}

	/* dma_window should point to the second window now */
	adapter->riobn = be32_to_cpu(*dma_window);

	return 0;
}

static int ibmvmc_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct crq_server_adapter *adapter = &ibmvmc_adapter;
	int rc;

	dev_set_drvdata(&vdev->dev, NULL);
	memset(adapter, 0, sizeof(*adapter));
	adapter->dev = &vdev->dev;

	dev_info(adapter->dev, "Probe for UA 0x%x\n", vdev->unit_address);

	rc = read_dma_window(vdev, adapter);
	if (rc != 0) {
		ibmvmc.state = ibmvmc_state_failed;
		return -1;
	}

	dev_dbg(adapter->dev, "Probe: liobn 0x%x, riobn 0x%x\n",
		adapter->liobn, adapter->riobn);

	init_waitqueue_head(&adapter->reset_wait_queue);
	adapter->reset_task = kthread_run(ibmvmc_reset_task, adapter, "ibmvmc");
	if (IS_ERR(adapter->reset_task)) {
		dev_err(adapter->dev, "Failed to start reset thread\n");
		ibmvmc.state = ibmvmc_state_failed;
		rc = PTR_ERR(adapter->reset_task);
		adapter->reset_task = NULL;
		return rc;
	}

	rc = ibmvmc_init_crq_queue(adapter);
	if (rc != 0 && rc != H_RESOURCE) {
		dev_err(adapter->dev, "Error initializing CRQ.  rc = 0x%x\n",
			rc);
		ibmvmc.state = ibmvmc_state_failed;
		goto crq_failed;
	}

	ibmvmc.state = ibmvmc_state_crqinit;

	/* Try to send an initialization message.  Note that this is allowed
	 * to fail if the other end is not acive.  In that case we just wait
	 * for the other side to initialize.
	 */
	if (ibmvmc_send_crq(adapter, 0xC001000000000000LL, 0) != 0 &&
	    rc != H_RESOURCE)
		dev_warn(adapter->dev, "Failed to send initialize CRQ message\n");

	dev_set_drvdata(&vdev->dev, adapter);

	return 0;

crq_failed:
	kthread_stop(adapter->reset_task);
	adapter->reset_task = NULL;
	return -EPERM;
}

static int ibmvmc_remove(struct vio_dev *vdev)
{
	struct crq_server_adapter *adapter = dev_get_drvdata(&vdev->dev);

	dev_info(adapter->dev, "Entering remove for UA 0x%x\n",
		 vdev->unit_address);
	ibmvmc_release_crq_queue(adapter);

	return 0;
}

static struct vio_device_id ibmvmc_device_table[] = {
	{ "ibm,vmc", "IBM,vmc" },
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmvmc_device_table);

static struct vio_driver ibmvmc_driver = {
	.name        = ibmvmc_driver_name,
	.id_table    = ibmvmc_device_table,
	.probe       = ibmvmc_probe,
	.remove      = ibmvmc_remove,
};

static void __init ibmvmc_scrub_module_parms(void)
{
	if (ibmvmc_max_mtu > MAX_MTU) {
		pr_warn("ibmvmc: Max MTU reduced to %d\n", MAX_MTU);
		ibmvmc_max_mtu = MAX_MTU;
	} else if (ibmvmc_max_mtu < MIN_MTU) {
		pr_warn("ibmvmc: Max MTU increased to %d\n", MIN_MTU);
		ibmvmc_max_mtu = MIN_MTU;
	}

	if (ibmvmc_max_buf_pool_size > MAX_BUF_POOL_SIZE) {
		pr_warn("ibmvmc: Max buffer pool size reduced to %d\n",
			MAX_BUF_POOL_SIZE);
		ibmvmc_max_buf_pool_size = MAX_BUF_POOL_SIZE;
	} else if (ibmvmc_max_buf_pool_size < MIN_BUF_POOL_SIZE) {
		pr_warn("ibmvmc: Max buffer pool size increased to %d\n",
			MIN_BUF_POOL_SIZE);
		ibmvmc_max_buf_pool_size = MIN_BUF_POOL_SIZE;
	}

	if (ibmvmc_max_hmcs > MAX_HMCS) {
		pr_warn("ibmvmc: Max HMCs reduced to %d\n", MAX_HMCS);
		ibmvmc_max_hmcs = MAX_HMCS;
	} else if (ibmvmc_max_hmcs < MIN_HMCS) {
		pr_warn("ibmvmc: Max HMCs increased to %d\n", MIN_HMCS);
		ibmvmc_max_hmcs = MIN_HMCS;
	}
}

static struct miscdevice ibmvmc_miscdev = {
	.name = ibmvmc_driver_name,
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &ibmvmc_fops,
};

static int __init ibmvmc_module_init(void)
{
	int rc, i, j;

	ibmvmc.state = ibmvmc_state_initial;
	pr_info("ibmvmc: version %s\n", IBMVMC_DRIVER_VERSION);

	rc = misc_register(&ibmvmc_miscdev);
	if (rc) {
		pr_err("ibmvmc: misc registration failed\n");
		goto misc_register_failed;
	}
	pr_info("ibmvmc: node %d:%d\n", MISC_MAJOR,
		ibmvmc_miscdev.minor);

	/* Initialize data structures */
	memset(hmcs, 0, sizeof(struct ibmvmc_hmc) * MAX_HMCS);
	for (i = 0; i < MAX_HMCS; i++) {
		spin_lock_init(&hmcs[i].lock);
		hmcs[i].state = ibmhmc_state_free;
		for (j = 0; j < MAX_BUF_POOL_SIZE; j++)
			hmcs[i].queue_outbound_msgs[j] = VMC_INVALID_BUFFER_ID;
	}

	/* Sanity check module parms */
	ibmvmc_scrub_module_parms();

	/*
	 * Initialize some reasonable values.  Might be negotiated smaller
	 * values during the capabilities exchange.
	 */
	ibmvmc.max_mtu = ibmvmc_max_mtu;
	ibmvmc.max_buffer_pool_size = ibmvmc_max_buf_pool_size;
	ibmvmc.max_hmc_index = ibmvmc_max_hmcs - 1;

	rc = vio_register_driver(&ibmvmc_driver);

	if (rc) {
		pr_err("ibmvmc: rc %d from vio_register_driver\n", rc);
		goto vio_reg_failed;
	}

	return 0;

vio_reg_failed:
	misc_deregister(&ibmvmc_miscdev);
misc_register_failed:
	return rc;
}

static void __exit ibmvmc_module_exit(void)
{
	pr_info("ibmvmc: module exit\n");
	vio_unregister_driver(&ibmvmc_driver);
	misc_deregister(&ibmvmc_miscdev);
}

module_init(ibmvmc_module_init);
module_exit(ibmvmc_module_exit);

module_param_named(buf_pool_size, ibmvmc_max_buf_pool_size,
		   int, 0644);
MODULE_PARM_DESC(buf_pool_size, "Buffer pool size");
module_param_named(max_hmcs, ibmvmc_max_hmcs, int, 0644);
MODULE_PARM_DESC(max_hmcs, "Max HMCs");
module_param_named(max_mtu, ibmvmc_max_mtu, int, 0644);
MODULE_PARM_DESC(max_mtu, "Max MTU");

MODULE_AUTHOR("Steven Royer <seroyer@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM VMC");
MODULE_VERSION(IBMVMC_DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
