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
	LINK_CHANGE_EVT = 0x0608,
	LINK_REPORT_EN  = 0x0613,
	PHY_LINK_SET    = 0x0630,
	SET_PHY_UP      = 0x0800,
	POWER_UP        = 0x0803,
};

#define RNPGBE_FW_ADV_10_FULL		BIT(2)
#define RNPGBE_FW_ADV_100_FULL		BIT(3)
#define RNPGBE_FW_ADV_1000_FULL		BIT(4)
#define RNPGBE_FW_ADV_10_HALF		BIT(10)
#define RNPGBE_FW_ADV_100_HALF		BIT(11)

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

#define ST_STATUS_LLDP_STATUS_MASK        BIT(12)

#define DUPLEX_BIT                        BIT(0)
struct st_status {
	u8 phyid;
	u8 flags;
	__le16 speed;
	__le16 status;
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
		struct {
			__le32 port_mask;
			__le32 status;
		} phy_status;
		struct {
			__le32 adv_speed_mask;
			__le32 autoneg;
			__le32 speed;
			__le32 duplex;
			__le32 nr_lane;
			__le32 tp_mdix_ctrl;
		} phy_link_set;
		struct {
			/* LINK_REPORT_EN uses its own 16-bit payload layout:
			 * status precedes port_mask.
			 */
			__le16 status;
			__le16 port_mask;
		} report_status;
		struct {
			__le16 changed_lanes;
			__le16 port_status;
			__le32 port_magic;
#define ST_VALID_MAGIC 0xa4a6a8a9
			struct st_status st;
		} link_stat;
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

/* Union wrappers to expose struct as __le32 dword array for mailbox
 * transport, eliminating the need for pointer casts.  The __packed
 * structs have no padding, so dwords[] overlays the fields exactly.
 */
union mbx_fw_cmd_req_u {
	struct mbx_fw_cmd_req r;
	__le32 dwords[sizeof(struct mbx_fw_cmd_req) / sizeof(__le32)];
};

union mbx_fw_cmd_reply_u {
	struct mbx_fw_cmd_reply r;
	__le32 dwords[sizeof(struct mbx_fw_cmd_reply) / sizeof(__le32)];
};

/* Firmware encoding for RNPGBE_LINK_ST[11:8]. Valid link events report
 * only these three speeds.
 */
enum mucse_speed {
	mucse_speed_10 = 0,
	mucse_speed_100 = 1,
	mucse_speed_1000 = 2,
};

int mucse_mbx_sync_fw(struct mucse_hw *hw);
int mucse_mbx_powerup(struct mucse_hw *hw, bool is_powerup);
int mucse_mbx_reset_hw(struct mucse_hw *hw);
int mucse_mbx_get_macaddr(struct mucse_hw *hw, int pfvfnum,
			  u8 *mac_addr, int port);
int mucse_mbx_set_link(struct mucse_hw *hw, u32 advertising, bool autoneg,
		       u32 speed, u32 duplex, u32 mdix_ctrl);
int mucse_mbx_phyup(struct mucse_hw *hw, bool is_phyup);
int mucse_mbx_link_report(struct mucse_hw *hw, bool is_report);
void mucse_fw_irq_handler(struct mucse_hw *hw);
#endif /* _RNPGBE_MBX_FW_H */
