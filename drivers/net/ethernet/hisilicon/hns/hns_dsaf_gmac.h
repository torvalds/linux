/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 */

#ifndef _HNS_GMAC_H
#define _HNS_GMAC_H

#include "hns_dsaf_mac.h"

enum hns_port_mode {
	GMAC_10M_MII = 0,
	GMAC_100M_MII,
	GMAC_1000M_GMII,
	GMAC_10M_RGMII,
	GMAC_100M_RGMII,
	GMAC_1000M_RGMII,
	GMAC_10M_SGMII,
	GMAC_100M_SGMII,
	GMAC_1000M_SGMII,
	GMAC_10000M_SGMII	/* 10GE */
};

enum hns_gmac_duplex_mdoe {
	GMAC_HALF_DUPLEX_MODE = 0,
	GMAC_FULL_DUPLEX_MODE
};

struct hns_gmac_port_mode_cfg {
	enum hns_port_mode port_mode;
	u32 max_frm_size;
	u32 short_runts_thr;
	u32 pad_enable;
	u32 crc_add;
	u32 an_enable;	/*auto-nego enable  */
	u32 runt_pkt_en;
	u32 strip_pad_en;
};

#define ETH_GMAC_DUMP_NUM		96
#endif				/* __HNS_GMAC_H__ */
