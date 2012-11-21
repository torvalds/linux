/*
 * linux/drivers/video/omap2/omapfb-ioctl.c
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
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
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/omapfb.h>
#include <linux/vmalloc.h>
#include <linux/export.h>

#include <video/omapdss.h>
#include <plat/vrfb.h>
#include <plat/vram.h>

#include "omapfb.h"

static u8 get_mem_idx(struct omapfb_info *ofbi)
{
	if (ofbi->id == ofbi->region->id)
		return 0;

	return OMAPFB_MEM_IDX_ENABLED | ofbi->region->id;
}

static struct omapfb2_mem_region *get_mem_region(struct omapfb_info *ofbi,
						 u8 mem_idx)
{
	struct omapfb2_device *fbdev = ofbi->fbdev;

	if (mem_idx & OMAPFB_MEM_IDX_ENABLED)
		mem_idx &= OMAPFB_MEM_IDX_MASK;
	else
		mem_idx = ofbi->id;

	if (mem_idx >= fbdev->num_fbs)
		return NULL;

	return &fbdev->regions[mem_idx];
}

static int omapfb_setup_plane(struct fb_info *fbi, struct omapfb_plane_info *pi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_overlay *ovl;
	struct omap_overlay_info old_info;
	struct omapfb2_mem_region *old_rg, *new_rg;
	int r = 0;

	DBG("omapfb_setup_plane\n");

	if (ofbi->num_overlays == 0) {
		r = -EINVAL;
		goto out;
	}

	/* XXX uses only the first overlay */
	ovl = ofbi->overlays[0];

	old_rg = ofbi->region;
	new_rg = get_mem_region(ofbi, pi->mem_idx);
	if (!new_rg) {
		r = -EINVAL;
		goto out;
	}

	/* Take the locks in a specific order to keep lockdep happy */
	if (old_rg->id < new_rg->id) {
		omapfb_get_mem_region(old_rg);
		omapfb_get_mem_region(new_rg);
	} else if (new_rg->id < old_rg->id) {
		omapfb_get_mem_region(new_rg);
		omapfb_get_mem_region(old_rg);
	} else
		omapfb_get_mem_region(old_rg);

	if (pi->enabled && !new_rg->size) {
		/*
		 * This plane's memory was freed, can't enable it
		 * until it's reallocated.
		 */
		r = -EINVAL;
		goto put_mem;
	}

	ovl->get_overlay_info(ovl, &old_info);

	if (old_rg != new_rg) {
		ofbi->region = new_rg;
		set_fb_fix(fbi);
	}

	if (!pi->enabled) {
		r = ovl->disable(ovl);
		if (r)
			goto undo;
	}

	if (pi->enabled) {
		r = omapfb_setup_overlay(fbi, ovl, pi->pos_x, pi->pos_y,
			pi->out_width, pi->out_height);
		if (r)
			goto undo;
	} else {
		struct omap_overlay_info info;

		ovl->get_overlay_info(ovl, &info);

		info.pos_x = pi->pos_x;
		info.pos_y = pi->pos_y;
		info.out_width = pi->out_width;
		info.out_height = pi->out_height;

		r = ovl->set_overlay_info(ovl, &info);
		if (r)
			goto undo;
	}

	if (ovl->manager)
		ovl->manager->apply(ovl->manager);

	if (pi->enabled) {
		r = ovl->enable(ovl);
		if (r)
			goto undo;
	}

	/* Release the locks in a specific order to keep lockdep happy */
	if (old_rg->id > new_rg->id) {
		omapfb_put_mem_region(old_rg);
		omapfb_put_mem_region(new_rg);
	} else if (new_rg->id > old_rg->id) {
		omapfb_put_mem_region(new_rg);
		omapfb_put_mem_region(old_rg);
	} else
		omapfb_put_mem_region(old_rg);

	return 0;

 undo:
	if (old_rg != new_rg) {
		ofbi->region = old_rg;
		set_fb_fix(fbi);
	}

	ovl->set_overlay_info(ovl, &old_info);
 put_mem:
	/* Release the locks in a specific order to keep lockdep happy */
	if (old_rg->id > new_rg->id) {
		omapfb_put_mem_region(old_rg);
		omapfb_put_mem_region(new_rg);
	} else if (new_rg->id > old_rg->id) {
		omapfb_put_mem_region(new_rg);
		omapfb_put_mem_region(old_rg);
	} else
		omapfb_put_mem_region(old_rg);
 out:
	dev_err(fbdev->dev, "setup_plane failed\n");

	return r;
}

