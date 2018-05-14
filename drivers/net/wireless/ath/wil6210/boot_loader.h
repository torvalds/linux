/* Copyright (c) 2015 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* This file contains the definitions for the boot loader
 * for the Qualcomm "Sparrow" 60 Gigabit wireless solution.
 */
#ifndef BOOT_LOADER_EXPORT_H_
#define BOOT_LOADER_EXPORT_H_

struct bl_dedicated_registers_v1 {
	__le32	boot_loader_ready;		/* 0x880A3C driver will poll
						 * this Dword until BL will
						 * set it to 1 (initial value
						 * should be 0)
						 */
	__le32	boot_loader_struct_version;	/* 0x880A40 BL struct ver. */
	__le16	rf_type;			/* 0x880A44 connected RF ID */
	__le16	rf_status;			/* 0x880A46 RF status,
						 * 0 is OK else error
						 */
	__le32	baseband_type;			/* 0x880A48 board type ID */
	u8	mac_address[6];			/* 0x880A4c BL mac address */
	u8	bl_version_major;		/* 0x880A52 BL ver. major */
	u8	bl_version_minor;		/* 0x880A53 BL ver. minor */
	__le16	bl_version_subminor;		/* 0x880A54 BL ver. subminor */
	__le16	bl_version_build;		/* 0x880A56 BL ver. build */
	/* valid only for version 2 and above */
	__le32  bl_assert_code;         /* 0x880A58 BL Assert code */
	__le32  bl_assert_blink;        /* 0x880A5C BL Assert Branch */
	__le32  bl_shutdown_handshake;  /* 0x880A60 BL cleaner shutdown */
	__le32  bl_reserved[21];        /* 0x880A64 - 0x880AB4 */
	__le32  bl_magic_number;        /* 0x880AB8 BL Magic number */
} __packed;

/* the following struct is the version 0 struct */

struct bl_dedicated_registers_v0 {
	__le32	boot_loader_ready;		/* 0x880A3C driver will poll
						 * this Dword until BL will
						 * set it to 1 (initial value
						 * should be 0)
						 */
#define BL_READY (1)	/* ready indication */
	__le32	boot_loader_struct_version;	/* 0x880A40 BL struct ver. */
	__le32	rf_type;			/* 0x880A44 connected RF ID */
	__le32	baseband_type;			/* 0x880A48 board type ID */
	u8	mac_address[6];			/* 0x880A4c BL mac address */
} __packed;

/* bits for bl_shutdown_handshake */
#define BL_SHUTDOWN_HS_GRTD		BIT(0)
#define BL_SHUTDOWN_HS_RTD		BIT(1)
#define BL_SHUTDOWN_HS_PROT_VER(x) WIL_GET_BITS(x, 28, 31)

#endif /* BOOT_LOADER_EXPORT_H_ */
