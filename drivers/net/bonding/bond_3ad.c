/*
 * Copyright(c) 1999 - 2004 Intel Corporation. All rights reserved.
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
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */

//#define BONDING_DEBUG 1

#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/if_bonding.h>
#include <linux/pkt_sched.h>
#include <net/net_namespace.h>
#include "bonding.h"
#include "bond_3ad.h"

// General definitions
#define AD_SHORT_TIMEOUT           1
#define AD_LONG_TIMEOUT            0
#define AD_STANDBY                 0x2
#define AD_MAX_TX_IN_SECOND        3
#define AD_COLLECTOR_MAX_DELAY     0

// Timer definitions(43.4.4 in the 802.3ad standard)
#define AD_FAST_PERIODIC_TIME      1
#define AD_SLOW_PERIODIC_TIME      30
#define AD_SHORT_TIMEOUT_TIME      (3*AD_FAST_PERIODIC_TIME)
#define AD_LONG_TIMEOUT_TIME       (3*AD_SLOW_PERIODIC_TIME)
#define AD_CHURN_DETECTION_TIME    60
#define AD_AGGREGATE_WAIT_TIME     2

// Port state definitions(43.4.2.2 in the 802.3ad standard)
#define AD_STATE_LACP_ACTIVITY   0x1
#define AD_STATE_LACP_TIMEOUT    0x2
#define AD_STATE_AGGREGATION     0x4
#define AD_STATE_SYNCHRONIZATION 0x8
#define AD_STATE_COLLECTING      0x10
#define AD_STATE_DISTRIBUTING    0x20
#define AD_STATE_DEFAULTED       0x40
#define AD_STATE_EXPIRED         0x80

// Port Variables definitions used by the State Machines(43.4.7 in the 802.3ad standard)
#define AD_PORT_BEGIN           0x1
#define AD_PORT_LACP_ENABLED    0x2
#define AD_PORT_ACTOR_CHURN     0x4
#define AD_PORT_PARTNER_CHURN   0x8
#define AD_PORT_READY           0x10
#define AD_PORT_READY_N         0x20
#define AD_PORT_MATCHED         0x40
#define AD_PORT_STANDBY         0x80
#define AD_PORT_SELECTED        0x100
#define AD_PORT_MOVED           0x200

// Port Key definitions
// key is determined according to the link speed, duplex and
// user key(which is yet not supported)
//              ------------------------------------------------------------
// Port key :   | User key                       |      Speed       |Duplex|
//              ------------------------------------------------------------
//              16                               6               1 0
#define  AD_DUPLEX_KEY_BITS    0x1
#define  AD_SPEED_KEY_BITS     0x3E
#define  AD_USER_KEY_BITS      0xFFC0

//dalloun
#define     AD_LINK_SPEED_BITMASK_1MBPS       0x1
#define     AD_LINK_SPEED_BITMASK_10MBPS      0x2
#define     AD_LINK_SPEED_BITMASK_100MBPS     0x4
#define     AD_LINK_SPEED_BITMASK_1000MBPS    0x8
#define     AD_LINK_SPEED_BITMASK_10000MBPS   0x10
//endalloun

// compare MAC addresses
#define MAC_ADDRESS_COMPARE(A, B) memcmp(A, B, ETH_ALEN)

static struct mac_addr null_mac_addr = {{0, 0, 0, 0, 0, 0}};
static u16 ad_ticks_per_sec;
static const int ad_delta_in_ticks = (AD_TIMER_INTERVAL * HZ) / 1000;

// ================= 3AD api to bonding and kernel code ==================
static u16 __get_link_speed(struct port *port);
static u8 __get_duplex(struct port *port);
static inline void __initialize_port_locks(struct port *port);
//conversions
static u16 __ad_timer_to_ticks(u16 timer_type, u16 Par);


// ================= ad code helper functions ==================
//needed by ad_rx_machine(...)
static void __record_pdu(struct lacpdu *lacpdu, struct port *port);
static void __record_default(struct port *port);
static void __update_selected(struct lacpdu *lacpdu, struct port *port);
static void __update_default_selected(struct port *port);
static void __choose_matched(struct lacpdu *lacpdu, struct port *port);
static void __update_ntt(struct lacpdu *lacpdu, struct port *port);

//needed for ad_mux_machine(..)
static void __attach_bond_to_agg(struct port *port);
static void __detach_bond_from_agg(struct port *port);
static int __agg_ports_are_ready(struct aggregator *aggregator);
static void __set_agg_ports_ready(struct aggregator *aggregator, int val);

//needed for ad_agg_selection_logic(...)
static u32 __get_agg_bandwidth(struct aggregator *aggregator);
static struct aggregator *__get_active_agg(struct aggregator *aggregator);


// ================= main 802.3ad protocol functions ==================
static int ad_lacpdu_send(struct port *port);
static int ad_marker_send(struct port *port, struct bond_marker *marker);
static void ad_mux_machine(struct port *port);
static void ad_rx_machine(struct lacpdu *lacpdu, struct port *port);
static void ad_tx_machine(struct port *port);
static void ad_periodic_machine(struct port *port);
static void ad_port_selection_logic(struct port *port);
static void ad_agg_selection_logic(struct aggregator *aggregator);
static void ad_clear_agg(struct aggregator *aggregator);
static void ad_initialize_agg(struct aggregator *aggregator);
static void ad_initialize_port(struct port *port, int lacp_fast);
static void ad_initialize_lacpdu(struct lacpdu *Lacpdu);
static void ad_enable_collecting_distributing(struct port *port);
static void ad_disable_collecting_distributing(struct port *port);
static void ad_marker_info_received(struct bond_marker *marker_info, struct port *port);
static void ad_marker_response_received(struct bond_marker *marker, struct port *port);


/////////////////////////////////////////////////////////////////////////////////
// ================= api to bonding and kernel code ==================
/////////////////////////////////////////////////////////////////////////////////

/**
 * __get_bond_by_port - get the port's bonding struct
 * @port: the port we're looking at
 *
 * Return @port's bonding struct, or %NULL if it can't be found.
 */
static inline struct bonding *__get_bond_by_port(struct port *port)
{
	if (port->slave == NULL) {
		return NULL;
	}

	return bond_get_bond_by_slave(port->slave);
}

/**
 * __get_first_port - get the first port in the bond
 * @bond: the bond we're looking at
 *
 * Return the port of the first slave in @bond, or %NULL if it can't be found.
 */
static inline struct port *__get_first_port(struct bonding *bond)
{
	if (bond->slave_cnt == 0) {
		return NULL;
	}

	return &(SLAVE_AD_INFO(bond->first_slave).port);
}

/**
 * __get_next_port - get the next port in the bond
 * @port: the port we're looking at
 *
 * Return the port of the slave that is next in line of @port's slave in the
 * bond, or %NULL if it can't be found.
 */
static inline struct port *__get_next_port(struct port *port)
{
	struct bonding *bond = __get_bond_by_port(port);
	struct slave *slave = port->slave;

	// If there's no bond for this port, or this is the last slave
	if ((bond == NULL) || (slave->next == bond->first_slave)) {
		return NULL;
	}

	return &(SLAVE_AD_INFO(slave->next).port);
}

/**
 * __get_first_agg - get the first aggregator in the bond
 * @bond: the bond we're looking at
 *
 * Return the aggregator of the first slave in @bond, or %NULL if it can't be
 * found.
 */
static inline struct aggregator *__get_first_agg(struct port *port)
{
	struct bonding *bond = __get_bond_by_port(port);

	// If there's no bond for this port, or bond has no slaves
	if ((bond == NULL) || (bond->slave_cnt == 0)) {
		return NULL;
	}

	return &(SLAVE_AD_INFO(bond->first_slave).aggregator);
}

/**
 * __get_next_agg - get the next aggregator in the bond
 * @aggregator: the aggregator we're looking at
 *
 * Return the aggregator of the slave that is next in line of @aggregator's
 * slave in the bond, or %NULL if it can't be found.
 */
static inline struct aggregator *__get_next_agg(struct aggregator *aggregator)
{
	struct slave *slave = aggregator->slave;
	struct bonding *bond = bond_get_bond_by_slave(slave);

	// If there's no bond for this aggregator, or this is the last slave
	if ((bond == NULL) || (slave->next == bond->first_slave)) {
		return NULL;
	}

	return &(SLAVE_AD_INFO(slave->next).aggregator);
}

/**
 * __disable_port - disable the port's slave
 * @port: the port we're looking at
 *
 */
static inline void __disable_port(struct port *port)
{
	bond_set_slave_inactive_flags(port->slave);
}

/**
 * __enable_port - enable the port's slave, if it's up
 * @port: the port we're looking at
 *
 */
static inline void __enable_port(struct port *port)
{
	struct slave *slave = port->slave;

	if ((slave->link == BOND_LINK_UP) && IS_UP(slave->dev)) {
		bond_set_slave_active_flags(slave);
	}
}

/**
 * __port_is_enabled - check if the port's slave is in active state
 * @port: the port we're looking at
 *
 */
static inline int __port_is_enabled(struct port *port)
{
	return(port->slave->state == BOND_STATE_ACTIVE);
}

/**
 * __get_agg_selection_mode - get the aggregator selection mode
 * @port: the port we're looking at
 *
 * Get the aggregator selection mode. Can be %BANDWIDTH or %COUNT.
 */
static inline u32 __get_agg_selection_mode(struct port *port)
{
	struct bonding *bond = __get_bond_by_port(port);

	if (bond == NULL) {
		return AD_BANDWIDTH;
	}

	return BOND_AD_INFO(bond).agg_select_mode;
}

/**
 * __check_agg_selection_timer - check if the selection timer has expired
 * @port: the port we're looking at
 *
 */
static inline int __check_agg_selection_timer(struct port *port)
{
	struct bonding *bond = __get_bond_by_port(port);

	if (bond == NULL) {
		return 0;
	}

	return BOND_AD_INFO(bond).agg_select_timer ? 1 : 0;
}

/**
 * __get_rx_machine_lock - lock the port's RX machine
 * @port: the port we're looking at
 *
 */
static inline void __get_rx_machine_lock(struct port *port)
{
	spin_lock_bh(&(SLAVE_AD_INFO(port->slave).rx_machine_lock));
}

/**
 * __release_rx_machine_lock - unlock the port's RX machine
 * @port: the port we're looking at
 *
 */
static inline void __release_rx_machine_lock(struct port *port)
{
	spin_unlock_bh(&(SLAVE_AD_INFO(port->slave).rx_machine_lock));
}

/**
 * __get_link_speed - get a port's speed
 * @port: the port we're looking at
 *
 * Return @port's speed in 802.3ad bitmask format. i.e. one of:
 *     0,
 *     %AD_LINK_SPEED_BITMASK_10MBPS,
 *     %AD_LINK_SPEED_BITMASK_100MBPS,
 *     %AD_LINK_SPEED_BITMASK_1000MBPS,
 *     %AD_LINK_SPEED_BITMASK_10000MBPS
 */
static u16 __get_link_speed(struct port *port)
{
	struct slave *slave = port->slave;
	u16 speed;

	/* this if covers only a special case: when the configuration starts with
	 * link down, it sets the speed to 0.
	 * This is done in spite of the fact that the e100 driver reports 0 to be
	 * compatible with MVT in the future.*/
	if (slave->link != BOND_LINK_UP) {
		speed=0;
	} else {
		switch (slave->speed) {
		case SPEED_10:
			speed = AD_LINK_SPEED_BITMASK_10MBPS;
			break;

		case SPEED_100:
			speed = AD_LINK_SPEED_BITMASK_100MBPS;
			break;

		case SPEED_1000:
			speed = AD_LINK_SPEED_BITMASK_1000MBPS;
			break;

		case SPEED_10000:
			speed = AD_LINK_SPEED_BITMASK_10000MBPS;
			break;

		default:
			speed = 0; // unknown speed value from ethtool. shouldn't happen
			break;
		}
	}

	dprintk("Port %d Received link speed %d update from adapter\n", port->actor_port_number, speed);
	return speed;
}

