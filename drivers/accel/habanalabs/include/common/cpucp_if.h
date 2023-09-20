/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef CPUCP_IF_H
#define CPUCP_IF_H

#include <linux/types.h>
#include <linux/if_ether.h>

#include "hl_boot_if.h"

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

#define PLL_MAP_MAX_BITS	128
#define PLL_MAP_LEN		(PLL_MAP_MAX_BITS / 8)

/*
 * info of the pkt queue pointers in the first async occurrence
 */
struct cpucp_pkt_sync_err {
	__le32 pi;
	__le32 ci;
};

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
	__u8 is_critical;
	__u8 pad[6];
};

enum hl_sm_sei_cause {
	SM_SEI_SO_OVERFLOW,
	SM_SEI_LBW_4B_UNALIGNED,
	SM_SEI_AXI_RESPONSE_ERR
};

struct hl_eq_sm_sei_data {
	__le32 sei_log;
	/* enum hl_sm_sei_cause */
	__u8 sei_cause;
	__u8 pad[3];
};

enum hl_fw_alive_severity {
	FW_ALIVE_SEVERITY_MINOR,
	FW_ALIVE_SEVERITY_CRITICAL
};

struct hl_eq_fw_alive {
	__le64 uptime_seconds;
	__le32 process_id;
	__le32 thread_id;
	/* enum hl_fw_alive_severity */
	__u8 severity;
	__u8 pad[7];
};

struct hl_eq_intr_cause {
	__le64 intr_cause_data;
};

struct hl_eq_pcie_drain_ind_data {
	struct hl_eq_intr_cause intr_cause;
	__le64 drain_wr_addr_lbw;
	__le64 drain_rd_addr_lbw;
	__le64 drain_wr_addr_hbw;
	__le64 drain_rd_addr_hbw;
};

struct hl_eq_razwi_lbw_info_regs {
	__le32 rr_aw_razwi_reg;
	__le32 rr_aw_razwi_id_reg;
	__le32 rr_ar_razwi_reg;
	__le32 rr_ar_razwi_id_reg;
};

struct hl_eq_razwi_hbw_info_regs {
	__le32 rr_aw_razwi_hi_reg;
	__le32 rr_aw_razwi_lo_reg;
	__le32 rr_aw_razwi_id_reg;
	__le32 rr_ar_razwi_hi_reg;
	__le32 rr_ar_razwi_lo_reg;
	__le32 rr_ar_razwi_id_reg;
};

/* razwi_happened masks */
#define RAZWI_HAPPENED_HBW	0x1
#define RAZWI_HAPPENED_LBW	0x2
#define RAZWI_HAPPENED_AW	0x4
#define RAZWI_HAPPENED_AR	0x8

struct hl_eq_razwi_info {
	__le32 razwi_happened_mask;
	union {
		struct hl_eq_razwi_lbw_info_regs lbw;
		struct hl_eq_razwi_hbw_info_regs hbw;
	};
	__le32 pad;
};

struct hl_eq_razwi_with_intr_cause {
	struct hl_eq_razwi_info razwi_info;
	struct hl_eq_intr_cause intr_cause;
};

#define HBM_CA_ERR_CMD_LIFO_LEN		8
#define HBM_RD_ERR_DATA_LIFO_LEN	8
#define HBM_WR_PAR_CMD_LIFO_LEN		11

enum hl_hbm_sei_cause {
	/* Command/address parity error event is split into 2 events due to
	 * size limitation: ODD suffix for odd HBM CK_t cycles and EVEN  suffix
	 * for even HBM CK_t cycles
	 */
	HBM_SEI_CMD_PARITY_EVEN,
	HBM_SEI_CMD_PARITY_ODD,
	/* Read errors can be reflected as a combination of SERR/DERR/parity
	 * errors. Therefore, we define one event for all read error types.
	 * LKD will perform further proccessing.
	 */
	HBM_SEI_READ_ERR,
	HBM_SEI_WRITE_DATA_PARITY_ERR,
	HBM_SEI_CATTRIP,
	HBM_SEI_MEM_BIST_FAIL,
	HBM_SEI_DFI,
	HBM_SEI_INV_TEMP_READ_OUT,
	HBM_SEI_BIST_FAIL,
};

/* Masks for parsing hl_hbm_sei_headr fields */
#define HBM_ECC_SERR_CNTR_MASK		0xFF
#define HBM_ECC_DERR_CNTR_MASK		0xFF00
#define HBM_RD_PARITY_CNTR_MASK		0xFF0000

/* HBM index and MC index are known by the event_id */
struct hl_hbm_sei_header {
	union {
		/* relevant only in case of HBM read error */
		struct {
			__u8 ecc_serr_cnt;
			__u8 ecc_derr_cnt;
			__u8 read_par_cnt;
			__u8 reserved;
		};
		/* All other cases */
		__le32 cnt;
	};
	__u8 sei_cause;		/* enum hl_hbm_sei_cause */
	__u8 mc_channel;		/* range: 0-3 */
	__u8 mc_pseudo_channel;	/* range: 0-7 */
	__u8 is_critical;
};

