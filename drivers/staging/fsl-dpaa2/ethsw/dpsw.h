// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2018 NXP
 *
 */

#ifndef __FSL_DPSW_H
#define __FSL_DPSW_H

/* Data Path L2-Switch API
 * Contains API for handling DPSW topology and functionality
 */

struct fsl_mc_io;

/**
 * DPSW general definitions
 */

/**
 * Maximum number of traffic class priorities
 */
#define DPSW_MAX_PRIORITIES	8
/**
 * Maximum number of interfaces
 */
#define DPSW_MAX_IF		64

int dpsw_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int dpsw_id,
	      u16 *token);

int dpsw_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

/**
 * DPSW options
 */

/**
 * Disable flooding
 */
#define DPSW_OPT_FLOODING_DIS		0x0000000000000001ULL
/**
 * Disable Multicast
 */
#define DPSW_OPT_MULTICAST_DIS		0x0000000000000004ULL
/**
 * Support control interface
 */
#define DPSW_OPT_CTRL_IF_DIS		0x0000000000000010ULL
/**
 * Disable flooding metering
 */
#define DPSW_OPT_FLOODING_METERING_DIS  0x0000000000000020ULL
/**
 * Enable metering
 */
#define DPSW_OPT_METERING_EN            0x0000000000000040ULL

/**
 * enum dpsw_component_type - component type of a bridge
 * @DPSW_COMPONENT_TYPE_C_VLAN: A C-VLAN component of an
 *   enterprise VLAN bridge or of a Provider Bridge used
 *   to process C-tagged frames
 * @DPSW_COMPONENT_TYPE_S_VLAN: An S-VLAN component of a
 *   Provider Bridge
 *
 */
enum dpsw_component_type {
	DPSW_COMPONENT_TYPE_C_VLAN = 0,
	DPSW_COMPONENT_TYPE_S_VLAN
};

/**
 * struct dpsw_cfg - DPSW configuration
 * @num_ifs: Number of external and internal interfaces
 * @adv: Advanced parameters; default is all zeros;
 *	 use this structure to change default settings
 * @adv.options: Enable/Disable DPSW features (bitmap)
 * @adv.max_vlans: Maximum Number of VLAN's; 0 - indicates default 16
 * @adv.max_meters_per_if: Number of meters per interface
 * @adv.max_fdbs: Maximum Number of FDB's; 0 - indicates default 16
 * @adv.max_fdb_entries: Number of FDB entries for default FDB table;
 *	0 - indicates default 1024 entries.
 * @adv.fdb_aging_time: Default FDB aging time for default FDB table;
 *	0 - indicates default 300 seconds
 * @adv.max_fdb_mc_groups: Number of multicast groups in each FDB table;
 *	0 - indicates default 32
 * @adv.component_type: Indicates the component type of this bridge
 */
struct dpsw_cfg {
	u16 num_ifs;
	struct {
		u64 options;
		u16 max_vlans;
		u8 max_meters_per_if;
		u8 max_fdbs;
		u16 max_fdb_entries;
		u16 fdb_aging_time;
		u16 max_fdb_mc_groups;
		enum dpsw_component_type component_type;
	} adv;
};

int dpsw_enable(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dpsw_disable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token);

int dpsw_reset(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

/**
 * DPSW IRQ Index and Events
 */

#define DPSW_IRQ_INDEX_IF		0x0000
#define DPSW_IRQ_INDEX_L2SW		0x0001

/**
 * IRQ event - Indicates that the link state changed
 */
#define DPSW_IRQ_EVENT_LINK_CHANGED	0x0001

/**
 * struct dpsw_irq_cfg - IRQ configuration
 * @addr:	Address that must be written to signal a message-based interrupt
 * @val:	Value to write into irq_addr address
 * @irq_num: A user defined number associated with this IRQ
 */
struct dpsw_irq_cfg {
	u64 addr;
	u32 val;
	int irq_num;
};

int dpsw_set_irq_enable(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u8 en);

int dpsw_set_irq_mask(struct fsl_mc_io *mc_io,
		      u32 cmd_flags,
		      u16 token,
		      u8 irq_index,
		      u32 mask);

int dpsw_get_irq_status(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u8 irq_index,
			u32 *status);

int dpsw_clear_irq_status(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  u8 irq_index,
			  u32 status);

/**
 * struct dpsw_attr - Structure representing DPSW attributes
 * @id: DPSW object ID
 * @options: Enable/Disable DPSW features
 * @max_vlans: Maximum Number of VLANs
 * @max_meters_per_if:  Number of meters per interface
 * @max_fdbs: Maximum Number of FDBs
 * @max_fdb_entries: Number of FDB entries for default FDB table;
 *			0 - indicates default 1024 entries.
 * @fdb_aging_time: Default FDB aging time for default FDB table;
 *			0 - indicates default 300 seconds
 * @max_fdb_mc_groups: Number of multicast groups in each FDB table;
 *			0 - indicates default 32
 * @mem_size: DPSW frame storage memory size
 * @num_ifs: Number of interfaces
 * @num_vlans: Current number of VLANs
 * @num_fdbs: Current number of FDBs
 * @component_type: Component type of this bridge
 */
struct dpsw_attr {
	int id;
	u64 options;
	u16 max_vlans;
	u8 max_meters_per_if;
	u8 max_fdbs;
	u16 max_fdb_entries;
	u16 fdb_aging_time;
	u16 max_fdb_mc_groups;
	u16 num_ifs;
	u16 mem_size;
	u16 num_vlans;
	u8 num_fdbs;
	enum dpsw_component_type component_type;
};

int dpsw_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpsw_attr *attr);

