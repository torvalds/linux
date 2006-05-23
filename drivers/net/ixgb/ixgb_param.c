/*******************************************************************************

  
  Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "ixgb.h"

/* This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */

#define IXGB_MAX_NIC 8

#define OPTION_UNSET	-1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

/* All parameters are treated the same, as an integer array of values.
 * This macro just reduces the need to repeat the same declaration code
 * over and over (plus this helps to avoid typo bugs).
 */

#define IXGB_PARAM_INIT { [0 ... IXGB_MAX_NIC] = OPTION_UNSET }
#define IXGB_PARAM(X, desc) \
	static int __devinitdata X[IXGB_MAX_NIC+1] = IXGB_PARAM_INIT; \
	static int num_##X = 0; \
	module_param_array_named(X, X, int, &num_##X, 0); \
	MODULE_PARM_DESC(X, desc);

/* Transmit Descriptor Count
 *
 * Valid Range: 64-4096
 *
 * Default Value: 256
 */

IXGB_PARAM(TxDescriptors, "Number of transmit descriptors");

/* Receive Descriptor Count
 *
 * Valid Range: 64-4096
 *
 * Default Value: 1024
 */

IXGB_PARAM(RxDescriptors, "Number of receive descriptors");

/* User Specified Flow Control Override
 *
 * Valid Range: 0-3
 *  - 0 - No Flow Control
 *  - 1 - Rx only, respond to PAUSE frames but do not generate them
 *  - 2 - Tx only, generate PAUSE frames but ignore them on receive
 *  - 3 - Full Flow Control Support
 *
 * Default Value: 2 - Tx only (silicon bug avoidance)
 */

IXGB_PARAM(FlowControl, "Flow Control setting");

/* XsumRX - Receive Checksum Offload Enable/Disable
 *
 * Valid Range: 0, 1
 *  - 0 - disables all checksum offload
 *  - 1 - enables receive IP/TCP/UDP checksum offload
 *        on 82597 based NICs
 *
 * Default Value: 1
 */

IXGB_PARAM(XsumRX, "Disable or enable Receive Checksum offload");

/* Transmit Interrupt Delay in units of 0.8192 microseconds
 *
 * Valid Range: 0-65535
 *
 * Default Value: 32
 */

IXGB_PARAM(TxIntDelay, "Transmit Interrupt Delay");

/* Receive Interrupt Delay in units of 0.8192 microseconds
 *
 * Valid Range: 0-65535
 *
 * Default Value: 72
 */

IXGB_PARAM(RxIntDelay, "Receive Interrupt Delay");

/* Receive Flow control high threshold (when we send a pause frame)
 * (FCRTH)
 *
 * Valid Range: 1,536 - 262,136 (0x600 - 0x3FFF8, 8 byte granularity)
 *
 * Default Value: 196,608 (0x30000)
 */

IXGB_PARAM(RxFCHighThresh, "Receive Flow Control High Threshold");

/* Receive Flow control low threshold (when we send a resume frame)
 * (FCRTL)
 *
 * Valid Range: 64 - 262,136 (0x40 - 0x3FFF8, 8 byte granularity)
 *              must be less than high threshold by at least 8 bytes
 *
 * Default Value:  163,840 (0x28000)
 */

IXGB_PARAM(RxFCLowThresh, "Receive Flow Control Low Threshold");

/* Flow control request timeout (how long to pause the link partner's tx)
 * (PAP 15:0)
 *
 * Valid Range: 1 - 65535 
 *
 * Default Value:  65535 (0xffff) (we'll send an xon if we recover)
 */

IXGB_PARAM(FCReqTimeout, "Flow Control Request Timeout");

/* Interrupt Delay Enable
 *
 * Valid Range: 0, 1
 *
 *  - 0 - disables transmit interrupt delay
 *  - 1 - enables transmmit interrupt delay
 *
 * Default Value: 1
 */

IXGB_PARAM(IntDelayEnable, "Transmit Interrupt Delay Enable");


#define DEFAULT_TIDV	   		     32
#define MAX_TIDV			 0xFFFF
#define MIN_TIDV			      0

#define DEFAULT_RDTR		   	     72
#define MAX_RDTR			 0xFFFF
#define MIN_RDTR			      0

#define XSUMRX_DEFAULT		 OPTION_ENABLED

#define DEFAULT_FCRTL	  		0x28000
#define DEFAULT_FCRTH			0x30000
#define MIN_FCRTL			      0
#define MAX_FCRTL			0x3FFE8
#define MIN_FCRTH			      8
#define MAX_FCRTH			0x3FFF0

#define MIN_FCPAUSE			      1
#define MAX_FCPAUSE			 0xffff
#define DEFAULT_FCPAUSE		  	 0xFFFF /* this may be too long */

struct ixgb_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int def;
	union {
		struct {	/* range_option info */
			int min;
			int max;
		} r;
		struct {	/* list_option info */
			int nr;
			struct ixgb_opt_list {
				int i;
				char *str;
			} *p;
		} l;
	} arg;
};

