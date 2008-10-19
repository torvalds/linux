/*
 * linux/drivers/video/metronomefb.c -- FB driver for Metronome controller
 *
 * Copyright (C) 2008, Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Layout is based on skeletonfb.c by James Simmons and Geert Uytterhoeven.
 *
 * This work was made possible by help and equipment support from E-Ink
 * Corporation. http://support.eink.com/community
 *
 * This driver is written to be used with the Metronome display controller.
 * It is intended to be architecture independent. A board specific driver
 * must be used to perform all the physical IO interactions. An example
 * is provided as am200epd.c
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/irq.h>

#include <video/metronomefb.h>

#include <asm/unaligned.h>

/* Display specific information */
#define DPY_W 832
#define DPY_H 622

static int user_wfm_size;

/* frame differs from image. frame includes non-visible pixels */
struct epd_frame {
	int fw; /* frame width */
	int fh; /* frame height */
	u16 config[4];
	int wfm_size;
};

static struct epd_frame epd_frame_table[] = {
	{
		.fw = 832,
		.fh = 622,
		.config = {
			15 /* sdlew */
			| 2 << 8 /* sdosz */
			| 0 << 11 /* sdor */
			| 0 << 12 /* sdces */
			| 0 << 15, /* sdcer */
			42 /* gdspl */
			| 1 << 8 /* gdr1 */
			| 1 << 9 /* sdshr */
			| 0 << 15, /* gdspp */
			18 /* gdspw */
			| 0 << 15, /* dispc */
			599 /* vdlc */
			| 0 << 11 /* dsi */
			| 0 << 12, /* dsic */
		},
		.wfm_size = 47001,
	},
	{
		.fw = 1088,
		.fh = 791,
		.config = {
			0x0104,
			0x031f,
			0x0088,
			0x02ff,
		},
		.wfm_size = 46770,
	},
	{
		.fw = 1200,
		.fh = 842,
		.config = {
			0x0101,
			0x030e,
			0x0012,
			0x0280,
		},
		.wfm_size = 46770,
	},
};

static struct fb_fix_screeninfo metronomefb_fix __devinitdata = {
	.id =		"metronomefb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_STATIC_PSEUDOCOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.line_length =	DPY_W,
	.accel =	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo metronomefb_var __devinitdata = {
	.xres		= DPY_W,
	.yres		= DPY_H,
	.xres_virtual	= DPY_W,
	.yres_virtual	= DPY_H,
	.bits_per_pixel	= 8,
	.grayscale	= 1,
	.nonstd		= 1,
	.red =		{ 4, 3, 0 },
	.green =	{ 0, 0, 0 },
	.blue =		{ 0, 0, 0 },
	.transp =	{ 0, 0, 0 },
};

/* the waveform structure that is coming from userspace firmware */
struct waveform_hdr {
	u8 stuff[32];

	u8 wmta[3];
	u8 fvsn;

	u8 luts;
	u8 mc;
	u8 trc;
	u8 stuff3;

	u8 endb;
	u8 swtb;
	u8 stuff2a[2];

	u8 stuff2b[3];
	u8 wfm_cs;
} __attribute__ ((packed));

/* main metronomefb functions */
static u8 calc_cksum(int start, int end, u8 *mem)
{
	u8 tmp = 0;
	int i;

	for (i = start; i < end; i++)
		tmp += mem[i];

	return tmp;
}

static u16 calc_img_cksum(u16 *start, int length)
{
	u16 tmp = 0;

	while (length--)
		tmp += *start++;

	return tmp;
}

/* here we decode the incoming waveform file and populate metromem */
static int __devinit load_waveform(u8 *mem, size_t size, int m, int t,
				struct metronomefb_par *par)
{
	int tta;
	int wmta;
	int trn = 0;
	int i;
	unsigned char v;
	u8 cksum;
	int cksum_idx;
	int wfm_idx, owfm_idx;
	int mem_idx = 0;
	struct waveform_hdr *wfm_hdr;
	u8 *metromem = par->metromem_wfm;
	struct device *dev = par->info->dev;

	if (user_wfm_size)
		epd_frame_table[par->dt].wfm_size = user_wfm_size;

