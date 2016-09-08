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
