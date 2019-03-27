/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2009 System Fabric Works, Inc. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2009-2015 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *	Declaration of osm_subn_t.
 *	This object represents an IBA subnet.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_SUBNET_H_
#define _OSM_SUBNET_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_fleximap.h>
#include <complib/cl_map.h>
#include <complib/cl_ptr_vector.h>
#include <complib/cl_list.h>
#include <opensm/osm_base.h>
#include <opensm/osm_prefix_route.h>
#include <opensm/osm_db.h>
#include <stdio.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#define OSM_SUBNET_VECTOR_MIN_SIZE			0
#define OSM_SUBNET_VECTOR_GROW_SIZE			1
#define OSM_SUBNET_VECTOR_CAPACITY			256

#define OSM_PARTITION_ENFORCE_BOTH			"both"
#define OSM_PARTITION_ENFORCE_IN			"in"
#define OSM_PARTITION_ENFORCE_OUT			"out"
#define OSM_PARTITION_ENFORCE_OFF			"off"

typedef enum _osm_partition_enforce_type_enum {
	OSM_PARTITION_ENFORCE_TYPE_BOTH,
	OSM_PARTITION_ENFORCE_TYPE_IN,
	OSM_PARTITION_ENFORCE_TYPE_OUT,
	OSM_PARTITION_ENFORCE_TYPE_OFF
} osm_partition_enforce_type_enum;

/* XXX: not actual max, max we're currently going to support */
#define OSM_CCT_ENTRY_MAX        128
#define OSM_CCT_ENTRY_MAD_BLOCKS (OSM_CCT_ENTRY_MAX/64)

struct osm_opensm;
struct osm_qos_policy;

/****h* OpenSM/Subnet
* NAME
*	Subnet
*
* DESCRIPTION
*	The Subnet object encapsulates the information needed by the
*	OpenSM to manage a subnet.  The OpenSM allocates one Subnet object
*	per IBA subnet.
*
*	The Subnet object is not thread safe, thus callers must provide
*	serialization.
*
*	This object is essentially a container for the various components
*	of a subnet.  Callers may directly access the member variables.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: Subnet/osm_qos_options_t
* NAME
*	osm_qos_options_t
*
* DESCRIPTION
*	Subnet QoS options structure.  This structure contains the various
*	QoS specific configuration parameters for the subnet.
*
* SYNOPSIS
*/
typedef struct osm_qos_options {
	unsigned max_vls;
	int high_limit;
	char *vlarb_high;
	char *vlarb_low;
	char *sl2vl;
} osm_qos_options_t;
/*
* FIELDS
*
*	max_vls
*		The number of maximum VLs on the Subnet (0 == use default)
*
*	high_limit
*		The limit of High Priority component of VL Arbitration
*		table (IBA 7.6.9) (-1 == use default)
*
*	vlarb_high
*		High priority VL Arbitration table template. (NULL == use default)
*
*	vlarb_low
*		Low priority VL Arbitration table template. (NULL == use default)
*
*	sl2vl
*		SL2VL Mapping table (IBA 7.6.6) template. (NULL == use default)
*
*********/

/****s* OpenSM: Subnet/osm_cct_entry_t
* NAME
*	osm_cct_entry_t
*
* DESCRIPTION
*	Subnet Congestion Control Table entry.  See A10.2.2.1.1 for format details.
*
* SYNOPSIS
*/
typedef struct osm_cct_entry {
	uint8_t shift; //Alex: shift 2 bits
	uint16_t multiplier; //Alex multiplier 14 bits
} osm_cct_entry_t;
/*
* FIELDS
*
*	shift
*		shift field in CCT entry.  See A10.2.2.1.1.
*
*	multiplier
*		multiplier field in CCT entry.  See A10.2.2.1.1.
*
*********/

/****s* OpenSM: Subnet/osm_cacongestion_entry_t
* NAME
*	osm_cacongestion_entry_t
*
* DESCRIPTION
*	Subnet CA Congestion entry.  See A10.4.3.8.4 for format details.
*
* SYNOPSIS
*/
typedef struct osm_cacongestion_entry {
	ib_net16_t ccti_timer; //Alex: ccti_timer and ccti_increase should be replaced
	uint8_t ccti_increase;
	uint8_t trigger_threshold;
	uint8_t ccti_min;
} osm_cacongestion_entry_t;
/*
* FIELDS
*
*	ccti_timer
*		CCTI Timer
*
*	ccti_increase
*		CCTI Increase
*
*	trigger_threshold
*		CCTI trigger for log message
*
*	ccti_min
*		CCTI Minimum
*
*********/

/****s* OpenSM: Subnet/osm_cct_t
* NAME
*	osm_cct_t
*
* DESCRIPTION
*	Subnet CongestionControlTable.  See A10.4.3.9 for format details.
*
* SYNOPSIS
*/
typedef struct osm_cct {
	osm_cct_entry_t entries[OSM_CCT_ENTRY_MAX];
	unsigned int entries_len;
	char *input_str;
} osm_cct_t;
/*
* FIELDS
*
*	entries
*		Entries in CCT
*
*	entries_len
*		Length of entries
*
*	input_str
*		Original str input
*
*********/


