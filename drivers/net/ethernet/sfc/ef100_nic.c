// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "ef100_nic.h"
#include "efx_common.h"
#include "efx_channels.h"
#include "io.h"
#include "selftest.h"
#include "ef100_regs.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "mcdi_port_common.h"
#include "mcdi_functions.h"
#include "mcdi_filters.h"
#include "ef100_rx.h"
#include "ef100_tx.h"
#include "ef100_sriov.h"
#include "ef100_netdev.h"
#include "tc.h"
#include "mae.h"
#include "rx_common.h"

#define EF100_MAX_VIS 4096
#define EF100_NUM_MCDI_BUFFERS	1
#define MCDI_BUF_LEN (8 + MCDI_CTL_SDU_LEN_MAX)

#define EF100_RESET_PORT ((ETH_RESET_MAC | ETH_RESET_PHY) << ETH_RESET_SHARED_SHIFT)

/*	MCDI
 */
static u8 *ef100_mcdi_buf(struct efx_nic *efx, u8 bufid, dma_addr_t *dma_addr)
{
	struct ef100_nic_data *nic_data = efx->nic_data;

	if (dma_addr)
		*dma_addr = nic_data->mcdi_buf.dma_addr +
			    bufid * ALIGN(MCDI_BUF_LEN, 256);
	return nic_data->mcdi_buf.addr + bufid * ALIGN(MCDI_BUF_LEN, 256);
}

static int ef100_get_warm_boot_count(struct efx_nic *efx)
{
	efx_dword_t reg;

	efx_readd(efx, &reg, efx_reg(efx, ER_GZ_MC_SFT_STATUS));

	if (EFX_DWORD_FIELD(reg, EFX_DWORD_0) == 0xffffffff) {
		netif_err(efx, hw, efx->net_dev, "Hardware unavailable\n");
		efx->state = STATE_DISABLED;
		return -ENETDOWN;
	} else {
		return EFX_DWORD_FIELD(reg, EFX_WORD_1) == 0xb007 ?
			EFX_DWORD_FIELD(reg, EFX_WORD_0) : -EIO;
	}
}

static void ef100_mcdi_request(struct efx_nic *efx,
			       const efx_dword_t *hdr, size_t hdr_len,
			       const efx_dword_t *sdu, size_t sdu_len)
{
	dma_addr_t dma_addr;
	u8 *pdu = ef100_mcdi_buf(efx, 0, &dma_addr);

	memcpy(pdu, hdr, hdr_len);
	memcpy(pdu + hdr_len, sdu, sdu_len);
	wmb();

	/* The hardware provides 'low' and 'high' (doorbell) registers
	 * for passing the 64-bit address of an MCDI request to
	 * firmware.  However the dwords are swapped by firmware.  The
	 * least significant bits of the doorbell are then 0 for all
	 * MCDI requests due to alignment.
	 */
	_efx_writed(efx, cpu_to_le32((u64)dma_addr >> 32),  efx_reg(efx, ER_GZ_MC_DB_LWRD));
	_efx_writed(efx, cpu_to_le32((u32)dma_addr),  efx_reg(efx, ER_GZ_MC_DB_HWRD));
}

static bool ef100_mcdi_poll_response(struct efx_nic *efx)
{
	const efx_dword_t hdr =
		*(const efx_dword_t *)(ef100_mcdi_buf(efx, 0, NULL));

	rmb();
	return EFX_DWORD_FIELD(hdr, MCDI_HEADER_RESPONSE);
}

static void ef100_mcdi_read_response(struct efx_nic *efx,
				     efx_dword_t *outbuf, size_t offset,
				     size_t outlen)
{
	const u8 *pdu = ef100_mcdi_buf(efx, 0, NULL);

	memcpy(outbuf, pdu + offset, outlen);
}

static int ef100_mcdi_poll_reboot(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = ef100_get_warm_boot_count(efx);
	if (rc < 0) {
		/* The firmware is presumably in the process of
		 * rebooting.  However, we are supposed to report each
		 * reboot just once, so we must only do that once we
		 * can read and store the updated warm boot count.
		 */
		return 0;
	}

	if (rc == nic_data->warm_boot_count)
		return 0;

	nic_data->warm_boot_count = rc;

	return -EIO;
}

static void ef100_mcdi_reboot_detected(struct efx_nic *efx)
{
}

/*	MCDI calls
 */
static int ef100_get_mac_address(struct efx_nic *efx, u8 *mac_address)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_MAC_ADDRESSES_OUT_LEN);
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_MAC_ADDRESSES_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_MAC_ADDRESSES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_GET_MAC_ADDRESSES_OUT_LEN)
		return -EIO;

	ether_addr_copy(mac_address,
			MCDI_PTR(outbuf, GET_MAC_ADDRESSES_OUT_MAC_ADDR_BASE));
	return 0;
}

int efx_ef100_init_datapath_caps(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_CAPABILITIES_V7_OUT_LEN);
	struct ef100_nic_data *nic_data = efx->nic_data;
	u8 vi_window_mode;
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_CAPABILITIES_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_CAPABILITIES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_GET_CAPABILITIES_V4_OUT_LEN) {
		netif_err(efx, drv, efx->net_dev,
			  "unable to read datapath firmware capabilities\n");
		return -EIO;
	}

	nic_data->datapath_caps = MCDI_DWORD(outbuf,
					     GET_CAPABILITIES_OUT_FLAGS1);
	nic_data->datapath_caps2 = MCDI_DWORD(outbuf,
					      GET_CAPABILITIES_V2_OUT_FLAGS2);
	if (outlen < MC_CMD_GET_CAPABILITIES_V7_OUT_LEN)
		nic_data->datapath_caps3 = 0;
	else
		nic_data->datapath_caps3 = MCDI_DWORD(outbuf,
						      GET_CAPABILITIES_V7_OUT_FLAGS3);

	vi_window_mode = MCDI_BYTE(outbuf,
				   GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE);
	rc = efx_mcdi_window_mode_to_stride(efx, vi_window_mode);
	if (rc)
		return rc;

	if (efx_ef100_has_cap(nic_data->datapath_caps2, TX_TSO_V3)) {
		struct net_device *net_dev = efx->net_dev;
		netdev_features_t tso = NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_GSO_PARTIAL |
					NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_UDP_TUNNEL_CSUM |
					NETIF_F_GSO_GRE | NETIF_F_GSO_GRE_CSUM;

		net_dev->features |= tso;
		net_dev->hw_features |= tso;
		net_dev->hw_enc_features |= tso;
		/* EF100 HW can only offload outer checksums if they are UDP,
		 * so for GRE_CSUM we have to use GSO_PARTIAL.
		 */
		net_dev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;
	}
	efx->num_mac_stats = MCDI_WORD(outbuf,
				       GET_CAPABILITIES_V4_OUT_MAC_STATS_NUM_STATS);
	netif_dbg(efx, probe, efx->net_dev,
		  "firmware reports num_mac_stats = %u\n",
		  efx->num_mac_stats);
	return 0;
}

