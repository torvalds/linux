/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _INTEL_THC_DEV_H_
#define _INTEL_THC_DEV_H_

#include <linux/cdev.h>
#include <linux/mutex.h>

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
 * struct thc_device - THC private device struct
 * @thc_regmap: MMIO regmap structure for accessing THC registers
 * @mmio_addr: MMIO registers address
 * @thc_bus_lock: mutex locker for THC config
 * @port_type: port type of THC port instance
 * @pio_int_supported: PIO interrupt supported flag
 */
struct thc_device {
	struct device *dev;
	struct regmap *thc_regmap;
	void __iomem *mmio_addr;
	struct mutex thc_bus_lock;
	enum thc_port_type port_type;
	bool pio_int_supported;
};

struct thc_device *thc_dev_init(struct device *device, void __iomem *mem_addr);
int thc_tic_pio_read(struct thc_device *dev, const u32 address,
		     const u32 size, u32 *actual_size, u32 *buffer);
int thc_tic_pio_write(struct thc_device *dev, const u32 address,
		      const u32 size, const u32 *buffer);
int thc_tic_pio_write_and_read(struct thc_device *dev, const u32 address,
			       const u32 write_size, const u32 *write_buffer,
			       const u32 read_size, u32 *actual_size, u32 *read_buffer);

#endif /* _INTEL_THC_DEV_H_ */
