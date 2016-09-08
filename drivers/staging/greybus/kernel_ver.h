/*
 * Greybus kernel "version" glue logic.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 *
 * Backports of newer kernel apis to allow the code to build properly on older
 * kernel versions.  Remove this file when merging to upstream, it should not be
 * needed at all
 */

#ifndef __GREYBUS_KERNEL_VER_H
#define __GREYBUS_KERNEL_VER_H

#include <linux/kernel.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/* Commit: 297d716 power_supply: Change ownership from driver to core */
#define CORE_OWNS_PSY_STRUCT
#endif

/*
 * The GPIO api sucks rocks in places, like removal, so work around their
 * explicit requirements of catching the return value for kernels older than
 * 3.17, which they explicitly changed in the 3.17 kernel.  Consistency is
 * overrated.
 */
#include <linux/gpio.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
#define MMC_HS400_SUPPORTED
#define MMC_DDR52_DEFINED
#endif

#ifndef MMC_CAP2_CORE_RUNTIME_PM
#define MMC_CAP2_CORE_RUNTIME_PM	0
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#define MMC_POWER_UNDEFINED_SUPPORTED
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/*
 * At this time the internal API for the set brightness was changed to the async
 * version, and one sync API was added to handle cases that need immediate
 * effect. Also, the led class flash and lock for sysfs access was introduced.
 */
#define LED_HAVE_SET_SYNC
#define LED_HAVE_FLASH
#define LED_HAVE_LOCK
#include <linux/led-class-flash.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
/*
 * New change in LED api, the set_sync operation was renamed to set_blocking and
 * the workqueue is now handle by core. So, only one set operation is need.
 */
#undef LED_HAVE_SET_SYNC
#define LED_HAVE_SET_BLOCKING
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
/*
 * New helper functions for registering/unregistering flash led devices as v4l2
 * subdevices were added.
 */
#define V4L2_HAVE_FLASH
#include <media/v4l2-flash-led-class.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/*
 * Power supply get by name need to drop reference after call
 */
#define PSY_HAVE_PUT
#endif

/*
 * General power supply properties that could be absent from various reasons,
 * like kernel versions or vendor specific versions
 */
#ifndef POWER_SUPPLY_PROP_VOLTAGE_BOOT
	#define POWER_SUPPLY_PROP_VOLTAGE_BOOT	-1
#endif
#ifndef POWER_SUPPLY_PROP_CURRENT_BOOT
	#define POWER_SUPPLY_PROP_CURRENT_BOOT	-1
#endif
#ifndef POWER_SUPPLY_PROP_CALIBRATE
	#define POWER_SUPPLY_PROP_CALIBRATE	-1
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
#define SPI_DEV_MODALIAS "spidev"
#define SPI_NOR_MODALIAS "spi-nor"
#else
#define SPI_DEV_MODALIAS "spidev"
#define SPI_NOR_MODALIAS "m25p80"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
/* Starting from this version, the spi core handles runtime pm automatically */
#define SPI_CORE_SUPPORT_PM
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/*
 * After commit b2b49ccbdd54 (PM: Kconfig: Set PM_RUNTIME if PM_SLEEP is
 * selected) PM_RUNTIME is always set if PM is set, so files that are build
 * conditionally if CONFIG_PM_RUNTIME is set may now be build if CONFIG_PM is
 * set.
 */

#ifdef CONFIG_PM
#define CONFIG_PM_RUNTIME
#endif /* CONFIG_PM */
#endif

#endif	/* __GREYBUS_KERNEL_VER_H */
