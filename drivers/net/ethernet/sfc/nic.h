/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_NIC_H
#define EFX_NIC_H

#include <linux/net_tstamp.h>
#include <linux/i2c-algo-bit.h>
#include "net_driver.h"
#include "efx.h"
#include "mcdi.h"

enum {
	EFX_REV_FALCON_A0 = 0,
	EFX_REV_FALCON_A1 = 1,
	EFX_REV_FALCON_B0 = 2,
	EFX_REV_SIENA_A0 = 3,
	EFX_REV_HUNT_A0 = 4,
};

static inline int efx_nic_rev(struct efx_nic *efx)
{
	return efx->type->revision;
}

extern u32 efx_farch_fpga_ver(struct efx_nic *efx);

/* NIC has two interlinked PCI functions for the same port. */
static inline bool efx_nic_is_dual_func(struct efx_nic *efx)
{
	return efx_nic_rev(efx) < EFX_REV_FALCON_B0;
}

/* Read the current event from the event queue */
static inline efx_qword_t *efx_event(struct efx_channel *channel,
				     unsigned int index)
{
	return ((efx_qword_t *) (channel->eventq.buf.addr)) +
		(index & channel->eventq_mask);
}

/* See if an event is present
 *
 * We check both the high and low dword of the event for all ones.  We
 * wrote all ones when we cleared the event, and no valid event can
 * have all ones in either its high or low dwords.  This approach is
 * robust against reordering.
 *
 * Note that using a single 64-bit comparison is incorrect; even
 * though the CPU read will be atomic, the DMA write may not be.
 */
static inline int efx_event_present(efx_qword_t *event)
{
	return !(EFX_DWORD_IS_ALL_ONES(event->dword[0]) |
		  EFX_DWORD_IS_ALL_ONES(event->dword[1]));
}

/* Returns a pointer to the specified transmit descriptor in the TX
 * descriptor queue belonging to the specified channel.
 */
static inline efx_qword_t *
efx_tx_desc(struct efx_tx_queue *tx_queue, unsigned int index)
{
	return ((efx_qword_t *) (tx_queue->txd.buf.addr)) + index;
}

/* Decide whether to push a TX descriptor to the NIC vs merely writing
 * the doorbell.  This can reduce latency when we are adding a single
 * descriptor to an empty queue, but is otherwise pointless.  Further,
 * Falcon and Siena have hardware bugs (SF bug 33851) that may be
 * triggered if we don't check this.
 */
static inline bool efx_nic_may_push_tx_desc(struct efx_tx_queue *tx_queue,
					    unsigned int write_count)
{
	unsigned empty_read_count = ACCESS_ONCE(tx_queue->empty_read_count);

	if (empty_read_count == 0)
		return false;

	tx_queue->empty_read_count = 0;
	return ((empty_read_count ^ write_count) & ~EFX_EMPTY_COUNT_VALID) == 0
		&& tx_queue->write_count - write_count == 1;
}

/* Returns a pointer to the specified descriptor in the RX descriptor queue */
static inline efx_qword_t *
efx_rx_desc(struct efx_rx_queue *rx_queue, unsigned int index)
{
	return ((efx_qword_t *) (rx_queue->rxd.buf.addr)) + index;
}

enum {
	PHY_TYPE_NONE = 0,
	PHY_TYPE_TXC43128 = 1,
	PHY_TYPE_88E1111 = 2,
	PHY_TYPE_SFX7101 = 3,
	PHY_TYPE_QT2022C2 = 4,
	PHY_TYPE_PM8358 = 6,
	PHY_TYPE_SFT9001A = 8,
	PHY_TYPE_QT2025C = 9,
	PHY_TYPE_SFT9001B = 10,
};

#define FALCON_XMAC_LOOPBACKS			\
	((1 << LOOPBACK_XGMII) |		\
	 (1 << LOOPBACK_XGXS) |			\
	 (1 << LOOPBACK_XAUI))

/* Alignment of PCIe DMA boundaries (4KB) */
#define EFX_PAGE_SIZE	4096
/* Size and alignment of buffer table entries (same) */
#define EFX_BUF_SIZE	EFX_PAGE_SIZE

