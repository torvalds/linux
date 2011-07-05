extern void drm_gem_object_release_wrap(struct drm_gem_object *obj);
extern int drm_gem_private_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size);
extern int gem_create_mmap_offset(struct drm_gem_object *obj);
