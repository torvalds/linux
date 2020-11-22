/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef CPUCP_IF_H
#define CPUCP_IF_H

#include <linux/types.h>
#include <linux/if_ether.h>

#define NUM_HBM_PSEUDO_CH				2
#define NUM_HBM_CH_PER_DEV				8
#define CPUCP_PKT_HBM_ECC_INFO_WR_PAR_SHIFT		0
#define CPUCP_PKT_HBM_ECC_INFO_WR_PAR_MASK		0x00000001
#define CPUCP_PKT_HBM_ECC_INFO_RD_PAR_SHIFT		1
#define CPUCP_PKT_HBM_ECC_INFO_RD_PAR_MASK		0x00000002
#define CPUCP_PKT_HBM_ECC_INFO_CA_PAR_SHIFT		2
#define CPUCP_PKT_HBM_ECC_INFO_CA_PAR_MASK		0x00000004
#define CPUCP_PKT_HBM_ECC_INFO_DERR_SHIFT		3
#define CPUCP_PKT_HBM_ECC_INFO_DERR_MASK		0x00000008
#define CPUCP_PKT_HBM_ECC_INFO_SERR_SHIFT		4
#define CPUCP_PKT_HBM_ECC_INFO_SERR_MASK		0x00000010
#define CPUCP_PKT_HBM_ECC_INFO_TYPE_SHIFT		5
#define CPUCP_PKT_HBM_ECC_INFO_TYPE_MASK		0x00000020
#define CPUCP_PKT_HBM_ECC_INFO_HBM_CH_SHIFT		6
#define CPUCP_PKT_HBM_ECC_INFO_HBM_CH_MASK		0x000007C0

struct hl_eq_hbm_ecc_data {
	/* SERR counter */
	__le32 sec_cnt;
	/* DERR counter */
	__le32 dec_cnt;
	/* Supplemental Information according to the mask bits */
	__le32 hbm_ecc_info;
	/* Address in hbm where the ecc happened */
	__le32 first_addr;
	/* SERR continuous address counter */
	__le32 sec_cont_cnt;
	__le32 pad;
};

/*
 * EVENT QUEUE
 */

struct hl_eq_header {
	__le32 reserved;
	__le32 ctl;
};

struct hl_eq_ecc_data {
	__le64 ecc_address;
	__le64 ecc_syndrom;
	__u8 memory_wrapper_idx;
	__u8 pad[7];
};