/****s* OpenSM: Subnet/osm_subn_opt_t
* NAME
*	osm_subn_opt_t
*
* DESCRIPTION
*	Subnet options structure.  This structure contains the various
*	site specific configuration parameters for the subnet.
*
* SYNOPSIS
*/
typedef struct osm_subn_opt {
	const char *config_file;
	ib_net64_t guid;
	ib_net64_t m_key;
	ib_net64_t sm_key;
	ib_net64_t sa_key;
	ib_net64_t subnet_prefix;
	ib_net16_t m_key_lease_period;
	uint8_t m_key_protect_bits;
	boolean_t m_key_lookup;
	uint32_t sweep_interval;
	uint32_t max_wire_smps;
	uint32_t max_wire_smps2;
	uint32_t max_smps_timeout;
	uint32_t transaction_timeout;
	uint32_t transaction_retries;
	uint8_t sm_priority;
	uint8_t lmc;
	boolean_t lmc_esp0;
	uint8_t max_op_vls;
	uint8_t force_link_speed;
	uint8_t force_link_speed_ext;
	uint8_t fdr10;
	boolean_t reassign_lids;
	boolean_t ignore_other_sm;
	boolean_t single_thread;
	boolean_t disable_multicast;
	boolean_t force_log_flush;
	uint8_t subnet_timeout;
	uint8_t packet_life_time;
	uint8_t vl_stall_count;
	uint8_t leaf_vl_stall_count;
	uint8_t head_of_queue_lifetime;
	uint8_t leaf_head_of_queue_lifetime;
	uint8_t local_phy_errors_threshold;
	uint8_t overrun_errors_threshold;
	boolean_t use_mfttop;
	uint32_t sminfo_polling_timeout;
	uint32_t polling_retry_number;
	uint32_t max_msg_fifo_timeout;
	boolean_t force_heavy_sweep;
	uint8_t log_flags;
	char *dump_files_dir;
	char *log_file;
	uint32_t log_max_size;
	char *partition_config_file;
	boolean_t no_partition_enforcement;
	char *part_enforce;
	osm_partition_enforce_type_enum part_enforce_enum;
	boolean_t allow_both_pkeys;
	uint8_t sm_assigned_guid;
	boolean_t qos;
	char *qos_policy_file;
	boolean_t suppress_sl2vl_mad_status_errors;
	boolean_t accum_log_file;
	char *console;
	uint16_t console_port;
	char *port_prof_ignore_file;
	char *hop_weights_file;
	char *port_search_ordering_file;
	boolean_t port_profile_switch_nodes;
	boolean_t sweep_on_trap;
	char *routing_engine_names;
	boolean_t use_ucast_cache;
	boolean_t connect_roots;
	char *lid_matrix_dump_file;
	char *lfts_file;
	char *root_guid_file;
	char *cn_guid_file;
	char *io_guid_file;
	boolean_t port_shifting;
	uint32_t scatter_ports;
	uint16_t max_reverse_hops;
	char *ids_guid_file;
	char *guid_routing_order_file;
	boolean_t guid_routing_order_no_scatter;
	char *sa_db_file;
	boolean_t sa_db_dump;
	char *torus_conf_file;
	boolean_t do_mesh_analysis;
	boolean_t exit_on_fatal;
	boolean_t honor_guid2lid_file;
	boolean_t daemon;
	boolean_t sm_inactive;
	boolean_t babbling_port_policy;
	boolean_t drop_event_subscriptions;
	boolean_t ipoib_mcgroup_creation_validation;
	boolean_t mcgroup_join_validation;
	boolean_t use_optimized_slvl;
	boolean_t fsync_high_avail_files;
	osm_qos_options_t qos_options;
	osm_qos_options_t qos_ca_options;
	osm_qos_options_t qos_sw0_options;
	osm_qos_options_t qos_swe_options;
	osm_qos_options_t qos_rtr_options;
	boolean_t congestion_control;
	ib_net64_t cc_key;
	uint32_t cc_max_outstanding_mads;
	ib_net32_t cc_sw_cong_setting_control_map;
	uint8_t cc_sw_cong_setting_victim_mask[IB_CC_PORT_MASK_DATA_SIZE];
	uint8_t cc_sw_cong_setting_credit_mask[IB_CC_PORT_MASK_DATA_SIZE];
	uint8_t cc_sw_cong_setting_threshold;
	uint8_t cc_sw_cong_setting_packet_size;
	uint8_t cc_sw_cong_setting_credit_starvation_threshold;
	osm_cct_entry_t cc_sw_cong_setting_credit_starvation_return_delay;
	ib_net16_t cc_sw_cong_setting_marking_rate;
	ib_net16_t cc_ca_cong_setting_port_control;
	ib_net16_t cc_ca_cong_setting_control_map;
	osm_cacongestion_entry_t cc_ca_cong_entries[IB_CA_CONG_ENTRY_DATA_SIZE];
	osm_cct_t cc_cct;
	boolean_t enable_quirks;
	boolean_t no_clients_rereg;
#ifdef ENABLE_OSM_PERF_MGR
	boolean_t perfmgr;
	boolean_t perfmgr_redir;
	uint16_t perfmgr_sweep_time_s;
	uint32_t perfmgr_max_outstanding_queries;
	boolean_t perfmgr_ignore_cas;
	char *event_db_dump_file;
	int perfmgr_rm_nodes;
	boolean_t perfmgr_log_errors;
	boolean_t perfmgr_query_cpi;
	boolean_t perfmgr_xmit_wait_log;
	uint32_t perfmgr_xmit_wait_threshold;
#endif				/* ENABLE_OSM_PERF_MGR */
	char *event_plugin_name;
	char *event_plugin_options;
	char *node_name_map_name;
	char *prefix_routes_file;
	char *log_prefix;
	boolean_t consolidate_ipv6_snm_req;
	struct osm_subn_opt *file_opts; /* used for update */
	uint8_t lash_start_vl;			/* starting vl to use in lash */
	uint8_t sm_sl;			/* which SL to use for SM/SA communication */
	char *per_module_logging_file;
	boolean_t quasi_ftree_indexing;
} osm_subn_opt_t;
/*
* FIELDS
*
*	config_file
*		The name of the config file.
*
*	guid
*		The port guid that the SM is binding to.
*
*	m_key
*		M_Key value sent to all ports qualifying all Set(PortInfo).
*
*	sm_key
*		SM_Key value of the SM used for SM authentication.
*
*	sa_key
*		SM_Key value to qualify rcv SA queries as "trusted".
*
*	subnet_prefix
*		Subnet prefix used on this subnet.
*
*	m_key_lease_period
*		The lease period used for the M_Key on this subnet.
*
*	sweep_interval
*		The number of seconds between subnet sweeps.  A value of 0
*		disables sweeping.
*
*	max_wire_smps
*		The maximum number of SMPs sent in parallel.  Default is 4.
*
*	max_wire_smps2
*		The maximum number of timeout SMPs allowed to be outstanding.
*		Default is same as max_wire_smps which disables the timeout
*		mechanism.
*
*	max_smps_timeout
*		The wait time in usec for timeout based SMPs.  Default is
*		timeout * retries.
*
*	transaction_timeout
*		The maximum time in milliseconds allowed for a transaction
*		to complete.  Default is 200.
*
*	transaction_retries
*		The number of retries for a transaction. Default is 3.
*
*	sm_priority
*		The priority of this SM as specified by the user.  This
*		value is made available in the SMInfo attribute.
*
*	lmc
*		The LMC value used on this subnet.
*
*	lmc_esp0
*		Whether LMC value used on subnet should be used for
*		enhanced switch port 0 or not.  If TRUE, it is used.
*		Otherwise (the default), LMC is set to 0 for ESP0.
*
*	max_op_vls
*		Limit the maximal operational VLs. default is 1.
*
*	reassign_lids
*		If TRUE cause all lids to be re-assigend.
*		Otherwise (the default),
*		OpenSM always tries to preserve as LIDs as much as possible.
*
*	ignore_other_sm_option
*		This flag is TRUE if other SMs on the subnet should be ignored.
*
*	disable_multicast
*		This flag is TRUE if OpenSM should disable multicast support.
*
*	max_msg_fifo_timeout
*		The maximal time a message can stay in the incoming message
*		queue. If there is more than one message in the queue and the
*		last message stayed in the queue more than this value the SA
*		request will be immediately returned with a BUSY status.
*
*	subnet_timeout
*		The subnet_timeout that will be set for all the ports in the
*		design SubnSet(PortInfo.vl_stall_life))
*
*	vl_stall_count
*		The number of sequential packets dropped that cause the port
*		to enter the VLStalled state.
*
*	leaf_vl_stall_count
*		The number of sequential packets dropped that cause the port
*		to enter the VLStalled state. This is for switch ports driving
*		a CA or router port.
*
*	head_of_queue_lifetime
*		The maximal time a packet can live at the head of a VL queue
*		on any port not driving a CA or router port.
*
*	leaf_head_of_queue_lifetime
*		The maximal time a packet can live at the head of a VL queue
*		on switch ports driving a CA or router.
*
*	local_phy_errors_threshold
*		Threshold of local phy errors for sending Trap 129
*
*	overrun_errors_threshold
*		Threshold of credits overrun errors for sending Trap 129
*
*	sminfo_polling_timeout
*		Specifies the polling timeout (in milliseconds) - the timeout
*		between one poll to another.
*
*	packet_life_time
*		The maximal time a packet can stay in a switch.
*		The value is send to all switches as
*		SubnSet(SwitchInfo.life_state)
*
*	dump_files_dir
*		The directory to be used for opensm-subnet.lst, opensm.fdbs,
*		opensm.mcfdbs, and default log file (the latter for Windows,
*		not Linux).
*
*	log_file
*		Name of the log file (or NULL) for stdout.
*
*	log_max_size
*		This option defines maximal log file size in MB. When
*		specified the log file will be truncated upon reaching
*		this limit.
*
*	qos
*		Boolean that specifies whether the OpenSM QoS functionality
*		should be off or on.
*
*	qos_policy_file
*		Name of the QoS policy file.
*
*	accum_log_file
*		If TRUE (default) - the log file will be accumulated.
*		If FALSE - the log file will be erased before starting
*		current opensm run.
*
*	port_prof_ignore_file
*		Name of file with port guids to be ignored by port profiling.
*
*	port_profile_switch_nodes
*		If TRUE will count the number of switch nodes routed through
*		the link. If FALSE - only CA/RT nodes are counted.
*
*	sweep_on_trap
*		Received traps will initiate a new sweep.
*
*	routing_engine_names
*		Name of routing engine(s) to use.
*
*	connect_roots
*		The option which will enforce root to root connectivity with
*		up/down and fat-tree routing engines (even if this violates
*		"pure" deadlock free up/down or fat-tree algorithm)
*
*	use_ucast_cache
*		When TRUE enables unicast routing cache.
*
*	lid_matrix_dump_file
*		Name of the lid matrix dump file from where switch
*		lid matrices (min hops tables) will be loaded
*
*	lfts_file
*		Name of the unicast LFTs routing file from where switch
*		forwarding tables will be loaded
*
*	root_guid_file
*		Name of the file that contains list of root guids that
*		will be used by fat-tree or up/dn routing (provided by User)
*
*	cn_guid_file
*		Name of the file that contains list of compute node guids that
*		will be used by fat-tree routing (provided by User)
*
*	io_guid_file
*		Name of the file that contains list of I/O node guids that
*		will be used by fat-tree routing (provided by User)
*
*	port_shifting
*		This option will turn on port_shifting in routing.
*
*	ids_guid_file
*		Name of the file that contains list of ids which should be
*		used by Up/Down algorithm instead of node GUIDs
*
*	guid_routing_order_file
*		Name of the file that contains list of guids for routing order
*		that will be used by minhop and up/dn routing (provided by User).
*
*	sa_db_file
*		Name of the SA database file.
*
*	sa_db_dump
*		When TRUE causes OpenSM to dump SA DB at the end of every
*		light sweep regardless the current verbosity level.
*
*	torus_conf_file
*		Name of the file with extra configuration info for torus-2QoS
*		routing engine.
*
*	exit_on_fatal
*		If TRUE (default) - SM will exit on fatal subnet initialization
*		issues.
*		If FALSE - SM will not exit.
*		Fatal initialization issues:
*		a. SM recognizes 2 different nodes with the same guid, or
*		   12x link with lane reversal badly configured.
*
*	honor_guid2lid_file
*		Always honor the guid2lid file if it exists and is valid. This
*		means that the file will be honored when SM is coming out of
*		STANDBY. By default this is FALSE.
*
*	daemon
*		OpenSM will run in daemon mode.
*
*	sm_inactive
*		OpenSM will start with SM in not active state.
*
*	babbling_port_policy
*		OpenSM will enforce its "babbling" port policy.
*
*	drop_event_subscriptions
*		OpenSM will drop event subscriptions if the port goes away.
*
*	ipoib_mcgroup_creation_validation
*		OpenSM will validate IPoIB non-broadcast group parameters
*		against IPoIB broadcast group.
*
*	mcgroup_join_validation
*		OpenSM will validate multicast join parameters against
*		multicast group parameters when MC group already exists.
*
*	use_optimized_slvl
*		Use optimized SLtoVLMappingTable programming if
*		device indicates it supports this.
*
*	fsync_high_avail_files
*		Synchronize high availability in memory files
*		with storage.
*
*	perfmgr
*		Enable or disable the performance manager
*
*	perfmgr_redir
*		Enable or disable the saving of redirection by PerfMgr
*
*	perfmgr_sweep_time_s
*		Define the period (in seconds) of PerfMgr sweeps
*
*       event_db_dump_file
*               File to dump the event database to
*
*       event_plugin_name
*               Specify the name(s) of the event plugin(s)
*
*       event_plugin_options
*               Options string that would be passed to the plugin(s)
*
*	qos_options
*		Default set of QoS options
*
*	qos_ca_options
*		QoS options for CA ports
*
*	qos_sw0_options
*		QoS options for switches' port 0
*
*	qos_swe_options
*		QoS options for switches' external ports
*
*	qos_rtr_options
*		QoS options for router ports
*
*	congestion_control
*		Boolean that specifies whether OpenSM congestion control configuration
*		should be off or no.
*
*	cc_key
*		CCkey to use when configuring congestion control.
*
*	cc_max_outstanding_mads
*		Max number of outstanding CC mads that can be on the wire.
*
*	cc_sw_cong_setting_control_map
*		Congestion Control Switch Congestion Setting Control Map
*		configuration setting.
*
*	cc_sw_cong_setting_victim_mask
*		Congestion Control Switch Congestion Setting Victim Mask
*		configuration setting.
*
*	cc_sw_cong_setting_credit_mask
*		Congestion Control Switch Congestion Setting Credit Mask
*		configuration setting.
*
*	cc_sw_cong_setting_threshold
*		Congestion Control Switch Congestion Setting Threshold
*		configuration setting.
*
*	cc_sw_cong_setting_packet_size
*		Congestion Control Switch Congestion Setting Packet Size
*		configuration setting.
*
*	cc_sw_cong_setting_credit_starvation_threshold
*		Congestion Control Switch Congestion Setting Credit Starvation Threshold
*		configuration setting.
*
*	cc_sw_cong_setting_credit_starvation_return_delay
*		Congestion Control Switch Congestion Setting Credit Starvation Return Delay
*		configuration setting.
*
*	cc_sw_cong_setting_marking_rate
*		Congestion Control Switch Congestion Setting Marking Rate
*		configuration setting.
*
*	cc_ca_cong_setting_port_control
*		Congestion Control CA Congestion Setting Port Control
*
*	cc_ca_cong_setting_control_map
*		Congestion Control CA Congestion Setting Control Map

*	cc_ca_cong_entries
*		Congestion Control CA Congestion Setting Entries
*
*	cc_cct
*		Congestion Control Table array of entries
*
*	enable_quirks
*		Enable high risk new features and not fully qualified
*		hardware specific work arounds
*
*	no_clients_rereg
*		When TRUE disables clients reregistration request
*
*	scatter_ports
*		When not zero, randomize best possible ports chosen
*		for a route. The value is used as a random key seed.
*
*	per_module_logging_file
*		File name of per module logging configuration.
*
* SEE ALSO
*	Subnet object
*********/

