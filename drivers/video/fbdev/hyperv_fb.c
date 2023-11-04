// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, Microsoft Corporation.
 *
 * Author:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 */

/*
 * Hyper-V Synthetic Video Frame Buffer Driver
 *
 * This is the driver for the Hyper-V Synthetic Video, which supports
 * screen resolution up to Full HD 1920x1080 with 32 bit color on Windows
 * Server 2012, and 1600x1200 with 16 bit color on Windows Server 2008 R2
 * or earlier.
 *
 * It also solves the double mouse cursor issue of the emulated video mode.
 *
 * The default screen resolution is 1152x864, which may be changed by a
 * kernel parameter:
 *     video=hyperv_fb:<width>x<height>
 *     For example: video=hyperv_fb:1280x1024
 *
 * Portrait orientation is also supported:
 *     For example: video=hyperv_fb:864x1152
 *
 * When a Windows 10 RS5+ host is used, the virtual machine screen
 * resolution is obtained from the host. The "video=hyperv_fb" option is
 * not needed, but still can be used to overwrite what the host specifies.
 * The VM resolution on the host could be set by executing the powershell
 * "set-vmvideo" command. For example
 *     set-vmvideo -vmname name -horizontalresolution:1920 \
 * -verticalresolution:1200 -resolutiontype single
 *
 * Gen 1 VMs also support direct using VM's physical memory for framebuffer.
 * It could improve the efficiency and performance for framebuffer and VM.
 * This requires to allocate contiguous physical memory from Linux kernel's
 * CMA memory allocator. To enable this, supply a kernel parameter to give
 * enough memory space to CMA allocator for framebuffer. For example:
 *    cma=130m
 * This gives 130MB memory to CMA allocator that can be allocated to
 * framebuffer. For reference, 8K resolution (7680x4320) takes about
 * 127MB memory.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/screen_info.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/panic_notifier.h>
#include <linux/efi.h>
#include <linux/console.h>

#include <linux/hyperv.h>

/* Hyper-V Synthetic Video Protocol definitions and structures */
#define MAX_VMBUS_PKT_SIZE 0x4000

#define SYNTHVID_VERSION(major, minor) ((minor) << 16 | (major))
/* Support for VERSION_WIN7 is removed. #define is retained for reference. */
#define SYNTHVID_VERSION_WIN7 SYNTHVID_VERSION(3, 0)
#define SYNTHVID_VERSION_WIN8 SYNTHVID_VERSION(3, 2)
#define SYNTHVID_VERSION_WIN10 SYNTHVID_VERSION(3, 5)

#define SYNTHVID_VER_GET_MAJOR(ver) (ver & 0x0000ffff)
#define SYNTHVID_VER_GET_MINOR(ver) ((ver & 0xffff0000) >> 16)

#define SYNTHVID_DEPTH_WIN8 32
#define SYNTHVID_FB_SIZE_WIN8 (8 * 1024 * 1024)

enum pipe_msg_type {
	PIPE_MSG_INVALID,
	PIPE_MSG_DATA,
	PIPE_MSG_MAX
};

struct pipe_msg_hdr {
	u32 type;
	u32 size; /* size of message after this field */
} __packed;


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

#define		SYNTHVID_EDID_BLOCK_SIZE	128
#define		SYNTHVID_MAX_RESOLUTION_COUNT	64

struct hvd_screen_info {
	u16 width;
	u16 height;
} __packed;

struct synthvid_msg_hdr {
	u32 type;
	u32 size;  /* size of this header + payload after this field*/
} __packed;

struct synthvid_version_req {
	u32 version;
} __packed;

struct synthvid_version_resp {
	u32 version;
	u8 is_accepted;
	u8 max_video_outputs;
} __packed;

struct synthvid_supported_resolution_req {
	u8 maximum_resolution_count;
} __packed;

struct synthvid_supported_resolution_resp {
	u8 edid_block[SYNTHVID_EDID_BLOCK_SIZE];
	u8 resolution_count;
	u8 default_resolution_index;
	u8 is_standard;
	struct hvd_screen_info
		supported_resolution[SYNTHVID_MAX_RESOLUTION_COUNT];
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


#define CURSOR_MAX_X 96
#define CURSOR_MAX_Y 96
#define CURSOR_ARGB_PIXEL_SIZE 4
#define CURSOR_MAX_SIZE (CURSOR_MAX_X * CURSOR_MAX_Y * CURSOR_ARGB_PIXEL_SIZE)
#define CURSOR_COMPLETE (-1)

struct synthvid_pointer_shape {
	u8 part_idx;
	u8 is_argb;
	u32 width; /* CURSOR_MAX_X at most */
	u32 height; /* CURSOR_MAX_Y at most */
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


/* FB driver definitions and structures */
#define HVFB_WIDTH 1152 /* default screen width */
#define HVFB_HEIGHT 864 /* default screen height */
#define HVFB_WIDTH_MIN 640
#define HVFB_HEIGHT_MIN 480

#define RING_BUFSIZE (256 * 1024)
#define VSP_TIMEOUT (10 * HZ)
#define HVFB_UPDATE_DELAY (HZ / 20)
#define HVFB_ONDEMAND_THROTTLE (HZ / 20)

struct hvfb_par {
	struct fb_info *info;
	struct resource *mem;
	bool fb_ready; /* fb device is ready */
	struct completion wait;
	u32 synthvid_version;

