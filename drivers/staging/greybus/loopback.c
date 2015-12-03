/*
 * Loopback bridge driver for the Greybus loopback module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/sizes.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <linux/list_sort.h>

#include <asm/div64.h>

#include "greybus.h"
#include "connection.h"

#define NSEC_PER_DAY 86400000000000ULL

struct gb_loopback_stats {
	u32 min;
	u32 max;
	u64 sum;
	u32 count;
};

struct gb_loopback_device {
	struct dentry *root;
	struct dentry *file;
	u32 count;

	struct kfifo kfifo;
	struct mutex mutex;
	struct list_head list;
	wait_queue_head_t wq;

	int type;
	u32 mask;
	u32 size;
	u32 iteration_max;
	u32 iteration_count;
	size_t size_max;
	int ms_wait;
	u32 error;

	struct timeval start;
	struct timeval end;

	/* Overall stats */
	struct gb_loopback_stats latency;
	struct gb_loopback_stats throughput;
	struct gb_loopback_stats requests_per_second;
	struct gb_loopback_stats apbridge_unipro_latency;
	struct gb_loopback_stats gpbridge_firmware_latency;
};

static struct gb_loopback_device gb_dev;

struct gb_loopback {
	struct gb_connection *connection;

	struct dentry *file;
	struct kfifo kfifo_lat;
	struct kfifo kfifo_ts;
	struct mutex mutex;
	struct task_struct *task;
	struct list_head entry;

	/* Per connection stats */
	struct gb_loopback_stats latency;
	struct gb_loopback_stats throughput;
	struct gb_loopback_stats requests_per_second;
	struct gb_loopback_stats apbridge_unipro_latency;
	struct gb_loopback_stats gpbridge_firmware_latency;

	u32 lbid;
	u32 iteration_count;
	u64 elapsed_nsecs;
	u32 error;
	u32 apbridge_latency_ts;
	u32 gpbridge_latency_ts;
};

#define GB_LOOPBACK_FIFO_DEFAULT			8192

static unsigned kfifo_depth = GB_LOOPBACK_FIFO_DEFAULT;
module_param(kfifo_depth, uint, 0444);

/* Maximum size of any one send data buffer we support */
#define MAX_PACKET_SIZE (PAGE_SIZE * 2)

#define GB_LOOPBACK_MS_WAIT_MAX				1000

