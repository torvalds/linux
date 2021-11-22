// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel ECLite opregion driver for talking to ECLite firmware running on
 * Intel Integrated Sensor Hub (ISH) using ISH Transport Protocol (ISHTP)
 *
 * Copyright (c) 2021, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/intel-ish-client-if.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/uaccess.h>

#define ECLITE_DATA_OPREGION_ID	0x9E
#define ECLITE_CMD_OPREGION_ID	0x9F

#define ECL_MSG_DATA	0x1
#define ECL_MSG_EVENT	0x2

#define ECL_ISH_READ	0x1
#define ECL_ISH_WRITE	0x2
#define ECL_ISH_HEADER_VERSION	0

#define ECL_CL_RX_RING_SIZE	16
#define ECL_CL_TX_RING_SIZE	8

#define ECL_DATA_OPR_BUFLEN	384
#define ECL_EVENTS_NOTIFY	333

#define cmd_opr_offsetof(element)	offsetof(struct opregion_cmd, element)
#define cl_data_to_dev(opr_dev)	ishtp_device((opr_dev)->cl_device)

#ifndef BITS_TO_BYTES
#define BITS_TO_BYTES(x) ((x) / 8)
#endif

struct opregion_cmd {
	unsigned int command;
	unsigned int offset;
	unsigned int length;
	unsigned int event_id;
};

struct opregion_data {
	char data[ECL_DATA_OPR_BUFLEN];
};

struct opregion_context {
	struct opregion_cmd cmd_area;
	struct opregion_data data_area;
};

struct ecl_message_header {
	unsigned int version:2;
	unsigned int data_type:2;
	unsigned int request_type:2;
	unsigned int offset:9;
	unsigned int data_len:9;
	unsigned int event:8;
};

struct ecl_message {
	struct ecl_message_header header;
	char payload[ECL_DATA_OPR_BUFLEN];
};

struct ishtp_opregion_dev {
	struct opregion_context opr_context;
	struct ishtp_cl *ecl_ishtp_cl;
	struct ishtp_cl_device *cl_device;
	struct ishtp_fw_client *fw_client;
	struct ishtp_cl_rb *rb;
	struct acpi_device *adev;
	unsigned int dsm_event_id;
	unsigned int ish_link_ready;
	unsigned int ish_read_done;
	unsigned int acpi_init_done;
	wait_queue_head_t read_wait;
	struct work_struct event_work;
	struct work_struct reset_work;
	/* lock for opregion context */
	struct mutex lock;

};

/* eclite ishtp client UUID: 6a19cc4b-d760-4de3-b14d-f25ebd0fbcd9 */
static const guid_t ecl_ishtp_guid =
	GUID_INIT(0x6a19cc4b, 0xd760, 0x4de3,
		  0xb1, 0x4d, 0xf2, 0x5e, 0xbd, 0xf, 0xbc, 0xd9);

/* ACPI DSM UUID: 91d936a7-1f01-49c6-a6b4-72f00ad8d8a5 */
static const guid_t ecl_acpi_guid =
	GUID_INIT(0x91d936a7, 0x1f01, 0x49c6, 0xa6,
		  0xb4, 0x72, 0xf0, 0x0a, 0xd8, 0xd8, 0xa5);

/**
 * ecl_ish_cl_read() - Read data from eclite FW
 *
 * @opr_dev:  pointer to opregion device
 *
 * This function issues a read request to eclite FW and waits until it
 * receives a response. When response is received the read data is copied to
 * opregion buffer.
 */
