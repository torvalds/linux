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
#include "regs.h"
#include "io.h"
#include "phy.h"
#include "workarounds.h"
#include "mcdi.h"
#include "mcdi_pcol.h"

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

static int siena_mdio_write(struct net_device *net_dev,
			    int prtad, int devad, u16 addr, u16 value)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	uint32_t status;
	int rc;

	rc = efx_mcdi_mdio_write(efx, efx->mdio_bus, prtad, devad,
				 addr, value, &status);
	if (rc)
		return rc;
	if (status != MC_CMD_MDIO_STATUS_GOOD)
		return -EIO;

	return 0;
}

static int siena_mdio_read(struct net_device *net_dev,
			   int prtad, int devad, u16 addr)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	uint16_t value;
	uint32_t status;
	int rc;

	rc = efx_mcdi_mdio_read(efx, efx->mdio_bus, prtad, devad,
				addr, &value, &status);
	if (rc)
		return rc;
	if (status != MC_CMD_MDIO_STATUS_GOOD)
		return -EIO;

	return (int)value;
}

/* This call is responsible for hooking in the MAC and PHY operations */
static int siena_probe_port(struct efx_nic *efx)
{
	int rc;

	/* Hook in PHY operations table */
	efx->phy_op = &efx_mcdi_phy_ops;

	/* Set up MDIO structure for PHY */
	efx->mdio.mode_support = MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;
	efx->mdio.mdio_read = siena_mdio_read;
	efx->mdio.mdio_write = siena_mdio_write;

	/* Fill out MDIO structure, loopback modes, and initial link state */
	rc = efx->phy_op->probe(efx);
	if (rc != 0)
		return rc;

	/* Allocate buffer for stats */
	rc = efx_nic_alloc_buffer(efx, &efx->stats_buffer,
				  MC_CMD_MAC_NSTATS * sizeof(u64));
	if (rc)
		return rc;
	netif_dbg(efx, probe, efx->net_dev,
		  "stats buffer at %llx (virt %p phys %llx)\n",
		  (u64)efx->stats_buffer.dma_addr,
		  efx->stats_buffer.addr,
		  (u64)virt_to_phys(efx->stats_buffer.addr));

	efx_mcdi_mac_stats(efx, efx->stats_buffer.dma_addr, 0, 0, 1);

	return 0;
}

static void siena_remove_port(struct efx_nic *efx)
{
	efx->phy_op->remove(efx);
	efx_nic_free_buffer(efx, &efx->stats_buffer);
}

static const struct efx_nic_register_test siena_register_tests[] = {
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

static int siena_test_registers(struct efx_nic *efx)
{
	return efx_nic_test_registers(efx, siena_register_tests,
				      ARRAY_SIZE(siena_register_tests));
}

/**************************************************************************
 *
 * Device reset
 *
 **************************************************************************
 */

static enum reset_type siena_map_reset_reason(enum reset_type reason)
{
	return RESET_TYPE_ALL;
}

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

static int siena_reset_hw(struct efx_nic *efx, enum reset_type method)
{
	int rc;

	/* Recover from a failed assertion pre-reset */
	rc = efx_mcdi_handle_assertion(efx);
	if (rc)
		return rc;

	if (method == RESET_TYPE_WORLD)
		return efx_mcdi_reset_mc(efx);
	else
		return efx_mcdi_reset_port(efx);
}

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
	efx_nic_dimension_resources(efx, FR_CZ_BUF_FULL_TBL_ROWS / 2);
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

	if (efx_nic_fpga_ver(efx) != 0) {
		netif_err(efx, probe, efx->net_dev,
			  "Siena FPGA not supported\n");
		rc = -ENODEV;
		goto fail1;
	}

	efx_reado(efx, &reg, FR_AZ_CS_DEBUG);
	efx->net_dev->dev_id = EFX_OWORD_FIELD(reg, FRF_CZ_CS_PORT_NUM) - 1;

	efx_mcdi_init(efx);

	/* Recover from a failed assertion before probing */
	rc = efx_mcdi_handle_assertion(efx);
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
	rc = siena_reset_hw(efx, RESET_TYPE_ALL);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to reset NIC\n");
		goto fail3;
	}

	siena_init_wol(efx);

	/* Allocate memory for INT_KER */
	rc = efx_nic_alloc_buffer(efx, &efx->irq_status, sizeof(efx_oword_t));
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

	return 0;

fail5:
	efx_nic_free_buffer(efx, &efx->irq_status);
fail4:
fail3:
	efx_mcdi_drv_attach(efx, false, NULL);
fail2:
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

	efx_nic_init_common(efx);
	return 0;
}

