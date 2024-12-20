/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_CORE_H__
#define __MGB4_CORE_H__

#include <linux/spi/flash.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/dmaengine.h>
#include "mgb4_regs.h"

#define MGB4_HW_FREQ 125000000

#define MGB4_VIN_DEVICES  2
#define MGB4_VOUT_DEVICES 2

#define MGB4_MGB4_BAR_ID  0
#define MGB4_XDMA_BAR_ID  1

#define MGB4_IS_GMSL(mgbdev) \
	((mgbdev)->module_version >> 4 == 2)
#define MGB4_IS_FPDL3(mgbdev) \
	((mgbdev)->module_version >> 4 == 1)

struct mgb4_dma_channel {
	struct dma_chan *chan;
	struct completion req_compl;
};

struct mgb4_dev {
	struct pci_dev *pdev;
	struct platform_device *xdev;
	struct mgb4_vin_dev *vin[MGB4_VIN_DEVICES];
	struct mgb4_vout_dev *vout[MGB4_VOUT_DEVICES];

	struct mgb4_dma_channel c2h_chan[MGB4_VIN_DEVICES];
	struct mgb4_dma_channel h2c_chan[MGB4_VOUT_DEVICES];
	struct dma_slave_map slave_map[MGB4_VIN_DEVICES + MGB4_VOUT_DEVICES];

	struct mgb4_regs video;
	struct mgb4_regs cmt;

	struct clk_hw *i2c_clk;
	struct clk_lookup *i2c_cl;
	struct platform_device *i2c_pdev;
	struct i2c_adapter *i2c_adap;
	struct mutex i2c_lock; /* I2C bus access lock */

	struct platform_device *spi_pdev;
	struct flash_platform_data flash_data;
	struct mtd_partition partitions[2];
	char flash_name[16];
	char fw_part_name[16];
	char data_part_name[16];
	char channel_names[MGB4_VIN_DEVICES + MGB4_VOUT_DEVICES][16];

	struct iio_dev *indio_dev;
#if IS_REACHABLE(CONFIG_HWMON)
	struct device *hwmon_dev;
#endif

	unsigned long io_reconfig;

	u8 module_version;
	u32 serial_number;

	struct dentry *debugfs;
};

#endif
