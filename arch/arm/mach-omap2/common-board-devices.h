/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OMAP_COMMON_BOARD_DEVICES__
#define __OMAP_COMMON_BOARD_DEVICES__

#include <linux/mfd/menelaus.h>

void *n8x0_legacy_init(void);

extern struct menelaus_platform_data n8x0_menelaus_platform_data;

#endif /* __OMAP_COMMON_BOARD_DEVICES__ */
