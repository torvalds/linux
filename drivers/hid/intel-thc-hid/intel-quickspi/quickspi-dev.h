/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKSPI_DEV_H_
#define _QUICKSPI_DEV_H_

#include <linux/hid-over-spi.h>

#define PCI_DEVICE_ID_INTEL_THC_MTL_DEVICE_ID_SPI_PORT1		0x7E49
#define PCI_DEVICE_ID_INTEL_THC_MTL_DEVICE_ID_SPI_PORT2		0x7E4B
#define PCI_DEVICE_ID_INTEL_THC_LNL_DEVICE_ID_SPI_PORT1		0xA849
#define PCI_DEVICE_ID_INTEL_THC_LNL_DEVICE_ID_SPI_PORT2		0xA84B
#define PCI_DEVICE_ID_INTEL_THC_PTL_H_DEVICE_ID_SPI_PORT1	0xE349
#define PCI_DEVICE_ID_INTEL_THC_PTL_H_DEVICE_ID_SPI_PORT2	0xE34B
#define PCI_DEVICE_ID_INTEL_THC_PTL_U_DEVICE_ID_SPI_PORT1	0xE449
#define PCI_DEVICE_ID_INTEL_THC_PTL_U_DEVICE_ID_SPI_PORT2	0xE44B

/* HIDSPI special ACPI parameters DSM methods */
#define ACPI_QUICKSPI_REVISION_NUM			2
#define ACPI_QUICKSPI_FUNC_NUM_INPUT_REP_HDR_ADDR	1
#define ACPI_QUICKSPI_FUNC_NUM_INPUT_REP_BDY_ADDR	2
#define ACPI_QUICKSPI_FUNC_NUM_OUTPUT_REP_ADDR		3
#define ACPI_QUICKSPI_FUNC_NUM_READ_OPCODE		4
#define ACPI_QUICKSPI_FUNC_NUM_WRITE_OPCODE		5
#define ACPI_QUICKSPI_FUNC_NUM_IO_MODE			6

/* QickSPI device special ACPI parameters DSM methods */
#define ACPI_QUICKSPI_FUNC_NUM_CONNECTION_SPEED		1
#define ACPI_QUICKSPI_FUNC_NUM_LIMIT_PACKET_SIZE	2
#define ACPI_QUICKSPI_FUNC_NUM_PERFORMANCE_LIMIT	3

/* Platform special ACPI parameters DSM methods */
#define ACPI_QUICKSPI_FUNC_NUM_ACTIVE_LTR		1
#define ACPI_QUICKSPI_FUNC_NUM_LP_LTR			2

#define SPI_WRITE_IO_MODE				BIT(13)
#define SPI_IO_MODE_OPCODE				GENMASK(15, 14)
#define PERFORMANCE_LIMITATION				GENMASK(15, 0)

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
struct hid_device;
struct acpi_device;

/**
 * struct quickspi_device -  THC QuickSpi device struct
 * @dev: point to kernel device
 * @pdev: point to PCI device
 * @thc_hw: point to THC device
 * @hid_dev: point to hid device
 * @acpi_dev: point to ACPI device
 * @driver_data: point to quickspi specific driver data
 * @state: THC SPI device state
 * @mem_addr: MMIO memory address
 * @dev_desc: device descriptor for HIDSPI protocol
 * @input_report_hdr_addr: device input report header address
 * @input_report_bdy_addr: device input report body address
 * @output_report_bdy_addr: device output report address
 * @spi_freq_val: device supported max SPI frequnecy, in Hz
 * @spi_read_io_mode: device supported SPI read io mode
 * @spi_write_io_mode: device supported SPI write io mode
 * @spi_read_opcode: device read opcode
 * @spi_write_opcode: device write opcode
 * @limit_packet_size: 1 - limit read/write packet to 64Bytes
 *                     0 - device no packet size limiation for read/write
 * @performance_limit: delay time, in ms.
 *                     if device has performance limitation, must give a delay
 *                     before write operation after a read operation.
 * @active_ltr_val: THC active LTR value
 * @low_power_ltr_val: THC low power LTR value
 * @report_descriptor: store a copy of device report descriptor
 */
struct quickspi_device {
	struct device *dev;
	struct pci_dev *pdev;
	struct thc_device *thc_hw;
	struct hid_device *hid_dev;
	struct acpi_device *acpi_dev;
	struct quickspi_driver_data *driver_data;
	enum quickspi_dev_state state;

	void __iomem *mem_addr;

	struct hidspi_dev_descriptor dev_desc;
	u32 input_report_hdr_addr;
	u32 input_report_bdy_addr;
	u32 output_report_addr;
	u32 spi_freq_val;
	u32 spi_read_io_mode;
	u32 spi_write_io_mode;
	u32 spi_read_opcode;
	u32 spi_write_opcode;
	u32 limit_packet_size;
	u32 spi_packet_size;
	u32 performance_limit;

	u32 active_ltr_val;
	u32 low_power_ltr_val;

	u8 *report_descriptor;
};

#endif /* _QUICKSPI_DEV_H_ */