static int omapfb_query_plane(struct fb_info *fbi, struct omapfb_plane_info *pi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);

	if (ofbi->num_overlays == 0) {
		memset(pi, 0, sizeof(*pi));
	} else {
		struct omap_overlay *ovl;
		struct omap_overlay_info ovli;

		ovl = ofbi->overlays[0];
		ovl->get_overlay_info(ovl, &ovli);

		pi->pos_x = ovli.pos_x;
		pi->pos_y = ovli.pos_y;
		pi->enabled = ovl->is_enabled(ovl);
		pi->channel_out = 0; /* xxx */
		pi->mirror = 0;
		pi->mem_idx = get_mem_idx(ofbi);
		pi->out_width = ovli.out_width;
		pi->out_height = ovli.out_height;
	}

	return 0;
}

static int omapfb_setup_mem(struct fb_info *fbi, struct omapfb_mem_info *mi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb2_mem_region *rg;
	int r = 0, i;
	size_t size;

	if (mi->type != OMAPFB_MEMTYPE_SDRAM)
		return -EINVAL;

	size = PAGE_ALIGN(mi->size);

	rg = ofbi->region;

	down_write_nested(&rg->lock, rg->id);
	atomic_inc(&rg->lock_count);

	if (rg->size == size && rg->type == mi->type)
		goto out;

	if (atomic_read(&rg->map_count)) {
		r = -EBUSY;
		goto out;
	}

	for (i = 0; i < fbdev->num_fbs; i++) {
		struct omapfb_info *ofbi2 = FB2OFB(fbdev->fbs[i]);
		int j;

		if (ofbi2->region != rg)
			continue;

		for (j = 0; j < ofbi2->num_overlays; j++) {
			struct omap_overlay *ovl;
			ovl = ofbi2->overlays[j];
			if (ovl->is_enabled(ovl)) {
				r = -EBUSY;
				goto out;
			}
		}
	}

	r = omapfb_realloc_fbmem(fbi, size, mi->type);
	if (r) {
		dev_err(fbdev->dev, "realloc fbmem failed\n");
		goto out;
	}

 out:
	atomic_dec(&rg->lock_count);
	up_write(&rg->lock);

	return r;
}

static int omapfb_query_mem(struct fb_info *fbi, struct omapfb_mem_info *mi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_mem_region *rg;

	rg = omapfb_get_mem_region(ofbi->region);
	memset(mi, 0, sizeof(*mi));

	mi->size = rg->size;
	mi->type = rg->type;

	omapfb_put_mem_region(rg);

	return 0;
}

static int omapfb_update_window_nolock(struct fb_info *fbi,
		u32 x, u32 y, u32 w, u32 h)
{
	struct omap_dss_device *display = fb2display(fbi);
	u16 dw, dh;

	if (!display)
		return 0;

	if (w == 0 || h == 0)
		return 0;

	display->driver->get_resolution(display, &dw, &dh);

	if (x + w > dw || y + h > dh)
		return -EINVAL;

	return display->driver->update(display, x, y, w, h);
}

/* This function is exported for SGX driver use */
int omapfb_update_window(struct fb_info *fbi,
		u32 x, u32 y, u32 w, u32 h)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	int r;

	if (!lock_fb_info(fbi))
		return -ENODEV;
	omapfb_lock(fbdev);

	r = omapfb_update_window_nolock(fbi, x, y, w, h);

	omapfb_unlock(fbdev);
	unlock_fb_info(fbi);

	return r;
}
EXPORT_SYMBOL(omapfb_update_window);

