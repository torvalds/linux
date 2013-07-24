/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _HCI_PACKET_H_
#define _HCI_PACKET_H_

#define HCI_HEADER_SIZE 4

/*
 * The NIC type definition:
 * For backward compatibility, lower 16 bits used as they were.
 * Lower 16 bit: NIC_TYPE values
 * Uppoer 16 bit: NIC_TYPE Flags
 */
#define NIC_TYPE_NIC0		0x00000010
#define NIC_TYPE_NIC1		0x00000011
#define NIC_TYPE_NIC2		0x00000012
#define NIC_TYPE_NIC3		0x00000013
#define NIC_TYPE_ARP		0x00000100
#define NIC_TYPE_ICMPV6		0x00000200
#define NIC_TYPE_MASK		0x0000FFFF
#define NIC_TYPE_F_IPV4		0x00010000
#define NIC_TYPE_F_IPV6		0x00020000
#define NIC_TYPE_F_DHCP		0x00040000
#define NIC_TYPE_F_NDP		0x00080000
#define NIC_TYPE_F_VLAN		0x00100000

struct hci_packet {
	u16 cmd_evt;
	u16 len;
	u8 data[0];
} __packed;

struct tlv {
	u8 type;
	u8 len;
	u8 *data[1];
} __packed;

struct sdu_header {
	u16 cmd_evt;
	u16 len;
	u32 dftEpsId;
	u32 bearer_ID;
	u32 nic_type;
} __packed;

struct sdu {
	u16 cmd_evt;
	u16 len;
	u32 dftEpsId;
	u32 bearer_ID;
	u32 nic_type;
	u8 data[0];
} __packed;

struct multi_sdu {
	u16 cmd_evt;
	u16 len;
	u16 num_packet;
	u16 reserved;
	u8 data[0];
} __packed;

struct hci_pdn_table_ind {
	u16 cmd_evt;
	u16 len;
	u8 activate;
	u32 dft_eps_id;
	u32 nic_type;
	u8 pdn_type;
	u8 ipv4_addr[4];
	u8 ipv6_intf_id[8];
} __packed;

struct hci_connect_ind {
	u16 cmd_evt;
	u16 len;
	u32 connect;
} __packed;


#endif /* _HCI_PACKET_H_ */
