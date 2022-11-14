#ifndef DRM_KUNIT_HELPERS_H_
#define DRM_KUNIT_HELPERS_H_

struct drm_device;
struct kunit;

struct drm_device *drm_kunit_device_init(struct kunit *test, u32 features, char *name);

#endif // DRM_KUNIT_HELPERS_H_
