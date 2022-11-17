// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver VCAP debugFS implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/types.h>
#include <linux/list.h>

#include "sparx5_vcap_debugfs.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"
#include "sparx5_vcap_ag_api.h"

/* Provide port information via a callback interface */
int sparx5_port_info(struct net_device *ndev,
		     struct vcap_admin *admin,
		     struct vcap_output_print *out)
{
	/* this will be added later */
	return 0;
}