static int ecl_ish_cl_read(struct ishtp_opregion_dev *opr_dev)
{
	struct ecl_message_header header;
	int len, rv;

	if (!opr_dev->ish_link_ready)
		return -EIO;

	if ((opr_dev->opr_context.cmd_area.offset +
	     opr_dev->opr_context.cmd_area.length) > ECL_DATA_OPR_BUFLEN) {
		return -EINVAL;
	}

	header.version = ECL_ISH_HEADER_VERSION;
	header.data_type = ECL_MSG_DATA;
	header.request_type = ECL_ISH_READ;
	header.offset = opr_dev->opr_context.cmd_area.offset;
	header.data_len = opr_dev->opr_context.cmd_area.length;
	header.event = opr_dev->opr_context.cmd_area.event_id;
	len = sizeof(header);

	opr_dev->ish_read_done = false;
	rv = ishtp_cl_send(opr_dev->ecl_ishtp_cl, (uint8_t *)&header, len);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev), "ish-read : send failed\n");
		return -EIO;
	}

	dev_dbg(cl_data_to_dev(opr_dev),
		"[ish_rd] Req: off : %x, len : %x\n",
		header.offset,
		header.data_len);

	rv = wait_event_interruptible_timeout(opr_dev->read_wait,
					      opr_dev->ish_read_done,
					      2 * HZ);
	if (!rv) {
		dev_err(cl_data_to_dev(opr_dev),
			"[ish_rd] No response from firmware\n");
		return -EIO;
	}

	return 0;
}

/**
 * ecl_ish_cl_write() - This function writes data to eclite FW.
 *
 * @opr_dev:  pointer to opregion device
 *
 * This function writes data to eclite FW.
 */
static int ecl_ish_cl_write(struct ishtp_opregion_dev *opr_dev)
{
	struct ecl_message message;
	int len;

	if (!opr_dev->ish_link_ready)
		return -EIO;

	if ((opr_dev->opr_context.cmd_area.offset +
	     opr_dev->opr_context.cmd_area.length) > ECL_DATA_OPR_BUFLEN) {
		return -EINVAL;
	}

	message.header.version = ECL_ISH_HEADER_VERSION;
	message.header.data_type = ECL_MSG_DATA;
	message.header.request_type = ECL_ISH_WRITE;
	message.header.offset = opr_dev->opr_context.cmd_area.offset;
	message.header.data_len = opr_dev->opr_context.cmd_area.length;
	message.header.event = opr_dev->opr_context.cmd_area.event_id;
	len = sizeof(struct ecl_message_header) + message.header.data_len;

	memcpy(message.payload,
	       opr_dev->opr_context.data_area.data + message.header.offset,
	       message.header.data_len);

	dev_dbg(cl_data_to_dev(opr_dev),
		"[ish_wr] off : %x, len : %x\n",
		message.header.offset,
		message.header.data_len);

	return ishtp_cl_send(opr_dev->ecl_ishtp_cl, (uint8_t *)&message, len);
}

static acpi_status
ecl_opregion_cmd_handler(u32 function, acpi_physical_address address,
			 u32 bits, u64 *value64,
			 void *handler_context, void *region_context)
{
	struct ishtp_opregion_dev *opr_dev;
	struct opregion_cmd *cmd;
	acpi_status status = AE_OK;

	if (!region_context || !value64)
		return AE_BAD_PARAMETER;

	if (function == ACPI_READ)
		return AE_ERROR;

	opr_dev = (struct ishtp_opregion_dev *)region_context;

	mutex_lock(&opr_dev->lock);

	cmd = &opr_dev->opr_context.cmd_area;

	switch (address) {
	case cmd_opr_offsetof(command):
		cmd->command = (u32)*value64;

		if (cmd->command == ECL_ISH_READ)
			status =  ecl_ish_cl_read(opr_dev);
		else if (cmd->command == ECL_ISH_WRITE)
			status = ecl_ish_cl_write(opr_dev);
		else
			status = AE_ERROR;
		break;
	case cmd_opr_offsetof(offset):
		cmd->offset = (u32)*value64;
		break;
	case cmd_opr_offsetof(length):
		cmd->length = (u32)*value64;
		break;
	case cmd_opr_offsetof(event_id):
		cmd->event_id = (u32)*value64;
		break;
	default:
		status = AE_ERROR;
	}

	mutex_unlock(&opr_dev->lock);

	return status;
}

