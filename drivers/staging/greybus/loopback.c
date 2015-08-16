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

#include <asm/div64.h>

#include "greybus.h"

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
};

static struct gb_loopback_device gb_dev;

struct gb_loopback {
	struct gb_connection *connection;

	struct dentry *file;
	struct kfifo kfifo;
	struct mutex mutex;
	struct task_struct *task;
	wait_queue_head_t wq;

	int type;
	u32 size;
	u32 iteration_max;
	u32 iteration_count;
	size_t size_max;
	int ms_wait;

	struct gb_loopback_stats latency;
	struct gb_loopback_stats latency_gb;
	struct gb_loopback_stats throughput;
	struct gb_loopback_stats requests_per_second;
	u64 elapsed_nsecs;
	u64 elapsed_nsecs_gb;
	u32 error;
};

#define GB_LOOPBACK_FIFO_DEFAULT			8192

static unsigned kfifo_depth = GB_LOOPBACK_FIFO_DEFAULT;
module_param(kfifo_depth, uint, 0444);

/* Maximum size of any one send data buffer we support */
#define MAX_PACKET_SIZE (PAGE_SIZE * 2)

#define GB_LOOPBACK_MS_WAIT_MAX				1000

/* interface sysfs attributes */
#define gb_loopback_ro_attr(field)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb = connection->private;			\
	return sprintf(buf, "%u\n", gb->field);				\
}									\
static DEVICE_ATTR_RO(field)

