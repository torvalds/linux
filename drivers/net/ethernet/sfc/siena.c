/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/random.h>
#include "net_driver.h"
#include "bitfield.h"
#include "efx.h"
#include "nic.h"
#include "spi.h"
#include "farch_regs.h"
#include "io.h"
#include "phy.h"
#include "workarounds.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "selftest.h"

/* Hardware control for SFC9000 family including SFL9021 (aka Siena). */

static void siena_init_wol(struct efx_nic *efx);


static void siena_push_irq_moderation(struct efx_channel *channel)
{
	efx_dword_t timer_cmd;

	if (channel->irq_moderation)
		EFX_POPULATE_DWORD_2(timer_cmd,
				     FRF_CZ_TC_TIMER_MODE,
				     FFE_CZ_TIMER_MODE_INT_HLDOFF,
				     FRF_CZ_TC_TIMER_VAL,
				     channel->irq_moderation - 1);
	else
		EFX_POPULATE_DWORD_2(timer_cmd,
				     FRF_CZ_TC_TIMER_MODE,
				     FFE_CZ_TIMER_MODE_DIS,
				     FRF_CZ_TC_TIMER_VAL, 0);
	efx_writed_page_locked(channel->efx, &timer_cmd, FR_BZ_TIMER_COMMAND_P0,
			       channel->channel);
}

void siena_prepare_flush(struct efx_nic *efx)
{
	if (efx->fc_disable++ == 0)
		efx_mcdi_set_mac(efx);
}

void siena_finish_flush(struct efx_nic *efx)
{
	if (--efx->fc_disable == 0)
		efx_mcdi_set_mac(efx);
}

