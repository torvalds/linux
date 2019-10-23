// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2013 Andrew Duggan <aduggan@synaptics.com>
 *  Copyright (c) 2013 Synaptics Incorporated
 *  Copyright (c) 2014 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2014 Red Hat, Inc
 */

#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/rmi.h>
#include "hid-ids.h"

#define RMI_MOUSE_REPORT_ID		0x01 /* Mouse emulation Report */
#define RMI_WRITE_REPORT_ID		0x09 /* Output Report */
#define RMI_READ_ADDR_REPORT_ID		0x0a /* Output Report */
#define RMI_READ_DATA_REPORT_ID		0x0b /* Input Report */
#define RMI_ATTN_REPORT_ID		0x0c /* Input Report */
#define RMI_SET_RMI_MODE_REPORT_ID	0x0f /* Feature Report */

/* flags */
#define RMI_READ_REQUEST_PENDING	0
#define RMI_READ_DATA_PENDING		1
#define RMI_STARTED			2

/* device flags */
#define RMI_DEVICE			BIT(0)
#define RMI_DEVICE_HAS_PHYS_BUTTONS	BIT(1)
#define RMI_DEVICE_OUTPUT_SET_REPORT	BIT(2)

/*
 * retrieve the ctrl registers
 * the ctrl register has a size of 20 but a fw bug split it into 16 + 4,
 * and there is no way to know if the first 20 bytes are here or not.
 * We use only the first 12 bytes, so get only them.
 */
#define RMI_F11_CTRL_REG_COUNT		12

enum rmi_mode_type {
	RMI_MODE_OFF			= 0,
	RMI_MODE_ATTN_REPORTS		= 1,
	RMI_MODE_NO_PACKED_ATTN_REPORTS	= 2,
};

/**
 * struct rmi_data - stores information for hid communication
 *
 * @page_mutex: Locks current page to avoid changing pages in unexpected ways.
 * @page: Keeps track of the current virtual page
 * @xport: transport device to be registered with the RMI4 core.
 *
 * @wait: Used for waiting for read data
 *
 * @writeReport: output buffer when writing RMI registers
 * @readReport: input buffer when reading RMI registers
 *
 * @input_report_size: size of an input report (advertised by HID)
 * @output_report_size: size of an output report (advertised by HID)
 *
 * @flags: flags for the current device (started, reading, etc...)
 *
 * @reset_work: worker which will be called in case of a mouse report
 * @hdev: pointer to the struct hid_device
 *
 * @device_flags: flags which describe the device
 *
 * @domain: the IRQ domain allocated for this RMI4 device
 * @rmi_irq: the irq that will be used to generate events to rmi-core
 */
struct rmi_data {
	struct mutex page_mutex;
	int page;
	struct rmi_transport_dev xport;

	wait_queue_head_t wait;

	u8 *writeReport;
	u8 *readReport;

	u32 input_report_size;
	u32 output_report_size;

	unsigned long flags;

	struct work_struct reset_work;
	struct hid_device *hdev;

	unsigned long device_flags;

	struct irq_domain *domain;
	int rmi_irq;
};

#define RMI_PAGE(addr) (((addr) >> 8) & 0xff)

static int rmi_write_report(struct hid_device *hdev, u8 *report, int len);

/**
 * rmi_set_page - Set RMI page
 * @hdev: The pointer to the hid_device struct
 * @page: The new page address.
 *
 * RMI devices have 16-bit addressing, but some of the physical
 * implementations (like SMBus) only have 8-bit addressing. So RMI implements
 * a page address at 0xff of every page so we can reliable page addresses
 * every 256 registers.
 *
 * The page_mutex lock must be held when this function is entered.
 *
 * Returns zero on success, non-zero on failure.
 */
static int rmi_set_page(struct hid_device *hdev, u8 page)
{
	struct rmi_data *data = hid_get_drvdata(hdev);
	int retval;

	data->writeReport[0] = RMI_WRITE_REPORT_ID;
	data->writeReport[1] = 1;
	data->writeReport[2] = 0xFF;
	data->writeReport[4] = page;

	retval = rmi_write_report(hdev, data->writeReport,
			data->output_report_size);
	if (retval != data->output_report_size) {
		dev_err(&hdev->dev,
			"%s: set page failed: %d.", __func__, retval);
		return retval;
	}

	data->page = page;
	return 0;
}

