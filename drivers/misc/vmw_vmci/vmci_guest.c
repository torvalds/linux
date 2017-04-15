/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

#include "vmci_datagram.h"
#include "vmci_doorbell.h"
#include "vmci_context.h"
#include "vmci_driver.h"
#include "vmci_event.h"

#define PCI_DEVICE_ID_VMWARE_VMCI	0x0740

#define VMCI_UTIL_NUM_RESOURCES 1

static bool vmci_disable_msi;
module_param_named(disable_msi, vmci_disable_msi, bool, 0);
MODULE_PARM_DESC(disable_msi, "Disable MSI use in driver - (default=0)");

static bool vmci_disable_msix;
module_param_named(disable_msix, vmci_disable_msix, bool, 0);
MODULE_PARM_DESC(disable_msix, "Disable MSI-X use in driver - (default=0)");

static u32 ctx_update_sub_id = VMCI_INVALID_ID;
static u32 vm_context_id = VMCI_INVALID_ID;

struct vmci_guest_device {
	struct device *dev;	/* PCI device we are attached to */
	void __iomem *iobase;

	bool exclusive_vectors;

	struct tasklet_struct datagram_tasklet;
	struct tasklet_struct bm_tasklet;

	void *data_buffer;
	void *notification_bitmap;
	dma_addr_t notification_base;
};

/* vmci_dev singleton device and supporting data*/
struct pci_dev *vmci_pdev;
static struct vmci_guest_device *vmci_dev_g;
static DEFINE_SPINLOCK(vmci_dev_spinlock);

static atomic_t vmci_num_guest_devices = ATOMIC_INIT(0);

bool vmci_guest_code_active(void)
{
	return atomic_read(&vmci_num_guest_devices) != 0;
}

u32 vmci_get_vm_context_id(void)
{
	if (vm_context_id == VMCI_INVALID_ID) {
		struct vmci_datagram get_cid_msg;
		get_cid_msg.dst =
		    vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
				     VMCI_GET_CONTEXT_ID);
		get_cid_msg.src = VMCI_ANON_SRC_HANDLE;
		get_cid_msg.payload_size = 0;
		vm_context_id = vmci_send_datagram(&get_cid_msg);
	}
	return vm_context_id;
}

/*
 * VM to hypervisor call mechanism. We use the standard VMware naming
 * convention since shared code is calling this function as well.
 */
int vmci_send_datagram(struct vmci_datagram *dg)
{
	unsigned long flags;
	int result;

	/* Check args. */
	if (dg == NULL)
		return VMCI_ERROR_INVALID_ARGS;

	/*
	 * Need to acquire spinlock on the device because the datagram
	 * data may be spread over multiple pages and the monitor may
	 * interleave device user rpc calls from multiple
	 * VCPUs. Acquiring the spinlock precludes that
	 * possibility. Disabling interrupts to avoid incoming
	 * datagrams during a "rep out" and possibly landing up in
	 * this function.
	 */
	spin_lock_irqsave(&vmci_dev_spinlock, flags);

	if (vmci_dev_g) {
		iowrite8_rep(vmci_dev_g->iobase + VMCI_DATA_OUT_ADDR,
			     dg, VMCI_DG_SIZE(dg));
		result = ioread32(vmci_dev_g->iobase + VMCI_RESULT_LOW_ADDR);
	} else {
		result = VMCI_ERROR_UNAVAILABLE;
	}

	spin_unlock_irqrestore(&vmci_dev_spinlock, flags);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_send_datagram);

/*
 * Gets called with the new context id if updated or resumed.
 * Context id.
 */
static void vmci_guest_cid_update(u32 sub_id,
				  const struct vmci_event_data *event_data,
				  void *client_data)
{
	const struct vmci_event_payld_ctx *ev_payload =
				vmci_event_data_const_payload(event_data);

	if (sub_id != ctx_update_sub_id) {
		pr_devel("Invalid subscriber (ID=0x%x)\n", sub_id);
		return;
	}

	if (!event_data || ev_payload->context_id == VMCI_INVALID_ID) {
		pr_devel("Invalid event data\n");
		return;
	}

	pr_devel("Updating context from (ID=0x%x) to (ID=0x%x) on event (type=%d)\n",
		 vm_context_id, ev_payload->context_id, event_data->event);

	vm_context_id = ev_payload->context_id;
}