/**
 * struct falcon_board_type - board operations and type information
 * @id: Board type id, as found in NVRAM
 * @init: Allocate resources and initialise peripheral hardware
 * @init_phy: Do board-specific PHY initialisation
 * @fini: Shut down hardware and free resources
 * @set_id_led: Set state of identifying LED or revert to automatic function
 * @monitor: Board-specific health check function
 */
struct falcon_board_type {
	u8 id;
	int (*init) (struct efx_nic *nic);
	void (*init_phy) (struct efx_nic *efx);
	void (*fini) (struct efx_nic *nic);
	void (*set_id_led) (struct efx_nic *efx, enum efx_led_mode mode);
	int (*monitor) (struct efx_nic *nic);
};

/**
 * struct falcon_board - board information
 * @type: Type of board
 * @major: Major rev. ('A', 'B' ...)
 * @minor: Minor rev. (0, 1, ...)
 * @i2c_adap: I2C adapter for on-board peripherals
 * @i2c_data: Data for bit-banging algorithm
 * @hwmon_client: I2C client for hardware monitor
 * @ioexp_client: I2C client for power/port control
 */
struct falcon_board {
	const struct falcon_board_type *type;
	int major;
	int minor;
	struct i2c_adapter i2c_adap;
	struct i2c_algo_bit_data i2c_data;
	struct i2c_client *hwmon_client, *ioexp_client;
};

/**
 * struct falcon_spi_device - a Falcon SPI (Serial Peripheral Interface) device
 * @device_id:		Controller's id for the device
 * @size:		Size (in bytes)
 * @addr_len:		Number of address bytes in read/write commands
 * @munge_address:	Flag whether addresses should be munged.
 *	Some devices with 9-bit addresses (e.g. AT25040A EEPROM)
 *	use bit 3 of the command byte as address bit A8, rather
 *	than having a two-byte address.  If this flag is set, then
 *	commands should be munged in this way.
 * @erase_command:	Erase command (or 0 if sector erase not needed).
 * @erase_size:		Erase sector size (in bytes)
 *	Erase commands affect sectors with this size and alignment.
 *	This must be a power of two.
 * @block_size:		Write block size (in bytes).
 *	Write commands are limited to blocks with this size and alignment.
 */
struct falcon_spi_device {
	int device_id;
	unsigned int size;
	unsigned int addr_len;
	unsigned int munge_address:1;
	u8 erase_command;
	unsigned int erase_size;
	unsigned int block_size;
};

static inline bool falcon_spi_present(const struct falcon_spi_device *spi)
{
	return spi->size != 0;
}

enum {
	FALCON_STAT_tx_bytes,
	FALCON_STAT_tx_packets,
	FALCON_STAT_tx_pause,
	FALCON_STAT_tx_control,
	FALCON_STAT_tx_unicast,
	FALCON_STAT_tx_multicast,
	FALCON_STAT_tx_broadcast,
	FALCON_STAT_tx_lt64,
	FALCON_STAT_tx_64,
	FALCON_STAT_tx_65_to_127,
	FALCON_STAT_tx_128_to_255,
	FALCON_STAT_tx_256_to_511,
	FALCON_STAT_tx_512_to_1023,
	FALCON_STAT_tx_1024_to_15xx,
	FALCON_STAT_tx_15xx_to_jumbo,
	FALCON_STAT_tx_gtjumbo,
	FALCON_STAT_tx_non_tcpudp,
	FALCON_STAT_tx_mac_src_error,
	FALCON_STAT_tx_ip_src_error,
	FALCON_STAT_rx_bytes,
	FALCON_STAT_rx_good_bytes,
	FALCON_STAT_rx_bad_bytes,
	FALCON_STAT_rx_packets,
	FALCON_STAT_rx_good,
	FALCON_STAT_rx_bad,
	FALCON_STAT_rx_pause,
	FALCON_STAT_rx_control,
	FALCON_STAT_rx_unicast,
	FALCON_STAT_rx_multicast,
	FALCON_STAT_rx_broadcast,
	FALCON_STAT_rx_lt64,
	FALCON_STAT_rx_64,
	FALCON_STAT_rx_65_to_127,
	FALCON_STAT_rx_128_to_255,
	FALCON_STAT_rx_256_to_511,
	FALCON_STAT_rx_512_to_1023,
	FALCON_STAT_rx_1024_to_15xx,
	FALCON_STAT_rx_15xx_to_jumbo,
	FALCON_STAT_rx_gtjumbo,
	FALCON_STAT_rx_bad_lt64,
	FALCON_STAT_rx_bad_gtjumbo,
	FALCON_STAT_rx_overflow,
	FALCON_STAT_rx_symbol_error,
	FALCON_STAT_rx_align_error,
	FALCON_STAT_rx_length_error,
	FALCON_STAT_rx_internal_error,
	FALCON_STAT_rx_nodesc_drop_cnt,
	FALCON_STAT_COUNT
};

