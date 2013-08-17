/* ------------------------------------------------------------
 * rpa_vscsi.c
 * (C) Copyright IBM Corporation 1994, 2003
 * Authors: Colin DeVilbiss (devilbis@us.ibm.com)
 *          Santiago Leon (santil@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * ------------------------------------------------------------
 * RPA-specific functions of the SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */

#include <asm/vio.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/hvcall.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include "ibmvscsi.h"

static char partition_name[97] = "UNKNOWN";
static unsigned int partition_number = -1;

/* ------------------------------------------------------------
 * Routines for managing the command/response queue
 */
/**
 * rpavscsi_handle_event: - Interrupt handler for crq events
 * @irq:	number of irq to handle, not used
 * @dev_instance: ibmvscsi_host_data of host that received interrupt
 *
 * Disables interrupts and schedules srp_task
 * Always returns IRQ_HANDLED
 */
static irqreturn_t rpavscsi_handle_event(int irq, void *dev_instance)
{
	struct ibmvscsi_host_data *hostdata =
	    (struct ibmvscsi_host_data *)dev_instance;
	vio_disable_interrupts(to_vio_dev(hostdata->dev));
	tasklet_schedule(&hostdata->srp_task);
	return IRQ_HANDLED;
}

/**
 * release_crq_queue: - Deallocates data and unregisters CRQ
 * @queue:	crq_queue to initialize and register
 * @host_data:	ibmvscsi_host_data of host
 *
 * Frees irq, deallocates a page for messages, unmaps dma, and unregisters
 * the crq with the hypervisor.
 */
static void rpavscsi_release_crq_queue(struct crq_queue *queue,
				       struct ibmvscsi_host_data *hostdata,
				       int max_requests)
{
	long rc = 0;
	struct vio_dev *vdev = to_vio_dev(hostdata->dev);
	free_irq(vdev->irq, (void *)hostdata);
	tasklet_kill(&hostdata->srp_task);
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));
	dma_unmap_single(hostdata->dev,
			 queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
	free_page((unsigned long)queue->msgs);
}

/**
 * crq_queue_next_crq: - Returns the next entry in message queue
 * @queue:	crq_queue to use
 *
 * Returns pointer to next entry in queue, or NULL if there are no new 
 * entried in the CRQ.
 */
static struct viosrp_crq *crq_queue_next_crq(struct crq_queue *queue)
{
	struct viosrp_crq *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
	} else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}

/**
 * rpavscsi_send_crq: - Send a CRQ
 * @hostdata:	the adapter
 * @word1:	the first 64 bits of the data
 * @word2:	the second 64 bits of the data
 */
static int rpavscsi_send_crq(struct ibmvscsi_host_data *hostdata,
			     u64 word1, u64 word2)
{
	struct vio_dev *vdev = to_vio_dev(hostdata->dev);

	return plpar_hcall_norets(H_SEND_CRQ, vdev->unit_address, word1, word2);
}

/**
 * rpavscsi_task: - Process srps asynchronously
 * @data:	ibmvscsi_host_data of host
 */
static void rpavscsi_task(void *data)
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)data;
	struct vio_dev *vdev = to_vio_dev(hostdata->dev);
	struct viosrp_crq *crq;
	int done = 0;

	while (!done) {
		/* Pull all the valid messages off the CRQ */
		while ((crq = crq_queue_next_crq(&hostdata->queue)) != NULL) {
			ibmvscsi_handle_crq(crq, hostdata);
			crq->valid = 0x00;
		}

		vio_enable_interrupts(vdev);
		if ((crq = crq_queue_next_crq(&hostdata->queue)) != NULL) {
			vio_disable_interrupts(vdev);
			ibmvscsi_handle_crq(crq, hostdata);
			crq->valid = 0x00;
		} else {
			done = 1;
		}
	}
}

static void gather_partition_info(void)
{
	struct device_node *rootdn;

	const char *ppartition_name;
	const unsigned int *p_number_ptr;

	/* Retrieve information about this partition */
	rootdn = of_find_node_by_path("/");
	if (!rootdn) {
		return;
	}

	ppartition_name = of_get_property(rootdn, "ibm,partition-name", NULL);
	if (ppartition_name)
		strncpy(partition_name, ppartition_name,
				sizeof(partition_name));
	p_number_ptr = of_get_property(rootdn, "ibm,partition-no", NULL);
	if (p_number_ptr)
		partition_number = *p_number_ptr;
	of_node_put(rootdn);
}

static void set_adapter_info(struct ibmvscsi_host_data *hostdata)
{
	memset(&hostdata->madapter_info, 0x00,
			sizeof(hostdata->madapter_info));

	dev_info(hostdata->dev, "SRP_VERSION: %s\n", SRP_VERSION);
	strcpy(hostdata->madapter_info.srp_version, SRP_VERSION);

	strncpy(hostdata->madapter_info.partition_name, partition_name,
			sizeof(hostdata->madapter_info.partition_name));

	hostdata->madapter_info.partition_number = partition_number;

	hostdata->madapter_info.mad_version = 1;
	hostdata->madapter_info.os_type = 2;
}