/*
 * Verify that the host supports the hypercalls we need. If it does not,
 * try to find fallback hypercalls and use those instead.  Returns
 * true if required hypercalls (or fallback hypercalls) are
 * supported by the host, false otherwise.
 */
static int vmci_check_host_caps(struct pci_dev *pdev)
{
	bool result;
	struct vmci_resource_query_msg *msg;
	u32 msg_size = sizeof(struct vmci_resource_query_hdr) +
				VMCI_UTIL_NUM_RESOURCES * sizeof(u32);
	struct vmci_datagram *check_msg;

	check_msg = kmalloc(msg_size, GFP_KERNEL);
	if (!check_msg) {
		dev_err(&pdev->dev, "%s: Insufficient memory\n", __func__);
		return -ENOMEM;
	}

	check_msg->dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					  VMCI_RESOURCES_QUERY);
	check_msg->src = VMCI_ANON_SRC_HANDLE;
	check_msg->payload_size = msg_size - VMCI_DG_HEADERSIZE;
	msg = (struct vmci_resource_query_msg *)VMCI_DG_PAYLOAD(check_msg);

	msg->num_resources = VMCI_UTIL_NUM_RESOURCES;
	msg->resources[0] = VMCI_GET_CONTEXT_ID;

	/* Checks that hyper calls are supported */
	result = vmci_send_datagram(check_msg) == 0x01;
	kfree(check_msg);

	dev_dbg(&pdev->dev, "%s: Host capability check: %s\n",
		__func__, result ? "PASSED" : "FAILED");

	/* We need the vector. There are no fallbacks. */
	return result ? 0 : -ENXIO;
}

/*
 * Reads datagrams from the data in port and dispatches them. We
 * always start reading datagrams into only the first page of the
 * datagram buffer. If the datagrams don't fit into one page, we
 * use the maximum datagram buffer size for the remainder of the
 * invocation. This is a simple heuristic for not penalizing
 * small datagrams.
 *
 * This function assumes that it has exclusive access to the data
 * in port for the duration of the call.
 */
