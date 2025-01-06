/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKI2C_DEV_H_
#define _QUICKI2C_DEV_H_

#define THC_LNL_DEVICE_ID_I2C_PORT1	0xA848
#define THC_LNL_DEVICE_ID_I2C_PORT2	0xA84A
#define THC_PTL_H_DEVICE_ID_I2C_PORT1	0xE348
#define THC_PTL_H_DEVICE_ID_I2C_PORT2	0xE34A
#define THC_PTL_U_DEVICE_ID_I2C_PORT1	0xE448
#define THC_PTL_U_DEVICE_ID_I2C_PORT2	0xE44A

/* Packet size value, the unit is 16 bytes */
#define MAX_PACKET_SIZE_VALUE_LNL			256

enum quicki2c_dev_state {
	QUICKI2C_NONE,
	QUICKI2C_RESETING,
	QUICKI2C_RESETED,
	QUICKI2C_INITED,
	QUICKI2C_ENABLED,
	QUICKI2C_DISABLED,
};

struct device;
struct pci_dev;
struct thc_device;

/**
 * struct quicki2c_device -  THC QuickI2C device struct
 * @dev: point to kernel device
 * @pdev: point to PCI device
 * @thc_hw: point to THC device
 * @driver_data: point to quicki2c specific driver data
 * @state: THC I2C device state
 * @mem_addr: MMIO memory address
 */
struct quicki2c_device {
	struct device *dev;
	struct pci_dev *pdev;
	struct thc_device *thc_hw;
	enum quicki2c_dev_state state;

	void __iomem *mem_addr;
};

#endif /* _QUICKI2C_DEV_H_ */
