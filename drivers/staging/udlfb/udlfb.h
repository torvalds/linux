#ifndef UDLFB_H
#define UDLFB_H

/* as libdlo */
#define BUF_HIGH_WATER_MARK	1024
#define BUF_SIZE		(64*1024)

struct urb_node {
	struct list_head entry;
	struct dlfb_data *dev;
	struct urb *urb;
};

struct urb_list {
	struct list_head list;
	spinlock_t lock;
	struct semaphore limit_sem;
	int available;
	int count;
	size_t size;
};

struct dlfb_data {
	struct usb_device *udev;
	struct device *gdev; /* &udev->dev */
	struct usb_interface *interface;
	struct urb *tx_urb, *ctrl_urb;
	struct usb_ctrlrequest dr;
	struct fb_info *info;
	struct urb_list urbs;
	struct kref kref;
	char *buf;
	char *bufend;
	char *backing_buffer;
	struct mutex bulk_mutex;
	int fb_count;
	atomic_t usb_active; /* 0 = update virtual buffer, but no usb traffic */
	atomic_t lost_pixels; /* 1 = a render op failed. Need screen refresh */
	atomic_t use_defio; /* 0 = rely on ioctls and blit/copy/fill rects */
	char edid[128];
	int sku_pixel_limit;
	int screen_size;
	int line_length;
	struct completion done;
	int base16;
	int base16d;
	int base8;
	int base8d;
	u32 pseudo_palette[256];
	/* blit-only rendering path metrics, exposed through sysfs */
	atomic_t bytes_rendered; /* raw pixel-bytes driver asked to render */
	atomic_t bytes_identical; /* saved effort with backbuffer comparison */
	atomic_t bytes_sent; /* to usb, after compression including overhead */
	atomic_t cpu_kcycles_used; /* transpired during pixel processing */
	/* interface usage metrics. Clients can call driver via several */
	atomic_t blit_count;
	atomic_t copy_count;
	atomic_t fill_count;
	atomic_t damage_count;
	atomic_t defio_fault_count;
};

#define NR_USB_REQUEST_I2C_SUB_IO 0x02
#define NR_USB_REQUEST_CHANNEL 0x12

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE 512
#define MAX_TRANSFER (PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT (4)

#define GET_URB_TIMEOUT	HZ
#define FREE_URB_TIMEOUT (HZ*2)

static void dlfb_bulk_callback(struct urb *urb)
{
	struct dlfb_data *dev_info = urb->context;
	complete(&dev_info->done);
}

static void dlfb_edid(struct dlfb_data *dev_info)
{
	int i;
	int ret;
	char rbuf[2];

	for (i = 0; i < 128; i++) {
		ret =
		    usb_control_msg(dev_info->udev,
				    usb_rcvctrlpipe(dev_info->udev, 0), (0x02),
				    (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				    0);
		dev_info->edid[i] = rbuf[1];
	}

}

static int dlfb_bulk_msg(struct dlfb_data *dev_info, int len)
{
	int ret;

	init_completion(&dev_info->done);

	dev_info->tx_urb->actual_length = 0;
	dev_info->tx_urb->transfer_buffer_length = len;

	ret = usb_submit_urb(dev_info->tx_urb, GFP_KERNEL);
	if (!wait_for_completion_timeout(&dev_info->done, 1000)) {
		usb_kill_urb(dev_info->tx_urb);
		printk("usb timeout !!!\n");
	}

	return dev_info->tx_urb->actual_length;
}

#define dlfb_set_register insert_command

/* remove once this gets added to sysfs.h */
#define __ATTR_RW(attr) __ATTR(attr, 0644, attr##_show, attr##_store)

#define dl_err(format, arg...) \
	dev_err(dev->gdev, "dlfb: " format, ## arg)
#define dl_warn(format, arg...) \
	dev_warn(dev->gdev, "dlfb: " format, ## arg)
#define dl_notice(format, arg...) \
	dev_notice(dev->gdev, "dlfb: " format, ## arg)
#define dl_info(format, arg...) \
	dev_info(dev->gdev, "dlfb: " format, ## arg)
#endif
