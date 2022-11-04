// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2022 Linaro Ltd.
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

/* Hardware is told about receive buffers once a "batch" has been queued */
#define IPA_REPLENISH_BATCH	16		/* Must be non-zero */

/* The amount of RX buffer space consumed by standard skb overhead */
#define IPA_RX_BUFFER_OVERHEAD	(PAGE_SIZE - SKB_MAX_ORDER(NET_SKB_PAD, 0))

/* Where to find the QMAP mux_id for a packet within modem-supplied metadata */
#define IPA_ENDPOINT_QMAP_METADATA_MASK		0x000000ff /* host byte order */

#define IPA_ENDPOINT_RESET_AGGR_RETRY_MAX	3

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

/* Compute the aggregation size value to use for a given buffer size */
static u32 ipa_aggr_size_kb(u32 rx_buffer_size, bool aggr_hard_limit)
{
	/* A hard aggregation limit will not be crossed; aggregation closes
	 * if saving incoming data would cross the hard byte limit boundary.
	 *
	 * With a soft limit, aggregation closes *after* the size boundary
	 * has been crossed.  In that case the limit must leave enough space
	 * after that limit to receive a full MTU of data plus overhead.
	 */
	if (!aggr_hard_limit)
		rx_buffer_size -= IPA_MTU + IPA_RX_BUFFER_OVERHEAD;

	/* The byte limit is encoded as a number of kilobytes */

	return rx_buffer_size / SZ_1K;
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
		const struct ipa_endpoint_rx *rx_config;
		const struct ipa_reg *reg;
		u32 buffer_size;
		u32 aggr_size;
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

		rx_config = &data->endpoint.config.rx;

		/* The buffer size must hold an MTU plus overhead */
		buffer_size = rx_config->buffer_size;
		limit = IPA_MTU + IPA_RX_BUFFER_OVERHEAD;
		if (buffer_size < limit) {
			dev_err(dev, "RX buffer size too small for RX endpoint %u (%u < %u)\n",
				data->endpoint_id, buffer_size, limit);
			return false;
		}

		if (!data->endpoint.config.aggregation) {
			bool result = true;

			/* No aggregation; check for bogus aggregation data */
			if (rx_config->aggr_time_limit) {
				dev_err(dev,
					"time limit with no aggregation for RX endpoint %u\n",
					data->endpoint_id);
				result = false;
			}

			if (rx_config->aggr_hard_limit) {
				dev_err(dev, "hard limit with no aggregation for RX endpoint %u\n",
					data->endpoint_id);
				result = false;
			}

			if (rx_config->aggr_close_eof) {
				dev_err(dev, "close EOF with no aggregation for RX endpoint %u\n",
					data->endpoint_id);
				result = false;
			}

			return result;	/* Nothing more to check */
		}

		/* For an endpoint supporting receive aggregation, the byte
		 * limit defines the point at which aggregation closes.  This
		 * check ensures the receive buffer size doesn't result in a
		 * limit that exceeds what's representable in the aggregation
		 * byte limit field.
		 */
		aggr_size = ipa_aggr_size_kb(buffer_size - NET_SKB_PAD,
					     rx_config->aggr_hard_limit);
		reg = ipa_reg(ipa, ENDP_INIT_AGGR);

		limit = ipa_reg_field_max(reg, BYTE_LIMIT);
		if (aggr_size > limit) {
			dev_err(dev, "aggregated size too large for RX endpoint %u (%u KB > %u KB)\n",
				data->endpoint_id, aggr_size, limit);

			return false;
		}

		return true;	/* Nothing more to check for RX */
	}

	/* Starting with IPA v4.5 sequencer replication is obsolete */
	if (ipa->version >= IPA_VERSION_4_5) {
		if (data->endpoint.config.tx.seq_rep_type) {
			dev_err(dev, "no-zero seq_rep_type TX endpoint %u\n",
				data->endpoint_id);
			return false;
		}
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

/* Validate endpoint configuration data.  Return max defined endpoint ID */
static u32 ipa_endpoint_max(struct ipa *ipa, u32 count,
			    const struct ipa_gsi_endpoint_data *data)
{
	const struct ipa_gsi_endpoint_data *dp = data;
	struct device *dev = &ipa->pdev->dev;
	enum ipa_endpoint_name name;
	u32 max;

	if (count > IPA_ENDPOINT_COUNT) {
		dev_err(dev, "too many endpoints specified (%u > %u)\n",
			count, IPA_ENDPOINT_COUNT);
		return 0;
	}

	/* Make sure needed endpoints have defined data */
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_COMMAND_TX])) {
		dev_err(dev, "command TX endpoint not defined\n");
		return 0;
	}
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_LAN_RX])) {
		dev_err(dev, "LAN RX endpoint not defined\n");
		return 0;
	}
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_MODEM_TX])) {
		dev_err(dev, "AP->modem TX endpoint not defined\n");
		return 0;
	}
	if (ipa_gsi_endpoint_data_empty(&data[IPA_ENDPOINT_AP_MODEM_RX])) {
		dev_err(dev, "AP<-modem RX endpoint not defined\n");
		return 0;
	}

	max = 0;
	for (name = 0; name < count; name++, dp++) {
		if (!ipa_endpoint_data_valid_one(ipa, count, data, dp))
			return 0;
		max = max_t(u32, max, dp->endpoint_id);
	}

	return max;
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
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 field_id;
	u32 offset;
	bool state;
	u32 mask;
	u32 val;

	if (endpoint->toward_ipa)
		WARN_ON(ipa->version >= IPA_VERSION_4_2);
	else
		WARN_ON(ipa->version >= IPA_VERSION_4_0);

	reg = ipa_reg(ipa, ENDP_INIT_CTRL);
	offset = ipa_reg_n_offset(reg, endpoint->endpoint_id);
	val = ioread32(ipa->reg_virt + offset);

	field_id = endpoint->toward_ipa ? ENDP_DELAY : ENDP_SUSPEND;
	mask = ipa_reg_bit(reg, field_id);

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
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	u32 unit = endpoint_id / 32;
	const struct ipa_reg *reg;
	u32 val;

	WARN_ON(!test_bit(endpoint_id, ipa->available));

	reg = ipa_reg(ipa, STATE_AGGR_ACTIVE);
	val = ioread32(ipa->reg_virt + ipa_reg_n_offset(reg, unit));

	return !!(val & BIT(endpoint_id % 32));
}

