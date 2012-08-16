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
#include <asm/div64.h>
#include <linux/rk_screen.h>
#include <linux/rk_fb.h>


static ssize_t show_screen_info(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	rk_screen * screen = dev_drv->screen;
	int fps;
	u64 ft = (u64)(screen->upper_margin + screen->lower_margin + screen->y_res +screen->vsync_len)*
		(screen->left_margin + screen->right_margin + screen->x_res + screen->hsync_len)*
		(dev_drv->pixclock);       // one frame time ,(pico seconds)
	fps = div64_u64(1000000000000llu,ft);
	return snprintf(buf, PAGE_SIZE,"xres:%d\nyres:%d\nfps:%d\n",
		screen->x_res,screen->y_res,fps);
}

static ssize_t show_disp_info(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int layer_id = dev_drv->fb_get_layer(dev_drv,fbi->fix.id);
	if(dev_drv->get_disp_info)
		dev_drv->get_disp_info(dev_drv,layer_id);

	return 0;
}

static ssize_t show_phys(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "0x%lx-----0x%x\n",
		fbi->fix.smem_start,fbi->fix.smem_len);
}

static ssize_t show_virt(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%p-----0x%x\n",
		fbi->screen_base,fbi->fix.smem_len);
}

static ssize_t show_fb_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int layer_id = dev_drv->fb_get_layer(dev_drv,fbi->fix.id);
	int state = dev_drv->get_layer_state(dev_drv,layer_id);
	return snprintf(buf, PAGE_SIZE, "%s\n",state?"enabled":"disabled");
	
}
static ssize_t set_fb_state(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int layer_id = dev_drv->fb_get_layer(dev_drv,fbi->fix.id);
	int state;
	int ret;
	ret = kstrtoint(buf, 0, &state);
	if(ret)
	{
		return ret;
	}
	dev_drv->open(dev_drv,layer_id,state);
	return count;
}

static ssize_t show_overlay(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int ovl;
	ovl =  dev_drv->ovl_mgr(dev_drv,0,0);
	if(ovl < 0)
	{
		return ovl;
	}

	return snprintf(buf, PAGE_SIZE, "%s\n",
		ovl?"win0 on the top of win1":"win1 on the top of win0");
	
}
static ssize_t set_overlay(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int ovl;
	int ret;
	ret = kstrtoint(buf, 0, &ovl);
	if(ret)
	{
		return ret;
	}
	ret = dev_drv->ovl_mgr(dev_drv,ovl,1);
	if(ret < 0)
	{
		return ret;
	}

	return count;
}


static ssize_t show_fps(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int fps;
	fps =  dev_drv->fps_mgr(dev_drv,0,0);
	if(fps < 0)
	{
		return fps;
	}

	return snprintf(buf, PAGE_SIZE, "fps:%d\n",fps);
	
}

static ssize_t set_fps(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int fps;
	int ret;
	ret = kstrtoint(buf, 0, &fps);
	if(ret)
	{
		return ret;
	}
	ret = dev_drv->fps_mgr(dev_drv,fps,1);
	if(ret < 0)
	{
		return ret;
	}

	return count;
}

static ssize_t show_fb_win_map(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;

	mutex_lock(&dev_drv->fb_win_id_mutex);
	ret = snprintf(buf, PAGE_SIZE,"fb0:win%d\nfb1:win%d\nfb2:win%d\n",dev_drv->fb0_win_id,dev_drv->fb1_win_id,
		dev_drv->fb2_win_id);
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return ret;
	
}