/**
 * __get_duplex - get a port's duplex
 * @port: the port we're looking at
 *
 * Return @port's duplex in 802.3ad bitmask format. i.e.:
 *     0x01 if in full duplex
 *     0x00 otherwise
 */
static u8 __get_duplex(struct port *port)
{
	struct slave *slave = port->slave;

	u8 retval;

	//  handling a special case: when the configuration starts with
	// link down, it sets the duplex to 0.
	if (slave->link != BOND_LINK_UP) {
		retval=0x0;
	} else {
		switch (slave->duplex) {
		case DUPLEX_FULL:
			retval=0x1;
			dprintk("Port %d Received status full duplex update from adapter\n", port->actor_port_number);
			break;
		case DUPLEX_HALF:
		default:
			retval=0x0;
			dprintk("Port %d Received status NOT full duplex update from adapter\n", port->actor_port_number);
			break;
		}
	}
	return retval;
}

/**
 * __initialize_port_locks - initialize a port's RX machine spinlock
 * @port: the port we're looking at
 *
 */
static inline void __initialize_port_locks(struct port *port)
{
	// make sure it isn't called twice
	spin_lock_init(&(SLAVE_AD_INFO(port->slave).rx_machine_lock));
}

//conversions

/**
 * __ad_timer_to_ticks - convert a given timer type to AD module ticks
 * @timer_type:	which timer to operate
 * @par: timer parameter. see below
 *
 * If @timer_type is %current_while_timer, @par indicates long/short timer.
 * If @timer_type is %periodic_timer, @par is one of %FAST_PERIODIC_TIME,
 *						    %SLOW_PERIODIC_TIME.
 */
static u16 __ad_timer_to_ticks(u16 timer_type, u16 par)
{
	u16 retval=0;	 //to silence the compiler

	switch (timer_type) {
	case AD_CURRENT_WHILE_TIMER:   // for rx machine usage
		if (par) {	      // for short or long timeout
			retval = (AD_SHORT_TIMEOUT_TIME*ad_ticks_per_sec); // short timeout
		} else {
			retval = (AD_LONG_TIMEOUT_TIME*ad_ticks_per_sec); // long timeout
		}
		break;
	case AD_ACTOR_CHURN_TIMER:	    // for local churn machine
		retval = (AD_CHURN_DETECTION_TIME*ad_ticks_per_sec);
		break;
	case AD_PERIODIC_TIMER:	    // for periodic machine
		retval = (par*ad_ticks_per_sec); // long timeout
		break;
	case AD_PARTNER_CHURN_TIMER:   // for remote churn machine
		retval = (AD_CHURN_DETECTION_TIME*ad_ticks_per_sec);
		break;
	case AD_WAIT_WHILE_TIMER:	    // for selection machine
		retval = (AD_AGGREGATE_WAIT_TIME*ad_ticks_per_sec);
		break;
	}
	return retval;
}


/////////////////////////////////////////////////////////////////////////////////
// ================= ad_rx_machine helper functions ==================
/////////////////////////////////////////////////////////////////////////////////

/**
 * __record_pdu - record parameters from a received lacpdu
 * @lacpdu: the lacpdu we've received
 * @port: the port we're looking at
 *
 * Record the parameter values for the Actor carried in a received lacpdu as
 * the current partner operational parameter values and sets
 * actor_oper_port_state.defaulted to FALSE.
 */
static void __record_pdu(struct lacpdu *lacpdu, struct port *port)
{
	// validate lacpdu and port
	if (lacpdu && port) {
		// record the new parameter values for the partner operational
		port->partner_oper_port_number = ntohs(lacpdu->actor_port);
		port->partner_oper_port_priority = ntohs(lacpdu->actor_port_priority);
		port->partner_oper_system = lacpdu->actor_system;
		port->partner_oper_system_priority = ntohs(lacpdu->actor_system_priority);
		port->partner_oper_key = ntohs(lacpdu->actor_key);
		// zero partener's lase states
		port->partner_oper_port_state = 0;
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_LACP_ACTIVITY);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_LACP_TIMEOUT);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_AGGREGATION);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_SYNCHRONIZATION);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_COLLECTING);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_DISTRIBUTING);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_DEFAULTED);
		port->partner_oper_port_state |= (lacpdu->actor_state & AD_STATE_EXPIRED);

		// set actor_oper_port_state.defaulted to FALSE
		port->actor_oper_port_state &= ~AD_STATE_DEFAULTED;

		// set the partner sync. to on if the partner is sync. and the port is matched
		if ((port->sm_vars & AD_PORT_MATCHED) && (lacpdu->actor_state & AD_STATE_SYNCHRONIZATION)) {
			port->partner_oper_port_state |= AD_STATE_SYNCHRONIZATION;
		} else {
			port->partner_oper_port_state &= ~AD_STATE_SYNCHRONIZATION;
		}
	}
}

/**
 * __record_default - record default parameters
 * @port: the port we're looking at
 *
 * This function records the default parameter values for the partner carried
 * in the Partner Admin parameters as the current partner operational parameter
 * values and sets actor_oper_port_state.defaulted to TRUE.
 */
static void __record_default(struct port *port)
{
	// validate the port
	if (port) {
		// record the partner admin parameters
		port->partner_oper_port_number = port->partner_admin_port_number;
		port->partner_oper_port_priority = port->partner_admin_port_priority;
		port->partner_oper_system = port->partner_admin_system;
		port->partner_oper_system_priority = port->partner_admin_system_priority;
		port->partner_oper_key = port->partner_admin_key;
		port->partner_oper_port_state = port->partner_admin_port_state;

		// set actor_oper_port_state.defaulted to true
		port->actor_oper_port_state |= AD_STATE_DEFAULTED;
	}
}

/**
 * __update_selected - update a port's Selected variable from a received lacpdu
 * @lacpdu: the lacpdu we've received
 * @port: the port we're looking at
 *
 * Update the value of the selected variable, using parameter values from a
 * newly received lacpdu. The parameter values for the Actor carried in the
 * received PDU are compared with the corresponding operational parameter
 * values for the ports partner. If one or more of the comparisons shows that
 * the value(s) received in the PDU differ from the current operational values,
 * then selected is set to FALSE and actor_oper_port_state.synchronization is
 * set to out_of_sync. Otherwise, selected remains unchanged.
 */
static void __update_selected(struct lacpdu *lacpdu, struct port *port)
{
	// validate lacpdu and port
	if (lacpdu && port) {
		// check if any parameter is different
		if ((ntohs(lacpdu->actor_port) != port->partner_oper_port_number) ||
		    (ntohs(lacpdu->actor_port_priority) != port->partner_oper_port_priority) ||
		    MAC_ADDRESS_COMPARE(&(lacpdu->actor_system), &(port->partner_oper_system)) ||
		    (ntohs(lacpdu->actor_system_priority) != port->partner_oper_system_priority) ||
		    (ntohs(lacpdu->actor_key) != port->partner_oper_key) ||
		    ((lacpdu->actor_state & AD_STATE_AGGREGATION) != (port->partner_oper_port_state & AD_STATE_AGGREGATION))
		   ) {
			// update the state machine Selected variable
			port->sm_vars &= ~AD_PORT_SELECTED;
		}
	}
}

/**
 * __update_default_selected - update a port's Selected variable from Partner
 * @port: the port we're looking at
 *
 * This function updates the value of the selected variable, using the partner
 * administrative parameter values. The administrative values are compared with
 * the corresponding operational parameter values for the partner. If one or
 * more of the comparisons shows that the administrative value(s) differ from
 * the current operational values, then Selected is set to FALSE and
 * actor_oper_port_state.synchronization is set to OUT_OF_SYNC. Otherwise,
 * Selected remains unchanged.
 */
static void __update_default_selected(struct port *port)
{
	// validate the port
	if (port) {
		// check if any parameter is different
		if ((port->partner_admin_port_number != port->partner_oper_port_number) ||
		    (port->partner_admin_port_priority != port->partner_oper_port_priority) ||
		    MAC_ADDRESS_COMPARE(&(port->partner_admin_system), &(port->partner_oper_system)) ||
		    (port->partner_admin_system_priority != port->partner_oper_system_priority) ||
		    (port->partner_admin_key != port->partner_oper_key) ||
		    ((port->partner_admin_port_state & AD_STATE_AGGREGATION) != (port->partner_oper_port_state & AD_STATE_AGGREGATION))
		   ) {
			// update the state machine Selected variable
			port->sm_vars &= ~AD_PORT_SELECTED;
		}
	}
}

/**
 * __choose_matched - update a port's matched variable from a received lacpdu
 * @lacpdu: the lacpdu we've received
 * @port: the port we're looking at
 *
 * Update the value of the matched variable, using parameter values from a
 * newly received lacpdu. Parameter values for the partner carried in the
 * received PDU are compared with the corresponding operational parameter
 * values for the actor. Matched is set to TRUE if all of these parameters
 * match and the PDU parameter partner_state.aggregation has the same value as
 * actor_oper_port_state.aggregation and lacp will actively maintain the link
 * in the aggregation. Matched is also set to TRUE if the value of
 * actor_state.aggregation in the received PDU is set to FALSE, i.e., indicates
 * an individual link and lacp will actively maintain the link. Otherwise,
 * matched is set to FALSE. LACP is considered to be actively maintaining the
 * link if either the PDU's actor_state.lacp_activity variable is TRUE or both
 * the actor's actor_oper_port_state.lacp_activity and the PDU's
 * partner_state.lacp_activity variables are TRUE.
 */
static void __choose_matched(struct lacpdu *lacpdu, struct port *port)
{
	// validate lacpdu and port
	if (lacpdu && port) {
		// check if all parameters are alike
		if (((ntohs(lacpdu->partner_port) == port->actor_port_number) &&
		     (ntohs(lacpdu->partner_port_priority) == port->actor_port_priority) &&
		     !MAC_ADDRESS_COMPARE(&(lacpdu->partner_system), &(port->actor_system)) &&
		     (ntohs(lacpdu->partner_system_priority) == port->actor_system_priority) &&
		     (ntohs(lacpdu->partner_key) == port->actor_oper_port_key) &&
		     ((lacpdu->partner_state & AD_STATE_AGGREGATION) == (port->actor_oper_port_state & AD_STATE_AGGREGATION))) ||
		    // or this is individual link(aggregation == FALSE)
		    ((lacpdu->actor_state & AD_STATE_AGGREGATION) == 0)
		   ) {
			// update the state machine Matched variable
			port->sm_vars |= AD_PORT_MATCHED;
		} else {
			port->sm_vars &= ~AD_PORT_MATCHED;
		}
	}
}

/**
 * __update_ntt - update a port's ntt variable from a received lacpdu
 * @lacpdu: the lacpdu we've received
 * @port: the port we're looking at
 *
 * Updates the value of the ntt variable, using parameter values from a newly
 * received lacpdu. The parameter values for the partner carried in the
 * received PDU are compared with the corresponding operational parameter
 * values for the Actor. If one or more of the comparisons shows that the
 * value(s) received in the PDU differ from the current operational values,
 * then ntt is set to TRUE. Otherwise, ntt remains unchanged.
 */
