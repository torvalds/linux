/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright © 2014 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 * This header file contains mode setting related functions and definitions
 * which are only used within the drm module as internal implementation details
 * and are not exported to drivers.
 */

#include <linux/types.h>

enum drm_color_encoding;
enum drm_color_range;
enum drm_connector_force;
enum drm_mode_status;

struct drm_atomic_state;
struct drm_bridge;
struct drm_connector;
struct drm_crtc;
struct drm_device;
struct drm_display_mode;
struct drm_file;
struct drm_framebuffer;
struct drm_mode_create_dumb;
struct drm_mode_fb_cmd2;
struct drm_mode_fb_cmd;
struct drm_mode_object;
struct drm_mode_set;
struct drm_plane;
struct drm_plane_state;
struct drm_property;
struct edid;
struct kref;
struct work_struct;
struct fwnode_handle;

/* drm_crtc.c */
int drm_mode_crtc_set_obj_prop(struct drm_mode_object *obj,
			       struct drm_property *property,
			       uint64_t value);
int drm_crtc_check_viewport(const struct drm_crtc *crtc,
			    int x, int y,
			    const struct drm_display_mode *mode,
			    const struct drm_framebuffer *fb);
int drm_crtc_register_all(struct drm_device *dev);
void drm_crtc_unregister_all(struct drm_device *dev);
int drm_crtc_force_disable(struct drm_crtc *crtc);

struct dma_fence *drm_crtc_create_fence(struct drm_crtc *crtc);

struct drm_property *
drm_create_scaling_filter_prop(struct drm_device *dev,
			       unsigned int supported_filters);
/* IOCTLs */
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv);
int drm_mode_setcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv);


/* drm_mode_config.c */
int drm_modeset_register_all(struct drm_device *dev);
void drm_modeset_unregister_all(struct drm_device *dev);
void drm_mode_config_validate(struct drm_device *dev);

/* drm_modes.c */
const char *drm_get_mode_status_name(enum drm_mode_status status);

/* IOCTLs */
int drm_mode_getresources(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);


/* drm_dumb_buffers.c */
int drm_mode_create_dumb(struct drm_device *dev,
			 struct drm_mode_create_dumb *args,
			 struct drm_file *file_priv);
int drm_mode_destroy_dumb(struct drm_device *dev, u32 handle,
			  struct drm_file *file_priv);

/* IOCTLs */
int drm_mode_create_dumb_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
int drm_mode_mmap_dumb_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);
int drm_mode_destroy_dumb_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);

/* drm_color_mgmt.c */
const char *drm_get_color_encoding_name(enum drm_color_encoding encoding);
const char *drm_get_color_range_name(enum drm_color_range range);

/* IOCTLs */
int drm_mode_gamma_get_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);
int drm_mode_gamma_set_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);

/* drm_property.c */
void drm_property_destroy_user_blobs(struct drm_device *dev,
				     struct drm_file *file_priv);
bool drm_property_change_valid_get(struct drm_property *property,
				   uint64_t value,
				   struct drm_mode_object **ref);
void drm_property_change_valid_put(struct drm_property *property,
				   struct drm_mode_object *ref);

/* IOCTL */
int drm_mode_getproperty_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
int drm_mode_getblob_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);
int drm_mode_createblob_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv);
int drm_mode_destroyblob_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);

/* drm_mode_object.c */
int __drm_mode_object_add(struct drm_device *dev, struct drm_mode_object *obj,
			  uint32_t obj_type, bool register_obj,
			  void (*obj_free_cb)(struct kref *kref));
int drm_mode_object_add(struct drm_device *dev, struct drm_mode_object *obj,
			uint32_t obj_type);
void drm_mode_object_register(struct drm_device *dev,
			      struct drm_mode_object *obj);
struct drm_mode_object *__drm_mode_object_find(struct drm_device *dev,
					       struct drm_file *file_priv,
					       uint32_t id, uint32_t type);
void drm_mode_object_unregister(struct drm_device *dev,
				struct drm_mode_object *object);
int drm_mode_object_get_properties(struct drm_mode_object *obj, bool atomic,
				   uint32_t __user *prop_ptr,
				   uint64_t __user *prop_values,
				   uint32_t *arg_count_props);
struct drm_property *drm_mode_obj_find_prop_id(struct drm_mode_object *obj,
					       uint32_t prop_id);

/* IOCTL */

int drm_mode_obj_get_properties_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);
int drm_mode_obj_set_property_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);

