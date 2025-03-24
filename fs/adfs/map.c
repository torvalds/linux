// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/map.c
 *
 *  Copyright (C) 1997-2002 Russell King
 */
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/unaligned.h>
#include "adfs.h"

/*
 * The ADFS map is basically a set of sectors.  Each sector is called a
 * zone which contains a bitstream made up of variable sized fragments.
 * Each bit refers to a set of bytes in the filesystem, defined by
 * log2bpmb.  This may be larger or smaller than the sector size, but
 * the overall size it describes will always be a round number of
 * sectors.  A fragment id is always idlen bits long.
 *
 *  < idlen > <       n        > <1>
 * +---------+-------//---------+---+
 * | frag id |  0000....000000  | 1 |
 * +---------+-------//---------+---+
 *
 * The physical disk space used by a fragment is taken from the start of
 * the fragment id up to and including the '1' bit - ie, idlen + n + 1
 * bits.
 *
 * A fragment id can be repeated multiple times in the whole map for
 * large or fragmented files.  The first map zone a fragment starts in
 * is given by fragment id / ids_per_zone - this allows objects to start
 * from any zone on the disk.
 *
 * Free space is described by a linked list of fragments.  Each free
 * fragment describes free space in the same way as the other fragments,
 * however, the frag id specifies an offset (in map bits) from the end
 * of this fragment to the start of the next free fragment.
 *
 * Objects stored on the disk are allocated object ids (we use these as
 * our inode numbers.)  Object ids contain a fragment id and an optional
 * offset.  This allows a directory fragment to contain small files
 * associated with that directory.
 */

/*
 * For the future...
 */
static DEFINE_RWLOCK(adfs_map_lock);

/*
 * This is fun.  We need to load up to 19 bits from the map at an
 * arbitrary bit alignment.  (We're limited to 19 bits by F+ version 2).
 */
#define GET_FRAG_ID(_map,_start,_idmask)				\
	({								\
		unsigned char *_m = _map + (_start >> 3);		\
		u32 _frag = get_unaligned_le32(_m);			\
		_frag >>= (_start & 7);					\
		_frag & _idmask;					\
	})

/*
 * return the map bit offset of the fragment frag_id in the zone dm.
 * Note that the loop is optimised for best asm code - look at the
 * output of:
 *  gcc -D__KERNEL__ -O2 -I../../include -o - -S map.c
 */
static int lookup_zone(const struct adfs_discmap *dm, const unsigned int idlen,
		       const u32 frag_id, unsigned int *offset)
{
	const unsigned int endbit = dm->dm_endbit;
	const u32 idmask = (1 << idlen) - 1;
	unsigned char *map = dm->dm_bh->b_data;
	unsigned int start = dm->dm_startbit;
	unsigned int freelink, fragend;
	u32 frag;

	frag = GET_FRAG_ID(map, 8, idmask & 0x7fff);
	freelink = frag ? 8 + frag : 0;

	do {
		frag = GET_FRAG_ID(map, start, idmask);

		fragend = find_next_bit_le(map, endbit, start + idlen);
		if (fragend >= endbit)
			goto error;

		if (start == freelink) {
			freelink += frag & 0x7fff;
		} else if (frag == frag_id) {
			unsigned int length = fragend + 1 - start;

			if (*offset < length)
				return start + *offset;
			*offset -= length;
		}

		start = fragend + 1;
	} while (start < endbit);
	return -1;

error:
	printk(KERN_ERR "adfs: oversized fragment 0x%x at 0x%x-0x%x\n",
		frag, start, fragend);
	return -1;
}

/*
 * Scan the free space map, for this zone, calculating the total
 * number of map bits in each free space fragment.
 *
 * Note: idmask is limited to 15 bits [3.2]
 */
static unsigned int
scan_free_map(struct adfs_sb_info *asb, struct adfs_discmap *dm)
{
	const unsigned int endbit = dm->dm_endbit;
	const unsigned int idlen  = asb->s_idlen;
	const unsigned int frag_idlen = idlen <= 15 ? idlen : 15;
	const u32 idmask = (1 << frag_idlen) - 1;
	unsigned char *map = dm->dm_bh->b_data;
	unsigned int start = 8, fragend;
	u32 frag;
	unsigned long total = 0;

	/*
	 * get fragment id
	 */
	frag = GET_FRAG_ID(map, start, idmask);

	/*
	 * If the freelink is null, then no free fragments
	 * exist in this zone.
	 */
	if (frag == 0)
		return 0;

	do {
		start += frag;

		frag = GET_FRAG_ID(map, start, idmask);

		fragend = find_next_bit_le(map, endbit, start + idlen);
		if (fragend >= endbit)
			goto error;

		total += fragend + 1 - start;
	} while (frag >= idlen + 1);

	if (frag != 0)
		printk(KERN_ERR "adfs: undersized free fragment\n");

	return total;
error:
	printk(KERN_ERR "adfs: oversized free fragment\n");
	return 0;
}

