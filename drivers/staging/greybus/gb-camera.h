/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Greybus Camera protocol driver.
 *
 * Copyright 2015 Google Inc.
 */
#ifndef __GB_CAMERA_H
#define __GB_CAMERA_H

#include <linux/v4l2-mediabus.h>

/* Input flags need to be set from the caller */
#define GB_CAMERA_IN_FLAG_TEST		(1 << 0)
/* Output flags returned */
#define GB_CAMERA_OUT_FLAG_ADJUSTED	(1 << 0)

/**
 * struct gb_camera_stream - Represents greybus camera stream.
 * @width: Stream width in pixels.
 * @height: Stream height in pixels.
 * @pixel_code: Media bus pixel code.
 * @vc: MIPI CSI virtual channel.
 * @dt: MIPI CSI data types. Most formats use a single data type, in which case
 *      the second element will be ignored.
 * @max_size: Maximum size of a frame in bytes. The camera module guarantees
 *            that all data between the Frame Start and Frame End packet for
 *            the associated virtual channel and data type(s) will not exceed
 *            this size.
 */
struct gb_camera_stream {
	unsigned int width;
	unsigned int height;
	enum v4l2_mbus_pixelcode pixel_code;
	unsigned int vc;
	unsigned int dt[2];
	unsigned int max_size;
};

/**
 * struct gb_camera_csi_params - CSI configuration parameters
 * @num_lanes: number of CSI data lanes
 * @clk_freq: CSI clock frequency in Hz
 */
struct gb_camera_csi_params {
	unsigned int num_lanes;
	unsigned int clk_freq;
};

/**
 * struct gb_camera_ops - Greybus camera operations, used by the Greybus camera
 *                        driver to expose operations to the host camera driver.
 * @capabilities: Retrieve camera capabilities and store them in the buffer
 *                'buf' capabilities. The buffer maximum size is specified by
 *                the caller in the 'size' parameter, and the effective
 *                capabilities size is returned from the function. If the buffer
 *                size is too small to hold the capabilities an error is
 *                returned and the buffer is left untouched.
 *
 * @configure_streams: Negotiate configuration and prepare the module for video
 *                     capture. The caller specifies the number of streams it
 *                     requests in the 'nstreams' argument and the associated
 *                     streams configurations in the 'streams' argument. The
 *                     GB_CAMERA_IN_FLAG_TEST 'flag' can be set to test a
 *                     configuration without applying it, otherwise the
 *                     configuration is applied by the module. The module can
 *                     decide to modify the requested configuration, including
 *                     using a different number of streams. In that case the
 *                     modified configuration won't be applied, the
 *                     GB_CAMERA_OUT_FLAG_ADJUSTED 'flag' will be set upon
 *                     return, and the modified configuration and number of
 *                     streams stored in 'streams' and 'array'. The module
 *                     returns its CSI-2 bus parameters in the 'csi_params'
 *                     structure in all cases.
 *
 * @capture: Submit a capture request. The supplied 'request_id' must be unique
 *           and higher than the IDs of all the previously submitted requests.
 *           The 'streams' argument specifies which streams are affected by the
 *           request in the form of a bitmask, with bits corresponding to the
 *           configured streams indexes. If the request contains settings, the
 *           'settings' argument points to the settings buffer and its size is
 *           specified by the 'settings_size' argument. Otherwise the 'settings'
 *           argument should be set to NULL and 'settings_size' to 0.
 *
 * @flush: Flush the capture requests queue. Return the ID of the last request
 *         that will processed by the device before it stops transmitting video
 *         frames. All queued capture requests with IDs higher than the returned
 *         ID will be dropped without being processed.
 */
struct gb_camera_ops {
	ssize_t (*capabilities)(void *priv, char *buf, size_t len);
	int (*configure_streams)(void *priv, unsigned int *nstreams,
			unsigned int *flags, struct gb_camera_stream *streams,
			struct gb_camera_csi_params *csi_params);
	int (*capture)(void *priv, u32 request_id,
		       unsigned int streams, unsigned int num_frames,
		       size_t settings_size, const void *settings);
	int (*flush)(void *priv, u32 *request_id);
};

/**
 * struct gb_camera_module - Represents greybus camera module.
 * @priv: Module private data, passed to all camera operations.
 * @ops: Greybus camera operation callbacks.
 * @interface_id: Interface id of the module.
 * @refcount: Reference counting object.
 * @release: Module release function.
 * @list: List entry in the camera modules list.
 */
struct gb_camera_module {
	void *priv;
	const struct gb_camera_ops *ops;

	unsigned int interface_id;
	struct kref refcount;
	void (*release)(struct kref *kref);
	struct list_head list; /* Global list */
};

#define gb_camera_call(f, op, args...)      \
	(!(f) ? -ENODEV : (((f)->ops->op) ?  \
	(f)->ops->op((f)->priv, ##args) : -ENOIOCTLCMD))

int gb_camera_register(struct gb_camera_module *module);
int gb_camera_unregister(struct gb_camera_module *module);

#endif /* __GB_CAMERA_H */
