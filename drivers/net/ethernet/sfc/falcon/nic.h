/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EF4_NIC_H
#define EF4_NIC_H

#include <linux/net_tstamp.h>
#include <linux/i2c-algo-bit.h>
#include "net_driver.h"
#include "efx.h"

enum {
	EF4_REV_FALCON_A0 = 0,
	EF4_REV_FALCON_A1 = 1,
	EF4_REV_FALCON_B0 = 2,
};

static inline int ef4_nic_rev(struct ef4_nic *efx)
{
	return efx->type->revision;
}

u32 ef4_farch_fpga_ver(struct ef4_nic *efx);

/* NIC has two interlinked PCI functions for the same port. */
static inline bool ef4_nic_is_dual_func(struct ef4_nic *efx)
{
	return ef4_nic_rev(efx) < EF4_REV_FALCON_B0;
}

/* Read the current event from the event queue */
static inline ef4_qword_t *ef4_event(struct ef4_channel *channel,
				     unsigned int index)
{
	return ((ef4_qword_t *) (channel->eventq.buf.addr)) +
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
static inline int ef4_event_present(ef4_qword_t *event)
{
	return !(EF4_DWORD_IS_ALL_ONES(event->dword[0]) |
		  EF4_DWORD_IS_ALL_ONES(event->dword[1]));
}

/* Returns a pointer to the specified transmit descriptor in the TX
 * descriptor queue belonging to the specified channel.
 */
static inline ef4_qword_t *
ef4_tx_desc(struct ef4_tx_queue *tx_queue, unsigned int index)
{
	return ((ef4_qword_t *) (tx_queue->txd.buf.addr)) + index;
}

/* Get partner of a TX queue, seen as part of the same net core queue */
static inline struct ef4_tx_queue *ef4_tx_queue_partner(struct ef4_tx_queue *tx_queue)
{
	if (tx_queue->queue & EF4_TXQ_TYPE_OFFLOAD)
		return tx_queue - EF4_TXQ_TYPE_OFFLOAD;
	else
		return tx_queue + EF4_TXQ_TYPE_OFFLOAD;
}

/* Report whether this TX queue would be empty for the given write_count.
 * May return false negative.
 */
static inline bool __ef4_nic_tx_is_empty(struct ef4_tx_queue *tx_queue,
					 unsigned int write_count)
{
	unsigned int empty_read_count = READ_ONCE(tx_queue->empty_read_count);

	if (empty_read_count == 0)
		return false;

	return ((empty_read_count ^ write_count) & ~EF4_EMPTY_COUNT_VALID) == 0;
}

/* Decide whether to push a TX descriptor to the NIC vs merely writing
 * the doorbell.  This can reduce latency when we are adding a single
 * descriptor to an empty queue, but is otherwise pointless.  Further,
 * Falcon and Siena have hardware bugs (SF bug 33851) that may be
 * triggered if we don't check this.
 * We use the write_count used for the last doorbell push, to get the
 * NIC's view of the tx queue.
 */
static inline bool ef4_nic_may_push_tx_desc(struct ef4_tx_queue *tx_queue,
					    unsigned int write_count)
{
	bool was_empty = __ef4_nic_tx_is_empty(tx_queue, write_count);

	tx_queue->empty_read_count = 0;
	return was_empty && tx_queue->write_count - write_count == 1;
}

/* Returns a pointer to the specified descriptor in the RX descriptor queue */
static inline ef4_qword_t *
ef4_rx_desc(struct ef4_rx_queue *rx_queue, unsigned int index)
{
	return ((ef4_qword_t *) (rx_queue->rxd.buf.addr)) + index;
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
#define EF4_PAGE_SIZE	4096
/* Size and alignment of buffer table entries (same) */
#define EF4_BUF_SIZE	EF4_PAGE_SIZE

/* NIC-generic software stats */
enum {
	GENERIC_STAT_rx_noskb_drops,
	GENERIC_STAT_rx_nodesc_trunc,
	GENERIC_STAT_COUNT
};

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
	int (*init) (struct ef4_nic *nic);
	void (*init_phy) (struct ef4_nic *efx);
	void (*fini) (struct ef4_nic *nic);
	void (*set_id_led) (struct ef4_nic *efx, enum ef4_led_mode mode);
	int (*monitor) (struct ef4_nic *nic);
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
	FALCON_STAT_tx_bytes = GENERIC_STAT_COUNT,
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

static inline struct falcon_board *falcon_board(struct ef4_nic *efx)
{
	struct falcon_nic_data *data = efx->nic_data;
	return &data->board;
}

struct ethtool_ts_info;

extern const struct ef4_nic_type falcon_a1_nic_type;
extern const struct ef4_nic_type falcon_b0_nic_type;

/**************************************************************************
 *
 * Externs
 *
 **************************************************************************
 */

int falcon_probe_board(struct ef4_nic *efx, u16 revision_info);

/* TX data path */
static inline int ef4_nic_probe_tx(struct ef4_tx_queue *tx_queue)
{
	return tx_queue->efx->type->tx_probe(tx_queue);
}
static inline void ef4_nic_init_tx(struct ef4_tx_queue *tx_queue)
{
	tx_queue->efx->type->tx_init(tx_queue);
}
static inline void ef4_nic_remove_tx(struct ef4_tx_queue *tx_queue)
{
	tx_queue->efx->type->tx_remove(tx_queue);
}
static inline void ef4_nic_push_buffers(struct ef4_tx_queue *tx_queue)
{
	tx_queue->efx->type->tx_write(tx_queue);
}

/* RX data path */
static inline int ef4_nic_probe_rx(struct ef4_rx_queue *rx_queue)
{
	return rx_queue->efx->type->rx_probe(rx_queue);
}
static inline void ef4_nic_init_rx(struct ef4_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_init(rx_queue);
}
static inline void ef4_nic_remove_rx(struct ef4_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_remove(rx_queue);
}
static inline void ef4_nic_notify_rx_desc(struct ef4_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_write(rx_queue);
}
static inline void ef4_nic_generate_fill_event(struct ef4_rx_queue *rx_queue)
{
	rx_queue->efx->type->rx_defer_refill(rx_queue);
}

/* Event data path */
static inline int ef4_nic_probe_eventq(struct ef4_channel *channel)
{
	return channel->efx->type->ev_probe(channel);
}
static inline int ef4_nic_init_eventq(struct ef4_channel *channel)
{
	return channel->efx->type->ev_init(channel);
}
static inline void ef4_nic_fini_eventq(struct ef4_channel *channel)
{
	channel->efx->type->ev_fini(channel);
}
static inline void ef4_nic_remove_eventq(struct ef4_channel *channel)
{
	channel->efx->type->ev_remove(channel);
}
static inline int
ef4_nic_process_eventq(struct ef4_channel *channel, int quota)
{
	return channel->efx->type->ev_process(channel, quota);
}
static inline void ef4_nic_eventq_read_ack(struct ef4_channel *channel)
{
	channel->efx->type->ev_read_ack(channel);
}
void ef4_nic_event_test_start(struct ef4_channel *channel);

/* queue operations */
int ef4_farch_tx_probe(struct ef4_tx_queue *tx_queue);
void ef4_farch_tx_init(struct ef4_tx_queue *tx_queue);
void ef4_farch_tx_fini(struct ef4_tx_queue *tx_queue);
void ef4_farch_tx_remove(struct ef4_tx_queue *tx_queue);
void ef4_farch_tx_write(struct ef4_tx_queue *tx_queue);
unsigned int ef4_farch_tx_limit_len(struct ef4_tx_queue *tx_queue,
				    dma_addr_t dma_addr, unsigned int len);
int ef4_farch_rx_probe(struct ef4_rx_queue *rx_queue);
void ef4_farch_rx_init(struct ef4_rx_queue *rx_queue);
void ef4_farch_rx_fini(struct ef4_rx_queue *rx_queue);
void ef4_farch_rx_remove(struct ef4_rx_queue *rx_queue);
void ef4_farch_rx_write(struct ef4_rx_queue *rx_queue);
void ef4_farch_rx_defer_refill(struct ef4_rx_queue *rx_queue);
int ef4_farch_ev_probe(struct ef4_channel *channel);
int ef4_farch_ev_init(struct ef4_channel *channel);
void ef4_farch_ev_fini(struct ef4_channel *channel);
void ef4_farch_ev_remove(struct ef4_channel *channel);
int ef4_farch_ev_process(struct ef4_channel *channel, int quota);
void ef4_farch_ev_read_ack(struct ef4_channel *channel);
void ef4_farch_ev_test_generate(struct ef4_channel *channel);

/* filter operations */
int ef4_farch_filter_table_probe(struct ef4_nic *efx);
void ef4_farch_filter_table_restore(struct ef4_nic *efx);
void ef4_farch_filter_table_remove(struct ef4_nic *efx);
void ef4_farch_filter_update_rx_scatter(struct ef4_nic *efx);
s32 ef4_farch_filter_insert(struct ef4_nic *efx, struct ef4_filter_spec *spec,
			    bool replace);
int ef4_farch_filter_remove_safe(struct ef4_nic *efx,
				 enum ef4_filter_priority priority,
				 u32 filter_id);
int ef4_farch_filter_get_safe(struct ef4_nic *efx,
			      enum ef4_filter_priority priority, u32 filter_id,
			      struct ef4_filter_spec *);
int ef4_farch_filter_clear_rx(struct ef4_nic *efx,
			      enum ef4_filter_priority priority);
u32 ef4_farch_filter_count_rx_used(struct ef4_nic *efx,
				   enum ef4_filter_priority priority);
u32 ef4_farch_filter_get_rx_id_limit(struct ef4_nic *efx);
s32 ef4_farch_filter_get_rx_ids(struct ef4_nic *efx,
				enum ef4_filter_priority priority, u32 *buf,
				u32 size);
#ifdef CONFIG_RFS_ACCEL
s32 ef4_farch_filter_rfs_insert(struct ef4_nic *efx,
				struct ef4_filter_spec *spec);
bool ef4_farch_filter_rfs_expire_one(struct ef4_nic *efx, u32 flow_id,
				     unsigned int index);
#endif
void ef4_farch_filter_sync_rx_mode(struct ef4_nic *efx);

bool ef4_nic_event_present(struct ef4_channel *channel);

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
static inline void ef4_update_diff_stat(u64 *stat, u64 diff)
{
	if ((s64)(diff - *stat) > 0)
		*stat = diff;
}

/* Interrupts */
int ef4_nic_init_interrupt(struct ef4_nic *efx);
int ef4_nic_irq_test_start(struct ef4_nic *efx);
void ef4_nic_fini_interrupt(struct ef4_nic *efx);
void ef4_farch_irq_enable_master(struct ef4_nic *efx);
int ef4_farch_irq_test_generate(struct ef4_nic *efx);
void ef4_farch_irq_disable_master(struct ef4_nic *efx);
irqreturn_t ef4_farch_msi_interrupt(int irq, void *dev_id);
irqreturn_t ef4_farch_legacy_interrupt(int irq, void *dev_id);
irqreturn_t ef4_farch_fatal_interrupt(struct ef4_nic *efx);

static inline int ef4_nic_event_test_irq_cpu(struct ef4_channel *channel)
{
	return READ_ONCE(channel->event_test_cpu);
}
static inline int ef4_nic_irq_test_irq_cpu(struct ef4_nic *efx)
{
	return READ_ONCE(efx->last_irq_cpu);
}

/* Global Resources */
int ef4_nic_flush_queues(struct ef4_nic *efx);
int ef4_farch_fini_dmaq(struct ef4_nic *efx);
void ef4_farch_finish_flr(struct ef4_nic *efx);
void falcon_start_nic_stats(struct ef4_nic *efx);
void falcon_stop_nic_stats(struct ef4_nic *efx);
int falcon_reset_xaui(struct ef4_nic *efx);
void ef4_farch_dimension_resources(struct ef4_nic *efx, unsigned sram_lim_qw);
void ef4_farch_init_common(struct ef4_nic *efx);
void ef4_farch_rx_push_indir_table(struct ef4_nic *efx);

int ef4_nic_alloc_buffer(struct ef4_nic *efx, struct ef4_buffer *buffer,
			 unsigned int len, gfp_t gfp_flags);
void ef4_nic_free_buffer(struct ef4_nic *efx, struct ef4_buffer *buffer);

/* Tests */
struct ef4_farch_register_test {
	unsigned address;
	ef4_oword_t mask;
};
int ef4_farch_test_registers(struct ef4_nic *efx,
			     const struct ef4_farch_register_test *regs,
			     size_t n_regs);

size_t ef4_nic_get_regs_len(struct ef4_nic *efx);
void ef4_nic_get_regs(struct ef4_nic *efx, void *buf);

size_t ef4_nic_describe_stats(const struct ef4_hw_stat_desc *desc, size_t count,
			      const unsigned long *mask, u8 *names);
void ef4_nic_update_stats(const struct ef4_hw_stat_desc *desc, size_t count,
			  const unsigned long *mask, u64 *stats,
			  const void *dma_buf, bool accumulate);
void ef4_nic_fix_nodesc_drop_stat(struct ef4_nic *efx, u64 *stat);

#define EF4_MAX_FLUSH_TIME 5000

void ef4_farch_generate_event(struct ef4_nic *efx, unsigned int evq,
			      ef4_qword_t *event);

#endif /* EF4_NIC_H */
