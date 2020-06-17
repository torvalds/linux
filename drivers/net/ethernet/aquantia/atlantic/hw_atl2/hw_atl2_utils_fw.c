// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include <linux/iopoll.h>

#include "aq_hw.h"
#include "aq_hw_utils.h"
#include "hw_atl/hw_atl_llh.h"
#include "hw_atl2_utils.h"
#include "hw_atl2_llh.h"
#include "hw_atl2_internal.h"

#define AQ_A2_FW_READ_TRY_MAX 1000

#define hw_atl2_shared_buffer_write(HW, ITEM, VARIABLE) \
	hw_atl2_mif_shared_buf_write(HW,\
		(offsetof(struct fw_interface_in, ITEM) / sizeof(u32)),\
		(u32 *)&(VARIABLE), sizeof(VARIABLE) / sizeof(u32))

#define hw_atl2_shared_buffer_get(HW, ITEM, VARIABLE) \
	hw_atl2_mif_shared_buf_get(HW, \
		(offsetof(struct fw_interface_in, ITEM) / sizeof(u32)),\
		(u32 *)&(VARIABLE), \
		sizeof(VARIABLE) / sizeof(u32))

/* This should never be used on non atomic fields,
 * treat any > u32 read as non atomic.
 */
#define hw_atl2_shared_buffer_read(HW, ITEM, VARIABLE) \
{\
	BUILD_BUG_ON_MSG((offsetof(struct fw_interface_out, ITEM) % \
			 sizeof(u32)) != 0,\
			 "Non aligned read " # ITEM);\
	BUILD_BUG_ON_MSG(sizeof(VARIABLE) > sizeof(u32),\
			 "Non atomic read " # ITEM);\
	hw_atl2_mif_shared_buf_read(HW, \
		(offsetof(struct fw_interface_out, ITEM) / sizeof(u32)),\
		(u32 *)&(VARIABLE), sizeof(VARIABLE) / sizeof(u32));\
}

#define hw_atl2_shared_buffer_read_safe(HW, ITEM, DATA) \
	hw_atl2_shared_buffer_read_block((HW), \
		(offsetof(struct fw_interface_out, ITEM) / sizeof(u32)),\
		sizeof(((struct fw_interface_out *)0)->ITEM) / sizeof(u32),\
		(DATA))

static int hw_atl2_shared_buffer_read_block(struct aq_hw_s *self,
					    u32 offset, u32 dwords, void *data)
{
	struct transaction_counter_s tid1, tid2;
	int cnt = 0;

	do {
		do {
			hw_atl2_shared_buffer_read(self, transaction_id, tid1);
			cnt++;
			if (cnt > AQ_A2_FW_READ_TRY_MAX)
				return -ETIME;
			if (tid1.transaction_cnt_a != tid1.transaction_cnt_b)
				udelay(1);
		} while (tid1.transaction_cnt_a != tid1.transaction_cnt_b);

		hw_atl2_mif_shared_buf_read(self, offset, (u32 *)data, dwords);

		hw_atl2_shared_buffer_read(self, transaction_id, tid2);

		cnt++;
		if (cnt > AQ_A2_FW_READ_TRY_MAX)
			return -ETIME;
	} while (tid2.transaction_cnt_a != tid2.transaction_cnt_b ||
		 tid1.transaction_cnt_a != tid2.transaction_cnt_a);

	return 0;
}

static inline int hw_atl2_shared_buffer_finish_ack(struct aq_hw_s *self)
{
	u32 val;
	int err;

	hw_atl2_mif_host_finished_write_set(self, 1U);
	err = readx_poll_timeout_atomic(hw_atl2_mif_mcp_finished_read_get,
					self, val, val == 0U,
					100, 100000U);
	WARN(err, "hw_atl2_shared_buffer_finish_ack");

	return err;
}

static int aq_a2_fw_init(struct aq_hw_s *self)
{
	struct link_control_s link_control;
	u32 mtu;
	u32 val;
	int err;

	hw_atl2_shared_buffer_get(self, link_control, link_control);
	link_control.mode = AQ_HOST_MODE_ACTIVE;
	hw_atl2_shared_buffer_write(self, link_control, link_control);

	hw_atl2_shared_buffer_get(self, mtu, mtu);
	mtu = HW_ATL2_MTU_JUMBO;
	hw_atl2_shared_buffer_write(self, mtu, mtu);

	hw_atl2_mif_host_finished_write_set(self, 1U);
	err = readx_poll_timeout_atomic(hw_atl2_mif_mcp_finished_read_get,
					self, val, val == 0U,
					100, 5000000U);
	WARN(err, "hw_atl2_shared_buffer_finish_ack");

	return err;
}

static int aq_a2_fw_deinit(struct aq_hw_s *self)
{
	struct link_control_s link_control;

	hw_atl2_shared_buffer_get(self, link_control, link_control);
	link_control.mode = AQ_HOST_MODE_SHUTDOWN;
	hw_atl2_shared_buffer_write(self, link_control, link_control);

	return hw_atl2_shared_buffer_finish_ack(self);
}

static void a2_link_speed_mask2fw(u32 speed,
				  struct link_options_s *link_options)
{
	link_options->rate_10G = !!(speed & AQ_NIC_RATE_10G);
	link_options->rate_5G = !!(speed & AQ_NIC_RATE_5G);
	link_options->rate_N5G = !!(speed & AQ_NIC_RATE_5GSR);
	link_options->rate_2P5G = !!(speed & AQ_NIC_RATE_2G5);
	link_options->rate_N2P5G = link_options->rate_2P5G;
	link_options->rate_1G = !!(speed & AQ_NIC_RATE_1G);
	link_options->rate_100M = !!(speed & AQ_NIC_RATE_100M);
	link_options->rate_10M = !!(speed & AQ_NIC_RATE_10M);
}

static int aq_a2_fw_set_link_speed(struct aq_hw_s *self, u32 speed)
{
	struct link_options_s link_options;

	hw_atl2_shared_buffer_get(self, link_options, link_options);
	link_options.link_up = 1U;
	a2_link_speed_mask2fw(speed, &link_options);
	hw_atl2_shared_buffer_write(self, link_options, link_options);

	return hw_atl2_shared_buffer_finish_ack(self);
}

static int aq_a2_fw_set_state(struct aq_hw_s *self,
			      enum hal_atl_utils_fw_state_e state)
{
	struct link_options_s link_options;

	hw_atl2_shared_buffer_get(self, link_options, link_options);

	switch (state) {
	case MPI_INIT:
		link_options.link_up = 1U;
		break;
	case MPI_DEINIT:
		link_options.link_up = 0U;
		break;
	case MPI_RESET:
	case MPI_POWER:
		/* No actions */
		break;
	}

	hw_atl2_shared_buffer_write(self, link_options, link_options);

	return hw_atl2_shared_buffer_finish_ack(self);
}

static int aq_a2_fw_update_link_status(struct aq_hw_s *self)
{
	struct link_status_s link_status;

	hw_atl2_shared_buffer_read(self, link_status, link_status);

	switch (link_status.link_rate) {
	case AQ_A2_FW_LINK_RATE_10G:
		self->aq_link_status.mbps = 10000;
		break;
	case AQ_A2_FW_LINK_RATE_5G:
		self->aq_link_status.mbps = 5000;
		break;
	case AQ_A2_FW_LINK_RATE_2G5:
		self->aq_link_status.mbps = 2500;
		break;
	case AQ_A2_FW_LINK_RATE_1G:
		self->aq_link_status.mbps = 1000;
		break;
	case AQ_A2_FW_LINK_RATE_100M:
		self->aq_link_status.mbps = 100;
		break;
	case AQ_A2_FW_LINK_RATE_10M:
		self->aq_link_status.mbps = 10;
		break;
	default:
		self->aq_link_status.mbps = 0;
	}

	return 0;
}

static int aq_a2_fw_get_mac_permanent(struct aq_hw_s *self, u8 *mac)
{
	struct mac_address_aligned_s mac_address;

	hw_atl2_shared_buffer_get(self, mac_address, mac_address);
	ether_addr_copy(mac, (u8 *)mac_address.aligned.mac_address);

	return 0;
}

static int aq_a2_fw_update_stats(struct aq_hw_s *self)
{
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	struct statistics_s stats;

	hw_atl2_shared_buffer_read_safe(self, stats, &stats);

#define AQ_SDELTA(_N_, _F_) (self->curr_stats._N_ += \
			stats.msm._F_ - priv->last_stats.msm._F_)

	if (self->aq_link_status.mbps) {
		AQ_SDELTA(uprc, rx_unicast_frames);
		AQ_SDELTA(mprc, rx_multicast_frames);
		AQ_SDELTA(bprc, rx_broadcast_frames);
		AQ_SDELTA(erpr, rx_error_frames);

		AQ_SDELTA(uptc, tx_unicast_frames);
		AQ_SDELTA(mptc, tx_multicast_frames);
		AQ_SDELTA(bptc, tx_broadcast_frames);
		AQ_SDELTA(erpt, tx_errors);

		AQ_SDELTA(ubrc, rx_unicast_octets);
		AQ_SDELTA(ubtc, tx_unicast_octets);
		AQ_SDELTA(mbrc, rx_multicast_octets);
		AQ_SDELTA(mbtc, tx_multicast_octets);
		AQ_SDELTA(bbrc, rx_broadcast_octets);
		AQ_SDELTA(bbtc, tx_broadcast_octets);
	}
#undef AQ_SDELTA
	self->curr_stats.dma_pkt_rc =
		hw_atl_stats_rx_dma_good_pkt_counter_get(self);
	self->curr_stats.dma_pkt_tc =
		hw_atl_stats_tx_dma_good_pkt_counter_get(self);
	self->curr_stats.dma_oct_rc =
		hw_atl_stats_rx_dma_good_octet_counter_get(self);
	self->curr_stats.dma_oct_tc =
		hw_atl_stats_tx_dma_good_octet_counter_get(self);
	self->curr_stats.dpc = hw_atl_rpb_rx_dma_drop_pkt_cnt_get(self);

	memcpy(&priv->last_stats, &stats, sizeof(stats));

	return 0;
}

static int aq_a2_fw_renegotiate(struct aq_hw_s *self)
{
	struct link_options_s link_options;
	int err;

	hw_atl2_shared_buffer_get(self, link_options, link_options);
	link_options.link_renegotiate = 1U;
	hw_atl2_shared_buffer_write(self, link_options, link_options);

	err = hw_atl2_shared_buffer_finish_ack(self);

	/* We should put renegotiate status back to zero
	 * after command completes
	 */
	link_options.link_renegotiate = 0U;
	hw_atl2_shared_buffer_write(self, link_options, link_options);

	return err;
}

u32 hw_atl2_utils_get_fw_version(struct aq_hw_s *self)
{
	struct version_s version;

	hw_atl2_shared_buffer_read_safe(self, version, &version);

	/* A2 FW version is stored in reverse order */
	return version.mac.major << 24 |
	       version.mac.minor << 16 |
	       version.mac.build;
}

int hw_atl2_utils_get_action_resolve_table_caps(struct aq_hw_s *self,
						u8 *base_index, u8 *count)
{
	struct filter_caps_s filter_caps;
	int err;

	err = hw_atl2_shared_buffer_read_safe(self, filter_caps, &filter_caps);
	if (err)
		return err;

	*base_index = filter_caps.rslv_tbl_base_index;
	*count = filter_caps.rslv_tbl_count;
	return 0;
}

const struct aq_fw_ops aq_a2_fw_ops = {
	.init               = aq_a2_fw_init,
	.deinit             = aq_a2_fw_deinit,
	.reset              = NULL,
	.renegotiate        = aq_a2_fw_renegotiate,
	.get_mac_permanent  = aq_a2_fw_get_mac_permanent,
	.set_link_speed     = aq_a2_fw_set_link_speed,
	.set_state          = aq_a2_fw_set_state,
	.update_link_status = aq_a2_fw_update_link_status,
	.update_stats       = aq_a2_fw_update_stats,
};