static int rmi_set_mode(struct hid_device *hdev, u8 mode)
{
	int ret;
	const u8 txbuf[2] = {RMI_SET_RMI_MODE_REPORT_ID, mode};
	u8 *buf;

	buf = kmemdup(txbuf, sizeof(txbuf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, RMI_SET_RMI_MODE_REPORT_ID, buf,
			sizeof(txbuf), HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	kfree(buf);
	if (ret < 0) {
		dev_err(&hdev->dev, "unable to set rmi mode to %d (%d)\n", mode,
			ret);
		return ret;
	}

	return 0;
}

static int rmi_write_report(struct hid_device *hdev, u8 *report, int len)
{
	struct rmi_data *data = hid_get_drvdata(hdev);
	int ret;

	if (data->device_flags & RMI_DEVICE_OUTPUT_SET_REPORT) {
		/*
		 * Talk to device by using SET_REPORT requests instead.
		 */
		ret = hid_hw_raw_request(hdev, report[0], report,
				len, HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	} else {
		ret = hid_hw_output_report(hdev, (void *)report, len);
	}

	if (ret < 0) {
		dev_err(&hdev->dev, "failed to write hid report (%d)\n", ret);
		return ret;
	}

	return ret;
}

static int rmi_hid_read_block(struct rmi_transport_dev *xport, u16 addr,
		void *buf, size_t len)
{
	struct rmi_data *data = container_of(xport, struct rmi_data, xport);
	struct hid_device *hdev = data->hdev;
	int ret;
	int bytes_read;
	int bytes_needed;
	int retries;
	int read_input_count;

	mutex_lock(&data->page_mutex);

	if (RMI_PAGE(addr) != data->page) {
		ret = rmi_set_page(hdev, RMI_PAGE(addr));
		if (ret < 0)
			goto exit;
	}

	for (retries = 5; retries > 0; retries--) {
		data->writeReport[0] = RMI_READ_ADDR_REPORT_ID;
		data->writeReport[1] = 0; /* old 1 byte read count */
		data->writeReport[2] = addr & 0xFF;
		data->writeReport[3] = (addr >> 8) & 0xFF;
		data->writeReport[4] = len  & 0xFF;
		data->writeReport[5] = (len >> 8) & 0xFF;

		set_bit(RMI_READ_REQUEST_PENDING, &data->flags);

		ret = rmi_write_report(hdev, data->writeReport,
						data->output_report_size);
		if (ret != data->output_report_size) {
			clear_bit(RMI_READ_REQUEST_PENDING, &data->flags);
			dev_err(&hdev->dev,
				"failed to write request output report (%d)\n",
				ret);
			goto exit;
		}

		bytes_read = 0;
		bytes_needed = len;
		while (bytes_read < len) {
			if (!wait_event_timeout(data->wait,
				test_bit(RMI_READ_DATA_PENDING, &data->flags),
					msecs_to_jiffies(1000))) {
				hid_warn(hdev, "%s: timeout elapsed\n",
					 __func__);
				ret = -EAGAIN;
				break;
			}

			read_input_count = data->readReport[1];
			memcpy(buf + bytes_read, &data->readReport[2],
				read_input_count < bytes_needed ?
					read_input_count : bytes_needed);

			bytes_read += read_input_count;
			bytes_needed -= read_input_count;
			clear_bit(RMI_READ_DATA_PENDING, &data->flags);
		}

		if (ret >= 0) {
			ret = 0;
			break;
		}
	}

exit:
	clear_bit(RMI_READ_REQUEST_PENDING, &data->flags);
	mutex_unlock(&data->page_mutex);
	return ret;
}

static int rmi_hid_write_block(struct rmi_transport_dev *xport, u16 addr,
		const void *buf, size_t len)
{
	struct rmi_data *data = container_of(xport, struct rmi_data, xport);
	struct hid_device *hdev = data->hdev;
	int ret;

	mutex_lock(&data->page_mutex);

	if (RMI_PAGE(addr) != data->page) {
		ret = rmi_set_page(hdev, RMI_PAGE(addr));
		if (ret < 0)
			goto exit;
	}

	data->writeReport[0] = RMI_WRITE_REPORT_ID;
	data->writeReport[1] = len;
	data->writeReport[2] = addr & 0xFF;
	data->writeReport[3] = (addr >> 8) & 0xFF;
	memcpy(&data->writeReport[4], buf, len);

	ret = rmi_write_report(hdev, data->writeReport,
					data->output_report_size);
	if (ret < 0) {
		dev_err(&hdev->dev,
			"failed to write request output report (%d)\n",
			ret);
		goto exit;
	}
	ret = 0;

exit:
	mutex_unlock(&data->page_mutex);
	return ret;
}

static int rmi_reset_attn_mode(struct hid_device *hdev)
{
	struct rmi_data *data = hid_get_drvdata(hdev);
	struct rmi_device *rmi_dev = data->xport.rmi_dev;
	int ret;

	ret = rmi_set_mode(hdev, RMI_MODE_ATTN_REPORTS);
	if (ret)
		return ret;

	if (test_bit(RMI_STARTED, &data->flags))
		ret = rmi_dev->driver->reset_handler(rmi_dev);

	return ret;
}

static void rmi_reset_work(struct work_struct *work)
{
	struct rmi_data *hdata = container_of(work, struct rmi_data,
						reset_work);

	/* switch the device to RMI if we receive a generic mouse report */
	rmi_reset_attn_mode(hdata->hdev);
}

static int rmi_input_event(struct hid_device *hdev, u8 *data, int size)
{
	struct rmi_data *hdata = hid_get_drvdata(hdev);
	struct rmi_device *rmi_dev = hdata->xport.rmi_dev;
	unsigned long flags;

	if (!(test_bit(RMI_STARTED, &hdata->flags)))
		return 0;

	local_irq_save(flags);

	rmi_set_attn_data(rmi_dev, data[1], &data[2], size - 2);

	generic_handle_irq(hdata->rmi_irq);

	local_irq_restore(flags);

	return 1;
}

static int rmi_read_data_event(struct hid_device *hdev, u8 *data, int size)
{
	struct rmi_data *hdata = hid_get_drvdata(hdev);

	if (!test_bit(RMI_READ_REQUEST_PENDING, &hdata->flags)) {
		hid_dbg(hdev, "no read request pending\n");
		return 0;
	}

	memcpy(hdata->readReport, data, size < hdata->input_report_size ?
			size : hdata->input_report_size);
	set_bit(RMI_READ_DATA_PENDING, &hdata->flags);
	wake_up(&hdata->wait);

	return 1;
}

static int rmi_check_sanity(struct hid_device *hdev, u8 *data, int size)
{
	int valid_size = size;
	/*
	 * On the Dell XPS 13 9333, the bus sometimes get confused and fills
	 * the report with a sentinel value "ff". Synaptics told us that such
	 * behavior does not comes from the touchpad itself, so we filter out
	 * such reports here.
	 */

	while ((data[valid_size - 1] == 0xff) && valid_size > 0)
		valid_size--;

	return valid_size;
}

static int rmi_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct rmi_data *hdata = hid_get_drvdata(hdev);

	if (!(hdata->device_flags & RMI_DEVICE))
		return 0;

	size = rmi_check_sanity(hdev, data, size);
	if (size < 2)
		return 0;

	switch (data[0]) {
	case RMI_READ_DATA_REPORT_ID:
		return rmi_read_data_event(hdev, data, size);
	case RMI_ATTN_REPORT_ID:
		return rmi_input_event(hdev, data, size);
	default:
		return 1;
	}

	return 0;
}

static int rmi_event(struct hid_device *hdev, struct hid_field *field,
			struct hid_usage *usage, __s32 value)
{
	struct rmi_data *data = hid_get_drvdata(hdev);

	if ((data->device_flags & RMI_DEVICE) &&
	    (field->application == HID_GD_POINTER ||
	    field->application == HID_GD_MOUSE)) {
		if (data->device_flags & RMI_DEVICE_HAS_PHYS_BUTTONS) {
			if ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON)
				return 0;

			if ((usage->hid == HID_GD_X || usage->hid == HID_GD_Y)
			    && !value)
				return 1;
		}

		schedule_work(&data->reset_work);
		return 1;
	}

	return 0;
}

static void rmi_report(struct hid_device *hid, struct hid_report *report)
{
	struct hid_field *field = report->field[0];

	if (!(hid->claimed & HID_CLAIMED_INPUT))
		return;

	switch (report->id) {
	case RMI_READ_DATA_REPORT_ID:
		/* fall-through */
	case RMI_ATTN_REPORT_ID:
		return;
	}

	if (field && field->hidinput && field->hidinput->input)
		input_sync(field->hidinput->input);
}

#ifdef CONFIG_PM
static int rmi_suspend(struct hid_device *hdev, pm_message_t message)
{
	struct rmi_data *data = hid_get_drvdata(hdev);
	struct rmi_device *rmi_dev = data->xport.rmi_dev;
	int ret;

	if (!(data->device_flags & RMI_DEVICE))
		return 0;

	ret = rmi_driver_suspend(rmi_dev, false);
	if (ret) {
		hid_warn(hdev, "Failed to suspend device: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rmi_post_resume(struct hid_device *hdev)
{
	struct rmi_data *data = hid_get_drvdata(hdev);
	struct rmi_device *rmi_dev = data->xport.rmi_dev;
	int ret;

	if (!(data->device_flags & RMI_DEVICE))
		return 0;

	/* Make sure the HID device is ready to receive events */
	ret = hid_hw_open(hdev);
	if (ret)
		return ret;

	ret = rmi_reset_attn_mode(hdev);
	if (ret)
		goto out;

	ret = rmi_driver_resume(rmi_dev, false);
	if (ret) {
		hid_warn(hdev, "Failed to resume device: %d\n", ret);
		goto out;
	}

out:
	hid_hw_close(hdev);
	return ret;
}
#endif /* CONFIG_PM */

static int rmi_hid_reset(struct rmi_transport_dev *xport, u16 reset_addr)
{
	struct rmi_data *data = container_of(xport, struct rmi_data, xport);
	struct hid_device *hdev = data->hdev;

	return rmi_reset_attn_mode(hdev);
}

static int rmi_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct rmi_data *data = hid_get_drvdata(hdev);
	struct input_dev *input = hi->input;
	int ret = 0;

	if (!(data->device_flags & RMI_DEVICE))
		return 0;

	data->xport.input = input;

	hid_dbg(hdev, "Opening low level driver\n");
	ret = hid_hw_open(hdev);
	if (ret)
		return ret;

	/* Allow incoming hid reports */
	hid_device_io_start(hdev);

	ret = rmi_set_mode(hdev, RMI_MODE_ATTN_REPORTS);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed to set rmi mode\n");
		goto exit;
	}

	ret = rmi_set_page(hdev, 0);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed to set page select to 0.\n");
		goto exit;
	}

	ret = rmi_register_transport_device(&data->xport);
	if (ret < 0) {
		dev_err(&hdev->dev, "failed to register transport driver\n");
		goto exit;
	}

	set_bit(RMI_STARTED, &data->flags);

exit:
	hid_device_io_stop(hdev);
	hid_hw_close(hdev);
	return ret;
}

static int rmi_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	struct rmi_data *data = hid_get_drvdata(hdev);

	/*
	 * we want to make HID ignore the advertised HID collection
	 * for RMI deivces
	 */
	if (data->device_flags & RMI_DEVICE) {
		if ((data->device_flags & RMI_DEVICE_HAS_PHYS_BUTTONS) &&
		    ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON))
			return 0;

		return -1;
	}

	return 0;
}

