// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Microsoft
 *
 * Portions of this code is derived from hyperv_fb.c
 */

#include <linux/hyperv.h>

#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>

#include "hyperv_drm.h"

#define VMBUS_RING_BUFSIZE (256 * 1024)
#define VMBUS_VSP_TIMEOUT (10 * HZ)

#define SYNTHVID_VERSION(major, minor) ((minor) << 16 | (major))
#define SYNTHVID_VER_GET_MAJOR(ver) (ver & 0x0000ffff)
#define SYNTHVID_VER_GET_MINOR(ver) ((ver & 0xffff0000) >> 16)
#define SYNTHVID_VERSION_WIN7 SYNTHVID_VERSION(3, 0)
#define SYNTHVID_VERSION_WIN8 SYNTHVID_VERSION(3, 2)
#define SYNTHVID_VERSION_WIN10 SYNTHVID_VERSION(3, 5)

#define SYNTHVID_DEPTH_WIN7 16
#define SYNTHVID_DEPTH_WIN8 32
#define SYNTHVID_FB_SIZE_WIN7 (4 * 1024 * 1024)
#define SYNTHVID_FB_SIZE_WIN8 (8 * 1024 * 1024)
#define SYNTHVID_WIDTH_MAX_WIN7 1600
#define SYNTHVID_HEIGHT_MAX_WIN7 1200

enum pipe_msg_type {
	PIPE_MSG_INVALID,
	PIPE_MSG_DATA,
	PIPE_MSG_MAX
};

enum synthvid_msg_type {
	SYNTHVID_ERROR			= 0,
	SYNTHVID_VERSION_REQUEST	= 1,
	SYNTHVID_VERSION_RESPONSE	= 2,
	SYNTHVID_VRAM_LOCATION		= 3,
	SYNTHVID_VRAM_LOCATION_ACK	= 4,
	SYNTHVID_SITUATION_UPDATE	= 5,
	SYNTHVID_SITUATION_UPDATE_ACK	= 6,
	SYNTHVID_POINTER_POSITION	= 7,
	SYNTHVID_POINTER_SHAPE		= 8,
	SYNTHVID_FEATURE_CHANGE		= 9,
	SYNTHVID_DIRT			= 10,
	SYNTHVID_RESOLUTION_REQUEST	= 13,
	SYNTHVID_RESOLUTION_RESPONSE	= 14,

	SYNTHVID_MAX			= 15
};

struct pipe_msg_hdr {
	u32 type;
	u32 size; /* size of message after this field */
} __packed;

struct hvd_screen_info {
	u16 width;
	u16 height;
} __packed;

struct synthvid_msg_hdr {
	u32 type;
	u32 size;  /* size of this header + payload after this field */
} __packed;

struct synthvid_version_req {
	u32 version;
} __packed;

struct synthvid_version_resp {
	u32 version;
	u8 is_accepted;
	u8 max_video_outputs;
} __packed;

struct synthvid_vram_location {
	u64 user_ctx;
	u8 is_vram_gpa_specified;
	u64 vram_gpa;
} __packed;

struct synthvid_vram_location_ack {
	u64 user_ctx;
} __packed;

struct video_output_situation {
	u8 active;
	u32 vram_offset;
	u8 depth_bits;
	u32 width_pixels;
	u32 height_pixels;
	u32 pitch_bytes;
} __packed;

struct synthvid_situation_update {
	u64 user_ctx;
	u8 video_output_count;
	struct video_output_situation video_output[1];
} __packed;

struct synthvid_situation_update_ack {
	u64 user_ctx;
} __packed;

struct synthvid_pointer_position {
	u8 is_visible;
	u8 video_output;
	s32 image_x;
	s32 image_y;
} __packed;

#define SYNTHVID_CURSOR_MAX_X 96
#define SYNTHVID_CURSOR_MAX_Y 96
#define SYNTHVID_CURSOR_ARGB_PIXEL_SIZE 4
#define SYNTHVID_CURSOR_MAX_SIZE (SYNTHVID_CURSOR_MAX_X * \
	SYNTHVID_CURSOR_MAX_Y * SYNTHVID_CURSOR_ARGB_PIXEL_SIZE)
#define SYNTHVID_CURSOR_COMPLETE (-1)

struct synthvid_pointer_shape {
	u8 part_idx;
	u8 is_argb;
	u32 width; /* SYNTHVID_CURSOR_MAX_X at most */
	u32 height; /* SYNTHVID_CURSOR_MAX_Y at most */
	u32 hot_x; /* hotspot relative to upper-left of pointer image */
	u32 hot_y;
	u8 data[4];
} __packed;

struct synthvid_feature_change {
	u8 is_dirt_needed;
	u8 is_ptr_pos_needed;
	u8 is_ptr_shape_needed;
	u8 is_situ_needed;
} __packed;

