/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#ifndef _QUICKI2C_DEV_H_
#define _QUICKI2C_DEV_H_

#include <linux/hid-over-i2c.h>
#include <linux/workqueue.h>

#define THC_LNL_DEVICE_ID_I2C_PORT1	0xA848
#define THC_LNL_DEVICE_ID_I2C_PORT2	0xA84A
#define THC_PTL_H_DEVICE_ID_I2C_PORT1	0xE348
#define THC_PTL_H_DEVICE_ID_I2C_PORT2	0xE34A
#define THC_PTL_U_DEVICE_ID_I2C_PORT1	0xE448
#define THC_PTL_U_DEVICE_ID_I2C_PORT2	0xE44A

/* Packet size value, the unit is 16 bytes */
#define MAX_PACKET_SIZE_VALUE_LNL			256

/* HIDI2C special ACPI parameters DSD name */
#define QUICKI2C_ACPI_METHOD_NAME_ICRS		"ICRS"
#define QUICKI2C_ACPI_METHOD_NAME_ISUB		"ISUB"

/* HIDI2C special ACPI parameters DSM methods */
#define QUICKI2C_ACPI_REVISION_NUM		1
#define QUICKI2C_ACPI_FUNC_NUM_HID_DESC_ADDR	1
#define QUICKI2C_ACPI_FUNC_NUM_ACTIVE_LTR_VAL	1
#define QUICKI2C_ACPI_FUNC_NUM_LP_LTR_VAL	2

#define QUICKI2C_SUBIP_STANDARD_MODE_MAX_SPEED		100000
#define QUICKI2C_SUBIP_FAST_MODE_MAX_SPEED		400000
#define QUICKI2C_SUBIP_FASTPLUS_MODE_MAX_SPEED		1000000
#define QUICKI2C_SUBIP_HIGH_SPEED_MODE_MAX_SPEED	3400000

#define QUICKI2C_DEFAULT_ACTIVE_LTR_VALUE	5
#define QUICKI2C_DEFAULT_LP_LTR_VALUE		500
#define QUICKI2C_RPM_TIMEOUT_MS			500

/*
 * THC uses runtime auto suspend to dynamically switch between THC active LTR
 * and low power LTR to save CPU power.
 * Default value is 5000ms, that means if no touch event in this time, THC will
 * change to low power LTR mode.
 */
#define DEFAULT_AUTO_SUSPEND_DELAY_MS			5000

enum quicki2c_dev_state {
	QUICKI2C_NONE,
	QUICKI2C_RESETING,
	QUICKI2C_RESETED,
	QUICKI2C_INITED,
	QUICKI2C_ENABLED,
	QUICKI2C_DISABLED,
};

enum {
	HIDI2C_ADDRESSING_MODE_7BIT,
	HIDI2C_ADDRESSING_MODE_10BIT,
};

/**
 * struct quicki2c_subip_acpi_parameter - QuickI2C ACPI DSD parameters
 * @device_address: I2C device slave address
 * @connection_speed: I2C device expected connection speed
 * @addressing_mode: I2C device slave address mode, 7bit or 10bit
 *
 * Those properties get from QUICKI2C_ACPI_METHOD_NAME_ICRS method, used for
 * Bus parameter.
 */
struct quicki2c_subip_acpi_parameter {
	u16 device_address;
	u64 connection_speed;
	u8 addressing_mode;
} __packed;