int omapfb_set_update_mode(struct fb_info *fbi,
				   enum omapfb_update_mode mode)
{
	struct omap_dss_device *display = fb2display(fbi);
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb_display_data *d;
	int r;

	if (!display)
		return -EINVAL;

	if (mode != OMAPFB_AUTO_UPDATE && mode != OMAPFB_MANUAL_UPDATE)
		return -EINVAL;

	omapfb_lock(fbdev);

	d = get_display_data(fbdev, display);

	if (d->update_mode == mode) {
		omapfb_unlock(fbdev);
		return 0;
	}

	r = 0;

	if (display->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE) {
		if (mode == OMAPFB_AUTO_UPDATE)
			omapfb_start_auto_update(fbdev, display);
		else /* MANUAL_UPDATE */
			omapfb_stop_auto_update(fbdev, display);

		d->update_mode = mode;
	} else { /* AUTO_UPDATE */
		if (mode == OMAPFB_MANUAL_UPDATE)
			r = -EINVAL;
	}

	omapfb_unlock(fbdev);

	return r;
}

int omapfb_get_update_mode(struct fb_info *fbi,
		enum omapfb_update_mode *mode)
{
	struct omap_dss_device *display = fb2display(fbi);
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb_display_data *d;

	if (!display)
		return -EINVAL;

	omapfb_lock(fbdev);

	d = get_display_data(fbdev, display);

	*mode = d->update_mode;

	omapfb_unlock(fbdev);

	return 0;
}

/* XXX this color key handling is a hack... */
static struct omapfb_color_key omapfb_color_keys[2];

static int _omapfb_set_color_key(struct omap_overlay_manager *mgr,
		struct omapfb_color_key *ck)
{
	struct omap_overlay_manager_info info;
	enum omap_dss_trans_key_type kt;
	int r;

	mgr->get_manager_info(mgr, &info);

	if (ck->key_type == OMAPFB_COLOR_KEY_DISABLED) {
		info.trans_enabled = false;
		omapfb_color_keys[mgr->id] = *ck;

		r = mgr->set_manager_info(mgr, &info);
		if (r)
			return r;

		r = mgr->apply(mgr);

		return r;
	}

	switch (ck->key_type) {
	case OMAPFB_COLOR_KEY_GFX_DST:
		kt = OMAP_DSS_COLOR_KEY_GFX_DST;
		break;
	case OMAPFB_COLOR_KEY_VID_SRC:
		kt = OMAP_DSS_COLOR_KEY_VID_SRC;
		break;
	default:
		return -EINVAL;
	}

	info.default_color = ck->background;
	info.trans_key = ck->trans_key;
	info.trans_key_type = kt;
	info.trans_enabled = true;

	omapfb_color_keys[mgr->id] = *ck;

	r = mgr->set_manager_info(mgr, &info);
	if (r)
		return r;

	r = mgr->apply(mgr);

	return r;
}

static int omapfb_set_color_key(struct fb_info *fbi,
		struct omapfb_color_key *ck)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	int r;
	int i;
	struct omap_overlay_manager *mgr = NULL;

	omapfb_lock(fbdev);

	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ofbi->overlays[i]->manager) {
			mgr = ofbi->overlays[i]->manager;
			break;
		}
	}

	if (!mgr) {
		r = -EINVAL;
		goto err;
	}

	r = _omapfb_set_color_key(mgr, ck);
err:
	omapfb_unlock(fbdev);

	return r;
}

static int omapfb_get_color_key(struct fb_info *fbi,
		struct omapfb_color_key *ck)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_overlay_manager *mgr = NULL;
	int r = 0;
	int i;

	omapfb_lock(fbdev);

	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ofbi->overlays[i]->manager) {
			mgr = ofbi->overlays[i]->manager;
			break;
		}
	}

	if (!mgr) {
		r = -EINVAL;
		goto err;
	}

	*ck = omapfb_color_keys[mgr->id];
err:
	omapfb_unlock(fbdev);

	return r;
}

static int omapfb_memory_read(struct fb_info *fbi,
		struct omapfb_memory_read *mr)
{
	struct omap_dss_device *display = fb2display(fbi);
	void *buf;
	int r;

