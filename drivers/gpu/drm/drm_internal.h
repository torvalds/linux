/*
 * Copyright © 2014 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
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

#ifndef __DRM_INTERNAL_H__
#define __DRM_INTERNAL_H__

#include <linux/kthread.h>
#include <linux/types.h>

#include <drm/drm_ioctl.h>
#include <drm/drm_vblank.h>

#define DRM_IF_MAJOR 1
#define DRM_IF_MINOR 4

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

struct dentry;
struct dma_buf;
struct iosys_map;
struct drm_connector;
struct drm_crtc;
struct drm_framebuffer;
struct drm_gem_object;
struct drm_master;
struct drm_minor;
struct drm_prime_file_private;
struct drm_printer;
struct drm_vblank_crtc;

/* drm_client_event.c */
#if defined(CONFIG_DRM_CLIENT)
void drm_client_debugfs_init(struct drm_device *dev);
#else
static inline void drm_client_debugfs_init(struct drm_device *dev)
{ }
#endif

/* drm_file.c */
extern struct mutex drm_global_mutex;
bool drm_dev_needs_global_mutex(struct drm_device *dev);
struct drm_file *drm_file_alloc(struct drm_minor *minor);
void drm_file_free(struct drm_file *file);

#ifdef CONFIG_PCI

/* drm_pci.c */
int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master);

#else

static inline int drm_pci_set_busid(struct drm_device *dev,
				    struct drm_master *master)
{
	return -EINVAL;
}

#endif

/* drm_prime.c */
int drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
int drm_prime_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);

void drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv);
void drm_prime_destroy_file_private(struct drm_prime_file_private *prime_fpriv);
void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv,
				 uint32_t handle);

/* drm_managed.c */
void drm_managed_release(struct drm_device *dev);
void drmm_add_final_kfree(struct drm_device *dev, void *container);

/* drm_vblank.c */
static inline bool drm_vblank_passed(u64 seq, u64 ref)
{
	return (seq - ref) <= (1 << 23);
}

void drm_vblank_disable_and_save(struct drm_device *dev, unsigned int pipe);
int drm_vblank_get(struct drm_device *dev, unsigned int pipe);
void drm_vblank_put(struct drm_device *dev, unsigned int pipe);
u64 drm_vblank_count(struct drm_device *dev, unsigned int pipe);

/* drm_vblank_work.c */
static inline void drm_vblank_flush_worker(struct drm_vblank_crtc *vblank)
{
	kthread_flush_worker(vblank->worker);
}

static inline void drm_vblank_destroy_worker(struct drm_vblank_crtc *vblank)
{
	if (vblank->worker)
		kthread_destroy_worker(vblank->worker);
}

int drm_vblank_worker_init(struct drm_vblank_crtc *vblank);
void drm_vblank_cancel_pending_works(struct drm_vblank_crtc *vblank);
void drm_handle_vblank_works(struct drm_vblank_crtc *vblank);

/* IOCTLS */
int drm_wait_vblank_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);

/* drm_irq.c */

/* IOCTLS */
int drm_crtc_get_sequence_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);

int drm_crtc_queue_sequence_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *filp);

/* drm_auth.c */
int drm_getmagic(struct drm_device *dev, void *data,
		 struct drm_file *file_priv);
int drm_authmagic(struct drm_device *dev, void *data,
		  struct drm_file *file_priv);
int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int drm_master_open(struct drm_file *file_priv);
void drm_master_release(struct drm_file *file_priv);
bool drm_master_internal_acquire(struct drm_device *dev);
void drm_master_internal_release(struct drm_device *dev);

/* drm_sysfs.c */
extern struct class *drm_class;

int drm_sysfs_init(void);
void drm_sysfs_destroy(void);
struct device *drm_sysfs_minor_alloc(struct drm_minor *minor);
int drm_sysfs_connector_add(struct drm_connector *connector);
int drm_sysfs_connector_add_late(struct drm_connector *connector);
void drm_sysfs_connector_remove_early(struct drm_connector *connector);
void drm_sysfs_connector_remove(struct drm_connector *connector);

