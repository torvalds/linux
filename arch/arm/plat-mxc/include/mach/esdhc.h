/*
 * Copyright 2010 Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef __ASM_ARCH_IMX_ESDHC_H
#define __ASM_ARCH_IMX_ESDHC_H

/**
 * struct esdhc_platform_data - optional platform data for esdhc on i.MX
 *
 * strongly recommended for i.MX25/35, not needed for other variants
 *
 * @wp_gpio:	gpio for write_protect (-EINVAL if unused)
 * @cd_gpio:	gpio for card_detect interrupt (-EINVAL if unused)
 */

struct esdhc_platform_data {
	unsigned int wp_gpio;
	unsigned int cd_gpio;
};
#endif /* __ASM_ARCH_IMX_ESDHC_H */
