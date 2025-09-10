/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#ifndef __HCLGE_DEBUGFS_H
#define __HCLGE_DEBUGFS_H

#include <linux/etherdevice.h>
#include "hclge_cmd.h"

#define HCLGE_DBG_MNG_TBL_MAX	   64

#define HCLGE_DBG_MNG_VLAN_MASK_B  BIT(0)
#define HCLGE_DBG_MNG_MAC_MASK_B   BIT(1)
#define HCLGE_DBG_MNG_ETHER_MASK_B BIT(2)
#define HCLGE_DBG_MNG_E_TYPE_B	   BIT(11)
#define HCLGE_DBG_MNG_DROP_B	   BIT(13)
#define HCLGE_DBG_MNG_VLAN_TAG	   0x0FFF
#define HCLGE_DBG_MNG_PF_ID	   0x0007
#define HCLGE_DBG_MNG_VF_ID	   0x00FF

/* Get DFX BD number offset */
#define HCLGE_DBG_DFX_BIOS_OFFSET  1
#define HCLGE_DBG_DFX_SSU_0_OFFSET 2
#define HCLGE_DBG_DFX_SSU_1_OFFSET 3
#define HCLGE_DBG_DFX_IGU_OFFSET   4
#define HCLGE_DBG_DFX_RPU_0_OFFSET 5

#define HCLGE_DBG_DFX_RPU_1_OFFSET 6
#define HCLGE_DBG_DFX_NCSI_OFFSET  7
#define HCLGE_DBG_DFX_RTC_OFFSET   8
#define HCLGE_DBG_DFX_PPP_OFFSET   9
#define HCLGE_DBG_DFX_RCB_OFFSET   10
#define HCLGE_DBG_DFX_TQP_OFFSET   11

#define HCLGE_DBG_DFX_SSU_2_OFFSET 12

struct hclge_qos_pri_map_cmd {
	u8 pri0_tc  : 4,
	   pri1_tc  : 4;
	u8 pri2_tc  : 4,
	   pri3_tc  : 4;
	u8 pri4_tc  : 4,
	   pri5_tc  : 4;
	u8 pri6_tc  : 4,
	   pri7_tc  : 4;
	u8 vlan_pri : 4,
	   rev	    : 4;
};

struct hclge_dbg_bitmap_cmd {
	union {
		u8 bitmap;
		struct {
			u8 bit0 : 1,
			   bit1 : 1,
			   bit2 : 1,
			   bit3 : 1,
			   bit4 : 1,
			   bit5 : 1,
			   bit6 : 1,
			   bit7 : 1;
		};
	};
};

struct hclge_dbg_reg_common_msg {
	int msg_num;
	int offset;
	enum hclge_opcode_type cmd;
};

struct hclge_dbg_tcam_msg {
	u8 stage;
	u32 loc;
};

#define	HCLGE_DBG_MAX_DFX_MSG_LEN	60
struct hclge_dbg_dfx_message {
	int flag;
	char message[HCLGE_DBG_MAX_DFX_MSG_LEN];
};

#define HCLGE_DBG_MAC_REG_TYPE_LEN	32
struct hclge_dbg_reg_type_info {
	enum hnae3_dbg_cmd cmd;
	const struct hclge_dbg_dfx_message *dfx_msg;
	struct hclge_dbg_reg_common_msg reg_msg;
};

struct hclge_dbg_func {
	enum hnae3_dbg_cmd cmd;
	int (*dbg_dump)(struct hclge_dev *hdev, char *buf, int len);
	int (*dbg_dump_reg)(struct hclge_dev *hdev, enum hnae3_dbg_cmd cmd,
			    char *buf, int len);
	read_func dbg_read_func;
};

struct hclge_dbg_status_dfx_info {
	u32  offset;
	char message[HCLGE_DBG_MAX_DFX_MSG_LEN];
};

#define HCLGE_DBG_INFO_LEN			256
#define HCLGE_DBG_VLAN_FLTR_INFO_LEN		256
#define HCLGE_DBG_VLAN_OFFLOAD_INFO_LEN		512
#define HCLGE_DBG_ID_LEN			16
#define HCLGE_DBG_ITEM_NAME_LEN			32
#define HCLGE_DBG_DATA_STR_LEN			32
#define HCLGE_DBG_TM_INFO_LEN			256

#define HCLGE_BILLION_NANO_SECONDS	1000000000

struct hclge_dbg_item {
	char name[HCLGE_DBG_ITEM_NAME_LEN];
	u16 interval; /* blank numbers after the item */
};

struct hclge_dbg_vlan_cfg {
	u16 pvid;
	u8 accept_tag1;
	u8 accept_tag2;
	u8 accept_untag1;
	u8 accept_untag2;
	u8 insert_tag1;
	u8 insert_tag2;
	u8 shift_tag;
	u8 strip_tag1;
	u8 strip_tag2;
	u8 drop_tag1;
	u8 drop_tag2;
	u8 pri_only1;
	u8 pri_only2;
};

int hclge_dbg_cmd_send(struct hclge_dev *hdev, struct hclge_desc *desc_src,
		       int index, int bd_num, enum hclge_opcode_type cmd);

#endif