struct hl_eq_entry {
	struct hl_eq_header hdr;
	union {
		struct hl_eq_ecc_data ecc_data;
		struct hl_eq_hbm_ecc_data hbm_ecc_data;
		__le64 data[7];
	};
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
 * CpuCP Primary Queue Packets
 *
 * During normal operation, the host's kernel driver needs to send various
 * messages to CpuCP, usually either to SET some value into a H/W periphery or
 * to GET the current value of some H/W periphery. For example, SET the
 * frequency of MME/TPC and GET the value of the thermal sensor.
 *
 * These messages can be initiated either by the User application or by the
 * host's driver itself, e.g. power management code. In either case, the
 * communication from the host's driver to CpuCP will *always* be in
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
 * During device initialization phase, the host will pass to CpuCP that address.
 * Most of the message types will contain inputs/outputs inside the message
 * itself. The common part of each message will contain the opcode of the
 * message (its type) and a field representing a fence object.
 *
 * When the host's driver wishes to send a message to CPU CP, it will write the
 * message contents to the device DDR, clear the fence object and then write to
 * the PSOC_ARC1_AUX_SW_INTR, to issue interrupt 121 to ARC Management CPU.
 *
 * Upon receiving the interrupt (#121), CpuCP will read the message from the
 * DDR. In case the message is a SET operation, CpuCP will first perform the
 * operation and then write to the fence object on the device DDR. In case the
 * message is a GET operation, CpuCP will first fill the results section on the
 * device DDR and then write to the fence object. If an error occurred, CpuCP
 * will fill the rc field with the right error code.
 *
 * In the meantime, the host's driver will poll on the fence object. Once the
 * host sees that the fence object is signaled, it will read the results from
 * the device DDR (if relevant) and resume the code execution in the host's
 * driver.
 *
 * To use QMAN packets, the opcode must be the QMAN opcode, shifted by 8
 * so the value being put by the host's driver matches the value read by CpuCP
 *
 * Non-QMAN packets should be limited to values 1 through (2^8 - 1)
 *
 * Detailed description:
 *
 * CPUCP_PACKET_DISABLE_PCI_ACCESS -
 *       After receiving this packet the embedded CPU must NOT issue PCI
 *       transactions (read/write) towards the Host CPU. This also include
 *       sending MSI-X interrupts.
 *       This packet is usually sent before the device is moved to D3Hot state.
 *
 * CPUCP_PACKET_ENABLE_PCI_ACCESS -
 *       After receiving this packet the embedded CPU is allowed to issue PCI
 *       transactions towards the Host CPU, including sending MSI-X interrupts.
 *       This packet is usually send after the device is moved to D0 state.
 *
 * CPUCP_PACKET_TEMPERATURE_GET -
 *       Fetch the current temperature / Max / Max Hyst / Critical /
 *       Critical Hyst of a specified thermal sensor. The packet's
 *       arguments specify the desired sensor and the field to get.
 *
 * CPUCP_PACKET_VOLTAGE_GET -
 *       Fetch the voltage / Max / Min of a specified sensor. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_CURRENT_GET -
 *       Fetch the current / Max / Min of a specified sensor. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_FAN_SPEED_GET -
 *       Fetch the speed / Max / Min of a specified fan. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_PWM_GET -
 *       Fetch the pwm value / mode of a specified pwm. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_PWM_SET -
 *       Set the pwm value / mode of a specified pwm. The packet's
 *       arguments specify the sensor, type and value.
 *
 * CPUCP_PACKET_FREQUENCY_SET -
 *       Set the frequency of a specified PLL. The packet's arguments specify
 *       the PLL and the desired frequency. The actual frequency in the device
 *       might differ from the requested frequency.
 *
 * CPUCP_PACKET_FREQUENCY_GET -
 *       Fetch the frequency of a specified PLL. The packet's arguments specify
 *       the PLL.
 *
 * CPUCP_PACKET_LED_SET -
 *       Set the state of a specified led. The packet's arguments
 *       specify the led and the desired state.
 *
 * CPUCP_PACKET_I2C_WR -
 *       Write 32-bit value to I2C device. The packet's arguments specify the
 *       I2C bus, address and value.
 *
 * CPUCP_PACKET_I2C_RD -
 *       Read 32-bit value from I2C device. The packet's arguments specify the
 *       I2C bus and address.
 *
 * CPUCP_PACKET_INFO_GET -
 *       Fetch information from the device as specified in the packet's
 *       structure. The host's driver passes the max size it allows the CpuCP to
 *       write to the structure, to prevent data corruption in case of
 *       mismatched driver/FW versions.
 *
 * CPUCP_PACKET_FLASH_PROGRAM_REMOVED - this packet was removed
 *
 * CPUCP_PACKET_UNMASK_RAZWI_IRQ -
 *       Unmask the given IRQ. The IRQ number is specified in the value field.
 *       The packet is sent after receiving an interrupt and printing its
 *       relevant information.
 *
 * CPUCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY -
 *       Unmask the given IRQs. The IRQs numbers are specified in an array right
 *       after the cpucp_packet structure, where its first element is the array
 *       length. The packet is sent after a soft reset was done in order to
 *       handle any interrupts that were sent during the reset process.
 *
 * CPUCP_PACKET_TEST -
 *       Test packet for CpuCP connectivity. The CPU will put the fence value
 *       in the result field.
 *
 * CPUCP_PACKET_FREQUENCY_CURR_GET -
 *       Fetch the current frequency of a specified PLL. The packet's arguments
 *       specify the PLL.
 *
 * CPUCP_PACKET_MAX_POWER_GET -
 *       Fetch the maximal power of the device.
 *
 * CPUCP_PACKET_MAX_POWER_SET -
 *       Set the maximal power of the device. The packet's arguments specify
 *       the power.
 *
 * CPUCP_PACKET_EEPROM_DATA_GET -
 *       Get EEPROM data from the CpuCP kernel. The buffer is specified in the
 *       addr field. The CPU will put the returned data size in the result
 *       field. In addition, the host's driver passes the max size it allows the
 *       CpuCP to write to the structure, to prevent data corruption in case of
 *       mismatched driver/FW versions.
 *
 * CPUCP_PACKET_NIC_INFO_GET -
 *       Fetch information from the device regarding the NIC. the host's driver
 *       passes the max size it allows the CpuCP to write to the structure, to
 *       prevent data corruption in case of mismatched driver/FW versions.
 *
 * CPUCP_PACKET_TEMPERATURE_SET -
 *       Set the value of the offset property of a specified thermal sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * CPUCP_PACKET_VOLTAGE_SET -
 *       Trigger the reset_history property of a specified voltage sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * CPUCP_PACKET_CURRENT_SET -
 *       Trigger the reset_history property of a specified current sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * CPUCP_PACKET_PCIE_THROUGHPUT_GET
 *       Get throughput of PCIe.
 *       The packet's arguments specify the transaction direction (TX/RX).
 *       The window measurement is 10[msec], and the return value is in KB/sec.
 *
 * CPUCP_PACKET_PCIE_REPLAY_CNT_GET
 *       Replay count measures number of "replay" events, which is basicly
 *       number of retries done by PCIe.
 *
 * CPUCP_PACKET_TOTAL_ENERGY_GET
 *       Total Energy is measurement of energy from the time FW Linux
 *       is loaded. It is calculated by multiplying the average power
 *       by time (passed from armcp start). The units are in MilliJouls.
 *
 * CPUCP_PACKET_PLL_INFO_GET
 *       Fetch frequencies of PLL from the required PLL IP.
 *       The packet's arguments specify the device PLL type
 *       Pll type is the PLL from device pll_index enum.
 *       The result is composed of 4 outputs, each is 16-bit
 *       frequency in MHz.
 *
 */

enum cpucp_packet_id {
	CPUCP_PACKET_DISABLE_PCI_ACCESS = 1,	/* internal */
	CPUCP_PACKET_ENABLE_PCI_ACCESS,		/* internal */
	CPUCP_PACKET_TEMPERATURE_GET,		/* sysfs */
	CPUCP_PACKET_VOLTAGE_GET,		/* sysfs */
	CPUCP_PACKET_CURRENT_GET,		/* sysfs */
	CPUCP_PACKET_FAN_SPEED_GET,		/* sysfs */
	CPUCP_PACKET_PWM_GET,			/* sysfs */
	CPUCP_PACKET_PWM_SET,			/* sysfs */
	CPUCP_PACKET_FREQUENCY_SET,		/* sysfs */
	CPUCP_PACKET_FREQUENCY_GET,		/* sysfs */
	CPUCP_PACKET_LED_SET,			/* debugfs */
	CPUCP_PACKET_I2C_WR,			/* debugfs */
	CPUCP_PACKET_I2C_RD,			/* debugfs */
	CPUCP_PACKET_INFO_GET,			/* IOCTL */
	CPUCP_PACKET_FLASH_PROGRAM_REMOVED,
	CPUCP_PACKET_UNMASK_RAZWI_IRQ,		/* internal */
	CPUCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY,	/* internal */
	CPUCP_PACKET_TEST,			/* internal */
	CPUCP_PACKET_FREQUENCY_CURR_GET,	/* sysfs */
	CPUCP_PACKET_MAX_POWER_GET,		/* sysfs */
	CPUCP_PACKET_MAX_POWER_SET,		/* sysfs */
	CPUCP_PACKET_EEPROM_DATA_GET,		/* sysfs */
	CPUCP_PACKET_NIC_INFO_GET,		/* internal */
	CPUCP_PACKET_TEMPERATURE_SET,		/* sysfs */
	CPUCP_PACKET_VOLTAGE_SET,		/* sysfs */
	CPUCP_PACKET_CURRENT_SET,		/* sysfs */
	CPUCP_PACKET_PCIE_THROUGHPUT_GET,	/* internal */
	CPUCP_PACKET_PCIE_REPLAY_CNT_GET,	/* internal */
	CPUCP_PACKET_TOTAL_ENERGY_GET,		/* internal */
	CPUCP_PACKET_PLL_INFO_GET,		/* internal */
};

#define CPUCP_PACKET_FENCE_VAL	0xFE8CE7A5

#define CPUCP_PKT_CTL_RC_SHIFT		12
#define CPUCP_PKT_CTL_RC_MASK		0x0000F000

#define CPUCP_PKT_CTL_OPCODE_SHIFT	16
#define CPUCP_PKT_CTL_OPCODE_MASK	0x1FFF0000

#define CPUCP_PKT_RES_PLL_OUT0_SHIFT	0
#define CPUCP_PKT_RES_PLL_OUT0_MASK	0x000000000000FFFFull
#define CPUCP_PKT_RES_PLL_OUT1_SHIFT	16
#define CPUCP_PKT_RES_PLL_OUT1_MASK	0x00000000FFFF0000ull
#define CPUCP_PKT_RES_PLL_OUT2_SHIFT	32
#define CPUCP_PKT_RES_PLL_OUT2_MASK	0x0000FFFF00000000ull
#define CPUCP_PKT_RES_PLL_OUT3_SHIFT	48
#define CPUCP_PKT_RES_PLL_OUT3_MASK	0xFFFF000000000000ull

struct cpucp_packet {
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

		struct {/* For PLL info fetch */
			__le16 pll_type;
			/* TODO pll_reg is kept temporary before removal */
			__le16 pll_reg;
		};

		/* For any general request */
		__le32 index;

		/* For frequency get/set */
		__le32 pll_index;

		/* For led set */
		__le32 led_index;

		/* For get CpuCP info/EEPROM data/NIC info */
		__le32 data_max_size;
	};

	__le32 reserved;
};

struct cpucp_unmask_irq_arr_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 irqs[0];
};