#define HBM_RD_ADDR_SID_SHIFT		0
#define HBM_RD_ADDR_SID_MASK		0x1
#define HBM_RD_ADDR_BG_SHIFT		1
#define HBM_RD_ADDR_BG_MASK		0x6
#define HBM_RD_ADDR_BA_SHIFT		3
#define HBM_RD_ADDR_BA_MASK		0x18
#define HBM_RD_ADDR_COL_SHIFT		5
#define HBM_RD_ADDR_COL_MASK		0x7E0
#define HBM_RD_ADDR_ROW_SHIFT		11
#define HBM_RD_ADDR_ROW_MASK		0x3FFF800

struct hbm_rd_addr {
	union {
		/* bit fields are only for FW use */
		struct {
			u32 dbg_rd_err_addr_sid:1;
			u32 dbg_rd_err_addr_bg:2;
			u32 dbg_rd_err_addr_ba:2;
			u32 dbg_rd_err_addr_col:6;
			u32 dbg_rd_err_addr_row:15;
			u32 reserved:6;
		};
		__le32 rd_addr_val;
	};
};

#define HBM_RD_ERR_BEAT_SHIFT		2
/* dbg_rd_err_misc fields: */
/* Read parity is calculated per DW on every beat */
#define HBM_RD_ERR_PAR_ERR_BEAT0_SHIFT	0
#define HBM_RD_ERR_PAR_ERR_BEAT0_MASK	0x3
#define HBM_RD_ERR_PAR_DATA_BEAT0_SHIFT	8
#define HBM_RD_ERR_PAR_DATA_BEAT0_MASK	0x300
/* ECC is calculated per PC on every beat */
#define HBM_RD_ERR_SERR_BEAT0_SHIFT	16
#define HBM_RD_ERR_SERR_BEAT0_MASK	0x10000
#define HBM_RD_ERR_DERR_BEAT0_SHIFT	24
#define HBM_RD_ERR_DERR_BEAT0_MASK	0x100000

struct hl_eq_hbm_sei_read_err_intr_info {
	/* DFI_RD_ERR_REP_ADDR */
	struct hbm_rd_addr dbg_rd_err_addr;
	/* DFI_RD_ERR_REP_ERR */
	union {
		struct {
			/* bit fields are only for FW use */
			u32 dbg_rd_err_par:8;
			u32 dbg_rd_err_par_data:8;
			u32 dbg_rd_err_serr:4;
			u32 dbg_rd_err_derr:4;
			u32 reserved:8;
		};
		__le32 dbg_rd_err_misc;
	};
	/* DFI_RD_ERR_REP_DM */
	__le32 dbg_rd_err_dm;
	/* DFI_RD_ERR_REP_SYNDROME */
	__le32 dbg_rd_err_syndrome;
	/* DFI_RD_ERR_REP_DATA */
	__le32 dbg_rd_err_data[HBM_RD_ERR_DATA_LIFO_LEN];
};

struct hl_eq_hbm_sei_ca_par_intr_info {
	/* 14 LSBs */
	__le16 dbg_row[HBM_CA_ERR_CMD_LIFO_LEN];
	/* 18 LSBs */
	__le32 dbg_col[HBM_CA_ERR_CMD_LIFO_LEN];
};

#define WR_PAR_LAST_CMD_COL_SHIFT	0
#define WR_PAR_LAST_CMD_COL_MASK	0x3F
#define WR_PAR_LAST_CMD_BG_SHIFT	6
#define WR_PAR_LAST_CMD_BG_MASK		0xC0
#define WR_PAR_LAST_CMD_BA_SHIFT	8
#define WR_PAR_LAST_CMD_BA_MASK		0x300
#define WR_PAR_LAST_CMD_SID_SHIFT	10
#define WR_PAR_LAST_CMD_SID_MASK	0x400

/* Row address isn't latched */
struct hbm_sei_wr_cmd_address {
	/* DFI_DERR_LAST_CMD */
	union {
		struct {
			/* bit fields are only for FW use */
			u32 col:6;
			u32 bg:2;
			u32 ba:2;
			u32 sid:1;
			u32 reserved:21;
		};
		__le32 dbg_wr_cmd_addr;
	};
};

struct hl_eq_hbm_sei_wr_par_intr_info {
	/* entry 0: WR command address from the 1st cycle prior to the error
	 * entry 1: WR command address from the 2nd cycle prior to the error
	 * and so on...
	 */
	struct hbm_sei_wr_cmd_address dbg_last_wr_cmds[HBM_WR_PAR_CMD_LIFO_LEN];
	/* derr[0:1] - 1st HBM cycle DERR output
	 * derr[2:3] - 2nd HBM cycle DERR output
	 */
	__u8 dbg_derr;
	/* extend to reach 8B */
	__u8 pad[3];
};

/*
 * this struct represents the following sei causes:
 * command parity, ECC double error, ECC single error, dfi error, cattrip,
 * temperature read-out, read parity error and write parity error.
 * some only use the header while some have extra data.
 */
struct hl_eq_hbm_sei_data {
	struct hl_hbm_sei_header hdr;
	union {
		struct hl_eq_hbm_sei_ca_par_intr_info ca_parity_even_info;
		struct hl_eq_hbm_sei_ca_par_intr_info ca_parity_odd_info;
		struct hl_eq_hbm_sei_read_err_intr_info read_err_info;
		struct hl_eq_hbm_sei_wr_par_intr_info wr_parity_info;
	};
};