struct rect {
	s32 x1, y1; /* top left corner */
	s32 x2, y2; /* bottom right corner, exclusive */
} __packed;

struct synthvid_dirt {
	u8 video_output;
	u8 dirt_count;
	struct rect rect[1];
} __packed;

#define SYNTHVID_EDID_BLOCK_SIZE	128
#define	SYNTHVID_MAX_RESOLUTION_COUNT	64

struct synthvid_supported_resolution_req {
	u8 maximum_resolution_count;
} __packed;

struct synthvid_supported_resolution_resp {
	u8 edid_block[SYNTHVID_EDID_BLOCK_SIZE];
	u8 resolution_count;
	u8 default_resolution_index;
	u8 is_standard;
	struct hvd_screen_info supported_resolution[SYNTHVID_MAX_RESOLUTION_COUNT];
} __packed;

struct synthvid_msg {
	struct pipe_msg_hdr pipe_hdr;
	struct synthvid_msg_hdr vid_hdr;
	union {
		struct synthvid_version_req ver_req;
		struct synthvid_version_resp ver_resp;
		struct synthvid_vram_location vram;
		struct synthvid_vram_location_ack vram_ack;
		struct synthvid_situation_update situ;
		struct synthvid_situation_update_ack situ_ack;
		struct synthvid_pointer_position ptr_pos;
		struct synthvid_pointer_shape ptr_shape;
		struct synthvid_feature_change feature_chg;
		struct synthvid_dirt dirt;
		struct synthvid_supported_resolution_req resolution_req;
		struct synthvid_supported_resolution_resp resolution_resp;
	};
} __packed;

static inline bool hyperv_version_ge(u32 ver1, u32 ver2)
{
	if (SYNTHVID_VER_GET_MAJOR(ver1) > SYNTHVID_VER_GET_MAJOR(ver2) ||
	    (SYNTHVID_VER_GET_MAJOR(ver1) == SYNTHVID_VER_GET_MAJOR(ver2) &&
	     SYNTHVID_VER_GET_MINOR(ver1) >= SYNTHVID_VER_GET_MINOR(ver2)))
		return true;

	return false;
}

static inline int hyperv_sendpacket(struct hv_device *hdev, struct synthvid_msg *msg)
{
	static atomic64_t request_id = ATOMIC64_INIT(0);
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	int ret;

	msg->pipe_hdr.type = PIPE_MSG_DATA;
	msg->pipe_hdr.size = msg->vid_hdr.size;

	ret = vmbus_sendpacket(hdev->channel, msg,
			       msg->vid_hdr.size + sizeof(struct pipe_msg_hdr),
			       atomic64_inc_return(&request_id),
			       VM_PKT_DATA_INBAND, 0);

	if (ret)
		drm_err(&hv->dev, "Unable to send packet via vmbus\n");

	return ret;
}

static int hyperv_negotiate_version(struct hv_device *hdev, u32 ver)
{
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct synthvid_msg *msg = (struct synthvid_msg *)hv->init_buf;
	struct drm_device *dev = &hv->dev;
	unsigned long t;

	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_VERSION_REQUEST;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_version_req);
	msg->ver_req.version = ver;
	hyperv_sendpacket(hdev, msg);

	t = wait_for_completion_timeout(&hv->wait, VMBUS_VSP_TIMEOUT);
	if (!t) {
		drm_err(dev, "Time out on waiting version response\n");
		return -ETIMEDOUT;
	}

	if (!msg->ver_resp.is_accepted) {
		drm_err(dev, "Version request not accepted\n");
		return -ENODEV;
	}

	hv->synthvid_version = ver;
	drm_info(dev, "Synthvid Version major %d, minor %d\n",
		 SYNTHVID_VER_GET_MAJOR(ver), SYNTHVID_VER_GET_MINOR(ver));

	return 0;
}

int hyperv_update_vram_location(struct hv_device *hdev, phys_addr_t vram_pp)
{
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct synthvid_msg *msg = (struct synthvid_msg *)hv->init_buf;
	struct drm_device *dev = &hv->dev;
	unsigned long t;

	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_VRAM_LOCATION;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_vram_location);
	msg->vram.user_ctx = vram_pp;
	msg->vram.vram_gpa = vram_pp;
	msg->vram.is_vram_gpa_specified = 1;
	hyperv_sendpacket(hdev, msg);

	t = wait_for_completion_timeout(&hv->wait, VMBUS_VSP_TIMEOUT);
	if (!t) {
		drm_err(dev, "Time out on waiting vram location ack\n");
		return -ETIMEDOUT;
	}
	if (msg->vram_ack.user_ctx != vram_pp) {
		drm_err(dev, "Unable to set VRAM location\n");
		return -ENODEV;
	}

	return 0;
}