/**
 * enum dpsw_action - Action selection for special/control frames
 * @DPSW_ACTION_DROP: Drop frame
 * @DPSW_ACTION_REDIRECT: Redirect frame to control port
 */
enum dpsw_action {
	DPSW_ACTION_DROP = 0,
	DPSW_ACTION_REDIRECT = 1
};

/**
 * Enable auto-negotiation
 */
#define DPSW_LINK_OPT_AUTONEG		0x0000000000000001ULL
/**
 * Enable half-duplex mode
 */
#define DPSW_LINK_OPT_HALF_DUPLEX	0x0000000000000002ULL
/**
 * Enable pause frames
 */
#define DPSW_LINK_OPT_PAUSE		0x0000000000000004ULL
/**
 * Enable a-symmetric pause frames
 */
#define DPSW_LINK_OPT_ASYM_PAUSE	0x0000000000000008ULL

/**
 * struct dpsw_link_cfg - Structure representing DPSW link configuration
 * @rate: Rate
 * @options: Mask of available options; use 'DPSW_LINK_OPT_<X>' values
 */
struct dpsw_link_cfg {
	u32 rate;
	u64 options;
};

int dpsw_if_set_link_cfg(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u16 if_id,
			 struct dpsw_link_cfg *cfg);
/**
 * struct dpsw_link_state - Structure representing DPSW link state
 * @rate: Rate
 * @options: Mask of available options; use 'DPSW_LINK_OPT_<X>' values
 * @up: 0 - covers two cases: down and disconnected, 1 - up
 */
struct dpsw_link_state {
	u32 rate;
	u64 options;
	u8 up;
};

int dpsw_if_get_link_state(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   u16 if_id,
			   struct dpsw_link_state *state);

int dpsw_if_set_flooding(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u16 if_id,
			 u8 en);

int dpsw_if_set_broadcast(struct fsl_mc_io *mc_io,
			  u32 cmd_flags,
			  u16 token,
			  u16 if_id,
			  u8 en);

/**
 * struct dpsw_tci_cfg - Tag Control Information (TCI) configuration
 * @pcp: Priority Code Point (PCP): a 3-bit field which refers
 *		 to the IEEE 802.1p priority
 * @dei: Drop Eligible Indicator (DEI): a 1-bit field. May be used
 *		 separately or in conjunction with PCP to indicate frames
 *		 eligible to be dropped in the presence of congestion
 * @vlan_id: VLAN Identifier (VID): a 12-bit field specifying the VLAN
 *			to which the frame belongs. The hexadecimal values
 *			of 0x000 and 0xFFF are reserved;
 *			all other values may be used as VLAN identifiers,
 *			allowing up to 4,094 VLANs
 */
struct dpsw_tci_cfg {
	u8 pcp;
	u8 dei;
	u16 vlan_id;
};

int dpsw_if_set_tci(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    u16 if_id,
		    const struct dpsw_tci_cfg *cfg);

int dpsw_if_get_tci(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    u16 if_id,
		    struct dpsw_tci_cfg *cfg);

/**
 * enum dpsw_stp_state - Spanning Tree Protocol (STP) states
 * @DPSW_STP_STATE_BLOCKING: Blocking state
 * @DPSW_STP_STATE_LISTENING: Listening state
 * @DPSW_STP_STATE_LEARNING: Learning state
 * @DPSW_STP_STATE_FORWARDING: Forwarding state
 *
 */
enum dpsw_stp_state {
	DPSW_STP_STATE_DISABLED = 0,
	DPSW_STP_STATE_LISTENING = 1,
	DPSW_STP_STATE_LEARNING = 2,
	DPSW_STP_STATE_FORWARDING = 3,
	DPSW_STP_STATE_BLOCKING = 0
};

/**
 * struct dpsw_stp_cfg - Spanning Tree Protocol (STP) Configuration
 * @vlan_id: VLAN ID STP state
 * @state: STP state
 */
