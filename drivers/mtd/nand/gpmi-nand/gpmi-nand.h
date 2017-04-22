/*
 * Freescale GPMI NAND Flash Driver
 *
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __DRIVERS_MTD_NAND_GPMI_NAND_H
#define __DRIVERS_MTD_NAND_GPMI_NAND_H

#include <linux/mtd/nand.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#define GPMI_CLK_MAX 5 /* MX6Q needs five clocks */
struct resources {
	void __iomem  *gpmi_regs;
	void __iomem  *bch_regs;
	unsigned int  dma_low_channel;
	unsigned int  dma_high_channel;
	struct clk    *clock[GPMI_CLK_MAX];
};

/**
 * struct bch_geometry - BCH geometry description.
 * @gf_len:                   The length of Galois Field. (e.g., 13 or 14)
 * @ecc_strength:             A number that describes the strength of the ECC
 *                            algorithm.
 * @page_size:                The size, in bytes, of a physical page, including
 *                            both data and OOB.
 * @metadata_size:            The size, in bytes, of the metadata.
 * @ecc_chunk_size:           The size, in bytes, of a single ECC chunk. Note
 *                            the first chunk in the page includes both data and
 *                            metadata, so it's a bit larger than this value.
 * @ecc_chunk_count:          The number of ECC chunks in the page,
 * @payload_size:             The size, in bytes, of the payload buffer.
 * @auxiliary_size:           The size, in bytes, of the auxiliary buffer.
 * @auxiliary_status_offset:  The offset into the auxiliary buffer at which
 *                            the ECC status appears.
 * @block_mark_byte_offset:   The byte offset in the ECC-based page view at
 *                            which the underlying physical block mark appears.
 * @block_mark_bit_offset:    The bit offset into the ECC-based page view at
 *                            which the underlying physical block mark appears.
 */
struct bch_geometry {
	unsigned int  gf_len;
	unsigned int  ecc_strength;
	unsigned int  page_size;
	unsigned int  metadata_size;
	unsigned int  ecc_chunk_size;
	unsigned int  ecc_chunk_count;
	unsigned int  payload_size;
	unsigned int  auxiliary_size;
	unsigned int  auxiliary_status_offset;
	unsigned int  block_mark_byte_offset;
	unsigned int  block_mark_bit_offset;
};

/**
 * struct boot_rom_geometry - Boot ROM geometry description.
 * @stride_size_in_pages:        The size of a boot block stride, in pages.
 * @search_area_stride_exponent: The logarithm to base 2 of the size of a
 *                               search area in boot block strides.
 */
struct boot_rom_geometry {
	unsigned int  stride_size_in_pages;
	unsigned int  search_area_stride_exponent;
};

/* DMA operations types */
enum dma_ops_type {
	DMA_FOR_COMMAND = 1,
	DMA_FOR_READ_DATA,
	DMA_FOR_WRITE_DATA,
	DMA_FOR_READ_ECC_PAGE,
	DMA_FOR_WRITE_ECC_PAGE
};

/**
 * struct nand_timing - Fundamental timing attributes for NAND.
 * @data_setup_in_ns:         The data setup time, in nanoseconds. Usually the
 *                            maximum of tDS and tWP. A negative value
 *                            indicates this characteristic isn't known.
 * @data_hold_in_ns:          The data hold time, in nanoseconds. Usually the
 *                            maximum of tDH, tWH and tREH. A negative value
 *                            indicates this characteristic isn't known.
 * @address_setup_in_ns:      The address setup time, in nanoseconds. Usually
 *                            the maximum of tCLS, tCS and tALS. A negative
 *                            value indicates this characteristic isn't known.
 * @gpmi_sample_delay_in_ns:  A GPMI-specific timing parameter. A negative value
 *                            indicates this characteristic isn't known.
 * @tREA_in_ns:               tREA, in nanoseconds, from the data sheet. A
 *                            negative value indicates this characteristic isn't
 *                            known.
 * @tRLOH_in_ns:              tRLOH, in nanoseconds, from the data sheet. A
 *                            negative value indicates this characteristic isn't
 *                            known.
 * @tRHOH_in_ns:              tRHOH, in nanoseconds, from the data sheet. A
 *                            negative value indicates this characteristic isn't
 *                            known.
 */