int hyperv_update_situation(struct hv_device *hdev, u8 active, u32 bpp,
			    u32 w, u32 h, u32 pitch)
{
	struct synthvid_msg msg;

	memset(&msg, 0, sizeof(struct synthvid_msg));

	msg.vid_hdr.type = SYNTHVID_SITUATION_UPDATE;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_situation_update);
	msg.situ.user_ctx = 0;
	msg.situ.video_output_count = 1;
	msg.situ.video_output[0].active = active;
	/* vram_offset should always be 0 */
	msg.situ.video_output[0].vram_offset = 0;
	msg.situ.video_output[0].depth_bits = bpp;
	msg.situ.video_output[0].width_pixels = w;
	msg.situ.video_output[0].height_pixels = h;
	msg.situ.video_output[0].pitch_bytes = pitch;

	hyperv_sendpacket(hdev, &msg);

	return 0;
}

/*
 * Hyper-V supports a hardware cursor feature. It's not used by Linux VM,
 * but the Hyper-V host still draws a point as an extra mouse pointer,
 * which is unwanted, especially when Xorg is running.
 *
 * The hyperv_fb driver uses synthvid_send_ptr() to hide the unwanted
 * pointer, by setting msg.ptr_pos.is_visible = 1 and setting the
 * msg.ptr_shape.data. Note: setting msg.ptr_pos.is_visible to 0 doesn't
 * work in tests.
 *
 * Copy synthvid_send_ptr() to hyperv_drm and rename it to
 * hyperv_hide_hw_ptr(). Note: hyperv_hide_hw_ptr() is also called in the
 * handler of the SYNTHVID_FEATURE_CHANGE event, otherwise the host still
 * draws an extra unwanted mouse pointer after the VM Connection window is
 * closed and reopened.
 */
int hyperv_hide_hw_ptr(struct hv_device *hdev)
{
	struct synthvid_msg msg;

	memset(&msg, 0, sizeof(struct synthvid_msg));
	msg.vid_hdr.type = SYNTHVID_POINTER_POSITION;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_pointer_position);
	msg.ptr_pos.is_visible = 1;
	msg.ptr_pos.video_output = 0;
	msg.ptr_pos.image_x = 0;
	msg.ptr_pos.image_y = 0;
	hyperv_sendpacket(hdev, &msg);

	memset(&msg, 0, sizeof(struct synthvid_msg));
	msg.vid_hdr.type = SYNTHVID_POINTER_SHAPE;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_pointer_shape);
	msg.ptr_shape.part_idx = SYNTHVID_CURSOR_COMPLETE;
	msg.ptr_shape.is_argb = 1;
	msg.ptr_shape.width = 1;
	msg.ptr_shape.height = 1;
	msg.ptr_shape.hot_x = 0;
	msg.ptr_shape.hot_y = 0;
	msg.ptr_shape.data[0] = 0;
	msg.ptr_shape.data[1] = 1;
	msg.ptr_shape.data[2] = 1;
	msg.ptr_shape.data[3] = 1;
	hyperv_sendpacket(hdev, &msg);

	return 0;
}

int hyperv_update_dirt(struct hv_device *hdev, struct drm_rect *rect)
{
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct synthvid_msg msg;

	if (!hv->dirt_needed)
		return 0;

	memset(&msg, 0, sizeof(struct synthvid_msg));

	msg.vid_hdr.type = SYNTHVID_DIRT;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_dirt);
	msg.dirt.video_output = 0;
	msg.dirt.dirt_count = 1;
	msg.dirt.rect[0].x1 = rect->x1;
	msg.dirt.rect[0].y1 = rect->y1;
	msg.dirt.rect[0].x2 = rect->x2;
	msg.dirt.rect[0].y2 = rect->y2;

	hyperv_sendpacket(hdev, &msg);

	return 0;
}

static int hyperv_get_supported_resolution(struct hv_device *hdev)
{
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct synthvid_msg *msg = (struct synthvid_msg *)hv->init_buf;
	struct drm_device *dev = &hv->dev;
	unsigned long t;
	u8 index;
	int i;

	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_RESOLUTION_REQUEST;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_supported_resolution_req);
	msg->resolution_req.maximum_resolution_count =
		SYNTHVID_MAX_RESOLUTION_COUNT;
	hyperv_sendpacket(hdev, msg);

	t = wait_for_completion_timeout(&hv->wait, VMBUS_VSP_TIMEOUT);
	if (!t) {
		drm_err(dev, "Time out on waiting resolution response\n");
		return -ETIMEDOUT;
	}

	if (msg->resolution_resp.resolution_count == 0) {
		drm_err(dev, "No supported resolutions\n");
		return -ENODEV;
	}

	index = msg->resolution_resp.default_resolution_index;
	if (index >= msg->resolution_resp.resolution_count) {
		drm_err(dev, "Invalid resolution index: %d\n", index);
		return -ENODEV;
	}

	for (i = 0; i < msg->resolution_resp.resolution_count; i++) {
		hv->screen_width_max = max_t(u32, hv->screen_width_max,
			msg->resolution_resp.supported_resolution[i].width);
		hv->screen_height_max = max_t(u32, hv->screen_height_max,
			msg->resolution_resp.supported_resolution[i].height);
	}

	hv->preferred_width =
		msg->resolution_resp.supported_resolution[index].width;
	hv->preferred_height =
		msg->resolution_resp.supported_resolution[index].height;

	return 0;
}