/* drm_encoder.c */
int drm_encoder_register_all(struct drm_device *dev);
void drm_encoder_unregister_all(struct drm_device *dev);

/* IOCTL */
int drm_mode_getencoder(struct drm_device *dev,
			void *data, struct drm_file *file_priv);

/* drm_connector.c */
void drm_connector_ida_init(void);
void drm_connector_ida_destroy(void);
void drm_connector_unregister_all(struct drm_device *dev);
int drm_connector_register_all(struct drm_device *dev);
int drm_connector_set_obj_prop(struct drm_mode_object *obj,
				    struct drm_property *property,
				    uint64_t value);
int drm_connector_create_standard_properties(struct drm_device *dev);
const char *drm_get_connector_force_name(enum drm_connector_force force);
void drm_connector_free_work_fn(struct work_struct *work);
struct drm_connector *drm_connector_find_by_fwnode(struct fwnode_handle *fwnode);

/* IOCTL */
int drm_connector_property_set_ioctl(struct drm_device *dev,
				     void *data, struct drm_file *file_priv);
int drm_mode_getconnector(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);

/* drm_framebuffer.c */
struct drm_framebuffer *
drm_internal_framebuffer_create(struct drm_device *dev,
				const struct drm_mode_fb_cmd2 *r,
				struct drm_file *file_priv);
void drm_framebuffer_free(struct kref *kref);
int drm_framebuffer_check_src_coords(uint32_t src_x, uint32_t src_y,
				     uint32_t src_w, uint32_t src_h,
				     const struct drm_framebuffer *fb);
void drm_fb_release(struct drm_file *file_priv);

int drm_mode_addfb(struct drm_device *dev, struct drm_mode_fb_cmd *or,
		   struct drm_file *file_priv);
int drm_mode_addfb2(struct drm_device *dev,
		    void *data, struct drm_file *file_priv);
int drm_mode_rmfb(struct drm_device *dev, u32 fb_id,
		  struct drm_file *file_priv);


/* IOCTL */
int drm_mode_addfb_ioctl(struct drm_device *dev,
			 void *data, struct drm_file *file_priv);
int drm_mode_addfb2_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
int drm_mode_rmfb_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int drm_mode_getfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv);
int drm_mode_getfb2_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
int drm_mode_dirtyfb_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);

/* drm_atomic.c */
#ifdef CONFIG_DEBUG_FS
struct drm_minor;
void drm_atomic_debugfs_init(struct drm_minor *minor);
#endif

int __drm_atomic_helper_disable_plane(struct drm_plane *plane,
				      struct drm_plane_state *plane_state);
int __drm_atomic_helper_set_config(struct drm_mode_set *set,
				   struct drm_atomic_state *state);

void drm_atomic_print_new_state(const struct drm_atomic_state *state,
		struct drm_printer *p);

/* drm_atomic_uapi.c */
int drm_atomic_connector_commit_dpms(struct drm_atomic_state *state,
				     struct drm_connector *connector,
				     int mode);
int drm_atomic_set_property(struct drm_atomic_state *state,
			    struct drm_file *file_priv,
			    struct drm_mode_object *obj,
			    struct drm_property *prop,
			    uint64_t prop_value);
int drm_atomic_get_property(struct drm_mode_object *obj,
			    struct drm_property *property, uint64_t *val);

/* IOCTL */
int drm_mode_atomic_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);


/* drm_plane.c */
int drm_plane_register_all(struct drm_device *dev);
void drm_plane_unregister_all(struct drm_device *dev);
int drm_plane_check_pixel_format(struct drm_plane *plane,
				 u32 format, u64 modifier);
struct drm_mode_rect *
__drm_plane_get_damage_clips(const struct drm_plane_state *state);

/* drm_bridge.c */
void drm_bridge_detach(struct drm_bridge *bridge);

/* IOCTL */
int drm_mode_getplane_res(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int drm_mode_getplane(struct drm_device *dev,
		      void *data, struct drm_file *file_priv);
int drm_mode_setplane(struct drm_device *dev,
		      void *data, struct drm_file *file_priv);
int drm_mode_cursor_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
int drm_mode_cursor2_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);
int drm_mode_page_flip_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);

/* drm_edid.c */
void drm_mode_fixup_1366x768(struct drm_display_mode *mode);
int drm_edid_override_set(struct drm_connector *connector, const void *edid, size_t size);
int drm_edid_override_reset(struct drm_connector *connector);
