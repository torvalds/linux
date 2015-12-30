/*
 * Greybus Camera protocol driver.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "es2.h"
#include "greybus.h"
#include "greybus_protocols.h"

enum gb_camera_debugs_buffer_id {
	GB_CAMERA_DEBUGFS_BUFFER_CAPABILITIES,
	GB_CAMERA_DEBUGFS_BUFFER_STREAMS,
	GB_CAMERA_DEBUGFS_BUFFER_CAPTURE,
	GB_CAMERA_DEBUGFS_BUFFER_FLUSH,
	GB_CAMERA_DEBUGFS_BUFFER_MAX,
};

struct gb_camera_debugfs_buffer {
	char data[PAGE_SIZE];
	size_t length;
};

/**
 * struct gb_camera - A Greybus Camera Device
 * @connection: the greybus connection for camera control
 * @data_connected: whether the data connection has been established
 * @debugfs: debugfs entries for camera protocol operations testing
 */
struct gb_camera {
	struct gb_connection *connection;
	bool data_connected;

	struct {
		struct dentry *root;
		struct gb_camera_debugfs_buffer *buffers;
	} debugfs;
};

struct gb_camera_stream_config {
	unsigned int width;
	unsigned int height;
	unsigned int format;
	unsigned int vc;
	unsigned int dt[2];
	unsigned int max_size;
};

#define ES2_APB_CDSI0_CPORT		16
#define ES2_APB_CDSI1_CPORT		17

#define GB_CAMERA_MAX_SETTINGS_SIZE	8192

#define gcam_dbg(gcam, format...) \
	dev_dbg(&gcam->connection->bundle->dev, format)
#define gcam_info(gcam, format...) \
	dev_info(&gcam->connection->bundle->dev, format)
#define gcam_err(gcam, format...) \
	dev_err(&gcam->connection->bundle->dev, format)

/* -----------------------------------------------------------------------------
 * Camera Protocol Operations
 */

static int gb_camera_configure_streams(struct gb_camera *gcam,
				       unsigned int nstreams,
				       struct gb_camera_stream_config *streams)
{
	struct gb_camera_configure_streams_request *req;
	struct gb_camera_configure_streams_response *resp;
	struct es2_ap_csi_config csi_cfg;
	unsigned int i;
	size_t req_size;
	size_t resp_size;
	int ret;

	if (nstreams > GB_CAMERA_MAX_STREAMS)
		return -EINVAL;

	req_size = sizeof(*req) + nstreams * sizeof(req->config[0]);
	resp_size = sizeof(*resp) + nstreams * sizeof(resp->config[0]);

	req = kmalloc(req_size, GFP_KERNEL);
	resp = kmalloc(resp_size, GFP_KERNEL);
	if (!req || !resp) {
		ret = -ENOMEM;
		goto done;
	}

	req->num_streams = cpu_to_le16(nstreams);
	req->padding = 0;

	for (i = 0; i < nstreams; ++i) {
		struct gb_camera_stream_config_request *cfg = &req->config[i];

		cfg->width = cpu_to_le16(streams[i].width);
		cfg->height = cpu_to_le16(streams[i].height);
		cfg->format = cpu_to_le16(streams[i].format);
		cfg->padding = 0;
	}

	ret = gb_operation_sync(gcam->connection,
				GB_CAMERA_TYPE_CONFIGURE_STREAMS,
				req, req_size, resp, resp_size);
	if (ret < 0)
		goto done;

	if (le16_to_cpu(resp->num_streams) > nstreams) {
		gcam_dbg(gcam, "got #streams %u > request %u\n",
			 le16_to_cpu(resp->num_streams), nstreams);
		ret = -EIO;
		goto done;
	}

	if (resp->padding != 0) {
		gcam_dbg(gcam, "response padding != 0");
		ret = -EIO;
		goto done;
	}

