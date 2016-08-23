/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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

/*
 * This header file contains mode setting related functions and definitions
 * which are only used within the drm module as internal implementation details
 * and are not exported to drivers.
 */


/* drm_crtc.c */
int drm_mode_object_get_reg(struct drm_device *dev,
			    struct drm_mode_object *obj,
			    uint32_t obj_type,
			    bool register_obj,
			    void (*obj_free_cb)(struct kref *kref));
void drm_mode_object_register(struct drm_device *dev,
			      struct drm_mode_object *obj);
int drm_mode_object_get(struct drm_device *dev,
			struct drm_mode_object *obj, uint32_t obj_type);
struct drm_mode_object *__drm_mode_object_find(struct drm_device *dev,
					       uint32_t id, uint32_t type);
void drm_mode_object_unregister(struct drm_device *dev,
				struct drm_mode_object *object);
int drm_mode_object_get_properties(struct drm_mode_object *obj, bool atomic,
				   uint32_t __user *prop_ptr,
				   uint64_t __user *prop_values,
				   uint32_t *arg_count_props);
bool drm_property_change_valid_get(struct drm_property *property,
				   uint64_t value,
				   struct drm_mode_object **ref);
void drm_property_change_valid_put(struct drm_property *property,
				   struct drm_mode_object *ref);

int drm_plane_check_pixel_format(const struct drm_plane *plane,
				 u32 format);
int drm_crtc_check_viewport(const struct drm_crtc *crtc,
			    int x, int y,
			    const struct drm_display_mode *mode,
			    const struct drm_framebuffer *fb);

void drm_fb_release(struct drm_file *file_priv);
void drm_property_destroy_user_blobs(struct drm_device *dev,
				     struct drm_file *file_priv);

/* dumb buffer support IOCTLs */
int drm_mode_create_dumb_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
int drm_mode_mmap_dumb_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);
int drm_mode_destroy_dumb_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);

/* IOCTLs */
int drm_mode_obj_get_properties_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv);
int drm_mode_obj_set_property_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);

int drm_mode_getresources(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
int drm_mode_getplane_res(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv);
int drm_mode_setcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv);
int drm_mode_getplane(struct drm_device *dev,
		      void *data, struct drm_file *file_priv);
int drm_mode_setplane(struct drm_device *dev,
		      void *data, struct drm_file *file_priv);
int drm_mode_cursor_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);
int drm_mode_cursor2_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);
int drm_mode_getproperty_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
int drm_mode_getblob_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);
int drm_mode_createblob_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv);
int drm_mode_destroyblob_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv);
int drm_mode_getencoder(struct drm_device *dev,
			void *data, struct drm_file *file_priv);
int drm_mode_gamma_get_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);
int drm_mode_gamma_set_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);

int drm_mode_page_flip_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);

/* drm_connector.c */
void drm_connector_ida_init(void);
void drm_connector_ida_destroy(void);
void drm_connector_unregister_all(struct drm_device *dev);
int drm_connector_register_all(struct drm_device *dev);
int drm_mode_connector_set_obj_prop(struct drm_mode_object *obj,
				    struct drm_property *property,
				    uint64_t value);
int drm_connector_create_standard_properties(struct drm_device *dev);

/* IOCTL */
int drm_mode_connector_property_set_ioctl(struct drm_device *dev,
					  void *data, struct drm_file *file_priv);
int drm_mode_getconnector(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);

/* drm_framebuffer.c */
struct drm_framebuffer *
drm_internal_framebuffer_create(struct drm_device *dev,
				const struct drm_mode_fb_cmd2 *r,
				struct drm_file *file_priv);
void drm_framebuffer_free(struct kref *kref);

/* IOCTL */
int drm_mode_addfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv);
int drm_mode_addfb2(struct drm_device *dev,
		    void *data, struct drm_file *file_priv);
int drm_mode_rmfb(struct drm_device *dev,
		  void *data, struct drm_file *file_priv);
int drm_mode_getfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv);
int drm_mode_dirtyfb_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv);

/* drm_atomic.c */
int drm_atomic_get_property(struct drm_mode_object *obj,
			    struct drm_property *property, uint64_t *val);
int drm_mode_atomic_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv);

int drm_modeset_register_all(struct drm_device *dev);
void drm_modeset_unregister_all(struct drm_device *dev);

/* drm_blend.c */
int drm_atomic_normalize_zpos(struct drm_device *dev,
			      struct drm_atomic_state *state);