static int rmi_check_valid_report_id(struct hid_device *hdev, unsigned type,
		unsigned id, struct hid_report **report)
{
	int i;

	*report = hdev->report_enum[type].report_id_hash[id];
	if (*report) {
		for (i = 0; i < (*report)->maxfield; i++) {
			unsigned app = (*report)->field[i]->application;
			if ((app & HID_USAGE_PAGE) >= HID_UP_MSVENDOR)
				return 1;
		}
	}

	return 0;
}

static struct rmi_device_platform_data rmi_hid_pdata = {
	.sensor_pdata = {
		.sensor_type = rmi_sensor_touchpad,
		.axis_align.flip_y = true,
		.dribble = RMI_REG_STATE_ON,
		.palm_detect = RMI_REG_STATE_OFF,
	},
};

static const struct rmi_transport_ops hid_rmi_ops = {
	.write_block	= rmi_hid_write_block,
	.read_block	= rmi_hid_read_block,
	.reset		= rmi_hid_reset,
};

static void rmi_irq_teardown(void *data)
{
	struct rmi_data *hdata = data;
	struct irq_domain *domain = hdata->domain;

	if (!domain)
		return;

	irq_dispose_mapping(irq_find_mapping(domain, 0));

	irq_domain_remove(domain);
	hdata->domain = NULL;
	hdata->rmi_irq = 0;
}