	for (i = 0; i < nstreams; ++i) {
		struct gb_camera_stream_config_response *cfg = &resp->config[i];

		streams[i].width = le16_to_cpu(cfg->width);
		streams[i].height = le16_to_cpu(cfg->height);
		streams[i].format = le16_to_cpu(cfg->format);
		streams[i].vc = cfg->virtual_channel;
		streams[i].dt[0] = cfg->data_type[0];
		streams[i].dt[1] = cfg->data_type[1];
		streams[i].max_size = le32_to_cpu(cfg->max_size);

		if (cfg->padding[0] || cfg->padding[1] || cfg->padding[2]) {
			gcam_dbg(gcam, "stream #%u padding != 0", i);
			ret = -EIO;
			goto done;
		}
	}

	/* Configure the CSI transmitter. Hardcode the parameters for now. */
	if (nstreams && !(resp->flags & GB_CAMERA_CONFIGURE_STREAMS_ADJUSTED)) {
		csi_cfg.csi_id = 1;
		csi_cfg.clock_mode = 0;
		csi_cfg.num_lanes = 2;
		csi_cfg.bus_freq = 250000000;

		ret = es2_ap_csi_setup(gcam->connection->hd, true, &csi_cfg);
	} else if (nstreams == 0) {
		csi_cfg.csi_id = 1;
		csi_cfg.clock_mode = 0;
		csi_cfg.num_lanes = 0;
		csi_cfg.bus_freq = 0;

		ret = es2_ap_csi_setup(gcam->connection->hd, false, &csi_cfg);
	}

	if (ret < 0)
		gcam_err(gcam, "failed to %s the CSI transmitter\n",
			 nstreams ? "start" : "stop");

	ret = le16_to_cpu(resp->num_streams);

done:
	kfree(req);
	kfree(resp);
	return ret;
}