static const struct efx_farch_register_test siena_register_tests[] = {
	{ FR_AZ_ADR_REGION,
	  EFX_OWORD32(0x0003FFFF, 0x0003FFFF, 0x0003FFFF, 0x0003FFFF) },
	{ FR_CZ_USR_EV_CFG,
	  EFX_OWORD32(0x000103FF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_RX_CFG,
	  EFX_OWORD32(0xFFFFFFFE, 0xFFFFFFFF, 0x0003FFFF, 0x00000000) },
	{ FR_AZ_TX_CFG,
	  EFX_OWORD32(0x7FFF0037, 0xFFFF8000, 0xFFFFFFFF, 0x03FFFFFF) },
	{ FR_AZ_TX_RESERVED,
	  EFX_OWORD32(0xFFFEFE80, 0x1FFFFFFF, 0x020000FE, 0x007FFFFF) },
	{ FR_AZ_SRM_TX_DC_CFG,
	  EFX_OWORD32(0x001FFFFF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_RX_DC_CFG,
	  EFX_OWORD32(0x00000003, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_RX_DC_PF_WM,
	  EFX_OWORD32(0x000003FF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_BZ_DP_CTRL,
	  EFX_OWORD32(0x00000FFF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_BZ_RX_RSS_TKEY,
	  EFX_OWORD32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF) },
	{ FR_CZ_RX_RSS_IPV6_REG1,
	  EFX_OWORD32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF) },
	{ FR_CZ_RX_RSS_IPV6_REG2,
	  EFX_OWORD32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF) },
	{ FR_CZ_RX_RSS_IPV6_REG3,
	  EFX_OWORD32(0xFFFFFFFF, 0xFFFFFFFF, 0x00000007, 0x00000000) },
};

static int siena_test_chip(struct efx_nic *efx, struct efx_self_tests *tests)
{
	enum reset_type reset_method = RESET_TYPE_ALL;
	int rc, rc2;

	efx_reset_down(efx, reset_method);

	/* Reset the chip immediately so that it is completely
	 * quiescent regardless of what any VF driver does.
	 */
	rc = efx_mcdi_reset(efx, reset_method);
	if (rc)
		goto out;

	tests->registers =
		efx_farch_test_registers(efx, siena_register_tests,
					 ARRAY_SIZE(siena_register_tests))
		? -1 : 1;

	rc = efx_mcdi_reset(efx, reset_method);
out:
	rc2 = efx_reset_up(efx, reset_method, rc == 0);
	return rc ? rc : rc2;
}

/**************************************************************************
 *
 * Device reset
 *
 **************************************************************************
 */

static int siena_map_reset_flags(u32 *flags)
{
	enum {
		SIENA_RESET_PORT = (ETH_RESET_DMA | ETH_RESET_FILTER |
				    ETH_RESET_OFFLOAD | ETH_RESET_MAC |
				    ETH_RESET_PHY),
		SIENA_RESET_MC = (SIENA_RESET_PORT |
				  ETH_RESET_MGMT << ETH_RESET_SHARED_SHIFT),
	};

	if ((*flags & SIENA_RESET_MC) == SIENA_RESET_MC) {
		*flags &= ~SIENA_RESET_MC;
		return RESET_TYPE_WORLD;
	}

	if ((*flags & SIENA_RESET_PORT) == SIENA_RESET_PORT) {
		*flags &= ~SIENA_RESET_PORT;
		return RESET_TYPE_ALL;
	}

	/* no invisible reset implemented */

	return -EINVAL;
}

#ifdef CONFIG_EEH
/* When a PCI device is isolated from the bus, a subsequent MMIO read is
 * required for the kernel EEH mechanisms to notice. As the Solarflare driver
 * was written to minimise MMIO read (for latency) then a periodic call to check
 * the EEH status of the device is required so that device recovery can happen
 * in a timely fashion.
 */
static void siena_monitor(struct efx_nic *efx)
{
	struct eeh_dev *eehdev =
		of_node_to_eeh_dev(pci_device_to_OF_node(efx->pci_dev));

	eeh_dev_check_failure(eehdev);
}
#endif

static int siena_probe_nvconfig(struct efx_nic *efx)
{
	u32 caps = 0;
	int rc;

	rc = efx_mcdi_get_board_cfg(efx, efx->net_dev->perm_addr, NULL, &caps);

	efx->timer_quantum_ns =
		(caps & (1 << MC_CMD_CAPABILITIES_TURBO_ACTIVE_LBN)) ?
		3072 : 6144; /* 768 cycles */
	return rc;
}

static void siena_dimension_resources(struct efx_nic *efx)
{
	/* Each port has a small block of internal SRAM dedicated to
	 * the buffer table and descriptor caches.  In theory we can
	 * map both blocks to one port, but we don't.
	 */
	efx_farch_dimension_resources(efx, FR_CZ_BUF_FULL_TBL_ROWS / 2);
}

static unsigned int siena_mem_map_size(struct efx_nic *efx)
{
	return FR_CZ_MC_TREG_SMEM +
		FR_CZ_MC_TREG_SMEM_STEP * FR_CZ_MC_TREG_SMEM_ROWS;
}

static int siena_probe_nic(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data;
	bool already_attached = false;
	efx_oword_t reg;
	int rc;

	/* Allocate storage for hardware specific data */
	nic_data = kzalloc(sizeof(struct siena_nic_data), GFP_KERNEL);
	if (!nic_data)
		return -ENOMEM;
	efx->nic_data = nic_data;

	if (efx_farch_fpga_ver(efx) != 0) {
		netif_err(efx, probe, efx->net_dev,
			  "Siena FPGA not supported\n");
		rc = -ENODEV;
		goto fail1;
	}

	efx->max_channels = EFX_MAX_CHANNELS;

	efx_reado(efx, &reg, FR_AZ_CS_DEBUG);
	efx->port_num = EFX_OWORD_FIELD(reg, FRF_CZ_CS_PORT_NUM) - 1;

	rc = efx_mcdi_init(efx);
	if (rc)
		goto fail1;

	/* Let the BMC know that the driver is now in charge of link and
	 * filter settings. We must do this before we reset the NIC */
	rc = efx_mcdi_drv_attach(efx, true, &already_attached);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "Unable to register driver with MCPU\n");
		goto fail2;
	}
	if (already_attached)
		/* Not a fatal error */
		netif_err(efx, probe, efx->net_dev,
			  "Host already registered with MCPU\n");

	/* Now we can reset the NIC */
	rc = efx_mcdi_reset(efx, RESET_TYPE_ALL);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to reset NIC\n");
		goto fail3;
	}

	siena_init_wol(efx);

	/* Allocate memory for INT_KER */
	rc = efx_nic_alloc_buffer(efx, &efx->irq_status, sizeof(efx_oword_t),
				  GFP_KERNEL);
	if (rc)
		goto fail4;
	BUG_ON(efx->irq_status.dma_addr & 0x0f);

	netif_dbg(efx, probe, efx->net_dev,
		  "INT_KER at %llx (virt %p phys %llx)\n",
		  (unsigned long long)efx->irq_status.dma_addr,
		  efx->irq_status.addr,
		  (unsigned long long)virt_to_phys(efx->irq_status.addr));

	/* Read in the non-volatile configuration */
	rc = siena_probe_nvconfig(efx);
	if (rc == -EINVAL) {
		netif_err(efx, probe, efx->net_dev,
			  "NVRAM is invalid therefore using defaults\n");
		efx->phy_type = PHY_TYPE_NONE;
		efx->mdio.prtad = MDIO_PRTAD_NONE;
	} else if (rc) {
		goto fail5;
	}

	rc = efx_mcdi_mon_probe(efx);
	if (rc)
		goto fail5;

	efx_sriov_probe(efx);
	efx_ptp_probe(efx);

	return 0;