static int rmi_irq_map(struct irq_domain *h, unsigned int virq,
		       irq_hw_number_t hw_irq_num)
{
	irq_set_chip_and_handler(virq, &dummy_irq_chip, handle_simple_irq);

	return 0;
}

static const struct irq_domain_ops rmi_irq_ops = {
	.map = rmi_irq_map,
};

static int rmi_setup_irq_domain(struct hid_device *hdev)
{
	struct rmi_data *hdata = hid_get_drvdata(hdev);
	int ret;

	hdata->domain = irq_domain_create_linear(hdev->dev.fwnode, 1,
						 &rmi_irq_ops, hdata);
	if (!hdata->domain)
		return -ENOMEM;

	ret = devm_add_action_or_reset(&hdev->dev, &rmi_irq_teardown, hdata);
	if (ret)
		return ret;

	hdata->rmi_irq = irq_create_mapping(hdata->domain, 0);
	if (hdata->rmi_irq <= 0) {
		hid_err(hdev, "Can't allocate an IRQ\n");
		return hdata->rmi_irq < 0 ? hdata->rmi_irq : -ENXIO;
	}

	return 0;
}

static int rmi_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct rmi_data *data = NULL;
	int ret;
	size_t alloc_size;
	struct hid_report *input_report;
	struct hid_report *output_report;
	struct hid_report *feature_report;

	data = devm_kzalloc(&hdev->dev, sizeof(struct rmi_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_WORK(&data->reset_work, rmi_reset_work);
	data->hdev = hdev;

	hid_set_drvdata(hdev, data);

	hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;
	hdev->quirks |= HID_QUIRK_NO_INPUT_SYNC;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	if (id->driver_data)
		data->device_flags = id->driver_data;

	/*
	 * Check for the RMI specific report ids. If they are misisng
	 * simply return and let the events be processed by hid-input
	 */
	if (!rmi_check_valid_report_id(hdev, HID_FEATURE_REPORT,
	    RMI_SET_RMI_MODE_REPORT_ID, &feature_report)) {
		hid_dbg(hdev, "device does not have set mode feature report\n");
		goto start;
	}

	if (!rmi_check_valid_report_id(hdev, HID_INPUT_REPORT,
	    RMI_ATTN_REPORT_ID, &input_report)) {
		hid_dbg(hdev, "device does not have attention input report\n");
		goto start;
	}

	data->input_report_size = hid_report_len(input_report);

	if (!rmi_check_valid_report_id(hdev, HID_OUTPUT_REPORT,
	    RMI_WRITE_REPORT_ID, &output_report)) {
		hid_dbg(hdev,
			"device does not have rmi write output report\n");
		goto start;
	}

	data->output_report_size = hid_report_len(output_report);

	data->device_flags |= RMI_DEVICE;
	alloc_size = data->output_report_size + data->input_report_size;

	data->writeReport = devm_kzalloc(&hdev->dev, alloc_size, GFP_KERNEL);
	if (!data->writeReport) {
		hid_err(hdev, "failed to allocate buffer for HID reports\n");
		return -ENOMEM;
	}

	data->readReport = data->writeReport + data->output_report_size;

	init_waitqueue_head(&data->wait);

	mutex_init(&data->page_mutex);

	ret = rmi_setup_irq_domain(hdev);
	if (ret) {
		hid_err(hdev, "failed to allocate IRQ domain\n");
		return ret;
	}

	if (data->device_flags & RMI_DEVICE_HAS_PHYS_BUTTONS)
		rmi_hid_pdata.f30_data.disable = true;

	data->xport.dev = hdev->dev.parent;
	data->xport.pdata = rmi_hid_pdata;
	data->xport.pdata.irq = data->rmi_irq;
	data->xport.proto_name = "hid";
	data->xport.ops = &hid_rmi_ops;

start:
	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static void rmi_remove(struct hid_device *hdev)
{
	struct rmi_data *hdata = hid_get_drvdata(hdev);

	if ((hdata->device_flags & RMI_DEVICE)
	    && test_bit(RMI_STARTED, &hdata->flags)) {
		clear_bit(RMI_STARTED, &hdata->flags);
		cancel_work_sync(&hdata->reset_work);
		rmi_unregister_transport_device(&hdata->xport);
	}

	hid_hw_stop(hdev);
}

static const struct hid_device_id rmi_id[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAZER, USB_DEVICE_ID_RAZER_BLADE_14),
		.driver_data = RMI_DEVICE_HAS_PHYS_BUTTONS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_X1_COVER) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_PRIMAX, USB_DEVICE_ID_PRIMAX_REZEL) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SYNAPTICS, USB_DEVICE_ID_SYNAPTICS_ACER_SWITCH5),
		.driver_data = RMI_DEVICE_OUTPUT_SET_REPORT },
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_RMI, HID_ANY_ID, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, rmi_id);

static struct hid_driver rmi_driver = {
	.name = "hid-rmi",
	.id_table		= rmi_id,
	.probe			= rmi_probe,
	.remove			= rmi_remove,
	.event			= rmi_event,
	.raw_event		= rmi_raw_event,
	.report			= rmi_report,
	.input_mapping		= rmi_input_mapping,
	.input_configured	= rmi_input_configured,
#ifdef CONFIG_PM
	.suspend		= rmi_suspend,
	.resume			= rmi_post_resume,
	.reset_resume		= rmi_post_resume,
#endif
};

module_hid_driver(rmi_driver);

MODULE_AUTHOR("Andrew Duggan <aduggan@synaptics.com>");
MODULE_DESCRIPTION("RMI HID driver");
MODULE_LICENSE("GPL");
