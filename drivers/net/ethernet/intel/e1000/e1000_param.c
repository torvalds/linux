// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2006 Intel Corporation. */

#include "e1000.h"

/* This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */

#define E1000_MAX_NIC 32

#define OPTION_UNSET   -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

/* All parameters are treated the same, as an integer array of values.
 * This macro just reduces the need to repeat the same declaration code
 * over and over (plus this helps to avoid typo bugs).
 */

#define E1000_PARAM_INIT { [0 ... E1000_MAX_NIC] = OPTION_UNSET }
#define E1000_PARAM(X, desc) \
	static int X[E1000_MAX_NIC+1] = E1000_PARAM_INIT; \
	static unsigned int num_##X; \
	module_param_array_named(X, X, int, &num_##X, 0); \
	MODULE_PARM_DESC(X, desc);

/* Transmit Descriptor Count
 *
 * Valid Range: 80-256 for 82542 and 82543 gigabit ethernet controllers
 * Valid Range: 80-4096 for 82544 and newer
 *
 * Default Value: 256
 */
E1000_PARAM(TxDescriptors, "Number of transmit descriptors");

/* Receive Descriptor Count
 *
 * Valid Range: 80-256 for 82542 and 82543 gigabit ethernet controllers
 * Valid Range: 80-4096 for 82544 and newer
 *
 * Default Value: 256
 */
E1000_PARAM(RxDescriptors, "Number of receive descriptors");

/* User Specified Speed Override
 *
 * Valid Range: 0, 10, 100, 1000
 *  - 0    - auto-negotiate at all supported speeds
 *  - 10   - only link at 10 Mbps
 *  - 100  - only link at 100 Mbps
 *  - 1000 - only link at 1000 Mbps
 *
 * Default Value: 0
 */
E1000_PARAM(Speed, "Speed setting");

/* User Specified Duplex Override
 *
 * Valid Range: 0-2
 *  - 0 - auto-negotiate for duplex
 *  - 1 - only link at half duplex
 *  - 2 - only link at full duplex
 *
 * Default Value: 0
 */
E1000_PARAM(Duplex, "Duplex setting");

/* Auto-negotiation Advertisement Override
 *
 * Valid Range: 0x01-0x0F, 0x20-0x2F (copper); 0x20 (fiber)
 *
 * The AutoNeg value is a bit mask describing which speed and duplex
 * combinations should be advertised during auto-negotiation.
 * The supported speed and duplex modes are listed below
 *
 * Bit           7     6     5      4      3     2     1      0
 * Speed (Mbps)  N/A   N/A   1000   N/A    100   100   10     10
 * Duplex                    Full          Full  Half  Full   Half
 *
 * Default Value: 0x2F (copper); 0x20 (fiber)
 */
E1000_PARAM(AutoNeg, "Advertised auto-negotiation setting");
#define AUTONEG_ADV_DEFAULT  0x2F
#define AUTONEG_ADV_MASK     0x2F

/* User Specified Flow Control Override
 *
 * Valid Range: 0-3
 *  - 0 - No Flow Control
 *  - 1 - Rx only, respond to PAUSE frames but do not generate them
 *  - 2 - Tx only, generate PAUSE frames but ignore them on receive
 *  - 3 - Full Flow Control Support
 *
 * Default Value: Read flow control settings from the EEPROM
 */
E1000_PARAM(FlowControl, "Flow Control setting");
#define FLOW_CONTROL_DEFAULT FLOW_CONTROL_FULL

/* XsumRX - Receive Checksum Offload Enable/Disable
 *
 * Valid Range: 0, 1
 *  - 0 - disables all checksum offload
 *  - 1 - enables receive IP/TCP/UDP checksum offload
 *        on 82543 and newer -based NICs
 *
 * Default Value: 1
 */
E1000_PARAM(XsumRX, "Disable or enable Receive Checksum offload");

/* Transmit Interrupt Delay in units of 1.024 microseconds
 *  Tx interrupt delay needs to typically be set to something non zero
 *
 * Valid Range: 0-65535
 */
E1000_PARAM(TxIntDelay, "Transmit Interrupt Delay");
#define DEFAULT_TIDV                   8
#define MAX_TXDELAY               0xFFFF
#define MIN_TXDELAY                    0

/* Transmit Absolute Interrupt Delay in units of 1.024 microseconds
 *
 * Valid Range: 0-65535
 */
E1000_PARAM(TxAbsIntDelay, "Transmit Absolute Interrupt Delay");
#define DEFAULT_TADV                  32
#define MAX_TXABSDELAY            0xFFFF
#define MIN_TXABSDELAY                 0

/* Receive Interrupt Delay in units of 1.024 microseconds
 *   hardware will likely hang if you set this to anything but zero.
 *
 * Valid Range: 0-65535
 */
E1000_PARAM(RxIntDelay, "Receive Interrupt Delay");
#define DEFAULT_RDTR                   0
#define MAX_RXDELAY               0xFFFF
#define MIN_RXDELAY                    0

/* Receive Absolute Interrupt Delay in units of 1.024 microseconds
 *
 * Valid Range: 0-65535
 */
E1000_PARAM(RxAbsIntDelay, "Receive Absolute Interrupt Delay");
#define DEFAULT_RADV                   8
#define MAX_RXABSDELAY            0xFFFF
#define MIN_RXABSDELAY                 0

/* Interrupt Throttle Rate (interrupts/sec)
 *
 * Valid Range: 100-100000 (0=off, 1=dynamic, 3=dynamic conservative)
 */
E1000_PARAM(InterruptThrottleRate, "Interrupt Throttling Rate");
#define DEFAULT_ITR                    3
#define MAX_ITR                   100000
#define MIN_ITR                      100

/* Enable Smart Power Down of the PHY
 *
 * Valid Range: 0, 1
 *
 * Default Value: 0 (disabled)
 */
E1000_PARAM(SmartPowerDownEnable, "Enable PHY smart power down");

struct e1000_option {
	enum { enable_option, range_option, list_option } type;
	const char *name;
	const char *err;
	int def;
	union {
		struct { /* range_option info */
			int min;
			int max;
		} r;
		struct { /* list_option info */
			int nr;
			const struct e1000_opt_list { int i; char *str; } *p;
		} l;
	} arg;
};

static int e1000_validate_option(unsigned int *value,
				 const struct e1000_option *opt,
				 struct e1000_adapter *adapter)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			e_dev_info("%s Enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			e_dev_info("%s Disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			e_dev_info("%s set to %i\n", opt->name, *value);
			return 0;
		}
		break;
	case list_option: {
		int i;
		const struct e1000_opt_list *ent;

		for (i = 0; i < opt->arg.l.nr; i++) {
			ent = &opt->arg.l.p[i];
			if (*value == ent->i) {
				if (ent->str[0] != '\0')
					e_dev_info("%s\n", ent->str);
				return 0;
			}
		}
	}
		break;
	default:
		BUG();
	}

	e_dev_info("Invalid %s value specified (%i) %s\n",
	       opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

static void e1000_check_fiber_options(struct e1000_adapter *adapter);
static void e1000_check_copper_options(struct e1000_adapter *adapter);

/**
 * e1000_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line parameters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 **/
void e1000_check_options(struct e1000_adapter *adapter)
{
	struct e1000_option opt;
	int bd = adapter->bd_number;

	if (bd >= E1000_MAX_NIC) {
		e_dev_warn("Warning: no configuration for board #%i "
			   "using defaults for all values\n", bd);
	}

	{ /* Transmit Descriptor Count */
		struct e1000_tx_ring *tx_ring = adapter->tx_ring;
		int i;
		e1000_mac_type mac_type = adapter->hw.mac_type;

		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Transmit Descriptors",
			.err  = "using default of "
				__MODULE_STRING(E1000_DEFAULT_TXD),
			.def  = E1000_DEFAULT_TXD,
			.arg  = { .r = {
				.min = E1000_MIN_TXD,
				.max = mac_type < e1000_82544 ? E1000_MAX_TXD : E1000_MAX_82544_TXD
				}}
		};

		if (num_TxDescriptors > bd) {
			tx_ring->count = TxDescriptors[bd];
			e1000_validate_option(&tx_ring->count, &opt, adapter);
			tx_ring->count = ALIGN(tx_ring->count,
						REQ_TX_DESCRIPTOR_MULTIPLE);
		} else {
			tx_ring->count = opt.def;
		}
		for (i = 0; i < adapter->num_tx_queues; i++)
			tx_ring[i].count = tx_ring->count;
	}
	{ /* Receive Descriptor Count */
		struct e1000_rx_ring *rx_ring = adapter->rx_ring;
		int i;
		e1000_mac_type mac_type = adapter->hw.mac_type;

		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Receive Descriptors",
			.err  = "using default of "
				__MODULE_STRING(E1000_DEFAULT_RXD),
			.def  = E1000_DEFAULT_RXD,
			.arg  = { .r = {
				.min = E1000_MIN_RXD,
				.max = mac_type < e1000_82544 ? E1000_MAX_RXD :
				       E1000_MAX_82544_RXD
			}}
		};

		if (num_RxDescriptors > bd) {
			rx_ring->count = RxDescriptors[bd];
			e1000_validate_option(&rx_ring->count, &opt, adapter);
			rx_ring->count = ALIGN(rx_ring->count,
						REQ_RX_DESCRIPTOR_MULTIPLE);
		} else {
			rx_ring->count = opt.def;
		}
		for (i = 0; i < adapter->num_rx_queues; i++)
			rx_ring[i].count = rx_ring->count;
	}
	{ /* Checksum Offload Enable/Disable */
		opt = (struct e1000_option) {
			.type = enable_option,
			.name = "Checksum Offload",
			.err  = "defaulting to Enabled",
			.def  = OPTION_ENABLED
		};

		if (num_XsumRX > bd) {
			unsigned int rx_csum = XsumRX[bd];
			e1000_validate_option(&rx_csum, &opt, adapter);
			adapter->rx_csum = rx_csum;
		} else {
			adapter->rx_csum = opt.def;
		}
	}
	{ /* Flow Control */

		static const struct e1000_opt_list fc_list[] = {
		       { E1000_FC_NONE, "Flow Control Disabled" },
		       { E1000_FC_RX_PAUSE, "Flow Control Receive Only" },
		       { E1000_FC_TX_PAUSE, "Flow Control Transmit Only" },
		       { E1000_FC_FULL, "Flow Control Enabled" },
		       { E1000_FC_DEFAULT, "Flow Control Hardware Default" }
		};

		opt = (struct e1000_option) {
			.type = list_option,
			.name = "Flow Control",
			.err  = "reading default settings from EEPROM",
			.def  = E1000_FC_DEFAULT,
			.arg  = { .l = { .nr = ARRAY_SIZE(fc_list),
					 .p = fc_list }}
		};

		if (num_FlowControl > bd) {
			unsigned int fc = FlowControl[bd];
			e1000_validate_option(&fc, &opt, adapter);
			adapter->hw.fc = adapter->hw.original_fc = fc;
		} else {
			adapter->hw.fc = adapter->hw.original_fc = opt.def;
		}
	}
	{ /* Transmit Interrupt Delay */
		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Transmit Interrupt Delay",
			.err  = "using default of " __MODULE_STRING(DEFAULT_TIDV),
			.def  = DEFAULT_TIDV,
			.arg  = { .r = { .min = MIN_TXDELAY,
					 .max = MAX_TXDELAY }}
		};

		if (num_TxIntDelay > bd) {
			adapter->tx_int_delay = TxIntDelay[bd];
			e1000_validate_option(&adapter->tx_int_delay, &opt,
			                      adapter);
		} else {
			adapter->tx_int_delay = opt.def;
		}
	}
	{ /* Transmit Absolute Interrupt Delay */
		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Transmit Absolute Interrupt Delay",
			.err  = "using default of " __MODULE_STRING(DEFAULT_TADV),
			.def  = DEFAULT_TADV,
			.arg  = { .r = { .min = MIN_TXABSDELAY,
					 .max = MAX_TXABSDELAY }}
		};

		if (num_TxAbsIntDelay > bd) {
			adapter->tx_abs_int_delay = TxAbsIntDelay[bd];
			e1000_validate_option(&adapter->tx_abs_int_delay, &opt,
					      adapter);
		} else {
			adapter->tx_abs_int_delay = opt.def;
		}
	}
	{ /* Receive Interrupt Delay */
		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Receive Interrupt Delay",
			.err  = "using default of " __MODULE_STRING(DEFAULT_RDTR),
			.def  = DEFAULT_RDTR,
			.arg  = { .r = { .min = MIN_RXDELAY,
					 .max = MAX_RXDELAY }}
		};

		if (num_RxIntDelay > bd) {
			adapter->rx_int_delay = RxIntDelay[bd];
			e1000_validate_option(&adapter->rx_int_delay, &opt,
					      adapter);
		} else {
			adapter->rx_int_delay = opt.def;
		}
	}
	{ /* Receive Absolute Interrupt Delay */
		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Receive Absolute Interrupt Delay",
			.err  = "using default of " __MODULE_STRING(DEFAULT_RADV),
			.def  = DEFAULT_RADV,
			.arg  = { .r = { .min = MIN_RXABSDELAY,
					 .max = MAX_RXABSDELAY }}
		};

		if (num_RxAbsIntDelay > bd) {
			adapter->rx_abs_int_delay = RxAbsIntDelay[bd];
			e1000_validate_option(&adapter->rx_abs_int_delay, &opt,
					      adapter);
		} else {
			adapter->rx_abs_int_delay = opt.def;
		}
	}
	{ /* Interrupt Throttling Rate */
		opt = (struct e1000_option) {
			.type = range_option,
			.name = "Interrupt Throttling Rate (ints/sec)",
			.err  = "using default of " __MODULE_STRING(DEFAULT_ITR),
			.def  = DEFAULT_ITR,
			.arg  = { .r = { .min = MIN_ITR,
					 .max = MAX_ITR }}
		};

		if (num_InterruptThrottleRate > bd) {
			adapter->itr = InterruptThrottleRate[bd];
			switch (adapter->itr) {
			case 0:
				e_dev_info("%s turned off\n", opt.name);
				break;
			case 1:
				e_dev_info("%s set to dynamic mode\n",
					   opt.name);
				adapter->itr_setting = adapter->itr;
				adapter->itr = 20000;
				break;
			case 3:
				e_dev_info("%s set to dynamic conservative "
					   "mode\n", opt.name);
				adapter->itr_setting = adapter->itr;
				adapter->itr = 20000;
				break;
			case 4:
				e_dev_info("%s set to simplified "
					   "(2000-8000) ints mode\n", opt.name);
				adapter->itr_setting = adapter->itr;
				break;
			default:
				e1000_validate_option(&adapter->itr, &opt,
						      adapter);
				/* save the setting, because the dynamic bits
				 * change itr.
				 * clear the lower two bits because they are
				 * used as control
				 */
				adapter->itr_setting = adapter->itr & ~3;
				break;
			}
		} else {
			adapter->itr_setting = opt.def;
			adapter->itr = 20000;
		}
	}
	{ /* Smart Power Down */
		opt = (struct e1000_option) {
			.type = enable_option,
			.name = "PHY Smart Power Down",
			.err  = "defaulting to Disabled",
			.def  = OPTION_DISABLED
		};

		if (num_SmartPowerDownEnable > bd) {
			unsigned int spd = SmartPowerDownEnable[bd];
			e1000_validate_option(&spd, &opt, adapter);
			adapter->smart_power_down = spd;
		} else {
			adapter->smart_power_down = opt.def;
		}
	}

	switch (adapter->hw.media_type) {
	case e1000_media_type_fiber:
	case e1000_media_type_internal_serdes:
		e1000_check_fiber_options(adapter);
		break;
	case e1000_media_type_copper:
		e1000_check_copper_options(adapter);
		break;
	default:
		BUG();
	}
}

