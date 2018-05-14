/* Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _RMNET_MAP_H_
#define _RMNET_MAP_H_

struct rmnet_map_control_command {
	u8  command_name;
	u8  cmd_type:2;
	u8  reserved:6;
	u16 reserved2;
	u32 transaction_id;
	union {
		struct {
			u16 ip_family:2;
			u16 reserved:14;
			u16 flow_control_seq_num;
			u32 qos_id;
		} flow_control;
		u8 data[0];
	};
}  __aligned(1);

enum rmnet_map_commands {
	RMNET_MAP_COMMAND_NONE,
	RMNET_MAP_COMMAND_FLOW_DISABLE,
	RMNET_MAP_COMMAND_FLOW_ENABLE,
	/* These should always be the last 2 elements */
	RMNET_MAP_COMMAND_UNKNOWN,
	RMNET_MAP_COMMAND_ENUM_LENGTH
};

struct rmnet_map_header {
	u8  pad_len:6;
	u8  reserved_bit:1;
	u8  cd_bit:1;
	u8  mux_id;
	u16 pkt_len;
}  __aligned(1);

struct rmnet_map_dl_csum_trailer {
	u8  reserved1;
	u8  valid:1;
	u8  reserved2:7;
	u16 csum_start_offset;
	u16 csum_length;
	__be16 csum_value;
} __aligned(1);

struct rmnet_map_ul_csum_header {
	__be16 csum_start_offset;
	u16 csum_insert_offset:14;
	u16 udp_ip4_ind:1;
	u16 csum_enabled:1;
} __aligned(1);

#define RMNET_MAP_GET_MUX_ID(Y) (((struct rmnet_map_header *) \
				 (Y)->data)->mux_id)
#define RMNET_MAP_GET_CD_BIT(Y) (((struct rmnet_map_header *) \
				(Y)->data)->cd_bit)
#define RMNET_MAP_GET_PAD(Y) (((struct rmnet_map_header *) \
				(Y)->data)->pad_len)
#define RMNET_MAP_GET_CMD_START(Y) ((struct rmnet_map_control_command *) \
				    ((Y)->data + \
				      sizeof(struct rmnet_map_header)))
#define RMNET_MAP_GET_LENGTH(Y) (ntohs(((struct rmnet_map_header *) \
					(Y)->data)->pkt_len))

#define RMNET_MAP_COMMAND_REQUEST     0
#define RMNET_MAP_COMMAND_ACK         1
#define RMNET_MAP_COMMAND_UNSUPPORTED 2
#define RMNET_MAP_COMMAND_INVALID     3

#define RMNET_MAP_NO_PAD_BYTES        0
#define RMNET_MAP_ADD_PAD_BYTES       1

struct sk_buff *rmnet_map_deaggregate(struct sk_buff *skb,
				      struct rmnet_port *port);
struct rmnet_map_header *rmnet_map_add_map_header(struct sk_buff *skb,
						  int hdrlen, int pad);
void rmnet_map_command(struct sk_buff *skb, struct rmnet_port *port);
int rmnet_map_checksum_downlink_packet(struct sk_buff *skb, u16 len);
void rmnet_map_checksum_uplink_packet(struct sk_buff *skb,
				      struct net_device *orig_dev);

#endif /* _RMNET_MAP_H_ */