static void vmci_dispatch_dgs(unsigned long data)
{
	struct vmci_guest_device *vmci_dev = (struct vmci_guest_device *)data;
	u8 *dg_in_buffer = vmci_dev->data_buffer;
	struct vmci_datagram *dg;
	size_t dg_in_buffer_size = VMCI_MAX_DG_SIZE;
	size_t current_dg_in_buffer_size = PAGE_SIZE;
	size_t remaining_bytes;

	BUILD_BUG_ON(VMCI_MAX_DG_SIZE < PAGE_SIZE);

	ioread8_rep(vmci_dev->iobase + VMCI_DATA_IN_ADDR,
		    vmci_dev->data_buffer, current_dg_in_buffer_size);
	dg = (struct vmci_datagram *)dg_in_buffer;
	remaining_bytes = current_dg_in_buffer_size;

	while (dg->dst.resource != VMCI_INVALID_ID ||
	       remaining_bytes > PAGE_SIZE) {
		unsigned dg_in_size;

		/*
		 * When the input buffer spans multiple pages, a datagram can
		 * start on any page boundary in the buffer.
		 */
		if (dg->dst.resource == VMCI_INVALID_ID) {
			dg = (struct vmci_datagram *)roundup(
				(uintptr_t)dg + 1, PAGE_SIZE);
			remaining_bytes =
				(size_t)(dg_in_buffer +
					 current_dg_in_buffer_size -
					 (u8 *)dg);
			continue;
		}

		dg_in_size = VMCI_DG_SIZE_ALIGNED(dg);

		if (dg_in_size <= dg_in_buffer_size) {
			int result;

			/*
			 * If the remaining bytes in the datagram
			 * buffer doesn't contain the complete
			 * datagram, we first make sure we have enough
			 * room for it and then we read the reminder
			 * of the datagram and possibly any following
			 * datagrams.
			 */
			if (dg_in_size > remaining_bytes) {
				if (remaining_bytes !=
				    current_dg_in_buffer_size) {

					/*
					 * We move the partial
					 * datagram to the front and
					 * read the reminder of the
					 * datagram and possibly
					 * following calls into the
					 * following bytes.
					 */
					memmove(dg_in_buffer, dg_in_buffer +
						current_dg_in_buffer_size -
						remaining_bytes,
						remaining_bytes);
					dg = (struct vmci_datagram *)
					    dg_in_buffer;
				}

				if (current_dg_in_buffer_size !=
				    dg_in_buffer_size)
					current_dg_in_buffer_size =
					    dg_in_buffer_size;

				ioread8_rep(vmci_dev->iobase +
						VMCI_DATA_IN_ADDR,
					vmci_dev->data_buffer +
						remaining_bytes,
					current_dg_in_buffer_size -
						remaining_bytes);
			}

			/*
			 * We special case event datagrams from the
			 * hypervisor.
			 */
			if (dg->src.context == VMCI_HYPERVISOR_CONTEXT_ID &&
			    dg->dst.resource == VMCI_EVENT_HANDLER) {
				result = vmci_event_dispatch(dg);
			} else {
				result = vmci_datagram_invoke_guest_handler(dg);
			}
			if (result < VMCI_SUCCESS)
				dev_dbg(vmci_dev->dev,
					"Datagram with resource (ID=0x%x) failed (err=%d)\n",
					 dg->dst.resource, result);

			/* On to the next datagram. */
			dg = (struct vmci_datagram *)((u8 *)dg +
						      dg_in_size);
		} else {
			size_t bytes_to_skip;

			/*
			 * Datagram doesn't fit in datagram buffer of maximal
			 * size. We drop it.
			 */
			dev_dbg(vmci_dev->dev,
				"Failed to receive datagram (size=%u bytes)\n",
				 dg_in_size);

			bytes_to_skip = dg_in_size - remaining_bytes;
			if (current_dg_in_buffer_size != dg_in_buffer_size)
				current_dg_in_buffer_size = dg_in_buffer_size;

			for (;;) {
				ioread8_rep(vmci_dev->iobase +
						VMCI_DATA_IN_ADDR,
					vmci_dev->data_buffer,
					current_dg_in_buffer_size);
				if (bytes_to_skip <= current_dg_in_buffer_size)
					break;

				bytes_to_skip -= current_dg_in_buffer_size;
			}
			dg = (struct vmci_datagram *)(dg_in_buffer +
						      bytes_to_skip);
		}

		remaining_bytes =
		    (size_t) (dg_in_buffer + current_dg_in_buffer_size -
			      (u8 *)dg);

		if (remaining_bytes < VMCI_DG_HEADERSIZE) {
			/* Get the next batch of datagrams. */

			ioread8_rep(vmci_dev->iobase + VMCI_DATA_IN_ADDR,
				    vmci_dev->data_buffer,
				    current_dg_in_buffer_size);
			dg = (struct vmci_datagram *)dg_in_buffer;
			remaining_bytes = current_dg_in_buffer_size;
		}
	}
}

/*
 * Scans the notification bitmap for raised flags, clears them
 * and handles the notifications.
 */
static void vmci_process_bitmap(unsigned long data)
{
	struct vmci_guest_device *dev = (struct vmci_guest_device *)data;

	if (!dev->notification_bitmap) {
		dev_dbg(dev->dev, "No bitmap present in %s\n", __func__);
		return;
	}

	vmci_dbell_scan_notification_entries(dev->notification_bitmap);
}

/*
 * Interrupt handler for legacy or MSI interrupt, or for first MSI-X
 * interrupt (vector VMCI_INTR_DATAGRAM).
 */
static irqreturn_t vmci_interrupt(int irq, void *_dev)
{
	struct vmci_guest_device *dev = _dev;

	/*
	 * If we are using MSI-X with exclusive vectors then we simply schedule
	 * the datagram tasklet, since we know the interrupt was meant for us.
	 * Otherwise we must read the ICR to determine what to do.
	 */

	if (dev->exclusive_vectors) {
		tasklet_schedule(&dev->datagram_tasklet);
	} else {
		unsigned int icr;

		/* Acknowledge interrupt and determine what needs doing. */
		icr = ioread32(dev->iobase + VMCI_ICR_ADDR);
		if (icr == 0 || icr == ~0)
			return IRQ_NONE;

		if (icr & VMCI_ICR_DATAGRAM) {
			tasklet_schedule(&dev->datagram_tasklet);
			icr &= ~VMCI_ICR_DATAGRAM;
		}

		if (icr & VMCI_ICR_NOTIFICATION) {
			tasklet_schedule(&dev->bm_tasklet);
			icr &= ~VMCI_ICR_NOTIFICATION;
		}

		if (icr != 0)
			dev_warn(dev->dev,
				 "Ignoring unknown interrupt cause (%d)\n",
				 icr);
	}

	return IRQ_HANDLED;
}