static acpi_status
ecl_opregion_data_handler(u32 function, acpi_physical_address address,
			  u32 bits, u64 *value64,
			  void *handler_context, void *region_context)
{
	struct ishtp_opregion_dev *opr_dev;
	unsigned int bytes = BITS_TO_BYTES(bits);
	void *data_addr;

	if (!region_context || !value64)
		return AE_BAD_PARAMETER;

	if (address + bytes > ECL_DATA_OPR_BUFLEN)
		return AE_BAD_PARAMETER;

	opr_dev = (struct ishtp_opregion_dev *)region_context;

	mutex_lock(&opr_dev->lock);

	data_addr = &opr_dev->opr_context.data_area.data[address];

	if (function == ACPI_READ) {
		memcpy(value64, data_addr, bytes);
	} else if (function == ACPI_WRITE) {
		memcpy(data_addr, value64, bytes);
	} else {
		mutex_unlock(&opr_dev->lock);
		return AE_BAD_PARAMETER;
	}

	mutex_unlock(&opr_dev->lock);

	return AE_OK;
}

static int acpi_find_eclite_device(struct ishtp_opregion_dev *opr_dev)
{
	struct acpi_device *adev;

	/* Find ECLite device and save reference */
	adev = acpi_dev_get_first_match_dev("INTC1035", NULL, -1);
	if (!adev) {
		dev_err(cl_data_to_dev(opr_dev), "eclite ACPI device not found\n");
		return -ENODEV;
	}

	opr_dev->adev = adev;

	return 0;
}

static int acpi_opregion_init(struct ishtp_opregion_dev *opr_dev)
{
	acpi_status status;

	status = acpi_install_address_space_handler(opr_dev->adev->handle,
						    ECLITE_CMD_OPREGION_ID,
						    ecl_opregion_cmd_handler,
						    NULL, opr_dev);
	if (ACPI_FAILURE(status)) {
		dev_err(cl_data_to_dev(opr_dev),
			"cmd space handler install failed\n");
		return -ENODEV;
	}

	status = acpi_install_address_space_handler(opr_dev->adev->handle,
						    ECLITE_DATA_OPREGION_ID,
						    ecl_opregion_data_handler,
						    NULL, opr_dev);
	if (ACPI_FAILURE(status)) {
		dev_err(cl_data_to_dev(opr_dev),
			"data space handler install failed\n");

		acpi_remove_address_space_handler(opr_dev->adev->handle,
						  ECLITE_CMD_OPREGION_ID,
						  ecl_opregion_cmd_handler);
		return -ENODEV;
	}
	opr_dev->acpi_init_done = true;

	dev_dbg(cl_data_to_dev(opr_dev), "Opregion handlers are installed\n");

	return 0;
}

static void acpi_opregion_deinit(struct ishtp_opregion_dev *opr_dev)
{
	acpi_remove_address_space_handler(opr_dev->adev->handle,
					  ECLITE_CMD_OPREGION_ID,
					  ecl_opregion_cmd_handler);

	acpi_remove_address_space_handler(opr_dev->adev->handle,
					  ECLITE_DATA_OPREGION_ID,
					  ecl_opregion_data_handler);
	opr_dev->acpi_init_done = false;
}

static void ecl_acpi_invoke_dsm(struct work_struct *work)
{
	struct ishtp_opregion_dev *opr_dev;
	union acpi_object *obj;

	opr_dev = container_of(work, struct ishtp_opregion_dev, event_work);
	if (!opr_dev->acpi_init_done)
		return;

	obj = acpi_evaluate_dsm(opr_dev->adev->handle, &ecl_acpi_guid, 0,
				opr_dev->dsm_event_id, NULL);
	if (!obj) {
		dev_warn(cl_data_to_dev(opr_dev), "_DSM fn call failed\n");
		return;
	}

	dev_dbg(cl_data_to_dev(opr_dev), "Exec DSM function code: %d success\n",
		opr_dev->dsm_event_id);

	ACPI_FREE(obj);
}