struct nand_timing {
	int8_t  data_setup_in_ns;
	int8_t  data_hold_in_ns;
	int8_t  address_setup_in_ns;
	int8_t  gpmi_sample_delay_in_ns;
	int8_t  tREA_in_ns;
	int8_t  tRLOH_in_ns;
	int8_t  tRHOH_in_ns;
};

enum gpmi_type {
	IS_MX23,
	IS_MX28,
	IS_MX6Q,
	IS_MX6SX
};

struct gpmi_devdata {
	enum gpmi_type type;
	int bch_max_ecc_strength;
	int max_chain_delay; /* See the async EDO mode */
	const char * const *clks;
	const int clks_count;
};

struct gpmi_nand_data {
	/* flags */
#define GPMI_ASYNC_EDO_ENABLED	(1 << 0)
#define GPMI_TIMING_INIT_OK	(1 << 1)
	int			flags;
	const struct gpmi_devdata *devdata;

	/* System Interface */
	struct device		*dev;
	struct platform_device	*pdev;

	/* Resources */
	struct resources	resources;

	/* Flash Hardware */
	struct nand_timing	timing;
	int			timing_mode;

	/* BCH */
	struct bch_geometry	bch_geometry;
	struct completion	bch_done;

	/* NAND Boot issue */
	bool			swap_block_mark;
	struct boot_rom_geometry rom_geometry;

	/* MTD / NAND */
	struct nand_chip	nand;

	/* General-use Variables */
	int			current_chip;
	unsigned int		command_length;

	/* passed from upper layer */
	uint8_t			*upper_buf;
	int			upper_len;

	/* for DMA operations */
	bool			direct_dma_map_ok;

	struct scatterlist	cmd_sgl;
	char			*cmd_buffer;

	struct scatterlist	data_sgl;
	char			*data_buffer_dma;

	void			*page_buffer_virt;
	dma_addr_t		page_buffer_phys;
	unsigned int		page_buffer_size;

	void			*payload_virt;
	dma_addr_t		payload_phys;

	void			*auxiliary_virt;
	dma_addr_t		auxiliary_phys;

	void			*raw_buffer;

	/* DMA channels */
#define DMA_CHANS		8
	struct dma_chan		*dma_chans[DMA_CHANS];
	enum dma_ops_type	last_dma_type;
	enum dma_ops_type	dma_type;
	struct completion	dma_done;

	/* private */
	void			*private;
};

/**
 * struct gpmi_nfc_hardware_timing - GPMI hardware timing parameters.
 * @data_setup_in_cycles:      The data setup time, in cycles.
 * @data_hold_in_cycles:       The data hold time, in cycles.
 * @address_setup_in_cycles:   The address setup time, in cycles.
 * @device_busy_timeout:       The timeout waiting for NAND Ready/Busy,
 *                             this value is the number of cycles multiplied
 *                             by 4096.
 * @use_half_periods:          Indicates the clock is running slowly, so the
 *                             NFC DLL should use half-periods.
 * @sample_delay_factor:       The sample delay factor.
 * @wrn_dly_sel:               The delay on the GPMI write strobe.
 */
struct gpmi_nfc_hardware_timing {
	/* for HW_GPMI_TIMING0 */
	uint8_t  data_setup_in_cycles;
	uint8_t  data_hold_in_cycles;
	uint8_t  address_setup_in_cycles;

	/* for HW_GPMI_TIMING1 */
	uint16_t device_busy_timeout;
#define GPMI_DEFAULT_BUSY_TIMEOUT	0x500 /* default busy timeout value.*/

	/* for HW_GPMI_CTRL1 */
	bool     use_half_periods;
	uint8_t  sample_delay_factor;
	uint8_t  wrn_dly_sel;
};

