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
	}

	return "invalid";
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

static int dump_win(struct rk_fb *rk_fb, struct rk_fb_reg_area_data *area_data,
		    u8 data_format, int win_id, int area_id, bool is_bmp)
{
	void __iomem *vaddr = NULL;
	struct file *filp;
	mm_segment_t old_fs;
	char name[100];
	struct ion_handle *ion_handle = area_data->ion_handle;
	int width = area_data->xvir;
	int height = area_data->yvir;

	if (ion_handle) {
		vaddr = ion_map_kernel(rk_fb->ion_client, ion_handle);
	} else if (area_data->smem_start && area_data->smem_start != -1) {
		unsigned long start;
		unsigned int nr_pages;
		struct page **pages;
		int i = 0;

		start = area_data->smem_start;
		nr_pages = width * height * 3 / 2 / PAGE_SIZE;
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
			       area_data->smem_start);
			return -1;
		}
	} else {
		return -1;
	}

	snprintf(name, 100, "/data/win%d_%d_%dx%d_%s.%s", win_id, area_id,
		 width, height, get_format_str(data_format),
		 is_bmp ? "bmp" : "bin");

	pr_info("dump win == > /data/win%d_%d_%dx%d_%s.%s\n", win_id, area_id,
	        width, height, get_format_str(data_format),
	        is_bmp ? "bmp" : "bin");

	filp = filp_open(name, O_RDWR | O_CREAT, 0x664);
	if (!filp)
		printk("fail to create %s\n", name);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (is_bmp)
		bmpencoder(vaddr, width, height,
			   data_format, filp, fill_buffer);
	else
		fill_buffer(filp, vaddr, width * height * 4);

	set_fs(old_fs);

	if (ion_handle) {
		ion_unmap_kernel(rk_fb->ion_client, ion_handle);

		ion_handle_put(ion_handle);
	} else if (vaddr) {
		vunmap(vaddr);
	}

	filp_close(filp, NULL);

	return 0;
}

static ssize_t set_dump_info(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)

{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_fb *rk_fb = dev_get_drvdata(fbi->device);
	struct rk_fb_reg_data *front_regs;
	struct rk_fb_reg_win_data *win_data;
	struct rk_fb_reg_area_data *area_data;
	bool is_img;
	int i, j;

	if (!rk_fb->ion_client)
		return 0;

	front_regs = kmalloc(sizeof(*front_regs), GFP_KERNEL);
	if (!front_regs)
		return -ENOMEM;

	mutex_lock(&dev_drv->front_lock);

	if (!dev_drv->front_regs) {
		mutex_unlock(&dev_drv->front_lock);
		return 0;
	}
	memcpy(front_regs, dev_drv->front_regs, sizeof(*front_regs));
	for (i = 0; i < front_regs->win_num; i++) {
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			win_data = &front_regs->reg_win_data[i];
			area_data = &win_data->reg_area_data[j];
			if (area_data->ion_handle) {
				ion_handle_get(area_data->ion_handle);
			}
		}
	}
	mutex_unlock(&dev_drv->front_lock);

	if (strncmp(buf, "bin", 3))
		is_img = true;
	else
		is_img = false;

	for (i = 0; i < front_regs->win_num; i++) {
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			win_data = &front_regs->reg_win_data[i];
			if (dump_win(rk_fb, &win_data->reg_area_data[j],
				     win_data->reg_area_data[i].data_format,i,
				     j, is_img))
				continue;
		}
	}
	kfree(front_regs);

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
	int state = dev_drv->ops->get_win_state(dev_drv, win_id);

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
	int ret, mode = 0;

	ret = kstrtoint(buf, 0, &mode);
	if (ret)
		return ret;

	if (dev_drv->ops->set_dsp_cabc)
		ret = dev_drv->ops->set_dsp_cabc(dev_drv, mode);
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
	struct rk_screen *screen = dev_drv->cur_screen;

	return snprintf(buf, PAGE_SIZE,
		"xscale=%d yscale=%d\nleft=%d top=%d right=%d bottom=%d\n",
		(screen->overscan.left + screen->overscan.right)/2,
		(screen->overscan.top + screen->overscan.bottom)/2,
		screen->overscan.left, screen->overscan.top,
		screen->overscan.right, screen->overscan.bottom);
}

