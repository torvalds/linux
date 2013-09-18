/*
 * include/asm-xtensa/bootparam.h
 *
 * Definition of the Linux/Xtensa boot parameter structure
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005  Tensilica Inc.
 *
 * (Concept borrowed from the 68K port)
 */

#ifndef _XTENSA_BOOTPARAM_H
#define _XTENSA_BOOTPARAM_H

#define BP_VERSION 0x0001

#define BP_TAG_COMMAND_LINE	0x1001	/* command line (0-terminated string)*/
#define BP_TAG_INITRD		0x1002	/* ramdisk addr and size (bp_meminfo) */
#define BP_TAG_MEMORY		0x1003	/* memory addr and size (bp_meminfo) */
#define BP_TAG_SERIAL_BAUDRATE	0x1004	/* baud rate of current console. */
#define BP_TAG_SERIAL_PORT	0x1005	/* serial device of current console */
#define BP_TAG_FDT		0x1006	/* flat device tree addr */

#define BP_TAG_FIRST		0x7B0B  /* first tag with a version number */
#define BP_TAG_LAST 		0x7E0B	/* last tag */

#ifndef __ASSEMBLY__

/* All records are aligned to 4 bytes */

typedef struct bp_tag {
	unsigned short id;	/* tag id */
	unsigned short size;	/* size of this record excluding the structure*/
	unsigned long data[0];	/* data */
} bp_tag_t;

typedef struct meminfo {
	unsigned long type;
	unsigned long start;
	unsigned long end;
} meminfo_t;

#define SYSMEM_BANKS_MAX 5

#define MEMORY_TYPE_CONVENTIONAL	0x1000
#define MEMORY_TYPE_NONE		0x2000

typedef struct sysmem_info {
	int nr_banks;
	meminfo_t bank[SYSMEM_BANKS_MAX];
} sysmem_info_t;

extern sysmem_info_t sysmem;

#endif
#endif
