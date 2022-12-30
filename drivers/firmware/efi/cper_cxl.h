/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UEFI Common Platform Error Record (CPER) support for CXL Section.
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 *
 * Author: Smita Koralahalli <Smita.KoralahalliChannabasappa@amd.com>
 */

#ifndef LINUX_CPER_CXL_H
#define LINUX_CPER_CXL_H

/* CXL Protocol Error Section */
#define CPER_SEC_CXL_PROT_ERR						\
	GUID_INIT(0x80B9EFB4, 0x52B5, 0x4DE3, 0xA7, 0x77, 0x68, 0x78,	\
		  0x4B, 0x77, 0x10, 0x48)

#pragma pack(1)

/* Compute Express Link Protocol Error Section, UEFI v2.10 sec N.2.13 */
struct cper_sec_prot_err {
	u64 valid_bits;
	u8 agent_type;
	u8 reserved[7];

	/*
	 * Except for RCH Downstream Port, all the remaining CXL Agent
	 * types are uniquely identified by the PCIe compatible SBDF number.
	 */
	union {
		u64 rcrb_base_addr;
		struct {
			u8 function;
			u8 device;
			u8 bus;
			u16 segment;
			u8 reserved_1[3];
		};
	} agent_addr;

	struct {
		u16 vendor_id;
		u16 device_id;
		u16 subsystem_vendor_id;
		u16 subsystem_id;
		u8 class_code[2];
		u16 slot;
		u8 reserved_1[4];
	} device_id;

	struct {
		u32 lower_dw;
		u32 upper_dw;
	} dev_serial_num;

	u8 capability[60];
	u16 dvsec_len;
	u16 err_len;
	u8 reserved_2[4];
};

#pragma pack()

void cper_print_prot_err(const char *pfx, const struct cper_sec_prot_err *prot_err);

#endif //__CPER_CXL_
