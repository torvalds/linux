/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2012-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "ef10_regs.h"
#include "io.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "nic.h"
#include "workarounds.h"
#include <linux/in.h>
#include <linux/jhash.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

/* Hardware control for EF10 architecture including 'Huntington'. */

#define EFX_EF10_DRVGEN_EV		7
enum {
	EFX_EF10_TEST = 1,
	EFX_EF10_REFILL,
};

/* The reserved RSS context value */
#define EFX_EF10_RSS_CONTEXT_INVALID	0xffffffff

/* The filter table(s) are managed by firmware and we have write-only
 * access.  When removing filters we must identify them to the
 * firmware by a 64-bit handle, but this is too wide for Linux kernel
 * interfaces (32-bit for RX NFC, 16-bit for RFS).  Also, we need to
 * be able to tell in advance whether a requested insertion will
 * replace an existing filter.  Therefore we maintain a software hash
 * table, which should be at least as large as the hardware hash
 * table.
 *
 * Huntington has a single 8K filter table shared between all filter
 * types and both ports.
 */
#define HUNT_FILTER_TBL_ROWS 8192

struct efx_ef10_filter_table {
/* The RX match field masks supported by this fw & hw, in order of priority */
	enum efx_filter_match_flags rx_match_flags[
		MC_CMD_GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES_MAXNUM];
	unsigned int rx_match_count;

	struct {
		unsigned long spec;	/* pointer to spec plus flag bits */
/* BUSY flag indicates that an update is in progress.  STACK_OLD is
 * used to mark and sweep stack-owned MAC filters.
 */
#define EFX_EF10_FILTER_FLAG_BUSY	1UL
#define EFX_EF10_FILTER_FLAG_STACK_OLD	2UL
#define EFX_EF10_FILTER_FLAGS		3UL
		u64 handle;		/* firmware handle */
	} *entry;
	wait_queue_head_t waitq;
/* Shadow of net_device address lists, guarded by mac_lock */
#define EFX_EF10_FILTER_STACK_UC_MAX	32
#define EFX_EF10_FILTER_STACK_MC_MAX	256
	struct {
		u8 addr[ETH_ALEN];
		u16 id;
	} stack_uc_list[EFX_EF10_FILTER_STACK_UC_MAX],
	  stack_mc_list[EFX_EF10_FILTER_STACK_MC_MAX];
	int stack_uc_count;		/* negative for PROMISC */
	int stack_mc_count;		/* negative for PROMISC/ALLMULTI */
};

/* An arbitrary search limit for the software hash table */
#define EFX_EF10_FILTER_SEARCH_LIMIT 200

static void efx_ef10_rx_push_indir_table(struct efx_nic *efx);
static void efx_ef10_rx_free_indir_table(struct efx_nic *efx);
static void efx_ef10_filter_table_remove(struct efx_nic *efx);

static int efx_ef10_get_warm_boot_count(struct efx_nic *efx)
{
	efx_dword_t reg;

	efx_readd(efx, &reg, ER_DZ_BIU_MC_SFT_STATUS);
	return EFX_DWORD_FIELD(reg, EFX_WORD_1) == 0xb007 ?
		EFX_DWORD_FIELD(reg, EFX_WORD_0) : -EIO;
}

static unsigned int efx_ef10_mem_map_size(struct efx_nic *efx)
{
	return resource_size(&efx->pci_dev->resource[EFX_MEM_BAR]);
}

static int efx_ef10_init_datapath_caps(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_CAPABILITIES_OUT_LEN);
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_CAPABILITIES_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_CAPABILITIES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < sizeof(outbuf)) {
		netif_err(efx, drv, efx->net_dev,
			  "unable to read datapath firmware capabilities\n");
		return -EIO;
	}

	nic_data->datapath_caps =
		MCDI_DWORD(outbuf, GET_CAPABILITIES_OUT_FLAGS1);

	if (!(nic_data->datapath_caps &
	      (1 << MC_CMD_GET_CAPABILITIES_OUT_TX_TSO_LBN))) {
		netif_err(efx, drv, efx->net_dev,
			  "current firmware does not support TSO\n");
		return -ENODEV;
	}

	if (!(nic_data->datapath_caps &
	      (1 << MC_CMD_GET_CAPABILITIES_OUT_RX_PREFIX_LEN_14_LBN))) {
		netif_err(efx, probe, efx->net_dev,
			  "current firmware does not support an RX prefix\n");
		return -ENODEV;
	}

	return 0;
}

static int efx_ef10_get_sysclk_freq(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_CLOCK_OUT_LEN);
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_CLOCK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		return rc;
	rc = MCDI_DWORD(outbuf, GET_CLOCK_OUT_SYS_FREQ);
	return rc > 0 ? rc : -ERANGE;
}

static int efx_ef10_get_mac_address(struct efx_nic *efx, u8 *mac_address)
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

	memcpy(mac_address,
	       MCDI_PTR(outbuf, GET_MAC_ADDRESSES_OUT_MAC_ADDR_BASE), ETH_ALEN);
	return 0;
}

static int efx_ef10_probe(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data;
	int i, rc;

	/* We can have one VI for each 8K region.  However we need
	 * multiple TX queues per channel.
	 */
	efx->max_channels =
		min_t(unsigned int,
		      EFX_MAX_CHANNELS,
		      resource_size(&efx->pci_dev->resource[EFX_MEM_BAR]) /
		      (EFX_VI_PAGE_SIZE * EFX_TXQ_TYPES));
	BUG_ON(efx->max_channels == 0);

	nic_data = kzalloc(sizeof(*nic_data), GFP_KERNEL);
	if (!nic_data)
		return -ENOMEM;
	efx->nic_data = nic_data;

	rc = efx_nic_alloc_buffer(efx, &nic_data->mcdi_buf,
				  8 + MCDI_CTL_SDU_LEN_MAX_V2, GFP_KERNEL);
	if (rc)
		goto fail1;

	/* Get the MC's warm boot count.  In case it's rebooting right
	 * now, be prepared to retry.
	 */
	i = 0;
	for (;;) {
		rc = efx_ef10_get_warm_boot_count(efx);
		if (rc >= 0)
			break;
		if (++i == 5)
			goto fail2;
		ssleep(1);
	}
	nic_data->warm_boot_count = rc;

	nic_data->rx_rss_context = EFX_EF10_RSS_CONTEXT_INVALID;

	/* In case we're recovering from a crash (kexec), we want to
	 * cancel any outstanding request by the previous user of this
	 * function.  We send a special message using the least
	 * significant bits of the 'high' (doorbell) register.
	 */
	_efx_writed(efx, cpu_to_le32(1), ER_DZ_MC_DB_HWRD);

	rc = efx_mcdi_init(efx);
	if (rc)
		goto fail2;

	/* Reset (most) configuration for this function */
	rc = efx_mcdi_reset(efx, RESET_TYPE_ALL);
	if (rc)
		goto fail3;

	/* Enable event logging */
	rc = efx_mcdi_log_ctrl(efx, true, false, 0);
	if (rc)
		goto fail3;

	rc = efx_ef10_init_datapath_caps(efx);
	if (rc < 0)
		goto fail3;

	efx->rx_packet_len_offset =
		ES_DZ_RX_PREFIX_PKTLEN_OFST - ES_DZ_RX_PREFIX_SIZE;

	rc = efx_mcdi_port_get_number(efx);
	if (rc < 0)
		goto fail3;
	efx->port_num = rc;

	rc = efx_ef10_get_mac_address(efx, efx->net_dev->perm_addr);
	if (rc)
		goto fail3;

	rc = efx_ef10_get_sysclk_freq(efx);
	if (rc < 0)
		goto fail3;
	efx->timer_quantum_ns = 1536000 / rc; /* 1536 cycles */

	/* Check whether firmware supports bug 35388 workaround */
	rc = efx_mcdi_set_workaround(efx, MC_CMD_WORKAROUND_BUG35388, true);
	if (rc == 0)
		nic_data->workaround_35388 = true;
	else if (rc != -ENOSYS && rc != -ENOENT)
		goto fail3;
	netif_dbg(efx, probe, efx->net_dev,
		  "workaround for bug 35388 is %sabled\n",
		  nic_data->workaround_35388 ? "en" : "dis");

	rc = efx_mcdi_mon_probe(efx);
	if (rc)
		goto fail3;

	return 0;

fail3:
	efx_mcdi_fini(efx);
fail2:
	efx_nic_free_buffer(efx, &nic_data->mcdi_buf);
fail1:
	kfree(nic_data);
	efx->nic_data = NULL;
	return rc;
}

static int efx_ef10_free_vis(struct efx_nic *efx)
{
	int rc = efx_mcdi_rpc(efx, MC_CMD_FREE_VIS, NULL, 0, NULL, 0, NULL);

	/* -EALREADY means nothing to free, so ignore */
	if (rc == -EALREADY)
		rc = 0;
	return rc;
}

static void efx_ef10_remove(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	efx_mcdi_mon_remove(efx);

	/* This needs to be after efx_ptp_remove_channel() with no filters */
	efx_ef10_rx_free_indir_table(efx);

	rc = efx_ef10_free_vis(efx);
	WARN_ON(rc != 0);

	efx_mcdi_fini(efx);
	efx_nic_free_buffer(efx, &nic_data->mcdi_buf);
	kfree(nic_data);
}

static int efx_ef10_alloc_vis(struct efx_nic *efx,
			      unsigned int min_vis, unsigned int max_vis)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_ALLOC_VIS_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_ALLOC_VIS_OUT_LEN);
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, ALLOC_VIS_IN_MIN_VI_COUNT, min_vis);
	MCDI_SET_DWORD(inbuf, ALLOC_VIS_IN_MAX_VI_COUNT, max_vis);
	rc = efx_mcdi_rpc(efx, MC_CMD_ALLOC_VIS, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc != 0)
		return rc;

	if (outlen < MC_CMD_ALLOC_VIS_OUT_LEN)
		return -EIO;

	netif_dbg(efx, drv, efx->net_dev, "base VI is A0x%03x\n",
		  MCDI_DWORD(outbuf, ALLOC_VIS_OUT_VI_BASE));

	nic_data->vi_base = MCDI_DWORD(outbuf, ALLOC_VIS_OUT_VI_BASE);
	nic_data->n_allocated_vis = MCDI_DWORD(outbuf, ALLOC_VIS_OUT_VI_COUNT);
	return 0;
}

static int efx_ef10_dimension_resources(struct efx_nic *efx)
{
	unsigned int n_vis =
		max(efx->n_channels, efx->n_tx_channels * EFX_TXQ_TYPES);

	return efx_ef10_alloc_vis(efx, n_vis, n_vis);
}

static int efx_ef10_init_nic(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (nic_data->must_check_datapath_caps) {
		rc = efx_ef10_init_datapath_caps(efx);
		if (rc)
			return rc;
		nic_data->must_check_datapath_caps = false;
	}

	if (nic_data->must_realloc_vis) {
		/* We cannot let the number of VIs change now */
		rc = efx_ef10_alloc_vis(efx, nic_data->n_allocated_vis,
					nic_data->n_allocated_vis);
		if (rc)
			return rc;
		nic_data->must_realloc_vis = false;
	}

	efx_ef10_rx_push_indir_table(efx);
	return 0;
}

static int efx_ef10_map_reset_flags(u32 *flags)
{
	enum {
		EF10_RESET_PORT = ((ETH_RESET_MAC | ETH_RESET_PHY) <<
				   ETH_RESET_SHARED_SHIFT),
		EF10_RESET_MC = ((ETH_RESET_DMA | ETH_RESET_FILTER |
				  ETH_RESET_OFFLOAD | ETH_RESET_MAC |
				  ETH_RESET_PHY | ETH_RESET_MGMT) <<
				 ETH_RESET_SHARED_SHIFT)
	};

	/* We assume for now that our PCI function is permitted to
	 * reset everything.
	 */

	if ((*flags & EF10_RESET_MC) == EF10_RESET_MC) {
		*flags &= ~EF10_RESET_MC;
		return RESET_TYPE_WORLD;
	}

	if ((*flags & EF10_RESET_PORT) == EF10_RESET_PORT) {
		*flags &= ~EF10_RESET_PORT;
		return RESET_TYPE_ALL;
	}

	/* no invisible reset implemented */

	return -EINVAL;
}

