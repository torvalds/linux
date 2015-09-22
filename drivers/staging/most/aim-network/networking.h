/*
 * Networking AIM - Networking Application Interface Module for MostCore
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */
#ifndef _NETWORKING_H_
#define _NETWORKING_H_

#include "mostcore.h"


void most_deliver_netinfo(struct most_interface *iface,
			  unsigned char link_stat, unsigned char *mac_addr);


#endif