	struct delayed_work dwork;
	bool update;
	bool update_saved; /* The value of 'update' before hibernation */

	u32 pseudo_palette[16];
	u8 init_buf[MAX_VMBUS_PKT_SIZE];
	u8 recv_buf[MAX_VMBUS_PKT_SIZE];

	/* If true, the VSC notifies the VSP on every framebuffer change */
	bool synchronous_fb;

	/* If true, need to copy from deferred IO mem to framebuffer mem */
	bool need_docopy;

	struct notifier_block hvfb_panic_nb;

	/* Memory for deferred IO and frame buffer itself */
	unsigned char *dio_vp;
	unsigned char *mmio_vp;
	phys_addr_t mmio_pp;

	/* Dirty rectangle, protected by delayed_refresh_lock */
	int x1, y1, x2, y2;
	bool delayed_refresh;
	spinlock_t delayed_refresh_lock;
};

static uint screen_width = HVFB_WIDTH;
static uint screen_height = HVFB_HEIGHT;
static uint screen_depth;
static uint screen_fb_size;
static uint dio_fb_size; /* FB size for deferred IO */

/* Send message to Hyper-V host */
static inline int synthvid_send(struct hv_device *hdev,
				struct synthvid_msg *msg)
{
	static atomic64_t request_id = ATOMIC64_INIT(0);
	int ret;

	msg->pipe_hdr.type = PIPE_MSG_DATA;
	msg->pipe_hdr.size = msg->vid_hdr.size;

	ret = vmbus_sendpacket(hdev->channel, msg,
			       msg->vid_hdr.size + sizeof(struct pipe_msg_hdr),
			       atomic64_inc_return(&request_id),
			       VM_PKT_DATA_INBAND, 0);

	if (ret)
		pr_err_ratelimited("Unable to send packet via vmbus; error %d\n", ret);

	return ret;
}


/* Send screen resolution info to host */
static int synthvid_send_situ(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct synthvid_msg msg;

	if (!info)
		return -ENODEV;

	memset(&msg, 0, sizeof(struct synthvid_msg));

	msg.vid_hdr.type = SYNTHVID_SITUATION_UPDATE;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_situation_update);
	msg.situ.user_ctx = 0;
	msg.situ.video_output_count = 1;
	msg.situ.video_output[0].active = 1;
	msg.situ.video_output[0].vram_offset = 0;
	msg.situ.video_output[0].depth_bits = info->var.bits_per_pixel;
	msg.situ.video_output[0].width_pixels = info->var.xres;
	msg.situ.video_output[0].height_pixels = info->var.yres;
	msg.situ.video_output[0].pitch_bytes = info->fix.line_length;

	synthvid_send(hdev, &msg);

	return 0;
}

/* Send mouse pointer info to host */
static int synthvid_send_ptr(struct hv_device *hdev)
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
	synthvid_send(hdev, &msg);

	memset(&msg, 0, sizeof(struct synthvid_msg));
	msg.vid_hdr.type = SYNTHVID_POINTER_SHAPE;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_pointer_shape);
	msg.ptr_shape.part_idx = CURSOR_COMPLETE;
	msg.ptr_shape.is_argb = 1;
	msg.ptr_shape.width = 1;
	msg.ptr_shape.height = 1;
	msg.ptr_shape.hot_x = 0;
	msg.ptr_shape.hot_y = 0;
	msg.ptr_shape.data[0] = 0;
	msg.ptr_shape.data[1] = 1;
	msg.ptr_shape.data[2] = 1;
	msg.ptr_shape.data[3] = 1;
	synthvid_send(hdev, &msg);

	return 0;
}

/* Send updated screen area (dirty rectangle) location to host */
static int
synthvid_update(struct fb_info *info, int x1, int y1, int x2, int y2)
{
	struct hv_device *hdev = device_to_hv_device(info->device);
	struct synthvid_msg msg;

	memset(&msg, 0, sizeof(struct synthvid_msg));
	if (x2 == INT_MAX)
		x2 = info->var.xres;
	if (y2 == INT_MAX)
		y2 = info->var.yres;

	msg.vid_hdr.type = SYNTHVID_DIRT;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_dirt);
	msg.dirt.video_output = 0;
	msg.dirt.dirt_count = 1;
	msg.dirt.rect[0].x1 = (x1 > x2) ? 0 : x1;
	msg.dirt.rect[0].y1 = (y1 > y2) ? 0 : y1;
	msg.dirt.rect[0].x2 =
		(x2 < x1 || x2 > info->var.xres) ? info->var.xres : x2;
	msg.dirt.rect[0].y2 =
		(y2 < y1 || y2 > info->var.yres) ? info->var.yres : y2;

	synthvid_send(hdev, &msg);

	return 0;
}