struct dpsw_stp_cfg {
	u16 vlan_id;
	enum dpsw_stp_state state;
};

int dpsw_if_set_stp(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    u16 if_id,
		    const struct dpsw_stp_cfg *cfg);

/**
 * enum dpsw_accepted_frames - Types of frames to accept
 * @DPSW_ADMIT_ALL: The device accepts VLAN tagged, untagged and
 *			priority tagged frames
 * @DPSW_ADMIT_ONLY_VLAN_TAGGED: The device discards untagged frames or
 *			Priority-Tagged frames received on this interface.
 *
 */
enum dpsw_accepted_frames {
	DPSW_ADMIT_ALL = 1,
	DPSW_ADMIT_ONLY_VLAN_TAGGED = 3
};

/**
 * enum dpsw_counter  - Counters types
 * @DPSW_CNT_ING_FRAME: Counts ingress frames
 * @DPSW_CNT_ING_BYTE: Counts ingress bytes
 * @DPSW_CNT_ING_FLTR_FRAME: Counts filtered ingress frames
 * @DPSW_CNT_ING_FRAME_DISCARD: Counts discarded ingress frame
 * @DPSW_CNT_ING_MCAST_FRAME: Counts ingress multicast frames
 * @DPSW_CNT_ING_MCAST_BYTE: Counts ingress multicast bytes
 * @DPSW_CNT_ING_BCAST_FRAME: Counts ingress broadcast frames
 * @DPSW_CNT_ING_BCAST_BYTES: Counts ingress broadcast bytes
 * @DPSW_CNT_EGR_FRAME: Counts egress frames
 * @DPSW_CNT_EGR_BYTE: Counts eEgress bytes
 * @DPSW_CNT_EGR_FRAME_DISCARD: Counts discarded egress frames
 * @DPSW_CNT_EGR_STP_FRAME_DISCARD: Counts egress STP discarded frames
 */
enum dpsw_counter {
	DPSW_CNT_ING_FRAME = 0x0,
	DPSW_CNT_ING_BYTE = 0x1,
	DPSW_CNT_ING_FLTR_FRAME = 0x2,
	DPSW_CNT_ING_FRAME_DISCARD = 0x3,
	DPSW_CNT_ING_MCAST_FRAME = 0x4,
	DPSW_CNT_ING_MCAST_BYTE = 0x5,
	DPSW_CNT_ING_BCAST_FRAME = 0x6,
	DPSW_CNT_ING_BCAST_BYTES = 0x7,
	DPSW_CNT_EGR_FRAME = 0x8,
	DPSW_CNT_EGR_BYTE = 0x9,
	DPSW_CNT_EGR_FRAME_DISCARD = 0xa,
	DPSW_CNT_EGR_STP_FRAME_DISCARD = 0xb
};

int dpsw_if_get_counter(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u16 if_id,
			enum dpsw_counter type,
			u64 *counter);

int dpsw_if_enable(struct fsl_mc_io *mc_io,
		   u32 cmd_flags,
		   u16 token,
		   u16 if_id);

int dpsw_if_disable(struct fsl_mc_io *mc_io,
		    u32 cmd_flags,
		    u16 token,
		    u16 if_id);

int dpsw_if_set_max_frame_length(struct fsl_mc_io *mc_io,
				 u32 cmd_flags,
				 u16 token,
				 u16 if_id,
				 u16 frame_length);

/**
 * struct dpsw_vlan_cfg - VLAN Configuration
 * @fdb_id: Forwarding Data Base
 */
struct dpsw_vlan_cfg {
	u16 fdb_id;
};

int dpsw_vlan_add(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token,
		  u16 vlan_id,
		  const struct dpsw_vlan_cfg *cfg);

/**
 * struct dpsw_vlan_if_cfg - Set of VLAN Interfaces
 * @num_ifs: The number of interfaces that are assigned to the egress
 *		list for this VLAN
 * @if_id: The set of interfaces that are
 *		assigned to the egress list for this VLAN
 */
struct dpsw_vlan_if_cfg {
	u16 num_ifs;
	u16 if_id[DPSW_MAX_IF];
};

int dpsw_vlan_add_if(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     u16 vlan_id,
		     const struct dpsw_vlan_if_cfg *cfg);

int dpsw_vlan_add_if_untagged(struct fsl_mc_io *mc_io,
			      u32 cmd_flags,
			      u16 token,
			      u16 vlan_id,
			      const struct dpsw_vlan_if_cfg *cfg);

int dpsw_vlan_remove_if(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			u16 vlan_id,
			const struct dpsw_vlan_if_cfg *cfg);

int dpsw_vlan_remove_if_untagged(struct fsl_mc_io *mc_io,
				 u32 cmd_flags,
				 u16 token,
				 u16 vlan_id,
				 const struct dpsw_vlan_if_cfg *cfg);

