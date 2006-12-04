/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */

#ifndef __NETXEN_NIC_IOCTL_H__
#define __NETXEN_NIC_IOCTL_H__

#include <linux/sockios.h>

#define NETXEN_CMD_START	SIOCDEVPRIVATE
#define NETXEN_NIC_CMD		(NETXEN_CMD_START + 1)
#define NETXEN_NIC_NAME		(NETXEN_CMD_START + 2)
#define NETXEN_NIC_NAME_LEN	16
#define NETXEN_NIC_NAME_RSP	"NETXEN"

typedef enum {
	netxen_nic_cmd_none = 0,
	netxen_nic_cmd_pci_read,
	netxen_nic_cmd_pci_write,
	netxen_nic_cmd_pci_mem_read,
	netxen_nic_cmd_pci_mem_write,
	netxen_nic_cmd_pci_config_read,
	netxen_nic_cmd_pci_config_write,
	netxen_nic_cmd_get_stats,
	netxen_nic_cmd_clear_stats,
	netxen_nic_cmd_get_version
} netxen_nic_ioctl_cmd_t;

struct netxen_nic_ioctl_data {
	u32 cmd;
	u32 unused1;
	u64 off;
	u32 size;
	u32 rv;
	char u[64];
	void *ptr;
};

struct netxen_statistics {
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 rx_errors;
	u64 tx_bytes;
	u64 tx_errors;
	u64 rx_crc_errors;
	u64 rx_short_length_error;
	u64 rx_long_length_error;
	u64 rx_mac_errors;
};

#endif				/* __NETXEN_NIC_IOCTL_H_ */