static int gb_camera_capture(struct gb_camera *gcam, u32 request_id,
			     unsigned int streams, unsigned int num_frames,
			     size_t settings_size, const void *settings)
{
	struct gb_camera_capture_request *req;
	size_t req_size;

	if (settings_size > GB_CAMERA_MAX_SETTINGS_SIZE)
		return -EINVAL;

	req_size = sizeof(*req) + settings_size;
	req = kmalloc(req_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->request_id = cpu_to_le32(request_id);
	req->streams = streams;
	req->padding = 0;
	req->num_frames = cpu_to_le16(num_frames);
	memcpy(req->settings, settings, settings_size);

	return gb_operation_sync(gcam->connection, GB_CAMERA_TYPE_CAPTURE,
				 req, req_size, NULL, 0);
}

static int gb_camera_flush(struct gb_camera *gcam, u32 *request_id)
{
	struct gb_camera_flush_response resp;
	int ret;

	ret = gb_operation_sync(gcam->connection, GB_CAMERA_TYPE_FLUSH, NULL, 0,
				&resp, sizeof(resp));
	if (ret < 0)
		return ret;

	if (request_id)
		*request_id = le32_to_cpu(resp.request_id);

	return 0;
}

static int gb_camera_event_recv(u8 type, struct gb_operation *op)
{
	struct gb_camera *gcam = op->connection->private;
	struct gb_camera_metadata_request *payload;
	struct gb_message *request;

	if (type != GB_CAMERA_TYPE_METADATA) {
		gcam_err(gcam, "Unsupported unsolicited event: %u\n", type);
		return -EINVAL;
	}

	request = op->request;

	if (request->payload_size < sizeof(*payload)) {
		gcam_err(gcam, "Wrong event size received (%zu < %zu)\n",
			 request->payload_size, sizeof(*payload));
		return -EINVAL;
	}

	payload = request->payload;

	gcam_dbg(gcam, "received metadata for request %u, frame %u, stream %u\n",
		 payload->request_id, payload->frame_number, payload->stream);

	return 0;
}

/* -----------------------------------------------------------------------------
 * DebugFS
 */
static ssize_t gb_camera_debugfs_capabilities(struct gb_camera *gcam,
		char *buf, size_t len)
{
	return len;
}

static ssize_t gb_camera_debugfs_configure_streams(struct gb_camera *gcam,
		char *buf, size_t len)
{
	struct gb_camera_debugfs_buffer *buffer =
		&gcam->debugfs.buffers[GB_CAMERA_DEBUGFS_BUFFER_STREAMS];
	struct gb_camera_stream_config *streams;
	unsigned int nstreams;
	const char *sep = ";";
	unsigned int i;
	char *token;
	int ret;

	/* Retrieve number of streams to configure */
	token = strsep(&buf, sep);
	if (token == NULL)
		return -EINVAL;

	ret = kstrtouint(token, 10, &nstreams);
	if (ret < 0)
		return ret;

	if (nstreams > GB_CAMERA_MAX_STREAMS)
		return -EINVAL;

	/* For each stream to configure parse width, height and format */
	streams = kzalloc(nstreams * sizeof(*streams), GFP_KERNEL);
	if (!streams)
		return -ENOMEM;

	for (i = 0; i < nstreams; ++i) {
		struct gb_camera_stream_config *stream = &streams[i];

		/* width */
		token = strsep(&buf, ";");
		if (token == NULL) {
			ret = -EINVAL;
			goto done;
		}
		ret = kstrtouint(token, 10, &stream->width);
		if (ret < 0)
			goto done;

		/* height */
		token = strsep(&buf, ";");
		if (token == NULL)
			goto done;

		ret = kstrtouint(token, 10, &stream->height);
		if (ret < 0)
			goto done;

		/* Image format code */
		token = strsep(&buf, ";");
		if (token == NULL)
			goto done;

		ret = kstrtouint(token, 16, &stream->format);
		if (ret < 0)
			goto done;
	}

	ret = gb_camera_configure_streams(gcam, nstreams, streams);
	if (ret < 0)
		goto done;

	nstreams = ret;
	buffer->length = sprintf(buffer->data, "%u;", nstreams);

	for (i = 0; i < nstreams; ++i) {
		struct gb_camera_stream_config *stream = &streams[i];

		buffer->length += sprintf(buffer->data + buffer->length,
					  "%u;%u;%u;%u;%u;%u;%u;",
					  stream->width, stream->height,
					  stream->format, stream->vc,
					  stream->dt[0], stream->dt[1],
					  stream->max_size);
	}

	ret = len;

done:
	kfree(streams);
	return ret;
};

static ssize_t gb_camera_debugfs_capture(struct gb_camera *gcam,
		char *buf, size_t len)
{
	unsigned int request_id;
	unsigned int streams_mask;
	unsigned int num_frames;
	char *token;
	int ret;

	/* Request id */
	token = strsep(&buf, ";");
	if (token == NULL)
		return -EINVAL;
	ret = kstrtouint(token, 10, &request_id);
	if (ret < 0)
		return ret;

	/* Stream mask */
	token = strsep(&buf, ";");
	if (token == NULL)
		return -EINVAL;
	ret = kstrtouint(token, 16, &streams_mask);
	if (ret < 0)
		return ret;

	/* number of frames */
	token = strsep(&buf, ";");
	if (token == NULL)
		return -EINVAL;
	ret = kstrtouint(token, 10, &num_frames);
	if (ret < 0)
		return ret;

	ret = gb_camera_capture(gcam, request_id, streams_mask, num_frames, 0,
				NULL);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t gb_camera_debugfs_flush(struct gb_camera *gcam,
		char *buf, size_t len)
{
	struct gb_camera_debugfs_buffer *buffer =
		&gcam->debugfs.buffers[GB_CAMERA_DEBUGFS_BUFFER_FLUSH];
	unsigned int req_id;
	int ret;

	ret = gb_camera_flush(gcam, &req_id);
	if (ret < 0)
		return ret;

	buffer->length = sprintf(buffer->data, "%u", req_id);

	return len;
}

struct gb_camera_debugfs_entry {
	const char *name;
	unsigned int mask;
	unsigned int buffer;
	ssize_t (*execute)(struct gb_camera *gcam, char *buf, size_t len);
};

static const struct gb_camera_debugfs_entry gb_camera_debugfs_entries[] = {
	{
		.name = "capabilities",
		.mask = S_IFREG | S_IRUGO,
		.buffer = GB_CAMERA_DEBUGFS_BUFFER_CAPABILITIES,
		.execute = gb_camera_debugfs_capabilities,
	}, {
		.name = "configure_streams",
		.mask = S_IFREG | S_IRUGO | S_IWUGO,
		.buffer = GB_CAMERA_DEBUGFS_BUFFER_STREAMS,
		.execute = gb_camera_debugfs_configure_streams,
	}, {
		.name = "capture",
		.mask = S_IFREG | S_IRUGO | S_IWUGO,
		.buffer = GB_CAMERA_DEBUGFS_BUFFER_CAPTURE,
		.execute = gb_camera_debugfs_capture,
	}, {
		.name = "flush",
		.mask = S_IFREG | S_IRUGO | S_IWUGO,
		.buffer = GB_CAMERA_DEBUGFS_BUFFER_FLUSH,
		.execute = gb_camera_debugfs_flush,
	},
};

static ssize_t gb_camera_debugfs_read(struct file *file, char __user *buf,
				      size_t len, loff_t *offset)
{
	const struct gb_camera_debugfs_entry *op = file->private_data;
	struct gb_camera *gcam = file->f_inode->i_private;
	struct gb_camera_debugfs_buffer *buffer;
	ssize_t ret;

	/* For read-only entries the operation is triggered by a read. */
	if (!(op->mask & S_IWUGO)) {
		ret = op->execute(gcam, NULL, 0);
		if (ret < 0)
			return ret;
	}

	buffer = &gcam->debugfs.buffers[op->buffer];

	return simple_read_from_buffer(buf, len, offset, buffer->data,
				       buffer->length);
}

static ssize_t gb_camera_debugfs_write(struct file *file,
				       const char __user *buf, size_t len,
				       loff_t *offset)
{
	const struct gb_camera_debugfs_entry *op = file->private_data;
	struct gb_camera *gcam = file->f_inode->i_private;
	ssize_t ret;
	char *kbuf;

	if (len > 1024)
	       return -EINVAL;

	kbuf = kmalloc(len + 1, GFP_KERNEL);
	if (kbuf == NULL)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, len)) {
		ret = -EFAULT;
		goto done;
	}

	kbuf[len] = '\0';

	ret = op->execute(gcam, kbuf, len);

done:
	kfree(kbuf);
	return ret;
}