fail5:
	efx_nic_free_buffer(efx, &efx->irq_status);
fail4:
fail3:
	efx_mcdi_drv_attach(efx, false, NULL);
fail2:
	efx_mcdi_fini(efx);
fail1:
	kfree(efx->nic_data);
	return rc;
}

/* This call performs hardware-specific global initialisation, such as
 * defining the descriptor cache sizes and number of RSS channels.
 * It does not set up any buffers, descriptor rings or event queues.
 */
static int siena_init_nic(struct efx_nic *efx)
{
	efx_oword_t temp;
	int rc;

	/* Recover from a failed assertion post-reset */
	rc = efx_mcdi_handle_assertion(efx);
	if (rc)
		return rc;

	/* Squash TX of packets of 16 bytes or less */
	efx_reado(efx, &temp, FR_AZ_TX_RESERVED);
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_TX_FLUSH_MIN_LEN_EN, 1);
	efx_writeo(efx, &temp, FR_AZ_TX_RESERVED);

	/* Do not enable TX_NO_EOP_DISC_EN, since it limits packets to 16
	 * descriptors (which is bad).
	 */
	efx_reado(efx, &temp, FR_AZ_TX_CFG);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_NO_EOP_DISC_EN, 0);
	EFX_SET_OWORD_FIELD(temp, FRF_CZ_TX_FILTER_EN_BIT, 1);
	efx_writeo(efx, &temp, FR_AZ_TX_CFG);

	efx_reado(efx, &temp, FR_AZ_RX_CFG);
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_RX_DESC_PUSH_EN, 0);
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_RX_INGR_EN, 1);
	/* Enable hash insertion. This is broken for the 'Falcon' hash
	 * if IPv6 hashing is also enabled, so also select Toeplitz
	 * TCP/IPv4 and IPv4 hashes. */
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_RX_HASH_INSRT_HDR, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_RX_HASH_ALG, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_RX_IP_HASH, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_BZ_RX_USR_BUF_SIZE,
			    EFX_RX_USR_BUF_SIZE >> 5);
	efx_writeo(efx, &temp, FR_AZ_RX_CFG);

	/* Set hash key for IPv4 */
	memcpy(&temp, efx->rx_hash_key, sizeof(temp));
	efx_writeo(efx, &temp, FR_BZ_RX_RSS_TKEY);

	/* Enable IPv6 RSS */
	BUILD_BUG_ON(sizeof(efx->rx_hash_key) <
		     2 * sizeof(temp) + FRF_CZ_RX_RSS_IPV6_TKEY_HI_WIDTH / 8 ||
		     FRF_CZ_RX_RSS_IPV6_TKEY_HI_LBN != 0);
	memcpy(&temp, efx->rx_hash_key, sizeof(temp));
	efx_writeo(efx, &temp, FR_CZ_RX_RSS_IPV6_REG1);
	memcpy(&temp, efx->rx_hash_key + sizeof(temp), sizeof(temp));
	efx_writeo(efx, &temp, FR_CZ_RX_RSS_IPV6_REG2);
	EFX_POPULATE_OWORD_2(temp, FRF_CZ_RX_RSS_IPV6_THASH_ENABLE, 1,
			     FRF_CZ_RX_RSS_IPV6_IP_THASH_ENABLE, 1);
	memcpy(&temp, efx->rx_hash_key + 2 * sizeof(temp),
	       FRF_CZ_RX_RSS_IPV6_TKEY_HI_WIDTH / 8);
	efx_writeo(efx, &temp, FR_CZ_RX_RSS_IPV6_REG3);

	/* Enable event logging */
	rc = efx_mcdi_log_ctrl(efx, true, false, 0);
	if (rc)
		return rc;

	/* Set destination of both TX and RX Flush events */
	EFX_POPULATE_OWORD_1(temp, FRF_BZ_FLS_EVQ_ID, 0);
	efx_writeo(efx, &temp, FR_BZ_DP_CTRL);

	EFX_POPULATE_OWORD_1(temp, FRF_CZ_USREV_DIS, 1);
	efx_writeo(efx, &temp, FR_CZ_USR_EV_CFG);

	efx_farch_init_common(efx);
	return 0;
}

