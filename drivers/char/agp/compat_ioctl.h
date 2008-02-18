/*
 * Copyright (C) 1999 Jeff Hartmann
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _AGP_COMPAT_IOCTL_H
#define _AGP_COMPAT_IOCTL_H

#include <linux/compat.h>
#include <linux/agpgart.h>

#define AGPIOC_INFO32       _IOR (AGPIOC_BASE, 0, compat_uptr_t)
#define AGPIOC_ACQUIRE32    _IO  (AGPIOC_BASE, 1)
#define AGPIOC_RELEASE32    _IO  (AGPIOC_BASE, 2)
#define AGPIOC_SETUP32      _IOW (AGPIOC_BASE, 3, compat_uptr_t)
#define AGPIOC_RESERVE32    _IOW (AGPIOC_BASE, 4, compat_uptr_t)
#define AGPIOC_PROTECT32    _IOW (AGPIOC_BASE, 5, compat_uptr_t)
#define AGPIOC_ALLOCATE32   _IOWR(AGPIOC_BASE, 6, compat_uptr_t)
#define AGPIOC_DEALLOCATE32 _IOW (AGPIOC_BASE, 7, compat_int_t)
#define AGPIOC_BIND32       _IOW (AGPIOC_BASE, 8, compat_uptr_t)
#define AGPIOC_UNBIND32     _IOW (AGPIOC_BASE, 9, compat_uptr_t)
#define AGPIOC_CHIPSET_FLUSH32 _IO (AGPIOC_BASE, 10)

struct agp_info32 {
	struct agp_version version;	/* version of the driver        */
	u32 bridge_id;		/* bridge vendor/device         */
	u32 agp_mode;		/* mode info of bridge          */
	compat_long_t aper_base;	/* base of aperture             */
	compat_size_t aper_size;	/* size of aperture             */
	compat_size_t pg_total;	/* max pages (swap + system)    */
	compat_size_t pg_system;	/* max pages (system)           */
	compat_size_t pg_used;		/* current pages used           */
};

/*
 * The "prot" down below needs still a "sleep" flag somehow ...
 */
struct agp_segment32 {
	compat_off_t pg_start;		/* starting page to populate    */
	compat_size_t pg_count;	/* number of pages              */
	compat_int_t prot;		/* prot flags for mmap          */
};

struct agp_region32 {
	compat_pid_t pid;		/* pid of process               */
	compat_size_t seg_count;	/* number of segments           */
	struct agp_segment32 *seg_list;
};

struct agp_allocate32 {
	compat_int_t key;		/* tag of allocation            */
	compat_size_t pg_count;	/* number of pages              */
	u32 type;		/* 0 == normal, other devspec   */
	u32 physical;           /* device specific (some devices
				 * need a phys address of the
				 * actual page behind the gatt
				 * table)                        */
};

struct agp_bind32 {
	compat_int_t key;		/* tag of allocation            */
	compat_off_t pg_start;		/* starting page to populate    */
};

struct agp_unbind32 {
	compat_int_t key;		/* tag of allocation            */
	u32 priority;		/* priority for paging out      */
};

extern struct agp_front_data agp_fe;

int agpioc_acquire_wrap(struct agp_file_private *priv);
int agpioc_release_wrap(struct agp_file_private *priv);
int agpioc_protect_wrap(struct agp_file_private *priv);
int agpioc_setup_wrap(struct agp_file_private *priv, void __user *arg);
int agpioc_deallocate_wrap(struct agp_file_private *priv, int arg);
struct agp_file_private *agp_find_private(pid_t pid);
struct agp_client *agp_create_client(pid_t id);
int agp_remove_client(pid_t id);
int agp_create_segment(struct agp_client *client, struct agp_region *region);
void agp_free_memory_wrap(struct agp_memory *memory);
struct agp_memory *agp_allocate_memory_wrap(size_t pg_count, u32 type);
struct agp_memory *agp_find_mem_by_key(int key);
struct agp_client *agp_find_client_by_pid(pid_t id);
int agpioc_chipset_flush_wrap(struct agp_file_private *priv);

#endif /* _AGP_COMPAT_H */