#define EF10_DMA_STAT(ext_name, mcdi_name)			\
	[EF10_STAT_ ## ext_name] =				\
	{ #ext_name, 64, 8 * MC_CMD_MAC_ ## mcdi_name }
#define EF10_DMA_INVIS_STAT(int_name, mcdi_name)		\
	[EF10_STAT_ ## int_name] =				\
	{ NULL, 64, 8 * MC_CMD_MAC_ ## mcdi_name }
#define EF10_OTHER_STAT(ext_name)				\
	[EF10_STAT_ ## ext_name] = { #ext_name, 0, 0 }

static const struct efx_hw_stat_desc efx_ef10_stat_desc[EF10_STAT_COUNT] = {
	EF10_DMA_STAT(tx_bytes, TX_BYTES),
	EF10_DMA_STAT(tx_packets, TX_PKTS),
	EF10_DMA_STAT(tx_pause, TX_PAUSE_PKTS),
	EF10_DMA_STAT(tx_control, TX_CONTROL_PKTS),
	EF10_DMA_STAT(tx_unicast, TX_UNICAST_PKTS),
	EF10_DMA_STAT(tx_multicast, TX_MULTICAST_PKTS),
	EF10_DMA_STAT(tx_broadcast, TX_BROADCAST_PKTS),
	EF10_DMA_STAT(tx_lt64, TX_LT64_PKTS),
	EF10_DMA_STAT(tx_64, TX_64_PKTS),
	EF10_DMA_STAT(tx_65_to_127, TX_65_TO_127_PKTS),
	EF10_DMA_STAT(tx_128_to_255, TX_128_TO_255_PKTS),
	EF10_DMA_STAT(tx_256_to_511, TX_256_TO_511_PKTS),
	EF10_DMA_STAT(tx_512_to_1023, TX_512_TO_1023_PKTS),
	EF10_DMA_STAT(tx_1024_to_15xx, TX_1024_TO_15XX_PKTS),
	EF10_DMA_STAT(tx_15xx_to_jumbo, TX_15XX_TO_JUMBO_PKTS),
	EF10_DMA_STAT(rx_bytes, RX_BYTES),
	EF10_DMA_INVIS_STAT(rx_bytes_minus_good_bytes, RX_BAD_BYTES),
	EF10_OTHER_STAT(rx_good_bytes),
	EF10_OTHER_STAT(rx_bad_bytes),
	EF10_DMA_STAT(rx_packets, RX_PKTS),
	EF10_DMA_STAT(rx_good, RX_GOOD_PKTS),
	EF10_DMA_STAT(rx_bad, RX_BAD_FCS_PKTS),
	EF10_DMA_STAT(rx_pause, RX_PAUSE_PKTS),
	EF10_DMA_STAT(rx_control, RX_CONTROL_PKTS),
	EF10_DMA_STAT(rx_unicast, RX_UNICAST_PKTS),
	EF10_DMA_STAT(rx_multicast, RX_MULTICAST_PKTS),
	EF10_DMA_STAT(rx_broadcast, RX_BROADCAST_PKTS),
	EF10_DMA_STAT(rx_lt64, RX_UNDERSIZE_PKTS),
	EF10_DMA_STAT(rx_64, RX_64_PKTS),
	EF10_DMA_STAT(rx_65_to_127, RX_65_TO_127_PKTS),
	EF10_DMA_STAT(rx_128_to_255, RX_128_TO_255_PKTS),
	EF10_DMA_STAT(rx_256_to_511, RX_256_TO_511_PKTS),
	EF10_DMA_STAT(rx_512_to_1023, RX_512_TO_1023_PKTS),
	EF10_DMA_STAT(rx_1024_to_15xx, RX_1024_TO_15XX_PKTS),
	EF10_DMA_STAT(rx_15xx_to_jumbo, RX_15XX_TO_JUMBO_PKTS),
	EF10_DMA_STAT(rx_gtjumbo, RX_GTJUMBO_PKTS),
	EF10_DMA_STAT(rx_bad_gtjumbo, RX_JABBER_PKTS),
	EF10_DMA_STAT(rx_overflow, RX_OVERFLOW_PKTS),
	EF10_DMA_STAT(rx_align_error, RX_ALIGN_ERROR_PKTS),
	EF10_DMA_STAT(rx_length_error, RX_LENGTH_ERROR_PKTS),
	EF10_DMA_STAT(rx_nodesc_drops, RX_NODESC_DROPS),
};

#define HUNT_COMMON_STAT_MASK ((1ULL << EF10_STAT_tx_bytes) |		\
			       (1ULL << EF10_STAT_tx_packets) |		\
			       (1ULL << EF10_STAT_tx_pause) |		\
			       (1ULL << EF10_STAT_tx_unicast) |		\
			       (1ULL << EF10_STAT_tx_multicast) |	\
			       (1ULL << EF10_STAT_tx_broadcast) |	\
			       (1ULL << EF10_STAT_rx_bytes) |		\
			       (1ULL << EF10_STAT_rx_bytes_minus_good_bytes) | \
			       (1ULL << EF10_STAT_rx_good_bytes) |	\
			       (1ULL << EF10_STAT_rx_bad_bytes) |	\
			       (1ULL << EF10_STAT_rx_packets) |		\
			       (1ULL << EF10_STAT_rx_good) |		\
			       (1ULL << EF10_STAT_rx_bad) |		\
			       (1ULL << EF10_STAT_rx_pause) |		\
			       (1ULL << EF10_STAT_rx_control) |		\
			       (1ULL << EF10_STAT_rx_unicast) |		\
			       (1ULL << EF10_STAT_rx_multicast) |	\
			       (1ULL << EF10_STAT_rx_broadcast) |	\
			       (1ULL << EF10_STAT_rx_lt64) |		\
			       (1ULL << EF10_STAT_rx_64) |		\
			       (1ULL << EF10_STAT_rx_65_to_127) |	\
			       (1ULL << EF10_STAT_rx_128_to_255) |	\
			       (1ULL << EF10_STAT_rx_256_to_511) |	\
			       (1ULL << EF10_STAT_rx_512_to_1023) |	\
			       (1ULL << EF10_STAT_rx_1024_to_15xx) |	\
			       (1ULL << EF10_STAT_rx_15xx_to_jumbo) |	\
			       (1ULL << EF10_STAT_rx_gtjumbo) |		\
			       (1ULL << EF10_STAT_rx_bad_gtjumbo) |	\
			       (1ULL << EF10_STAT_rx_overflow) |	\
			       (1ULL << EF10_STAT_rx_nodesc_drops))

/* These statistics are only provided by the 10G MAC.  For a 10G/40G
 * switchable port we do not expose these because they might not
 * include all the packets they should.
 */
#define HUNT_10G_ONLY_STAT_MASK ((1ULL << EF10_STAT_tx_control) |	\
				 (1ULL << EF10_STAT_tx_lt64) |		\
				 (1ULL << EF10_STAT_tx_64) |		\
				 (1ULL << EF10_STAT_tx_65_to_127) |	\
				 (1ULL << EF10_STAT_tx_128_to_255) |	\
				 (1ULL << EF10_STAT_tx_256_to_511) |	\
				 (1ULL << EF10_STAT_tx_512_to_1023) |	\
				 (1ULL << EF10_STAT_tx_1024_to_15xx) |	\
				 (1ULL << EF10_STAT_tx_15xx_to_jumbo))

/* These statistics are only provided by the 40G MAC.  For a 10G/40G
 * switchable port we do expose these because the errors will otherwise
 * be silent.
 */
#define HUNT_40G_EXTRA_STAT_MASK ((1ULL << EF10_STAT_rx_align_error) |	\
				  (1ULL << EF10_STAT_rx_length_error))

#if BITS_PER_LONG == 64
#define STAT_MASK_BITMAP(bits) (bits)
#else
#define STAT_MASK_BITMAP(bits) (bits) & 0xffffffff, (bits) >> 32
#endif

static const unsigned long *efx_ef10_stat_mask(struct efx_nic *efx)
{
	static const unsigned long hunt_40g_stat_mask[] = {
		STAT_MASK_BITMAP(HUNT_COMMON_STAT_MASK |
				 HUNT_40G_EXTRA_STAT_MASK)
	};
	static const unsigned long hunt_10g_only_stat_mask[] = {
		STAT_MASK_BITMAP(HUNT_COMMON_STAT_MASK |
				 HUNT_10G_ONLY_STAT_MASK)
	};
	u32 port_caps = efx_mcdi_phy_get_caps(efx);

	if (port_caps & (1 << MC_CMD_PHY_CAP_40000FDX_LBN))
		return hunt_40g_stat_mask;
	else
		return hunt_10g_only_stat_mask;
}

static size_t efx_ef10_describe_stats(struct efx_nic *efx, u8 *names)
{
	return efx_nic_describe_stats(efx_ef10_stat_desc, EF10_STAT_COUNT,
				      efx_ef10_stat_mask(efx), names);
}

static int efx_ef10_try_update_nic_stats(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	const unsigned long *stats_mask = efx_ef10_stat_mask(efx);
	__le64 generation_start, generation_end;
	u64 *stats = nic_data->stats;
	__le64 *dma_stats;

	dma_stats = efx->stats_buffer.addr;
	nic_data = efx->nic_data;

	generation_end = dma_stats[MC_CMD_MAC_GENERATION_END];
	if (generation_end == EFX_MC_STATS_GENERATION_INVALID)
		return 0;
	rmb();
	efx_nic_update_stats(efx_ef10_stat_desc, EF10_STAT_COUNT, stats_mask,
			     stats, efx->stats_buffer.addr, false);
	generation_start = dma_stats[MC_CMD_MAC_GENERATION_START];
	if (generation_end != generation_start)
		return -EAGAIN;

	/* Update derived statistics */
	stats[EF10_STAT_rx_good_bytes] =
		stats[EF10_STAT_rx_bytes] -
		stats[EF10_STAT_rx_bytes_minus_good_bytes];
	efx_update_diff_stat(&stats[EF10_STAT_rx_bad_bytes],
			     stats[EF10_STAT_rx_bytes_minus_good_bytes]);

	return 0;
}


static size_t efx_ef10_update_stats(struct efx_nic *efx, u64 *full_stats,
				    struct rtnl_link_stats64 *core_stats)
{
	const unsigned long *mask = efx_ef10_stat_mask(efx);
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	u64 *stats = nic_data->stats;
	size_t stats_count = 0, index;
	int retry;

	/* If we're unlucky enough to read statistics during the DMA, wait
	 * up to 10ms for it to finish (typically takes <500us)
	 */
	for (retry = 0; retry < 100; ++retry) {
		if (efx_ef10_try_update_nic_stats(efx) == 0)
			break;
		udelay(100);
	}

	if (full_stats) {
		for_each_set_bit(index, mask, EF10_STAT_COUNT) {
			if (efx_ef10_stat_desc[index].name) {
				*full_stats++ = stats[index];
				++stats_count;
			}
		}
	}

	if (core_stats) {
		core_stats->rx_packets = stats[EF10_STAT_rx_packets];
		core_stats->tx_packets = stats[EF10_STAT_tx_packets];
		core_stats->rx_bytes = stats[EF10_STAT_rx_bytes];
		core_stats->tx_bytes = stats[EF10_STAT_tx_bytes];
		core_stats->rx_dropped = stats[EF10_STAT_rx_nodesc_drops];
		core_stats->multicast = stats[EF10_STAT_rx_multicast];
		core_stats->rx_length_errors =
			stats[EF10_STAT_rx_gtjumbo] +
			stats[EF10_STAT_rx_length_error];
		core_stats->rx_crc_errors = stats[EF10_STAT_rx_bad];
		core_stats->rx_frame_errors = stats[EF10_STAT_rx_align_error];
		core_stats->rx_fifo_errors = stats[EF10_STAT_rx_overflow];
		core_stats->rx_errors = (core_stats->rx_length_errors +
					 core_stats->rx_crc_errors +
					 core_stats->rx_frame_errors);
	}

	return stats_count;
}

static void efx_ef10_push_irq_moderation(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	unsigned int mode, value;
	efx_dword_t timer_cmd;

	if (channel->irq_moderation) {
		mode = 3;
		value = channel->irq_moderation - 1;
	} else {
		mode = 0;
		value = 0;
	}

	if (EFX_EF10_WORKAROUND_35388(efx)) {
		EFX_POPULATE_DWORD_3(timer_cmd, ERF_DD_EVQ_IND_TIMER_FLAGS,
				     EFE_DD_EVQ_IND_TIMER_FLAGS,
				     ERF_DD_EVQ_IND_TIMER_MODE, mode,
				     ERF_DD_EVQ_IND_TIMER_VAL, value);
		efx_writed_page(efx, &timer_cmd, ER_DD_EVQ_INDIRECT,
				channel->channel);
	} else {
		EFX_POPULATE_DWORD_2(timer_cmd, ERF_DZ_TC_TIMER_MODE, mode,
				     ERF_DZ_TC_TIMER_VAL, value);
		efx_writed_page(efx, &timer_cmd, ER_DZ_EVQ_TMR,
				channel->channel);
	}
}

static void efx_ef10_get_wol(struct efx_nic *efx, struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int efx_ef10_set_wol(struct efx_nic *efx, u32 type)
{
	if (type != 0)
		return -EINVAL;
	return 0;
}

static void efx_ef10_mcdi_request(struct efx_nic *efx,
				  const efx_dword_t *hdr, size_t hdr_len,
				  const efx_dword_t *sdu, size_t sdu_len)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	u8 *pdu = nic_data->mcdi_buf.addr;

	memcpy(pdu, hdr, hdr_len);
	memcpy(pdu + hdr_len, sdu, sdu_len);
	wmb();

	/* The hardware provides 'low' and 'high' (doorbell) registers
	 * for passing the 64-bit address of an MCDI request to
	 * firmware.  However the dwords are swapped by firmware.  The
	 * least significant bits of the doorbell are then 0 for all
	 * MCDI requests due to alignment.
	 */
	_efx_writed(efx, cpu_to_le32((u64)nic_data->mcdi_buf.dma_addr >> 32),
		    ER_DZ_MC_DB_LWRD);
	_efx_writed(efx, cpu_to_le32((u32)nic_data->mcdi_buf.dma_addr),
		    ER_DZ_MC_DB_HWRD);
}

static bool efx_ef10_mcdi_poll_response(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	const efx_dword_t hdr = *(const efx_dword_t *)nic_data->mcdi_buf.addr;

	rmb();
	return EFX_DWORD_FIELD(hdr, MCDI_HEADER_RESPONSE);
}

static void
efx_ef10_mcdi_read_response(struct efx_nic *efx, efx_dword_t *outbuf,
			    size_t offset, size_t outlen)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	const u8 *pdu = nic_data->mcdi_buf.addr;

	memcpy(outbuf, pdu + offset, outlen);
}

static int efx_ef10_mcdi_poll_reboot(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = efx_ef10_get_warm_boot_count(efx);
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

	/* All our allocations have been reset */
	nic_data->must_realloc_vis = true;
	nic_data->must_restore_filters = true;
	nic_data->rx_rss_context = EFX_EF10_RSS_CONTEXT_INVALID;

	/* The datapath firmware might have been changed */
	nic_data->must_check_datapath_caps = true;

	/* MAC statistics have been cleared on the NIC; clear the local
	 * statistic that we update with efx_update_diff_stat().
	 */
	nic_data->stats[EF10_STAT_rx_bad_bytes] = 0;

	return -EIO;
}

/* Handle an MSI interrupt
 *
 * Handle an MSI hardware interrupt.  This routine schedules event
 * queue processing.  No interrupt acknowledgement cycle is necessary.
 * Also, we never need to check that the interrupt is for us, since
 * MSI interrupts cannot be shared.
 */
static irqreturn_t efx_ef10_msi_interrupt(int irq, void *dev_id)
{
	struct efx_msi_context *context = dev_id;
	struct efx_nic *efx = context->efx;

	netif_vdbg(efx, intr, efx->net_dev,
		   "IRQ %d on CPU %d\n", irq, raw_smp_processor_id());

	if (likely(ACCESS_ONCE(efx->irq_soft_enabled))) {
		/* Note test interrupts */
		if (context->index == efx->irq_level)
			efx->last_irq_cpu = raw_smp_processor_id();

		/* Schedule processing of the channel */
		efx_schedule_channel_irq(efx->channel[context->index]);
	}

	return IRQ_HANDLED;
}

static irqreturn_t efx_ef10_legacy_interrupt(int irq, void *dev_id)
{
	struct efx_nic *efx = dev_id;
	bool soft_enabled = ACCESS_ONCE(efx->irq_soft_enabled);
	struct efx_channel *channel;
	efx_dword_t reg;
	u32 queues;

	/* Read the ISR which also ACKs the interrupts */
	efx_readd(efx, &reg, ER_DZ_BIU_INT_ISR);
	queues = EFX_DWORD_FIELD(reg, ERF_DZ_ISR_REG);

	if (queues == 0)
		return IRQ_NONE;

	if (likely(soft_enabled)) {
		/* Note test interrupts */
		if (queues & (1U << efx->irq_level))
			efx->last_irq_cpu = raw_smp_processor_id();

		efx_for_each_channel(channel, efx) {
			if (queues & 1)
				efx_schedule_channel_irq(channel);
			queues >>= 1;
		}
	}

	netif_vdbg(efx, intr, efx->net_dev,
		   "IRQ %d on CPU %d status " EFX_DWORD_FMT "\n",
		   irq, raw_smp_processor_id(), EFX_DWORD_VAL(reg));

	return IRQ_HANDLED;
}

static void efx_ef10_irq_test_generate(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_TRIGGER_INTERRUPT_IN_LEN);

	BUILD_BUG_ON(MC_CMD_TRIGGER_INTERRUPT_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, TRIGGER_INTERRUPT_IN_INTR_LEVEL, efx->irq_level);
	(void) efx_mcdi_rpc(efx, MC_CMD_TRIGGER_INTERRUPT,
			    inbuf, sizeof(inbuf), NULL, 0, NULL);
}