static void __update_ntt(struct lacpdu *lacpdu, struct port *port)
{
	// validate lacpdu and port
	if (lacpdu && port) {
		// check if any parameter is different
		if ((ntohs(lacpdu->partner_port) != port->actor_port_number) ||
		    (ntohs(lacpdu->partner_port_priority) != port->actor_port_priority) ||
		    MAC_ADDRESS_COMPARE(&(lacpdu->partner_system), &(port->actor_system)) ||
		    (ntohs(lacpdu->partner_system_priority) != port->actor_system_priority) ||
		    (ntohs(lacpdu->partner_key) != port->actor_oper_port_key) ||
		    ((lacpdu->partner_state & AD_STATE_LACP_ACTIVITY) != (port->actor_oper_port_state & AD_STATE_LACP_ACTIVITY)) ||
		    ((lacpdu->partner_state & AD_STATE_LACP_TIMEOUT) != (port->actor_oper_port_state & AD_STATE_LACP_TIMEOUT)) ||
		    ((lacpdu->partner_state & AD_STATE_SYNCHRONIZATION) != (port->actor_oper_port_state & AD_STATE_SYNCHRONIZATION)) ||
		    ((lacpdu->partner_state & AD_STATE_AGGREGATION) != (port->actor_oper_port_state & AD_STATE_AGGREGATION))
		   ) {
			// set ntt to be TRUE
			port->ntt = 1;
		}
	}
}

/**
 * __attach_bond_to_agg
 * @port: the port we're looking at
 *
 * Handle the attaching of the port's control parser/multiplexer and the
 * aggregator. This function does nothing since the parser/multiplexer of the
 * receive and the parser/multiplexer of the aggregator are already combined.
 */
static void __attach_bond_to_agg(struct port *port)
{
	port=NULL; // just to satisfy the compiler
	// This function does nothing since the parser/multiplexer of the receive
	// and the parser/multiplexer of the aggregator are already combined
}

/**
 * __detach_bond_from_agg
 * @port: the port we're looking at
 *
 * Handle the detaching of the port's control parser/multiplexer from the
 * aggregator. This function does nothing since the parser/multiplexer of the
 * receive and the parser/multiplexer of the aggregator are already combined.
 */
static void __detach_bond_from_agg(struct port *port)
{
	port=NULL; // just to satisfy the compiler
	// This function does nothing sience the parser/multiplexer of the receive
	// and the parser/multiplexer of the aggregator are already combined
}

/**
 * __agg_ports_are_ready - check if all ports in an aggregator are ready
 * @aggregator: the aggregator we're looking at
 *
 */
static int __agg_ports_are_ready(struct aggregator *aggregator)
{
	struct port *port;
	int retval = 1;

	if (aggregator) {
		// scan all ports in this aggregator to verfy if they are all ready
		for (port=aggregator->lag_ports; port; port=port->next_port_in_aggregator) {
			if (!(port->sm_vars & AD_PORT_READY_N)) {
				retval = 0;
				break;
			}
		}
	}

	return retval;
}

/**
 * __set_agg_ports_ready - set value of Ready bit in all ports of an aggregator
 * @aggregator: the aggregator we're looking at
 * @val: Should the ports' ready bit be set on or off
 *
 */
static void __set_agg_ports_ready(struct aggregator *aggregator, int val)
{
	struct port *port;

	for (port=aggregator->lag_ports; port; port=port->next_port_in_aggregator) {
		if (val) {
			port->sm_vars |= AD_PORT_READY;
		} else {
			port->sm_vars &= ~AD_PORT_READY;
		}
	}
}

/**
 * __get_agg_bandwidth - get the total bandwidth of an aggregator
 * @aggregator: the aggregator we're looking at
 *
 */
static u32 __get_agg_bandwidth(struct aggregator *aggregator)
{
	u32 bandwidth=0;
	u32 basic_speed;

	if (aggregator->num_of_ports) {
		basic_speed = __get_link_speed(aggregator->lag_ports);
		switch (basic_speed) {
		case AD_LINK_SPEED_BITMASK_1MBPS:
			bandwidth = aggregator->num_of_ports;
			break;
		case AD_LINK_SPEED_BITMASK_10MBPS:
			bandwidth = aggregator->num_of_ports * 10;
			break;
		case AD_LINK_SPEED_BITMASK_100MBPS:
			bandwidth = aggregator->num_of_ports * 100;
			break;
		case AD_LINK_SPEED_BITMASK_1000MBPS:
			bandwidth = aggregator->num_of_ports * 1000;
			break;
		case AD_LINK_SPEED_BITMASK_10000MBPS:
			bandwidth = aggregator->num_of_ports * 10000;
			break;
		default:
			bandwidth=0; // to silent the compilor ....
		}
	}
	return bandwidth;
}

/**
 * __get_active_agg - get the current active aggregator
 * @aggregator: the aggregator we're looking at
 *
 */
static struct aggregator *__get_active_agg(struct aggregator *aggregator)
{
	struct aggregator *retval = NULL;

	for (; aggregator; aggregator = __get_next_agg(aggregator)) {
		if (aggregator->is_active) {
			retval = aggregator;
			break;
		}
	}

	return retval;
}

/**
 * __update_lacpdu_from_port - update a port's lacpdu fields
 * @port: the port we're looking at
 *
 */
static inline void __update_lacpdu_from_port(struct port *port)
{
	struct lacpdu *lacpdu = &port->lacpdu;

	/* update current actual Actor parameters */
	/* lacpdu->subtype                   initialized
	 * lacpdu->version_number            initialized
	 * lacpdu->tlv_type_actor_info       initialized
	 * lacpdu->actor_information_length  initialized
	 */

	lacpdu->actor_system_priority = htons(port->actor_system_priority);
	lacpdu->actor_system = port->actor_system;
	lacpdu->actor_key = htons(port->actor_oper_port_key);
	lacpdu->actor_port_priority = htons(port->actor_port_priority);
	lacpdu->actor_port = htons(port->actor_port_number);
	lacpdu->actor_state = port->actor_oper_port_state;

	/* lacpdu->reserved_3_1              initialized
	 * lacpdu->tlv_type_partner_info     initialized
	 * lacpdu->partner_information_length initialized
	 */

	lacpdu->partner_system_priority = htons(port->partner_oper_system_priority);
	lacpdu->partner_system = port->partner_oper_system;
	lacpdu->partner_key = htons(port->partner_oper_key);
	lacpdu->partner_port_priority = htons(port->partner_oper_port_priority);
	lacpdu->partner_port = htons(port->partner_oper_port_number);
	lacpdu->partner_state = port->partner_oper_port_state;

	/* lacpdu->reserved_3_2              initialized
	 * lacpdu->tlv_type_collector_info   initialized
	 * lacpdu->collector_information_length initialized
	 * collector_max_delay                initialized
	 * reserved_12[12]                   initialized
	 * tlv_type_terminator               initialized
	 * terminator_length                 initialized
	 * reserved_50[50]                   initialized
	 */
}

//////////////////////////////////////////////////////////////////////////////////////
// ================= main 802.3ad protocol code ======================================
//////////////////////////////////////////////////////////////////////////////////////

/**
 * ad_lacpdu_send - send out a lacpdu packet on a given port
 * @port: the port we're looking at
 *
 * Returns:   0 on success
 *          < 0 on error
 */
static int ad_lacpdu_send(struct port *port)
{
	struct slave *slave = port->slave;
	struct sk_buff *skb;
	struct lacpdu_header *lacpdu_header;
	int length = sizeof(struct lacpdu_header);
	struct mac_addr lacpdu_multicast_address = AD_MULTICAST_LACPDU_ADDR;

	skb = dev_alloc_skb(length);
	if (!skb) {
		return -ENOMEM;
	}

	skb->dev = slave->dev;
	skb_reset_mac_header(skb);
	skb->network_header = skb->mac_header + ETH_HLEN;
	skb->protocol = PKT_TYPE_LACPDU;
	skb->priority = TC_PRIO_CONTROL;

	lacpdu_header = (struct lacpdu_header *)skb_put(skb, length);

	lacpdu_header->ad_header.destination_address = lacpdu_multicast_address;
	/* Note: source addres is set to be the member's PERMANENT address, because we use it
	   to identify loopback lacpdus in receive. */
	lacpdu_header->ad_header.source_address = *((struct mac_addr *)(slave->perm_hwaddr));
	lacpdu_header->ad_header.length_type = PKT_TYPE_LACPDU;

	lacpdu_header->lacpdu = port->lacpdu; // struct copy

	dev_queue_xmit(skb);

	return 0;
}

/**
 * ad_marker_send - send marker information/response on a given port
 * @port: the port we're looking at
 * @marker: marker data to send
 *
 * Returns:   0 on success
 *          < 0 on error
 */
static int ad_marker_send(struct port *port, struct bond_marker *marker)
{
	struct slave *slave = port->slave;
	struct sk_buff *skb;
	struct bond_marker_header *marker_header;
	int length = sizeof(struct bond_marker_header);
	struct mac_addr lacpdu_multicast_address = AD_MULTICAST_LACPDU_ADDR;

	skb = dev_alloc_skb(length + 16);
	if (!skb) {
		return -ENOMEM;
	}

	skb_reserve(skb, 16);

	skb->dev = slave->dev;
	skb_reset_mac_header(skb);
	skb->network_header = skb->mac_header + ETH_HLEN;
	skb->protocol = PKT_TYPE_LACPDU;

	marker_header = (struct bond_marker_header *)skb_put(skb, length);

	marker_header->ad_header.destination_address = lacpdu_multicast_address;
	/* Note: source addres is set to be the member's PERMANENT address, because we use it
	   to identify loopback MARKERs in receive. */
	marker_header->ad_header.source_address = *((struct mac_addr *)(slave->perm_hwaddr));
	marker_header->ad_header.length_type = PKT_TYPE_LACPDU;

	marker_header->marker = *marker; // struct copy

	dev_queue_xmit(skb);

	return 0;
}

/**
 * ad_mux_machine - handle a port's mux state machine
 * @port: the port we're looking at
 *
 */