static ssize_t set_fb_win_map(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	int order;
	int ret;
	ret = kstrtoint(buf, 0, &order);
	if((order != FB0_WIN2_FB1_WIN1_FB2_WIN0) && (order != FB0_WIN1_FB1_WIN2_FB2_WIN0 ) &&
	   (order != FB0_WIN2_FB1_WIN0_FB2_WIN1) && (order != FB0_WIN0_FB1_WIN2_FB2_WIN1 ) &&
	   (order != FB0_WIN0_FB1_WIN1_FB2_WIN2) && (order != FB0_WIN1_FB1_WIN0_FB2_WIN2 ))
	{
		printk(KERN_ERR "un support map\nyou can use the following order: \
			\n201:\nfb0-win1\nfb1-win0\nfb2-win2\n			   \
			\n210:\nfb0-win0\nfb1-win1\nfb2-win2\n			  \
			\n120:\nfb0-win0\nfb1-win2\nfb2-win1\n			  \
			\n102:\nfb0-win2\nfb1-win0\nfb2-win1\n			   \
			\n021:\nfb0-win1\nfb1-win2\nfb2-win0\n			   \
			\n012:\nfb0-win2\nfb1-win1\nfb2-win0\n");
		return count;
	}
	else
	{
		dev_drv->fb_layer_remap(dev_drv,order);
	}

	return count;
	
	
}

static ssize_t show_dsp_lut(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	
}
static ssize_t set_dsp_lut(struct device *dev,struct device_attribute *attr,
	const char *buf, size_t count)
{
	int dsp_lut[256];
	char *start = buf;
	int i=256,j,temp;
	int space_max = 10;

	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_device_driver * dev_drv = 
		(struct rk_lcdc_device_driver * )fbi->par;
	
	for(i=0;i<256;i++)
	{
		temp = i;
		dsp_lut[i] = temp + (temp<<8) + (temp<<16);  //init by default value
	}
	//printk("count:%d\n>>%s\n\n",count,start);
	for(i=0;i<256;i++)
	{
		space_max = 10;  //max space number 10;
		temp = simple_strtoul(start,NULL,10);
		dsp_lut[i] = temp;
		do
		{
			start++;
			space_max--;
		}while ((*start != ' ')&&space_max);
		
		if(!space_max)
			break;
		else
			start++;
	}
#if 0
	for(i=0;i<16;i++)
	{
		for(j=0;j<16;j++)
			printk("0x%08x ",dsp_lut[i*16+j]);
		printk("\n");
	}
#endif
	dev_drv->set_dsp_lut(dev_drv,dsp_lut);

	return count;
	
}

static struct device_attribute rkfb_attrs[] = {
	__ATTR(phys_addr, S_IRUGO, show_phys, NULL),
	__ATTR(virt_addr, S_IRUGO, show_virt, NULL),
	__ATTR(disp_info, S_IRUGO, show_disp_info, NULL),
	__ATTR(screen_info, S_IRUGO, show_screen_info, NULL),
	__ATTR(enable, S_IRUGO | S_IWUSR, show_fb_state, set_fb_state),
	__ATTR(overlay, S_IRUGO | S_IWUSR, show_overlay, set_overlay),
	__ATTR(fps, S_IRUGO | S_IWUSR, show_fps, set_fps),
	__ATTR(map, S_IRUGO | S_IWUSR, show_fb_win_map, set_fb_win_map),
	__ATTR(dsp_lut, S_IRUGO | S_IWUSR, show_dsp_lut, set_dsp_lut),
};

int rkfb_create_sysfs(struct fb_info *fbi)
{
	int r;
	int t;
	for (t = 0; t < ARRAY_SIZE(rkfb_attrs); t++)
	{
		r = device_create_file(fbi->dev,&rkfb_attrs[t]);
		if (r)
		{
			dev_err(fbi->dev, "failed to create sysfs "
					"file\n");
			return r;
		}
	}
	

	return 0;
}

void rkfb_remove_sysfs(struct rk_fb_inf *inf)
{
	int i, t;

	for (i = 0; i < inf->num_fb; i++) {
		for (t = 0; t < ARRAY_SIZE(rkfb_attrs); t++)
			device_remove_file(inf->fb[i]->dev,
					&rkfb_attrs[t]);
	}
}

