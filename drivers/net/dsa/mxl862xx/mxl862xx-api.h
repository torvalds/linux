/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_API_H
#define __MXL862XX_API_H

#include <linux/bits.h>
#include <linux/if_ether.h>

/**
 * struct mdio_relay_data - relayed access to the switch internal MDIO bus
 * @data: data to be read or written
 * @phy: PHY index
 * @mmd: MMD device
 * @reg: register index
 */
struct mdio_relay_data {
	__le16 data;
	u8 phy;
	u8 mmd;
	__le16 reg;
} __packed;

/**
 * struct mxl862xx_register_mod - Register access parameter to directly
 *                                modify internal registers
 * @addr: Register address offset for modification
 * @data: Value to write to the register address
 * @mask: Mask of bits to be modified (1 to modify, 0 to ignore)
 *
 * Used for direct register modification operations.
 */
struct mxl862xx_register_mod {
	__le16 addr;
	__le16 data;
	__le16 mask;
} __packed;

/**
 * enum mxl862xx_mac_table_filter - Source/Destination MAC address filtering
 *
 * @MXL862XX_MAC_FILTER_NONE: no filter
 * @MXL862XX_MAC_FILTER_SRC: source address filter
 * @MXL862XX_MAC_FILTER_DEST: destination address filter
 * @MXL862XX_MAC_FILTER_BOTH: both source and destination filter
 */
enum mxl862xx_mac_table_filter {
	MXL862XX_MAC_FILTER_NONE = 0,
	MXL862XX_MAC_FILTER_SRC = BIT(0),
	MXL862XX_MAC_FILTER_DEST = BIT(1),
	MXL862XX_MAC_FILTER_BOTH = BIT(0) | BIT(1),
};

#define MXL862XX_TCI_VLAN_ID		GENMASK(11, 0)
#define MXL862XX_TCI_VLAN_CFI_DEI	BIT(12)
#define MXL862XX_TCI_VLAN_PRI		GENMASK(15, 13)

/* Set in port_id to use port_map[] as a portmap bitmap instead of a single
 * port ID. When clear, port_id selects one port; when set, the firmware
 * ignores the lower bits of port_id and writes port_map[] directly into
 * the PCE bridge port map.
 */
#define MXL862XX_PORTMAP_FLAG		BIT(31)

/**
 * struct mxl862xx_mac_table_add - MAC Table Entry to be added
 * @fid: Filtering Identifier (FID) (not supported by all switches)
 * @port_id: Ethernet Port number
 * @port_map: Bridge Port Map
 * @sub_if_id: Sub-Interface Identifier Destination
 * @age_timer: Aging Time in seconds
 * @vlan_id: STAG VLAN Id
 * @static_entry: Static Entry (value will be aged out if not set to static)
 * @traffic_class: Egress queue traffic class
 * @mac: MAC Address to add to the table
 * @filter_flag: See &enum mxl862xx_mac_table_filter
 * @igmp_controlled: Packet is marked as IGMP controlled if destination MAC
 *                   address matches MAC in this entry
 * @associated_mac: Associated Mac address
 * @tci: TCI for B-Step
 *	Bit [0:11] - VLAN ID
 *	Bit [12] - VLAN CFI/DEI
 *	Bit [13:15] - VLAN PRI
 */
struct mxl862xx_mac_table_add {
	__le16 fid;
	__le32 port_id;
	__le16 port_map[8];
	__le16 sub_if_id;
	__le32 age_timer;
	__le16 vlan_id;
	u8 static_entry;
	u8 traffic_class;
	u8 mac[ETH_ALEN];
	u8 filter_flag;
	u8 igmp_controlled;
	u8 associated_mac[ETH_ALEN];
	__le16 tci;
} __packed;

/**
 * struct mxl862xx_mac_table_remove - MAC Table Entry to be removed
 * @fid: Filtering Identifier (FID)
 * @mac: MAC Address to be removed from the table.
 * @filter_flag: See &enum mxl862xx_mac_table_filter
 * @tci: TCI for B-Step
 *	Bit [0:11] - VLAN ID
 *	Bit [12] - VLAN CFI/DEI
 *	Bit [13:15] - VLAN PRI
 */
struct mxl862xx_mac_table_remove {
	__le16 fid;
	u8 mac[ETH_ALEN];
	u8 filter_flag;
	__le16 tci;
} __packed;

/**
 * struct mxl862xx_mac_table_read - MAC Table Entry to be read
 * @initial: Restart the get operation from the beginning of the table
 * @last: Indicates that the read operation returned last entry
 * @fid: Get the MAC table entry belonging to the given Filtering Identifier
 * @port_id: The Bridge Port ID
 * @port_map: Bridge Port Map
 * @age_timer: Aging Time
 * @vlan_id: STAG VLAN Id
 * @static_entry: Indicates if this is a Static Entry
 * @sub_if_id: Sub-Interface Identifier Destination
 * @mac: MAC Address. Filled out by the switch API implementation.
 * @filter_flag: See &enum mxl862xx_mac_table_filter
 * @igmp_controlled: Packet is marked as IGMP controlled if destination MAC
 *                   address matches the MAC in this entry
 * @entry_changed: Indicate if the Entry has Changed
 * @associated_mac: Associated MAC address
 * @hit_status: MAC Table Hit Status Update
 * @tci: TCI for B-Step
 *	Bit [0:11] - VLAN ID
 *	Bit [12] - VLAN CFI/DEI
 *	Bit [13:15] - VLAN PRI
 * @first_bridge_port_id: The port this MAC address has first been learned.
 *                        This is used for loop detection.
 */
struct mxl862xx_mac_table_read {
	u8 initial;
	u8 last;
	__le16 fid;
	__le32 port_id;
	__le16 port_map[8];
	__le32 age_timer;
	__le16 vlan_id;
	u8 static_entry;
	__le16 sub_if_id;
	u8 mac[ETH_ALEN];
	u8 filter_flag;
	u8 igmp_controlled;
	u8 entry_changed;
	u8 associated_mac[ETH_ALEN];
	u8 hit_status;
	__le16 tci;
	__le16 first_bridge_port_id;
} __packed;

/**
 * struct mxl862xx_mac_table_query - MAC Table Entry key-based lookup
 * @mac: MAC Address to search for (input)
 * @fid: Filtering Identifier (input)
 * @found: Set by firmware: 1 if entry was found, 0 if not
 * @port_id: Bridge Port ID (output; MSB set if portmap mode)
 * @port_map: Bridge Port Map (output; valid for static entries)
 * @sub_if_id: Sub-Interface Identifier Destination
 * @age_timer: Aging Time
 * @vlan_id: STAG VLAN Id
 * @static_entry: Indicates if this is a Static Entry
 * @filter_flag: See &enum mxl862xx_mac_table_filter (input+output)
 * @igmp_controlled: IGMP controlled flag
 * @entry_changed: Entry changed flag
 * @associated_mac: Associated MAC address
 * @hit_status: MAC Table Hit Status Update
 * @tci: TCI (VLAN ID + CFI/DEI + PRI) (input)
 * @first_bridge_port_id: First learned bridge port
 */
struct mxl862xx_mac_table_query {
	u8 mac[ETH_ALEN];
	__le16 fid;
	u8 found;
	__le32 port_id;
	__le16 port_map[8];
	__le16 sub_if_id;
	__le32 age_timer;
	__le16 vlan_id;
	u8 static_entry;
	u8 filter_flag;
	u8 igmp_controlled;
	u8 entry_changed;
	u8 associated_mac[ETH_ALEN];
	u8 hit_status;
	__le16 tci;
	__le16 first_bridge_port_id;
} __packed;

