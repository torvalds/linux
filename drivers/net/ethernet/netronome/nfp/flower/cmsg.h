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
#include <net/geneve.h>

#include "../nfp_app.h"
#include "../nfpcore/nfp_cpp.h"

#define NFP_FLOWER_LAYER_EXT_META	BIT(0)
#define NFP_FLOWER_LAYER_PORT		BIT(1)
#define NFP_FLOWER_LAYER_MAC		BIT(2)
#define NFP_FLOWER_LAYER_TP		BIT(3)
#define NFP_FLOWER_LAYER_IPV4		BIT(4)
#define NFP_FLOWER_LAYER_IPV6		BIT(5)
#define NFP_FLOWER_LAYER_CT		BIT(6)
#define NFP_FLOWER_LAYER_VXLAN		BIT(7)

#define NFP_FLOWER_LAYER2_GENEVE	BIT(5)
#define NFP_FLOWER_LAYER2_GENEVE_OP	BIT(6)

#define NFP_FLOWER_MASK_VLAN_PRIO	GENMASK(15, 13)
#define NFP_FLOWER_MASK_VLAN_PRESENT	BIT(12)
#define NFP_FLOWER_MASK_VLAN_VID	GENMASK(11, 0)

#define NFP_FLOWER_MASK_MPLS_LB		GENMASK(31, 12)
#define NFP_FLOWER_MASK_MPLS_TC		GENMASK(11, 9)
#define NFP_FLOWER_MASK_MPLS_BOS	BIT(8)
#define NFP_FLOWER_MASK_MPLS_Q		BIT(0)

#define NFP_FL_IP_FRAG_FIRST		BIT(7)
#define NFP_FL_IP_FRAGMENTED		BIT(6)

/* Compressed HW representation of TCP Flags */
#define NFP_FL_TCP_FLAG_URG		BIT(4)
#define NFP_FL_TCP_FLAG_PSH		BIT(3)
#define NFP_FL_TCP_FLAG_RST		BIT(2)
#define NFP_FL_TCP_FLAG_SYN		BIT(1)
#define NFP_FL_TCP_FLAG_FIN		BIT(0)

#define NFP_FL_SC_ACT_DROP		0x80000000
#define NFP_FL_SC_ACT_USER		0x7D000000
#define NFP_FL_SC_ACT_POPV		0x6A000000
#define NFP_FL_SC_ACT_NULL		0x00000000

/* The maximum action list size (in bytes) supported by the NFP.
 */
#define NFP_FL_MAX_A_SIZ		1216
#define NFP_FL_LW_SIZ			2

/* Maximum allowed geneve options */
#define NFP_FL_MAX_GENEVE_OPT_ACT	32
#define NFP_FL_MAX_GENEVE_OPT_CNT	64
#define NFP_FL_MAX_GENEVE_OPT_KEY	32

/* Action opcodes */
#define NFP_FL_ACTION_OPCODE_OUTPUT		0
#define NFP_FL_ACTION_OPCODE_PUSH_VLAN		1
#define NFP_FL_ACTION_OPCODE_POP_VLAN		2
#define NFP_FL_ACTION_OPCODE_SET_IPV4_TUNNEL	6
#define NFP_FL_ACTION_OPCODE_SET_ETHERNET	7
#define NFP_FL_ACTION_OPCODE_SET_IPV4_ADDRS	9
#define NFP_FL_ACTION_OPCODE_SET_IPV6_SRC	11
#define NFP_FL_ACTION_OPCODE_SET_IPV6_DST	12
#define NFP_FL_ACTION_OPCODE_SET_UDP		14
#define NFP_FL_ACTION_OPCODE_SET_TCP		15
#define NFP_FL_ACTION_OPCODE_PRE_LAG		16
#define NFP_FL_ACTION_OPCODE_PRE_TUNNEL		17
#define NFP_FL_ACTION_OPCODE_PUSH_GENEVE	26
#define NFP_FL_ACTION_OPCODE_NUM		32

#define NFP_FL_OUT_FLAGS_LAST		BIT(15)
#define NFP_FL_OUT_FLAGS_USE_TUN	BIT(4)
#define NFP_FL_OUT_FLAGS_TYPE_IDX	GENMASK(2, 0)

#define NFP_FL_PUSH_VLAN_PRIO		GENMASK(15, 13)
#define NFP_FL_PUSH_VLAN_VID		GENMASK(11, 0)

/* LAG ports */
#define NFP_FL_LAG_OUT			0xC0DE0000

/* Tunnel ports */
#define NFP_FL_PORT_TYPE_TUN		0x50000000
#define NFP_FL_IPV4_TUNNEL_TYPE		GENMASK(7, 4)
#define NFP_FL_IPV4_PRE_TUN_INDEX	GENMASK(2, 0)

#define NFP_FLOWER_WORKQ_MAX_SKBS	30000