enum cpucp_packet_rc {
	cpucp_packet_success,
	cpucp_packet_invalid,
	cpucp_packet_fault
};

/*
 * cpucp_temp_type should adhere to hwmon_temp_attributes
 * defined in Linux kernel hwmon.h file
 */
enum cpucp_temp_type {
	cpucp_temp_input,
	cpucp_temp_max = 6,
	cpucp_temp_max_hyst,
	cpucp_temp_crit,
	cpucp_temp_crit_hyst,
	cpucp_temp_offset = 19,
	cpucp_temp_highest = 22,
	cpucp_temp_reset_history = 23
};

enum cpucp_in_attributes {
	cpucp_in_input,
	cpucp_in_min,
	cpucp_in_max,
	cpucp_in_highest = 7,
	cpucp_in_reset_history
};

enum cpucp_curr_attributes {
	cpucp_curr_input,
	cpucp_curr_min,
	cpucp_curr_max,
	cpucp_curr_highest = 7,
	cpucp_curr_reset_history
};

enum cpucp_fan_attributes {
	cpucp_fan_input,
	cpucp_fan_min = 2,
	cpucp_fan_max
};

enum cpucp_pwm_attributes {
	cpucp_pwm_input,
	cpucp_pwm_enable
};

enum cpucp_pcie_throughput_attributes {
	cpucp_pcie_throughput_tx,
	cpucp_pcie_throughput_rx
};