static void hvfb_docopy(struct hvfb_par *par,
			unsigned long offset,
			unsigned long size)
{
	if (!par || !par->mmio_vp || !par->dio_vp || !par->fb_ready ||
	    size == 0 || offset >= dio_fb_size)
		return;

	if (offset + size > dio_fb_size)
		size = dio_fb_size - offset;

	memcpy(par->mmio_vp + offset, par->dio_vp + offset, size);
}

/* Deferred IO callback */
static void synthvid_deferred_io(struct fb_info *p, struct list_head *pagereflist)
{
	struct hvfb_par *par = p->par;
	struct fb_deferred_io_pageref *pageref;
	unsigned long start, end;
	int y1, y2, miny, maxy;

	miny = INT_MAX;
	maxy = 0;

	/*
	 * Merge dirty pages. It is possible that last page cross
	 * over the end of frame buffer row yres. This is taken care of
	 * in synthvid_update function by clamping the y2
	 * value to yres.
	 */
	list_for_each_entry(pageref, pagereflist, list) {
		start = pageref->offset;
		end = start + PAGE_SIZE - 1;
		y1 = start / p->fix.line_length;
		y2 = end / p->fix.line_length;
		miny = min_t(int, miny, y1);
		maxy = max_t(int, maxy, y2);

		/* Copy from dio space to mmio address */
		if (par->fb_ready && par->need_docopy)
			hvfb_docopy(par, start, PAGE_SIZE);
	}

	if (par->fb_ready && par->update)
		synthvid_update(p, 0, miny, p->var.xres, maxy + 1);
}

static struct fb_deferred_io synthvid_defio = {
	.delay		= HZ / 20,
	.deferred_io	= synthvid_deferred_io,
};

/*
 * Actions on received messages from host:
 * Complete the wait event.
 * Or, reply with screen and cursor info.
 */
static void synthvid_recv_sub(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par;
	struct synthvid_msg *msg;

	if (!info)
		return;

	par = info->par;
	msg = (struct synthvid_msg *)par->recv_buf;

	/* Complete the wait event */
	if (msg->vid_hdr.type == SYNTHVID_VERSION_RESPONSE ||
	    msg->vid_hdr.type == SYNTHVID_RESOLUTION_RESPONSE ||
	    msg->vid_hdr.type == SYNTHVID_VRAM_LOCATION_ACK) {
		memcpy(par->init_buf, msg, MAX_VMBUS_PKT_SIZE);
		complete(&par->wait);
		return;
	}

	/* Reply with screen and cursor info */
	if (msg->vid_hdr.type == SYNTHVID_FEATURE_CHANGE) {
		if (par->fb_ready) {
			synthvid_send_ptr(hdev);
			synthvid_send_situ(hdev);
		}

		par->update = msg->feature_chg.is_dirt_needed;
		if (par->update)
			schedule_delayed_work(&par->dwork, HVFB_UPDATE_DELAY);
	}
}

/* Receive callback for messages from the host */
static void synthvid_receive(void *ctx)
{
	struct hv_device *hdev = ctx;
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par;
	struct synthvid_msg *recv_buf;
	u32 bytes_recvd;
	u64 req_id;
	int ret;

	if (!info)
		return;

	par = info->par;
	recv_buf = (struct synthvid_msg *)par->recv_buf;

	do {
		ret = vmbus_recvpacket(hdev->channel, recv_buf,
				       MAX_VMBUS_PKT_SIZE,
				       &bytes_recvd, &req_id);
		if (bytes_recvd > 0 &&
		    recv_buf->pipe_hdr.type == PIPE_MSG_DATA)
			synthvid_recv_sub(hdev);
	} while (bytes_recvd > 0 && ret == 0);
}

/* Check if the ver1 version is equal or greater than ver2 */
static inline bool synthvid_ver_ge(u32 ver1, u32 ver2)
{
	if (SYNTHVID_VER_GET_MAJOR(ver1) > SYNTHVID_VER_GET_MAJOR(ver2) ||
	    (SYNTHVID_VER_GET_MAJOR(ver1) == SYNTHVID_VER_GET_MAJOR(ver2) &&
	     SYNTHVID_VER_GET_MINOR(ver1) >= SYNTHVID_VER_GET_MINOR(ver2)))
		return true;

	return false;
}

