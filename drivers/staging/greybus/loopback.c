// SPDX-License-Identifier: GPL-2.0
/*
 * Loopback bridge driver for the Greybus loopback module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/pm_runtime.h>

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
	u32 count;
	size_t size_max;

	/* We need to take a lock in atomic context */
	spinlock_t lock;
	wait_queue_head_t wq;
};

static struct gb_loopback_device gb_dev;

struct gb_loopback_async_operation {
	struct gb_loopback *gb;
	struct gb_operation *operation;
	ktime_t ts;
	int (*completion)(struct gb_loopback_async_operation *op_async);
};

struct gb_loopback {
	struct gb_connection *connection;

	struct dentry *file;
	struct kfifo kfifo_lat;
	struct mutex mutex;
	struct task_struct *task;
	struct device *dev;
	wait_queue_head_t wq;
	wait_queue_head_t wq_completion;
	atomic_t outstanding_operations;

	/* Per connection stats */
	ktime_t ts;
	struct gb_loopback_stats latency;
	struct gb_loopback_stats throughput;
	struct gb_loopback_stats requests_per_second;
	struct gb_loopback_stats apbridge_unipro_latency;
	struct gb_loopback_stats gbphy_firmware_latency;

	int type;
	int async;
	int id;
	u32 size;
	u32 iteration_max;
	u32 iteration_count;
	int us_wait;
	u32 error;
	u32 requests_completed;
	u32 requests_timedout;
	u32 timeout;
	u32 jiffy_timeout;
	u32 timeout_min;
	u32 timeout_max;
	u32 outstanding_operations_max;
	u64 elapsed_nsecs;
	u32 apbridge_latency_ts;
	u32 gbphy_latency_ts;

	u32 send_count;
};

static struct class loopback_class = {
	.name		= "gb_loopback",
	.owner		= THIS_MODULE,
};
static DEFINE_IDA(loopback_ida);

/* Min/max values in jiffies */
#define GB_LOOPBACK_TIMEOUT_MIN				1
#define GB_LOOPBACK_TIMEOUT_MAX				10000

#define GB_LOOPBACK_FIFO_DEFAULT			8192

static unsigned int kfifo_depth = GB_LOOPBACK_FIFO_DEFAULT;
module_param(kfifo_depth, uint, 0444);

/* Maximum size of any one send data buffer we support */
#define MAX_PACKET_SIZE (PAGE_SIZE * 2)

#define GB_LOOPBACK_US_WAIT_MAX				1000000

/* interface sysfs attributes */
#define gb_loopback_ro_attr(field)				\
static ssize_t field##_show(struct device *dev,			\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	return sprintf(buf, "%u\n", gb->field);			\
}									\
static DEVICE_ATTR_RO(field)

#define gb_loopback_ro_stats_attr(name, field, type)		\
static ssize_t name##_##field##_show(struct device *dev,	\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	/* Report 0 for min and max if no transfer successed */		\
	if (!gb->requests_completed)					\
		return sprintf(buf, "0\n");				\
	return sprintf(buf, "%"#type"\n", gb->name.field);	\
}									\
static DEVICE_ATTR_RO(name##_##field)

#define gb_loopback_ro_avg_attr(name)			\
static ssize_t name##_avg_show(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback_stats *stats;				\
	struct gb_loopback *gb;						\
	u64 avg, rem;							\
	u32 count;							\
	gb = dev_get_drvdata(dev);			\
	stats = &gb->name;					\
	count = stats->count ? stats->count : 1;			\
	avg = stats->sum + count / 2000000; /* round closest */		\
	rem = do_div(avg, count);					\
	rem *= 1000000;							\
	do_div(rem, count);						\
	return sprintf(buf, "%llu.%06u\n", avg, (u32)rem);		\
}									\
static DEVICE_ATTR_RO(name##_avg)

#define gb_loopback_stats_attrs(field)				\
	gb_loopback_ro_stats_attr(field, min, u);		\
	gb_loopback_ro_stats_attr(field, max, u);		\
	gb_loopback_ro_avg_attr(field)

#define gb_loopback_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	return sprintf(buf, "%"#type"\n", gb->field);			\
}									\
static ssize_t field##_store(struct device *dev,			\
			    struct device_attribute *attr,		\
			    const char *buf,				\
			    size_t len)					\
{									\
	int ret;							\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	mutex_lock(&gb->mutex);						\
	ret = sscanf(buf, "%"#type, &gb->field);			\
	if (ret != 1)							\
		len = -EINVAL;						\
	else								\
		gb_loopback_check_attr(gb, bundle);			\
	mutex_unlock(&gb->mutex);					\
	return len;							\
}									\
static DEVICE_ATTR_RW(field)

#define gb_dev_loopback_ro_attr(field, conn)				\
static ssize_t field##_show(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	return sprintf(buf, "%u\n", gb->field);				\
}									\
static DEVICE_ATTR_RO(field)

