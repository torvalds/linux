/* Staging board support for KZM9D. Enable not-yet-DT-capable devices here. */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include "board.h"

static const struct resource usbs1_res[] __initconst = {
	DEFINE_RES_MEM(0xe2800000, 0x2000),
	DEFINE_RES_IRQ(159),
};

static void __init kzm9d_init(void)
{
	if (!board_staging_dt_node_available(usbs1_res, ARRAY_SIZE(usbs1_res)))
		platform_device_register_simple("emxx_udc", -1, usbs1_res,
						ARRAY_SIZE(usbs1_res));
}

board_staging("renesas,kzm9d", kzm9d_init);