/* Check synthetic video protocol version with the host */
static int synthvid_negotiate_ver(struct hv_device *hdev, u32 ver)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	struct synthvid_msg *msg = (struct synthvid_msg *)par->init_buf;
	int ret = 0;
	unsigned long t;

	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_VERSION_REQUEST;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_version_req);
	msg->ver_req.version = ver;
	synthvid_send(hdev, msg);

	t = wait_for_completion_timeout(&par->wait, VSP_TIMEOUT);
	if (!t) {
		pr_err("Time out on waiting version response\n");
		ret = -ETIMEDOUT;
		goto out;
	}
	if (!msg->ver_resp.is_accepted) {
		ret = -ENODEV;
		goto out;
	}

	par->synthvid_version = ver;
	pr_info("Synthvid Version major %d, minor %d\n",
		SYNTHVID_VER_GET_MAJOR(ver), SYNTHVID_VER_GET_MINOR(ver));

out:
	return ret;
}

/* Get current resolution from the host */
static int synthvid_get_supported_resolution(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	struct synthvid_msg *msg = (struct synthvid_msg *)par->init_buf;
	int ret = 0;
	unsigned long t;
	u8 index;

	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_RESOLUTION_REQUEST;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_supported_resolution_req);

	msg->resolution_req.maximum_resolution_count =
		SYNTHVID_MAX_RESOLUTION_COUNT;
	synthvid_send(hdev, msg);

	t = wait_for_completion_timeout(&par->wait, VSP_TIMEOUT);
	if (!t) {
		pr_err("Time out on waiting resolution response\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	if (msg->resolution_resp.resolution_count == 0) {
		pr_err("No supported resolutions\n");
		ret = -ENODEV;
		goto out;
	}

	index = msg->resolution_resp.default_resolution_index;
	if (index >= msg->resolution_resp.resolution_count) {
		pr_err("Invalid resolution index: %d\n", index);
		ret = -ENODEV;
		goto out;
	}

	screen_width =
		msg->resolution_resp.supported_resolution[index].width;
	screen_height =
		msg->resolution_resp.supported_resolution[index].height;

out:
	return ret;
}

/* Connect to VSP (Virtual Service Provider) on host */
static int synthvid_connect_vsp(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	int ret;

	ret = vmbus_open(hdev->channel, RING_BUFSIZE, RING_BUFSIZE,
			 NULL, 0, synthvid_receive, hdev);
	if (ret) {
		pr_err("Unable to open vmbus channel\n");
		return ret;
	}

	/* Negotiate the protocol version with host */
	switch (vmbus_proto_version) {
	case VERSION_WIN10:
	case VERSION_WIN10_V5:
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN10);
		if (!ret)
			break;
		fallthrough;
	case VERSION_WIN8:
	case VERSION_WIN8_1:
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN8);
		break;
	default:
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN10);
		break;
	}

	if (ret) {
		pr_err("Synthetic video device version not accepted\n");
		goto error;
	}

	screen_depth = SYNTHVID_DEPTH_WIN8;
	if (synthvid_ver_ge(par->synthvid_version, SYNTHVID_VERSION_WIN10)) {
		ret = synthvid_get_supported_resolution(hdev);
		if (ret)
			pr_info("Failed to get supported resolution from host, use default\n");
	}

	screen_fb_size = hdev->channel->offermsg.offer.
				mmio_megabytes * 1024 * 1024;

	return 0;

error:
	vmbus_close(hdev->channel);
	return ret;
}

/* Send VRAM and Situation messages to the host */
static int synthvid_send_config(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	struct synthvid_msg *msg = (struct synthvid_msg *)par->init_buf;
	int ret = 0;
	unsigned long t;

	/* Send VRAM location */
	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_VRAM_LOCATION;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_vram_location);
	msg->vram.user_ctx = msg->vram.vram_gpa = par->mmio_pp;
	msg->vram.is_vram_gpa_specified = 1;
	synthvid_send(hdev, msg);

	t = wait_for_completion_timeout(&par->wait, VSP_TIMEOUT);
	if (!t) {
		pr_err("Time out on waiting vram location ack\n");
		ret = -ETIMEDOUT;
		goto out;
	}
	if (msg->vram_ack.user_ctx != par->mmio_pp) {
		pr_err("Unable to set VRAM location\n");
		ret = -ENODEV;
		goto out;
	}

	/* Send pointer and situation update */
	synthvid_send_ptr(hdev);
	synthvid_send_situ(hdev);

out:
	return ret;
}


/*
 * Delayed work callback:
 * It is scheduled to call whenever update request is received and it has
 * not been called in last HVFB_ONDEMAND_THROTTLE time interval.
 */
