// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
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
#include "ef100_netdev.h"

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

static int efx_ef100_init_datapath_caps(struct efx_nic *efx)
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

	if (efx_ef100_has_cap(nic_data->datapath_caps2, TX_TSO_V3))
		efx->net_dev->features |= NETIF_F_TSO | NETIF_F_TSO6;
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

static int ef100_ev_process(struct efx_channel *channel, int quota)
{
	struct efx_nic *efx = channel->efx;
	struct ef100_nic_data *nic_data;
	bool evq_phase, old_evq_phase;
	unsigned int read_ptr;
	efx_qword_t *p_event;
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
		case ESE_GZ_EF100_EV_MCDI:
			efx_mcdi_process_event(channel, p_event);
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

static int ef100_phy_probe(struct efx_nic *efx)
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

/*	NIC level access functions
 */
const struct efx_nic_type ef100_pf_nic_type = {
	.revision = EFX_REV_EF100,
	.is_vf = false,
	.probe = ef100_probe_pf,
	.mcdi_max_ver = 2,
	.mcdi_request = ef100_mcdi_request,
	.mcdi_poll_response = ef100_mcdi_poll_response,
	.mcdi_read_response = ef100_mcdi_read_response,
	.mcdi_poll_reboot = ef100_mcdi_poll_reboot,
	.mcdi_reboot_detected = ef100_mcdi_reboot_detected,
	.irq_enable_master = efx_port_dummy_op_void,
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
	.tx_probe = ef100_tx_probe,
	.tx_init = ef100_tx_init,
	.tx_write = ef100_tx_write,
	.tx_enqueue = ef100_enqueue_skb,
	.rx_probe = efx_mcdi_rx_probe,
	.rx_init = efx_mcdi_rx_init,
	.rx_remove = efx_mcdi_rx_remove,
	.rx_write = ef100_rx_write,
	.rx_packet = __ef100_rx_packet,

	.get_phys_port_id = efx_ef100_get_phys_port_id,

	.reconfigure_mac = ef100_reconfigure_mac,

	/* Per-type bar/size configuration not used on ef100. Location of
	 * registers is defined by extended capabilities.
	 */
	.mem_bar = NULL,
	.mem_map_size = NULL,

};

/*	NIC probe and remove
 */
static int ef100_probe_main(struct efx_nic *efx)
{
	unsigned int bar_size = resource_size(&efx->pci_dev->resource[efx->mem_bar]);
	struct net_device *net_dev = efx->net_dev;
	struct ef100_nic_data *nic_data;
	int i, rc;

	if (WARN_ON(bar_size == 0))
		return -EIO;

	nic_data = kzalloc(sizeof(*nic_data), GFP_KERNEL);
	if (!nic_data)
		return -ENOMEM;
	efx->nic_data = nic_data;
	nic_data->efx = efx;
	net_dev->features |= efx->type->offload_features;
	net_dev->hw_features |= efx->type->offload_features;

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
	if (!rc && efx->mcdi->fn_flags &
		   (1 << MC_CMD_DRV_ATTACH_EXT_OUT_FLAG_NO_ACTIVE_PORT)) {
		netif_info(efx, probe, efx->net_dev,
			   "No network port on this PCI function");
		rc = -ENODEV;
	}
	if (rc)
		goto fail;
	/* Reset (most) configuration for this function */
	rc = efx_mcdi_reset(efx, RESET_TYPE_ALL);
	if (rc)
		goto fail;

	rc = efx_ef100_init_datapath_caps(efx);
	if (rc < 0)
		goto fail;

	efx->max_vis = EF100_MAX_VIS;

	rc = efx_mcdi_port_get_number(efx);
	if (rc < 0)
		goto fail;
	efx->port_num = rc;

	rc = ef100_phy_probe(efx);
	if (rc)
		goto fail;

	rc = efx_init_channels(efx);
	if (rc)
		goto fail;

	rc = ef100_register_netdev(efx);
	if (rc)
		goto fail;

	return 0;
fail:
	return rc;
}

int ef100_probe_pf(struct efx_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	struct ef100_nic_data *nic_data;
	int rc = ef100_probe_main(efx);

	if (rc)
		goto fail;

	nic_data = efx->nic_data;
	rc = ef100_get_mac_address(efx, net_dev->perm_addr);
	if (rc)
		goto fail;
	/* Assign MAC address */
	memcpy(net_dev->dev_addr, net_dev->perm_addr, ETH_ALEN);
	memcpy(nic_data->port_id, net_dev->perm_addr, ETH_ALEN);

	return 0;

fail:
	return rc;
}

void ef100_remove(struct efx_nic *efx)
{
	struct ef100_nic_data *nic_data = efx->nic_data;

	ef100_unregister_netdev(efx);
	efx_fini_channels(efx);
	kfree(efx->phy_data);
	efx->phy_data = NULL;
	efx_mcdi_detach(efx);
	efx_mcdi_fini(efx);
	if (nic_data)
		efx_nic_free_buffer(efx, &nic_data->mcdi_buf);
	kfree(nic_data);
	efx->nic_data = NULL;
}
