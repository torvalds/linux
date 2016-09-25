/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_VIRTCHNL_H_
#define _I40E_VIRTCHNL_H_

#include "i40e_type.h"

/* Description:
 * This header file describes the VF-PF communication protocol used
 * by the various i40e drivers.
 *
 * Admin queue buffer usage:
 * desc->opcode is always i40e_aqc_opc_send_msg_to_pf
 * flags, retval, datalen, and data addr are all used normally.
 * Firmware copies the cookie fields when sending messages between the PF and
 * VF, but uses all other fields internally. Due to this limitation, we
 * must send all messages as "indirect", i.e. using an external buffer.
 *
 * All the vsi indexes are relative to the VF. Each VF can have maximum of
 * three VSIs. All the queue indexes are relative to the VSI.  Each VF can
 * have a maximum of sixteen queues for all of its VSIs.
 *
 * The PF is required to return a status code in v_retval for all messages
 * except RESET_VF, which does not require any response. The return value is of
 * i40e_status_code type, defined in the i40e_type.h.
 *
 * In general, VF driver initialization should roughly follow the order of these
 * opcodes. The VF driver must first validate the API version of the PF driver,
 * then request a reset, then get resources, then configure queues and
 * interrupts. After these operations are complete, the VF driver may start
 * its queues, optionally add MAC and VLAN filters, and process traffic.
 */

/* Opcodes for VF-PF communication. These are placed in the v_opcode field
 * of the virtchnl_msg structure.
 */
enum i40e_virtchnl_ops {
/* The PF sends status change events to VFs using
 * the I40E_VIRTCHNL_OP_EVENT opcode.
 * VFs send requests to the PF using the other ops.
 */
	I40E_VIRTCHNL_OP_UNKNOWN = 0,
	I40E_VIRTCHNL_OP_VERSION = 1, /* must ALWAYS be 1 */
	I40E_VIRTCHNL_OP_RESET_VF = 2,
	I40E_VIRTCHNL_OP_GET_VF_RESOURCES = 3,
	I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE = 4,
	I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE = 5,
	I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES = 6,
	I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP = 7,
	I40E_VIRTCHNL_OP_ENABLE_QUEUES = 8,
	I40E_VIRTCHNL_OP_DISABLE_QUEUES = 9,
	I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS = 10,
	I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS = 11,
	I40E_VIRTCHNL_OP_ADD_VLAN = 12,
	I40E_VIRTCHNL_OP_DEL_VLAN = 13,
	I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE = 14,
	I40E_VIRTCHNL_OP_GET_STATS = 15,
	I40E_VIRTCHNL_OP_FCOE = 16,
	I40E_VIRTCHNL_OP_EVENT = 17, /* must ALWAYS be 17 */
	I40E_VIRTCHNL_OP_IWARP = 20,
	I40E_VIRTCHNL_OP_CONFIG_IWARP_IRQ_MAP = 21,
	I40E_VIRTCHNL_OP_RELEASE_IWARP_IRQ_MAP = 22,
	I40E_VIRTCHNL_OP_CONFIG_RSS_KEY = 23,
	I40E_VIRTCHNL_OP_CONFIG_RSS_LUT = 24,
	I40E_VIRTCHNL_OP_GET_RSS_HENA_CAPS = 25,
	I40E_VIRTCHNL_OP_SET_RSS_HENA = 26,

};

/* Virtual channel message descriptor. This overlays the admin queue
 * descriptor. All other data is passed in external buffers.
 */

struct i40e_virtchnl_msg {
	u8 pad[8];			 /* AQ flags/opcode/len/retval fields */
	enum i40e_virtchnl_ops v_opcode; /* avoid confusion with desc->opcode */
	i40e_status v_retval;  /* ditto for desc->retval */
	u32 vfid;			 /* used by PF when sending to VF */
};

/* Message descriptions and data structures.*/

/* I40E_VIRTCHNL_OP_VERSION
 * VF posts its version number to the PF. PF responds with its version number
 * in the same format, along with a return code.
 * Reply from PF has its major/minor versions also in param0 and param1.
 * If there is a major version mismatch, then the VF cannot operate.
 * If there is a minor version mismatch, then the VF can operate but should
 * add a warning to the system log.
 *
 * This enum element MUST always be specified as == 1, regardless of other
 * changes in the API. The PF must always respond to this message without
 * error regardless of version mismatch.
 */
