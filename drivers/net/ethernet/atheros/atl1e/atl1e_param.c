/*
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/netdevice.h>

#include "atl1e.h"

/* This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */

#define ATL1E_MAX_NIC 32

#define OPTION_UNSET    -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

/* All parameters are treated the same, as an integer array of values.
 * This macro just reduces the need to repeat the same declaration code
 * over and over (plus this helps to avoid typo bugs).
 */
#define ATL1E_PARAM_INIT { [0 ... ATL1E_MAX_NIC] = OPTION_UNSET }

#define ATL1E_PARAM(x, desc) \
	static int x[ATL1E_MAX_NIC + 1] = ATL1E_PARAM_INIT; \
	static unsigned int num_##x; \
	module_param_array_named(x, x, int, &num_##x, 0); \
	MODULE_PARM_DESC(x, desc);

/* Transmit Memory count
 *
 * Valid Range: 64-2048
 *
 * Default Value: 128
 */
#define ATL1E_MIN_TX_DESC_CNT		32
#define ATL1E_MAX_TX_DESC_CNT		1020
#define ATL1E_DEFAULT_TX_DESC_CNT	128
ATL1E_PARAM(tx_desc_cnt, "Transmit description count");

/* Receive Memory Block Count
 *
 * Valid Range: 16-512
 *
 * Default Value: 128
 */
#define ATL1E_MIN_RX_MEM_SIZE		8    /* 8KB   */
#define ATL1E_MAX_RX_MEM_SIZE		1024 /* 1MB   */
#define ATL1E_DEFAULT_RX_MEM_SIZE	256  /* 128KB */
ATL1E_PARAM(rx_mem_size, "memory size of rx buffer(KB)");

/* User Specified MediaType Override
 *
 * Valid Range: 0-5
 *  - 0    - auto-negotiate at all supported speeds
 *  - 1    - only link at 100Mbps Full Duplex
 *  - 2    - only link at 100Mbps Half Duplex
 *  - 3    - only link at 10Mbps Full Duplex
 *  - 4    - only link at 10Mbps Half Duplex
 * Default Value: 0
 */

ATL1E_PARAM(media_type, "MediaType Select");

/* Interrupt Moderate Timer in units of 2 us
 *
 * Valid Range: 10-65535
 *
 * Default Value: 45000(90ms)
 */
#define INT_MOD_DEFAULT_CNT             100 /* 200us */
#define INT_MOD_MAX_CNT                 65000
#define INT_MOD_MIN_CNT                 50
ATL1E_PARAM(int_mod_timer, "Interrupt Moderator Timer");

#define AUTONEG_ADV_DEFAULT  0x2F
#define AUTONEG_ADV_MASK     0x2F
#define FLOW_CONTROL_DEFAULT FLOW_CONTROL_FULL

#define FLASH_VENDOR_DEFAULT    0
#define FLASH_VENDOR_MIN        0
#define FLASH_VENDOR_MAX        2

struct atl1e_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int  def;
	union {
		struct { /* range_option info */
			int min;
			int max;
		} r;
		struct { /* list_option info */
			int nr;
			struct atl1e_opt_list { int i; char *str; } *p;
		} l;
	} arg;
};

static int atl1e_validate_option(int *value, struct atl1e_option *opt,
				 struct atl1e_adapter *adapter)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			netdev_info(adapter->netdev,
				    "%s Enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			netdev_info(adapter->netdev,
				    "%s Disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			netdev_info(adapter->netdev, "%s set to %i\n",
				    opt->name, *value);
			return 0;
		}
		break;
	case list_option:{
			int i;
			struct atl1e_opt_list *ent;

			for (i = 0; i < opt->arg.l.nr; i++) {
				ent = &opt->arg.l.p[i];
				if (*value == ent->i) {
					if (ent->str[0] != '\0')
						netdev_info(adapter->netdev,
							    "%s\n", ent->str);
					return 0;
				}
			}
			break;
		}
	default:
		BUG();
	}

	netdev_info(adapter->netdev, "Invalid %s specified (%i) %s\n",
		    opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

/**
 * atl1e_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 */
void atl1e_check_options(struct atl1e_adapter *adapter)
{
	int bd = adapter->bd_number;

	if (bd >= ATL1E_MAX_NIC) {
		netdev_notice(adapter->netdev,
			      "no configuration for board #%i\n", bd);
		netdev_notice(adapter->netdev,
			      "Using defaults for all values\n");
	}

	{ 		/* Transmit Ring Size */
		struct atl1e_option opt = {
			.type = range_option,
			.name = "Transmit Ddescription Count",
			.err  = "using default of "
				__MODULE_STRING(ATL1E_DEFAULT_TX_DESC_CNT),
			.def  = ATL1E_DEFAULT_TX_DESC_CNT,
			.arg  = { .r = { .min = ATL1E_MIN_TX_DESC_CNT,
					 .max = ATL1E_MAX_TX_DESC_CNT} }
		};
		int val;
		if (num_tx_desc_cnt > bd) {
			val = tx_desc_cnt[bd];
			atl1e_validate_option(&val, &opt, adapter);
			adapter->tx_ring.count = (u16) val & 0xFFFC;
		} else
			adapter->tx_ring.count = (u16)opt.def;
	}

	{ 		/* Receive Memory Block Count */
		struct atl1e_option opt = {
			.type = range_option,
			.name = "Memory size of rx buffer(KB)",
			.err  = "using default of "
				__MODULE_STRING(ATL1E_DEFAULT_RX_MEM_SIZE),
			.def  = ATL1E_DEFAULT_RX_MEM_SIZE,
			.arg  = { .r = { .min = ATL1E_MIN_RX_MEM_SIZE,
					 .max = ATL1E_MAX_RX_MEM_SIZE} }
		};
		int val;
		if (num_rx_mem_size > bd) {
			val = rx_mem_size[bd];
			atl1e_validate_option(&val, &opt, adapter);
			adapter->rx_ring.page_size = (u32)val * 1024;
		} else {
			adapter->rx_ring.page_size = (u32)opt.def * 1024;
		}
	}

	{ 		/* Interrupt Moderate Timer */
		struct atl1e_option opt = {
			.type = range_option,
			.name = "Interrupt Moderate Timer",
			.err  = "using default of "
				__MODULE_STRING(INT_MOD_DEFAULT_CNT),
			.def  = INT_MOD_DEFAULT_CNT,
			.arg  = { .r = { .min = INT_MOD_MIN_CNT,
					 .max = INT_MOD_MAX_CNT} }
		} ;
		int val;
		if (num_int_mod_timer > bd) {
			val = int_mod_timer[bd];
			atl1e_validate_option(&val, &opt, adapter);
			adapter->hw.imt = (u16) val;
		} else
			adapter->hw.imt = (u16)(opt.def);
	}

	{ 		/* MediaType */
		struct atl1e_option opt = {
			.type = range_option,
			.name = "Speed/Duplex Selection",
			.err  = "using default of "
				__MODULE_STRING(MEDIA_TYPE_AUTO_SENSOR),
			.def  = MEDIA_TYPE_AUTO_SENSOR,
			.arg  = { .r = { .min = MEDIA_TYPE_AUTO_SENSOR,
					 .max = MEDIA_TYPE_10M_HALF} }
		} ;
		int val;
		if (num_media_type > bd) {
			val = media_type[bd];
			atl1e_validate_option(&val, &opt, adapter);
			adapter->hw.media_type = (u16) val;
		} else
			adapter->hw.media_type = (u16)(opt.def);

	}
}