/* TODO temporary kept before removal */
enum cpucp_pll_reg_attributes {
	cpucp_pll_nr_reg,
	cpucp_pll_nf_reg,
	cpucp_pll_od_reg,
	cpucp_pll_div_factor_reg,
	cpucp_pll_div_sel_reg
};

/* TODO temporary kept before removal */
enum cpucp_pll_type_attributes {
	cpucp_pll_cpu,
	cpucp_pll_pci,
};

/* Event Queue Packets */

struct eq_generic_event {
	__le64 data[7];
};

/*
 * CpuCP info
 */

#define CARD_NAME_MAX_LEN		16
#define VERSION_MAX_LEN			128
#define CPUCP_MAX_SENSORS		128
#define CPUCP_MAX_NICS			128
#define CPUCP_LANES_PER_NIC		4
#define CPUCP_NIC_QSFP_EEPROM_MAX_LEN	1024
#define CPUCP_MAX_NIC_LANES		(CPUCP_MAX_NICS * CPUCP_LANES_PER_NIC)
#define CPUCP_NIC_MASK_ARR_LEN		((CPUCP_MAX_NICS + 63) / 64)
#define CPUCP_NIC_POLARITY_ARR_LEN	((CPUCP_MAX_NIC_LANES + 63) / 64)

struct cpucp_sensor {
	__le32 type;
	__le32 flags;
};

