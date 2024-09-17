// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freescale SOC support functions
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include "ops.h"
#include "types.h"
#include "fsl-soc.h"
#include "stdio.h"

static u32 prop_buf[MAX_PROP_LEN / 4];

u32 *fsl_get_immr(void)
{
	void *soc;
	unsigned long ret = 0;

	soc = find_node_by_devtype(NULL, "soc");
	if (soc) {
		int size;
		u32 naddr;

		size = getprop(soc, "#address-cells", prop_buf, MAX_PROP_LEN);
		if (size == 4)
			naddr = prop_buf[0];
		else
			naddr = 2;

		if (naddr != 1 && naddr != 2)
			goto err;

		size = getprop(soc, "ranges", prop_buf, MAX_PROP_LEN);

		if (size < 12)
			goto err;
		if (prop_buf[0] != 0)
			goto err;
		if (naddr == 2 && prop_buf[1] != 0)
			goto err;

		if (!dt_xlate_addr(soc, prop_buf + naddr, 8, &ret))
			ret = 0;
	}

err:
	if (!ret)
		printf("fsl_get_immr: Failed to find immr base\r\n");

	return (u32 *)ret;
}
