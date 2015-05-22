/*
 * linux/drivers/video/rockchip/rkfb-sysfs.c
 *
 * Copyright (C) 2012 Rockchip Corporation
 * Author: yxj<yxj@rock-chips.com>
 *
 * Some code and ideas taken from
 *drivers/video/omap2/omapfb/omapfb-sys.c
 *driver by Tomi Valkeinen.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fb.h>
#include <linux/namei.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/div64.h>
#include <linux/rk_screen.h>
#include <linux/rk_fb.h>
#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#endif
#include "bmp_helper.h"

struct rkfb_sys_trace {
	int num_frames;
	int count_frame;
	int mask_win;
	int mask_area;
	bool is_bmp;
	bool is_append;
};
#define DUMP_BUF_PATH		"/data/dmp_buf"

static char *get_format_str(enum data_format format)
{
	switch (format) {
	case ARGB888:
		return "ARGB888";
	case RGB888:
		return "RGB888";
	case RGB565:
		return "RGB565";
	case YUV420:
	case YUV420_NV21:
		return "YUV420";
	case YUV422:
		return "YUV422";
	case YUV444:
		return "YUV444";
	case YUV420_A:
		return "YUV420_A";
	case YUV422_A:
		return "YUV422_A";
	case YUV444_A:
		return "YUV444_A";
	case XRGB888:
		return "XRGB888";
	case XBGR888:
		return "XBGR888";
	case ABGR888:
		return "ABGR888";
	case FBDC_RGB_565:
		return "FBDC_RGB_565";
	case FBDC_ARGB_888:
		return "FBDC_ARGB_888";
	case FBDC_RGBX_888:
		return "FBDC_RGBX_888";
	default:
		return "invalid";
	}
}

static ssize_t show_screen_info(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_screen *screen = dev_drv->cur_screen;
	int fps = 0;
	u32 x = screen->mode.left_margin + screen->mode.right_margin +
		screen->mode.xres + screen->mode.hsync_len;
	u32 y = screen->mode.upper_margin + screen->mode.lower_margin +
		screen->mode.yres + screen->mode.vsync_len;
	u64 ft = (u64)x * y * (dev_drv->pixclock);

	if (ft > 0)
		fps = div64_u64(1000000000000llu, ft);
	return snprintf(buf, PAGE_SIZE, "xres:%d\nyres:%d\nfps:%d\n",
			screen->mode.xres, screen->mode.yres, fps);
}

static ssize_t show_disp_info(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int win_id = dev_drv->ops->fb_get_win_id(dev_drv, fbi->fix.id);

	if (dev_drv->ops->get_disp_info)
		return dev_drv->ops->get_disp_info(dev_drv, buf, win_id);

	return 0;
}

static void fill_buffer(void *handle, void *vaddr, int size)
{
	struct file *filp = handle;

	if (filp)
		vfs_write(filp, vaddr, size, &filp->f_pos);
}

static int dump_win(struct ion_client *ion_client,
		    struct ion_handle *ion_handle, phys_addr_t phys_addr,
		    int width, int height, u8 data_format, uint32_t frameid,
		    int win_id, int area_id, bool is_bmp, bool is_append)
{
	void __iomem *vaddr = NULL;
	struct file *filp;
	mm_segment_t old_fs;
	char name[100];
	int flags;
	int bits;

	switch (data_format) {
	case XRGB888:
	case XBGR888:
	case ARGB888:
	case ABGR888:
	case FBDC_RGBX_888:
		bits = 32;
		break;
	case YUV444_A:
	case YUV444:
	case RGB888:
	case FBDC_ARGB_888:
		bits = 24;
		break;
	case RGB565:
	case FBDC_RGB_565:
	case YUV422:
	case YUV422_A:
		bits = 16;
		break;
	case YUV420_A:
	case YUV420:
	case YUV420_NV21:
		bits = 12;
		break;
	default:
		return 0;
	}

	if (ion_handle) {
		vaddr = ion_map_kernel(ion_client, ion_handle);
	} else if (phys_addr) {
		unsigned long start;
		unsigned int nr_pages;
		struct page **pages;
		int i = 0;

		start = phys_addr;
		nr_pages = roundup(width * height * (bits >> 3), PAGE_SIZE);
		nr_pages /= PAGE_SIZE;
		pages = kzalloc(sizeof(struct page) * nr_pages,GFP_KERNEL);
		while (i < nr_pages) {
			pages[i] = phys_to_page(start);
			start += PAGE_SIZE;
			i++;
		}
		vaddr = vmap(pages, nr_pages, VM_MAP,
			     pgprot_writecombine(PAGE_KERNEL));
		if (!vaddr) {
			pr_err("failed to vmap phy addr %lx\n",
			       start);
			return -1;
		}
	} else {
		return 0;
	}

	flags = O_RDWR | O_CREAT | O_NONBLOCK;
	if (is_append) {
		snprintf(name, 100, "%s/append_win%d_%d_%dx%d_%s.%s",
			 DUMP_BUF_PATH, win_id, area_id, width, height,
			 get_format_str(data_format), is_bmp ? "bmp" : "bin");
		flags |= O_APPEND;
	} else {
		snprintf(name, 100, "%s/frame%d_win%d_%d_%dx%d_%s.%s",
			 DUMP_BUF_PATH, frameid, win_id, area_id, width, height,
			 get_format_str(data_format), is_bmp ? "bmp" : "bin");
	}

	pr_info("dump win == > %s\n", name);
	filp = filp_open(name, flags, 0x600);
	if (!filp)
		printk("fail to create %s\n", name);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (is_bmp)
		bmpencoder(vaddr, width, height,
			   data_format, filp, fill_buffer);
	else
		fill_buffer(filp, vaddr, width * height * bits >> 3);

	set_fs(old_fs);

	if (ion_handle)
		ion_unmap_kernel(ion_client, ion_handle);
	else if (vaddr)
		vunmap(vaddr);

	filp_close(filp, NULL);

	return 0;
}

static ssize_t show_dump_buffer(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	ssize_t size;

	size = snprintf(buf, PAGE_SIZE,
			"bmp       -- dump buffer to bmp image\n"
			"bin       -- dump buffer to bin image\n"
			"multi    --  each dump will create new file\n"
			"win=num   -- mask win to dump, default mask all\n"
			"             win=1, will dump win1 buffer\n"
			"             win=23, will dump win2 area3 buffer\n"
			"trace=num -- trace num frames buffer dump\n"
			"             this option will block buffer switch\n"
			"             so recommend use with bin and win=xx\n"
			"\nExample:\n"
			"echo bmp > dump_buf; -- dump current buf to bmp file\n"
			"echo bin > dump_buf; -- dump current buf to bin file\n"
			"echo trace=50:win=1:win=23 > dump_buf\n"
			"         -- dump 50 frames, dump win1 and win2 area3\n"
			"         -- dump all buffer to single file\n"
			"You can found dump files at %s\n"
			, DUMP_BUF_PATH);

	return size;
}

void trace_buffer_dump(struct device *dev, struct rk_lcdc_driver *dev_drv)
{
	struct rk_fb *rk_fb = dev_get_drvdata(dev);
	struct rk_fb_reg_data *front_regs;
	struct rk_fb_reg_win_data *win_data;
	struct rk_fb_reg_area_data *area_data;
	struct rkfb_sys_trace *trace = dev_drv->trace_buf;
	int i,j;

	if (!trace)
		return;
	if (trace->num_frames <= trace->count_frame)
		return;

	if (!dev_drv->front_regs)
		return;
	front_regs = dev_drv->front_regs;

	for (i = 0; i < front_regs->win_num; i++) {
		if (trace->mask_win && !(trace->mask_win & (1 << i)))
			continue;
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			win_data = &front_regs->reg_win_data[i];
			area_data = &win_data->reg_area_data[j];
			if (trace->mask_area && !(trace->mask_area & (1 << j)))
				continue;

			dump_win(rk_fb->ion_client, area_data->ion_handle,
				 area_data->smem_start,
				 area_data->xvir, area_data->yvir,
				 area_data->data_format, trace->count_frame,
				 i, j, trace->is_bmp, trace->is_append);
		}
	}
	trace->count_frame++;
}

static ssize_t set_dump_buffer(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_fb *rk_fb = dev_get_drvdata(fbi->device);
	struct rk_fb_reg_data *front_regs;
	struct rk_fb_reg_win_data *win_data;
	struct rk_fb_reg_area_data *area_data;
	struct rkfb_sys_trace *trace;
	struct dentry *dentry;
	struct path path;
	int err = 0;
	int num_frames = 0;
	int mask_win = 0;
	int mask_area = 0;
	bool is_bmp = false;
	bool is_append = true;
	char *p;
	int i, j;

	if (!rk_fb->ion_client)
		return 0;

	if (!dev_drv->trace_buf) {
		dev_drv->trace_buf = devm_kmalloc(dev_drv->dev,
						  sizeof(struct rkfb_sys_trace),
						  GFP_KERNEL);
		if (!dev_drv->trace_buf)
			return -ENOMEM;
	}
	trace = dev_drv->trace_buf;
	/*
	 * Stop buffer trace.
	 */
	trace->num_frames = 0;

	while ((p = strsep((char **)&buf, ":")) != NULL) {
		if (!*p)
			continue;
		if (!strncmp(p, "trace=", 6)) {
			if (kstrtoint(p + 6, 0, &num_frames))
				dev_err(dev, "can't found trace frames\n");
			continue;
		}
		if (!strncmp(p, "win=", 4)) {
			int win;

			if (kstrtoint(p + 4, 0, &win))
				dev_err(dev, "can't found trace frames\n");
			if (win < 10)
			       mask_win |= 1 << win;
			else {
				mask_win |= 1 << (win / 10);
				mask_area |= 1 << (win % 10);
			}

			continue;
		}
		if (!strncmp(p, "bmp", 3)) {
			is_bmp = true;
			is_append = false;
			continue;
		}
		if (!strncmp(p, "bin", 3)) {
			is_bmp = false;
			continue;
		}
		if (!strncmp(p, "multi", 5)) {
			is_append = true;
			is_bmp = false;
			continue;
		}

		dev_err(dev, "unknown option %s\n", p);
	}

	dentry = kern_path_create(AT_FDCWD, DUMP_BUF_PATH, &path,
				  LOOKUP_DIRECTORY);
	if (!IS_ERR(dentry)) {
		err = vfs_mkdir(path.dentry->d_inode, dentry, 700);
		if (err)
			dev_err(dev, "can't create %s err%d\n",
				DUMP_BUF_PATH, err);
		done_path_create(&path, dentry);
	} else if (PTR_ERR(dentry) != -EEXIST) {
		dev_err(dev, "can't create PATH %s err%d\n",
				DUMP_BUF_PATH, err);
		return PTR_ERR(dentry);
	}

	if (!num_frames) {
		mutex_lock(&dev_drv->front_lock);

		if (!dev_drv->front_regs) {
			u16 xact, yact;
			int data_format;
			u32 dsp_addr;

			mutex_unlock(&dev_drv->front_lock);

			if (dev_drv->ops->get_dspbuf_info)
				dev_drv->ops->get_dspbuf_info(dev_drv, &xact,
						&yact, &data_format, &dsp_addr);

			dump_win(NULL, NULL, dsp_addr, xact, yact, data_format,
				 0, 0, 0, is_bmp, false);
			goto out;
		}
		front_regs = kmalloc(sizeof(*front_regs), GFP_KERNEL);
		if (!front_regs)
			return -ENOMEM;
		memcpy(front_regs, dev_drv->front_regs, sizeof(*front_regs));

		for (i = 0; i < front_regs->win_num; i++) {
			if (mask_win && !(mask_win & (1 << i)))
				continue;
			for (j = 0; j < RK_WIN_MAX_AREA; j++) {
				if (mask_area && !(mask_area & (1 << j)))
					continue;
				win_data = &front_regs->reg_win_data[i];
				area_data = &win_data->reg_area_data[j];
				if (area_data->ion_handle)
					ion_handle_get(area_data->ion_handle);
			}
		}

		for (i = 0; i < front_regs->win_num; i++) {
			if (mask_win && !(mask_win & (1 << i)))
				continue;
			for (j = 0; j < RK_WIN_MAX_AREA; j++) {
				if (mask_area && !(mask_area & (1 << j)))
					continue;

				win_data = &front_regs->reg_win_data[i];
				area_data = &win_data->reg_area_data[j];

				dump_win(rk_fb->ion_client, area_data->ion_handle,
					 area_data->smem_start,
					 area_data->xvir, area_data->yvir,
					 area_data->data_format, trace->count_frame,
					 i, j, trace->is_bmp, trace->is_append);
				if (area_data->ion_handle)
					ion_handle_put(area_data->ion_handle);
			}
		}

		kfree(front_regs);

		mutex_unlock(&dev_drv->front_lock);
	} else {
		trace->num_frames = num_frames;
		trace->count_frame = 0;
		trace->is_bmp = is_bmp;
		trace->is_append = is_append;
		trace->mask_win = mask_win;
		trace->mask_area = mask_area;
	}
