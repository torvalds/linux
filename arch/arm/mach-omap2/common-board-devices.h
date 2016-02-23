#ifndef __OMAP_COMMON_BOARD_DEVICES__
#define __OMAP_COMMON_BOARD_DEVICES__

#include <sound/tlv320aic3x.h>
#include <linux/mfd/menelaus.h>
#include "twl-common.h"

#define NAND_BLOCK_SIZE	SZ_128K

struct mtd_partition;
struct ads7846_platform_data;

void omap_ads7846_init(int bus_num, int gpio_pendown, int gpio_debounce,
		       struct ads7846_platform_data *board_pdata);
void *n8x0_legacy_init(void);

extern struct menelaus_platform_data n8x0_menelaus_platform_data;
extern struct aic3x_pdata n810_aic33_data;

#endif /* __OMAP_COMMON_BOARD_DEVICES__ */