static void ad_mux_machine(struct port *port)
{
	mux_states_t last_state;

	// keep current State Machine state to compare later if it was changed
	last_state = port->sm_mux_state;

	if (port->sm_vars & AD_PORT_BEGIN) {
		port->sm_mux_state = AD_MUX_DETACHED;		 // next state
	} else {
		switch (port->sm_mux_state) {
		case AD_MUX_DETACHED:
			if ((port->sm_vars & AD_PORT_SELECTED) || (port->sm_vars & AD_PORT_STANDBY)) { // if SELECTED or STANDBY
				port->sm_mux_state = AD_MUX_WAITING; // next state
			}
			break;
		case AD_MUX_WAITING:
			// if SELECTED == FALSE return to DETACH state
			if (!(port->sm_vars & AD_PORT_SELECTED)) { // if UNSELECTED
				port->sm_vars &= ~AD_PORT_READY_N;
				// in order to withhold the Selection Logic to check all ports READY_N value
				// every callback cycle to update ready variable, we check READY_N and update READY here
				__set_agg_ports_ready(port->aggregator, __agg_ports_are_ready(port->aggregator));
				port->sm_mux_state = AD_MUX_DETACHED;	 // next state
				break;
			}

			// check if the wait_while_timer expired
			if (port->sm_mux_timer_counter && !(--port->sm_mux_timer_counter)) {
				port->sm_vars |= AD_PORT_READY_N;
			}

			// in order to withhold the selection logic to check all ports READY_N value
			// every callback cycle to update ready variable, we check READY_N and update READY here
			__set_agg_ports_ready(port->aggregator, __agg_ports_are_ready(port->aggregator));

			// if the wait_while_timer expired, and the port is in READY state, move to ATTACHED state
			if ((port->sm_vars & AD_PORT_READY) && !port->sm_mux_timer_counter) {
				port->sm_mux_state = AD_MUX_ATTACHED;	 // next state
			}
			break;
		case AD_MUX_ATTACHED:
			// check also if agg_select_timer expired(so the edable port will take place only after this timer)
			if ((port->sm_vars & AD_PORT_SELECTED) && (port->partner_oper_port_state & AD_STATE_SYNCHRONIZATION) && !__check_agg_selection_timer(port)) {
				port->sm_mux_state = AD_MUX_COLLECTING_DISTRIBUTING;// next state
			} else if (!(port->sm_vars & AD_PORT_SELECTED) || (port->sm_vars & AD_PORT_STANDBY)) {	  // if UNSELECTED or STANDBY
				port->sm_vars &= ~AD_PORT_READY_N;
				// in order to withhold the selection logic to check all ports READY_N value
				// every callback cycle to update ready variable, we check READY_N and update READY here
				__set_agg_ports_ready(port->aggregator, __agg_ports_are_ready(port->aggregator));
				port->sm_mux_state = AD_MUX_DETACHED;// next state
			}
			break;
		case AD_MUX_COLLECTING_DISTRIBUTING:
			if (!(port->sm_vars & AD_PORT_SELECTED) || (port->sm_vars & AD_PORT_STANDBY) ||
			    !(port->partner_oper_port_state & AD_STATE_SYNCHRONIZATION)
			   ) {
				port->sm_mux_state = AD_MUX_ATTACHED;// next state

			} else {
				// if port state hasn't changed make
				// sure that a collecting distributing
				// port in an active aggregator is enabled
				if (port->aggregator &&
				    port->aggregator->is_active &&
				    !__port_is_enabled(port)) {

					__enable_port(port);
				}
			}
			break;
		default:    //to silence the compiler
			break;
		}
	}

	// check if the state machine was changed
	if (port->sm_mux_state != last_state) {
		dprintk("Mux Machine: Port=%d, Last State=%d, Curr State=%d\n", port->actor_port_number, last_state, port->sm_mux_state);
		switch (port->sm_mux_state) {
		case AD_MUX_DETACHED:
			__detach_bond_from_agg(port);
			port->actor_oper_port_state &= ~AD_STATE_SYNCHRONIZATION;
			ad_disable_collecting_distributing(port);
			port->actor_oper_port_state &= ~AD_STATE_COLLECTING;
			port->actor_oper_port_state &= ~AD_STATE_DISTRIBUTING;
			port->ntt = 1;
			break;
		case AD_MUX_WAITING:
			port->sm_mux_timer_counter = __ad_timer_to_ticks(AD_WAIT_WHILE_TIMER, 0);
			break;
		case AD_MUX_ATTACHED:
			__attach_bond_to_agg(port);
			port->actor_oper_port_state |= AD_STATE_SYNCHRONIZATION;
			port->actor_oper_port_state &= ~AD_STATE_COLLECTING;
			port->actor_oper_port_state &= ~AD_STATE_DISTRIBUTING;
			ad_disable_collecting_distributing(port);
			port->ntt = 1;
			break;
		case AD_MUX_COLLECTING_DISTRIBUTING:
			port->actor_oper_port_state |= AD_STATE_COLLECTING;
			port->actor_oper_port_state |= AD_STATE_DISTRIBUTING;
			ad_enable_collecting_distributing(port);
			port->ntt = 1;
			break;
		default:    //to silence the compiler
			break;
		}
	}
}

/**
 * ad_rx_machine - handle a port's rx State Machine
 * @lacpdu: the lacpdu we've received
 * @port: the port we're looking at
 *
 * If lacpdu arrived, stop previous timer (if exists) and set the next state as
 * CURRENT. If timer expired set the state machine in the proper state.
 * In other cases, this function checks if we need to switch to other state.
 */
static void ad_rx_machine(struct lacpdu *lacpdu, struct port *port)
{
	rx_states_t last_state;

	// Lock to prevent 2 instances of this function to run simultaneously(rx interrupt and periodic machine callback)
	__get_rx_machine_lock(port);

	// keep current State Machine state to compare later if it was changed
	last_state = port->sm_rx_state;

	// check if state machine should change state
	// first, check if port was reinitialized
	if (port->sm_vars & AD_PORT_BEGIN) {
		port->sm_rx_state = AD_RX_INITIALIZE;		    // next state
	}
	// check if port is not enabled
	else if (!(port->sm_vars & AD_PORT_BEGIN) && !port->is_enabled && !(port->sm_vars & AD_PORT_MOVED)) {
		port->sm_rx_state = AD_RX_PORT_DISABLED;	    // next state
	}
	// check if new lacpdu arrived
	else if (lacpdu && ((port->sm_rx_state == AD_RX_EXPIRED) || (port->sm_rx_state == AD_RX_DEFAULTED) || (port->sm_rx_state == AD_RX_CURRENT))) {
		port->sm_rx_timer_counter = 0; // zero timer
		port->sm_rx_state = AD_RX_CURRENT;
	} else {
		// if timer is on, and if it is expired
		if (port->sm_rx_timer_counter && !(--port->sm_rx_timer_counter)) {
			switch (port->sm_rx_state) {
			case AD_RX_EXPIRED:
				port->sm_rx_state = AD_RX_DEFAULTED;		// next state
				break;
			case AD_RX_CURRENT:
				port->sm_rx_state = AD_RX_EXPIRED;	    // next state
				break;
			default:    //to silence the compiler
				break;
			}
		} else {
			// if no lacpdu arrived and no timer is on
			switch (port->sm_rx_state) {
			case AD_RX_PORT_DISABLED:
				if (port->sm_vars & AD_PORT_MOVED) {
					port->sm_rx_state = AD_RX_INITIALIZE;	    // next state
				} else if (port->is_enabled && (port->sm_vars & AD_PORT_LACP_ENABLED)) {
					port->sm_rx_state = AD_RX_EXPIRED;	// next state
				} else if (port->is_enabled && ((port->sm_vars & AD_PORT_LACP_ENABLED) == 0)) {
					port->sm_rx_state = AD_RX_LACP_DISABLED;    // next state
				}
				break;
			default:    //to silence the compiler
				break;

			}
		}
	}

	// check if the State machine was changed or new lacpdu arrived
	if ((port->sm_rx_state != last_state) || (lacpdu)) {
		dprintk("Rx Machine: Port=%d, Last State=%d, Curr State=%d\n", port->actor_port_number, last_state, port->sm_rx_state);
		switch (port->sm_rx_state) {
		case AD_RX_INITIALIZE:
			if (!(port->actor_oper_port_key & AD_DUPLEX_KEY_BITS)) {
				port->sm_vars &= ~AD_PORT_LACP_ENABLED;
			} else {
				port->sm_vars |= AD_PORT_LACP_ENABLED;
			}
			port->sm_vars &= ~AD_PORT_SELECTED;
			__record_default(port);
			port->actor_oper_port_state &= ~AD_STATE_EXPIRED;
			port->sm_vars &= ~AD_PORT_MOVED;
			port->sm_rx_state = AD_RX_PORT_DISABLED;	// next state

			/*- Fall Through -*/

		case AD_RX_PORT_DISABLED:
			port->sm_vars &= ~AD_PORT_MATCHED;
			break;
		case AD_RX_LACP_DISABLED:
			port->sm_vars &= ~AD_PORT_SELECTED;
			__record_default(port);
			port->partner_oper_port_state &= ~AD_STATE_AGGREGATION;
			port->sm_vars |= AD_PORT_MATCHED;
			port->actor_oper_port_state &= ~AD_STATE_EXPIRED;
			break;
		case AD_RX_EXPIRED:
			//Reset of the Synchronization flag. (Standard 43.4.12)
			//This reset cause to disable this port in the COLLECTING_DISTRIBUTING state of the
			//mux machine in case of EXPIRED even if LINK_DOWN didn't arrive for the port.
			port->partner_oper_port_state &= ~AD_STATE_SYNCHRONIZATION;
			port->sm_vars &= ~AD_PORT_MATCHED;
			port->partner_oper_port_state |= AD_SHORT_TIMEOUT;
			port->sm_rx_timer_counter = __ad_timer_to_ticks(AD_CURRENT_WHILE_TIMER, (u16)(AD_SHORT_TIMEOUT));
			port->actor_oper_port_state |= AD_STATE_EXPIRED;
			break;
		case AD_RX_DEFAULTED:
			__update_default_selected(port);
			__record_default(port);
			port->sm_vars |= AD_PORT_MATCHED;
			port->actor_oper_port_state &= ~AD_STATE_EXPIRED;
			break;
		case AD_RX_CURRENT:
			// detect loopback situation
			if (!MAC_ADDRESS_COMPARE(&(lacpdu->actor_system), &(port->actor_system))) {
				// INFO_RECEIVED_LOOPBACK_FRAMES
				printk(KERN_ERR DRV_NAME ": %s: An illegal loopback occurred on "
				       "adapter (%s). Check the configuration to verify that all "
				       "Adapters are connected to 802.3ad compliant switch ports\n",
				       port->slave->dev->master->name, port->slave->dev->name);
				__release_rx_machine_lock(port);
				return;
			}
			__update_selected(lacpdu, port);
			__update_ntt(lacpdu, port);
			__record_pdu(lacpdu, port);
			__choose_matched(lacpdu, port);
			port->sm_rx_timer_counter = __ad_timer_to_ticks(AD_CURRENT_WHILE_TIMER, (u16)(port->actor_oper_port_state & AD_STATE_LACP_TIMEOUT));
			port->actor_oper_port_state &= ~AD_STATE_EXPIRED;
			// verify that if the aggregator is enabled, the port is enabled too.
			//(because if the link goes down for a short time, the 802.3ad will not
			// catch it, and the port will continue to be disabled)
			if (port->aggregator && port->aggregator->is_active && !__port_is_enabled(port)) {
				__enable_port(port);
			}
			break;
		default:    //to silence the compiler
			break;
		}
	}
	__release_rx_machine_lock(port);
}

/**
 * ad_tx_machine - handle a port's tx state machine
 * @port: the port we're looking at
 *
 */
static void ad_tx_machine(struct port *port)
{
	// check if tx timer expired, to verify that we do not send more than 3 packets per second
	if (port->sm_tx_timer_counter && !(--port->sm_tx_timer_counter)) {
		// check if there is something to send
		if (port->ntt && (port->sm_vars & AD_PORT_LACP_ENABLED)) {
			__update_lacpdu_from_port(port);
			// send the lacpdu
			if (ad_lacpdu_send(port) >= 0) {
				dprintk("Sent LACPDU on port %d\n", port->actor_port_number);
				// mark ntt as false, so it will not be sent again until demanded
				port->ntt = 0;
			}
		}
		// restart tx timer(to verify that we will not exceed AD_MAX_TX_IN_SECOND
		port->sm_tx_timer_counter=ad_ticks_per_sec/AD_MAX_TX_IN_SECOND;
	}
}

/**
 * ad_periodic_machine - handle a port's periodic state machine
 * @port: the port we're looking at
 *
 * Turn ntt flag on priodically to perform periodic transmission of lacpdu's.
 */