static void hvfb_update_work(struct work_struct *w)
{
	struct hvfb_par *par = container_of(w, struct hvfb_par, dwork.work);
	struct fb_info *info = par->info;
	unsigned long flags;
	int x1, x2, y1, y2;
	int j;

	spin_lock_irqsave(&par->delayed_refresh_lock, flags);
	/* Reset the request flag */
	par->delayed_refresh = false;

	/* Store the dirty rectangle to local variables */
	x1 = par->x1;
	x2 = par->x2;
	y1 = par->y1;
	y2 = par->y2;

	/* Clear dirty rectangle */
	par->x1 = par->y1 = INT_MAX;
	par->x2 = par->y2 = 0;

	spin_unlock_irqrestore(&par->delayed_refresh_lock, flags);

	if (x1 > info->var.xres || x2 > info->var.xres ||
	    y1 > info->var.yres || y2 > info->var.yres || x2 <= x1)
		return;

	/* Copy the dirty rectangle to frame buffer memory */
	if (par->need_docopy)
		for (j = y1; j < y2; j++)
			hvfb_docopy(par,
				    j * info->fix.line_length +
				    (x1 * screen_depth / 8),
				    (x2 - x1) * screen_depth / 8);

	/* Refresh */
	if (par->fb_ready && par->update)
		synthvid_update(info, x1, y1, x2, y2);
}

/*
 * Control the on-demand refresh frequency. It schedules a delayed
 * screen update if it has not yet.
 */
static void hvfb_ondemand_refresh_throttle(struct hvfb_par *par,
					   int x1, int y1, int w, int h)
{
	unsigned long flags;
	int x2 = x1 + w;
	int y2 = y1 + h;

	spin_lock_irqsave(&par->delayed_refresh_lock, flags);

	/* Merge dirty rectangle */
	par->x1 = min_t(int, par->x1, x1);
	par->y1 = min_t(int, par->y1, y1);
	par->x2 = max_t(int, par->x2, x2);
	par->y2 = max_t(int, par->y2, y2);

	/* Schedule a delayed screen update if not yet */
	if (par->delayed_refresh == false) {
		schedule_delayed_work(&par->dwork,
				      HVFB_ONDEMAND_THROTTLE);
		par->delayed_refresh = true;
	}

	spin_unlock_irqrestore(&par->delayed_refresh_lock, flags);
}

static int hvfb_on_panic(struct notifier_block *nb,
			 unsigned long e, void *p)
{
	struct hv_device *hdev;
	struct hvfb_par *par;
	struct fb_info *info;

	par = container_of(nb, struct hvfb_par, hvfb_panic_nb);
	info = par->info;
	hdev = device_to_hv_device(info->device);

	if (hv_ringbuffer_spinlock_busy(hdev->channel))
		return NOTIFY_DONE;

	par->synchronous_fb = true;
	if (par->need_docopy)
		hvfb_docopy(par, 0, dio_fb_size);
	synthvid_update(info, 0, 0, INT_MAX, INT_MAX);

	return NOTIFY_DONE;
}

/* Framebuffer operation handlers */

static int hvfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xres < HVFB_WIDTH_MIN || var->yres < HVFB_HEIGHT_MIN ||
	    var->xres > screen_width || var->yres >  screen_height ||
	    var->bits_per_pixel != screen_depth)
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	return 0;
}

static int hvfb_set_par(struct fb_info *info)
{
	struct hv_device *hdev = device_to_hv_device(info->device);

	return synthvid_send_situ(hdev);
}


static inline u32 chan_to_field(u32 chan, struct fb_bitfield *bf)
{
	return ((chan & 0xffff) >> (16 - bf->length)) << bf->offset;
}

static int hvfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;

	if (regno > 15)
		return -EINVAL;

	pal[regno] = chan_to_field(red, &info->var.red)
		| chan_to_field(green, &info->var.green)
		| chan_to_field(blue, &info->var.blue)
		| chan_to_field(transp, &info->var.transp);

	return 0;
}

static int hvfb_blank(int blank, struct fb_info *info)
{
	return 1;	/* get fb_blank to set the colormap to all black */
}

static void hvfb_ops_damage_range(struct fb_info *info, off_t off, size_t len)
{
	/* TODO: implement damage handling */
}

static void hvfb_ops_damage_area(struct fb_info *info, u32 x, u32 y, u32 width, u32 height)
{
	struct hvfb_par *par = info->par;

	if (par->synchronous_fb)
		synthvid_update(info, 0, 0, INT_MAX, INT_MAX);
	else
		hvfb_ondemand_refresh_throttle(par, x, y, width, height);
}

/*
 * TODO: GEN1 codepaths allocate from system or DMA-able memory. Fix the
 *       driver to use the _SYSMEM_ or _DMAMEM_ helpers in these cases.
 */
FB_GEN_DEFAULT_DEFERRED_IOMEM_OPS(hvfb_ops,
				  hvfb_ops_damage_range,
				  hvfb_ops_damage_area)

