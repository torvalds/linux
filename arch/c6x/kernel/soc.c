/*
 *  Miscellaneous SoC-specific hooks.
 *
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/etherdevice.h>
#include <asm/setup.h>
#include <asm/soc.h>

struct soc_ops soc_ops;

int soc_get_exception(void)
{
	if (!soc_ops.get_exception)
		return -1;
	return soc_ops.get_exception();
}

void soc_assert_event(unsigned int evt)
{
	if (soc_ops.assert_event)
		soc_ops.assert_event(evt);
}

static u8 cmdline_mac[6];

static int __init get_mac_addr_from_cmdline(char *str)
{
	int count, i, val;

	for (count = 0; count < 6 && *str; count++, str += 3) {
		if (!isxdigit(str[0]) || !isxdigit(str[1]))
			return 0;
		if (str[2] != ((count < 5) ? ':' : '\0'))
			return 0;

		for (i = 0, val = 0; i < 2; i++) {
			val = val << 4;
			val |= isdigit(str[i]) ?
				str[i] - '0' : toupper(str[i]) - 'A' + 10;
		}
		cmdline_mac[count] = val;
	}
	return 1;
}
__setup("emac_addr=", get_mac_addr_from_cmdline);

/*
 * Setup the MAC address for SoC ethernet devices.
 *
 * Before calling this function, the ethernet driver will have
 * initialized the addr with local-mac-address from the device
 * tree (if found). Allow command line to override, but not
 * the fused address.
 */
int soc_mac_addr(unsigned int index, u8 *addr)
{
	int i, have_dt_mac = 0, have_cmdline_mac = 0, have_fuse_mac = 0;

	for (i = 0; i < 6; i++) {
		if (cmdline_mac[i])
			have_cmdline_mac = 1;
		if (c6x_fuse_mac[i])
			have_fuse_mac = 1;
		if (addr[i])
			have_dt_mac = 1;
	}

	/* cmdline overrides all */
	if (have_cmdline_mac)
		memcpy(addr, cmdline_mac, 6);
	else if (!have_dt_mac) {
		if (have_fuse_mac)
			memcpy(addr, c6x_fuse_mac, 6);
		else
			random_ether_addr(addr);
	}

	/* adjust for specific EMAC device */
	addr[5] += index * c6x_num_cores;
	return 1;
}
EXPORT_SYMBOL_GPL(soc_mac_addr);
