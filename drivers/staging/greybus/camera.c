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

#include "gb-camera.h"
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
 * @debugfs: debugfs entries for camera protocol operations testing
 * @module: Greybus camera module registered to HOST processor.
 */
struct gb_camera {
	struct gb_bundle *bundle;
	struct gb_connection *connection;
	struct gb_connection *data_connection;

	struct {
		struct dentry *root;
		struct gb_camera_debugfs_buffer *buffers;
	} debugfs;

	struct gb_camera_module module;
};

struct gb_camera_stream_config {
	unsigned int width;
	unsigned int height;
	unsigned int format;
	unsigned int vc;
	unsigned int dt[2];
	unsigned int max_size;
};

struct gb_camera_fmt_map {
	enum v4l2_mbus_pixelcode mbus_code;
	unsigned int gb_format;
};

/* GB format to media code map */
static const struct gb_camera_fmt_map mbus_to_gbus_format[] = {
	{
		.mbus_code = V4L2_MBUS_FMT_UYVY8_1X16,
		.gb_format = 0x01,
	},
	{
		.mbus_code = V4L2_MBUS_FMT_NV12_1x8,
		.gb_format = 0x12,
	},
	{
		.mbus_code = V4L2_MBUS_FMT_NV21_1x8,
		.gb_format = 0x13,
	},
	{
		.mbus_code = V4L2_MBUS_FMT_YU12_1x8,
		.gb_format = 0x16,
	},
	{
		.mbus_code = V4L2_MBUS_FMT_YV12_1x8,
		.gb_format = 0x17,
	},
	{
		.mbus_code = V4L2_MBUS_FMT_JPEG_1X8,
		.gb_format = 0x40,
	},
	{
		.mbus_code = V4L2_MBUS_FMT_ARA_METADATA_1X8,
		.gb_format = 0x41,
	}
};

#define ES2_APB_CDSI0_CPORT		16
#define ES2_APB_CDSI1_CPORT		17

#define GB_CAMERA_MAX_SETTINGS_SIZE	8192

#define gcam_dbg(gcam, format...)	dev_dbg(&gcam->bundle->dev, format)
#define gcam_info(gcam, format...)	dev_info(&gcam->bundle->dev, format)
#define gcam_err(gcam, format...)	dev_err(&gcam->bundle->dev, format)

/* -----------------------------------------------------------------------------
 * Camera Protocol Operations
 */

static int gb_camera_set_intf_power_mode(struct gb_camera *gcam, u8 intf_id,
					 bool hs)
{
	struct gb_svc *svc = gcam->connection->hd->svc;
	int ret;

	if (hs)
		ret = gb_svc_intf_set_power_mode(svc, intf_id,
						 GB_SVC_UNIPRO_HS_SERIES_A,
						 GB_SVC_UNIPRO_FAST_MODE, 2, 2,
						 GB_SVC_UNIPRO_FAST_MODE, 2, 2,
						 GB_SVC_PWRM_RXTERMINATION |
						 GB_SVC_PWRM_TXTERMINATION, 0);
	else
		ret = gb_svc_intf_set_power_mode(svc, intf_id,
						 GB_SVC_UNIPRO_HS_SERIES_A,
						 GB_SVC_UNIPRO_SLOW_AUTO_MODE,
						 2, 1,
						 GB_SVC_UNIPRO_SLOW_AUTO_MODE,
						 2, 1,
						 0, 0);

	return ret;
}

static int gb_camera_set_power_mode(struct gb_camera *gcam, bool hs)
{
	struct gb_interface *intf = gcam->connection->intf;
	struct gb_svc *svc = gcam->connection->hd->svc;
	int ret;

	ret = gb_camera_set_intf_power_mode(gcam, intf->interface_id, hs);
	if (ret < 0) {
		gcam_err(gcam, "failed to set module interface to %s (%d)\n",
			 hs ? "HS" : "PWM", ret);
		return ret;
	}

	ret = gb_camera_set_intf_power_mode(gcam, svc->ap_intf_id, hs);
	if (ret < 0) {
		gb_camera_set_intf_power_mode(gcam, intf->interface_id, !hs);
		gcam_err(gcam, "failed to set AP interface to %s (%d)\n",
			 hs ? "HS" : "PWM", ret);
		return ret;
	}

	return 0;
}

