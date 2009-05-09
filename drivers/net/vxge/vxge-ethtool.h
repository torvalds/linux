/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-ethtool.h: Driver for Neterion Inc's X3100 Series 10GbE PCIe I/O
 *                 Virtualized Server Adapter.
 * Copyright(c) 2002-2009 Neterion Inc.
 ******************************************************************************/
#ifndef _VXGE_ETHTOOL_H
#define _VXGE_ETHTOOL_H

#include "vxge-main.h"

/* Ethtool related variables and Macros. */
static int vxge_ethtool_get_sset_count(struct net_device *dev, int sset);

static char ethtool_driver_stats_keys[][ETH_GSTRING_LEN] = {
	{"\n DRIVER STATISTICS"},
	{"vpaths_opened"},
	{"vpath_open_fail_cnt"},
	{"link_up_cnt"},
	{"link_down_cnt"},
	{"tx_frms"},
	{"tx_errors"},
	{"tx_bytes"},
	{"txd_not_free"},
	{"txd_out_of_desc"},
	{"rx_frms"},
	{"rx_errors"},
	{"rx_bytes"},
	{"rx_mcast"},
	{"pci_map_fail_cnt"},
	{"skb_alloc_fail_cnt"}
};

#define VXGE_TITLE_LEN			5
#define VXGE_HW_VPATH_STATS_LEN 	27
#define VXGE_HW_AGGR_STATS_LEN  	13
#define VXGE_HW_PORT_STATS_LEN  	94
#define VXGE_HW_VPATH_TX_STATS_LEN	19
#define VXGE_HW_VPATH_RX_STATS_LEN	42
#define VXGE_SW_STATS_LEN		60
#define VXGE_HW_STATS_LEN	(VXGE_HW_VPATH_STATS_LEN +\
				VXGE_HW_AGGR_STATS_LEN +\
				VXGE_HW_PORT_STATS_LEN +\
				VXGE_HW_VPATH_TX_STATS_LEN +\
				VXGE_HW_VPATH_RX_STATS_LEN)

#define DRIVER_STAT_LEN (sizeof(ethtool_driver_stats_keys)/ETH_GSTRING_LEN)
#define STAT_LEN (VXGE_HW_STATS_LEN + DRIVER_STAT_LEN + VXGE_SW_STATS_LEN)

/* Maximum flicker time of adapter LED */
#define VXGE_MAX_FLICKER_TIME (60 * HZ) /* 60 seconds */
#define VXGE_FLICKER_ON		1
#define VXGE_FLICKER_OFF	0

#define vxge_add_string(fmt, size, buf, ...) {\
	snprintf(buf + *size, ETH_GSTRING_LEN, fmt, __VA_ARGS__); \
	*size += ETH_GSTRING_LEN; \
}

#endif /*_VXGE_ETHTOOL_H*/