/**
 * e1000_check_fiber_options - Range Checking for Link Options, Fiber Version
 * @adapter: board private structure
 *
 * Handles speed and duplex options on fiber adapters
 **/
static void e1000_check_fiber_options(struct e1000_adapter *adapter)
{
	int bd = adapter->bd_number;
	if (num_Speed > bd) {
		e_dev_info("Speed not valid for fiber adapters, parameter "
			   "ignored\n");
	}

	if (num_Duplex > bd) {
		e_dev_info("Duplex not valid for fiber adapters, parameter "
			   "ignored\n");
	}

	if ((num_AutoNeg > bd) && (AutoNeg[bd] != 0x20)) {
		e_dev_info("AutoNeg other than 1000/Full is not valid for fiber"
			   "adapters, parameter ignored\n");
	}
}

/**
 * e1000_check_copper_options - Range Checking for Link Options, Copper Version
 * @adapter: board private structure
 *
 * Handles speed and duplex options on copper adapters
 **/
static void e1000_check_copper_options(struct e1000_adapter *adapter)
{
	struct e1000_option opt;
	unsigned int speed, dplx, an;
	int bd = adapter->bd_number;

	{ /* Speed */
		static const struct e1000_opt_list speed_list[] = {
			{          0, "" },
			{   SPEED_10, "" },
			{  SPEED_100, "" },
			{ SPEED_1000, "" }};

		opt = (struct e1000_option) {
			.type = list_option,
			.name = "Speed",
			.err  = "parameter ignored",
			.def  = 0,
			.arg  = { .l = { .nr = ARRAY_SIZE(speed_list),
					 .p = speed_list }}
		};

		if (num_Speed > bd) {
			speed = Speed[bd];
			e1000_validate_option(&speed, &opt, adapter);
		} else {
			speed = opt.def;
		}
	}
	{ /* Duplex */
		static const struct e1000_opt_list dplx_list[] = {
			{           0, "" },
			{ HALF_DUPLEX, "" },
			{ FULL_DUPLEX, "" }};

		opt = (struct e1000_option) {
			.type = list_option,
			.name = "Duplex",
			.err  = "parameter ignored",
			.def  = 0,
			.arg  = { .l = { .nr = ARRAY_SIZE(dplx_list),
					 .p = dplx_list }}
		};

		if (num_Duplex > bd) {
			dplx = Duplex[bd];
			e1000_validate_option(&dplx, &opt, adapter);
		} else {
			dplx = opt.def;
		}
	}

	if ((num_AutoNeg > bd) && (speed != 0 || dplx != 0)) {
		e_dev_info("AutoNeg specified along with Speed or Duplex, "
			   "parameter ignored\n");
		adapter->hw.autoneg_advertised = AUTONEG_ADV_DEFAULT;
	} else { /* Autoneg */
		static const struct e1000_opt_list an_list[] =
			#define AA "AutoNeg advertising "
			{{ 0x01, AA "10/HD" },
			 { 0x02, AA "10/FD" },
			 { 0x03, AA "10/FD, 10/HD" },
			 { 0x04, AA "100/HD" },
			 { 0x05, AA "100/HD, 10/HD" },
			 { 0x06, AA "100/HD, 10/FD" },
			 { 0x07, AA "100/HD, 10/FD, 10/HD" },
			 { 0x08, AA "100/FD" },
			 { 0x09, AA "100/FD, 10/HD" },
			 { 0x0a, AA "100/FD, 10/FD" },
			 { 0x0b, AA "100/FD, 10/FD, 10/HD" },
			 { 0x0c, AA "100/FD, 100/HD" },
			 { 0x0d, AA "100/FD, 100/HD, 10/HD" },
			 { 0x0e, AA "100/FD, 100/HD, 10/FD" },
			 { 0x0f, AA "100/FD, 100/HD, 10/FD, 10/HD" },
			 { 0x20, AA "1000/FD" },
			 { 0x21, AA "1000/FD, 10/HD" },
			 { 0x22, AA "1000/FD, 10/FD" },
			 { 0x23, AA "1000/FD, 10/FD, 10/HD" },
			 { 0x24, AA "1000/FD, 100/HD" },
			 { 0x25, AA "1000/FD, 100/HD, 10/HD" },
			 { 0x26, AA "1000/FD, 100/HD, 10/FD" },
			 { 0x27, AA "1000/FD, 100/HD, 10/FD, 10/HD" },
			 { 0x28, AA "1000/FD, 100/FD" },
			 { 0x29, AA "1000/FD, 100/FD, 10/HD" },
			 { 0x2a, AA "1000/FD, 100/FD, 10/FD" },
			 { 0x2b, AA "1000/FD, 100/FD, 10/FD, 10/HD" },
			 { 0x2c, AA "1000/FD, 100/FD, 100/HD" },
			 { 0x2d, AA "1000/FD, 100/FD, 100/HD, 10/HD" },
			 { 0x2e, AA "1000/FD, 100/FD, 100/HD, 10/FD" },
			 { 0x2f, AA "1000/FD, 100/FD, 100/HD, 10/FD, 10/HD" }};

		opt = (struct e1000_option) {
			.type = list_option,
			.name = "AutoNeg",
			.err  = "parameter ignored",
			.def  = AUTONEG_ADV_DEFAULT,
			.arg  = { .l = { .nr = ARRAY_SIZE(an_list),
					 .p = an_list }}
		};

		if (num_AutoNeg > bd) {
			an = AutoNeg[bd];
			e1000_validate_option(&an, &opt, adapter);
		} else {
			an = opt.def;
		}
		adapter->hw.autoneg_advertised = an;
	}

	switch (speed + dplx) {
	case 0:
		adapter->hw.autoneg = adapter->fc_autoneg = 1;
		if ((num_Speed > bd) && (speed != 0 || dplx != 0))
			e_dev_info("Speed and duplex autonegotiation "
				   "enabled\n");
		break;
	case HALF_DUPLEX:
		e_dev_info("Half Duplex specified without Speed\n");
		e_dev_info("Using Autonegotiation at Half Duplex only\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_10_HALF |
						 ADVERTISE_100_HALF;
		break;
	case FULL_DUPLEX:
		e_dev_info("Full Duplex specified without Speed\n");
		e_dev_info("Using Autonegotiation at Full Duplex only\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_10_FULL |
						 ADVERTISE_100_FULL |
						 ADVERTISE_1000_FULL;
		break;
	case SPEED_10:
		e_dev_info("10 Mbps Speed specified without Duplex\n");
		e_dev_info("Using Autonegotiation at 10 Mbps only\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_10_HALF |
						 ADVERTISE_10_FULL;
		break;
	case SPEED_10 + HALF_DUPLEX:
		e_dev_info("Forcing to 10 Mbps Half Duplex\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_10_half;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_10 + FULL_DUPLEX:
		e_dev_info("Forcing to 10 Mbps Full Duplex\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_10_full;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_100:
		e_dev_info("100 Mbps Speed specified without Duplex\n");
		e_dev_info("Using Autonegotiation at 100 Mbps only\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_100_HALF |
						 ADVERTISE_100_FULL;
		break;
	case SPEED_100 + HALF_DUPLEX:
		e_dev_info("Forcing to 100 Mbps Half Duplex\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_100_half;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_100 + FULL_DUPLEX:
		e_dev_info("Forcing to 100 Mbps Full Duplex\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_100_full;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_1000:
		e_dev_info("1000 Mbps Speed specified without Duplex\n");
		goto full_duplex_only;
	case SPEED_1000 + HALF_DUPLEX:
		e_dev_info("Half Duplex is not supported at 1000 Mbps\n");
		/* fall through */
	case SPEED_1000 + FULL_DUPLEX:
full_duplex_only:
		e_dev_info("Using Autonegotiation at 1000 Mbps Full Duplex "
			   "only\n");
		adapter->hw.autoneg = adapter->fc_autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	default:
		BUG();
	}

	/* Speed, AutoNeg and MDI/MDI-X must all play nice */
	if (e1000_validate_mdi_setting(&(adapter->hw)) < 0) {
		e_dev_info("Speed, AutoNeg and MDI-X specs are incompatible. "
			   "Setting MDI-X to a compatible value.\n");
	}
}