/****s* OpenSM: Subnet/osm_subn_t
* NAME
*	osm_subn_t
*
* DESCRIPTION
*	Subnet structure.  Callers may directly access member components,
*	after grabbing a lock.
*
* TO DO
*	This structure should probably be volatile.
*
* SYNOPSIS
*/
typedef struct osm_subn {
	struct osm_opensm *p_osm;
	cl_qmap_t sw_guid_tbl;
	cl_qmap_t node_guid_tbl;
	cl_qmap_t port_guid_tbl;
	cl_qmap_t alias_port_guid_tbl;
	cl_qmap_t assigned_guids_tbl;
	cl_qmap_t rtr_guid_tbl;
	cl_qlist_t prefix_routes_list;
	cl_qmap_t prtn_pkey_tbl;
	cl_qmap_t sm_guid_tbl;
	cl_qlist_t sa_sr_list;
	cl_qlist_t sa_infr_list;
	cl_qlist_t alias_guid_list;
	cl_ptr_vector_t port_lid_tbl;
	ib_net16_t master_sm_base_lid;
	ib_net16_t sm_base_lid;
	ib_net64_t sm_port_guid;
	uint8_t last_sm_port_state;
	uint8_t sm_state;
	osm_subn_opt_t opt;
	struct osm_qos_policy *p_qos_policy;
	uint16_t max_ucast_lid_ho;
	uint16_t max_mcast_lid_ho;
	uint8_t min_ca_mtu;
	uint8_t min_ca_rate;
	uint8_t min_data_vls;
	uint8_t min_sw_data_vls;
	boolean_t ignore_existing_lfts;
	boolean_t subnet_initialization_error;
	boolean_t force_heavy_sweep;
	boolean_t force_reroute;
	boolean_t in_sweep_hop_0;
	boolean_t force_first_time_master_sweep;
	boolean_t first_time_master_sweep;
	boolean_t coming_out_of_standby;
	boolean_t sweeping_enabled;
	unsigned need_update;
	cl_fmap_t mgrp_mgid_tbl;
	osm_db_domain_t *p_g2m;
	osm_db_domain_t *p_neighbor;
	void *mboxes[IB_LID_MCAST_END_HO - IB_LID_MCAST_START_HO + 1];
} osm_subn_t;
/*
* FIELDS
*	sw_guid_tbl
*		Container of pointers to all Switch objects in the subnet.
*		Indexed by node GUID.
*
*	node_guid_tbl
*		Container of pointers to all Node objects in the subnet.
*		Indexed by node GUID.
*
*	port_guid_tbl
*		Container of pointers to all Port objects in the subnet.
*		Indexed by port GUID.
*
*	rtr_guid_tbl
*		Container of pointers to all Router objects in the subnet.
*		Indexed by node GUID.
*
*	prtn_pkey_tbl
*		Container of pointers to all Partition objects in the subnet.
*		Indexed by P_KEY.
*
*	sm_guid_tbl
*		Container of pointers to SM objects representing other SMs
*		on the subnet.
*
*	port_lid_tbl
*		Container of pointers to all Port objects in the subnet.
*		Indexed by port LID.
*
*	master_sm_base_lid
*		The base LID owned by the subnet's master SM.
*
*	sm_base_lid
*		The base LID of the local port where the SM is.
*
*	sm_port_guid
*		This SM's own port GUID.
*
*	last_sm_port_state
*		Last state of this SM's port.
*		0 is down and 1 is up.
*
*	sm_state
*		The high-level state of the SM.  This value is made available
*		in the SMInfo attribute.
*
*	opt
*		Subnet options structure contains site specific configuration.
*
*	p_qos_policy
*		Subnet QoS policy structure.
*
*	max_ucast_lid_ho
*		The minimal max unicast lid reported by all switches
*
*	max_mcast_lid_ho
*		The minimal max multicast lid reported by all switches
*
*	min_ca_mtu
*		The minimal MTU reported by all CAs ports on the subnet
*
*	min_ca_rate
*		The minimal rate reported by all CA ports on the subnet
*
*	ignore_existing_lfts
*		This flag is a dynamic flag to instruct the LFT assignment to
*		ignore existing legal LFT settings.
*		The value will be set according to :
*		- Any change to the list of switches will set it to high
*		- Coming out of STANDBY it will be cleared (other SM worked)
*		- Set to FALSE upon end of all lft assignments.
*
*	subnet_initalization_error
*		Similar to the force_heavy_sweep flag. If TRUE - means that
*		we had errors during initialization (due to SubnSet requests
*		that failed). We want to declare the subnet as unhealthy, and
*		force another heavy sweep.
*
*	force_heavy_sweep
*		If TRUE - we want to force a heavy sweep. This can be done
*		either due to receiving of trap - meaning there is some change
*		on the subnet, or we received a handover from a remote sm.
*		In this case we want to sweep and reconfigure the entire
*		subnet. This will cause another heavy sweep to occure when
*		the current sweep is done.
*
*	force_reroute
*		If TRUE - we want to force switches in the fabric to be
*		rerouted.
*
*	in_sweep_hop_0
*		When in_sweep_hop_0 flag is set to TRUE - this means we are
*		in sweep_hop_0 - meaning we do not want to continue beyond
*		the current node.
*		This is relevant for the case of SM on switch, since in the
*		switch info we need to signal somehow not to continue
*		the sweeping.
*
*	force_first_time_master_sweep
*		This flag is used to avoid race condition when Master SM being
*		in the middle of very long configuration stage of the heavy sweep,
*		receives HANDOVER from another MASTER SM. When the current heavy sweep
*		is finished, new heavy sweep will be started immediately.
*		At the beginning of the sweep, opensm will set first_time_master_sweep,
*		force_heavy_sweep and coming_out_of_standby flags in order to allow full
*		reconfiguration of the fabric. This is required as another MASTER SM could
*		change configuration of the fabric before sending HANDOVER to MASTER SM.
*
*	first_time_master_sweep
*		This flag is used for the PortInfo setting. On the first
*		sweep as master (meaning after moving from Standby|Discovering
*		state), the SM must send a PortInfoSet to all ports. After
*		that - we want to minimize the number of PortInfoSet requests
*		sent, and to send only requests that change the value from
*		what is updated in the port (or send a first request if this
*		is a new port). We will set this flag to TRUE when entering
*		the master state, and set it back to FALSE at the end of the
*		drop manager. This is done since at the end of the drop manager
*		we have updated all the ports that are reachable, and from now
*		on these are the only ports we have data of. We don't want
*		to send extra set requests to these ports anymore.
*
*	coming_out_of_standby
*		TRUE on the first sweep after the SM was in standby.
*		Used for nulling any cache of LID and Routing.
*		The flag is set true if the SM state was standby and now
*		changed to MASTER it is reset at the end of the sweep.
*
*	sweeping_enabled
*		FALSE - sweeping is administratively disabled, all
*		sweeping is inhibited, TRUE - sweeping is done
*		normally
*
*	need_update
*		This flag should be on during first non-master heavy
*		(including pre-master discovery stage)
*
*	mgrp_mgid_tbl
*		Container of pointers to all Multicast group objects in
*		the subnet. Indexed by MGID.
*
*	mboxes
*		Array of pointers to all Multicast MLID box objects in the
*		subnet. Indexed by MLID offset from base MLID.
*
* SEE ALSO
*	Subnet object
*********/

