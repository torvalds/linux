// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

/**
 * DOC: Nitro Enclaves (NE) PCI device driver.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nitro_enclaves.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "ne_misc_dev.h"
#include "ne_pci_dev.h"

/**
 * NE_DEFAULT_TIMEOUT_MSECS - Default timeout to wait for a reply from
 *			      the NE PCI device.
 */
#define NE_DEFAULT_TIMEOUT_MSECS	(120000) /* 120 sec */

static const struct pci_device_id ne_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMAZON, PCI_DEVICE_ID_NE) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, ne_pci_ids);

/**
 * ne_submit_request() - Submit command request to the PCI device based on the
 *			 command type.
 * @pdev:		PCI device to send the command to.
 * @cmd_type:		Command type of the request sent to the PCI device.
 * @cmd_request:	Command request payload.
 * @cmd_request_size:	Size of the command request payload.
 *
 * Context: Process context. This function is called with the ne_pci_dev mutex held.
 */
static void ne_submit_request(struct pci_dev *pdev, enum ne_pci_dev_cmd_type cmd_type,
			      void *cmd_request, size_t cmd_request_size)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	memcpy_toio(ne_pci_dev->iomem_base + NE_SEND_DATA, cmd_request, cmd_request_size);

	iowrite32(cmd_type, ne_pci_dev->iomem_base + NE_COMMAND);
}

/**
 * ne_retrieve_reply() - Retrieve reply from the PCI device.
 * @pdev:		PCI device to receive the reply from.
 * @cmd_reply:		Command reply payload.
 * @cmd_reply_size:	Size of the command reply payload.
 *
 * Context: Process context. This function is called with the ne_pci_dev mutex held.
 */
static void ne_retrieve_reply(struct pci_dev *pdev, struct ne_pci_dev_cmd_reply *cmd_reply,
			      size_t cmd_reply_size)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	memcpy_fromio(cmd_reply, ne_pci_dev->iomem_base + NE_RECV_DATA, cmd_reply_size);
}

/**
 * ne_wait_for_reply() - Wait for a reply of a PCI device command.
 * @pdev:	PCI device for which a reply is waited.
 *
 * Context: Process context. This function is called with the ne_pci_dev mutex held.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_wait_for_reply(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);
	int rc = -EINVAL;

	/*
	 * TODO: Update to _interruptible and handle interrupted wait event
	 * e.g. -ERESTARTSYS, incoming signals + update timeout, if needed.
	 */
	rc = wait_event_timeout(ne_pci_dev->cmd_reply_wait_q,
				atomic_read(&ne_pci_dev->cmd_reply_avail) != 0,
				msecs_to_jiffies(NE_DEFAULT_TIMEOUT_MSECS));
	if (!rc)
		return -ETIMEDOUT;

	return 0;
}

int ne_do_request(struct pci_dev *pdev, enum ne_pci_dev_cmd_type cmd_type,
		  void *cmd_request, size_t cmd_request_size,
		  struct ne_pci_dev_cmd_reply *cmd_reply, size_t cmd_reply_size)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);
	int rc = -EINVAL;

	if (cmd_type <= INVALID_CMD || cmd_type >= MAX_CMD) {
		dev_err_ratelimited(&pdev->dev, "Invalid cmd type=%u\n", cmd_type);

		return -EINVAL;
	}

	if (!cmd_request) {
		dev_err_ratelimited(&pdev->dev, "Null cmd request for cmd type=%u\n",
				    cmd_type);

		return -EINVAL;
	}

	if (cmd_request_size > NE_SEND_DATA_SIZE) {
		dev_err_ratelimited(&pdev->dev, "Invalid req size=%zu for cmd type=%u\n",
				    cmd_request_size, cmd_type);

		return -EINVAL;
	}

	if (!cmd_reply) {
		dev_err_ratelimited(&pdev->dev, "Null cmd reply for cmd type=%u\n",
				    cmd_type);

		return -EINVAL;
	}

	if (cmd_reply_size > NE_RECV_DATA_SIZE) {
		dev_err_ratelimited(&pdev->dev, "Invalid reply size=%zu for cmd type=%u\n",
				    cmd_reply_size, cmd_type);

		return -EINVAL;
	}

	/*
	 * Use this mutex so that the PCI device handles one command request at
	 * a time.
	 */
	mutex_lock(&ne_pci_dev->pci_dev_mutex);

	atomic_set(&ne_pci_dev->cmd_reply_avail, 0);

	ne_submit_request(pdev, cmd_type, cmd_request, cmd_request_size);

	rc = ne_wait_for_reply(pdev);
	if (rc < 0) {
		dev_err_ratelimited(&pdev->dev, "Error in wait for reply for cmd type=%u [rc=%d]\n",
				    cmd_type, rc);

		goto unlock_mutex;
	}

	ne_retrieve_reply(pdev, cmd_reply, cmd_reply_size);

	atomic_set(&ne_pci_dev->cmd_reply_avail, 0);

	if (cmd_reply->rc < 0) {
		rc = cmd_reply->rc;

		dev_err_ratelimited(&pdev->dev, "Error in cmd process logic, cmd type=%u [rc=%d]\n",
				    cmd_type, rc);

		goto unlock_mutex;
	}

	rc = 0;

