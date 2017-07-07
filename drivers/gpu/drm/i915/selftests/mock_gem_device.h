#ifndef __MOCK_GEM_DEVICE_H__
#define __MOCK_GEM_DEVICE_H__

struct drm_i915_private;

struct drm_i915_private *mock_gem_device(void);
void mock_device_flush(struct drm_i915_private *i915);

#endif /* !__MOCK_GEM_DEVICE_H__ */