#define I40E_VIRTCHNL_VERSION_MAJOR		1
#define I40E_VIRTCHNL_VERSION_MINOR		1
#define I40E_VIRTCHNL_VERSION_MINOR_NO_VF_CAPS	0

struct i40e_virtchnl_version_info {
	u32 major;
	u32 minor;
};

/* I40E_VIRTCHNL_OP_RESET_VF
 * VF sends this request to PF with no parameters
 * PF does NOT respond! VF driver must delay then poll VFGEN_RSTAT register
 * until reset completion is indicated. The admin queue must be reinitialized
 * after this operation.
 *
 * When reset is complete, PF must ensure that all queues in all VSIs associated
 * with the VF are stopped, all queue configurations in the HMC are set to 0,
 * and all MAC and VLAN filters (except the default MAC address) on all VSIs
 * are cleared.
 */

/* I40E_VIRTCHNL_OP_GET_VF_RESOURCES
 * Version 1.0 VF sends this request to PF with no parameters
 * Version 1.1 VF sends this request to PF with u32 bitmap of its capabilities
 * PF responds with an indirect message containing
 * i40e_virtchnl_vf_resource and one or more
 * i40e_virtchnl_vsi_resource structures.
 */

struct i40e_virtchnl_vsi_resource {
	u16 vsi_id;
	u16 num_queue_pairs;
	enum i40e_vsi_type vsi_type;
	u16 qset_handle;
	u8 default_mac_addr[ETH_ALEN];
};
/* VF offload flags */
#define I40E_VIRTCHNL_VF_OFFLOAD_L2		0x00000001
#define I40E_VIRTCHNL_VF_OFFLOAD_IWARP		0x00000002
#define I40E_VIRTCHNL_VF_OFFLOAD_FCOE		0x00000004
#define I40E_VIRTCHNL_VF_OFFLOAD_RSS_AQ		0x00000008
#define I40E_VIRTCHNL_VF_OFFLOAD_RSS_REG	0x00000010
#define I40E_VIRTCHNL_VF_OFFLOAD_WB_ON_ITR	0x00000020
#define I40E_VIRTCHNL_VF_OFFLOAD_VLAN		0x00010000
#define I40E_VIRTCHNL_VF_OFFLOAD_RX_POLLING	0x00020000
#define I40E_VIRTCHNL_VF_OFFLOAD_RSS_PCTYPE_V2	0x00040000
#define I40E_VIRTCHNL_VF_OFFLOAD_RSS_PF		0X00080000
#define I40E_VIRTCHNL_VF_OFFLOAD_ENCAP_CSUM	0X00100000

struct i40e_virtchnl_vf_resource {
	u16 num_vsis;
	u16 num_queue_pairs;
	u16 max_vectors;
	u16 max_mtu;

	u32 vf_offload_flags;
	u32 rss_key_size;
	u32 rss_lut_size;

	struct i40e_virtchnl_vsi_resource vsi_res[1];
};

/* I40E_VIRTCHNL_OP_CONFIG_TX_QUEUE
 * VF sends this message to set up parameters for one TX queue.
 * External data buffer contains one instance of i40e_virtchnl_txq_info.
 * PF configures requested queue and returns a status code.
 */

/* Tx queue config info */
struct i40e_virtchnl_txq_info {
	u16 vsi_id;
	u16 queue_id;
	u16 ring_len;		/* number of descriptors, multiple of 8 */
	u16 headwb_enabled;
	u64 dma_ring_addr;
	u64 dma_headwb_addr;
};

/* I40E_VIRTCHNL_OP_CONFIG_RX_QUEUE
 * VF sends this message to set up parameters for one RX queue.
 * External data buffer contains one instance of i40e_virtchnl_rxq_info.
 * PF configures requested queue and returns a status code.
 */

/* Rx queue config info */
struct i40e_virtchnl_rxq_info {
	u16 vsi_id;
	u16 queue_id;
	u32 ring_len;		/* number of descriptors, multiple of 32 */
	u16 hdr_size;
	u16 splithdr_enabled;
	u32 databuffer_size;
	u32 max_pkt_size;
	u64 dma_ring_addr;
	enum i40e_hmc_obj_rx_hsplit_0 rx_split_pos;
};

