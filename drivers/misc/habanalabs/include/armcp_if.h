/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef ARMCP_IF_H
#define ARMCP_IF_H

#include <linux/types.h>

/*
 * EVENT QUEUE
 */

struct hl_eq_header {
	__le32 reserved;
	__le32 ctl;
};

struct hl_eq_entry {
	struct hl_eq_header hdr;
	__le64 data[7];
};

#define HL_EQ_ENTRY_SIZE		sizeof(struct hl_eq_entry)

#define EQ_CTL_READY_SHIFT		31
#define EQ_CTL_READY_MASK		0x80000000

#define EQ_CTL_EVENT_TYPE_SHIFT		16
#define EQ_CTL_EVENT_TYPE_MASK		0x03FF0000

enum pq_init_status {
	PQ_INIT_STATUS_NA = 0,
	PQ_INIT_STATUS_READY_FOR_CP,
	PQ_INIT_STATUS_READY_FOR_HOST,
	PQ_INIT_STATUS_READY_FOR_CP_SINGLE_MSI
};

/*
 * ArmCP Primary Queue Packets
 *
 * During normal operation, the host's kernel driver needs to send various
 * messages to ArmCP, usually either to SET some value into a H/W periphery or
 * to GET the current value of some H/W periphery. For example, SET the
 * frequency of MME/TPC and GET the value of the thermal sensor.
 *
 * These messages can be initiated either by the User application or by the
 * host's driver itself, e.g. power management code. In either case, the
 * communication from the host's driver to ArmCP will *always* be in
 * synchronous mode, meaning that the host will send a single message and poll
 * until the message was acknowledged and the results are ready (if results are
 * needed).
 *
 * This means that only a single message can be sent at a time and the host's
 * driver must wait for its result before sending the next message. Having said
 * that, because these are control messages which are sent in a relatively low
 * frequency, this limitation seems acceptable. It's important to note that
 * in case of multiple devices, messages to different devices *can* be sent
 * at the same time.
 *
 * The message, inputs/outputs (if relevant) and fence object will be located
 * on the device DDR at an address that will be determined by the host's driver.
 * During device initialization phase, the host will pass to ArmCP that address.
 * Most of the message types will contain inputs/outputs inside the message
 * itself. The common part of each message will contain the opcode of the
 * message (its type) and a field representing a fence object.
 *
 * When the host's driver wishes to send a message to ArmCP, it will write the
 * message contents to the device DDR, clear the fence object and then write the
 * value 484 to the mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR register to issue
 * the 484 interrupt-id to the ARM core.
 *
 * Upon receiving the 484 interrupt-id, ArmCP will read the message from the
 * DDR. In case the message is a SET operation, ArmCP will first perform the
 * operation and then write to the fence object on the device DDR. In case the
 * message is a GET operation, ArmCP will first fill the results section on the
 * device DDR and then write to the fence object. If an error occurred, ArmCP
 * will fill the rc field with the right error code.
 *
 * In the meantime, the host's driver will poll on the fence object. Once the
 * host sees that the fence object is signaled, it will read the results from
 * the device DDR (if relevant) and resume the code execution in the host's
 * driver.
 *
 * To use QMAN packets, the opcode must be the QMAN opcode, shifted by 8
 * so the value being put by the host's driver matches the value read by ArmCP
 *
 * Non-QMAN packets should be limited to values 1 through (2^8 - 1)
 *
 * Detailed description:
 *
 * ARMCP_PACKET_DISABLE_PCI_ACCESS -
 *       After receiving this packet the embedded CPU must NOT issue PCI
 *       transactions (read/write) towards the Host CPU. This also include
 *       sending MSI-X interrupts.
 *       This packet is usually sent before the device is moved to D3Hot state.
 *
 * ARMCP_PACKET_ENABLE_PCI_ACCESS -
 *       After receiving this packet the embedded CPU is allowed to issue PCI
 *       transactions towards the Host CPU, including sending MSI-X interrupts.
 *       This packet is usually send after the device is moved to D0 state.
 *
 * ARMCP_PACKET_TEMPERATURE_GET -
 *       Fetch the current temperature / Max / Max Hyst / Critical /
 *       Critical Hyst of a specified thermal sensor. The packet's
 *       arguments specify the desired sensor and the field to get.
 *
 * ARMCP_PACKET_VOLTAGE_GET -
 *       Fetch the voltage / Max / Min of a specified sensor. The packet's
 *       arguments specify the sensor and type.
 *
 * ARMCP_PACKET_CURRENT_GET -
 *       Fetch the current / Max / Min of a specified sensor. The packet's
 *       arguments specify the sensor and type.
 *
 * ARMCP_PACKET_FAN_SPEED_GET -
 *       Fetch the speed / Max / Min of a specified fan. The packet's
 *       arguments specify the sensor and type.
 *
 * ARMCP_PACKET_PWM_GET -
 *       Fetch the pwm value / mode of a specified pwm. The packet's
 *       arguments specify the sensor and type.
 *
 * ARMCP_PACKET_PWM_SET -
 *       Set the pwm value / mode of a specified pwm. The packet's
 *       arguments specify the sensor, type and value.
 *
 * ARMCP_PACKET_FREQUENCY_SET -
 *       Set the frequency of a specified PLL. The packet's arguments specify
 *       the PLL and the desired frequency. The actual frequency in the device
 *       might differ from the requested frequency.
 *
 * ARMCP_PACKET_FREQUENCY_GET -
 *       Fetch the frequency of a specified PLL. The packet's arguments specify
 *       the PLL.
 *
 * ARMCP_PACKET_LED_SET -
 *       Set the state of a specified led. The packet's arguments
 *       specify the led and the desired state.
 *
 * ARMCP_PACKET_I2C_WR -
 *       Write 32-bit value to I2C device. The packet's arguments specify the
 *       I2C bus, address and value.
 *
 * ARMCP_PACKET_I2C_RD -
 *       Read 32-bit value from I2C device. The packet's arguments specify the
 *       I2C bus and address.
 *
 * ARMCP_PACKET_INFO_GET -
 *       Fetch information from the device as specified in the packet's
 *       structure. The host's driver passes the max size it allows the ArmCP to
 *       write to the structure, to prevent data corruption in case of
 *       mismatched driver/FW versions.
 *
 * ARMCP_PACKET_FLASH_PROGRAM_REMOVED - this packet was removed
 *
 * ARMCP_PACKET_UNMASK_RAZWI_IRQ -
 *       Unmask the given IRQ. The IRQ number is specified in the value field.
 *       The packet is sent after receiving an interrupt and printing its
 *       relevant information.
 *
 * ARMCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY -
 *       Unmask the given IRQs. The IRQs numbers are specified in an array right
 *       after the armcp_packet structure, where its first element is the array
 *       length. The packet is sent after a soft reset was done in order to
 *       handle any interrupts that were sent during the reset process.
 *
 * ARMCP_PACKET_TEST -
 *       Test packet for ArmCP connectivity. The CPU will put the fence value
 *       in the result field.
 *
 * ARMCP_PACKET_FREQUENCY_CURR_GET -
 *       Fetch the current frequency of a specified PLL. The packet's arguments
 *       specify the PLL.
 *
 * ARMCP_PACKET_MAX_POWER_GET -
 *       Fetch the maximal power of the device.
 *
 * ARMCP_PACKET_MAX_POWER_SET -
 *       Set the maximal power of the device. The packet's arguments specify
 *       the power.
 *
 * ARMCP_PACKET_EEPROM_DATA_GET -
 *       Get EEPROM data from the ArmCP kernel. The buffer is specified in the
 *       addr field. The CPU will put the returned data size in the result
 *       field. In addition, the host's driver passes the max size it allows the
 *       ArmCP to write to the structure, to prevent data corruption in case of
 *       mismatched driver/FW versions.
 *
 * ARMCP_PACKET_TEMPERATURE_SET -
 *       Set the value of the offset property of a specified thermal sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * ARMCP_PACKET_VOLTAGE_SET -
 *       Trigger the reset_history property of a specified voltage sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * ARMCP_PACKET_CURRENT_SET -
 *       Trigger the reset_history property of a specified current sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 */

