/*
 * Copyright (C) 2017 Netronome Systems, Inc.
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

#ifndef NFP_FLOWER_CMSG_H
#define NFP_FLOWER_CMSG_H

#include <linux/bitfield.h>
#include <linux/skbuff.h>
#include <linux/types.h>

#include "../nfp_app.h"

/* The base header for a control message packet.
 * Defines an 8-bit version, and an 8-bit type, padded
 * to a 32-bit word. Rest of the packet is type-specific.
 */
struct nfp_flower_cmsg_hdr {
	__be16 pad;
	u8 type;
	u8 version;
};

#define NFP_FLOWER_CMSG_HLEN		sizeof(struct nfp_flower_cmsg_hdr)
#define NFP_FLOWER_CMSG_VER1		1

/* Types defined for port related control messages  */
enum nfp_flower_cmsg_type_port {
	NFP_FLOWER_CMSG_TYPE_PORT_MOD =		8,
	NFP_FLOWER_CMSG_TYPE_PORT_ECHO =	16,
	NFP_FLOWER_CMSG_TYPE_MAX =		32,
};

/* NFP_FLOWER_CMSG_TYPE_PORT_MOD */
struct nfp_flower_cmsg_portmod {
	__be32 portnum;
	u8 reserved;
	u8 info;
	__be16 mtu;
};

#define NFP_FLOWER_CMSG_PORTMOD_INFO_LINK	BIT(0)

enum nfp_flower_cmsg_port_type {
	NFP_FLOWER_CMSG_PORT_TYPE_UNSPEC =	0x0,
	NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT =	0x1,
	NFP_FLOWER_CMSG_PORT_TYPE_PCIE_PORT =	0x2,
};

enum nfp_flower_cmsg_port_vnic_type {
	NFP_FLOWER_CMSG_PORT_VNIC_TYPE_VF =	0x0,
	NFP_FLOWER_CMSG_PORT_VNIC_TYPE_PF =	0x1,
	NFP_FLOWER_CMSG_PORT_VNIC_TYPE_CTRL =	0x2,
};

#define NFP_FLOWER_CMSG_PORT_TYPE		GENMASK(31, 28)
#define NFP_FLOWER_CMSG_PORT_SYS_ID		GENMASK(27, 24)
#define NFP_FLOWER_CMSG_PORT_NFP_ID		GENMASK(23, 22)
#define NFP_FLOWER_CMSG_PORT_PCI		GENMASK(15, 14)
#define NFP_FLOWER_CMSG_PORT_VNIC_TYPE		GENMASK(13, 12)
#define NFP_FLOWER_CMSG_PORT_VNIC		GENMASK(11, 6)
#define NFP_FLOWER_CMSG_PORT_PCIE_Q		GENMASK(5, 0)
#define NFP_FLOWER_CMSG_PORT_PHYS_PORT_NUM	GENMASK(7, 0)

static inline u32 nfp_flower_cmsg_phys_port(u8 phys_port)
{
	return FIELD_PREP(NFP_FLOWER_CMSG_PORT_PHYS_PORT_NUM, phys_port) |
		FIELD_PREP(NFP_FLOWER_CMSG_PORT_TYPE,
			   NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT);
}

static inline u32
nfp_flower_cmsg_pcie_port(u8 nfp_pcie, enum nfp_flower_cmsg_port_vnic_type type,
			  u8 vnic, u8 q)
{
	return FIELD_PREP(NFP_FLOWER_CMSG_PORT_PCI, nfp_pcie) |
		FIELD_PREP(NFP_FLOWER_CMSG_PORT_VNIC_TYPE, type) |
		FIELD_PREP(NFP_FLOWER_CMSG_PORT_VNIC, vnic) |
		FIELD_PREP(NFP_FLOWER_CMSG_PORT_PCIE_Q, q) |
		FIELD_PREP(NFP_FLOWER_CMSG_PORT_TYPE,
			   NFP_FLOWER_CMSG_PORT_TYPE_PCIE_PORT);
}

int nfp_flower_cmsg_portmod(struct nfp_repr *repr, bool carrier_ok);
void nfp_flower_cmsg_rx(struct nfp_app *app, struct sk_buff *skb);

#endif
