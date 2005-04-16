/*
 * Written by: Garry Forsgren, Unisys Corporation
 *             Natalie Protasevich, Unisys Corporation
 * This file contains the code to configure and interface 
 * with Unisys ES7000 series hardware system manager.
 *
 * Copyright (c) 2003 Unisys Corporation.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Unisys Corporation, Township Line & Union Meeting 
 * Roads-A, Unisys Way, Blue Bell, Pennsylvania, 19424, or:
 *
 * http://www.unisys.com
 */

#define	MIP_REG			1
#define	MIP_PSAI_REG		4

#define	MIP_BUSY		1
#define	MIP_SPIN		0xf0000
#define	MIP_VALID		0x0100000000000000ULL
#define	MIP_PORT(VALUE)	((VALUE >> 32) & 0xffff)

#define	MIP_RD_LO(VALUE)	(VALUE & 0xffffffff)   

struct mip_reg_info {
	unsigned long long mip_info;
	unsigned long long delivery_info;
	unsigned long long host_reg;
	unsigned long long mip_reg;
};

struct part_info {
	unsigned char type;   
	unsigned char length;
	unsigned char part_id;
	unsigned char apic_mode;
	unsigned long snum;    
	char ptype[16];
	char sname[64];
	char pname[64];
};

struct psai {
	unsigned long long entry_type;
	unsigned long long addr;
	unsigned long long bep_addr;
};

struct es7000_mem_info {
	unsigned char type;   
	unsigned char length;
	unsigned char resv[6];
	unsigned long long  start; 
	unsigned long long  size; 
};

struct es7000_oem_table {
	unsigned long long hdr;
	struct mip_reg_info mip;
	struct part_info pif;
	struct es7000_mem_info shm;
	struct psai psai;
};

struct acpi_table_sdt {
	unsigned long pa;
	unsigned long count;
	struct {
		unsigned long pa;
		enum acpi_table_id id;
		unsigned long size;
	}	entry[50];
};

struct oem_table {
	struct acpi_table_header Header;
	u32 OEMTableAddr;
	u32 OEMTableSize;
};

struct mip_reg {
	unsigned long long off_0;
	unsigned long long off_8;
	unsigned long long off_10;
	unsigned long long off_18;
	unsigned long long off_20;
	unsigned long long off_28;
	unsigned long long off_30;
	unsigned long long off_38;
};

#define	MIP_SW_APIC		0x1020b
#define	MIP_FUNC(VALUE) 	(VALUE & 0xff)

extern int parse_unisys_oem (char *oemptr, int oem_entries);
extern int find_unisys_acpi_oem_table(unsigned long *oem_addr, int *length);
extern int es7000_start_cpu(int cpu, unsigned long eip);
extern void es7000_sw_apic(void);