/**
 * enum mxl862xx_mac_clear_type - MAC table clear type
 * @MXL862XX_MAC_CLEAR_PHY_PORT: clear dynamic entries based on port_id
 * @MXL862XX_MAC_CLEAR_DYNAMIC: clear all dynamic entries
 */
enum mxl862xx_mac_clear_type {
	MXL862XX_MAC_CLEAR_PHY_PORT = 0,
	MXL862XX_MAC_CLEAR_DYNAMIC,
};

/**
 * struct mxl862xx_mac_table_clear - MAC table clear
 * @type: see &enum mxl862xx_mac_clear_type
 * @port_id: physical port id
 */
struct mxl862xx_mac_table_clear {
	u8 type;
	u8 port_id;
} __packed;

/**
 * enum mxl862xx_age_timer - Aging Timer Value.
 * @MXL862XX_AGETIMER_1_SEC: 1 second aging time
 * @MXL862XX_AGETIMER_10_SEC: 10 seconds aging time
 * @MXL862XX_AGETIMER_300_SEC: 300 seconds aging time
 * @MXL862XX_AGETIMER_1_HOUR: 1 hour aging time
 * @MXL862XX_AGETIMER_1_DAY: 24 hours aging time
 * @MXL862XX_AGETIMER_CUSTOM: Custom aging time in seconds
 */
enum mxl862xx_age_timer {
	MXL862XX_AGETIMER_1_SEC = 1,
	MXL862XX_AGETIMER_10_SEC,
	MXL862XX_AGETIMER_300_SEC,
	MXL862XX_AGETIMER_1_HOUR,
	MXL862XX_AGETIMER_1_DAY,
	MXL862XX_AGETIMER_CUSTOM,
};

/**
 * struct mxl862xx_bridge_alloc - Bridge Allocation
 * @bridge_id: If the bridge allocation is successful, a valid ID will be
 *             returned in this field. Otherwise, INVALID_HANDLE is
 *             returned. For bridge free, this field should contain a
 *             valid ID returned by the bridge allocation. ID 0 is not
 *             used for historic reasons.
 *
 * Used by MXL862XX_BRIDGE_ALLOC and MXL862XX_BRIDGE_FREE.
 */
struct mxl862xx_bridge_alloc {
	__le16 bridge_id;
};

/**
 * enum mxl862xx_bridge_config_mask - Bridge configuration mask
 * @MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNING_LIMIT:
 *     Mask for mac_learning_limit_enable and mac_learning_limit.
 * @MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNED_COUNT:
 *     Mask for mac_learning_count
 * @MXL862XX_BRIDGE_CONFIG_MASK_MAC_DISCARD_COUNT:
 *     Mask for learning_discard_event
 * @MXL862XX_BRIDGE_CONFIG_MASK_SUB_METER:
 *     Mask for sub_metering_enable and traffic_sub_meter_id
 * @MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE:
 *     Mask for forward_broadcast, forward_unknown_multicast_ip,
 *     forward_unknown_multicast_non_ip and forward_unknown_unicast.
 * @MXL862XX_BRIDGE_CONFIG_MASK_ALL: Enable all
 * @MXL862XX_BRIDGE_CONFIG_MASK_FORCE: Bypass any check for debug purpose
 */
enum mxl862xx_bridge_config_mask {
	MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNING_LIMIT = BIT(0),
	MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNED_COUNT = BIT(1),
	MXL862XX_BRIDGE_CONFIG_MASK_MAC_DISCARD_COUNT = BIT(2),
	MXL862XX_BRIDGE_CONFIG_MASK_SUB_METER = BIT(3),
	MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE = BIT(4),
	MXL862XX_BRIDGE_CONFIG_MASK_ALL = 0x7FFFFFFF,
	MXL862XX_BRIDGE_CONFIG_MASK_FORCE = BIT(31)
};

/**
 * enum mxl862xx_bridge_port_egress_meter - Meters for egress traffic type
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST:
 *     Index of broadcast traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_MULTICAST:
 *     Index of known multicast traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP:
 *     Index of unknown multicast IP traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP:
 *     Index of unknown multicast non-IP traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC:
 *     Index of unknown unicast traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_OTHERS:
 *     Index of traffic meter for other types
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX: Number of index
 */
enum mxl862xx_bridge_port_egress_meter {
	MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST = 0,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_MULTICAST,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_OTHERS,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX,
};

/**
 * struct mxl862xx_qos_meter_cfg - Rate meter configuration
 * @enable: Enable/disable meter
 * @meter_id: Meter ID (assigned by firmware on alloc)
 * @meter_name: Meter name string
 * @meter_type: Meter algorithm type (srTCM = 0, trTCM = 1)
 * @cbs: Committed Burst Size (in bytes)
 * @res1: Reserved
 * @ebs: Excess Burst Size (in bytes)
 * @res2: Reserved
 * @rate: Committed Information Rate (in kbit/s)
 * @pi_rate: Peak Information Rate (in kbit/s)
 * @colour_blind_mode: Colour-blind mode enable
 * @pkt_mode: Packet mode enable
 * @local_overhd: Local overhead accounting enable
 * @local_overhd_val: Local overhead accounting value
 */
struct mxl862xx_qos_meter_cfg {
	u8 enable;
	__le16 meter_id;
	char meter_name[32];
	__le32 meter_type;
	__le32 cbs;
	__le32 res1;
	__le32 ebs;
	__le32 res2;
	__le32 rate;
	__le32 pi_rate;
	u8 colour_blind_mode;
	u8 pkt_mode;
	u8 local_overhd;
	__le16 local_overhd_val;
} __packed;

/**
 * enum mxl862xx_bridge_forward_mode - Bridge forwarding type of packet
 * @MXL862XX_BRIDGE_FORWARD_FLOOD: Packet is flooded to port members of
 *                                 ingress bridge port
 * @MXL862XX_BRIDGE_FORWARD_DISCARD: Packet is discarded
 */
enum mxl862xx_bridge_forward_mode {
	MXL862XX_BRIDGE_FORWARD_FLOOD = 0,
	MXL862XX_BRIDGE_FORWARD_DISCARD,
};

/**
 * struct mxl862xx_bridge_config - Bridge Configuration
 * @bridge_id: Bridge ID (FID)
 * @mask: See &enum mxl862xx_bridge_config_mask
 * @mac_learning_limit_enable: Enable MAC learning limitation
 * @mac_learning_limit: Max number of MAC addresses that can be learned in
 *                      this bridge (all bridge ports)
 * @mac_learning_count: Number of MAC addresses learned from this bridge
 * @learning_discard_event: Number of learning discard events due to
 *                          hardware resource not available
 * @sub_metering_enable: Traffic metering on type of traffic (such as
 *                       broadcast, multicast, unknown unicast, etc) applies
 * @traffic_sub_meter_id: Meter for bridge process with specific type (such
 *                        as broadcast, multicast, unknown unicast, etc)
 * @forward_broadcast: Forwarding mode of broadcast traffic. See
 *                     &enum mxl862xx_bridge_forward_mode
 * @forward_unknown_multicast_ip: Forwarding mode of unknown multicast IP
 *                                traffic.
 *                                See &enum mxl862xx_bridge_forward_mode
 * @forward_unknown_multicast_non_ip: Forwarding mode of unknown multicast
 *                                    non-IP traffic.
 *                                    See &enum mxl862xx_bridge_forward_mode
 * @forward_unknown_unicast: Forwarding mode of unknown unicast traffic. See
 *                           &enum mxl862xx_bridge_forward_mode
 */