static void ecl_ish_process_rx_data(struct ishtp_opregion_dev *opr_dev)
{
	struct ecl_message *message =
		(struct ecl_message *)opr_dev->rb->buffer.data;

	dev_dbg(cl_data_to_dev(opr_dev),
		"[ish_rd] Resp: off : %x, len : %x\n",
		message->header.offset,
		message->header.data_len);

	if ((message->header.offset + message->header.data_len) >
			ECL_DATA_OPR_BUFLEN) {
		return;
	}

	memcpy(opr_dev->opr_context.data_area.data + message->header.offset,
	       message->payload, message->header.data_len);

	opr_dev->ish_read_done = true;
	wake_up_interruptible(&opr_dev->read_wait);
}

static void ecl_ish_process_rx_event(struct ishtp_opregion_dev *opr_dev)
{
	struct ecl_message_header *header =
		(struct ecl_message_header *)opr_dev->rb->buffer.data;

	dev_dbg(cl_data_to_dev(opr_dev),
		"[ish_ev] Evt received: %8x\n", header->event);

	opr_dev->dsm_event_id = header->event;
	schedule_work(&opr_dev->event_work);
}

static int ecl_ish_cl_enable_events(struct ishtp_opregion_dev *opr_dev,
				    bool config_enable)
{
	struct ecl_message message;
	int len;

	message.header.version = ECL_ISH_HEADER_VERSION;
	message.header.data_type = ECL_MSG_DATA;
	message.header.request_type = ECL_ISH_WRITE;
	message.header.offset = ECL_EVENTS_NOTIFY;
	message.header.data_len = 1;
	message.payload[0] = config_enable;

	len = sizeof(struct ecl_message_header) + message.header.data_len;

	return ishtp_cl_send(opr_dev->ecl_ishtp_cl, (uint8_t *)&message, len);
}

static void ecl_ishtp_cl_event_cb(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *ecl_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_opregion_dev *opr_dev;
	struct ecl_message_header *header;
	struct ishtp_cl_rb *rb;

	opr_dev = ishtp_get_client_data(ecl_ishtp_cl);
	while ((rb = ishtp_cl_rx_get_rb(opr_dev->ecl_ishtp_cl)) != NULL) {
		opr_dev->rb = rb;
		header = (struct ecl_message_header *)rb->buffer.data;

		if (header->data_type == ECL_MSG_DATA)
			ecl_ish_process_rx_data(opr_dev);
		else if (header->data_type == ECL_MSG_EVENT)
			ecl_ish_process_rx_event(opr_dev);
		else
			/* Got an event with wrong data_type, ignore it */
			dev_err(cl_data_to_dev(opr_dev),
				"[ish_cb] Received wrong data_type\n");

		ishtp_cl_io_rb_recycle(rb);
	}
}

static int ecl_ishtp_cl_init(struct ishtp_cl *ecl_ishtp_cl)
{
	struct ishtp_opregion_dev *opr_dev =
		ishtp_get_client_data(ecl_ishtp_cl);
	struct ishtp_fw_client *fw_client;
	struct ishtp_device *dev;
	int rv;

	rv = ishtp_cl_link(ecl_ishtp_cl);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev), "ishtp_cl_link failed\n");
		return	rv;
	}

	dev = ishtp_get_ishtp_device(ecl_ishtp_cl);

	/* Connect to FW client */
	ishtp_set_tx_ring_size(ecl_ishtp_cl, ECL_CL_TX_RING_SIZE);
	ishtp_set_rx_ring_size(ecl_ishtp_cl, ECL_CL_RX_RING_SIZE);

	fw_client = ishtp_fw_cl_get_client(dev, &ecl_ishtp_guid);
	if (!fw_client) {
		dev_err(cl_data_to_dev(opr_dev), "fw client not found\n");
		return -ENOENT;
	}

	ishtp_cl_set_fw_client_id(ecl_ishtp_cl,
				  ishtp_get_fw_client_id(fw_client));

	ishtp_set_connection_state(ecl_ishtp_cl, ISHTP_CL_CONNECTING);

	rv = ishtp_cl_connect(ecl_ishtp_cl);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev), "client connect failed\n");

		ishtp_cl_unlink(ecl_ishtp_cl);
		return rv;
	}

	dev_dbg(cl_data_to_dev(opr_dev), "Host connected to fw client\n");

	return 0;
}