static void ipa_endpoint_force_close(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	u32 mask = BIT(endpoint_id % 32);
	struct ipa *ipa = endpoint->ipa;
	u32 unit = endpoint_id / 32;
	const struct ipa_reg *reg;

	WARN_ON(!test_bit(endpoint_id, ipa->available));

	reg = ipa_reg(ipa, AGGR_FORCE_CLOSE);
	iowrite32(mask, ipa->reg_virt + ipa_reg_n_offset(reg, unit));
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

	if (!endpoint->config.aggregation)
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
	u32 endpoint_id = 0;

	while (endpoint_id < ipa->endpoint_count) {
		struct ipa_endpoint *endpoint = &ipa->endpoint[endpoint_id++];

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
	struct gsi_trans *trans;
	u32 endpoint_id;
	u32 count;

	/* We need one command per modem TX endpoint, plus the commands
	 * that clear the pipeline.
	 */
	count = ipa->modem_tx_count + ipa_cmd_pipeline_clear_count();
	trans = ipa_cmd_trans_alloc(ipa, count);
	if (!trans) {
		dev_err(&ipa->pdev->dev,
			"no transaction to reset modem exception endpoints\n");
		return -EBUSY;
	}

	for_each_set_bit(endpoint_id, ipa->defined, ipa->endpoint_count) {
		struct ipa_endpoint *endpoint;
		const struct ipa_reg *reg;
		u32 offset;

		/* We only reset modem TX endpoints */
		endpoint = &ipa->endpoint[endpoint_id];
		if (!(endpoint->ee_id == GSI_EE_MODEM && endpoint->toward_ipa))
			continue;

		reg = ipa_reg(ipa, ENDP_STATUS);
		offset = ipa_reg_n_offset(reg, endpoint_id);

		/* Value written is 0, and all bits are updated.  That
		 * means status is disabled on the endpoint, and as a
		 * result all other fields in the register are ignored.
		 */
		ipa_cmd_register_write_add(trans, offset, 0, ~0, false);
	}

	ipa_cmd_pipeline_clear_add(trans);

	gsi_trans_commit_wait(trans);

	ipa_cmd_pipeline_clear_wait(ipa);

	return 0;
}

static void ipa_endpoint_init_cfg(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	enum ipa_cs_offload_en enabled;
	const struct ipa_reg *reg;
	u32 val = 0;

	reg = ipa_reg(ipa, ENDP_INIT_CFG);
	/* FRAG_OFFLOAD_EN is 0 */
	if (endpoint->config.checksum) {
		enum ipa_version version = ipa->version;

		if (endpoint->toward_ipa) {
			u32 off;

			/* Checksum header offset is in 4-byte units */
			off = sizeof(struct rmnet_map_header) / sizeof(u32);
			val |= ipa_reg_encode(reg, CS_METADATA_HDR_OFFSET, off);

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
	val |= ipa_reg_encode(reg, CS_OFFLOAD_EN, enabled);
	/* CS_GEN_QMB_MASTER_SEL is 0 */

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static void ipa_endpoint_init_nat(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val;

	if (!endpoint->toward_ipa)
		return;

	reg = ipa_reg(ipa, ENDP_INIT_NAT);
	val = ipa_reg_encode(reg, NAT_EN, IPA_NAT_BYPASS);

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static u32
ipa_qmap_header_size(enum ipa_version version, struct ipa_endpoint *endpoint)
{
	u32 header_size = sizeof(struct rmnet_map_header);

	/* Without checksum offload, we just have the MAP header */
	if (!endpoint->config.checksum)
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

/* Encoded value for ENDP_INIT_HDR register HDR_LEN* field(s) */
static u32 ipa_header_size_encode(enum ipa_version version,
				  const struct ipa_reg *reg, u32 header_size)
{
	u32 field_max = ipa_reg_field_max(reg, HDR_LEN);
	u32 val;

	/* We know field_max can be used as a mask (2^n - 1) */
	val = ipa_reg_encode(reg, HDR_LEN, header_size & field_max);
	if (version < IPA_VERSION_4_5) {
		WARN_ON(header_size > field_max);
		return val;
	}

	/* IPA v4.5 adds a few more most-significant bits */
	header_size >>= hweight32(field_max);
	WARN_ON(header_size > ipa_reg_field_max(reg, HDR_LEN_MSB));
	val |= ipa_reg_encode(reg, HDR_LEN_MSB, header_size);

	return val;
}

/* Encoded value for ENDP_INIT_HDR register OFST_METADATA* field(s) */
static u32 ipa_metadata_offset_encode(enum ipa_version version,
				      const struct ipa_reg *reg, u32 offset)
{
	u32 field_max = ipa_reg_field_max(reg, HDR_OFST_METADATA);
	u32 val;

	/* We know field_max can be used as a mask (2^n - 1) */
	val = ipa_reg_encode(reg, HDR_OFST_METADATA, offset);
	if (version < IPA_VERSION_4_5) {
		WARN_ON(offset > field_max);
		return val;
	}

	/* IPA v4.5 adds a few more most-significant bits */
	offset >>= hweight32(field_max);
	WARN_ON(offset > ipa_reg_field_max(reg, HDR_OFST_METADATA_MSB));
	val |= ipa_reg_encode(reg, HDR_OFST_METADATA_MSB, offset);

	return val;
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
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val = 0;

	reg = ipa_reg(ipa, ENDP_INIT_HDR);
	if (endpoint->config.qmap) {
		enum ipa_version version = ipa->version;
		size_t header_size;

		header_size = ipa_qmap_header_size(version, endpoint);
		val = ipa_header_size_encode(version, reg, header_size);

		/* Define how to fill fields in a received QMAP header */
		if (!endpoint->toward_ipa) {
			u32 off;     /* Field offset within header */

			/* Where IPA will write the metadata value */
			off = offsetof(struct rmnet_map_header, mux_id);
			val |= ipa_metadata_offset_encode(version, reg, off);

			/* Where IPA will write the length */
			off = offsetof(struct rmnet_map_header, pkt_len);
			/* Upper bits are stored in HDR_EXT with IPA v4.5 */
			if (version >= IPA_VERSION_4_5)
				off &= ipa_reg_field_max(reg, HDR_OFST_PKT_SIZE);

			val |= ipa_reg_bit(reg, HDR_OFST_PKT_SIZE_VALID);
			val |= ipa_reg_encode(reg, HDR_OFST_PKT_SIZE, off);
		}
		/* For QMAP TX, metadata offset is 0 (modem assumes this) */
		val |= ipa_reg_bit(reg, HDR_OFST_METADATA_VALID);

		/* HDR_ADDITIONAL_CONST_LEN is 0; (RX only) */
		/* HDR_A5_MUX is 0 */
		/* HDR_LEN_INC_DEAGG_HDR is 0 */
		/* HDR_METADATA_REG_VALID is 0 (TX only, version < v4.5) */
	}

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static void ipa_endpoint_init_hdr_ext(struct ipa_endpoint *endpoint)
{
	u32 pad_align = endpoint->config.rx.pad_align;
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val = 0;

	reg = ipa_reg(ipa, ENDP_INIT_HDR_EXT);
	if (endpoint->config.qmap) {
		/* We have a header, so we must specify its endianness */
		val |= ipa_reg_bit(reg, HDR_ENDIANNESS);	/* big endian */

		/* A QMAP header contains a 6 bit pad field at offset 0.
		 * The RMNet driver assumes this field is meaningful in
		 * packets it receives, and assumes the header's payload
		 * length includes that padding.  The RMNet driver does
		 * *not* pad packets it sends, however, so the pad field
		 * (although 0) should be ignored.
		 */
		if (!endpoint->toward_ipa) {
			val |= ipa_reg_bit(reg, HDR_TOTAL_LEN_OR_PAD_VALID);
			/* HDR_TOTAL_LEN_OR_PAD is 0 (pad, not total_len) */
			val |= ipa_reg_bit(reg, HDR_PAYLOAD_LEN_INC_PADDING);
			/* HDR_TOTAL_LEN_OR_PAD_OFFSET is 0 */
		}
	}

	/* HDR_PAYLOAD_LEN_INC_PADDING is 0 */
	if (!endpoint->toward_ipa)
		val |= ipa_reg_encode(reg, HDR_PAD_TO_ALIGNMENT, pad_align);

	/* IPA v4.5 adds some most-significant bits to a few fields,
	 * two of which are defined in the HDR (not HDR_EXT) register.
	 */
	if (ipa->version >= IPA_VERSION_4_5) {
		/* HDR_TOTAL_LEN_OR_PAD_OFFSET is 0, so MSB is 0 */
		if (endpoint->config.qmap && !endpoint->toward_ipa) {
			u32 mask = ipa_reg_field_max(reg, HDR_OFST_PKT_SIZE);
			u32 off;     /* Field offset within header */

			off = offsetof(struct rmnet_map_header, pkt_len);
			/* Low bits are in the ENDP_INIT_HDR register */
			off >>= hweight32(mask);
			val |= ipa_reg_encode(reg, HDR_OFST_PKT_SIZE_MSB, off);
			/* HDR_ADDITIONAL_CONST_LEN is 0 so MSB is 0 */
		}
	}

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static void ipa_endpoint_init_hdr_metadata_mask(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val = 0;
	u32 offset;

	if (endpoint->toward_ipa)
		return;		/* Register not valid for TX endpoints */

	reg = ipa_reg(ipa,  ENDP_INIT_HDR_METADATA_MASK);
	offset = ipa_reg_n_offset(reg, endpoint_id);

	/* Note that HDR_ENDIANNESS indicates big endian header fields */
	if (endpoint->config.qmap)
		val = (__force u32)cpu_to_be32(IPA_ENDPOINT_QMAP_METADATA_MASK);

	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_endpoint_init_mode(struct ipa_endpoint *endpoint)
{
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 offset;
	u32 val;

	if (!endpoint->toward_ipa)
		return;		/* Register not valid for RX endpoints */

	reg = ipa_reg(ipa, ENDP_INIT_MODE);
	if (endpoint->config.dma_mode) {
		enum ipa_endpoint_name name = endpoint->config.dma_endpoint;
		u32 dma_endpoint_id = ipa->name_map[name]->endpoint_id;

		val = ipa_reg_encode(reg, ENDP_MODE, IPA_DMA);
		val |= ipa_reg_encode(reg, DEST_PIPE_INDEX, dma_endpoint_id);
	} else {
		val = ipa_reg_encode(reg, ENDP_MODE, IPA_BASIC);
	}
	/* All other bits unspecified (and 0) */

	offset = ipa_reg_n_offset(reg, endpoint->endpoint_id);
	iowrite32(val, ipa->reg_virt + offset);
}

/* For IPA v4.5+, times are expressed using Qtime.  The AP uses one of two
 * pulse generators (0 and 1) to measure elapsed time.  In ipa_qtime_config()
 * they're configured to have granularity 100 usec and 1 msec, respectively.
 *
 * The return value is the positive or negative Qtime value to use to
 * express the (microsecond) time provided.  A positive return value
 * means pulse generator 0 can be used; otherwise use pulse generator 1.
 */
static int ipa_qtime_val(u32 microseconds, u32 max)
{
	u32 val;

	/* Use 100 microsecond granularity if possible */
	val = DIV_ROUND_CLOSEST(microseconds, 100);
	if (val <= max)
		return (int)val;

	/* Have to use pulse generator 1 (millisecond granularity) */
	val = DIV_ROUND_CLOSEST(microseconds, 1000);
	WARN_ON(val > max);

	return (int)-val;
}

/* Encode the aggregation timer limit (microseconds) based on IPA version */
static u32 aggr_time_limit_encode(struct ipa *ipa, const struct ipa_reg *reg,
				  u32 microseconds)
{
	u32 max;
	u32 val;

	if (!microseconds)
		return 0;	/* Nothing to compute if time limit is 0 */

	max = ipa_reg_field_max(reg, TIME_LIMIT);
	if (ipa->version >= IPA_VERSION_4_5) {
		u32 gran_sel;
		int ret;

		/* Compute the Qtime limit value to use */
		ret = ipa_qtime_val(microseconds, max);
		if (ret < 0) {
			val = -ret;
			gran_sel = ipa_reg_bit(reg, AGGR_GRAN_SEL);
		} else {
			val = ret;
			gran_sel = 0;
		}

		return gran_sel | ipa_reg_encode(reg, TIME_LIMIT, val);
	}

	/* We program aggregation granularity in ipa_hardware_config() */
	val = DIV_ROUND_CLOSEST(microseconds, IPA_AGGR_GRANULARITY);
	WARN(val > max, "aggr_time_limit too large (%u > %u usec)\n",
	     microseconds, max * IPA_AGGR_GRANULARITY);

	return ipa_reg_encode(reg, TIME_LIMIT, val);
}

static void ipa_endpoint_init_aggr(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val = 0;

	reg = ipa_reg(ipa, ENDP_INIT_AGGR);
	if (endpoint->config.aggregation) {
		if (!endpoint->toward_ipa) {
			const struct ipa_endpoint_rx *rx_config;
			u32 buffer_size;
			u32 limit;

			rx_config = &endpoint->config.rx;
			val |= ipa_reg_encode(reg, AGGR_EN, IPA_ENABLE_AGGR);
			val |= ipa_reg_encode(reg, AGGR_TYPE, IPA_GENERIC);

			buffer_size = rx_config->buffer_size;
			limit = ipa_aggr_size_kb(buffer_size - NET_SKB_PAD,
						 rx_config->aggr_hard_limit);
			val |= ipa_reg_encode(reg, BYTE_LIMIT, limit);

			limit = rx_config->aggr_time_limit;
			val |= aggr_time_limit_encode(ipa, reg, limit);

			/* AGGR_PKT_LIMIT is 0 (unlimited) */

			if (rx_config->aggr_close_eof)
				val |= ipa_reg_bit(reg, SW_EOF_ACTIVE);
		} else {
			val |= ipa_reg_encode(reg, AGGR_EN, IPA_ENABLE_DEAGGR);
			val |= ipa_reg_encode(reg, AGGR_TYPE, IPA_QCMAP);
			/* other fields ignored */
		}
		/* AGGR_FORCE_CLOSE is 0 */
		/* AGGR_GRAN_SEL is 0 for IPA v4.5 */
	} else {
		val |= ipa_reg_encode(reg, AGGR_EN, IPA_BYPASS_AGGR);
		/* other fields ignored */
	}

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

/* The head-of-line blocking timer is defined as a tick count.  For
 * IPA version 4.5 the tick count is based on the Qtimer, which is
 * derived from the 19.2 MHz SoC XO clock.  For older IPA versions
 * each tick represents 128 cycles of the IPA core clock.
 *
 * Return the encoded value representing the timeout period provided
 * that should be written to the ENDP_INIT_HOL_BLOCK_TIMER register.
 */
static u32 hol_block_timer_encode(struct ipa *ipa, const struct ipa_reg *reg,
				  u32 microseconds)
{
	u32 width;
	u32 scale;
	u64 ticks;
	u64 rate;
	u32 high;
	u32 val;

	if (!microseconds)
		return 0;	/* Nothing to compute if timer period is 0 */

	if (ipa->version >= IPA_VERSION_4_5) {
		u32 max = ipa_reg_field_max(reg, TIMER_LIMIT);
		u32 gran_sel;
		int ret;

		/* Compute the Qtime limit value to use */
		ret = ipa_qtime_val(microseconds, max);
		if (ret < 0) {
			val = -ret;
			gran_sel = ipa_reg_bit(reg, TIMER_GRAN_SEL);
		} else {
			val = ret;
			gran_sel = 0;
		}

		return gran_sel | ipa_reg_encode(reg, TIMER_LIMIT, val);
	}

	/* Use 64 bit arithmetic to avoid overflow */
	rate = ipa_core_clock_rate(ipa);
	ticks = DIV_ROUND_CLOSEST(microseconds * rate, 128 * USEC_PER_SEC);

	/* We still need the result to fit into the field */
	WARN_ON(ticks > ipa_reg_field_max(reg, TIMER_BASE_VALUE));

	/* IPA v3.5.1 through v4.1 just record the tick count */
	if (ipa->version < IPA_VERSION_4_2)
		return ipa_reg_encode(reg, TIMER_BASE_VALUE, (u32)ticks);

	/* For IPA v4.2, the tick count is represented by base and
	 * scale fields within the 32-bit timer register, where:
	 *     ticks = base << scale;
	 * The best precision is achieved when the base value is as
	 * large as possible.  Find the highest set bit in the tick
	 * count, and extract the number of bits in the base field
	 * such that high bit is included.
	 */
	high = fls(ticks);		/* 1..32 (or warning above) */
	width = hweight32(ipa_reg_fmask(reg, TIMER_BASE_VALUE));
	scale = high > width ? high - width : 0;
	if (scale) {
		/* If we're scaling, round up to get a closer result */
		ticks += 1 << (scale - 1);
		/* High bit was set, so rounding might have affected it */
		if (fls(ticks) != high)
			scale++;
	}

	val = ipa_reg_encode(reg, TIMER_SCALE, scale);
	val |= ipa_reg_encode(reg, TIMER_BASE_VALUE, (u32)ticks >> scale);

	return val;
}

/* If microseconds is 0, timeout is immediate */
static void ipa_endpoint_init_hol_block_timer(struct ipa_endpoint *endpoint,
					      u32 microseconds)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val;

	/* This should only be changed when HOL_BLOCK_EN is disabled */
	reg = ipa_reg(ipa, ENDP_INIT_HOL_BLOCK_TIMER);
	val = hol_block_timer_encode(ipa, reg, microseconds);

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static void
ipa_endpoint_init_hol_block_en(struct ipa_endpoint *endpoint, bool enable)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 offset;
	u32 val;

	reg = ipa_reg(ipa, ENDP_INIT_HOL_BLOCK_EN);
	offset = ipa_reg_n_offset(reg, endpoint_id);
	val = enable ? ipa_reg_bit(reg, HOL_BLOCK_EN) : 0;

	iowrite32(val, ipa->reg_virt + offset);

	/* When enabling, the register must be written twice for IPA v4.5+ */
	if (enable && ipa->version >= IPA_VERSION_4_5)
		iowrite32(val, ipa->reg_virt + offset);
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
	u32 endpoint_id = 0;

	while (endpoint_id < ipa->endpoint_count) {
		struct ipa_endpoint *endpoint = &ipa->endpoint[endpoint_id++];

		if (endpoint->toward_ipa || endpoint->ee_id != GSI_EE_MODEM)
			continue;

		ipa_endpoint_init_hol_block_disable(endpoint);
		ipa_endpoint_init_hol_block_enable(endpoint, 0);
	}
}

static void ipa_endpoint_init_deaggr(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val = 0;

	if (!endpoint->toward_ipa)
		return;		/* Register not valid for RX endpoints */

	reg = ipa_reg(ipa, ENDP_INIT_DEAGGR);
	/* DEAGGR_HDR_LEN is 0 */
	/* PACKET_OFFSET_VALID is 0 */
	/* PACKET_OFFSET_LOCATION is ignored (not valid) */
	/* MAX_PACKET_LEN is 0 (not enforced) */

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static void ipa_endpoint_init_rsrc_grp(struct ipa_endpoint *endpoint)
{
	u32 resource_group = endpoint->config.resource_group;
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val;

	reg = ipa_reg(ipa, ENDP_INIT_RSRC_GRP);
	val = ipa_reg_encode(reg, ENDP_RSRC_GRP, resource_group);

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static void ipa_endpoint_init_seq(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct ipa_reg *reg;
	u32 val;

	if (!endpoint->toward_ipa)
		return;		/* Register not valid for RX endpoints */

	reg = ipa_reg(ipa, ENDP_INIT_SEQ);

	/* Low-order byte configures primary packet processing */
	val = ipa_reg_encode(reg, SEQ_TYPE, endpoint->config.tx.seq_type);

	/* Second byte (if supported) configures replicated packet processing */
	if (ipa->version < IPA_VERSION_4_5)
		val |= ipa_reg_encode(reg, SEQ_REP_TYPE,
				      endpoint->config.tx.seq_rep_type);

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
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
	if (nr_frags > endpoint->skb_frag_max) {
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
	const struct ipa_reg *reg;
	u32 val = 0;

	reg = ipa_reg(ipa, ENDP_STATUS);
	if (endpoint->config.status_enable) {
		val |= ipa_reg_bit(reg, STATUS_EN);
		if (endpoint->toward_ipa) {
			enum ipa_endpoint_name name;
			u32 status_endpoint_id;

			name = endpoint->config.tx.status_endpoint;
			status_endpoint_id = ipa->name_map[name]->endpoint_id;

			val |= ipa_reg_encode(reg, STATUS_ENDP,
					      status_endpoint_id);
		}
		/* STATUS_LOCATION is 0, meaning status element precedes
		 * packet (not present for IPA v4.5+)
		 */
		/* STATUS_PKT_SUPPRESS_FMASK is 0 (not present for v4.0+) */
	}

	iowrite32(val, ipa->reg_virt + ipa_reg_n_offset(reg, endpoint_id));
}

static int ipa_endpoint_replenish_one(struct ipa_endpoint *endpoint,
				      struct gsi_trans *trans)
{
	struct page *page;
	u32 buffer_size;
	u32 offset;
	u32 len;
	int ret;

	buffer_size = endpoint->config.rx.buffer_size;
	page = dev_alloc_pages(get_order(buffer_size));
	if (!page)
		return -ENOMEM;

	/* Offset the buffer to make space for skb headroom */
	offset = NET_SKB_PAD;
	len = buffer_size - offset;

	ret = gsi_trans_page_add(trans, page, len, offset);
	if (ret)
		put_page(page);
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
	if (skb) {
		/* Copy the data into the socket buffer and receive it */
		skb_put(skb, len);
		memcpy(skb->data, data, len);
		skb->truesize += extra;
	}

	ipa_modem_skb_rx(endpoint->netdev, skb);
}

static bool ipa_endpoint_skb_build(struct ipa_endpoint *endpoint,
				   struct page *page, u32 len)
{
	u32 buffer_size = endpoint->config.rx.buffer_size;
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
	u32 buffer_size = endpoint->config.rx.buffer_size;
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
		align = endpoint->config.rx.pad_align ? : 1;
		len = le16_to_cpu(status->pkt_len);
		len = sizeof(*status) + ALIGN(len, align);
		if (endpoint->config.checksum)
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

void ipa_endpoint_trans_complete(struct ipa_endpoint *endpoint,
				 struct gsi_trans *trans)
{
	struct page *page;

	if (endpoint->toward_ipa)
		return;

	if (trans->cancelled)
		goto done;

	/* Parse or build a socket buffer using the actual received length */
	page = trans->data;
	if (endpoint->config.status_enable)
		ipa_endpoint_status_parse(endpoint, page, trans->len);
	else if (ipa_endpoint_skb_build(endpoint, page, trans->len))
		trans->data = NULL;	/* Pages have been consumed */
done:
	ipa_endpoint_replenish(endpoint);
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

		if (page)
			put_page(page);
	}
}

void ipa_endpoint_default_route_set(struct ipa *ipa, u32 endpoint_id)
{
	const struct ipa_reg *reg;
	u32 val;

	reg = ipa_reg(ipa, ROUTE);
	/* ROUTE_DIS is 0 */
	val = ipa_reg_encode(reg, ROUTE_DEF_PIPE, endpoint_id);
	val |= ipa_reg_bit(reg, ROUTE_DEF_HDR_TABLE);
	/* ROUTE_DEF_HDR_OFST is 0 */
	val |= ipa_reg_encode(reg, ROUTE_FRAG_DEF_PIPE, endpoint_id);
	val |= ipa_reg_bit(reg, ROUTE_DEF_RETAIN_HDR);

	iowrite32(val, ipa->reg_virt + ipa_reg_offset(reg));
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
			endpoint->config.aggregation;
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
	if (!endpoint->toward_ipa) {
		if (endpoint->config.rx.holb_drop)
			ipa_endpoint_init_hol_block_enable(endpoint, 0);
		else
			ipa_endpoint_init_hol_block_disable(endpoint);
	}
	ipa_endpoint_init_deaggr(endpoint);
	ipa_endpoint_init_rsrc_grp(endpoint);
	ipa_endpoint_init_seq(endpoint);
	ipa_endpoint_status(endpoint);
}

int ipa_endpoint_enable_one(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	struct gsi *gsi = &ipa->gsi;
	int ret;

	ret = gsi_channel_start(gsi, endpoint->channel_id);
	if (ret) {
		dev_err(&ipa->pdev->dev,
			"error %d starting %cX channel %u for endpoint %u\n",
			ret, endpoint->toward_ipa ? 'T' : 'R',
			endpoint->channel_id, endpoint_id);
		return ret;
	}

	if (!endpoint->toward_ipa) {
		ipa_interrupt_suspend_enable(ipa->interrupt, endpoint_id);
		ipa_endpoint_replenish_enable(endpoint);
	}

	__set_bit(endpoint_id, ipa->enabled);

	return 0;
}

void ipa_endpoint_disable_one(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	struct gsi *gsi = &ipa->gsi;
	int ret;

	if (!test_bit(endpoint_id, ipa->enabled))
		return;

	__clear_bit(endpoint_id, endpoint->ipa->enabled);

	if (!endpoint->toward_ipa) {
		ipa_endpoint_replenish_disable(endpoint);
		ipa_interrupt_suspend_disable(ipa->interrupt, endpoint_id);
	}

	/* Note that if stop fails, the channel's state is not well-defined */
	ret = gsi_channel_stop(gsi, endpoint->channel_id);
	if (ret)
		dev_err(&ipa->pdev->dev,
			"error %d attempting to stop endpoint %u\n", ret,
			endpoint_id);
}

void ipa_endpoint_suspend_one(struct ipa_endpoint *endpoint)
{
	struct device *dev = &endpoint->ipa->pdev->dev;
	struct gsi *gsi = &endpoint->ipa->gsi;
	int ret;

	if (!test_bit(endpoint->endpoint_id, endpoint->ipa->enabled))
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

	if (!test_bit(endpoint->endpoint_id, endpoint->ipa->enabled))
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

	endpoint->skb_frag_max = gsi->channel[channel_id].trans_tre_max - 1;
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

	__set_bit(endpoint->endpoint_id, endpoint->ipa->set_up);
}

static void ipa_endpoint_teardown_one(struct ipa_endpoint *endpoint)
{
	__clear_bit(endpoint->endpoint_id, endpoint->ipa->set_up);

	if (!endpoint->toward_ipa)
		cancel_delayed_work_sync(&endpoint->replenish_work);

	ipa_endpoint_reset(endpoint);
}

void ipa_endpoint_setup(struct ipa *ipa)
{
	u32 endpoint_id;

	for_each_set_bit(endpoint_id, ipa->defined, ipa->endpoint_count)
		ipa_endpoint_setup_one(&ipa->endpoint[endpoint_id]);
}

void ipa_endpoint_teardown(struct ipa *ipa)
{
	u32 endpoint_id;

	for_each_set_bit(endpoint_id, ipa->set_up, ipa->endpoint_count)
		ipa_endpoint_teardown_one(&ipa->endpoint[endpoint_id]);
}

void ipa_endpoint_deconfig(struct ipa *ipa)
{
	ipa->available_count = 0;
	bitmap_free(ipa->available);
	ipa->available = NULL;
}

int ipa_endpoint_config(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	const struct ipa_reg *reg;
	u32 endpoint_id;
	u32 tx_count;
	u32 rx_count;
	u32 rx_base;
	u32 limit;
	u32 val;

	/* Prior to IPA v3.5, the FLAVOR_0 register was not supported.
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
		ipa->available = bitmap_zalloc(IPA_ENDPOINT_MAX, GFP_KERNEL);
		if (!ipa->available)
			return -ENOMEM;
		ipa->available_count = IPA_ENDPOINT_MAX;

		bitmap_set(ipa->available, 0, IPA_ENDPOINT_MAX);

		return 0;
	}

	/* Find out about the endpoints supplied by the hardware, and ensure
	 * the highest one doesn't exceed the number supported by software.
	 */
	reg = ipa_reg(ipa, FLAVOR_0);
	val = ioread32(ipa->reg_virt + ipa_reg_offset(reg));

	/* Our RX is an IPA producer; our TX is an IPA consumer. */
	tx_count = ipa_reg_decode(reg, MAX_CONS_PIPES, val);
	rx_count = ipa_reg_decode(reg, MAX_PROD_PIPES, val);
	rx_base = ipa_reg_decode(reg, PROD_LOWEST, val);

	limit = rx_base + rx_count;
	if (limit > IPA_ENDPOINT_MAX) {
		dev_err(dev, "too many endpoints, %u > %u\n",
			limit, IPA_ENDPOINT_MAX);
		return -EINVAL;
	}

	/* Allocate and initialize the available endpoint bitmap */
	ipa->available = bitmap_zalloc(limit, GFP_KERNEL);
	if (!ipa->available)
		return -ENOMEM;
	ipa->available_count = limit;

	/* Mark all supported RX and TX endpoints as available */
	bitmap_set(ipa->available, 0, tx_count);
	bitmap_set(ipa->available, rx_base, rx_count);

	for_each_set_bit(endpoint_id, ipa->defined, ipa->endpoint_count) {
		struct ipa_endpoint *endpoint;

		if (endpoint_id >= limit) {
			dev_err(dev, "invalid endpoint id, %u > %u\n",
				endpoint_id, limit - 1);
			goto err_free_bitmap;
		}

		if (!test_bit(endpoint_id, ipa->available)) {
			dev_err(dev, "unavailable endpoint id %u\n",
				endpoint_id);
			goto err_free_bitmap;
		}

		/* Make sure it's pointing in the right direction */
		endpoint = &ipa->endpoint[endpoint_id];
		if (endpoint->toward_ipa) {
			if (endpoint_id < tx_count)
				continue;
		} else if (endpoint_id >= rx_base) {
			continue;
		}

		dev_err(dev, "endpoint id %u wrong direction\n", endpoint_id);
		goto err_free_bitmap;
	}

	return 0;

err_free_bitmap:
	ipa_endpoint_deconfig(ipa);

	return -EINVAL;
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
	endpoint->config = data->endpoint.config;

	__set_bit(endpoint->endpoint_id, ipa->defined);
}

static void ipa_endpoint_exit_one(struct ipa_endpoint *endpoint)
{
	__clear_bit(endpoint->endpoint_id, endpoint->ipa->defined);

	memset(endpoint, 0, sizeof(*endpoint));
}

void ipa_endpoint_exit(struct ipa *ipa)
{
	u32 endpoint_id;

	ipa->filtered = 0;

	for_each_set_bit(endpoint_id, ipa->defined, ipa->endpoint_count)
		ipa_endpoint_exit_one(&ipa->endpoint[endpoint_id]);

	bitmap_free(ipa->enabled);
	ipa->enabled = NULL;
	bitmap_free(ipa->set_up);
	ipa->set_up = NULL;
	bitmap_free(ipa->defined);
	ipa->defined = NULL;

	memset(ipa->name_map, 0, sizeof(ipa->name_map));
	memset(ipa->channel_map, 0, sizeof(ipa->channel_map));
}

/* Returns a bitmask of endpoints that support filtering, or 0 on error */
int ipa_endpoint_init(struct ipa *ipa, u32 count,
		      const struct ipa_gsi_endpoint_data *data)
{
	enum ipa_endpoint_name name;
	u32 filtered;

	BUILD_BUG_ON(!IPA_REPLENISH_BATCH);

	/* Number of endpoints is one more than the maximum ID */
	ipa->endpoint_count = ipa_endpoint_max(ipa, count, data) + 1;
	if (!ipa->endpoint_count)
		return -EINVAL;

	/* Initialize endpoint state bitmaps */
	ipa->defined = bitmap_zalloc(ipa->endpoint_count, GFP_KERNEL);
	if (!ipa->defined)
		return -ENOMEM;

	ipa->set_up = bitmap_zalloc(ipa->endpoint_count, GFP_KERNEL);
	if (!ipa->set_up)
		goto err_free_defined;

	ipa->enabled = bitmap_zalloc(ipa->endpoint_count, GFP_KERNEL);
	if (!ipa->enabled)
		goto err_free_set_up;

	filtered = 0;
	for (name = 0; name < count; name++, data++) {
		if (ipa_gsi_endpoint_data_empty(data))
			continue;	/* Skip over empty slots */

		ipa_endpoint_init_one(ipa, name, data);

		if (data->endpoint.filter_support)
			filtered |= BIT(data->endpoint_id);
		if (data->ee_id == GSI_EE_MODEM && data->toward_ipa)
			ipa->modem_tx_count++;
	}

	/* Make sure the set of filtered endpoints is valid */
	if (!ipa_filtered_valid(ipa, filtered)) {
		ipa_endpoint_exit(ipa);

		return -EINVAL;
	}

	ipa->filtered = filtered;

	return 0;

err_free_set_up:
	bitmap_free(ipa->set_up);
	ipa->set_up = NULL;
err_free_defined:
	bitmap_free(ipa->defined);
	ipa->defined = NULL;

	return -ENOMEM;
}