static int efx_ef10_tx_probe(struct efx_tx_queue *tx_queue)
{
	return efx_nic_alloc_buffer(tx_queue->efx, &tx_queue->txd.buf,
				    (tx_queue->ptr_mask + 1) *
				    sizeof(efx_qword_t),
				    GFP_KERNEL);
}

/* This writes to the TX_DESC_WPTR and also pushes data */
static inline void efx_ef10_push_tx_desc(struct efx_tx_queue *tx_queue,
					 const efx_qword_t *txd)
{
	unsigned int write_ptr;
	efx_oword_t reg;

	write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
	EFX_POPULATE_OWORD_1(reg, ERF_DZ_TX_DESC_WPTR, write_ptr);
	reg.qword[0] = *txd;
	efx_writeo_page(tx_queue->efx, &reg,
			ER_DZ_TX_DESC_UPD, tx_queue->queue);
}

static void efx_ef10_tx_init(struct efx_tx_queue *tx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_INIT_TXQ_IN_LEN(EFX_MAX_DMAQ_SIZE * 8 /
						       EFX_BUF_SIZE));
	MCDI_DECLARE_BUF(outbuf, MC_CMD_INIT_TXQ_OUT_LEN);
	bool csum_offload = tx_queue->queue & EFX_TXQ_TYPE_OFFLOAD;
	size_t entries = tx_queue->txd.buf.len / EFX_BUF_SIZE;
	struct efx_channel *channel = tx_queue->channel;
	struct efx_nic *efx = tx_queue->efx;
	size_t inlen, outlen;
	dma_addr_t dma_addr;
	efx_qword_t *txd;
	int rc;
	int i;

	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_SIZE, tx_queue->ptr_mask + 1);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_TARGET_EVQ, channel->channel);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_LABEL, tx_queue->queue);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_INSTANCE, tx_queue->queue);
	MCDI_POPULATE_DWORD_2(inbuf, INIT_TXQ_IN_FLAGS,
			      INIT_TXQ_IN_FLAG_IP_CSUM_DIS, !csum_offload,
			      INIT_TXQ_IN_FLAG_TCP_CSUM_DIS, !csum_offload);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_OWNER_ID, 0);
	MCDI_SET_DWORD(inbuf, INIT_TXQ_IN_PORT_ID, EVB_PORT_ID_ASSIGNED);

	dma_addr = tx_queue->txd.buf.dma_addr;

	netif_dbg(efx, hw, efx->net_dev, "pushing TXQ %d. %zu entries (%llx)\n",
		  tx_queue->queue, entries, (u64)dma_addr);

	for (i = 0; i < entries; ++i) {
		MCDI_SET_ARRAY_QWORD(inbuf, INIT_TXQ_IN_DMA_ADDR, i, dma_addr);
		dma_addr += EFX_BUF_SIZE;
	}

	inlen = MC_CMD_INIT_TXQ_IN_LEN(entries);

	rc = efx_mcdi_rpc(efx, MC_CMD_INIT_TXQ, inbuf, inlen,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	/* A previous user of this TX queue might have set us up the
	 * bomb by writing a descriptor to the TX push collector but
	 * not the doorbell.  (Each collector belongs to a port, not a
	 * queue or function, so cannot easily be reset.)  We must
	 * attempt to push a no-op descriptor in its place.
	 */
	tx_queue->buffer[0].flags = EFX_TX_BUF_OPTION;
	tx_queue->insert_count = 1;
	txd = efx_tx_desc(tx_queue, 0);
	EFX_POPULATE_QWORD_4(*txd,
			     ESF_DZ_TX_DESC_IS_OPT, true,
			     ESF_DZ_TX_OPTION_TYPE,
			     ESE_DZ_TX_OPTION_DESC_CRC_CSUM,
			     ESF_DZ_TX_OPTION_UDP_TCP_CSUM, csum_offload,
			     ESF_DZ_TX_OPTION_IP_CSUM, csum_offload);
	tx_queue->write_count = 1;
	wmb();
	efx_ef10_push_tx_desc(tx_queue, txd);

	return;

fail:
	WARN_ON(true);
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static void efx_ef10_tx_fini(struct efx_tx_queue *tx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FINI_TXQ_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FINI_TXQ_OUT_LEN);
	struct efx_nic *efx = tx_queue->efx;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, FINI_TXQ_IN_INSTANCE,
		       tx_queue->queue);

	rc = efx_mcdi_rpc(efx, MC_CMD_FINI_TXQ, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);

	if (rc && rc != -EALREADY)
		goto fail;

	return;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static void efx_ef10_tx_remove(struct efx_tx_queue *tx_queue)
{
	efx_nic_free_buffer(tx_queue->efx, &tx_queue->txd.buf);
}

/* This writes to the TX_DESC_WPTR; write pointer for TX descriptor ring */
static inline void efx_ef10_notify_tx_desc(struct efx_tx_queue *tx_queue)
{
	unsigned int write_ptr;
	efx_dword_t reg;

	write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
	EFX_POPULATE_DWORD_1(reg, ERF_DZ_TX_DESC_WPTR_DWORD, write_ptr);
	efx_writed_page(tx_queue->efx, &reg,
			ER_DZ_TX_DESC_UPD_DWORD, tx_queue->queue);
}

static void efx_ef10_tx_write(struct efx_tx_queue *tx_queue)
{
	unsigned int old_write_count = tx_queue->write_count;
	struct efx_tx_buffer *buffer;
	unsigned int write_ptr;
	efx_qword_t *txd;

	BUG_ON(tx_queue->write_count == tx_queue->insert_count);

	do {
		write_ptr = tx_queue->write_count & tx_queue->ptr_mask;
		buffer = &tx_queue->buffer[write_ptr];
		txd = efx_tx_desc(tx_queue, write_ptr);
		++tx_queue->write_count;

		/* Create TX descriptor ring entry */
		if (buffer->flags & EFX_TX_BUF_OPTION) {
			*txd = buffer->option;
		} else {
			BUILD_BUG_ON(EFX_TX_BUF_CONT != 1);
			EFX_POPULATE_QWORD_3(
				*txd,
				ESF_DZ_TX_KER_CONT,
				buffer->flags & EFX_TX_BUF_CONT,
				ESF_DZ_TX_KER_BYTE_CNT, buffer->len,
				ESF_DZ_TX_KER_BUF_ADDR, buffer->dma_addr);
		}
	} while (tx_queue->write_count != tx_queue->insert_count);

	wmb(); /* Ensure descriptors are written before they are fetched */

	if (efx_nic_may_push_tx_desc(tx_queue, old_write_count)) {
		txd = efx_tx_desc(tx_queue,
				  old_write_count & tx_queue->ptr_mask);
		efx_ef10_push_tx_desc(tx_queue, txd);
		++tx_queue->pushes;
	} else {
		efx_ef10_notify_tx_desc(tx_queue);
	}
}

static int efx_ef10_alloc_rss_context(struct efx_nic *efx, u32 *context)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_ALLOC_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_RSS_CONTEXT_ALLOC_OUT_LEN);
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_ALLOC_IN_UPSTREAM_PORT_ID,
		       EVB_PORT_ID_ASSIGNED);
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_ALLOC_IN_TYPE,
		       MC_CMD_RSS_CONTEXT_ALLOC_IN_TYPE_EXCLUSIVE);
	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_ALLOC_IN_NUM_QUEUES,
		       EFX_MAX_CHANNELS);

	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_ALLOC, inbuf, sizeof(inbuf),
		outbuf, sizeof(outbuf), &outlen);
	if (rc != 0)
		return rc;

	if (outlen < MC_CMD_RSS_CONTEXT_ALLOC_OUT_LEN)
		return -EIO;

	*context = MCDI_DWORD(outbuf, RSS_CONTEXT_ALLOC_OUT_RSS_CONTEXT_ID);

	return 0;
}

static void efx_ef10_free_rss_context(struct efx_nic *efx, u32 context)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_RSS_CONTEXT_FREE_IN_LEN);
	int rc;

	MCDI_SET_DWORD(inbuf, RSS_CONTEXT_FREE_IN_RSS_CONTEXT_ID,
		       context);

	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_FREE, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
	WARN_ON(rc != 0);
}

static int efx_ef10_populate_rss_table(struct efx_nic *efx, u32 context)
{
	MCDI_DECLARE_BUF(tablebuf, MC_CMD_RSS_CONTEXT_SET_TABLE_IN_LEN);
	MCDI_DECLARE_BUF(keybuf, MC_CMD_RSS_CONTEXT_SET_KEY_IN_LEN);
	int i, rc;

	MCDI_SET_DWORD(tablebuf, RSS_CONTEXT_SET_TABLE_IN_RSS_CONTEXT_ID,
		       context);
	BUILD_BUG_ON(ARRAY_SIZE(efx->rx_indir_table) !=
		     MC_CMD_RSS_CONTEXT_SET_TABLE_IN_INDIRECTION_TABLE_LEN);

	for (i = 0; i < ARRAY_SIZE(efx->rx_indir_table); ++i)
		MCDI_PTR(tablebuf,
			 RSS_CONTEXT_SET_TABLE_IN_INDIRECTION_TABLE)[i] =
				(u8) efx->rx_indir_table[i];

	rc = efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_SET_TABLE, tablebuf,
			  sizeof(tablebuf), NULL, 0, NULL);
	if (rc != 0)
		return rc;

	MCDI_SET_DWORD(keybuf, RSS_CONTEXT_SET_KEY_IN_RSS_CONTEXT_ID,
		       context);
	BUILD_BUG_ON(ARRAY_SIZE(efx->rx_hash_key) !=
		     MC_CMD_RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY_LEN);
	for (i = 0; i < ARRAY_SIZE(efx->rx_hash_key); ++i)
		MCDI_PTR(keybuf, RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY)[i] =
			efx->rx_hash_key[i];

	return efx_mcdi_rpc(efx, MC_CMD_RSS_CONTEXT_SET_KEY, keybuf,
			    sizeof(keybuf), NULL, 0, NULL);
}

static void efx_ef10_rx_free_indir_table(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	if (nic_data->rx_rss_context != EFX_EF10_RSS_CONTEXT_INVALID)
		efx_ef10_free_rss_context(efx, nic_data->rx_rss_context);
	nic_data->rx_rss_context = EFX_EF10_RSS_CONTEXT_INVALID;
}

static void efx_ef10_rx_push_indir_table(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	netif_dbg(efx, drv, efx->net_dev, "pushing RX indirection table\n");

	if (nic_data->rx_rss_context == EFX_EF10_RSS_CONTEXT_INVALID) {
		rc = efx_ef10_alloc_rss_context(efx, &nic_data->rx_rss_context);
		if (rc != 0)
			goto fail;
	}

	rc = efx_ef10_populate_rss_table(efx, nic_data->rx_rss_context);
	if (rc != 0)
		goto fail;

	return;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static int efx_ef10_rx_probe(struct efx_rx_queue *rx_queue)
{
	return efx_nic_alloc_buffer(rx_queue->efx, &rx_queue->rxd.buf,
				    (rx_queue->ptr_mask + 1) *
				    sizeof(efx_qword_t),
				    GFP_KERNEL);
}

static void efx_ef10_rx_init(struct efx_rx_queue *rx_queue)
{
	MCDI_DECLARE_BUF(inbuf,
			 MC_CMD_INIT_RXQ_IN_LEN(EFX_MAX_DMAQ_SIZE * 8 /
						EFX_BUF_SIZE));
	MCDI_DECLARE_BUF(outbuf, MC_CMD_INIT_RXQ_OUT_LEN);
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	size_t entries = rx_queue->rxd.buf.len / EFX_BUF_SIZE;
	struct efx_nic *efx = rx_queue->efx;
	size_t inlen, outlen;
	dma_addr_t dma_addr;
	int rc;
	int i;

	rx_queue->scatter_n = 0;
	rx_queue->scatter_len = 0;

	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_SIZE, rx_queue->ptr_mask + 1);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_TARGET_EVQ, channel->channel);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_LABEL, efx_rx_queue_index(rx_queue));
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_INSTANCE,
		       efx_rx_queue_index(rx_queue));
	MCDI_POPULATE_DWORD_1(inbuf, INIT_RXQ_IN_FLAGS,
			      INIT_RXQ_IN_FLAG_PREFIX, 1);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_OWNER_ID, 0);
	MCDI_SET_DWORD(inbuf, INIT_RXQ_IN_PORT_ID, EVB_PORT_ID_ASSIGNED);

	dma_addr = rx_queue->rxd.buf.dma_addr;

	netif_dbg(efx, hw, efx->net_dev, "pushing RXQ %d. %zu entries (%llx)\n",
		  efx_rx_queue_index(rx_queue), entries, (u64)dma_addr);

	for (i = 0; i < entries; ++i) {
		MCDI_SET_ARRAY_QWORD(inbuf, INIT_RXQ_IN_DMA_ADDR, i, dma_addr);
		dma_addr += EFX_BUF_SIZE;
	}

	inlen = MC_CMD_INIT_RXQ_IN_LEN(entries);

	rc = efx_mcdi_rpc(efx, MC_CMD_INIT_RXQ, inbuf, inlen,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	return;

fail:
	WARN_ON(true);
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static void efx_ef10_rx_fini(struct efx_rx_queue *rx_queue)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FINI_RXQ_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FINI_RXQ_OUT_LEN);
	struct efx_nic *efx = rx_queue->efx;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, FINI_RXQ_IN_INSTANCE,
		       efx_rx_queue_index(rx_queue));

	rc = efx_mcdi_rpc(efx, MC_CMD_FINI_RXQ, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);

	if (rc && rc != -EALREADY)
		goto fail;

	return;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static void efx_ef10_rx_remove(struct efx_rx_queue *rx_queue)
{
	efx_nic_free_buffer(rx_queue->efx, &rx_queue->rxd.buf);
}