static void siena_remove_nic(struct efx_nic *efx)
{
	efx_mcdi_mon_remove(efx);

	efx_nic_free_buffer(efx, &efx->irq_status);

	efx_mcdi_reset(efx, RESET_TYPE_ALL);

	/* Relinquish the device back to the BMC */
	efx_mcdi_drv_attach(efx, false, NULL);

	/* Tear down the private nic state */
	kfree(efx->nic_data);
	efx->nic_data = NULL;

	efx_mcdi_fini(efx);
}

static int siena_try_update_nic_stats(struct efx_nic *efx)
{
	__le64 *dma_stats;
	struct efx_mac_stats *mac_stats;
	__le64 generation_start, generation_end;

	mac_stats = &efx->mac_stats;
	dma_stats = efx->stats_buffer.addr;

	generation_end = dma_stats[MC_CMD_MAC_GENERATION_END];
	if (generation_end == EFX_MC_STATS_GENERATION_INVALID)
		return 0;
	rmb();

#define MAC_STAT(M, D) \
	mac_stats->M = le64_to_cpu(dma_stats[MC_CMD_MAC_ ## D])

	MAC_STAT(tx_bytes, TX_BYTES);
	MAC_STAT(tx_bad_bytes, TX_BAD_BYTES);
	efx_update_diff_stat(&mac_stats->tx_good_bytes,
			     mac_stats->tx_bytes - mac_stats->tx_bad_bytes);
	MAC_STAT(tx_packets, TX_PKTS);
	MAC_STAT(tx_bad, TX_BAD_FCS_PKTS);
	MAC_STAT(tx_pause, TX_PAUSE_PKTS);
	MAC_STAT(tx_control, TX_CONTROL_PKTS);
	MAC_STAT(tx_unicast, TX_UNICAST_PKTS);
	MAC_STAT(tx_multicast, TX_MULTICAST_PKTS);
	MAC_STAT(tx_broadcast, TX_BROADCAST_PKTS);
	MAC_STAT(tx_lt64, TX_LT64_PKTS);
	MAC_STAT(tx_64, TX_64_PKTS);
	MAC_STAT(tx_65_to_127, TX_65_TO_127_PKTS);
	MAC_STAT(tx_128_to_255, TX_128_TO_255_PKTS);
	MAC_STAT(tx_256_to_511, TX_256_TO_511_PKTS);
	MAC_STAT(tx_512_to_1023, TX_512_TO_1023_PKTS);
	MAC_STAT(tx_1024_to_15xx, TX_1024_TO_15XX_PKTS);
	MAC_STAT(tx_15xx_to_jumbo, TX_15XX_TO_JUMBO_PKTS);
	MAC_STAT(tx_gtjumbo, TX_GTJUMBO_PKTS);
	mac_stats->tx_collision = 0;
	MAC_STAT(tx_single_collision, TX_SINGLE_COLLISION_PKTS);
	MAC_STAT(tx_multiple_collision, TX_MULTIPLE_COLLISION_PKTS);
	MAC_STAT(tx_excessive_collision, TX_EXCESSIVE_COLLISION_PKTS);
	MAC_STAT(tx_deferred, TX_DEFERRED_PKTS);
	MAC_STAT(tx_late_collision, TX_LATE_COLLISION_PKTS);
	mac_stats->tx_collision = (mac_stats->tx_single_collision +
				   mac_stats->tx_multiple_collision +
				   mac_stats->tx_excessive_collision +
				   mac_stats->tx_late_collision);
	MAC_STAT(tx_excessive_deferred, TX_EXCESSIVE_DEFERRED_PKTS);
	MAC_STAT(tx_non_tcpudp, TX_NON_TCPUDP_PKTS);
	MAC_STAT(tx_mac_src_error, TX_MAC_SRC_ERR_PKTS);
	MAC_STAT(tx_ip_src_error, TX_IP_SRC_ERR_PKTS);
	MAC_STAT(rx_bytes, RX_BYTES);
	MAC_STAT(rx_bad_bytes, RX_BAD_BYTES);
	efx_update_diff_stat(&mac_stats->rx_good_bytes,
			     mac_stats->rx_bytes - mac_stats->rx_bad_bytes);
	MAC_STAT(rx_packets, RX_PKTS);
	MAC_STAT(rx_good, RX_GOOD_PKTS);
	MAC_STAT(rx_bad, RX_BAD_FCS_PKTS);
	MAC_STAT(rx_pause, RX_PAUSE_PKTS);
	MAC_STAT(rx_control, RX_CONTROL_PKTS);
	MAC_STAT(rx_unicast, RX_UNICAST_PKTS);
	MAC_STAT(rx_multicast, RX_MULTICAST_PKTS);
	MAC_STAT(rx_broadcast, RX_BROADCAST_PKTS);
	MAC_STAT(rx_lt64, RX_UNDERSIZE_PKTS);
	MAC_STAT(rx_64, RX_64_PKTS);
	MAC_STAT(rx_65_to_127, RX_65_TO_127_PKTS);
	MAC_STAT(rx_128_to_255, RX_128_TO_255_PKTS);
	MAC_STAT(rx_256_to_511, RX_256_TO_511_PKTS);
	MAC_STAT(rx_512_to_1023, RX_512_TO_1023_PKTS);
	MAC_STAT(rx_1024_to_15xx, RX_1024_TO_15XX_PKTS);
	MAC_STAT(rx_15xx_to_jumbo, RX_15XX_TO_JUMBO_PKTS);
	MAC_STAT(rx_gtjumbo, RX_GTJUMBO_PKTS);
	mac_stats->rx_bad_lt64 = 0;
	mac_stats->rx_bad_64_to_15xx = 0;
	mac_stats->rx_bad_15xx_to_jumbo = 0;
	MAC_STAT(rx_bad_gtjumbo, RX_JABBER_PKTS);
	MAC_STAT(rx_overflow, RX_OVERFLOW_PKTS);
	mac_stats->rx_missed = 0;
	MAC_STAT(rx_false_carrier, RX_FALSE_CARRIER_PKTS);
	MAC_STAT(rx_symbol_error, RX_SYMBOL_ERROR_PKTS);
	MAC_STAT(rx_align_error, RX_ALIGN_ERROR_PKTS);
	MAC_STAT(rx_length_error, RX_LENGTH_ERROR_PKTS);
	MAC_STAT(rx_internal_error, RX_INTERNAL_ERROR_PKTS);
	mac_stats->rx_good_lt64 = 0;

	efx->n_rx_nodesc_drop_cnt =
		le64_to_cpu(dma_stats[MC_CMD_MAC_RX_NODESC_DROPS]);

#undef MAC_STAT

	rmb();
	generation_start = dma_stats[MC_CMD_MAC_GENERATION_START];
	if (generation_end != generation_start)
		return -EAGAIN;

	return 0;
}

static void siena_update_nic_stats(struct efx_nic *efx)
{
	int retry;

	/* If we're unlucky enough to read statistics wduring the DMA, wait
	 * up to 10ms for it to finish (typically takes <500us) */
	for (retry = 0; retry < 100; ++retry) {
		if (siena_try_update_nic_stats(efx) == 0)
			return;
		udelay(100);
	}

	/* Use the old values instead */
}

static int siena_mac_reconfigure(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SET_MCAST_HASH_IN_LEN);
	int rc;

	BUILD_BUG_ON(MC_CMD_SET_MCAST_HASH_IN_LEN !=
		     MC_CMD_SET_MCAST_HASH_IN_HASH0_OFST +
		     sizeof(efx->multicast_hash));

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	rc = efx_mcdi_set_mac(efx);
	if (rc != 0)
		return rc;

	memcpy(MCDI_PTR(inbuf, SET_MCAST_HASH_IN_HASH0),
	       efx->multicast_hash.byte, sizeof(efx->multicast_hash));
	return efx_mcdi_rpc(efx, MC_CMD_SET_MCAST_HASH,
			    inbuf, sizeof(inbuf), NULL, 0, NULL);
}

/**************************************************************************
 *
 * Wake on LAN
 *
 **************************************************************************
 */

static void siena_get_wol(struct efx_nic *efx, struct ethtool_wolinfo *wol)
{
	struct siena_nic_data *nic_data = efx->nic_data;

	wol->supported = WAKE_MAGIC;
	if (nic_data->wol_filter_id != -1)
		wol->wolopts = WAKE_MAGIC;
	else
		wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}


static int siena_set_wol(struct efx_nic *efx, u32 type)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	int rc;

	if (type & ~WAKE_MAGIC)
		return -EINVAL;

	if (type & WAKE_MAGIC) {
		if (nic_data->wol_filter_id != -1)
			efx_mcdi_wol_filter_remove(efx,
						   nic_data->wol_filter_id);
		rc = efx_mcdi_wol_filter_set_magic(efx, efx->net_dev->dev_addr,
						   &nic_data->wol_filter_id);
		if (rc)
			goto fail;

		pci_wake_from_d3(efx->pci_dev, true);
	} else {
		rc = efx_mcdi_wol_filter_reset(efx);
		nic_data->wol_filter_id = -1;
		pci_wake_from_d3(efx->pci_dev, false);
		if (rc)
			goto fail;
	}

	return 0;
 fail:
	netif_err(efx, hw, efx->net_dev, "%s failed: type=%d rc=%d\n",
		  __func__, type, rc);
	return rc;
}