#define nfp_flower_cmsg_warn(app, fmt, args...)                         \
	do {                                                            \
		if (net_ratelimit())                                    \
			nfp_warn((app)->cpp, fmt, ## args);             \
	} while (0)

enum nfp_flower_tun_type {
	NFP_FL_TUNNEL_NONE =	0,
	NFP_FL_TUNNEL_VXLAN =	2,
	NFP_FL_TUNNEL_GENEVE =	4,
};

struct nfp_fl_act_head {
	u8 jump_id;
	u8 len_lw;
};

struct nfp_fl_set_eth {
	struct nfp_fl_act_head head;
	__be16 reserved;
	u8 eth_addr_mask[ETH_ALEN * 2];
	u8 eth_addr_val[ETH_ALEN * 2];
};

struct nfp_fl_set_ip4_addrs {
	struct nfp_fl_act_head head;
	__be16 reserved;
	__be32 ipv4_src_mask;
	__be32 ipv4_src;
	__be32 ipv4_dst_mask;
	__be32 ipv4_dst;
};

struct nfp_fl_set_ipv6_addr {
	struct nfp_fl_act_head head;
	__be16 reserved;
	struct {
		__be32 mask;
		__be32 exact;
	} ipv6[4];
};

struct nfp_fl_set_tport {
	struct nfp_fl_act_head head;
	__be16 reserved;
	u8 tp_port_mask[4];
	u8 tp_port_val[4];
};

struct nfp_fl_output {
	struct nfp_fl_act_head head;
	__be16 flags;
	__be32 port;
};

struct nfp_fl_push_vlan {
	struct nfp_fl_act_head head;
	__be16 reserved;
	__be16 vlan_tpid;
	__be16 vlan_tci;
};

struct nfp_fl_pop_vlan {
	struct nfp_fl_act_head head;
	__be16 reserved;
};

struct nfp_fl_pre_lag {
	struct nfp_fl_act_head head;
	__be16 group_id;
	u8 lag_version[3];
	u8 instance;
};

#define NFP_FL_PRE_LAG_VER_OFF	8

struct nfp_fl_pre_tunnel {
	struct nfp_fl_act_head head;
	__be16 reserved;
	__be32 ipv4_dst;
	/* reserved for use with IPv6 addresses */
	__be32 extra[3];
};

struct nfp_fl_set_ipv4_udp_tun {
	struct nfp_fl_act_head head;
	__be16 reserved;
	__be64 tun_id __packed;
	__be32 tun_type_index;
	__be16 tun_flags;
	u8 ttl;
	u8 tos;
	__be32 extra;
	u8 tun_len;
	u8 res2;
	__be16 tun_proto;
};

struct nfp_fl_push_geneve {
	struct nfp_fl_act_head head;
	__be16 reserved;
	__be16 class;
	u8 type;
	u8 length;
	u8 opt_data[];
};

/* Metadata with L2 (1W/4B)
 * ----------------------------------------------------------------
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    key_type   |    mask_id    | PCP |p|   vlan outermost VID  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *                                 ^                               ^
 *                           NOTE: |             TCI               |
 *                                 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_meta_tci {
	u8 nfp_flow_key_layer;
	u8 mask_id;
	__be16 tci;
};

/* Extended metadata for additional key_layers (1W/4B)
 * ----------------------------------------------------------------
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      nfp_flow_key_layer2                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_ext_meta {
	__be32 nfp_flow_key_layer2;
};

/* Port details (1W/4B)
 * ----------------------------------------------------------------
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         port_ingress                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_in_port {
	__be32 in_port;
};

/* L2 details (4W/16B)
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     mac_addr_dst, 31 - 0                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      mac_addr_dst, 47 - 32    |     mac_addr_src, 15 - 0      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     mac_addr_src, 47 - 16                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |       mpls outermost label            |  TC |B|   reserved  |q|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_mac_mpls {
	u8 mac_dst[6];
	u8 mac_src[6];
	__be32 mpls_lse;
};

/* L4 ports (for UDP, TCP, SCTP) (1W/4B)
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            port_src           |           port_dst            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_tp_ports {
	__be16 port_src;
	__be16 port_dst;
};

struct nfp_flower_ip_ext {
	u8 tos;
	u8 proto;
	u8 ttl;
	u8 flags;
};

/* L3 IPv4 details (3W/12B)
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    DSCP   |ECN|   protocol    |      ttl      |     flags     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        ipv4_addr_src                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        ipv4_addr_dst                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_ipv4 {
	struct nfp_flower_ip_ext ip_ext;
	__be32 ipv4_src;
	__be32 ipv4_dst;
};

/* L3 IPv6 details (10W/40B)
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    DSCP   |ECN|   protocol    |      ttl      |     flags     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   ipv6_exthdr   | res |            ipv6_flow_label            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_src,   31 - 0                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_src,  63 - 32                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_src,  95 - 64                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_src, 127 - 96                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_dst,   31 - 0                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_dst,  63 - 32                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_dst,  95 - 64                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  ipv6_addr_dst, 127 - 96                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_ipv6 {
	struct nfp_flower_ip_ext ip_ext;
	__be32 ipv6_flow_label_exthdr;
	struct in6_addr ipv6_src;
	struct in6_addr ipv6_dst;
};

/* Flow Frame IPv4 UDP TUNNEL --> Tunnel details (4W/16B)
 * -----------------------------------------------------------------
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         ipv4_addr_src                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         ipv4_addr_dst                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Reserved            |      tos      |      ttl      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Reserved                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     VNI                       |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_flower_ipv4_udp_tun {
	__be32 ip_src;
	__be32 ip_dst;
	__be16 reserved1;
	u8 tos;
	u8 ttl;
	__be32 reserved2;
	__be32 tun_id;
};

struct nfp_flower_geneve_options {
	u8 data[NFP_FL_MAX_GENEVE_OPT_KEY];
};

#define NFP_FL_TUN_VNI_OFFSET 8

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
	NFP_FLOWER_CMSG_TYPE_FLOW_ADD =		0,
	NFP_FLOWER_CMSG_TYPE_FLOW_DEL =		2,
	NFP_FLOWER_CMSG_TYPE_LAG_CONFIG =	4,
	NFP_FLOWER_CMSG_TYPE_PORT_REIFY =	6,
	NFP_FLOWER_CMSG_TYPE_MAC_REPR =		7,
	NFP_FLOWER_CMSG_TYPE_PORT_MOD =		8,
	NFP_FLOWER_CMSG_TYPE_NO_NEIGH =		10,
	NFP_FLOWER_CMSG_TYPE_TUN_MAC =		11,
	NFP_FLOWER_CMSG_TYPE_ACTIVE_TUNS =	12,
	NFP_FLOWER_CMSG_TYPE_TUN_NEIGH =	13,
	NFP_FLOWER_CMSG_TYPE_TUN_IPS =		14,
	NFP_FLOWER_CMSG_TYPE_FLOW_STATS =	15,
	NFP_FLOWER_CMSG_TYPE_PORT_ECHO =	16,
	NFP_FLOWER_CMSG_TYPE_MAX =		32,
};

/* NFP_FLOWER_CMSG_TYPE_MAC_REPR */
struct nfp_flower_cmsg_mac_repr {
	u8 reserved[3];
	u8 num_ports;
	struct {
		u8 idx;
		u8 info;
		u8 nbi_port;
		u8 phys_port;
	} ports[0];
};

#define NFP_FLOWER_CMSG_MAC_REPR_NBI		GENMASK(1, 0)

/* NFP_FLOWER_CMSG_TYPE_PORT_MOD */
struct nfp_flower_cmsg_portmod {
	__be32 portnum;
	u8 reserved;
	u8 info;
	__be16 mtu;
};

#define NFP_FLOWER_CMSG_PORTMOD_INFO_LINK	BIT(0)
#define NFP_FLOWER_CMSG_PORTMOD_MTU_CHANGE_ONLY	BIT(1)

/* NFP_FLOWER_CMSG_TYPE_PORT_REIFY */
struct nfp_flower_cmsg_portreify {
	__be32 portnum;
	u16 reserved;
	__be16 info;
};

#define NFP_FLOWER_CMSG_PORTREIFY_INFO_EXIST	BIT(0)

enum nfp_flower_cmsg_port_type {
	NFP_FLOWER_CMSG_PORT_TYPE_UNSPEC =	0x0,
	NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT =	0x1,
	NFP_FLOWER_CMSG_PORT_TYPE_PCIE_PORT =	0x2,
	NFP_FLOWER_CMSG_PORT_TYPE_OTHER_PORT =  0x3,
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

static inline void *nfp_flower_cmsg_get_data(struct sk_buff *skb)
{
	return (unsigned char *)skb->data + NFP_FLOWER_CMSG_HLEN;
}

static inline int nfp_flower_cmsg_get_data_len(struct sk_buff *skb)
{
	return skb->len - NFP_FLOWER_CMSG_HLEN;
}

struct sk_buff *
nfp_flower_cmsg_mac_repr_start(struct nfp_app *app, unsigned int num_ports);
void
nfp_flower_cmsg_mac_repr_add(struct sk_buff *skb, unsigned int idx,
			     unsigned int nbi, unsigned int nbi_port,
			     unsigned int phys_port);
int nfp_flower_cmsg_portmod(struct nfp_repr *repr, bool carrier_ok,
			    unsigned int mtu, bool mtu_only);
int nfp_flower_cmsg_portreify(struct nfp_repr *repr, bool exists);
void nfp_flower_cmsg_process_rx(struct work_struct *work);
void nfp_flower_cmsg_rx(struct nfp_app *app, struct sk_buff *skb);
struct sk_buff *
nfp_flower_cmsg_alloc(struct nfp_app *app, unsigned int size,
		      enum nfp_flower_cmsg_type_port type, gfp_t flag);

#endif