/**
 * struct falcon_nic_data - Falcon NIC state
 * @pci_dev2: Secondary function of Falcon A
 * @board: Board state and functions
 * @stats: Hardware statistics
 * @stats_disable_count: Nest count for disabling statistics fetches
 * @stats_pending: Is there a pending DMA of MAC statistics.
 * @stats_timer: A timer for regularly fetching MAC statistics.
 * @spi_flash: SPI flash device
 * @spi_eeprom: SPI EEPROM device
 * @spi_lock: SPI bus lock
 * @mdio_lock: MDIO bus lock
 * @xmac_poll_required: XMAC link state needs polling
 */
struct falcon_nic_data {
	struct pci_dev *pci_dev2;
	struct falcon_board board;
	u64 stats[FALCON_STAT_COUNT];
	unsigned int stats_disable_count;
	bool stats_pending;
	struct timer_list stats_timer;
	struct falcon_spi_device spi_flash;
	struct falcon_spi_device spi_eeprom;
	struct mutex spi_lock;
	struct mutex mdio_lock;
	bool xmac_poll_required;
};

static inline struct falcon_board *falcon_board(struct efx_nic *efx)
{
	struct falcon_nic_data *data = efx->nic_data;
	return &data->board;
}

enum {
	SIENA_STAT_tx_bytes,
	SIENA_STAT_tx_good_bytes,
	SIENA_STAT_tx_bad_bytes,
	SIENA_STAT_tx_packets,
	SIENA_STAT_tx_bad,
	SIENA_STAT_tx_pause,
	SIENA_STAT_tx_control,
	SIENA_STAT_tx_unicast,
	SIENA_STAT_tx_multicast,
	SIENA_STAT_tx_broadcast,
	SIENA_STAT_tx_lt64,
	SIENA_STAT_tx_64,
	SIENA_STAT_tx_65_to_127,
	SIENA_STAT_tx_128_to_255,
	SIENA_STAT_tx_256_to_511,
	SIENA_STAT_tx_512_to_1023,
	SIENA_STAT_tx_1024_to_15xx,
	SIENA_STAT_tx_15xx_to_jumbo,
	SIENA_STAT_tx_gtjumbo,
	SIENA_STAT_tx_collision,
	SIENA_STAT_tx_single_collision,
	SIENA_STAT_tx_multiple_collision,
	SIENA_STAT_tx_excessive_collision,
	SIENA_STAT_tx_deferred,
	SIENA_STAT_tx_late_collision,
	SIENA_STAT_tx_excessive_deferred,
	SIENA_STAT_tx_non_tcpudp,
	SIENA_STAT_tx_mac_src_error,
	SIENA_STAT_tx_ip_src_error,
	SIENA_STAT_rx_bytes,
	SIENA_STAT_rx_good_bytes,
	SIENA_STAT_rx_bad_bytes,
	SIENA_STAT_rx_packets,
	SIENA_STAT_rx_good,
	SIENA_STAT_rx_bad,
	SIENA_STAT_rx_pause,
	SIENA_STAT_rx_control,
	SIENA_STAT_rx_unicast,
	SIENA_STAT_rx_multicast,
	SIENA_STAT_rx_broadcast,
	SIENA_STAT_rx_lt64,
	SIENA_STAT_rx_64,
	SIENA_STAT_rx_65_to_127,
	SIENA_STAT_rx_128_to_255,
	SIENA_STAT_rx_256_to_511,
	SIENA_STAT_rx_512_to_1023,
	SIENA_STAT_rx_1024_to_15xx,
	SIENA_STAT_rx_15xx_to_jumbo,
	SIENA_STAT_rx_gtjumbo,
	SIENA_STAT_rx_bad_gtjumbo,
	SIENA_STAT_rx_overflow,
	SIENA_STAT_rx_false_carrier,
	SIENA_STAT_rx_symbol_error,
	SIENA_STAT_rx_align_error,
	SIENA_STAT_rx_length_error,
	SIENA_STAT_rx_internal_error,
	SIENA_STAT_rx_nodesc_drop_cnt,
	SIENA_STAT_COUNT
};