#define gb_loopback_ro_stats_attr(name, field, type)			\
static ssize_t name##_##field##_show(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb = connection->private;			\
	return sprintf(buf, "%"#type"\n", gb->name.field);		\
}									\
static DEVICE_ATTR_RO(name##_##field)

#define gb_loopback_ro_avg_attr(name)					\
static ssize_t name##_avg_show(struct device *dev,			\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb = connection->private;			\
	struct gb_loopback_stats *stats = &gb->name;			\
	u32 count = stats->count ? stats->count : 1;			\
	u64 avg = stats->sum + count / 2;	/* round closest */	\
	u32 rem = do_div(avg, count);					\
	return sprintf(buf, "%llu.%06u\n", avg, 1000000 * rem / count);	\
}									\
static DEVICE_ATTR_RO(name##_avg)

#define gb_loopback_stats_attrs(field)					\
	gb_loopback_ro_stats_attr(field, min, u);			\
	gb_loopback_ro_stats_attr(field, max, u);			\
	gb_loopback_ro_avg_attr(field);

#define gb_loopback_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb = connection->private;			\
	return sprintf(buf, "%"#type"\n", gb->field);			\
}									\
static ssize_t field##_store(struct device *dev,			\
			    struct device_attribute *attr,		\
			    const char *buf,				\
			    size_t len)					\
{									\
	int ret;							\
	struct gb_connection *connection = to_gb_connection(dev);	\
	struct gb_loopback *gb = connection->private;			\
	mutex_lock(&gb->mutex);						\
	ret = sscanf(buf, "%"#type, &gb->field);			\
	if (ret != 1)							\
		len = -EINVAL;						\
	else								\
		gb_loopback_check_attr(connection, gb);			\
	mutex_unlock(&gb->mutex);					\
	return len;							\
}									\
static DEVICE_ATTR_RW(field)

static void gb_loopback_reset_stats(struct gb_loopback *gb);
static void gb_loopback_check_attr(struct gb_connection *connection,
				   struct gb_loopback *gb)
{
	if (gb->ms_wait > GB_LOOPBACK_MS_WAIT_MAX)
		gb->ms_wait = GB_LOOPBACK_MS_WAIT_MAX;
	if (gb->size > gb->size_max)
		gb->size = gb->size_max;
	gb->error = 0;
	gb->iteration_count = 0;
	gb_loopback_reset_stats(gb);

	if (kfifo_depth < gb->iteration_max) {
		dev_warn(&connection->dev,
			 "iteration_max %u kfifo_depth %u cannot log all data\n",
			 gb->iteration_max, kfifo_depth);
	}

	switch (gb->type) {
	case GB_LOOPBACK_TYPE_PING:
	case GB_LOOPBACK_TYPE_TRANSFER:
	case GB_LOOPBACK_TYPE_SINK:
		kfifo_reset_out(&gb->kfifo);
		wake_up(&gb->wq);
		break;
	default:
		gb->type = 0;
		break;
	}
}

/* Time to send and receive one message */
gb_loopback_stats_attrs(latency);
/* Time to send and receive one message not including greybus */
gb_loopback_stats_attrs(latency_gb);
/* Number of requests sent per second on this cport */
gb_loopback_stats_attrs(requests_per_second);
/* Quantity of data sent and received on this cport */
gb_loopback_stats_attrs(throughput);
/* Number of errors encountered during loop */
gb_loopback_ro_attr(error);
/* The current index of the for (i = 0; i < iteration_max; i++) loop */
gb_loopback_ro_attr(iteration_count);

/*
 * Type of loopback message to send based on protocol type definitions
 * 0 => Don't send message
 * 2 => Send ping message continuously (message without payload)
 * 3 => Send transfer message continuously (message with payload,
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
	dev_stats_attrs(latency_gb),
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

static u64 gb_loopback_calc_latency(struct timeval *ts, struct timeval *te)
{
	u64 t1, t2;

	t1 = timeval_to_ns(ts);
	t2 = timeval_to_ns(te);
	if (t2 > t1)
		return t2 - t1;
	else
		return NSEC_PER_DAY - t2 + t1;
}

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

	/* Calculate the total time the message took */
	gb->elapsed_nsecs = gb_loopback_calc_latency(&ts, &te);

	/* Calculate non-greybus related component of the latency */
	gb_connection_pop_timestamp(gb->connection, &ts);
	gb_connection_pop_timestamp(gb->connection, &te);
	gb->elapsed_nsecs_gb = gb_loopback_calc_latency(&ts, &te);


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

	memset(request->data, 0x5A, len);

	request->len = cpu_to_le32(len);

	do_gettimeofday(&ts);
	retval = gb_operation_sync(gb->connection, GB_LOOPBACK_TYPE_TRANSFER,
				   request, len + sizeof(*request),
				   response, len + sizeof(*response));
	do_gettimeofday(&te);

	/* Calculate the total time the message took */
	gb->elapsed_nsecs = gb_loopback_calc_latency(&ts, &te);

	/* Calculate non-greybus related component of the latency */
	gb_connection_pop_timestamp(gb->connection, &ts);
	gb_connection_pop_timestamp(gb->connection, &te);
	gb->elapsed_nsecs_gb = gb_loopback_calc_latency(&ts, &te);

	if (retval)
		goto gb_error;

	if (memcmp(request->data, response->data, len)) {
		pr_info("%s: Loopback Data doesn't match\n", __func__);
		retval = -EREMOTEIO;
	}

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

	/* Calculate the total time the message took */
	gb->elapsed_nsecs = gb_loopback_calc_latency(&ts, &te);

	/* Calculate non-greybus related component of the latency */
	gb_connection_pop_timestamp(gb->connection, &ts);
	gb_connection_pop_timestamp(gb->connection, &te);
	gb->elapsed_nsecs_gb = gb_loopback_calc_latency(&ts, &te);

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
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
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
		.min = U32_MAX,
	};
	memcpy(&gb->latency, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb->latency_gb, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb->throughput, &reset, sizeof(struct gb_loopback_stats));
	memcpy(&gb->requests_per_second, &reset,
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

	/* Log latency statistic */
	gb_loopback_update_stats(&gb->latency, lat);
	kfifo_in(&gb->kfifo, (unsigned char *)&lat, sizeof(lat));

	/* Log throughput and requests using latency as benchmark */
	gb_loopback_throughput_update(gb, lat);
	gb_loopback_requests_update(gb, lat);

	/* Calculate the greybus related latency number in nanoseconds */
	tmp = gb->elapsed_nsecs - gb->elapsed_nsecs_gb;
	lat = tmp;
	gb_loopback_update_stats(&gb->latency_gb, lat);
}