/* I40E_VIRTCHNL_OP_CONFIG_VSI_QUEUES
 * VF sends this message to set parameters for all active TX and RX queues
 * associated with the specified VSI.
 * PF configures queues and returns status.
 * If the number of queues specified is greater than the number of queues
 * associated with the VSI, an error is returned and no queues are configured.
 */
struct i40e_virtchnl_queue_pair_info {
	/* NOTE: vsi_id and queue_id should be identical for both queues. */
	struct i40e_virtchnl_txq_info txq;
	struct i40e_virtchnl_rxq_info rxq;
};

struct i40e_virtchnl_vsi_queue_config_info {
	u16 vsi_id;
	u16 num_queue_pairs;
	struct i40e_virtchnl_queue_pair_info qpair[1];
};

/* I40E_VIRTCHNL_OP_CONFIG_IRQ_MAP
 * VF uses this message to map vectors to queues.
 * The rxq_map and txq_map fields are bitmaps used to indicate which queues
 * are to be associated with the specified vector.
 * The "other" causes are always mapped to vector 0.
 * PF configures interrupt mapping and returns status.
 */
struct i40e_virtchnl_vector_map {
	u16 vsi_id;
	u16 vector_id;
	u16 rxq_map;
	u16 txq_map;
	u16 rxitr_idx;
	u16 txitr_idx;
};

struct i40e_virtchnl_irq_map_info {
	u16 num_vectors;
	struct i40e_virtchnl_vector_map vecmap[1];
};

/* I40E_VIRTCHNL_OP_ENABLE_QUEUES
 * I40E_VIRTCHNL_OP_DISABLE_QUEUES
 * VF sends these message to enable or disable TX/RX queue pairs.
 * The queues fields are bitmaps indicating which queues to act upon.
 * (Currently, we only support 16 queues per VF, but we make the field
 * u32 to allow for expansion.)
 * PF performs requested action and returns status.
 */
struct i40e_virtchnl_queue_select {
	u16 vsi_id;
	u16 pad;
	u32 rx_queues;
	u32 tx_queues;
};

/* I40E_VIRTCHNL_OP_ADD_ETHER_ADDRESS
 * VF sends this message in order to add one or more unicast or multicast
 * address filters for the specified VSI.
 * PF adds the filters and returns status.
 */

/* I40E_VIRTCHNL_OP_DEL_ETHER_ADDRESS
 * VF sends this message in order to remove one or more unicast or multicast
 * filters for the specified VSI.
 * PF removes the filters and returns status.
 */

struct i40e_virtchnl_ether_addr {
	u8 addr[ETH_ALEN];
	u8 pad[2];
};

struct i40e_virtchnl_ether_addr_list {
	u16 vsi_id;
	u16 num_elements;
	struct i40e_virtchnl_ether_addr list[1];
};

/* I40E_VIRTCHNL_OP_ADD_VLAN
 * VF sends this message to add one or more VLAN tag filters for receives.
 * PF adds the filters and returns status.
 * If a port VLAN is configured by the PF, this operation will return an
 * error to the VF.
 */

/* I40E_VIRTCHNL_OP_DEL_VLAN
 * VF sends this message to remove one or more VLAN tag filters for receives.
 * PF removes the filters and returns status.
 * If a port VLAN is configured by the PF, this operation will return an
 * error to the VF.
 */

struct i40e_virtchnl_vlan_filter_list {
	u16 vsi_id;
	u16 num_elements;
	u16 vlan_id[1];
};

/* I40E_VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE
 * VF sends VSI id and flags.
 * PF returns status code in retval.
 * Note: we assume that broadcast accept mode is always enabled.
 */
struct i40e_virtchnl_promisc_info {
	u16 vsi_id;
	u16 flags;
};

#define I40E_FLAG_VF_UNICAST_PROMISC	0x00000001
#define I40E_FLAG_VF_MULTICAST_PROMISC	0x00000002

/* I40E_VIRTCHNL_OP_GET_STATS
 * VF sends this message to request stats for the selected VSI. VF uses
 * the i40e_virtchnl_queue_select struct to specify the VSI. The queue_id
 * field is ignored by the PF.
 *
 * PF replies with struct i40e_eth_stats in an external buffer.
 */