/**
 * struct siena_nic_data - Siena NIC state
 * @wol_filter_id: Wake-on-LAN packet filter id
 * @stats: Hardware statistics
 */
struct siena_nic_data {
	int wol_filter_id;
	u64 stats[SIENA_STAT_COUNT];
};

enum {
	EF10_STAT_tx_bytes,
	EF10_STAT_tx_packets,
	EF10_STAT_tx_pause,
	EF10_STAT_tx_control,
	EF10_STAT_tx_unicast,
	EF10_STAT_tx_multicast,
	EF10_STAT_tx_broadcast,
	EF10_STAT_tx_lt64,
	EF10_STAT_tx_64,
	EF10_STAT_tx_65_to_127,
	EF10_STAT_tx_128_to_255,
	EF10_STAT_tx_256_to_511,
	EF10_STAT_tx_512_to_1023,
	EF10_STAT_tx_1024_to_15xx,
	EF10_STAT_tx_15xx_to_jumbo,
	EF10_STAT_rx_bytes,
	EF10_STAT_rx_bytes_minus_good_bytes,
	EF10_STAT_rx_good_bytes,
	EF10_STAT_rx_bad_bytes,
	EF10_STAT_rx_packets,
	EF10_STAT_rx_good,
	EF10_STAT_rx_bad,
	EF10_STAT_rx_pause,
	EF10_STAT_rx_control,
	EF10_STAT_rx_unicast,
	EF10_STAT_rx_multicast,
	EF10_STAT_rx_broadcast,
	EF10_STAT_rx_lt64,
	EF10_STAT_rx_64,
	EF10_STAT_rx_65_to_127,
	EF10_STAT_rx_128_to_255,
	EF10_STAT_rx_256_to_511,
	EF10_STAT_rx_512_to_1023,
	EF10_STAT_rx_1024_to_15xx,
	EF10_STAT_rx_15xx_to_jumbo,
	EF10_STAT_rx_gtjumbo,
	EF10_STAT_rx_bad_gtjumbo,
	EF10_STAT_rx_overflow,
	EF10_STAT_rx_align_error,
	EF10_STAT_rx_length_error,
	EF10_STAT_rx_nodesc_drops,
	EF10_STAT_rx_pm_trunc_bb_overflow,
	EF10_STAT_rx_pm_discard_bb_overflow,
	EF10_STAT_rx_pm_trunc_vfifo_full,
	EF10_STAT_rx_pm_discard_vfifo_full,
	EF10_STAT_rx_pm_trunc_qbb,
	EF10_STAT_rx_pm_discard_qbb,
	EF10_STAT_rx_pm_discard_mapping,
	EF10_STAT_rx_dp_q_disabled_packets,
	EF10_STAT_rx_dp_di_dropped_packets,
	EF10_STAT_rx_dp_streaming_packets,
	EF10_STAT_rx_dp_emerg_fetch,
	EF10_STAT_rx_dp_emerg_wait,
	EF10_STAT_COUNT
};