static int gb_loopback_fn(void *data)
{
	int error = 0;
	int ms_wait;
	struct gb_loopback *gb = data;

	while (1) {
		if (!gb->type)
			wait_event_interruptible(gb->wq, gb->type ||
						 kthread_should_stop());
		if (kthread_should_stop())
			break;

		mutex_lock(&gb->mutex);
		if (gb->iteration_max) {
			if (gb->iteration_count < gb->iteration_max) {
				gb->iteration_count++;
				sysfs_notify(&gb->connection->dev.kobj, NULL,
					     "iteration_count");
			} else {
				gb->type = 0;
				mutex_unlock(&gb->mutex);
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
		ms_wait = gb->ms_wait;
		mutex_unlock(&gb->mutex);
		if (ms_wait)
			msleep(ms_wait);
	}
	return 0;
}

static int gb_loopback_dbgfs_latency_show(struct seq_file *s, void *unused)
{
	struct gb_loopback *gb = s->private;
	u32 latency;
	int retval;

	if (kfifo_len(&gb->kfifo) == 0) {
		retval = -EAGAIN;
		goto done;
	}

	mutex_lock(&gb->mutex);
	retval = kfifo_out(&gb->kfifo, &latency, sizeof(latency));
	if (retval > 0) {
		seq_printf(s, "%u", latency);
		retval = 0;
	}
	mutex_unlock(&gb->mutex);
done:
	return retval;
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

#define DEBUGFS_NAMELEN 24

static int gb_loopback_connection_init(struct gb_connection *connection)
{
	struct gb_loopback *gb;
	int retval;
	char name[DEBUGFS_NAMELEN];

	gb = kzalloc(sizeof(*gb), GFP_KERNEL);
	if (!gb)
		return -ENOMEM;
	gb_loopback_reset_stats(gb);

	snprintf(name, sizeof(name), "raw_latency_endo0:%d:%d:%d:%d",
		connection->bundle->intf->module->module_id,
		connection->bundle->intf->interface_id,
		connection->bundle->id,
		connection->intf_cport_id);
	gb->file = debugfs_create_file(name, S_IFREG | S_IRUGO, gb_dev.root, gb,
				       &gb_loopback_debugfs_latency_ops);
	gb->connection = connection;
	connection->private = gb;
	retval = sysfs_create_groups(&connection->dev.kobj, loopback_groups);
	if (retval)
		goto out_debugfs;

	/* Calculate maximum payload */
	gb->size_max = gb_operation_get_payload_size_max(connection);
	if (gb->size_max <= sizeof(struct gb_loopback_transfer_request)) {
		retval = -EINVAL;
		goto out_sysfs;
	}
	gb->size_max -= sizeof(struct gb_loopback_transfer_request);

	/* Allocate kfifo */
	if (kfifo_alloc(&gb->kfifo, kfifo_depth * sizeof(u32),
			  GFP_KERNEL)) {
		retval = -ENOMEM;
		goto out_sysfs;
	}

	/* Fork worker thread */
	init_waitqueue_head(&gb->wq);
	mutex_init(&gb->mutex);
	gb->task = kthread_run(gb_loopback_fn, gb, "gb_loopback");
	if (IS_ERR(gb->task)) {
		retval = PTR_ERR(gb->task);
		goto out_kfifo;
	}

	gb_dev.count++;
	return 0;

out_kfifo:
	kfifo_free(&gb->kfifo);
out_sysfs:
	sysfs_remove_groups(&connection->dev.kobj, loopback_groups);
out_debugfs:
	debugfs_remove(gb->file);
	connection->private = NULL;
	kfree(gb);

	return retval;
}

static void gb_loopback_connection_exit(struct gb_connection *connection)
{
	struct gb_loopback *gb = connection->private;

	gb_dev.count--;
	connection->private = NULL;
	if (!IS_ERR_OR_NULL(gb->task))
		kthread_stop(gb->task);

	kfifo_free(&gb->kfifo);
	sysfs_remove_groups(&connection->dev.kobj, loopback_groups);
	debugfs_remove(gb->file);
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
	gb_dev.root = debugfs_create_dir("gb_loopback", NULL);
	return gb_protocol_register(&loopback_protocol);
}
module_init(loopback_init);

static void __exit loopback_exit(void)
{
	debugfs_remove_recursive(gb_dev.root);
	gb_protocol_deregister(&loopback_protocol);
}
module_exit(loopback_exit);

MODULE_LICENSE("GPL v2");