static void siena_remove_nic(struct efx_nic *efx)
{
	efx_mcdi_mon_remove(efx);

	efx_nic_free_buffer(efx, &efx->irq_status);

	siena_reset_hw(efx, RESET_TYPE_ALL);

	/* Relinquish the device back to the BMC */
	efx_mcdi_drv_attach(efx, false, NULL);

	/* Tear down the private nic state */
	kfree(efx->nic_data);
	efx->nic_data = NULL;
}

#define STATS_GENERATION_INVALID ((__force __le64)(-1))

static int siena_try_update_nic_stats(struct efx_nic *efx)
{
	__le64 *dma_stats;
	struct efx_mac_stats *mac_stats;
	__le64 generation_start, generation_end;

	mac_stats = &efx->mac_stats;
	dma_stats = efx->stats_buffer.addr;

	generation_end = dma_stats[MC_CMD_MAC_GENERATION_END];
	if (generation_end == STATS_GENERATION_INVALID)
		return 0;
	rmb();

#define MAC_STAT(M, D) \
	mac_stats->M = le64_to_cpu(dma_stats[MC_CMD_MAC_ ## D])

	MAC_STAT(tx_bytes, TX_BYTES);
	MAC_STAT(tx_bad_bytes, TX_BAD_BYTES);
	mac_stats->tx_good_bytes = (mac_stats->tx_bytes -
				    mac_stats->tx_bad_bytes);
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
	mac_stats->rx_good_bytes = (mac_stats->rx_bytes -
				    mac_stats->rx_bad_bytes);
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

static void siena_start_nic_stats(struct efx_nic *efx)
{
	__le64 *dma_stats = efx->stats_buffer.addr;

	dma_stats[MC_CMD_MAC_GENERATION_END] = STATS_GENERATION_INVALID;

	efx_mcdi_mac_stats(efx, efx->stats_buffer.dma_addr,
			   MC_CMD_MAC_NSTATS * sizeof(u64), 1, 0);
}

static void siena_stop_nic_stats(struct efx_nic *efx)
{
	efx_mcdi_mac_stats(efx, efx->stats_buffer.dma_addr, 0, 0, 0);
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
 * Revision-dependent attributes used by efx.c and nic.c
 *
 **************************************************************************
 */

const struct efx_nic_type siena_a0_nic_type = {
	.probe = siena_probe_nic,
	.remove = siena_remove_nic,
	.init = siena_init_nic,
	.dimension_resources = siena_dimension_resources,
	.fini = efx_port_dummy_op_void,
	.monitor = NULL,
	.map_reset_reason = siena_map_reset_reason,
	.map_reset_flags = siena_map_reset_flags,
	.reset = siena_reset_hw,
	.probe_port = siena_probe_port,
	.remove_port = siena_remove_port,
	.prepare_flush = efx_port_dummy_op_void,
	.update_stats = siena_update_nic_stats,
	.start_stats = siena_start_nic_stats,
	.stop_stats = siena_stop_nic_stats,
	.set_id_led = efx_mcdi_set_id_led,
	.push_irq_moderation = siena_push_irq_moderation,
	.reconfigure_mac = efx_mcdi_mac_reconfigure,
	.check_mac_fault = efx_mcdi_mac_check_fault,
	.reconfigure_port = efx_mcdi_phy_reconfigure,
	.get_wol = siena_get_wol,
	.set_wol = siena_set_wol,
	.resume_wol = siena_init_wol,
	.test_registers = siena_test_registers,
	.test_nvram = efx_mcdi_nvram_test_all,

	.revision = EFX_REV_SIENA_A0,
	.mem_map_size = (FR_CZ_MC_TREG_SMEM +
			 FR_CZ_MC_TREG_SMEM_STEP * FR_CZ_MC_TREG_SMEM_ROWS),
	.txd_ptr_tbl_base = FR_BZ_TX_DESC_PTR_TBL,
	.rxd_ptr_tbl_base = FR_BZ_RX_DESC_PTR_TBL,
	.buf_tbl_base = FR_BZ_BUF_FULL_TBL,
	.evq_ptr_tbl_base = FR_BZ_EVQ_PTR_TBL,
	.evq_rptr_tbl_base = FR_BZ_EVQ_RPTR,
	.max_dma_mask = DMA_BIT_MASK(FSF_AZ_TX_KER_BUF_ADDR_WIDTH),
	.rx_buffer_hash_size = 0x10,
	.rx_buffer_padding = 0,
	.max_interrupt_mode = EFX_INT_MODE_MSIX,
	.phys_addr_channels = 32, /* Hardware limit is 64, but the legacy
				   * interrupt handler only supports 32
				   * channels */
	.timer_period_max = 1 << FRF_CZ_TC_TIMER_VAL_WIDTH,
	.offload_features = (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			     NETIF_F_RXHASH | NETIF_F_NTUPLE),
};