static int gb_camera_capabilities(struct gb_camera *gcam,
				  u8 *capabilities, size_t *size)
{
	struct gb_operation *op;
	int ret;

	op = gb_operation_create_flags(gcam->connection,
				       GB_CAMERA_TYPE_CAPABILITIES, 0, *size,
				       GB_OPERATION_FLAG_SHORT_RESPONSE,
				       GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	ret = gb_operation_request_send_sync(op);
	if (ret) {
		gcam_err(gcam, "failed to retrieve capabilities: %d\n", ret);
		goto done;
	}

	memcpy(capabilities, op->response->payload, op->response->payload_size);
	*size = op->response->payload_size;

done:
	gb_operation_put(op);
	return ret;
}

struct ap_csi_config_request {
	__u8 csi_id;
	__u8 flags;
#define GB_CAMERA_CSI_FLAG_CLOCK_CONTINUOUS 0x01
	__u8 num_lanes;
	__u8 padding;
	__le32 bus_freq;
	__le32 lines_per_second;
} __packed;

static int gb_camera_configure_streams(struct gb_camera *gcam,
				       unsigned int *num_streams,
				       unsigned int *flags,
				       struct gb_camera_stream_config *streams,
				       struct gb_camera_csi_params *csi_params)
{
	struct gb_camera_configure_streams_request *req;
	struct gb_camera_configure_streams_response *resp;
	struct ap_csi_config_request csi_cfg;

	unsigned int nstreams = *num_streams;
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

	req->num_streams = nstreams;
	req->flags = *flags;
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

	if (resp->num_streams > nstreams) {
		gcam_dbg(gcam, "got #streams %u > request %u\n",
			 resp->num_streams, nstreams);
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

	if ((resp->flags & GB_CAMERA_CONFIGURE_STREAMS_ADJUSTED) ||
	    (*flags & GB_CAMERA_CONFIGURE_STREAMS_TEST_ONLY)) {
		*flags = resp->flags;
		*num_streams = resp->num_streams;
		goto done;
	}

	/* Setup unipro link speed. */
	ret = gb_camera_set_power_mode(gcam, nstreams != 0);
	if (ret < 0)
		goto done;

	/*
	 * Configure the APB1 CSI transmitter using the lines count reported by
	 * the  camera module, but with hard-coded bus frequency and lanes number.
	 *
	 * TODO: use the clocking and size informations reported by camera module
	 * to compute the required CSI bandwidth, and configure the CSI receiver
	 * on AP side, and the CSI transmitter on APB1 side accordingly.
	 */
	memset(&csi_cfg, 0, sizeof(csi_cfg));

	if (nstreams) {
		csi_cfg.csi_id = 1;
		csi_cfg.flags = 0;
		csi_cfg.num_lanes = resp->num_lanes;
		csi_cfg.bus_freq = cpu_to_le32(960000000);
		csi_cfg.lines_per_second = resp->lines_per_second;
		ret = gb_hd_output(gcam->connection->hd, &csi_cfg,
				   sizeof(csi_cfg),
				   GB_APB_REQUEST_CSI_TX_CONTROL, false);
		if (csi_params) {
			csi_params->num_lanes = csi_cfg.num_lanes;
			/* Transmitting two bits per cycle. (DDR clock) */
			csi_params->clk_freq = csi_cfg.bus_freq / 2;
			csi_params->lines_per_second = csi_cfg.lines_per_second;
		}
	} else {
		csi_cfg.csi_id = 1;
		ret = gb_hd_output(gcam->connection->hd, &csi_cfg,
				   sizeof(csi_cfg),
				   GB_APB_REQUEST_CSI_TX_CONTROL, false);
	}

	if (ret < 0)
		gcam_err(gcam, "failed to %s the CSI transmitter\n",
			 nstreams ? "start" : "stop");

	*flags = resp->flags;
	*num_streams = resp->num_streams;
	ret = 0;

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
	int ret;

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

	ret = gb_operation_sync(gcam->connection, GB_CAMERA_TYPE_CAPTURE,
				 req, req_size, NULL, 0);

	kfree(req);

	return ret;
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

static int gb_camera_request_handler(struct gb_operation *op)
{
	struct gb_camera *gcam = gb_connection_get_data(op->connection);
	struct gb_camera_metadata_request *payload;
	struct gb_message *request;

	if (op->type != GB_CAMERA_TYPE_METADATA) {
		gcam_err(gcam, "Unsupported unsolicited event: %u\n", op->type);
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
 * Interface with HOST ara camera.
 */
static unsigned int gb_camera_mbus_to_gb(enum v4l2_mbus_pixelcode mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mbus_to_gbus_format); i++) {
		if (mbus_to_gbus_format[i].mbus_code == mbus_code)
			return mbus_to_gbus_format[i].gb_format;
	}
	return mbus_to_gbus_format[0].gb_format;
}

static enum v4l2_mbus_pixelcode gb_camera_gb_to_mbus(u16 gb_fmt)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mbus_to_gbus_format); i++) {
		if (mbus_to_gbus_format[i].gb_format == gb_fmt)
			return mbus_to_gbus_format[i].mbus_code;
	}
	return mbus_to_gbus_format[0].mbus_code;
}