/****s* OpenSM: Subnet/osm_assigned_guids_t
* NAME
*	osm_assigned_guids_t
*
* DESCRIPTION
*	SA assigned GUIDs structure.
*
* SYNOPSIS
*/
typedef struct osm_assigned_guids {
	cl_map_item_t map_item;
	ib_net64_t port_guid;
	ib_net64_t assigned_guid[1];
} osm_assigned_guids_t;
/*
* FIELDS
*	map_item
*		Linkage structure for cl_qmap.  MUST BE FIRST MEMBER!
*
*	port_guid
*		Base port GUID.
*
*	assigned_guids
*		Table of persistent SA assigned GUIDs.
*
* SEE ALSO
*	Subnet object
*********/

/****f* OpenSM: Subnet/osm_subn_construct
* NAME
*	osm_subn_construct
*
* DESCRIPTION
*	This function constructs a Subnet object.
*
* SYNOPSIS
*/
void osm_subn_construct(IN osm_subn_t * p_subn);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to a Subnet object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_subn_init, and osm_subn_destroy.
*
*	Calling osm_subn_construct is a prerequisite to calling any other
*	method except osm_subn_init.
*
* SEE ALSO
*	Subnet object, osm_subn_init, osm_subn_destroy
*********/

/****f* OpenSM: Subnet/osm_subn_destroy
* NAME
*	osm_subn_destroy
*
* DESCRIPTION
*	The osm_subn_destroy function destroys a subnet, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_subn_destroy(IN osm_subn_t * p_subn);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to a Subnet object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified Subnet object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to osm_subn_construct
*	or osm_subn_init.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_init
*********/

