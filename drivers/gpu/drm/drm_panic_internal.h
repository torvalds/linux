/* SPDX-License-Identifier: GPL-2.0 or MIT */

/*
 * Copyright (c) 2024 Intel
 * Copyright (c) 2024 Red Hat
 */

#ifndef __DRM_PANIC_INTERNAL_H__
#define __DRM_PANIC_INTERNAL_H__

#include <linux/spinlock.h>

struct drm_device;

#ifdef CONFIG_DRM_PANIC

/**
 * drm_panic_lock - protect panic printing relevant state
 * @dev: struct drm_device
 * @flags: unsigned long irq flags you need to pass to the unlock() counterpart
 *
 * This function must be called to protect software and hardware state that the
 * panic printing code must be able to rely on. The protected sections must be
 * as small as possible. It uses the irqsave/irqrestore variant, and can be
 * called from irq handler. Examples include:
 *
 * - Access to peek/poke or other similar registers, if that is the way the
 *   driver prints the pixels into the scanout buffer at panic time.
 *
 * - Updates to pointers like &drm_plane.state, allowing the panic handler to
 *   safely deference these. This is done in drm_atomic_helper_swap_state().
 *
 * - An state that isn't invariant and that the driver must be able to access
 *   during panic printing.
 */
#define drm_panic_lock(dev, flags) \
	raw_spin_lock_irqsave(&(dev)->mode_config.panic_lock, flags)

/**
 * drm_panic_unlock - end of the panic printing critical section
 * @dev: struct drm_device
 * @flags: irq flags that were returned when acquiring the lock
 *
 * Unlocks the raw spinlock acquired by either drm_panic_lock() or
 * drm_panic_trylock().
 */
#define drm_panic_unlock(dev, flags) \
	raw_spin_unlock_irqrestore(&(dev)->mode_config.panic_lock, flags)

#else
static inline void drm_panic_lock(struct drm_device *dev, unsigned long flags) {}
static inline void drm_panic_unlock(struct drm_device *dev, unsigned long flags) {}
#endif

#if IS_ENABLED(CONFIG_DRM_PANIC_HELPER)
/* drm_panic_helper.c */
int __init drm_panic_helper_init(void);
void __exit drm_panic_helper_exit(void);
#else
static inline int __init drm_panic_helper_init(void)
{
	return 0;
}

static inline void __exit drm_panic_helper_exit(void)
{ }
#endif

#endif /* __DRM_PANIC_INTERNAL_H__ */