static int gb_camera_debugfs_open(struct inode *inode, struct file *file)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(gb_camera_debugfs_entries); ++i) {
		const struct gb_camera_debugfs_entry *entry =
			&gb_camera_debugfs_entries[i];

		if (!strcmp(file->f_path.dentry->d_iname, entry->name)) {
			file->private_data = (void *)entry;
			break;
		}
	}

	return 0;
}

static const struct file_operations gb_camera_debugfs_ops = {
	.open = gb_camera_debugfs_open,
	.read = gb_camera_debugfs_read,
	.write = gb_camera_debugfs_write,
};

static int gb_camera_debugfs_init(struct gb_camera *gcam)
{
	struct gb_connection *connection = gcam->connection;
	char dirname[27];
	unsigned int i;

	/*
	 * Create root debugfs entry and a file entry for each camera operation.
	 */
	snprintf(dirname, 27, "camera-%u.%u", connection->intf->interface_id,
		 connection->bundle->id);

	gcam->debugfs.root = debugfs_create_dir(dirname, gb_debugfs_get());
	if (IS_ERR(gcam->debugfs.root)) {
		gcam_err(gcam, "debugfs root create failed (%ld)\n",
			 PTR_ERR(gcam->debugfs.root));
		return PTR_ERR(gcam->debugfs.root);
	}

	gcam->debugfs.buffers = vmalloc(sizeof(*gcam->debugfs.buffers) *
					GB_CAMERA_DEBUGFS_BUFFER_MAX);
	if (!gcam->debugfs.buffers)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(gb_camera_debugfs_entries); ++i) {
		const struct gb_camera_debugfs_entry *entry =
			&gb_camera_debugfs_entries[i];
		struct dentry *dentry;

		gcam->debugfs.buffers[i].length = 0;

		dentry = debugfs_create_file(entry->name, entry->mask,
					     gcam->debugfs.root, gcam,
					     &gb_camera_debugfs_ops);
		if (IS_ERR(dentry)) {
			gcam_err(gcam,
				 "debugfs operation %s create failed (%ld)\n",
				 entry->name, PTR_ERR(gcam->debugfs.root));
			return PTR_ERR(dentry);
		}
	}

	return 0;
}