enum armcp_packet_id {
	ARMCP_PACKET_DISABLE_PCI_ACCESS = 1,	/* internal */
	ARMCP_PACKET_ENABLE_PCI_ACCESS,		/* internal */
	ARMCP_PACKET_TEMPERATURE_GET,		/* sysfs */
	ARMCP_PACKET_VOLTAGE_GET,		/* sysfs */
	ARMCP_PACKET_CURRENT_GET,		/* sysfs */
	ARMCP_PACKET_FAN_SPEED_GET,		/* sysfs */
	ARMCP_PACKET_PWM_GET,			/* sysfs */
	ARMCP_PACKET_PWM_SET,			/* sysfs */
	ARMCP_PACKET_FREQUENCY_SET,		/* sysfs */
	ARMCP_PACKET_FREQUENCY_GET,		/* sysfs */
	ARMCP_PACKET_LED_SET,			/* debugfs */
	ARMCP_PACKET_I2C_WR,			/* debugfs */
	ARMCP_PACKET_I2C_RD,			/* debugfs */
	ARMCP_PACKET_INFO_GET,			/* IOCTL */
	ARMCP_PACKET_FLASH_PROGRAM_REMOVED,
	ARMCP_PACKET_UNMASK_RAZWI_IRQ,		/* internal */
	ARMCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY,	/* internal */
	ARMCP_PACKET_TEST,			/* internal */
	ARMCP_PACKET_FREQUENCY_CURR_GET,	/* sysfs */
	ARMCP_PACKET_MAX_POWER_GET,		/* sysfs */
	ARMCP_PACKET_MAX_POWER_SET,		/* sysfs */
	ARMCP_PACKET_EEPROM_DATA_GET,		/* sysfs */
	ARMCP_RESERVED,
	ARMCP_PACKET_TEMPERATURE_SET,		/* sysfs */
	ARMCP_PACKET_VOLTAGE_SET,		/* sysfs */
	ARMCP_PACKET_CURRENT_SET,		/* sysfs */
};

