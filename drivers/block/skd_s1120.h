/* Copyright 2012 STEC, Inc.
 *
 * This file is licensed under the terms of the 3-clause
 * BSD License (http://opensource.org/licenses/BSD-3-Clause)
 * or the GNU GPL-2.0 (http://www.gnu.org/licenses/gpl-2.0.html),
 * at your option. Both licenses are also available in the LICENSE file
 * distributed with this project. This file may not be copied, modified,
 * or distributed except in accordance with those terms.
 */


#ifndef SKD_S1120_H
#define SKD_S1120_H

#pragma pack(push, s1120_h, 1)

/*
 * Q-channel, 64-bit r/w
 */
#define FIT_Q_COMMAND			0x400u
#define FIT_QCMD_QID_MASK		(0x3 << 1)
#define  FIT_QCMD_QID0			(0x0 << 1)
#define  FIT_QCMD_QID_NORMAL		FIT_QCMD_QID0
#define  FIT_QCMD_QID1			(0x1 << 1)
#define  FIT_QCMD_QID2			(0x2 << 1)
#define  FIT_QCMD_QID3			(0x3 << 1)
#define  FIT_QCMD_FLUSH_QUEUE		(0ull)	/* add QID */
#define  FIT_QCMD_MSGSIZE_MASK		(0x3 << 4)
#define  FIT_QCMD_MSGSIZE_64		(0x0 << 4)
#define  FIT_QCMD_MSGSIZE_128		(0x1 << 4)
#define  FIT_QCMD_MSGSIZE_256		(0x2 << 4)
#define  FIT_QCMD_MSGSIZE_512		(0x3 << 4)
#define  FIT_QCMD_BASE_ADDRESS_MASK	(0xFFFFFFFFFFFFFFC0ull)

/*
 * Control, 32-bit r/w
 */
#define FIT_CONTROL			0x500u
#define  FIT_CR_HARD_RESET		(1u << 0u)
#define  FIT_CR_SOFT_RESET		(1u << 1u)
#define  FIT_CR_DIS_TIMESTAMPS		(1u << 6u)
#define  FIT_CR_ENABLE_INTERRUPTS	(1u << 7u)

/*
 * Status, 32-bit, r/o
 */
#define FIT_STATUS			0x510u
#define FIT_SR_DRIVE_STATE_MASK		0x000000FFu
#define	FIT_SR_SIGNATURE		(0xFF << 8)
#define	FIT_SR_PIO_DMA			(1 << 16)
#define FIT_SR_DRIVE_OFFLINE		0x00
#define FIT_SR_DRIVE_INIT		0x01
/* #define FIT_SR_DRIVE_READY		0x02 */
#define FIT_SR_DRIVE_ONLINE		0x03
#define FIT_SR_DRIVE_BUSY		0x04
#define FIT_SR_DRIVE_FAULT		0x05
#define FIT_SR_DRIVE_DEGRADED		0x06
#define FIT_SR_PCIE_LINK_DOWN		0x07
#define FIT_SR_DRIVE_SOFT_RESET		0x08
#define FIT_SR_DRIVE_INIT_FAULT		0x09
#define FIT_SR_DRIVE_BUSY_SANITIZE	0x0A
#define FIT_SR_DRIVE_BUSY_ERASE		0x0B
#define FIT_SR_DRIVE_FW_BOOTING		0x0C
#define FIT_SR_DRIVE_NEED_FW_DOWNLOAD	0xFE
#define FIT_SR_DEVICE_MISSING		0xFF
#define FIT_SR__RESERVED		0xFFFFFF00u

/*
 * FIT_STATUS - Status register data definition
 */
#define FIT_SR_STATE_MASK		(0xFF << 0)
#define FIT_SR_SIGNATURE		(0xFF << 8)
#define FIT_SR_PIO_DMA			(1 << 16)

/*
 * Interrupt status, 32-bit r/w1c (w1c ==> write 1 to clear)
 */
#define FIT_INT_STATUS_HOST		0x520u
#define  FIT_ISH_FW_STATE_CHANGE	(1u << 0u)
#define  FIT_ISH_COMPLETION_POSTED	(1u << 1u)
#define  FIT_ISH_MSG_FROM_DEV		(1u << 2u)
#define  FIT_ISH_UNDEFINED_3		(1u << 3u)
#define  FIT_ISH_UNDEFINED_4		(1u << 4u)
#define  FIT_ISH_Q0_FULL		(1u << 5u)
#define  FIT_ISH_Q1_FULL		(1u << 6u)
#define  FIT_ISH_Q2_FULL		(1u << 7u)
#define  FIT_ISH_Q3_FULL		(1u << 8u)
#define  FIT_ISH_QCMD_FIFO_OVERRUN	(1u << 9u)
#define  FIT_ISH_BAD_EXP_ROM_READ	(1u << 10u)

#define FIT_INT_DEF_MASK \
	(FIT_ISH_FW_STATE_CHANGE | \
	 FIT_ISH_COMPLETION_POSTED | \
	 FIT_ISH_MSG_FROM_DEV | \
	 FIT_ISH_Q0_FULL | \
	 FIT_ISH_Q1_FULL | \
	 FIT_ISH_Q2_FULL | \
	 FIT_ISH_Q3_FULL | \
	 FIT_ISH_QCMD_FIFO_OVERRUN | \
	 FIT_ISH_BAD_EXP_ROM_READ)