/* Engine/farm arc interrupt type */
enum hl_engine_arc_interrupt_type {
	/* Qman/farm ARC DCCM QUEUE FULL interrupt type */
	ENGINE_ARC_DCCM_QUEUE_FULL_IRQ = 1
};

/* Data structure specifies details of payload of DCCM QUEUE FULL interrupt */
struct hl_engine_arc_dccm_queue_full_irq {
	/* Queue index value which caused DCCM QUEUE FULL */
	__le32 queue_index;
	__le32 pad;
};

/* Data structure specifies details of QM/FARM ARC interrupt */
struct hl_eq_engine_arc_intr_data {
	/* ARC engine id e.g.  DCORE0_TPC0_QM_ARC, DCORE0_TCP1_QM_ARC */
	__le32 engine_id;
	__le32 intr_type; /* enum hl_engine_arc_interrupt_type */
	/* More info related to the interrupt e.g. queue index
	 * incase of DCCM_QUEUE_FULL interrupt.
	 */
	__le64 payload;
	__le64 pad[5];
};

#define ADDR_DEC_ADDRESS_COUNT_MAX 4

/* Data structure specifies details of ADDR_DEC interrupt */
struct hl_eq_addr_dec_intr_data {
	struct hl_eq_intr_cause intr_cause;
	__le64 addr[ADDR_DEC_ADDRESS_COUNT_MAX];
	__u8 addr_cnt;
	__u8 pad[7];
};

struct hl_eq_entry {
	struct hl_eq_header hdr;
	union {
		__le64 data_placeholder;
		struct hl_eq_ecc_data ecc_data;
		struct hl_eq_hbm_ecc_data hbm_ecc_data;	/* Obsolete */
		struct hl_eq_sm_sei_data sm_sei_data;
		struct cpucp_pkt_sync_err pkt_sync_err;
		struct hl_eq_fw_alive fw_alive;
		struct hl_eq_intr_cause intr_cause;
		struct hl_eq_pcie_drain_ind_data pcie_drain_ind_data;
		struct hl_eq_razwi_info razwi_info;
		struct hl_eq_razwi_with_intr_cause razwi_with_intr_cause;
		struct hl_eq_hbm_sei_data sei_data;	/* Gaudi2 HBM */
		struct hl_eq_engine_arc_intr_data arc_data;
		struct hl_eq_addr_dec_intr_data addr_dec;
		__le64 data[7];
	};
};

#define HL_EQ_ENTRY_SIZE		sizeof(struct hl_eq_entry)

#define EQ_CTL_READY_SHIFT		31
#define EQ_CTL_READY_MASK		0x80000000

#define EQ_CTL_EVENT_TYPE_SHIFT		16
#define EQ_CTL_EVENT_TYPE_MASK		0x0FFF0000

#define EQ_CTL_INDEX_SHIFT		0
#define EQ_CTL_INDEX_MASK		0x0000FFFF

