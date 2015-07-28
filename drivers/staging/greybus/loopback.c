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
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/sizes.h>
#include <asm/div64.h>

#include "greybus.h"

struct gb_loopback_stats {
	u64 min;
	u64 max;
	u64 avg;
	u64 sum;
	u64 count;
};

struct gb_loopback {
	struct gb_connection *connection;
	u8 version_major;
	u8 version_minor;

	struct task_struct *task;
	wait_queue_head_t wq;

	int type;
	u32 size;
	u32 iteration_max;
	u32 iteration_count;
	size_t size_max;
	int ms_wait;

	struct gb_loopback_stats latency;
	struct gb_loopback_stats throughput;
	struct gb_loopback_stats requests_per_second;
	u64 elapsed_nsecs;
	u32 error;
};

#define GB_LOOPBACK_MS_WAIT_MAX				1000

/* Define get_version() routine */
define_get_version(gb_loopback, LOOPBACK);

/* interface sysfs attributes */
#define gb_loopback_ro_attr(field, type)				\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb =					\
		(struct gb_loopback *)connection->private;		\
	return sprintf(buf, "%"#type"\n", gb->field);			\
}									\
static DEVICE_ATTR_RO(field)

#define gb_loopback_ro_stats_attr(name, field, type)			\
static ssize_t name##_##field##_show(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb =					\
		(struct gb_loopback *)connection->private;		\
	return sprintf(buf, "%"#type"\n", gb->name.field);		\
}									\
static DEVICE_ATTR_RO(name##_##field)

#define gb_loopback_stats_attrs(field)					\
	gb_loopback_ro_stats_attr(field, min, llu);			\
	gb_loopback_ro_stats_attr(field, max, llu);			\
	gb_loopback_ro_stats_attr(field, avg, llu);

#define gb_loopback_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb =					\
		(struct gb_loopback *)connection->private;		\
	return sprintf(buf, "%"#type"\n", gb->field);			\
}									\
static ssize_t field##_store(struct device *dev,			\
			    struct device_attribute *attr,		\
			    const char *buf,				\
			    size_t len)					\
{									\
	int ret;							\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb =					\
		(struct gb_loopback *)connection->private;		\
	ret = sscanf(buf, "%"#type, &gb->field);			\
	if (ret != 1)							\
		return -EINVAL;						\
	gb_loopback_check_attr(gb);					\
	return len;							\
}									\
static DEVICE_ATTR_RW(field)

static void gb_loopback_reset_stats(struct gb_loopback *gb);
static void gb_loopback_check_attr(struct gb_loopback *gb)
{
	if (gb->ms_wait > GB_LOOPBACK_MS_WAIT_MAX)
		gb->ms_wait = GB_LOOPBACK_MS_WAIT_MAX;
	if (gb->size > gb->size_max)
		gb->size = gb->size_max;
	gb->error = 0;
	gb->iteration_count = 0;
	gb_loopback_reset_stats(gb);

	switch (gb->type) {
	case GB_LOOPBACK_TYPE_PING:
	case GB_LOOPBACK_TYPE_TRANSFER:
	case GB_LOOPBACK_TYPE_SINK:
		wake_up(&gb->wq);
		break;
	default:
		gb->type = 0;
		break;
	}
}

/* Time to send and receive one message */
gb_loopback_stats_attrs(latency);
/* Number of requests sent per second on this cport */
gb_loopback_stats_attrs(requests_per_second);
/* Quantity of data sent and received on this cport */
gb_loopback_stats_attrs(throughput);
/* Number of errors encountered during loop */
gb_loopback_ro_attr(error, d);
/* The current index of the for (i = 0; i < iteration_max; i++) loop */
gb_loopback_ro_attr(iteration_count, u);

/*
 * Type of loopback message to send based on protocol type definitions
 * 0 => Don't send message
 * 2 => Send ping message continuously (message without payload)
 * 3 => Send transer message continuously (message with payload,
 *					   payload returned in response)
 * 4 => Send a sink message (message with payload, no payload in response)
 */
gb_loopback_attr(type, d);
/* Size of transfer message payload: 0-4096 bytes */
gb_loopback_attr(size, u);
/* Time to wait between two messages: 0-1000 ms */
gb_loopback_attr(ms_wait, d);
/* Maximum iterations for a given operation: 1-(2^32-1), 0 implies infinite */
gb_loopback_attr(iteration_max, u);

#define dev_stats_attrs(name)						\
	&dev_attr_##name##_min.attr,					\
	&dev_attr_##name##_max.attr,					\
	&dev_attr_##name##_avg.attr

static struct attribute *loopback_attrs[] = {
	dev_stats_attrs(latency),
	dev_stats_attrs(requests_per_second),
	dev_stats_attrs(throughput),
	&dev_attr_type.attr,
	&dev_attr_size.attr,
	&dev_attr_ms_wait.attr,
	&dev_attr_iteration_count.attr,
	&dev_attr_iteration_max.attr,
	&dev_attr_error.attr,
	NULL,
};
ATTRIBUTE_GROUPS(loopback);

static int gb_loopback_sink(struct gb_loopback *gb, u32 len)
{
	struct timeval ts, te;
	struct gb_loopback_transfer_request *request;
	int retval;

	request = kmalloc(len + sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->len = cpu_to_le32(len);

	do_gettimeofday(&ts);
	retval = gb_operation_sync(gb->connection, GB_LOOPBACK_TYPE_SINK,
				   request, len + sizeof(*request), NULL, 0);

	do_gettimeofday(&te);
	gb->elapsed_nsecs = timeval_to_ns(&te) - timeval_to_ns(&ts);

	kfree(request);
	return retval;
}

static int gb_loopback_transfer(struct gb_loopback *gb, u32 len)
{
	struct timeval ts, te;
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

	request->len = cpu_to_le32(len);

	do_gettimeofday(&ts);
	retval = gb_operation_sync(gb->connection, GB_LOOPBACK_TYPE_TRANSFER,
				   request, len + sizeof(*request),
				   response, len + sizeof(*response));
	do_gettimeofday(&te);
	gb->elapsed_nsecs = timeval_to_ns(&te) - timeval_to_ns(&ts);

	if (retval)
		goto gb_error;

	if (memcmp(request->data, response->data, len))
		retval = -EREMOTEIO;

gb_error:
	kfree(request);
	kfree(response);

	return retval;
}

static int gb_loopback_ping(struct gb_loopback *gb)
{
	struct timeval ts, te;
	int retval;

	do_gettimeofday(&ts);
	retval = gb_operation_sync(gb->connection, GB_LOOPBACK_TYPE_PING,
				   NULL, 0, NULL, 0);
	do_gettimeofday(&te);
	gb->elapsed_nsecs = timeval_to_ns(&te) - timeval_to_ns(&ts);

	return retval;
}

static int gb_loopback_request_recv(u8 type, struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	struct gb_loopback *gb = connection->private;
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	size_t len;

	/* By convention, the AP initiates the version operation */
	switch (type) {
	case GB_LOOPBACK_TYPE_PROTOCOL_VERSION:
		dev_err(&connection->dev,
			"module-initiated version operation\n");
		return -EINVAL;
	case GB_LOOPBACK_TYPE_PING:
	case GB_LOOPBACK_TYPE_SINK:
		return 0;
	case GB_LOOPBACK_TYPE_TRANSFER:
		if (operation->request->payload_size < sizeof(*request)) {
			dev_err(&connection->dev,
				"transfer request too small (%zu < %zu)\n",
				operation->request->payload_size,
				sizeof(*request));
			return -EINVAL;	/* -EMSGSIZE */
		}
		request = operation->request->payload;
		len = le32_to_cpu(request->len);
		if (len > gb->size_max) {
			dev_err(&connection->dev,
				"transfer request too large (%zu > %zu)\n",
				len, gb->size_max);
			return -EINVAL;
		}

		if (len) {
			if (!gb_operation_response_alloc(operation, len,
							 GFP_KERNEL)) {
				dev_err(&connection->dev,
					"error allocating response\n");
				return -ENOMEM;
			}
			response = operation->response->payload;
			memcpy(response->data, request->data, len);
		}
		return 0;
	default:
		dev_err(&connection->dev,
			"unsupported request: %hhu\n", type);
		return -EINVAL;
	}
}

static void gb_loopback_reset_stats(struct gb_loopback *gb)
{
	struct gb_loopback_stats reset = {
		.min = 0xffffffff,
	};
	memcpy(&gb->latency, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb->throughput, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb->requests_per_second, &reset,
	       sizeof(struct gb_loopback_stats));
}

static void gb_loopback_update_stats(struct gb_loopback_stats *stats, u64 val)
{
	if (stats->min > val)
		stats->min = val;
	if (stats->max < val)
		stats->max = val;
	stats->sum += val;
	stats->count++;
	stats->avg = stats->sum;
	do_div(stats->avg, stats->count);
}

static void gb_loopback_requests_update(struct gb_loopback *gb, u32 latency)
{
	u32 req = USEC_PER_SEC;

	do_div(req, latency);
	gb_loopback_update_stats(&gb->requests_per_second, req);
}

static void gb_loopback_throughput_update(struct gb_loopback *gb, u32 latency)
{
	u32 throughput;
	u32 aggregate_size = sizeof(struct gb_operation_msg_hdr) * 2;

	switch (gb->type) {
	case GB_LOOPBACK_TYPE_PING:
		break;
	case GB_LOOPBACK_TYPE_SINK:
		aggregate_size += sizeof(struct gb_loopback_transfer_request) +
				  gb->size;
		break;
	case GB_LOOPBACK_TYPE_TRANSFER:
		aggregate_size += sizeof(struct gb_loopback_transfer_request) +
				  sizeof(struct gb_loopback_transfer_response) +
				  gb->size * 2;
		break;
	default:
		return;
	}

	/* Calculate bytes per second */
	throughput = USEC_PER_SEC;
	do_div(throughput, latency);
	throughput *= aggregate_size;
	gb_loopback_update_stats(&gb->throughput, throughput);
}

static void gb_loopback_calculate_stats(struct gb_loopback *gb)
{
	u32 lat;
	u64 tmp;

	/* Express latency in terms of microseconds */
	tmp = gb->elapsed_nsecs;
	do_div(tmp, NSEC_PER_USEC);
	lat = tmp;

	/* Log latency stastic */
	gb_loopback_update_stats(&gb->latency, lat);

	/* Log throughput and requests using latency as benchmark */
	gb_loopback_throughput_update(gb, lat);
	gb_loopback_requests_update(gb, lat);
}

static int gb_loopback_fn(void *data)
{
	int error = 0;
	struct gb_loopback *gb = (struct gb_loopback *)data;

	while (1) {
		if (!gb->type)
			wait_event_interruptible(gb->wq, gb->type ||
						 kthread_should_stop());
		if (kthread_should_stop())
			break;
		if (gb->iteration_max) {
			if (gb->iteration_count < gb->iteration_max) {
				gb->iteration_count++;
				sysfs_notify(&gb->connection->dev.kobj, NULL,
					     "iteration_count");
			} else {
				gb->type = 0;
				continue;
			}
		}
		if (gb->type == GB_LOOPBACK_TYPE_PING)
			error = gb_loopback_ping(gb);
		else if (gb->type == GB_LOOPBACK_TYPE_TRANSFER)
			error = gb_loopback_transfer(gb, gb->size);
		else if (gb->type == GB_LOOPBACK_TYPE_SINK)
			error = gb_loopback_sink(gb, gb->size);
		if (error)
			gb->error++;
		gb_loopback_calculate_stats(gb);
		if (gb->ms_wait)
			msleep(gb->ms_wait);
	}
	return 0;
}

static int gb_loopback_connection_init(struct gb_connection *connection)
{
	struct gb_loopback *gb;
	int retval;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;

	gb->connection = connection;
	connection->private = gb;
	retval = sysfs_create_groups(&connection->dev.kobj, loopback_groups);
	if (retval)
		goto out_free;

	/* Check the version */
	retval = get_version(gb);
	if (retval)
		goto out_get_ver;

	/* Calculate maximum payload */
	gb->size_max = gb_operation_get_payload_size_max(connection);
	if (gb->size_max <= sizeof(struct gb_loopback_transfer_request)) {
		retval = -EINVAL;
		goto out_get_ver;
	}
	gb->size_max -= sizeof(struct gb_loopback_transfer_request);

	gb_loopback_reset_stats(gb);
	init_waitqueue_head(&gb->wq);
	gb->task = kthread_run(gb_loopback_fn, gb, "gb_loopback");
	if (IS_ERR(gb->task)) {
		retval = PTR_ERR(gb->task);
		goto out_get_ver;
	}

	return 0;

out_get_ver:
	sysfs_remove_groups(&connection->dev.kobj, loopback_groups);
out_free:
	kfree(gb);
	return retval;
}

static void gb_loopback_connection_exit(struct gb_connection *connection)
{
	struct gb_loopback *gb = connection->private;

	if (!IS_ERR_OR_NULL(gb->task))
		kthread_stop(gb->task);
	sysfs_remove_groups(&connection->dev.kobj, loopback_groups);
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

gb_protocol_driver(&loopback_protocol);

MODULE_LICENSE("GPL v2");
