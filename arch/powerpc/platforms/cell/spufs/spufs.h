/*
 * SPU file system
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef SPUFS_H
#define SPUFS_H

#include <linux/kref.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/fs.h>

#include <asm/spu.h>

/* The magic number for our file system */
enum {
	SPUFS_MAGIC = 0x23c9b64e,
};

struct spu_context {
	struct spu *spu;		  /* pointer to a physical SPU */
	struct rw_semaphore backing_sema; /* protects the above */
	spinlock_t mmio_lock;		  /* protects mmio access */

	struct kref kref;
};

struct spufs_inode_info {
	struct spu_context *i_ctx;
	struct inode vfs_inode;
};
#define SPUFS_I(inode) \
	container_of(inode, struct spufs_inode_info, vfs_inode)

extern struct tree_descr spufs_dir_contents[];

/* system call implementation */
long spufs_run_spu(struct file *file,
		   struct spu_context *ctx, u32 *npc, u32 *status);
long spufs_create_thread(struct nameidata *nd, const char *name,
			 unsigned int flags, mode_t mode);

/* context management */
struct spu_context * alloc_spu_context(void);
void destroy_spu_context(struct kref *kref);
struct spu_context * get_spu_context(struct spu_context *ctx);
int put_spu_context(struct spu_context *ctx);

void spu_acquire(struct spu_context *ctx);
void spu_release(struct spu_context *ctx);
void spu_acquire_runnable(struct spu_context *ctx);
void spu_acquire_saved(struct spu_context *ctx);

#endif
