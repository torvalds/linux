// SPDX-License-Identifier: GPL-2.0
/*
 * HID class driver for the Greybus.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/hid.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/greybus.h>

/* Greybus HID device's structure */
struct gb_hid {
	struct gb_bundle *bundle;
	struct gb_connection		*connection;

	struct hid_device		*hid;
	struct gb_hid_desc_response	hdesc;

	unsigned long			flags;
#define GB_HID_STARTED			0x01
#define GB_HID_READ_PENDING		0x04

	unsigned int			bufsize;
	char				*inbuf;
};

/* Routines to get controller's information over greybus */

/* Operations performed on greybus */
static int gb_hid_get_desc(struct gb_hid *ghid)
{
	return gb_operation_sync(ghid->connection, GB_HID_TYPE_GET_DESC, NULL,
				 0, &ghid->hdesc, sizeof(ghid->hdesc));
}

static int gb_hid_get_report_desc(struct gb_hid *ghid, char *rdesc)
{
	int ret;

	ret = gb_pm_runtime_get_sync(ghid->bundle);
	if (ret)
		return ret;

	ret = gb_operation_sync(ghid->connection, GB_HID_TYPE_GET_REPORT_DESC,
				NULL, 0, rdesc,
				le16_to_cpu(ghid->hdesc.wReportDescLength));

	gb_pm_runtime_put_autosuspend(ghid->bundle);

	return ret;
}

static int gb_hid_set_power(struct gb_hid *ghid, int type)
{
	int ret;

	ret = gb_pm_runtime_get_sync(ghid->bundle);
	if (ret)
		return ret;

	ret = gb_operation_sync(ghid->connection, type, NULL, 0, NULL, 0);

	gb_pm_runtime_put_autosuspend(ghid->bundle);

	return ret;
}

static int gb_hid_get_report(struct gb_hid *ghid, u8 report_type, u8 report_id,
			     unsigned char *buf, int len)
{
	struct gb_hid_get_report_request request;
	int ret;

	ret = gb_pm_runtime_get_sync(ghid->bundle);
	if (ret)
		return ret;

	request.report_type = report_type;
	request.report_id = report_id;

	ret = gb_operation_sync(ghid->connection, GB_HID_TYPE_GET_REPORT,
				&request, sizeof(request), buf, len);

	gb_pm_runtime_put_autosuspend(ghid->bundle);

	return ret;
}

static int gb_hid_set_report(struct gb_hid *ghid, u8 report_type, u8 report_id,
			     unsigned char *buf, int len)
{
	struct gb_hid_set_report_request *request;
	struct gb_operation *operation;
	int ret, size = sizeof(*request) + len - 1;

	ret = gb_pm_runtime_get_sync(ghid->bundle);
	if (ret)
		return ret;

	operation = gb_operation_create(ghid->connection,
					GB_HID_TYPE_SET_REPORT, size, 0,
					GFP_KERNEL);
	if (!operation) {
		gb_pm_runtime_put_autosuspend(ghid->bundle);
		return -ENOMEM;
	}

	request = operation->request->payload;
	request->report_type = report_type;
	request->report_id = report_id;
	memcpy(request->report, buf, len);

	ret = gb_operation_request_send_sync(operation);
	if (ret) {
		dev_err(&operation->connection->bundle->dev,
			"failed to set report: %d\n", ret);
	} else {
		ret = len;
	}

	gb_operation_put(operation);
	gb_pm_runtime_put_autosuspend(ghid->bundle);

	return ret;
}

static int gb_hid_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_hid *ghid = gb_connection_get_data(connection);
	struct gb_hid_input_report_request *request = op->request->payload;

	if (op->type != GB_HID_TYPE_IRQ_EVENT) {
		dev_err(&connection->bundle->dev,
			"unsupported unsolicited request\n");
		return -EINVAL;
	}

	if (test_bit(GB_HID_STARTED, &ghid->flags))
		hid_input_report(ghid->hid, HID_INPUT_REPORT,
				 request->report, op->request->payload_size, 1);

	return 0;
}

static int gb_hid_report_len(struct hid_report *report)
{
	return ((report->size - 1) >> 3) + 1 +
		report->device->report_enum[report->type].numbered;
}

static void gb_hid_find_max_report(struct hid_device *hid, unsigned int type,
				   unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = gb_hid_report_len(report);
		if (*max < size)
			*max = size;
	}
}

static void gb_hid_free_buffers(struct gb_hid *ghid)
{
	kfree(ghid->inbuf);
	ghid->inbuf = NULL;
	ghid->bufsize = 0;
}