/* I40E_VIRTCHNL_OP_CONFIG_RSS_KEY
 * I40E_VIRTCHNL_OP_CONFIG_RSS_LUT
 * VF sends these messages to configure RSS. Only supported if both PF
 * and VF drivers set the I40E_VIRTCHNL_VF_OFFLOAD_RSS_PF bit during
 * configuration negotiation. If this is the case, then the RSS fields in
 * the VF resource struct are valid.
 * Both the key and LUT are initialized to 0 by the PF, meaning that
 * RSS is effectively disabled until set up by the VF.
 */
struct i40e_virtchnl_rss_key {
	u16 vsi_id;
	u16 key_len;
	u8 key[1];         /* RSS hash key, packed bytes */
};

struct i40e_virtchnl_rss_lut {
	u16 vsi_id;
	u16 lut_entries;
	u8 lut[1];        /* RSS lookup table*/
};

/* I40E_VIRTCHNL_OP_GET_RSS_HENA_CAPS
 * I40E_VIRTCHNL_OP_SET_RSS_HENA
 * VF sends these messages to get and set the hash filter enable bits for RSS.
 * By default, the PF sets these to all possible traffic types that the
 * hardware supports. The VF can query this value if it wants to change the
 * traffic types that are hashed by the hardware.
 * Traffic types are defined in the i40e_filter_pctype enum in i40e_type.h
 */
struct i40e_virtchnl_rss_hena {
	u64 hena;
};

/* I40E_VIRTCHNL_OP_EVENT
 * PF sends this message to inform the VF driver of events that may affect it.
 * No direct response is expected from the VF, though it may generate other
 * messages in response to this one.
 */
enum i40e_virtchnl_event_codes {
	I40E_VIRTCHNL_EVENT_UNKNOWN = 0,
	I40E_VIRTCHNL_EVENT_LINK_CHANGE,
	I40E_VIRTCHNL_EVENT_RESET_IMPENDING,
	I40E_VIRTCHNL_EVENT_PF_DRIVER_CLOSE,
};
#define I40E_PF_EVENT_SEVERITY_INFO		0
#define I40E_PF_EVENT_SEVERITY_CERTAIN_DOOM	255

struct i40e_virtchnl_pf_event {
	enum i40e_virtchnl_event_codes event;
	union {
		struct {
			enum i40e_aq_link_speed link_speed;
			bool link_status;
		} link_event;
	} event_data;

	int severity;
};

/* I40E_VIRTCHNL_OP_CONFIG_IWARP_IRQ_MAP
 * VF uses this message to request PF to map IWARP vectors to IWARP queues.
 * The request for this originates from the VF IWARP driver through
 * a client interface between VF LAN and VF IWARP driver.
 * A vector could have an AEQ and CEQ attached to it although
 * there is a single AEQ per VF IWARP instance in which case
 * most vectors will have an INVALID_IDX for aeq and valid idx for ceq.
 * There will never be a case where there will be multiple CEQs attached
 * to a single vector.
 * PF configures interrupt mapping and returns status.
 */

/* HW does not define a type value for AEQ; only for RX/TX and CEQ.
 * In order for us to keep the interface simple, SW will define a
 * unique type value for AEQ.
*/
#define I40E_QUEUE_TYPE_PE_AEQ  0x80
#define I40E_QUEUE_INVALID_IDX  0xFFFF

struct i40e_virtchnl_iwarp_qv_info {
	u32 v_idx; /* msix_vector */
	u16 ceq_idx;
	u16 aeq_idx;
	u8 itr_idx;
};

struct i40e_virtchnl_iwarp_qvlist_info {
	u32 num_vectors;
	struct i40e_virtchnl_iwarp_qv_info qv_info[1];
};

/* VF reset states - these are written into the RSTAT register:
 * I40E_VFGEN_RSTAT1 on the PF
 * I40E_VFGEN_RSTAT on the VF
 * When the PF initiates a reset, it writes 0
 * When the reset is complete, it writes 1
 * When the PF detects that the VF has recovered, it writes 2
 * VF checks this register periodically to determine if a reset has occurred,
 * then polls it to know when the reset is complete.
 * If either the PF or VF reads the register while the hardware
 * is in a reset state, it will return DEADBEEF, which, when masked
 * will result in 3.
 */
enum i40e_vfr_states {
	I40E_VFR_INPROGRESS = 0,
	I40E_VFR_COMPLETED,
	I40E_VFR_VFACTIVE,
	I40E_VFR_UNKNOWN,
};

#endif /* _I40E_VIRTCHNL_H_ */