static void ecl_ishtp_cl_deinit(struct ishtp_cl *ecl_ishtp_cl)
{
	ishtp_cl_unlink(ecl_ishtp_cl);
	ishtp_cl_flush_queues(ecl_ishtp_cl);
	ishtp_cl_free(ecl_ishtp_cl);
}

static void ecl_ishtp_cl_reset_handler(struct work_struct *work)
{
	struct ishtp_opregion_dev *opr_dev;
	struct ishtp_cl_device *cl_device;
	struct ishtp_cl *ecl_ishtp_cl;
	int rv;
	int retry;

	opr_dev = container_of(work, struct ishtp_opregion_dev, reset_work);

	opr_dev->ish_link_ready = false;

	cl_device = opr_dev->cl_device;
	ecl_ishtp_cl = opr_dev->ecl_ishtp_cl;

	ecl_ishtp_cl_deinit(ecl_ishtp_cl);

	ecl_ishtp_cl = ishtp_cl_allocate(cl_device);
	if (!ecl_ishtp_cl)
		return;

	ishtp_set_drvdata(cl_device, ecl_ishtp_cl);
	ishtp_set_client_data(ecl_ishtp_cl, opr_dev);

	opr_dev->ecl_ishtp_cl = ecl_ishtp_cl;

	for (retry = 0; retry < 3; ++retry) {
		rv = ecl_ishtp_cl_init(ecl_ishtp_cl);
		if (!rv)
			break;
	}
	if (rv) {
		ishtp_cl_free(ecl_ishtp_cl);
		opr_dev->ecl_ishtp_cl = NULL;
		dev_err(cl_data_to_dev(opr_dev),
			"[ish_rst] Reset failed. Link not ready.\n");
		return;
	}

	ishtp_register_event_cb(cl_device, ecl_ishtp_cl_event_cb);
	dev_info(cl_data_to_dev(opr_dev),
		 "[ish_rst] Reset Success. Link ready.\n");

	opr_dev->ish_link_ready = true;

	if (opr_dev->acpi_init_done)
		return;

	rv = acpi_opregion_init(opr_dev);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev),
			"ACPI opregion init failed\n");
	}
}

static int ecl_ishtp_cl_probe(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *ecl_ishtp_cl;
	struct ishtp_opregion_dev *opr_dev;
	int rv;

	opr_dev = devm_kzalloc(ishtp_device(cl_device), sizeof(*opr_dev),
			       GFP_KERNEL);
	if (!opr_dev)
		return -ENOMEM;

	ecl_ishtp_cl = ishtp_cl_allocate(cl_device);
	if (!ecl_ishtp_cl)
		return -ENOMEM;

	ishtp_set_drvdata(cl_device, ecl_ishtp_cl);
	ishtp_set_client_data(ecl_ishtp_cl, opr_dev);
	opr_dev->ecl_ishtp_cl = ecl_ishtp_cl;
	opr_dev->cl_device = cl_device;

	init_waitqueue_head(&opr_dev->read_wait);
	INIT_WORK(&opr_dev->event_work, ecl_acpi_invoke_dsm);
	INIT_WORK(&opr_dev->reset_work, ecl_ishtp_cl_reset_handler);

	/* Initialize ish client device */
	rv = ecl_ishtp_cl_init(ecl_ishtp_cl);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev), "Client init failed\n");
		goto err_exit;
	}

	dev_dbg(cl_data_to_dev(opr_dev), "eclite-ishtp client initialised\n");

	opr_dev->ish_link_ready = true;
	mutex_init(&opr_dev->lock);

	rv = acpi_find_eclite_device(opr_dev);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev), "ECLite ACPI ID not found\n");
		goto err_exit;
	}

	/* Register a handler for eclite fw events */
	ishtp_register_event_cb(cl_device, ecl_ishtp_cl_event_cb);

	/* Now init opregion handlers */
	rv = acpi_opregion_init(opr_dev);
	if (rv) {
		dev_err(cl_data_to_dev(opr_dev), "ACPI opregion init failed\n");
		goto err_exit;
	}

	/* Reprobe devices depending on ECLite - battery, fan, etc. */
	acpi_dev_clear_dependencies(opr_dev->adev);

	return 0;