/*	Event handling
 */
static int ef100_ev_probe(struct efx_channel *channel)
{
	/* Allocate an extra descriptor for the QMDA status completion entry */
	return efx_nic_alloc_buffer(channel->efx, &channel->eventq.buf,
				    (channel->eventq_mask + 2) *
				    sizeof(efx_qword_t),
				    GFP_KERNEL);
}

static int ef100_ev_init(struct efx_channel *channel)
{
	struct ef100_nic_data *nic_data = channel->efx->nic_data;

	/* initial phase is 0 */
	clear_bit(channel->channel, nic_data->evq_phases);

	return efx_mcdi_ev_init(channel, false, false);
}

static void ef100_ev_read_ack(struct efx_channel *channel)
{
	efx_dword_t evq_prime;

	EFX_POPULATE_DWORD_2(evq_prime,
			     ERF_GZ_EVQ_ID, channel->channel,
			     ERF_GZ_IDX, channel->eventq_read_ptr &
					 channel->eventq_mask);

	efx_writed(channel->efx, &evq_prime,
		   efx_reg(channel->efx, ER_GZ_EVQ_INT_PRIME));
}

#define EFX_NAPI_MAX_TX 512

static int ef100_ev_process(struct efx_channel *channel, int quota)
{
	struct efx_nic *efx = channel->efx;
	struct ef100_nic_data *nic_data;
	bool evq_phase, old_evq_phase;
	unsigned int read_ptr;
	efx_qword_t *p_event;
	int spent_tx = 0;
	int spent = 0;
	bool ev_phase;
	int ev_type;

	if (unlikely(!channel->enabled))
		return 0;

	nic_data = efx->nic_data;
	evq_phase = test_bit(channel->channel, nic_data->evq_phases);
	old_evq_phase = evq_phase;
	read_ptr = channel->eventq_read_ptr;
	BUILD_BUG_ON(ESF_GZ_EV_RXPKTS_PHASE_LBN != ESF_GZ_EV_TXCMPL_PHASE_LBN);

	while (spent < quota) {
		p_event = efx_event(channel, read_ptr);

		ev_phase = !!EFX_QWORD_FIELD(*p_event, ESF_GZ_EV_RXPKTS_PHASE);
		if (ev_phase != evq_phase)
			break;

		netif_vdbg(efx, drv, efx->net_dev,
			   "processing event on %d " EFX_QWORD_FMT "\n",
			   channel->channel, EFX_QWORD_VAL(*p_event));

		ev_type = EFX_QWORD_FIELD(*p_event, ESF_GZ_E_TYPE);

		switch (ev_type) {
		case ESE_GZ_EF100_EV_RX_PKTS:
			efx_ef100_ev_rx(channel, p_event);
			++spent;
			break;
		case ESE_GZ_EF100_EV_MCDI:
			efx_mcdi_process_event(channel, p_event);
			break;
		case ESE_GZ_EF100_EV_TX_COMPLETION:
			spent_tx += ef100_ev_tx(channel, p_event);
			if (spent_tx >= EFX_NAPI_MAX_TX)
				spent = quota;
			break;
		case ESE_GZ_EF100_EV_DRIVER:
			netif_info(efx, drv, efx->net_dev,
				   "Driver initiated event " EFX_QWORD_FMT "\n",
				   EFX_QWORD_VAL(*p_event));
			break;
		default:
			netif_info(efx, drv, efx->net_dev,
				   "Unhandled event " EFX_QWORD_FMT "\n",
				   EFX_QWORD_VAL(*p_event));
		}

		++read_ptr;
		if ((read_ptr & channel->eventq_mask) == 0)
			evq_phase = !evq_phase;
	}

	channel->eventq_read_ptr = read_ptr;
	if (evq_phase != old_evq_phase)
		change_bit(channel->channel, nic_data->evq_phases);

	return spent;
}

static irqreturn_t ef100_msi_interrupt(int irq, void *dev_id)
{
	struct efx_msi_context *context = dev_id;
	struct efx_nic *efx = context->efx;

	netif_vdbg(efx, intr, efx->net_dev,
		   "IRQ %d on CPU %d\n", irq, raw_smp_processor_id());

	if (likely(READ_ONCE(efx->irq_soft_enabled))) {
		/* Note test interrupts */
		if (context->index == efx->irq_level)
			efx->last_irq_cpu = raw_smp_processor_id();

		/* Schedule processing of the channel */
		efx_schedule_channel_irq(efx->channel[context->index]);
	}

	return IRQ_HANDLED;
}

