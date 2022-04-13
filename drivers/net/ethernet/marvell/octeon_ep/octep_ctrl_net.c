// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#include <linux/string.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_ctrl_net.h"

int octep_get_link_status(struct octep_device *oct)
{
	return 0;
}

void octep_set_link_status(struct octep_device *oct, bool up)
{
}

void octep_set_rx_state(struct octep_device *oct, bool up)
{
}

int octep_get_mac_addr(struct octep_device *oct, u8 *addr)
{
	return -1;
}

int octep_get_link_info(struct octep_device *oct)
{
	return -1;
}

int octep_set_link_info(struct octep_device *oct, struct octep_iface_link_info *link_info)
{
	return -1;
}