static void ad_periodic_machine(struct port *port)
{
	periodic_states_t last_state;

	// keep current state machine state to compare later if it was changed
	last_state = port->sm_periodic_state;

	// check if port was reinitialized
	if (((port->sm_vars & AD_PORT_BEGIN) || !(port->sm_vars & AD_PORT_LACP_ENABLED) || !port->is_enabled) ||
	    (!(port->actor_oper_port_state & AD_STATE_LACP_ACTIVITY) && !(port->partner_oper_port_state & AD_STATE_LACP_ACTIVITY))
	   ) {
		port->sm_periodic_state = AD_NO_PERIODIC;	     // next state
	}
	// check if state machine should change state
	else if (port->sm_periodic_timer_counter) {
		// check if periodic state machine expired
		if (!(--port->sm_periodic_timer_counter)) {
			// if expired then do tx
			port->sm_periodic_state = AD_PERIODIC_TX;    // next state
		} else {
			// If not expired, check if there is some new timeout parameter from the partner state
			switch (port->sm_periodic_state) {
			case AD_FAST_PERIODIC:
				if (!(port->partner_oper_port_state & AD_STATE_LACP_TIMEOUT)) {
					port->sm_periodic_state = AD_SLOW_PERIODIC;  // next state
				}
				break;
			case AD_SLOW_PERIODIC:
				if ((port->partner_oper_port_state & AD_STATE_LACP_TIMEOUT)) {
					// stop current timer
					port->sm_periodic_timer_counter = 0;
					port->sm_periodic_state = AD_PERIODIC_TX;	 // next state
				}
				break;
			default:    //to silence the compiler
				break;
			}
		}
	} else {
		switch (port->sm_periodic_state) {
		case AD_NO_PERIODIC:
			port->sm_periodic_state = AD_FAST_PERIODIC;	 // next state
			break;
		case AD_PERIODIC_TX:
			if (!(port->partner_oper_port_state & AD_STATE_LACP_TIMEOUT)) {
				port->sm_periodic_state = AD_SLOW_PERIODIC;  // next state
			} else {
				port->sm_periodic_state = AD_FAST_PERIODIC;  // next state
			}
			break;
		default:    //to silence the compiler
			break;
		}
	}

	// check if the state machine was changed
	if (port->sm_periodic_state != last_state) {
		dprintk("Periodic Machine: Port=%d, Last State=%d, Curr State=%d\n", port->actor_port_number, last_state, port->sm_periodic_state);
		switch (port->sm_periodic_state) {
		case AD_NO_PERIODIC:
			port->sm_periodic_timer_counter = 0;	   // zero timer
			break;
		case AD_FAST_PERIODIC:
			port->sm_periodic_timer_counter = __ad_timer_to_ticks(AD_PERIODIC_TIMER, (u16)(AD_FAST_PERIODIC_TIME))-1; // decrement 1 tick we lost in the PERIODIC_TX cycle
			break;
		case AD_SLOW_PERIODIC:
			port->sm_periodic_timer_counter = __ad_timer_to_ticks(AD_PERIODIC_TIMER, (u16)(AD_SLOW_PERIODIC_TIME))-1; // decrement 1 tick we lost in the PERIODIC_TX cycle
			break;
		case AD_PERIODIC_TX:
			port->ntt = 1;
			break;
		default:    //to silence the compiler
			break;
		}
	}
}

/**
 * ad_port_selection_logic - select aggregation groups
 * @port: the port we're looking at
 *
 * Select aggregation groups, and assign each port for it's aggregetor. The
 * selection logic is called in the inititalization (after all the handshkes),
 * and after every lacpdu receive (if selected is off).
 */
static void ad_port_selection_logic(struct port *port)
{
	struct aggregator *aggregator, *free_aggregator = NULL, *temp_aggregator;
	struct port *last_port = NULL, *curr_port;
	int found = 0;

	// if the port is already Selected, do nothing
	if (port->sm_vars & AD_PORT_SELECTED) {
		return;
	}

	// if the port is connected to other aggregator, detach it
	if (port->aggregator) {
		// detach the port from its former aggregator
		temp_aggregator=port->aggregator;
		for (curr_port=temp_aggregator->lag_ports; curr_port; last_port=curr_port, curr_port=curr_port->next_port_in_aggregator) {
			if (curr_port == port) {
				temp_aggregator->num_of_ports--;
				if (!last_port) {// if it is the first port attached to the aggregator
					temp_aggregator->lag_ports=port->next_port_in_aggregator;
				} else {// not the first port attached to the aggregator
					last_port->next_port_in_aggregator=port->next_port_in_aggregator;
				}

				// clear the port's relations to this aggregator
				port->aggregator = NULL;
				port->next_port_in_aggregator=NULL;
				port->actor_port_aggregator_identifier=0;

				dprintk("Port %d left LAG %d\n", port->actor_port_number, temp_aggregator->aggregator_identifier);
				// if the aggregator is empty, clear its parameters, and set it ready to be attached
				if (!temp_aggregator->lag_ports) {
					ad_clear_agg(temp_aggregator);
				}
				break;
			}
		}
		if (!curr_port) { // meaning: the port was related to an aggregator but was not on the aggregator port list
			printk(KERN_WARNING DRV_NAME ": %s: Warning: Port %d (on %s) was "
			       "related to aggregator %d but was not on its port list\n",
			       port->slave->dev->master->name,
			       port->actor_port_number, port->slave->dev->name,
			       port->aggregator->aggregator_identifier);
		}
	}
	// search on all aggregators for a suitable aggregator for this port
	for (aggregator = __get_first_agg(port); aggregator;
	     aggregator = __get_next_agg(aggregator)) {

		// keep a free aggregator for later use(if needed)
		if (!aggregator->lag_ports) {
			if (!free_aggregator) {
				free_aggregator=aggregator;
			}
			continue;
		}
		// check if current aggregator suits us
		if (((aggregator->actor_oper_aggregator_key == port->actor_oper_port_key) && // if all parameters match AND
		     !MAC_ADDRESS_COMPARE(&(aggregator->partner_system), &(port->partner_oper_system)) &&
		     (aggregator->partner_system_priority == port->partner_oper_system_priority) &&
		     (aggregator->partner_oper_aggregator_key == port->partner_oper_key)
		    ) &&
		    ((MAC_ADDRESS_COMPARE(&(port->partner_oper_system), &(null_mac_addr)) && // partner answers
		      !aggregator->is_individual)  // but is not individual OR
		    )
		   ) {
			// attach to the founded aggregator
			port->aggregator = aggregator;
			port->actor_port_aggregator_identifier=port->aggregator->aggregator_identifier;
			port->next_port_in_aggregator=aggregator->lag_ports;
			port->aggregator->num_of_ports++;
			aggregator->lag_ports=port;
			dprintk("Port %d joined LAG %d(existing LAG)\n", port->actor_port_number, port->aggregator->aggregator_identifier);

			// mark this port as selected
			port->sm_vars |= AD_PORT_SELECTED;
			found = 1;
			break;
		}
	}

	// the port couldn't find an aggregator - attach it to a new aggregator
	if (!found) {
		if (free_aggregator) {
			// assign port a new aggregator
			port->aggregator = free_aggregator;
			port->actor_port_aggregator_identifier=port->aggregator->aggregator_identifier;

			// update the new aggregator's parameters
			// if port was responsed from the end-user
			if (port->actor_oper_port_key & AD_DUPLEX_KEY_BITS) {// if port is full duplex
				port->aggregator->is_individual = 0;
			} else {
				port->aggregator->is_individual = 1;
			}

			port->aggregator->actor_admin_aggregator_key = port->actor_admin_port_key;
			port->aggregator->actor_oper_aggregator_key = port->actor_oper_port_key;
			port->aggregator->partner_system=port->partner_oper_system;
			port->aggregator->partner_system_priority = port->partner_oper_system_priority;
			port->aggregator->partner_oper_aggregator_key = port->partner_oper_key;
			port->aggregator->receive_state = 1;
			port->aggregator->transmit_state = 1;
			port->aggregator->lag_ports = port;
			port->aggregator->num_of_ports++;

			// mark this port as selected
			port->sm_vars |= AD_PORT_SELECTED;

			dprintk("Port %d joined LAG %d(new LAG)\n", port->actor_port_number, port->aggregator->aggregator_identifier);
		} else {
			printk(KERN_ERR DRV_NAME ": %s: Port %d (on %s) did not find a suitable aggregator\n",
			       port->slave->dev->master->name,
			       port->actor_port_number, port->slave->dev->name);
		}
	}
	// if all aggregator's ports are READY_N == TRUE, set ready=TRUE in all aggregator's ports
	// else set ready=FALSE in all aggregator's ports
	__set_agg_ports_ready(port->aggregator, __agg_ports_are_ready(port->aggregator));

	if (!__check_agg_selection_timer(port) && (aggregator = __get_first_agg(port))) {
		ad_agg_selection_logic(aggregator);
	}
}

/**
 * ad_agg_selection_logic - select an aggregation group for a team
 * @aggregator: the aggregator we're looking at
 *
 * It is assumed that only one aggregator may be selected for a team.
 * The logic of this function is to select (at first time) the aggregator with
 * the most ports attached to it, and to reselect the active aggregator only if
 * the previous aggregator has no more ports related to it.
 *
 * FIXME: this function MUST be called with the first agg in the bond, or
 * __get_active_agg() won't work correctly. This function should be better
 * called with the bond itself, and retrieve the first agg from it.
 */