static int scan_map(struct adfs_sb_info *asb, unsigned int zone,
		    const u32 frag_id, unsigned int mapoff)
{
	const unsigned int idlen = asb->s_idlen;
	struct adfs_discmap *dm, *dm_end;
	int result;

	dm	= asb->s_map + zone;
	zone	= asb->s_map_size;
	dm_end	= asb->s_map + zone;

	do {
		result = lookup_zone(dm, idlen, frag_id, &mapoff);

		if (result != -1)
			goto found;

		dm ++;
		if (dm == dm_end)
			dm = asb->s_map;
	} while (--zone > 0);

	return -1;
found:
	result -= dm->dm_startbit;
	result += dm->dm_startblk;

	return result;
}

/*
 * calculate the amount of free blocks in the map.
 *
 *              n=1
 *  total_free = E(free_in_zone_n)
 *              nzones
 */
void adfs_map_statfs(struct super_block *sb, struct kstatfs *buf)
{
	struct adfs_sb_info *asb = ADFS_SB(sb);
	struct adfs_discrecord *dr = adfs_map_discrecord(asb->s_map);
	struct adfs_discmap *dm;
	unsigned int total = 0;
	unsigned int zone;

	dm   = asb->s_map;
	zone = asb->s_map_size;

	do {
		total += scan_free_map(asb, dm++);
	} while (--zone > 0);

	buf->f_blocks  = adfs_disc_size(dr) >> sb->s_blocksize_bits;
	buf->f_files   = asb->s_ids_per_zone * asb->s_map_size;
	buf->f_bavail  =
	buf->f_bfree   = signed_asl(total, asb->s_map2blk);
}

int adfs_map_lookup(struct super_block *sb, u32 frag_id, unsigned int offset)
{
	struct adfs_sb_info *asb = ADFS_SB(sb);
	unsigned int zone, mapoff;
	int result;

	/*
	 * map & root fragment is special - it starts in the center of the
	 * disk.  The other fragments start at zone (frag / ids_per_zone)
	 */
	if (frag_id == ADFS_ROOT_FRAG)
		zone = asb->s_map_size >> 1;
	else
		zone = frag_id / asb->s_ids_per_zone;

	if (zone >= asb->s_map_size)
		goto bad_fragment;

	/* Convert sector offset to map offset */
	mapoff = signed_asl(offset, -asb->s_map2blk);

	read_lock(&adfs_map_lock);
	result = scan_map(asb, zone, frag_id, mapoff);
	read_unlock(&adfs_map_lock);

	if (result > 0) {
		unsigned int secoff;

		/* Calculate sector offset into map block */
		secoff = offset - signed_asl(mapoff, asb->s_map2blk);
		return secoff + signed_asl(result, asb->s_map2blk);
	}

	adfs_error(sb, "fragment 0x%04x at offset %d not found in map",
		   frag_id, offset);
	return 0;

bad_fragment:
	adfs_error(sb, "invalid fragment 0x%04x (zone = %d, max = %d)",
		   frag_id, zone, asb->s_map_size);
	return 0;
}

static unsigned char adfs_calczonecheck(struct super_block *sb, unsigned char *map)
{
	unsigned int v0, v1, v2, v3;
	int i;

	v0 = v1 = v2 = v3 = 0;
	for (i = sb->s_blocksize - 4; i; i -= 4) {
		v0 += map[i]     + (v3 >> 8);
		v3 &= 0xff;
		v1 += map[i + 1] + (v0 >> 8);
		v0 &= 0xff;
		v2 += map[i + 2] + (v1 >> 8);
		v1 &= 0xff;
		v3 += map[i + 3] + (v2 >> 8);
		v2 &= 0xff;
	}
	v0 +=           v3 >> 8;
	v1 += map[1] + (v0 >> 8);
	v2 += map[2] + (v1 >> 8);
	v3 += map[3] + (v2 >> 8);

	return v0 ^ v1 ^ v2 ^ v3;
}