struct mxl862xx_bridge_config {
	__le16 bridge_id;
	__le32 mask; /* enum mxl862xx_bridge_config_mask */
	u8 mac_learning_limit_enable;
	__le16 mac_learning_limit;
	__le16 mac_learning_count;
	__le32 learning_discard_event;
	u8 sub_metering_enable[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	__le16 traffic_sub_meter_id[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	__le32 forward_broadcast; /* enum mxl862xx_bridge_forward_mode */
	__le32 forward_unknown_multicast_ip; /* enum mxl862xx_bridge_forward_mode */
	__le32 forward_unknown_multicast_non_ip; /* enum mxl862xx_bridge_forward_mode */
	__le32 forward_unknown_unicast; /* enum mxl862xx_bridge_forward_mode */
} __packed;

/**
 * struct mxl862xx_bridge_port_alloc - Bridge Port Allocation
 * @bridge_port_id: If the bridge port allocation is successful, a valid ID
 *                  will be returned in this field. Otherwise, INVALID_HANDLE
 *                  is returned. For bridge port free, this field should
 *                  contain a valid ID returned by the bridge port allocation.
 *
 * Used by MXL862XX_BRIDGE_PORT_ALLOC and MXL862XX_BRIDGE_PORT_FREE.
 */
struct mxl862xx_bridge_port_alloc {
	__le16 bridge_port_id;
};

/**
 * enum mxl862xx_bridge_port_config_mask - Bridge Port configuration mask
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID:
 *     Mask for bridge_id
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN:
 *     Mask for ingress_extended_vlan_enable,
 *     ingress_extended_vlan_block_id and ingress_extended_vlan_block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN:
 *     Mask for egress_extended_vlan_enable, egress_extended_vlan_block_id
 *     and egress_extended_vlan_block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_MARKING:
 *     Mask for ingress_marking_mode
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_REMARKING:
 *     Mask for egress_remarking_mode
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_METER:
 *     Mask for ingress_metering_enable and ingress_traffic_meter_id
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER:
 *     Mask for egress_sub_metering_enable and egress_traffic_sub_meter_id
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING:
 *     Mask for dest_logical_port_id, pmapper_enable, dest_sub_if_id_group,
 *     pmapper_mapping_mode, pmapper_id_valid and pmapper
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP:
 *     Mask for bridge_port_map
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_IP_LOOKUP:
 *     Mask for mc_dest_ip_lookup_disable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_IP_LOOKUP:
 *     Mask for mc_src_ip_lookup_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_MAC_LOOKUP:
 *     Mask for dest_mac_lookup_disable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING:
 *     Mask for src_mac_learning_disable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_SPOOFING:
 *     Mask for mac_spoofing_detect_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_PORT_LOCK:
 *     Mask for port_lock_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNING_LIMIT:
 *     Mask for mac_learning_limit_enable and mac_learning_limit
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNED_COUNT:
 *     Mask for mac_learning_count
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN_FILTER:
 *     Mask for ingress_vlan_filter_enable, ingress_vlan_filter_block_id
 *     and ingress_vlan_filter_block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER1:
 *     Mask for bypass_egress_vlan_filter1, egress_vlan_filter1enable,
 *     egress_vlan_filter1block_id and egress_vlan_filter1block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER2:
 *     Mask for egress_vlan_filter2enable, egress_vlan_filter2block_id and
 *     egress_vlan_filter2block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING:
 *     Mask for vlan_tag_selection, vlan_src_mac_priority_enable,
 *     vlan_src_mac_dei_enable, vlan_src_mac_vid_enable,
 *     vlan_dst_mac_priority_enable, vlan_dst_mac_dei_enable and
 *     vlan_dst_mac_vid_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MULTICAST_LOOKUP:
 *     Mask for vlan_multicast_priority_enable,
 *     vlan_multicast_dei_enable and vlan_multicast_vid_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_LOOP_VIOLATION_COUNTER:
 *     Mask for loop_violation_count
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_ALL: Enable all
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_FORCE: Bypass any check for debug purpose
 */
enum mxl862xx_bridge_port_config_mask {
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID = BIT(0),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN = BIT(1),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN = BIT(2),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_MARKING = BIT(3),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_REMARKING = BIT(4),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_METER = BIT(5),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER = BIT(6),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING = BIT(7),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP = BIT(8),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_IP_LOOKUP = BIT(9),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_IP_LOOKUP = BIT(10),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_MAC_LOOKUP = BIT(11),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING = BIT(12),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_SPOOFING = BIT(13),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_PORT_LOCK = BIT(14),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNING_LIMIT = BIT(15),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNED_COUNT = BIT(16),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN_FILTER = BIT(17),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER1 = BIT(18),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER2 = BIT(19),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING = BIT(20),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MULTICAST_LOOKUP = BIT(21),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_LOOP_VIOLATION_COUNTER = BIT(22),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_ALL = 0x7FFFFFFF,
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_FORCE = BIT(31)
};

/**
 * enum mxl862xx_color_marking_mode - Color Marking Mode
 * @MXL862XX_MARKING_ALL_GREEN: mark packets (except critical) to green
 * @MXL862XX_MARKING_INTERNAL_MARKING: do not change color and priority
 * @MXL862XX_MARKING_DEI: DEI mark mode
 * @MXL862XX_MARKING_PCP_8P0D: PCP 8P0D mark mode
 * @MXL862XX_MARKING_PCP_7P1D: PCP 7P1D mark mode
 * @MXL862XX_MARKING_PCP_6P2D: PCP 6P2D mark mode
 * @MXL862XX_MARKING_PCP_5P3D: PCP 5P3D mark mode
 * @MXL862XX_MARKING_DSCP_AF: DSCP AF class
 */
enum mxl862xx_color_marking_mode {
	MXL862XX_MARKING_ALL_GREEN = 0,
	MXL862XX_MARKING_INTERNAL_MARKING,
	MXL862XX_MARKING_DEI,
	MXL862XX_MARKING_PCP_8P0D,
	MXL862XX_MARKING_PCP_7P1D,
	MXL862XX_MARKING_PCP_6P2D,
	MXL862XX_MARKING_PCP_5P3D,
	MXL862XX_MARKING_DSCP_AF,
};

/**
 * enum mxl862xx_color_remarking_mode - Color Remarking Mode
 * @MXL862XX_REMARKING_NONE: values from last process stage
 * @MXL862XX_REMARKING_DEI: DEI mark mode
 * @MXL862XX_REMARKING_PCP_8P0D: PCP 8P0D mark mode
 * @MXL862XX_REMARKING_PCP_7P1D: PCP 7P1D mark mode
 * @MXL862XX_REMARKING_PCP_6P2D: PCP 6P2D mark mode
 * @MXL862XX_REMARKING_PCP_5P3D: PCP 5P3D mark mode
 * @MXL862XX_REMARKING_DSCP_AF: DSCP AF class
 */
enum mxl862xx_color_remarking_mode {
	MXL862XX_REMARKING_NONE = 0,
	MXL862XX_REMARKING_DEI = 2,
	MXL862XX_REMARKING_PCP_8P0D,
	MXL862XX_REMARKING_PCP_7P1D,
	MXL862XX_REMARKING_PCP_6P2D,
	MXL862XX_REMARKING_PCP_5P3D,
	MXL862XX_REMARKING_DSCP_AF,
};

/**
 * enum mxl862xx_pmapper_mapping_mode - P-mapper Mapping Mode
 * @MXL862XX_PMAPPER_MAPPING_PCP: Use PCP for VLAN tagged packets to derive
 *                                sub interface ID group
 * @MXL862XX_PMAPPER_MAPPING_LAG: Use LAG Index for Pmapper access
 *                                regardless of IP and VLAN packet
 * @MXL862XX_PMAPPER_MAPPING_DSCP: Use DSCP for VLAN tagged IP packets to
 *                                 derive sub interface ID group
 */
enum mxl862xx_pmapper_mapping_mode {
	MXL862XX_PMAPPER_MAPPING_PCP = 0,
	MXL862XX_PMAPPER_MAPPING_LAG,
	MXL862XX_PMAPPER_MAPPING_DSCP,
};

/**
 * struct mxl862xx_pmapper - P-mapper Configuration
 * @pmapper_id: Index of P-mapper (0-31)
 * @dest_sub_if_id_group: Sub interface ID group. Entry 0 is for non-IP and
 *                        non-VLAN tagged packets.
 *                        Entries 1-8 are PCP mapping entries for VLAN tagged
 *                        packets.
 *                        Entries 9-72 are DSCP or LAG mapping entries.
 *
 * Used by CTP port config and bridge port config. In case of LAG, it is
 * user's responsibility to provide the mapped entries in given P-mapper
 * table. In other modes the entries are auto mapped from input packet.
 */
struct mxl862xx_pmapper {
	__le16 pmapper_id;
	u8 dest_sub_if_id_group[73];
} __packed;

/**
 * struct mxl862xx_bridge_port_config - Bridge Port Configuration
 * @bridge_port_id: Bridge Port ID allocated by bridge port allocation
 * @mask: See &enum mxl862xx_bridge_port_config_mask
 * @bridge_id: Bridge ID (FID) to which this bridge port is associated
 * @ingress_extended_vlan_enable: Enable extended VLAN processing for
 *                                ingress traffic
 * @ingress_extended_vlan_block_id: Extended VLAN block allocated for
 *                                  ingress traffic
 * @ingress_extended_vlan_block_size: Extended VLAN block size for ingress
 *                                    traffic
 * @egress_extended_vlan_enable: Enable extended VLAN processing for egress
 *                               traffic
 * @egress_extended_vlan_block_id: Extended VLAN block allocated for egress
 *                                 traffic
 * @egress_extended_vlan_block_size: Extended VLAN block size for egress
 *                                   traffic
 * @ingress_marking_mode: Ingress color marking mode. See
 *                        &enum mxl862xx_color_marking_mode
 * @egress_remarking_mode: Color remarking for egress traffic. See
 *                         &enum mxl862xx_color_remarking_mode
 * @ingress_metering_enable: Traffic metering on ingress traffic applies
 * @ingress_traffic_meter_id: Meter for ingress Bridge Port process
 * @egress_sub_metering_enable: Traffic metering on various types of egress
 *                              traffic
 * @egress_traffic_sub_meter_id: Meter for egress Bridge Port process with
 *                               specific type
 * @dest_logical_port_id: Destination logical port
 * @pmapper_enable: Enable P-mapper
 * @dest_sub_if_id_group: Destination sub interface ID group when
 *                        pmapper_enable is false
 * @pmapper_mapping_mode: P-mapper mapping mode. See
 *                        &enum mxl862xx_pmapper_mapping_mode
 * @pmapper_id_valid: When true, P-mapper is re-used; when false,
 *                    allocation is handled by API
 * @pmapper: P-mapper configuration used when pmapper_enable is true
 * @bridge_port_map: Port map defining broadcast domain. Each bit
 *                   represents one bridge port. Bridge port ID is
 *                   index * 16 + bit offset.
 * @mc_dest_ip_lookup_disable: Disable multicast IP destination table
 *                             lookup
 * @mc_src_ip_lookup_enable: Enable multicast IP source table lookup
 * @dest_mac_lookup_disable: Disable destination MAC lookup; packet treated
 *                           as unknown
 * @src_mac_learning_disable: Disable source MAC address learning
 * @mac_spoofing_detect_enable: Enable MAC spoofing detection
 * @port_lock_enable: Enable port locking
 * @mac_learning_limit_enable: Enable MAC learning limitation
 * @mac_learning_limit: Maximum number of MAC addresses that can be learned
 *                      from this bridge port
 * @loop_violation_count: Number of loop violation events from this bridge
 *                        port
 * @mac_learning_count: Number of MAC addresses learned from this bridge
 *                      port
 * @ingress_vlan_filter_enable: Enable ingress VLAN filter
 * @ingress_vlan_filter_block_id: VLAN filter block of ingress traffic
 * @ingress_vlan_filter_block_size: VLAN filter block size for ingress
 *                                  traffic
 * @bypass_egress_vlan_filter1: For ingress traffic, bypass VLAN filter 1
 *                              at egress bridge port processing
 * @egress_vlan_filter1enable: Enable egress VLAN filter 1
 * @egress_vlan_filter1block_id: VLAN filter block 1 of egress traffic
 * @egress_vlan_filter1block_size: VLAN filter block 1 size
 * @egress_vlan_filter2enable: Enable egress VLAN filter 2
 * @egress_vlan_filter2block_id: VLAN filter block 2 of egress traffic
 * @egress_vlan_filter2block_size: VLAN filter block 2 size
 * @vlan_tag_selection: VLAN tag selection for MAC address/multicast
 *                      learning, lookup and filtering.
 *                      0 - Intermediate outer VLAN tag is used.
 *                      1 - Original outer VLAN tag is used.
 * @vlan_src_mac_priority_enable: Enable VLAN Priority field for source MAC
 *                                learning and filtering
 * @vlan_src_mac_dei_enable: Enable VLAN DEI/CFI field for source MAC
 *                           learning and filtering
 * @vlan_src_mac_vid_enable: Enable VLAN ID field for source MAC learning
 *                           and filtering
 * @vlan_dst_mac_priority_enable: Enable VLAN Priority field for destination
 *                                MAC lookup and filtering
 * @vlan_dst_mac_dei_enable: Enable VLAN CFI/DEI field for destination MAC
 *                           lookup and filtering
 * @vlan_dst_mac_vid_enable: Enable VLAN ID field for destination MAC lookup
 *                           and filtering
 * @vlan_multicast_priority_enable: Enable VLAN Priority field for IP
 *                                  multicast lookup
 * @vlan_multicast_dei_enable: Enable VLAN CFI/DEI field for IP multicast
 *                             lookup
 * @vlan_multicast_vid_enable: Enable VLAN ID field for IP multicast lookup
 */
struct mxl862xx_bridge_port_config {
	__le16 bridge_port_id;
	__le32 mask; /* enum mxl862xx_bridge_port_config_mask */
	__le16 bridge_id;
	u8 ingress_extended_vlan_enable;
	__le16 ingress_extended_vlan_block_id;
	__le16 ingress_extended_vlan_block_size;
	u8 egress_extended_vlan_enable;
	__le16 egress_extended_vlan_block_id;
	__le16 egress_extended_vlan_block_size;
	__le32 ingress_marking_mode; /* enum mxl862xx_color_marking_mode */
	__le32 egress_remarking_mode; /* enum mxl862xx_color_remarking_mode */
	u8 ingress_metering_enable;
	__le16 ingress_traffic_meter_id;
	u8 egress_sub_metering_enable[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	__le16 egress_traffic_sub_meter_id[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	u8 dest_logical_port_id;
	u8 pmapper_enable;
	__le16 dest_sub_if_id_group;
	__le32 pmapper_mapping_mode; /* enum mxl862xx_pmapper_mapping_mode */
	u8 pmapper_id_valid;
	struct mxl862xx_pmapper pmapper;
	__le16 bridge_port_map[8];
	u8 mc_dest_ip_lookup_disable;
	u8 mc_src_ip_lookup_enable;
	u8 dest_mac_lookup_disable;
	u8 src_mac_learning_disable;
	u8 mac_spoofing_detect_enable;
	u8 port_lock_enable;
	u8 mac_learning_limit_enable;
	__le16 mac_learning_limit;
	__le16 loop_violation_count;
	__le16 mac_learning_count;
	u8 ingress_vlan_filter_enable;
	__le16 ingress_vlan_filter_block_id;
	__le16 ingress_vlan_filter_block_size;
	u8 bypass_egress_vlan_filter1;
	u8 egress_vlan_filter1enable;
	__le16 egress_vlan_filter1block_id;
	__le16 egress_vlan_filter1block_size;
	u8 egress_vlan_filter2enable;
	__le16 egress_vlan_filter2block_id;
	__le16 egress_vlan_filter2block_size;
	u8 vlan_tag_selection;
	u8 vlan_src_mac_priority_enable;
	u8 vlan_src_mac_dei_enable;
	u8 vlan_src_mac_vid_enable;
	u8 vlan_dst_mac_priority_enable;
	u8 vlan_dst_mac_dei_enable;
	u8 vlan_dst_mac_vid_enable;
	u8 vlan_multicast_priority_enable;
	u8 vlan_multicast_dei_enable;
	u8 vlan_multicast_vid_enable;
} __packed;

/**
 * struct mxl862xx_cfg -  Global Switch configuration Attributes
 * @mac_table_age_timer: See &enum mxl862xx_age_timer
 * @age_timer: Custom MAC table aging timer in seconds
 * @max_packet_len: Maximum Ethernet packet length
 * @learning_limit_action: Automatic MAC address table learning limitation
 *                         consecutive action
 * @mac_locking_action: Accept or discard MAC port locking violation
 *                      packets
 * @mac_spoofing_action: Accept or discard MAC spoofing and port MAC locking
 *                       violation packets
 * @pause_mac_mode_src: Pause frame MAC source address mode
 * @pause_mac_src: Pause frame MAC source address
 */
struct mxl862xx_cfg {
	__le32 mac_table_age_timer; /* enum mxl862xx_age_timer */
	__le32 age_timer;
	__le16 max_packet_len;
	u8 learning_limit_action;
	u8 mac_locking_action;
	u8 mac_spoofing_action;
	u8 pause_mac_mode_src;
	u8 pause_mac_src[ETH_ALEN];
} __packed;

/**
 * enum mxl862xx_extended_vlan_filter_type - Extended VLAN filter tag type
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NORMAL: Normal tagged
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_FILTER: No filter (wildcard)
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT: Default entry
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_TAG: Untagged
 */
enum mxl862xx_extended_vlan_filter_type {
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NORMAL = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_FILTER = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT = 2,
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_TAG = 3,
};

/**
 * enum mxl862xx_extended_vlan_filter_tpid - Extended VLAN filter TPID
 * @MXL862XX_EXTENDEDVLAN_FILTER_TPID_NO_FILTER: No TPID filter
 * @MXL862XX_EXTENDEDVLAN_FILTER_TPID_8021Q: 802.1Q TPID
 * @MXL862XX_EXTENDEDVLAN_FILTER_TPID_VTETYPE: VLAN type extension
 */
enum mxl862xx_extended_vlan_filter_tpid {
	MXL862XX_EXTENDEDVLAN_FILTER_TPID_NO_FILTER = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_TPID_8021Q = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_TPID_VTETYPE = 2,
};

/**
 * enum mxl862xx_extended_vlan_filter_dei - Extended VLAN filter DEI
 * @MXL862XX_EXTENDEDVLAN_FILTER_DEI_NO_FILTER: No DEI filter
 * @MXL862XX_EXTENDEDVLAN_FILTER_DEI_0: DEI = 0
 * @MXL862XX_EXTENDEDVLAN_FILTER_DEI_1: DEI = 1
 */
enum mxl862xx_extended_vlan_filter_dei {
	MXL862XX_EXTENDEDVLAN_FILTER_DEI_NO_FILTER = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_DEI_0 = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_DEI_1 = 2,
};

/**
 * enum mxl862xx_extended_vlan_treatment_remove_tag - Tag removal action
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG: Do not remove tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG: Remove one tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_2_TAG: Remove two tags
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM: Discard frame
 */
enum mxl862xx_extended_vlan_treatment_remove_tag {
	MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_2_TAG = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM = 3,
};

/**
 * enum mxl862xx_extended_vlan_treatment_priority - Treatment priority mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_PRIORITY_VAL: Use explicit value
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_PRIORITY: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_PRIORITY: Copy from outer tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DSCP: Derive from DSCP
 */
enum mxl862xx_extended_vlan_treatment_priority {
	MXL862XX_EXTENDEDVLAN_TREATMENT_PRIORITY_VAL = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_PRIORITY = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_PRIORITY = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DSCP = 3,
};

/**
 * enum mxl862xx_extended_vlan_treatment_vid - Treatment VID mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL: Use explicit VID value
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_VID: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_VID: Copy from outer tag
 */
enum mxl862xx_extended_vlan_treatment_vid {
	MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_VID = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_VID = 2,
};

/**
 * enum mxl862xx_extended_vlan_treatment_tpid - Treatment TPID mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_TPID: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_TPID: Copy from outer tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_VTETYPE: Use VLAN type extension
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q: Use 802.1Q TPID
 */
enum mxl862xx_extended_vlan_treatment_tpid {
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_TPID = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_TPID = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_VTETYPE = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q = 3,
};

/**
 * enum mxl862xx_extended_vlan_treatment_dei - Treatment DEI mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_DEI: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_DEI: Copy from outer tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_0: Set DEI to 0
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_1: Set DEI to 1
 */
enum mxl862xx_extended_vlan_treatment_dei {
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_DEI = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_DEI = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_0 = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_1 = 3,
};

/**
 * enum mxl862xx_extended_vlan_4_tpid_mode - 4-TPID mode selector
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_1: VLAN TPID type 1
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_2: VLAN TPID type 2
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_3: VLAN TPID type 3
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_4: VLAN TPID type 4
 */
enum mxl862xx_extended_vlan_4_tpid_mode {
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_1 = 0,
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_2 = 1,
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_3 = 2,
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_4 = 3,
};

/**
 * enum mxl862xx_extended_vlan_filter_ethertype - Filter EtherType match
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_NO_FILTER: No filter
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPOE: IPoE
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_PPPOE: PPPoE
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_ARP: ARP
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPV6IPOE: IPv6 IPoE
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_EAPOL: EAPOL
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV4: DHCPv4
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV6: DHCPv6
 */
enum mxl862xx_extended_vlan_filter_ethertype {
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_NO_FILTER = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPOE = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_PPPOE = 2,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_ARP = 3,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPV6IPOE = 4,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_EAPOL = 5,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV4 = 6,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV6 = 7,
};

/**
 * struct mxl862xx_extendedvlan_filter_vlan - Per-tag filter in Extended VLAN
 * @type: Tag presence/type match (see &enum mxl862xx_extended_vlan_filter_type)
 * @priority_enable: Enable PCP value matching
 * @priority_val: PCP value to match
 * @vid_enable: Enable VID matching
 * @vid_val: VID value to match
 * @tpid: TPID match mode (see &enum mxl862xx_extended_vlan_filter_tpid)
 * @dei: DEI match mode (see &enum mxl862xx_extended_vlan_filter_dei)
 */
struct mxl862xx_extendedvlan_filter_vlan {
	__le32 type;
	u8 priority_enable;
	__le32 priority_val;
	u8 vid_enable;
	__le32 vid_val;
	__le32 tpid;
	__le32 dei;
} __packed;

/**
 * struct mxl862xx_extendedvlan_filter - Extended VLAN filter configuration
 * @original_packet_filter_mode: If true, filter on original (pre-treatment)
 *                               packet
 * @filter_4_tpid_mode: 4-TPID mode (see &enum mxl862xx_extended_vlan_4_tpid_mode)
 * @outer_vlan: Outer VLAN tag filter
 * @inner_vlan: Inner VLAN tag filter
 * @ether_type: EtherType filter (see
 *              &enum mxl862xx_extended_vlan_filter_ethertype)
 */
struct mxl862xx_extendedvlan_filter {
	u8 original_packet_filter_mode;
	__le32 filter_4_tpid_mode;
	struct mxl862xx_extendedvlan_filter_vlan outer_vlan;
	struct mxl862xx_extendedvlan_filter_vlan inner_vlan;
	__le32 ether_type;
} __packed;

/**
 * struct mxl862xx_extendedvlan_treatment_vlan - Per-tag treatment in
 *                                               Extended VLAN
 * @priority_mode: Priority assignment mode
 *                 (see &enum mxl862xx_extended_vlan_treatment_priority)
 * @priority_val: Priority value (when mode is VAL)
 * @vid_mode: VID assignment mode
 *            (see &enum mxl862xx_extended_vlan_treatment_vid)
 * @vid_val: VID value (when mode is VAL)
 * @tpid: TPID assignment mode
 *        (see &enum mxl862xx_extended_vlan_treatment_tpid)
 * @dei: DEI assignment mode
 *       (see &enum mxl862xx_extended_vlan_treatment_dei)
 */
struct mxl862xx_extendedvlan_treatment_vlan {
	__le32 priority_mode;
	__le32 priority_val;
	__le32 vid_mode;
	__le32 vid_val;
	__le32 tpid;
	__le32 dei;
} __packed;

/**
 * struct mxl862xx_extendedvlan_treatment - Extended VLAN treatment
 * @remove_tag: Tag removal action
 *              (see &enum mxl862xx_extended_vlan_treatment_remove_tag)
 * @treatment_4_tpid_mode: 4-TPID treatment mode
 * @add_outer_vlan: Add outer VLAN tag
 * @outer_vlan: Outer VLAN tag treatment parameters
 * @add_inner_vlan: Add inner VLAN tag
 * @inner_vlan: Inner VLAN tag treatment parameters
 * @reassign_bridge_port: Reassign to different bridge port
 * @new_bridge_port_id: New bridge port ID
 * @new_dscp_enable: Enable new DSCP assignment
 * @new_dscp: New DSCP value
 * @new_traffic_class_enable: Enable new traffic class assignment
 * @new_traffic_class: New traffic class value
 * @new_meter_enable: Enable new metering
 * @s_new_traffic_meter_id: New traffic meter ID
 * @dscp2pcp_map: DSCP to PCP mapping table (64 entries)
 * @loopback_enable: Enable loopback
 * @da_sa_swap_enable: Enable DA/SA swap
 * @mirror_enable: Enable mirroring
 */
struct mxl862xx_extendedvlan_treatment {
	__le32 remove_tag;
	__le32 treatment_4_tpid_mode;
	u8 add_outer_vlan;
	struct mxl862xx_extendedvlan_treatment_vlan outer_vlan;
	u8 add_inner_vlan;
	struct mxl862xx_extendedvlan_treatment_vlan inner_vlan;
	u8 reassign_bridge_port;
	__le16 new_bridge_port_id;
	u8 new_dscp_enable;
	__le16 new_dscp;
	u8 new_traffic_class_enable;
	u8 new_traffic_class;
	u8 new_meter_enable;
	__le16 s_new_traffic_meter_id;
	u8 dscp2pcp_map[64];
	u8 loopback_enable;
	u8 da_sa_swap_enable;
	u8 mirror_enable;
} __packed;

/**
 * struct mxl862xx_extendedvlan_alloc - Extended VLAN block allocation
 * @number_of_entries: Number of entries to allocate (input) / allocated
 *                     (output)
 * @extended_vlan_block_id: Block ID assigned by firmware (output on alloc,
 *                          input on free)
 *
 * Used with %MXL862XX_EXTENDEDVLAN_ALLOC and %MXL862XX_EXTENDEDVLAN_FREE.
 */
struct mxl862xx_extendedvlan_alloc {
	__le16 number_of_entries;
	__le16 extended_vlan_block_id;
} __packed;

/**
 * struct mxl862xx_extendedvlan_config - Extended VLAN entry configuration
 * @extended_vlan_block_id: Block ID from allocation
 * @entry_index: Entry index within the block
 * @filter: Filter (match) configuration
 * @treatment: Treatment (action) configuration
 *
 * Used with %MXL862XX_EXTENDEDVLAN_SET and %MXL862XX_EXTENDEDVLAN_GET.
 */
struct mxl862xx_extendedvlan_config {
	__le16 extended_vlan_block_id;
	__le16 entry_index;
	struct mxl862xx_extendedvlan_filter filter;
	struct mxl862xx_extendedvlan_treatment treatment;
} __packed;

/**
 * enum mxl862xx_vlan_filter_tci_mask - VLAN Filter TCI mask
 * @MXL862XX_VLAN_FILTER_TCI_MASK_VID: TCI mask for VLAN ID
 * @MXL862XX_VLAN_FILTER_TCI_MASK_PCP: TCI mask for VLAN PCP
 * @MXL862XX_VLAN_FILTER_TCI_MASK_TCI: TCI mask for VLAN TCI
 */
enum mxl862xx_vlan_filter_tci_mask {
	MXL862XX_VLAN_FILTER_TCI_MASK_VID = 0,
	MXL862XX_VLAN_FILTER_TCI_MASK_PCP = 1,
	MXL862XX_VLAN_FILTER_TCI_MASK_TCI = 2,
};

/**
 * struct mxl862xx_vlanfilter_alloc - VLAN Filter block allocation
 * @number_of_entries: Number of entries to allocate (input) / allocated
 *                     (output)
 * @vlan_filter_block_id: Block ID assigned by firmware (output on alloc,
 *                        input on free)
 * @discard_untagged: Discard untagged packets
 * @discard_unmatched_tagged: Discard tagged packets that do not match any
 *                            entry in the block
 * @use_default_port_vid: Use default port VLAN ID for filtering
 *
 * Used with %MXL862XX_VLANFILTER_ALLOC and %MXL862XX_VLANFILTER_FREE.
 */
struct mxl862xx_vlanfilter_alloc {
	__le16 number_of_entries;
	__le16 vlan_filter_block_id;
	u8 discard_untagged;
	u8 discard_unmatched_tagged;
	u8 use_default_port_vid;
} __packed;

/**
 * struct mxl862xx_vlanfilter_config - VLAN Filter entry configuration
 * @vlan_filter_block_id: Block ID from allocation
 * @entry_index: Entry index within the block
 * @vlan_filter_mask: TCI field(s) to match (see
 *                    &enum mxl862xx_vlan_filter_tci_mask)
 * @val: TCI value(s) to match (VID, PCP, or full TCI depending on mask)
 * @discard_matched: When true, discard frames matching this entry;
 *                   when false, allow them
 *
 * Used with %MXL862XX_VLANFILTER_SET and %MXL862XX_VLANFILTER_GET.
 */
struct mxl862xx_vlanfilter_config {
	__le16 vlan_filter_block_id;
	__le16 entry_index;
	__le32 vlan_filter_mask; /* enum mxl862xx_vlan_filter_tci_mask */
	__le32 val;
	u8 discard_matched;
} __packed;

/**
 * enum mxl862xx_ss_sp_tag_mask - Special tag valid field indicator bits
 * @MXL862XX_SS_SP_TAG_MASK_RX: valid RX special tag mode
 * @MXL862XX_SS_SP_TAG_MASK_TX: valid TX special tag mode
 * @MXL862XX_SS_SP_TAG_MASK_RX_PEN: valid RX special tag info over preamble
 * @MXL862XX_SS_SP_TAG_MASK_TX_PEN: valid TX special tag info over preamble
 */
enum mxl862xx_ss_sp_tag_mask {
	MXL862XX_SS_SP_TAG_MASK_RX = BIT(0),
	MXL862XX_SS_SP_TAG_MASK_TX = BIT(1),
	MXL862XX_SS_SP_TAG_MASK_RX_PEN = BIT(2),
	MXL862XX_SS_SP_TAG_MASK_TX_PEN = BIT(3),
};

/**
 * enum mxl862xx_ss_sp_tag_rx - RX special tag mode
 * @MXL862XX_SS_SP_TAG_RX_NO_TAG_NO_INSERT: packet does NOT have special
 *                                          tag and special tag is NOT inserted
 * @MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT: packet does NOT have special tag
 *                                       and special tag is inserted
 * @MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT: packet has special tag and special
 *                                       tag is NOT inserted
 */
enum mxl862xx_ss_sp_tag_rx {
	MXL862XX_SS_SP_TAG_RX_NO_TAG_NO_INSERT = 0,
	MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT = 1,
	MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT = 2,
};

/**
 * enum mxl862xx_ss_sp_tag_tx - TX special tag mode
 * @MXL862XX_SS_SP_TAG_TX_NO_TAG_NO_REMOVE: packet does NOT have special
 *                                          tag and special tag is NOT removed
 * @MXL862XX_SS_SP_TAG_TX_TAG_REPLACE: packet has special tag and special
 *                                     tag is replaced
 * @MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE: packet has special tag and special
 *                                       tag is NOT removed
 * @MXL862XX_SS_SP_TAG_TX_TAG_REMOVE: packet has special tag and special
 *                                    tag is removed
 */
enum mxl862xx_ss_sp_tag_tx {
	MXL862XX_SS_SP_TAG_TX_NO_TAG_NO_REMOVE = 0,
	MXL862XX_SS_SP_TAG_TX_TAG_REPLACE = 1,
	MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE = 2,
	MXL862XX_SS_SP_TAG_TX_TAG_REMOVE = 3,
};

/**
 * enum mxl862xx_ss_sp_tag_rx_pen - RX special tag info over preamble
 * @MXL862XX_SS_SP_TAG_RX_PEN_ALL_0: special tag info inserted from byte 2
 *                                   to 7 are all 0
 * @MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_IS_16: special tag byte 5 is 16, other
 *                                          bytes from 2 to 7 are 0
 * @MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_FROM_PREAMBLE: special tag byte 5 is
 *                                                  from preamble field, others
 *                                                  are 0
 * @MXL862XX_SS_SP_TAG_RX_PEN_BYTE_2_TO_7_FROM_PREAMBLE: special tag byte 2
 *                                                       to 7 are from preamble
 *                                                       field
 */
enum mxl862xx_ss_sp_tag_rx_pen {
	MXL862XX_SS_SP_TAG_RX_PEN_ALL_0 = 0,
	MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_IS_16 = 1,
	MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_FROM_PREAMBLE = 2,
	MXL862XX_SS_SP_TAG_RX_PEN_BYTE_2_TO_7_FROM_PREAMBLE = 3,
};

/**
 * struct mxl862xx_ss_sp_tag - Special tag port settings
 * @pid: port ID (1~16)
 * @mask: See &enum mxl862xx_ss_sp_tag_mask
 * @rx: See &enum mxl862xx_ss_sp_tag_rx
 * @tx: See &enum mxl862xx_ss_sp_tag_tx
 * @rx_pen: See &enum mxl862xx_ss_sp_tag_rx_pen
 * @tx_pen: TX special tag info over preamble
 *	0 - disabled
 *	1 - enabled
 */
struct mxl862xx_ss_sp_tag {
	u8 pid;
	u8 mask; /* enum mxl862xx_ss_sp_tag_mask */
	u8 rx; /* enum mxl862xx_ss_sp_tag_rx */
	u8 tx; /* enum mxl862xx_ss_sp_tag_tx */
	u8 rx_pen; /* enum mxl862xx_ss_sp_tag_rx_pen */
	u8 tx_pen; /* boolean */
} __packed;

/**
 * enum mxl862xx_logical_port_mode - Logical port mode
 * @MXL862XX_LOGICAL_PORT_8BIT_WLAN: WLAN with 8-bit station ID
 * @MXL862XX_LOGICAL_PORT_9BIT_WLAN: WLAN with 9-bit station ID
 * @MXL862XX_LOGICAL_PORT_ETHERNET: Ethernet port
 * @MXL862XX_LOGICAL_PORT_OTHER: Others
 */
enum mxl862xx_logical_port_mode {
	MXL862XX_LOGICAL_PORT_8BIT_WLAN = 0,
	MXL862XX_LOGICAL_PORT_9BIT_WLAN,
	MXL862XX_LOGICAL_PORT_ETHERNET,
	MXL862XX_LOGICAL_PORT_OTHER = 0xFF,
};

/**
 * struct mxl862xx_ctp_port_assignment - CTP Port Assignment/association
 *                                       with logical port
 * @logical_port_id: Logical Port Id. The valid range is hardware dependent
 * @first_ctp_port_id: First CTP (Connectivity Termination Port) ID mapped
 *                     to above logical port ID
 * @number_of_ctp_port: Total number of CTP Ports mapped above logical port
 *                      ID
 * @mode: Logical port mode to define sub interface ID format. See
 *        &enum mxl862xx_logical_port_mode
 * @bridge_port_id: Bridge Port ID (not FID). For allocation, each CTP
 *                  allocated is mapped to the Bridge Port given by this field.
 *                  The Bridge Port will be configured to use first CTP as
 *                  egress CTP.
 */
struct mxl862xx_ctp_port_assignment {
	u8 logical_port_id;
	__le16 first_ctp_port_id;
	__le16 number_of_ctp_port;
	__le32 mode; /* enum mxl862xx_logical_port_mode */
	__le16 bridge_port_id;
} __packed;

/**
 * enum mxl862xx_stp_port_state - Spanning Tree Protocol port states
 * @MXL862XX_STP_PORT_STATE_FORWARD: Forwarding state
 * @MXL862XX_STP_PORT_STATE_DISABLE: Disabled/Discarding state
 * @MXL862XX_STP_PORT_STATE_LEARNING: Learning state
 * @MXL862XX_STP_PORT_STATE_BLOCKING: Blocking/Listening
 */
enum mxl862xx_stp_port_state {
	MXL862XX_STP_PORT_STATE_FORWARD = 0,
	MXL862XX_STP_PORT_STATE_DISABLE,
	MXL862XX_STP_PORT_STATE_LEARNING,
	MXL862XX_STP_PORT_STATE_BLOCKING,
};

/**
 * struct mxl862xx_stp_port_cfg - Configures the Spanning Tree Protocol state
 * @port_id: Port number
 * @fid: Filtering Identifier (FID)
 * @port_state: See &enum mxl862xx_stp_port_state
 */
struct mxl862xx_stp_port_cfg {
	__le16 port_id;
	__le16 fid;
	__le32 port_state; /* enum mxl862xx_stp_port_state */
} __packed;

/**
 * struct mxl862xx_sys_fw_image_version - Firmware version information
 * @iv_major: firmware major version
 * @iv_minor: firmware minor version
 * @iv_revision: firmware revision
 * @iv_build_num: firmware build number
 */
struct mxl862xx_sys_fw_image_version {
	u8 iv_major;
	u8 iv_minor;
	__le16 iv_revision;
	__le32 iv_build_num;
} __packed;

/**
 * enum mxl862xx_port_type - Port Type
 * @MXL862XX_LOGICAL_PORT: Logical Port
 * @MXL862XX_PHYSICAL_PORT: Physical Port
 * @MXL862XX_CTP_PORT: Connectivity Termination Port (CTP)
 * @MXL862XX_BRIDGE_PORT: Bridge Port
 */
enum mxl862xx_port_type {
	MXL862XX_LOGICAL_PORT = 0,
	MXL862XX_PHYSICAL_PORT,
	MXL862XX_CTP_PORT,
	MXL862XX_BRIDGE_PORT,
};

/**
 * enum mxl862xx_rmon_port_type - RMON counter table type
 * @MXL862XX_RMON_CTP_PORT_RX: CTP RX counters
 * @MXL862XX_RMON_CTP_PORT_TX: CTP TX counters
 * @MXL862XX_RMON_BRIDGE_PORT_RX: Bridge port RX counters
 * @MXL862XX_RMON_BRIDGE_PORT_TX: Bridge port TX counters
 * @MXL862XX_RMON_CTP_PORT_PCE_BYPASS: CTP PCE bypass counters
 * @MXL862XX_RMON_TFLOW_RX: TFLOW RX counters
 * @MXL862XX_RMON_TFLOW_TX: TFLOW TX counters
 * @MXL862XX_RMON_QMAP: QMAP counters
 * @MXL862XX_RMON_METER: Meter counters
 * @MXL862XX_RMON_PMAC: PMAC counters
 */
enum mxl862xx_rmon_port_type {
	MXL862XX_RMON_CTP_PORT_RX = 0,
	MXL862XX_RMON_CTP_PORT_TX,
	MXL862XX_RMON_BRIDGE_PORT_RX,
	MXL862XX_RMON_BRIDGE_PORT_TX,
	MXL862XX_RMON_CTP_PORT_PCE_BYPASS,
	MXL862XX_RMON_TFLOW_RX,
	MXL862XX_RMON_TFLOW_TX,
	MXL862XX_RMON_QMAP = 0x0e,
	MXL862XX_RMON_METER = 0x19,
	MXL862XX_RMON_PMAC = 0x1c,
};

/**
 * struct mxl862xx_rmon_port_cnt - RMON counters for a port
 * @port_type: Port type for counter retrieval (see &enum mxl862xx_port_type)
 * @port_id: Ethernet port number (zero-based)
 * @sub_if_id_group: Sub-interface ID group
 * @pce_bypass: Separate CTP Tx counters when PCE is bypassed
 * @rx_extended_vlan_discard_pkts: Discarded at extended VLAN operation
 * @mtu_exceed_discard_pkts: Discarded due to MTU exceeded
 * @tx_under_size_good_pkts: Tx undersize (<64) packet count
 * @tx_oversize_good_pkts: Tx oversize (>1518) packet count
 * @rx_good_pkts: Received good packet count
 * @rx_unicast_pkts: Received unicast packet count
 * @rx_broadcast_pkts: Received broadcast packet count
 * @rx_multicast_pkts: Received multicast packet count
 * @rx_fcserror_pkts: Received FCS error packet count
 * @rx_under_size_good_pkts: Received undersize good packet count
 * @rx_oversize_good_pkts: Received oversize good packet count
 * @rx_under_size_error_pkts: Received undersize error packet count
 * @rx_good_pause_pkts: Received good pause packet count
 * @rx_oversize_error_pkts: Received oversize error packet count
 * @rx_align_error_pkts: Received alignment error packet count
 * @rx_filtered_pkts: Filtered packet count
 * @rx64byte_pkts: Received 64-byte packet count
 * @rx127byte_pkts: Received 65-127 byte packet count
 * @rx255byte_pkts: Received 128-255 byte packet count
 * @rx511byte_pkts: Received 256-511 byte packet count
 * @rx1023byte_pkts: Received 512-1023 byte packet count
 * @rx_max_byte_pkts: Received 1024-max byte packet count
 * @tx_good_pkts: Transmitted good packet count
 * @tx_unicast_pkts: Transmitted unicast packet count
 * @tx_broadcast_pkts: Transmitted broadcast packet count
 * @tx_multicast_pkts: Transmitted multicast packet count
 * @tx_single_coll_count: Transmit single collision count
 * @tx_mult_coll_count: Transmit multiple collision count
 * @tx_late_coll_count: Transmit late collision count
 * @tx_excess_coll_count: Transmit excessive collision count
 * @tx_coll_count: Transmit collision count
 * @tx_pause_count: Transmit pause packet count
 * @tx64byte_pkts: Transmitted 64-byte packet count
 * @tx127byte_pkts: Transmitted 65-127 byte packet count
 * @tx255byte_pkts: Transmitted 128-255 byte packet count
 * @tx511byte_pkts: Transmitted 256-511 byte packet count
 * @tx1023byte_pkts: Transmitted 512-1023 byte packet count
 * @tx_max_byte_pkts: Transmitted 1024-max byte packet count
 * @tx_dropped_pkts: Transmit dropped packet count
 * @tx_acm_dropped_pkts: Transmit ACM dropped packet count
 * @rx_dropped_pkts: Received dropped packet count
 * @rx_good_bytes: Received good byte count (64-bit)
 * @rx_bad_bytes: Received bad byte count (64-bit)
 * @tx_good_bytes: Transmitted good byte count (64-bit)
 */
struct mxl862xx_rmon_port_cnt {
	__le32 port_type; /* enum mxl862xx_port_type */
	__le16 port_id;
	__le16 sub_if_id_group;
	u8 pce_bypass;
	__le32 rx_extended_vlan_discard_pkts;
	__le32 mtu_exceed_discard_pkts;
	__le32 tx_under_size_good_pkts;
	__le32 tx_oversize_good_pkts;
	__le32 rx_good_pkts;
	__le32 rx_unicast_pkts;
	__le32 rx_broadcast_pkts;
	__le32 rx_multicast_pkts;
	__le32 rx_fcserror_pkts;
	__le32 rx_under_size_good_pkts;
	__le32 rx_oversize_good_pkts;
	__le32 rx_under_size_error_pkts;
	__le32 rx_good_pause_pkts;
	__le32 rx_oversize_error_pkts;
	__le32 rx_align_error_pkts;
	__le32 rx_filtered_pkts;
	__le32 rx64byte_pkts;
	__le32 rx127byte_pkts;
	__le32 rx255byte_pkts;
	__le32 rx511byte_pkts;
	__le32 rx1023byte_pkts;
	__le32 rx_max_byte_pkts;
	__le32 tx_good_pkts;
	__le32 tx_unicast_pkts;
	__le32 tx_broadcast_pkts;
	__le32 tx_multicast_pkts;
	__le32 tx_single_coll_count;
	__le32 tx_mult_coll_count;
	__le32 tx_late_coll_count;
	__le32 tx_excess_coll_count;
	__le32 tx_coll_count;
	__le32 tx_pause_count;
	__le32 tx64byte_pkts;
	__le32 tx127byte_pkts;
	__le32 tx255byte_pkts;
	__le32 tx511byte_pkts;
	__le32 tx1023byte_pkts;
	__le32 tx_max_byte_pkts;
	__le32 tx_dropped_pkts;
	__le32 tx_acm_dropped_pkts;
	__le32 rx_dropped_pkts;
	__le64 rx_good_bytes;
	__le64 rx_bad_bytes;
	__le64 tx_good_bytes;
} __packed;

#endif /* __MXL862XX_API_H */