static int gb_hid_alloc_buffers(struct gb_hid *ghid, size_t bufsize)
{
	ghid->inbuf = kzalloc(bufsize, GFP_KERNEL);
	if (!ghid->inbuf)
		return -ENOMEM;

	ghid->bufsize = bufsize;

	return 0;
}

/* Routines dealing with reports */
static void gb_hid_init_report(struct gb_hid *ghid, struct hid_report *report)
{
	unsigned int size;

	size = gb_hid_report_len(report);
	if (gb_hid_get_report(ghid, report->type, report->id, ghid->inbuf,
			      size))
		return;

	/*
	 * hid->driver_lock is held as we are in probe function,
	 * we just need to setup the input fields, so using
	 * hid_report_raw_event is safe.
	 */
	hid_report_raw_event(ghid->hid, report->type, ghid->inbuf, size, 1);
}

static void gb_hid_init_reports(struct gb_hid *ghid)
{
	struct hid_device *hid = ghid->hid;
	struct hid_report *report;

	list_for_each_entry(report,
			    &hid->report_enum[HID_INPUT_REPORT].report_list,
			    list)
		gb_hid_init_report(ghid, report);

	list_for_each_entry(report,
			    &hid->report_enum[HID_FEATURE_REPORT].report_list,
			    list)
		gb_hid_init_report(ghid, report);
}

static int __gb_hid_get_raw_report(struct hid_device *hid,
				   unsigned char report_number, __u8 *buf, size_t count,
				   unsigned char report_type)
{
	struct gb_hid *ghid = hid->driver_data;
	int ret;

	if (report_type == HID_OUTPUT_REPORT)
		return -EINVAL;

	ret = gb_hid_get_report(ghid, report_type, report_number, buf, count);
	if (!ret)
		ret = count;

	return ret;
}

static int __gb_hid_output_raw_report(struct hid_device *hid, __u8 *buf,
				      size_t len, unsigned char report_type)
{
	struct gb_hid *ghid = hid->driver_data;
	int report_id = buf[0];
	int ret;

	if (report_type == HID_INPUT_REPORT)
		return -EINVAL;

	if (report_id) {
		buf++;
		len--;
	}

	ret = gb_hid_set_report(ghid, report_type, report_id, buf, len);
	if (report_id && ret >= 0)
		ret++; /* add report_id to the number of transferred bytes */

	return 0;
}

static int gb_hid_raw_request(struct hid_device *hid, unsigned char reportnum,
			      __u8 *buf, size_t len, unsigned char rtype,
			      int reqtype)
{
	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		return __gb_hid_get_raw_report(hid, reportnum, buf, len, rtype);
	case HID_REQ_SET_REPORT:
		if (buf[0] != reportnum)
			return -EINVAL;
		return __gb_hid_output_raw_report(hid, buf, len, rtype);
	default:
		return -EIO;
	}
}

/* HID Callbacks */
static int gb_hid_parse(struct hid_device *hid)
{
	struct gb_hid *ghid = hid->driver_data;
	unsigned int rsize;
	char *rdesc;
	int ret;

	rsize = le16_to_cpu(ghid->hdesc.wReportDescLength);
	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dbg_hid("weird size of report descriptor (%u)\n", rsize);
		return -EINVAL;
	}

	rdesc = kzalloc(rsize, GFP_KERNEL);
	if (!rdesc)
		return -ENOMEM;

	ret = gb_hid_get_report_desc(ghid, rdesc);
	if (ret) {
		hid_err(hid, "reading report descriptor failed\n");
		goto free_rdesc;
	}

	ret = hid_parse_report(hid, rdesc, rsize);
	if (ret)
		dbg_hid("parsing report descriptor failed\n");

free_rdesc:
	kfree(rdesc);

	return ret;
}

static int gb_hid_start(struct hid_device *hid)
{
	struct gb_hid *ghid = hid->driver_data;
	unsigned int bufsize = HID_MIN_BUFFER_SIZE;
	int ret;

	gb_hid_find_max_report(hid, HID_INPUT_REPORT, &bufsize);
	gb_hid_find_max_report(hid, HID_OUTPUT_REPORT, &bufsize);
	gb_hid_find_max_report(hid, HID_FEATURE_REPORT, &bufsize);

	if (bufsize > HID_MAX_BUFFER_SIZE)
		bufsize = HID_MAX_BUFFER_SIZE;

	ret = gb_hid_alloc_buffers(ghid, bufsize);
	if (ret)
		return ret;

	if (!(hid->quirks & HID_QUIRK_NO_INIT_REPORTS))
		gb_hid_init_reports(ghid);

	return 0;
}

static void gb_hid_stop(struct hid_device *hid)
{
	struct gb_hid *ghid = hid->driver_data;

	gb_hid_free_buffers(ghid);
}

