/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver VCAP implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_VCAP_DEBUGFS_H__
#define __SPARX5_VCAP_DEBUGFS_H__

#include <linux/netdevice.h>

#include <vcap_api.h>
#include <vcap_api_client.h>

#if defined(CONFIG_DEBUG_FS)

/* Provide port information via a callback interface */
int sparx5_port_info(struct net_device *ndev,
		     struct vcap_admin *admin,
		     struct vcap_output_print *out);

#else

static inline int sparx5_port_info(struct net_device *ndev,
				   struct vcap_admin *admin,
				   struct vcap_output_print *out)
{
	return 0;
}

#endif

#endif /* __SPARX5_VCAP_DEBUGFS_H__ */
