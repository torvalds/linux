/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _CORESIGHT_MALI_SOURCES_H
#define _CORESIGHT_MALI_SOURCES_H

#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/version.h>

#include "coresight_mali_common.h"

/**
 * struct coresight_mali_source_drvdata - Coresight mali source driver data
 *
 * @base:       Common driver data structure between coresight mali sources and sinks
 * @trcid:      Trace id
 * @type_name:  Type name of the driver, for example "itm" or "etm"
 */
struct coresight_mali_source_drvdata {
	struct coresight_mali_drvdata base;
	u32 trcid;
#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	char *type_name;
#endif
};

/**
 * coresight_mali_sources_probe - Generic probe for a coresight mali source
 *
 * @pdev: Pointer to a platform device
 *
 * Return: 0 if success. Error code on failure.
 */
int coresight_mali_sources_probe(struct platform_device *pdev);

/**
 * coresight_mali_sources_remove - Generic remove for a coresight mali source
 *
 * @pdev: Pointer to a platform device
 *
 * Return: 0 if success. Error code on failure.
 */
int coresight_mali_sources_remove(struct platform_device *pdev);

/**
 * coresight_mali_sources_init_drvdata - Driver data initialization hook.
 *
 * @drvdata: Driver data structure to initialize
 *
 * Used for initializing source specific enable and disable sequences and other relevant data.
 *
 * Return: 0 if success. Error code on failure.
 */
int coresight_mali_sources_init_drvdata(struct coresight_mali_source_drvdata *drvdata);

/**
 * coresight_mali_sources_deinit_drvdata - Driver data deinitialization hook.
 *
 * @drvdata: Driver data structure to deinitialize
 *
 * Used for releasing source specific enable and disable sequences and other relevant data.
 */
void coresight_mali_sources_deinit_drvdata(struct coresight_mali_source_drvdata *drvdata);

/**
 * coresight_mali_source_groups_get - Getter for source groups.
 *
 * Return: a pointer to an array of attribute groups of the driver. Can also be NULL.
 *
 * Groups are drivers sysfs subnodes that can be used to read state of the coresight component
 * or write component configuration.
 */
const struct attribute_group **coresight_mali_source_groups_get(void);

#endif /* _CORESIGHT_MALI_SOURCES_H */