unlock_mutex:
	mutex_unlock(&ne_pci_dev->pci_dev_mutex);

	return rc;
}

/**
 * ne_reply_handler() - Interrupt handler for retrieving a reply matching a
 *			request sent to the PCI device for enclave lifetime
 *			management.
 * @irq:	Received interrupt for a reply sent by the PCI device.
 * @args:	PCI device private data structure.
 *
 * Context: Interrupt context.
 * Return:
 * * IRQ_HANDLED on handled interrupt.
 */
static irqreturn_t ne_reply_handler(int irq, void *args)
{
	struct ne_pci_dev *ne_pci_dev = (struct ne_pci_dev *)args;

	atomic_set(&ne_pci_dev->cmd_reply_avail, 1);

	/* TODO: Update to _interruptible. */
	wake_up(&ne_pci_dev->cmd_reply_wait_q);

	return IRQ_HANDLED;
}

/**
 * ne_event_work_handler() - Work queue handler for notifying enclaves on a
 *			     state change received by the event interrupt
 *			     handler.
 * @work:	Item containing the NE PCI device for which an out-of-band event
 *		was issued.
 *
 * An out-of-band event is being issued by the Nitro Hypervisor when at least
 * one enclave is changing state without client interaction.
 *
 * Context: Work queue context.
 */
static void ne_event_work_handler(struct work_struct *work)
{
	struct ne_pci_dev_cmd_reply cmd_reply = {};
	struct ne_enclave *ne_enclave = NULL;
	struct ne_pci_dev *ne_pci_dev =
		container_of(work, struct ne_pci_dev, notify_work);
	struct pci_dev *pdev = ne_pci_dev->pdev;
	int rc = -EINVAL;
	struct slot_info_req slot_info_req = {};

	mutex_lock(&ne_pci_dev->enclaves_list_mutex);

	/*
	 * Iterate over all enclaves registered for the Nitro Enclaves
	 * PCI device and determine for which enclave(s) the out-of-band event
	 * is corresponding to.
	 */
	list_for_each_entry(ne_enclave, &ne_pci_dev->enclaves_list, enclave_list_entry) {
		mutex_lock(&ne_enclave->enclave_info_mutex);

		/*
		 * Enclaves that were never started cannot receive out-of-band
		 * events.
		 */
		if (ne_enclave->state != NE_STATE_RUNNING)
			goto unlock;

		slot_info_req.slot_uid = ne_enclave->slot_uid;

		rc = ne_do_request(pdev, SLOT_INFO,
				   &slot_info_req, sizeof(slot_info_req),
				   &cmd_reply, sizeof(cmd_reply));
		if (rc < 0)
			dev_err(&pdev->dev, "Error in slot info [rc=%d]\n", rc);

		/* Notify enclave process that the enclave state changed. */
		if (ne_enclave->state != cmd_reply.state) {
			ne_enclave->state = cmd_reply.state;

			ne_enclave->has_event = true;

			wake_up_interruptible(&ne_enclave->eventq);
		}

unlock:
		 mutex_unlock(&ne_enclave->enclave_info_mutex);
	}

	mutex_unlock(&ne_pci_dev->enclaves_list_mutex);
}

/**
 * ne_event_handler() - Interrupt handler for PCI device out-of-band events.
 *			This interrupt does not supply any data in the MMIO
 *			region. It notifies a change in the state of any of
 *			the launched enclaves.
 * @irq:	Received interrupt for an out-of-band event.
 * @args:	PCI device private data structure.
 *
 * Context: Interrupt context.
 * Return:
 * * IRQ_HANDLED on handled interrupt.
 */
static irqreturn_t ne_event_handler(int irq, void *args)
{
	struct ne_pci_dev *ne_pci_dev = (struct ne_pci_dev *)args;

	queue_work(ne_pci_dev->event_wq, &ne_pci_dev->notify_work);

	return IRQ_HANDLED;
}

