#ifndef __DRM_LEGACY_H__
#define __DRM_LEGACY_H__

/*
 * Copyright (c) 2014 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * This file contains legacy interfaces that modern drm drivers
 * should no longer be using. They cannot be removed as legacy
 * drivers use them, and removing them are API breaks.
 */
#include <linux/list.h>

#include <drm/drm.h>
#include <drm/drm_device.h>
#include <drm/drm_legacy.h>

struct agp_memory;
struct drm_buf_desc;
struct drm_device;
struct drm_file;
struct drm_hash_item;
struct drm_open_hash;

/*
 * Hash-table Support
 */

#define drm_hash_entry(_ptr, _type, _member) container_of(_ptr, _type, _member)

/* drm_hashtab.c */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
int drm_ht_create(struct drm_open_hash *ht, unsigned int order);
int drm_ht_insert_item(struct drm_open_hash *ht, struct drm_hash_item *item);
int drm_ht_just_insert_please(struct drm_open_hash *ht, struct drm_hash_item *item,
			      unsigned long seed, int bits, int shift,
			      unsigned long add);
int drm_ht_find_item(struct drm_open_hash *ht, unsigned long key, struct drm_hash_item **item);

void drm_ht_verbose_list(struct drm_open_hash *ht, unsigned long key);
int drm_ht_remove_key(struct drm_open_hash *ht, unsigned long key);
int drm_ht_remove_item(struct drm_open_hash *ht, struct drm_hash_item *item);
void drm_ht_remove(struct drm_open_hash *ht);
#endif

/*
 * RCU-safe interface
 *
 * The user of this API needs to make sure that two or more instances of the
 * hash table manipulation functions are never run simultaneously.
 * The lookup function drm_ht_find_item_rcu may, however, run simultaneously
 * with any of the manipulation functions as long as it's called from within
 * an RCU read-locked section.
 */
#define drm_ht_insert_item_rcu drm_ht_insert_item
#define drm_ht_just_insert_please_rcu drm_ht_just_insert_please
#define drm_ht_remove_key_rcu drm_ht_remove_key
#define drm_ht_remove_item_rcu drm_ht_remove_item
#define drm_ht_find_item_rcu drm_ht_find_item

/*
 * Generic DRM Contexts
 */

#define DRM_KERNEL_CONTEXT		0
#define DRM_RESERVED_CONTEXTS		1

#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_ctxbitmap_init(struct drm_device *dev);
void drm_legacy_ctxbitmap_cleanup(struct drm_device *dev);
void drm_legacy_ctxbitmap_flush(struct drm_device *dev, struct drm_file *file);
#else
static inline void drm_legacy_ctxbitmap_init(struct drm_device *dev) {}
static inline void drm_legacy_ctxbitmap_cleanup(struct drm_device *dev) {}
static inline void drm_legacy_ctxbitmap_flush(struct drm_device *dev, struct drm_file *file) {}
#endif

void drm_legacy_ctxbitmap_free(struct drm_device *dev, int ctx_handle);

#if IS_ENABLED(CONFIG_DRM_LEGACY)
int drm_legacy_resctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_addctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_getctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_switchctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_newctx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_rmctx(struct drm_device *d, void *v, struct drm_file *f);