/****f* OpenSM: Subnet/osm_subn_init
* NAME
*	osm_subn_init
*
* DESCRIPTION
*	The osm_subn_init function initializes a Subnet object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_subn_init(IN osm_subn_t * p_subn,
			      IN struct osm_opensm *p_osm,
			      IN const osm_subn_opt_t * p_opt);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object to initialize.
*
*	p_opt
*		[in] Pointer to the subnet options structure.
*
* RETURN VALUES
*	IB_SUCCESS if the Subnet object was initialized successfully.
*
* NOTES
*	Allows calling other Subnet methods.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy
*********/

/*
  Forward references.
*/
struct osm_mad_addr;
struct osm_log;
struct osm_switch;
struct osm_physp;
struct osm_port;
struct osm_mgrp;

/****f* OpenSM: Helper/osm_get_gid_by_mad_addr
* NAME
*	osm_get_gid_by_mad_addr
*
* DESCRIPTION
*	Looks for the requester gid in the mad address.
*
* Note: This code is not thread safe. Need to grab the lock before
* calling it.
*
* SYNOPSIS
*/
ib_api_status_t osm_get_gid_by_mad_addr(IN struct osm_log *p_log,
					IN const osm_subn_t * p_subn,
					IN struct osm_mad_addr *p_mad_addr,
					OUT ib_gid_t * p_gid);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to a log object.