static void siena_init_wol(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = efx_mcdi_wol_filter_get_magic(efx, &nic_data->wol_filter_id);

	if (rc != 0) {
		/* If it failed, attempt to get into a synchronised
		 * state with MC by resetting any set WoL filters */
		efx_mcdi_wol_filter_reset(efx);
		nic_data->wol_filter_id = -1;
	} else if (nic_data->wol_filter_id != -1) {
		pci_wake_from_d3(efx->pci_dev, true);
	}
}

/**************************************************************************
 *
 * MCDI
 *
 **************************************************************************
 */

#define MCDI_PDU(efx)							\
	(efx_port_num(efx) ? MC_SMEM_P1_PDU_OFST : MC_SMEM_P0_PDU_OFST)
#define MCDI_DOORBELL(efx)						\
	(efx_port_num(efx) ? MC_SMEM_P1_DOORBELL_OFST : MC_SMEM_P0_DOORBELL_OFST)
#define MCDI_STATUS(efx)						\
	(efx_port_num(efx) ? MC_SMEM_P1_STATUS_OFST : MC_SMEM_P0_STATUS_OFST)

static void siena_mcdi_request(struct efx_nic *efx,
			       const efx_dword_t *hdr, size_t hdr_len,
			       const efx_dword_t *sdu, size_t sdu_len)
{
	unsigned pdu = FR_CZ_MC_TREG_SMEM + MCDI_PDU(efx);
	unsigned doorbell = FR_CZ_MC_TREG_SMEM + MCDI_DOORBELL(efx);
	unsigned int i;
	unsigned int inlen_dw = DIV_ROUND_UP(sdu_len, 4);

	EFX_BUG_ON_PARANOID(hdr_len != 4);

	efx_writed(efx, hdr, pdu);

	for (i = 0; i < inlen_dw; i++)
		efx_writed(efx, &sdu[i], pdu + hdr_len + 4 * i);

	/* Ensure the request is written out before the doorbell */
	wmb();

	/* ring the doorbell with a distinctive value */
	_efx_writed(efx, (__force __le32) 0x45789abc, doorbell);
}

