// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/partitions/amiga.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#define pr_fmt(fmt) fmt

#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/overflow.h>
#include <linux/affs_hardblocks.h>

#include "check.h"

/* magic offsets in partition DosEnvVec */
#define NR_HD	3
#define NR_SECT	5
#define LO_CYL	9
#define HI_CYL	10

static __inline__ u32
checksum_block(__be32 *m, int size)
{
	u32 sum = 0;

	while (size--)
		sum += be32_to_cpu(*m++);
	return sum;
}

int amiga_partition(struct parsed_partitions *state)
{
	Sector sect;
	unsigned char *data;
	struct RigidDiskBlock *rdb;
	struct PartitionBlock *pb;
	u64 start_sect, nr_sects;
	sector_t blk, end_sect;
	u32 cylblk;		/* rdb_CylBlocks = nr_heads*sect_per_track */
	u32 nr_hd, nr_sect, lo_cyl, hi_cyl;
	int part, res = 0;
	unsigned int blksize = 1;	/* Multiplier for disk block size */
	int slot = 1;

	for (blk = 0; ; blk++, put_dev_sector(sect)) {
		if (blk == RDB_ALLOCATION_LIMIT)
			goto rdb_done;
		data = read_part_sector(state, blk, &sect);
		if (!data) {
			pr_err("Dev %s: unable to read RDB block %llu\n",
			       state->disk->disk_name, blk);
			res = -1;
			goto rdb_done;
		}
		if (*(__be32 *)data != cpu_to_be32(IDNAME_RIGIDDISK))
			continue;

		rdb = (struct RigidDiskBlock *)data;
		if (checksum_block((__be32 *)data, be32_to_cpu(rdb->rdb_SummedLongs) & 0x7F) == 0)
			break;
		/* Try again with 0xdc..0xdf zeroed, Windows might have
		 * trashed it.
		 */
		*(__be32 *)(data+0xdc) = 0;
		if (checksum_block((__be32 *)data,
				be32_to_cpu(rdb->rdb_SummedLongs) & 0x7F)==0) {
			pr_err("Trashed word at 0xd0 in block %llu ignored in checksum calculation\n",
			       blk);
			break;
		}

		pr_err("Dev %s: RDB in block %llu has bad checksum\n",
		       state->disk->disk_name, blk);
	}

	/* blksize is blocks per 512 byte standard block */
	blksize = be32_to_cpu( rdb->rdb_BlockBytes ) / 512;

	{
		char tmp[7 + 10 + 1 + 1];

		/* Be more informative */
		snprintf(tmp, sizeof(tmp), " RDSK (%d)", blksize * 512);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
	}
	blk = be32_to_cpu(rdb->rdb_PartitionList);
	put_dev_sector(sect);
	for (part = 1; blk>0 && part<=16; part++, put_dev_sector(sect)) {
		/* Read in terms partition table understands */
		if (check_mul_overflow(blk, (sector_t) blksize, &blk)) {
			pr_err("Dev %s: overflow calculating partition block %llu! Skipping partitions %u and beyond\n",
				state->disk->disk_name, blk, part);
			break;
		}
		data = read_part_sector(state, blk, &sect);
		if (!data) {
			pr_err("Dev %s: unable to read partition block %llu\n",
			       state->disk->disk_name, blk);
			res = -1;
			goto rdb_done;
		}
		pb  = (struct PartitionBlock *)data;
		blk = be32_to_cpu(pb->pb_Next);
		if (pb->pb_ID != cpu_to_be32(IDNAME_PARTITION))
			continue;
		if (checksum_block((__be32 *)pb, be32_to_cpu(pb->pb_SummedLongs) & 0x7F) != 0 )
			continue;

		/* RDB gives us more than enough rope to hang ourselves with,
		 * many times over (2^128 bytes if all fields max out).
		 * Some careful checks are in order, so check for potential
		 * overflows.
		 * We are multiplying four 32 bit numbers to one sector_t!
		 */

		nr_hd   = be32_to_cpu(pb->pb_Environment[NR_HD]);
		nr_sect = be32_to_cpu(pb->pb_Environment[NR_SECT]);

		/* CylBlocks is total number of blocks per cylinder */
		if (check_mul_overflow(nr_hd, nr_sect, &cylblk)) {
			pr_err("Dev %s: heads*sects %u overflows u32, skipping partition!\n",
				state->disk->disk_name, cylblk);
			continue;
		}

		/* check for consistency with RDB defined CylBlocks */
		if (cylblk > be32_to_cpu(rdb->rdb_CylBlocks)) {
			pr_warn("Dev %s: cylblk %u > rdb_CylBlocks %u!\n",
				state->disk->disk_name, cylblk,
				be32_to_cpu(rdb->rdb_CylBlocks));
		}

		/* RDB allows for variable logical block size -
		 * normalize to 512 byte blocks and check result.
		 */

		if (check_mul_overflow(cylblk, blksize, &cylblk)) {
			pr_err("Dev %s: partition %u bytes per cyl. overflows u32, skipping partition!\n",
				state->disk->disk_name, part);
			continue;
		}

		/* Calculate partition start and end. Limit of 32 bit on cylblk
		 * guarantees no overflow occurs if LBD support is enabled.
		 */

		lo_cyl = be32_to_cpu(pb->pb_Environment[LO_CYL]);
		start_sect = ((u64) lo_cyl * cylblk);

		hi_cyl = be32_to_cpu(pb->pb_Environment[HI_CYL]);
		nr_sects = (((u64) hi_cyl - lo_cyl + 1) * cylblk);

		if (!nr_sects)
			continue;

		/* Warn user if partition end overflows u32 (AmigaDOS limit) */

		if ((start_sect + nr_sects) > UINT_MAX) {
			pr_warn("Dev %s: partition %u (%llu-%llu) needs 64 bit device support!\n",
				state->disk->disk_name, part,
				start_sect, start_sect + nr_sects);
		}

		if (check_add_overflow(start_sect, nr_sects, &end_sect)) {
			pr_err("Dev %s: partition %u (%llu-%llu) needs LBD device support, skipping partition!\n",
				state->disk->disk_name, part,
				start_sect, end_sect);
			continue;
		}

		/* Tell Kernel about it */

		put_partition(state,slot++,start_sect,nr_sects);
		{
			/* Be even more informative to aid mounting */
			char dostype[4];
			char tmp[42];

			__be32 *dt = (__be32 *)dostype;
			*dt = pb->pb_Environment[16];
			if (dostype[3] < ' ')
				snprintf(tmp, sizeof(tmp), " (%c%c%c^%c)",
					dostype[0], dostype[1],
					dostype[2], dostype[3] + '@' );
			else
				snprintf(tmp, sizeof(tmp), " (%c%c%c%c)",
					dostype[0], dostype[1],
					dostype[2], dostype[3]);
			strlcat(state->pp_buf, tmp, PAGE_SIZE);
			snprintf(tmp, sizeof(tmp), "(res %d spb %d)",
				be32_to_cpu(pb->pb_Environment[6]),
				be32_to_cpu(pb->pb_Environment[4]));
			strlcat(state->pp_buf, tmp, PAGE_SIZE);
		}
		res = 1;
	}
	strlcat(state->pp_buf, "\n", PAGE_SIZE);

rdb_done:
	return res;
}