static ssize_t set_scale(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_screen *screen = dev_drv->cur_screen;
	u32 left, top, right, bottom;

	if (!strncmp(buf, "overscan", 8)) {
		sscanf(buf,
		       "overscan %d,%d,%d,%d", &left, &top, &right, &bottom);
		if (left > 0 && left <= 100)
			screen->overscan.left = left;
		if (top > 0 && top <= 100)
			screen->overscan.top = top;
		if (right > 0 && right <= 100)
			screen->overscan.right = right;
		if (bottom > 0 && bottom <= 100)
			screen->overscan.bottom = bottom;
	} else if (!strncmp(buf, "left", 4)) {
		sscanf(buf, "left=%d", &left);
		if (left > 0 && left <= 100)
			screen->overscan.left = left;
	} else if (!strncmp(buf, "top", 3)) {
		sscanf(buf, "top=%d", &top);
		if (top > 0 && top <= 100)
			screen->overscan.top = top;
	} else if (!strncmp(buf, "right", 5)) {
		sscanf(buf, "right=%d", &right);
		if (right > 0 && right <= 100)
			screen->overscan.right = right;
	} else if (!strncmp(buf, "bottom", 6)) {
		sscanf(buf, "bottom=%d", &bottom);
		if (bottom > 0 && bottom <= 100)
			screen->overscan.bottom = bottom;
	} else if (!strncmp(buf, "xscale", 6)) {
		sscanf(buf, "xscale=%d", &left);
		if (left > 0 && left <= 100) {
			screen->overscan.left = left;
			screen->overscan.right = left;
		}
	} else if (!strncmp(buf, "yscale", 6)) {
		sscanf(buf, "yscale=%d", &left);
		if (left > 0 && left <= 100) {
			screen->overscan.top = left;
			screen->overscan.bottom = left;
		}
	} else {
		sscanf(buf, "%d", &left);
		if (left > 0 && left <= 100) {
			screen->overscan.left = left;
			screen->overscan.right = left;
			screen->overscan.top = left;
			screen->overscan.bottom = left;
		}
	}

	if (dev_drv->ops->set_overscan)
		dev_drv->ops->set_overscan(dev_drv, &screen->overscan);

	return count;
}

static struct device_attribute rkfb_attrs[] = {
	__ATTR(phys_addr, S_IRUGO, show_phys, NULL),
	__ATTR(virt_addr, S_IRUGO, show_virt, NULL),
	__ATTR(disp_info, S_IRUGO | S_IWUSR, show_disp_info, set_dump_info),
	__ATTR(screen_info, S_IRUGO, show_screen_info, NULL),
	__ATTR(dual_mode, S_IRUGO, show_dual_mode, NULL),
	__ATTR(enable, S_IRUGO | S_IWUSR, show_fb_state, set_fb_state),
	__ATTR(overlay, S_IRUGO | S_IWUSR, show_overlay, set_overlay),
	__ATTR(fps, S_IRUGO | S_IWUSR, show_fps, set_fps),
	__ATTR(map, S_IRUGO | S_IWUSR, show_fb_win_map, set_fb_win_map),
	__ATTR(dsp_lut, S_IRUGO | S_IWUSR, show_dsp_lut, set_dsp_lut),
	__ATTR(hwc_lut, S_IRUGO | S_IWUSR, show_hwc_lut, set_hwc_lut),
	__ATTR(cabc, S_IRUGO | S_IWUSR, show_dsp_cabc, set_dsp_cabc),
	__ATTR(bcsh, S_IRUGO | S_IWUSR, show_dsp_bcsh, set_dsp_bcsh),
	__ATTR(scale, S_IRUGO | S_IWUSR, show_scale, set_scale),
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
