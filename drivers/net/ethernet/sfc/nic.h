/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2011 Solarflare Communications Inc.
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
 * @wol_filter_id: Wake-on-LAN packet filter id
 */
struct siena_nic_data {
	int wol_filter_id;
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

extern const struct efx_nic_type falcon_a1_nic_type;
extern const struct efx_nic_type falcon_b0_nic_type;
extern const struct efx_nic_type siena_a0_nic_type;

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
extern void efx_nic_generate_fill_event(struct efx_rx_queue *rx_queue);

/* Event data path */
extern int efx_nic_probe_eventq(struct efx_channel *channel);
extern void efx_nic_init_eventq(struct efx_channel *channel);
extern void efx_nic_fini_eventq(struct efx_channel *channel);
extern void efx_nic_remove_eventq(struct efx_channel *channel);
extern int efx_nic_process_eventq(struct efx_channel *channel, int rx_quota);
extern void efx_nic_eventq_read_ack(struct efx_channel *channel);
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

/* Interrupts and test events */
extern int efx_nic_init_interrupt(struct efx_nic *efx);
extern void efx_nic_enable_interrupts(struct efx_nic *efx);
extern void efx_nic_event_test_start(struct efx_channel *channel);
extern void efx_nic_irq_test_start(struct efx_nic *efx);
extern void efx_nic_disable_interrupts(struct efx_nic *efx);
extern void efx_nic_fini_interrupt(struct efx_nic *efx);
extern irqreturn_t efx_nic_fatal_interrupt(struct efx_nic *efx);
extern irqreturn_t falcon_legacy_interrupt_a1(int irq, void *dev_id);

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
extern void siena_finish_flush(struct efx_nic *efx);
extern void falcon_start_nic_stats(struct efx_nic *efx);
extern void falcon_stop_nic_stats(struct efx_nic *efx);
extern int falcon_reset_xaui(struct efx_nic *efx);
extern void
efx_nic_dimension_resources(struct efx_nic *efx, unsigned sram_lim_qw);
extern void efx_nic_init_common(struct efx_nic *efx);
extern void efx_nic_push_rx_indir_table(struct efx_nic *efx);

int efx_nic_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			 unsigned int len, gfp_t gfp_flags);
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

#define EFX_MAX_FLUSH_TIME 5000

extern void efx_generate_event(struct efx_nic *efx, unsigned int evq,
			       efx_qword_t *event);

#endif /* EFX_NIC_H */