int ef100_phy_probe(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_data;
	int rc;

	/* Probe for the PHY */
	efx->phy_data = kzalloc(sizeof(struct efx_mcdi_phy_data), GFP_KERNEL);
	if (!efx->phy_data)
		return -ENOMEM;

	rc = efx_mcdi_get_phy_cfg(efx, efx->phy_data);
	if (rc)
		return rc;

	/* Populate driver and ethtool settings */
	phy_data = efx->phy_data;
	mcdi_to_ethtool_linkset(phy_data->media, phy_data->supported_cap,
				efx->link_advertising);
	efx->fec_config = mcdi_fec_caps_to_ethtool(phy_data->supported_cap,
						   false);

	/* Default to Autonegotiated flow control if the PHY supports it */
	efx->wanted_fc = EFX_FC_RX | EFX_FC_TX;
	if (phy_data->supported_cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		efx->wanted_fc |= EFX_FC_AUTO;
	efx_link_set_wanted_fc(efx, efx->wanted_fc);

	/* Push settings to the PHY. Failure is not fatal, the user can try to
	 * fix it using ethtool.
	 */
	rc = efx_mcdi_port_reconfigure(efx);
	if (rc && rc != -EPERM)
		netif_warn(efx, drv, efx->net_dev,
			   "could not initialise PHY settings\n");

	return 0;
}

int ef100_filter_table_probe(struct efx_nic *efx)
{
	return efx_mcdi_filter_table_probe(efx, true);
}

static int ef100_filter_table_up(struct efx_nic *efx)
{
	int rc;

	down_write(&efx->filter_sem);
	rc = efx_mcdi_filter_add_vlan(efx, EFX_FILTER_VID_UNSPEC);
	if (rc)
		goto fail_unspec;

	rc = efx_mcdi_filter_add_vlan(efx, 0);
	if (rc)
		goto fail_vlan0;
	/* Drop the lock: we've finished altering table existence, and
	 * filter insertion will need to take the lock for read.
	 */
	up_write(&efx->filter_sem);
#ifdef CONFIG_SFC_SRIOV
	rc = efx_tc_insert_rep_filters(efx);
	/* Rep filter failure is nonfatal */
	if (rc)
		netif_warn(efx, drv, efx->net_dev,
			   "Failed to insert representor filters, rc %d\n",
			   rc);
#endif
	return 0;

fail_vlan0:
	efx_mcdi_filter_del_vlan(efx, EFX_FILTER_VID_UNSPEC);
fail_unspec:
	efx_mcdi_filter_table_down(efx);
	up_write(&efx->filter_sem);
	return rc;
}

static void ef100_filter_table_down(struct efx_nic *efx)
{
#ifdef CONFIG_SFC_SRIOV
	efx_tc_remove_rep_filters(efx);
#endif
	down_write(&efx->filter_sem);
	efx_mcdi_filter_del_vlan(efx, 0);
	efx_mcdi_filter_del_vlan(efx, EFX_FILTER_VID_UNSPEC);
	efx_mcdi_filter_table_down(efx);
	up_write(&efx->filter_sem);
}

/*	Other
 */
static int ef100_reconfigure_mac(struct efx_nic *efx, bool mtu_only)
{
	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	efx_mcdi_filter_sync_rx_mode(efx);

	if (mtu_only && efx_has_cap(efx, SET_MAC_ENHANCED))
		return efx_mcdi_set_mtu(efx);
	return efx_mcdi_set_mac(efx);
}

static enum reset_type ef100_map_reset_reason(enum reset_type reason)
{
	if (reason == RESET_TYPE_TX_WATCHDOG)
		return reason;
	return RESET_TYPE_DISABLE;
}

static int ef100_map_reset_flags(u32 *flags)
{
	/* Only perform a RESET_TYPE_ALL because we don't support MC_REBOOTs */
	if ((*flags & EF100_RESET_PORT)) {
		*flags &= ~EF100_RESET_PORT;
		return RESET_TYPE_ALL;
	}
	if (*flags & ETH_RESET_MGMT) {
		*flags &= ~ETH_RESET_MGMT;
		return RESET_TYPE_DISABLE;
	}

	return -EINVAL;
}

static int ef100_reset(struct efx_nic *efx, enum reset_type reset_type)
{
	int rc;

	dev_close(efx->net_dev);

	if (reset_type == RESET_TYPE_TX_WATCHDOG) {
		netif_device_attach(efx->net_dev);
		__clear_bit(reset_type, &efx->reset_pending);
		rc = dev_open(efx->net_dev, NULL);
	} else if (reset_type == RESET_TYPE_ALL) {
		rc = efx_mcdi_reset(efx, reset_type);
		if (rc)
			return rc;

		netif_device_attach(efx->net_dev);

		rc = dev_open(efx->net_dev, NULL);
	} else {
		rc = 1;	/* Leave the device closed */
	}
	return rc;
}

static void ef100_common_stat_mask(unsigned long *mask)
{
	__set_bit(EF100_STAT_port_rx_packets, mask);
	__set_bit(EF100_STAT_port_tx_packets, mask);
	__set_bit(EF100_STAT_port_rx_bytes, mask);
	__set_bit(EF100_STAT_port_tx_bytes, mask);
	__set_bit(EF100_STAT_port_rx_multicast, mask);
	__set_bit(EF100_STAT_port_rx_bad, mask);
	__set_bit(EF100_STAT_port_rx_align_error, mask);
	__set_bit(EF100_STAT_port_rx_overflow, mask);
}

static void ef100_ethtool_stat_mask(unsigned long *mask)
{
	__set_bit(EF100_STAT_port_tx_pause, mask);
	__set_bit(EF100_STAT_port_tx_unicast, mask);
	__set_bit(EF100_STAT_port_tx_multicast, mask);
	__set_bit(EF100_STAT_port_tx_broadcast, mask);
	__set_bit(EF100_STAT_port_tx_lt64, mask);
	__set_bit(EF100_STAT_port_tx_64, mask);
	__set_bit(EF100_STAT_port_tx_65_to_127, mask);
	__set_bit(EF100_STAT_port_tx_128_to_255, mask);
	__set_bit(EF100_STAT_port_tx_256_to_511, mask);
	__set_bit(EF100_STAT_port_tx_512_to_1023, mask);
	__set_bit(EF100_STAT_port_tx_1024_to_15xx, mask);
	__set_bit(EF100_STAT_port_tx_15xx_to_jumbo, mask);
	__set_bit(EF100_STAT_port_rx_good, mask);
	__set_bit(EF100_STAT_port_rx_pause, mask);
	__set_bit(EF100_STAT_port_rx_unicast, mask);
	__set_bit(EF100_STAT_port_rx_broadcast, mask);
	__set_bit(EF100_STAT_port_rx_lt64, mask);
	__set_bit(EF100_STAT_port_rx_64, mask);
	__set_bit(EF100_STAT_port_rx_65_to_127, mask);
	__set_bit(EF100_STAT_port_rx_128_to_255, mask);
	__set_bit(EF100_STAT_port_rx_256_to_511, mask);
	__set_bit(EF100_STAT_port_rx_512_to_1023, mask);
	__set_bit(EF100_STAT_port_rx_1024_to_15xx, mask);
	__set_bit(EF100_STAT_port_rx_15xx_to_jumbo, mask);
	__set_bit(EF100_STAT_port_rx_gtjumbo, mask);
	__set_bit(EF100_STAT_port_rx_bad_gtjumbo, mask);
	__set_bit(EF100_STAT_port_rx_length_error, mask);
	__set_bit(EF100_STAT_port_rx_nodesc_drops, mask);
	__set_bit(GENERIC_STAT_rx_nodesc_trunc, mask);
	__set_bit(GENERIC_STAT_rx_noskb_drops, mask);
}

#define EF100_DMA_STAT(ext_name, mcdi_name)			\
	[EF100_STAT_ ## ext_name] =				\
	{ #ext_name, 64, 8 * MC_CMD_MAC_ ## mcdi_name }

static const struct efx_hw_stat_desc ef100_stat_desc[EF100_STAT_COUNT] = {
	EF100_DMA_STAT(port_tx_bytes, TX_BYTES),
	EF100_DMA_STAT(port_tx_packets, TX_PKTS),
	EF100_DMA_STAT(port_tx_pause, TX_PAUSE_PKTS),
	EF100_DMA_STAT(port_tx_unicast, TX_UNICAST_PKTS),
	EF100_DMA_STAT(port_tx_multicast, TX_MULTICAST_PKTS),
	EF100_DMA_STAT(port_tx_broadcast, TX_BROADCAST_PKTS),
	EF100_DMA_STAT(port_tx_lt64, TX_LT64_PKTS),
	EF100_DMA_STAT(port_tx_64, TX_64_PKTS),
	EF100_DMA_STAT(port_tx_65_to_127, TX_65_TO_127_PKTS),
	EF100_DMA_STAT(port_tx_128_to_255, TX_128_TO_255_PKTS),
	EF100_DMA_STAT(port_tx_256_to_511, TX_256_TO_511_PKTS),
	EF100_DMA_STAT(port_tx_512_to_1023, TX_512_TO_1023_PKTS),
	EF100_DMA_STAT(port_tx_1024_to_15xx, TX_1024_TO_15XX_PKTS),
	EF100_DMA_STAT(port_tx_15xx_to_jumbo, TX_15XX_TO_JUMBO_PKTS),
	EF100_DMA_STAT(port_rx_bytes, RX_BYTES),
	EF100_DMA_STAT(port_rx_packets, RX_PKTS),
	EF100_DMA_STAT(port_rx_good, RX_GOOD_PKTS),
	EF100_DMA_STAT(port_rx_bad, RX_BAD_FCS_PKTS),
	EF100_DMA_STAT(port_rx_pause, RX_PAUSE_PKTS),
	EF100_DMA_STAT(port_rx_unicast, RX_UNICAST_PKTS),
	EF100_DMA_STAT(port_rx_multicast, RX_MULTICAST_PKTS),
	EF100_DMA_STAT(port_rx_broadcast, RX_BROADCAST_PKTS),
	EF100_DMA_STAT(port_rx_lt64, RX_UNDERSIZE_PKTS),
	EF100_DMA_STAT(port_rx_64, RX_64_PKTS),
	EF100_DMA_STAT(port_rx_65_to_127, RX_65_TO_127_PKTS),
	EF100_DMA_STAT(port_rx_128_to_255, RX_128_TO_255_PKTS),
	EF100_DMA_STAT(port_rx_256_to_511, RX_256_TO_511_PKTS),
	EF100_DMA_STAT(port_rx_512_to_1023, RX_512_TO_1023_PKTS),
	EF100_DMA_STAT(port_rx_1024_to_15xx, RX_1024_TO_15XX_PKTS),
	EF100_DMA_STAT(port_rx_15xx_to_jumbo, RX_15XX_TO_JUMBO_PKTS),
	EF100_DMA_STAT(port_rx_gtjumbo, RX_GTJUMBO_PKTS),
	EF100_DMA_STAT(port_rx_bad_gtjumbo, RX_JABBER_PKTS),
	EF100_DMA_STAT(port_rx_align_error, RX_ALIGN_ERROR_PKTS),
	EF100_DMA_STAT(port_rx_length_error, RX_LENGTH_ERROR_PKTS),
	EF100_DMA_STAT(port_rx_overflow, RX_OVERFLOW_PKTS),
	EF100_DMA_STAT(port_rx_nodesc_drops, RX_NODESC_DROPS),
	EFX_GENERIC_SW_STAT(rx_nodesc_trunc),
	EFX_GENERIC_SW_STAT(rx_noskb_drops),
};

static size_t ef100_describe_stats(struct efx_nic *efx, u8 *names)
{
	DECLARE_BITMAP(mask, EF100_STAT_COUNT) = {};

	ef100_ethtool_stat_mask(mask);
	return efx_nic_describe_stats(ef100_stat_desc, EF100_STAT_COUNT,
				      mask, names);
}

static size_t ef100_update_stats_common(struct efx_nic *efx, u64 *full_stats,
					struct rtnl_link_stats64 *core_stats)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	DECLARE_BITMAP(mask, EF100_STAT_COUNT) = {};
	size_t stats_count = 0, index;
	u64 *stats = nic_data->stats;

	ef100_ethtool_stat_mask(mask);

	if (full_stats) {
		for_each_set_bit(index, mask, EF100_STAT_COUNT) {
			if (ef100_stat_desc[index].name) {
				*full_stats++ = stats[index];
				++stats_count;
			}
		}
	}

	if (!core_stats)
		return stats_count;

	core_stats->rx_packets = stats[EF100_STAT_port_rx_packets];
	core_stats->tx_packets = stats[EF100_STAT_port_tx_packets];
	core_stats->rx_bytes = stats[EF100_STAT_port_rx_bytes];
	core_stats->tx_bytes = stats[EF100_STAT_port_tx_bytes];
	core_stats->rx_dropped = stats[EF100_STAT_port_rx_nodesc_drops] +
				 stats[GENERIC_STAT_rx_nodesc_trunc] +
				 stats[GENERIC_STAT_rx_noskb_drops];
	core_stats->multicast = stats[EF100_STAT_port_rx_multicast];
	core_stats->rx_length_errors =
			stats[EF100_STAT_port_rx_gtjumbo] +
			stats[EF100_STAT_port_rx_length_error];
	core_stats->rx_crc_errors = stats[EF100_STAT_port_rx_bad];
	core_stats->rx_frame_errors =
			stats[EF100_STAT_port_rx_align_error];
	core_stats->rx_fifo_errors = stats[EF100_STAT_port_rx_overflow];
	core_stats->rx_errors = (core_stats->rx_length_errors +
				 core_stats->rx_crc_errors +
				 core_stats->rx_frame_errors);

	return stats_count;
}

static size_t ef100_update_stats(struct efx_nic *efx,
				 u64 *full_stats,
				 struct rtnl_link_stats64 *core_stats)
{
	__le64 *mc_stats = kmalloc(array_size(efx->num_mac_stats, sizeof(__le64)), GFP_ATOMIC);
	struct ef100_nic_data *nic_data = efx->nic_data;
	DECLARE_BITMAP(mask, EF100_STAT_COUNT) = {};
	u64 *stats = nic_data->stats;

	ef100_common_stat_mask(mask);
	ef100_ethtool_stat_mask(mask);

	if (!mc_stats)
		return 0;

	efx_nic_copy_stats(efx, mc_stats);
	efx_nic_update_stats(ef100_stat_desc, EF100_STAT_COUNT, mask,
			     stats, mc_stats, false);

	kfree(mc_stats);

	return ef100_update_stats_common(efx, full_stats, core_stats);
}

static int efx_ef100_get_phys_port_id(struct efx_nic *efx,
				      struct netdev_phys_item_id *ppid)
{
	struct ef100_nic_data *nic_data = efx->nic_data;

	if (!is_valid_ether_addr(nic_data->port_id))
		return -EOPNOTSUPP;

	ppid->id_len = ETH_ALEN;
	memcpy(ppid->id, nic_data->port_id, ppid->id_len);

	return 0;
}

static int efx_ef100_irq_test_generate(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_TRIGGER_INTERRUPT_IN_LEN);

	BUILD_BUG_ON(MC_CMD_TRIGGER_INTERRUPT_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, TRIGGER_INTERRUPT_IN_INTR_LEVEL, efx->irq_level);
	return efx_mcdi_rpc_quiet(efx, MC_CMD_TRIGGER_INTERRUPT,
				  inbuf, sizeof(inbuf), NULL, 0, NULL);
}

#define EFX_EF100_TEST 1

static void efx_ef100_ev_test_generate(struct efx_channel *channel)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_DRIVER_EVENT_IN_LEN);
	struct efx_nic *efx = channel->efx;
	efx_qword_t event;
	int rc;

	EFX_POPULATE_QWORD_2(event,
			     ESF_GZ_E_TYPE, ESE_GZ_EF100_EV_DRIVER,
			     ESF_GZ_DRIVER_DATA, EFX_EF100_TEST);

	MCDI_SET_DWORD(inbuf, DRIVER_EVENT_IN_EVQ, channel->channel);

	/* MCDI_SET_QWORD is not appropriate here since EFX_POPULATE_* has
	 * already swapped the data to little-endian order.
	 */
	memcpy(MCDI_PTR(inbuf, DRIVER_EVENT_IN_DATA), &event.u64[0],
	       sizeof(efx_qword_t));

	rc = efx_mcdi_rpc(efx, MC_CMD_DRIVER_EVENT, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc && (rc != -ENETDOWN))
		goto fail;

	return;