enum pq_init_status {
	PQ_INIT_STATUS_NA = 0,
	PQ_INIT_STATUS_READY_FOR_CP,
	PQ_INIT_STATUS_READY_FOR_HOST,
	PQ_INIT_STATUS_READY_FOR_CP_SINGLE_MSI,
	PQ_INIT_STATUS_LEN_NOT_POWER_OF_TWO_ERR,
	PQ_INIT_STATUS_ILLEGAL_Q_ADDR_ERR
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
 * CPUCP_PACKET_PCIE_THROUGHPUT_GET -
 *       Get throughput of PCIe.
 *       The packet's arguments specify the transaction direction (TX/RX).
 *       The window measurement is 10[msec], and the return value is in KB/sec.
 *
 * CPUCP_PACKET_PCIE_REPLAY_CNT_GET
 *       Replay count measures number of "replay" events, which is basicly
 *       number of retries done by PCIe.
 *
 * CPUCP_PACKET_TOTAL_ENERGY_GET -
 *       Total Energy is measurement of energy from the time FW Linux
 *       is loaded. It is calculated by multiplying the average power
 *       by time (passed from armcp start). The units are in MilliJouls.
 *
 * CPUCP_PACKET_PLL_INFO_GET -
 *       Fetch frequencies of PLL from the required PLL IP.
 *       The packet's arguments specify the device PLL type
 *       Pll type is the PLL from device pll_index enum.
 *       The result is composed of 4 outputs, each is 16-bit
 *       frequency in MHz.
 *
 * CPUCP_PACKET_POWER_GET -
 *       Fetch the present power consumption of the device (Current * Voltage).
 *
 * CPUCP_PACKET_NIC_PFC_SET -
 *       Enable/Disable the NIC PFC feature. The packet's arguments specify the
 *       NIC port, relevant lanes to configure and one bit indication for
 *       enable/disable.
 *
 * CPUCP_PACKET_NIC_FAULT_GET -
 *       Fetch the current indication for local/remote faults from the NIC MAC.
 *       The result is 32-bit value of the relevant register.
 *
 * CPUCP_PACKET_NIC_LPBK_SET -
 *       Enable/Disable the MAC loopback feature. The packet's arguments specify
 *       the NIC port, relevant lanes to configure and one bit indication for
 *       enable/disable.
 *
 * CPUCP_PACKET_NIC_MAC_INIT -
 *       Configure the NIC MAC channels. The packet's arguments specify the
 *       NIC port and the speed.
 *
 * CPUCP_PACKET_MSI_INFO_SET -
 *       set the index number for each supported msi type going from
 *       host to device
 *
 * CPUCP_PACKET_NIC_XPCS91_REGS_GET -
 *       Fetch the un/correctable counters values from the NIC MAC.
 *
 * CPUCP_PACKET_NIC_STAT_REGS_GET -
 *       Fetch various NIC MAC counters from the NIC STAT.
 *
 * CPUCP_PACKET_NIC_STAT_REGS_CLR -
 *       Clear the various NIC MAC counters in the NIC STAT.
 *
 * CPUCP_PACKET_NIC_STAT_REGS_ALL_GET -
 *       Fetch all NIC MAC counters from the NIC STAT.
 *
 * CPUCP_PACKET_IS_IDLE_CHECK -
 *       Check if the device is IDLE in regard to the DMA/compute engines
 *       and QMANs. The f/w will return a bitmask where each bit represents
 *       a different engine or QMAN according to enum cpucp_idle_mask.
 *       The bit will be 1 if the engine is NOT idle.
 *
 * CPUCP_PACKET_HBM_REPLACED_ROWS_INFO_GET -
 *       Fetch all HBM replaced-rows and prending to be replaced rows data.
 *
 * CPUCP_PACKET_HBM_PENDING_ROWS_STATUS -
 *       Fetch status of HBM rows pending replacement and need a reboot to
 *       be replaced.
 *
 * CPUCP_PACKET_POWER_SET -
 *       Resets power history of device to 0
 *
 * CPUCP_PACKET_ENGINE_CORE_ASID_SET -
 *       Packet to perform engine core ASID configuration
 *
 * CPUCP_PACKET_SEC_ATTEST_GET -
 *       Get the attestaion data that is collected during various stages of the
 *       boot sequence. the attestation data is also hashed with some unique
 *       number (nonce) provided by the host to prevent replay attacks.
 *       public key and certificate also provided as part of the FW response.
 *
 * CPUCP_PACKET_MONITOR_DUMP_GET -
 *       Get monitors registers dump from the CpuCP kernel.
 *       The CPU will put the registers dump in the a buffer allocated by the driver
 *       which address is passed via the CpuCp packet. In addition, the host's driver
 *       passes the max size it allows the CpuCP to write to the structure, to prevent
 *       data corruption in case of mismatched driver/FW versions.
 *       Obsolete.
 *
 * CPUCP_PACKET_GENERIC_PASSTHROUGH -
 *      Generic opcode for all firmware info that is only passed to host
 *      through the LKD, without getting parsed there.
 *
 * CPUCP_PACKET_ACTIVE_STATUS_SET -
 *       LKD sends FW indication whether device is free or in use, this indication is reported
 *       also to the BMC.
 *
 * CPUCP_PACKET_REGISTER_INTERRUPTS -
 *       Packet to register interrupts indicating LKD is ready to receive events from FW.
 *
 * CPUCP_PACKET_SOFT_RESET -
 *	 Packet to perform soft-reset.
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
	CPUCP_PACKET_NIC_STATUS,		/* internal */
	CPUCP_PACKET_POWER_GET,			/* internal */
	CPUCP_PACKET_NIC_PFC_SET,		/* internal */
	CPUCP_PACKET_NIC_FAULT_GET,		/* internal */
	CPUCP_PACKET_NIC_LPBK_SET,		/* internal */
	CPUCP_PACKET_NIC_MAC_CFG,		/* internal */
	CPUCP_PACKET_MSI_INFO_SET,		/* internal */
	CPUCP_PACKET_NIC_XPCS91_REGS_GET,	/* internal */
	CPUCP_PACKET_NIC_STAT_REGS_GET,		/* internal */
	CPUCP_PACKET_NIC_STAT_REGS_CLR,		/* internal */
	CPUCP_PACKET_NIC_STAT_REGS_ALL_GET,	/* internal */
	CPUCP_PACKET_IS_IDLE_CHECK,		/* internal */
	CPUCP_PACKET_HBM_REPLACED_ROWS_INFO_GET,/* internal */
	CPUCP_PACKET_HBM_PENDING_ROWS_STATUS,	/* internal */
	CPUCP_PACKET_POWER_SET,			/* internal */
	CPUCP_PACKET_RESERVED,			/* not used */
	CPUCP_PACKET_ENGINE_CORE_ASID_SET,	/* internal */
	CPUCP_PACKET_RESERVED2,			/* not used */
	CPUCP_PACKET_SEC_ATTEST_GET,		/* internal */
	CPUCP_PACKET_RESERVED3,			/* not used */
	CPUCP_PACKET_RESERVED4,			/* not used */
	CPUCP_PACKET_MONITOR_DUMP_GET,		/* debugfs */
	CPUCP_PACKET_RESERVED5,			/* not used */
	CPUCP_PACKET_RESERVED6,			/* not used */
	CPUCP_PACKET_RESERVED7,			/* not used */
	CPUCP_PACKET_GENERIC_PASSTHROUGH,	/* IOCTL */
	CPUCP_PACKET_RESERVED8,			/* not used */
	CPUCP_PACKET_ACTIVE_STATUS_SET,		/* internal */
	CPUCP_PACKET_RESERVED9,			/* not used */
	CPUCP_PACKET_RESERVED10,		/* not used */
	CPUCP_PACKET_RESERVED11,		/* not used */
	CPUCP_PACKET_RESERVED12,		/* internal */
	CPUCP_PACKET_REGISTER_INTERRUPTS,	/* internal */
	CPUCP_PACKET_SOFT_RESET,		/* internal */
	CPUCP_PACKET_ID_MAX			/* must be last */
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

#define CPUCP_PKT_RES_EEPROM_OUT0_SHIFT	0
#define CPUCP_PKT_RES_EEPROM_OUT0_MASK	0x000000000000FFFFull
#define CPUCP_PKT_RES_EEPROM_OUT1_SHIFT	16
#define CPUCP_PKT_RES_EEPROM_OUT1_MASK	0x0000000000FF0000ull

#define CPUCP_PKT_VAL_PFC_IN1_SHIFT	0
#define CPUCP_PKT_VAL_PFC_IN1_MASK	0x0000000000000001ull
#define CPUCP_PKT_VAL_PFC_IN2_SHIFT	1
#define CPUCP_PKT_VAL_PFC_IN2_MASK	0x000000000000001Eull

#define CPUCP_PKT_VAL_LPBK_IN1_SHIFT	0
#define CPUCP_PKT_VAL_LPBK_IN1_MASK	0x0000000000000001ull
#define CPUCP_PKT_VAL_LPBK_IN2_SHIFT	1
#define CPUCP_PKT_VAL_LPBK_IN2_MASK	0x000000000000001Eull

#define CPUCP_PKT_VAL_MAC_CNT_IN1_SHIFT	0
#define CPUCP_PKT_VAL_MAC_CNT_IN1_MASK	0x0000000000000001ull
#define CPUCP_PKT_VAL_MAC_CNT_IN2_SHIFT	1
#define CPUCP_PKT_VAL_MAC_CNT_IN2_MASK	0x00000000FFFFFFFEull

/* heartbeat status bits */
#define CPUCP_PKT_HB_STATUS_EQ_FAULT_SHIFT		0
#define CPUCP_PKT_HB_STATUS_EQ_FAULT_MASK		0x00000001

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
			/*
			 * In legacy implemetations, i2c_len was not present,
			 * was unused and just added as pad.
			 * So if i2c_len is 0, it is treated as legacy
			 * and r/w 1 Byte, else if i2c_len is specified,
			 * its treated as new multibyte r/w support.
			 */
			__u8 i2c_len;
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

