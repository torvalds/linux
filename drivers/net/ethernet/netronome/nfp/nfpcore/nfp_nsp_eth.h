/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef NSP_NSP_ETH_H
#define NSP_NSP_ETH_H 1

#include <linux/types.h>
#include <linux/if_ether.h>

/**
 * struct nfp_eth_table - ETH table information
 * @count:	number of table entries
 * @ports:	table of ports
 *
 * @eth_index:	port index according to legacy ethX numbering
 * @index:	chip-wide first channel index
 * @nbi:	NBI index
 * @base:	first channel index (within NBI)
 * @lanes:	number of channels
 * @speed:	interface speed (in Mbps)
 * @mac_addr:	interface MAC address
 * @label:	interface id string
 * @enabled:	is enabled?
 * @tx_enabled:	is TX enabled?
 * @rx_enabled:	is RX enabled?
 */
struct nfp_eth_table {
	unsigned int count;
	struct nfp_eth_table_port {
		unsigned int eth_index;
		unsigned int index;
		unsigned int nbi;
		unsigned int base;
		unsigned int lanes;
		unsigned int speed;

		u8 mac_addr[ETH_ALEN];
		char label[8];

		bool enabled;
		bool tx_enabled;
		bool rx_enabled;
	} ports[0];
};

struct nfp_eth_table *nfp_eth_read_ports(struct nfp_cpp *cpp);
struct nfp_eth_table *
__nfp_eth_read_ports(struct nfp_cpp *cpp, struct nfp_nsp *nsp);
int nfp_eth_set_mod_enable(struct nfp_cpp *cpp, unsigned int idx, bool enable);

#endif