fail:
	WARN_ON(true);
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static unsigned int ef100_check_caps(const struct efx_nic *efx,
				     u8 flag, u32 offset)
{
	const struct ef100_nic_data *nic_data = efx->nic_data;

	switch (offset) {
	case MC_CMD_GET_CAPABILITIES_V8_OUT_FLAGS1_OFST:
		return nic_data->datapath_caps & BIT_ULL(flag);
	case MC_CMD_GET_CAPABILITIES_V8_OUT_FLAGS2_OFST:
		return nic_data->datapath_caps2 & BIT_ULL(flag);
	case MC_CMD_GET_CAPABILITIES_V8_OUT_FLAGS3_OFST:
		return nic_data->datapath_caps3 & BIT_ULL(flag);
	default:
		return 0;
	}
}

static unsigned int efx_ef100_recycle_ring_size(const struct efx_nic *efx)
{
	/* Maximum link speed for Riverhead is 100G */
	return 10 * EFX_RECYCLE_RING_SIZE_10G;
}

#ifdef CONFIG_SFC_SRIOV
static int efx_ef100_get_base_mport(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	u32 selector, id;
	int rc;

	/* Construct mport selector for "physical network port" */
	efx_mae_mport_wire(efx, &selector);
	/* Look up actual mport ID */
	rc = efx_mae_lookup_mport(efx, selector, &id);
	if (rc)
		return rc;
	/* The ID should always fit in 16 bits, because that's how wide the
	 * corresponding fields in the RX prefix & TX override descriptor are
	 */
	if (id >> 16)
		netif_warn(efx, probe, efx->net_dev, "Bad base m-port id %#x\n",
			   id);
	nic_data->base_mport = id;
	nic_data->have_mport = true;
	return 0;
}
#endif

