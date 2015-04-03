/*
 * HID class driver for the Greybus.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/bitops.h>
#include <linux/hid.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "greybus.h"

/* Version of the Greybus hid protocol we support */
#define GB_HID_VERSION_MAJOR		0x00
#define GB_HID_VERSION_MINOR		0x01

/* Greybus HID request types */
#define GB_HID_TYPE_INVALID		0x00
#define GB_HID_TYPE_PROTOCOL_VERSION	0x01
#define GB_HID_TYPE_GET_DESC		0x02
#define GB_HID_TYPE_GET_REPORT_DESC	0x03
#define GB_HID_TYPE_PWR_ON		0x04
#define GB_HID_TYPE_PWR_OFF		0x05
#define GB_HID_TYPE_GET_REPORT		0x06
#define GB_HID_TYPE_SET_REPORT		0x07
#define GB_HID_TYPE_IRQ_EVENT		0x08
#define GB_HID_TYPE_RESPONSE		0x80	/* OR'd with rest */

/* Report type */
#define GB_HID_INPUT_REPORT		0
#define GB_HID_OUTPUT_REPORT		1
#define GB_HID_FEATURE_REPORT		2

/* Different request/response structures */
/* HID get descriptor response */
struct gb_hid_desc_response {
	__u8				bLength;
	__le16				wReportDescLength;
	__le16				bcdHID;
	__le16				wProductID;
	__le16				wVendorID;
	__u8				bCountryCode;
} __packed;

/* HID get report request/response */
struct gb_hid_get_report_request {
	__u8				report_type;
	__u8				report_id;
};

/* HID set report request */
struct gb_hid_set_report_request {
	__u8				report_type;
	__u8				report_id;
	__u8				report[0];
};

/* HID input report request, via interrupt pipe */
struct gb_hid_input_report_request {
	__u8				report[0];
};

/* Greybus HID device's structure */
struct gb_hid {
	struct gb_connection		*connection;
	u8				version_major;
	u8				version_minor;

	struct hid_device		*hid;
	struct gb_hid_desc_response	hdesc;

	unsigned long			flags;
#define GB_HID_STARTED			0x01
#define GB_HID_READ_PENDING		0x04

	unsigned int			bufsize;
	char				*inbuf;
};

static DEFINE_MUTEX(gb_hid_open_mutex);

/* Routines to get controller's infomation over greybus */

/* Define get_version() routine */
define_get_version(gb_hid, HID);

/* Operations performed on greybus */
static int gb_hid_get_desc(struct gb_hid *ghid)
{
	return gb_operation_sync(ghid->connection, GB_HID_TYPE_GET_DESC, NULL,
				 0, &ghid->hdesc, sizeof(ghid->hdesc));
}

static int gb_hid_get_report_desc(struct gb_hid *ghid, char *rdesc)
{
	return gb_operation_sync(ghid->connection, GB_HID_TYPE_GET_REPORT_DESC,
				 NULL, 0, rdesc,
				 le16_to_cpu(ghid->hdesc.wReportDescLength));
}

static int gb_hid_set_power(struct gb_hid *ghid, int type)
{
	return gb_operation_sync(ghid->connection, type, NULL, 0, NULL, 0);
}

static int gb_hid_get_report(struct gb_hid *ghid, u8 report_type, u8 report_id,
			     unsigned char *buf, int len)
{
	struct gb_hid_get_report_request request;

	request.report_type = report_type;
	request.report_id = report_id;

	return gb_operation_sync(ghid->connection, GB_HID_TYPE_GET_REPORT,
				 &request, sizeof(request), buf, len);
}

static int gb_hid_set_report(struct gb_hid *ghid, u8 report_type, u8 report_id,
			     unsigned char *buf, int len)
{
	struct gb_hid_set_report_request *request;
	struct gb_operation *operation;
	int ret, size = sizeof(*request) + len - 1;

	operation = gb_operation_create(ghid->connection,
					GB_HID_TYPE_SET_REPORT, size, 0);
	if (!operation)
		return -ENOMEM;

	request = operation->request->payload;
	request->report_type = report_type;
	request->report_id = report_id;
	memcpy(request->report, buf, len);

	ret = gb_operation_request_send_sync(operation);
	if (ret) {
		dev_err(&operation->connection->dev,
			"failed to set report: %d\n", ret);
	} else {
		ret = len;
	}

	gb_operation_destroy(operation);
	return ret;
}

