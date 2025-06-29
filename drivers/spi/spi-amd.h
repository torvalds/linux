/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD SPI controller driver common stuff
 *
 * Copyright (c) 2025, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Krishnamoorthi M <krishnamoorthi.m@amd.com>
 */

#ifndef SPI_AMD_H
#define SPI_AMD_H

/**
 * enum amd_spi_versions - SPI controller versions
 * @AMD_SPI_V1:         AMDI0061 hardware version
 * @AMD_SPI_V2:         AMDI0062 hardware version
 * @AMD_HID2_SPI:       AMDI0063 hardware version
 */
enum amd_spi_versions {
	AMD_SPI_V1 = 1,
	AMD_SPI_V2,
	AMD_HID2_SPI,
};

/**
 * struct amd_spi - SPI driver instance
 * @io_remap_addr:      Start address of the SPI controller registers
 * @phy_dma_buf:        Physical address of DMA buffer
 * @dma_virt_addr:      Virtual address of DMA buffer
 * @version:            SPI controller hardware version
 * @speed_hz:           Device frequency
 */
struct amd_spi {
	void __iomem *io_remap_addr;
	dma_addr_t phy_dma_buf;
	void *dma_virt_addr;
	enum amd_spi_versions version;
	unsigned int speed_hz;
};

int amd_spi_probe_common(struct device *dev, struct spi_controller *host);

#endif /* SPI_AMD_H */
