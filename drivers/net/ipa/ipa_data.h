/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */
#ifndef _IPA_DATA_H_
#define _IPA_DATA_H_

#include <linux/types.h>

#include "ipa_version.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"

/**
 * DOC: IPA/GSI Configuration Data
 *
 * Boot-time configuration data is used to define the configuration of the
 * IPA and GSI resources to use for a given platform.  This data is supplied
 * via the Device Tree match table, associated with a particular compatible
 * string.  The data defines information about resources, endpoints, and
 * channels.
 *
 * Resources are data structures used internally by the IPA hardware.  The
 * configuration data defines the number (or limits of the number) of various
 * types of these resources.
 *
 * Endpoint configuration data defines properties of both IPA endpoints and
 * GSI channels.  A channel is a GSI construct, and represents a single
 * communication path between the IPA and a particular execution environment
 * (EE), such as the AP or Modem.  Each EE has a set of channels associated
 * with it, and each channel has an ID unique for that EE.  For the most part
 * the only GSI channels of concern to this driver belong to the AP
 *
 * An endpoint is an IPA construct representing a single channel anywhere
 * in the system.  An IPA endpoint ID maps directly to an (EE, channel_id)
 * pair.  Generally, this driver is concerned with only endpoints associated
 * with the AP, however this will change when support for routing (etc.) is
 * added.  IPA endpoint and GSI channel configuration data are defined
 * together, establishing the endpoint_id->(EE, channel_id) mapping.
 *
 * Endpoint configuration data consists of three parts:  properties that
 * are common to IPA and GSI (EE ID, channel ID, endpoint ID, and direction);
 * properties associated with the GSI channel; and properties associated with
 * the IPA endpoint.
 */

/* The maximum value returned by ipa_resource_group_count() */
#define IPA_RESOURCE_GROUP_COUNT	4

/** enum ipa_resource_type_src - source resource types */
/**
 * struct gsi_channel_data - GSI channel configuration data
 * @tre_count:		number of TREs in the channel ring
 * @event_count:	number of slots in the associated event ring
 * @tlv_count:		number of entries in channel's TLV FIFO
 *
 * A GSI channel is a unidirectional means of transferring data to or
 * from (and through) the IPA.  A GSI channel has a ring buffer made
 * up of "transfer elements" (TREs) that specify individual data transfers
 * or IPA immediate commands.  TREs are filled by the AP, and control
 * is passed to IPA hardware by writing the last written element
 * into a doorbell register.
 *
 * When data transfer commands have completed the GSI generates an
 * event (a structure of data) and optionally signals the AP with
 * an interrupt.  Event structures are implemented by another ring
 * buffer, directed toward the AP from the IPA.
 *
 * The input to a GSI channel is a FIFO of type/length/value (TLV)
 * elements, and the size of this FIFO limits the number of TREs
 * that can be included in a single transaction.
 */
struct gsi_channel_data {
	u16 tre_count;
	u16 event_count;
	u8 tlv_count;
};

/**
 * struct ipa_endpoint_tx_data - configuration data for TX endpoints
 * @status_endpoint:	endpoint to which status elements are sent
 *
 * The @status_endpoint is only valid if the endpoint's @status_enable
 * flag is set.
 */
struct ipa_endpoint_tx_data {
	enum ipa_endpoint_name status_endpoint;
};

/**
 * struct ipa_endpoint_rx_data - configuration data for RX endpoints
 * @pad_align:	power-of-2 boundary to which packet payload is aligned
 * @aggr_close_eof: whether aggregation closes on end-of-frame
 *
 * With each packet it transfers, the IPA hardware can perform certain
 * transformations of its packet data.  One of these is adding pad bytes
 * to the end of the packet data so the result ends on a power-of-2 boundary.
 *
 * It is also able to aggregate multiple packets into a single receive buffer.
 * Aggregation is "open" while a buffer is being filled, and "closes" when
 * certain criteria are met.  One of those criteria is the sender indicating
 * a "frame" consisting of several transfers has ended.
 */
struct ipa_endpoint_rx_data {
	u32 pad_align;
	bool aggr_close_eof;
};

/**
 * struct ipa_endpoint_config_data - IPA endpoint hardware configuration
 * @checksum:		whether checksum offload is enabled
 * @qmap:		whether endpoint uses QMAP protocol
 * @aggregation:	whether endpoint supports aggregation
 * @status_enable:	whether endpoint uses status elements
 * @dma_mode:		whether endpoint operates in DMA mode
 * @dma_endpoint:	peer endpoint, if operating in DMA mode
 * @tx:			TX-specific endpoint information (see above)
 * @rx:			RX-specific endpoint information (see above)
 */
struct ipa_endpoint_config_data {
	bool checksum;
	bool qmap;
	bool aggregation;
	bool status_enable;
	bool dma_mode;
	enum ipa_endpoint_name dma_endpoint;
	union {
		struct ipa_endpoint_tx_data tx;
		struct ipa_endpoint_rx_data rx;
	};
};

