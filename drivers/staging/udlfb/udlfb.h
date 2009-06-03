#ifndef UDLFB_H
#define UDLFB_H

#define MAX_VMODES	4
#define FB_BPP		16

#define STD_CHANNEL	"\x57\xCD\xDC\xA7\x1C\x88\x5E\x15"	\
			"\x60\xFE\xC6\x97\x16\x3D\x47\xF2"

/* as libdlo */
#define BUF_HIGH_WATER_MARK	1024
#define BUF_SIZE		(64*1024)

struct dlfb_data {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct urb *tx_urb, *ctrl_urb;
	struct usb_ctrlrequest dr;
	struct fb_info *info;
	char *buf;
	char *bufend;
	char *backing_buffer;
	struct mutex bulk_mutex;
	char edid[128];
	int screen_size;
	int line_length;
	struct completion done;
	int base16;
	int base8;
};

struct dlfb_video_mode {
	uint8_t col;
	uint32_t hclock;
	uint32_t vclock;
	uint8_t unknown1[6];
	uint16_t xres;
	uint8_t unknown2[6];
	uint16_t yres;
	uint8_t unknown3[4];
} __attribute__ ((__packed__));

static struct dlfb_video_mode dlfb_video_modes[MAX_VMODES];

static void dlfb_bulk_callback(struct urb *urb)
{
	struct dlfb_data *dev_info = urb->context;
	complete(&dev_info->done);
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

static void dlfb_init_modes(void)
{
	dlfb_video_modes[0].col = 0;
	memcpy(&dlfb_video_modes[0].hclock, "\x20\x3C\x7A\xC9", 4);
	memcpy(&dlfb_video_modes[0].vclock, "\xF2\x6C\x48\xF9", 4);
	memcpy(&dlfb_video_modes[0].unknown1, "\x70\x53\xFF\xFF\x21\x27", 6);
	dlfb_video_modes[0].xres = 800;
	memcpy(&dlfb_video_modes[0].unknown2, "\x91\xF3\xFF\xFF\xFF\xF9", 6);
	dlfb_video_modes[0].yres = 480;
	memcpy(&dlfb_video_modes[0].unknown3, "\x01\x02\xC8\x19", 4);

	dlfb_video_modes[1].col = 0;
	memcpy(&dlfb_video_modes[1].hclock, "\x36\x18\xD5\x10", 4);
	memcpy(&dlfb_video_modes[1].vclock, "\x60\xA9\x7B\x33", 4);
	memcpy(&dlfb_video_modes[1].unknown1, "\xA1\x2B\x27\x32\xFF\xFF", 6);
	dlfb_video_modes[1].xres = 1024;
	memcpy(&dlfb_video_modes[1].unknown2, "\xD9\x9A\xFF\xCA\xFF\xFF", 6);
	dlfb_video_modes[1].yres = 768;
	memcpy(&dlfb_video_modes[1].unknown3, "\x04\x03\xC8\x32", 4);

	dlfb_video_modes[2].col = 0;
	memcpy(&dlfb_video_modes[2].hclock, "\x98\xF8\x0D\x57", 4);
	memcpy(&dlfb_video_modes[2].vclock, "\x2A\x55\x4D\x54", 4);
	memcpy(&dlfb_video_modes[2].unknown1, "\xCA\x0D\xFF\xFF\x94\x43", 6);
	dlfb_video_modes[2].xres = 1280;
	memcpy(&dlfb_video_modes[2].unknown2, "\x9A\xA8\xFF\xFF\xFF\xF9", 6);
	dlfb_video_modes[2].yres = 1024;
	memcpy(&dlfb_video_modes[2].unknown3, "\x04\x02\x60\x54", 4);

	dlfb_video_modes[3].col = 0;
	memcpy(&dlfb_video_modes[3].hclock, "\x42\x24\x38\x36", 4);
	memcpy(&dlfb_video_modes[3].vclock, "\xC1\x52\xD9\x29", 4);
	memcpy(&dlfb_video_modes[3].unknown1, "\xEA\xB8\x32\x60\xFF\xFF", 6);
	dlfb_video_modes[3].xres = 1400;
	memcpy(&dlfb_video_modes[3].unknown2, "\xC9\x4E\xFF\xFF\xFF\xF2", 6);
	dlfb_video_modes[3].yres = 1050;
	memcpy(&dlfb_video_modes[3].unknown3, "\x04\x02\x1E\x5F", 4);
}

static char *dlfb_set_register(char *bufptr, uint8_t reg, uint8_t val)
{
	*bufptr++ = 0xAF;
	*bufptr++ = 0x20;
	*bufptr++ = reg;
	*bufptr++ = val;

	return bufptr;
}

static int dlfb_set_video_mode(struct dlfb_data *dev_info, int width, int height)
{
	int i, ret;
	unsigned char j;
	char *bufptr = dev_info->buf;
	uint8_t *vdata;

	for (i = 0; i < MAX_VMODES; i++) {
		printk("INIT VIDEO %d %d %d\n", i, dlfb_video_modes[i].xres,
		       dlfb_video_modes[i].yres);
		if (dlfb_video_modes[i].xres == width
		    && dlfb_video_modes[i].yres == height) {

			dev_info->base16 = 0;

			dev_info->base8 = width * height * (FB_BPP / 8);;

			/* set encryption key (null) */
			memcpy(dev_info->buf, STD_CHANNEL, 16);
			ret =
			    usb_control_msg(dev_info->udev,
					    usb_sndctrlpipe(dev_info->udev, 0),
					    0x12, (0x02 << 5), 0, 0,
					    dev_info->buf, 16, 0);
			printk("ret control msg 1 (STD_CHANNEL): %d\n", ret);

			/* set registers */
			bufptr = dlfb_set_register(bufptr, 0xFF, 0x00);

			/* set addresses */
			bufptr =
			    dlfb_set_register(bufptr, 0x20,
					      (char)(dev_info->base16 >> 16));
			bufptr =
			    dlfb_set_register(bufptr, 0x21,
					      (char)(dev_info->base16 >> 8));
			bufptr =
			    dlfb_set_register(bufptr, 0x22,
					      (char)(dev_info->base16));

			bufptr =
			    dlfb_set_register(bufptr, 0x26,
					      (char)(dev_info->base8 >> 16));
			bufptr =
			    dlfb_set_register(bufptr, 0x27,
					      (char)(dev_info->base8 >> 8));
			bufptr =
			    dlfb_set_register(bufptr, 0x28,
					      (char)(dev_info->base8));

			/* set video mode */
			vdata = (uint8_t *)&dlfb_video_modes[i];
			for (j = 0; j < 29; j++)
				bufptr = dlfb_set_register(bufptr, j, vdata[j]);

			/* blank */
			bufptr = dlfb_set_register(bufptr, 0x1F, 0x00);

			/* end registers */
			bufptr = dlfb_set_register(bufptr, 0xFF, 0xFF);

			/* send */
			ret = dlfb_bulk_msg(dev_info, bufptr - dev_info->buf);
			printk("ret bulk 2: %d %d\n", ret,
			       bufptr - dev_info->buf);

			/* flush */
			ret = dlfb_bulk_msg(dev_info, 0);
			printk("ret bulk 3: %d\n", ret);

			dev_info->screen_size = width * height * (FB_BPP / 8);
			dev_info->line_length = width * (FB_BPP / 8);

			return 0;
		}
	}

	return -1;
}

#endif