/* interface sysfs attributes */
#define gb_loopback_ro_attr(field, pfx, conn)				\
static ssize_t field##_##pfx##_show(struct device *dev,			\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_bundle *bundle;					\
	struct gb_loopback *gb;						\
	if (conn) {							\
		bundle = to_gb_bundle(dev);				\
		gb = bundle->private;					\
		return sprintf(buf, "%u\n", gb->field);			\
	} else {							\
		return sprintf(buf, "%u\n", gb_dev.field);		\
	}								\
}									\
static DEVICE_ATTR_RO(field##_##pfx)

#define gb_loopback_ro_stats_attr(name, field, type, pfx, conn)		\
static ssize_t name##_##field##_##pfx##_show(struct device *dev,	\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_bundle *bundle;					\
	struct gb_loopback *gb;						\
	if (conn) {							\
		bundle = to_gb_bundle(dev);				\
		gb = bundle->private;					\
		return sprintf(buf, "%"#type"\n", gb->name.field);	\
	} else {							\
		return sprintf(buf, "%"#type"\n", gb_dev.name.field);	\
	}								\
}									\
static DEVICE_ATTR_RO(name##_##field##_##pfx)

#define gb_loopback_ro_avg_attr(name, pfx, conn)			\
static ssize_t name##_avg_##pfx##_show(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback_stats *stats;				\
	struct gb_bundle *bundle;					\
	struct gb_loopback *gb;						\
	u64 avg;							\
	u32 count, rem;							\
	if (conn) {							\
		bundle = to_gb_bundle(dev);				\
		gb = bundle->private;					\
		stats = &gb->name;					\
	} else {							\
		stats = &gb_dev.name;					\
	}								\
	count = stats->count ? stats->count : 1;			\
	avg = stats->sum + count / 2;	/* round closest */		\
	rem = do_div(avg, count);					\
	return sprintf(buf, "%llu.%06u\n", avg, 1000000 * rem / count);	\
}									\
static DEVICE_ATTR_RO(name##_avg_##pfx)

#define gb_loopback_stats_attrs(field, pfx, conn)			\
	gb_loopback_ro_stats_attr(field, min, u, pfx, conn);		\
	gb_loopback_ro_stats_attr(field, max, u, pfx, conn);		\
	gb_loopback_ro_avg_attr(field, pfx, conn)

#define gb_loopback_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_bundle *bundle = to_gb_bundle(dev);			\
	struct gb_loopback *gb = bundle->private;			\
	return sprintf(buf, "%"#type"\n", gb->field);			\
}									\
static ssize_t field##_store(struct device *dev,			\
			    struct device_attribute *attr,		\
			    const char *buf,				\
			    size_t len)					\
{									\
	int ret;							\
	struct gb_bundle *bundle = to_gb_bundle(dev);			\
	mutex_lock(&gb_dev.mutex);					\
	ret = sscanf(buf, "%"#type, &gb->field);			\
	if (ret != 1)							\
		len = -EINVAL;						\
	else								\
		gb_loopback_check_attr(&gb_dev, bundle);		\
	mutex_unlock(&gb_dev.mutex);					\
	return len;							\
}									\
static DEVICE_ATTR_RW(field)

#define gb_dev_loopback_ro_attr(field, conn)				\
static ssize_t field##_show(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	return sprintf(buf, "%u\n", gb_dev.field);			\
}									\
static DEVICE_ATTR_RO(field)

#define gb_dev_loopback_rw_attr(field, type)				\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	return sprintf(buf, "%"#type"\n", gb_dev.field);		\
}									\
static ssize_t field##_store(struct device *dev,			\
			    struct device_attribute *attr,		\
			    const char *buf,				\
			    size_t len)					\
{									\
	int ret;							\
	struct gb_bundle *bundle = to_gb_bundle(dev);			\
	mutex_lock(&gb_dev.mutex);					\
	ret = sscanf(buf, "%"#type, &gb_dev.field);			\
	if (ret != 1)							\
		len = -EINVAL;						\
	else								\
		gb_loopback_check_attr(&gb_dev, bundle);		\
	mutex_unlock(&gb_dev.mutex);					\
	return len;							\
}									\
static DEVICE_ATTR_RW(field)

static void gb_loopback_reset_stats(struct gb_loopback_device *gb_dev);
static void gb_loopback_check_attr(struct gb_loopback_device *gb_dev,
				   struct gb_bundle *bundle)
{
	struct gb_loopback *gb;

	if (gb_dev->ms_wait > GB_LOOPBACK_MS_WAIT_MAX)
		gb_dev->ms_wait = GB_LOOPBACK_MS_WAIT_MAX;
	if (gb_dev->size > gb_dev->size_max)
		gb_dev->size = gb_dev->size_max;
	gb_dev->iteration_count = 0;
	gb_dev->error = 0;

	list_for_each_entry(gb, &gb_dev->list, entry) {
		mutex_lock(&gb->mutex);
		gb->iteration_count = 0;
		gb->error = 0;
		if (kfifo_depth < gb_dev->iteration_max) {
			dev_warn(&bundle->dev,
				 "cannot log bytes %u kfifo_depth %u\n",
				 gb_dev->iteration_max, kfifo_depth);
		}
		kfifo_reset_out(&gb->kfifo_lat);
		kfifo_reset_out(&gb->kfifo_ts);
		mutex_unlock(&gb->mutex);
	}

	switch (gb_dev->type) {
	case GB_LOOPBACK_TYPE_PING:
	case GB_LOOPBACK_TYPE_TRANSFER:
	case GB_LOOPBACK_TYPE_SINK:
		kfifo_reset_out(&gb_dev->kfifo);
		gb_loopback_reset_stats(gb_dev);
		wake_up(&gb_dev->wq);
		break;
	default:
		gb_dev->type = 0;
		break;
	}
}

/* Time to send and receive one message */
gb_loopback_stats_attrs(latency, dev, false);
gb_loopback_stats_attrs(latency, con, true);
/* Number of requests sent per second on this cport */
gb_loopback_stats_attrs(requests_per_second, dev, false);
gb_loopback_stats_attrs(requests_per_second, con, true);
/* Quantity of data sent and received on this cport */
gb_loopback_stats_attrs(throughput, dev, false);
gb_loopback_stats_attrs(throughput, con, true);
/* Latency across the UniPro link from APBridge's perspective */
gb_loopback_stats_attrs(apbridge_unipro_latency, dev, false);
gb_loopback_stats_attrs(apbridge_unipro_latency, con, true);
/* Firmware induced overhead in the GPBridge */
gb_loopback_stats_attrs(gpbridge_firmware_latency, dev, false);
gb_loopback_stats_attrs(gpbridge_firmware_latency, con, true);
/* Number of errors encountered during loop */
gb_loopback_ro_attr(error, dev, false);
gb_loopback_ro_attr(error, con, true);

/*
 * Type of loopback message to send based on protocol type definitions
 * 0 => Don't send message
 * 2 => Send ping message continuously (message without payload)
 * 3 => Send transfer message continuously (message with payload,
 *					   payload returned in response)
 * 4 => Send a sink message (message with payload, no payload in response)
 */
gb_dev_loopback_rw_attr(type, d);
/* Size of transfer message payload: 0-4096 bytes */
gb_dev_loopback_rw_attr(size, u);
/* Time to wait between two messages: 0-1000 ms */
gb_dev_loopback_rw_attr(ms_wait, d);
/* Maximum iterations for a given operation: 1-(2^32-1), 0 implies infinite */
gb_dev_loopback_rw_attr(iteration_max, u);
/* The current index of the for (i = 0; i < iteration_max; i++) loop */
gb_dev_loopback_ro_attr(iteration_count, false);
/* A bit-mask of destination connecitons to include in the test run */
gb_dev_loopback_rw_attr(mask, u);

static struct attribute *loopback_dev_attrs[] = {
	&dev_attr_latency_min_dev.attr,
	&dev_attr_latency_max_dev.attr,
	&dev_attr_latency_avg_dev.attr,
	&dev_attr_requests_per_second_min_dev.attr,
	&dev_attr_requests_per_second_max_dev.attr,
	&dev_attr_requests_per_second_avg_dev.attr,
	&dev_attr_throughput_min_dev.attr,
	&dev_attr_throughput_max_dev.attr,
	&dev_attr_throughput_avg_dev.attr,
	&dev_attr_apbridge_unipro_latency_min_dev.attr,
	&dev_attr_apbridge_unipro_latency_max_dev.attr,
	&dev_attr_apbridge_unipro_latency_avg_dev.attr,
	&dev_attr_gpbridge_firmware_latency_min_dev.attr,
	&dev_attr_gpbridge_firmware_latency_max_dev.attr,
	&dev_attr_gpbridge_firmware_latency_avg_dev.attr,
	&dev_attr_type.attr,
	&dev_attr_size.attr,
	&dev_attr_ms_wait.attr,
	&dev_attr_iteration_count.attr,
	&dev_attr_iteration_max.attr,
	&dev_attr_mask.attr,
	&dev_attr_error_dev.attr,
	NULL,
};
ATTRIBUTE_GROUPS(loopback_dev);

static struct attribute *loopback_con_attrs[] = {
	&dev_attr_latency_min_con.attr,
	&dev_attr_latency_max_con.attr,
	&dev_attr_latency_avg_con.attr,
	&dev_attr_requests_per_second_min_con.attr,
	&dev_attr_requests_per_second_max_con.attr,
	&dev_attr_requests_per_second_avg_con.attr,
	&dev_attr_throughput_min_con.attr,
	&dev_attr_throughput_max_con.attr,
	&dev_attr_throughput_avg_con.attr,
	&dev_attr_apbridge_unipro_latency_min_con.attr,
	&dev_attr_apbridge_unipro_latency_max_con.attr,
	&dev_attr_apbridge_unipro_latency_avg_con.attr,
	&dev_attr_gpbridge_firmware_latency_min_con.attr,
	&dev_attr_gpbridge_firmware_latency_max_con.attr,
	&dev_attr_gpbridge_firmware_latency_avg_con.attr,
	&dev_attr_error_con.attr,
	NULL,
};
ATTRIBUTE_GROUPS(loopback_con);

static u32 gb_loopback_nsec_to_usec_latency(u64 elapsed_nsecs)
{
	u32 lat;

	do_div(elapsed_nsecs, NSEC_PER_USEC);
	lat = elapsed_nsecs;
	return lat;
}

static u64 __gb_loopback_calc_latency(u64 t1, u64 t2)
{
	if (t2 > t1)
		return t2 - t1;
	else
		return NSEC_PER_DAY - t2 + t1;
}

static u64 gb_loopback_calc_latency(struct timeval *ts, struct timeval *te)
{
	u64 t1, t2;

	t1 = timeval_to_ns(ts);
	t2 = timeval_to_ns(te);

	return __gb_loopback_calc_latency(t1, t2);
}

static void gb_loopback_push_latency_ts(struct gb_loopback *gb,
					struct timeval *ts, struct timeval *te)
{
	kfifo_in(&gb->kfifo_ts, (unsigned char *)ts, sizeof(*ts));
	kfifo_in(&gb->kfifo_ts, (unsigned char *)te, sizeof(*te));
}

static int gb_loopback_active(struct gb_loopback *gb)
{
	return (gb_dev.mask == 0 || (gb_dev.mask & gb->lbid));
}

static int gb_loopback_operation_sync(struct gb_loopback *gb, int type,
				      void *request, int request_size,
				      void *response, int response_size)
{
	struct gb_operation *operation;
	struct timeval ts, te;
	int ret;

	do_gettimeofday(&ts);
	operation = gb_operation_create(gb->connection, type, request_size,
					response_size, GFP_KERNEL);
	if (!operation) {
		ret = -ENOMEM;
		goto error;
	}

	if (request_size)
		memcpy(operation->request->payload, request, request_size);

	ret = gb_operation_request_send_sync(operation);
	if (ret) {
		dev_err(&gb->connection->bundle->dev,
			"synchronous operation failed: %d\n", ret);
	} else {
		if (response_size == operation->response->payload_size) {
			memcpy(response, operation->response->payload,
			       response_size);
		} else {
			dev_err(&gb->connection->bundle->dev,
				"response size %zu expected %d\n",
				operation->response->payload_size,
				response_size);
		}
	}

	gb_operation_put(operation);

error:
	do_gettimeofday(&te);

	/* Calculate the total time the message took */
	gb_loopback_push_latency_ts(gb, &ts, &te);
	gb->elapsed_nsecs = gb_loopback_calc_latency(&ts, &te);

	return ret;
}

static int gb_loopback_sink(struct gb_loopback *gb, u32 len)
{
	struct gb_loopback_transfer_request *request;
	int retval;

	request = kmalloc(len + sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->len = cpu_to_le32(len);
	retval = gb_loopback_operation_sync(gb, GB_LOOPBACK_TYPE_SINK,
					    request, len + sizeof(*request),
					    NULL, 0);
	kfree(request);
	return retval;
}

static int gb_loopback_transfer(struct gb_loopback *gb, u32 len)
{
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	int retval;

	request = kmalloc(len + sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;
	response = kmalloc(len + sizeof(*response), GFP_KERNEL);
	if (!response) {
		kfree(request);
		return -ENOMEM;
	}

	memset(request->data, 0x5A, len);

	request->len = cpu_to_le32(len);
	retval = gb_loopback_operation_sync(gb, GB_LOOPBACK_TYPE_TRANSFER,
					    request, len + sizeof(*request),
					    response, len + sizeof(*response));
	if (retval)
		goto gb_error;

	if (memcmp(request->data, response->data, len)) {
		dev_err(&gb->connection->bundle->dev,
			"Loopback Data doesn't match\n");
		retval = -EREMOTEIO;
	}
	gb->apbridge_latency_ts = (u32)__le32_to_cpu(response->reserved0);
	gb->gpbridge_latency_ts = (u32)__le32_to_cpu(response->reserved1);

gb_error:
	kfree(request);
	kfree(response);

	return retval;
}

static int gb_loopback_ping(struct gb_loopback *gb)
{
	return gb_loopback_operation_sync(gb, GB_LOOPBACK_TYPE_PING,
					  NULL, 0, NULL, 0);
}

static int gb_loopback_request_recv(u8 type, struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	struct device *dev = &connection->bundle->dev;
	size_t len;

	/* By convention, the AP initiates the version operation */
	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		dev_err(dev, "module-initiated version operation\n");
		return -EINVAL;
	case GB_LOOPBACK_TYPE_PING:
	case GB_LOOPBACK_TYPE_SINK:
		return 0;
	case GB_LOOPBACK_TYPE_TRANSFER:
		if (operation->request->payload_size < sizeof(*request)) {
			dev_err(dev, "transfer request too small (%zu < %zu)\n",
				operation->request->payload_size,
				sizeof(*request));
			return -EINVAL;	/* -EMSGSIZE */
		}
		request = operation->request->payload;
		len = le32_to_cpu(request->len);
		if (len > gb_dev.size_max) {
			dev_err(dev, "transfer request too large (%zu > %zu)\n",
				len, gb_dev.size_max);
			return -EINVAL;
		}

		if (!gb_operation_response_alloc(operation,
				len + sizeof(*response), GFP_KERNEL)) {
			dev_err(dev, "error allocating response\n");
			return -ENOMEM;
		}
		response = operation->response->payload;
		response->len = cpu_to_le32(len);
		if (len)
			memcpy(response->data, request->data, len);

		return 0;
	default:
		dev_err(dev, "unsupported request: %hhu\n", type);
		return -EINVAL;
	}
}

static void gb_loopback_reset_stats(struct gb_loopback_device *gb_dev)
{
	struct gb_loopback_stats reset = {
		.min = U32_MAX,
	};
	struct gb_loopback *gb;

	/* Reset per-connection stats */
	list_for_each_entry(gb, &gb_dev->list, entry) {
		mutex_lock(&gb->mutex);
		memcpy(&gb->latency, &reset,
		       sizeof(struct gb_loopback_stats));
		memcpy(&gb->throughput, &reset,
		       sizeof(struct gb_loopback_stats));
		memcpy(&gb->requests_per_second, &reset,
		       sizeof(struct gb_loopback_stats));
		memcpy(&gb->apbridge_unipro_latency, &reset,
		       sizeof(struct gb_loopback_stats));
		memcpy(&gb->gpbridge_firmware_latency, &reset,
		       sizeof(struct gb_loopback_stats));
		mutex_unlock(&gb->mutex);
	}

	/* Reset aggregate stats */
	memset(&gb_dev->start, 0, sizeof(struct timeval));
	memset(&gb_dev->end, 0, sizeof(struct timeval));
	memcpy(&gb_dev->latency, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb_dev->throughput, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb_dev->requests_per_second, &reset,
	       sizeof(struct gb_loopback_stats));
	memcpy(&gb_dev->apbridge_unipro_latency, &reset,
	       sizeof(struct gb_loopback_stats));
	memcpy(&gb_dev->gpbridge_firmware_latency, &reset,
	       sizeof(struct gb_loopback_stats));
}

static void gb_loopback_update_stats(struct gb_loopback_stats *stats, u32 val)
{
	if (stats->min > val)
		stats->min = val;
	if (stats->max < val)
		stats->max = val;
	stats->sum += val;
	stats->count++;
}

static void gb_loopback_requests_update(struct gb_loopback *gb, u32 latency)
{
	u32 req = USEC_PER_SEC;

	do_div(req, latency);
	gb_loopback_update_stats(&gb_dev.requests_per_second, req);
	gb_loopback_update_stats(&gb->requests_per_second, req);
}

static void gb_loopback_throughput_update(struct gb_loopback *gb, u32 latency)
{
	u32 throughput;
	u32 aggregate_size = sizeof(struct gb_operation_msg_hdr) * 2;

	switch (gb_dev.type) {
	case GB_LOOPBACK_TYPE_PING:
		break;
	case GB_LOOPBACK_TYPE_SINK:
		aggregate_size += sizeof(struct gb_loopback_transfer_request) +
				  gb_dev.size;
		break;
	case GB_LOOPBACK_TYPE_TRANSFER:
		aggregate_size += sizeof(struct gb_loopback_transfer_request) +
				  sizeof(struct gb_loopback_transfer_response) +
				  gb_dev.size * 2;
		break;
	default:
		return;
	}

	/* Calculate bytes per second */
	throughput = USEC_PER_SEC;
	do_div(throughput, latency);
	throughput *= aggregate_size;
	gb_loopback_update_stats(&gb_dev.throughput, throughput);
	gb_loopback_update_stats(&gb->throughput, throughput);
}

static int gb_loopback_calculate_aggregate_stats(void)
{
	struct gb_loopback *gb;
	struct timeval ts;
	struct timeval te;
	u64 t1, t2;
	u64 ts_min;
	u64 te_max;
	u64 elapsed_nsecs;
	u32 lat;
	int i, latched;
	int rollover = 0;

	for (i = 0; i < gb_dev.iteration_max; i++) {
		latched = 0;
		ts_min = 0;
		te_max = 0;
		list_for_each_entry(gb, &gb_dev.list, entry) {
			if (!gb_loopback_active(gb))
				continue;
			if (kfifo_out(&gb->kfifo_ts, &ts, sizeof(ts)) < sizeof(ts))
				goto error;
			if (kfifo_out(&gb->kfifo_ts, &te, sizeof(te)) < sizeof(te))
				goto error;
			t1 = timeval_to_ns(&ts);
			t2 = timeval_to_ns(&te);

			/* minimum timestamp is always what we want */
			if (latched == 0 || t1 < ts_min)
				ts_min = t1;

			/* maximum timestamp needs to handle rollover */
			if (t2 > t1) {
				if (latched == 0 || t2 > te_max)
					te_max = t2;
			} else {
				if (latched == 0 || rollover == 0)
					te_max = t2;
				if (rollover == 1 && t2 > te_max)
					te_max = t2;
				rollover = 1;
			}
			latched = 1;
		}
		/* Calculate the aggregate timestamp */
		elapsed_nsecs = __gb_loopback_calc_latency(ts_min, te_max);
		lat = gb_loopback_nsec_to_usec_latency(elapsed_nsecs);
		kfifo_in(&gb_dev.kfifo, (unsigned char *)&lat, sizeof(lat));
	}
	return 0;
error:
	kfifo_reset_out(&gb_dev.kfifo);
	return -ENOMEM;
}

static void gb_loopback_calculate_stats(struct gb_loopback *gb)
{
	u32 lat;

	/* Express latency in terms of microseconds */
	lat = gb_loopback_nsec_to_usec_latency(gb->elapsed_nsecs);

	/* Log latency stastic */
	gb_loopback_update_stats(&gb_dev.latency, lat);
	gb_loopback_update_stats(&gb->latency, lat);

	/* Raw latency log on a per thread basis */
	kfifo_in(&gb->kfifo_lat, (unsigned char *)&lat, sizeof(lat));

	/* Log throughput and requests using latency as benchmark */
	gb_loopback_throughput_update(gb, lat);
	gb_loopback_requests_update(gb, lat);

	/* Log the firmware supplied latency values */
	gb_loopback_update_stats(&gb_dev.apbridge_unipro_latency,
				 gb->apbridge_latency_ts);
	gb_loopback_update_stats(&gb->apbridge_unipro_latency,
				 gb->apbridge_latency_ts);
	gb_loopback_update_stats(&gb_dev.gpbridge_firmware_latency,
				 gb->gpbridge_latency_ts);
	gb_loopback_update_stats(&gb->gpbridge_firmware_latency,
				 gb->gpbridge_latency_ts);
}

static int gb_loopback_fn(void *data)
{
	int error = 0;
	int ms_wait = 0;
	int type;
	u32 size;
	u32 low_count;
	struct gb_loopback *gb = data;
	struct gb_loopback *gb_list;

	while (1) {
		if (!gb_dev.type)
			wait_event_interruptible(gb_dev.wq, gb_dev.type ||
						 kthread_should_stop());
		if (kthread_should_stop())
			break;

		mutex_lock(&gb_dev.mutex);
		if (!gb_loopback_active(gb)) {
			ms_wait = 100;
			goto unlock_continue;
		}
		if (gb_dev.iteration_max) {
			/* Determine overall lowest count */
			low_count = gb->iteration_count;
			list_for_each_entry(gb_list, &gb_dev.list, entry) {
				if (!gb_loopback_active(gb_list))
					continue;
				if (gb_list->iteration_count < low_count)
					low_count = gb_list->iteration_count;
			}
			/* All threads achieved at least low_count iterations */
			if (gb_dev.iteration_count < low_count) {
				gb_dev.iteration_count = low_count;
				sysfs_notify(&gb->connection->bundle->dev.kobj,
					     NULL, "iteration_count");
			}
			/* Optionally terminate */
			if (gb_dev.iteration_count == gb_dev.iteration_max) {
				gb_loopback_calculate_aggregate_stats();
				gb_dev.type = 0;
				goto unlock_continue;
			}
		}
		size = gb_dev.size;
		ms_wait = gb_dev.ms_wait;
		type = gb_dev.type;
		mutex_unlock(&gb_dev.mutex);

		mutex_lock(&gb->mutex);
		if (gb->iteration_count >= gb_dev.iteration_max) {
			/* If this thread finished before siblings then sleep */
			ms_wait = 1;
			mutex_unlock(&gb->mutex);
			goto sleep;
		}
		mutex_unlock(&gb->mutex);

		/* Else operations to perform */
		gb->apbridge_latency_ts = 0;
		gb->gpbridge_latency_ts = 0;
		if (type == GB_LOOPBACK_TYPE_PING)
			error = gb_loopback_ping(gb);
		else if (type == GB_LOOPBACK_TYPE_TRANSFER)
			error = gb_loopback_transfer(gb, size);
		else if (type == GB_LOOPBACK_TYPE_SINK)
			error = gb_loopback_sink(gb, size);

		mutex_lock(&gb_dev.mutex);
		mutex_lock(&gb->mutex);

		if (error) {
			gb_dev.error++;
			gb->error++;
		}
		gb_loopback_calculate_stats(gb);
		gb->iteration_count++;

		mutex_unlock(&gb->mutex);
unlock_continue:
		mutex_unlock(&gb_dev.mutex);
sleep:
		if (ms_wait)
			msleep(ms_wait);
	}
	return 0;
}

static int gb_loopback_dbgfs_latency_show_common(struct seq_file *s,
						 struct kfifo *kfifo,
						 struct mutex *mutex)
{
	u32 latency;
	int retval;

	if (kfifo_len(kfifo) == 0) {
		retval = -EAGAIN;
		goto done;
	}

	mutex_lock(mutex);
	retval = kfifo_out(kfifo, &latency, sizeof(latency));
	if (retval > 0) {
		seq_printf(s, "%u", latency);
		retval = 0;
	}
	mutex_unlock(mutex);
done:
	return retval;
}

static int gb_loopback_dbgfs_latency_show(struct seq_file *s, void *unused)
{
	struct gb_loopback *gb = s->private;

	return gb_loopback_dbgfs_latency_show_common(s, &gb->kfifo_lat,
						     &gb->mutex);
}

static int gb_loopback_latency_open(struct inode *inode, struct file *file)
{
	return single_open(file, gb_loopback_dbgfs_latency_show,
			   inode->i_private);
}

static const struct file_operations gb_loopback_debugfs_latency_ops = {
	.open		= gb_loopback_latency_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int gb_loopback_dbgfs_dev_latency_show(struct seq_file *s, void *unused)
{
	struct gb_loopback_device *gb_dev = s->private;

	return gb_loopback_dbgfs_latency_show_common(s, &gb_dev->kfifo,
						     &gb_dev->mutex);
}

static int gb_loopback_dev_latency_open(struct inode *inode, struct file *file)
{
	return single_open(file, gb_loopback_dbgfs_dev_latency_show,
			   inode->i_private);
}

static const struct file_operations gb_loopback_debugfs_dev_latency_ops = {
	.open		= gb_loopback_dev_latency_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int gb_loopback_bus_id_compare(void *priv, struct list_head *lha,
				      struct list_head *lhb)
{
	struct gb_loopback *a = list_entry(lha, struct gb_loopback, entry);
	struct gb_loopback *b = list_entry(lhb, struct gb_loopback, entry);
	struct gb_connection *ca = a->connection;
	struct gb_connection *cb = b->connection;

	if (ca->bundle->intf->interface_id < cb->bundle->intf->interface_id)
		return -1;
	if (cb->bundle->intf->interface_id < ca->bundle->intf->interface_id)
		return 1;
	if (ca->bundle->id < cb->bundle->id)
		return -1;
	if (cb->bundle->id < ca->bundle->id)
		return 1;
	if (ca->intf_cport_id < cb->intf_cport_id)
		return -1;
	else if (cb->intf_cport_id < ca->intf_cport_id)
		return 1;

	return 0;
}

static void gb_loopback_insert_id(struct gb_loopback *gb)
{
	struct gb_loopback *gb_list;
	u32 new_lbid = 0;

	/* perform an insertion sort */
	list_add_tail(&gb->entry, &gb_dev.list);
	list_sort(NULL, &gb_dev.list, gb_loopback_bus_id_compare);
	list_for_each_entry(gb_list, &gb_dev.list, entry) {
		gb_list->lbid = 1 << new_lbid;
		new_lbid++;
	}
}

#define DEBUGFS_NAMELEN 32

static int gb_loopback_connection_init(struct gb_connection *connection)
{
	struct gb_loopback *gb;
	int retval;
	char name[DEBUGFS_NAMELEN];
	struct kobject *kobj = &connection->bundle->dev.kobj;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;
	gb_loopback_reset_stats(&gb_dev);

	/* If this is the first connection - create a per-bus entry */
	mutex_lock(&gb_dev.mutex);
	if (!gb_dev.count) {
		snprintf(name, sizeof(name), "raw_latency_%d",
				connection->bundle->intf->hd->bus_id);
		gb_dev.file = debugfs_create_file(name, S_IFREG | S_IRUGO,
						  gb_dev.root, &gb_dev,
				  &gb_loopback_debugfs_dev_latency_ops);
		retval = sysfs_create_groups(kobj, loopback_dev_groups);
		if (retval)
			goto out_sysfs;

		/* Calculate maximum payload */
		gb_dev.size_max = gb_operation_get_payload_size_max(connection);
		if (gb_dev.size_max <=
			sizeof(struct gb_loopback_transfer_request)) {
			retval = -EINVAL;
			goto out_sysfs_dev;
		}
		gb_dev.size_max -= sizeof(struct gb_loopback_transfer_request);
	}

	/* Create per-connection sysfs and debugfs data-points */
	snprintf(name, sizeof(name), "raw_latency_%s",
		 dev_name(&connection->bundle->dev));
	gb->file = debugfs_create_file(name, S_IFREG | S_IRUGO, gb_dev.root, gb,
				       &gb_loopback_debugfs_latency_ops);
	gb->connection = connection;
	connection->bundle->private = gb;
	retval = sysfs_create_groups(&connection->bundle->dev.kobj,
				     loopback_con_groups);
	if (retval)
		goto out_sysfs_dev;

	/* Allocate kfifo */
	if (kfifo_alloc(&gb->kfifo_lat, kfifo_depth * sizeof(u32),
			  GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_sysfs_conn;
	}
	if (kfifo_alloc(&gb->kfifo_ts, kfifo_depth * sizeof(struct timeval) * 2,
			  GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_kfifo0;
	}

	/* Fork worker thread */
	mutex_init(&gb->mutex);
	gb->task = kthread_run(gb_loopback_fn, gb, "gb_loopback");
	if (IS_ERR(gb->task)) {
		retval = PTR_ERR(gb->task);
		goto out_kfifo1;
	}

	gb_loopback_insert_id(gb);
	gb_connection_latency_tag_enable(connection);
	gb_dev.count++;
	mutex_unlock(&gb_dev.mutex);
	return 0;

out_kfifo1:
	kfifo_free(&gb->kfifo_ts);
out_kfifo0:
	kfifo_free(&gb->kfifo_lat);
out_sysfs_conn:
	sysfs_remove_groups(&connection->bundle->dev.kobj, loopback_con_groups);
out_sysfs_dev:
	if (!gb_dev.count) {
		sysfs_remove_groups(kobj, loopback_dev_groups);
		debugfs_remove(gb_dev.file);
	}
	debugfs_remove(gb->file);
	connection->bundle->private = NULL;
out_sysfs:
	mutex_unlock(&gb_dev.mutex);
	kfree(gb);

	return retval;
}

static void gb_loopback_connection_exit(struct gb_connection *connection)
{
	struct gb_loopback *gb = connection->bundle->private;
	struct kobject *kobj = &connection->bundle->dev.kobj;

	if (!IS_ERR_OR_NULL(gb->task))
		kthread_stop(gb->task);

	mutex_lock(&gb_dev.mutex);

	connection->bundle->private = NULL;
	kfifo_free(&gb->kfifo_lat);
	kfifo_free(&gb->kfifo_ts);
	gb_connection_latency_tag_disable(connection);
	gb_dev.count--;
	if (!gb_dev.count) {
		sysfs_remove_groups(kobj, loopback_dev_groups);
		debugfs_remove(gb_dev.file);
	}
	sysfs_remove_groups(&connection->bundle->dev.kobj,
			    loopback_con_groups);
	debugfs_remove(gb->file);
	list_del(&gb->entry);
	mutex_unlock(&gb_dev.mutex);
	kfree(gb);
}

static struct gb_protocol loopback_protocol = {
	.name			= "loopback",
	.id			= GREYBUS_PROTOCOL_LOOPBACK,
	.major			= GB_LOOPBACK_VERSION_MAJOR,
	.minor			= GB_LOOPBACK_VERSION_MINOR,
	.connection_init	= gb_loopback_connection_init,
	.connection_exit	= gb_loopback_connection_exit,
	.request_recv		= gb_loopback_request_recv,
};

static int loopback_init(void)
{
	int retval;

	init_waitqueue_head(&gb_dev.wq);
	INIT_LIST_HEAD(&gb_dev.list);
	mutex_init(&gb_dev.mutex);
	gb_dev.root = debugfs_create_dir("gb_loopback", NULL);

	if (kfifo_alloc(&gb_dev.kfifo, kfifo_depth * sizeof(u32), GFP_KERNEL)) {
		retval = -ENOMEM;
		goto error_debugfs;
	}

	retval = gb_protocol_register(&loopback_protocol);
	if (!retval)
		return retval;

error_debugfs:
	debugfs_remove_recursive(gb_dev.root);
	return retval;
}
module_init(loopback_init);

static void __exit loopback_exit(void)
{
	debugfs_remove_recursive(gb_dev.root);
	kfifo_free(&gb_dev.kfifo);
	gb_protocol_deregister(&loopback_protocol);
}
module_exit(loopback_exit);

MODULE_LICENSE("GPL v2");
