// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver VCAP implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */

#include <linux/types.h>
#include <linux/list.h>

#include "vcap_api.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_ag_api.h"

/* Allocate a vcap control and vcap instances and configure the system */
int sparx5_vcap_init(struct sparx5 *sparx5)
{
	struct vcap_control *ctrl;

	/* Create a VCAP control instance that owns the platform specific VCAP
	 * model with VCAP instances and information about keysets, keys,
	 * actionsets and actions
	 */
	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	sparx5->vcap_ctrl = ctrl;
	/* select the sparx5 VCAP model */
	ctrl->vcaps = sparx5_vcaps;
	ctrl->stats = &sparx5_vcap_stats;

	return 0;
}

void sparx5_vcap_destroy(struct sparx5 *sparx5)
{
	if (!sparx5->vcap_ctrl)
		return;

	kfree(sparx5->vcap_ctrl);
}