#define ARMCP_PACKET_FENCE_VAL	0xFE8CE7A5

#define ARMCP_PKT_CTL_RC_SHIFT		12
#define ARMCP_PKT_CTL_RC_MASK		0x0000F000

#define ARMCP_PKT_CTL_OPCODE_SHIFT	16
#define ARMCP_PKT_CTL_OPCODE_MASK	0x1FFF0000

struct armcp_packet {
	union {
		__le64 value;	/* For SET packets */
		__le64 result;	/* For GET packets */
		__le64 addr;	/* For PQ */
	};

	__le32 ctl;

	__le32 fence;		/* Signal to host that message is completed */

	union {
		struct {/* For temperature/current/voltage/fan/pwm get/set */
			__le16 sensor_index;
			__le16 type;
		};

		struct {	/* For I2C read/write */
			__u8 i2c_bus;
			__u8 i2c_addr;
			__u8 i2c_reg;
			__u8 pad; /* unused */
		};

		/* For frequency get/set */
		__le32 pll_index;

		/* For led set */
		__le32 led_index;

		/* For get Armcp info/EEPROM data */
		__le32 data_max_size;
	};
};

struct armcp_unmask_irq_arr_packet {
	struct armcp_packet armcp_pkt;
	__le32 length;
	__le32 irqs[0];
};

enum armcp_packet_rc {
	armcp_packet_success,
	armcp_packet_invalid,
	armcp_packet_fault
};

/*
 * armcp_temp_type should adhere to hwmon_temp_attributes
 * defined in Linux kernel hwmon.h file
 */
enum armcp_temp_type {
	armcp_temp_input,
	armcp_temp_max = 6,
	armcp_temp_max_hyst,
	armcp_temp_crit,
	armcp_temp_crit_hyst,
	armcp_temp_offset = 19,
	armcp_temp_highest = 22,
	armcp_temp_reset_history = 23
};

enum armcp_in_attributes {
	armcp_in_input,
	armcp_in_min,
	armcp_in_max,
	armcp_in_highest = 7,
	armcp_in_reset_history
};

enum armcp_curr_attributes {
	armcp_curr_input,
	armcp_curr_min,
	armcp_curr_max,
	armcp_curr_highest = 7,
	armcp_curr_reset_history
};

enum armcp_fan_attributes {
	armcp_fan_input,
	armcp_fan_min = 2,
	armcp_fan_max
};

enum armcp_pwm_attributes {
	armcp_pwm_input,
	armcp_pwm_enable
};

/* Event Queue Packets */

struct eq_generic_event {
	__le64 data[7];
};

/*
 * ArmCP info
 */

#define CARD_NAME_MAX_LEN		16
#define VERSION_MAX_LEN			128
#define ARMCP_MAX_SENSORS		128

struct armcp_sensor {
	__le32 type;
	__le32 flags;
};

/**
 * struct armcp_card_types - ASIC card type.
 * @armcp_card_type_pci: PCI card.
 * @armcp_card_type_pmc: PCI Mezzanine Card.
 */
enum armcp_card_types {
	armcp_card_type_pci,
	armcp_card_type_pmc
};

/**
 * struct armcp_info - Info from ArmCP that is necessary to the host's driver
 * @sensors: available sensors description.
 * @kernel_version: ArmCP linux kernel version.
 * @reserved: reserved field.
 * @card_type: card configuration type.
 * @card_location: in a server, each card has different connections topology
 *                 depending on its location (relevant for PMC card type)
 * @cpld_version: CPLD programmed F/W version.
 * @infineon_version: Infineon main DC-DC version.
 * @fuse_version: silicon production FUSE information.
 * @thermal_version: thermald S/W version.
 * @armcp_version: ArmCP S/W version.
 * @dram_size: available DRAM size.
 * @card_name: card name that will be displayed in HWMON subsystem on the host
 */
struct armcp_info {
	struct armcp_sensor sensors[ARMCP_MAX_SENSORS];
	__u8 kernel_version[VERSION_MAX_LEN];
	__le32 reserved;
	__le32 card_type;
	__le32 card_location;
	__le32 cpld_version;
	__le32 infineon_version;
	__u8 fuse_version[VERSION_MAX_LEN];
	__u8 thermal_version[VERSION_MAX_LEN];
	__u8 armcp_version[VERSION_MAX_LEN];
	__le64 dram_size;
	char card_name[CARD_NAME_MAX_LEN];
};

#endif /* ARMCP_IF_H */
