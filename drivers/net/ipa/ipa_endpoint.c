// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2021 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/bitfield.h>
#include <linux/if_rmnet.h>
#include <linux/dma-direction.h>

#include "gsi.h"
#include "gsi_trans.h"
#include "ipa.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_cmd.h"
#include "ipa_mem.h"
#include "ipa_modem.h"
#include "ipa_table.h"
#include "ipa_gsi.h"
#include "ipa_power.h"

#define atomic_dec_not_zero(v)	atomic_add_unless((v), -1, 0)

/* Hardware is told about receive buffers once a "batch" has been queued */
#define IPA_REPLENISH_BATCH	16		/* Must be non-zero */

/* The amount of RX buffer space consumed by standard skb overhead */
#define IPA_RX_BUFFER_OVERHEAD	(PAGE_SIZE - SKB_MAX_ORDER(NET_SKB_PAD, 0))

/* Where to find the QMAP mux_id for a packet within modem-supplied metadata */
#define IPA_ENDPOINT_QMAP_METADATA_MASK		0x000000ff /* host byte order */

#define IPA_ENDPOINT_RESET_AGGR_RETRY_MAX	3
#define IPA_AGGR_TIME_LIMIT			500	/* microseconds */

/** enum ipa_status_opcode - status element opcode hardware values */
enum ipa_status_opcode {
	IPA_STATUS_OPCODE_PACKET		= 0x01,
	IPA_STATUS_OPCODE_DROPPED_PACKET	= 0x04,
	IPA_STATUS_OPCODE_SUSPENDED_PACKET	= 0x08,
	IPA_STATUS_OPCODE_PACKET_2ND_PASS	= 0x40,
};

/** enum ipa_status_exception - status element exception type */
enum ipa_status_exception {
	/* 0 means no exception */
	IPA_STATUS_EXCEPTION_DEAGGR		= 0x01,
};

/* Status element provided by hardware */
struct ipa_status {
	u8 opcode;		/* enum ipa_status_opcode */
	u8 exception;		/* enum ipa_status_exception */
	__le16 mask;
	__le16 pkt_len;
	u8 endp_src_idx;
	u8 endp_dst_idx;
	__le32 metadata;
	__le32 flags1;
	__le64 flags2;
	__le32 flags3;
	__le32 flags4;
};

/* Field masks for struct ipa_status structure fields */
#define IPA_STATUS_MASK_TAG_VALID_FMASK		GENMASK(4, 4)
#define IPA_STATUS_SRC_IDX_FMASK		GENMASK(4, 0)
#define IPA_STATUS_DST_IDX_FMASK		GENMASK(4, 0)
#define IPA_STATUS_FLAGS1_RT_RULE_ID_FMASK	GENMASK(31, 22)
#define IPA_STATUS_FLAGS2_TAG_FMASK		GENMASK_ULL(63, 16)

static u32 aggr_byte_limit_max(enum ipa_version version)
{
	if (version < IPA_VERSION_4_5)
		return field_max(aggr_byte_limit_fmask(true));

	return field_max(aggr_byte_limit_fmask(false));
}

static bool ipa_endpoint_data_valid_one(struct ipa *ipa, u32 count,
			    const struct ipa_gsi_endpoint_data *all_data,
			    const struct ipa_gsi_endpoint_data *data)
{
	const struct ipa_gsi_endpoint_data *other_data;
	struct device *dev = &ipa->pdev->dev;
	enum ipa_endpoint_name other_name;

	if (ipa_gsi_endpoint_data_empty(data))
		return true;

	if (!data->toward_ipa) {
		u32 buffer_size;
		u32 limit;

		if (data->endpoint.filter_support) {
			dev_err(dev, "filtering not supported for "
					"RX endpoint %u\n",
				data->endpoint_id);
			return false;
		}

		/* Nothing more to check for non-AP RX */
		if (data->ee_id != GSI_EE_AP)
			return true;

		buffer_size = data->endpoint.config.rx.buffer_size;
		/* The buffer size must hold an MTU plus overhead */
		limit = IPA_MTU + IPA_RX_BUFFER_OVERHEAD;
		if (buffer_size < limit) {
			dev_err(dev, "RX buffer size too small for RX endpoint %u (%u < %u)\n",
				data->endpoint_id, buffer_size, limit);
			return false;
		}

		/* For an endpoint supporting receive aggregation, the
		 * aggregation byte limit defines the point at which an
		 * aggregation window will close.  It is programmed into the
		 * IPA hardware as a number of KB.  We don't use "hard byte
		 * limit" aggregation, so we need to supply enough space in
		 * a receive buffer to hold a complete MTU plus normal skb
		 * overhead *after* that aggregation byte limit has been
		 * crossed.
		 *
		 * This check just ensures the receive buffer size doesn't
		 * exceed what's representable in the aggregation limit field.
		 */
		if (data->endpoint.config.aggregation) {
			limit += SZ_1K * aggr_byte_limit_max(ipa->version);
			if (buffer_size > limit) {
				dev_err(dev, "RX buffer size too large for aggregated RX endpoint %u (%u > %u)\n",
					data->endpoint_id, buffer_size, limit);

				return false;
			}
		}

		return true;	/* Nothing more to check for RX */
	}

	if (data->endpoint.config.status_enable) {
		other_name = data->endpoint.config.tx.status_endpoint;
		if (other_name >= count) {
			dev_err(dev, "status endpoint name %u out of range "
					"for endpoint %u\n",
				other_name, data->endpoint_id);
			return false;
		}

		/* Status endpoint must be defined... */
		other_data = &all_data[other_name];
		if (ipa_gsi_endpoint_data_empty(other_data)) {
			dev_err(dev, "DMA endpoint name %u undefined "
					"for endpoint %u\n",
				other_name, data->endpoint_id);
			return false;
		}

		/* ...and has to be an RX endpoint... */
		if (other_data->toward_ipa) {
			dev_err(dev,
				"status endpoint for endpoint %u not RX\n",
				data->endpoint_id);
			return false;
		}

		/* ...and if it's to be an AP endpoint... */
		if (other_data->ee_id == GSI_EE_AP) {
			/* ...make sure it has status enabled. */
			if (!other_data->endpoint.config.status_enable) {
				dev_err(dev,
					"status not enabled for endpoint %u\n",
					other_data->endpoint_id);
				return false;
			}
		}
	}

	if (data->endpoint.config.dma_mode) {
		other_name = data->endpoint.config.dma_endpoint;
		if (other_name >= count) {
			dev_err(dev, "DMA endpoint name %u out of range "
					"for endpoint %u\n",
				other_name, data->endpoint_id);
			return false;
		}

		other_data = &all_data[other_name];
		if (ipa_gsi_endpoint_data_empty(other_data)) {
			dev_err(dev, "DMA endpoint name %u undefined "
					"for endpoint %u\n",
				other_name, data->endpoint_id);
			return false;
		}
	}

	return true;
}

static bool ipa_endpoint_data_valid(struct ipa *ipa, u32 count,
				    const struct ipa_gsi_endpoint_data *data)
{
	const struct ipa_gsi_endpoint_data *dp = data;
	struct device *dev = &ipa->pdev->dev;
	enum ipa_endpoint_name name;

	if (count > IPA_ENDPOINT_COUNT) {
		dev_err(dev, "too many endpoints specified (%u > %u)\n",
			count, IPA_ENDPOINT_COUNT);
		return false;
	}

	/* Make sure needed endpoints have defined data */
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_COMMAND_TX])) {
		dev_err(dev, "command TX endpoint not defined\n");
		return false;
	}
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_LAN_RX])) {
		dev_err(dev, "LAN RX endpoint not defined\n");
		return false;
	}
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_MODEM_TX])) {
		dev_err(dev, "AP->modem TX endpoint not defined\n");
		return false;
	}
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_MODEM_RX])) {
		dev_err(dev, "AP<-modem RX endpoint not defined\n");
		return false;
	}

	for (name = 0; name < count; name++, dp++)
		if (!ipa_endpoint_data_valid_one(ipa, count, data, dp))
			return false;

	return true;
}

