/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_NIC_H
#define EFX_NIC_H

#include <linux/i2c-algo-bit.h>
#include "net_driver.h"
#include "efx.h"
#include "mcdi.h"
#include "spi.h"

/*
 * Falcon hardware control
 */

enum {
	EFX_REV_FALCON_A0 = 0,
	EFX_REV_FALCON_A1 = 1,
	EFX_REV_FALCON_B0 = 2,
	EFX_REV_SIENA_A0 = 3,
};

static inline int efx_nic_rev(struct efx_nic *efx)
{
	return efx->type->revision;
}

extern u32 efx_nic_fpga_ver(struct efx_nic *efx);

static inline bool efx_nic_has_mc(struct efx_nic *efx)
{
	return efx_nic_rev(efx) >= EFX_REV_SIENA_A0;
}
/* NIC has two interlinked PCI functions for the same port. */
static inline bool efx_nic_is_dual_func(struct efx_nic *efx)
{
	return efx_nic_rev(efx) < EFX_REV_FALCON_B0;
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

#define FALCON_GMAC_LOOPBACKS			\
	(1 << LOOPBACK_GMAC)

/**
 * struct falcon_board_type - board operations and type information
 * @id: Board type id, as found in NVRAM
 * @ref_model: Model number of Solarflare reference design
 * @gen_type: Generic board type description
 * @init: Allocate resources and initialise peripheral hardware
 * @init_phy: Do board-specific PHY initialisation
 * @fini: Shut down hardware and free resources
 * @set_id_led: Set state of identifying LED or revert to automatic function
 * @monitor: Board-specific health check function
 */
struct falcon_board_type {
	u8 id;
	const char *ref_model;
	const char *gen_type;
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
 * struct falcon_nic_data - Falcon NIC state
 * @pci_dev2: Secondary function of Falcon A
 * @board: Board state and functions
 * @stats_disable_count: Nest count for disabling statistics fetches
 * @stats_pending: Is there a pending DMA of MAC statistics.
 * @stats_timer: A timer for regularly fetching MAC statistics.
 * @stats_dma_done: Pointer to the flag which indicates DMA completion.
 * @spi_flash: SPI flash device
 * @spi_eeprom: SPI EEPROM device
 * @spi_lock: SPI bus lock
 * @mdio_lock: MDIO bus lock
 * @xmac_poll_required: XMAC link state needs polling
 */
struct falcon_nic_data {
	struct pci_dev *pci_dev2;
	struct falcon_board board;
	unsigned int stats_disable_count;
	bool stats_pending;
	struct timer_list stats_timer;
	u32 *stats_dma_done;
	struct efx_spi_device spi_flash;
	struct efx_spi_device spi_eeprom;
	struct mutex spi_lock;
	struct mutex mdio_lock;
	bool xmac_poll_required;
};

static inline struct falcon_board *falcon_board(struct efx_nic *efx)
{
	struct falcon_nic_data *data = efx->nic_data;
	return &data->board;
}

/**
 * struct siena_nic_data - Siena NIC state
 * @fw_version: Management controller firmware version
 * @fw_build: Firmware build number
 * @mcdi: Management-Controller-to-Driver Interface
 * @wol_filter_id: Wake-on-LAN packet filter id
 */
struct siena_nic_data {
	u64 fw_version;
	u32 fw_build;
	struct efx_mcdi_iface mcdi;
	int wol_filter_id;
};

extern void siena_print_fwver(struct efx_nic *efx, char *buf, size_t len);

extern struct efx_nic_type falcon_a1_nic_type;
extern struct efx_nic_type falcon_b0_nic_type;
extern struct efx_nic_type siena_a0_nic_type;

/**************************************************************************
 *
 * Externs
 *
 **************************************************************************
 */

extern int falcon_probe_board(struct efx_nic *efx, u16 revision_info);

/* TX data path */
extern int efx_nic_probe_tx(struct efx_tx_queue *tx_queue);
extern void efx_nic_init_tx(struct efx_tx_queue *tx_queue);
extern void efx_nic_fini_tx(struct efx_tx_queue *tx_queue);
extern void efx_nic_remove_tx(struct efx_tx_queue *tx_queue);
extern void efx_nic_push_buffers(struct efx_tx_queue *tx_queue);

/* RX data path */
extern int efx_nic_probe_rx(struct efx_rx_queue *rx_queue);
extern void efx_nic_init_rx(struct efx_rx_queue *rx_queue);
extern void efx_nic_fini_rx(struct efx_rx_queue *rx_queue);
extern void efx_nic_remove_rx(struct efx_rx_queue *rx_queue);
extern void efx_nic_notify_rx_desc(struct efx_rx_queue *rx_queue);

/* Event data path */
extern int efx_nic_probe_eventq(struct efx_channel *channel);
extern void efx_nic_init_eventq(struct efx_channel *channel);
extern void efx_nic_fini_eventq(struct efx_channel *channel);
extern void efx_nic_remove_eventq(struct efx_channel *channel);
extern int efx_nic_process_eventq(struct efx_channel *channel, int rx_quota);
extern void efx_nic_eventq_read_ack(struct efx_channel *channel);

/* MAC/PHY */
extern void falcon_drain_tx_fifo(struct efx_nic *efx);
extern void falcon_reconfigure_mac_wrapper(struct efx_nic *efx);
extern int efx_nic_rx_xoff_thresh, efx_nic_rx_xon_thresh;

/* Interrupts and test events */
extern int efx_nic_init_interrupt(struct efx_nic *efx);
extern void efx_nic_enable_interrupts(struct efx_nic *efx);
extern void efx_nic_generate_test_event(struct efx_channel *channel);
extern void efx_nic_generate_fill_event(struct efx_channel *channel);
extern void efx_nic_generate_interrupt(struct efx_nic *efx);
extern void efx_nic_disable_interrupts(struct efx_nic *efx);
extern void efx_nic_fini_interrupt(struct efx_nic *efx);
extern irqreturn_t efx_nic_fatal_interrupt(struct efx_nic *efx);
extern irqreturn_t falcon_legacy_interrupt_a1(int irq, void *dev_id);
extern void falcon_irq_ack_a1(struct efx_nic *efx);

#define EFX_IRQ_MOD_RESOLUTION 5

/* Global Resources */
extern int efx_nic_flush_queues(struct efx_nic *efx);
extern void falcon_start_nic_stats(struct efx_nic *efx);
extern void falcon_stop_nic_stats(struct efx_nic *efx);
extern void falcon_setup_xaui(struct efx_nic *efx);
extern int falcon_reset_xaui(struct efx_nic *efx);
extern void efx_nic_init_common(struct efx_nic *efx);
extern void efx_nic_push_rx_indir_table(struct efx_nic *efx);

int efx_nic_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			 unsigned int len);
void efx_nic_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer);