*
*	p_subn
*		[in] Pointer to subnet object.
*
*	p_mad_addr
*		[in] Pointer to mad address object.
*
*	p_gid
*		[out] Pointer to the GID structure to fill in.
*
* RETURN VALUES
*     IB_SUCCESS if able to find the GID by address given.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/osm_get_physp_by_mad_addr
* NAME
*	osm_get_physp_by_mad_addr
*
* DESCRIPTION
*	Looks for the requester physical port in the mad address.
*
* Note: This code is not thread safe. Need to grab the lock before
* calling it.
*
* SYNOPSIS
*/
struct osm_physp *osm_get_physp_by_mad_addr(IN struct osm_log *p_log,
					     IN const osm_subn_t * p_subn,
					     IN struct osm_mad_addr
					     *p_mad_addr);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to a log object.
*
*	p_subn
*		[in] Pointer to subnet object.
*
*	p_mad_addr
*		[in] Pointer to mad address object.
*
* RETURN VALUES
*	Pointer to requester physical port object if found. Null otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Helper/osm_get_port_by_mad_addr
* NAME
*	osm_get_port_by_mad_addr
*
* DESCRIPTION
*	Looks for the requester port in the mad address.
*
* Note: This code is not thread safe. Need to grab the lock before
* calling it.
*
* SYNOPSIS
*/
struct osm_port *osm_get_port_by_mad_addr(IN struct osm_log *p_log,
					   IN const osm_subn_t * p_subn,
					   IN struct osm_mad_addr *p_mad_addr);