out:

	return count;
}

static ssize_t show_phys(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%lx-----0x%x\n",
			fbi->fix.smem_start, fbi->fix.smem_len);
}

static ssize_t show_virt(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%p-----0x%x\n",
			fbi->screen_base, fbi->fix.smem_len);
}

static ssize_t show_fb_state(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	int win_id = dev_drv->ops->fb_get_win_id(dev_drv, fbi->fix.id);
	int state = dev_drv->ops->get_win_state(dev_drv, win_id, 0);

	return snprintf(buf, PAGE_SIZE, "%s\n", state ? "enabled" : "disabled");
}

static ssize_t show_dual_mode(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb *rk_fb = dev_get_drvdata(fbi->device);
	int mode = rk_fb->disp_mode;

	return snprintf(buf, PAGE_SIZE, "%d\n", mode);
}

static ssize_t set_fb_state(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int win_id = dev_drv->ops->fb_get_win_id(dev_drv, fbi->fix.id);
	int state;
	int ret;

	ret = kstrtoint(buf, 0, &state);
	if (ret)
		return ret;
	dev_drv->ops->open(dev_drv, win_id, state);
	if (state) {
		dev_drv->ops->set_par(dev_drv, win_id);
		dev_drv->ops->pan_display(dev_drv, win_id);
		dev_drv->ops->cfg_done(dev_drv);
	}
	return count;
}