static bool siena_mcdi_poll_response(struct efx_nic *efx)
{
	unsigned int pdu = FR_CZ_MC_TREG_SMEM + MCDI_PDU(efx);
	efx_dword_t hdr;

	efx_readd(efx, &hdr, pdu);

	/* All 1's indicates that shared memory is in reset (and is
	 * not a valid hdr). Wait for it to come out reset before
	 * completing the command
	 */
	return EFX_DWORD_FIELD(hdr, EFX_DWORD_0) != 0xffffffff &&
		EFX_DWORD_FIELD(hdr, MCDI_HEADER_RESPONSE);
}

static void siena_mcdi_read_response(struct efx_nic *efx, efx_dword_t *outbuf,
				     size_t offset, size_t outlen)
{
	unsigned int pdu = FR_CZ_MC_TREG_SMEM + MCDI_PDU(efx);
	unsigned int outlen_dw = DIV_ROUND_UP(outlen, 4);
	int i;

	for (i = 0; i < outlen_dw; i++)
		efx_readd(efx, &outbuf[i], pdu + offset + 4 * i);
}

static int siena_mcdi_poll_reboot(struct efx_nic *efx)
{
	unsigned int addr = FR_CZ_MC_TREG_SMEM + MCDI_STATUS(efx);
	efx_dword_t reg;
	u32 value;

	efx_readd(efx, &reg, addr);
	value = EFX_DWORD_FIELD(reg, EFX_DWORD_0);

	if (value == 0)
		return 0;

	EFX_ZERO_DWORD(reg);
	efx_writed(efx, &reg, addr);

	if (value == MC_STATUS_DWORD_ASSERT)
		return -EINTR;
	else
		return -EIO;
}

