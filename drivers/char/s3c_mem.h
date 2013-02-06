/* drivers/char/s3c_mem.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file for s3c mem
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define DEBUG_S3C_MEM
#undef	DEBUG_S3C_MEM

#ifdef DEBUG_S3C_MEM
#define DEBUG(fmt, args...) 	printk(fmt, ##args)
#else
#define DEBUG(fmt, args...) 	do {} while (0)
#endif

#ifdef CONFIG_S3C_DMA_MEM
#include "s3c_dma_mem.h"
#endif

#define MEM_IOCTL_MAGIC			'M'

#define S3C_MEM_ALLOC			_IOWR(MEM_IOCTL_MAGIC, 310, struct s3c_mem_alloc)
#define S3C_MEM_FREE			_IOWR(MEM_IOCTL_MAGIC, 311, struct s3c_mem_alloc)

#define S3C_MEM_SHARE_ALLOC		_IOWR(MEM_IOCTL_MAGIC, 314, struct s3c_mem_alloc)
#define S3C_MEM_SHARE_FREE		_IOWR(MEM_IOCTL_MAGIC, 315, struct s3c_mem_alloc)

#define S3C_MEM_CACHEABLE_ALLOC		_IOWR(MEM_IOCTL_MAGIC, 316, struct s3c_mem_alloc)
#define S3C_MEM_CACHEABLE_SHARE_ALLOC	_IOWR(MEM_IOCTL_MAGIC, 317, struct s3c_mem_alloc)

#ifdef CONFIG_S3C_DMA_MEM
#define S3C_MEM_DMA_COPY		_IOWR(MEM_IOCTL_MAGIC, 318, struct s3c_mem_dma_param)
#endif

#define S3C_MEM_GET_PADDR		_IOWR(MEM_IOCTL_MAGIC, 320, struct s3c_mem_alloc)

#ifdef CONFIG_VIDEO_SAMSUNG_USE_DMA_MEM
#define S3C_MEM_CMA_ALLOC		\
			_IOWR(MEM_IOCTL_MAGIC, 321, struct s3c_mem_alloc)
#define S3C_MEM_CMA_FREE		\
			_IOWR(MEM_IOCTL_MAGIC, 322, struct s3c_mem_alloc)
#endif

#define MEM_ALLOC			1
#define MEM_ALLOC_SHARE			2
#define MEM_ALLOC_CACHEABLE		3
#define MEM_ALLOC_CACHEABLE_SHARE	4

#ifdef CONFIG_VIDEO_SAMSUNG_USE_DMA_MEM
#define MEM_CMA_ALLOC	5
#endif

#define S3C_MEM_MINOR  			13
#undef USE_DMA_ALLOC

static DEFINE_MUTEX(mem_alloc_lock);
static DEFINE_MUTEX(mem_free_lock);

static DEFINE_MUTEX(mem_share_alloc_lock);
static DEFINE_MUTEX(mem_share_free_lock);

static DEFINE_MUTEX(mem_cacheable_alloc_lock);
static DEFINE_MUTEX(mem_cacheable_share_alloc_lock);

#ifdef CONFIG_VIDEO_SAMSUNG_USE_DMA_MEM
static DEFINE_MUTEX(mem_open_lock);
static DEFINE_MUTEX(mem_release_lock);
#endif

struct s3c_mem_alloc {
	int size;
	unsigned int vir_addr;
	unsigned int phy_addr;

#ifdef USE_DMA_ALLOC
	unsigned int kvir_addr;
#endif
};

#ifdef CONFIG_S3C_DMA_MEM
#define s3c_dma_init() s3c_dma_mem_init()
#else
#define s3c_dma_init() do { } while (0)
#endif

#ifdef CONFIG_VIDEO_SAMSUNG_USE_DMA_MEM
#define S3C_MEM_CMA_MAX_CTX_BUF 3
extern int s3c_mem_open(struct inode *inode, struct file *filp);
extern int s3c_mem_release(struct inode *inode, struct file *filp);

struct s3c_dev_info {
	struct s3c_mem_alloc s_cur_mem_info[S3C_MEM_CMA_MAX_CTX_BUF];
	int s3c_mem_ctx_buf_num;
};

#endif
