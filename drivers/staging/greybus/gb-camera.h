/*
 * Greybus Camera protocol driver.
 *
 * Copyright 2015 Google Inc.
 *
 * Released under the GPLv2 only.
 */
#ifndef __GB_CAMERA_H
#define __GB_CAMERA_H

#include <linux/v4l2-mediabus.h>

struct gb_camera_stream {
	unsigned int width;
	unsigned int height;
	enum v4l2_mbus_pixelcode pixel_code;
	unsigned int vc;
	unsigned int dt[2];
	unsigned int max_size;
};

struct gb_camera_ops {
	ssize_t (*capabilities)(void *priv, char *buf, size_t len);
	int (*configure_streams)(void *priv, unsigned int nstreams,
				struct gb_camera_stream *streams);
	int (*capture)(void *priv, u32 request_id,
			unsigned int streams, unsigned int num_frames,
			size_t settings_size, const void *settings);
	int (*flush)(void *priv, u32 *request_id);
};

#define gb_camera_call(f, p, op, args...)             \
	(((f)->op) ? (f)->op(p, ##args) : -ENOIOCTLCMD)

int gb_camera_register(struct gb_camera_ops *ops, void *priv);

#endif /* __GB_CAMERA_H */