/**
 * ne_setup_msix() - Setup MSI-X vectors for the PCI device.
 * @pdev:	PCI device to setup the MSI-X for.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_setup_msix(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);
	int nr_vecs = 0;
	int rc = -EINVAL;

	nr_vecs = pci_msix_vec_count(pdev);
	if (nr_vecs < 0) {
		rc = nr_vecs;

		dev_err(&pdev->dev, "Error in getting vec count [rc=%d]\n", rc);

		return rc;
	}

	rc = pci_alloc_irq_vectors(pdev, nr_vecs, nr_vecs, PCI_IRQ_MSIX);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in alloc MSI-X vecs [rc=%d]\n", rc);

		return rc;
	}

	/*
	 * This IRQ gets triggered every time the PCI device responds to a
	 * command request. The reply is then retrieved, reading from the MMIO
	 * space of the PCI device.
	 */
	rc = request_irq(pci_irq_vector(pdev, NE_VEC_REPLY), ne_reply_handler,
			 0, "enclave_cmd", ne_pci_dev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in request irq reply [rc=%d]\n", rc);

		goto free_irq_vectors;
	}

	ne_pci_dev->event_wq = create_singlethread_workqueue("ne_pci_dev_wq");
	if (!ne_pci_dev->event_wq) {
		rc = -ENOMEM;

		dev_err(&pdev->dev, "Cannot get wq for dev events [rc=%d]\n", rc);

		goto free_reply_irq_vec;
	}

	INIT_WORK(&ne_pci_dev->notify_work, ne_event_work_handler);

	/*
	 * This IRQ gets triggered every time any enclave's state changes. Its
	 * handler then scans for the changes and propagates them to the user
	 * space.
	 */
	rc = request_irq(pci_irq_vector(pdev, NE_VEC_EVENT), ne_event_handler,
			 0, "enclave_evt", ne_pci_dev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in request irq event [rc=%d]\n", rc);

		goto destroy_wq;
	}

	return 0;

destroy_wq:
	destroy_workqueue(ne_pci_dev->event_wq);
free_reply_irq_vec:
	free_irq(pci_irq_vector(pdev, NE_VEC_REPLY), ne_pci_dev);
free_irq_vectors:
	pci_free_irq_vectors(pdev);

	return rc;
}

/**
 * ne_teardown_msix() - Teardown MSI-X vectors for the PCI device.
 * @pdev:	PCI device to teardown the MSI-X for.
 *
 * Context: Process context.
 */
static void ne_teardown_msix(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	free_irq(pci_irq_vector(pdev, NE_VEC_EVENT), ne_pci_dev);

	flush_work(&ne_pci_dev->notify_work);
	flush_workqueue(ne_pci_dev->event_wq);
	destroy_workqueue(ne_pci_dev->event_wq);

	free_irq(pci_irq_vector(pdev, NE_VEC_REPLY), ne_pci_dev);

	pci_free_irq_vectors(pdev);
}

/**
 * ne_pci_dev_enable() - Select the PCI device version and enable it.
 * @pdev:	PCI device to select version for and then enable.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_pci_dev_enable(struct pci_dev *pdev)
{
	u8 dev_enable_reply = 0;
	u16 dev_version_reply = 0;
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	iowrite16(NE_VERSION_MAX, ne_pci_dev->iomem_base + NE_VERSION);

	dev_version_reply = ioread16(ne_pci_dev->iomem_base + NE_VERSION);
	if (dev_version_reply != NE_VERSION_MAX) {
		dev_err(&pdev->dev, "Error in pci dev version cmd\n");

		return -EIO;
	}

	iowrite8(NE_ENABLE_ON, ne_pci_dev->iomem_base + NE_ENABLE);

	dev_enable_reply = ioread8(ne_pci_dev->iomem_base + NE_ENABLE);
	if (dev_enable_reply != NE_ENABLE_ON) {
		dev_err(&pdev->dev, "Error in pci dev enable cmd\n");

		return -EIO;
	}

	return 0;
}

/**
 * ne_pci_dev_disable() - Disable the PCI device.
 * @pdev:	PCI device to disable.
 *
 * Context: Process context.
 */
static void ne_pci_dev_disable(struct pci_dev *pdev)
{
	u8 dev_disable_reply = 0;
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);
	const unsigned int sleep_time = 10; /* 10 ms */
	unsigned int sleep_time_count = 0;

	iowrite8(NE_ENABLE_OFF, ne_pci_dev->iomem_base + NE_ENABLE);

	/*
	 * Check for NE_ENABLE_OFF in a loop, to handle cases when the device
	 * state is not immediately set to disabled and going through a
	 * transitory state of disabling.
	 */
	while (sleep_time_count < NE_DEFAULT_TIMEOUT_MSECS) {
		dev_disable_reply = ioread8(ne_pci_dev->iomem_base + NE_ENABLE);
		if (dev_disable_reply == NE_ENABLE_OFF)
			return;

		msleep_interruptible(sleep_time);
		sleep_time_count += sleep_time;
	}

	dev_disable_reply = ioread8(ne_pci_dev->iomem_base + NE_ENABLE);
	if (dev_disable_reply != NE_ENABLE_OFF)
		dev_err(&pdev->dev, "Error in pci dev disable cmd\n");
}