	if (!display || !display->driver->memory_read)
		return -ENOENT;

	if (!access_ok(VERIFY_WRITE, mr->buffer, mr->buffer_size))
		return -EFAULT;

	if (mr->w * mr->h * 3 > mr->buffer_size)
		return -EINVAL;

	buf = vmalloc(mr->buffer_size);
	if (!buf) {
		DBG("vmalloc failed\n");
		return -ENOMEM;
	}

	r = display->driver->memory_read(display, buf, mr->buffer_size,
			mr->x, mr->y, mr->w, mr->h);

	if (r > 0) {
		if (copy_to_user(mr->buffer, buf, mr->buffer_size))
			r = -EFAULT;
	}

	vfree(buf);

	return r;
}

static int omapfb_get_ovl_colormode(struct omapfb2_device *fbdev,
			     struct omapfb_ovl_colormode *mode)
{
	int ovl_idx = mode->overlay_idx;
	int mode_idx = mode->mode_idx;
	struct omap_overlay *ovl;
	enum omap_color_mode supported_modes;
	struct fb_var_screeninfo var;
	int i;

	if (ovl_idx >= fbdev->num_overlays)
		return -ENODEV;
	ovl = fbdev->overlays[ovl_idx];
	supported_modes = ovl->supported_modes;

	mode_idx = mode->mode_idx;

	for (i = 0; i < sizeof(supported_modes) * 8; i++) {
		if (!(supported_modes & (1 << i)))
			continue;
		/*
		 * It's possible that the FB doesn't support a mode
		 * that is supported by the overlay, so call the
		 * following here.
		 */
		if (dss_mode_to_fb_mode(1 << i, &var) < 0)
			continue;

		mode_idx--;
		if (mode_idx < 0)
			break;
	}

	if (i == sizeof(supported_modes) * 8)
		return -ENOENT;

	mode->bits_per_pixel = var.bits_per_pixel;
	mode->nonstd = var.nonstd;
	mode->red = var.red;
	mode->green = var.green;
	mode->blue = var.blue;
	mode->transp = var.transp;

	return 0;
}

static int omapfb_wait_for_go(struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	int r = 0;
	int i;

	for (i = 0; i < ofbi->num_overlays; ++i) {
		struct omap_overlay *ovl = ofbi->overlays[i];
		r = ovl->wait_for_go(ovl);
		if (r)
			break;
	}

	return r;
}