/* Allocate a transaction to use on a non-command endpoint */
static struct gsi_trans *ipa_endpoint_trans_alloc(struct ipa_endpoint *endpoint,
						  u32 tre_count)
{
	struct gsi *gsi = &endpoint->ipa->gsi;
	u32 channel_id = endpoint->channel_id;
	enum dma_data_direction direction;

	direction = endpoint->toward_ipa ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

	return gsi_channel_trans_alloc(gsi, channel_id, tre_count, direction);
}

/* suspend_delay represents suspend for RX, delay for TX endpoints.
 * Note that suspend is not supported starting with IPA v4.0, and
 * delay mode should not be used starting with IPA v4.2.
 */
static bool
ipa_endpoint_init_ctrl(struct ipa_endpoint *endpoint, bool suspend_delay)
{
	u32 offset = IPA_REG_ENDP_INIT_CTRL_N_OFFSET(endpoint->endpoint_id);
	struct ipa *ipa = endpoint->ipa;
	bool state;
	u32 mask;
	u32 val;

	if (endpoint->toward_ipa)
		WARN_ON(ipa->version >= IPA_VERSION_4_2);
	else
		WARN_ON(ipa->version >= IPA_VERSION_4_0);

	mask = endpoint->toward_ipa ? ENDP_DELAY_FMASK : ENDP_SUSPEND_FMASK;

	val = ioread32(ipa->reg_virt + offset);
	state = !!(val & mask);

	/* Don't bother if it's already in the requested state */
	if (suspend_delay != state) {
		val ^= mask;
		iowrite32(val, ipa->reg_virt + offset);
	}

	return state;
}

/* We don't care what the previous state was for delay mode */
static void
ipa_endpoint_program_delay(struct ipa_endpoint *endpoint, bool enable)
{
	/* Delay mode should not be used for IPA v4.2+ */
	WARN_ON(endpoint->ipa->version >= IPA_VERSION_4_2);
	WARN_ON(!endpoint->toward_ipa);

	(void)ipa_endpoint_init_ctrl(endpoint, enable);
}

static bool ipa_endpoint_aggr_active(struct ipa_endpoint *endpoint)
{
	u32 mask = BIT(endpoint->endpoint_id);
	struct ipa *ipa = endpoint->ipa;
	u32 offset;
	u32 val;

	WARN_ON(!(mask & ipa->available));

	offset = ipa_reg_state_aggr_active_offset(ipa->version);
	val = ioread32(ipa->reg_virt + offset);

	return !!(val & mask);
}

static void ipa_endpoint_force_close(struct ipa_endpoint *endpoint)
{
	u32 mask = BIT(endpoint->endpoint_id);
	struct ipa *ipa = endpoint->ipa;

	WARN_ON(!(mask & ipa->available));

	iowrite32(mask, ipa->reg_virt + IPA_REG_AGGR_FORCE_CLOSE_OFFSET);
}

/**
 * ipa_endpoint_suspend_aggr() - Emulate suspend interrupt
 * @endpoint:	Endpoint on which to emulate a suspend
 *
 *  Emulate suspend IPA interrupt to unsuspend an endpoint suspended
 *  with an open aggregation frame.  This is to work around a hardware
 *  issue in IPA version 3.5.1 where the suspend interrupt will not be
 *  generated when it should be.
 */
static void ipa_endpoint_suspend_aggr(struct ipa_endpoint *endpoint)
{
	struct ipa *ipa = endpoint->ipa;

	if (!endpoint->data->aggregation)
		return;

	/* Nothing to do if the endpoint doesn't have aggregation open */
	if (!ipa_endpoint_aggr_active(endpoint))
		return;

	/* Force close aggregation */
	ipa_endpoint_force_close(endpoint);

	ipa_interrupt_simulate_suspend(ipa->interrupt);
}

/* Returns previous suspend state (true means suspend was enabled) */
static bool
ipa_endpoint_program_suspend(struct ipa_endpoint *endpoint, bool enable)
{
	bool suspended;

	if (endpoint->ipa->version >= IPA_VERSION_4_0)
		return enable;	/* For IPA v4.0+, no change made */

	WARN_ON(endpoint->toward_ipa);

	suspended = ipa_endpoint_init_ctrl(endpoint, enable);

	/* A client suspended with an open aggregation frame will not
	 * generate a SUSPEND IPA interrupt.  If enabling suspend, have
	 * ipa_endpoint_suspend_aggr() handle this.
	 */
	if (enable && !suspended)
		ipa_endpoint_suspend_aggr(endpoint);

	return suspended;
}

/* Put all modem RX endpoints into suspend mode, and stop transmission
 * on all modem TX endpoints.  Prior to IPA v4.2, endpoint DELAY mode is
 * used for TX endpoints; starting with IPA v4.2 we use GSI channel flow
 * control instead.
 */
void ipa_endpoint_modem_pause_all(struct ipa *ipa, bool enable)
{
	u32 endpoint_id;

	for (endpoint_id = 0; endpoint_id < IPA_ENDPOINT_MAX; endpoint_id++) {
		struct ipa_endpoint *endpoint = &ipa->endpoint[endpoint_id];

		if (endpoint->ee_id != GSI_EE_MODEM)
			continue;

		if (!endpoint->toward_ipa)
			(void)ipa_endpoint_program_suspend(endpoint, enable);
		else if (ipa->version < IPA_VERSION_4_2)
			ipa_endpoint_program_delay(endpoint, enable);
		else
			gsi_modem_channel_flow_control(&ipa->gsi,
						       endpoint->channel_id,
						       enable);
	}
}

/* Reset all modem endpoints to use the default exception endpoint */
int ipa_endpoint_modem_exception_reset_all(struct ipa *ipa)
{
	u32 initialized = ipa->initialized;
	struct gsi_trans *trans;
	u32 count;

	/* We need one command per modem TX endpoint.  We can get an upper
	 * bound on that by assuming all initialized endpoints are modem->IPA.
	 * That won't happen, and we could be more precise, but this is fine
	 * for now.  End the transaction with commands to clear the pipeline.
	 */
	count = hweight32(initialized) + ipa_cmd_pipeline_clear_count();
	trans = ipa_cmd_trans_alloc(ipa, count);
	if (!trans) {
		dev_err(&ipa->pdev->dev,
			"no transaction to reset modem exception endpoints\n");
		return -EBUSY;
	}

	while (initialized) {
		u32 endpoint_id = __ffs(initialized);
		struct ipa_endpoint *endpoint;
		u32 offset;

		initialized ^= BIT(endpoint_id);

		/* We only reset modem TX endpoints */
		endpoint = &ipa->endpoint[endpoint_id];
		if (!(endpoint->ee_id == GSI_EE_MODEM && endpoint->toward_ipa))
			continue;

		offset = IPA_REG_ENDP_STATUS_N_OFFSET(endpoint_id);

		/* Value written is 0, and all bits are updated.  That
		 * means status is disabled on the endpoint, and as a
		 * result all other fields in the register are ignored.
		 */
		ipa_cmd_register_write_add(trans, offset, 0, ~0, false);
	}

	ipa_cmd_pipeline_clear_add(trans);

	/* XXX This should have a 1 second timeout */
	gsi_trans_commit_wait(trans);

	ipa_cmd_pipeline_clear_wait(ipa);

	return 0;
}