static void gb_camera_debugfs_cleanup(struct gb_camera *gcam)
{
	if (gcam->debugfs.root)
		debugfs_remove_recursive(gcam->debugfs.root);

	vfree(gcam->debugfs.buffers);
}

/* -----------------------------------------------------------------------------
 * Init & Cleanup
 */

static void gb_camera_cleanup(struct gb_camera *gcam)
{
	gb_camera_debugfs_cleanup(gcam);

	if (gcam->data_connected) {
		struct gb_interface *intf = gcam->connection->intf;
		struct gb_svc *svc = gcam->connection->hd->svc;

		gb_svc_connection_destroy(svc, intf->interface_id,
					  ES2_APB_CDSI0_CPORT, svc->ap_intf_id,
					  ES2_APB_CDSI1_CPORT);
	}

	kfree(gcam);
}

static int gb_camera_connection_init(struct gb_connection *connection)
{
	struct gb_svc *svc = connection->hd->svc;
	struct gb_camera *gcam;
	int ret;

	gcam = kzalloc(sizeof(*gcam), GFP_KERNEL);
	if (!gcam)
		return -ENOMEM;

	gcam->connection = connection;
	connection->private = gcam;

	/*
	 * Create the data connection between camera module CDSI0 and APB CDS1.
	 * The CPort IDs are hardcoded by the ES2 bridges.
	 */
	ret = gb_svc_connection_create(svc, connection->intf->interface_id,
				       ES2_APB_CDSI0_CPORT, svc->ap_intf_id,
				       ES2_APB_CDSI1_CPORT, false);
	if (ret < 0)
		goto error;

	ret = gb_svc_link_config(svc, connection->intf->interface_id,
				 GB_SVC_LINK_CONFIG_BURST_HS_A, 2, 2, 0);
	if (ret < 0)
		goto error;

	ret = gb_svc_link_config(svc, svc->ap_intf_id,
				 GB_SVC_LINK_CONFIG_BURST_HS_A, 2, 2, 0);
	if (ret < 0)
		goto error;

	gcam->data_connected = true;

	ret = gb_camera_debugfs_init(gcam);
	if (ret < 0)
		goto error;

	return 0;

error:
	gb_camera_cleanup(gcam);
	return ret;
}

static void gb_camera_connection_exit(struct gb_connection *connection)
{
	struct gb_camera *gcam = connection->private;

	gb_camera_cleanup(gcam);
}

static struct gb_protocol camera_protocol = {
	.name			= "camera",
	.id			= GREYBUS_PROTOCOL_CAMERA_MGMT,
	.major			= GB_CAMERA_VERSION_MAJOR,
	.minor			= GB_CAMERA_VERSION_MINOR,
	.connection_init	= gb_camera_connection_init,
	.connection_exit	= gb_camera_connection_exit,
	.request_recv		= gb_camera_event_recv,
};

gb_protocol_driver(&camera_protocol);

MODULE_LICENSE("GPL v2");