static int compare_versions(const char *a, const char *b)
{
	int a_major, a_minor, a_point, a_patch;
	int b_major, b_minor, b_point, b_patch;
	int a_matched, b_matched;

	a_matched = sscanf(a, "%d.%d.%d.%d", &a_major, &a_minor, &a_point, &a_patch);
	b_matched = sscanf(b, "%d.%d.%d.%d", &b_major, &b_minor, &b_point, &b_patch);

	if (a_matched == 4 && b_matched != 4)
		return +1;

	if (a_matched != 4 && b_matched == 4)
		return -1;

	if (a_matched != 4 && b_matched != 4)
		return 0;

	if (a_major != b_major)
		return a_major - b_major;

	if (a_minor != b_minor)
		return a_minor - b_minor;

	if (a_point != b_point)
		return a_point - b_point;

	return a_patch - b_patch;
}

enum ef100_tlv_state_machine {
	EF100_TLV_TYPE,
	EF100_TLV_TYPE_CONT,
	EF100_TLV_LENGTH,
	EF100_TLV_VALUE
};

struct ef100_tlv_state {
	enum ef100_tlv_state_machine state;
	u64 value;
	u32 value_offset;
	u16 type;
	u8 len;
};

static int ef100_tlv_feed(struct ef100_tlv_state *state, u8 byte)
{
	switch (state->state) {
	case EF100_TLV_TYPE:
		state->type = byte & 0x7f;
		state->state = (byte & 0x80) ? EF100_TLV_TYPE_CONT
					     : EF100_TLV_LENGTH;
		/* Clear ready to read in a new entry */
		state->value = 0;
		state->value_offset = 0;
		return 0;
	case EF100_TLV_TYPE_CONT:
		state->type |= byte << 7;
		state->state = EF100_TLV_LENGTH;
		return 0;
	case EF100_TLV_LENGTH:
		state->len = byte;
		/* We only handle TLVs that fit in a u64 */
		if (state->len > sizeof(state->value))
			return -EOPNOTSUPP;
		/* len may be zero, implying a value of zero */
		state->state = state->len ? EF100_TLV_VALUE : EF100_TLV_TYPE;
		return 0;
	case EF100_TLV_VALUE:
		state->value |= ((u64)byte) << (state->value_offset * 8);
		state->value_offset++;
		if (state->value_offset >= state->len)
			state->state = EF100_TLV_TYPE;
		return 0;
	default: /* state machine error, can't happen */
		WARN_ON_ONCE(1);
		return -EIO;
	}
}