		/*
		 * For any general status bitmask. Shall be used whenever the
		 * result cannot be used to hold general purpose data.
		 */
		__le32 status_mask;

		/* random, used once number, for security packets */
		__le32 nonce;
	};

	union {
		/* For NIC requests */
		__le32 port_index;

		/* For Generic packet sub index */
		__le32 pkt_subidx;
	};
};

struct cpucp_unmask_irq_arr_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 irqs[];
};

struct cpucp_nic_status_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 data[];
};

struct cpucp_array_data_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 data[];
};

enum cpucp_led_index {
	CPUCP_LED0_INDEX = 0,
	CPUCP_LED1_INDEX,
	CPUCP_LED2_INDEX,
	CPUCP_LED_MAX_INDEX = CPUCP_LED2_INDEX
};

/*
 * enum cpucp_packet_rc - Error return code
 * @cpucp_packet_success	-> in case of success.
 * @cpucp_packet_invalid	-> this is to support first generation platforms.
 * @cpucp_packet_fault		-> in case of processing error like failing to
 *                                 get device binding or semaphore etc.
 * @cpucp_packet_invalid_pkt	-> when cpucp packet is un-supported.
 * @cpucp_packet_invalid_params	-> when checking parameter like length of buffer
 *				   or attribute value etc.
 * @cpucp_packet_rc_max		-> It indicates size of enum so should be at last.
 */
enum cpucp_packet_rc {
	cpucp_packet_success,
	cpucp_packet_invalid,
	cpucp_packet_fault,
	cpucp_packet_invalid_pkt,
	cpucp_packet_invalid_params,
	cpucp_packet_rc_max
};

/*
 * cpucp_temp_type should adhere to hwmon_temp_attributes
 * defined in Linux kernel hwmon.h file
 */
enum cpucp_temp_type {
	cpucp_temp_input,
	cpucp_temp_min = 4,
	cpucp_temp_min_hyst,
	cpucp_temp_max = 6,
	cpucp_temp_max_hyst,
	cpucp_temp_crit,
	cpucp_temp_crit_hyst,
	cpucp_temp_offset = 19,
	cpucp_temp_lowest = 21,
	cpucp_temp_highest = 22,
	cpucp_temp_reset_history = 23,
	cpucp_temp_warn = 24,
	cpucp_temp_max_crit = 25,
	cpucp_temp_max_warn = 26,
};

enum cpucp_in_attributes {
	cpucp_in_input,
	cpucp_in_min,
	cpucp_in_max,
	cpucp_in_lowest = 6,
	cpucp_in_highest = 7,
	cpucp_in_reset_history,
	cpucp_in_intr_alarm_a,
	cpucp_in_intr_alarm_b,
};