static ssize_t gb_camera_op_capabilities(void *priv, char *data, size_t len)
{
	struct gb_camera *gcam = priv;
	size_t capabilities_len = len;
	int ret;

	ret = gb_camera_capabilities(gcam, data, &capabilities_len);
	if (ret)
		return ret;

	return capabilities_len;
}

static int gb_camera_op_configure_streams(void *priv, unsigned int *nstreams,
		unsigned int *flags, struct gb_camera_stream *streams,
		struct gb_camera_csi_params *csi_params)
{
	struct gb_camera *gcam = priv;
	struct gb_camera_stream_config *gb_streams;
	unsigned int gb_flags = 0;
	unsigned int gb_nstreams = *nstreams;
	unsigned int i;
	int ret;

	if (gb_nstreams > GB_CAMERA_MAX_STREAMS)
		return -EINVAL;

	gb_streams = kzalloc(gb_nstreams * sizeof(*gb_streams), GFP_KERNEL);
	if (!gb_streams)
		return -ENOMEM;

	for (i = 0; i < gb_nstreams; i++) {
		gb_streams[i].width = streams[i].width;
		gb_streams[i].height = streams[i].height;
		gb_streams[i].format =
			gb_camera_mbus_to_gb(streams[i].pixel_code);
	}

	if (*flags & GB_CAMERA_IN_FLAG_TEST)
		gb_flags |= GB_CAMERA_CONFIGURE_STREAMS_TEST_ONLY;

	ret = gb_camera_configure_streams(gcam, &gb_nstreams,
					  &gb_flags, gb_streams, csi_params);
	if (ret < 0)
		goto done;
	if (gb_nstreams > *nstreams) {
		ret = -EINVAL;
		goto done;
	}

	*flags = 0;
	if (gb_flags & GB_CAMERA_CONFIGURE_STREAMS_ADJUSTED)
		*flags |= GB_CAMERA_OUT_FLAG_ADJUSTED;

	for (i = 0; i < gb_nstreams; i++) {
		streams[i].width = gb_streams[i].width;
		streams[i].height = gb_streams[i].height;
		streams[i].vc = gb_streams[i].vc;
		streams[i].dt[0] = gb_streams[i].dt[0];
		streams[i].dt[1] = gb_streams[i].dt[1];
		streams[i].max_size = gb_streams[i].max_size;
		streams[i].pixel_code =
			gb_camera_gb_to_mbus(gb_streams[i].format);
	}
	*nstreams = gb_nstreams;

done:
	kfree(gb_streams);
	return ret;
}

static int gb_camera_op_capture(void *priv, u32 request_id,
		unsigned int streams, unsigned int num_frames,
		size_t settings_size, const void *settings)
{
	struct gb_camera *gcam = priv;

	return gb_camera_capture(gcam, request_id, streams, num_frames,
				 settings_size, settings);
}

static int gb_camera_op_flush(void *priv, u32 *request_id)
{
	struct gb_camera *gcam = priv;

	return gb_camera_flush(gcam, request_id);
}

static const struct gb_camera_ops gb_cam_ops = {
	.capabilities = gb_camera_op_capabilities,
	.configure_streams = gb_camera_op_configure_streams,
	.capture = gb_camera_op_capture,
	.flush = gb_camera_op_flush,
};

static int gb_camera_register_intf_ops(struct gb_camera *gcam)
{
	gcam->module.priv = gcam;
	gcam->module.ops = &gb_cam_ops;
	return gb_camera_register(&gcam->module);
}

static int gb_camera_unregister_intf_ops(struct gb_camera *gcam)
{
	return gb_camera_unregister(&gcam->module);
}

/* -----------------------------------------------------------------------------
 * DebugFS
 */