#define FIT_INT_QUEUE_FULL \
	(FIT_ISH_Q0_FULL | \
	 FIT_ISH_Q1_FULL | \
	 FIT_ISH_Q2_FULL | \
	 FIT_ISH_Q3_FULL)

#define MSI_MSG_NWL_ERROR_0		0x00000000
#define MSI_MSG_NWL_ERROR_1		0x00000001
#define MSI_MSG_NWL_ERROR_2		0x00000002
#define MSI_MSG_NWL_ERROR_3		0x00000003
#define MSI_MSG_STATE_CHANGE		0x00000004
#define MSI_MSG_COMPLETION_POSTED	0x00000005
#define MSI_MSG_MSG_FROM_DEV		0x00000006
#define MSI_MSG_RESERVED_0		0x00000007
#define MSI_MSG_RESERVED_1		0x00000008
#define MSI_MSG_QUEUE_0_FULL		0x00000009
#define MSI_MSG_QUEUE_1_FULL		0x0000000A
#define MSI_MSG_QUEUE_2_FULL		0x0000000B
#define MSI_MSG_QUEUE_3_FULL		0x0000000C

#define FIT_INT_RESERVED_MASK \
	(FIT_ISH_UNDEFINED_3 | \
	 FIT_ISH_UNDEFINED_4)

/*
 * Interrupt mask, 32-bit r/w
 * Bit definitions are the same as FIT_INT_STATUS_HOST
 */
#define FIT_INT_MASK_HOST		0x528u

/*
 * Message to device, 32-bit r/w
 */
#define FIT_MSG_TO_DEVICE		0x540u

/*
 * Message from device, 32-bit, r/o
 */
#define FIT_MSG_FROM_DEVICE		0x548u

/*
 * 32-bit messages to/from device, composition/extraction macros
 */
#define FIT_MXD_CONS(TYPE, PARAM, DATA) \
	((((TYPE)  & 0xFFu) << 24u) | \
	(((PARAM) & 0xFFu) << 16u) | \
	(((DATA)  & 0xFFFFu) << 0u))
#define FIT_MXD_TYPE(MXD)		(((MXD) >> 24u) & 0xFFu)
#define FIT_MXD_PARAM(MXD)		(((MXD) >> 16u) & 0xFFu)
#define FIT_MXD_DATA(MXD)		(((MXD) >> 0u) & 0xFFFFu)

/*
 * Types of messages to/from device
 */
#define FIT_MTD_FITFW_INIT		0x01u
#define FIT_MTD_GET_CMDQ_DEPTH		0x02u
#define FIT_MTD_SET_COMPQ_DEPTH		0x03u
#define FIT_MTD_SET_COMPQ_ADDR		0x04u
#define FIT_MTD_ARM_QUEUE		0x05u
#define FIT_MTD_CMD_LOG_HOST_ID		0x07u
#define FIT_MTD_CMD_LOG_TIME_STAMP_LO	0x08u
#define FIT_MTD_CMD_LOG_TIME_STAMP_HI	0x09u
#define FIT_MFD_SMART_EXCEEDED		0x10u
#define FIT_MFD_POWER_DOWN		0x11u
#define FIT_MFD_OFFLINE			0x12u
#define FIT_MFD_ONLINE			0x13u
#define FIT_MFD_FW_RESTARTING		0x14u
#define FIT_MFD_PM_ACTIVE		0x15u
#define FIT_MFD_PM_STANDBY		0x16u
#define FIT_MFD_PM_SLEEP		0x17u
#define FIT_MFD_CMD_PROGRESS		0x18u

#define FIT_MTD_DEBUG			0xFEu
#define FIT_MFD_DEBUG			0xFFu

#define FIT_MFD_MASK			(0xFFu)
#define FIT_MFD_DATA_MASK		(0xFFu)
#define FIT_MFD_MSG(x)			(((x) >> 24) & FIT_MFD_MASK)
#define FIT_MFD_DATA(x)			((x) & FIT_MFD_MASK)

/*
 * Extra arg to FIT_MSG_TO_DEVICE, 64-bit r/w
 * Used to set completion queue address (FIT_MTD_SET_COMPQ_ADDR)
 * (was Response buffer in docs)
 */
#define FIT_MSG_TO_DEVICE_ARG		0x580u

/*
 * Hardware (ASIC) version, 32-bit r/o
 */
#define FIT_HW_VERSION			0x588u

/*
 * Scatter/gather list descriptor.
 * 32-bytes and must be aligned on a 32-byte boundary.
 * All fields are in little endian order.
 */
struct fit_sg_descriptor {
	uint32_t control;
	uint32_t byte_count;
	uint64_t host_side_addr;
	uint64_t dev_side_addr;
	uint64_t next_desc_ptr;
};

#define FIT_SGD_CONTROL_NOT_LAST	0x000u
#define FIT_SGD_CONTROL_LAST		0x40Eu