static ssize_t show_overlay(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int ovl;

	if (dev_drv->ops->ovl_mgr)
		ovl = dev_drv->ops->ovl_mgr(dev_drv, 0, 0);

	if (ovl < 0)
		return ovl;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			ovl ? "win0 on the top of win1" :
			"win1 on the top of win0");
}

static ssize_t set_overlay(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	u32 ovl;
	int ret;

	ret = kstrtou32(buf, 0, &ovl);
	if (ret)
		return ret;
	if (dev_drv->ops->ovl_mgr)
		ret = dev_drv->ops->ovl_mgr(dev_drv, ovl, 1);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t show_fps(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int fps;

	if (dev_drv->ops->fps_mgr)
		fps = dev_drv->ops->fps_mgr(dev_drv, 0, 0);
	if (fps < 0)
		return fps;

	return snprintf(buf, PAGE_SIZE, "fps:%d\n", fps);
}

static ssize_t set_fps(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	u32 fps;
	int ret;

	ret = kstrtou32(buf, 0, &fps);
	if (ret)
		return ret;

	if (fps == 0 || fps > 60) {
		dev_info(dev, "unsupport fps value,pelase set 1~60\n");
		return count;
	}

	if (dev_drv->ops->fps_mgr)
		ret = dev_drv->ops->fps_mgr(dev_drv, fps, 1);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t show_fb_win_map(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	mutex_lock(&dev_drv->fb_win_id_mutex);
	ret =
	    snprintf(buf, PAGE_SIZE, "fb0:win%d\nfb1:win%d\nfb2:win%d\n",
		     dev_drv->fb0_win_id, dev_drv->fb1_win_id,
		     dev_drv->fb2_win_id);
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return ret;
}

static ssize_t set_fb_win_map(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	u32 order;
	int ret;

	ret = kstrtou32(buf, 0, &order);
	if ((order != FB0_WIN2_FB1_WIN1_FB2_WIN0) &&
	    (order != FB0_WIN1_FB1_WIN2_FB2_WIN0) &&
	    (order != FB0_WIN2_FB1_WIN0_FB2_WIN1) &&
	    (order != FB0_WIN0_FB1_WIN2_FB2_WIN1) &&
	    (order != FB0_WIN0_FB1_WIN1_FB2_WIN2) &&
	    (order != FB0_WIN1_FB1_WIN0_FB2_WIN2)) {
		dev_info(dev, "un supported map\n"
		       "you can use the following order:\n" "201:\n"
		       "fb0-win1\n" "fb1-win0\n" "fb2-win2\n" "210:\n"
		       "fb0-win0\n" "fb1-win1\n" "fb2-win2\n" "120:\n"
		       "fb0-win0\n" "fb1-win2\n" "fb2-win1\n" "102:\n"
		       "fb0-win2\n" "fb1-win0\n" "fb2-win1\n" "021:\n"
		       "fb0-win1\n" "fb1-win2\n" "fb2-win0\n" "012:\n"
		       "fb0-win2\n" "fb1-win1\n" "fb2-win0\n");
		return count;
	} else {
		if (dev_drv->ops->fb_win_remap)
			dev_drv->ops->fb_win_remap(dev_drv, order);
	}

	return count;
}

static ssize_t show_hwc_lut(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t set_hwc_lut(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int hwc_lut[256];
	const char *start = buf;
	int i = 256, temp;
	int space_max;

	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	/*printk("count:%d\n>>%s\n\n",count,start);*/
	for (i = 0; i < 256; i++) {
		space_max = 15;	/*max space number 15*/
		temp = simple_strtoul(start, NULL, 16);
		hwc_lut[i] = temp;
		do {
			start++;
			space_max--;
		} while ((*start != ' ') && space_max);

		if (!space_max)
			break;
		else
			start++;
	}
#if 0
	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++)
			printk("0x%08x ", hwc_lut[i * 16 + j]);
		printk("\n");
	}
#endif
	if (dev_drv->ops->set_hwc_lut)
		dev_drv->ops->set_hwc_lut(dev_drv, hwc_lut, 1);

	return count;
}

static ssize_t show_cabc_lut(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t set_cabc_lut(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int cabc_lut[256];
	const char *start = buf;
	int i = 256, temp;
	int space_max = 10;

	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	for (i = 0; i < 256; i++) {
		temp = i;
		/*init by default value*/
		cabc_lut[i] = temp + (temp << 8) + (temp << 16);
	}
	for (i = 0; i < 256; i++) {
		space_max = 10;	/*max space number 10*/
		temp = simple_strtoul(start, NULL, 10);
		cabc_lut[i] = temp;
		do {
			start++;
			space_max--;
		} while ((*start != ' ') && space_max);

		if (!space_max)
			break;
		else
			start++;
	}
	if (dev_drv->ops->set_cabc_lut)
		dev_drv->ops->set_cabc_lut(dev_drv, cabc_lut);

	return count;
}

static ssize_t show_dsp_lut(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t set_dsp_lut(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int dsp_lut[256];
	const char *start = buf;
	int i = 256, temp;
	int space_max = 10;

	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	for (i = 0; i < 256; i++) {
		temp = i;
		/*init by default value*/
		dsp_lut[i] = temp + (temp << 8) + (temp << 16);
	}
	/*printk("count:%d\n>>%s\n\n",count,start);*/
	for (i = 0; i < 256; i++) {
		space_max = 10;	/*max space number 10*/
		temp = simple_strtoul(start, NULL, 10);
		dsp_lut[i] = temp;
		do {
			start++;
			space_max--;
		} while ((*start != ' ') && space_max);

		if (!space_max)
			break;
		else
			start++;
	}
#if 0
	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++)
			printk("0x%08x ", dsp_lut[i * 16 + j]);
		printk("\n");
	}
#endif
	if (dev_drv->ops->set_dsp_lut)
		dev_drv->ops->set_dsp_lut(dev_drv, dsp_lut);

	return count;
}

static ssize_t show_dsp_cabc(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	return snprintf(buf, PAGE_SIZE, "cabc mode=%d\n",
		dev_drv->cabc_mode);
	return 0;
}

static ssize_t set_dsp_cabc(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int space_max, ret, mode = 0, calc = 0,up = 0, down = 0, global = 0;
	const char *start = buf;

	space_max = 10;	/*max space number 10*/
	mode = simple_strtoul(start, NULL, 10);
	do {
		start++;
		space_max--;
	} while ((*start != ' ') && space_max);
	start++;
	calc = simple_strtoul(start, NULL, 10);

	do {
		start++;
		space_max--;
	} while ((*start != ' ') && space_max);
	start++;
	up  = simple_strtoul(start, NULL, 10);

	do {
		start++;
		space_max--;
	} while ((*start != ' ') && space_max);
	start++;
	down = simple_strtoul(start, NULL, 10);

	do {
		start++;
		space_max--;
	} while ((*start != ' ') && space_max);
	start++;
	global = simple_strtoul(start, NULL, 10);

    if (dev_drv->ops->set_dsp_cabc)
		ret = dev_drv->ops->set_dsp_cabc(dev_drv, mode, calc, up, down, global);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t show_dsp_bcsh(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int brightness, contrast, sat_con, sin_hue, cos_hue;

	if (dev_drv->ops->get_dsp_bcsh_bcs) {
		brightness = dev_drv->ops->get_dsp_bcsh_bcs(dev_drv,
							    BRIGHTNESS);
		contrast = dev_drv->ops->get_dsp_bcsh_bcs(dev_drv, CONTRAST);
		sat_con = dev_drv->ops->get_dsp_bcsh_bcs(dev_drv, SAT_CON);
	}
	if (dev_drv->ops->get_dsp_bcsh_hue) {
		sin_hue = dev_drv->ops->get_dsp_bcsh_hue(dev_drv, H_SIN);
		cos_hue = dev_drv->ops->get_dsp_bcsh_hue(dev_drv, H_COS);
	}
	return snprintf(buf, PAGE_SIZE,
			"brightness:%4d,contrast:%4d,sat_con:%4d,"
			"sin_hue:%4d,cos_hue:%4d\n",
			brightness, contrast, sat_con, sin_hue, cos_hue);
}

static ssize_t set_dsp_bcsh(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int brightness, contrast, sat_con, ret = 0, sin_hue, cos_hue;

	if (!strncmp(buf, "open", 4)) {
		if (dev_drv->ops->open_bcsh)
			ret = dev_drv->ops->open_bcsh(dev_drv, 1);
		else
			ret = -1;
	} else if (!strncmp(buf, "close", 5)) {
		if (dev_drv->ops->open_bcsh)
			ret = dev_drv->ops->open_bcsh(dev_drv, 0);
		else
			ret = -1;
	} else if (!strncmp(buf, "brightness", 10)) {
		sscanf(buf, "brightness %d", &brightness);
		if (unlikely(brightness > 255)) {
			dev_err(fbi->dev,
				"brightness should be [0:255],now=%d\n\n",
				brightness);
			brightness = 255;
		}
		if (dev_drv->ops->set_dsp_bcsh_bcs)
			ret = dev_drv->ops->set_dsp_bcsh_bcs(dev_drv,
							     BRIGHTNESS,
							     brightness);
		else
			ret = -1;
	} else if (!strncmp(buf, "contrast", 8)) {
		sscanf(buf, "contrast %d", &contrast);
		if (unlikely(contrast > 510)) {
			dev_err(fbi->dev,
				"contrast should be [0:510],now=%d\n",
				contrast);
			contrast = 510;
		}
		if (dev_drv->ops->set_dsp_bcsh_bcs)
			ret = dev_drv->ops->set_dsp_bcsh_bcs(dev_drv,
							     CONTRAST,
							     contrast);
		else
			ret = -1;
	} else if (!strncmp(buf, "sat_con", 7)) {
		sscanf(buf, "sat_con %d", &sat_con);
		if (unlikely(sat_con > 1015)) {
			dev_err(fbi->dev,
				"sat_con should be [0:1015],now=%d\n",
				sat_con);
			sat_con = 1015;
		}
		if (dev_drv->ops->set_dsp_bcsh_bcs)
			ret = dev_drv->ops->set_dsp_bcsh_bcs(dev_drv,
							     SAT_CON,
							     sat_con);
		else
			ret = -1;
	} else if (!strncmp(buf, "hue", 3)) {
		sscanf(buf, "hue %d %d", &sin_hue, &cos_hue);
		if (unlikely(sin_hue > 511 || cos_hue > 511)) {
			dev_err(fbi->dev, "sin_hue=%d,cos_hue=%d\n",
				sin_hue, cos_hue);
		}
		if (dev_drv->ops->set_dsp_bcsh_hue)
			ret = dev_drv->ops->set_dsp_bcsh_hue(dev_drv,
							     sin_hue,
							     cos_hue);
		else
			ret = -1;
	} else {
		dev_info(dev, "format error\n");
	}

	if (ret < 0)
		return ret;

	return count;
}

static ssize_t show_scale(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	return snprintf(buf, PAGE_SIZE,
		"xscale=%d yscale=%d\nleft=%d top=%d right=%d bottom=%d\n",
		(dev_drv->overscan.left + dev_drv->overscan.right) / 2,
		(dev_drv->overscan.top + dev_drv->overscan.bottom) / 2,
		dev_drv->overscan.left, dev_drv->overscan.top,
		dev_drv->overscan.right, dev_drv->overscan.bottom);
}

static ssize_t set_scale(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	u32 left, top, right, bottom;

	if (!strncmp(buf, "overscan", 8)) {
		sscanf(buf,
		       "overscan %d,%d,%d,%d", &left, &top, &right, &bottom);
		if (left > 0 && left <= 100)
			dev_drv->overscan.left = left;
		if (top > 0 && top <= 100)
			dev_drv->overscan.top = top;
		if (right > 0 && right <= 100)
			dev_drv->overscan.right = right;
		if (bottom > 0 && bottom <= 100)
			dev_drv->overscan.bottom = bottom;
	} else if (!strncmp(buf, "left", 4)) {
		sscanf(buf, "left=%d", &left);
		if (left > 0 && left <= 100)
			dev_drv->overscan.left = left;
	} else if (!strncmp(buf, "top", 3)) {
		sscanf(buf, "top=%d", &top);
		if (top > 0 && top <= 100)
			dev_drv->overscan.top = top;
	} else if (!strncmp(buf, "right", 5)) {
		sscanf(buf, "right=%d", &right);
		if (right > 0 && right <= 100)
			dev_drv->overscan.right = right;
	} else if (!strncmp(buf, "bottom", 6)) {
		sscanf(buf, "bottom=%d", &bottom);
		if (bottom > 0 && bottom <= 100)
			dev_drv->overscan.bottom = bottom;
	} else if (!strncmp(buf, "xscale", 6)) {
		sscanf(buf, "xscale=%d", &left);
		if (left > 0 && left <= 100) {
			dev_drv->overscan.left = left;
			dev_drv->overscan.right = left;
		}
	} else if (!strncmp(buf, "yscale", 6)) {
		sscanf(buf, "yscale=%d", &left);
		if (left > 0 && left <= 100) {
			dev_drv->overscan.top = left;
			dev_drv->overscan.bottom = left;
		}
	} else {
		sscanf(buf, "%d", &left);
		if (left > 0 && left <= 100) {
			dev_drv->overscan.left = left;
			dev_drv->overscan.right = left;
			dev_drv->overscan.top = left;
			dev_drv->overscan.bottom = left;
		}
	}

	if (dev_drv->ops->set_overscan)
		dev_drv->ops->set_overscan(dev_drv, &dev_drv->overscan);

	return count;
}

static ssize_t show_lcdc_id(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	return snprintf(buf, PAGE_SIZE, "%d\n", dev_drv->id);
}

static struct device_attribute rkfb_attrs[] = {
	__ATTR(phys_addr, S_IRUGO, show_phys, NULL),
	__ATTR(virt_addr, S_IRUGO, show_virt, NULL),
	__ATTR(disp_info, S_IRUGO, show_disp_info, NULL),
	__ATTR(dump_buf, S_IRUGO | S_IWUSR, show_dump_buffer, set_dump_buffer),
	__ATTR(screen_info, S_IRUGO, show_screen_info, NULL),
	__ATTR(dual_mode, S_IRUGO, show_dual_mode, NULL),
	__ATTR(enable, S_IRUGO | S_IWUSR, show_fb_state, set_fb_state),
	__ATTR(overlay, S_IRUGO | S_IWUSR, show_overlay, set_overlay),
	__ATTR(fps, S_IRUGO | S_IWUSR, show_fps, set_fps),
	__ATTR(map, S_IRUGO | S_IWUSR, show_fb_win_map, set_fb_win_map),
	__ATTR(dsp_lut, S_IRUGO | S_IWUSR, show_dsp_lut, set_dsp_lut),
	__ATTR(cabc_lut, S_IRUGO | S_IWUSR, show_cabc_lut, set_cabc_lut),
	__ATTR(hwc_lut, S_IRUGO | S_IWUSR, show_hwc_lut, set_hwc_lut),
	__ATTR(cabc, S_IRUGO | S_IWUSR, show_dsp_cabc, set_dsp_cabc),
	__ATTR(bcsh, S_IRUGO | S_IWUSR, show_dsp_bcsh, set_dsp_bcsh),
	__ATTR(scale, S_IRUGO | S_IWUSR, show_scale, set_scale),
	__ATTR(lcdcid, S_IRUGO, show_lcdc_id, NULL),
};

int rkfb_create_sysfs(struct fb_info *fbi)
{
	int r, t;

	for (t = 0; t < ARRAY_SIZE(rkfb_attrs); t++) {
		r = device_create_file(fbi->dev, &rkfb_attrs[t]);
		if (r) {
			dev_err(fbi->dev, "failed to create sysfs " "file\n");
			return r;
		}
	}

	return 0;
}

void rkfb_remove_sysfs(struct rk_fb *rk_fb)
{
	int i, t;

	for (i = 0; i < rk_fb->num_fb; i++) {
		for (t = 0; t < ARRAY_SIZE(rkfb_attrs); t++)
			device_remove_file(rk_fb->fb[i]->dev, &rkfb_attrs[t]);
	}
}