/**
 * struct efx_ef10_nic_data - EF10 architecture NIC state
 * @mcdi_buf: DMA buffer for MCDI
 * @warm_boot_count: Last seen MC warm boot count
 * @vi_base: Absolute index of first VI in this function
 * @n_allocated_vis: Number of VIs allocated to this function
 * @must_realloc_vis: Flag: VIs have yet to be reallocated after MC reboot
 * @must_restore_filters: Flag: filters have yet to be restored after MC reboot
 * @rx_rss_context: Firmware handle for our RSS context
 * @stats: Hardware statistics
 * @workaround_35388: Flag: firmware supports workaround for bug 35388
 * @must_check_datapath_caps: Flag: @datapath_caps needs to be revalidated
 *	after MC reboot
 * @datapath_caps: Capabilities of datapath firmware (FLAGS1 field of
 *	%MC_CMD_GET_CAPABILITIES response)
 */
struct efx_ef10_nic_data {
	struct efx_buffer mcdi_buf;
	u16 warm_boot_count;
	unsigned int vi_base;
	unsigned int n_allocated_vis;
	bool must_realloc_vis;
	bool must_restore_filters;
	u32 rx_rss_context;
	u64 stats[EF10_STAT_COUNT];
	bool workaround_35388;
	bool must_check_datapath_caps;
	u32 datapath_caps;
};

/*
 * On the SFC9000 family each port is associated with 1 PCI physical
 * function (PF) handled by sfc and a configurable number of virtual
 * functions (VFs) that may be handled by some other driver, often in
 * a VM guest.  The queue pointer registers are mapped in both PF and
 * VF BARs such that an 8K region provides access to a single RX, TX
 * and event queue (collectively a Virtual Interface, VI or VNIC).
 *
 * The PF has access to all 1024 VIs while VFs are mapped to VIs
 * according to VI_BASE and VI_SCALE: VF i has access to VIs numbered
 * in range [VI_BASE + i << VI_SCALE, VI_BASE + i + 1 << VI_SCALE).
 * The number of VIs and the VI_SCALE value are configurable but must
 * be established at boot time by firmware.
 */

/* Maximum VI_SCALE parameter supported by Siena */
#define EFX_VI_SCALE_MAX 6
/* Base VI to use for SR-IOV. Must be aligned to (1 << EFX_VI_SCALE_MAX),
 * so this is the smallest allowed value. */
#define EFX_VI_BASE 128U
/* Maximum number of VFs allowed */
#define EFX_VF_COUNT_MAX 127
/* Limit EVQs on VFs to be only 8k to reduce buffer table reservation */
#define EFX_MAX_VF_EVQ_SIZE 8192UL
/* The number of buffer table entries reserved for each VI on a VF */
#define EFX_VF_BUFTBL_PER_VI					\
	((EFX_MAX_VF_EVQ_SIZE + 2 * EFX_MAX_DMAQ_SIZE) *	\
	 sizeof(efx_qword_t) / EFX_BUF_SIZE)

#ifdef CONFIG_SFC_SRIOV

static inline bool efx_sriov_wanted(struct efx_nic *efx)
{
	return efx->vf_count != 0;
}
static inline bool efx_sriov_enabled(struct efx_nic *efx)
{
	return efx->vf_init_count != 0;
}
static inline unsigned int efx_vf_size(struct efx_nic *efx)
{
	return 1 << efx->vi_scale;
}

extern int efx_init_sriov(void);
extern void efx_sriov_probe(struct efx_nic *efx);
extern int efx_sriov_init(struct efx_nic *efx);
extern void efx_sriov_mac_address_changed(struct efx_nic *efx);
extern void efx_sriov_tx_flush_done(struct efx_nic *efx, efx_qword_t *event);
extern void efx_sriov_rx_flush_done(struct efx_nic *efx, efx_qword_t *event);
extern void efx_sriov_event(struct efx_channel *channel, efx_qword_t *event);
extern void efx_sriov_desc_fetch_err(struct efx_nic *efx, unsigned dmaq);
extern void efx_sriov_flr(struct efx_nic *efx, unsigned flr);
extern void efx_sriov_reset(struct efx_nic *efx);
extern void efx_sriov_fini(struct efx_nic *efx);
extern void efx_fini_sriov(void);

