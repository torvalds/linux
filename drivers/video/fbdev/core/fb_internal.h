/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FB_INTERNAL_H
#define _FB_INTERNAL_H

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/mutex.h>

/* fb_devfs.c */
#if defined(CONFIG_FB_DEVICE)
int fb_register_chrdev(void);
void fb_unregister_chrdev(void);
#else
static inline int fb_register_chrdev(void)
{
	return 0;
}
static inline void fb_unregister_chrdev(void)
{ }
#endif

/* fbmem.c */
extern struct class *fb_class;
extern struct mutex registration_lock;
extern struct fb_info *registered_fb[FB_MAX];
extern int num_registered_fb;
struct fb_info *get_fb_info(unsigned int idx);
void put_fb_info(struct fb_info *fb_info);

/* fb_procfs.c */
#if defined(CONFIG_FB_DEVICE)
int fb_init_procfs(void);
void fb_cleanup_procfs(void);
#else
static inline int fb_init_procfs(void)
{
	return 0;
}
static inline void fb_cleanup_procfs(void)
{ }
#endif

/* fbsysfs.c */
#if defined(CONFIG_FB_DEVICE)
int fb_device_create(struct fb_info *fb_info);
void fb_device_destroy(struct fb_info *fb_info);
#else
static inline int fb_device_create(struct fb_info *fb_info)
{
	/*
	 * Acquire a reference on the parent device to avoid
	 * unplug operations behind our back. With the fbdev
	 * device enabled, this is performed within register_device().
	 */
	get_device(fb_info->device);

	return 0;
}
static inline void fb_device_destroy(struct fb_info *fb_info)
{
	/* Undo the get_device() from fb_device_create() */
	put_device(fb_info->device);
}
#endif

#endif