/*
 * Interrupt handler for MSI-X interrupt vector VMCI_INTR_NOTIFICATION,
 * which is for the notification bitmap.  Will only get called if we are
 * using MSI-X with exclusive vectors.
 */
static irqreturn_t vmci_interrupt_bm(int irq, void *_dev)
{
	struct vmci_guest_device *dev = _dev;

	/* For MSI-X we can just assume it was meant for us. */
	tasklet_schedule(&dev->bm_tasklet);

	return IRQ_HANDLED;
}

/*
 * Most of the initialization at module load time is done here.
 */
static int vmci_guest_probe_device(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct vmci_guest_device *vmci_dev;
	void __iomem *iobase;
	unsigned int capabilities;
	unsigned long cmd;
	int vmci_err;
	int error;

	dev_dbg(&pdev->dev, "Probing for vmci/PCI guest device\n");

	error = pcim_enable_device(pdev);
	if (error) {
		dev_err(&pdev->dev,
			"Failed to enable VMCI device: %d\n", error);
		return error;
	}

	error = pcim_iomap_regions(pdev, 1 << 0, KBUILD_MODNAME);
	if (error) {
		dev_err(&pdev->dev, "Failed to reserve/map IO regions\n");
		return error;
	}

	iobase = pcim_iomap_table(pdev)[0];

	dev_info(&pdev->dev, "Found VMCI PCI device at %#lx, irq %u\n",
		 (unsigned long)iobase, pdev->irq);

	vmci_dev = devm_kzalloc(&pdev->dev, sizeof(*vmci_dev), GFP_KERNEL);
	if (!vmci_dev) {
		dev_err(&pdev->dev,
			"Can't allocate memory for VMCI device\n");
		return -ENOMEM;
	}

	vmci_dev->dev = &pdev->dev;
	vmci_dev->exclusive_vectors = false;
	vmci_dev->iobase = iobase;

	tasklet_init(&vmci_dev->datagram_tasklet,
		     vmci_dispatch_dgs, (unsigned long)vmci_dev);
	tasklet_init(&vmci_dev->bm_tasklet,
		     vmci_process_bitmap, (unsigned long)vmci_dev);

	vmci_dev->data_buffer = vmalloc(VMCI_MAX_DG_SIZE);
	if (!vmci_dev->data_buffer) {
		dev_err(&pdev->dev,
			"Can't allocate memory for datagram buffer\n");
		return -ENOMEM;
	}

	pci_set_master(pdev);	/* To enable queue_pair functionality. */

	/*
	 * Verify that the VMCI Device supports the capabilities that
	 * we need. If the device is missing capabilities that we would
	 * like to use, check for fallback capabilities and use those
	 * instead (so we can run a new VM on old hosts). Fail the load if
	 * a required capability is missing and there is no fallback.
	 *
	 * Right now, we need datagrams. There are no fallbacks.
	 */
	capabilities = ioread32(vmci_dev->iobase + VMCI_CAPS_ADDR);
	if (!(capabilities & VMCI_CAPS_DATAGRAM)) {
		dev_err(&pdev->dev, "Device does not support datagrams\n");
		error = -ENXIO;
		goto err_free_data_buffer;
	}

	/*
	 * If the hardware supports notifications, we will use that as
	 * well.
	 */
	if (capabilities & VMCI_CAPS_NOTIFICATIONS) {
		vmci_dev->notification_bitmap = dma_alloc_coherent(
			&pdev->dev, PAGE_SIZE, &vmci_dev->notification_base,
			GFP_KERNEL);
		if (!vmci_dev->notification_bitmap) {
			dev_warn(&pdev->dev,
				 "Unable to allocate notification bitmap\n");
		} else {
			memset(vmci_dev->notification_bitmap, 0, PAGE_SIZE);
			capabilities |= VMCI_CAPS_NOTIFICATIONS;
		}
	}

	dev_info(&pdev->dev, "Using capabilities 0x%x\n", capabilities);

	/* Let the host know which capabilities we intend to use. */
	iowrite32(capabilities, vmci_dev->iobase + VMCI_CAPS_ADDR);

	/* Set up global device so that we can start sending datagrams */
	spin_lock_irq(&vmci_dev_spinlock);
	vmci_dev_g = vmci_dev;
	vmci_pdev = pdev;
	spin_unlock_irq(&vmci_dev_spinlock);

	/*
	 * Register notification bitmap with device if that capability is
	 * used.
	 */
	if (capabilities & VMCI_CAPS_NOTIFICATIONS) {
		unsigned long bitmap_ppn =
			vmci_dev->notification_base >> PAGE_SHIFT;
		if (!vmci_dbell_register_notification_bitmap(bitmap_ppn)) {
			dev_warn(&pdev->dev,
				 "VMCI device unable to register notification bitmap with PPN 0x%x\n",
				 (u32) bitmap_ppn);
			error = -ENXIO;
			goto err_remove_vmci_dev_g;
		}
	}

	/* Check host capabilities. */
	error = vmci_check_host_caps(pdev);
	if (error)
		goto err_remove_bitmap;

	/* Enable device. */

	/*
	 * We subscribe to the VMCI_EVENT_CTX_ID_UPDATE here so we can
	 * update the internal context id when needed.
	 */
	vmci_err = vmci_event_subscribe(VMCI_EVENT_CTX_ID_UPDATE,
					vmci_guest_cid_update, NULL,
					&ctx_update_sub_id);
	if (vmci_err < VMCI_SUCCESS)
		dev_warn(&pdev->dev,
			 "Failed to subscribe to event (type=%d): %d\n",
			 VMCI_EVENT_CTX_ID_UPDATE, vmci_err);

	/*
	 * Enable interrupts.  Try MSI-X first, then MSI, and then fallback on
	 * legacy interrupts.
	 */
	error = pci_alloc_irq_vectors(pdev, VMCI_MAX_INTRS, VMCI_MAX_INTRS,
			PCI_IRQ_MSIX);
	if (error < 0) {
		error = pci_alloc_irq_vectors(pdev, 1, 1,
				PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_LEGACY);
		if (error < 0)
			goto err_remove_bitmap;
	} else {
		vmci_dev->exclusive_vectors = true;
	}

	/*
	 * Request IRQ for legacy or MSI interrupts, or for first
	 * MSI-X vector.
	 */
	error = request_irq(pci_irq_vector(pdev, 0), vmci_interrupt,
			    IRQF_SHARED, KBUILD_MODNAME, vmci_dev);
	if (error) {
		dev_err(&pdev->dev, "Irq %u in use: %d\n",
			pci_irq_vector(pdev, 0), error);
		goto err_disable_msi;
	}

	/*
	 * For MSI-X with exclusive vectors we need to request an
	 * interrupt for each vector so that we get a separate
	 * interrupt handler routine.  This allows us to distinguish
	 * between the vectors.
	 */
	if (vmci_dev->exclusive_vectors) {
		error = request_irq(pci_irq_vector(pdev, 1),
				    vmci_interrupt_bm, 0, KBUILD_MODNAME,
				    vmci_dev);
		if (error) {
			dev_err(&pdev->dev,
				"Failed to allocate irq %u: %d\n",
				pci_irq_vector(pdev, 1), error);
			goto err_free_irq;
		}
	}

	dev_dbg(&pdev->dev, "Registered device\n");

	atomic_inc(&vmci_num_guest_devices);

	/* Enable specific interrupt bits. */
	cmd = VMCI_IMR_DATAGRAM;
	if (capabilities & VMCI_CAPS_NOTIFICATIONS)
		cmd |= VMCI_IMR_NOTIFICATION;
	iowrite32(cmd, vmci_dev->iobase + VMCI_IMR_ADDR);

	/* Enable interrupts. */
	iowrite32(VMCI_CONTROL_INT_ENABLE,
		  vmci_dev->iobase + VMCI_CONTROL_ADDR);

	pci_set_drvdata(pdev, vmci_dev);
	return 0;

err_free_irq:
	free_irq(pci_irq_vector(pdev, 0), vmci_dev);
	tasklet_kill(&vmci_dev->datagram_tasklet);
	tasklet_kill(&vmci_dev->bm_tasklet);

err_disable_msi:
	pci_free_irq_vectors(pdev);

	vmci_err = vmci_event_unsubscribe(ctx_update_sub_id);
	if (vmci_err < VMCI_SUCCESS)
		dev_warn(&pdev->dev,
			 "Failed to unsubscribe from event (type=%d) with subscriber (ID=0x%x): %d\n",
			 VMCI_EVENT_CTX_ID_UPDATE, ctx_update_sub_id, vmci_err);

err_remove_bitmap:
	if (vmci_dev->notification_bitmap) {
		iowrite32(VMCI_CONTROL_RESET,
			  vmci_dev->iobase + VMCI_CONTROL_ADDR);
		dma_free_coherent(&pdev->dev, PAGE_SIZE,
				  vmci_dev->notification_bitmap,
				  vmci_dev->notification_base);
	}

err_remove_vmci_dev_g:
	spin_lock_irq(&vmci_dev_spinlock);
	vmci_pdev = NULL;
	vmci_dev_g = NULL;
	spin_unlock_irq(&vmci_dev_spinlock);

err_free_data_buffer:
	vfree(vmci_dev->data_buffer);

	/* The rest are managed resources and will be freed by PCI core */
	return error;
}

