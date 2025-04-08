/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _INTEL_THC_DEV_H_
#define _INTEL_THC_DEV_H_

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "intel-thc-dma.h"

#define THC_REGMAP_COMMON_OFFSET  0x10
#define THC_REGMAP_MMIO_OFFSET    0x1000

/*
 * THC Port type
 * @THC_PORT_TYPE_SPI: This port is used for HIDSPI
 * @THC_PORT_TYPE_I2C: This port is used for HIDI2C
 */
enum thc_port_type {
	THC_PORT_TYPE_SPI = 0,
	THC_PORT_TYPE_I2C = 1,
};

/**
 * THC interrupt flag
 * @THC_NONDMA_INT: THC non-DMA interrupt
 * @THC_RXDMA1_INT: THC RxDMA1 interrupt
 * @THC_RXDMA2_INT: THC RxDMA2 interrupt
 * @THC_SWDMA_INT: THC SWDMA interrupt
 * @THC_TXDMA_INT: THC TXDMA interrupt
 * @THC_PIO_DONE_INT: THC PIO complete interrupt
 * @THC_I2CSUBIP_INT: THC I2C subsystem interrupt
 * @THC_TXN_ERR_INT: THC transfer error interrupt
 * @THC_FATAL_ERR_INT: THC fatal error interrupt
 */
enum thc_int_type {
	THC_NONDMA_INT = 0,
	THC_RXDMA1_INT = 1,
	THC_RXDMA2_INT = 2,
	THC_SWDMA_INT = 3,
	THC_TXDMA_INT = 4,
	THC_PIO_DONE_INT = 5,
	THC_I2CSUBIP_INT = 6,
	THC_TXN_ERR_INT = 7,
	THC_FATAL_ERR_INT = 8,
	THC_UNKNOWN_INT
};

/**
 * struct thc_device - THC private device struct
 * @thc_regmap: MMIO regmap structure for accessing THC registers
 * @mmio_addr: MMIO registers address
 * @thc_bus_lock: mutex locker for THC config
 * @port_type: port type of THC port instance
 * @pio_int_supported: PIO interrupt supported flag
 * @dma_ctx: DMA specific data
 * @write_complete_wait: signal event for DMA write complete
 * @swdma_complete_wait: signal event for SWDMA sequence complete
 * @write_done: bool value that indicates if DMA write is done
 * @swdma_done: bool value that indicates if SWDMA swquence is done
 * @perf_limit: the delay between read operation and write operation
 * @i2c_subip_regs: the copy of THC I2C sub-system registers for resuming restore
 */
struct thc_device {
	struct device *dev;
	struct regmap *thc_regmap;
	void __iomem *mmio_addr;
	struct mutex thc_bus_lock;
	enum thc_port_type port_type;
	bool pio_int_supported;

	struct thc_dma_context *dma_ctx;

	wait_queue_head_t write_complete_wait;
	wait_queue_head_t swdma_complete_wait;
	bool write_done;
	bool swdma_done;

	u32 perf_limit;

	u32 *i2c_subip_regs;
};

struct thc_device *thc_dev_init(struct device *device, void __iomem *mem_addr);
int thc_tic_pio_read(struct thc_device *dev, const u32 address,
		     const u32 size, u32 *actual_size, u32 *buffer);
int thc_tic_pio_write(struct thc_device *dev, const u32 address,
		      const u32 size, const u32 *buffer);
int thc_tic_pio_write_and_read(struct thc_device *dev, const u32 address,
			       const u32 write_size, const u32 *write_buffer,
			       const u32 read_size, u32 *actual_size, u32 *read_buffer);
void thc_interrupt_config(struct thc_device *dev);
void thc_int_trigger_type_select(struct thc_device *dev, bool edge_trigger);
void thc_interrupt_enable(struct thc_device *dev, bool int_enable);
void thc_set_pio_interrupt_support(struct thc_device *dev, bool supported);
int thc_interrupt_quiesce(const struct thc_device *dev, bool int_quiesce);
void thc_ltr_config(struct thc_device *dev, u32 active_ltr_us, u32 lp_ltr_us);
void thc_change_ltr_mode(struct thc_device *dev, u32 ltr_mode);
void thc_ltr_unconfig(struct thc_device *dev);
u32 thc_int_cause_read(struct thc_device *dev);
int thc_interrupt_handler(struct thc_device *dev);
int thc_port_select(struct thc_device *dev, enum thc_port_type port_type);
int thc_spi_read_config(struct thc_device *dev, u32 spi_freq_val,
			u32 io_mode, u32 opcode, u32 spi_rd_mps);
int thc_spi_write_config(struct thc_device *dev, u32 spi_freq_val,
			 u32 io_mode, u32 opcode, u32 spi_wr_mps, u32 perf_limit);
void thc_spi_input_output_address_config(struct thc_device *dev, u32 input_hdr_addr,
					 u32 input_bdy_addr, u32 output_addr);
int thc_i2c_subip_init(struct thc_device *dev, const u32 target_address,
		       const u32 speed, const u32 hcnt, const u32 lcnt);
int thc_i2c_subip_regs_save(struct thc_device *dev);
int thc_i2c_subip_regs_restore(struct thc_device *dev);

#endif /* _INTEL_THC_DEV_H_ */