enum cpucp_curr_attributes {
	cpucp_curr_input,
	cpucp_curr_min,
	cpucp_curr_max,
	cpucp_curr_lowest = 6,
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

/*
 * cpucp_power_type aligns with hwmon_power_attributes
 * defined in Linux kernel hwmon.h file
 */
enum cpucp_power_type {
	CPUCP_POWER_INPUT = 8,
	CPUCP_POWER_INPUT_HIGHEST = 9,
	CPUCP_POWER_RESET_INPUT_HISTORY = 11
};

/*
 * MSI type enumeration table for all ASICs and future SW versions.
 * For future ASIC-LKD compatibility, we can only add new enumerations.
 * at the end of the table (before CPUCP_NUM_OF_MSI_TYPES).
 * Changing the order of entries or removing entries is not allowed.
 */
enum cpucp_msi_type {
	CPUCP_EVENT_QUEUE_MSI_TYPE,
	CPUCP_NIC_PORT1_MSI_TYPE,
	CPUCP_NIC_PORT3_MSI_TYPE,
	CPUCP_NIC_PORT5_MSI_TYPE,
	CPUCP_NIC_PORT7_MSI_TYPE,
	CPUCP_NIC_PORT9_MSI_TYPE,
	CPUCP_NUM_OF_MSI_TYPES
};

/*
 * PLL enumeration table used for all ASICs and future SW versions.
 * For future ASIC-LKD compatibility, we can only add new enumerations.
 * at the end of the table.
 * Changing the order of entries or removing entries is not allowed.
 */
enum pll_index {
	CPU_PLL = 0,
	PCI_PLL = 1,
	NIC_PLL = 2,
	DMA_PLL = 3,
	MESH_PLL = 4,
	MME_PLL = 5,
	TPC_PLL = 6,
	IF_PLL = 7,
	SRAM_PLL = 8,
	NS_PLL = 9,
	HBM_PLL = 10,
	MSS_PLL = 11,
	DDR_PLL = 12,
	VID_PLL = 13,
	BANK_PLL = 14,
	MMU_PLL = 15,
	IC_PLL = 16,
	MC_PLL = 17,
	EMMC_PLL = 18,
	D2D_PLL = 19,
	CS_PLL = 20,
	C2C_PLL = 21,
	NCH_PLL = 22,
	C2M_PLL = 23,
	PLL_MAX
};

enum rl_index {
	TPC_RL = 0,
	MME_RL,
	EDMA_RL,
};

enum pvt_index {
	PVT_SW,
	PVT_SE,
	PVT_NW,
	PVT_NE
};

/* Event Queue Packets */

struct eq_generic_event {
	__le64 data[7];
};

/*
 * CpuCP info
 */

#define CARD_NAME_MAX_LEN		16
#define CPUCP_MAX_SENSORS		128
#define CPUCP_MAX_NICS			128
#define CPUCP_LANES_PER_NIC		4
#define CPUCP_NIC_QSFP_EEPROM_MAX_LEN	1024
#define CPUCP_MAX_NIC_LANES		(CPUCP_MAX_NICS * CPUCP_LANES_PER_NIC)
#define CPUCP_NIC_MASK_ARR_LEN		((CPUCP_MAX_NICS + 63) / 64)
#define CPUCP_NIC_POLARITY_ARR_LEN	((CPUCP_MAX_NIC_LANES + 63) / 64)
#define CPUCP_HBM_ROW_REPLACE_MAX	32

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
 * @infineon_second_stage_version: Infineon 2nd stage DC-DC version.
 * @dram_size: available DRAM size.
 * @card_name: card name that will be displayed in HWMON subsystem on the host
 * @tpc_binning_mask: TPC binning mask, 1 bit per TPC instance
 *                    (0 = functional, 1 = binned)
 * @decoder_binning_mask: Decoder binning mask, 1 bit per decoder instance
 *                        (0 = functional, 1 = binned), maximum 1 per dcore
 * @sram_binning: Categorize SRAM functionality
 *                (0 = fully functional, 1 = lower-half is not functional,
 *                 2 = upper-half is not functional)
 * @sec_info: security information
 * @pll_map: Bit map of supported PLLs for current ASIC version.
 * @mme_binning_mask: MME binning mask,
 *                    bits [0:6]   <==> dcore0 mme fma
 *                    bits [7:13]  <==> dcore1 mme fma
 *                    bits [14:20] <==> dcore0 mme ima
 *                    bits [21:27] <==> dcore1 mme ima
 *                    For each group, if the 6th bit is set then first 5 bits
 *                    represent the col's idx [0-31], otherwise these bits are
 *                    ignored, and col idx 32 is binned. 7th bit is don't care.
 * @dram_binning_mask: DRAM binning mask, 1 bit per dram instance
 *                     (0 = functional 1 = binned)
 * @memory_repair_flag: eFuse flag indicating memory repair
 * @edma_binning_mask: EDMA binning mask, 1 bit per EDMA instance
 *                     (0 = functional 1 = binned)
 * @xbar_binning_mask: Xbar binning mask, 1 bit per Xbar instance
 *                     (0 = functional 1 = binned)
 * @interposer_version: Interposer version programmed in eFuse
 * @substrate_version: Substrate version programmed in eFuse
 * @fw_hbm_region_size: Size in bytes of FW reserved region in HBM.
 * @fw_os_version: Firmware OS Version
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
	__le32 infineon_second_stage_version;
	__le64 dram_size;
	char card_name[CARD_NAME_MAX_LEN];
	__le64 tpc_binning_mask;
	__le64 decoder_binning_mask;
	__u8 sram_binning;
	__u8 dram_binning_mask;
	__u8 memory_repair_flag;
	__u8 edma_binning_mask;
	__u8 xbar_binning_mask;
	__u8 interposer_version;
	__u8 substrate_version;
	__u8 reserved2;
	struct cpucp_security_info sec_info;
	__le32 fw_hbm_region_size;
	__u8 pll_map[PLL_MAP_LEN];
	__le64 mme_binning_mask;
	__u8 fw_os_version[VERSION_MAX_LEN];
};

