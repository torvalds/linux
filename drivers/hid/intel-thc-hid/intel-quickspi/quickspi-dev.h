/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKSPI_DEV_H_
#define _QUICKSPI_DEV_H_

#define PCI_DEVICE_ID_INTEL_THC_MTL_DEVICE_ID_SPI_PORT1		0x7E49
#define PCI_DEVICE_ID_INTEL_THC_MTL_DEVICE_ID_SPI_PORT2		0x7E4B
#define PCI_DEVICE_ID_INTEL_THC_LNL_DEVICE_ID_SPI_PORT1		0xA849
#define PCI_DEVICE_ID_INTEL_THC_LNL_DEVICE_ID_SPI_PORT2		0xA84B
#define PCI_DEVICE_ID_INTEL_THC_PTL_H_DEVICE_ID_SPI_PORT1	0xE349
#define PCI_DEVICE_ID_INTEL_THC_PTL_H_DEVICE_ID_SPI_PORT2	0xE34B
#define PCI_DEVICE_ID_INTEL_THC_PTL_U_DEVICE_ID_SPI_PORT1	0xE449
#define PCI_DEVICE_ID_INTEL_THC_PTL_U_DEVICE_ID_SPI_PORT2	0xE44B

/* Packet size value, the unit is 16 bytes */
#define DEFAULT_MIN_PACKET_SIZE_VALUE			4
#define MAX_PACKET_SIZE_VALUE_MTL			128
#define MAX_PACKET_SIZE_VALUE_LNL			256

enum quickspi_dev_state {
	QUICKSPI_NONE,
	QUICKSPI_RESETING,
	QUICKSPI_RESETED,
	QUICKSPI_INITED,
	QUICKSPI_ENABLED,
	QUICKSPI_DISABLED,
};

/**
 * struct quickspi_driver_data - Driver specific data for quickspi device
 * @max_packet_size_value: identify max packet size, unit is 16 bytes
 */
struct quickspi_driver_data {
	u32 max_packet_size_value;
};

struct device;
struct pci_dev;
struct thc_device;

/**
 * struct quickspi_device -  THC QuickSpi device struct
 * @dev: point to kernel device
 * @pdev: point to PCI device
 * @thc_hw: point to THC device
 * @driver_data: point to quickspi specific driver data
 * @state: THC SPI device state
 * @mem_addr: MMIO memory address
 */
struct quickspi_device {
	struct device *dev;
	struct pci_dev *pdev;
	struct thc_device *thc_hw;
	struct quickspi_driver_data *driver_data;
	enum quickspi_dev_state state;

	void __iomem *mem_addr;
};

#endif /* _QUICKSPI_DEV_H_ */