static void ad_agg_selection_logic(struct aggregator *aggregator)
{
	struct aggregator *best_aggregator = NULL, *active_aggregator = NULL;
	struct aggregator *last_active_aggregator = NULL, *origin_aggregator;
	struct port *port;
	u16 num_of_aggs=0;

	origin_aggregator = aggregator;

	//get current active aggregator
	last_active_aggregator = __get_active_agg(aggregator);

	// search for the aggregator with the most ports attached to it.
	do {
		// count how many candidate lag's we have
		if (aggregator->lag_ports) {
			num_of_aggs++;
		}
		if (aggregator->is_active && !aggregator->is_individual &&   // if current aggregator is the active aggregator
		    MAC_ADDRESS_COMPARE(&(aggregator->partner_system), &(null_mac_addr))) {   // and partner answers to 802.3ad PDUs
			if (aggregator->num_of_ports) {	// if any ports attached to the current aggregator
				best_aggregator=NULL;	 // disregard the best aggregator that was chosen by now
				break;		 // stop the selection of other aggregator if there are any ports attached to this active aggregator
			} else { // no ports attached to this active aggregator
				aggregator->is_active = 0; // mark this aggregator as not active anymore
			}
		}
		if (aggregator->num_of_ports) {	// if any ports attached
			if (best_aggregator) {	// if there is a candidte aggregator
				//The reasons for choosing new best aggregator:
				// 1. if current agg is NOT individual and the best agg chosen so far is individual OR
				// current and best aggs are both individual or both not individual, AND
				// 2a.  current agg partner reply but best agg partner do not reply OR
				// 2b.  current agg partner reply OR current agg partner do not reply AND best agg partner also do not reply AND
				//      current has more ports/bandwidth, or same amount of ports but current has faster ports, THEN
				//      current agg become best agg so far

				//if current agg is NOT individual and the best agg chosen so far is individual change best_aggregator
				if (!aggregator->is_individual && best_aggregator->is_individual) {
					best_aggregator=aggregator;
				}
				// current and best aggs are both individual or both not individual
				else if ((aggregator->is_individual && best_aggregator->is_individual) ||
					 (!aggregator->is_individual && !best_aggregator->is_individual)) {
					//  current and best aggs are both individual or both not individual AND
					//  current agg partner reply but best agg partner do not reply
					if ((MAC_ADDRESS_COMPARE(&(aggregator->partner_system), &(null_mac_addr)) &&
					     !MAC_ADDRESS_COMPARE(&(best_aggregator->partner_system), &(null_mac_addr)))) {
						best_aggregator=aggregator;
					}
					//  current agg partner reply OR current agg partner do not reply AND best agg partner also do not reply
					else if (! (!MAC_ADDRESS_COMPARE(&(aggregator->partner_system), &(null_mac_addr)) &&
						    MAC_ADDRESS_COMPARE(&(best_aggregator->partner_system), &(null_mac_addr)))) {
						if ((__get_agg_selection_mode(aggregator->lag_ports) == AD_BANDWIDTH)&&
						    (__get_agg_bandwidth(aggregator) > __get_agg_bandwidth(best_aggregator))) {
							best_aggregator=aggregator;
						} else if (__get_agg_selection_mode(aggregator->lag_ports) == AD_COUNT) {
							if (((aggregator->num_of_ports > best_aggregator->num_of_ports) &&
							     (aggregator->actor_oper_aggregator_key & AD_SPEED_KEY_BITS))||
							    ((aggregator->num_of_ports == best_aggregator->num_of_ports) &&
							     ((u16)(aggregator->actor_oper_aggregator_key & AD_SPEED_KEY_BITS) >
							      (u16)(best_aggregator->actor_oper_aggregator_key & AD_SPEED_KEY_BITS)))) {
								best_aggregator=aggregator;
							}
						}
					}
				}
			} else {
				best_aggregator=aggregator;
			}
		}
		aggregator->is_active = 0; // mark all aggregators as not active anymore
	} while ((aggregator = __get_next_agg(aggregator)));

	// if we have new aggregator selected, don't replace the old aggregator if it has an answering partner,
	// or if both old aggregator and new aggregator don't have answering partner
	if (best_aggregator) {
		if (last_active_aggregator && last_active_aggregator->lag_ports && last_active_aggregator->lag_ports->is_enabled &&
		    (MAC_ADDRESS_COMPARE(&(last_active_aggregator->partner_system), &(null_mac_addr)) ||   // partner answers OR
		     (!MAC_ADDRESS_COMPARE(&(last_active_aggregator->partner_system), &(null_mac_addr)) &&	// both old and new
		      !MAC_ADDRESS_COMPARE(&(best_aggregator->partner_system), &(null_mac_addr))))     // partner do not answer
		   ) {
			// if new aggregator has link, and old aggregator does not, replace old aggregator.(do nothing)
			// -> don't replace otherwise.
			if (!(!last_active_aggregator->actor_oper_aggregator_key && best_aggregator->actor_oper_aggregator_key)) {
				best_aggregator=NULL;
				last_active_aggregator->is_active = 1; // don't replace good old aggregator

			}
		}
	}

	// if there is new best aggregator, activate it
	if (best_aggregator) {
		for (aggregator = __get_first_agg(best_aggregator->lag_ports);
		    aggregator;
		    aggregator = __get_next_agg(aggregator)) {

			dprintk("Agg=%d; Ports=%d; a key=%d; p key=%d; Indiv=%d; Active=%d\n",
					aggregator->aggregator_identifier, aggregator->num_of_ports,
					aggregator->actor_oper_aggregator_key, aggregator->partner_oper_aggregator_key,
					aggregator->is_individual, aggregator->is_active);
		}

		// check if any partner replys
		if (best_aggregator->is_individual) {
			printk(KERN_WARNING DRV_NAME ": %s: Warning: No 802.3ad response from "
			       "the link partner for any adapters in the bond\n",
			       best_aggregator->slave->dev->master->name);
		}

		// check if there are more than one aggregator
		if (num_of_aggs > 1) {
			dprintk("Warning: More than one Link Aggregation Group was "
				"found in the bond. Only one group will function in the bond\n");
		}

		best_aggregator->is_active = 1;
		dprintk("LAG %d choosed as the active LAG\n", best_aggregator->aggregator_identifier);
		dprintk("Agg=%d; Ports=%d; a key=%d; p key=%d; Indiv=%d; Active=%d\n",
				best_aggregator->aggregator_identifier, best_aggregator->num_of_ports,
				best_aggregator->actor_oper_aggregator_key, best_aggregator->partner_oper_aggregator_key,
				best_aggregator->is_individual, best_aggregator->is_active);

		// disable the ports that were related to the former active_aggregator
		if (last_active_aggregator) {
			for (port=last_active_aggregator->lag_ports; port; port=port->next_port_in_aggregator) {
				__disable_port(port);
			}
		}
	}

	// if the selected aggregator is of join individuals(partner_system is NULL), enable their ports
	active_aggregator = __get_active_agg(origin_aggregator);

	if (active_aggregator) {
		if (!MAC_ADDRESS_COMPARE(&(active_aggregator->partner_system), &(null_mac_addr))) {
			for (port=active_aggregator->lag_ports; port; port=port->next_port_in_aggregator) {
				__enable_port(port);
			}
		}
	}
}

/**
 * ad_clear_agg - clear a given aggregator's parameters
 * @aggregator: the aggregator we're looking at
 *
 */
static void ad_clear_agg(struct aggregator *aggregator)
{
	if (aggregator) {
		aggregator->is_individual = 0;
		aggregator->actor_admin_aggregator_key = 0;
		aggregator->actor_oper_aggregator_key = 0;
		aggregator->partner_system = null_mac_addr;
		aggregator->partner_system_priority = 0;
		aggregator->partner_oper_aggregator_key = 0;
		aggregator->receive_state = 0;
		aggregator->transmit_state = 0;
		aggregator->lag_ports = NULL;
		aggregator->is_active = 0;
		aggregator->num_of_ports = 0;
		dprintk("LAG %d was cleared\n", aggregator->aggregator_identifier);
	}
}

/**
 * ad_initialize_agg - initialize a given aggregator's parameters
 * @aggregator: the aggregator we're looking at
 *
 */
static void ad_initialize_agg(struct aggregator *aggregator)
{
	if (aggregator) {
		ad_clear_agg(aggregator);

		aggregator->aggregator_mac_address = null_mac_addr;
		aggregator->aggregator_identifier = 0;
		aggregator->slave = NULL;
	}
}

/**
 * ad_initialize_port - initialize a given port's parameters
 * @aggregator: the aggregator we're looking at
 * @lacp_fast: boolean. whether fast periodic should be used
 *
 */
static void ad_initialize_port(struct port *port, int lacp_fast)
{
	if (port) {
		port->actor_port_number = 1;
		port->actor_port_priority = 0xff;
		port->actor_system = null_mac_addr;
		port->actor_system_priority = 0xffff;
		port->actor_port_aggregator_identifier = 0;
		port->ntt = 0;
		port->actor_admin_port_key = 1;
		port->actor_oper_port_key  = 1;
		port->actor_admin_port_state = AD_STATE_AGGREGATION | AD_STATE_LACP_ACTIVITY;
		port->actor_oper_port_state  = AD_STATE_AGGREGATION | AD_STATE_LACP_ACTIVITY;

		if (lacp_fast) {
			port->actor_oper_port_state |= AD_STATE_LACP_TIMEOUT;
		}

		port->partner_admin_system = null_mac_addr;
		port->partner_oper_system  = null_mac_addr;
		port->partner_admin_system_priority = 0xffff;
		port->partner_oper_system_priority  = 0xffff;
		port->partner_admin_key = 1;
		port->partner_oper_key  = 1;
		port->partner_admin_port_number = 1;
		port->partner_oper_port_number  = 1;
		port->partner_admin_port_priority = 0xff;
		port->partner_oper_port_priority  = 0xff;
		port->partner_admin_port_state = 1;
		port->partner_oper_port_state  = 1;
		port->is_enabled = 1;
		// ****** private parameters ******
		port->sm_vars = 0x3;
		port->sm_rx_state = 0;
		port->sm_rx_timer_counter = 0;
		port->sm_periodic_state = 0;
		port->sm_periodic_timer_counter = 0;
		port->sm_mux_state = 0;
		port->sm_mux_timer_counter = 0;
		port->sm_tx_state = 0;
		port->sm_tx_timer_counter = 0;
		port->slave = NULL;
		port->aggregator = NULL;
		port->next_port_in_aggregator = NULL;
		port->transaction_id = 0;

		ad_initialize_lacpdu(&(port->lacpdu));
	}
}

/**
 * ad_enable_collecting_distributing - enable a port's transmit/receive
 * @port: the port we're looking at
 *
 * Enable @port if it's in an active aggregator
 */
static void ad_enable_collecting_distributing(struct port *port)
{
	if (port->aggregator->is_active) {
		dprintk("Enabling port %d(LAG %d)\n", port->actor_port_number, port->aggregator->aggregator_identifier);
		__enable_port(port);
	}
}

/**
 * ad_disable_collecting_distributing - disable a port's transmit/receive
 * @port: the port we're looking at
 *
 */
static void ad_disable_collecting_distributing(struct port *port)
{
	if (port->aggregator && MAC_ADDRESS_COMPARE(&(port->aggregator->partner_system), &(null_mac_addr))) {
		dprintk("Disabling port %d(LAG %d)\n", port->actor_port_number, port->aggregator->aggregator_identifier);
		__disable_port(port);
	}
}

#if 0
/**
 * ad_marker_info_send - send a marker information frame
 * @port: the port we're looking at
 *
 * This function does nothing since we decided not to implement send and handle
 * response for marker PDU's, in this stage, but only to respond to marker
 * information.
 */
static void ad_marker_info_send(struct port *port)
{
	struct bond_marker marker;
	u16 index;

	// fill the marker PDU with the appropriate values
	marker.subtype = 0x02;
	marker.version_number = 0x01;
	marker.tlv_type = AD_MARKER_INFORMATION_SUBTYPE;
	marker.marker_length = 0x16;
	// convert requester_port to Big Endian
	marker.requester_port = (((port->actor_port_number & 0xFF) << 8) |((u16)(port->actor_port_number & 0xFF00) >> 8));
	marker.requester_system = port->actor_system;
	// convert requester_port(u32) to Big Endian
	marker.requester_transaction_id = (((++port->transaction_id & 0xFF) << 24) |((port->transaction_id & 0xFF00) << 8) |((port->transaction_id & 0xFF0000) >> 8) |((port->transaction_id & 0xFF000000) >> 24));
	marker.pad = 0;
	marker.tlv_type_terminator = 0x00;
	marker.terminator_length = 0x00;
	for (index=0; index<90; index++) {
		marker.reserved_90[index]=0;
	}

	// send the marker information
	if (ad_marker_send(port, &marker) >= 0) {
		dprintk("Sent Marker Information on port %d\n", port->actor_port_number);
	}
}
#endif

/**
 * ad_marker_info_received - handle receive of a Marker information frame
 * @marker_info: Marker info received
 * @port: the port we're looking at
 *
 */
static void ad_marker_info_received(struct bond_marker *marker_info,
	struct port *port)
{
	struct bond_marker marker;

	// copy the received marker data to the response marker
	//marker = *marker_info;
	memcpy(&marker, marker_info, sizeof(struct bond_marker));
	// change the marker subtype to marker response
	marker.tlv_type=AD_MARKER_RESPONSE_SUBTYPE;
	// send the marker response

	if (ad_marker_send(port, &marker) >= 0) {
		dprintk("Sent Marker Response on port %d\n", port->actor_port_number);
	}
}

/**
 * ad_marker_response_received - handle receive of a marker response frame
 * @marker: marker PDU received
 * @port: the port we're looking at
 *
 * This function does nothing since we decided not to implement send and handle
 * response for marker PDU's, in this stage, but only to respond to marker
 * information.
 */
static void ad_marker_response_received(struct bond_marker *marker,
	struct port *port)
{
	marker=NULL; // just to satisfy the compiler
	port=NULL;  // just to satisfy the compiler
	// DO NOTHING, SINCE WE DECIDED NOT TO IMPLEMENT THIS FEATURE FOR NOW
}

/**
 * ad_initialize_lacpdu - initialize a given lacpdu structure
 * @lacpdu: lacpdu structure to initialize
 *
 */
static void ad_initialize_lacpdu(struct lacpdu *lacpdu)
{
	u16 index;

	// initialize lacpdu data
	lacpdu->subtype = 0x01;
	lacpdu->version_number = 0x01;
	lacpdu->tlv_type_actor_info = 0x01;
	lacpdu->actor_information_length = 0x14;
	// lacpdu->actor_system_priority    updated on send
	// lacpdu->actor_system             updated on send
	// lacpdu->actor_key                updated on send
	// lacpdu->actor_port_priority      updated on send
	// lacpdu->actor_port               updated on send
	// lacpdu->actor_state              updated on send
	lacpdu->tlv_type_partner_info = 0x02;
	lacpdu->partner_information_length = 0x14;
	for (index=0; index<=2; index++) {
		lacpdu->reserved_3_1[index]=0;
	}
	// lacpdu->partner_system_priority  updated on send
	// lacpdu->partner_system           updated on send
	// lacpdu->partner_key              updated on send
	// lacpdu->partner_port_priority    updated on send
	// lacpdu->partner_port             updated on send
	// lacpdu->partner_state            updated on send
	for (index=0; index<=2; index++) {
		lacpdu->reserved_3_2[index]=0;
	}
	lacpdu->tlv_type_collector_info = 0x03;
	lacpdu->collector_information_length= 0x10;
	lacpdu->collector_max_delay = htons(AD_COLLECTOR_MAX_DELAY);
	for (index=0; index<=11; index++) {
		lacpdu->reserved_12[index]=0;
	}
	lacpdu->tlv_type_terminator = 0x00;
	lacpdu->terminator_length = 0;
	for (index=0; index<=49; index++) {
		lacpdu->reserved_50[index]=0;
	}
}