static int gb_hid_irq_handler(u8 type, struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_hid *ghid = connection->private;
	struct gb_hid_input_report_request *request = op->request->payload;

	if (type != GB_HID_TYPE_IRQ_EVENT) {
		dev_err(&connection->dev,
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
		&hid->report_enum[HID_INPUT_REPORT].report_list, list)
		gb_hid_init_report(ghid, report);

	list_for_each_entry(report,
		&hid->report_enum[HID_FEATURE_REPORT].report_list, list)
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
		ret++; /* add report_id to the number of transfered bytes */

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
static int gb_hid_get_raw_report(struct hid_device *hid,
				   unsigned char reportnum, __u8 *buf,
				   size_t len, unsigned char rtype)
{
	return gb_hid_raw_request(hid, reportnum, buf, len, rtype,
				  HID_REQ_GET_REPORT);
}

static int gb_hid_output_raw_report(struct hid_device *hid, __u8 *buf,
				    size_t len, unsigned char rtype)
{
	return gb_hid_raw_request(hid, buf[0], buf, len, rtype,
				  HID_REQ_SET_REPORT);
}
#endif

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
	if (!rdesc) {
		dbg_hid("couldn't allocate rdesc memory\n");
		return -ENOMEM;
	}

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
	int ret = 0;

	mutex_lock(&gb_hid_open_mutex);
	if (!hid->open++) {
		ret = gb_hid_set_power(ghid, GB_HID_TYPE_PWR_ON);
		if (ret < 0)
			hid->open--;
		else
			set_bit(GB_HID_STARTED, &ghid->flags);
	}
	mutex_unlock(&gb_hid_open_mutex);

	return ret;
}

static void gb_hid_close(struct hid_device *hid)
{
	struct gb_hid *ghid = hid->driver_data;

	/*
	 * Protecting hid->open to make sure we don't restart data acquistion
	 * due to a resumption we no longer care about..
	 */
	mutex_lock(&gb_hid_open_mutex);
	if (!--hid->open) {
		clear_bit(GB_HID_STARTED, &ghid->flags);

		/* Save some power */
		WARN_ON(gb_hid_set_power(ghid, GB_HID_TYPE_PWR_OFF));
	}
	mutex_unlock(&gb_hid_open_mutex);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)
	.raw_request = gb_hid_raw_request,
#endif
};

static int gb_hid_init(struct gb_hid *ghid)
{
	struct hid_device *hid = ghid->hid;
	int ret;

	ret = get_version(ghid);
	if (ret)
		return ret;

	ret = gb_hid_get_desc(ghid);
	if (ret)
		return ret;

	hid->version = le16_to_cpu(ghid->hdesc.bcdHID);
	hid->vendor = le16_to_cpu(ghid->hdesc.wVendorID);
	hid->product = le16_to_cpu(ghid->hdesc.wProductID);
	hid->country = ghid->hdesc.bCountryCode;

	hid->driver_data = ghid;
	hid->ll_driver = &gb_hid_ll_driver;
	hid->dev.parent = &ghid->connection->dev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
	hid->hid_get_raw_report = gb_hid_get_raw_report;
	hid->hid_output_raw_report = gb_hid_output_raw_report;
#endif
//	hid->bus = BUS_GREYBUS; /* Need a bustype for GREYBUS in <linux/input.h> */

	/* Set HID device's name */
	snprintf(hid->name, sizeof(hid->name), "%s %04hX:%04hX",
		 dev_name(&ghid->connection->dev), hid->vendor, hid->product);

	return 0;
}

static int gb_hid_connection_init(struct gb_connection *connection)
{
	struct hid_device *hid;
	struct gb_hid *ghid;
	int ret;

	ghid = kzalloc(sizeof(*ghid), GFP_KERNEL);
	if (!ghid)
		return -ENOMEM;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto free_ghid;
	}

	connection->private = ghid;
	ghid->connection = connection;
	ghid->hid = hid;

	ret = gb_hid_init(ghid);
	if (ret)
		goto destroy_hid;

	ret = hid_add_device(hid);
	if (!ret)
		return 0;

	hid_err(hid, "can't add hid device: %d\n", ret);

destroy_hid:
	hid_destroy_device(hid);
free_ghid:
	kfree(ghid);

	return ret;
}

static void gb_hid_connection_exit(struct gb_connection *connection)
{
	struct gb_hid *ghid = connection->private;

	hid_destroy_device(ghid->hid);
	kfree(ghid);
}

static struct gb_protocol hid_protocol = {
	.name			= "hid",
	.id			= GREYBUS_PROTOCOL_HID,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_hid_connection_init,
	.connection_exit	= gb_hid_connection_exit,
	.request_recv		= gb_hid_irq_handler,
};

int gb_hid_protocol_init(void)
{
	return gb_protocol_register(&hid_protocol);
}

void gb_hid_protocol_exit(void)
{
	gb_protocol_deregister(&hid_protocol);
}
