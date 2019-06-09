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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/efi.h>

#include <linux/hyperv.h>


/* Hyper-V Synthetic Video Protocol definitions and structures */
#define MAX_VMBUS_PKT_SIZE 0x4000

#define SYNTHVID_VERSION(major, minor) ((minor) << 16 | (major))
#define SYNTHVID_VERSION_WIN7 SYNTHVID_VERSION(3, 0)
#define SYNTHVID_VERSION_WIN8 SYNTHVID_VERSION(3, 2)

#define SYNTHVID_DEPTH_WIN7 16
#define SYNTHVID_DEPTH_WIN8 32

#define SYNTHVID_FB_SIZE_WIN7 (4 * 1024 * 1024)
#define SYNTHVID_WIDTH_MAX_WIN7 1600
#define SYNTHVID_HEIGHT_MAX_WIN7 1200

#define SYNTHVID_FB_SIZE_WIN8 (8 * 1024 * 1024)

#define PCI_VENDOR_ID_MICROSOFT 0x1414
#define PCI_DEVICE_ID_HYPERV_VIDEO 0x5353


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

	SYNTHVID_MAX			= 11
};

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

struct hvfb_par {
	struct fb_info *info;
	struct resource *mem;
	bool fb_ready; /* fb device is ready */
	struct completion wait;
	u32 synthvid_version;

	struct delayed_work dwork;
	bool update;

	u32 pseudo_palette[16];
	u8 init_buf[MAX_VMBUS_PKT_SIZE];
	u8 recv_buf[MAX_VMBUS_PKT_SIZE];

	/* If true, the VSC notifies the VSP on every framebuffer change */
	bool synchronous_fb;

	struct notifier_block hvfb_panic_nb;
};

static uint screen_width = HVFB_WIDTH;
static uint screen_height = HVFB_HEIGHT;
static uint screen_depth;
static uint screen_fb_size;

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
		pr_err("Unable to send packet via vmbus\n");

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
static int synthvid_update(struct fb_info *info)
{
	struct hv_device *hdev = device_to_hv_device(info->device);
	struct synthvid_msg msg;

	memset(&msg, 0, sizeof(struct synthvid_msg));

	msg.vid_hdr.type = SYNTHVID_DIRT;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_dirt);
	msg.dirt.video_output = 0;
	msg.dirt.dirt_count = 1;
	msg.dirt.rect[0].x1 = 0;
	msg.dirt.rect[0].y1 = 0;
	msg.dirt.rect[0].x2 = info->var.xres;
	msg.dirt.rect[0].y2 = info->var.yres;

	synthvid_send(hdev, &msg);

	return 0;
}


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
	if (vmbus_proto_version == VERSION_WS2008 ||
	    vmbus_proto_version == VERSION_WIN7)
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN7);
	else
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN8);

	if (ret) {
		pr_err("Synthetic video device version not accepted\n");
		goto error;
	}

	if (par->synthvid_version == SYNTHVID_VERSION_WIN7)
		screen_depth = SYNTHVID_DEPTH_WIN7;
	else
		screen_depth = SYNTHVID_DEPTH_WIN8;

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
	msg->vram.user_ctx = msg->vram.vram_gpa = info->fix.smem_start;
	msg->vram.is_vram_gpa_specified = 1;
	synthvid_send(hdev, msg);

	t = wait_for_completion_timeout(&par->wait, VSP_TIMEOUT);
	if (!t) {
		pr_err("Time out on waiting vram location ack\n");
		ret = -ETIMEDOUT;
		goto out;
	}
	if (msg->vram_ack.user_ctx != info->fix.smem_start) {
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
 * It is called at HVFB_UPDATE_DELAY or longer time interval to process
 * screen updates. It is re-scheduled if further update is necessary.
 */
static void hvfb_update_work(struct work_struct *w)
{
	struct hvfb_par *par = container_of(w, struct hvfb_par, dwork.work);
	struct fb_info *info = par->info;

	if (par->fb_ready)
		synthvid_update(info);

	if (par->update)
		schedule_delayed_work(&par->dwork, HVFB_UPDATE_DELAY);
}

static int hvfb_on_panic(struct notifier_block *nb,
			 unsigned long e, void *p)
{
	struct hvfb_par *par;
	struct fb_info *info;

	par = container_of(nb, struct hvfb_par, hvfb_panic_nb);
	par->synchronous_fb = true;
	info = par->info;
	synthvid_update(info);

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

static void hvfb_cfb_fillrect(struct fb_info *p,
			      const struct fb_fillrect *rect)
{
	struct hvfb_par *par = p->par;

	cfb_fillrect(p, rect);
	if (par->synchronous_fb)
		synthvid_update(p);
}

static void hvfb_cfb_copyarea(struct fb_info *p,
			      const struct fb_copyarea *area)
{
	struct hvfb_par *par = p->par;

	cfb_copyarea(p, area);
	if (par->synchronous_fb)
		synthvid_update(p);
}

static void hvfb_cfb_imageblit(struct fb_info *p,
			       const struct fb_image *image)
{
	struct hvfb_par *par = p->par;

	cfb_imageblit(p, image);
	if (par->synchronous_fb)
		synthvid_update(p);
}

static struct fb_ops hvfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = hvfb_check_var,
	.fb_set_par = hvfb_set_par,
	.fb_setcolreg = hvfb_setcolreg,
	.fb_fillrect = hvfb_cfb_fillrect,
	.fb_copyarea = hvfb_cfb_copyarea,
	.fb_imageblit = hvfb_cfb_imageblit,
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
	    (par->synthvid_version == SYNTHVID_VERSION_WIN8 &&
	     x * y * screen_depth / 8 > SYNTHVID_FB_SIZE_WIN8) ||
	    (par->synthvid_version == SYNTHVID_VERSION_WIN7 &&
	     (x > SYNTHVID_WIDTH_MAX_WIN7 || y > SYNTHVID_HEIGHT_MAX_WIN7))) {
		pr_err("Screen resolution option is out of range: skipped\n");
		return;
	}

	screen_width = x;
	screen_height = y;
	return;
}