//////////////////////////////////////////////////////////////////////////////////////
// ================= AD exported functions to the main bonding code ==================
//////////////////////////////////////////////////////////////////////////////////////

// Check aggregators status in team every T seconds
#define AD_AGGREGATOR_SELECTION_TIMER  8

static u16 aggregator_identifier;

/**
 * bond_3ad_initialize - initialize a bond's 802.3ad parameters and structures
 * @bond: bonding struct to work on
 * @tick_resolution: tick duration (millisecond resolution)
 * @lacp_fast: boolean. whether fast periodic should be used
 *
 * Can be called only after the mac address of the bond is set.
 */
void bond_3ad_initialize(struct bonding *bond, u16 tick_resolution, int lacp_fast)
{                         
	// check that the bond is not initialized yet
	if (MAC_ADDRESS_COMPARE(&(BOND_AD_INFO(bond).system.sys_mac_addr), &(bond->dev->dev_addr))) {

		aggregator_identifier = 0;

		BOND_AD_INFO(bond).lacp_fast = lacp_fast;
		BOND_AD_INFO(bond).system.sys_priority = 0xFFFF;
		BOND_AD_INFO(bond).system.sys_mac_addr = *((struct mac_addr *)bond->dev->dev_addr);

		// initialize how many times this module is called in one second(should be about every 100ms)
		ad_ticks_per_sec = tick_resolution;

		// initialize the aggregator selection timer(to activate an aggregation selection after initialize)
		BOND_AD_INFO(bond).agg_select_timer = (AD_AGGREGATOR_SELECTION_TIMER * ad_ticks_per_sec);
		BOND_AD_INFO(bond).agg_select_mode = AD_BANDWIDTH;
	}
}

/**
 * bond_3ad_bind_slave - initialize a slave's port
 * @slave: slave struct to work on
 *
 * Returns:   0 on success
 *          < 0 on error
 */
int bond_3ad_bind_slave(struct slave *slave)
{
	struct bonding *bond = bond_get_bond_by_slave(slave);
	struct port *port;
	struct aggregator *aggregator;

	if (bond == NULL) {
		printk(KERN_ERR DRV_NAME ": %s: The slave %s is not attached to its bond\n",
		       slave->dev->master->name, slave->dev->name);
		return -1;
	}

	//check that the slave has not been intialized yet.
	if (SLAVE_AD_INFO(slave).port.slave != slave) {

		// port initialization
		port = &(SLAVE_AD_INFO(slave).port);

		ad_initialize_port(port, BOND_AD_INFO(bond).lacp_fast);

		port->slave = slave;
		port->actor_port_number = SLAVE_AD_INFO(slave).id;
		// key is determined according to the link speed, duplex and user key(which is yet not supported)
		//              ------------------------------------------------------------
		// Port key :   | User key                       |      Speed       |Duplex|
		//              ------------------------------------------------------------
		//              16                               6               1 0
		port->actor_admin_port_key = 0;	// initialize this parameter
		port->actor_admin_port_key |= __get_duplex(port);
		port->actor_admin_port_key |= (__get_link_speed(port) << 1);
		port->actor_oper_port_key = port->actor_admin_port_key;
		// if the port is not full duplex, then the port should be not lacp Enabled
		if (!(port->actor_oper_port_key & AD_DUPLEX_KEY_BITS)) {
			port->sm_vars &= ~AD_PORT_LACP_ENABLED;
		}
		// actor system is the bond's system
		port->actor_system = BOND_AD_INFO(bond).system.sys_mac_addr;
		// tx timer(to verify that no more than MAX_TX_IN_SECOND lacpdu's are sent in one second)
		port->sm_tx_timer_counter = ad_ticks_per_sec/AD_MAX_TX_IN_SECOND;
		port->aggregator = NULL;
		port->next_port_in_aggregator = NULL;

		__disable_port(port);
		__initialize_port_locks(port);


		// aggregator initialization
		aggregator = &(SLAVE_AD_INFO(slave).aggregator);

		ad_initialize_agg(aggregator);

		aggregator->aggregator_mac_address = *((struct mac_addr *)bond->dev->dev_addr);
		aggregator->aggregator_identifier = (++aggregator_identifier);
		aggregator->slave = slave;
		aggregator->is_active = 0;
		aggregator->num_of_ports = 0;
	}

	return 0;
}

/**
 * bond_3ad_unbind_slave - deinitialize a slave's port
 * @slave: slave struct to work on
 *
 * Search for the aggregator that is related to this port, remove the
 * aggregator and assign another aggregator for other port related to it
 * (if any), and remove the port.
 */
void bond_3ad_unbind_slave(struct slave *slave)
{
	struct port *port, *prev_port, *temp_port;
	struct aggregator *aggregator, *new_aggregator, *temp_aggregator;
	int select_new_active_agg = 0;
	
	// find the aggregator related to this slave
	aggregator = &(SLAVE_AD_INFO(slave).aggregator);

	// find the port related to this slave
	port = &(SLAVE_AD_INFO(slave).port);

	// if slave is null, the whole port is not initialized
	if (!port->slave) {
		printk(KERN_WARNING DRV_NAME ": Warning: %s: Trying to "
		       "unbind an uninitialized port on %s\n",
		       slave->dev->master->name, slave->dev->name);
		return;
	}

	dprintk("Unbinding Link Aggregation Group %d\n", aggregator->aggregator_identifier);

	/* Tell the partner that this port is not suitable for aggregation */
	port->actor_oper_port_state &= ~AD_STATE_AGGREGATION;
	__update_lacpdu_from_port(port);
	ad_lacpdu_send(port);

	// check if this aggregator is occupied
	if (aggregator->lag_ports) {
		// check if there are other ports related to this aggregator except
		// the port related to this slave(thats ensure us that there is a
		// reason to search for new aggregator, and that we will find one
		if ((aggregator->lag_ports != port) || (aggregator->lag_ports->next_port_in_aggregator)) {
			// find new aggregator for the related port(s)
			new_aggregator = __get_first_agg(port);
			for (; new_aggregator; new_aggregator = __get_next_agg(new_aggregator)) {
				// if the new aggregator is empty, or it connected to to our port only
				if (!new_aggregator->lag_ports || ((new_aggregator->lag_ports == port) && !new_aggregator->lag_ports->next_port_in_aggregator)) {
					break;
				}
			}
			// if new aggregator found, copy the aggregator's parameters
			// and connect the related lag_ports to the new aggregator
			if ((new_aggregator) && ((!new_aggregator->lag_ports) || ((new_aggregator->lag_ports == port) && !new_aggregator->lag_ports->next_port_in_aggregator))) {
				dprintk("Some port(s) related to LAG %d - replaceing with LAG %d\n", aggregator->aggregator_identifier, new_aggregator->aggregator_identifier);

				if ((new_aggregator->lag_ports == port) && new_aggregator->is_active) {
					printk(KERN_INFO DRV_NAME ": %s: Removing an active aggregator\n",
					       aggregator->slave->dev->master->name);
					// select new active aggregator
					 select_new_active_agg = 1;
				}

				new_aggregator->is_individual = aggregator->is_individual;
				new_aggregator->actor_admin_aggregator_key = aggregator->actor_admin_aggregator_key;
				new_aggregator->actor_oper_aggregator_key = aggregator->actor_oper_aggregator_key;
				new_aggregator->partner_system = aggregator->partner_system;
				new_aggregator->partner_system_priority = aggregator->partner_system_priority;
				new_aggregator->partner_oper_aggregator_key = aggregator->partner_oper_aggregator_key;
				new_aggregator->receive_state = aggregator->receive_state;
				new_aggregator->transmit_state = aggregator->transmit_state;
				new_aggregator->lag_ports = aggregator->lag_ports;
				new_aggregator->is_active = aggregator->is_active;
				new_aggregator->num_of_ports = aggregator->num_of_ports;

				// update the information that is written on the ports about the aggregator
				for (temp_port=aggregator->lag_ports; temp_port; temp_port=temp_port->next_port_in_aggregator) {
					temp_port->aggregator=new_aggregator;
					temp_port->actor_port_aggregator_identifier = new_aggregator->aggregator_identifier;
				}

				// clear the aggregator
				ad_clear_agg(aggregator);
				
				if (select_new_active_agg) {
					ad_agg_selection_logic(__get_first_agg(port));
				}
			} else {
				printk(KERN_WARNING DRV_NAME ": %s: Warning: unbinding aggregator, "
				       "and could not find a new aggregator for its ports\n",
				       slave->dev->master->name);
			}
		} else { // in case that the only port related to this aggregator is the one we want to remove
			select_new_active_agg = aggregator->is_active;
			// clear the aggregator
			ad_clear_agg(aggregator);
			if (select_new_active_agg) {
				printk(KERN_INFO DRV_NAME ": %s: Removing an active aggregator\n",
				       slave->dev->master->name);
				// select new active aggregator
				ad_agg_selection_logic(__get_first_agg(port));
			}
		}
	}

	dprintk("Unbinding port %d\n", port->actor_port_number);
	// find the aggregator that this port is connected to
	temp_aggregator = __get_first_agg(port);
	for (; temp_aggregator; temp_aggregator = __get_next_agg(temp_aggregator)) {
		prev_port = NULL;
		// search the port in the aggregator's related ports
		for (temp_port=temp_aggregator->lag_ports; temp_port; prev_port=temp_port, temp_port=temp_port->next_port_in_aggregator) {
			if (temp_port == port) { // the aggregator found - detach the port from this aggregator
				if (prev_port) {
					prev_port->next_port_in_aggregator = temp_port->next_port_in_aggregator;
				} else {
					temp_aggregator->lag_ports = temp_port->next_port_in_aggregator;
				}
				temp_aggregator->num_of_ports--;
				if (temp_aggregator->num_of_ports==0) {
					select_new_active_agg = temp_aggregator->is_active;
					// clear the aggregator
					ad_clear_agg(temp_aggregator);
					if (select_new_active_agg) {
						printk(KERN_INFO DRV_NAME ": %s: Removing an active aggregator\n",
						       slave->dev->master->name);
						// select new active aggregator
						ad_agg_selection_logic(__get_first_agg(port));
					}
				}
				break;
			}
		}
	}
	port->slave=NULL;	
}

/**
 * bond_3ad_state_machine_handler - handle state machines timeout
 * @bond: bonding struct to work on
 *
 * The state machine handling concept in this module is to check every tick
 * which state machine should operate any function. The execution order is
 * round robin, so when we have an interaction between state machines, the
 * reply of one to each other might be delayed until next tick.
 *
 * This function also complete the initialization when the agg_select_timer
 * times out, and it selects an aggregator for the ports that are yet not
 * related to any aggregator, and selects the active aggregator for a bond.
 */