static int gb_hid_open(struct hid_device *hid)
{
	struct gb_hid *ghid = hid->driver_data;
	int ret;

	ret = gb_hid_set_power(ghid, GB_HID_TYPE_PWR_ON);
	if (ret < 0)
		return ret;

	set_bit(GB_HID_STARTED, &ghid->flags);
	return 0;
}

static void gb_hid_close(struct hid_device *hid)
{
	struct gb_hid *ghid = hid->driver_data;
	int ret;

	clear_bit(GB_HID_STARTED, &ghid->flags);

	/* Save some power */
	ret = gb_hid_set_power(ghid, GB_HID_TYPE_PWR_OFF);
	if (ret)
		dev_err(&ghid->connection->bundle->dev,
			"failed to power off (%d)\n", ret);
}

static int gb_hid_power(struct hid_device *hid, int lvl)
{
	struct gb_hid *ghid = hid->driver_data;

	switch (lvl) {
	case PM_HINT_FULLON:
		return gb_hid_set_power(ghid, GB_HID_TYPE_PWR_ON);
	case PM_HINT_NORMAL:
		return gb_hid_set_power(ghid, GB_HID_TYPE_PWR_OFF);
	}

	return 0;
}

/* HID structure to pass callbacks */
static struct hid_ll_driver gb_hid_ll_driver = {
	.parse = gb_hid_parse,
	.start = gb_hid_start,
	.stop = gb_hid_stop,
	.open = gb_hid_open,
	.close = gb_hid_close,
	.power = gb_hid_power,
	.raw_request = gb_hid_raw_request,
};

static int gb_hid_init(struct gb_hid *ghid)
{
	struct hid_device *hid = ghid->hid;
	int ret;

	ret = gb_hid_get_desc(ghid);
	if (ret)
		return ret;

	hid->version = le16_to_cpu(ghid->hdesc.bcdHID);
	hid->vendor = le16_to_cpu(ghid->hdesc.wVendorID);
	hid->product = le16_to_cpu(ghid->hdesc.wProductID);
	hid->country = ghid->hdesc.bCountryCode;

	hid->driver_data = ghid;
	hid->ll_driver = &gb_hid_ll_driver;
	hid->dev.parent = &ghid->connection->bundle->dev;
//	hid->bus = BUS_GREYBUS; /* Need a bustype for GREYBUS in <linux/input.h> */

	/* Set HID device's name */
	snprintf(hid->name, sizeof(hid->name), "%s %04X:%04X",
		 dev_name(&ghid->connection->bundle->dev),
		 hid->vendor, hid->product);

	return 0;
}

static int gb_hid_probe(struct gb_bundle *bundle,
			const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct hid_device *hid;
	struct gb_hid *ghid;
	int ret;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_HID)
		return -ENODEV;

	ghid = kzalloc(sizeof(*ghid), GFP_KERNEL);
	if (!ghid)
		return -ENOMEM;

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gb_hid_request_handler);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto err_free_ghid;
	}

	gb_connection_set_data(connection, ghid);
	ghid->connection = connection;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto err_connection_destroy;
	}

	ghid->hid = hid;
	ghid->bundle = bundle;

	greybus_set_drvdata(bundle, ghid);

	ret = gb_connection_enable(connection);
	if (ret)
		goto err_destroy_hid;

	ret = gb_hid_init(ghid);
	if (ret)
		goto err_connection_disable;

	ret = hid_add_device(hid);
	if (ret) {
		hid_err(hid, "can't add hid device: %d\n", ret);
		goto err_connection_disable;
	}

	gb_pm_runtime_put_autosuspend(bundle);

	return 0;

err_connection_disable:
	gb_connection_disable(connection);
err_destroy_hid:
	hid_destroy_device(hid);
err_connection_destroy:
	gb_connection_destroy(connection);
err_free_ghid:
	kfree(ghid);

	return ret;
}

static void gb_hid_disconnect(struct gb_bundle *bundle)
{
	struct gb_hid *ghid = greybus_get_drvdata(bundle);

	if (gb_pm_runtime_get_sync(bundle))
		gb_pm_runtime_get_noresume(bundle);

	hid_destroy_device(ghid->hid);
	gb_connection_disable(ghid->connection);
	gb_connection_destroy(ghid->connection);
	kfree(ghid);
}

static const struct greybus_bundle_id gb_hid_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_HID) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_hid_id_table);

static struct greybus_driver gb_hid_driver = {
	.name		= "hid",
	.probe		= gb_hid_probe,
	.disconnect	= gb_hid_disconnect,
	.id_table	= gb_hid_id_table,
};
module_greybus_driver(gb_hid_driver);

MODULE_LICENSE("GPL v2");