/**
 * struct cpucp_card_types - ASIC card type.
 * @cpucp_card_type_pci: PCI card.
 * @cpucp_card_type_pmc: PCI Mezzanine Card.
 */
enum cpucp_card_types {
	cpucp_card_type_pci,
	cpucp_card_type_pmc
};

#define CPUCP_SEC_CONF_ENABLED_SHIFT	0
#define CPUCP_SEC_CONF_ENABLED_MASK	0x00000001

#define CPUCP_SEC_CONF_FLASH_WP_SHIFT	1
#define CPUCP_SEC_CONF_FLASH_WP_MASK	0x00000002

#define CPUCP_SEC_CONF_EEPROM_WP_SHIFT	2
#define CPUCP_SEC_CONF_EEPROM_WP_MASK	0x00000004

/**
 * struct cpucp_security_info - Security information.
 * @config: configuration bit field
 * @keys_num: number of stored keys
 * @revoked_keys: revoked keys bit field
 * @min_svn: minimal security version
 */
struct cpucp_security_info {
	__u8 config;
	__u8 keys_num;
	__u8 revoked_keys;
	__u8 min_svn;
};

/**
 * struct cpucp_info - Info from CpuCP that is necessary to the host's driver
 * @sensors: available sensors description.
 * @kernel_version: CpuCP linux kernel version.
 * @reserved: reserved field.
 * @card_type: card configuration type.
 * @card_location: in a server, each card has different connections topology
 *                 depending on its location (relevant for PMC card type)
 * @cpld_version: CPLD programmed F/W version.
 * @infineon_version: Infineon main DC-DC version.
 * @fuse_version: silicon production FUSE information.
 * @thermal_version: thermald S/W version.
 * @cpucp_version: CpuCP S/W version.
 * @dram_size: available DRAM size.
 * @card_name: card name that will be displayed in HWMON subsystem on the host
 * @sec_info: security information
 */
struct cpucp_info {
	struct cpucp_sensor sensors[CPUCP_MAX_SENSORS];
	__u8 kernel_version[VERSION_MAX_LEN];
	__le32 reserved;
	__le32 card_type;
	__le32 card_location;
	__le32 cpld_version;
	__le32 infineon_version;
	__u8 fuse_version[VERSION_MAX_LEN];
	__u8 thermal_version[VERSION_MAX_LEN];
	__u8 cpucp_version[VERSION_MAX_LEN];
	__le32 reserved2;
	__le64 dram_size;
	char card_name[CARD_NAME_MAX_LEN];
	__le64 reserved3;
	__le64 reserved4;
	__u8 reserved5;
	__u8 pad[7];
	struct cpucp_security_info sec_info;
	__le32 reserved6;
};

struct cpucp_mac_addr {
	__u8 mac_addr[ETH_ALEN];
};

struct cpucp_nic_info {
	struct cpucp_mac_addr mac_addrs[CPUCP_MAX_NICS];
	__le64 link_mask[CPUCP_NIC_MASK_ARR_LEN];
	__le64 pol_tx_mask[CPUCP_NIC_POLARITY_ARR_LEN];
	__le64 pol_rx_mask[CPUCP_NIC_POLARITY_ARR_LEN];
	__le64 link_ext_mask[CPUCP_NIC_MASK_ARR_LEN];
	__u8 qsfp_eeprom[CPUCP_NIC_QSFP_EEPROM_MAX_LEN];
	__le64 auto_neg_mask[CPUCP_NIC_MASK_ARR_LEN];
};

#endif /* CPUCP_IF_H */