static const struct fb_ops hvfb_ops = {
	.owner = THIS_MODULE,
	FB_DEFAULT_DEFERRED_OPS(hvfb_ops),
	.fb_check_var = hvfb_check_var,
	.fb_set_par = hvfb_set_par,
	.fb_setcolreg = hvfb_setcolreg,
	.fb_blank = hvfb_blank,
};

/* Get options from kernel paramenter "video=" */
static void hvfb_get_option(struct fb_info *info)
{
	struct hvfb_par *par = info->par;
	char *opt = NULL, *p;
	uint x = 0, y = 0;

	if (fb_get_options(KBUILD_MODNAME, &opt) || !opt || !*opt)
		return;

	p = strsep(&opt, "x");
	if (!*p || kstrtouint(p, 0, &x) ||
	    !opt || !*opt || kstrtouint(opt, 0, &y)) {
		pr_err("Screen option is invalid: skipped\n");
		return;
	}

	if (x < HVFB_WIDTH_MIN || y < HVFB_HEIGHT_MIN ||
	    (synthvid_ver_ge(par->synthvid_version, SYNTHVID_VERSION_WIN10) &&
	    (x * y * screen_depth / 8 > screen_fb_size)) ||
	    (par->synthvid_version == SYNTHVID_VERSION_WIN8 &&
	     x * y * screen_depth / 8 > SYNTHVID_FB_SIZE_WIN8)) {
		pr_err("Screen resolution option is out of range: skipped\n");
		return;
	}

	screen_width = x;
	screen_height = y;
	return;
}

/*
 * Allocate enough contiguous physical memory.
 * Return physical address if succeeded or -1 if failed.
 */
static phys_addr_t hvfb_get_phymem(struct hv_device *hdev,
				   unsigned int request_size)
{
	struct page *page = NULL;
	dma_addr_t dma_handle;
	void *vmem;
	phys_addr_t paddr = 0;
	unsigned int order = get_order(request_size);

	if (request_size == 0)
		return -1;

	if (order <= MAX_ORDER) {
		/* Call alloc_pages if the size is less than 2^MAX_ORDER */
		page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
		if (!page)
			return -1;

		paddr = (page_to_pfn(page) << PAGE_SHIFT);
	} else {
		/* Allocate from CMA */
		hdev->device.coherent_dma_mask = DMA_BIT_MASK(64);

		vmem = dma_alloc_coherent(&hdev->device,
					  round_up(request_size, PAGE_SIZE),
					  &dma_handle,
					  GFP_KERNEL | __GFP_NOWARN);

		if (!vmem)
			return -1;

		paddr = virt_to_phys(vmem);
	}

	return paddr;
}

/* Release contiguous physical memory */
static void hvfb_release_phymem(struct hv_device *hdev,
				phys_addr_t paddr, unsigned int size)
{
	unsigned int order = get_order(size);

	if (order <= MAX_ORDER)
		__free_pages(pfn_to_page(paddr >> PAGE_SHIFT), order);
	else
		dma_free_coherent(&hdev->device,
				  round_up(size, PAGE_SIZE),
				  phys_to_virt(paddr),
				  paddr);
}


