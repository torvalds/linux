#ifndef __CAMSYS_MIPICSI_PHY_H__
#define __CAMSYS_MIPICSI_PHY_H__

#include "camsys_internal.h"

typedef struct camsys_mipiphy_clk_s {
	struct clk *hclk;

	unsigned int on;
} camsys_mipiphy_clk_t;

int camsys_mipiphy_probe_cb
(struct platform_device *pdev, camsys_dev_t *camsys_dev);
#endif
