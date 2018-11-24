/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#ifndef __HCLGE_DEBUGFS_H
#define __HCLGE_DEBUGFS_H

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