/*
* PARAMETERS
*	p_log
*		[in] Pointer to a log object.
*
*	p_subn
*		[in] Pointer to subnet object.
*
*	p_mad_addr
*		[in] Pointer to mad address object.
*
* RETURN VALUES
*	Pointer to requester port object if found. Null otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Subnet/osm_get_switch_by_guid
* NAME
*	osm_get_switch_by_guid
*
* DESCRIPTION
*	Looks for the given switch guid in the subnet table of switches by guid.
*  NOTE: this code is not thread safe. Need to grab the lock before
*  calling it.
*
* SYNOPSIS
*/
struct osm_switch *osm_get_switch_by_guid(IN const osm_subn_t * p_subn,
					  IN ib_net64_t guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	guid
*		[in] The node guid in network byte order
*
* RETURN VALUES
*	The switch structure pointer if found. NULL otherwise.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy,
*	osm_switch_t
*********/

/****f* OpenSM: Subnet/osm_get_node_by_guid
* NAME
*	osm_get_node_by_guid
*
* DESCRIPTION
*	This looks for the given node guid in the subnet table of nodes by guid.
*  NOTE: this code is not thread safe. Need to grab the lock before
*  calling it.
*
* SYNOPSIS
*/
struct osm_node *osm_get_node_by_guid(IN osm_subn_t const *p_subn,
				      IN ib_net64_t guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	guid
*		[in] The node guid in network byte order
*
* RETURN VALUES
*	The node structure pointer if found. NULL otherwise.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy,
*	osm_node_t
*********/

/****f* OpenSM: Subnet/osm_get_port_by_guid
* NAME
*	osm_get_port_by_guid
*
* DESCRIPTION
*	This looks for the given port guid in the subnet table of ports by guid.
*  NOTE: this code is not thread safe. Need to grab the lock before
*  calling it.
*
* SYNOPSIS
*/
struct osm_port *osm_get_port_by_guid(IN osm_subn_t const *p_subn,
				      IN ib_net64_t guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	guid
*		[in] The port guid in network order
*
* RETURN VALUES
*	The port structure pointer if found. NULL otherwise.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy,
*	osm_port_t
*********/

/****f* OpenSM: Port/osm_get_port_by_lid_ho
* NAME
*	osm_get_port_by_lid_ho
*
* DESCRIPTION
*	Returns a pointer of the port object for given lid value.
*
* SYNOPSIS
*/
struct osm_port *osm_get_port_by_lid_ho(const osm_subn_t * subn, uint16_t lid);
/*
* PARAMETERS
*	subn
*		[in] Pointer to the subnet data structure.
*
*	lid
*		[in] LID requested in host byte order.
*
* RETURN VALUES
*	The port structure pointer if found. NULL otherwise.
*
* SEE ALSO
*       Subnet object, osm_port_t
*********/

/****f* OpenSM: Subnet/osm_get_alias_guid_by_guid
* NAME
*	osm_get_alias_guid_by_guid
*
* DESCRIPTION
*	This looks for the given port guid in the subnet table of ports by
*	alias guid.
*  NOTE: this code is not thread safe. Need to grab the lock before
*  calling it.
*
* SYNOPSIS
*/
struct osm_alias_guid *osm_get_alias_guid_by_guid(IN osm_subn_t const *p_subn,
						  IN ib_net64_t guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	guid
*		[in] The alias port guid in network order
*
* RETURN VALUES
*	The alias guid structure pointer if found. NULL otherwise.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy,
*	osm_alias_guid_t
*********/

/****f* OpenSM: Subnet/osm_get_port_by_alias_guid
* NAME
*	osm_get_port_by_alias_guid
*
* DESCRIPTION
*	This looks for the given port guid in the subnet table of ports by
*	alias guid.
*  NOTE: this code is not thread safe. Need to grab the lock before
*  calling it.
*
* SYNOPSIS
*/
struct osm_port *osm_get_port_by_alias_guid(IN osm_subn_t const *p_subn,
					    IN ib_net64_t guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	guid
*		[in] The alias port guid in network order
*
* RETURN VALUES
*	The port structure pointer if found. NULL otherwise.
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy,
*	osm_port_t
*********/

/****f* OpenSM: Port/osm_assigned_guids_new
* NAME
*	osm_assigned_guids_new
*
* DESCRIPTION
*	This function allocates and initializes an assigned guids object.
*
* SYNOPSIS
*/
osm_assigned_guids_t *osm_assigned_guids_new(IN const ib_net64_t port_guid,
					     IN const uint32_t num_guids);
/*
* PARAMETERS
*       port_guid
*               [in] Base port GUID in network order
*
* RETURN VALUE
*       Pointer to the initialized assigned alias guid object.
*
* SEE ALSO
*	Subnet object, osm_assigned_guids_t, osm_assigned_guids_delete,
*	osm_get_assigned_guids_by_guid
*********/

/****f* OpenSM: Port/osm_assigned_guids_delete
* NAME
*	osm_assigned_guids_delete
*
* DESCRIPTION
*	This function destroys and deallocates an assigned guids object.
*
* SYNOPSIS
*/
void osm_assigned_guids_delete(IN OUT osm_assigned_guids_t ** pp_assigned_guids);
/*
* PARAMETERS
*       pp_assigned_guids
*		[in][out] Pointer to a pointer to an assigned guids object to delete.
*		On return, this pointer is NULL.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified assigned guids object.
*
* SEE ALSO
*	Subnet object, osm_assigned_guids_new, osm_get_assigned_guids_by_guid
*********/

/****f* OpenSM: Subnet/osm_get_assigned_guids_by_guid
* NAME
*	osm_get_assigned_guids_by_guid
*
* DESCRIPTION
*	This looks for the given port guid and returns a pointer
*	to the guid table of SA assigned alias guids for that port.
*
* SYNOPSIS
*/
osm_assigned_guids_t *osm_get_assigned_guids_by_guid(IN osm_subn_t const *p_subn,
						     IN ib_net64_t port_guid);
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	port_guid
*		[in] The base port guid in network order
*
* RETURN VALUES
*	The osm_assigned_guids structure pointer if found. NULL otherwise.
*
* SEE ALSO
*	Subnet object, osm_assigned_guids_new, osm_assigned_guids_delete,
*	osm_assigned_guids_t
*********/

/****f* OpenSM: Port/osm_get_port_by_lid
* NAME
*	osm_get_port_by_lid
*
* DESCRIPTION
*	Returns a pointer of the port object for given lid value.
*
* SYNOPSIS
*/
static inline struct osm_port *osm_get_port_by_lid(IN osm_subn_t const * subn,
						   IN ib_net16_t lid)
{
	return osm_get_port_by_lid_ho(subn, cl_ntoh16(lid));
}
/*
* PARAMETERS
*	subn
*		[in] Pointer to the subnet data structure.
*
*	lid
*		[in] LID requested in network byte order.
*
* RETURN VALUES
*	The port structure pointer if found. NULL otherwise.
*
* SEE ALSO
*       Subnet object, osm_port_t
*********/

/****f* OpenSM: Subnet/osm_get_mgrp_by_mgid
* NAME
*	osm_get_mgrp_by_mgid
*
* DESCRIPTION
*	This looks for the given multicast group in the subnet table by mgid.
*	NOTE: this code is not thread safe. Need to grab the lock before
*	calling it.
*
* SYNOPSIS
*/
struct osm_mgrp *osm_get_mgrp_by_mgid(IN osm_subn_t * subn, IN ib_gid_t * mgid);
/*
* PARAMETERS
*	subn
*		[in] Pointer to an osm_subn_t object
*
*	mgid
*		[in] The multicast group MGID value
*
* RETURN VALUES
*	The multicast group structure pointer if found. NULL otherwise.
*********/

/****f* OpenSM: Subnet/osm_get_mbox_by_mlid
* NAME
*	osm_get_mbox_by_mlid
*
* DESCRIPTION
*	This looks for the given multicast group in the subnet table by mlid.
*	NOTE: this code is not thread safe. Need to grab the lock before
*	calling it.
*
* SYNOPSIS
*/
static inline struct osm_mgrp_box *osm_get_mbox_by_mlid(osm_subn_t const *p_subn, ib_net16_t mlid)
{
	return (struct osm_mgrp_box *)p_subn->mboxes[cl_ntoh16(mlid) - IB_LID_MCAST_START_HO];
}
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to an osm_subn_t object
*
*	mlid
*		[in] The multicast group mlid in network order
*
* RETURN VALUES
*	The multicast group structure pointer if found. NULL otherwise.
*********/

int is_mlnx_ext_port_info_supported(ib_net32_t vendid, ib_net16_t devid);

/****f* OpenSM: Subnet/osm_subn_set_default_opt
* NAME
*	osm_subn_set_default_opt
*
* DESCRIPTION
*	The osm_subn_set_default_opt function sets the default options.
*
* SYNOPSIS
*/
void osm_subn_set_default_opt(IN osm_subn_opt_t * p_opt);
/*
* PARAMETERS
*
*	p_opt
*		[in] Pointer to the subnet options structure.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	Subnet object, osm_subn_construct, osm_subn_destroy
*********/

/****f* OpenSM: Subnet/osm_subn_parse_conf_file
* NAME
*	osm_subn_parse_conf_file
*
* DESCRIPTION
*	The osm_subn_parse_conf_file function parses the configuration file
*	and sets the defaults accordingly.
*
* SYNOPSIS
*/
int osm_subn_parse_conf_file(const char *conf_file, osm_subn_opt_t * p_opt);
/*
* PARAMETERS
*
*	p_opt
*		[in] Pointer to the subnet options structure.
*
* RETURN VALUES
*	0 on success, positive value if file doesn't exist,
*	negative value otherwise
*********/

/****f* OpenSM: Subnet/osm_subn_rescan_conf_files
* NAME
*	osm_subn_rescan_conf_files
*
* DESCRIPTION
*	The osm_subn_rescan_conf_files function parses the configuration
*	files and update selected subnet options
*
* SYNOPSIS
*/
int osm_subn_rescan_conf_files(IN osm_subn_t * p_subn);
/*
* PARAMETERS
*
*	p_subn
*		[in] Pointer to the subnet structure.
*
* RETURN VALUES
*	0 on success, positive value if file doesn't exist,
*	negative value otherwise
*
*********/

/****f* OpenSM: Subnet/osm_subn_output_conf
* NAME
*	osm_subn_output_conf
*
* DESCRIPTION
*	Output configuration info
*
* SYNOPSIS
*/
void osm_subn_output_conf(FILE *out, IN osm_subn_opt_t * p_opt);
/*
* PARAMETERS
*
*	out
*		[in] File stream to output to.
*
*	p_opt
*		[in] Pointer to the subnet options structure.
*
* RETURN VALUES
*	This method does not return a value
*********/

/****f* OpenSM: Subnet/osm_subn_write_conf_file
* NAME
*	osm_subn_write_conf_file
*
* DESCRIPTION
*	Write the configuration file into the cache
*
* SYNOPSIS
*/
int osm_subn_write_conf_file(char *file_name, IN osm_subn_opt_t * p_opt);
/*
* PARAMETERS
*
*	p_opt
*		[in] Pointer to the subnet options structure.
*
* RETURN VALUES
*	0 on success, negative value otherwise
*
* NOTES
*	Assumes the conf file is part of the cache dir which defaults to
*	OSM_DEFAULT_CACHE_DIR or OSM_CACHE_DIR the name is opensm.opts
*********/
int osm_subn_verify_config(osm_subn_opt_t * p_opt);

END_C_DECLS
#endif				/* _OSM_SUBNET_H_ */