void drm_sysfs_lease_event(struct drm_device *dev);

/* drm_gem.c */
int drm_gem_init(struct drm_device *dev);
int drm_gem_handle_create_tail(struct drm_file *file_priv,
			       struct drm_gem_object *obj,
			       u32 *handlep);
int drm_gem_close_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_gem_flink_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_gem_open_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
void drm_gem_open(struct drm_device *dev, struct drm_file *file_private);
void drm_gem_release(struct drm_device *dev, struct drm_file *file_private);
void drm_gem_print_info(struct drm_printer *p, unsigned int indent,
			const struct drm_gem_object *obj);

int drm_gem_pin_locked(struct drm_gem_object *obj);
void drm_gem_unpin_locked(struct drm_gem_object *obj);
int drm_gem_pin(struct drm_gem_object *obj);
void drm_gem_unpin(struct drm_gem_object *obj);
int drm_gem_vmap_locked(struct drm_gem_object *obj, struct iosys_map *map);
void drm_gem_vunmap_locked(struct drm_gem_object *obj, struct iosys_map *map);

/* drm_debugfs.c drm_debugfs_crc.c */
#if defined(CONFIG_DEBUG_FS)
void drm_debugfs_dev_fini(struct drm_device *dev);
void drm_debugfs_dev_register(struct drm_device *dev);
int drm_debugfs_register(struct drm_minor *minor, int minor_id,
			 struct dentry *root);
void drm_debugfs_unregister(struct drm_minor *minor);
void drm_debugfs_connector_add(struct drm_connector *connector);
void drm_debugfs_connector_remove(struct drm_connector *connector);
void drm_debugfs_crtc_add(struct drm_crtc *crtc);
void drm_debugfs_crtc_remove(struct drm_crtc *crtc);
void drm_debugfs_crtc_crc_add(struct drm_crtc *crtc);
void drm_debugfs_encoder_add(struct drm_encoder *encoder);
void drm_debugfs_encoder_remove(struct drm_encoder *encoder);
#else
static inline void drm_debugfs_dev_fini(struct drm_device *dev)
{
}

static inline void drm_debugfs_dev_register(struct drm_device *dev)
{
}

static inline int drm_debugfs_register(struct drm_minor *minor, int minor_id,
				       struct dentry *root)
{
	return 0;
}

static inline void drm_debugfs_unregister(struct drm_minor *minor)
{
}

static inline void drm_debugfs_connector_add(struct drm_connector *connector)
{
}
static inline void drm_debugfs_connector_remove(struct drm_connector *connector)
{
}

static inline void drm_debugfs_crtc_add(struct drm_crtc *crtc)
{
}
static inline void drm_debugfs_crtc_remove(struct drm_crtc *crtc)
{
}

static inline void drm_debugfs_crtc_crc_add(struct drm_crtc *crtc)
{
}

static inline void drm_debugfs_encoder_add(struct drm_encoder *encoder)
{
}

static inline void drm_debugfs_encoder_remove(struct drm_encoder *encoder)
{
}

#endif

drm_ioctl_t drm_version;
drm_ioctl_t drm_getunique;
drm_ioctl_t drm_getclient;

/* drm_syncobj.c */
void drm_syncobj_open(struct drm_file *file_private);
void drm_syncobj_release(struct drm_file *file_private);
int drm_syncobj_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_private);
int drm_syncobj_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_private);
int drm_syncobj_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_private);
int drm_syncobj_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_private);
int drm_syncobj_transfer_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_private);
int drm_syncobj_wait_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_private);
int drm_syncobj_timeline_wait_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_private);
int drm_syncobj_eventfd_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_private);
int drm_syncobj_reset_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_private);
int drm_syncobj_signal_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_private);
int drm_syncobj_timeline_signal_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_private);
int drm_syncobj_query_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_private);

/* drm_framebuffer.c */
void drm_framebuffer_print_info(struct drm_printer *p, unsigned int indent,
				const struct drm_framebuffer *fb);
void drm_framebuffer_debugfs_init(struct drm_device *dev);

#endif /* __DRM_INTERNAL_H__ */