/* This creates an entry in the RX descriptor queue */
static inline void
efx_ef10_build_rx_desc(struct efx_rx_queue *rx_queue, unsigned int index)
{
	struct efx_rx_buffer *rx_buf;
	efx_qword_t *rxd;

	rxd = efx_rx_desc(rx_queue, index);
	rx_buf = efx_rx_buffer(rx_queue, index);
	EFX_POPULATE_QWORD_2(*rxd,
			     ESF_DZ_RX_KER_BYTE_CNT, rx_buf->len,
			     ESF_DZ_RX_KER_BUF_ADDR, rx_buf->dma_addr);
}

static void efx_ef10_rx_write(struct efx_rx_queue *rx_queue)
{
	struct efx_nic *efx = rx_queue->efx;
	unsigned int write_count;
	efx_dword_t reg;

	/* Firmware requires that RX_DESC_WPTR be a multiple of 8 */
	write_count = rx_queue->added_count & ~7;
	if (rx_queue->notified_count == write_count)
		return;

	do
		efx_ef10_build_rx_desc(
			rx_queue,
			rx_queue->notified_count & rx_queue->ptr_mask);
	while (++rx_queue->notified_count != write_count);

	wmb();
	EFX_POPULATE_DWORD_1(reg, ERF_DZ_RX_DESC_WPTR,
			     write_count & rx_queue->ptr_mask);
	efx_writed_page(efx, &reg, ER_DZ_RX_DESC_UPD,
			efx_rx_queue_index(rx_queue));
}

static efx_mcdi_async_completer efx_ef10_rx_defer_refill_complete;

static void efx_ef10_rx_defer_refill(struct efx_rx_queue *rx_queue)
{
	struct efx_channel *channel = efx_rx_queue_channel(rx_queue);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_DRIVER_EVENT_IN_LEN);
	efx_qword_t event;

	EFX_POPULATE_QWORD_2(event,
			     ESF_DZ_EV_CODE, EFX_EF10_DRVGEN_EV,
			     ESF_DZ_EV_DATA, EFX_EF10_REFILL);

	MCDI_SET_DWORD(inbuf, DRIVER_EVENT_IN_EVQ, channel->channel);

	/* MCDI_SET_QWORD is not appropriate here since EFX_POPULATE_* has
	 * already swapped the data to little-endian order.
	 */
	memcpy(MCDI_PTR(inbuf, DRIVER_EVENT_IN_DATA), &event.u64[0],
	       sizeof(efx_qword_t));

	efx_mcdi_rpc_async(channel->efx, MC_CMD_DRIVER_EVENT,
			   inbuf, sizeof(inbuf), 0,
			   efx_ef10_rx_defer_refill_complete, 0);
}

static void
efx_ef10_rx_defer_refill_complete(struct efx_nic *efx, unsigned long cookie,
				  int rc, efx_dword_t *outbuf,
				  size_t outlen_actual)
{
	/* nothing to do */
}

static int efx_ef10_ev_probe(struct efx_channel *channel)
{
	return efx_nic_alloc_buffer(channel->efx, &channel->eventq.buf,
				    (channel->eventq_mask + 1) *
				    sizeof(efx_qword_t),
				    GFP_KERNEL);
}

static int efx_ef10_ev_init(struct efx_channel *channel)
{
	MCDI_DECLARE_BUF(inbuf,
			 MC_CMD_INIT_EVQ_IN_LEN(EFX_MAX_EVQ_SIZE * 8 /
						EFX_BUF_SIZE));
	MCDI_DECLARE_BUF(outbuf, MC_CMD_INIT_EVQ_OUT_LEN);
	size_t entries = channel->eventq.buf.len / EFX_BUF_SIZE;
	struct efx_nic *efx = channel->efx;
	struct efx_ef10_nic_data *nic_data;
	bool supports_rx_merge;
	size_t inlen, outlen;
	dma_addr_t dma_addr;
	int rc;
	int i;

	nic_data = efx->nic_data;
	supports_rx_merge =
		!!(nic_data->datapath_caps &
		   1 << MC_CMD_GET_CAPABILITIES_OUT_RX_BATCHING_LBN);

	/* Fill event queue with all ones (i.e. empty events) */
	memset(channel->eventq.buf.addr, 0xff, channel->eventq.buf.len);

	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_SIZE, channel->eventq_mask + 1);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_INSTANCE, channel->channel);
	/* INIT_EVQ expects index in vector table, not absolute */
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_IRQ_NUM, channel->channel);
	MCDI_POPULATE_DWORD_4(inbuf, INIT_EVQ_IN_FLAGS,
			      INIT_EVQ_IN_FLAG_INTERRUPTING, 1,
			      INIT_EVQ_IN_FLAG_RX_MERGE, 1,
			      INIT_EVQ_IN_FLAG_TX_MERGE, 1,
			      INIT_EVQ_IN_FLAG_CUT_THRU, !supports_rx_merge);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_TMR_MODE,
		       MC_CMD_INIT_EVQ_IN_TMR_MODE_DIS);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_TMR_LOAD, 0);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_TMR_RELOAD, 0);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_COUNT_MODE,
		       MC_CMD_INIT_EVQ_IN_COUNT_MODE_DIS);
	MCDI_SET_DWORD(inbuf, INIT_EVQ_IN_COUNT_THRSHLD, 0);

	dma_addr = channel->eventq.buf.dma_addr;
	for (i = 0; i < entries; ++i) {
		MCDI_SET_ARRAY_QWORD(inbuf, INIT_EVQ_IN_DMA_ADDR, i, dma_addr);
		dma_addr += EFX_BUF_SIZE;
	}

	inlen = MC_CMD_INIT_EVQ_IN_LEN(entries);

	rc = efx_mcdi_rpc(efx, MC_CMD_INIT_EVQ, inbuf, inlen,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	/* IRQ return is ignored */

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static void efx_ef10_ev_fini(struct efx_channel *channel)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FINI_EVQ_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FINI_EVQ_OUT_LEN);
	struct efx_nic *efx = channel->efx;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, FINI_EVQ_IN_INSTANCE, channel->channel);

	rc = efx_mcdi_rpc(efx, MC_CMD_FINI_EVQ, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);

	if (rc && rc != -EALREADY)
		goto fail;

	return;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

static void efx_ef10_ev_remove(struct efx_channel *channel)
{
	efx_nic_free_buffer(channel->efx, &channel->eventq.buf);
}

static void efx_ef10_handle_rx_wrong_queue(struct efx_rx_queue *rx_queue,
					   unsigned int rx_queue_label)
{
	struct efx_nic *efx = rx_queue->efx;

	netif_info(efx, hw, efx->net_dev,
		   "rx event arrived on queue %d labeled as queue %u\n",
		   efx_rx_queue_index(rx_queue), rx_queue_label);

	efx_schedule_reset(efx, RESET_TYPE_DISABLE);
}

static void
efx_ef10_handle_rx_bad_lbits(struct efx_rx_queue *rx_queue,
			     unsigned int actual, unsigned int expected)
{
	unsigned int dropped = (actual - expected) & rx_queue->ptr_mask;
	struct efx_nic *efx = rx_queue->efx;

	netif_info(efx, hw, efx->net_dev,
		   "dropped %d events (index=%d expected=%d)\n",
		   dropped, actual, expected);

	efx_schedule_reset(efx, RESET_TYPE_DISABLE);
}

/* partially received RX was aborted. clean up. */
static void efx_ef10_handle_rx_abort(struct efx_rx_queue *rx_queue)
{
	unsigned int rx_desc_ptr;

	WARN_ON(rx_queue->scatter_n == 0);

	netif_dbg(rx_queue->efx, hw, rx_queue->efx->net_dev,
		  "scattered RX aborted (dropping %u buffers)\n",
		  rx_queue->scatter_n);

	rx_desc_ptr = rx_queue->removed_count & rx_queue->ptr_mask;

	efx_rx_packet(rx_queue, rx_desc_ptr, rx_queue->scatter_n,
		      0, EFX_RX_PKT_DISCARD);

	rx_queue->removed_count += rx_queue->scatter_n;
	rx_queue->scatter_n = 0;
	rx_queue->scatter_len = 0;
	++efx_rx_queue_channel(rx_queue)->n_rx_nodesc_trunc;
}

static int efx_ef10_handle_rx_event(struct efx_channel *channel,
				    const efx_qword_t *event)
{
	unsigned int rx_bytes, next_ptr_lbits, rx_queue_label, rx_l4_class;
	unsigned int n_descs, n_packets, i;
	struct efx_nic *efx = channel->efx;
	struct efx_rx_queue *rx_queue;
	bool rx_cont;
	u16 flags = 0;

	if (unlikely(ACCESS_ONCE(efx->reset_pending)))
		return 0;

	/* Basic packet information */
	rx_bytes = EFX_QWORD_FIELD(*event, ESF_DZ_RX_BYTES);
	next_ptr_lbits = EFX_QWORD_FIELD(*event, ESF_DZ_RX_DSC_PTR_LBITS);
	rx_queue_label = EFX_QWORD_FIELD(*event, ESF_DZ_RX_QLABEL);
	rx_l4_class = EFX_QWORD_FIELD(*event, ESF_DZ_RX_L4_CLASS);
	rx_cont = EFX_QWORD_FIELD(*event, ESF_DZ_RX_CONT);

	WARN_ON(EFX_QWORD_FIELD(*event, ESF_DZ_RX_DROP_EVENT));

	rx_queue = efx_channel_get_rx_queue(channel);

	if (unlikely(rx_queue_label != efx_rx_queue_index(rx_queue)))
		efx_ef10_handle_rx_wrong_queue(rx_queue, rx_queue_label);

	n_descs = ((next_ptr_lbits - rx_queue->removed_count) &
		   ((1 << ESF_DZ_RX_DSC_PTR_LBITS_WIDTH) - 1));

	if (n_descs != rx_queue->scatter_n + 1) {
		/* detect rx abort */
		if (unlikely(n_descs == rx_queue->scatter_n)) {
			WARN_ON(rx_bytes != 0);
			efx_ef10_handle_rx_abort(rx_queue);
			return 0;
		}

		if (unlikely(rx_queue->scatter_n != 0)) {
			/* Scattered packet completions cannot be
			 * merged, so something has gone wrong.
			 */
			efx_ef10_handle_rx_bad_lbits(
				rx_queue, next_ptr_lbits,
				(rx_queue->removed_count +
				 rx_queue->scatter_n + 1) &
				((1 << ESF_DZ_RX_DSC_PTR_LBITS_WIDTH) - 1));
			return 0;
		}

		/* Merged completion for multiple non-scattered packets */
		rx_queue->scatter_n = 1;
		rx_queue->scatter_len = 0;
		n_packets = n_descs;
		++channel->n_rx_merge_events;
		channel->n_rx_merge_packets += n_packets;
		flags |= EFX_RX_PKT_PREFIX_LEN;
	} else {
		++rx_queue->scatter_n;
		rx_queue->scatter_len += rx_bytes;
		if (rx_cont)
			return 0;
		n_packets = 1;
	}

	if (unlikely(EFX_QWORD_FIELD(*event, ESF_DZ_RX_ECRC_ERR)))
		flags |= EFX_RX_PKT_DISCARD;

	if (unlikely(EFX_QWORD_FIELD(*event, ESF_DZ_RX_IPCKSUM_ERR))) {
		channel->n_rx_ip_hdr_chksum_err += n_packets;
	} else if (unlikely(EFX_QWORD_FIELD(*event,
					    ESF_DZ_RX_TCPUDP_CKSUM_ERR))) {
		channel->n_rx_tcp_udp_chksum_err += n_packets;
	} else if (rx_l4_class == ESE_DZ_L4_CLASS_TCP ||
		   rx_l4_class == ESE_DZ_L4_CLASS_UDP) {
		flags |= EFX_RX_PKT_CSUMMED;
	}

	if (rx_l4_class == ESE_DZ_L4_CLASS_TCP)
		flags |= EFX_RX_PKT_TCP;

	channel->irq_mod_score += 2 * n_packets;

	/* Handle received packet(s) */
	for (i = 0; i < n_packets; i++) {
		efx_rx_packet(rx_queue,
			      rx_queue->removed_count & rx_queue->ptr_mask,
			      rx_queue->scatter_n, rx_queue->scatter_len,
			      flags);
		rx_queue->removed_count += rx_queue->scatter_n;
	}

	rx_queue->scatter_n = 0;
	rx_queue->scatter_len = 0;

	return n_packets;
}

static int
efx_ef10_handle_tx_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	struct efx_tx_queue *tx_queue;
	unsigned int tx_ev_desc_ptr;
	unsigned int tx_ev_q_label;
	int tx_descs = 0;

	if (unlikely(ACCESS_ONCE(efx->reset_pending)))
		return 0;

	if (unlikely(EFX_QWORD_FIELD(*event, ESF_DZ_TX_DROP_EVENT)))
		return 0;

	/* Transmit completion */
	tx_ev_desc_ptr = EFX_QWORD_FIELD(*event, ESF_DZ_TX_DESCR_INDX);
	tx_ev_q_label = EFX_QWORD_FIELD(*event, ESF_DZ_TX_QLABEL);
	tx_queue = efx_channel_get_tx_queue(channel,
					    tx_ev_q_label % EFX_TXQ_TYPES);
	tx_descs = ((tx_ev_desc_ptr + 1 - tx_queue->read_count) &
		    tx_queue->ptr_mask);
	efx_xmit_done(tx_queue, tx_ev_desc_ptr & tx_queue->ptr_mask);

	return tx_descs;
}

