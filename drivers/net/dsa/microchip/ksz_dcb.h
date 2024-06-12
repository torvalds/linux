/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Pengutronix, Oleksij Rempel <kernel@pengutronix.de> */

#ifndef __KSZ_DCB_H
#define __KSZ_DCB_H

#include <net/dsa.h>

#include "ksz_common.h"

int ksz_port_get_default_prio(struct dsa_switch *ds, int port);
int ksz_port_set_default_prio(struct dsa_switch *ds, int port, u8 prio);
int ksz_port_get_dscp_prio(struct dsa_switch *ds, int port, u8 dscp);
int ksz_port_add_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio);
int ksz_port_del_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio);
int ksz_port_set_apptrust(struct dsa_switch *ds, int port,
			  const unsigned char *sel,
			  int nsel);
int ksz_port_get_apptrust(struct dsa_switch *ds, int port, u8 *sel, int *nsel);
int ksz_dcb_init_port(struct ksz_device *dev, int port);
int ksz_dcb_init(struct ksz_device *dev);

#endif /* __KSZ_DCB_H */
