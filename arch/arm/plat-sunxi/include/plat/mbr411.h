/*
 * drivers/block/sunxi_nand/nfd/mbr.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef    __MBR411_H__
#define    __MBR411_H__

#include <linux/kernel.h>

#define MAX_PART_COUNT		120			//max part count
#define MBR_COPY_NUM		4			//mbr backup count

#define MBR_START_ADDRESS	0x0			//mbr start address
#define MBR_SIZE		1024UL*16UL		//mbr size
#define MBR_PART_SIZE		sizeof(struct tag_PARTITION)
#define MBR_RESERVED		(MBR_SIZE - sizeof(struct tag_MBR) - \
					(MAX_PART_COUNT * MBR_PART_SIZE))
#define MBR_VERSION		0x200
#define MBR_MAGIC		"softw411"

struct nand_disk{
	unsigned long size;
	unsigned long offset;
	unsigned char type;
};

/* part info */
struct tag_PARTITION {
	__u32 addrhi;				//start address high 32 bit
	__u32 addrlo;				//start address low 32 bit
	__u32 lenhi;				//size high 32 bit
	__u32 lenlo;				//size low 32 bit
	__u8  classname[16];			//major device name
	__u8  name[16];				//minor device name
	__u32 user_type;			//标志当前盘符所属于的用户
	__u32 keydata;				//关键数据，要求量产不丢失
	__u32 ro;				//标志当前盘符的读写属性
	__u8  res[68];				//reserved
} __attribute__((packed));

/* mbr info */
struct tag_MBR {
	__u32		crc32;				// crc, from byte 4 to mbr tail
	__u32		version;			// version
	__u8		magic[8];			// magic number
	__u32		copy;				// mbr backup count
	__u32		index;				// current part no
	__u32		PartCount;			// part counter
	__u32		stamp[1];			// 对齐
} __attribute__((packed));

struct MBR {
	struct tag_MBR tag;
	struct tag_PARTITION	array[MAX_PART_COUNT];		// part info
	__u8		res[MBR_RESERVED];		// reserved space
} __attribute__((packed));

#endif    //__MBR411_H__