/**
 * struct timing_threshod - Timing threshold
 * @max_data_setup_cycles:       The maximum number of data setup cycles that
 *                               can be expressed in the hardware.
 * @internal_data_setup_in_ns:   The time, in ns, that the NFC hardware requires
 *                               for data read internal setup. In the Reference
 *                               Manual, see the chapter "High-Speed NAND
 *                               Timing" for more details.
 * @max_sample_delay_factor:     The maximum sample delay factor that can be
 *                               expressed in the hardware.
 * @max_dll_clock_period_in_ns:  The maximum period of the GPMI clock that the
 *                               sample delay DLL hardware can possibly work
 *                               with (the DLL is unusable with longer periods).
 *                               If the full-cycle period is greater than HALF
 *                               this value, the DLL must be configured to use
 *                               half-periods.
 * @max_dll_delay_in_ns:         The maximum amount of delay, in ns, that the
 *                               DLL can implement.
 * @clock_frequency_in_hz:       The clock frequency, in Hz, during the current
 *                               I/O transaction. If no I/O transaction is in
 *                               progress, this is the clock frequency during
 *                               the most recent I/O transaction.
 */
struct timing_threshod {
	const unsigned int      max_chip_count;
	const unsigned int      max_data_setup_cycles;
	const unsigned int      internal_data_setup_in_ns;
	const unsigned int      max_sample_delay_factor;
	const unsigned int      max_dll_clock_period_in_ns;
	const unsigned int      max_dll_delay_in_ns;
	unsigned long           clock_frequency_in_hz;

};

/* Common Services */
extern int common_nfc_set_geometry(struct gpmi_nand_data *);
extern struct dma_chan *get_dma_chan(struct gpmi_nand_data *);
extern void prepare_data_dma(struct gpmi_nand_data *,
				enum dma_data_direction dr);
extern int start_dma_without_bch_irq(struct gpmi_nand_data *,
				struct dma_async_tx_descriptor *);
extern int start_dma_with_bch_irq(struct gpmi_nand_data *,
				struct dma_async_tx_descriptor *);

/* GPMI-NAND helper function library */
extern int gpmi_init(struct gpmi_nand_data *);
extern int gpmi_extra_init(struct gpmi_nand_data *);
extern void gpmi_clear_bch(struct gpmi_nand_data *);
extern void gpmi_dump_info(struct gpmi_nand_data *);
extern int bch_set_geometry(struct gpmi_nand_data *);
extern int gpmi_is_ready(struct gpmi_nand_data *, unsigned chip);
extern int gpmi_send_command(struct gpmi_nand_data *);
extern void gpmi_begin(struct gpmi_nand_data *);
extern void gpmi_end(struct gpmi_nand_data *);
extern int gpmi_read_data(struct gpmi_nand_data *);
extern int gpmi_send_data(struct gpmi_nand_data *);
extern int gpmi_send_page(struct gpmi_nand_data *,
			dma_addr_t payload, dma_addr_t auxiliary);
extern int gpmi_read_page(struct gpmi_nand_data *,
			dma_addr_t payload, dma_addr_t auxiliary);

void gpmi_copy_bits(u8 *dst, size_t dst_bit_off,
		    const u8 *src, size_t src_bit_off,
		    size_t nbits);

/* BCH : Status Block Completion Codes */
#define STATUS_GOOD		0x00
#define STATUS_ERASED		0xff
#define STATUS_UNCORRECTABLE	0xfe

/* Use the devdata to distinguish different Archs. */
#define GPMI_IS_MX23(x)		((x)->devdata->type == IS_MX23)
#define GPMI_IS_MX28(x)		((x)->devdata->type == IS_MX28)
#define GPMI_IS_MX6Q(x)		((x)->devdata->type == IS_MX6Q)
#define GPMI_IS_MX6SX(x)	((x)->devdata->type == IS_MX6SX)

#define GPMI_IS_MX6(x)		(GPMI_IS_MX6Q(x) || GPMI_IS_MX6SX(x))
#endif