err_exit:
	ishtp_set_connection_state(ecl_ishtp_cl, ISHTP_CL_DISCONNECTING);
	ishtp_cl_disconnect(ecl_ishtp_cl);
	ecl_ishtp_cl_deinit(ecl_ishtp_cl);

	return rv;
}

static void ecl_ishtp_cl_remove(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *ecl_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_opregion_dev *opr_dev =
		ishtp_get_client_data(ecl_ishtp_cl);

	if (opr_dev->acpi_init_done)
		acpi_opregion_deinit(opr_dev);

	acpi_dev_put(opr_dev->adev);

	ishtp_set_connection_state(ecl_ishtp_cl, ISHTP_CL_DISCONNECTING);
	ishtp_cl_disconnect(ecl_ishtp_cl);
	ecl_ishtp_cl_deinit(ecl_ishtp_cl);

	cancel_work_sync(&opr_dev->reset_work);
	cancel_work_sync(&opr_dev->event_work);
}

static int ecl_ishtp_cl_reset(struct ishtp_cl_device *cl_device)
{
	struct ishtp_cl *ecl_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_opregion_dev *opr_dev =
		ishtp_get_client_data(ecl_ishtp_cl);

	schedule_work(&opr_dev->reset_work);

	return 0;
}

static int ecl_ishtp_cl_suspend(struct device *device)
{
	struct ishtp_cl_device *cl_device = ishtp_dev_to_cl_device(device);
	struct ishtp_cl *ecl_ishtp_cl = ishtp_get_drvdata(cl_device);
	struct ishtp_opregion_dev *opr_dev =
		ishtp_get_client_data(ecl_ishtp_cl);

	if (acpi_target_system_state() == ACPI_STATE_S0)
		return 0;

	acpi_opregion_deinit(opr_dev);
	ecl_ish_cl_enable_events(opr_dev, false);

	return 0;
}

static int ecl_ishtp_cl_resume(struct device *device)
{
	/* A reset is expected to call after an Sx. At this point
	 * we are not sure if the link is up or not to restore anything,
	 * so do nothing in resume path
	 */
	return 0;
}

static const struct dev_pm_ops ecl_ishtp_pm_ops = {
	.suspend = ecl_ishtp_cl_suspend,
	.resume = ecl_ishtp_cl_resume,
};

static struct ishtp_cl_driver ecl_ishtp_cl_driver = {
	.name = "ishtp-eclite",
	.guid = &ecl_ishtp_guid,
	.probe = ecl_ishtp_cl_probe,
	.remove = ecl_ishtp_cl_remove,
	.reset = ecl_ishtp_cl_reset,
	.driver.pm = &ecl_ishtp_pm_ops,
};

static int __init ecl_ishtp_init(void)
{
	return ishtp_cl_driver_register(&ecl_ishtp_cl_driver, THIS_MODULE);
}

static void __exit ecl_ishtp_exit(void)
{
	return ishtp_cl_driver_unregister(&ecl_ishtp_cl_driver);
}

late_initcall(ecl_ishtp_init);
module_exit(ecl_ishtp_exit);

MODULE_DESCRIPTION("ISH ISHTP eclite client opregion driver");
MODULE_AUTHOR("K Naduvalath, Sumesh <sumesh.k.naduvalath@intel.com>");

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("ishtp:*");