static int ef100_process_design_param(struct efx_nic *efx,
				      const struct ef100_tlv_state *reader)
{
	struct ef100_nic_data *nic_data = efx->nic_data;

	switch (reader->type) {
	case ESE_EF100_DP_GZ_PAD: /* padding, skip it */
		return 0;
	case ESE_EF100_DP_GZ_PARTIAL_TSTAMP_SUB_NANO_BITS:
		/* Driver doesn't support timestamping yet, so we don't care */
		return 0;
	case ESE_EF100_DP_GZ_EVQ_UNSOL_CREDIT_SEQ_BITS:
		/* Driver doesn't support unsolicited-event credits yet, so
		 * we don't care
		 */
		return 0;
	case ESE_EF100_DP_GZ_NMMU_GROUP_SIZE:
		/* Driver doesn't manage the NMMU (so we don't care) */
		return 0;
	case ESE_EF100_DP_GZ_RX_L4_CSUM_PROTOCOLS:
		/* Driver uses CHECKSUM_COMPLETE, so we don't care about
		 * protocol checksum validation
		 */
		return 0;
	case ESE_EF100_DP_GZ_TSO_MAX_HDR_LEN:
		nic_data->tso_max_hdr_len = min_t(u64, reader->value, 0xffff);
		return 0;
	case ESE_EF100_DP_GZ_TSO_MAX_HDR_NUM_SEGS:
		/* We always put HDR_NUM_SEGS=1 in our TSO descriptors */
		if (!reader->value) {
			netif_err(efx, probe, efx->net_dev,
				  "TSO_MAX_HDR_NUM_SEGS < 1\n");
			return -EOPNOTSUPP;
		}
		return 0;
	case ESE_EF100_DP_GZ_RXQ_SIZE_GRANULARITY:
	case ESE_EF100_DP_GZ_TXQ_SIZE_GRANULARITY:
		/* Our TXQ and RXQ sizes are always power-of-two and thus divisible by
		 * EFX_MIN_DMAQ_SIZE, so we just need to check that
		 * EFX_MIN_DMAQ_SIZE is divisible by GRANULARITY.
		 * This is very unlikely to fail.
		 */
		if (!reader->value || reader->value > EFX_MIN_DMAQ_SIZE ||
		    EFX_MIN_DMAQ_SIZE % (u32)reader->value) {
			netif_err(efx, probe, efx->net_dev,
				  "%s size granularity is %llu, can't guarantee safety\n",
				  reader->type == ESE_EF100_DP_GZ_RXQ_SIZE_GRANULARITY ? "RXQ" : "TXQ",
				  reader->value);
			return -EOPNOTSUPP;
		}
		return 0;
	case ESE_EF100_DP_GZ_TSO_MAX_PAYLOAD_LEN:
		nic_data->tso_max_payload_len = min_t(u64, reader->value,
						      GSO_LEGACY_MAX_SIZE);
		netif_set_tso_max_size(efx->net_dev,
				       nic_data->tso_max_payload_len);
		return 0;
	case ESE_EF100_DP_GZ_TSO_MAX_PAYLOAD_NUM_SEGS:
		nic_data->tso_max_payload_num_segs = min_t(u64, reader->value, 0xffff);
		netif_set_tso_max_segs(efx->net_dev,
				       nic_data->tso_max_payload_num_segs);
		return 0;
	case ESE_EF100_DP_GZ_TSO_MAX_NUM_FRAMES:
		nic_data->tso_max_frames = min_t(u64, reader->value, 0xffff);
		return 0;
	case ESE_EF100_DP_GZ_COMPAT:
		if (reader->value) {
			netif_err(efx, probe, efx->net_dev,
				  "DP_COMPAT has unknown bits %#llx, driver not compatible with this hw\n",
				  reader->value);
			return -EOPNOTSUPP;
		}
		return 0;
	case ESE_EF100_DP_GZ_MEM2MEM_MAX_LEN:
		/* Driver doesn't use mem2mem transfers */
		return 0;
	case ESE_EF100_DP_GZ_EVQ_TIMER_TICK_NANOS:
		/* Driver doesn't currently use EVQ_TIMER */
		return 0;
	case ESE_EF100_DP_GZ_NMMU_PAGE_SIZES:
		/* Driver doesn't manage the NMMU (so we don't care) */
		return 0;
	case ESE_EF100_DP_GZ_VI_STRIDES:
		/* We never try to set the VI stride, and we don't rely on
		 * being able to find VIs past VI 0 until after we've learned
		 * the current stride from MC_CMD_GET_CAPABILITIES.
		 * So the value of this shouldn't matter.
		 */
		if (reader->value != ESE_EF100_DP_GZ_VI_STRIDES_DEFAULT)
			netif_dbg(efx, probe, efx->net_dev,
				  "NIC has other than default VI_STRIDES (mask "
				  "%#llx), early probing might use wrong one\n",
				  reader->value);
		return 0;
	case ESE_EF100_DP_GZ_RX_MAX_RUNT:
		/* Driver doesn't look at L2_STATUS:LEN_ERR bit, so we don't
		 * care whether it indicates runt or overlength for any given
		 * packet, so we don't care about this parameter.
		 */
		return 0;
	default:
		/* Host interface says "Drivers should ignore design parameters
		 * that they do not recognise."
		 */
		netif_dbg(efx, probe, efx->net_dev,
			  "Ignoring unrecognised design parameter %u\n",
			  reader->type);
		return 0;
	}
}

static int ef100_check_design_params(struct efx_nic *efx)
{
	struct ef100_tlv_state reader = {};
	u32 total_len, offset = 0;
	efx_dword_t reg;
	int rc = 0, i;
	u32 data;

	efx_readd(efx, &reg, ER_GZ_PARAMS_TLV_LEN);
	total_len = EFX_DWORD_FIELD(reg, EFX_DWORD_0);
	pci_dbg(efx->pci_dev, "%u bytes of design parameters\n", total_len);
	while (offset < total_len) {
		efx_readd(efx, &reg, ER_GZ_PARAMS_TLV + offset);
		data = EFX_DWORD_FIELD(reg, EFX_DWORD_0);
		for (i = 0; i < sizeof(data); i++) {
			rc = ef100_tlv_feed(&reader, data);
			/* Got a complete value? */
			if (!rc && reader.state == EF100_TLV_TYPE)
				rc = ef100_process_design_param(efx, &reader);
			if (rc)
				goto out;
			data >>= 8;
			offset++;
		}
	}
	/* Check we didn't end halfway through a TLV entry, which could either
	 * mean that the TLV stream is truncated or just that it's corrupted
	 * and our state machine is out of sync.
	 */
	if (reader.state != EF100_TLV_TYPE) {
		if (reader.state == EF100_TLV_TYPE_CONT)
			netif_err(efx, probe, efx->net_dev,
				  "truncated design parameter (incomplete type %u)\n",
				  reader.type);
		else
			netif_err(efx, probe, efx->net_dev,
				  "truncated design parameter %u\n",
				  reader.type);
		rc = -EIO;
	}
out:
	return rc;
}

/*	NIC probe and remove
 */