/* Get framebuffer memory from Hyper-V video pci space */
static int hvfb_getmem(struct hv_device *hdev, struct fb_info *info)
{
	struct hvfb_par *par = info->par;
	struct pci_dev *pdev  = NULL;
	void __iomem *fb_virt;
	int gen2vm = efi_enabled(EFI_BOOT);
	resource_size_t pot_start, pot_end;
	int ret;

	if (gen2vm) {
		pot_start = 0;
		pot_end = -1;
	} else {
		pdev = pci_get_device(PCI_VENDOR_ID_MICROSOFT,
			      PCI_DEVICE_ID_HYPERV_VIDEO, NULL);
		if (!pdev) {
			pr_err("Unable to find PCI Hyper-V video\n");
			return -ENODEV;
		}

		if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
		    pci_resource_len(pdev, 0) < screen_fb_size)
			goto err1;

		pot_end = pci_resource_end(pdev, 0);
		pot_start = pot_end - screen_fb_size + 1;
	}

	ret = vmbus_allocate_mmio(&par->mem, hdev, pot_start, pot_end,
				  screen_fb_size, 0x100000, true);
	if (ret != 0) {
		pr_err("Unable to allocate framebuffer memory\n");
		goto err1;
	}

	fb_virt = ioremap(par->mem->start, screen_fb_size);
	if (!fb_virt)
		goto err2;

	info->apertures = alloc_apertures(1);
	if (!info->apertures)
		goto err3;

	if (gen2vm) {
		info->apertures->ranges[0].base = screen_info.lfb_base;
		info->apertures->ranges[0].size = screen_info.lfb_size;
		remove_conflicting_framebuffers(info->apertures,
						KBUILD_MODNAME, false);
	} else {
		info->apertures->ranges[0].base = pci_resource_start(pdev, 0);
		info->apertures->ranges[0].size = pci_resource_len(pdev, 0);
	}

	info->fix.smem_start = par->mem->start;
	info->fix.smem_len = screen_fb_size;
	info->screen_base = fb_virt;
	info->screen_size = screen_fb_size;

	if (!gen2vm)
		pci_dev_put(pdev);

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
static void hvfb_putmem(struct fb_info *info)
{
	struct hvfb_par *par = info->par;

	iounmap(info->screen_base);
	vmbus_free_mmio(par->mem->start, screen_fb_size);
	par->mem = NULL;
}


static int hvfb_probe(struct hv_device *hdev,
		      const struct hv_vmbus_device_id *dev_id)
{
	struct fb_info *info;
	struct hvfb_par *par;
	int ret;

	info = framebuffer_alloc(sizeof(struct hvfb_par), &hdev->device);
	if (!info) {
		pr_err("No memory for framebuffer info\n");
		return -ENOMEM;
	}

	par = info->par;
	par->info = info;
	par->fb_ready = false;
	init_completion(&par->wait);
	INIT_DELAYED_WORK(&par->dwork, hvfb_update_work);

	/* Connect to VSP */
	hv_set_drvdata(hdev, info);
	ret = synthvid_connect_vsp(hdev);
	if (ret) {
		pr_err("Unable to connect to VSP\n");
		goto error1;
	}

	ret = hvfb_getmem(hdev, info);
	if (ret) {
		pr_err("No memory for framebuffer\n");
		goto error2;
	}

	hvfb_get_option(info);
	pr_info("Screen resolution: %dx%d, Color depth: %d\n",
		screen_width, screen_height, screen_depth);


	/* Set up fb_info */
	info->flags = FBINFO_DEFAULT;

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
	par->hvfb_panic_nb.notifier_call = hvfb_on_panic;
	atomic_notifier_chain_register(&panic_notifier_list,
				       &par->hvfb_panic_nb);

	return 0;

error:
	hvfb_putmem(info);
error2:
	vmbus_close(hdev->channel);
error1:
	cancel_delayed_work_sync(&par->dwork);
	hv_set_drvdata(hdev, NULL);
	framebuffer_release(info);
	return ret;
}


static int hvfb_remove(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &par->hvfb_panic_nb);

	par->update = false;
	par->fb_ready = false;

	unregister_framebuffer(info);
	cancel_delayed_work_sync(&par->dwork);

	vmbus_close(hdev->channel);
	hv_set_drvdata(hdev, NULL);

	hvfb_putmem(info);
	framebuffer_release(info);

	return 0;
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