static void hyperv_receive_sub(struct hv_device *hdev)
{
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct synthvid_msg *msg;

	if (!hv)
		return;

	msg = (struct synthvid_msg *)hv->recv_buf;

	/* Complete the wait event */
	if (msg->vid_hdr.type == SYNTHVID_VERSION_RESPONSE ||
	    msg->vid_hdr.type == SYNTHVID_RESOLUTION_RESPONSE ||
	    msg->vid_hdr.type == SYNTHVID_VRAM_LOCATION_ACK) {
		memcpy(hv->init_buf, msg, VMBUS_MAX_PACKET_SIZE);
		complete(&hv->wait);
		return;
	}

	if (msg->vid_hdr.type == SYNTHVID_FEATURE_CHANGE) {
		hv->dirt_needed = msg->feature_chg.is_dirt_needed;
		if (hv->dirt_needed)
			hyperv_hide_hw_ptr(hv->hdev);
	}
}

static void hyperv_receive(void *ctx)
{
	struct hv_device *hdev = ctx;
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct synthvid_msg *recv_buf;
	u32 bytes_recvd;
	u64 req_id;
	int ret;

	if (!hv)
		return;

	recv_buf = (struct synthvid_msg *)hv->recv_buf;

	do {
		ret = vmbus_recvpacket(hdev->channel, recv_buf,
				       VMBUS_MAX_PACKET_SIZE,
				       &bytes_recvd, &req_id);
		if (bytes_recvd > 0 &&
		    recv_buf->pipe_hdr.type == PIPE_MSG_DATA)
			hyperv_receive_sub(hdev);
	} while (bytes_recvd > 0 && ret == 0);
}

int hyperv_connect_vsp(struct hv_device *hdev)
{
	struct hyperv_drm_device *hv = hv_get_drvdata(hdev);
	struct drm_device *dev = &hv->dev;
	int ret;

	ret = vmbus_open(hdev->channel, VMBUS_RING_BUFSIZE, VMBUS_RING_BUFSIZE,
			 NULL, 0, hyperv_receive, hdev);
	if (ret) {
		drm_err(dev, "Unable to open vmbus channel\n");
		return ret;
	}

	/* Negotiate the protocol version with host */
	switch (vmbus_proto_version) {
	case VERSION_WIN10:
	case VERSION_WIN10_V5:
		ret = hyperv_negotiate_version(hdev, SYNTHVID_VERSION_WIN10);
		if (!ret)
			break;
		fallthrough;
	case VERSION_WIN8:
	case VERSION_WIN8_1:
		ret = hyperv_negotiate_version(hdev, SYNTHVID_VERSION_WIN8);
		if (!ret)
			break;
		fallthrough;
	case VERSION_WS2008:
	case VERSION_WIN7:
		ret = hyperv_negotiate_version(hdev, SYNTHVID_VERSION_WIN7);
		break;
	default:
		ret = hyperv_negotiate_version(hdev, SYNTHVID_VERSION_WIN10);
		break;
	}

	if (ret) {
		drm_err(dev, "Synthetic video device version not accepted %d\n", ret);
		goto error;
	}

	if (hv->synthvid_version == SYNTHVID_VERSION_WIN7)
		hv->screen_depth = SYNTHVID_DEPTH_WIN7;
	else
		hv->screen_depth = SYNTHVID_DEPTH_WIN8;

	if (hyperv_version_ge(hv->synthvid_version, SYNTHVID_VERSION_WIN10)) {
		ret = hyperv_get_supported_resolution(hdev);
		if (ret)
			drm_err(dev, "Failed to get supported resolution from host, use default\n");
	} else {
		hv->screen_width_max = SYNTHVID_WIDTH_MAX_WIN7;
		hv->screen_height_max = SYNTHVID_HEIGHT_MAX_WIN7;
	}

	hv->mmio_megabytes = hdev->channel->offermsg.offer.mmio_megabytes;

	return 0;

error:
	vmbus_close(hdev->channel);
	return ret;
}
