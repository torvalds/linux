/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Freescale GPMI NAND Flash Driver
 *
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc.
 */
#ifndef __DRIVERS_MTD_NAND_GPMI_NAND_H
#define __DRIVERS_MTD_NAND_GPMI_NAND_H

#include <linux/mtd/rawnand.h>
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

enum gpmi_type {
	IS_MX23,
	IS_MX28,
	IS_MX6Q,
	IS_MX6SX,
	IS_MX7D,
};

struct gpmi_devdata {
	enum gpmi_type type;
	int bch_max_ecc_strength;
	int max_chain_delay; /* See the async EDO mode */
	const char * const *clks;
	const int clks_count;
};

/**
 * struct gpmi_nfc_hardware_timing - GPMI hardware timing parameters.
 * @must_apply_timings:        Whether controller timings have already been
 *                             applied or not (useful only while there is
 *                             support for only one chip select)
 * @clk_rate:                  The clock rate that must be used to derive the
 *                             following parameters
 * @timing0:                   HW_GPMI_TIMING0 register
 * @timing1:                   HW_GPMI_TIMING1 register
 * @ctrl1n:                    HW_GPMI_CTRL1n register
 */
struct gpmi_nfc_hardware_timing {
	bool must_apply_timings;
	unsigned long int clk_rate;
	u32 timing0;
	u32 timing1;
	u32 ctrl1n;
};

struct gpmi_nand_data {
	/* Devdata */
	const struct gpmi_devdata *devdata;

	/* System Interface */
	struct device		*dev;
	struct platform_device	*pdev;

	/* Resources */
	struct resources	resources;

	/* Flash Hardware */
	struct gpmi_nfc_hardware_timing hw;

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
	struct completion	dma_done;
};

/* BCH : Status Block Completion Codes */
#define STATUS_GOOD		0x00
#define STATUS_ERASED		0xff
#define STATUS_UNCORRECTABLE	0xfe

/* Use the devdata to distinguish different Archs. */
#define GPMI_IS_MX23(x)		((x)->devdata->type == IS_MX23)
#define GPMI_IS_MX28(x)		((x)->devdata->type == IS_MX28)
#define GPMI_IS_MX6Q(x)		((x)->devdata->type == IS_MX6Q)
#define GPMI_IS_MX6SX(x)	((x)->devdata->type == IS_MX6SX)
#define GPMI_IS_MX7D(x)		((x)->devdata->type == IS_MX7D)

#define GPMI_IS_MX6(x)		(GPMI_IS_MX6Q(x) || GPMI_IS_MX6SX(x) || \
				 GPMI_IS_MX7D(x))
#define GPMI_IS_MXS(x)		(GPMI_IS_MX23(x) || GPMI_IS_MX28(x))
#endif
