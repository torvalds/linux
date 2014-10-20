/*
 * Greybus kernel "version" glue logic.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 *
 * Backports of newer kernel apis to allow the code to build properly on older
 * kernel versions.  Remove this file when merging to upstream, it should not be
 * needed at all
 */

#ifndef __GREYBUS_KERNEL_VER_H
#define __GREYBUS_KERNEL_VER_H

#ifndef DEVICE_ATTR_RO
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#endif

#ifndef U8_MAX
#define U8_MAX	((u8)~0U)
#endif /* ! U8_MAX */

#ifndef U16_MAX
#define U16_MAX	((u16)(~0U))
#endif /* !U16_MAX */

/*
 * The GPIO api sucks rocks in places, like removal, so work around their
 * explicit requirements of catching the return value for kernels older than
 * 3.17, which they explicitly changed in the 3.17 kernel.  Consistency is
 * overrated.
 */
#include <linux/version.h>
#include <linux/gpio.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)
static inline void gb_gpiochip_remove(struct gpio_chip *chip)
{
	gpiochip_remove(chip);
}
#else
static inline void gb_gpiochip_remove(struct gpio_chip *chip)
{
	int ret;
	ret = gpiochip_remove(chip);
}
#endif

#endif	/* __GREYBUS_KERNEL_VER_H */