/*
 * Header at the beginning of a FIT message. The header
 * is followed by SSDI requests each 64 bytes.
 * A FIT message can be up to 512 bytes long and must start
 * on a 64-byte boundary.
 */
struct fit_msg_hdr {
	uint8_t protocol_id;
	uint8_t num_protocol_cmds_coalesced;
	uint8_t _reserved[62];
};

#define FIT_PROTOCOL_ID_FIT	1
#define FIT_PROTOCOL_ID_SSDI	2
#define FIT_PROTOCOL_ID_SOFIT	3


#define FIT_PROTOCOL_MINOR_VER(mtd_val) ((mtd_val >> 16) & 0xF)
#define FIT_PROTOCOL_MAJOR_VER(mtd_val) ((mtd_val >> 20) & 0xF)

/*
 * Format of a completion entry. The completion queue is circular
 * and must have at least as many entries as the maximum number
 * of commands that may be issued to the device.
 *
 * There are no head/tail pointers. The cycle value is used to
 * infer the presence of new completion records.
 * Initially the cycle in all entries is 0, the index is 0, and
 * the cycle value to expect is 1. When completions are added
 * their cycle values are set to 1. When the index wraps the
 * cycle value to expect is incremented.
 *
 * Command_context is opaque and taken verbatim from the SSDI command.
 * All other fields are big endian.
 */
#define FIT_PROTOCOL_VERSION_0		0

/*
 *  Protocol major version 1 completion entry.
 *  The major protocol version is found in bits
 *  20-23 of the FIT_MTD_FITFW_INIT response.
 */
struct fit_completion_entry_v1 {
	uint32_t	num_returned_bytes;
	uint16_t	tag;
	uint8_t		status;  /* SCSI status */
	uint8_t		cycle;
};
#define FIT_PROTOCOL_VERSION_1		1
#define FIT_PROTOCOL_VERSION_CURRENT	FIT_PROTOCOL_VERSION_1

struct fit_comp_error_info {
	uint8_t		type:7; /* 00: Bits0-6 indicates the type of sense data. */
	uint8_t		valid:1; /* 00: Bit 7 := 1 ==> info field is valid. */
	uint8_t		reserved0; /* 01: Obsolete field */
	uint8_t		key:4; /* 02: Bits0-3 indicate the sense key. */
	uint8_t		reserved2:1; /* 02: Reserved bit. */
	uint8_t		bad_length:1; /* 02: Incorrect Length Indicator */
	uint8_t		end_medium:1; /* 02: End of Medium */
	uint8_t		file_mark:1; /* 02: Filemark */
	uint8_t		info[4]; /* 03: */
	uint8_t		reserved1; /* 07: Additional Sense Length */
	uint8_t		cmd_spec[4]; /* 08: Command Specific Information */
	uint8_t		code; /* 0C: Additional Sense Code */
	uint8_t		qual; /* 0D: Additional Sense Code Qualifier */
	uint8_t		fruc; /* 0E: Field Replaceable Unit Code */
	uint8_t		sks_high:7; /* 0F: Sense Key Specific (MSB) */
	uint8_t		sks_valid:1; /* 0F: Sense Key Specific Valid */
	uint16_t	sks_low; /* 10: Sense Key Specific (LSW) */
	uint16_t	reserved3; /* 12: Part of additional sense bytes (unused) */
	uint16_t	uec; /* 14: Additional Sense Bytes */
	uint64_t	per; /* 16: Additional Sense Bytes */
	uint8_t		reserved4[2]; /* 1E: Additional Sense Bytes (unused) */
};


/* Task management constants */
#define SOFT_TASK_SIMPLE		0x00
#define SOFT_TASK_HEAD_OF_QUEUE		0x01
#define SOFT_TASK_ORDERED		0x02

/* Version zero has the last 32 bits reserved,
 * Version one has the last 32 bits sg_list_len_bytes;
 */
struct skd_command_header {
	uint64_t	sg_list_dma_address;
	uint16_t	tag;
	uint8_t		attribute;
	uint8_t		add_cdb_len;     /* In 32 bit words */
	uint32_t	sg_list_len_bytes;
};

struct skd_scsi_request {
	struct		skd_command_header hdr;
	unsigned char	cdb[16];
/*	unsigned char _reserved[16]; */
};

struct driver_inquiry_data {
	uint8_t		peripheral_device_type:5;
	uint8_t		qualifier:3;
	uint8_t		page_code;
	uint16_t	page_length;
	uint16_t	pcie_bus_number;
	uint8_t		pcie_device_number;
	uint8_t		pcie_function_number;
	uint8_t		pcie_link_speed;
	uint8_t		pcie_link_lanes;
	uint16_t	pcie_vendor_id;
	uint16_t	pcie_device_id;
	uint16_t	pcie_subsystem_vendor_id;
	uint16_t	pcie_subsystem_device_id;
	uint8_t		reserved1[2];
	uint8_t		reserved2[3];
	uint8_t		driver_version_length;
	uint8_t		driver_version[0x14];
};

#pragma pack(pop, s1120_h)

#endif /* SKD_S1120_H */