static int __devinit
ixgb_validate_option(int *value, struct ixgb_option *opt)
{
	if(*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			printk(KERN_INFO "%s Enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			printk(KERN_INFO "%s Disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if(*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			printk(KERN_INFO "%s set to %i\n", opt->name, *value);
			return 0;
		}
		break;
	case list_option: {
		int i;
		struct ixgb_opt_list *ent;

		for(i = 0; i < opt->arg.l.nr; i++) {
			ent = &opt->arg.l.p[i];
			if(*value == ent->i) {
				if(ent->str[0] != '\0')
					printk(KERN_INFO "%s\n", ent->str);
				return 0;
			}
		}
	}
		break;
	default:
		BUG();
	}

	printk(KERN_INFO "Invalid %s specified (%i) %s\n",
		   opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

#define LIST_LEN(l) (sizeof(l) / sizeof(l[0]))

/**
 * ixgb_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 **/

void __devinit
ixgb_check_options(struct ixgb_adapter *adapter)
{
	int bd = adapter->bd_number;
	if(bd >= IXGB_MAX_NIC) {
		printk(KERN_NOTICE
			   "Warning: no configuration for board #%i\n", bd);
		printk(KERN_NOTICE "Using defaults for all values\n");
	}

	{ /* Transmit Descriptor Count */
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Transmit Descriptors",
			.err  = "using default of " __MODULE_STRING(DEFAULT_TXD),
			.def  = DEFAULT_TXD,
			.arg  = { .r = { .min = MIN_TXD,
					 .max = MAX_TXD}}
		};
		struct ixgb_desc_ring *tx_ring = &adapter->tx_ring;

		if(num_TxDescriptors > bd) {
			tx_ring->count = TxDescriptors[bd];
			ixgb_validate_option(&tx_ring->count, &opt);
		} else {
			tx_ring->count = opt.def;
		}
		IXGB_ROUNDUP(tx_ring->count, IXGB_REQ_TX_DESCRIPTOR_MULTIPLE);
	}
	{ /* Receive Descriptor Count */
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Receive Descriptors",
			.err  = "using default of " __MODULE_STRING(DEFAULT_RXD),
			.def  = DEFAULT_RXD,
			.arg  = { .r = { .min = MIN_RXD,
					 .max = MAX_RXD}}
		};
		struct ixgb_desc_ring *rx_ring = &adapter->rx_ring;

		if(num_RxDescriptors > bd) {
			rx_ring->count = RxDescriptors[bd];
			ixgb_validate_option(&rx_ring->count, &opt);
		} else {
			rx_ring->count = opt.def;
		}
		IXGB_ROUNDUP(rx_ring->count, IXGB_REQ_RX_DESCRIPTOR_MULTIPLE);
	}
	{ /* Receive Checksum Offload Enable */
		struct ixgb_option opt = {
			.type = enable_option,
			.name = "Receive Checksum Offload",
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED
		};

		if(num_XsumRX > bd) {
			int rx_csum = XsumRX[bd];
			ixgb_validate_option(&rx_csum, &opt);
			adapter->rx_csum = rx_csum;
		} else {
			adapter->rx_csum = opt.def;
		}
	}
	{ /* Flow Control */

		struct ixgb_opt_list fc_list[] =
			{{ ixgb_fc_none,	"Flow Control Disabled" },
			 { ixgb_fc_rx_pause,"Flow Control Receive Only" },
			 { ixgb_fc_tx_pause,"Flow Control Transmit Only" },
			 { ixgb_fc_full,	"Flow Control Enabled" },
			 { ixgb_fc_default, "Flow Control Hardware Default" }};

		struct ixgb_option opt = {
			.type = list_option,
			.name = "Flow Control",
			.err  = "reading default settings from EEPROM",
			.def  = ixgb_fc_tx_pause,
			.arg  = { .l = { .nr = LIST_LEN(fc_list),
					 .p = fc_list }}
		};

		if(num_FlowControl > bd) {
			int fc = FlowControl[bd];
			ixgb_validate_option(&fc, &opt);
			adapter->hw.fc.type = fc;
		} else {
			adapter->hw.fc.type = opt.def;
		}
	}
	{ /* Receive Flow Control High Threshold */
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Rx Flow Control High Threshold",
			.err  = "using default of " __MODULE_STRING(DEFAULT_FCRTH),
			.def  = DEFAULT_FCRTH,
			.arg  = { .r = { .min = MIN_FCRTH,
					 .max = MAX_FCRTH}}
		};

		if(num_RxFCHighThresh > bd) {
			adapter->hw.fc.high_water = RxFCHighThresh[bd];
			ixgb_validate_option(&adapter->hw.fc.high_water, &opt);
		} else {
			adapter->hw.fc.high_water = opt.def;
		}
		if (!(adapter->hw.fc.type & ixgb_fc_tx_pause) )
			printk (KERN_INFO
				"Ignoring RxFCHighThresh when no RxFC\n");
	}
	{ /* Receive Flow Control Low Threshold */
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Rx Flow Control Low Threshold",
			.err  = "using default of " __MODULE_STRING(DEFAULT_FCRTL),
			.def  = DEFAULT_FCRTL,
			.arg  = { .r = { .min = MIN_FCRTL,
					 .max = MAX_FCRTL}}
		};

		if(num_RxFCLowThresh > bd) {
			adapter->hw.fc.low_water = RxFCLowThresh[bd];
			ixgb_validate_option(&adapter->hw.fc.low_water, &opt);
		} else {
			adapter->hw.fc.low_water = opt.def;
		}
		if (!(adapter->hw.fc.type & ixgb_fc_tx_pause) )
			printk (KERN_INFO
				"Ignoring RxFCLowThresh when no RxFC\n");
	}
	{ /* Flow Control Pause Time Request*/
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Flow Control Pause Time Request",
			.err  = "using default of "__MODULE_STRING(DEFAULT_FCPAUSE),
			.def  = DEFAULT_FCPAUSE,
			.arg = { .r = { .min = MIN_FCPAUSE,
					.max = MAX_FCPAUSE}}
		};

		if(num_FCReqTimeout > bd) {
			int pause_time = FCReqTimeout[bd];
			ixgb_validate_option(&pause_time, &opt);
			adapter->hw.fc.pause_time = pause_time;
		} else {
			adapter->hw.fc.pause_time = opt.def;
		}
		if (!(adapter->hw.fc.type & ixgb_fc_tx_pause) )
			printk (KERN_INFO
				"Ignoring FCReqTimeout when no RxFC\n");
	}
	/* high low and spacing check for rx flow control thresholds */
	if (adapter->hw.fc.type & ixgb_fc_tx_pause) {
		/* high must be greater than low */
		if (adapter->hw.fc.high_water < (adapter->hw.fc.low_water + 8)) {
			/* set defaults */
			printk (KERN_INFO 
				"RxFCHighThresh must be >= (RxFCLowThresh + 8), "
				"Using Defaults\n");
			adapter->hw.fc.high_water = DEFAULT_FCRTH;
			adapter->hw.fc.low_water  = DEFAULT_FCRTL;
		}
	}
	{ /* Receive Interrupt Delay */
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Receive Interrupt Delay",
			.err  = "using default of " __MODULE_STRING(DEFAULT_RDTR),
			.def  = DEFAULT_RDTR,
			.arg  = { .r = { .min = MIN_RDTR,
					 .max = MAX_RDTR}}
		};

		if(num_RxIntDelay > bd) {
			adapter->rx_int_delay = RxIntDelay[bd];
			ixgb_validate_option(&adapter->rx_int_delay, &opt);
		} else {
			adapter->rx_int_delay = opt.def;
		}
	}
	{ /* Transmit Interrupt Delay */
		struct ixgb_option opt = {
			.type = range_option,
			.name = "Transmit Interrupt Delay",
			.err  = "using default of " __MODULE_STRING(DEFAULT_TIDV),
			.def  = DEFAULT_TIDV,
			.arg  = { .r = { .min = MIN_TIDV,
					 .max = MAX_TIDV}}
		};

		if(num_TxIntDelay > bd) {
			adapter->tx_int_delay = TxIntDelay[bd];
			ixgb_validate_option(&adapter->tx_int_delay, &opt);
		} else {
			adapter->tx_int_delay = opt.def;
		}
	}

	{ /* Transmit Interrupt Delay Enable */
		struct ixgb_option opt = {
			.type = enable_option,
			.name = "Tx Interrupt Delay Enable",
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED
		};

		if(num_IntDelayEnable > bd) {
			int ide = IntDelayEnable[bd];
			ixgb_validate_option(&ide, &opt);
			adapter->tx_int_delay_enable = ide;
		} else {
			adapter->tx_int_delay_enable = opt.def;
		}
	}
}
