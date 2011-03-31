/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#if !defined(_SCI_HOST_H_)
#define _SCI_HOST_H_

#include "phy.h"
/*#include "task.h"*/
#include "timers.h"
#include "remote_device.h"
#include "scic_remote_device.h"

#define DRV_NAME "isci"
#define SCI_PCI_BAR_COUNT 2
#define SCI_NUM_MSI_X_INT 2
#define SCI_SMU_BAR       0
#define SCI_SMU_BAR_SIZE  (16*1024)
#define SCI_SCU_BAR       1
#define SCI_SCU_BAR_SIZE  (4*1024*1024)
#define SCI_IO_SPACE_BAR0 2
#define SCI_IO_SPACE_BAR1 3
#define ISCI_CAN_QUEUE_VAL 250 /* < SCI_MAX_IO_REQUESTS ? */
#define SCIC_CONTROLLER_STOP_TIMEOUT 5000

struct coherent_memory_info {
	struct list_head node;
	dma_addr_t dma_handle;
	void *vaddr;
	size_t size;
	struct sci_physical_memory_descriptor *mde;
};

struct isci_host {
	struct scic_sds_controller *core_controller;
	union scic_oem_parameters oem_parameters;

	int id; /* unique within a given pci device */
	struct list_head timers;
	void *core_ctrl_memory;
	struct dma_pool *dma_pool;
	unsigned int dma_pool_alloc_size;
	struct isci_phy phys[SCI_MAX_PHYS];

	/* isci_ports and sas_ports are implicitly parallel to the
	 * ports maintained by the core
	 */
	struct isci_port isci_ports[SCI_MAX_PORTS];
	struct asd_sas_port sas_ports[SCI_MAX_PORTS];
	struct sas_ha_struct sas_ha;

	int can_queue;
	spinlock_t queue_lock;
	spinlock_t state_lock;

	struct pci_dev *pdev;

	enum isci_status status;
	#define IHOST_START_PENDING 0
	#define IHOST_STOP_PENDING 1
	unsigned long flags;
	wait_queue_head_t eventq;
	struct Scsi_Host *shost;
	struct tasklet_struct completion_tasklet;
	struct list_head mdl_struct_list;
	struct list_head requests_to_complete;
	struct list_head requests_to_errorback;
	spinlock_t scic_lock;

	/* careful only access this via idev_by_id */
	struct isci_remote_device devices[0];
};

static inline struct isci_remote_device *idev_by_id(struct isci_host *ihost, int i)
{
	void *p = ihost->devices;

	return p + i * (sizeof(struct isci_remote_device) +
			scic_remote_device_get_object_size());
}

/**
 * struct isci_pci_info - This class represents the pci function containing the
 *    controllers. Depending on PCI SKU, there could be up to 2 controllers in
 *    the PCI function.
 */
#define SCI_MAX_MSIX_INT (SCI_NUM_MSI_X_INT*SCI_MAX_CONTROLLERS)

struct isci_pci_info {
	struct msix_entry msix_entries[SCI_MAX_MSIX_INT];
	struct isci_host *hosts[SCI_MAX_CONTROLLERS];
	struct isci_orom *orom;
};

static inline struct isci_pci_info *to_pci_info(struct pci_dev *pdev)
{
	return pci_get_drvdata(pdev);
}

#define for_each_isci_host(id, ihost, pdev) \
	for (id = 0, ihost = to_pci_info(pdev)->hosts[id]; \
	     id < ARRAY_SIZE(to_pci_info(pdev)->hosts) && ihost; \
	     ihost = to_pci_info(pdev)->hosts[++id])

static inline
enum isci_status isci_host_get_state(
	struct isci_host *isci_host)
{
	return isci_host->status;
}


static inline void isci_host_change_state(
	struct isci_host *isci_host,
	enum isci_status status)
{
	unsigned long flags;

	dev_dbg(&isci_host->pdev->dev,
		"%s: isci_host = %p, state = 0x%x",
		__func__,
		isci_host,
		status);
	spin_lock_irqsave(&isci_host->state_lock, flags);
	isci_host->status = status;
	spin_unlock_irqrestore(&isci_host->state_lock, flags);

}

static inline int isci_host_can_queue(
	struct isci_host *isci_host,
	int num)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&isci_host->queue_lock, flags);
	if ((isci_host->can_queue - num) < 0) {
		dev_dbg(&isci_host->pdev->dev,
			"%s: isci_host->can_queue = %d\n",
			__func__,
			isci_host->can_queue);
		ret = -SAS_QUEUE_FULL;

	} else
		isci_host->can_queue -= num;

	spin_unlock_irqrestore(&isci_host->queue_lock, flags);

	return ret;
}

static inline void isci_host_can_dequeue(
	struct isci_host *isci_host,
	int num)
{
	unsigned long flags;

	spin_lock_irqsave(&isci_host->queue_lock, flags);
	isci_host->can_queue += num;
	spin_unlock_irqrestore(&isci_host->queue_lock, flags);
}

static inline void wait_for_start(struct isci_host *ihost)
{
	wait_event(ihost->eventq, !test_bit(IHOST_START_PENDING, &ihost->flags));
}

static inline void wait_for_stop(struct isci_host *ihost)
{
	wait_event(ihost->eventq, !test_bit(IHOST_STOP_PENDING, &ihost->flags));
}

static inline void wait_for_device_start(struct isci_host *ihost, struct isci_remote_device *idev)
{
	wait_event(ihost->eventq, !test_bit(IDEV_START_PENDING, &idev->flags));
}

static inline void wait_for_device_stop(struct isci_host *ihost, struct isci_remote_device *idev)
{
	wait_event(ihost->eventq, !test_bit(IDEV_STOP_PENDING, &idev->flags));
}

static inline struct isci_host *dev_to_ihost(struct domain_device *dev)
{
	return dev->port->ha->lldd_ha;
}

/**
 * isci_host_scan_finished() -
 *
 * This function is one of the SCSI Host Template functions. The SCSI midlayer
 * calls this function during a target scan, approx. once every 10 millisecs.
 */
int isci_host_scan_finished(
	struct Scsi_Host *,
	unsigned long);


/**
 * isci_host_scan_start() -
 *
 * This function is one of the SCSI Host Template function, called by the SCSI
 * mid layer berfore a target scan begins. The core library controller start
 * routine is called from here.
 */
void isci_host_scan_start(
	struct Scsi_Host *);

/**
 * isci_host_start_complete() -
 *
 * This function is called by the core library, through the ISCI Module, to
 * indicate controller start status.
 */
void isci_host_start_complete(
	struct isci_host *,
	enum sci_status);

void isci_host_stop_complete(
	struct isci_host *isci_host,
	enum sci_status completion_status);

int isci_host_init(struct isci_host *);

void isci_host_init_controller_names(
	struct isci_host *isci_host,
	unsigned int controller_idx);

void isci_host_deinit(
	struct isci_host *);

void isci_host_port_link_up(
	struct isci_host *,
	struct scic_sds_port *,
	struct scic_sds_phy *);
int isci_host_dev_found(struct domain_device *);

void isci_host_remote_device_start_complete(
	struct isci_host *,
	struct isci_remote_device *,
	enum sci_status);

#endif /* !defined(_SCI_HOST_H_) */