static void
efx_ef10_handle_driver_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	int subcode;

	subcode = EFX_QWORD_FIELD(*event, ESF_DZ_DRV_SUB_CODE);

	switch (subcode) {
	case ESE_DZ_DRV_TIMER_EV:
	case ESE_DZ_DRV_WAKE_UP_EV:
		break;
	case ESE_DZ_DRV_START_UP_EV:
		/* event queue init complete. ok. */
		break;
	default:
		netif_err(efx, hw, efx->net_dev,
			  "channel %d unknown driver event type %d"
			  " (data " EFX_QWORD_FMT ")\n",
			  channel->channel, subcode,
			  EFX_QWORD_VAL(*event));

	}
}

static void efx_ef10_handle_driver_generated_event(struct efx_channel *channel,
						   efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	u32 subcode;

	subcode = EFX_QWORD_FIELD(*event, EFX_DWORD_0);

	switch (subcode) {
	case EFX_EF10_TEST:
		channel->event_test_cpu = raw_smp_processor_id();
		break;
	case EFX_EF10_REFILL:
		/* The queue must be empty, so we won't receive any rx
		 * events, so efx_process_channel() won't refill the
		 * queue. Refill it here
		 */
		efx_fast_push_rx_descriptors(&channel->rx_queue);
		break;
	default:
		netif_err(efx, hw, efx->net_dev,
			  "channel %d unknown driver event type %u"
			  " (data " EFX_QWORD_FMT ")\n",
			  channel->channel, (unsigned) subcode,
			  EFX_QWORD_VAL(*event));
	}
}

static int efx_ef10_ev_process(struct efx_channel *channel, int quota)
{
	struct efx_nic *efx = channel->efx;
	efx_qword_t event, *p_event;
	unsigned int read_ptr;
	int ev_code;
	int tx_descs = 0;
	int spent = 0;

	read_ptr = channel->eventq_read_ptr;

	for (;;) {
		p_event = efx_event(channel, read_ptr);
		event = *p_event;

		if (!efx_event_present(&event))
			break;

		EFX_SET_QWORD(*p_event);

		++read_ptr;

		ev_code = EFX_QWORD_FIELD(event, ESF_DZ_EV_CODE);

		netif_vdbg(efx, drv, efx->net_dev,
			   "processing event on %d " EFX_QWORD_FMT "\n",
			   channel->channel, EFX_QWORD_VAL(event));

		switch (ev_code) {
		case ESE_DZ_EV_CODE_MCDI_EV:
			efx_mcdi_process_event(channel, &event);
			break;
		case ESE_DZ_EV_CODE_RX_EV:
			spent += efx_ef10_handle_rx_event(channel, &event);
			if (spent >= quota) {
				/* XXX can we split a merged event to
				 * avoid going over-quota?
				 */
				spent = quota;
				goto out;
			}
			break;
		case ESE_DZ_EV_CODE_TX_EV:
			tx_descs += efx_ef10_handle_tx_event(channel, &event);
			if (tx_descs > efx->txq_entries) {
				spent = quota;
				goto out;
			} else if (++spent == quota) {
				goto out;
			}
			break;
		case ESE_DZ_EV_CODE_DRIVER_EV:
			efx_ef10_handle_driver_event(channel, &event);
			if (++spent == quota)
				goto out;
			break;
		case EFX_EF10_DRVGEN_EV:
			efx_ef10_handle_driver_generated_event(channel, &event);
			break;
		default:
			netif_err(efx, hw, efx->net_dev,
				  "channel %d unknown event type %d"
				  " (data " EFX_QWORD_FMT ")\n",
				  channel->channel, ev_code,
				  EFX_QWORD_VAL(event));
		}
	}

out:
	channel->eventq_read_ptr = read_ptr;
	return spent;
}

static void efx_ef10_ev_read_ack(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	efx_dword_t rptr;

	if (EFX_EF10_WORKAROUND_35388(efx)) {
		BUILD_BUG_ON(EFX_MIN_EVQ_SIZE <
			     (1 << ERF_DD_EVQ_IND_RPTR_WIDTH));
		BUILD_BUG_ON(EFX_MAX_EVQ_SIZE >
			     (1 << 2 * ERF_DD_EVQ_IND_RPTR_WIDTH));

		EFX_POPULATE_DWORD_2(rptr, ERF_DD_EVQ_IND_RPTR_FLAGS,
				     EFE_DD_EVQ_IND_RPTR_FLAGS_HIGH,
				     ERF_DD_EVQ_IND_RPTR,
				     (channel->eventq_read_ptr &
				      channel->eventq_mask) >>
				     ERF_DD_EVQ_IND_RPTR_WIDTH);
		efx_writed_page(efx, &rptr, ER_DD_EVQ_INDIRECT,
				channel->channel);
		EFX_POPULATE_DWORD_2(rptr, ERF_DD_EVQ_IND_RPTR_FLAGS,
				     EFE_DD_EVQ_IND_RPTR_FLAGS_LOW,
				     ERF_DD_EVQ_IND_RPTR,
				     channel->eventq_read_ptr &
				     ((1 << ERF_DD_EVQ_IND_RPTR_WIDTH) - 1));
		efx_writed_page(efx, &rptr, ER_DD_EVQ_INDIRECT,
				channel->channel);
	} else {
		EFX_POPULATE_DWORD_1(rptr, ERF_DZ_EVQ_RPTR,
				     channel->eventq_read_ptr &
				     channel->eventq_mask);
		efx_writed_page(efx, &rptr, ER_DZ_EVQ_RPTR, channel->channel);
	}
}

static void efx_ef10_ev_test_generate(struct efx_channel *channel)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_DRIVER_EVENT_IN_LEN);
	struct efx_nic *efx = channel->efx;
	efx_qword_t event;
	int rc;

	EFX_POPULATE_QWORD_2(event,
			     ESF_DZ_EV_CODE, EFX_EF10_DRVGEN_EV,
			     ESF_DZ_EV_DATA, EFX_EF10_TEST);

	MCDI_SET_DWORD(inbuf, DRIVER_EVENT_IN_EVQ, channel->channel);

	/* MCDI_SET_QWORD is not appropriate here since EFX_POPULATE_* has
	 * already swapped the data to little-endian order.
	 */
	memcpy(MCDI_PTR(inbuf, DRIVER_EVENT_IN_DATA), &event.u64[0],
	       sizeof(efx_qword_t));

	rc = efx_mcdi_rpc(efx, MC_CMD_DRIVER_EVENT, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc != 0)
		goto fail;

	return;

fail:
	WARN_ON(true);
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
}

void efx_ef10_handle_drain_event(struct efx_nic *efx)
{
	if (atomic_dec_and_test(&efx->active_queues))
		wake_up(&efx->flush_wq);

	WARN_ON(atomic_read(&efx->active_queues) < 0);
}

static int efx_ef10_fini_dmaq(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	int pending;

	/* If the MC has just rebooted, the TX/RX queues will have already been
	 * torn down, but efx->active_queues needs to be set to zero.
	 */
	if (nic_data->must_realloc_vis) {
		atomic_set(&efx->active_queues, 0);
		return 0;
	}

	/* Do not attempt to write to the NIC during EEH recovery */
	if (efx->state != STATE_RECOVERY) {
		efx_for_each_channel(channel, efx) {
			efx_for_each_channel_rx_queue(rx_queue, channel)
				efx_ef10_rx_fini(rx_queue);
			efx_for_each_channel_tx_queue(tx_queue, channel)
				efx_ef10_tx_fini(tx_queue);
		}

		wait_event_timeout(efx->flush_wq,
				   atomic_read(&efx->active_queues) == 0,
				   msecs_to_jiffies(EFX_MAX_FLUSH_TIME));
		pending = atomic_read(&efx->active_queues);
		if (pending) {
			netif_err(efx, hw, efx->net_dev, "failed to flush %d queues\n",
				  pending);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static bool efx_ef10_filter_equal(const struct efx_filter_spec *left,
				  const struct efx_filter_spec *right)
{
	if ((left->match_flags ^ right->match_flags) |
	    ((left->flags ^ right->flags) &
	     (EFX_FILTER_FLAG_RX | EFX_FILTER_FLAG_TX)))
		return false;

	return memcmp(&left->outer_vid, &right->outer_vid,
		      sizeof(struct efx_filter_spec) -
		      offsetof(struct efx_filter_spec, outer_vid)) == 0;
}

static unsigned int efx_ef10_filter_hash(const struct efx_filter_spec *spec)
{
	BUILD_BUG_ON(offsetof(struct efx_filter_spec, outer_vid) & 3);
	return jhash2((const u32 *)&spec->outer_vid,
		      (sizeof(struct efx_filter_spec) -
		       offsetof(struct efx_filter_spec, outer_vid)) / 4,
		      0);
	/* XXX should we randomise the initval? */
}

/* Decide whether a filter should be exclusive or else should allow
 * delivery to additional recipients.  Currently we decide that
 * filters for specific local unicast MAC and IP addresses are
 * exclusive.
 */
static bool efx_ef10_filter_is_exclusive(const struct efx_filter_spec *spec)
{
	if (spec->match_flags & EFX_FILTER_MATCH_LOC_MAC &&
	    !is_multicast_ether_addr(spec->loc_mac))
		return true;

	if ((spec->match_flags &
	     (EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_LOC_HOST)) ==
	    (EFX_FILTER_MATCH_ETHER_TYPE | EFX_FILTER_MATCH_LOC_HOST)) {
		if (spec->ether_type == htons(ETH_P_IP) &&
		    !ipv4_is_multicast(spec->loc_host[0]))
			return true;
		if (spec->ether_type == htons(ETH_P_IPV6) &&
		    ((const u8 *)spec->loc_host)[0] != 0xff)
			return true;
	}

	return false;
}

static struct efx_filter_spec *
efx_ef10_filter_entry_spec(const struct efx_ef10_filter_table *table,
			   unsigned int filter_idx)
{
	return (struct efx_filter_spec *)(table->entry[filter_idx].spec &
					  ~EFX_EF10_FILTER_FLAGS);
}

static unsigned int
efx_ef10_filter_entry_flags(const struct efx_ef10_filter_table *table,
			   unsigned int filter_idx)
{
	return table->entry[filter_idx].spec & EFX_EF10_FILTER_FLAGS;
}

static void
efx_ef10_filter_set_entry(struct efx_ef10_filter_table *table,
			  unsigned int filter_idx,
			  const struct efx_filter_spec *spec,
			  unsigned int flags)
{
	table->entry[filter_idx].spec =	(unsigned long)spec | flags;
}

static void efx_ef10_filter_push_prep(struct efx_nic *efx,
				      const struct efx_filter_spec *spec,
				      efx_dword_t *inbuf, u64 handle,
				      bool replacing)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	memset(inbuf, 0, MC_CMD_FILTER_OP_IN_LEN);

	if (replacing) {
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       MC_CMD_FILTER_OP_IN_OP_REPLACE);
		MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE, handle);
	} else {
		u32 match_fields = 0;

		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       efx_ef10_filter_is_exclusive(spec) ?
			       MC_CMD_FILTER_OP_IN_OP_INSERT :
			       MC_CMD_FILTER_OP_IN_OP_SUBSCRIBE);

		/* Convert match flags and values.  Unlike almost
		 * everything else in MCDI, these fields are in
		 * network byte order.
		 */
		if (spec->match_flags & EFX_FILTER_MATCH_LOC_MAC_IG)
			match_fields |=
				is_multicast_ether_addr(spec->loc_mac) ?
				1 << MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_MCAST_DST_LBN :
				1 << MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_UCAST_DST_LBN;
#define COPY_FIELD(gen_flag, gen_field, mcdi_field)			     \
		if (spec->match_flags & EFX_FILTER_MATCH_ ## gen_flag) {     \
			match_fields |=					     \
				1 << MC_CMD_FILTER_OP_IN_MATCH_ ##	     \
				mcdi_field ## _LBN;			     \
			BUILD_BUG_ON(					     \
				MC_CMD_FILTER_OP_IN_ ## mcdi_field ## _LEN < \
				sizeof(spec->gen_field));		     \
			memcpy(MCDI_PTR(inbuf, FILTER_OP_IN_ ##	mcdi_field), \
			       &spec->gen_field, sizeof(spec->gen_field));   \
		}
		COPY_FIELD(REM_HOST, rem_host, SRC_IP);
		COPY_FIELD(LOC_HOST, loc_host, DST_IP);
		COPY_FIELD(REM_MAC, rem_mac, SRC_MAC);
		COPY_FIELD(REM_PORT, rem_port, SRC_PORT);
		COPY_FIELD(LOC_MAC, loc_mac, DST_MAC);
		COPY_FIELD(LOC_PORT, loc_port, DST_PORT);
		COPY_FIELD(ETHER_TYPE, ether_type, ETHER_TYPE);
		COPY_FIELD(INNER_VID, inner_vid, INNER_VLAN);
		COPY_FIELD(OUTER_VID, outer_vid, OUTER_VLAN);
		COPY_FIELD(IP_PROTO, ip_proto, IP_PROTO);
#undef COPY_FIELD
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_MATCH_FIELDS,
			       match_fields);
	}

	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_PORT_ID, EVB_PORT_ID_ASSIGNED);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_DEST,
		       spec->dmaq_id == EFX_FILTER_RX_DMAQ_ID_DROP ?
		       MC_CMD_FILTER_OP_IN_RX_DEST_DROP :
		       MC_CMD_FILTER_OP_IN_RX_DEST_HOST);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_TX_DEST,
		       MC_CMD_FILTER_OP_IN_TX_DEST_DEFAULT);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_QUEUE, spec->dmaq_id);
	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_MODE,
		       (spec->flags & EFX_FILTER_FLAG_RX_RSS) ?
		       MC_CMD_FILTER_OP_IN_RX_MODE_RSS :
		       MC_CMD_FILTER_OP_IN_RX_MODE_SIMPLE);
	if (spec->flags & EFX_FILTER_FLAG_RX_RSS)
		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_RX_CONTEXT,
			       spec->rss_context !=
			       EFX_FILTER_RSS_CONTEXT_DEFAULT ?
			       spec->rss_context : nic_data->rx_rss_context);
}

static int efx_ef10_filter_push(struct efx_nic *efx,
				const struct efx_filter_spec *spec,
				u64 *handle, bool replacing)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FILTER_OP_OUT_LEN);
	int rc;

	efx_ef10_filter_push_prep(efx, spec, inbuf, *handle, replacing);
	rc = efx_mcdi_rpc(efx, MC_CMD_FILTER_OP, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), NULL);
	if (rc == 0)
		*handle = MCDI_QWORD(outbuf, FILTER_OP_OUT_HANDLE);
	return rc;
}

