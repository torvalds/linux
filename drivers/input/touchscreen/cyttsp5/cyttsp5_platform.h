/*
 * cyttsp5_platform.h
 * Parade TrueTouch(TM) Standard Product V5 Platform Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Semiconductor
 * Copyright (C) 2013-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Semiconductor at www.parade.com <ttdrivers@paradetech.com>
 *
 */

#ifndef _LINUX_CYTTSP5_PLATFORM_H
#define _LINUX_CYTTSP5_PLATFORM_H

#include "cyttsp5_core.h"

#if defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5) \
	|| defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_MODULE)
extern struct cyttsp5_loader_platform_data _cyttsp5_loader_platform_data;

int cyttsp5_xres(struct cyttsp5_core_platform_data *pdata, struct device *dev);
int cyttsp5_init(struct cyttsp5_core_platform_data *pdata, int on,
		struct device *dev);
int cyttsp5_power(struct cyttsp5_core_platform_data *pdata, int on,
		struct device *dev, atomic_t *ignore_irq);
#ifdef CYTTSP5_DETECT_HW
int cyttsp5_detect(struct cyttsp5_core_platform_data *pdata,
		struct device *dev, cyttsp5_platform_read read);
#else
#define cyttsp5_detect		NULL
#endif
int cyttsp5_irq_stat(struct cyttsp5_core_platform_data *pdata,
		struct device *dev);
#else /* !CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5 */
static struct cyttsp5_loader_platform_data _cyttsp5_loader_platform_data;
#define cyttsp5_xres		NULL
#define cyttsp5_init		NULL
#define cyttsp5_power		NULL
#define cyttsp5_irq_stat	NULL
#define cyttsp5_detect		NULL
#endif /* CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5 */

#endif /* _LINUX_CYTTSP5_PLATFORM_H */