int omapfb_ioctl(struct fb_info *fbi, unsigned int cmd, unsigned long arg)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_dss_device *display = fb2display(fbi);
	struct omap_overlay_manager *mgr;

	union {
		struct omapfb_update_window_old	uwnd_o;
		struct omapfb_update_window	uwnd;
		struct omapfb_plane_info	plane_info;
		struct omapfb_caps		caps;
		struct omapfb_mem_info          mem_info;
		struct omapfb_color_key		color_key;
		struct omapfb_ovl_colormode	ovl_colormode;
		enum omapfb_update_mode		update_mode;
		int test_num;
		struct omapfb_memory_read	memory_read;
		struct omapfb_vram_info		vram_info;
		struct omapfb_tearsync_info	tearsync_info;
		struct omapfb_display_info	display_info;
		u32				crt;
	} p;

	int r = 0;

	switch (cmd) {
	case OMAPFB_SYNC_GFX:
		DBG("ioctl SYNC_GFX\n");
		if (!display || !display->driver->sync) {
			/* DSS1 never returns an error here, so we neither */
			/*r = -EINVAL;*/
			break;
		}

		r = display->driver->sync(display);
		break;

	case OMAPFB_UPDATE_WINDOW_OLD:
		DBG("ioctl UPDATE_WINDOW_OLD\n");
		if (!display || !display->driver->update) {
			r = -EINVAL;
			break;
		}

		if (copy_from_user(&p.uwnd_o,
					(void __user *)arg,
					sizeof(p.uwnd_o))) {
			r = -EFAULT;
			break;
		}

		r = omapfb_update_window_nolock(fbi, p.uwnd_o.x, p.uwnd_o.y,
				p.uwnd_o.width, p.uwnd_o.height);
		break;

	case OMAPFB_UPDATE_WINDOW:
		DBG("ioctl UPDATE_WINDOW\n");
		if (!display || !display->driver->update) {
			r = -EINVAL;
			break;
		}

		if (copy_from_user(&p.uwnd, (void __user *)arg,
					sizeof(p.uwnd))) {
			r = -EFAULT;
			break;
		}

		r = omapfb_update_window_nolock(fbi, p.uwnd.x, p.uwnd.y,
				p.uwnd.width, p.uwnd.height);
		break;

	case OMAPFB_SETUP_PLANE:
		DBG("ioctl SETUP_PLANE\n");
		if (copy_from_user(&p.plane_info, (void __user *)arg,
					sizeof(p.plane_info)))
			r = -EFAULT;
		else
			r = omapfb_setup_plane(fbi, &p.plane_info);
		break;

	case OMAPFB_QUERY_PLANE:
		DBG("ioctl QUERY_PLANE\n");
		r = omapfb_query_plane(fbi, &p.plane_info);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.plane_info,
					sizeof(p.plane_info)))
			r = -EFAULT;
		break;

	case OMAPFB_SETUP_MEM:
		DBG("ioctl SETUP_MEM\n");
		if (copy_from_user(&p.mem_info, (void __user *)arg,
					sizeof(p.mem_info)))
			r = -EFAULT;
		else
			r = omapfb_setup_mem(fbi, &p.mem_info);
		break;

	case OMAPFB_QUERY_MEM:
		DBG("ioctl QUERY_MEM\n");
		r = omapfb_query_mem(fbi, &p.mem_info);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.mem_info,
					sizeof(p.mem_info)))
			r = -EFAULT;
		break;

	case OMAPFB_GET_CAPS:
		DBG("ioctl GET_CAPS\n");
		if (!display) {
			r = -EINVAL;
			break;
		}

		memset(&p.caps, 0, sizeof(p.caps));
		if (display->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE)
			p.caps.ctrl |= OMAPFB_CAPS_MANUAL_UPDATE;
		if (display->caps & OMAP_DSS_DISPLAY_CAP_TEAR_ELIM)
			p.caps.ctrl |= OMAPFB_CAPS_TEARSYNC;

		if (copy_to_user((void __user *)arg, &p.caps, sizeof(p.caps)))
			r = -EFAULT;
		break;

	case OMAPFB_GET_OVERLAY_COLORMODE:
		DBG("ioctl GET_OVERLAY_COLORMODE\n");
		if (copy_from_user(&p.ovl_colormode, (void __user *)arg,
				   sizeof(p.ovl_colormode))) {
			r = -EFAULT;
			break;
		}
		r = omapfb_get_ovl_colormode(fbdev, &p.ovl_colormode);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.ovl_colormode,
				 sizeof(p.ovl_colormode)))
			r = -EFAULT;
		break;

	case OMAPFB_SET_UPDATE_MODE:
		DBG("ioctl SET_UPDATE_MODE\n");
		if (get_user(p.update_mode, (int __user *)arg))
			r = -EFAULT;
		else
			r = omapfb_set_update_mode(fbi, p.update_mode);
		break;

	case OMAPFB_GET_UPDATE_MODE:
		DBG("ioctl GET_UPDATE_MODE\n");
		r = omapfb_get_update_mode(fbi, &p.update_mode);
		if (r)
			break;
		if (put_user(p.update_mode,
					(enum omapfb_update_mode __user *)arg))
			r = -EFAULT;
		break;

	case OMAPFB_SET_COLOR_KEY:
		DBG("ioctl SET_COLOR_KEY\n");
		if (copy_from_user(&p.color_key, (void __user *)arg,
				   sizeof(p.color_key)))
			r = -EFAULT;
		else
			r = omapfb_set_color_key(fbi, &p.color_key);
		break;

	case OMAPFB_GET_COLOR_KEY:
		DBG("ioctl GET_COLOR_KEY\n");
		r = omapfb_get_color_key(fbi, &p.color_key);
		if (r)
			break;
		if (copy_to_user((void __user *)arg, &p.color_key,
				 sizeof(p.color_key)))
			r = -EFAULT;
		break;

	case FBIO_WAITFORVSYNC:
		if (get_user(p.crt, (__u32 __user *)arg)) {
			r = -EFAULT;
			break;
		}
		if (p.crt != 0) {
			r = -ENODEV;
			break;
		}
		/* FALLTHROUGH */

	case OMAPFB_WAITFORVSYNC:
		DBG("ioctl WAITFORVSYNC\n");
		if (!display && !display->output && !display->output->manager) {
			r = -EINVAL;
			break;
		}

		mgr = display->output->manager;

		r = mgr->wait_for_vsync(mgr);
		break;

	case OMAPFB_WAITFORGO:
		DBG("ioctl WAITFORGO\n");
		if (!display) {
			r = -EINVAL;
			break;
		}

		r = omapfb_wait_for_go(fbi);
		break;

	/* LCD and CTRL tests do the same thing for backward
	 * compatibility */
	case OMAPFB_LCD_TEST:
		DBG("ioctl LCD_TEST\n");
		if (get_user(p.test_num, (int __user *)arg)) {
			r = -EFAULT;
			break;
		}
		if (!display || !display->driver->run_test) {
			r = -EINVAL;
			break;
		}

		r = display->driver->run_test(display, p.test_num);

		break;

	case OMAPFB_CTRL_TEST:
		DBG("ioctl CTRL_TEST\n");
		if (get_user(p.test_num, (int __user *)arg)) {
			r = -EFAULT;
			break;
		}
		if (!display || !display->driver->run_test) {
			r = -EINVAL;
			break;
		}

		r = display->driver->run_test(display, p.test_num);

		break;

	case OMAPFB_MEMORY_READ:
		DBG("ioctl MEMORY_READ\n");

		if (copy_from_user(&p.memory_read, (void __user *)arg,
					sizeof(p.memory_read))) {
			r = -EFAULT;
			break;
		}

		r = omapfb_memory_read(fbi, &p.memory_read);

		break;

	case OMAPFB_GET_VRAM_INFO: {
		unsigned long vram, free, largest;

		DBG("ioctl GET_VRAM_INFO\n");

		omap_vram_get_info(&vram, &free, &largest);
		p.vram_info.total = vram;
		p.vram_info.free = free;
		p.vram_info.largest_free_block = largest;

		if (copy_to_user((void __user *)arg, &p.vram_info,
					sizeof(p.vram_info)))
			r = -EFAULT;
		break;
	}

	case OMAPFB_SET_TEARSYNC: {
		DBG("ioctl SET_TEARSYNC\n");

		if (copy_from_user(&p.tearsync_info, (void __user *)arg,
					sizeof(p.tearsync_info))) {
			r = -EFAULT;
			break;
		}

		if (!display || !display->driver->enable_te) {
			r = -ENODEV;
			break;
		}

		r = display->driver->enable_te(display,
				!!p.tearsync_info.enabled);

		break;
	}

	case OMAPFB_GET_DISPLAY_INFO: {
		u16 xres, yres;

		DBG("ioctl GET_DISPLAY_INFO\n");

		if (display == NULL) {
			r = -ENODEV;
			break;
		}

		display->driver->get_resolution(display, &xres, &yres);

		p.display_info.xres = xres;
		p.display_info.yres = yres;

		if (display->driver->get_dimensions) {
			u32 w, h;
			display->driver->get_dimensions(display, &w, &h);
			p.display_info.width = w;
			p.display_info.height = h;
		} else {
			p.display_info.width = 0;
			p.display_info.height = 0;
		}

		if (copy_to_user((void __user *)arg, &p.display_info,
					sizeof(p.display_info)))
			r = -EFAULT;
		break;
	}

	default:
		dev_err(fbdev->dev, "Unknown ioctl 0x%x\n", cmd);
		r = -EINVAL;
	}

	if (r < 0)
		DBG("ioctl failed: %d\n", r);

	return r;
}