static int efx_ef10_filter_rx_match_pri(struct efx_ef10_filter_table *table,
					enum efx_filter_match_flags match_flags)
{
	unsigned int match_pri;

	for (match_pri = 0;
	     match_pri < table->rx_match_count;
	     match_pri++)
		if (table->rx_match_flags[match_pri] == match_flags)
			return match_pri;

	return -EPROTONOSUPPORT;
}

static s32 efx_ef10_filter_insert(struct efx_nic *efx,
				  struct efx_filter_spec *spec,
				  bool replace_equal)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	DECLARE_BITMAP(mc_rem_map, EFX_EF10_FILTER_SEARCH_LIMIT);
	struct efx_filter_spec *saved_spec;
	unsigned int match_pri, hash;
	unsigned int priv_flags;
	bool replacing = false;
	int ins_index = -1;
	DEFINE_WAIT(wait);
	bool is_mc_recip;
	s32 rc;

	/* For now, only support RX filters */
	if ((spec->flags & (EFX_FILTER_FLAG_RX | EFX_FILTER_FLAG_TX)) !=
	    EFX_FILTER_FLAG_RX)
		return -EINVAL;

	rc = efx_ef10_filter_rx_match_pri(table, spec->match_flags);
	if (rc < 0)
		return rc;
	match_pri = rc;

	hash = efx_ef10_filter_hash(spec);
	is_mc_recip = efx_filter_is_mc_recipient(spec);
	if (is_mc_recip)
		bitmap_zero(mc_rem_map, EFX_EF10_FILTER_SEARCH_LIMIT);

	/* Find any existing filters with the same match tuple or
	 * else a free slot to insert at.  If any of them are busy,
	 * we have to wait and retry.
	 */
	for (;;) {
		unsigned int depth = 1;
		unsigned int i;

		spin_lock_bh(&efx->filter_lock);

		for (;;) {
			i = (hash + depth) & (HUNT_FILTER_TBL_ROWS - 1);
			saved_spec = efx_ef10_filter_entry_spec(table, i);

			if (!saved_spec) {
				if (ins_index < 0)
					ins_index = i;
			} else if (efx_ef10_filter_equal(spec, saved_spec)) {
				if (table->entry[i].spec &
				    EFX_EF10_FILTER_FLAG_BUSY)
					break;
				if (spec->priority < saved_spec->priority &&
				    !(saved_spec->priority ==
				      EFX_FILTER_PRI_REQUIRED &&
				      saved_spec->flags &
				      EFX_FILTER_FLAG_RX_STACK)) {
					rc = -EPERM;
					goto out_unlock;
				}
				if (!is_mc_recip) {
					/* This is the only one */
					if (spec->priority ==
					    saved_spec->priority &&
					    !replace_equal) {
						rc = -EEXIST;
						goto out_unlock;
					}
					ins_index = i;
					goto found;
				} else if (spec->priority >
					   saved_spec->priority ||
					   (spec->priority ==
					    saved_spec->priority &&
					    replace_equal)) {
					if (ins_index < 0)
						ins_index = i;
					else
						__set_bit(depth, mc_rem_map);
				}
			}

			/* Once we reach the maximum search depth, use
			 * the first suitable slot or return -EBUSY if
			 * there was none
			 */
			if (depth == EFX_EF10_FILTER_SEARCH_LIMIT) {
				if (ins_index < 0) {
					rc = -EBUSY;
					goto out_unlock;
				}
				goto found;
			}

			++depth;
		}

		prepare_to_wait(&table->waitq, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_bh(&efx->filter_lock);
		schedule();
	}

found:
	/* Create a software table entry if necessary, and mark it
	 * busy.  We might yet fail to insert, but any attempt to
	 * insert a conflicting filter while we're waiting for the
	 * firmware must find the busy entry.
	 */
	saved_spec = efx_ef10_filter_entry_spec(table, ins_index);
	if (saved_spec) {
		if (spec->flags & EFX_FILTER_FLAG_RX_STACK) {
			/* Just make sure it won't be removed */
			saved_spec->flags |= EFX_FILTER_FLAG_RX_STACK;
			table->entry[ins_index].spec &=
				~EFX_EF10_FILTER_FLAG_STACK_OLD;
			rc = ins_index;
			goto out_unlock;
		}
		replacing = true;
		priv_flags = efx_ef10_filter_entry_flags(table, ins_index);
	} else {
		saved_spec = kmalloc(sizeof(*spec), GFP_ATOMIC);
		if (!saved_spec) {
			rc = -ENOMEM;
			goto out_unlock;
		}
		*saved_spec = *spec;
		priv_flags = 0;
	}
	efx_ef10_filter_set_entry(table, ins_index, saved_spec,
				  priv_flags | EFX_EF10_FILTER_FLAG_BUSY);

	/* Mark lower-priority multicast recipients busy prior to removal */
	if (is_mc_recip) {
		unsigned int depth, i;

		for (depth = 0; depth < EFX_EF10_FILTER_SEARCH_LIMIT; depth++) {
			i = (hash + depth) & (HUNT_FILTER_TBL_ROWS - 1);
			if (test_bit(depth, mc_rem_map))
				table->entry[i].spec |=
					EFX_EF10_FILTER_FLAG_BUSY;
		}
	}

	spin_unlock_bh(&efx->filter_lock);

	rc = efx_ef10_filter_push(efx, spec, &table->entry[ins_index].handle,
				  replacing);

	/* Finalise the software table entry */
	spin_lock_bh(&efx->filter_lock);
	if (rc == 0) {
		if (replacing) {
			/* Update the fields that may differ */
			saved_spec->priority = spec->priority;
			saved_spec->flags &= EFX_FILTER_FLAG_RX_STACK;
			saved_spec->flags |= spec->flags;
			saved_spec->rss_context = spec->rss_context;
			saved_spec->dmaq_id = spec->dmaq_id;
		}
	} else if (!replacing) {
		kfree(saved_spec);
		saved_spec = NULL;
	}
	efx_ef10_filter_set_entry(table, ins_index, saved_spec, priv_flags);

	/* Remove and finalise entries for lower-priority multicast
	 * recipients
	 */
	if (is_mc_recip) {
		MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_IN_LEN);
		unsigned int depth, i;

		memset(inbuf, 0, sizeof(inbuf));

		for (depth = 0; depth < EFX_EF10_FILTER_SEARCH_LIMIT; depth++) {
			if (!test_bit(depth, mc_rem_map))
				continue;

			i = (hash + depth) & (HUNT_FILTER_TBL_ROWS - 1);
			saved_spec = efx_ef10_filter_entry_spec(table, i);
			priv_flags = efx_ef10_filter_entry_flags(table, i);

			if (rc == 0) {
				spin_unlock_bh(&efx->filter_lock);
				MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
					       MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE);
				MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
					       table->entry[i].handle);
				rc = efx_mcdi_rpc(efx, MC_CMD_FILTER_OP,
						  inbuf, sizeof(inbuf),
						  NULL, 0, NULL);
				spin_lock_bh(&efx->filter_lock);
			}

			if (rc == 0) {
				kfree(saved_spec);
				saved_spec = NULL;
				priv_flags = 0;
			} else {
				priv_flags &= ~EFX_EF10_FILTER_FLAG_BUSY;
			}
			efx_ef10_filter_set_entry(table, i, saved_spec,
						  priv_flags);
		}
	}

	/* If successful, return the inserted filter ID */
	if (rc == 0)
		rc = match_pri * HUNT_FILTER_TBL_ROWS + ins_index;

	wake_up_all(&table->waitq);
out_unlock:
	spin_unlock_bh(&efx->filter_lock);
	finish_wait(&table->waitq, &wait);
	return rc;
}

void efx_ef10_filter_update_rx_scatter(struct efx_nic *efx)
{
	/* no need to do anything here on EF10 */
}

/* Remove a filter.
 * If !stack_requested, remove by ID
 * If stack_requested, remove by index
 * Filter ID may come from userland and must be range-checked.
 */
static int efx_ef10_filter_remove_internal(struct efx_nic *efx,
					   enum efx_filter_priority priority,
					   u32 filter_id, bool stack_requested)
{
	unsigned int filter_idx = filter_id % HUNT_FILTER_TBL_ROWS;
	struct efx_ef10_filter_table *table = efx->filter_state;
	MCDI_DECLARE_BUF(inbuf,
			 MC_CMD_FILTER_OP_IN_HANDLE_OFST +
			 MC_CMD_FILTER_OP_IN_HANDLE_LEN);
	struct efx_filter_spec *spec;
	DEFINE_WAIT(wait);
	int rc;

	/* Find the software table entry and mark it busy.  Don't
	 * remove it yet; any attempt to update while we're waiting
	 * for the firmware must find the busy entry.
	 */
	for (;;) {
		spin_lock_bh(&efx->filter_lock);
		if (!(table->entry[filter_idx].spec &
		      EFX_EF10_FILTER_FLAG_BUSY))
			break;
		prepare_to_wait(&table->waitq, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_bh(&efx->filter_lock);
		schedule();
	}
	spec = efx_ef10_filter_entry_spec(table, filter_idx);
	if (!spec || spec->priority > priority ||
	    (!stack_requested &&
	     efx_ef10_filter_rx_match_pri(table, spec->match_flags) !=
	     filter_id / HUNT_FILTER_TBL_ROWS)) {
		rc = -ENOENT;
		goto out_unlock;
	}
	table->entry[filter_idx].spec |= EFX_EF10_FILTER_FLAG_BUSY;
	spin_unlock_bh(&efx->filter_lock);

	if (spec->flags & EFX_FILTER_FLAG_RX_STACK && !stack_requested) {
		/* Reset steering of a stack-owned filter */

		struct efx_filter_spec new_spec = *spec;

		new_spec.priority = EFX_FILTER_PRI_REQUIRED;
		new_spec.flags = (EFX_FILTER_FLAG_RX |
				  EFX_FILTER_FLAG_RX_RSS |
				  EFX_FILTER_FLAG_RX_STACK);
		new_spec.dmaq_id = 0;
		new_spec.rss_context = EFX_FILTER_RSS_CONTEXT_DEFAULT;
		rc = efx_ef10_filter_push(efx, &new_spec,
					  &table->entry[filter_idx].handle,
					  true);

		spin_lock_bh(&efx->filter_lock);
		if (rc == 0)
			*spec = new_spec;
	} else {
		/* Really remove the filter */

		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       efx_ef10_filter_is_exclusive(spec) ?
			       MC_CMD_FILTER_OP_IN_OP_REMOVE :
			       MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE);
		MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
			       table->entry[filter_idx].handle);
		rc = efx_mcdi_rpc(efx, MC_CMD_FILTER_OP,
				  inbuf, sizeof(inbuf), NULL, 0, NULL);

		spin_lock_bh(&efx->filter_lock);
		if (rc == 0) {
			kfree(spec);
			efx_ef10_filter_set_entry(table, filter_idx, NULL, 0);
		}
	}
	table->entry[filter_idx].spec &= ~EFX_EF10_FILTER_FLAG_BUSY;
	wake_up_all(&table->waitq);
out_unlock:
	spin_unlock_bh(&efx->filter_lock);
	finish_wait(&table->waitq, &wait);
	return rc;
}

static int efx_ef10_filter_remove_safe(struct efx_nic *efx,
				       enum efx_filter_priority priority,
				       u32 filter_id)
{
	return efx_ef10_filter_remove_internal(efx, priority, filter_id, false);
}

static int efx_ef10_filter_get_safe(struct efx_nic *efx,
				    enum efx_filter_priority priority,
				    u32 filter_id, struct efx_filter_spec *spec)
{
	unsigned int filter_idx = filter_id % HUNT_FILTER_TBL_ROWS;
	struct efx_ef10_filter_table *table = efx->filter_state;
	const struct efx_filter_spec *saved_spec;
	int rc;

	spin_lock_bh(&efx->filter_lock);
	saved_spec = efx_ef10_filter_entry_spec(table, filter_idx);
	if (saved_spec && saved_spec->priority == priority &&
	    efx_ef10_filter_rx_match_pri(table, saved_spec->match_flags) ==
	    filter_id / HUNT_FILTER_TBL_ROWS) {
		*spec = *saved_spec;
		rc = 0;
	} else {
		rc = -ENOENT;
	}
	spin_unlock_bh(&efx->filter_lock);
	return rc;
}

static void efx_ef10_filter_clear_rx(struct efx_nic *efx,
				     enum efx_filter_priority priority)
{
	/* TODO */
}

static u32 efx_ef10_filter_count_rx_used(struct efx_nic *efx,
					 enum efx_filter_priority priority)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	unsigned int filter_idx;
	s32 count = 0;

	spin_lock_bh(&efx->filter_lock);
	for (filter_idx = 0; filter_idx < HUNT_FILTER_TBL_ROWS; filter_idx++) {
		if (table->entry[filter_idx].spec &&
		    efx_ef10_filter_entry_spec(table, filter_idx)->priority ==
		    priority)
			++count;
	}
	spin_unlock_bh(&efx->filter_lock);
	return count;
}

static u32 efx_ef10_filter_get_rx_id_limit(struct efx_nic *efx)
{
	struct efx_ef10_filter_table *table = efx->filter_state;

	return table->rx_match_count * HUNT_FILTER_TBL_ROWS;
}

static s32 efx_ef10_filter_get_rx_ids(struct efx_nic *efx,
				      enum efx_filter_priority priority,
				      u32 *buf, u32 size)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	struct efx_filter_spec *spec;
	unsigned int filter_idx;
	s32 count = 0;

	spin_lock_bh(&efx->filter_lock);
	for (filter_idx = 0; filter_idx < HUNT_FILTER_TBL_ROWS; filter_idx++) {
		spec = efx_ef10_filter_entry_spec(table, filter_idx);
		if (spec && spec->priority == priority) {
			if (count == size) {
				count = -EMSGSIZE;
				break;
			}
			buf[count++] = (efx_ef10_filter_rx_match_pri(
						table, spec->match_flags) *
					HUNT_FILTER_TBL_ROWS +
					filter_idx);
		}
	}
	spin_unlock_bh(&efx->filter_lock);
	return count;
}

#ifdef CONFIG_RFS_ACCEL

static efx_mcdi_async_completer efx_ef10_filter_rfs_insert_complete;