struct cpucp_mac_addr {
	__u8 mac_addr[ETH_ALEN];
};

enum cpucp_serdes_type {
	TYPE_1_SERDES_TYPE,
	TYPE_2_SERDES_TYPE,
	HLS1_SERDES_TYPE,
	HLS1H_SERDES_TYPE,
	HLS2_SERDES_TYPE,
	HLS2_TYPE_1_SERDES_TYPE,
	MAX_NUM_SERDES_TYPE,		/* number of types */
	UNKNOWN_SERDES_TYPE = 0xFFFF	/* serdes_type is u16 */
};

struct cpucp_nic_info {
	struct cpucp_mac_addr mac_addrs[CPUCP_MAX_NICS];
	__le64 link_mask[CPUCP_NIC_MASK_ARR_LEN];
	__le64 pol_tx_mask[CPUCP_NIC_POLARITY_ARR_LEN];
	__le64 pol_rx_mask[CPUCP_NIC_POLARITY_ARR_LEN];
	__le64 link_ext_mask[CPUCP_NIC_MASK_ARR_LEN];
	__u8 qsfp_eeprom[CPUCP_NIC_QSFP_EEPROM_MAX_LEN];
	__le64 auto_neg_mask[CPUCP_NIC_MASK_ARR_LEN];
	__le16 serdes_type; /* enum cpucp_serdes_type */
	__le16 tx_swap_map[CPUCP_MAX_NICS];
	__u8 reserved[6];
};

#define PAGE_DISCARD_MAX	64

struct page_discard_info {
	__u8 num_entries;
	__u8 reserved[7];
	__le32 mmu_page_idx[PAGE_DISCARD_MAX];
};

/*
 * struct frac_val - fracture value represented by "integer.frac".
 * @integer: the integer part of the fracture value;
 * @frac: the fracture part of the fracture value.
 */
struct frac_val {
	union {
		struct {
			__le16 integer;
			__le16 frac;
		};
		__le32 val;
	};
};

/*
 * struct ser_val - the SER (symbol error rate) value is represented by "integer * 10 ^ -exp".
 * @integer: the integer part of the SER value;
 * @exp: the exponent part of the SER value.
 */
struct ser_val {
	__le16 integer;
	__le16 exp;
};

/*
 * struct cpucp_nic_status - describes the status of a NIC port.
 * @port: NIC port index.
 * @bad_format_cnt: e.g. CRC.
 * @responder_out_of_sequence_psn_cnt: e.g NAK.
 * @high_ber_reinit_cnt: link reinit due to high BER.
 * @correctable_err_cnt: e.g. bit-flip.
 * @uncorrectable_err_cnt: e.g. MAC errors.
 * @retraining_cnt: re-training counter.
 * @up: is port up.
 * @pcs_link: has PCS link.
 * @phy_ready: is PHY ready.
 * @auto_neg: is Autoneg enabled.
 * @timeout_retransmission_cnt: timeout retransmission events.
 * @high_ber_cnt: high ber events.
 * @pre_fec_ser: pre FEC SER value.
 * @post_fec_ser: post FEC SER value.
 * @throughput: measured throughput.
 * @latency: measured latency.
 */
struct cpucp_nic_status {
	__le32 port;
	__le32 bad_format_cnt;
	__le32 responder_out_of_sequence_psn_cnt;
	__le32 high_ber_reinit;
	__le32 correctable_err_cnt;
	__le32 uncorrectable_err_cnt;
	__le32 retraining_cnt;
	__u8 up;
	__u8 pcs_link;
	__u8 phy_ready;
	__u8 auto_neg;
	__le32 timeout_retransmission_cnt;
	__le32 high_ber_cnt;
	struct ser_val pre_fec_ser;
	struct ser_val post_fec_ser;
	struct frac_val bandwidth;
	struct frac_val lat;
};

enum cpucp_hbm_row_replace_cause {
	REPLACE_CAUSE_DOUBLE_ECC_ERR,
	REPLACE_CAUSE_MULTI_SINGLE_ECC_ERR,
};

struct cpucp_hbm_row_info {
	__u8 hbm_idx;
	__u8 pc;
	__u8 sid;
	__u8 bank_idx;
	__le16 row_addr;
	__u8 replaced_row_cause; /* enum cpucp_hbm_row_replace_cause */
	__u8 pad;
};