int drm_legacy_setsareactx(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_getsareactx(struct drm_device *d, void *v, struct drm_file *f);
#endif

/*
 * Generic Buffer Management
 */

#define DRM_MAP_HASH_OFFSET 0x10000000

#if IS_ENABLED(CONFIG_DRM_LEGACY)
static inline int drm_legacy_create_map_hash(struct drm_device *dev)
{
	return drm_ht_create(&dev->map_hash, 12);
}

static inline void drm_legacy_remove_map_hash(struct drm_device *dev)
{
	drm_ht_remove(&dev->map_hash);
}
#else
static inline int drm_legacy_create_map_hash(struct drm_device *dev)
{
	return 0;
}

static inline void drm_legacy_remove_map_hash(struct drm_device *dev) {}
#endif


#if IS_ENABLED(CONFIG_DRM_LEGACY)
int drm_legacy_getmap_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int drm_legacy_addmap_ioctl(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_rmmap_ioctl(struct drm_device *d, void *v, struct drm_file *f);

int drm_legacy_addbufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_infobufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_markbufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_freebufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_mapbufs(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_dma_ioctl(struct drm_device *d, void *v, struct drm_file *f);
#endif

int __drm_legacy_infobufs(struct drm_device *, void *, int *,
			  int (*)(void *, int, struct drm_buf_entry *));
int __drm_legacy_mapbufs(struct drm_device *, void *, int *,
			  void __user **,
			  int (*)(void *, int, unsigned long, struct drm_buf *),
			  struct drm_file *);

#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_master_rmmaps(struct drm_device *dev,
			      struct drm_master *master);
void drm_legacy_rmmaps(struct drm_device *dev);
#else
static inline void drm_legacy_master_rmmaps(struct drm_device *dev,
					    struct drm_master *master) {}
static inline void drm_legacy_rmmaps(struct drm_device *dev) {}
#endif

#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_vma_flush(struct drm_device *d);
#else
static inline void drm_legacy_vma_flush(struct drm_device *d)
{
	/* do nothing */
}
#endif

/*
 * AGP Support
 */

struct drm_agp_mem {
	unsigned long handle;
	struct agp_memory *memory;
	unsigned long bound;
	int pages;
	struct list_head head;
};

/* drm_agpsupport.c */
#if IS_ENABLED(CONFIG_DRM_LEGACY) && IS_ENABLED(CONFIG_AGP)
void drm_legacy_agp_clear(struct drm_device *dev);

int drm_legacy_agp_acquire_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
int drm_legacy_agp_release_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
int drm_legacy_agp_enable_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int drm_legacy_agp_info_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int drm_legacy_agp_alloc_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
int drm_legacy_agp_free_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int drm_legacy_agp_unbind_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int drm_legacy_agp_bind_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
#else
static inline void drm_legacy_agp_clear(struct drm_device *dev) {}
#endif

/* drm_lock.c */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
int drm_legacy_lock(struct drm_device *d, void *v, struct drm_file *f);
int drm_legacy_unlock(struct drm_device *d, void *v, struct drm_file *f);
void drm_legacy_lock_release(struct drm_device *dev, struct file *filp);
#else
static inline void drm_legacy_lock_release(struct drm_device *dev, struct file *filp) {}
#endif

/* DMA support */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
int drm_legacy_dma_setup(struct drm_device *dev);
void drm_legacy_dma_takedown(struct drm_device *dev);
#else
static inline int drm_legacy_dma_setup(struct drm_device *dev)
{
	return 0;
}
#endif

void drm_legacy_free_buffer(struct drm_device *dev,
			    struct drm_buf * buf);
#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_reclaim_buffers(struct drm_device *dev,
				struct drm_file *filp);
#else
static inline void drm_legacy_reclaim_buffers(struct drm_device *dev,
					      struct drm_file *filp) {}
#endif

/* Scatter Gather Support */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_sg_cleanup(struct drm_device *dev);
int drm_legacy_sg_alloc(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_legacy_sg_free(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
#endif

#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_init_members(struct drm_device *dev);
void drm_legacy_destroy_members(struct drm_device *dev);
void drm_legacy_dev_reinit(struct drm_device *dev);
int drm_legacy_setup(struct drm_device * dev);
#else
static inline void drm_legacy_init_members(struct drm_device *dev) {}
static inline void drm_legacy_destroy_members(struct drm_device *dev) {}
static inline void drm_legacy_dev_reinit(struct drm_device *dev) {}
static inline int drm_legacy_setup(struct drm_device * dev) { return 0; }
#endif

#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_legacy_lock_master_cleanup(struct drm_device *dev, struct drm_master *master);
#else
static inline void drm_legacy_lock_master_cleanup(struct drm_device *dev, struct drm_master *master) {}
#endif

#if IS_ENABLED(CONFIG_DRM_LEGACY)
void drm_master_legacy_init(struct drm_master *master);
#else
static inline void drm_master_legacy_init(struct drm_master *master) {}
#endif

/* drm_pci.c */
#if IS_ENABLED(CONFIG_DRM_LEGACY) && IS_ENABLED(CONFIG_PCI)
int drm_legacy_irq_by_busid(struct drm_device *dev, void *data, struct drm_file *file_priv);
void drm_legacy_pci_agp_destroy(struct drm_device *dev);
#else
static inline int drm_legacy_irq_by_busid(struct drm_device *dev, void *data,
					  struct drm_file *file_priv)
{
	return -EINVAL;
}

static inline void drm_legacy_pci_agp_destroy(struct drm_device *dev) {}
#endif

#endif /* __DRM_LEGACY_H__ */