static int ef100_probe_main(struct efx_nic *efx)
{
	unsigned int bar_size = resource_size(&efx->pci_dev->resource[efx->mem_bar]);
	struct ef100_nic_data *nic_data;
	char fw_version[32];
	u32 priv_mask = 0;
	int i, rc;

	if (WARN_ON(bar_size == 0))
		return -EIO;

	nic_data = kzalloc(sizeof(*nic_data), GFP_KERNEL);
	if (!nic_data)
		return -ENOMEM;
	efx->nic_data = nic_data;
	nic_data->efx = efx;
	efx->max_vis = EF100_MAX_VIS;

	/* Populate design-parameter defaults */
	nic_data->tso_max_hdr_len = ESE_EF100_DP_GZ_TSO_MAX_HDR_LEN_DEFAULT;
	nic_data->tso_max_frames = ESE_EF100_DP_GZ_TSO_MAX_NUM_FRAMES_DEFAULT;
	nic_data->tso_max_payload_num_segs = ESE_EF100_DP_GZ_TSO_MAX_PAYLOAD_NUM_SEGS_DEFAULT;
	nic_data->tso_max_payload_len = ESE_EF100_DP_GZ_TSO_MAX_PAYLOAD_LEN_DEFAULT;

	/* Read design parameters */
	rc = ef100_check_design_params(efx);
	if (rc) {
		pci_err(efx->pci_dev, "Unsupported design parameters\n");
		goto fail;
	}

	/* we assume later that we can copy from this buffer in dwords */
	BUILD_BUG_ON(MCDI_CTL_SDU_LEN_MAX_V2 % 4);

	/* MCDI buffers must be 256 byte aligned. */
	rc = efx_nic_alloc_buffer(efx, &nic_data->mcdi_buf, MCDI_BUF_LEN,
				  GFP_KERNEL);
	if (rc)
		goto fail;

	/* Get the MC's warm boot count.  In case it's rebooting right
	 * now, be prepared to retry.
	 */
	i = 0;
	for (;;) {
		rc = ef100_get_warm_boot_count(efx);
		if (rc >= 0)
			break;
		if (++i == 5)
			goto fail;
		ssleep(1);
	}
	nic_data->warm_boot_count = rc;

	/* In case we're recovering from a crash (kexec), we want to
	 * cancel any outstanding request by the previous user of this
	 * function.  We send a special message using the least
	 * significant bits of the 'high' (doorbell) register.
	 */
	_efx_writed(efx, cpu_to_le32(1), efx_reg(efx, ER_GZ_MC_DB_HWRD));

	/* Post-IO section. */

	rc = efx_mcdi_init(efx);
	if (rc)
		goto fail;
	/* Reset (most) configuration for this function */
	rc = efx_mcdi_reset(efx, RESET_TYPE_ALL);
	if (rc)
		goto fail;
	/* Enable event logging */
	rc = efx_mcdi_log_ctrl(efx, true, false, 0);
	if (rc)
		goto fail;

	rc = efx_get_pf_index(efx, &nic_data->pf_index);
	if (rc)
		goto fail;

	rc = efx_mcdi_port_get_number(efx);
	if (rc < 0)
		goto fail;
	efx->port_num = rc;

	efx_mcdi_print_fwver(efx, fw_version, sizeof(fw_version));
	pci_dbg(efx->pci_dev, "Firmware version %s\n", fw_version);

	rc = efx_mcdi_get_privilege_mask(efx, &priv_mask);
	if (rc) /* non-fatal, and priv_mask will still be 0 */
		pci_info(efx->pci_dev,
			 "Failed to get privilege mask from FW, rc %d\n", rc);
	nic_data->grp_mae = !!(priv_mask & MC_CMD_PRIVILEGE_MASK_IN_GRP_MAE);

	if (compare_versions(fw_version, "1.1.0.1000") < 0) {
		pci_info(efx->pci_dev, "Firmware uses old event descriptors\n");
		rc = -EINVAL;
		goto fail;
	}

	if (efx_has_cap(efx, UNSOL_EV_CREDIT_SUPPORTED)) {
		pci_info(efx->pci_dev, "Firmware uses unsolicited-event credits\n");
		rc = -EINVAL;
		goto fail;
	}

	return 0;
fail:
	return rc;
}

int ef100_probe_netdev_pf(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;
	struct net_device *net_dev = efx->net_dev;
	int rc;

	rc = ef100_get_mac_address(efx, net_dev->perm_addr);
	if (rc)
		goto fail;
	/* Assign MAC address */
	eth_hw_addr_set(net_dev, net_dev->perm_addr);
	memcpy(nic_data->port_id, net_dev->perm_addr, ETH_ALEN);

	if (!nic_data->grp_mae)
		return 0;

#ifdef CONFIG_SFC_SRIOV
	rc = efx_init_struct_tc(efx);
	if (rc)
		return rc;

	rc = efx_ef100_get_base_mport(efx);
	if (rc) {
		netif_warn(efx, probe, net_dev,
			   "Failed to probe base mport rc %d; representors will not function\n",
			   rc);
	}

	rc = efx_init_tc(efx);
	if (rc) {
		/* Either we don't have an MAE at all (i.e. legacy v-switching),
		 * or we do but we failed to probe it.  In the latter case, we
		 * may not have set up default rules, in which case we won't be
		 * able to pass any traffic.  However, we don't fail the probe,
		 * because the user might need to use the netdevice to apply
		 * configuration changes to fix whatever's wrong with the MAE.
		 */
		netif_warn(efx, probe, net_dev, "Failed to probe MAE rc %d\n",
			   rc);
	} else {
		net_dev->features |= NETIF_F_HW_TC;
		efx->fixed_features |= NETIF_F_HW_TC;
	}
#endif
	return 0;

fail:
	return rc;
}

int ef100_probe_vf(struct efx_nic *efx)
{
	return ef100_probe_main(efx);
}

void ef100_remove(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;

	efx_mcdi_detach(efx);
	efx_mcdi_fini(efx);
	if (nic_data)
		efx_nic_free_buffer(efx, &nic_data->mcdi_buf);
	kfree(nic_data);
	efx->nic_data = NULL;
}

/*	NIC level access functions
 */
#define EF100_OFFLOAD_FEATURES	(NETIF_F_HW_CSUM | NETIF_F_RXCSUM |	\
	NETIF_F_HIGHDMA | NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_NTUPLE | \
	NETIF_F_RXHASH | NETIF_F_RXFCS | NETIF_F_TSO_ECN | NETIF_F_RXALL | \
	NETIF_F_HW_VLAN_CTAG_TX)

