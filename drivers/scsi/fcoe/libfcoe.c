/*
 * Copyright(c) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#include <linux/module.h>
#include <linux/netdevice.h>

#include <scsi/libfc.h>

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FCoE");
MODULE_LICENSE("GPL v2");

/**
 * fcoe_wwn_from_mac() - Converts 48-bit IEEE MAC address to 64-bit FC WWN.
 * @mac: mac address
 * @scheme: check port
 * @port: port indicator for converting
 *
 * Returns: u64 fc world wide name
 */
u64 fcoe_wwn_from_mac(unsigned char mac[MAX_ADDR_LEN],
		      unsigned int scheme, unsigned int port)
{
	u64 wwn;
	u64 host_mac;

	/* The MAC is in NO, so flip only the low 48 bits */
	host_mac = ((u64) mac[0] << 40) |
		((u64) mac[1] << 32) |
		((u64) mac[2] << 24) |
		((u64) mac[3] << 16) |
		((u64) mac[4] << 8) |
		(u64) mac[5];

	WARN_ON(host_mac >= (1ULL << 48));
	wwn = host_mac | ((u64) scheme << 60);
	switch (scheme) {
	case 1:
		WARN_ON(port != 0);
		break;
	case 2:
		WARN_ON(port >= 0xfff);
		wwn |= (u64) port << 48;
		break;
	default:
		WARN_ON(1);
		break;
	}

	return wwn;
}
EXPORT_SYMBOL_GPL(fcoe_wwn_from_mac);

/**
 * fcoe_libfc_config() - sets up libfc related properties for lport
 * @lp: ptr to the fc_lport
 * @tt: libfc function template
 *
 * Returns : 0 for success
 */
int fcoe_libfc_config(struct fc_lport *lp, struct libfc_function_template *tt)
{
	/* Set the function pointers set by the LLDD */
	memcpy(&lp->tt, tt, sizeof(*tt));
	if (fc_fcp_init(lp))
		return -ENOMEM;
	fc_exch_init(lp);
	fc_elsct_init(lp);
	fc_lport_init(lp);
	fc_rport_init(lp);
	fc_disc_init(lp);

	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_libfc_config);