#else

static inline bool efx_sriov_wanted(struct efx_nic *efx) { return false; }
static inline bool efx_sriov_enabled(struct efx_nic *efx) { return false; }
static inline unsigned int efx_vf_size(struct efx_nic *efx) { return 0; }

static inline int efx_init_sriov(void) { return 0; }
static inline void efx_sriov_probe(struct efx_nic *efx) {}
static inline int efx_sriov_init(struct efx_nic *efx) { return -EOPNOTSUPP; }
static inline void efx_sriov_mac_address_changed(struct efx_nic *efx) {}
static inline void efx_sriov_tx_flush_done(struct efx_nic *efx,
					   efx_qword_t *event) {}
static inline void efx_sriov_rx_flush_done(struct efx_nic *efx,
					   efx_qword_t *event) {}
static inline void efx_sriov_event(struct efx_channel *channel,
				   efx_qword_t *event) {}
static inline void efx_sriov_desc_fetch_err(struct efx_nic *efx, unsigned dmaq) {}
static inline void efx_sriov_flr(struct efx_nic *efx, unsigned flr) {}
static inline void efx_sriov_reset(struct efx_nic *efx) {}
static inline void efx_sriov_fini(struct efx_nic *efx) {}
static inline void efx_fini_sriov(void) {}

#endif

extern int efx_sriov_set_vf_mac(struct net_device *dev, int vf, u8 *mac);
extern int efx_sriov_set_vf_vlan(struct net_device *dev, int vf,
				 u16 vlan, u8 qos);
extern int efx_sriov_get_vf_config(struct net_device *dev, int vf,
				   struct ifla_vf_info *ivf);
extern int efx_sriov_set_vf_spoofchk(struct net_device *net_dev, int vf,
				     bool spoofchk);

struct ethtool_ts_info;
extern void efx_ptp_probe(struct efx_nic *efx);
extern int efx_ptp_ioctl(struct efx_nic *efx, struct ifreq *ifr, int cmd);
extern void efx_ptp_get_ts_info(struct efx_nic *efx,
				struct ethtool_ts_info *ts_info);
extern bool efx_ptp_is_ptp_tx(struct efx_nic *efx, struct sk_buff *skb);
extern int efx_ptp_tx(struct efx_nic *efx, struct sk_buff *skb);
extern void efx_ptp_event(struct efx_nic *efx, efx_qword_t *ev);
void efx_ptp_start_datapath(struct efx_nic *efx);
void efx_ptp_stop_datapath(struct efx_nic *efx);

extern const struct efx_nic_type falcon_a1_nic_type;
extern const struct efx_nic_type falcon_b0_nic_type;
extern const struct efx_nic_type siena_a0_nic_type;
extern const struct efx_nic_type efx_hunt_a0_nic_type;

/**************************************************************************
 *
 * Externs
 *
 **************************************************************************
 */

extern int falcon_probe_board(struct efx_nic *efx, u16 revision_info);

/* TX data path */
static inline int efx_nic_probe_tx(struct efx_tx_queue *tx_queue)
{
	return tx_queue->efx->type->tx_probe(tx_queue);
}
static inline void efx_nic_init_tx(struct efx_tx_queue *tx_queue)
{
	tx_queue->efx->type->tx_init(tx_queue);
}
static inline void efx_nic_remove_tx(struct efx_tx_queue *tx_queue)
{
	tx_queue->efx->type->tx_remove(tx_queue);
}
static inline void efx_nic_push_buffers(struct efx_tx_queue *tx_queue)
{
	tx_queue->efx->type->tx_write(tx_queue);
}

/* RX data path */
static inline int efx_nic_probe_rx(struct efx_rx_queue *rx_queue)
{
	return rx_queue->efx->type->rx_probe(rx_queue);
}
static inline void efx_nic_init_rx(struct efx_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_init(rx_queue);
}
static inline void efx_nic_remove_rx(struct efx_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_remove(rx_queue);
}
static inline void efx_nic_notify_rx_desc(struct efx_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_write(rx_queue);
}
static inline void efx_nic_generate_fill_event(struct efx_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_defer_refill(rx_queue);
}

