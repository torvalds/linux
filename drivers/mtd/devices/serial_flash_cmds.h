/*
 * Generic/SFDP Flash Commands and Device Capabilities
 *
 * Copyright (C) 2013 Lee Jones <lee.jones@lianro.org>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _MTD_SERIAL_FLASH_CMDS_H
#define _MTD_SERIAL_FLASH_CMDS_H

/* Generic Flash Commands/OPCODEs */
#define SPINOR_OP_RDSR2		0x35
#define SPINOR_OP_WRVCR		0x81
#define SPINOR_OP_RDVCR		0x85

/* JEDEC Standard - Serial Flash Discoverable Parmeters (SFDP) Commands */
#define SPINOR_OP_WRITE		0x02	/* PAGE PROGRAM */
#define SPINOR_OP_WRITE_1_1_2	0xa2	/* DUAL INPUT PROGRAM */
#define SPINOR_OP_WRITE_1_2_2	0xd2	/* DUAL INPUT EXT PROGRAM */
#define SPINOR_OP_WRITE_1_1_4	0x32	/* QUAD INPUT PROGRAM */
#define SPINOR_OP_WRITE_1_4_4	0x12	/* QUAD INPUT EXT PROGRAM */

/* Configuration flags */
#define FLASH_FLAG_SINGLE	0x000000ff
#define FLASH_FLAG_READ_WRITE	0x00000001
#define FLASH_FLAG_READ_FAST	0x00000002
#define FLASH_FLAG_SE_4K	0x00000004
#define FLASH_FLAG_SE_32K	0x00000008
#define FLASH_FLAG_CE		0x00000010
#define FLASH_FLAG_32BIT_ADDR	0x00000020
#define FLASH_FLAG_RESET	0x00000040
#define FLASH_FLAG_DYB_LOCKING	0x00000080

#define FLASH_FLAG_DUAL		0x0000ff00
#define FLASH_FLAG_READ_1_1_2	0x00000100
#define FLASH_FLAG_READ_1_2_2	0x00000200
#define FLASH_FLAG_READ_2_2_2	0x00000400
#define FLASH_FLAG_WRITE_1_1_2	0x00001000
#define FLASH_FLAG_WRITE_1_2_2	0x00002000
#define FLASH_FLAG_WRITE_2_2_2	0x00004000

#define FLASH_FLAG_QUAD		0x00ff0000
#define FLASH_FLAG_READ_1_1_4	0x00010000
#define FLASH_FLAG_READ_1_4_4	0x00020000
#define FLASH_FLAG_READ_4_4_4	0x00040000
#define FLASH_FLAG_WRITE_1_1_4	0x00100000
#define FLASH_FLAG_WRITE_1_4_4	0x00200000
#define FLASH_FLAG_WRITE_4_4_4	0x00400000

#endif /* _MTD_SERIAL_FLASH_CMDS_H */