	if (size != epd_frame_table[par->dt].wfm_size) {
		dev_err(dev, "Error: unexpected size %Zd != %d\n", size,
					epd_frame_table[par->dt].wfm_size);
		return -EINVAL;
	}

	wfm_hdr = (struct waveform_hdr *) mem;

	if (wfm_hdr->fvsn != 1) {
		dev_err(dev, "Error: bad fvsn %x\n", wfm_hdr->fvsn);
		return -EINVAL;
	}
	if (wfm_hdr->luts != 0) {
		dev_err(dev, "Error: bad luts %x\n", wfm_hdr->luts);
		return -EINVAL;
	}
	cksum = calc_cksum(32, 47, mem);
	if (cksum != wfm_hdr->wfm_cs) {
		dev_err(dev, "Error: bad cksum %x != %x\n", cksum,
					wfm_hdr->wfm_cs);
		return -EINVAL;
	}
	wfm_hdr->mc += 1;
	wfm_hdr->trc += 1;
	for (i = 0; i < 5; i++) {
		if (*(wfm_hdr->stuff2a + i) != 0) {
			dev_err(dev, "Error: unexpected value in padding\n");
			return -EINVAL;
		}
	}

	/* calculating trn. trn is something used to index into
	the waveform. presumably selecting the right one for the
	desired temperature. it works out the offset of the first
	v that exceeds the specified temperature */
	if ((sizeof(*wfm_hdr) + wfm_hdr->trc) > size)
		return -EINVAL;

	for (i = sizeof(*wfm_hdr); i <= sizeof(*wfm_hdr) + wfm_hdr->trc; i++) {
		if (mem[i] > t) {
			trn = i - sizeof(*wfm_hdr) - 1;
			break;
		}
	}