#define gb_dev_loopback_rw_attr(field, type)				\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	return sprintf(buf, "%"#type"\n", gb->field);			\
}									\
static ssize_t field##_store(struct device *dev,			\
			    struct device_attribute *attr,		\
			    const char *buf,				\
			    size_t len)					\
{									\
	int ret;							\
	struct gb_loopback *gb = dev_get_drvdata(dev);			\
	mutex_lock(&gb->mutex);						\
	ret = sscanf(buf, "%"#type, &gb->field);			\
	if (ret != 1)							\
		len = -EINVAL;						\
	else								\
		gb_loopback_check_attr(gb);		\
	mutex_unlock(&gb->mutex);					\
	return len;							\
}									\
static DEVICE_ATTR_RW(field)

static void gb_loopback_reset_stats(struct gb_loopback *gb);
static void gb_loopback_check_attr(struct gb_loopback *gb)
{
	if (gb->us_wait > GB_LOOPBACK_US_WAIT_MAX)
		gb->us_wait = GB_LOOPBACK_US_WAIT_MAX;
	if (gb->size > gb_dev.size_max)
		gb->size = gb_dev.size_max;
	gb->requests_timedout = 0;
	gb->requests_completed = 0;
	gb->iteration_count = 0;
	gb->send_count = 0;
	gb->error = 0;

	if (kfifo_depth < gb->iteration_max) {
		dev_warn(gb->dev,
			 "cannot log bytes %u kfifo_depth %u\n",
			 gb->iteration_max, kfifo_depth);
	}
	kfifo_reset_out(&gb->kfifo_lat);

	switch (gb->type) {
	case GB_LOOPBACK_TYPE_PING:
	case GB_LOOPBACK_TYPE_TRANSFER:
	case GB_LOOPBACK_TYPE_SINK:
		gb->jiffy_timeout = usecs_to_jiffies(gb->timeout);
		if (!gb->jiffy_timeout)
			gb->jiffy_timeout = GB_LOOPBACK_TIMEOUT_MIN;
		else if (gb->jiffy_timeout > GB_LOOPBACK_TIMEOUT_MAX)
			gb->jiffy_timeout = GB_LOOPBACK_TIMEOUT_MAX;
		gb_loopback_reset_stats(gb);
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
/* Latency across the UniPro link from APBridge's perspective */
gb_loopback_stats_attrs(apbridge_unipro_latency);
/* Firmware induced overhead in the GPBridge */
gb_loopback_stats_attrs(gbphy_firmware_latency);

/* Number of errors encountered during loop */
gb_loopback_ro_attr(error);
/* Number of requests successfully completed async */
gb_loopback_ro_attr(requests_completed);
/* Number of requests timed out async */
gb_loopback_ro_attr(requests_timedout);
/* Timeout minimum in useconds */
gb_loopback_ro_attr(timeout_min);
/* Timeout minimum in useconds */
gb_loopback_ro_attr(timeout_max);

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
gb_dev_loopback_rw_attr(us_wait, d);
/* Maximum iterations for a given operation: 1-(2^32-1), 0 implies infinite */
gb_dev_loopback_rw_attr(iteration_max, u);
/* The current index of the for (i = 0; i < iteration_max; i++) loop */
gb_dev_loopback_ro_attr(iteration_count, false);
/* A flag to indicate synchronous or asynchronous operations */
gb_dev_loopback_rw_attr(async, u);
/* Timeout of an individual asynchronous request */
gb_dev_loopback_rw_attr(timeout, u);
/* Maximum number of in-flight operations before back-off */
gb_dev_loopback_rw_attr(outstanding_operations_max, u);

static struct attribute *loopback_attrs[] = {
	&dev_attr_latency_min.attr,
	&dev_attr_latency_max.attr,
	&dev_attr_latency_avg.attr,
	&dev_attr_requests_per_second_min.attr,
	&dev_attr_requests_per_second_max.attr,
	&dev_attr_requests_per_second_avg.attr,
	&dev_attr_throughput_min.attr,
	&dev_attr_throughput_max.attr,
	&dev_attr_throughput_avg.attr,
	&dev_attr_apbridge_unipro_latency_min.attr,
	&dev_attr_apbridge_unipro_latency_max.attr,
	&dev_attr_apbridge_unipro_latency_avg.attr,
	&dev_attr_gbphy_firmware_latency_min.attr,
	&dev_attr_gbphy_firmware_latency_max.attr,
	&dev_attr_gbphy_firmware_latency_avg.attr,
	&dev_attr_type.attr,
	&dev_attr_size.attr,
	&dev_attr_us_wait.attr,
	&dev_attr_iteration_count.attr,
	&dev_attr_iteration_max.attr,
	&dev_attr_async.attr,
	&dev_attr_error.attr,
	&dev_attr_requests_completed.attr,
	&dev_attr_requests_timedout.attr,
	&dev_attr_timeout.attr,
	&dev_attr_outstanding_operations_max.attr,
	&dev_attr_timeout_min.attr,
	&dev_attr_timeout_max.attr,
	NULL,
};
ATTRIBUTE_GROUPS(loopback);

static void gb_loopback_calculate_stats(struct gb_loopback *gb, bool error);

static u32 gb_loopback_nsec_to_usec_latency(u64 elapsed_nsecs)
{
	do_div(elapsed_nsecs, NSEC_PER_USEC);
	return elapsed_nsecs;
}

static u64 __gb_loopback_calc_latency(u64 t1, u64 t2)
{
	if (t2 > t1)
		return t2 - t1;
	else
		return NSEC_PER_DAY - t2 + t1;
}

static u64 gb_loopback_calc_latency(ktime_t ts, ktime_t te)
{
	return __gb_loopback_calc_latency(ktime_to_ns(ts), ktime_to_ns(te));
}

static int gb_loopback_operation_sync(struct gb_loopback *gb, int type,
				      void *request, int request_size,
				      void *response, int response_size)
{
	struct gb_operation *operation;
	ktime_t ts, te;
	int ret;

	ts = ktime_get();
	operation = gb_operation_create(gb->connection, type, request_size,
					response_size, GFP_KERNEL);
	if (!operation)
		return -ENOMEM;

	if (request_size)
		memcpy(operation->request->payload, request, request_size);

	ret = gb_operation_request_send_sync(operation);
	if (ret) {
		dev_err(&gb->connection->bundle->dev,
			"synchronous operation failed: %d\n", ret);
		goto out_put_operation;
	} else {
		if (response_size == operation->response->payload_size) {
			memcpy(response, operation->response->payload,
			       response_size);
		} else {
			dev_err(&gb->connection->bundle->dev,
				"response size %zu expected %d\n",
				operation->response->payload_size,
				response_size);
			ret = -EINVAL;
			goto out_put_operation;
		}
	}

	te = ktime_get();

	/* Calculate the total time the message took */
	gb->elapsed_nsecs = gb_loopback_calc_latency(ts, te);

out_put_operation:
	gb_operation_put(operation);

	return ret;
}

static void gb_loopback_async_wait_all(struct gb_loopback *gb)
{
	wait_event(gb->wq_completion,
		   !atomic_read(&gb->outstanding_operations));
}

static void gb_loopback_async_operation_callback(struct gb_operation *operation)
{
	struct gb_loopback_async_operation *op_async;
	struct gb_loopback *gb;
	ktime_t te;
	int result;

	te = ktime_get();
	result = gb_operation_result(operation);
	op_async = gb_operation_get_data(operation);
	gb = op_async->gb;

	mutex_lock(&gb->mutex);

	if (!result && op_async->completion)
		result = op_async->completion(op_async);

	if (!result) {
		gb->elapsed_nsecs = gb_loopback_calc_latency(op_async->ts, te);
	} else {
		gb->error++;
		if (result == -ETIMEDOUT)
			gb->requests_timedout++;
	}

	gb->iteration_count++;
	gb_loopback_calculate_stats(gb, result);

	mutex_unlock(&gb->mutex);

	dev_dbg(&gb->connection->bundle->dev, "complete operation %d\n",
		operation->id);

	/* Wake up waiters */
	atomic_dec(&op_async->gb->outstanding_operations);
	wake_up(&gb->wq_completion);

	/* Release resources */
	gb_operation_put(operation);
	kfree(op_async);
}

static int gb_loopback_async_operation(struct gb_loopback *gb, int type,
				       void *request, int request_size,
				       int response_size,
				       void *completion)
{
	struct gb_loopback_async_operation *op_async;
	struct gb_operation *operation;
	int ret;

	op_async = kzalloc(sizeof(*op_async), GFP_KERNEL);
	if (!op_async)
		return -ENOMEM;

	operation = gb_operation_create(gb->connection, type, request_size,
					response_size, GFP_KERNEL);
	if (!operation) {
		kfree(op_async);
		return -ENOMEM;
	}

	if (request_size)
		memcpy(operation->request->payload, request, request_size);

	gb_operation_set_data(operation, op_async);

	op_async->gb = gb;
	op_async->operation = operation;
	op_async->completion = completion;

	op_async->ts = ktime_get();

	atomic_inc(&gb->outstanding_operations);
	ret = gb_operation_request_send(operation,
					gb_loopback_async_operation_callback,
					jiffies_to_msecs(gb->jiffy_timeout),
					GFP_KERNEL);
	if (ret) {
		atomic_dec(&gb->outstanding_operations);
		gb_operation_put(operation);
		kfree(op_async);
	}
	return ret;
}

static int gb_loopback_sync_sink(struct gb_loopback *gb, u32 len)
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

static int gb_loopback_sync_transfer(struct gb_loopback *gb, u32 len)
{
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	int retval;

	gb->apbridge_latency_ts = 0;
	gb->gbphy_latency_ts = 0;

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
	gb->gbphy_latency_ts = (u32)__le32_to_cpu(response->reserved1);

gb_error:
	kfree(request);
	kfree(response);

	return retval;
}

static int gb_loopback_sync_ping(struct gb_loopback *gb)
{
	return gb_loopback_operation_sync(gb, GB_LOOPBACK_TYPE_PING,
					  NULL, 0, NULL, 0);
}

static int gb_loopback_async_sink(struct gb_loopback *gb, u32 len)
{
	struct gb_loopback_transfer_request *request;
	int retval;

	request = kmalloc(len + sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->len = cpu_to_le32(len);
	retval = gb_loopback_async_operation(gb, GB_LOOPBACK_TYPE_SINK,
					     request, len + sizeof(*request),
					     0, NULL);
	kfree(request);
	return retval;
}

static int gb_loopback_async_transfer_complete(
				struct gb_loopback_async_operation *op_async)
{
	struct gb_loopback *gb;
	struct gb_operation *operation;
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	size_t len;
	int retval = 0;

	gb = op_async->gb;
	operation = op_async->operation;
	request = operation->request->payload;
	response = operation->response->payload;
	len = le32_to_cpu(request->len);

	if (memcmp(request->data, response->data, len)) {
		dev_err(&gb->connection->bundle->dev,
			"Loopback Data doesn't match operation id %d\n",
			operation->id);
		retval = -EREMOTEIO;
	} else {
		gb->apbridge_latency_ts =
			(u32)__le32_to_cpu(response->reserved0);
		gb->gbphy_latency_ts =
			(u32)__le32_to_cpu(response->reserved1);
	}

	return retval;
}

static int gb_loopback_async_transfer(struct gb_loopback *gb, u32 len)
{
	struct gb_loopback_transfer_request *request;
	int retval, response_len;

	request = kmalloc(len + sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	memset(request->data, 0x5A, len);

	request->len = cpu_to_le32(len);
	response_len = sizeof(struct gb_loopback_transfer_response);
	retval = gb_loopback_async_operation(gb, GB_LOOPBACK_TYPE_TRANSFER,
					     request, len + sizeof(*request),
					     len + response_len,
					     gb_loopback_async_transfer_complete);
	if (retval)
		goto gb_error;

gb_error:
	kfree(request);
	return retval;
}

static int gb_loopback_async_ping(struct gb_loopback *gb)
{
	return gb_loopback_async_operation(gb, GB_LOOPBACK_TYPE_PING,
					   NULL, 0, 0, NULL);
}

static int gb_loopback_request_handler(struct gb_operation *operation)
{
	struct gb_connection *connection = operation->connection;
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	struct device *dev = &connection->bundle->dev;
	size_t len;

	/* By convention, the AP initiates the version operation */
	switch (operation->type) {
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
		dev_err(dev, "unsupported request: %u\n", operation->type);
		return -EINVAL;
	}
}

static void gb_loopback_reset_stats(struct gb_loopback *gb)
{
	struct gb_loopback_stats reset = {
		.min = U32_MAX,
	};

	/* Reset per-connection stats */
	memcpy(&gb->latency, &reset,
	       sizeof(struct gb_loopback_stats));
	memcpy(&gb->throughput, &reset,
	       sizeof(struct gb_loopback_stats));
	memcpy(&gb->requests_per_second, &reset,
	       sizeof(struct gb_loopback_stats));
	memcpy(&gb->apbridge_unipro_latency, &reset,
	       sizeof(struct gb_loopback_stats));
	memcpy(&gb->gbphy_firmware_latency, &reset,
	       sizeof(struct gb_loopback_stats));

	/* Should be initialized at least once per transaction set */
	gb->apbridge_latency_ts = 0;
	gb->gbphy_latency_ts = 0;
	gb->ts = ktime_set(0, 0);
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

static void gb_loopback_update_stats_window(struct gb_loopback_stats *stats,
					    u64 val, u32 count)
{
	stats->sum += val;
	stats->count += count;

	do_div(val, count);
	if (stats->min > val)
		stats->min = val;
	if (stats->max < val)
		stats->max = val;
}

static void gb_loopback_requests_update(struct gb_loopback *gb, u32 latency)
{
	u64 req = gb->requests_completed * USEC_PER_SEC;

	gb_loopback_update_stats_window(&gb->requests_per_second, req, latency);
}

static void gb_loopback_throughput_update(struct gb_loopback *gb, u32 latency)
{
	u64 aggregate_size = sizeof(struct gb_operation_msg_hdr) * 2;

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

	aggregate_size *= gb->requests_completed;
	aggregate_size *= USEC_PER_SEC;
	gb_loopback_update_stats_window(&gb->throughput, aggregate_size,
					latency);
}

static void gb_loopback_calculate_latency_stats(struct gb_loopback *gb)
{
	u32 lat;

	/* Express latency in terms of microseconds */
	lat = gb_loopback_nsec_to_usec_latency(gb->elapsed_nsecs);

	/* Log latency stastic */
	gb_loopback_update_stats(&gb->latency, lat);

	/* Raw latency log on a per thread basis */
	kfifo_in(&gb->kfifo_lat, (unsigned char *)&lat, sizeof(lat));

	/* Log the firmware supplied latency values */
	gb_loopback_update_stats(&gb->apbridge_unipro_latency,
				 gb->apbridge_latency_ts);
	gb_loopback_update_stats(&gb->gbphy_firmware_latency,
				 gb->gbphy_latency_ts);
}

static void gb_loopback_calculate_stats(struct gb_loopback *gb, bool error)
{
	u64 nlat;
	u32 lat;
	ktime_t te;

	if (!error) {
		gb->requests_completed++;
		gb_loopback_calculate_latency_stats(gb);
	}

	te = ktime_get();
	nlat = gb_loopback_calc_latency(gb->ts, te);
	if (nlat >= NSEC_PER_SEC || gb->iteration_count == gb->iteration_max) {
		lat = gb_loopback_nsec_to_usec_latency(nlat);

		gb_loopback_throughput_update(gb, lat);
		gb_loopback_requests_update(gb, lat);

		if (gb->iteration_count != gb->iteration_max) {
			gb->ts = te;
			gb->requests_completed = 0;
		}
	}
}

static void gb_loopback_async_wait_to_send(struct gb_loopback *gb)
{
	if (!(gb->async && gb->outstanding_operations_max))
		return;
	wait_event_interruptible(gb->wq_completion,
				 (atomic_read(&gb->outstanding_operations) <
				  gb->outstanding_operations_max) ||
				  kthread_should_stop());
}

static int gb_loopback_fn(void *data)
{
	int error = 0;
	int us_wait = 0;
	int type;
	int ret;
	u32 size;

	struct gb_loopback *gb = data;
	struct gb_bundle *bundle = gb->connection->bundle;

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		return ret;

	while (1) {
		if (!gb->type) {
			gb_pm_runtime_put_autosuspend(bundle);
			wait_event_interruptible(gb->wq, gb->type ||
						 kthread_should_stop());
			ret = gb_pm_runtime_get_sync(bundle);
			if (ret)
				return ret;
		}

		if (kthread_should_stop())
			break;

		/* Limit the maximum number of in-flight async operations */
		gb_loopback_async_wait_to_send(gb);
		if (kthread_should_stop())
			break;

		mutex_lock(&gb->mutex);

		/* Optionally terminate */
		if (gb->send_count == gb->iteration_max) {
			mutex_unlock(&gb->mutex);

			/* Wait for synchronous and asynchronus completion */
			gb_loopback_async_wait_all(gb);

			/* Mark complete unless user-space has poked us */
			mutex_lock(&gb->mutex);
			if (gb->iteration_count == gb->iteration_max) {
				gb->type = 0;
				gb->send_count = 0;
				sysfs_notify(&gb->dev->kobj,  NULL,
						"iteration_count");
				dev_dbg(&bundle->dev, "load test complete\n");
			} else {
				dev_dbg(&bundle->dev,
					"continuing on with new test set\n");
			}
			mutex_unlock(&gb->mutex);
			continue;
		}
		size = gb->size;
		us_wait = gb->us_wait;
		type = gb->type;
		if (ktime_to_ns(gb->ts) == 0)
			gb->ts = ktime_get();

		/* Else operations to perform */
		if (gb->async) {
			if (type == GB_LOOPBACK_TYPE_PING)
				error = gb_loopback_async_ping(gb);
			else if (type == GB_LOOPBACK_TYPE_TRANSFER)
				error = gb_loopback_async_transfer(gb, size);
			else if (type == GB_LOOPBACK_TYPE_SINK)
				error = gb_loopback_async_sink(gb, size);

			if (error) {
				gb->error++;
				gb->iteration_count++;
			}
		} else {
			/* We are effectively single threaded here */
			if (type == GB_LOOPBACK_TYPE_PING)
				error = gb_loopback_sync_ping(gb);
			else if (type == GB_LOOPBACK_TYPE_TRANSFER)
				error = gb_loopback_sync_transfer(gb, size);
			else if (type == GB_LOOPBACK_TYPE_SINK)
				error = gb_loopback_sync_sink(gb, size);

			if (error)
				gb->error++;
			gb->iteration_count++;
			gb_loopback_calculate_stats(gb, !!error);
		}
		gb->send_count++;
		mutex_unlock(&gb->mutex);

		if (us_wait) {
			if (us_wait < 20000)
				usleep_range(us_wait, us_wait + 100);
			else
				msleep(us_wait / 1000);
		}
	}

	gb_pm_runtime_put_autosuspend(bundle);

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

#define DEBUGFS_NAMELEN 32

static int gb_loopback_probe(struct gb_bundle *bundle,
			     const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct gb_loopback *gb;
	struct device *dev;
	int retval;
	char name[DEBUGFS_NAMELEN];
	unsigned long flags;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_LOOPBACK)
		return -ENODEV;

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gb_loopback_request_handler);
	if (IS_ERR(connection)) {
		retval = PTR_ERR(connection);
		goto out_kzalloc;
	}

	gb->connection = connection;
	greybus_set_drvdata(bundle, gb);

	init_waitqueue_head(&gb->wq);
	init_waitqueue_head(&gb->wq_completion);
	atomic_set(&gb->outstanding_operations, 0);
	gb_loopback_reset_stats(gb);

	/* Reported values to user-space for min/max timeouts */
	gb->timeout_min = jiffies_to_usecs(GB_LOOPBACK_TIMEOUT_MIN);
	gb->timeout_max = jiffies_to_usecs(GB_LOOPBACK_TIMEOUT_MAX);

	if (!gb_dev.count) {
		/* Calculate maximum payload */
		gb_dev.size_max = gb_operation_get_payload_size_max(connection);
		if (gb_dev.size_max <=
			sizeof(struct gb_loopback_transfer_request)) {
			retval = -EINVAL;
			goto out_connection_destroy;
		}
		gb_dev.size_max -= sizeof(struct gb_loopback_transfer_request);
	}

	/* Create per-connection sysfs and debugfs data-points */
	snprintf(name, sizeof(name), "raw_latency_%s",
		 dev_name(&connection->bundle->dev));
	gb->file = debugfs_create_file(name, S_IFREG | 0444, gb_dev.root, gb,
				       &gb_loopback_debugfs_latency_ops);

	gb->id = ida_simple_get(&loopback_ida, 0, 0, GFP_KERNEL);
	if (gb->id < 0) {
		retval = gb->id;
		goto out_debugfs_remove;
	}

	retval = gb_connection_enable(connection);
	if (retval)
		goto out_ida_remove;

	dev = device_create_with_groups(&loopback_class,
					&connection->bundle->dev,
					MKDEV(0, 0), gb, loopback_groups,
					"gb_loopback%d", gb->id);
	if (IS_ERR(dev)) {
		retval = PTR_ERR(dev);
		goto out_connection_disable;
	}
	gb->dev = dev;

	/* Allocate kfifo */
	if (kfifo_alloc(&gb->kfifo_lat, kfifo_depth * sizeof(u32),
			  GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_conn;
	}
	/* Fork worker thread */
	mutex_init(&gb->mutex);
	gb->task = kthread_run(gb_loopback_fn, gb, "gb_loopback");
	if (IS_ERR(gb->task)) {
		retval = PTR_ERR(gb->task);
		goto out_kfifo;
	}

	spin_lock_irqsave(&gb_dev.lock, flags);
	gb_dev.count++;
	spin_unlock_irqrestore(&gb_dev.lock, flags);

	gb_connection_latency_tag_enable(connection);

	gb_pm_runtime_put_autosuspend(bundle);

	return 0;

out_kfifo:
	kfifo_free(&gb->kfifo_lat);
out_conn:
	device_unregister(dev);
out_connection_disable:
	gb_connection_disable(connection);
out_ida_remove:
	ida_simple_remove(&loopback_ida, gb->id);
out_debugfs_remove:
	debugfs_remove(gb->file);
out_connection_destroy:
	gb_connection_destroy(connection);
out_kzalloc:
	kfree(gb);

	return retval;
}

static void gb_loopback_disconnect(struct gb_bundle *bundle)
{
	struct gb_loopback *gb = greybus_get_drvdata(bundle);
	unsigned long flags;
	int ret;

	ret = gb_pm_runtime_get_sync(bundle);
	if (ret)
		gb_pm_runtime_get_noresume(bundle);

	gb_connection_disable(gb->connection);

	if (!IS_ERR_OR_NULL(gb->task))
		kthread_stop(gb->task);

	kfifo_free(&gb->kfifo_lat);
	gb_connection_latency_tag_disable(gb->connection);
	debugfs_remove(gb->file);

	/*
	 * FIXME: gb_loopback_async_wait_all() is redundant now, as connection
	 * is disabled at the beginning and so we can't have any more
	 * incoming/outgoing requests.
	 */
	gb_loopback_async_wait_all(gb);

	spin_lock_irqsave(&gb_dev.lock, flags);
	gb_dev.count--;
	spin_unlock_irqrestore(&gb_dev.lock, flags);

	device_unregister(gb->dev);
	ida_simple_remove(&loopback_ida, gb->id);

	gb_connection_destroy(gb->connection);
	kfree(gb);
}

static const struct greybus_bundle_id gb_loopback_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_LOOPBACK) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_loopback_id_table);

static struct greybus_driver gb_loopback_driver = {
	.name		= "loopback",
	.probe		= gb_loopback_probe,
	.disconnect	= gb_loopback_disconnect,
	.id_table	= gb_loopback_id_table,
};

static int loopback_init(void)
{
	int retval;

	spin_lock_init(&gb_dev.lock);
	gb_dev.root = debugfs_create_dir("gb_loopback", NULL);

	retval = class_register(&loopback_class);
	if (retval)
		goto err;

	retval = greybus_register(&gb_loopback_driver);
	if (retval)
		goto err_unregister;

	return 0;

err_unregister:
	class_unregister(&loopback_class);
err:
	debugfs_remove_recursive(gb_dev.root);
	return retval;
}
module_init(loopback_init);

static void __exit loopback_exit(void)
{
	debugfs_remove_recursive(gb_dev.root);
	greybus_deregister(&gb_loopback_driver);
	class_unregister(&loopback_class);
	ida_destroy(&loopback_ida);
}
module_exit(loopback_exit);

MODULE_LICENSE("GPL v2");