/**
 * reset_crq_queue: - resets a crq after a failure
 * @queue:	crq_queue to initialize and register
 * @hostdata:	ibmvscsi_host_data of host
 *
 */
static int rpavscsi_reset_crq_queue(struct crq_queue *queue,
				    struct ibmvscsi_host_data *hostdata)
{
	int rc = 0;
	struct vio_dev *vdev = to_vio_dev(hostdata->dev);

	/* Close the CRQ */
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));

	/* Clean out the queue */
	memset(queue->msgs, 0x00, PAGE_SIZE);
	queue->cur = 0;

	set_adapter_info(hostdata);

	/* And re-open it again */
	rc = plpar_hcall_norets(H_REG_CRQ,
				vdev->unit_address,
				queue->msg_token, PAGE_SIZE);
	if (rc == 2) {
		/* Adapter is good, but other end is not ready */
		dev_warn(hostdata->dev, "Partner adapter not ready\n");
	} else if (rc != 0) {
		dev_warn(hostdata->dev, "couldn't register crq--rc 0x%x\n", rc);
	}
	return rc;
}

/**
 * initialize_crq_queue: - Initializes and registers CRQ with hypervisor
 * @queue:	crq_queue to initialize and register
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Allocates a page for messages, maps it for dma, and registers
 * the crq with the hypervisor.
 * Returns zero on success.
 */
static int rpavscsi_init_crq_queue(struct crq_queue *queue,
				   struct ibmvscsi_host_data *hostdata,
				   int max_requests)
{
	int rc;
	int retrc;
	struct vio_dev *vdev = to_vio_dev(hostdata->dev);

	queue->msgs = (struct viosrp_crq *)get_zeroed_page(GFP_KERNEL);

	if (!queue->msgs)
		goto malloc_failed;
	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(hostdata->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(hostdata->dev, queue->msg_token))
		goto map_failed;

	gather_partition_info();
	set_adapter_info(hostdata);

	retrc = rc = plpar_hcall_norets(H_REG_CRQ,
				vdev->unit_address,
				queue->msg_token, PAGE_SIZE);
	if (rc == H_RESOURCE)
		/* maybe kexecing and resource is busy. try a reset */
		rc = rpavscsi_reset_crq_queue(queue,
					      hostdata);

	if (rc == 2) {
		/* Adapter is good, but other end is not ready */
		dev_warn(hostdata->dev, "Partner adapter not ready\n");
		retrc = 0;
	} else if (rc != 0) {
		dev_warn(hostdata->dev, "Error %d opening adapter\n", rc);
		goto reg_crq_failed;
	}

	queue->cur = 0;
	spin_lock_init(&queue->lock);

	tasklet_init(&hostdata->srp_task, (void *)rpavscsi_task,
		     (unsigned long)hostdata);

	if (request_irq(vdev->irq,
			rpavscsi_handle_event,
			0, "ibmvscsi", (void *)hostdata) != 0) {
		dev_err(hostdata->dev, "couldn't register irq 0x%x\n",
			vdev->irq);
		goto req_irq_failed;
	}

	rc = vio_enable_interrupts(vdev);
	if (rc != 0) {
		dev_err(hostdata->dev, "Error %d enabling interrupts!!!\n", rc);
		goto req_irq_failed;
	}

	return retrc;

      req_irq_failed:
	tasklet_kill(&hostdata->srp_task);
	rc = 0;
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));
      reg_crq_failed:
	dma_unmap_single(hostdata->dev,
			 queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
      map_failed:
	free_page((unsigned long)queue->msgs);
      malloc_failed:
	return -1;
}

/**
 * reenable_crq_queue: - reenables a crq after
 * @queue:	crq_queue to initialize and register
 * @hostdata:	ibmvscsi_host_data of host
 *
 */
static int rpavscsi_reenable_crq_queue(struct crq_queue *queue,
				       struct ibmvscsi_host_data *hostdata)
{
	int rc = 0;
	struct vio_dev *vdev = to_vio_dev(hostdata->dev);

	/* Re-enable the CRQ */
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_ENABLE_CRQ, vdev->unit_address);
	} while ((rc == H_IN_PROGRESS) || (rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));

	if (rc)
		dev_err(hostdata->dev, "Error %d enabling adapter\n", rc);
	return rc;
}

/**
 * rpavscsi_resume: - resume after suspend
 * @hostdata:	ibmvscsi_host_data of host
 *
 */
static int rpavscsi_resume(struct ibmvscsi_host_data *hostdata)
{
	vio_disable_interrupts(to_vio_dev(hostdata->dev));
	tasklet_schedule(&hostdata->srp_task);
	return 0;
}

struct ibmvscsi_ops rpavscsi_ops = {
	.init_crq_queue = rpavscsi_init_crq_queue,
	.release_crq_queue = rpavscsi_release_crq_queue,
	.reset_crq_queue = rpavscsi_reset_crq_queue,
	.reenable_crq_queue = rpavscsi_reenable_crq_queue,
	.send_crq = rpavscsi_send_crq,
	.resume = rpavscsi_resume,
};