/**
 * struct quicki2c_subip_acpi_config - QuickI2C ACPI DSD parameters
 * @SMHX: Standard Mode (100 kbit/s) Serial Clock Line HIGH Period
 * @SMLX: Standard Mode (100 kbit/s) Serial Clock Line LOW Period
 * @SMTD: Standard Mode (100 kbit/s) Serial Data Line Transmit Hold Period
 * @SMRD: Standard Mode (100 kbit/s) Serial Data Receive Hold Period
 * @FMHX: Fast Mode (400 kbit/s) Serial Clock Line HIGH Period
 * @FMLX: Fast Mode (400 kbit/s) Serial Clock Line LOW Period
 * @FMTD: Fast Mode (400 kbit/s) Serial Data Line Transmit Hold Period
 * @FMRD: Fast Mode (400 kbit/s) Serial Data Line Receive Hold Period
 * @FMSL: Maximum length (in ic_clk_cycles) of suppressed spikes
 *        in Standard Mode, Fast Mode and Fast Mode Plus
 * @FPHX: Fast Mode Plus (1Mbit/sec) Serial Clock Line HIGH Period
 * @FPLX: Fast Mode Plus (1Mbit/sec) Serial Clock Line LOW Period
 * @FPTD: Fast Mode Plus (1Mbit/sec) Serial Data Line Transmit HOLD Period
 * @FPRD: Fast Mode Plus (1Mbit/sec) Serial Data Line Receive HOLD Period
 * @HMHX: High Speed Mode Plus (3.4Mbits/sec) Serial Clock Line HIGH Period
 * @HMLX: High Speed Mode Plus (3.4Mbits/sec) Serial Clock Line LOW Period
 * @HMTD: High Speed Mode Plus (3.4Mbits/sec) Serial Data Line Transmit HOLD Period
 * @HMRD: High Speed Mode Plus (3.4Mbits/sec) Serial Data Line Receive HOLD Period
 * @HMSL: Maximum length (in ic_clk_cycles) of suppressed spikes in High Speed Mode
 *
 * Those properties get from QUICKI2C_ACPI_METHOD_NAME_ISUB method, used for
 * I2C timing configure.
 */
struct quicki2c_subip_acpi_config {
	u64 SMHX;
	u64 SMLX;
	u64 SMTD;
	u64 SMRD;

	u64 FMHX;
	u64 FMLX;
	u64 FMTD;
	u64 FMRD;
	u64 FMSL;

	u64 FPHX;
	u64 FPLX;
	u64 FPTD;
	u64 FPRD;

	u64 HMHX;
	u64 HMLX;
	u64 HMTD;
	u64 HMRD;
	u64 HMSL;
};

struct device;
struct pci_dev;
struct thc_device;
struct hid_device;
struct acpi_device;

/**
 * struct quicki2c_device -  THC QuickI2C device struct
 * @dev: point to kernel device
 * @pdev: point to PCI device
 * @thc_hw: point to THC device
 * @hid_dev: point to hid device
 * @acpi_dev: point to ACPI device
 * @driver_data: point to quicki2c specific driver data
 * @state: THC I2C device state
 * @mem_addr: MMIO memory address
 * @dev_desc: device descriptor for HIDI2C protocol
 * @i2c_slave_addr: HIDI2C device slave address
 * @hid_desc_addr: Register address for retrieve HID device descriptor
 * @active_ltr_val: THC active LTR value
 * @low_power_ltr_val: THC low power LTR value
 * @i2c_speed_mode: 0 - standard mode, 1 - fast mode, 2 - fast mode plus
 * @i2c_clock_hcnt: I2C CLK high period time (unit in cycle count)
 * @i2c_clock_lcnt: I2C CLK low period time (unit in cycle count)
 * @report_descriptor: store a copy of device report descriptor
 * @input_buf: store a copy of latest input report data
 * @report_buf: store a copy of latest input/output report packet from set/get feature
 * @report_len: the length of input/output report packet
 * @reset_ack_wq: workqueue for waiting reset response from device
 * @reset_ack: indicate reset response received or not
 */
struct quicki2c_device {
	struct device *dev;
	struct pci_dev *pdev;
	struct thc_device *thc_hw;
	struct hid_device *hid_dev;
	struct acpi_device *acpi_dev;
	enum quicki2c_dev_state state;

	void __iomem *mem_addr;

	struct hidi2c_dev_descriptor dev_desc;
	u8 i2c_slave_addr;
	u16 hid_desc_addr;

	u32 active_ltr_val;
	u32 low_power_ltr_val;

	u32 i2c_speed_mode;
	u32 i2c_clock_hcnt;
	u32 i2c_clock_lcnt;

	u8 *report_descriptor;
	u8 *input_buf;
	u8 *report_buf;
	u32 report_len;

	wait_queue_head_t reset_ack_wq;
	bool reset_ack;
};

#endif /* _QUICKI2C_DEV_H_ */
