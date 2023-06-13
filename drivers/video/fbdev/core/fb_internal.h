/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FB_INTERNAL_H
#define _FB_INTERNAL_H

#include <linux/fb.h>
#include <linux/mutex.h>

/* fb_devfs.c */
int fb_register_chrdev(void);
void fb_unregister_chrdev(void);

/* fbmem.c */
extern struct mutex registration_lock;
extern struct fb_info *registered_fb[FB_MAX];
extern int num_registered_fb;
struct fb_info *get_fb_info(unsigned int idx);
void put_fb_info(struct fb_info *fb_info);

/* fb_procfs.c */
int fb_init_procfs(void);
void fb_cleanup_procfs(void);

/* fbsysfs.c */
int fb_device_create(struct fb_info *fb_info);
void fb_device_destroy(struct fb_info *fb_info);

#endif