/* Event data path */
static inline int efx_nic_probe_eventq(struct efx_channel *channel)
{
	return channel->efx->type->ev_probe(channel);
}
static inline int efx_nic_init_eventq(struct efx_channel *channel)
{
	return channel->efx->type->ev_init(channel);
}
static inline void efx_nic_fini_eventq(struct efx_channel *channel)
{
	channel->efx->type->ev_fini(channel);
}
static inline void efx_nic_remove_eventq(struct efx_channel *channel)
{
	channel->efx->type->ev_remove(channel);
}
static inline int
efx_nic_process_eventq(struct efx_channel *channel, int quota)
{
	return channel->efx->type->ev_process(channel, quota);
}
static inline void efx_nic_eventq_read_ack(struct efx_channel *channel)
{
	channel->efx->type->ev_read_ack(channel);
}
extern void efx_nic_event_test_start(struct efx_channel *channel);

/* Falcon/Siena queue operations */
extern int efx_farch_tx_probe(struct efx_tx_queue *tx_queue);
extern void efx_farch_tx_init(struct efx_tx_queue *tx_queue);
extern void efx_farch_tx_fini(struct efx_tx_queue *tx_queue);
extern void efx_farch_tx_remove(struct efx_tx_queue *tx_queue);
extern void efx_farch_tx_write(struct efx_tx_queue *tx_queue);
extern int efx_farch_rx_probe(struct efx_rx_queue *rx_queue);
extern void efx_farch_rx_init(struct efx_rx_queue *rx_queue);
extern void efx_farch_rx_fini(struct efx_rx_queue *rx_queue);
extern void efx_farch_rx_remove(struct efx_rx_queue *rx_queue);
extern void efx_farch_rx_write(struct efx_rx_queue *rx_queue);
extern void efx_farch_rx_defer_refill(struct efx_rx_queue *rx_queue);
extern int efx_farch_ev_probe(struct efx_channel *channel);
extern int efx_farch_ev_init(struct efx_channel *channel);
extern void efx_farch_ev_fini(struct efx_channel *channel);
extern void efx_farch_ev_remove(struct efx_channel *channel);
extern int efx_farch_ev_process(struct efx_channel *channel, int quota);
extern void efx_farch_ev_read_ack(struct efx_channel *channel);
extern void efx_farch_ev_test_generate(struct efx_channel *channel);

/* Falcon/Siena filter operations */
extern int efx_farch_filter_table_probe(struct efx_nic *efx);
extern void efx_farch_filter_table_restore(struct efx_nic *efx);
extern void efx_farch_filter_table_remove(struct efx_nic *efx);
extern void efx_farch_filter_update_rx_scatter(struct efx_nic *efx);
extern s32 efx_farch_filter_insert(struct efx_nic *efx,
				   struct efx_filter_spec *spec, bool replace);
extern int efx_farch_filter_remove_safe(struct efx_nic *efx,
					enum efx_filter_priority priority,
					u32 filter_id);
extern int efx_farch_filter_get_safe(struct efx_nic *efx,
				     enum efx_filter_priority priority,
				     u32 filter_id, struct efx_filter_spec *);
extern void efx_farch_filter_clear_rx(struct efx_nic *efx,
				      enum efx_filter_priority priority);
extern u32 efx_farch_filter_count_rx_used(struct efx_nic *efx,
					  enum efx_filter_priority priority);
extern u32 efx_farch_filter_get_rx_id_limit(struct efx_nic *efx);
extern s32 efx_farch_filter_get_rx_ids(struct efx_nic *efx,
				       enum efx_filter_priority priority,
				       u32 *buf, u32 size);
#ifdef CONFIG_RFS_ACCEL
extern s32 efx_farch_filter_rfs_insert(struct efx_nic *efx,
				       struct efx_filter_spec *spec);
