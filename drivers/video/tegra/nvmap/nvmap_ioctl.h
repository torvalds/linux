/*
 * drivers/video/tegra/nvmap/nvmap_ioctl.h
 *
 * ioctl declarations for nvmap
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __VIDEO_TEGRA_NVMAP_IOCTL_H
#define __VIDEO_TEGRA_NVMAP_IOCTL_H

#include <linux/ioctl.h>
#include <linux/file.h>

#include <mach/nvmap.h>

enum {
	NVMAP_HANDLE_PARAM_SIZE = 1,
	NVMAP_HANDLE_PARAM_ALIGNMENT,
	NVMAP_HANDLE_PARAM_BASE,
	NVMAP_HANDLE_PARAM_HEAP,
};

enum {
	NVMAP_CACHE_OP_WB = 0,
	NVMAP_CACHE_OP_INV,
	NVMAP_CACHE_OP_WB_INV,
};


struct nvmap_create_handle {
	union {
		__u32 key;	/* ClaimPreservedHandle */
		__u32 id;	/* FromId */
		__u32 size;	/* CreateHandle */
	};
	__u32 handle;
};

struct nvmap_alloc_handle {
	__u32 handle;
	__u32 heap_mask;
	__u32 flags;
	__u32 align;
};

struct nvmap_map_caller {
	__u32 handle;		/* hmem */
	__u32 offset;		/* offset into hmem; should be page-aligned */
	__u32 length;		/* number of bytes to map */
	__u32 flags;
	unsigned long addr;	/* user pointer */
};

struct nvmap_rw_handle {
	unsigned long addr;	/* user pointer */
	__u32 handle;		/* hmem */
	__u32 offset;		/* offset into hmem */
	__u32 elem_size;	/* individual atom size */
	__u32 hmem_stride;	/* delta in bytes between atoms in hmem */
	__u32 user_stride;	/* delta in bytes between atoms in user */
	__u32 count;		/* number of atoms to copy */
};

struct nvmap_pin_handle {
	unsigned long handles;	/* array of handles to pin/unpin */
	unsigned long addr;	/* array of addresses to return */
	__u32 count;		/* number of entries in handles */
};

struct nvmap_handle_param {
	__u32 handle;
	__u32 param;
	unsigned long result;
};

struct nvmap_cache_op {
	unsigned long addr;
	__u32 handle;
	__u32 len;
	__s32 op;
};

#define NVMAP_IOC_MAGIC 'N'

/* Creates a new memory handle. On input, the argument is the size of the new
 * handle; on return, the argument is the name of the new handle
 */
#define NVMAP_IOC_CREATE  _IOWR(NVMAP_IOC_MAGIC, 0, struct nvmap_create_handle)
#define NVMAP_IOC_CLAIM   _IOWR(NVMAP_IOC_MAGIC, 1, struct nvmap_create_handle)
#define NVMAP_IOC_FROM_ID _IOWR(NVMAP_IOC_MAGIC, 2, struct nvmap_create_handle)

/* Actually allocates memory for the specified handle */
#define NVMAP_IOC_ALLOC    _IOW(NVMAP_IOC_MAGIC, 3, struct nvmap_alloc_handle)

/* Frees a memory handle, unpinning any pinned pages and unmapping any mappings
 */
#define NVMAP_IOC_FREE       _IO(NVMAP_IOC_MAGIC, 4)

/* Maps the region of the specified handle into a user-provided virtual address
 * that was previously created via an mmap syscall on this fd */
#define NVMAP_IOC_MMAP       _IOWR(NVMAP_IOC_MAGIC, 5, struct nvmap_map_caller)

/* Reads/writes data (possibly strided) from a user-provided buffer into the
 * hmem at the specified offset */
#define NVMAP_IOC_WRITE      _IOW(NVMAP_IOC_MAGIC, 6, struct nvmap_rw_handle)
#define NVMAP_IOC_READ       _IOW(NVMAP_IOC_MAGIC, 7, struct nvmap_rw_handle)

#define NVMAP_IOC_PARAM _IOWR(NVMAP_IOC_MAGIC, 8, struct nvmap_handle_param)

/* Pins a list of memory handles into IO-addressable memory (either IOVMM
 * space or physical memory, depending on the allocation), and returns the
 * address. Handles may be pinned recursively. */
#define NVMAP_IOC_PIN_MULT   _IOWR(NVMAP_IOC_MAGIC, 10, struct nvmap_pin_handle)
#define NVMAP_IOC_UNPIN_MULT _IOW(NVMAP_IOC_MAGIC, 11, struct nvmap_pin_handle)

#define NVMAP_IOC_CACHE      _IOW(NVMAP_IOC_MAGIC, 12, struct nvmap_cache_op)

/* Returns a global ID usable to allow a remote process to create a handle
 * reference to the same handle */
#define NVMAP_IOC_GET_ID  _IOWR(NVMAP_IOC_MAGIC, 13, struct nvmap_create_handle)

#define NVMAP_IOC_MAXNR (_IOC_NR(NVMAP_IOC_GET_ID))

int nvmap_ioctl_pinop(struct file *filp, bool is_pin, void __user *arg);

int nvmap_ioctl_get_param(struct file *filp, void __user* arg);

int nvmap_ioctl_getid(struct file *filp, void __user *arg);

int nvmap_ioctl_alloc(struct file *filp, void __user *arg);

int nvmap_ioctl_free(struct file *filp, unsigned long arg);

int nvmap_ioctl_create(struct file *filp, unsigned int cmd, void __user *arg);

int nvmap_map_into_caller_ptr(struct file *filp, void __user *arg);

int nvmap_ioctl_cache_maint(struct file *filp, void __user *arg);

int nvmap_ioctl_rw_handle(struct file *filp, int is_read, void __user* arg);



#endif