/* Get framebuffer memory from Hyper-V video pci space */
static int hvfb_getmem(struct hv_device *hdev, struct fb_info *info)
{
	struct hvfb_par *par = info->par;
	struct pci_dev *pdev  = NULL;
	void __iomem *fb_virt;
	int gen2vm = efi_enabled(EFI_BOOT);
	resource_size_t base, size;
	phys_addr_t paddr;
	int ret;

	if (!gen2vm) {
		pdev = pci_get_device(PCI_VENDOR_ID_MICROSOFT,
			PCI_DEVICE_ID_HYPERV_VIDEO, NULL);
		if (!pdev) {
			pr_err("Unable to find PCI Hyper-V video\n");
			return -ENODEV;
		}

		base = pci_resource_start(pdev, 0);
		size = pci_resource_len(pdev, 0);

		/*
		 * For Gen 1 VM, we can directly use the contiguous memory
		 * from VM. If we succeed, deferred IO happens directly
		 * on this allocated framebuffer memory, avoiding extra
		 * memory copy.
		 */
		paddr = hvfb_get_phymem(hdev, screen_fb_size);
		if (paddr != (phys_addr_t) -1) {
			par->mmio_pp = paddr;
			par->mmio_vp = par->dio_vp = __va(paddr);

			info->fix.smem_start = paddr;
			info->fix.smem_len = screen_fb_size;
			info->screen_base = par->mmio_vp;
			info->screen_size = screen_fb_size;

			par->need_docopy = false;
			goto getmem_done;
		}
		pr_info("Unable to allocate enough contiguous physical memory on Gen 1 VM. Using MMIO instead.\n");
	} else if (IS_ENABLED(CONFIG_SYSFB)) {
		base = screen_info.lfb_base;
		size = screen_info.lfb_size;
	}

	/*
	 * Cannot use the contiguous physical memory.
	 * Allocate mmio space for framebuffer.
	 */
	dio_fb_size =
		screen_width * screen_height * screen_depth / 8;

	ret = vmbus_allocate_mmio(&par->mem, hdev, 0, -1,
				  screen_fb_size, 0x100000, true);
	if (ret != 0) {
		pr_err("Unable to allocate framebuffer memory\n");
		goto err1;
	}

	/*
	 * Map the VRAM cacheable for performance. This is also required for
	 * VM Connect to display properly for ARM64 Linux VM, as the host also
	 * maps the VRAM cacheable.
	 */
	fb_virt = ioremap_cache(par->mem->start, screen_fb_size);
	if (!fb_virt)
		goto err2;

	/* Allocate memory for deferred IO */
	par->dio_vp = vzalloc(round_up(dio_fb_size, PAGE_SIZE));
	if (par->dio_vp == NULL)
		goto err3;

	/* Physical address of FB device */
	par->mmio_pp = par->mem->start;
	/* Virtual address of FB device */
	par->mmio_vp = (unsigned char *) fb_virt;

	info->fix.smem_start = par->mem->start;
	info->fix.smem_len = dio_fb_size;
	info->screen_base = par->dio_vp;
	info->screen_size = dio_fb_size;

getmem_done:
	aperture_remove_conflicting_devices(base, size, KBUILD_MODNAME);

	if (!gen2vm) {
		pci_dev_put(pdev);
	} else if (IS_ENABLED(CONFIG_SYSFB)) {
		/* framebuffer is reallocated, clear screen_info to avoid misuse from kexec */
		screen_info.lfb_size = 0;
		screen_info.lfb_base = 0;
		screen_info.orig_video_isVGA = 0;
	}

	return 0;

err3:
	iounmap(fb_virt);
err2:
	vmbus_free_mmio(par->mem->start, screen_fb_size);
	par->mem = NULL;
err1:
	if (!gen2vm)
		pci_dev_put(pdev);

	return -ENOMEM;
}

/* Release the framebuffer */
static void hvfb_putmem(struct hv_device *hdev, struct fb_info *info)
{
	struct hvfb_par *par = info->par;

	if (par->need_docopy) {
		vfree(par->dio_vp);
		iounmap(info->screen_base);
		vmbus_free_mmio(par->mem->start, screen_fb_size);
	} else {
		hvfb_release_phymem(hdev, info->fix.smem_start,
				    screen_fb_size);
	}

	par->mem = NULL;
}


static int hvfb_probe(struct hv_device *hdev,
		      const struct hv_vmbus_device_id *dev_id)
{
	struct fb_info *info;
	struct hvfb_par *par;
	int ret;

	info = framebuffer_alloc(sizeof(struct hvfb_par), &hdev->device);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->info = info;
	par->fb_ready = false;
	par->need_docopy = true;
	init_completion(&par->wait);
	INIT_DELAYED_WORK(&par->dwork, hvfb_update_work);

	par->delayed_refresh = false;
	spin_lock_init(&par->delayed_refresh_lock);
	par->x1 = par->y1 = INT_MAX;
	par->x2 = par->y2 = 0;

	/* Connect to VSP */
	hv_set_drvdata(hdev, info);
	ret = synthvid_connect_vsp(hdev);
	if (ret) {
		pr_err("Unable to connect to VSP\n");
		goto error1;
	}

	hvfb_get_option(info);
	pr_info("Screen resolution: %dx%d, Color depth: %d, Frame buffer size: %d\n",
		screen_width, screen_height, screen_depth, screen_fb_size);

	ret = hvfb_getmem(hdev, info);
	if (ret) {
		pr_err("No memory for framebuffer\n");
		goto error2;
	}

	/* Set up fb_info */
	info->var.xres_virtual = info->var.xres = screen_width;
	info->var.yres_virtual = info->var.yres = screen_height;
	info->var.bits_per_pixel = screen_depth;

	if (info->var.bits_per_pixel == 16) {
		info->var.red = (struct fb_bitfield){11, 5, 0};
		info->var.green = (struct fb_bitfield){5, 6, 0};
		info->var.blue = (struct fb_bitfield){0, 5, 0};
		info->var.transp = (struct fb_bitfield){0, 0, 0};
	} else {
		info->var.red = (struct fb_bitfield){16, 8, 0};
		info->var.green = (struct fb_bitfield){8, 8, 0};
		info->var.blue = (struct fb_bitfield){0, 8, 0};
		info->var.transp = (struct fb_bitfield){24, 8, 0};
	}

	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	strcpy(info->fix.id, KBUILD_MODNAME);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = screen_width * screen_depth / 8;
	info->fix.accel = FB_ACCEL_NONE;

	info->fbops = &hvfb_ops;
	info->pseudo_palette = par->pseudo_palette;

	/* Initialize deferred IO */
	info->fbdefio = &synthvid_defio;
	fb_deferred_io_init(info);

	/* Send config to host */
	ret = synthvid_send_config(hdev);
	if (ret)
		goto error;

	ret = register_framebuffer(info);
	if (ret) {
		pr_err("Unable to register framebuffer\n");
		goto error;
	}

	par->fb_ready = true;

	par->synchronous_fb = false;

	/*
	 * We need to be sure this panic notifier runs _before_ the
	 * vmbus disconnect, so order it by priority. It must execute
	 * before the function hv_panic_vmbus_unload() [drivers/hv/vmbus_drv.c],
	 * which is almost at the end of list, with priority = INT_MIN + 1.
	 */
	par->hvfb_panic_nb.notifier_call = hvfb_on_panic;
	par->hvfb_panic_nb.priority = INT_MIN + 10,
	atomic_notifier_chain_register(&panic_notifier_list,
				       &par->hvfb_panic_nb);

	return 0;

error:
	fb_deferred_io_cleanup(info);
	hvfb_putmem(hdev, info);
error2:
	vmbus_close(hdev->channel);
error1:
	cancel_delayed_work_sync(&par->dwork);
	hv_set_drvdata(hdev, NULL);
	framebuffer_release(info);
	return ret;
}