/**************************************************************************
 *
 * Revision-dependent attributes used by efx.c and nic.c
 *
 **************************************************************************
 */

const struct efx_nic_type siena_a0_nic_type = {
	.mem_map_size = siena_mem_map_size,
	.probe = siena_probe_nic,
	.remove = siena_remove_nic,
	.init = siena_init_nic,
	.dimension_resources = siena_dimension_resources,
	.fini = efx_port_dummy_op_void,
#ifdef CONFIG_EEH
	.monitor = siena_monitor,
#else
	.monitor = NULL,
#endif
	.map_reset_reason = efx_mcdi_map_reset_reason,
	.map_reset_flags = siena_map_reset_flags,
	.reset = efx_mcdi_reset,
	.probe_port = efx_mcdi_port_probe,
	.remove_port = efx_mcdi_port_remove,
	.fini_dmaq = efx_farch_fini_dmaq,
	.prepare_flush = siena_prepare_flush,
	.finish_flush = siena_finish_flush,
	.update_stats = siena_update_nic_stats,
	.start_stats = efx_mcdi_mac_start_stats,
	.stop_stats = efx_mcdi_mac_stop_stats,
	.set_id_led = efx_mcdi_set_id_led,
	.push_irq_moderation = siena_push_irq_moderation,
	.reconfigure_mac = siena_mac_reconfigure,
	.check_mac_fault = efx_mcdi_mac_check_fault,
	.reconfigure_port = efx_mcdi_port_reconfigure,
	.get_wol = siena_get_wol,
	.set_wol = siena_set_wol,
	.resume_wol = siena_init_wol,
	.test_chip = siena_test_chip,
	.test_nvram = efx_mcdi_nvram_test_all,
	.mcdi_request = siena_mcdi_request,
	.mcdi_poll_response = siena_mcdi_poll_response,
	.mcdi_read_response = siena_mcdi_read_response,
	.mcdi_poll_reboot = siena_mcdi_poll_reboot,
	.irq_enable_master = efx_farch_irq_enable_master,
	.irq_test_generate = efx_farch_irq_test_generate,
	.irq_disable_non_ev = efx_farch_irq_disable_master,
	.irq_handle_msi = efx_farch_msi_interrupt,
	.irq_handle_legacy = efx_farch_legacy_interrupt,
	.tx_probe = efx_farch_tx_probe,
	.tx_init = efx_farch_tx_init,
	.tx_remove = efx_farch_tx_remove,
	.tx_write = efx_farch_tx_write,
	.rx_push_indir_table = efx_farch_rx_push_indir_table,
	.rx_probe = efx_farch_rx_probe,
	.rx_init = efx_farch_rx_init,
	.rx_remove = efx_farch_rx_remove,
	.rx_write = efx_farch_rx_write,
	.rx_defer_refill = efx_farch_rx_defer_refill,
	.ev_probe = efx_farch_ev_probe,
	.ev_init = efx_farch_ev_init,
	.ev_fini = efx_farch_ev_fini,
	.ev_remove = efx_farch_ev_remove,
	.ev_process = efx_farch_ev_process,
	.ev_read_ack = efx_farch_ev_read_ack,
	.ev_test_generate = efx_farch_ev_test_generate,
	.filter_table_probe = efx_farch_filter_table_probe,
	.filter_table_restore = efx_farch_filter_table_restore,
	.filter_table_remove = efx_farch_filter_table_remove,
	.filter_update_rx_scatter = efx_farch_filter_update_rx_scatter,
	.filter_insert = efx_farch_filter_insert,
	.filter_remove_safe = efx_farch_filter_remove_safe,
	.filter_get_safe = efx_farch_filter_get_safe,
	.filter_clear_rx = efx_farch_filter_clear_rx,
	.filter_count_rx_used = efx_farch_filter_count_rx_used,
	.filter_get_rx_id_limit = efx_farch_filter_get_rx_id_limit,
	.filter_get_rx_ids = efx_farch_filter_get_rx_ids,
#ifdef CONFIG_RFS_ACCEL
	.filter_rfs_insert = efx_farch_filter_rfs_insert,
	.filter_rfs_expire_one = efx_farch_filter_rfs_expire_one,
#endif

	.revision = EFX_REV_SIENA_A0,
	.txd_ptr_tbl_base = FR_BZ_TX_DESC_PTR_TBL,
	.rxd_ptr_tbl_base = FR_BZ_RX_DESC_PTR_TBL,
	.buf_tbl_base = FR_BZ_BUF_FULL_TBL,
	.evq_ptr_tbl_base = FR_BZ_EVQ_PTR_TBL,
	.evq_rptr_tbl_base = FR_BZ_EVQ_RPTR,
	.max_dma_mask = DMA_BIT_MASK(FSF_AZ_TX_KER_BUF_ADDR_WIDTH),
	.rx_buffer_hash_size = 0x10,
	.rx_buffer_padding = 0,
	.can_rx_scatter = true,
	.max_interrupt_mode = EFX_INT_MODE_MSIX,
	.timer_period_max = 1 << FRF_CZ_TC_TIMER_VAL_WIDTH,
	.offload_features = (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			     NETIF_F_RXHASH | NETIF_F_NTUPLE),
	.mcdi_max_ver = 1,
	.max_rx_ip_filters = FR_BZ_RX_FILTER_TBL0_ROWS,
};
