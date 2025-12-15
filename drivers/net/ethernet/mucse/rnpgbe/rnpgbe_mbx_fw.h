/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_MBX_FW_H
#define _RNPGBE_MBX_FW_H

#include <linux/types.h>

#include "rnpgbe.h"

#define MUCSE_MBX_REQ_HDR_LEN 24

enum MUCSE_FW_CMD {
	GET_HW_INFO     = 0x0601,
	GET_MAC_ADDRESS = 0x0602,
	RESET_HW        = 0x0603,
	POWER_UP        = 0x0803,
};

struct mucse_hw_info {
	u8 link_stat;
	u8 port_mask;
	__le32 speed;
	__le16 phy_type;
	__le16 nic_mode;
	__le16 pfnum;
	__le32 fw_version;
	__le32 axi_mhz;
	union {
		u8 port_id[4];
		__le32 port_ids;
	};
	__le32 bd_uid;
	__le32 phy_id;
	__le32 wol_status;
	__le32 ext_info;
} __packed;

struct mbx_fw_cmd_req {
	__le16 flags;
	__le16 opcode;
	__le16 datalen;
	__le16 ret_value;
	__le32 cookie_lo;
	__le32 cookie_hi;
	__le32 reply_lo;
	__le32 reply_hi;
	union {
		u8 data[32];
		struct {
			__le32 version;
			__le32 status;
		} powerup;
		struct {
			__le32 port_mask;
			__le32 pfvf_num;
		} get_mac_addr;
	};
} __packed;

struct mbx_fw_cmd_reply {
	__le16 flags;
	__le16 opcode;
	__le16 error_code;
	__le16 datalen;
	__le32 cookie_lo;
	__le32 cookie_hi;
	union {
		u8 data[40];
		struct mac_addr {
			__le32 ports;
			struct _addr {
				/* for macaddr:01:02:03:04:05:06
				 * mac-hi=0x01020304 mac-lo=0x05060000
				 */
				u8 mac[8];
			} addrs[4];
		} mac_addr;
		struct mucse_hw_info hw_info;
	};
} __packed;

int mucse_mbx_sync_fw(struct mucse_hw *hw);
int mucse_mbx_powerup(struct mucse_hw *hw, bool is_powerup);
int mucse_mbx_reset_hw(struct mucse_hw *hw);
int mucse_mbx_get_macaddr(struct mucse_hw *hw, int pfvfnum,
			  u8 *mac_addr, int port);
#endif /* _RNPGBE_MBX_FW_H */