const struct efx_nic_type ef100_pf_nic_type = {
	.revision = EFX_REV_EF100,
	.is_vf = false,
	.probe = ef100_probe_main,
	.offload_features = EF100_OFFLOAD_FEATURES,
	.mcdi_max_ver = 2,
	.mcdi_request = ef100_mcdi_request,
	.mcdi_poll_response = ef100_mcdi_poll_response,
	.mcdi_read_response = ef100_mcdi_read_response,
	.mcdi_poll_reboot = ef100_mcdi_poll_reboot,
	.mcdi_reboot_detected = ef100_mcdi_reboot_detected,
	.irq_enable_master = efx_port_dummy_op_void,
	.irq_test_generate = efx_ef100_irq_test_generate,
	.irq_disable_non_ev = efx_port_dummy_op_void,
	.push_irq_moderation = efx_channel_dummy_op_void,
	.min_interrupt_mode = EFX_INT_MODE_MSIX,
	.map_reset_reason = ef100_map_reset_reason,
	.map_reset_flags = ef100_map_reset_flags,
	.reset = ef100_reset,

	.check_caps = ef100_check_caps,

	.ev_probe = ef100_ev_probe,
	.ev_init = ef100_ev_init,
	.ev_fini = efx_mcdi_ev_fini,
	.ev_remove = efx_mcdi_ev_remove,
	.irq_handle_msi = ef100_msi_interrupt,
	.ev_process = ef100_ev_process,
	.ev_read_ack = ef100_ev_read_ack,
	.ev_test_generate = efx_ef100_ev_test_generate,
	.tx_probe = ef100_tx_probe,
	.tx_init = ef100_tx_init,
	.tx_write = ef100_tx_write,
	.tx_enqueue = ef100_enqueue_skb,
	.rx_probe = efx_mcdi_rx_probe,
	.rx_init = efx_mcdi_rx_init,
	.rx_remove = efx_mcdi_rx_remove,
	.rx_write = ef100_rx_write,
	.rx_packet = __ef100_rx_packet,
	.rx_buf_hash_valid = ef100_rx_buf_hash_valid,
	.fini_dmaq = efx_fini_dmaq,
	.max_rx_ip_filters = EFX_MCDI_FILTER_TBL_ROWS,
	.filter_table_probe = ef100_filter_table_up,
	.filter_table_restore = efx_mcdi_filter_table_restore,
	.filter_table_remove = ef100_filter_table_down,
	.filter_insert = efx_mcdi_filter_insert,
	.filter_remove_safe = efx_mcdi_filter_remove_safe,
	.filter_get_safe = efx_mcdi_filter_get_safe,
	.filter_clear_rx = efx_mcdi_filter_clear_rx,
	.filter_count_rx_used = efx_mcdi_filter_count_rx_used,
	.filter_get_rx_id_limit = efx_mcdi_filter_get_rx_id_limit,
	.filter_get_rx_ids = efx_mcdi_filter_get_rx_ids,
#ifdef CONFIG_RFS_ACCEL
	.filter_rfs_expire_one = efx_mcdi_filter_rfs_expire_one,
#endif

	.get_phys_port_id = efx_ef100_get_phys_port_id,

	.rx_prefix_size = ESE_GZ_RX_PKT_PREFIX_LEN,
	.rx_hash_offset = ESF_GZ_RX_PREFIX_RSS_HASH_LBN / 8,
	.rx_ts_offset = ESF_GZ_RX_PREFIX_PARTIAL_TSTAMP_LBN / 8,
	.rx_hash_key_size = 40,
	.rx_pull_rss_config = efx_mcdi_rx_pull_rss_config,
	.rx_push_rss_config = efx_mcdi_pf_rx_push_rss_config,
	.rx_push_rss_context_config = efx_mcdi_rx_push_rss_context_config,
	.rx_pull_rss_context_config = efx_mcdi_rx_pull_rss_context_config,
	.rx_restore_rss_contexts = efx_mcdi_rx_restore_rss_contexts,
	.rx_recycle_ring_size = efx_ef100_recycle_ring_size,

	.reconfigure_mac = ef100_reconfigure_mac,
	.reconfigure_port = efx_mcdi_port_reconfigure,
	.test_nvram = efx_new_mcdi_nvram_test_all,
	.describe_stats = ef100_describe_stats,
	.start_stats = efx_mcdi_mac_start_stats,
	.update_stats = ef100_update_stats,
	.pull_stats = efx_mcdi_mac_pull_stats,
	.stop_stats = efx_mcdi_mac_stop_stats,
#ifdef CONFIG_SFC_SRIOV
	.sriov_configure = efx_ef100_sriov_configure,
#endif

	/* Per-type bar/size configuration not used on ef100. Location of
	 * registers is defined by extended capabilities.
	 */
	.mem_bar = NULL,
	.mem_map_size = NULL,

};

const struct efx_nic_type ef100_vf_nic_type = {
	.revision = EFX_REV_EF100,
	.is_vf = true,
	.probe = ef100_probe_vf,
	.offload_features = EF100_OFFLOAD_FEATURES,
	.mcdi_max_ver = 2,
	.mcdi_request = ef100_mcdi_request,
	.mcdi_poll_response = ef100_mcdi_poll_response,
	.mcdi_read_response = ef100_mcdi_read_response,
	.mcdi_poll_reboot = ef100_mcdi_poll_reboot,
	.mcdi_reboot_detected = ef100_mcdi_reboot_detected,
	.irq_enable_master = efx_port_dummy_op_void,
	.irq_test_generate = efx_ef100_irq_test_generate,
	.irq_disable_non_ev = efx_port_dummy_op_void,
	.push_irq_moderation = efx_channel_dummy_op_void,
	.min_interrupt_mode = EFX_INT_MODE_MSIX,
	.map_reset_reason = ef100_map_reset_reason,
	.map_reset_flags = ef100_map_reset_flags,
	.reset = ef100_reset,
	.check_caps = ef100_check_caps,
	.ev_probe = ef100_ev_probe,
	.ev_init = ef100_ev_init,
	.ev_fini = efx_mcdi_ev_fini,
	.ev_remove = efx_mcdi_ev_remove,
	.irq_handle_msi = ef100_msi_interrupt,
	.ev_process = ef100_ev_process,
	.ev_read_ack = ef100_ev_read_ack,
	.ev_test_generate = efx_ef100_ev_test_generate,
	.tx_probe = ef100_tx_probe,
	.tx_init = ef100_tx_init,
	.tx_write = ef100_tx_write,
	.tx_enqueue = ef100_enqueue_skb,
	.rx_probe = efx_mcdi_rx_probe,
	.rx_init = efx_mcdi_rx_init,
	.rx_remove = efx_mcdi_rx_remove,
	.rx_write = ef100_rx_write,
	.rx_packet = __ef100_rx_packet,
	.rx_buf_hash_valid = ef100_rx_buf_hash_valid,
	.fini_dmaq = efx_fini_dmaq,
	.max_rx_ip_filters = EFX_MCDI_FILTER_TBL_ROWS,
	.filter_table_probe = ef100_filter_table_up,
	.filter_table_restore = efx_mcdi_filter_table_restore,
	.filter_table_remove = ef100_filter_table_down,
	.filter_insert = efx_mcdi_filter_insert,
	.filter_remove_safe = efx_mcdi_filter_remove_safe,
	.filter_get_safe = efx_mcdi_filter_get_safe,
	.filter_clear_rx = efx_mcdi_filter_clear_rx,
	.filter_count_rx_used = efx_mcdi_filter_count_rx_used,
	.filter_get_rx_id_limit = efx_mcdi_filter_get_rx_id_limit,
	.filter_get_rx_ids = efx_mcdi_filter_get_rx_ids,
#ifdef CONFIG_RFS_ACCEL
	.filter_rfs_expire_one = efx_mcdi_filter_rfs_expire_one,
#endif

	.rx_prefix_size = ESE_GZ_RX_PKT_PREFIX_LEN,
	.rx_hash_offset = ESF_GZ_RX_PREFIX_RSS_HASH_LBN / 8,
	.rx_ts_offset = ESF_GZ_RX_PREFIX_PARTIAL_TSTAMP_LBN / 8,
	.rx_hash_key_size = 40,
	.rx_pull_rss_config = efx_mcdi_rx_pull_rss_config,
	.rx_push_rss_config = efx_mcdi_pf_rx_push_rss_config,
	.rx_restore_rss_contexts = efx_mcdi_rx_restore_rss_contexts,
	.rx_recycle_ring_size = efx_ef100_recycle_ring_size,

	.reconfigure_mac = ef100_reconfigure_mac,
	.test_nvram = efx_new_mcdi_nvram_test_all,
	.describe_stats = ef100_describe_stats,
	.start_stats = efx_mcdi_mac_start_stats,
	.update_stats = ef100_update_stats,
	.pull_stats = efx_mcdi_mac_pull_stats,
	.stop_stats = efx_mcdi_mac_stop_stats,

	.mem_bar = NULL,
	.mem_map_size = NULL,

};