	/* check temperature range table checksum */
	cksum_idx = sizeof(*wfm_hdr) + wfm_hdr->trc + 1;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(sizeof(*wfm_hdr), cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		dev_err(dev, "Error: bad temperature range table cksum"
				" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	/* check waveform mode table address checksum */
	wmta = get_unaligned_le32(wfm_hdr->wmta) & 0x00FFFFFF;
	cksum_idx = wmta + m*4 + 3;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(cksum_idx - 3, cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		dev_err(dev, "Error: bad mode table address cksum"
				" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	/* check waveform temperature table address checksum */
	tta = get_unaligned_le32(mem + wmta + m * 4) & 0x00FFFFFF;
	cksum_idx = tta + trn*4 + 3;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(cksum_idx - 3, cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		dev_err(dev, "Error: bad temperature table address cksum"
			" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}

	/* here we do the real work of putting the waveform into the
	metromem buffer. this does runlength decoding of the waveform */
	wfm_idx = get_unaligned_le32(mem + tta + trn * 4) & 0x00FFFFFF;
	owfm_idx = wfm_idx;
	if (wfm_idx > size)
		return -EINVAL;
	while (wfm_idx < size) {
		unsigned char rl;
		v = mem[wfm_idx++];
		if (v == wfm_hdr->swtb) {
			while (((v = mem[wfm_idx++]) != wfm_hdr->swtb) &&
				wfm_idx < size)
				metromem[mem_idx++] = v;

			continue;
		}

		if (v == wfm_hdr->endb)
			break;

		rl = mem[wfm_idx++];
		for (i = 0; i <= rl; i++)
			metromem[mem_idx++] = v;
	}

	cksum_idx = wfm_idx;
	if (cksum_idx > size)
		return -EINVAL;
	cksum = calc_cksum(owfm_idx, cksum_idx, mem);
	if (cksum != mem[cksum_idx]) {
		dev_err(dev, "Error: bad waveform data cksum"
				" %x != %x\n", cksum, mem[cksum_idx]);
		return -EINVAL;
	}
	par->frame_count = (mem_idx/64);

	return 0;
}

static int metronome_display_cmd(struct metronomefb_par *par)
{
	int i;
	u16 cs;
	u16 opcode;
	static u8 borderval;

	/* setup display command
	we can't immediately set the opcode since the controller
	will try parse the command before we've set it all up
	so we just set cs here and set the opcode at the end */

	if (par->metromem_cmd->opcode == 0xCC40)
		opcode = cs = 0xCC41;
	else
		opcode = cs = 0xCC40;

	/* set the args ( 2 bytes ) for display */
	i = 0;
	par->metromem_cmd->args[i] = 	1 << 3 /* border update */
					| ((borderval++ % 4) & 0x0F) << 4
					| (par->frame_count - 1) << 8;
	cs += par->metromem_cmd->args[i++];

	/* the rest are 0 */
	memset((u8 *) (par->metromem_cmd->args + i), 0, (32-i)*2);

	par->metromem_cmd->csum = cs;
	par->metromem_cmd->opcode = opcode; /* display cmd */

	return par->board->met_wait_event_intr(par);
}

static int __devinit metronome_powerup_cmd(struct metronomefb_par *par)
{
	int i;
	u16 cs;

	/* setup power up command */
	par->metromem_cmd->opcode = 0x1234; /* pwr up pseudo cmd */
	cs = par->metromem_cmd->opcode;

	/* set pwr1,2,3 to 1024 */
	for (i = 0; i < 3; i++) {
		par->metromem_cmd->args[i] = 1024;
		cs += par->metromem_cmd->args[i];
	}

	/* the rest are 0 */
	memset((u8 *) (par->metromem_cmd->args + i), 0, (32-i)*2);

	par->metromem_cmd->csum = cs;

	msleep(1);
	par->board->set_rst(par, 1);

	msleep(1);
	par->board->set_stdby(par, 1);

	return par->board->met_wait_event(par);
}

static int __devinit metronome_config_cmd(struct metronomefb_par *par)
{
	/* setup config command
	we can't immediately set the opcode since the controller
	will try parse the command before we've set it all up */

	memcpy(par->metromem_cmd->args, epd_frame_table[par->dt].config,
		sizeof(epd_frame_table[par->dt].config));
	/* the rest are 0 */
	memset((u8 *) (par->metromem_cmd->args + 4), 0, (32-4)*2);

	par->metromem_cmd->csum = 0xCC10;
	par->metromem_cmd->csum += calc_img_cksum(par->metromem_cmd->args, 4);
	par->metromem_cmd->opcode = 0xCC10; /* config cmd */

	return par->board->met_wait_event(par);
}

static int __devinit metronome_init_cmd(struct metronomefb_par *par)
{
	int i;
	u16 cs;

	/* setup init command
	we can't immediately set the opcode since the controller
	will try parse the command before we've set it all up
	so we just set cs here and set the opcode at the end */

	cs = 0xCC20;

	/* set the args ( 2 bytes ) for init */
	i = 0;
	par->metromem_cmd->args[i] = 0;
	cs += par->metromem_cmd->args[i++];

	/* the rest are 0 */
	memset((u8 *) (par->metromem_cmd->args + i), 0, (32-i)*2);

	par->metromem_cmd->csum = cs;
	par->metromem_cmd->opcode = 0xCC20; /* init cmd */

	return par->board->met_wait_event(par);
}

static int __devinit metronome_init_regs(struct metronomefb_par *par)
{
	int res;

	res = par->board->setup_io(par);
	if (res)
		return res;

	res = metronome_powerup_cmd(par);
	if (res)
		return res;

	res = metronome_config_cmd(par);
	if (res)
		return res;

	res = metronome_init_cmd(par);

	return res;
}

static void metronomefb_dpy_update(struct metronomefb_par *par)
{
	int fbsize;
	u16 cksum;
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;

	fbsize = par->info->fix.smem_len;
	/* copy from vm to metromem */
	memcpy(par->metromem_img, buf, fbsize);

	cksum = calc_img_cksum((u16 *) par->metromem_img, fbsize/2);
	*((u16 *)(par->metromem_img) + fbsize/2) = cksum;
	metronome_display_cmd(par);
}

static u16 metronomefb_dpy_update_page(struct metronomefb_par *par, int index)
{
	int i;
	u16 csum = 0;
	u16 *buf = (u16 __force *)(par->info->screen_base + index);
	u16 *img = (u16 *)(par->metromem_img + index);

	/* swizzle from vm to metromem and recalc cksum at the same time*/
	for (i = 0; i < PAGE_SIZE/2; i++) {
		*(img + i) = (buf[i] << 5) & 0xE0E0;
		csum += *(img + i);
	}
	return csum;
}

/* this is called back from the deferred io workqueue */
static void metronomefb_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	u16 cksum;
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct metronomefb_par *par = info->par;

	/* walk the written page list and swizzle the data */
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		cksum = metronomefb_dpy_update_page(par,
					(cur->index << PAGE_SHIFT));
		par->metromem_img_csum -= par->csum_table[cur->index];
		par->csum_table[cur->index] = cksum;
		par->metromem_img_csum += cksum;
	}

	metronome_display_cmd(par);
}

static void metronomefb_fillrect(struct fb_info *info,
				   const struct fb_fillrect *rect)
{
	struct metronomefb_par *par = info->par;

	sys_fillrect(info, rect);
	metronomefb_dpy_update(par);
}

static void metronomefb_copyarea(struct fb_info *info,
				   const struct fb_copyarea *area)
{
	struct metronomefb_par *par = info->par;

	sys_copyarea(info, area);
	metronomefb_dpy_update(par);
}

static void metronomefb_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	struct metronomefb_par *par = info->par;

	sys_imageblit(info, image);
	metronomefb_dpy_update(par);
}

/*
 * this is the slow path from userspace. they can seek and write to
 * the fb. it is based on fb_sys_write
 */
static ssize_t metronomefb_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct metronomefb_par *par = info->par;
	unsigned long p = *ppos;
	void *dst;
	int err = 0;
	unsigned long total_size;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	dst = (void __force *)(info->screen_base + p);

	if (copy_from_user(dst, buf, count))
		err = -EFAULT;

	if  (!err)
		*ppos += count;

	metronomefb_dpy_update(par);

	return (err) ? err : count;
}

static struct fb_ops metronomefb_ops = {
	.owner		= THIS_MODULE,
	.fb_write	= metronomefb_write,
	.fb_fillrect	= metronomefb_fillrect,
	.fb_copyarea	= metronomefb_copyarea,
	.fb_imageblit	= metronomefb_imageblit,
};

static struct fb_deferred_io metronomefb_defio = {
	.delay		= HZ,
	.deferred_io	= metronomefb_dpy_deferred_io,
};

static int __devinit metronomefb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	struct metronome_board *board;
	int retval = -ENOMEM;
	int videomemorysize;
	unsigned char *videomemory;
	struct metronomefb_par *par;
	const struct firmware *fw_entry;
	int i;
	int panel_type;
	int fw, fh;
	int epd_dt_index;

	/* pick up board specific routines */
	board = dev->dev.platform_data;
	if (!board)
		return -EINVAL;

	/* try to count device specific driver, if can't, platform recalls */
	if (!try_module_get(board->owner))
		return -ENODEV;

	info = framebuffer_alloc(sizeof(struct metronomefb_par), &dev->dev);
	if (!info)
		goto err;

	/* we have two blocks of memory.
	info->screen_base which is vm, and is the fb used by apps.
	par->metromem which is physically contiguous memory and
	contains the display controller commands, waveform,
	processed image data and padding. this is the data pulled
	by the device's LCD controller and pushed to Metronome.
	the metromem memory is allocated by the board driver and
	is provided to us */

	panel_type = board->get_panel_type();
	switch (panel_type) {
	case 6:
		epd_dt_index = 0;
		break;
	case 8:
		epd_dt_index = 1;
		break;
	case 97:
		epd_dt_index = 2;
		break;
	default:
		dev_err(&dev->dev, "Unexpected panel type. Defaulting to 6\n");
		epd_dt_index = 0;
		break;
	}

	fw = epd_frame_table[epd_dt_index].fw;
	fh = epd_frame_table[epd_dt_index].fh;

	/* we need to add a spare page because our csum caching scheme walks
	 * to the end of the page */
	videomemorysize = PAGE_SIZE + (fw * fh);
	videomemory = vmalloc(videomemorysize);
	if (!videomemory)
		goto err_fb_rel;

	memset(videomemory, 0, videomemorysize);

	info->screen_base = (char __force __iomem *)videomemory;
	info->fbops = &metronomefb_ops;

	metronomefb_fix.line_length = fw;
	metronomefb_var.xres = fw;
	metronomefb_var.yres = fh;
	metronomefb_var.xres_virtual = fw;
	metronomefb_var.yres_virtual = fh;
	info->var = metronomefb_var;
	info->fix = metronomefb_fix;
	info->fix.smem_len = videomemorysize;
	par = info->par;
	par->info = info;
	par->board = board;
	par->dt = epd_dt_index;
	init_waitqueue_head(&par->waitq);

	/* this table caches per page csum values. */
	par->csum_table = vmalloc(videomemorysize/PAGE_SIZE);
	if (!par->csum_table)
		goto err_vfree;

	/* the physical framebuffer that we use is setup by
	 * the platform device driver. It will provide us
	 * with cmd, wfm and image memory in a contiguous area. */
	retval = board->setup_fb(par);
	if (retval) {
		dev_err(&dev->dev, "Failed to setup fb\n");
		goto err_csum_table;
	}

	/* after this point we should have a framebuffer */
	if ((!par->metromem_wfm) ||  (!par->metromem_img) ||
		(!par->metromem_dma)) {
		dev_err(&dev->dev, "fb access failure\n");
		retval = -EINVAL;
		goto err_csum_table;
	}

	info->fix.smem_start = par->metromem_dma;

	/* load the waveform in. assume mode 3, temp 31 for now
		a) request the waveform file from userspace
		b) process waveform and decode into metromem */
	retval = request_firmware(&fw_entry, "metronome.wbf", &dev->dev);
	if (retval < 0) {
		dev_err(&dev->dev, "Failed to get waveform\n");
		goto err_csum_table;
	}

	retval = load_waveform((u8 *) fw_entry->data, fw_entry->size, 3, 31,
				par);
	release_firmware(fw_entry);
	if (retval < 0) {
		dev_err(&dev->dev, "Failed processing waveform\n");
		goto err_csum_table;
	}

	if (board->setup_irq(info))
		goto err_csum_table;

	retval = metronome_init_regs(par);
	if (retval < 0)
		goto err_free_irq;

	info->flags = FBINFO_FLAG_DEFAULT;

	info->fbdefio = &metronomefb_defio;
	fb_deferred_io_init(info);

	retval = fb_alloc_cmap(&info->cmap, 8, 0);
	if (retval < 0) {
		dev_err(&dev->dev, "Failed to allocate colormap\n");
		goto err_free_irq;
	}

	/* set cmap */
	for (i = 0; i < 8; i++)
		info->cmap.red[i] = (((2*i)+1)*(0xFFFF))/16;
	memcpy(info->cmap.green, info->cmap.red, sizeof(u16)*8);
	memcpy(info->cmap.blue, info->cmap.red, sizeof(u16)*8);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_cmap;

	platform_set_drvdata(dev, info);

	dev_dbg(&dev->dev,
		"fb%d: Metronome frame buffer device, using %dK of video"
		" memory\n", info->node, videomemorysize >> 10);

	return 0;

err_cmap:
	fb_dealloc_cmap(&info->cmap);
err_free_irq:
	board->cleanup(par);
err_csum_table:
	vfree(par->csum_table);
err_vfree:
	vfree(videomemory);
err_fb_rel:
	framebuffer_release(info);
err:
	module_put(board->owner);
	return retval;
}

static int __devexit metronomefb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		struct metronomefb_par *par = info->par;

		unregister_framebuffer(info);
		fb_deferred_io_cleanup(info);
		fb_dealloc_cmap(&info->cmap);
		par->board->cleanup(par);
		vfree(par->csum_table);
		vfree((void __force *)info->screen_base);
		module_put(par->board->owner);
		dev_dbg(&dev->dev, "calling release\n");
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver metronomefb_driver = {
	.probe	= metronomefb_probe,
	.remove = metronomefb_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "metronomefb",
	},
};

static int __init metronomefb_init(void)
{
	return platform_driver_register(&metronomefb_driver);
}

static void __exit metronomefb_exit(void)
{
	platform_driver_unregister(&metronomefb_driver);
}

module_param(user_wfm_size, uint, 0);
MODULE_PARM_DESC(user_wfm_size, "Set custom waveform size");

module_init(metronomefb_init);
module_exit(metronomefb_exit);

MODULE_DESCRIPTION("fbdev driver for Metronome controller");
MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