static s32 efx_ef10_filter_rfs_insert(struct efx_nic *efx,
				      struct efx_filter_spec *spec)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_IN_LEN);
	struct efx_filter_spec *saved_spec;
	unsigned int hash, i, depth = 1;
	bool replacing = false;
	int ins_index = -1;
	u64 cookie;
	s32 rc;

	/* Must be an RX filter without RSS and not for a multicast
	 * destination address (RFS only works for connected sockets).
	 * These restrictions allow us to pass only a tiny amount of
	 * data through to the completion function.
	 */
	EFX_WARN_ON_PARANOID(spec->flags !=
			     (EFX_FILTER_FLAG_RX | EFX_FILTER_FLAG_RX_SCATTER));
	EFX_WARN_ON_PARANOID(spec->priority != EFX_FILTER_PRI_HINT);
	EFX_WARN_ON_PARANOID(efx_filter_is_mc_recipient(spec));

	hash = efx_ef10_filter_hash(spec);

	spin_lock_bh(&efx->filter_lock);

	/* Find any existing filter with the same match tuple or else
	 * a free slot to insert at.  If an existing filter is busy,
	 * we have to give up.
	 */
	for (;;) {
		i = (hash + depth) & (HUNT_FILTER_TBL_ROWS - 1);
		saved_spec = efx_ef10_filter_entry_spec(table, i);

		if (!saved_spec) {
			if (ins_index < 0)
				ins_index = i;
		} else if (efx_ef10_filter_equal(spec, saved_spec)) {
			if (table->entry[i].spec & EFX_EF10_FILTER_FLAG_BUSY) {
				rc = -EBUSY;
				goto fail_unlock;
			}
			EFX_WARN_ON_PARANOID(saved_spec->flags &
					     EFX_FILTER_FLAG_RX_STACK);
			if (spec->priority < saved_spec->priority) {
				rc = -EPERM;
				goto fail_unlock;
			}
			ins_index = i;
			break;
		}

		/* Once we reach the maximum search depth, use the
		 * first suitable slot or return -EBUSY if there was
		 * none
		 */
		if (depth == EFX_EF10_FILTER_SEARCH_LIMIT) {
			if (ins_index < 0) {
				rc = -EBUSY;
				goto fail_unlock;
			}
			break;
		}

		++depth;
	}

	/* Create a software table entry if necessary, and mark it
	 * busy.  We might yet fail to insert, but any attempt to
	 * insert a conflicting filter while we're waiting for the
	 * firmware must find the busy entry.
	 */
	saved_spec = efx_ef10_filter_entry_spec(table, ins_index);
	if (saved_spec) {
		replacing = true;
	} else {
		saved_spec = kmalloc(sizeof(*spec), GFP_ATOMIC);
		if (!saved_spec) {
			rc = -ENOMEM;
			goto fail_unlock;
		}
		*saved_spec = *spec;
	}
	efx_ef10_filter_set_entry(table, ins_index, saved_spec,
				  EFX_EF10_FILTER_FLAG_BUSY);

	spin_unlock_bh(&efx->filter_lock);

	/* Pack up the variables needed on completion */
	cookie = replacing << 31 | ins_index << 16 | spec->dmaq_id;

	efx_ef10_filter_push_prep(efx, spec, inbuf,
				  table->entry[ins_index].handle, replacing);
	efx_mcdi_rpc_async(efx, MC_CMD_FILTER_OP, inbuf, sizeof(inbuf),
			   MC_CMD_FILTER_OP_OUT_LEN,
			   efx_ef10_filter_rfs_insert_complete, cookie);

	return ins_index;

fail_unlock:
	spin_unlock_bh(&efx->filter_lock);
	return rc;
}

static void
efx_ef10_filter_rfs_insert_complete(struct efx_nic *efx, unsigned long cookie,
				    int rc, efx_dword_t *outbuf,
				    size_t outlen_actual)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	unsigned int ins_index, dmaq_id;
	struct efx_filter_spec *spec;
	bool replacing;

	/* Unpack the cookie */
	replacing = cookie >> 31;
	ins_index = (cookie >> 16) & (HUNT_FILTER_TBL_ROWS - 1);
	dmaq_id = cookie & 0xffff;

	spin_lock_bh(&efx->filter_lock);
	spec = efx_ef10_filter_entry_spec(table, ins_index);
	if (rc == 0) {
		table->entry[ins_index].handle =
			MCDI_QWORD(outbuf, FILTER_OP_OUT_HANDLE);
		if (replacing)
			spec->dmaq_id = dmaq_id;
	} else if (!replacing) {
		kfree(spec);
		spec = NULL;
	}
	efx_ef10_filter_set_entry(table, ins_index, spec, 0);
	spin_unlock_bh(&efx->filter_lock);

	wake_up_all(&table->waitq);
}

static void
efx_ef10_filter_rfs_expire_complete(struct efx_nic *efx,
				    unsigned long filter_idx,
				    int rc, efx_dword_t *outbuf,
				    size_t outlen_actual);

static bool efx_ef10_filter_rfs_expire_one(struct efx_nic *efx, u32 flow_id,
					   unsigned int filter_idx)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	struct efx_filter_spec *spec =
		efx_ef10_filter_entry_spec(table, filter_idx);
	MCDI_DECLARE_BUF(inbuf,
			 MC_CMD_FILTER_OP_IN_HANDLE_OFST +
			 MC_CMD_FILTER_OP_IN_HANDLE_LEN);

	if (!spec ||
	    (table->entry[filter_idx].spec & EFX_EF10_FILTER_FLAG_BUSY) ||
	    spec->priority != EFX_FILTER_PRI_HINT ||
	    !rps_may_expire_flow(efx->net_dev, spec->dmaq_id,
				 flow_id, filter_idx))
		return false;

	MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
		       MC_CMD_FILTER_OP_IN_OP_REMOVE);
	MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
		       table->entry[filter_idx].handle);
	if (efx_mcdi_rpc_async(efx, MC_CMD_FILTER_OP, inbuf, sizeof(inbuf), 0,
			       efx_ef10_filter_rfs_expire_complete, filter_idx))
		return false;

	table->entry[filter_idx].spec |= EFX_EF10_FILTER_FLAG_BUSY;
	return true;
}

static void
efx_ef10_filter_rfs_expire_complete(struct efx_nic *efx,
				    unsigned long filter_idx,
				    int rc, efx_dword_t *outbuf,
				    size_t outlen_actual)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	struct efx_filter_spec *spec =
		efx_ef10_filter_entry_spec(table, filter_idx);

	spin_lock_bh(&efx->filter_lock);
	if (rc == 0) {
		kfree(spec);
		efx_ef10_filter_set_entry(table, filter_idx, NULL, 0);
	}
	table->entry[filter_idx].spec &= ~EFX_EF10_FILTER_FLAG_BUSY;
	wake_up_all(&table->waitq);
	spin_unlock_bh(&efx->filter_lock);
}

#endif /* CONFIG_RFS_ACCEL */

static int efx_ef10_filter_match_flags_from_mcdi(u32 mcdi_flags)
{
	int match_flags = 0;

#define MAP_FLAG(gen_flag, mcdi_field) {				\
		u32 old_mcdi_flags = mcdi_flags;			\
		mcdi_flags &= ~(1 << MC_CMD_FILTER_OP_IN_MATCH_ ##	\
				mcdi_field ## _LBN);			\
		if (mcdi_flags != old_mcdi_flags)			\
			match_flags |= EFX_FILTER_MATCH_ ## gen_flag;	\
	}
	MAP_FLAG(LOC_MAC_IG, UNKNOWN_UCAST_DST);
	MAP_FLAG(LOC_MAC_IG, UNKNOWN_MCAST_DST);
	MAP_FLAG(REM_HOST, SRC_IP);
	MAP_FLAG(LOC_HOST, DST_IP);
	MAP_FLAG(REM_MAC, SRC_MAC);
	MAP_FLAG(REM_PORT, SRC_PORT);
	MAP_FLAG(LOC_MAC, DST_MAC);
	MAP_FLAG(LOC_PORT, DST_PORT);
	MAP_FLAG(ETHER_TYPE, ETHER_TYPE);
	MAP_FLAG(INNER_VID, INNER_VLAN);
	MAP_FLAG(OUTER_VID, OUTER_VLAN);
	MAP_FLAG(IP_PROTO, IP_PROTO);
#undef MAP_FLAG

	/* Did we map them all? */
	if (mcdi_flags)
		return -EINVAL;

	return match_flags;
}

static int efx_ef10_filter_table_probe(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_GET_PARSER_DISP_INFO_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PARSER_DISP_INFO_OUT_LENMAX);
	unsigned int pd_match_pri, pd_match_count;
	struct efx_ef10_filter_table *table;
	size_t outlen;
	int rc;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	/* Find out which RX filter types are supported, and their priorities */
	MCDI_SET_DWORD(inbuf, GET_PARSER_DISP_INFO_IN_OP,
		       MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SUPPORTED_RX_MATCHES);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PARSER_DISP_INFO,
			  inbuf, sizeof(inbuf), outbuf, sizeof(outbuf),
			  &outlen);
	if (rc)
		goto fail;
	pd_match_count = MCDI_VAR_ARRAY_LEN(
		outlen, GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES);
	table->rx_match_count = 0;

	for (pd_match_pri = 0; pd_match_pri < pd_match_count; pd_match_pri++) {
		u32 mcdi_flags =
			MCDI_ARRAY_DWORD(
				outbuf,
				GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES,
				pd_match_pri);
		rc = efx_ef10_filter_match_flags_from_mcdi(mcdi_flags);
		if (rc < 0) {
			netif_dbg(efx, probe, efx->net_dev,
				  "%s: fw flags %#x pri %u not supported in driver\n",
				  __func__, mcdi_flags, pd_match_pri);
		} else {
			netif_dbg(efx, probe, efx->net_dev,
				  "%s: fw flags %#x pri %u supported as driver flags %#x pri %u\n",
				  __func__, mcdi_flags, pd_match_pri,
				  rc, table->rx_match_count);
			table->rx_match_flags[table->rx_match_count++] = rc;
		}
	}

	table->entry = vzalloc(HUNT_FILTER_TBL_ROWS * sizeof(*table->entry));
	if (!table->entry) {
		rc = -ENOMEM;
		goto fail;
	}

	efx->filter_state = table;
	init_waitqueue_head(&table->waitq);
	return 0;

fail:
	kfree(table);
	return rc;
}

static void efx_ef10_filter_table_restore(struct efx_nic *efx)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct efx_filter_spec *spec;
	unsigned int filter_idx;
	bool failed = false;
	int rc;

	if (!nic_data->must_restore_filters)
		return;

	spin_lock_bh(&efx->filter_lock);

	for (filter_idx = 0; filter_idx < HUNT_FILTER_TBL_ROWS; filter_idx++) {
		spec = efx_ef10_filter_entry_spec(table, filter_idx);
		if (!spec)
			continue;

		table->entry[filter_idx].spec |= EFX_EF10_FILTER_FLAG_BUSY;
		spin_unlock_bh(&efx->filter_lock);

		rc = efx_ef10_filter_push(efx, spec,
					  &table->entry[filter_idx].handle,
					  false);
		if (rc)
			failed = true;

		spin_lock_bh(&efx->filter_lock);
		if (rc) {
			kfree(spec);
			efx_ef10_filter_set_entry(table, filter_idx, NULL, 0);
		} else {
			table->entry[filter_idx].spec &=
				~EFX_EF10_FILTER_FLAG_BUSY;
		}
	}

	spin_unlock_bh(&efx->filter_lock);

	if (failed)
		netif_err(efx, hw, efx->net_dev,
			  "unable to restore all filters\n");
	else
		nic_data->must_restore_filters = false;
}

static void efx_ef10_filter_table_remove(struct efx_nic *efx)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FILTER_OP_IN_LEN);
	struct efx_filter_spec *spec;
	unsigned int filter_idx;
	int rc;

	for (filter_idx = 0; filter_idx < HUNT_FILTER_TBL_ROWS; filter_idx++) {
		spec = efx_ef10_filter_entry_spec(table, filter_idx);
		if (!spec)
			continue;

		MCDI_SET_DWORD(inbuf, FILTER_OP_IN_OP,
			       efx_ef10_filter_is_exclusive(spec) ?
			       MC_CMD_FILTER_OP_IN_OP_REMOVE :
			       MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE);
		MCDI_SET_QWORD(inbuf, FILTER_OP_IN_HANDLE,
			       table->entry[filter_idx].handle);
		rc = efx_mcdi_rpc(efx, MC_CMD_FILTER_OP, inbuf, sizeof(inbuf),
				  NULL, 0, NULL);

		WARN_ON(rc != 0);
		kfree(spec);
	}

	vfree(table->entry);
	kfree(table);
}

