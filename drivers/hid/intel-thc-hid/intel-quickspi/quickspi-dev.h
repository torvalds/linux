/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKSPI_DEV_H_
#define _QUICKSPI_DEV_H_

#include <linux/bits.h>
#include <linux/hid-over-spi.h>
#include <linux/sizes.h>
#include <linux/wait.h>

#include "quickspi-protocol.h"

#define PCI_DEVICE_ID_INTEL_THC_MTL_DEVICE_ID_SPI_PORT1		0x7E49
#define PCI_DEVICE_ID_INTEL_THC_MTL_DEVICE_ID_SPI_PORT2		0x7E4B
#define PCI_DEVICE_ID_INTEL_THC_LNL_DEVICE_ID_SPI_PORT1		0xA849
#define PCI_DEVICE_ID_INTEL_THC_LNL_DEVICE_ID_SPI_PORT2		0xA84B
#define PCI_DEVICE_ID_INTEL_THC_PTL_H_DEVICE_ID_SPI_PORT1	0xE349
#define PCI_DEVICE_ID_INTEL_THC_PTL_H_DEVICE_ID_SPI_PORT2	0xE34B
#define PCI_DEVICE_ID_INTEL_THC_PTL_U_DEVICE_ID_SPI_PORT1	0xE449
#define PCI_DEVICE_ID_INTEL_THC_PTL_U_DEVICE_ID_SPI_PORT2	0xE44B
#define PCI_DEVICE_ID_INTEL_THC_WCL_DEVICE_ID_SPI_PORT1 	0x4D49
#define PCI_DEVICE_ID_INTEL_THC_WCL_DEVICE_ID_SPI_PORT2 	0x4D4B
#define PCI_DEVICE_ID_INTEL_THC_ARL_DEVICE_ID_SPI_PORT1 	0x7749
#define PCI_DEVICE_ID_INTEL_THC_ARL_DEVICE_ID_SPI_PORT2 	0x774B

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

/*
 * THC uses runtime auto suspend to dynamically switch between THC active LTR
 * and low power LTR to save CPU power.
 * Default value is 5000ms, that means if no touch event in this time, THC will
 * change to low power LTR mode.
 */
#define DEFAULT_AUTO_SUSPEND_DELAY_MS			5000

enum quickspi_dev_state {
	QUICKSPI_NONE,
	QUICKSPI_INITIATED,
	QUICKSPI_RESETING,
	QUICKSPI_RESET,
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
 * @input_buf: store a copy of latest input report data
 * @report_buf: store a copy of latest input/output report packet from set/get feature
 * @report_len: the length of input/output report packet
 * @reset_ack_wq: workqueue for waiting reset response from device
 * @reset_ack: indicate reset response received or not
 * @nondma_int_received_wq: workqueue for waiting THC non-DMA interrupt
 * @nondma_int_received: indicate THC non-DMA interrupt received or not
 * @report_desc_got_wq: workqueue for waiting device report descriptor
 * @report_desc_got: indicate device report descritor received or not
 * @set_power_on_wq: workqueue for waiting set power on response from device
 * @set_power_on: indicate set power on response received or not
 * @get_feature_cmpl_wq: workqueue for waiting get feature response from device
 * @get_feature_cmpl: indicate get feature received or not
 * @set_feature_cmpl_wq: workqueue for waiting set feature to device
 * @set_feature_cmpl: indicate set feature send complete or not
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
	u8 *input_buf;
	u8 *report_buf;
	u32 report_len;

	wait_queue_head_t reset_ack_wq;
	bool reset_ack;

	wait_queue_head_t nondma_int_received_wq;
	bool nondma_int_received;

	wait_queue_head_t report_desc_got_wq;
	bool report_desc_got;

	wait_queue_head_t get_report_cmpl_wq;
	bool get_report_cmpl;

	wait_queue_head_t set_report_cmpl_wq;
	bool set_report_cmpl;
};

#endif /* _QUICKSPI_DEV_H_ */
