/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#ifndef __HCLGE_DEBUGFS_H
#define __HCLGE_DEBUGFS_H

#define HCLGE_DBG_BUF_LEN	   256
#define HCLGE_DBG_MNG_TBL_MAX	   64

#define HCLGE_DBG_MNG_VLAN_MASK_B  BIT(0)
#define HCLGE_DBG_MNG_MAC_MASK_B   BIT(1)
#define HCLGE_DBG_MNG_ETHER_MASK_B BIT(2)
#define HCLGE_DBG_MNG_E_TYPE_B	   BIT(11)
#define HCLGE_DBG_MNG_DROP_B	   BIT(13)
#define HCLGE_DBG_MNG_VLAN_TAG	   0x0FFF
#define HCLGE_DBG_MNG_PF_ID	   0x0007
#define HCLGE_DBG_MNG_VF_ID	   0x00FF

#pragma pack(1)

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

#pragma pack()
#endif