static void efx_ef10_filter_sync_rx_mode(struct efx_nic *efx)
{
	struct efx_ef10_filter_table *table = efx->filter_state;
	struct net_device *net_dev = efx->net_dev;
	struct efx_filter_spec spec;
	bool remove_failed = false;
	struct netdev_hw_addr *uc;
	struct netdev_hw_addr *mc;
	unsigned int filter_idx;
	int i, n, rc;

	if (!efx_dev_registered(efx))
		return;

	/* Mark old filters that may need to be removed */
	spin_lock_bh(&efx->filter_lock);
	n = table->stack_uc_count < 0 ? 1 : table->stack_uc_count;
	for (i = 0; i < n; i++) {
		filter_idx = table->stack_uc_list[i].id % HUNT_FILTER_TBL_ROWS;
		table->entry[filter_idx].spec |= EFX_EF10_FILTER_FLAG_STACK_OLD;
	}
	n = table->stack_mc_count < 0 ? 1 : table->stack_mc_count;
	for (i = 0; i < n; i++) {
		filter_idx = table->stack_mc_list[i].id % HUNT_FILTER_TBL_ROWS;
		table->entry[filter_idx].spec |= EFX_EF10_FILTER_FLAG_STACK_OLD;
	}
	spin_unlock_bh(&efx->filter_lock);

	/* Copy/convert the address lists; add the primary station
	 * address and broadcast address
	 */
	netif_addr_lock_bh(net_dev);
	if (net_dev->flags & IFF_PROMISC ||
	    netdev_uc_count(net_dev) >= EFX_EF10_FILTER_STACK_UC_MAX) {
		table->stack_uc_count = -1;
	} else {
		table->stack_uc_count = 1 + netdev_uc_count(net_dev);
		memcpy(table->stack_uc_list[0].addr, net_dev->dev_addr,
		       ETH_ALEN);
		i = 1;
		netdev_for_each_uc_addr(uc, net_dev) {
			memcpy(table->stack_uc_list[i].addr,
			       uc->addr, ETH_ALEN);
			i++;
		}
	}
	if (net_dev->flags & (IFF_PROMISC | IFF_ALLMULTI) ||
	    netdev_mc_count(net_dev) >= EFX_EF10_FILTER_STACK_MC_MAX) {
		table->stack_mc_count = -1;
	} else {
		table->stack_mc_count = 1 + netdev_mc_count(net_dev);
		eth_broadcast_addr(table->stack_mc_list[0].addr);
		i = 1;
		netdev_for_each_mc_addr(mc, net_dev) {
			memcpy(table->stack_mc_list[i].addr,
			       mc->addr, ETH_ALEN);
			i++;
		}
	}
	netif_addr_unlock_bh(net_dev);

	/* Insert/renew unicast filters */
	if (table->stack_uc_count >= 0) {
		for (i = 0; i < table->stack_uc_count; i++) {
			efx_filter_init_rx(&spec, EFX_FILTER_PRI_REQUIRED,
					   EFX_FILTER_FLAG_RX_RSS |
					   EFX_FILTER_FLAG_RX_STACK,
					   0);
			efx_filter_set_eth_local(&spec, EFX_FILTER_VID_UNSPEC,
						 table->stack_uc_list[i].addr);
			rc = efx_ef10_filter_insert(efx, &spec, true);
			if (rc < 0) {
				/* Fall back to unicast-promisc */
				while (i--)
					efx_ef10_filter_remove_safe(
						efx, EFX_FILTER_PRI_REQUIRED,
						table->stack_uc_list[i].id);
				table->stack_uc_count = -1;
				break;
			}
			table->stack_uc_list[i].id = rc;
		}
	}
	if (table->stack_uc_count < 0) {
		efx_filter_init_rx(&spec, EFX_FILTER_PRI_REQUIRED,
				   EFX_FILTER_FLAG_RX_RSS |
				   EFX_FILTER_FLAG_RX_STACK,
				   0);
		efx_filter_set_uc_def(&spec);
		rc = efx_ef10_filter_insert(efx, &spec, true);
		if (rc < 0) {
			WARN_ON(1);
			table->stack_uc_count = 0;
		} else {
			table->stack_uc_list[0].id = rc;
		}
	}

	/* Insert/renew multicast filters */
	if (table->stack_mc_count >= 0) {
		for (i = 0; i < table->stack_mc_count; i++) {
			efx_filter_init_rx(&spec, EFX_FILTER_PRI_REQUIRED,
					   EFX_FILTER_FLAG_RX_RSS |
					   EFX_FILTER_FLAG_RX_STACK,
					   0);
			efx_filter_set_eth_local(&spec, EFX_FILTER_VID_UNSPEC,
						 table->stack_mc_list[i].addr);
			rc = efx_ef10_filter_insert(efx, &spec, true);
			if (rc < 0) {
				/* Fall back to multicast-promisc */
				while (i--)
					efx_ef10_filter_remove_safe(
						efx, EFX_FILTER_PRI_REQUIRED,
						table->stack_mc_list[i].id);
				table->stack_mc_count = -1;
				break;
			}
			table->stack_mc_list[i].id = rc;
		}
	}
	if (table->stack_mc_count < 0) {
		efx_filter_init_rx(&spec, EFX_FILTER_PRI_REQUIRED,
				   EFX_FILTER_FLAG_RX_RSS |
				   EFX_FILTER_FLAG_RX_STACK,
				   0);
		efx_filter_set_mc_def(&spec);
		rc = efx_ef10_filter_insert(efx, &spec, true);
		if (rc < 0) {
			WARN_ON(1);
			table->stack_mc_count = 0;
		} else {
			table->stack_mc_list[0].id = rc;
		}
	}

	/* Remove filters that weren't renewed.  Since nothing else
	 * changes the STACK_OLD flag or removes these filters, we
	 * don't need to hold the filter_lock while scanning for
	 * these filters.
	 */
	for (i = 0; i < HUNT_FILTER_TBL_ROWS; i++) {
		if (ACCESS_ONCE(table->entry[i].spec) &
		    EFX_EF10_FILTER_FLAG_STACK_OLD) {
			if (efx_ef10_filter_remove_internal(efx,
					EFX_FILTER_PRI_REQUIRED,
					i, true) < 0)
				remove_failed = true;
		}
	}
	WARN_ON(remove_failed);
}

static int efx_ef10_mac_reconfigure(struct efx_nic *efx)
{
	efx_ef10_filter_sync_rx_mode(efx);

	return efx_mcdi_set_mac(efx);
}

#ifdef CONFIG_SFC_MTD

struct efx_ef10_nvram_type_info {
	u16 type, type_mask;
	u8 port;
	const char *name;
};

static const struct efx_ef10_nvram_type_info efx_ef10_nvram_types[] = {
	{ NVRAM_PARTITION_TYPE_MC_FIRMWARE,	   0,    0, "sfc_mcfw" },
	{ NVRAM_PARTITION_TYPE_MC_FIRMWARE_BACKUP, 0,    0, "sfc_mcfw_backup" },
	{ NVRAM_PARTITION_TYPE_EXPANSION_ROM,	   0,    0, "sfc_exp_rom" },
	{ NVRAM_PARTITION_TYPE_STATIC_CONFIG,	   0,    0, "sfc_static_cfg" },
	{ NVRAM_PARTITION_TYPE_DYNAMIC_CONFIG,	   0,    0, "sfc_dynamic_cfg" },
	{ NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT0, 0,   0, "sfc_exp_rom_cfg" },
	{ NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT1, 0,   1, "sfc_exp_rom_cfg" },
	{ NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT2, 0,   2, "sfc_exp_rom_cfg" },
	{ NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT3, 0,   3, "sfc_exp_rom_cfg" },
	{ NVRAM_PARTITION_TYPE_PHY_MIN,		   0xff, 0, "sfc_phy_fw" },
};

static int efx_ef10_mtd_probe_partition(struct efx_nic *efx,
					struct efx_mcdi_mtd_partition *part,
					unsigned int type)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_NVRAM_METADATA_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_NVRAM_METADATA_OUT_LENMAX);
	const struct efx_ef10_nvram_type_info *info;
	size_t size, erase_size, outlen;
	bool protected;
	int rc;

	for (info = efx_ef10_nvram_types; ; info++) {
		if (info ==
		    efx_ef10_nvram_types + ARRAY_SIZE(efx_ef10_nvram_types))
			return -ENODEV;
		if ((type & ~info->type_mask) == info->type)
			break;
	}
	if (info->port != efx_port_num(efx))
		return -ENODEV;

	rc = efx_mcdi_nvram_info(efx, type, &size, &erase_size, &protected);
	if (rc)
		return rc;
	if (protected)
		return -ENODEV; /* hide it */

	part->nvram_type = type;

	MCDI_SET_DWORD(inbuf, NVRAM_METADATA_IN_TYPE, type);
	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_METADATA, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_NVRAM_METADATA_OUT_LENMIN)
		return -EIO;
	if (MCDI_DWORD(outbuf, NVRAM_METADATA_OUT_FLAGS) &
	    (1 << MC_CMD_NVRAM_METADATA_OUT_SUBTYPE_VALID_LBN))
		part->fw_subtype = MCDI_DWORD(outbuf,
					      NVRAM_METADATA_OUT_SUBTYPE);

	part->common.dev_type_name = "EF10 NVRAM manager";
	part->common.type_name = info->name;

	part->common.mtd.type = MTD_NORFLASH;
	part->common.mtd.flags = MTD_CAP_NORFLASH;
	part->common.mtd.size = size;
	part->common.mtd.erasesize = erase_size;

	return 0;
}

static int efx_ef10_mtd_probe(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_NVRAM_PARTITIONS_OUT_LENMAX);
	struct efx_mcdi_mtd_partition *parts;
	size_t outlen, n_parts_total, i, n_parts;
	unsigned int type;
	int rc;

	ASSERT_RTNL();

	BUILD_BUG_ON(MC_CMD_NVRAM_PARTITIONS_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_PARTITIONS, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_NVRAM_PARTITIONS_OUT_LENMIN)
		return -EIO;

	n_parts_total = MCDI_DWORD(outbuf, NVRAM_PARTITIONS_OUT_NUM_PARTITIONS);
	if (n_parts_total >
	    MCDI_VAR_ARRAY_LEN(outlen, NVRAM_PARTITIONS_OUT_TYPE_ID))
		return -EIO;

	parts = kcalloc(n_parts_total, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	n_parts = 0;
	for (i = 0; i < n_parts_total; i++) {
		type = MCDI_ARRAY_DWORD(outbuf, NVRAM_PARTITIONS_OUT_TYPE_ID,
					i);
		rc = efx_ef10_mtd_probe_partition(efx, &parts[n_parts], type);
		if (rc == 0)
			n_parts++;
		else if (rc != -ENODEV)
			goto fail;
	}

	rc = efx_mtd_add(efx, &parts[0].common, n_parts, sizeof(*parts));
fail:
	if (rc)
		kfree(parts);
	return rc;
}

#endif /* CONFIG_SFC_MTD */

static void efx_ef10_ptp_write_host_time(struct efx_nic *efx, u32 host_time)
{
	_efx_writed(efx, cpu_to_le32(host_time), ER_DZ_MC_DB_LWRD);
}

const struct efx_nic_type efx_hunt_a0_nic_type = {
	.mem_map_size = efx_ef10_mem_map_size,
	.probe = efx_ef10_probe,
	.remove = efx_ef10_remove,
	.dimension_resources = efx_ef10_dimension_resources,
	.init = efx_ef10_init_nic,
	.fini = efx_port_dummy_op_void,
	.map_reset_reason = efx_mcdi_map_reset_reason,
	.map_reset_flags = efx_ef10_map_reset_flags,
	.reset = efx_mcdi_reset,
	.probe_port = efx_mcdi_port_probe,
	.remove_port = efx_mcdi_port_remove,
	.fini_dmaq = efx_ef10_fini_dmaq,
	.describe_stats = efx_ef10_describe_stats,
	.update_stats = efx_ef10_update_stats,
	.start_stats = efx_mcdi_mac_start_stats,
	.stop_stats = efx_mcdi_mac_stop_stats,
	.set_id_led = efx_mcdi_set_id_led,
	.push_irq_moderation = efx_ef10_push_irq_moderation,
	.reconfigure_mac = efx_ef10_mac_reconfigure,
	.check_mac_fault = efx_mcdi_mac_check_fault,
	.reconfigure_port = efx_mcdi_port_reconfigure,
	.get_wol = efx_ef10_get_wol,
	.set_wol = efx_ef10_set_wol,
	.resume_wol = efx_port_dummy_op_void,
	/* TODO: test_chip */
	.test_nvram = efx_mcdi_nvram_test_all,
	.mcdi_request = efx_ef10_mcdi_request,
	.mcdi_poll_response = efx_ef10_mcdi_poll_response,
	.mcdi_read_response = efx_ef10_mcdi_read_response,
	.mcdi_poll_reboot = efx_ef10_mcdi_poll_reboot,
	.irq_enable_master = efx_port_dummy_op_void,
	.irq_test_generate = efx_ef10_irq_test_generate,
	.irq_disable_non_ev = efx_port_dummy_op_void,
	.irq_handle_msi = efx_ef10_msi_interrupt,
	.irq_handle_legacy = efx_ef10_legacy_interrupt,
	.tx_probe = efx_ef10_tx_probe,
	.tx_init = efx_ef10_tx_init,
	.tx_remove = efx_ef10_tx_remove,
	.tx_write = efx_ef10_tx_write,
	.rx_push_indir_table = efx_ef10_rx_push_indir_table,
	.rx_probe = efx_ef10_rx_probe,
	.rx_init = efx_ef10_rx_init,
	.rx_remove = efx_ef10_rx_remove,
	.rx_write = efx_ef10_rx_write,
	.rx_defer_refill = efx_ef10_rx_defer_refill,
	.ev_probe = efx_ef10_ev_probe,
	.ev_init = efx_ef10_ev_init,
	.ev_fini = efx_ef10_ev_fini,
	.ev_remove = efx_ef10_ev_remove,
	.ev_process = efx_ef10_ev_process,
	.ev_read_ack = efx_ef10_ev_read_ack,
	.ev_test_generate = efx_ef10_ev_test_generate,
	.filter_table_probe = efx_ef10_filter_table_probe,
	.filter_table_restore = efx_ef10_filter_table_restore,
	.filter_table_remove = efx_ef10_filter_table_remove,
	.filter_update_rx_scatter = efx_ef10_filter_update_rx_scatter,
	.filter_insert = efx_ef10_filter_insert,
	.filter_remove_safe = efx_ef10_filter_remove_safe,
	.filter_get_safe = efx_ef10_filter_get_safe,
	.filter_clear_rx = efx_ef10_filter_clear_rx,
	.filter_count_rx_used = efx_ef10_filter_count_rx_used,
	.filter_get_rx_id_limit = efx_ef10_filter_get_rx_id_limit,
	.filter_get_rx_ids = efx_ef10_filter_get_rx_ids,
#ifdef CONFIG_RFS_ACCEL
	.filter_rfs_insert = efx_ef10_filter_rfs_insert,
	.filter_rfs_expire_one = efx_ef10_filter_rfs_expire_one,
#endif
#ifdef CONFIG_SFC_MTD
	.mtd_probe = efx_ef10_mtd_probe,
	.mtd_rename = efx_mcdi_mtd_rename,
	.mtd_read = efx_mcdi_mtd_read,
	.mtd_erase = efx_mcdi_mtd_erase,
	.mtd_write = efx_mcdi_mtd_write,
	.mtd_sync = efx_mcdi_mtd_sync,
#endif
	.ptp_write_host_time = efx_ef10_ptp_write_host_time,

	.revision = EFX_REV_HUNT_A0,
	.max_dma_mask = DMA_BIT_MASK(ESF_DZ_TX_KER_BUF_ADDR_WIDTH),
	.rx_prefix_size = ES_DZ_RX_PREFIX_SIZE,
	.rx_hash_offset = ES_DZ_RX_PREFIX_HASH_OFST,
	.can_rx_scatter = true,
	.always_rx_scatter = true,
	.max_interrupt_mode = EFX_INT_MODE_MSIX,
	.timer_period_max = 1 << ERF_DD_EVQ_IND_TIMER_VAL_WIDTH,
	.offload_features = (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			     NETIF_F_RXHASH | NETIF_F_NTUPLE),
	.mcdi_max_ver = 2,
	.max_rx_ip_filters = HUNT_FILTER_TBL_ROWS,
};