/**
 * ne_pci_probe() - Probe function for the NE PCI device.
 * @pdev:	PCI device to match with the NE PCI driver.
 * @id :	PCI device id table associated with the NE PCI driver.
 *
 * Context: Process context.
 * Return:
 * * 0 on success.
 * * Negative return value on failure.
 */
static int ne_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ne_pci_dev *ne_pci_dev = NULL;
	int rc = -EINVAL;

	ne_pci_dev = kzalloc(sizeof(*ne_pci_dev), GFP_KERNEL);
	if (!ne_pci_dev)
		return -ENOMEM;

	rc = pci_enable_device(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in pci dev enable [rc=%d]\n", rc);

		goto free_ne_pci_dev;
	}

	pci_set_master(pdev);

	rc = pci_request_regions_exclusive(pdev, "nitro_enclaves");
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in pci request regions [rc=%d]\n", rc);

		goto disable_pci_dev;
	}

	ne_pci_dev->iomem_base = pci_iomap(pdev, PCI_BAR_NE, 0);
	if (!ne_pci_dev->iomem_base) {
		rc = -ENOMEM;

		dev_err(&pdev->dev, "Error in pci iomap [rc=%d]\n", rc);

		goto release_pci_regions;
	}

	pci_set_drvdata(pdev, ne_pci_dev);

	rc = ne_setup_msix(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in pci dev msix setup [rc=%d]\n", rc);

		goto iounmap_pci_bar;
	}

	ne_pci_dev_disable(pdev);

	rc = ne_pci_dev_enable(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in ne_pci_dev enable [rc=%d]\n", rc);

		goto teardown_msix;
	}

	atomic_set(&ne_pci_dev->cmd_reply_avail, 0);
	init_waitqueue_head(&ne_pci_dev->cmd_reply_wait_q);
	INIT_LIST_HEAD(&ne_pci_dev->enclaves_list);
	mutex_init(&ne_pci_dev->enclaves_list_mutex);
	mutex_init(&ne_pci_dev->pci_dev_mutex);
	ne_pci_dev->pdev = pdev;

	ne_devs.ne_pci_dev = ne_pci_dev;

	rc = misc_register(ne_devs.ne_misc_dev);
	if (rc < 0) {
		dev_err(&pdev->dev, "Error in misc dev register [rc=%d]\n", rc);

		goto disable_ne_pci_dev;
	}

	return 0;

disable_ne_pci_dev:
	ne_devs.ne_pci_dev = NULL;
	ne_pci_dev_disable(pdev);
teardown_msix:
	ne_teardown_msix(pdev);
iounmap_pci_bar:
	pci_set_drvdata(pdev, NULL);
	pci_iounmap(pdev, ne_pci_dev->iomem_base);
release_pci_regions:
	pci_release_regions(pdev);
disable_pci_dev:
	pci_disable_device(pdev);
free_ne_pci_dev:
	kfree(ne_pci_dev);

	return rc;
}

/**
 * ne_pci_remove() - Remove function for the NE PCI device.
 * @pdev:	PCI device associated with the NE PCI driver.
 *
 * Context: Process context.
 */
static void ne_pci_remove(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	misc_deregister(ne_devs.ne_misc_dev);

	ne_devs.ne_pci_dev = NULL;

	ne_pci_dev_disable(pdev);

	ne_teardown_msix(pdev);

	pci_set_drvdata(pdev, NULL);

	pci_iounmap(pdev, ne_pci_dev->iomem_base);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	kfree(ne_pci_dev);
}

/**
 * ne_pci_shutdown() - Shutdown function for the NE PCI device.
 * @pdev:	PCI device associated with the NE PCI driver.
 *
 * Context: Process context.
 */
static void ne_pci_shutdown(struct pci_dev *pdev)
{
	struct ne_pci_dev *ne_pci_dev = pci_get_drvdata(pdev);

	if (!ne_pci_dev)
		return;

	misc_deregister(ne_devs.ne_misc_dev);

	ne_devs.ne_pci_dev = NULL;

	ne_pci_dev_disable(pdev);

	ne_teardown_msix(pdev);

	pci_set_drvdata(pdev, NULL);

	pci_iounmap(pdev, ne_pci_dev->iomem_base);

	pci_release_regions(pdev);

	pci_disable_device(pdev);

	kfree(ne_pci_dev);
}

/*
 * TODO: Add suspend / resume functions for power management w/ CONFIG_PM, if
 * needed.
 */
/* NE PCI device driver. */
struct pci_driver ne_pci_driver = {
	.name		= "nitro_enclaves",
	.id_table	= ne_pci_ids,
	.probe		= ne_pci_probe,
	.remove		= ne_pci_remove,
	.shutdown	= ne_pci_shutdown,
};