int dpsw_vlan_remove(struct fsl_mc_io *mc_io,
		     u32 cmd_flags,
		     u16 token,
		     u16 vlan_id);

/**
 * enum dpsw_fdb_entry_type - FDB Entry type - Static/Dynamic
 * @DPSW_FDB_ENTRY_STATIC: Static entry
 * @DPSW_FDB_ENTRY_DINAMIC: Dynamic entry
 */
enum dpsw_fdb_entry_type {
	DPSW_FDB_ENTRY_STATIC = 0,
	DPSW_FDB_ENTRY_DINAMIC = 1
};

/**
 * struct dpsw_fdb_unicast_cfg - Unicast entry configuration
 * @type: Select static or dynamic entry
 * @mac_addr: MAC address
 * @if_egress: Egress interface ID
 */
struct dpsw_fdb_unicast_cfg {
	enum dpsw_fdb_entry_type type;
	u8 mac_addr[6];
	u16 if_egress;
};

int dpsw_fdb_add_unicast(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 u16 fdb_id,
			 const struct dpsw_fdb_unicast_cfg *cfg);

int dpsw_fdb_remove_unicast(struct fsl_mc_io *mc_io,
			    u32 cmd_flags,
			    u16 token,
			    u16 fdb_id,
			    const struct dpsw_fdb_unicast_cfg *cfg);

/**
 * struct dpsw_fdb_multicast_cfg - Multi-cast entry configuration
 * @type: Select static or dynamic entry
 * @mac_addr: MAC address
 * @num_ifs: Number of external and internal interfaces
 * @if_id: Egress interface IDs
 */
struct dpsw_fdb_multicast_cfg {
	enum dpsw_fdb_entry_type type;
	u8 mac_addr[6];
	u16 num_ifs;
	u16 if_id[DPSW_MAX_IF];
};

int dpsw_fdb_add_multicast(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   u16 fdb_id,
			   const struct dpsw_fdb_multicast_cfg *cfg);

int dpsw_fdb_remove_multicast(struct fsl_mc_io *mc_io,
			      u32 cmd_flags,
			      u16 token,
			      u16 fdb_id,
			      const struct dpsw_fdb_multicast_cfg *cfg);

/**
 * enum dpsw_fdb_learning_mode - Auto-learning modes
 * @DPSW_FDB_LEARNING_MODE_DIS: Disable Auto-learning
 * @DPSW_FDB_LEARNING_MODE_HW: Enable HW auto-Learning
 * @DPSW_FDB_LEARNING_MODE_NON_SECURE: Enable None secure learning by CPU
 * @DPSW_FDB_LEARNING_MODE_SECURE: Enable secure learning by CPU
 *
 *	NONE - SECURE LEARNING
 *	SMAC found	DMAC found	CTLU Action
 *	v		v	Forward frame to
 *						1.  DMAC destination
 *	-		v	Forward frame to
 *						1.  DMAC destination
 *						2.  Control interface
 *	v		-	Forward frame to
 *						1.  Flooding list of interfaces
 *	-		-	Forward frame to
 *						1.  Flooding list of interfaces
 *						2.  Control interface
 *	SECURE LEARING
 *	SMAC found	DMAC found	CTLU Action
 *	v		v		Forward frame to
 *						1.  DMAC destination
 *	-		v		Forward frame to
 *						1.  Control interface
 *	v		-		Forward frame to
 *						1.  Flooding list of interfaces
 *	-		-		Forward frame to
 *						1.  Control interface
 */
enum dpsw_fdb_learning_mode {
	DPSW_FDB_LEARNING_MODE_DIS = 0,
	DPSW_FDB_LEARNING_MODE_HW = 1,
	DPSW_FDB_LEARNING_MODE_NON_SECURE = 2,
	DPSW_FDB_LEARNING_MODE_SECURE = 3
};

int dpsw_fdb_set_learning_mode(struct fsl_mc_io *mc_io,
			       u32 cmd_flags,
			       u16 token,
			       u16 fdb_id,
			       enum dpsw_fdb_learning_mode mode);

/**
 * struct dpsw_fdb_attr - FDB Attributes
 * @max_fdb_entries: Number of FDB entries
 * @fdb_aging_time: Aging time in seconds
 * @learning_mode: Learning mode
 * @num_fdb_mc_groups: Current number of multicast groups
 * @max_fdb_mc_groups: Maximum number of multicast groups
 */
struct dpsw_fdb_attr {
	u16 max_fdb_entries;
	u16 fdb_aging_time;
	enum dpsw_fdb_learning_mode learning_mode;
	u16 num_fdb_mc_groups;
	u16 max_fdb_mc_groups;
};

int dpsw_get_api_version(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 *major_ver,
			 u16 *minor_ver);

#endif /* __FSL_DPSW_H */