static void vmci_guest_remove_device(struct pci_dev *pdev)
{
	struct vmci_guest_device *vmci_dev = pci_get_drvdata(pdev);
	int vmci_err;

	dev_dbg(&pdev->dev, "Removing device\n");

	atomic_dec(&vmci_num_guest_devices);

	vmci_qp_guest_endpoints_exit();

	vmci_err = vmci_event_unsubscribe(ctx_update_sub_id);
	if (vmci_err < VMCI_SUCCESS)
		dev_warn(&pdev->dev,
			 "Failed to unsubscribe from event (type=%d) with subscriber (ID=0x%x): %d\n",
			 VMCI_EVENT_CTX_ID_UPDATE, ctx_update_sub_id, vmci_err);

	spin_lock_irq(&vmci_dev_spinlock);
	vmci_dev_g = NULL;
	vmci_pdev = NULL;
	spin_unlock_irq(&vmci_dev_spinlock);

	dev_dbg(&pdev->dev, "Resetting vmci device\n");
	iowrite32(VMCI_CONTROL_RESET, vmci_dev->iobase + VMCI_CONTROL_ADDR);

	/*
	 * Free IRQ and then disable MSI/MSI-X as appropriate.  For
	 * MSI-X, we might have multiple vectors, each with their own
	 * IRQ, which we must free too.
	 */
	if (vmci_dev->exclusive_vectors)
		free_irq(pci_irq_vector(pdev, 1), vmci_dev);
	free_irq(pci_irq_vector(pdev, 0), vmci_dev);
	pci_free_irq_vectors(pdev);

	tasklet_kill(&vmci_dev->datagram_tasklet);
	tasklet_kill(&vmci_dev->bm_tasklet);

	if (vmci_dev->notification_bitmap) {
		/*
		 * The device reset above cleared the bitmap state of the
		 * device, so we can safely free it here.
		 */

		dma_free_coherent(&pdev->dev, PAGE_SIZE,
				  vmci_dev->notification_bitmap,
				  vmci_dev->notification_base);
	}

	vfree(vmci_dev->data_buffer);

	/* The rest are managed resources and will be freed by PCI core */
}

static const struct pci_device_id vmci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VMWARE, PCI_DEVICE_ID_VMWARE_VMCI), },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, vmci_ids);

static struct pci_driver vmci_guest_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= vmci_ids,
	.probe		= vmci_guest_probe_device,
	.remove		= vmci_guest_remove_device,
};

int __init vmci_guest_init(void)
{
	return pci_register_driver(&vmci_guest_driver);
}

void __exit vmci_guest_exit(void)
{
	pci_unregister_driver(&vmci_guest_driver);
}
