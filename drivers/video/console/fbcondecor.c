/*
 *  linux/drivers/video/console/fbcondecor.c -- Framebuffer console decorations
 *
 *  Copyright (C) 2004-2009 Michal Januszewski <spock@gentoo.org>
 *
 *  Code based upon "Bootsplash" (C) 2001-2003
 *       Volker Poplawski <volker@poplawski.de>,
 *       Stefan Reinauer <stepan@suse.de>,
 *       Steffen Winterfeldt <snwint@suse.de>,
 *       Michael Schroeder <mls@suse.de>,
 *       Ken Wimer <wimer@suse.de>.
 *
 *  Compat ioctl support by Thorsten Klein <TK@Thorsten-Klein.de>.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/vmalloc.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/console.h>

#include <asm/uaccess.h>
#include <asm/irq.h>

#include "fbcon.h"
#include "fbcondecor.h"

extern signed char con2fb_map[];
static int fbcon_decor_enable(struct vc_data *vc);
char fbcon_decor_path[KMOD_PATH_LEN] = "/sbin/fbcondecor_helper";
static int initialized = 0;

int fbcon_decor_call_helper(char* cmd, unsigned short vc)
{
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin",
		NULL
	};

	char tfb[5];
	char tcons[5];
	unsigned char fb = (int) con2fb_map[vc];

	char *argv[] = {
		fbcon_decor_path,
		"2",
		cmd,
		tcons,
		tfb,
		vc_cons[vc].d->vc_decor.theme,
		NULL
	};

	snprintf(tfb,5,"%d",fb);
	snprintf(tcons,5,"%d",vc);

	return call_usermodehelper(fbcon_decor_path, argv, envp, UMH_WAIT_EXEC);
}

/* Disables fbcondecor on a virtual console; called with console sem held. */
int fbcon_decor_disable(struct vc_data *vc, unsigned char redraw)
{
	struct fb_info* info;

	if (!vc->vc_decor.state)
		return -EINVAL;

	info = registered_fb[(int) con2fb_map[vc->vc_num]];

	if (info == NULL)
		return -EINVAL;

	vc->vc_decor.state = 0;
	vc_resize(vc, info->var.xres / vc->vc_font.width,
		  info->var.yres / vc->vc_font.height);

	if (fg_console == vc->vc_num && redraw) {
		redraw_screen(vc, 0);
		update_region(vc, vc->vc_origin +
			      vc->vc_size_row * vc->vc_top,
			      vc->vc_size_row * (vc->vc_bottom - vc->vc_top) / 2);
	}

	printk(KERN_INFO "fbcondecor: switched decor state to 'off' on console %d\n",
			 vc->vc_num);

	return 0;
}

/* Enables fbcondecor on a virtual console; called with console sem held. */
static int fbcon_decor_enable(struct vc_data *vc)
{
	struct fb_info* info;

	info = registered_fb[(int) con2fb_map[vc->vc_num]];

	if (vc->vc_decor.twidth == 0 || vc->vc_decor.theight == 0 ||
	    info == NULL || vc->vc_decor.state || (!info->bgdecor.data &&
	    vc->vc_num == fg_console))
		return -EINVAL;

	vc->vc_decor.state = 1;
	vc_resize(vc, vc->vc_decor.twidth / vc->vc_font.width,
		  vc->vc_decor.theight / vc->vc_font.height);

	if (fg_console == vc->vc_num) {
		redraw_screen(vc, 0);
		update_region(vc, vc->vc_origin +
			      vc->vc_size_row * vc->vc_top,
			      vc->vc_size_row * (vc->vc_bottom - vc->vc_top) / 2);
		fbcon_decor_clear_margins(vc, info, 0);
	}

	printk(KERN_INFO "fbcondecor: switched decor state to 'on' on console %d\n",
			 vc->vc_num);

	return 0;
}

static inline int fbcon_decor_ioctl_dosetstate(struct vc_data *vc, unsigned int state, unsigned char origin)
{
	int ret;

//	if (origin == FBCON_DECOR_IO_ORIG_USER)
		console_lock();
	if (!state)
		ret = fbcon_decor_disable(vc, 1);
	else
		ret = fbcon_decor_enable(vc);
//	if (origin == FBCON_DECOR_IO_ORIG_USER)
		console_unlock();

	return ret;
}

