/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */
#ifndef _IPA_DATA_H_
#define _IPA_DATA_H_

#include <linux/types.h>

#include "ipa_endpoint.h"
#include "ipa_mem.h"
#include "ipa_version.h"

/**
 * DOC: IPA/GSI Configuration Data
 *
 * Boot-time configuration data is used to define the configuration of the
 * IPA and GSI resources to use for a given platform.  This data is supplied
 * via the Device Tree match table, associated with a particular compatible
 * string.  The data defines information about how resources, endpoints and
 * channels, memory, power and so on are allocated and used for the
 * platform.
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
 * the only GSI channels of concern to this driver belong to the AP.
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

/* The maximum possible number of source or destination resource groups */
#define IPA_RESOURCE_GROUP_MAX	8

/** enum ipa_qsb_master_id - array index for IPA QSB configuration data */
enum ipa_qsb_master_id {
	IPA_QSB_MASTER_DDR,
	IPA_QSB_MASTER_PCIE,
};

/**
 * struct ipa_qsb_data - Qualcomm System Bus configuration data
 * @max_writes:	Maximum outstanding write requests for this master
 * @max_reads:	Maximum outstanding read requests for this master
 * @max_reads_beats: Max outstanding read bytes in 8-byte "beats" (if non-zero)
 */
struct ipa_qsb_data {
	u8 max_writes;
	u8 max_reads;
	u8 max_reads_beats;		/* Not present for IPA v3.5.1 */
};

/**
 * struct gsi_channel_data - GSI channel configuration data
 * @tre_count:		number of TREs in the channel ring
 * @event_count:	number of slots in the associated event ring
 * @tlv_count:		number of entries in channel's TLV FIFO
 *
 * A GSI channel is a unidirectional means of transferring data to or
 * from (and through) the IPA.  A GSI channel has a ring buffer made
 * up of "transfer ring elements" (TREs) that specify individual data
 * transfers or IPA immediate commands.  TREs are filled by the AP,
 * and control is passed to IPA hardware by writing the last written
 * element into a doorbell register.
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
	u16 tre_count;			/* must be a power of 2 */
	u16 event_count;		/* must be a power of 2 */
	u8 tlv_count;
};

/**
 * struct ipa_endpoint_data - IPA endpoint configuration data
 * @filter_support:	whether endpoint supports filtering
 * @config:		hardware configuration
 *
 * Not all endpoints support the IPA filtering capability.  A filter table
 * defines the filters to apply for those endpoints that support it.  The
 * AP is responsible for initializing this table, and it must include entries
 * for non-AP endpoints.  For this reason we define *all* endpoints used
 * in the system, and indicate whether they support filtering.
 *
 * The remaining endpoint configuration data specifies default hardware
 * configuration values that apply only to AP endpoints.
 */
struct ipa_endpoint_data {
	bool filter_support;
	struct ipa_endpoint_config config;
};

/**
 * struct ipa_gsi_endpoint_data - GSI channel/IPA endpoint data
 * @ee_id:	GSI execution environment ID
 * @channel_id:	GSI channel ID
 * @endpoint_id: IPA endpoint ID
 * @toward_ipa:	direction of data transfer
 * @channel:	GSI channel configuration data (see above)
 * @endpoint:	IPA endpoint configuration data (see above)
 */
struct ipa_gsi_endpoint_data {
	u8 ee_id;		/* enum gsi_ee_id */
	u8 channel_id;
	u8 endpoint_id;
	bool toward_ipa;

	struct gsi_channel_data channel;
	struct ipa_endpoint_data endpoint;
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
 * struct ipa_resource - resource group source or destination resource usage
 * @limits:	array of resource limits, indexed by group
 */
struct ipa_resource {
	struct ipa_resource_limits limits[IPA_RESOURCE_GROUP_MAX];
};

/**
 * struct ipa_resource_data - IPA resource configuration data
 * @rsrc_group_src_count: number of source resource groups supported
 * @rsrc_group_dst_count: number of destination resource groups supported
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
	u32 rsrc_group_src_count;
	u32 rsrc_group_dst_count;
	u32 resource_src_count;
	const struct ipa_resource *resource_src;
	u32 resource_dst_count;
	const struct ipa_resource *resource_dst;
};

/**
 * struct ipa_mem_data - description of IPA memory regions
 * @local_count:	number of regions defined in the local[] array
 * @local:		array of IPA-local memory region descriptors
 * @imem_addr:		physical address of IPA region within IMEM
 * @imem_size:		size in bytes of IPA IMEM region
 * @smem_size:		size in bytes of the IPA SMEM region
 */
struct ipa_mem_data {
	u32 local_count;
	const struct ipa_mem *local;
	u32 imem_addr;
	u32 imem_size;
	u32 smem_size;
};

/**
 * struct ipa_interconnect_data - description of IPA interconnect bandwidths
 * @name:		Interconnect name (matches interconnect-name in DT)
 * @peak_bandwidth:	Peak interconnect bandwidth (in 1000 byte/sec units)
 * @average_bandwidth:	Average interconnect bandwidth (in 1000 byte/sec units)
 */
struct ipa_interconnect_data {
	const char *name;
	u32 peak_bandwidth;
	u32 average_bandwidth;
};

/**
 * struct ipa_power_data - description of IPA power configuration data
 * @core_clock_rate:	Core clock rate (Hz)
 * @interconnect_count:	Number of entries in the interconnect_data array
 * @interconnect_data:	IPA interconnect configuration data
 */
struct ipa_power_data {
	u32 core_clock_rate;
	u32 interconnect_count;		/* # entries in interconnect_data[] */
	const struct ipa_interconnect_data *interconnect_data;
};

/**
 * struct ipa_data - combined IPA/GSI configuration data
 * @version:		IPA hardware version
 * @backward_compat:	BCR register value (prior to IPA v4.5 only)
 * @qsb_count:		number of entries in the qsb_data array
 * @qsb_data:		Qualcomm System Bus configuration data
 * @modem_route_count:	number of modem entries in a routing table
 * @endpoint_count:	number of entries in the endpoint_data array
 * @endpoint_data:	IPA endpoint/GSI channel data
 * @resource_data:	IPA resource configuration data
 * @mem_data:		IPA memory region data
 * @power_data:		IPA power data
 */
struct ipa_data {
	enum ipa_version version;
	u32 backward_compat;
	u32 qsb_count;		/* number of entries in qsb_data[] */
	const struct ipa_qsb_data *qsb_data;
	u32 modem_route_count;
	u32 endpoint_count;	/* number of entries in endpoint_data[] */
	const struct ipa_gsi_endpoint_data *endpoint_data;
	const struct ipa_resource_data *resource_data;
	const struct ipa_mem_data *mem_data;
	const struct ipa_power_data *power_data;
};

extern const struct ipa_data ipa_data_v3_1;
extern const struct ipa_data ipa_data_v3_5_1;
extern const struct ipa_data ipa_data_v4_2;
extern const struct ipa_data ipa_data_v4_5;
extern const struct ipa_data ipa_data_v4_7;
extern const struct ipa_data ipa_data_v4_9;
extern const struct ipa_data ipa_data_v4_11;
extern const struct ipa_data ipa_data_v5_0;
extern const struct ipa_data ipa_data_v5_5;

#endif /* _IPA_DATA_H_ */