static ssize_t gb_camera_debugfs_capabilities(struct gb_camera *gcam,
		char *buf, size_t len)
{
	struct gb_camera_debugfs_buffer *buffer =
		&gcam->debugfs.buffers[GB_CAMERA_DEBUGFS_BUFFER_CAPABILITIES];
	size_t size = 1024;
	unsigned int i;
	u8 *caps;
	int ret;

	caps = kmalloc(size, GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	ret = gb_camera_capabilities(gcam, caps, &size);
	if (ret < 0)
		goto done;

	/*
	 * hex_dump_to_buffer() doesn't return the number of bytes dumped prior
	 * to v4.0, we need our own implementation :-(
	 */
	buffer->length = 0;

	for (i = 0; i < size; i += 16) {
		unsigned int nbytes = min_t(unsigned int, size - i, 16);

		buffer->length += sprintf(buffer->data + buffer->length,
					  "%*ph\n", nbytes, caps + i);
	}

done:
	kfree(caps);
	return ret;
}

static ssize_t gb_camera_debugfs_configure_streams(struct gb_camera *gcam,
		char *buf, size_t len)
{
	struct gb_camera_debugfs_buffer *buffer =
		&gcam->debugfs.buffers[GB_CAMERA_DEBUGFS_BUFFER_STREAMS];
	struct gb_camera_stream_config *streams;
	unsigned int nstreams;
	unsigned int flags;
	unsigned int i;
	char *token;
	int ret;

	/* Retrieve number of streams to configure */
	token = strsep(&buf, ";");
	if (token == NULL)
		return -EINVAL;

	ret = kstrtouint(token, 10, &nstreams);
	if (ret < 0)
		return ret;

	if (nstreams > GB_CAMERA_MAX_STREAMS)
		return -EINVAL;

	token = strsep(&buf, ";");
	if (token == NULL)
		return -EINVAL;

	ret = kstrtouint(token, 10, &flags);
	if (ret < 0)
		return ret;

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

	ret = gb_camera_configure_streams(gcam, &nstreams, &flags, streams,
					  NULL);
	if (ret < 0)
		goto done;

	buffer->length = sprintf(buffer->data, "%u;%u;", nstreams, flags);

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
		 gcam->bundle->id);

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

	if (gcam->data_connection) {
		gb_connection_disable(gcam->data_connection);
		gb_connection_destroy(gcam->data_connection);
	}

	if (gcam->connection) {
		gb_connection_disable(gcam->connection);
		gb_connection_destroy(gcam->connection);
	}

	kfree(gcam);
}

static int gb_camera_probe(struct gb_bundle *bundle,
			   const struct greybus_bundle_id *id)
{
	struct gb_connection *conn;
	struct gb_camera *gcam;
	u16 mgmt_cport_id = 0;
	u16 data_cport_id = 0;
	unsigned int i;
	int ret;

	/*
	 * The camera bundle must contain exactly two CPorts, one for the
	 * camera management protocol and one for the camera data protocol.
	 */
	if (bundle->num_cports != 2)
		return -ENODEV;

	for (i = 0; i < bundle->num_cports; ++i) {
		struct greybus_descriptor_cport *desc = &bundle->cport_desc[i];

		switch (desc->protocol_id) {
		case GREYBUS_PROTOCOL_CAMERA_MGMT:
			mgmt_cport_id = le16_to_cpu(desc->id);
			break;
		case GREYBUS_PROTOCOL_CAMERA_DATA:
			data_cport_id = le16_to_cpu(desc->id);
			break;
		default:
			return -ENODEV;
		}
	}

	if (!mgmt_cport_id || !data_cport_id)
		return -ENODEV;

	gcam = kzalloc(sizeof(*gcam), GFP_KERNEL);
	if (!gcam)
		return -ENOMEM;

	gcam->bundle = bundle;

	conn = gb_connection_create(bundle, mgmt_cport_id,
				    gb_camera_request_handler);
	if (IS_ERR(conn)) {
		ret = PTR_ERR(conn);
		goto error;
	}

	gcam->connection = conn;
	gb_connection_set_data(conn, gcam);

	ret = gb_connection_enable(conn);
	if (ret)
		goto error;

	/*
	 * Create the data connection between the camera module data CPort and
	 * APB CDSI1. The CDSI1 CPort ID is hardcoded by the ES2 bridge.
	 */
	conn = gb_connection_create_offloaded(bundle, data_cport_id,
					      GB_CONNECTION_FLAG_NO_FLOWCTRL |
					      GB_CONNECTION_FLAG_CDSI1);
	if (IS_ERR(conn)) {
		ret = PTR_ERR(conn);
		goto error;
	}
	gcam->data_connection = conn;
	gb_connection_set_data(conn, gcam);

	ret = gb_connection_enable(conn);
	if (ret)
		goto error;

	ret = gb_camera_debugfs_init(gcam);
	if (ret < 0)
		goto error;

	ret = gb_camera_register_intf_ops(gcam);
	if (ret < 0)
		goto error;

	greybus_set_drvdata(bundle, gcam);

	return 0;

error:
	gb_camera_cleanup(gcam);
	return ret;
}

static void gb_camera_disconnect(struct gb_bundle *bundle)
{
	struct gb_camera *gcam = greybus_get_drvdata(bundle);

	gb_camera_unregister_intf_ops(gcam);
	gb_camera_cleanup(gcam);
}

static const struct greybus_bundle_id gb_camera_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_CAMERA) },
	{ },
};

static struct greybus_driver gb_camera_driver = {
	.name		= "camera",
	.probe		= gb_camera_probe,
	.disconnect	= gb_camera_disconnect,
	.id_table	= gb_camera_id_table,
};

module_greybus_driver(gb_camera_driver);

MODULE_LICENSE("GPL v2");