struct cpucp_hbm_row_replaced_rows_info {
	__le16 num_replaced_rows;
	__u8 pad[6];
	struct cpucp_hbm_row_info replaced_rows[CPUCP_HBM_ROW_REPLACE_MAX];
};

enum cpu_reset_status {
	CPU_RST_STATUS_NA = 0,
	CPU_RST_STATUS_SOFT_RST_DONE = 1,
};

#define SEC_PCR_DATA_BUF_SZ	256
#define SEC_PCR_QUOTE_BUF_SZ	510	/* (512 - 2) 2 bytes used for size */
#define SEC_SIGNATURE_BUF_SZ	255	/* (256 - 1) 1 byte used for size */
#define SEC_PUB_DATA_BUF_SZ	510	/* (512 - 2) 2 bytes used for size */
#define SEC_CERTIFICATE_BUF_SZ	2046	/* (2048 - 2) 2 bytes used for size */

/*
 * struct cpucp_sec_attest_info - attestation report of the boot
 * @pcr_data: raw values of the PCR registers
 * @pcr_num_reg: number of PCR registers in the pcr_data array
 * @pcr_reg_len: length of each PCR register in the pcr_data array (bytes)
 * @nonce: number only used once. random number provided by host. this also
 *	    passed to the quote command as a qualifying data.
 * @pcr_quote_len: length of the attestation quote data (bytes)
 * @pcr_quote: attestation report data structure
 * @quote_sig_len: length of the attestation report signature (bytes)
 * @quote_sig: signature structure of the attestation report
 * @pub_data_len: length of the public data (bytes)
 * @public_data: public key for the signed attestation
 *		 (outPublic + name + qualifiedName)
 * @certificate_len: length of the certificate (bytes)
 * @certificate: certificate for the attestation signing key
 */
struct cpucp_sec_attest_info {
	__u8 pcr_data[SEC_PCR_DATA_BUF_SZ];
	__u8 pcr_num_reg;
	__u8 pcr_reg_len;
	__le16 pad0;
	__le32 nonce;
	__le16 pcr_quote_len;
	__u8 pcr_quote[SEC_PCR_QUOTE_BUF_SZ];
	__u8 quote_sig_len;
	__u8 quote_sig[SEC_SIGNATURE_BUF_SZ];
	__le16 pub_data_len;
	__u8 public_data[SEC_PUB_DATA_BUF_SZ];
	__le16 certificate_len;
	__u8 certificate[SEC_CERTIFICATE_BUF_SZ];
};

/*
 * struct cpucp_dev_info_signed - device information signed by a secured device
 * @info: device information structure as defined above
 * @nonce: number only used once. random number provided by host. this number is
 *	   hashed and signed along with the device information.
 * @info_sig_len: length of the attestation signature (bytes)
 * @info_sig: signature of the info + nonce data.
 * @pub_data_len: length of the public data (bytes)
 * @public_data: public key info signed info data
 *		 (outPublic + name + qualifiedName)
 * @certificate_len: length of the certificate (bytes)
 * @certificate: certificate for the signing key
 */
struct cpucp_dev_info_signed {
	struct cpucp_info info;	/* assumed to be 64bit aligned */
	__le32 nonce;
	__le32 pad0;
	__u8 info_sig_len;
	__u8 info_sig[SEC_SIGNATURE_BUF_SZ];
	__le16 pub_data_len;
	__u8 public_data[SEC_PUB_DATA_BUF_SZ];
	__le16 certificate_len;
	__u8 certificate[SEC_CERTIFICATE_BUF_SZ];
};

#define DCORE_MON_REGS_SZ	512
/*
 * struct dcore_monitor_regs_data - DCORE monitor regs data.
 * the structure follows sync manager block layout. Obsolete.
 * @mon_pay_addrl: array of payload address low bits.
 * @mon_pay_addrh: array of payload address high bits.
 * @mon_pay_data: array of payload data.
 * @mon_arm: array of monitor arm.
 * @mon_status: array of monitor status.
 */
struct dcore_monitor_regs_data {
	__le32 mon_pay_addrl[DCORE_MON_REGS_SZ];
	__le32 mon_pay_addrh[DCORE_MON_REGS_SZ];
	__le32 mon_pay_data[DCORE_MON_REGS_SZ];
	__le32 mon_arm[DCORE_MON_REGS_SZ];
	__le32 mon_status[DCORE_MON_REGS_SZ];
};

/* contains SM data for each SYNC_MNGR (Obsolete) */
struct cpucp_monitor_dump {
	struct dcore_monitor_regs_data sync_mngr_w_s;
	struct dcore_monitor_regs_data sync_mngr_e_s;
	struct dcore_monitor_regs_data sync_mngr_w_n;
	struct dcore_monitor_regs_data sync_mngr_e_n;
};

/*
 * The Type of the generic request (and other input arguments) will be fetched from user by reading
 * from "pkt_subidx" field in struct cpucp_packet.
 *
 * HL_PASSTHROUGHT_VERSIONS	- Fetch all firmware versions.
 */
enum hl_passthrough_type {
	HL_PASSTHROUGH_VERSIONS,
};

#endif /* CPUCP_IF_H */