static int adfs_checkmap(struct super_block *sb, struct adfs_discmap *dm)
{
	unsigned char crosscheck = 0, zonecheck = 1;
	int i;

	for (i = 0; i < ADFS_SB(sb)->s_map_size; i++) {
		unsigned char *map;

		map = dm[i].dm_bh->b_data;

		if (adfs_calczonecheck(sb, map) != map[0]) {
			adfs_error(sb, "zone %d fails zonecheck", i);
			zonecheck = 0;
		}
		crosscheck ^= map[3];
	}
	if (crosscheck != 0xff)
		adfs_error(sb, "crosscheck != 0xff");
	return crosscheck == 0xff && zonecheck;
}

/*
 * Layout the map - the first zone contains a copy of the disc record,
 * and the last zone must be limited to the size of the filesystem.
 */
static void adfs_map_layout(struct adfs_discmap *dm, unsigned int nzones,
			    struct adfs_discrecord *dr)
{
	unsigned int zone, zone_size;
	u64 size;

	zone_size = (8 << dr->log2secsize) - le16_to_cpu(dr->zone_spare);

	dm[0].dm_bh       = NULL;
	dm[0].dm_startblk = 0;
	dm[0].dm_startbit = 32 + ADFS_DR_SIZE_BITS;
	dm[0].dm_endbit   = 32 + zone_size;

	for (zone = 1; zone < nzones; zone++) {
		dm[zone].dm_bh       = NULL;
		dm[zone].dm_startblk = zone * zone_size - ADFS_DR_SIZE_BITS;
		dm[zone].dm_startbit = 32;
		dm[zone].dm_endbit   = 32 + zone_size;
	}

	size = adfs_disc_size(dr) >> dr->log2bpmb;
	size -= (nzones - 1) * zone_size - ADFS_DR_SIZE_BITS;
	dm[nzones - 1].dm_endbit = 32 + size;
}

static int adfs_map_read(struct adfs_discmap *dm, struct super_block *sb,
			 unsigned int map_addr, unsigned int nzones)
{
	unsigned int zone;

	for (zone = 0; zone < nzones; zone++) {
		dm[zone].dm_bh = sb_bread(sb, map_addr + zone);
		if (!dm[zone].dm_bh)
			return -EIO;
	}

	return 0;
}

static void adfs_map_relse(struct adfs_discmap *dm, unsigned int nzones)
{
	unsigned int zone;

	for (zone = 0; zone < nzones; zone++)
		brelse(dm[zone].dm_bh);
}

struct adfs_discmap *adfs_read_map(struct super_block *sb, struct adfs_discrecord *dr)
{
	struct adfs_sb_info *asb = ADFS_SB(sb);
	struct adfs_discmap *dm;
	unsigned int map_addr, zone_size, nzones;
	int ret;

	nzones    = dr->nzones | dr->nzones_high << 8;
	zone_size = (8 << dr->log2secsize) - le16_to_cpu(dr->zone_spare);

	asb->s_idlen = dr->idlen;
	asb->s_map_size = nzones;
	asb->s_map2blk = dr->log2bpmb - dr->log2secsize;
	asb->s_log2sharesize = dr->log2sharesize;
	asb->s_ids_per_zone = zone_size / (asb->s_idlen + 1);

	map_addr = (nzones >> 1) * zone_size -
		     ((nzones > 1) ? ADFS_DR_SIZE_BITS : 0);
	map_addr = signed_asl(map_addr, asb->s_map2blk);

	dm = kmalloc_array(nzones, sizeof(*dm), GFP_KERNEL);
	if (dm == NULL) {
		adfs_error(sb, "not enough memory");
		return ERR_PTR(-ENOMEM);
	}

	adfs_map_layout(dm, nzones, dr);

	ret = adfs_map_read(dm, sb, map_addr, nzones);
	if (ret) {
		adfs_error(sb, "unable to read map");
		goto error_free;
	}

	if (adfs_checkmap(sb, dm))
		return dm;

	adfs_error(sb, "map corrupted");

error_free:
	adfs_map_relse(dm, nzones);
	kfree(dm);
	return ERR_PTR(-EIO);
}

void adfs_free_map(struct super_block *sb)
{
	struct adfs_sb_info *asb = ADFS_SB(sb);

	adfs_map_relse(asb->s_map, asb->s_map_size);
	kfree(asb->s_map);
}
