/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _INTEL_THC_DMA_H_
#define _INTEL_THC_DMA_H_

#include <linux/bits.h>
#include <linux/dma-mapping.h>
#include <linux/sizes.h>
#include <linux/time64.h>
#include <linux/types.h>

#define THC_POINTER_MASK		GENMASK(6, 0)
#define THC_POINTER_WRAPAROUND		0x80
#define THC_WRAPAROUND_VALUE_ODD	0x10
#define THC_WRAPAROUND_VALUE_EVEN	0x90
#define THC_MIN_BYTES_PER_SG_LIST_ENTRY SZ_4K

#define THC_DEFAULT_RXDMA_POLLING_US_INTERVAL 100
#define THC_DEFAULT_RXDMA_POLLING_US_TIMEOUT  (10 * USEC_PER_MSEC)

/*
 * THC needs 1KB aligned address, dest_addr is 54 bits, not 64,
 * so don't need to send the lower 10-bits of address.
 */
#define THC_ADDRESS_SHIFT 10

/**
 * THC DMA channels:
 * @THC_RXDMA1: legacy channel, reserved for raw data reading
 * @THC_RXDMA2: DMA to read HID data from touch device
 * @THC_TXDMA: DMA to write to touch device
 * @THC_SWDMA: SW triggered DMA to write and read from touch device
 */
enum thc_dma_channel {
	THC_RXDMA1 = 0,
	THC_RXDMA2 = 1,
	THC_TXDMA = 2,
	THC_SWDMA = 3,
	MAX_THC_DMA_CHANNEL
};

/**
 * THC DMA Physical Memory Descriptor (PRD)
 * @dest_addr:		bit[53:0], destination address in system memory
 * @int_on_completion:	bit[63], if set, thc will trigger interrupt to driver
 * @len:		bit[87:64], length of this entry
 * @end_of_prd:		bit[88], if set, this entry is last one of current PRD table
 * @hw_status:		bit[90:89], hw status bits
 */
struct thc_prd_entry {
	u64  dest_addr : 54;
	u64  reserved1 : 9;
	u64  int_on_completion : 1;
	u64  len : 24;
	u64  end_of_prd : 1;
	u64  hw_status : 2;
	u64  reserved2 : 37;
};

/*
 * Max OS memory fragmentation will be at a 4KB boundary, thus to address 1MB
 * of virtually contiguous memory 256 PRD entries are required for a single
 * PRD Table. SW writes the number of PRD Entries for each PRD table in the
 * THC_M_PRT_RPRD_CNTRL.PTEC register field. The PRD entry's length must be
 * multiple of 4KB except for the last entry in a PRD table.
 * This is the max possible number of etries supported by HW, in practise we
 * there will be less entries in each prd table(the actual number will be
 * given by scatter-gather list allocation).
 */
#define PRD_ENTRIES_NUM 16

/*
 * Number of PRD tables equals to number of data buffers.
 * The max number of PRD tables supported by the HW is 128,
 * but we allocate only 16.
 */
#define PRD_TABLES_NUM  16

/* THC DMA Physical Memory Descriptor Table */
struct thc_prd_table {
	struct thc_prd_entry entries[PRD_ENTRIES_NUM];
};

#define PRD_TABLE_SIZE	sizeof(struct thc_prd_table)

/**
 * struct thc_dma_configuration - THC DMA configure
 * @dma_channel: DMA channel for current DMA configuration
 * @prd_tbls_dma_handle: DMA buffer handle
 * @dir: direction of DMA for this config
 * @prd_tbls: PRD tables for current DMA
 * @sgls: array of pointers to scatter-gather lists
 * @sgls_nent: actual number of entries per sg list
 * @prd_tbl_num: actual number of PRD tables
 * @max_packet_size: size of the buffer needed for 1 DMA message (1 PRD table)
 * @prd_base_addr_high: High 32bits memory address where stores PRD table
 * @prd_base_addr_low: low 32bits memory address where stores PRD table
 * @prd_cntrl: PRD control register value
 * @dma_cntrl: DMA control register value
 */
struct thc_dma_configuration {
	enum thc_dma_channel dma_channel;
	dma_addr_t prd_tbls_dma_handle;
	enum dma_data_direction dir;
	bool is_enabled;

	struct thc_prd_table *prd_tbls;
	struct scatterlist *sgls[PRD_TABLES_NUM];
	u8 sgls_nent[PRD_TABLES_NUM];
	u8 prd_tbl_num;

	size_t max_packet_size;
	u32 prd_base_addr_high;
	u32 prd_base_addr_low;
	u32 prd_cntrl;
	u32 dma_cntrl;
};

/*
 * THC DMA context
 * Store all THC Channel configures
 */
struct thc_dma_context {
	struct thc_dma_configuration dma_config[MAX_THC_DMA_CHANNEL];
	u8 use_write_interrupts;
};

struct thc_device;

int  thc_dma_set_max_packet_sizes(struct thc_device *dev,
				  size_t mps_read1, size_t mps_read2,
				  size_t mps_write, size_t mps_swdma);
int  thc_dma_allocate(struct thc_device *dev);
int  thc_dma_configure(struct thc_device *dev);
void thc_dma_unconfigure(struct thc_device *dev);
void thc_dma_release(struct thc_device *dev);
int  thc_rxdma_read(struct thc_device *dev, enum thc_dma_channel dma_channel,
		    void *read_buff, size_t *read_len, int *read_finished);
int  thc_swdma_read(struct thc_device *dev, void *write_buff, size_t write_len,
		    u32 *prd_tbl_len, void *read_buff, size_t *read_len);
int  thc_dma_write(struct thc_device *dev, void *buffer, size_t buf_len);

struct thc_dma_context *thc_dma_init(struct thc_device *dev);

#endif /* _INTEL_THC_DMA_H_ */