extern bool efx_farch_filter_rfs_expire_one(struct efx_nic *efx, u32 flow_id,
					    unsigned int index);
#endif
extern void efx_farch_filter_sync_rx_mode(struct efx_nic *efx);

extern bool efx_nic_event_present(struct efx_channel *channel);

/* Some statistics are computed as A - B where A and B each increase
 * linearly with some hardware counter(s) and the counters are read
 * asynchronously.  If the counters contributing to B are always read
 * after those contributing to A, the computed value may be lower than
 * the true value by some variable amount, and may decrease between
 * subsequent computations.
 *
 * We should never allow statistics to decrease or to exceed the true
 * value.  Since the computed value will never be greater than the
 * true value, we can achieve this by only storing the computed value
 * when it increases.
 */
static inline void efx_update_diff_stat(u64 *stat, u64 diff)
{
	if ((s64)(diff - *stat) > 0)
		*stat = diff;
}

/* Interrupts */
extern int efx_nic_init_interrupt(struct efx_nic *efx);
extern void efx_nic_irq_test_start(struct efx_nic *efx);
extern void efx_nic_fini_interrupt(struct efx_nic *efx);

/* Falcon/Siena interrupts */
extern void efx_farch_irq_enable_master(struct efx_nic *efx);
extern void efx_farch_irq_test_generate(struct efx_nic *efx);
extern void efx_farch_irq_disable_master(struct efx_nic *efx);
extern irqreturn_t efx_farch_msi_interrupt(int irq, void *dev_id);
extern irqreturn_t efx_farch_legacy_interrupt(int irq, void *dev_id);
extern irqreturn_t efx_farch_fatal_interrupt(struct efx_nic *efx);

static inline int efx_nic_event_test_irq_cpu(struct efx_channel *channel)
{
	return ACCESS_ONCE(channel->event_test_cpu);
}
static inline int efx_nic_irq_test_irq_cpu(struct efx_nic *efx)
{
	return ACCESS_ONCE(efx->last_irq_cpu);
}

/* Global Resources */
extern int efx_nic_flush_queues(struct efx_nic *efx);
extern void siena_prepare_flush(struct efx_nic *efx);
extern int efx_farch_fini_dmaq(struct efx_nic *efx);
extern void siena_finish_flush(struct efx_nic *efx);
extern void falcon_start_nic_stats(struct efx_nic *efx);
extern void falcon_stop_nic_stats(struct efx_nic *efx);
extern int falcon_reset_xaui(struct efx_nic *efx);
extern void efx_farch_dimension_resources(struct efx_nic *efx, unsigned sram_lim_qw);
extern void efx_farch_init_common(struct efx_nic *efx);
extern void efx_ef10_handle_drain_event(struct efx_nic *efx);
static inline void efx_nic_push_rx_indir_table(struct efx_nic *efx)
{
	efx->type->rx_push_indir_table(efx);
}
extern void efx_farch_rx_push_indir_table(struct efx_nic *efx);

int efx_nic_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			 unsigned int len, gfp_t gfp_flags);
void efx_nic_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer);

/* Tests */
struct efx_farch_register_test {
	unsigned address;
	efx_oword_t mask;
};
extern int efx_farch_test_registers(struct efx_nic *efx,
				    const struct efx_farch_register_test *regs,
				    size_t n_regs);

extern size_t efx_nic_get_regs_len(struct efx_nic *efx);
extern void efx_nic_get_regs(struct efx_nic *efx, void *buf);

extern size_t
efx_nic_describe_stats(const struct efx_hw_stat_desc *desc, size_t count,
		       const unsigned long *mask, u8 *names);
extern void
efx_nic_update_stats(const struct efx_hw_stat_desc *desc, size_t count,
		     const unsigned long *mask,
		     u64 *stats, const void *dma_buf, bool accumulate);

#define EFX_MAX_FLUSH_TIME 5000

extern void efx_farch_generate_event(struct efx_nic *efx, unsigned int evq,
				     efx_qword_t *event);

#endif /* EFX_NIC_H */
