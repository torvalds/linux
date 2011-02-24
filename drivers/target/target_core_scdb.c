/*******************************************************************************
 * Filename:  target_core_scdb.c
 *
 * This file contains the generic target engine Split CDB related functions.
 *
 * Copyright (c) 2004-2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/net.h>
#include <linux/string.h>
#include <scsi/scsi.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_transport.h>

#include "target_core_scdb.h"

/*	split_cdb_XX_6():
 *
 *      21-bit LBA w/ 8-bit SECTORS
 */
void split_cdb_XX_6(
	unsigned long long lba,
	u32 *sectors,
	unsigned char *cdb)
{
	cdb[1] = (lba >> 16) & 0x1f;
	cdb[2] = (lba >> 8) & 0xff;
	cdb[3] = lba & 0xff;
	cdb[4] = *sectors & 0xff;
}

/*	split_cdb_XX_10():
 *
 *	32-bit LBA w/ 16-bit SECTORS
 */
void split_cdb_XX_10(
	unsigned long long lba,
	u32 *sectors,
	unsigned char *cdb)
{
	put_unaligned_be32(lba, &cdb[2]);
	put_unaligned_be16(*sectors, &cdb[7]);
}

/*	split_cdb_XX_12():
 *
 *	32-bit LBA w/ 32-bit SECTORS
 */
void split_cdb_XX_12(
	unsigned long long lba,
	u32 *sectors,
	unsigned char *cdb)
{
	put_unaligned_be32(lba, &cdb[2]);
	put_unaligned_be32(*sectors, &cdb[6]);
}

/*	split_cdb_XX_16():
 *
 *	64-bit LBA w/ 32-bit SECTORS
 */
void split_cdb_XX_16(
	unsigned long long lba,
	u32 *sectors,
	unsigned char *cdb)
{
	put_unaligned_be64(lba, &cdb[2]);
	put_unaligned_be32(*sectors, &cdb[10]);
}

/*
 *	split_cdb_XX_32():
 *
 * 	64-bit LBA w/ 32-bit SECTORS such as READ_32, WRITE_32 and emulated XDWRITEREAD_32
 */
void split_cdb_XX_32(
	unsigned long long lba,
	u32 *sectors,
	unsigned char *cdb)
{
	put_unaligned_be64(lba, &cdb[12]);
	put_unaligned_be32(*sectors, &cdb[28]);
}