static inline void fbcon_decor_ioctl_dogetstate(struct vc_data *vc, unsigned int *state)
{
	*state = vc->vc_decor.state;
}

static int fbcon_decor_ioctl_dosetcfg(struct vc_data *vc, struct vc_decor *cfg, unsigned char origin)
{
	struct fb_info *info;
	int len;
	char *tmp;

	info = registered_fb[(int) con2fb_map[vc->vc_num]];

	if (info == NULL || !cfg->twidth || !cfg->theight ||
	    cfg->tx + cfg->twidth  > info->var.xres ||
	    cfg->ty + cfg->theight > info->var.yres)
		return -EINVAL;

	len = strlen_user(cfg->theme);
	if (!len || len > FBCON_DECOR_THEME_LEN)
		return -EINVAL;
	tmp = kmalloc(len, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	if (copy_from_user(tmp, (void __user *)cfg->theme, len))
		return -EFAULT;
	cfg->theme = tmp;
	cfg->state = 0;

	/* If this ioctl is a response to a request from kernel, the console sem
	 * is already held; we also don't need to disable decor because either the
	 * new config and background picture will be successfully loaded, and the
	 * decor will stay on, or in case of a failure it'll be turned off in fbcon. */
//	if (origin == FBCON_DECOR_IO_ORIG_USER) {
		console_lock();
		if (vc->vc_decor.state)
			fbcon_decor_disable(vc, 1);
//	}

	if (vc->vc_decor.theme)
		kfree(vc->vc_decor.theme);

	vc->vc_decor = *cfg;

//	if (origin == FBCON_DECOR_IO_ORIG_USER)
		console_unlock();

	printk(KERN_INFO "fbcondecor: console %d using theme '%s'\n",
			 vc->vc_num, vc->vc_decor.theme);
	return 0;
}

static int fbcon_decor_ioctl_dogetcfg(struct vc_data *vc, struct vc_decor *decor)
{
	char __user *tmp;

	tmp = decor->theme;
	*decor = vc->vc_decor;
	decor->theme = tmp;

	if (vc->vc_decor.theme) {
		if (copy_to_user(tmp, vc->vc_decor.theme, strlen(vc->vc_decor.theme) + 1))
			return -EFAULT;
	} else
		if (put_user(0, tmp))
			return -EFAULT;

	return 0;
}

static int fbcon_decor_ioctl_dosetpic(struct vc_data *vc, struct fb_image *img, unsigned char origin)
{
	struct fb_info *info;
	int len;
	u8 *tmp;

	if (vc->vc_num != fg_console)
		return -EINVAL;

	info = registered_fb[(int) con2fb_map[vc->vc_num]];

	if (info == NULL)
		return -EINVAL;

	if (img->width != info->var.xres || img->height != info->var.yres) {
		printk(KERN_ERR "fbcondecor: picture dimensions mismatch\n");
		printk(KERN_ERR "%dx%d vs %dx%d\n", img->width, img->height, info->var.xres, info->var.yres);
		return -EINVAL;
	}

	if (img->depth != info->var.bits_per_pixel) {
		printk(KERN_ERR "fbcondecor: picture depth mismatch\n");
		return -EINVAL;
	}

	if (img->depth == 8) {
		if (!img->cmap.len || !img->cmap.red || !img->cmap.green ||
		    !img->cmap.blue)
			return -EINVAL;

		tmp = vmalloc(img->cmap.len * 3 * 2);
		if (!tmp)
			return -ENOMEM;

		if (copy_from_user(tmp,
			    	   (void __user*)img->cmap.red, (img->cmap.len << 1)) ||
		    copy_from_user(tmp + (img->cmap.len << 1),
			    	   (void __user*)img->cmap.green, (img->cmap.len << 1)) ||
		    copy_from_user(tmp + (img->cmap.len << 2),
			    	   (void __user*)img->cmap.blue, (img->cmap.len << 1))) {
			vfree(tmp);
			return -EFAULT;
		}

		img->cmap.transp = NULL;
		img->cmap.red = (u16*)tmp;
		img->cmap.green = img->cmap.red + img->cmap.len;
		img->cmap.blue = img->cmap.green + img->cmap.len;
	} else {
		img->cmap.red = NULL;
	}

	len = ((img->depth + 7) >> 3) * img->width * img->height;

	/*
	 * Allocate an additional byte so that we never go outside of the
	 * buffer boundaries in the rendering functions in a 24 bpp mode.
	 */
	tmp = vmalloc(len + 1);

	if (!tmp)
		goto out;

	if (copy_from_user(tmp, (void __user*)img->data, len))
		goto out;

	img->data = tmp;

	/* If this ioctl is a response to a request from kernel, the console sem
	 * is already held. */
//	if (origin == FBCON_DECOR_IO_ORIG_USER)
		console_lock();

	if (info->bgdecor.data)
		vfree((u8*)info->bgdecor.data);
	if (info->bgdecor.cmap.red)
		vfree(info->bgdecor.cmap.red);

	info->bgdecor = *img;

	if (fbcon_decor_active_vc(vc) && fg_console == vc->vc_num) {
		redraw_screen(vc, 0);
		update_region(vc, vc->vc_origin +
			      vc->vc_size_row * vc->vc_top,
			      vc->vc_size_row * (vc->vc_bottom - vc->vc_top) / 2);
		fbcon_decor_clear_margins(vc, info, 0);
	}

//	if (origin == FBCON_DECOR_IO_ORIG_USER)
		console_unlock();

	return 0;

out:	if (img->cmap.red)
		vfree(img->cmap.red);

	if (tmp)
		vfree(tmp);
	return -ENOMEM;
}

static long fbcon_decor_ioctl(struct file *filp, u_int cmd, u_long arg)
{
	struct fbcon_decor_iowrapper __user *wrapper = (void __user*) arg;
	struct vc_data *vc = NULL;
	unsigned short vc_num = 0;
	unsigned char origin = 0;
	void __user *data = NULL;

	if (!access_ok(VERIFY_READ, wrapper,
			sizeof(struct fbcon_decor_iowrapper)))
		return -EFAULT;

	__get_user(vc_num, &wrapper->vc);
	__get_user(origin, &wrapper->origin);
	__get_user(data, &wrapper->data);

	if (!vc_cons_allocated(vc_num))
		return -EINVAL;

	vc = vc_cons[vc_num].d;

	switch (cmd) {
	case FBIOCONDECOR_SETPIC:
	{
		struct fb_image img;
		if (copy_from_user(&img, (struct fb_image __user *)data, sizeof(struct fb_image)))
			return -EFAULT;

		return fbcon_decor_ioctl_dosetpic(vc, &img, origin);
	}
	case FBIOCONDECOR_SETCFG:
	{
		struct vc_decor cfg;
		if (copy_from_user(&cfg, (struct vc_decor __user *)data, sizeof(struct vc_decor)))
			return -EFAULT;

		return fbcon_decor_ioctl_dosetcfg(vc, &cfg, origin);
	}
	case FBIOCONDECOR_GETCFG:
	{
		int rval;
		struct vc_decor cfg;

		if (copy_from_user(&cfg, (struct vc_decor __user *)data, sizeof(struct vc_decor)))
			return -EFAULT;

		rval = fbcon_decor_ioctl_dogetcfg(vc, &cfg);

		if (copy_to_user(data, &cfg, sizeof(struct vc_decor)))
			return -EFAULT;
		return rval;
	}
	case FBIOCONDECOR_SETSTATE:
	{
		unsigned int state = 0;
		if (get_user(state, (unsigned int __user *)data))
			return -EFAULT;
		return fbcon_decor_ioctl_dosetstate(vc, state, origin);
	}
	case FBIOCONDECOR_GETSTATE:
	{
		unsigned int state = 0;
		fbcon_decor_ioctl_dogetstate(vc, &state);
		return put_user(state, (unsigned int __user *)data);
	}

	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT

static long fbcon_decor_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

	struct fbcon_decor_iowrapper32 __user *wrapper = (void __user *)arg;
	struct vc_data *vc = NULL;
	unsigned short vc_num = 0;
	unsigned char origin = 0;
	compat_uptr_t data_compat = 0;
	void __user *data = NULL;

	if (!access_ok(VERIFY_READ, wrapper,
                       sizeof(struct fbcon_decor_iowrapper32)))
		return -EFAULT;

	__get_user(vc_num, &wrapper->vc);
	__get_user(origin, &wrapper->origin);
	__get_user(data_compat, &wrapper->data);
	data = compat_ptr(data_compat);

	if (!vc_cons_allocated(vc_num))
		return -EINVAL;

	vc = vc_cons[vc_num].d;

	switch (cmd) {
	case FBIOCONDECOR_SETPIC32:
	{
		struct fb_image32 img_compat;
		struct fb_image img;

		if (copy_from_user(&img_compat, (struct fb_image32 __user *)data, sizeof(struct fb_image32)))
			return -EFAULT;

		fb_image_from_compat(img, img_compat);

		return fbcon_decor_ioctl_dosetpic(vc, &img, origin);
	}

	case FBIOCONDECOR_SETCFG32:
	{
		struct vc_decor32 cfg_compat;
		struct vc_decor cfg;

		if (copy_from_user(&cfg_compat, (struct vc_decor32 __user *)data, sizeof(struct vc_decor32)))
			return -EFAULT;

		vc_decor_from_compat(cfg, cfg_compat);

		return fbcon_decor_ioctl_dosetcfg(vc, &cfg, origin);
	}

	case FBIOCONDECOR_GETCFG32:
	{
		int rval;
		struct vc_decor32 cfg_compat;
		struct vc_decor cfg;

		if (copy_from_user(&cfg_compat, (struct vc_decor32 __user *)data, sizeof(struct vc_decor32)))
			return -EFAULT;
		cfg.theme = compat_ptr(cfg_compat.theme);

		rval = fbcon_decor_ioctl_dogetcfg(vc, &cfg);

		vc_decor_to_compat(cfg_compat, cfg);

		if (copy_to_user((struct vc_decor32 __user *)data, &cfg_compat, sizeof(struct vc_decor32)))
			return -EFAULT;
		return rval;
	}

	case FBIOCONDECOR_SETSTATE32:
	{
		compat_uint_t state_compat = 0;
		unsigned int state = 0;

		if (get_user(state_compat, (compat_uint_t __user *)data))
			return -EFAULT;

		state = (unsigned int)state_compat;

		return fbcon_decor_ioctl_dosetstate(vc, state, origin);
	}

	case FBIOCONDECOR_GETSTATE32:
	{
		compat_uint_t state_compat = 0;
		unsigned int state = 0;

		fbcon_decor_ioctl_dogetstate(vc, &state);
		state_compat = (compat_uint_t)state;

		return put_user(state_compat, (compat_uint_t __user *)data);
	}

	default:
		return -ENOIOCTLCMD;
	}
}
#else
  #define fbcon_decor_compat_ioctl NULL
#endif

static struct file_operations fbcon_decor_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fbcon_decor_ioctl,
	.compat_ioctl = fbcon_decor_compat_ioctl
};

static struct miscdevice fbcon_decor_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "fbcondecor",
	.fops = &fbcon_decor_ops
};

void fbcon_decor_reset()
{
	int i;

	for (i = 0; i < num_registered_fb; i++) {
		registered_fb[i]->bgdecor.data = NULL;
		registered_fb[i]->bgdecor.cmap.red = NULL;
	}

	for (i = 0; i < MAX_NR_CONSOLES && vc_cons[i].d; i++) {
		vc_cons[i].d->vc_decor.state = vc_cons[i].d->vc_decor.twidth =
						vc_cons[i].d->vc_decor.theight = 0;
		vc_cons[i].d->vc_decor.theme = NULL;
	}

	return;
}

int fbcon_decor_init()
{
	int i;

	fbcon_decor_reset();

	if (initialized)
		return 0;

	i = misc_register(&fbcon_decor_dev);
	if (i) {
		printk(KERN_ERR "fbcondecor: failed to register device\n");
		return i;
	}

	fbcon_decor_call_helper("init", 0);
	initialized = 1;
	return 0;
}

int fbcon_decor_exit(void)
{
	fbcon_decor_reset();
	return 0;
}

EXPORT_SYMBOL(fbcon_decor_path);