void bond_3ad_state_machine_handler(struct work_struct *work)
{
	struct bonding *bond = container_of(work, struct bonding,
					    ad_work.work);
	struct port *port;
	struct aggregator *aggregator;

	read_lock(&bond->lock);

	if (bond->kill_timers) {
		goto out;
	}

	//check if there are any slaves
	if (bond->slave_cnt == 0) {
		goto re_arm;
	}

	// check if agg_select_timer timer after initialize is timed out
	if (BOND_AD_INFO(bond).agg_select_timer && !(--BOND_AD_INFO(bond).agg_select_timer)) {
		// select the active aggregator for the bond
		if ((port = __get_first_port(bond))) {
			if (!port->slave) {
				printk(KERN_WARNING DRV_NAME ": %s: Warning: bond's first port is "
				       "uninitialized\n", bond->dev->name);
				goto re_arm;
			}

			aggregator = __get_first_agg(port);
			ad_agg_selection_logic(aggregator);
		}
	}

	// for each port run the state machines
	for (port = __get_first_port(bond); port; port = __get_next_port(port)) {
		if (!port->slave) {
			printk(KERN_WARNING DRV_NAME ": %s: Warning: Found an uninitialized "
			       "port\n", bond->dev->name);
			goto re_arm;
		}

		ad_rx_machine(NULL, port);
		ad_periodic_machine(port);
		ad_port_selection_logic(port);
		ad_mux_machine(port);
		ad_tx_machine(port);

		// turn off the BEGIN bit, since we already handled it
		if (port->sm_vars & AD_PORT_BEGIN) {
			port->sm_vars &= ~AD_PORT_BEGIN;
		}
	}

re_arm:
	queue_delayed_work(bond->wq, &bond->ad_work, ad_delta_in_ticks);
out:
	read_unlock(&bond->lock);
}

/**
 * bond_3ad_rx_indication - handle a received frame
 * @lacpdu: received lacpdu
 * @slave: slave struct to work on
 * @length: length of the data received
 *
 * It is assumed that frames that were sent on this NIC don't returned as new
 * received frames (loopback). Since only the payload is given to this
 * function, it check for loopback.
 */
static void bond_3ad_rx_indication(struct lacpdu *lacpdu, struct slave *slave, u16 length)
{
	struct port *port;

	if (length >= sizeof(struct lacpdu)) {

		port = &(SLAVE_AD_INFO(slave).port);

		if (!port->slave) {
			printk(KERN_WARNING DRV_NAME ": %s: Warning: port of slave %s is "
			       "uninitialized\n", slave->dev->name, slave->dev->master->name);
			return;
		}

		switch (lacpdu->subtype) {
		case AD_TYPE_LACPDU:
			dprintk("Received LACPDU on port %d\n", port->actor_port_number);
			ad_rx_machine(lacpdu, port);
			break;

		case AD_TYPE_MARKER:
			// No need to convert fields to Little Endian since we don't use the marker's fields.

			switch (((struct bond_marker *)lacpdu)->tlv_type) {
			case AD_MARKER_INFORMATION_SUBTYPE:
				dprintk("Received Marker Information on port %d\n", port->actor_port_number);
				ad_marker_info_received((struct bond_marker *)lacpdu, port);
				break;

			case AD_MARKER_RESPONSE_SUBTYPE:
				dprintk("Received Marker Response on port %d\n", port->actor_port_number);
				ad_marker_response_received((struct bond_marker *)lacpdu, port);
				break;

			default:
				dprintk("Received an unknown Marker subtype on slot %d\n", port->actor_port_number);
			}
		}
	}
}

/**
 * bond_3ad_adapter_speed_changed - handle a slave's speed change indication
 * @slave: slave struct to work on
 *
 * Handle reselection of aggregator (if needed) for this port.
 */
void bond_3ad_adapter_speed_changed(struct slave *slave)
{
	struct port *port;

	port = &(SLAVE_AD_INFO(slave).port);

	// if slave is null, the whole port is not initialized
	if (!port->slave) {
		printk(KERN_WARNING DRV_NAME ": Warning: %s: speed "
		       "changed for uninitialized port on %s\n",
		       slave->dev->master->name, slave->dev->name);
		return;
	}

	port->actor_admin_port_key &= ~AD_SPEED_KEY_BITS;
	port->actor_oper_port_key=port->actor_admin_port_key |= (__get_link_speed(port) << 1);
	dprintk("Port %d changed speed\n", port->actor_port_number);
	// there is no need to reselect a new aggregator, just signal the
	// state machines to reinitialize
	port->sm_vars |= AD_PORT_BEGIN;
}

/**
 * bond_3ad_adapter_duplex_changed - handle a slave's duplex change indication
 * @slave: slave struct to work on
 *
 * Handle reselection of aggregator (if needed) for this port.
 */
void bond_3ad_adapter_duplex_changed(struct slave *slave)
{
	struct port *port;

	port=&(SLAVE_AD_INFO(slave).port);

	// if slave is null, the whole port is not initialized
	if (!port->slave) {
		printk(KERN_WARNING DRV_NAME ": %s: Warning: duplex changed "
		       "for uninitialized port on %s\n",
		       slave->dev->master->name, slave->dev->name);
		return;
	}

	port->actor_admin_port_key &= ~AD_DUPLEX_KEY_BITS;
	port->actor_oper_port_key=port->actor_admin_port_key |= __get_duplex(port);
	dprintk("Port %d changed duplex\n", port->actor_port_number);
	// there is no need to reselect a new aggregator, just signal the
	// state machines to reinitialize
	port->sm_vars |= AD_PORT_BEGIN;
}

/**
 * bond_3ad_handle_link_change - handle a slave's link status change indication
 * @slave: slave struct to work on
 * @status: whether the link is now up or down
 *
 * Handle reselection of aggregator (if needed) for this port.
 */
void bond_3ad_handle_link_change(struct slave *slave, char link)
{
	struct port *port;

	port = &(SLAVE_AD_INFO(slave).port);

	// if slave is null, the whole port is not initialized
	if (!port->slave) {
		printk(KERN_WARNING DRV_NAME ": Warning: %s: link status changed for "
		       "uninitialized port on %s\n",
			slave->dev->master->name, slave->dev->name);
		return;
	}

	// on link down we are zeroing duplex and speed since some of the adaptors(ce1000.lan) report full duplex/speed instead of N/A(duplex) / 0(speed)
	// on link up we are forcing recheck on the duplex and speed since some of he adaptors(ce1000.lan) report
	if (link == BOND_LINK_UP) {
		port->is_enabled = 1;
		port->actor_admin_port_key &= ~AD_DUPLEX_KEY_BITS;
		port->actor_oper_port_key=port->actor_admin_port_key |= __get_duplex(port);
		port->actor_admin_port_key &= ~AD_SPEED_KEY_BITS;
		port->actor_oper_port_key=port->actor_admin_port_key |= (__get_link_speed(port) << 1);
	} else {
		/* link has failed */
		port->is_enabled = 0;
		port->actor_admin_port_key &= ~AD_DUPLEX_KEY_BITS;
		port->actor_oper_port_key= (port->actor_admin_port_key &= ~AD_SPEED_KEY_BITS);
	}
	//BOND_PRINT_DBG(("Port %d changed link status to %s", port->actor_port_number, ((link == BOND_LINK_UP)?"UP":"DOWN")));
	// there is no need to reselect a new aggregator, just signal the
	// state machines to reinitialize
	port->sm_vars |= AD_PORT_BEGIN;
}

/*
 * set link state for bonding master: if we have an active 
 * aggregator, we're up, if not, we're down.  Presumes that we cannot
 * have an active aggregator if there are no slaves with link up.
 *
 * This behavior complies with IEEE 802.3 section 43.3.9.
 *
 * Called by bond_set_carrier(). Return zero if carrier state does not
 * change, nonzero if it does.
 */
int bond_3ad_set_carrier(struct bonding *bond)
{
	if (__get_active_agg(&(SLAVE_AD_INFO(bond->first_slave).aggregator))) {
		if (!netif_carrier_ok(bond->dev)) {
			netif_carrier_on(bond->dev);
			return 1;
		}
		return 0;
	}

	if (netif_carrier_ok(bond->dev)) {
		netif_carrier_off(bond->dev);
		return 1;
	}
	return 0;
}

/**
 * bond_3ad_get_active_agg_info - get information of the active aggregator
 * @bond: bonding struct to work on
 * @ad_info: ad_info struct to fill with the bond's info
 *
 * Returns:   0 on success
 *          < 0 on error
 */
int bond_3ad_get_active_agg_info(struct bonding *bond, struct ad_info *ad_info)
{
	struct aggregator *aggregator = NULL;
	struct port *port;

	for (port = __get_first_port(bond); port; port = __get_next_port(port)) {
		if (port->aggregator && port->aggregator->is_active) {
			aggregator = port->aggregator;
			break;
		}
	}

	if (aggregator) {
		ad_info->aggregator_id = aggregator->aggregator_identifier;
		ad_info->ports = aggregator->num_of_ports;
		ad_info->actor_key = aggregator->actor_oper_aggregator_key;
		ad_info->partner_key = aggregator->partner_oper_aggregator_key;
		memcpy(ad_info->partner_system, aggregator->partner_system.mac_addr_value, ETH_ALEN);
		return 0;
	}

	return -1;
}

int bond_3ad_xmit_xor(struct sk_buff *skb, struct net_device *dev)
{
	struct slave *slave, *start_at;
	struct bonding *bond = dev->priv;
	int slave_agg_no;
	int slaves_in_agg;
	int agg_id;
	int i;
	struct ad_info ad_info;
	int res = 1;

	/* make sure that the slaves list will
	 * not change during tx
	 */
	read_lock(&bond->lock);

	if (!BOND_IS_OK(bond)) {
		goto out;
	}

	if (bond_3ad_get_active_agg_info(bond, &ad_info)) {
		printk(KERN_DEBUG DRV_NAME ": %s: Error: "
		       "bond_3ad_get_active_agg_info failed\n", dev->name);
		goto out;
	}

	slaves_in_agg = ad_info.ports;
	agg_id = ad_info.aggregator_id;

	if (slaves_in_agg == 0) {
		/*the aggregator is empty*/
		printk(KERN_DEBUG DRV_NAME ": %s: Error: active "
		       "aggregator is empty\n",
		       dev->name);
		goto out;
	}

	slave_agg_no = bond->xmit_hash_policy(skb, dev, slaves_in_agg);

	bond_for_each_slave(bond, slave, i) {
		struct aggregator *agg = SLAVE_AD_INFO(slave).port.aggregator;

		if (agg && (agg->aggregator_identifier == agg_id)) {
			slave_agg_no--;
			if (slave_agg_no < 0) {
				break;
			}
		}
	}

	if (slave_agg_no >= 0) {
		printk(KERN_ERR DRV_NAME ": %s: Error: Couldn't find a slave to tx on "
		       "for aggregator ID %d\n", dev->name, agg_id);
		goto out;
	}

	start_at = slave;

	bond_for_each_slave_from(bond, slave, i, start_at) {
		int slave_agg_id = 0;
		struct aggregator *agg = SLAVE_AD_INFO(slave).port.aggregator;

		if (agg) {
			slave_agg_id = agg->aggregator_identifier;
		}

		if (SLAVE_IS_OK(slave) && agg && (slave_agg_id == agg_id)) {
			res = bond_dev_queue_xmit(bond, skb, slave->dev);
			break;
		}
	}

out:
	if (res) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
	}
	read_unlock(&bond->lock);
	return 0;
}

int bond_3ad_lacpdu_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type* ptype, struct net_device *orig_dev)
{
	struct bonding *bond = dev->priv;
	struct slave *slave = NULL;
	int ret = NET_RX_DROP;

	if (dev->nd_net != &init_net)
		goto out;

	if (!(dev->flags & IFF_MASTER))
		goto out;

	read_lock(&bond->lock);
	slave = bond_get_slave_by_dev((struct bonding *)dev->priv, orig_dev);
	if (!slave)
		goto out_unlock;

	bond_3ad_rx_indication((struct lacpdu *) skb->data, slave, skb->len);

	ret = NET_RX_SUCCESS;

out_unlock:
	read_unlock(&bond->lock);
out:
	dev_kfree_skb(skb);

	return ret;
}