static void hvfb_remove(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &par->hvfb_panic_nb);

	par->update = false;
	par->fb_ready = false;

	fb_deferred_io_cleanup(info);

	unregister_framebuffer(info);
	cancel_delayed_work_sync(&par->dwork);

	vmbus_close(hdev->channel);
	hv_set_drvdata(hdev, NULL);

	hvfb_putmem(hdev, info);
	framebuffer_release(info);
}

static int hvfb_suspend(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;

	console_lock();

	/* 1 means do suspend */
	fb_set_suspend(info, 1);

	cancel_delayed_work_sync(&par->dwork);
	cancel_delayed_work_sync(&info->deferred_work);

	par->update_saved = par->update;
	par->update = false;
	par->fb_ready = false;

	vmbus_close(hdev->channel);

	console_unlock();

	return 0;
}

static int hvfb_resume(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	int ret;

	console_lock();

	ret = synthvid_connect_vsp(hdev);
	if (ret != 0)
		goto out;

	ret = synthvid_send_config(hdev);
	if (ret != 0) {
		vmbus_close(hdev->channel);
		goto out;
	}

	par->fb_ready = true;
	par->update = par->update_saved;

	schedule_delayed_work(&info->deferred_work, info->fbdefio->delay);
	schedule_delayed_work(&par->dwork, HVFB_UPDATE_DELAY);

	/* 0 means do resume */
	fb_set_suspend(info, 0);

out:
	console_unlock();

	return ret;
}


static const struct pci_device_id pci_stub_id_table[] = {
	{
		.vendor      = PCI_VENDOR_ID_MICROSOFT,
		.device      = PCI_DEVICE_ID_HYPERV_VIDEO,
	},
	{ /* end of list */ }
};

static const struct hv_vmbus_device_id id_table[] = {
	/* Synthetic Video Device GUID */
	{HV_SYNTHVID_GUID},
	{}
};

MODULE_DEVICE_TABLE(pci, pci_stub_id_table);
MODULE_DEVICE_TABLE(vmbus, id_table);

static struct hv_driver hvfb_drv = {
	.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = hvfb_probe,
	.remove = hvfb_remove,
	.suspend = hvfb_suspend,
	.resume = hvfb_resume,
	.driver = {
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int hvfb_pci_stub_probe(struct pci_dev *pdev,
			       const struct pci_device_id *ent)
{
	return 0;
}

static void hvfb_pci_stub_remove(struct pci_dev *pdev)
{
}

static struct pci_driver hvfb_pci_stub_driver = {
	.name =		KBUILD_MODNAME,
	.id_table =	pci_stub_id_table,
	.probe =	hvfb_pci_stub_probe,
	.remove =	hvfb_pci_stub_remove,
	.driver = {
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	}
};

static int __init hvfb_drv_init(void)
{
	int ret;

	if (fb_modesetting_disabled("hyper_fb"))
		return -ENODEV;

	ret = vmbus_driver_register(&hvfb_drv);
	if (ret != 0)
		return ret;

	ret = pci_register_driver(&hvfb_pci_stub_driver);
	if (ret != 0) {
		vmbus_driver_unregister(&hvfb_drv);
		return ret;
	}

	return 0;
}

static void __exit hvfb_drv_exit(void)
{
	pci_unregister_driver(&hvfb_pci_stub_driver);
	vmbus_driver_unregister(&hvfb_drv);
}

module_init(hvfb_drv_init);
module_exit(hvfb_drv_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microsoft Hyper-V Synthetic Video Frame Buffer Driver");