static void ipa_endpoint_init_cfg(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_CFG_N_OFFSET(endpoint->endpoint_id);
	enum ipa_cs_offload_en enabled;
	u32 val = 0;

	/* FRAG_OFFLOAD_EN is 0 */
	if (endpoint->data->checksum) {
		enum ipa_version version = endpoint->ipa->version;

		if (endpoint->toward_ipa) {
			u32 checksum_offset;

			/* Checksum header offset is in 4-byte units */
			checksum_offset = sizeof(struct rmnet_map_header);
			checksum_offset /= sizeof(u32);
			val |= u32_encode_bits(checksum_offset,
					       CS_METADATA_HDR_OFFSET_FMASK);

			enabled = version < IPA_VERSION_4_5
					? IPA_CS_OFFLOAD_UL
					: IPA_CS_OFFLOAD_INLINE;
		} else {
			enabled = version < IPA_VERSION_4_5
					? IPA_CS_OFFLOAD_DL
					: IPA_CS_OFFLOAD_INLINE;
		}
	} else {
		enabled = IPA_CS_OFFLOAD_NONE;
	}
	val |= u32_encode_bits(enabled, CS_OFFLOAD_EN_FMASK);
	/* CS_GEN_QMB_MASTER_SEL is 0 */

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

static void ipa_endpoint_init_nat(struct ipa_endpoint *endpoint)
{
	u32 offset;
	u32 val;

	if (!endpoint->toward_ipa)
		return;

	offset = IPA_REG_ENDP_INIT_NAT_N_OFFSET(endpoint->endpoint_id);
	val = u32_encode_bits(IPA_NAT_BYPASS, NAT_EN_FMASK);

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

static u32
ipa_qmap_header_size(enum ipa_version version, struct ipa_endpoint *endpoint)
{
	u32 header_size = sizeof(struct rmnet_map_header);

	/* Without checksum offload, we just have the MAP header */
	if (!endpoint->data->checksum)
		return header_size;

	if (version < IPA_VERSION_4_5) {
		/* Checksum header inserted for AP TX endpoints only */
		if (endpoint->toward_ipa)
			header_size += sizeof(struct rmnet_map_ul_csum_header);
	} else {
		/* Checksum header is used in both directions */
		header_size += sizeof(struct rmnet_map_v5_csum_header);
	}

	return header_size;
}

/**
 * ipa_endpoint_init_hdr() - Initialize HDR endpoint configuration register
 * @endpoint:	Endpoint pointer
 *
 * We program QMAP endpoints so each packet received is preceded by a QMAP
 * header structure.  The QMAP header contains a 1-byte mux_id and 2-byte
 * packet size field, and we have the IPA hardware populate both for each
 * received packet.  The header is configured (in the HDR_EXT register)
 * to use big endian format.
 *
 * The packet size is written into the QMAP header's pkt_len field.  That
 * location is defined here using the HDR_OFST_PKT_SIZE field.
 *
 * The mux_id comes from a 4-byte metadata value supplied with each packet
 * by the modem.  It is *not* a QMAP header, but it does contain the mux_id
 * value that we want, in its low-order byte.  A bitmask defined in the
 * endpoint's METADATA_MASK register defines which byte within the modem
 * metadata contains the mux_id.  And the OFST_METADATA field programmed
 * here indicates where the extracted byte should be placed within the QMAP
 * header.
 */
static void ipa_endpoint_init_hdr(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_HDR_N_OFFSET(endpoint->endpoint_id);
	struct ipa *ipa = endpoint->ipa;
	u32 val = 0;

	if (endpoint->data->qmap) {
		enum ipa_version version = ipa->version;
		size_t header_size;

		header_size = ipa_qmap_header_size(version, endpoint);
		val = ipa_header_size_encoded(version, header_size);

		/* Define how to fill fields in a received QMAP header */
		if (!endpoint->toward_ipa) {
			u32 offset;	/* Field offset within header */

			/* Where IPA will write the metadata value */
			offset = offsetof(struct rmnet_map_header, mux_id);
			val |= ipa_metadata_offset_encoded(version, offset);

			/* Where IPA will write the length */
			offset = offsetof(struct rmnet_map_header, pkt_len);
			/* Upper bits are stored in HDR_EXT with IPA v4.5 */
			if (version >= IPA_VERSION_4_5)
				offset &= field_mask(HDR_OFST_PKT_SIZE_FMASK);

			val |= HDR_OFST_PKT_SIZE_VALID_FMASK;
			val |= u32_encode_bits(offset, HDR_OFST_PKT_SIZE_FMASK);
		}
		/* For QMAP TX, metadata offset is 0 (modem assumes this) */
		val |= HDR_OFST_METADATA_VALID_FMASK;

		/* HDR_ADDITIONAL_CONST_LEN is 0; (RX only) */
		/* HDR_A5_MUX is 0 */
		/* HDR_LEN_INC_DEAGG_HDR is 0 */
		/* HDR_METADATA_REG_VALID is 0 (TX only, version < v4.5) */
	}

	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_endpoint_init_hdr_ext(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_HDR_EXT_N_OFFSET(endpoint->endpoint_id);
	u32 pad_align = endpoint->data->rx.pad_align;
	struct ipa *ipa = endpoint->ipa;
	u32 val = 0;

	val |= HDR_ENDIANNESS_FMASK;		/* big endian */

	/* A QMAP header contains a 6 bit pad field at offset 0.  The RMNet
	 * driver assumes this field is meaningful in packets it receives,
	 * and assumes the header's payload length includes that padding.
	 * The RMNet driver does *not* pad packets it sends, however, so
	 * the pad field (although 0) should be ignored.
	 */
	if (endpoint->data->qmap && !endpoint->toward_ipa) {
		val |= HDR_TOTAL_LEN_OR_PAD_VALID_FMASK;
		/* HDR_TOTAL_LEN_OR_PAD is 0 (pad, not total_len) */
		val |= HDR_PAYLOAD_LEN_INC_PADDING_FMASK;
		/* HDR_TOTAL_LEN_OR_PAD_OFFSET is 0 */
	}

	/* HDR_PAYLOAD_LEN_INC_PADDING is 0 */
	if (!endpoint->toward_ipa)
		val |= u32_encode_bits(pad_align, HDR_PAD_TO_ALIGNMENT_FMASK);

	/* IPA v4.5 adds some most-significant bits to a few fields,
	 * two of which are defined in the HDR (not HDR_EXT) register.
	 */
	if (ipa->version >= IPA_VERSION_4_5) {
		/* HDR_TOTAL_LEN_OR_PAD_OFFSET is 0, so MSB is 0 */
		if (endpoint->data->qmap && !endpoint->toward_ipa) {
			u32 offset;

			offset = offsetof(struct rmnet_map_header, pkt_len);
			offset >>= hweight32(HDR_OFST_PKT_SIZE_FMASK);
			val |= u32_encode_bits(offset,
					       HDR_OFST_PKT_SIZE_MSB_FMASK);
			/* HDR_ADDITIONAL_CONST_LEN is 0 so MSB is 0 */
		}
	}
	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_endpoint_init_hdr_metadata_mask(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	u32 val = 0;
	u32 offset;

	if (endpoint->toward_ipa)
		return;		/* Register not valid for TX endpoints */

	offset = IPA_REG_ENDP_INIT_HDR_METADATA_MASK_N_OFFSET(endpoint_id);

	/* Note that HDR_ENDIANNESS indicates big endian header fields */
	if (endpoint->data->qmap)
		val = (__force u32)cpu_to_be32(IPA_ENDPOINT_QMAP_METADATA_MASK);

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

static void ipa_endpoint_init_mode(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_MODE_N_OFFSET(endpoint->endpoint_id);
	u32 val;

	if (!endpoint->toward_ipa)
		return;		/* Register not valid for RX endpoints */

	if (endpoint->data->dma_mode) {
		enum ipa_endpoint_name name = endpoint->data->dma_endpoint;
		u32 dma_endpoint_id;

		dma_endpoint_id = endpoint->ipa->name_map[name]->endpoint_id;

		val = u32_encode_bits(IPA_DMA, MODE_FMASK);
		val |= u32_encode_bits(dma_endpoint_id, DEST_PIPE_INDEX_FMASK);
	} else {
		val = u32_encode_bits(IPA_BASIC, MODE_FMASK);
	}
	/* All other bits unspecified (and 0) */

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

/* Compute the aggregation size value to use for a given buffer size */
static u32 ipa_aggr_size_kb(u32 rx_buffer_size)
{
	/* We don't use "hard byte limit" aggregation, so we define the
	 * aggregation limit such that our buffer has enough space *after*
	 * that limit to receive a full MTU of data, plus overhead.
	 */
	rx_buffer_size -= IPA_MTU + IPA_RX_BUFFER_OVERHEAD;

	return rx_buffer_size / SZ_1K;
}

/* Encoded values for AGGR endpoint register fields */
static u32 aggr_byte_limit_encoded(enum ipa_version version, u32 limit)
{
	if (version < IPA_VERSION_4_5)
		return u32_encode_bits(limit, aggr_byte_limit_fmask(true));

	return u32_encode_bits(limit, aggr_byte_limit_fmask(false));
}

/* Encode the aggregation timer limit (microseconds) based on IPA version */
static u32 aggr_time_limit_encoded(enum ipa_version version, u32 limit)
{
	u32 gran_sel;
	u32 fmask;
	u32 val;

	if (version < IPA_VERSION_4_5) {
		/* We set aggregation granularity in ipa_hardware_config() */
		limit = DIV_ROUND_CLOSEST(limit, IPA_AGGR_GRANULARITY);

		return u32_encode_bits(limit, aggr_time_limit_fmask(true));
	}

	/* IPA v4.5 expresses the time limit using Qtime.  The AP has
	 * pulse generators 0 and 1 available, which were configured
	 * in ipa_qtime_config() to have granularity 100 usec and
	 * 1 msec, respectively.  Use pulse generator 0 if possible,
	 * otherwise fall back to pulse generator 1.
	 */
	fmask = aggr_time_limit_fmask(false);
	val = DIV_ROUND_CLOSEST(limit, 100);
	if (val > field_max(fmask)) {
		/* Have to use pulse generator 1 (millisecond granularity) */
		gran_sel = AGGR_GRAN_SEL_FMASK;
		val = DIV_ROUND_CLOSEST(limit, 1000);
	} else {
		/* We can use pulse generator 0 (100 usec granularity) */
		gran_sel = 0;
	}

	return gran_sel | u32_encode_bits(val, fmask);
}

static u32 aggr_sw_eof_active_encoded(enum ipa_version version, bool enabled)
{
	u32 val = enabled ? 1 : 0;

	if (version < IPA_VERSION_4_5)
		return u32_encode_bits(val, aggr_sw_eof_active_fmask(true));

	return u32_encode_bits(val, aggr_sw_eof_active_fmask(false));
}

static void ipa_endpoint_init_aggr(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_AGGR_N_OFFSET(endpoint->endpoint_id);
	enum ipa_version version = endpoint->ipa->version;
	u32 val = 0;

	if (endpoint->data->aggregation) {
		if (!endpoint->toward_ipa) {
			const struct ipa_endpoint_rx_data *rx_data;
			bool close_eof;
			u32 limit;

			rx_data = &endpoint->data->rx;
			val |= u32_encode_bits(IPA_ENABLE_AGGR, AGGR_EN_FMASK);
			val |= u32_encode_bits(IPA_GENERIC, AGGR_TYPE_FMASK);

			limit = ipa_aggr_size_kb(rx_data->buffer_size);
			val |= aggr_byte_limit_encoded(version, limit);

			limit = IPA_AGGR_TIME_LIMIT;
			val |= aggr_time_limit_encoded(version, limit);

			/* AGGR_PKT_LIMIT is 0 (unlimited) */

			close_eof = rx_data->aggr_close_eof;
			val |= aggr_sw_eof_active_encoded(version, close_eof);

			/* AGGR_HARD_BYTE_LIMIT_ENABLE is 0 */
		} else {
			val |= u32_encode_bits(IPA_ENABLE_DEAGGR,
					       AGGR_EN_FMASK);
			val |= u32_encode_bits(IPA_QCMAP, AGGR_TYPE_FMASK);
			/* other fields ignored */
		}
		/* AGGR_FORCE_CLOSE is 0 */
		/* AGGR_GRAN_SEL is 0 for IPA v4.5 */
	} else {
		val |= u32_encode_bits(IPA_BYPASS_AGGR, AGGR_EN_FMASK);
		/* other fields ignored */
	}

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

/* Return the Qtime-based head-of-line blocking timer value that
 * represents the given number of microseconds.  The result
 * includes both the timer value and the selected timer granularity.
 */
static u32 hol_block_timer_qtime_val(struct ipa *ipa, u32 microseconds)
{
	u32 gran_sel;
	u32 val;

	/* IPA v4.5 expresses time limits using Qtime.  The AP has
	 * pulse generators 0 and 1 available, which were configured
	 * in ipa_qtime_config() to have granularity 100 usec and
	 * 1 msec, respectively.  Use pulse generator 0 if possible,
	 * otherwise fall back to pulse generator 1.
	 */
	val = DIV_ROUND_CLOSEST(microseconds, 100);
	if (val > field_max(TIME_LIMIT_FMASK)) {
		/* Have to use pulse generator 1 (millisecond granularity) */
		gran_sel = GRAN_SEL_FMASK;
		val = DIV_ROUND_CLOSEST(microseconds, 1000);
	} else {
		/* We can use pulse generator 0 (100 usec granularity) */
		gran_sel = 0;
	}

	return gran_sel | u32_encode_bits(val, TIME_LIMIT_FMASK);
}

/* The head-of-line blocking timer is defined as a tick count.  For
 * IPA version 4.5 the tick count is based on the Qtimer, which is
 * derived from the 19.2 MHz SoC XO clock.  For older IPA versions
 * each tick represents 128 cycles of the IPA core clock.
 *
 * Return the encoded value that should be written to that register
 * that represents the timeout period provided.  For IPA v4.2 this
 * encodes a base and scale value, while for earlier versions the
 * value is a simple tick count.
 */
static u32 hol_block_timer_val(struct ipa *ipa, u32 microseconds)
{
	u32 width;
	u32 scale;
	u64 ticks;
	u64 rate;
	u32 high;
	u32 val;

	if (!microseconds)
		return 0;	/* Nothing to compute if timer period is 0 */

	if (ipa->version >= IPA_VERSION_4_5)
		return hol_block_timer_qtime_val(ipa, microseconds);

	/* Use 64 bit arithmetic to avoid overflow... */
	rate = ipa_core_clock_rate(ipa);
	ticks = DIV_ROUND_CLOSEST(microseconds * rate, 128 * USEC_PER_SEC);
	/* ...but we still need to fit into a 32-bit register */
	WARN_ON(ticks > U32_MAX);

	/* IPA v3.5.1 through v4.1 just record the tick count */
	if (ipa->version < IPA_VERSION_4_2)
		return (u32)ticks;

	/* For IPA v4.2, the tick count is represented by base and
	 * scale fields within the 32-bit timer register, where:
	 *     ticks = base << scale;
	 * The best precision is achieved when the base value is as
	 * large as possible.  Find the highest set bit in the tick
	 * count, and extract the number of bits in the base field
	 * such that high bit is included.
	 */
	high = fls(ticks);		/* 1..32 */
	width = HWEIGHT32(BASE_VALUE_FMASK);
	scale = high > width ? high - width : 0;
	if (scale) {
		/* If we're scaling, round up to get a closer result */
		ticks += 1 << (scale - 1);
		/* High bit was set, so rounding might have affected it */
		if (fls(ticks) != high)
			scale++;
	}

	val = u32_encode_bits(scale, SCALE_FMASK);
	val |= u32_encode_bits(ticks >> scale, BASE_VALUE_FMASK);

	return val;
}

/* If microseconds is 0, timeout is immediate */
static void ipa_endpoint_init_hol_block_timer(struct ipa_endpoint *endpoint,
					      u32 microseconds)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	u32 offset;
	u32 val;

	/* This should only be changed when HOL_BLOCK_EN is disabled */
	offset = IPA_REG_ENDP_INIT_HOL_BLOCK_TIMER_N_OFFSET(endpoint_id);
	val = hol_block_timer_val(ipa, microseconds);
	iowrite32(val, ipa->reg_virt + offset);
}

static void
ipa_endpoint_init_hol_block_en(struct ipa_endpoint *endpoint, bool enable)
{
	u32 endpoint_id = endpoint->endpoint_id;
	u32 offset;
	u32 val;

	val = enable ? HOL_BLOCK_EN_FMASK : 0;
	offset = IPA_REG_ENDP_INIT_HOL_BLOCK_EN_N_OFFSET(endpoint_id);
	iowrite32(val, endpoint->ipa->reg_virt + offset);
	/* When enabling, the register must be written twice for IPA v4.5+ */
	if (enable && endpoint->ipa->version >= IPA_VERSION_4_5)
		iowrite32(val, endpoint->ipa->reg_virt + offset);
}

/* Assumes HOL_BLOCK is in disabled state */
static void ipa_endpoint_init_hol_block_enable(struct ipa_endpoint *endpoint,
					       u32 microseconds)
{
	ipa_endpoint_init_hol_block_timer(endpoint, microseconds);
	ipa_endpoint_init_hol_block_en(endpoint, true);
}

static void ipa_endpoint_init_hol_block_disable(struct ipa_endpoint *endpoint)
{
	ipa_endpoint_init_hol_block_en(endpoint, false);
}

void ipa_endpoint_modem_hol_block_clear_all(struct ipa *ipa)
{
	u32 i;

	for (i = 0; i < IPA_ENDPOINT_MAX; i++) {
		struct ipa_endpoint *endpoint = &ipa->endpoint[i];

		if (endpoint->toward_ipa || endpoint->ee_id != GSI_EE_MODEM)
			continue;

		ipa_endpoint_init_hol_block_disable(endpoint);
		ipa_endpoint_init_hol_block_enable(endpoint, 0);
	}
}

static void ipa_endpoint_init_deaggr(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_DEAGGR_N_OFFSET(endpoint->endpoint_id);
	u32 val = 0;

	if (!endpoint->toward_ipa)
		return;		/* Register not valid for RX endpoints */

	/* DEAGGR_HDR_LEN is 0 */
	/* PACKET_OFFSET_VALID is 0 */
	/* PACKET_OFFSET_LOCATION is ignored (not valid) */
	/* MAX_PACKET_LEN is 0 (not enforced) */

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

static void ipa_endpoint_init_rsrc_grp(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_RSRC_GRP_N_OFFSET(endpoint->endpoint_id);
	struct ipa *ipa = endpoint->ipa;
	u32 val;

	val = rsrc_grp_encoded(ipa->version, endpoint->data->resource_group);
	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_endpoint_init_seq(struct ipa_endpoint *endpoint)
{
	u32 offset = IPA_REG_ENDP_INIT_SEQ_N_OFFSET(endpoint->endpoint_id);
	u32 val = 0;

	if (!endpoint->toward_ipa)
		return;		/* Register not valid for RX endpoints */

	/* Low-order byte configures primary packet processing */
	val |= u32_encode_bits(endpoint->data->tx.seq_type, SEQ_TYPE_FMASK);

	/* Second byte configures replicated packet processing */
	val |= u32_encode_bits(endpoint->data->tx.seq_rep_type,
			       SEQ_REP_TYPE_FMASK);

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

/**
 * ipa_endpoint_skb_tx() - Transmit a socket buffer
 * @endpoint:	Endpoint pointer
 * @skb:	Socket buffer to send
 *
 * Returns:	0 if successful, or a negative error code
 */
int ipa_endpoint_skb_tx(struct ipa_endpoint *endpoint, struct sk_buff *skb)
{
	struct gsi_trans *trans;
	u32 nr_frags;
	int ret;

	/* Make sure source endpoint's TLV FIFO has enough entries to
	 * hold the linear portion of the skb and all its fragments.
	 * If not, see if we can linearize it before giving up.
	 */
	nr_frags = skb_shinfo(skb)->nr_frags;
	if (1 + nr_frags > endpoint->trans_tre_max) {
		if (skb_linearize(skb))
			return -E2BIG;
		nr_frags = 0;
	}

	trans = ipa_endpoint_trans_alloc(endpoint, 1 + nr_frags);
	if (!trans)
		return -EBUSY;

	ret = gsi_trans_skb_add(trans, skb);
	if (ret)
		goto err_trans_free;
	trans->data = skb;	/* transaction owns skb now */

	gsi_trans_commit(trans, !netdev_xmit_more());

	return 0;

err_trans_free:
	gsi_trans_free(trans);

	return -ENOMEM;
}

static void ipa_endpoint_status(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	u32 val = 0;
	u32 offset;

	offset = IPA_REG_ENDP_STATUS_N_OFFSET(endpoint_id);

	if (endpoint->data->status_enable) {
		val |= STATUS_EN_FMASK;
		if (endpoint->toward_ipa) {
			enum ipa_endpoint_name name;
			u32 status_endpoint_id;

			name = endpoint->data->tx.status_endpoint;
			status_endpoint_id = ipa->name_map[name]->endpoint_id;

			val |= u32_encode_bits(status_endpoint_id,
					       STATUS_ENDP_FMASK);
		}
		/* STATUS_LOCATION is 0, meaning status element precedes
		 * packet (not present for IPA v4.5)
		 */
		/* STATUS_PKT_SUPPRESS_FMASK is 0 (not present for v3.5.1) */
	}

	iowrite32(val, ipa->reg_virt + offset);
}

static int ipa_endpoint_replenish_one(struct ipa_endpoint *endpoint,
				      struct gsi_trans *trans)
{
	struct page *page;
	u32 buffer_size;
	u32 offset;
	u32 len;
	int ret;

	buffer_size = endpoint->data->rx.buffer_size;
	page = dev_alloc_pages(get_order(buffer_size));
	if (!page)
		return -ENOMEM;

	/* Offset the buffer to make space for skb headroom */
	offset = NET_SKB_PAD;
	len = buffer_size - offset;

	ret = gsi_trans_page_add(trans, page, len, offset);
	if (ret)
		__free_pages(page, get_order(buffer_size));
	else
		trans->data = page;	/* transaction owns page now */

	return ret;
}

/**
 * ipa_endpoint_replenish() - Replenish endpoint receive buffers
 * @endpoint:	Endpoint to be replenished
 *
 * The IPA hardware can hold a fixed number of receive buffers for an RX
 * endpoint, based on the number of entries in the underlying channel ring
 * buffer.  If an endpoint's "backlog" is non-zero, it indicates how many
 * more receive buffers can be supplied to the hardware.  Replenishing for
 * an endpoint can be disabled, in which case buffers are not queued to
 * the hardware.
 */
static void ipa_endpoint_replenish(struct ipa_endpoint *endpoint)
{
	struct gsi_trans *trans;

	if (!test_bit(IPA_REPLENISH_ENABLED, endpoint->replenish_flags))
		return;

	/* Skip it if it's already active */
	if (test_and_set_bit(IPA_REPLENISH_ACTIVE, endpoint->replenish_flags))
		return;

	while ((trans = ipa_endpoint_trans_alloc(endpoint, 1))) {
		bool doorbell;

		if (ipa_endpoint_replenish_one(endpoint, trans))
			goto try_again_later;


		/* Ring the doorbell if we've got a full batch */
		doorbell = !(++endpoint->replenish_count % IPA_REPLENISH_BATCH);
		gsi_trans_commit(trans, doorbell);
	}

	clear_bit(IPA_REPLENISH_ACTIVE, endpoint->replenish_flags);

	return;

try_again_later:
	gsi_trans_free(trans);
	clear_bit(IPA_REPLENISH_ACTIVE, endpoint->replenish_flags);

	/* Whenever a receive buffer transaction completes we'll try to
	 * replenish again.  It's unlikely, but if we fail to supply even
	 * one buffer, nothing will trigger another replenish attempt.
	 * If the hardware has no receive buffers queued, schedule work to
	 * try replenishing again.
	 */
	if (gsi_channel_trans_idle(&endpoint->ipa->gsi, endpoint->channel_id))
		schedule_delayed_work(&endpoint->replenish_work,
				      msecs_to_jiffies(1));
}

static void ipa_endpoint_replenish_enable(struct ipa_endpoint *endpoint)
{
	set_bit(IPA_REPLENISH_ENABLED, endpoint->replenish_flags);

	/* Start replenishing if hardware currently has no buffers */
	if (gsi_channel_trans_idle(&endpoint->ipa->gsi, endpoint->channel_id))
		ipa_endpoint_replenish(endpoint);
}

static void ipa_endpoint_replenish_disable(struct ipa_endpoint *endpoint)
{
	clear_bit(IPA_REPLENISH_ENABLED, endpoint->replenish_flags);
}

static void ipa_endpoint_replenish_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ipa_endpoint *endpoint;

	endpoint = container_of(dwork, struct ipa_endpoint, replenish_work);

	ipa_endpoint_replenish(endpoint);
}

static void ipa_endpoint_skb_copy(struct ipa_endpoint *endpoint,
				  void *data, u32 len, u32 extra)
{
	struct sk_buff *skb;

	if (!endpoint->netdev)
		return;

	skb = __dev_alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return;

	/* Copy the data into the socket buffer and receive it */
	skb_put(skb, len);
	memcpy(skb->data, data, len);
	skb->truesize += extra;

	ipa_modem_skb_rx(endpoint->netdev, skb);
}

static bool ipa_endpoint_skb_build(struct ipa_endpoint *endpoint,
				   struct page *page, u32 len)
{
	u32 buffer_size = endpoint->data->rx.buffer_size;
	struct sk_buff *skb;

	/* Nothing to do if there's no netdev */
	if (!endpoint->netdev)
		return false;

	WARN_ON(len > SKB_WITH_OVERHEAD(buffer_size - NET_SKB_PAD));

	skb = build_skb(page_address(page), buffer_size);
	if (skb) {
		/* Reserve the headroom and account for the data */
		skb_reserve(skb, NET_SKB_PAD);
		skb_put(skb, len);
	}

	/* Receive the buffer (or record drop if unable to build it) */
	ipa_modem_skb_rx(endpoint->netdev, skb);

	return skb != NULL;
}

/* The format of a packet status element is the same for several status
 * types (opcodes).  Other types aren't currently supported.
 */
static bool ipa_status_format_packet(enum ipa_status_opcode opcode)
{
	switch (opcode) {
	case IPA_STATUS_OPCODE_PACKET:
	case IPA_STATUS_OPCODE_DROPPED_PACKET:
	case IPA_STATUS_OPCODE_SUSPENDED_PACKET:
	case IPA_STATUS_OPCODE_PACKET_2ND_PASS:
		return true;
	default:
		return false;
	}
}

static bool ipa_endpoint_status_skip(struct ipa_endpoint *endpoint,
				     const struct ipa_status *status)
{
	u32 endpoint_id;

	if (!ipa_status_format_packet(status->opcode))
		return true;
	if (!status->pkt_len)
		return true;
	endpoint_id = u8_get_bits(status->endp_dst_idx,
				  IPA_STATUS_DST_IDX_FMASK);
	if (endpoint_id != endpoint->endpoint_id)
		return true;

	return false;	/* Don't skip this packet, process it */
}

static bool ipa_endpoint_status_tag(struct ipa_endpoint *endpoint,
				    const struct ipa_status *status)
{
	struct ipa_endpoint *command_endpoint;
	struct ipa *ipa = endpoint->ipa;
	u32 endpoint_id;

	if (!le16_get_bits(status->mask, IPA_STATUS_MASK_TAG_VALID_FMASK))
		return false;	/* No valid tag */

	/* The status contains a valid tag.  We know the packet was sent to
	 * this endpoint (already verified by ipa_endpoint_status_skip()).
	 * If the packet came from the AP->command TX endpoint we know
	 * this packet was sent as part of the pipeline clear process.
	 */
	endpoint_id = u8_get_bits(status->endp_src_idx,
				  IPA_STATUS_SRC_IDX_FMASK);
	command_endpoint = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX];
	if (endpoint_id == command_endpoint->endpoint_id) {
		complete(&ipa->completion);
	} else {
		dev_err(&ipa->pdev->dev,
			"unexpected tagged packet from endpoint %u\n",
			endpoint_id);
	}

	return true;
}

/* Return whether the status indicates the packet should be dropped */
static bool ipa_endpoint_status_drop(struct ipa_endpoint *endpoint,
				     const struct ipa_status *status)
{
	u32 val;

	/* If the status indicates a tagged transfer, we'll drop the packet */
	if (ipa_endpoint_status_tag(endpoint, status))
		return true;

	/* Deaggregation exceptions we drop; all other types we consume */
	if (status->exception)
		return status->exception == IPA_STATUS_EXCEPTION_DEAGGR;

	/* Drop the packet if it fails to match a routing rule; otherwise no */
	val = le32_get_bits(status->flags1, IPA_STATUS_FLAGS1_RT_RULE_ID_FMASK);

	return val == field_max(IPA_STATUS_FLAGS1_RT_RULE_ID_FMASK);
}

static void ipa_endpoint_status_parse(struct ipa_endpoint *endpoint,
				      struct page *page, u32 total_len)
{
	u32 buffer_size = endpoint->data->rx.buffer_size;
	void *data = page_address(page) + NET_SKB_PAD;
	u32 unused = buffer_size - total_len;
	u32 resid = total_len;

	while (resid) {
		const struct ipa_status *status = data;
		u32 align;
		u32 len;

		if (resid < sizeof(*status)) {
			dev_err(&endpoint->ipa->pdev->dev,
				"short message (%u bytes < %zu byte status)\n",
				resid, sizeof(*status));
			break;
		}

		/* Skip over status packets that lack packet data */
		if (ipa_endpoint_status_skip(endpoint, status)) {
			data += sizeof(*status);
			resid -= sizeof(*status);
			continue;
		}

		/* Compute the amount of buffer space consumed by the packet,
		 * including the status element.  If the hardware is configured
		 * to pad packet data to an aligned boundary, account for that.
		 * And if checksum offload is enabled a trailer containing
		 * computed checksum information will be appended.
		 */
		align = endpoint->data->rx.pad_align ? : 1;
		len = le16_to_cpu(status->pkt_len);
		len = sizeof(*status) + ALIGN(len, align);
		if (endpoint->data->checksum)
			len += sizeof(struct rmnet_map_dl_csum_trailer);

		if (!ipa_endpoint_status_drop(endpoint, status)) {
			void *data2;
			u32 extra;
			u32 len2;

			/* Client receives only packet data (no status) */
			data2 = data + sizeof(*status);
			len2 = le16_to_cpu(status->pkt_len);

			/* Have the true size reflect the extra unused space in
			 * the original receive buffer.  Distribute the "cost"
			 * proportionately across all aggregated packets in the
			 * buffer.
			 */
			extra = DIV_ROUND_CLOSEST(unused * len, total_len);
			ipa_endpoint_skb_copy(endpoint, data2, len2, extra);
		}

		/* Consume status and the full packet it describes */
		data += len;
		resid -= len;
	}
}

/* Complete a TX transaction, command or from ipa_endpoint_skb_tx() */
static void ipa_endpoint_tx_complete(struct ipa_endpoint *endpoint,
				     struct gsi_trans *trans)
{
}

/* Complete transaction initiated in ipa_endpoint_replenish_one() */
static void ipa_endpoint_rx_complete(struct ipa_endpoint *endpoint,
				     struct gsi_trans *trans)
{
	struct page *page;

	if (trans->cancelled)
		goto done;

	/* Parse or build a socket buffer using the actual received length */
	page = trans->data;
	if (endpoint->data->status_enable)
		ipa_endpoint_status_parse(endpoint, page, trans->len);
	else if (ipa_endpoint_skb_build(endpoint, page, trans->len))
		trans->data = NULL;	/* Pages have been consumed */
done:
	ipa_endpoint_replenish(endpoint);
}

void ipa_endpoint_trans_complete(struct ipa_endpoint *endpoint,
				 struct gsi_trans *trans)
{
	if (endpoint->toward_ipa)
		ipa_endpoint_tx_complete(endpoint, trans);
	else
		ipa_endpoint_rx_complete(endpoint, trans);
}

void ipa_endpoint_trans_release(struct ipa_endpoint *endpoint,
				struct gsi_trans *trans)
{
	if (endpoint->toward_ipa) {
		struct ipa *ipa = endpoint->ipa;

		/* Nothing to do for command transactions */
		if (endpoint != ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX]) {
			struct sk_buff *skb = trans->data;

			if (skb)
				dev_kfree_skb_any(skb);
		}
	} else {
		struct page *page = trans->data;

		if (page) {
			u32 buffer_size = endpoint->data->rx.buffer_size;

			__free_pages(page, get_order(buffer_size));
		}
	}
}

void ipa_endpoint_default_route_set(struct ipa *ipa, u32 endpoint_id)
{
	u32 val;

	/* ROUTE_DIS is 0 */
	val = u32_encode_bits(endpoint_id, ROUTE_DEF_PIPE_FMASK);
	val |= ROUTE_DEF_HDR_TABLE_FMASK;
	val |= u32_encode_bits(0, ROUTE_DEF_HDR_OFST_FMASK);
	val |= u32_encode_bits(endpoint_id, ROUTE_FRAG_DEF_PIPE_FMASK);
	val |= ROUTE_DEF_RETAIN_HDR_FMASK;

	iowrite32(val, ipa->reg_virt + IPA_REG_ROUTE_OFFSET);
}

void ipa_endpoint_default_route_clear(struct ipa *ipa)
{
	ipa_endpoint_default_route_set(ipa, 0);
}

/**
 * ipa_endpoint_reset_rx_aggr() - Reset RX endpoint with aggregation active
 * @endpoint:	Endpoint to be reset
 *
 * If aggregation is active on an RX endpoint when a reset is performed
 * on its underlying GSI channel, a special sequence of actions must be
 * taken to ensure the IPA pipeline is properly cleared.
 *
 * Return:	0 if successful, or a negative error code
 */
static int ipa_endpoint_reset_rx_aggr(struct ipa_endpoint *endpoint)
{
	struct device *dev = &endpoint->ipa->pdev->dev;
	struct ipa *ipa = endpoint->ipa;
	struct gsi *gsi = &ipa->gsi;
	bool suspended = false;
	dma_addr_t addr;
	u32 retries;
	u32 len = 1;
	void *virt;
	int ret;

	virt = kzalloc(len, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	addr = dma_map_single(dev, virt, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, addr)) {
		ret = -ENOMEM;
		goto out_kfree;
	}

	/* Force close aggregation before issuing the reset */
	ipa_endpoint_force_close(endpoint);

	/* Reset and reconfigure the channel with the doorbell engine
	 * disabled.  Then poll until we know aggregation is no longer
	 * active.  We'll re-enable the doorbell (if appropriate) when
	 * we reset again below.
	 */
	gsi_channel_reset(gsi, endpoint->channel_id, false);

	/* Make sure the channel isn't suspended */
	suspended = ipa_endpoint_program_suspend(endpoint, false);

	/* Start channel and do a 1 byte read */
	ret = gsi_channel_start(gsi, endpoint->channel_id);
	if (ret)
		goto out_suspend_again;

	ret = gsi_trans_read_byte(gsi, endpoint->channel_id, addr);
	if (ret)
		goto err_endpoint_stop;

	/* Wait for aggregation to be closed on the channel */
	retries = IPA_ENDPOINT_RESET_AGGR_RETRY_MAX;
	do {
		if (!ipa_endpoint_aggr_active(endpoint))
			break;
		usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);
	} while (retries--);

	/* Check one last time */
	if (ipa_endpoint_aggr_active(endpoint))
		dev_err(dev, "endpoint %u still active during reset\n",
			endpoint->endpoint_id);

	gsi_trans_read_byte_done(gsi, endpoint->channel_id);

	ret = gsi_channel_stop(gsi, endpoint->channel_id);
	if (ret)
		goto out_suspend_again;

	/* Finally, reset and reconfigure the channel again (re-enabling
	 * the doorbell engine if appropriate).  Sleep for 1 millisecond to
	 * complete the channel reset sequence.  Finish by suspending the
	 * channel again (if necessary).
	 */
	gsi_channel_reset(gsi, endpoint->channel_id, true);

	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	goto out_suspend_again;

err_endpoint_stop:
	(void)gsi_channel_stop(gsi, endpoint->channel_id);
out_suspend_again:
	if (suspended)
		(void)ipa_endpoint_program_suspend(endpoint, true);
	dma_unmap_single(dev, addr, len, DMA_FROM_DEVICE);
out_kfree:
	kfree(virt);

	return ret;
}

static void ipa_endpoint_reset(struct ipa_endpoint *endpoint)
{
	u32 channel_id = endpoint->channel_id;
	struct ipa *ipa = endpoint->ipa;
	bool special;
	int ret = 0;

	/* On IPA v3.5.1, if an RX endpoint is reset while aggregation
	 * is active, we need to handle things specially to recover.
	 * All other cases just need to reset the underlying GSI channel.
	 */
	special = ipa->version < IPA_VERSION_4_0 && !endpoint->toward_ipa &&
			endpoint->data->aggregation;
	if (special && ipa_endpoint_aggr_active(endpoint))
		ret = ipa_endpoint_reset_rx_aggr(endpoint);
	else
		gsi_channel_reset(&ipa->gsi, channel_id, true);

	if (ret)
		dev_err(&ipa->pdev->dev,
			"error %d resetting channel %u for endpoint %u\n",
			ret, endpoint->channel_id, endpoint->endpoint_id);
}

static void ipa_endpoint_program(struct ipa_endpoint *endpoint)
{
	if (endpoint->toward_ipa) {
		/* Newer versions of IPA use GSI channel flow control
		 * instead of endpoint DELAY mode to prevent sending data.
		 * Flow control is disabled for newly-allocated channels,
		 * and we can assume flow control is not (ever) enabled
		 * for AP TX channels.
		 */
		if (endpoint->ipa->version < IPA_VERSION_4_2)
			ipa_endpoint_program_delay(endpoint, false);
	} else {
		/* Ensure suspend mode is off on all AP RX endpoints */
		(void)ipa_endpoint_program_suspend(endpoint, false);
	}
	ipa_endpoint_init_cfg(endpoint);
	ipa_endpoint_init_nat(endpoint);
	ipa_endpoint_init_hdr(endpoint);
	ipa_endpoint_init_hdr_ext(endpoint);
	ipa_endpoint_init_hdr_metadata_mask(endpoint);
	ipa_endpoint_init_mode(endpoint);
	ipa_endpoint_init_aggr(endpoint);
	if (!endpoint->toward_ipa)
		ipa_endpoint_init_hol_block_disable(endpoint);
	ipa_endpoint_init_deaggr(endpoint);
	ipa_endpoint_init_rsrc_grp(endpoint);
	ipa_endpoint_init_seq(endpoint);
	ipa_endpoint_status(endpoint);
}

int ipa_endpoint_enable_one(struct ipa_endpoint *endpoint)
{
	struct ipa *ipa = endpoint->ipa;
	struct gsi *gsi = &ipa->gsi;
	int ret;

	ret = gsi_channel_start(gsi, endpoint->channel_id);
	if (ret) {
		dev_err(&ipa->pdev->dev,
			"error %d starting %cX channel %u for endpoint %u\n",
			ret, endpoint->toward_ipa ? 'T' : 'R',
			endpoint->channel_id, endpoint->endpoint_id);
		return ret;
	}

	if (!endpoint->toward_ipa) {
		ipa_interrupt_suspend_enable(ipa->interrupt,
					     endpoint->endpoint_id);
		ipa_endpoint_replenish_enable(endpoint);
	}

	ipa->enabled |= BIT(endpoint->endpoint_id);

	return 0;
}

void ipa_endpoint_disable_one(struct ipa_endpoint *endpoint)
{
	u32 mask = BIT(endpoint->endpoint_id);
	struct ipa *ipa = endpoint->ipa;
	struct gsi *gsi = &ipa->gsi;
	int ret;

	if (!(ipa->enabled & mask))
		return;

	ipa->enabled ^= mask;

	if (!endpoint->toward_ipa) {
		ipa_endpoint_replenish_disable(endpoint);
		ipa_interrupt_suspend_disable(ipa->interrupt,
					      endpoint->endpoint_id);
	}

	/* Note that if stop fails, the channel's state is not well-defined */
	ret = gsi_channel_stop(gsi, endpoint->channel_id);
	if (ret)
		dev_err(&ipa->pdev->dev,
			"error %d attempting to stop endpoint %u\n", ret,
			endpoint->endpoint_id);
}

void ipa_endpoint_suspend_one(struct ipa_endpoint *endpoint)
{
	struct device *dev = &endpoint->ipa->pdev->dev;
	struct gsi *gsi = &endpoint->ipa->gsi;
	int ret;

	if (!(endpoint->ipa->enabled & BIT(endpoint->endpoint_id)))
		return;

	if (!endpoint->toward_ipa) {
		ipa_endpoint_replenish_disable(endpoint);
		(void)ipa_endpoint_program_suspend(endpoint, true);
	}

	ret = gsi_channel_suspend(gsi, endpoint->channel_id);
	if (ret)
		dev_err(dev, "error %d suspending channel %u\n", ret,
			endpoint->channel_id);
}

void ipa_endpoint_resume_one(struct ipa_endpoint *endpoint)
{
	struct device *dev = &endpoint->ipa->pdev->dev;
	struct gsi *gsi = &endpoint->ipa->gsi;
	int ret;

	if (!(endpoint->ipa->enabled & BIT(endpoint->endpoint_id)))
		return;

	if (!endpoint->toward_ipa)
		(void)ipa_endpoint_program_suspend(endpoint, false);

	ret = gsi_channel_resume(gsi, endpoint->channel_id);
	if (ret)
		dev_err(dev, "error %d resuming channel %u\n", ret,
			endpoint->channel_id);
	else if (!endpoint->toward_ipa)
		ipa_endpoint_replenish_enable(endpoint);
}

void ipa_endpoint_suspend(struct ipa *ipa)
{
	if (!ipa->setup_complete)
		return;

	if (ipa->modem_netdev)
		ipa_modem_suspend(ipa->modem_netdev);

	ipa_endpoint_suspend_one(ipa->name_map[IPA_ENDPOINT_AP_LAN_RX]);
	ipa_endpoint_suspend_one(ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX]);
}

void ipa_endpoint_resume(struct ipa *ipa)
{
	if (!ipa->setup_complete)
		return;

	ipa_endpoint_resume_one(ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX]);
	ipa_endpoint_resume_one(ipa->name_map[IPA_ENDPOINT_AP_LAN_RX]);

	if (ipa->modem_netdev)
		ipa_modem_resume(ipa->modem_netdev);
}

static void ipa_endpoint_setup_one(struct ipa_endpoint *endpoint)
{
	struct gsi *gsi = &endpoint->ipa->gsi;
	u32 channel_id = endpoint->channel_id;

	/* Only AP endpoints get set up */
	if (endpoint->ee_id != GSI_EE_AP)
		return;

	endpoint->trans_tre_max = gsi_channel_trans_tre_max(gsi, channel_id);
	if (!endpoint->toward_ipa) {
		/* RX transactions require a single TRE, so the maximum
		 * backlog is the same as the maximum outstanding TREs.
		 */
		clear_bit(IPA_REPLENISH_ENABLED, endpoint->replenish_flags);
		clear_bit(IPA_REPLENISH_ACTIVE, endpoint->replenish_flags);
		INIT_DELAYED_WORK(&endpoint->replenish_work,
				  ipa_endpoint_replenish_work);
	}

	ipa_endpoint_program(endpoint);

	endpoint->ipa->set_up |= BIT(endpoint->endpoint_id);
}

static void ipa_endpoint_teardown_one(struct ipa_endpoint *endpoint)
{
	endpoint->ipa->set_up &= ~BIT(endpoint->endpoint_id);

	if (!endpoint->toward_ipa)
		cancel_delayed_work_sync(&endpoint->replenish_work);

	ipa_endpoint_reset(endpoint);
}

void ipa_endpoint_setup(struct ipa *ipa)
{
	u32 initialized = ipa->initialized;

	ipa->set_up = 0;
	while (initialized) {
		u32 endpoint_id = __ffs(initialized);

		initialized ^= BIT(endpoint_id);

		ipa_endpoint_setup_one(&ipa->endpoint[endpoint_id]);
	}
}

void ipa_endpoint_teardown(struct ipa *ipa)
{
	u32 set_up = ipa->set_up;

	while (set_up) {
		u32 endpoint_id = __fls(set_up);

		set_up ^= BIT(endpoint_id);

		ipa_endpoint_teardown_one(&ipa->endpoint[endpoint_id]);
	}
	ipa->set_up = 0;
}

int ipa_endpoint_config(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	u32 initialized;
	u32 rx_base;
	u32 rx_mask;
	u32 tx_mask;
	int ret = 0;
	u32 max;
	u32 val;

	/* Prior to IPAv3.5, the FLAVOR_0 register was not supported.
	 * Furthermore, the endpoints were not grouped such that TX
	 * endpoint numbers started with 0 and RX endpoints had numbers
	 * higher than all TX endpoints, so we can't do the simple
	 * direction check used for newer hardware below.
	 *
	 * For hardware that doesn't support the FLAVOR_0 register,
	 * just set the available mask to support any endpoint, and
	 * assume the configuration is valid.
	 */
	if (ipa->version < IPA_VERSION_3_5) {
		ipa->available = ~0;
		return 0;
	}

	/* Find out about the endpoints supplied by the hardware, and ensure
	 * the highest one doesn't exceed the number we support.
	 */
	val = ioread32(ipa->reg_virt + IPA_REG_FLAVOR_0_OFFSET);

	/* Our RX is an IPA producer */
	rx_base = u32_get_bits(val, IPA_PROD_LOWEST_FMASK);
	max = rx_base + u32_get_bits(val, IPA_MAX_PROD_PIPES_FMASK);
	if (max > IPA_ENDPOINT_MAX) {
		dev_err(dev, "too many endpoints (%u > %u)\n",
			max, IPA_ENDPOINT_MAX);
		return -EINVAL;
	}
	rx_mask = GENMASK(max - 1, rx_base);

	/* Our TX is an IPA consumer */
	max = u32_get_bits(val, IPA_MAX_CONS_PIPES_FMASK);
	tx_mask = GENMASK(max - 1, 0);

	ipa->available = rx_mask | tx_mask;

	/* Check for initialized endpoints not supported by the hardware */
	if (ipa->initialized & ~ipa->available) {
		dev_err(dev, "unavailable endpoint id(s) 0x%08x\n",
			ipa->initialized & ~ipa->available);
		ret = -EINVAL;		/* Report other errors too */
	}

	initialized = ipa->initialized;
	while (initialized) {
		u32 endpoint_id = __ffs(initialized);
		struct ipa_endpoint *endpoint;

		initialized ^= BIT(endpoint_id);

		/* Make sure it's pointing in the right direction */
		endpoint = &ipa->endpoint[endpoint_id];
		if ((endpoint_id < rx_base) != endpoint->toward_ipa) {
			dev_err(dev, "endpoint id %u wrong direction\n",
				endpoint_id);
			ret = -EINVAL;
		}
	}

	return ret;
}

void ipa_endpoint_deconfig(struct ipa *ipa)
{
	ipa->available = 0;	/* Nothing more to do */
}

static void ipa_endpoint_init_one(struct ipa *ipa, enum ipa_endpoint_name name,
				  const struct ipa_gsi_endpoint_data *data)
{
	struct ipa_endpoint *endpoint;

	endpoint = &ipa->endpoint[data->endpoint_id];

	if (data->ee_id == GSI_EE_AP)
		ipa->channel_map[data->channel_id] = endpoint;
	ipa->name_map[name] = endpoint;

	endpoint->ipa = ipa;
	endpoint->ee_id = data->ee_id;
	endpoint->channel_id = data->channel_id;
	endpoint->endpoint_id = data->endpoint_id;
	endpoint->toward_ipa = data->toward_ipa;
	endpoint->data = &data->endpoint.config;

	ipa->initialized |= BIT(endpoint->endpoint_id);
}

static void ipa_endpoint_exit_one(struct ipa_endpoint *endpoint)
{
	endpoint->ipa->initialized &= ~BIT(endpoint->endpoint_id);

	memset(endpoint, 0, sizeof(*endpoint));
}

void ipa_endpoint_exit(struct ipa *ipa)
{
	u32 initialized = ipa->initialized;

	while (initialized) {
		u32 endpoint_id = __fls(initialized);

		initialized ^= BIT(endpoint_id);

		ipa_endpoint_exit_one(&ipa->endpoint[endpoint_id]);
	}
	memset(ipa->name_map, 0, sizeof(ipa->name_map));
	memset(ipa->channel_map, 0, sizeof(ipa->channel_map));
}

/* Returns a bitmask of endpoints that support filtering, or 0 on error */
u32 ipa_endpoint_init(struct ipa *ipa, u32 count,
		      const struct ipa_gsi_endpoint_data *data)
{
	enum ipa_endpoint_name name;
	u32 filter_map;

	BUILD_BUG_ON(!IPA_REPLENISH_BATCH);

	if (!ipa_endpoint_data_valid(ipa, count, data))
		return 0;	/* Error */

	ipa->initialized = 0;

	filter_map = 0;
	for (name = 0; name < count; name++, data++) {
		if (ipa_gsi_endpoint_data_empty(data))
			continue;	/* Skip over empty slots */

		ipa_endpoint_init_one(ipa, name, data);

		if (data->endpoint.filter_support)
			filter_map |= BIT(data->endpoint_id);
	}

	if (!ipa_filter_map_valid(ipa, filter_map))
		goto err_endpoint_exit;

	return filter_map;	/* Non-zero bitmask */

err_endpoint_exit:
	ipa_endpoint_exit(ipa);

	return 0;	/* Error */
}