/* Tests */
struct efx_nic_register_test {
	unsigned address;
	efx_oword_t mask;
};
extern int efx_nic_test_registers(struct efx_nic *efx,
				  const struct efx_nic_register_test *regs,
				  size_t n_regs);

extern size_t efx_nic_get_regs_len(struct efx_nic *efx);
extern void efx_nic_get_regs(struct efx_nic *efx, void *buf);

/**************************************************************************
 *
 * Falcon MAC stats
 *
 **************************************************************************
 */

#define FALCON_STAT_OFFSET(falcon_stat) EFX_VAL(falcon_stat, offset)
#define FALCON_STAT_WIDTH(falcon_stat) EFX_VAL(falcon_stat, WIDTH)

/* Retrieve statistic from statistics block */
#define FALCON_STAT(efx, falcon_stat, efx_stat) do {		\
	if (FALCON_STAT_WIDTH(falcon_stat) == 16)		\
		(efx)->mac_stats.efx_stat += le16_to_cpu(	\
			*((__force __le16 *)				\
			  (efx->stats_buffer.addr +		\
			   FALCON_STAT_OFFSET(falcon_stat))));	\
	else if (FALCON_STAT_WIDTH(falcon_stat) == 32)		\
		(efx)->mac_stats.efx_stat += le32_to_cpu(	\
			*((__force __le32 *)				\
			  (efx->stats_buffer.addr +		\
			   FALCON_STAT_OFFSET(falcon_stat))));	\
	else							\
		(efx)->mac_stats.efx_stat += le64_to_cpu(	\
			*((__force __le64 *)				\
			  (efx->stats_buffer.addr +		\
			   FALCON_STAT_OFFSET(falcon_stat))));	\
	} while (0)

#define FALCON_MAC_STATS_SIZE 0x100

#define MAC_DATA_LBN 0
#define MAC_DATA_WIDTH 32

extern void efx_nic_generate_event(struct efx_channel *channel,
				   efx_qword_t *event);

extern void falcon_poll_xmac(struct efx_nic *efx);

#endif /* EFX_NIC_H */