/**
 * struct ipa_endpoint_data - IPA endpoint configuration data
 * @filter_support:	whether endpoint supports filtering
 * @seq_type:		hardware sequencer type used for endpoint
 * @config:		hardware configuration (see above)
 *
 * Not all endpoints support the IPA filtering capability.  A filter table
 * defines the filters to apply for those endpoints that support it.  The
 * AP is responsible for initializing this table, and it must include entries
 * for non-AP endpoints.  For this reason we define *all* endpoints used
 * in the system, and indicate whether they support filtering.
 *
 * The remaining endpoint configuration data applies only to AP endpoints.
 * The IPA hardware is implemented by sequencers, and the AP must program
 * the type(s) of these sequencers at initialization time.  The remaining
 * endpoint configuration data is defined above.
 */
struct ipa_endpoint_data {
	bool filter_support;
	/* The next two are specified only for AP endpoints */
	enum ipa_seq_type seq_type;
	struct ipa_endpoint_config_data config;
};

/**
 * struct ipa_gsi_endpoint_data - GSI channel/IPA endpoint data
 * ee:		GSI execution environment ID
 * channel_id:	GSI channel ID
 * endpoint_id:	IPA endpoint ID
 * toward_ipa:	direction of data transfer
 * gsi:		GSI channel configuration data (see above)
 * ipa:		IPA endpoint configuration data (see above)
 */
struct ipa_gsi_endpoint_data {
	u8 ee_id;		/* enum gsi_ee_id */
	u8 channel_id;
	u8 endpoint_id;
	bool toward_ipa;

	struct gsi_channel_data channel;
	struct ipa_endpoint_data endpoint;
};

/** enum ipa_resource_type_src - source resource types */
enum ipa_resource_type_src {
	IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS,
	IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS,
	IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF,
	IPA_RESOURCE_TYPE_SRC_HPS_DMARS,
	IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES,
};

/** enum ipa_resource_type_dst - destination resource types */
enum ipa_resource_type_dst {
	IPA_RESOURCE_TYPE_DST_DATA_SECTORS,
	IPA_RESOURCE_TYPE_DST_DPS_DMARS,
};

/**
 * struct ipa_resource_limits - minimum and maximum resource counts
 * @min:	minimum number of resources of a given type
 * @max:	maximum number of resources of a given type
 */
struct ipa_resource_limits {
	u32 min;
	u32 max;
};

/**
 * struct ipa_resource_src - source endpoint group resource usage
 * @type:	source group resource type
 * @limits:	array of limits to use for each resource group
 */
struct ipa_resource_src {
	enum ipa_resource_type_src type;
	struct ipa_resource_limits limits[IPA_RESOURCE_GROUP_COUNT];
};

/**
 * struct ipa_resource_dst - destination endpoint group resource usage
 * @type:	destination group resource type
 * @limits:	array of limits to use for each resource group
 */
struct ipa_resource_dst {
	enum ipa_resource_type_dst type;
	struct ipa_resource_limits limits[IPA_RESOURCE_GROUP_COUNT];
};

/**
 * struct ipa_resource_data - IPA resource configuration data
 * @resource_src_count:	number of entries in the resource_src array
 * @resource_src:	source endpoint group resources
 * @resource_dst_count:	number of entries in the resource_dst array
 * @resource_dst:	destination endpoint group resources
 *
 * In order to manage quality of service between endpoints, certain resources
 * required for operation are allocated to groups of endpoints.  Generally
 * this information is invisible to the AP, but the AP is responsible for
 * programming it at initialization time, so we specify it here.
 */
struct ipa_resource_data {
	u32 resource_src_count;
	const struct ipa_resource_src *resource_src;
	u32 resource_dst_count;
	const struct ipa_resource_dst *resource_dst;
};

/**
 * struct ipa_mem - description of IPA memory regions
 * @local_count:	number of regions defined in the local[] array
 * @local:		array of IPA-local memory region descriptors
 * @imem_addr:		physical address of IPA region within IMEM
 * @imem_size:		size in bytes of IPA IMEM region
 * @smem_id:		item identifier for IPA region within SMEM memory
 * @imem_size:		size in bytes of the IPA SMEM region
 */
struct ipa_mem_data {
	u32 local_count;
	const struct ipa_mem *local;
	u32 imem_addr;
	u32 imem_size;
	u32 smem_id;
	u32 smem_size;
};

/**
 * struct ipa_data - combined IPA/GSI configuration data
 * @version:		IPA hardware version
 * @endpoint_count:	number of entries in endpoint_data array
 * @endpoint_data:	IPA endpoint/GSI channel data
 * @resource_data:	IPA resource configuration data
 * @mem_count:		number of entries in mem_data array
 * @mem_data:		IPA-local shared memory region data
 */
struct ipa_data {
	enum ipa_version version;
	u32 endpoint_count;	/* # entries in endpoint_data[] */
	const struct ipa_gsi_endpoint_data *endpoint_data;
	const struct ipa_resource_data *resource_data;
	const struct ipa_mem_data *mem_data;
};

extern const struct ipa_data ipa_data_sdm845;
extern const struct ipa_data ipa_data_sc7180;

#endif /* _IPA_DATA_H_ */
